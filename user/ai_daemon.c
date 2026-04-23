#include "llmrun_runtime.h"
#include "stat.h"

#define AI_DAEMON_TOKEN_MAX 255
#define AI_DAEMON_PREDICT_MAX 32
#define AI_DAEMON_RESULT_MAX 255

/*
 * Bonus 实验脚手架：
 * 当前由于Kernel限制，只能单核进行推理，所以主要瓶颈是prefill，
 * 那么如何减少Prefill时间？我们这里的Bonus来解决这个问题，使用Prefix Cache！
 */
struct daemon_prefix_cache {
    int valid;                    // 缓存是否有效
    uint32 model_kind;            // 缓存对应模型
    int cached_prefix_len;        // 缓存前缀长度
    int token_capacity;           // token 缓存容量
    uint64 kv_cache_capacity;     // KV 缓存容量
    uint32 *cached_tokens;        // 前缀 token 缓冲
    float *cached_kcache;         // K 缓存快照
    float *cached_vcache;         // V 缓存快照
    uint32 hits;                  // 命中次数
    uint32 misses;                // 未命中次数
    uint32 restores;              // 恢复次数
    uint32 saves;                 // 保存次数
    uint32 bypasses;              // 跳过次数
};

struct daemon_runtime {
    struct llm_runtime rt;              // 推理运行时
    struct llm_workspace ws;            // 工作区
    float *kcache;                      // K cache
    float *vcache;                      // V cache
    float *linear_conv_cache;           // 卷积状态缓存
    float *linear_state_cache;          // 线性状态缓存
    uint64 kv_cache_elems;              // KV cache 元素数
    uint64 linear_conv_elems;           // 卷积缓存元素数
    uint64 linear_state_elems;          // 线性状态元素数
    uint32 model_kind;                  // 当前模型类型
    llm_token_forward_fn token_forward; // 单 token forward 函数
    struct daemon_prefix_cache prefix_cache; // 前缀缓存状态
};

static uint64 kv_cache_elems(const struct model_cfg *cfg) {
    return (uint64)cfg->n_layers * (uint64)cfg->runtime_seq_len * (uint64)cfg->n_kv_heads * (uint64)cfg->head_dim;
}

static uint64 qwen_linear_conv_elems(const struct model_cfg *cfg) {
    return (uint64)cfg->n_layers * (uint64)cfg_linear_conv_dim(cfg) * (uint64)cfg->linear_conv_kernel;
}

static uint64 qwen_linear_state_elems(const struct model_cfg *cfg) {
    return (uint64)cfg->n_layers * (uint64)cfg->linear_num_v_heads *
           (uint64)cfg->linear_key_head_dim * (uint64)cfg->linear_value_head_dim;
}

// 将 dr->rt.prompt[i] 依次拷贝到 dr->rt.seq[i]
static void copy_prompt_to_seq(struct daemon_runtime *dr) {
    for (int i = 0; i < dr->rt.prompt_n; i++) {
        dr->rt.seq[i] = dr->rt.prompt[i];
    }
}

static int prefix_cache_plan_copy(
    const struct model_cfg *cfg,
    int prefix_len,
    uint64 *layer_span_out,
    uint64 *prefix_elems_out
) {
    
    // prefix_len 下限约束
    if (prefix_len < 0) {
        prefix_len = 0;
    }
    // prefix_len 上限约束，不能超过 KV Cache 支持的最大序列长度
    if (prefix_len > cfg->runtime_seq_len) {
        prefix_len = cfg->runtime_seq_len;
    }

    // 计算单个位置的 KV 元素数，只依赖于全局模型配置
    uint64 elems_per_pos = (uint64)cfg->n_kv_heads * (uint64)cfg->head_dim;
    // 计算单层 cache 跨度（序列元素数×单个位置的 KV 元素数），只依赖于全局模型配置
    uint64 layer_span = (uint64)cfg->runtime_seq_len * elems_per_pos;
    // 计算整个 prefix 序列的 KV 元素总数
    uint64 prefix_elems = (uint64)prefix_len * elems_per_pos;

    // 返回 layer_span 和 prefix_elems
    if (layer_span_out != NULL) {
        *layer_span_out = layer_span;
    }
    if (prefix_elems_out != NULL) {
        *prefix_elems_out = prefix_elems;
    }
    return prefix_len;
}

// 把 src 中保存的 prefix slice 复制到 dst 中
static void __attribute__((unused))
prefix_cache_copy_prefix_slice(float *dst, const float *src, const struct model_cfg *cfg, int prefix_len) {
    uint64 layer_span = 0;
    uint64 prefix_elems = 0;

    if (dst == 0 || src == 0) {
        return;
    }
    if (prefix_cache_plan_copy(cfg, prefix_len, &layer_span, &prefix_elems) <= 0) {
        return;
    }

    for (int layer_count = 0; layer_count < cfg->n_layers; layer_count++) {
        float *dst_layer = dst + (uint64)layer_count * layer_span;              // 定位到复制目标中本层起点
        const float *src_layer = src + (uint64)layer_count * layer_span;        // 定位到复制源中本层起点
        memcpy(dst_layer, src_layer, (uint64)(prefix_elems * sizeof(float)));   // 复制前 prefix_elems 个元素，每个元素都是 float 类型
    }
}

// 基于配置文件 cfg，将 cache 中 start_pos 及其之后的元素清零
static void __attribute__((unused)) clear_kv_cache_from(float *cache, const struct model_cfg *cfg, int start_pos) {
    if (cache == 0) {
        return;
    }
    if (start_pos < 0) {
        start_pos = 0;
    }
    if (start_pos >= cfg->runtime_seq_len) {
        return;
    }

    uint64 elems_per_pos = (uint64)cfg->n_kv_heads * (uint64)cfg->head_dim;
    uint64 layer_span = (uint64)cfg->runtime_seq_len * elems_per_pos;
    uint64 tail_elems = (uint64)(cfg->runtime_seq_len - start_pos) * elems_per_pos;
    for (int l = 0; l < cfg->n_layers; l++) {
        float *tail = cache + (uint64)l * layer_span + (uint64)start_pos * elems_per_pos;
        memset(tail, 0, (uint)(tail_elems * sizeof(float)));
    }
}

static int prefix_cache_supported(const struct daemon_runtime *dr) {
    const struct daemon_prefix_cache *pc = &dr->prefix_cache;

    /*
     * 这个 bonus 实验有意只支持 Smol。
     * 非 Smol 模型继续走原始冷启动路径，避免学生同时处理多种推理状态布局。
     */
    return dr->model_kind == MODEL_KIND_SMOL &&
           pc->cached_tokens != 0 &&
           pc->cached_kcache != 0 &&
           pc->cached_vcache != 0;
}

static void prefix_cache_reset(struct daemon_prefix_cache *pc) {
    pc->valid = 0;
    pc->cached_prefix_len = 0;
    if (pc->cached_tokens != 0 && pc->token_capacity > 0) {
        memset(pc->cached_tokens, 0, (uint)((uint64)pc->token_capacity * sizeof(uint32)));
    }
    if (pc->cached_kcache != 0 && pc->kv_cache_capacity != 0) {
        memset(pc->cached_kcache, 0, (uint)(pc->kv_cache_capacity * sizeof(float)));
    }
    if (pc->cached_vcache != 0 && pc->kv_cache_capacity != 0) {
        memset(pc->cached_vcache, 0, (uint)(pc->kv_cache_capacity * sizeof(float)));
    }
}

static void prefix_cache_init(struct daemon_prefix_cache *pc, uint32 model_kind, int token_capacity, uint64 kv_cache_capacity) {
    memset(pc, 0, sizeof(*pc));
    pc->model_kind = model_kind;
    pc->token_capacity = token_capacity;
    pc->kv_cache_capacity = kv_cache_capacity;

    if (model_kind != MODEL_KIND_SMOL || token_capacity <= 0 || kv_cache_capacity == 0) {
        return;
    }

    pc->cached_tokens = (uint32 *)xmalloc((uint64)token_capacity * sizeof(uint32));
    pc->cached_kcache = llm_alloc_zeroed_floats(kv_cache_capacity);
    pc->cached_vcache = llm_alloc_zeroed_floats(kv_cache_capacity);
    prefix_cache_reset(pc);
}

static int prefix_cache_prefix_match_len(
    const struct daemon_prefix_cache *pc,
    uint32 model_kind,
    const uint32 *tokens,
    int token_count
) {
    if (!pc->valid || pc->model_kind != model_kind || pc->cached_prefix_len <= 0) {
        return 0;
    }

    int limit = pc->cached_prefix_len;
    if (limit > token_count) {
        limit = token_count;
    }
    for (int i = 0; i < limit; i++) {
        if (pc->cached_tokens[i] != tokens[i]) {
            return i;
        }
    }
    return limit;
}

static int prefix_cache_should_bypass(struct daemon_runtime *dr, int token_count) {
    if (!prefix_cache_supported(dr)) {
        dr->prefix_cache.bypasses++;
        return 1;
    }
    if (token_count <= 1) {
        dr->prefix_cache.bypasses++;
        return 1;
    }
    if (token_count > dr->prefix_cache.token_capacity) {
        dr->prefix_cache.bypasses++;
        return 1;
    }
    return 0;
}

static int prefix_cache_try_restore(struct daemon_runtime *dr, int token_count, int *resume_pos_out) {
    struct daemon_prefix_cache *pc = &dr->prefix_cache;
    // 当前 prompt 与缓存前缀连续相同的 token 数量
    int matched = prefix_cache_prefix_match_len(pc, dr->model_kind, dr->rt.prompt, token_count);
    // 在处理当前 prompt 时，可以复用的缓存长度
    // 初始置为 matched
    int reuse_len = matched;

    if (resume_pos_out != NULL) {
        // 缺省重置开始继续推理的位置为 0，如果缓存命中会重新设置该字段
        *resume_pos_out = 0;
    }
    if (reuse_len > token_count - 1) {
        // 缓存最多只保存到 prompt_n - 1，对于超出的 token 不再进行复用，以便运行一次 forward 产生第一个新词的 logits
        reuse_len = token_count - 1;
    }
    if (reuse_len <= 0) {
        // 没有可复用的前缀，记录一次缓存未命中
        pc->misses++;
        return 0;
    }

    copy_prompt_to_seq(dr);     // 把 prompt token 拷入运行时序列

    // 从 KV Cache 缓存中恢复上一次的 KV Cache 数据
    prefix_cache_copy_prefix_slice(dr->kcache, pc->cached_kcache, &dr->rt.cfg, reuse_len);
    prefix_cache_copy_prefix_slice(dr->vcache, pc->cached_vcache, &dr->rt.cfg, reuse_len);

    // 将 KV Cache 中 reuse_len 以后的内容（无法用于本次 Prefill 过程）清空
    clear_kv_cache_from(dr->kcache, &dr->rt.cfg, reuse_len);
    clear_kv_cache_from(dr->vcache, &dr->rt.cfg, reuse_len);

    if (resume_pos_out != NULL) {
        // 恢复 KV Cache 缓存后，接下来需要从 reuse_len 处开始继续推理
        *resume_pos_out = reuse_len;
    }
    pc->restores++;             // 记录一次前缀缓存成功恢复
    return reuse_len;
}

static void prefix_cache_save_after_prefill(struct daemon_runtime *dr) {
    struct llm_runtime *rt = &dr->rt;
    struct daemon_prefix_cache *pc = &dr->prefix_cache;
    int save_prefix_len = rt->prompt_n - 1;             // 约束保存范围：限于 prompt token（除最后一个 token 外）

    /*
     * 这个 skeleton 最多只复用到 prompt_n - 1。
     * 恢复后仍然会重新执行最后一个 prompt token，
     * 这样就不需要额外保存 logits，或者为了产出第一个生成 token
     * 再专门保存“最后一个 prompt token”的隐藏状态。
     */
    if (!prefix_cache_supported(dr) || save_prefix_len <= 0) {
        return;
    }
    if (save_prefix_len > pc->token_capacity) {
        prefix_cache_reset(pc);
        return;
    }

    memcpy(pc->cached_tokens, rt->prompt, (uint)((uint64)save_prefix_len * sizeof(uint32)));

    pc->valid = 1;                              // 标记 KV Cache 缓存有效
    pc->model_kind = dr->model_kind;            // 记录模型类型
    pc->cached_prefix_len = save_prefix_len;    // 缓存有效长度
    pc->saves++;                                // 记录一次 KV Cache 缓存动作

    // 将刚完成的 Prefill 过程中的 KV Cache 备份到 pc->cached_k/vcache 中
    prefix_cache_copy_prefix_slice(pc->cached_kcache, dr->kcache, &dr->rt.cfg, save_prefix_len);
    prefix_cache_copy_prefix_slice(pc->cached_vcache, dr->vcache, &dr->rt.cfg, save_prefix_len);

    /*
     * 背后的原因是：如果等完整冷启动 decode 结束后再保存，
     * 否则 daemon 会先拷贝生成阶段写入的 cache 位置，再把它们清零回退成可复用前缀。
     */
}

static void reset_runtime_state(struct daemon_runtime *dr) {
    copy_prompt_to_seq(dr);
    memset(dr->kcache, 0, (uint)(dr->kv_cache_elems * sizeof(float)));
    memset(dr->vcache, 0, (uint)(dr->kv_cache_elems * sizeof(float)));
    if (dr->linear_conv_cache != 0) {
        memset(dr->linear_conv_cache, 0, (uint)(dr->linear_conv_elems * sizeof(float)));
    }
    if (dr->linear_state_cache != 0) {
        memset(dr->linear_state_cache, 0, (uint)(dr->linear_state_elems * sizeof(float)));
    }
}

static int append_u32(char *out, int out_cap, int *pos, uint32 value) {
    char digits[16];
    int nd = 0;
    do {
        digits[nd++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value != 0 && nd < (int)sizeof(digits));

    if (*pos + nd >= out_cap) {
        return -1;
    }
    for (int i = nd - 1; i >= 0; i--) {
        out[*pos] = digits[i];
        (*pos)++;
    }
    return 0;
}

static int format_generated_tokens(uint32 *seq, int prompt_n, int gen_n, char *out, int out_cap) {
    int pos = 0;
    for (int i = 0; i < gen_n; i++) {
        if (i > 0) {
            if (pos + 1 >= out_cap) {
                return -1;
            }
            out[pos++] = ' ';
        }
        if (append_u32(out, out_cap, &pos, seq[prompt_n + i]) < 0) {
            return -1;
        }
    }
    if (pos >= out_cap) {
        return -1;
    }
    out[pos] = '\0';
    return pos;
}

static int select_model(char *asset_dir, int asset_dir_cap, uint32 *model_kind_out) {
    struct stat st;
    int have_smol = stat("/AI/SMOL/CFG.BIN", &st) == 0;
    int have_qwen = stat("/AI/QWEN/CFG.BIN", &st) == 0;

    if (have_smol && have_qwen) {
        LLM_ERR("multiple model directories present; expected exactly one active model\n");
        return -1;
    }
    if (!have_smol && !have_qwen) {
        LLM_ERR("no model assets found under /AI\n");
        return -1;
    }

    if (have_smol) {
        if ((int)strlen("/AI/SMOL") + 1 > asset_dir_cap) {
            return -1;
        }
        strcpy(asset_dir, "/AI/SMOL");
        *model_kind_out = MODEL_KIND_SMOL;
    } else {
        if ((int)strlen("/AI/QWEN") + 1 > asset_dir_cap) {
            return -1;
        }
        strcpy(asset_dir, "/AI/QWEN");
        *model_kind_out = MODEL_KIND_QWEN;
    }
    return 0;
}

static int run_decode_steps(struct daemon_runtime *dr, int start_pos, int total_steps) {
    struct llm_runtime *rt = &dr->rt;
    for (int pos = start_pos; pos < total_steps; pos++) {
        int next = dr->token_forward(
            rt,
            dr->kcache,
            dr->vcache,
            dr->linear_conv_cache,
            dr->linear_state_cache,
            pos,
            &dr->ws
        );
        if (next < 0) {
            return -1;
        }
        if (pos >= rt->prompt_n - 1) {
            int gen_idx = pos - (rt->prompt_n - 1);
            rt->seq[rt->prompt_n + gen_idx] = (uint32)next;
        }
    }
    return 0;
}

static int run_decode_cold(struct daemon_runtime *dr, char *out, int out_cap) {
    struct llm_runtime *rt = &dr->rt;
    int total_steps = rt->prompt_n + rt->expect_n - 1;

    reset_runtime_state(dr);
    if (run_decode_steps(dr, 0, total_steps) < 0) {
        return -1;
    }
    return format_generated_tokens(rt->seq, rt->prompt_n, rt->expect_n, out, out_cap);
}

static int run_decode(struct daemon_runtime *dr, char *out, int out_cap) {
    struct llm_runtime *rt = &dr->rt;
    int total_steps = rt->prompt_n + rt->expect_n - 1;
    int resume_pos = 0;

    if (prefix_cache_should_bypass(dr, rt->prompt_n)) {
        return run_decode_cold(dr, out, out_cap);
    }

    if (prefix_cache_try_restore(dr, rt->prompt_n, &resume_pos) > 0) {
        if (run_decode_steps(dr, resume_pos, total_steps) < 0) {
            return -1;
        }
        return format_generated_tokens(rt->seq, rt->prompt_n, rt->expect_n, out, out_cap);
    }

    int n = run_decode_cold(dr, out, out_cap);
    if (n >= 0) {
        prefix_cache_save_after_prefill(dr);
    }
    return n;
}

static int validate_request_tokens(const struct daemon_runtime *dr, const uint32 *tokens, int token_count, int predict_count) {
    const struct llm_runtime *rt = &dr->rt;
    if (token_count <= 0 || token_count > AI_DAEMON_TOKEN_MAX) {
        return -1;
    }
    if (predict_count <= 0 || predict_count > AI_DAEMON_PREDICT_MAX) {
        return -1;
    }
    if (token_count + predict_count > rt->cfg.runtime_seq_len) {
        LLM_ERR("request too long for runtime_seq_len\n");
        return -1;
    }
    for (int i = 0; i < token_count; i++) {
        if (tokens[i] >= (uint32)rt->cfg.vocab_size) {
            LLM_ERR("token %d out of vocabulary range\n", i);
            return -1;
        }
    }
    return 0;
}

static int smol_token_forward(
    const struct llm_runtime *rt,
    float *kcache,
    float *vcache,
    float *linear_conv_cache,
    float *linear_state_cache,
    int pos,
    struct llm_workspace *ws
) {
    (void)linear_conv_cache;
    (void)linear_state_cache;

    const struct model_cfg *cfg = &rt->cfg;
    uint32 token = rt->seq[pos];
    const int8 *erow = rt->emb.q + (uint64)token * (uint64)rt->emb.cols;

    for (int i = 0; i < cfg->dim; i++) {
        ws->hidden[i] = (float)erow[i] * rt->emb.scales[token];
    }

    for (int l = 0; l < cfg->n_layers; l++) {
        llm_apply_full_layer(rt, kcache, vcache, l, pos, ws);
        llm_apply_ffn(cfg, &rt->layers[l], ws);
    }

    rmsnorm(ws->norm_hidden, ws->hidden, rt->final_norm, cfg->dim, cfg->rms_eps);
    return argmax_embed(rt->emb, ws->norm_hidden);
}

static float *linear_conv_state_at(float *cache, const struct model_cfg *cfg, int layer, int channel) {
    uint64 idx = ((uint64)layer * (uint64)cfg_linear_conv_dim(cfg) + (uint64)channel) * (uint64)cfg->linear_conv_kernel;
    return cache + idx;
}

static float *linear_state_at(float *cache, const struct model_cfg *cfg, int layer, int head) {
    uint64 idx = ((uint64)layer * (uint64)cfg->linear_num_v_heads + (uint64)head) *
                 (uint64)cfg->linear_key_head_dim * (uint64)cfg->linear_value_head_dim;
    return cache + idx;
}

static void apply_linear_layer(
    const struct llm_runtime *rt,
    float *linear_conv_cache,
    float *linear_state_cache,
    int layer_idx,
    struct llm_workspace *ws
) {
    const struct model_cfg *cfg = &rt->cfg;
    struct layer *ly = &rt->layers[layer_idx];
    int conv_dim = cfg_linear_conv_dim(cfg);
    int value_dim = cfg_linear_value_dim(cfg);
    int v_heads = cfg->linear_num_v_heads;
    int k_heads = cfg->linear_num_k_heads;
    int key_head_dim = cfg->linear_key_head_dim;
    int value_head_dim = cfg->linear_value_head_dim;
    int kernel = cfg->linear_conv_kernel;
    int head_rep = v_heads / k_heads;
    float *delta = ws->attn_gate;

    rmsnorm(ws->norm_hidden, ws->hidden, ly->input_norm, cfg->dim, cfg->rms_eps);
    qmatmul(ws->qkv_mixed, ly->u.linear.qkv_proj, ws->norm_hidden);
    for (int c = 0; c < conv_dim; c++) {
        float *state = linear_conv_state_at(linear_conv_cache, cfg, layer_idx, c);
        float acc = 0.0f;
        float *w = ly->u.linear.conv_weight + (uint64)c * (uint64)kernel;

        for (int j = 0; j < kernel - 1; j++) {
            state[j] = state[j + 1];
        }
        state[kernel - 1] = ws->qkv_mixed[c];
        for (int j = 0; j < kernel; j++) {
            acc += w[j] * state[j];
        }
        ws->qkv_mixed[c] = silu_approx(acc);
    }

    memcpy(ws->query, ws->qkv_mixed, (uint)cfg_linear_key_dim(cfg) * sizeof(float));
    memcpy(ws->key, ws->qkv_mixed + cfg_linear_key_dim(cfg), (uint)cfg_linear_key_dim(cfg) * sizeof(float));
    memcpy(ws->value, ws->qkv_mixed + cfg_linear_key_dim(cfg) * 2, (uint)value_dim * sizeof(float));

    qmatmul(ws->linear_gate, ly->u.linear.z_proj, ws->norm_hidden);
    qmatmul(ws->linear_a, ly->u.linear.a_proj, ws->norm_hidden);
    qmatmul(ws->linear_b, ly->u.linear.b_proj, ws->norm_hidden);
    memset(ws->attn_out, 0, (uint)((uint64)value_dim * sizeof(float)));

    float scale = 1.0f / fsqrt1((float)key_head_dim);
    for (int h = 0; h < v_heads; h++) {
        int src_head = h / head_rep;
        float *qh = ws->query + (uint64)src_head * (uint64)key_head_dim;
        float *kh = ws->key + (uint64)src_head * (uint64)key_head_dim;
        float *vh = ws->value + (uint64)h * (uint64)value_head_dim;
        float *zh = ws->linear_gate + (uint64)h * (uint64)value_head_dim;
        float *out_head = ws->attn_out + (uint64)h * (uint64)value_head_dim;
        float *state = linear_state_at(linear_state_cache, cfg, layer_idx, h);

        memcpy(ws->ffn_gate, qh, (uint)key_head_dim * sizeof(float));
        memcpy(ws->ffn_up, kh, (uint)key_head_dim * sizeof(float));
        l2norm_inplace(ws->ffn_gate, key_head_dim, LINEAR_NORM_EPS);
        l2norm_inplace(ws->ffn_up, key_head_dim, LINEAR_NORM_EPS);
        for (int i = 0; i < key_head_dim; i++) {
            ws->ffn_gate[i] *= scale;
        }

        float beta = sigmoid_approx(ws->linear_b[h]);
        float g = -exp_approx(ly->u.linear.a_log[h]) * softplus_approx(ws->linear_a[h] + ly->u.linear.dt_bias[h]);
        float gexp = exp_approx(g);
        for (int i = 0; i < key_head_dim * value_head_dim; i++) {
            state[i] *= gexp;
        }

        for (int v = 0; v < value_head_dim; v++) {
            float mem = 0.0f;
            for (int i = 0; i < key_head_dim; i++) {
                mem += state[(uint64)i * (uint64)value_head_dim + (uint64)v] * ws->ffn_up[i];
            }
            delta[v] = (vh[v] - mem) * beta;
        }

        for (int i = 0; i < key_head_dim; i++) {
            float *row = state + (uint64)i * (uint64)value_head_dim;
            float kval = ws->ffn_up[i];
            float qval = ws->ffn_gate[i];
            for (int v = 0; v < value_head_dim; v++) {
                row[v] += kval * delta[v];
                out_head[v] += row[v] * qval;
            }
        }

        rmsnorm(out_head, out_head, ly->u.linear.norm, value_head_dim, cfg->rms_eps);
        for (int v = 0; v < value_head_dim; v++) {
            out_head[v] *= silu_approx(zh[v]);
        }
    }

    qmatmul(ws->proj_out, ly->u.linear.out_proj, ws->attn_out);
    for (int i = 0; i < cfg->dim; i++) {
        ws->hidden[i] += ws->proj_out[i];
    }
}

static void qwen_alloc_linear_caches(const struct model_cfg *cfg, float **linear_conv_cache, float **linear_state_cache) {
    *linear_conv_cache = llm_alloc_zeroed_floats(qwen_linear_conv_elems(cfg));
    *linear_state_cache = llm_alloc_zeroed_floats(qwen_linear_state_elems(cfg));
}

static void qwen_workspace_init(const struct model_cfg *cfg, struct llm_workspace *ws) {
    int mixed_qkv_cap = max_int(cfg_full_q_rows(cfg), cfg_linear_conv_dim(cfg));
    int query_cap = max_int(cfg_attn_out_dim(cfg), cfg_linear_key_dim(cfg));
    int gate_cap = max_int(cfg_attn_out_dim(cfg), cfg_linear_value_dim(cfg));
    int key_cap = max_int(cfg_kv_dim(cfg), cfg_linear_key_dim(cfg));
    int value_cap = max_int(cfg_kv_dim(cfg), cfg_linear_value_dim(cfg));
    int attention_out_cap = max_int(cfg_attn_out_dim(cfg), cfg_linear_value_dim(cfg));

    memset(ws, 0, sizeof(*ws));
    ws->hidden = llm_alloc_floats((uint64)cfg->dim);
    ws->norm_hidden = llm_alloc_floats((uint64)cfg->dim);
    ws->proj_out = llm_alloc_floats((uint64)cfg->dim);
    ws->qkv_mixed = llm_alloc_floats((uint64)mixed_qkv_cap);
    ws->query = llm_alloc_floats((uint64)query_cap);
    ws->attn_gate = llm_alloc_floats((uint64)gate_cap);
    ws->key = llm_alloc_floats((uint64)key_cap);
    ws->value = llm_alloc_floats((uint64)value_cap);
    ws->linear_gate = llm_alloc_floats((uint64)cfg_linear_value_dim(cfg));
    ws->linear_a = llm_alloc_floats((uint64)cfg->linear_num_v_heads);
    ws->linear_b = llm_alloc_floats((uint64)cfg->linear_num_v_heads);
    ws->attn_out = llm_alloc_floats((uint64)attention_out_cap);
    ws->ffn_gate = llm_alloc_floats((uint64)cfg->hidden_dim);
    ws->ffn_up = llm_alloc_floats((uint64)cfg->hidden_dim);
    ws->scores = llm_alloc_floats((uint64)cfg->runtime_seq_len);
}

static int qwen_token_forward(
    const struct llm_runtime *rt,
    float *kcache,
    float *vcache,
    float *linear_conv_cache,
    float *linear_state_cache,
    int pos,
    struct llm_workspace *ws
) {
    const struct model_cfg *cfg = &rt->cfg;
    uint32 token = rt->seq[pos];
    const int8 *erow = rt->emb.q + (uint64)token * (uint64)rt->emb.cols;

    for (int i = 0; i < cfg->dim; i++) {
        ws->hidden[i] = (float)erow[i] * rt->emb.scales[token];
    }

    for (int l = 0; l < cfg->n_layers; l++) {
        if (rt->layer_kinds[l] == LAYER_KIND_FULL) {
            llm_apply_full_layer(rt, kcache, vcache, l, pos, ws);
        } else if (rt->layer_kinds[l] == LAYER_KIND_LINEAR) {
            apply_linear_layer(rt, linear_conv_cache, linear_state_cache, l, ws);
        } else {
            return -1;
        }
        llm_apply_ffn(cfg, &rt->layers[l], ws);
    }

    rmsnorm(ws->norm_hidden, ws->hidden, rt->final_norm, cfg->dim, cfg->rms_eps);
    return argmax_embed(rt->emb, ws->norm_hidden);
}

static int daemon_runtime_init(struct daemon_runtime *dr) {
    char asset_dir[16];

    memset(dr, 0, sizeof(*dr));
    if (select_model(asset_dir, sizeof(asset_dir), &dr->model_kind) < 0) {
        return -1;
    }
    if (chdir(asset_dir) < 0) {
        LLM_ERR("failed to chdir to %s\n", asset_dir);
        return -1;
    }

    if (dr->model_kind == MODEL_KIND_SMOL) {
        llm_runtime_init_model_only(&dr->rt, MODEL_KIND_SMOL, 0);
        dr->kv_cache_elems = kv_cache_elems(&dr->rt.cfg);
        llm_alloc_kv_caches(&dr->rt.cfg, &dr->kcache, &dr->vcache);
        llm_workspace_init_full(&dr->rt.cfg, &dr->ws);
        dr->token_forward = smol_token_forward;
    } else {
        llm_runtime_init_model_only(&dr->rt, MODEL_KIND_QWEN, 1);
        dr->kv_cache_elems = kv_cache_elems(&dr->rt.cfg);
        dr->linear_conv_elems = qwen_linear_conv_elems(&dr->rt.cfg);
        dr->linear_state_elems = qwen_linear_state_elems(&dr->rt.cfg);
        llm_alloc_kv_caches(&dr->rt.cfg, &dr->kcache, &dr->vcache);
        qwen_alloc_linear_caches(&dr->rt.cfg, &dr->linear_conv_cache, &dr->linear_state_cache);
        qwen_workspace_init(&dr->rt.cfg, &dr->ws);
        dr->token_forward = qwen_token_forward;
    }
    prefix_cache_init(&dr->prefix_cache, dr->model_kind, dr->rt.cfg.runtime_seq_len, dr->kv_cache_elems);

    LLM_LOG("model loaded and cached in memory\n");
    return 0;
}

static int handle_request(struct daemon_runtime *dr, const uint32 *tokens, int token_count, int predict_count, char *out, int out_cap) {
    if (validate_request_tokens(dr, tokens, token_count, predict_count) < 0) {
        return -1;
    }
    if (llm_runtime_set_request(&dr->rt, (uint32 *)tokens, token_count, 0, predict_count) < 0) {
        return -1;
    }
    return run_decode(dr, out, out_cap);
}

static int parse_ready_fd(int argc, char *argv[]) {
    for (int i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], "--ready-fd") == 0) {
            return atoi(argv[i + 1]);
        }
    }
    return -1;
}

static void notify_ready_fd(int ready_fd) {
    if (ready_fd < 0) {
        return;
    }
    char ok = '1';
    (void)write(ready_fd, &ok, 1);
    close(ready_fd);
}

int main(int argc, char *argv[]) {
    llm_set_prog_name("ai_daemon");
    int ready_fd = parse_ready_fd(argc, argv);

    struct daemon_runtime dr;
    if (daemon_runtime_init(&dr) < 0) {
        if (ready_fd >= 0) {
            close(ready_fd);
        }
        exit(1);
    }

    if (ai_worker_register() < 0) {
        LLM_ERR("ai_worker_register failed\n");
        if (ready_fd >= 0) {
            close(ready_fd);
        }
        exit(1);
    }
    notify_ready_fd(ready_fd);

    for (;;) {
        uint32 tokens[AI_DAEMON_TOKEN_MAX];
        char result[AI_DAEMON_RESULT_MAX + 1];
        int reqid = 0;
        int predict_count = 0;
        memset(tokens, 0, sizeof(tokens));
        memset(result, 0, sizeof(result));

        int token_count = ai_worker_get(tokens, AI_DAEMON_TOKEN_MAX, &reqid, &predict_count);
        if (token_count < 0) {
            LLM_ERR("ai_worker_get failed\n");
            exit(1);
        }

        int result_len = handle_request(&dr, tokens, token_count, predict_count, result, sizeof(result));
        int status = result_len < 0 ? -1 : 0;
        if (ai_worker_complete(reqid, status == 0 ? result : 0, status == 0 ? result_len : 0, status) < 0) {
            LLM_ERR("ai_worker_complete failed\n");
            exit(1);
        }
    }
}
