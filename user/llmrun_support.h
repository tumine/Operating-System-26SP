#ifndef __LLMRUN_SUPPORT_H__
#define __LLMRUN_SUPPORT_H__

#include "user.h"
#include "stat.h"
#include "fs.h"

#define LLM_CFG_MAGIC 0x31474643U
#define LLM_CFG_VERSION_V1 1U
#define LLM_CFG_VERSION_V2 2U

#define MODEL_KIND_SMOL 0U
#define MODEL_KIND_QWEN 1U

#define LAYER_KIND_FULL 0U
#define LAYER_KIND_LINEAR 1U

#define CFG_FLAG_ATTN_GATE 0x1U
#define CFG_FLAG_QK_NORM 0x2U

#define LEGACY_DIM 576
#define LEGACY_HIDDEN_DIM 1536
#define LEGACY_N_LAYERS 30
#define LEGACY_N_HEADS 9
#define LEGACY_N_KV_HEADS 3
#define LEGACY_HEAD_DIM 64
#define LEGACY_VOCAB_SIZE 49152
#define LEGACY_SEQ_LEN 2048
#define LEGACY_RUNTIME_SEQ_LEN 256
#define LEGACY_RMS_EPS 1e-5f
#define LEGACY_ROPE_THETA 100000.0f

#define LINEAR_NORM_EPS 1e-6f

#define LLM_REQUEST_TOKEN_MAX 255
#define LLM_REQUEST_LINE_MAX 4096
#define LLM_RESULT_TEXT_MAX 4096

struct model_cfg {
    uint32 model_kind;
    uint32 flags;
    int dim;
    int hidden_dim;
    int n_layers;
    int n_heads;
    int n_kv_heads;
    int head_dim;
    int vocab_size;
    int seq_len;
    int runtime_seq_len;
    float rms_eps;
    float rope_theta;
    int rope_rotary_dim;
    int linear_num_k_heads;
    int linear_num_v_heads;
    int linear_key_head_dim;
    int linear_value_head_dim;
    int linear_conv_kernel;
};

struct model_cfg_disk_v1 {
    uint32 magic;
    uint32 version;
    uint32 dim;
    uint32 hidden_dim;
    uint32 n_layers;
    uint32 n_heads;
    uint32 n_kv_heads;
    uint32 vocab_size;
    uint32 seq_len;
    uint32 runtime_seq_len;
    float rms_eps;
    float rope_theta;
} __attribute__((packed));

struct model_cfg_disk_v2 {
    uint32 magic;
    uint32 version;
    uint32 model_kind;
    uint32 flags;
    uint32 dim;
    uint32 hidden_dim;
    uint32 n_layers;
    uint32 n_heads;
    uint32 n_kv_heads;
    uint32 head_dim;
    uint32 vocab_size;
    uint32 seq_len;
    uint32 runtime_seq_len;
    float rms_eps;
    float rope_theta;
    uint32 rope_rotary_dim;
    uint32 linear_num_k_heads;
    uint32 linear_num_v_heads;
    uint32 linear_key_head_dim;
    uint32 linear_value_head_dim;
    uint32 linear_conv_kernel;
} __attribute__((packed));

struct qmat {
    int rows;
    int cols;
    float *scales;
    int8 *q;
};

struct full_layer {
    float *q_norm;
    float *k_norm;
    struct qmat q_proj;
    struct qmat k_proj;
    struct qmat v_proj;
    struct qmat o_proj;
};

struct linear_layer {
    float *conv_weight;
    float *dt_bias;
    float *a_log;
    float *norm;
    struct qmat qkv_proj;
    struct qmat z_proj;
    struct qmat a_proj;
    struct qmat b_proj;
    struct qmat out_proj;
};

struct layer {
    uint32 kind;
    float *input_norm;
    float *post_norm;
    struct qmat gate_proj;
    struct qmat up_proj;
    struct qmat down_proj;
    union {
        struct full_layer full;
        struct linear_layer linear;
    } u;
};

struct llm_workspace {
    float *hidden;
    float *norm_hidden;
    float *proj_out;
    float *qkv_mixed;
    float *query;
    float *attn_gate;
    float *key;
    float *value;
    float *linear_gate;
    float *linear_a;
    float *linear_b;
    float *attn_out;
    float *ffn_gate;
    float *ffn_up;
    float *scores;
};

struct llm_request {
    uint32 tokens[LLM_REQUEST_TOKEN_MAX];
    int token_count;
    int predict_count;
};

struct llm_runtime {
    struct model_cfg cfg;
    struct qmat emb;
    uint32 *layer_kinds;
    float *final_norm;
    struct layer *layers;
    float *rope;
    uint32 *seq;
};

struct llm_session {
    struct llm_runtime rt;
    struct llm_workspace ws;
    float *kcache;
    float *vcache;
    float *linear_conv_cache;
    float *linear_state_cache;
    uint64 kv_cache_elems;
    uint64 linear_conv_elems;
    uint64 linear_state_elems;
};

const char *llm_prog_name(void);
void llm_set_prog_name(const char *name);
void llm_set_prog_name_from_argv(const char *argv0, const char *fallback);

#define LLM_LOG(...)                     \
    do {                                \
        fprintf(2, "[%s] ", llm_prog_name()); \
        fprintf(2, __VA_ARGS__);        \
    } while (0)

#define LLM_ERR(...)                    \
    do {                                \
        fprintf(2, "%s: ", llm_prog_name()); \
        fprintf(2, __VA_ARGS__);        \
    } while (0)

int max_int(int a, int b);

int cfg_has_attn_gate(const struct model_cfg *cfg);
int cfg_has_qk_norm(const struct model_cfg *cfg);
int cfg_attn_out_dim(const struct model_cfg *cfg);
int cfg_full_q_rows(const struct model_cfg *cfg);
int cfg_kv_dim(const struct model_cfg *cfg);
int cfg_kv_mul(const struct model_cfg *cfg);
int cfg_linear_key_dim(const struct model_cfg *cfg);
int cfg_linear_value_dim(const struct model_cfg *cfg);
int cfg_linear_conv_dim(const struct model_cfg *cfg);

void *xmalloc(uint64 n);
int read_full(int fd, void *buf, int n);
char *load_file(const char *path, uint64 *size_out);
void load_model_cfg(struct model_cfg *cfg);
uint32 *load_layer_kinds(const struct model_cfg *cfg);

float fsqrt1(float x);
float exp_approx(float x);
float softplus_approx(float x);
float sigmoid_approx(float x);
float silu_approx(float x);
void softmax_inplace(float *x, int n);
void rmsnorm(float *out, const float *x, const float *weight, int n, float eps);
void l2norm_inplace(float *x, int n, float eps);
void qmatmul(float *out, struct qmat m, const float *x);
void apply_rope_partial(float *vec, const float *cosv, const float *sinv, int rotary_dim);

void parse_layer_blob(struct layer *ly, char *blob, uint64 size, const struct model_cfg *cfg, uint32 kind);
void make_layer_path(int layer, char *out, int out_sz);
int argmax_embed(struct qmat emb, const float *x);

int llm_parse_u32_token(const char *text, uint32 *value_out);
int llm_parse_request_line(char *line, struct llm_request *req);
int llm_format_generated_tokens(const uint32 *seq, int prompt_n, int gen_n, char *out, int out_cap);
int llm_load_prompt_file(const char *path, struct llm_request *req, int predict_count);
char *llm_trim_line(char *line);
int llm_is_exit_command(const char *line);
uint64 llm_kv_cache_elems(const struct model_cfg *cfg);
uint64 llm_qwen_linear_conv_elems(const struct model_cfg *cfg);
uint64 llm_qwen_linear_state_elems(const struct model_cfg *cfg);

typedef int (*llm_token_forward_fn)(
    const struct llm_runtime *rt,
    float *kcache,
    float *vcache,
    float *linear_conv_cache,
    float *linear_state_cache,
    int pos,
    struct llm_workspace *ws
);

#ifndef LLMRUN_SUPPORT_NO_RUNTIME_IMPL

static const char *llm_model_name(uint32 kind) {
    if (kind == MODEL_KIND_SMOL) {
        return "SmolLM";
    }
    if (kind == MODEL_KIND_QWEN) {
        return "Qwen3.5";
    }
    return "unknown-model";
}

static float *llm_alloc_floats(uint64 count) {
    if (count == 0) {
        return 0;
    }
    return (float *)xmalloc(count * sizeof(float));
}

static float *llm_alloc_zeroed_floats(uint64 count) {
    float *buf = llm_alloc_floats(count);
    if (buf != 0) {
        memset(buf, 0, (uint)(count * sizeof(float)));
    }
    return buf;
}

static float *llm_kcache_at(float *cache, const struct model_cfg *cfg, int layer, int pos, int kv_head) {
    uint64 idx = (((uint64)layer * (uint64)cfg->runtime_seq_len + (uint64)pos) * (uint64)cfg->n_kv_heads + (uint64)kv_head) *
                 (uint64)cfg->head_dim;
    return cache + idx;
}

static void llm_runtime_init(struct llm_runtime *rt, uint32 required_model_kind, int allow_linear) {
    memset(rt, 0, sizeof(*rt));

    LLM_LOG("loading LLM assets\n");
    // Hint for students:
    // - If you want to measure model-asset load time only, a natural start
    //   timestamp is right before load_model_cfg() / the file loads below.
    // - A natural end timestamp is near the end of this function, after the
    //   runtime has consumed CFG/EMB/NRM/Lxx/ROP and rt is ready to use.
    load_model_cfg(&rt->cfg);
    if (rt->cfg.model_kind != required_model_kind) {
        LLM_ERR("expected %s assets, got %s\n", llm_model_name(required_model_kind), llm_model_name(rt->cfg.model_kind));
        exit(1);
    }
    LLM_LOG("cfg kind=%d dim=%d hidden=%d layers=%d heads=%d kv_heads=%d head_dim=%d vocab=%d runtime_seq=%d\n",
            (int)rt->cfg.model_kind, rt->cfg.dim, rt->cfg.hidden_dim, rt->cfg.n_layers, rt->cfg.n_heads,
            rt->cfg.n_kv_heads, rt->cfg.head_dim, rt->cfg.vocab_size, rt->cfg.runtime_seq_len);

    rt->layer_kinds = load_layer_kinds(&rt->cfg);
    int linear_layers = 0;
    for (int l = 0; l < rt->cfg.n_layers; l++) {
        if (rt->layer_kinds[l] == LAYER_KIND_LINEAR) {
            linear_layers++;
        }
    }
    if (!allow_linear && linear_layers != 0) {
        LLM_ERR("this program only supports full-attention layers\n");
        exit(1);
    }
    if (linear_layers != 0 && cfg_linear_conv_dim(&rt->cfg) <= 0) {
        LLM_ERR("linear layer metadata is incomplete\n");
        exit(1);
    }

    uint64 emb_blob_sz = 0;
    char *emb_blob = load_file("EMB.BIN", &emb_blob_sz);
    if (emb_blob == 0) {
        LLM_ERR("missing EMB.BIN\n");
        exit(1);
    }
    LLM_LOG("EMB.BIN loaded (%lu bytes)\n", (unsigned long)emb_blob_sz);
    rt->emb.rows = rt->cfg.vocab_size;
    rt->emb.cols = rt->cfg.dim;
    rt->emb.scales = (float *)emb_blob;
    rt->emb.q = (int8 *)(emb_blob + (uint64)rt->cfg.vocab_size * sizeof(float));
    if ((uint64)((char *)rt->emb.q - emb_blob) + (uint64)rt->cfg.vocab_size * (uint64)rt->cfg.dim != emb_blob_sz) {
        LLM_ERR("embedding blob size mismatch\n");
        exit(1);
    }

    uint64 nrm_sz = 0;
    rt->final_norm = (float *)load_file("NRM.BIN", &nrm_sz);
    if (rt->final_norm == 0 || nrm_sz != (uint64)rt->cfg.dim * sizeof(float)) {
        LLM_ERR("bad NRM.BIN size\n");
        exit(1);
    }

    rt->layers = (struct layer *)xmalloc(sizeof(struct layer) * (uint64)rt->cfg.n_layers);
    for (int l = 0; l < rt->cfg.n_layers; l++) {
        char path[16];
        uint64 sz = 0;
        char *blob;

        make_layer_path(l, path, sizeof(path));
        blob = load_file(path, &sz);
        if (blob == 0) {
            LLM_ERR("missing %s\n", path);
            exit(1);
        }
        parse_layer_blob(&rt->layers[l], blob, sz, &rt->cfg, rt->layer_kinds[l]);
        if ((l % 5) == 4 || l == rt->cfg.n_layers - 1) {
            LLM_LOG("layers 0..%d loaded\n", l);
        }
    }
    LLM_LOG("layer mix full=%d linear=%d\n", rt->cfg.n_layers - linear_layers, linear_layers);

    uint64 rope_sz = 0;
    rt->rope = (float *)load_file("ROP.BIN", &rope_sz);
    if (rt->rope == 0 || rope_sz != (uint64)rt->cfg.runtime_seq_len * (uint64)rt->cfg.rope_rotary_dim * sizeof(float)) {
        LLM_ERR("bad ROP.BIN size\n");
        exit(1);
    }

    rt->seq = (uint32 *)xmalloc(sizeof(uint32) * (uint64)rt->cfg.runtime_seq_len);
    memset(rt->seq, 0, sizeof(uint32) * (uint64)rt->cfg.runtime_seq_len);
}

static void __attribute__((unused)) llm_alloc_kv_caches(const struct model_cfg *cfg, float **kcache_out, float **vcache_out) {
    uint64 cache_elems = llm_kv_cache_elems(cfg);
    *kcache_out = llm_alloc_zeroed_floats(cache_elems);
    *vcache_out = llm_alloc_zeroed_floats(cache_elems);
}

static void __attribute__((unused)) llm_workspace_init_full(const struct model_cfg *cfg, struct llm_workspace *ws) {
    memset(ws, 0, sizeof(*ws));
    ws->hidden = llm_alloc_floats((uint64)cfg->dim);
    ws->norm_hidden = llm_alloc_floats((uint64)cfg->dim);
    ws->proj_out = llm_alloc_floats((uint64)cfg->dim);
    ws->qkv_mixed = llm_alloc_floats((uint64)cfg_full_q_rows(cfg));
    ws->query = llm_alloc_floats((uint64)cfg_attn_out_dim(cfg));
    if (cfg_has_attn_gate(cfg)) {
        ws->attn_gate = llm_alloc_floats((uint64)cfg_attn_out_dim(cfg));
    }
    ws->key = llm_alloc_floats((uint64)cfg_kv_dim(cfg));
    ws->value = llm_alloc_floats((uint64)cfg_kv_dim(cfg));
    ws->attn_out = llm_alloc_floats((uint64)cfg_attn_out_dim(cfg));
    ws->ffn_gate = llm_alloc_floats((uint64)cfg->hidden_dim);
    ws->ffn_up = llm_alloc_floats((uint64)cfg->hidden_dim);
    ws->scores = llm_alloc_floats((uint64)cfg->runtime_seq_len);
}

static void llm_apply_full_layer(
    const struct llm_runtime *rt,
    float *kcache,
    float *vcache,
    int layer_idx,
    int pos,
    struct llm_workspace *ws
) {
    const struct model_cfg *cfg = &rt->cfg;
    struct layer *ly = &rt->layers[layer_idx];
    int head_dim = cfg->head_dim;
    int attn_out_dim = cfg_attn_out_dim(cfg);
    int kv_mul = cfg_kv_mul(cfg);
    float *cosv = rt->rope + (uint64)pos * (uint64)cfg->rope_rotary_dim;
    float *sinv = cosv + cfg->rope_rotary_dim / 2;

    rmsnorm(ws->norm_hidden, ws->hidden, ly->input_norm, cfg->dim, cfg->rms_eps);
    qmatmul(ws->qkv_mixed, ly->u.full.q_proj, ws->norm_hidden);
    if (cfg_has_attn_gate(cfg)) {
        for (int h = 0; h < cfg->n_heads; h++) {
            const float *base = ws->qkv_mixed + (uint64)h * (uint64)head_dim * 2ULL;
            memcpy(ws->query + (uint64)h * (uint64)head_dim, base, (uint)head_dim * sizeof(float));
            memcpy(ws->attn_gate + (uint64)h * (uint64)head_dim, base + head_dim, (uint)head_dim * sizeof(float));
        }
    } else {
        memcpy(ws->query, ws->qkv_mixed, (uint)attn_out_dim * sizeof(float));
    }

    qmatmul(ws->key, ly->u.full.k_proj, ws->norm_hidden);
    qmatmul(ws->value, ly->u.full.v_proj, ws->norm_hidden);

    if (cfg_has_qk_norm(cfg)) {
        for (int h = 0; h < cfg->n_heads; h++) {
            rmsnorm(ws->query + (uint64)h * (uint64)head_dim,
                    ws->query + (uint64)h * (uint64)head_dim,
                    ly->u.full.q_norm, head_dim, cfg->rms_eps);
        }
        for (int h = 0; h < cfg->n_kv_heads; h++) {
            rmsnorm(ws->key + (uint64)h * (uint64)head_dim,
                    ws->key + (uint64)h * (uint64)head_dim,
                    ly->u.full.k_norm, head_dim, cfg->rms_eps);
        }
    }

    for (int h = 0; h < cfg->n_heads; h++) {
        apply_rope_partial(ws->query + (uint64)h * (uint64)head_dim, cosv, sinv, cfg->rope_rotary_dim);
    }
    for (int h = 0; h < cfg->n_kv_heads; h++) {
        apply_rope_partial(ws->key + (uint64)h * (uint64)head_dim, cosv, sinv, cfg->rope_rotary_dim);
    }

    for (int h = 0; h < cfg->n_kv_heads; h++) {
        memcpy(llm_kcache_at(kcache, cfg, layer_idx, pos, h),
               ws->key + (uint64)h * (uint64)head_dim,
               (uint)head_dim * sizeof(float));
        memcpy(llm_kcache_at(vcache, cfg, layer_idx, pos, h),
               ws->value + (uint64)h * (uint64)head_dim,
               (uint)head_dim * sizeof(float));
    }

    memset(ws->attn_out, 0, (uint)((uint64)attn_out_dim * sizeof(float)));
    float scale = 1.0f / fsqrt1((float)head_dim);
    for (int h = 0; h < cfg->n_heads; h++) {
        int kvh = h / kv_mul;
        float *qh = ws->query + (uint64)h * (uint64)head_dim;
        float *ah = ws->attn_out + (uint64)h * (uint64)head_dim;

        for (int t = 0; t <= pos; t++) {
            float *kh = llm_kcache_at(kcache, cfg, layer_idx, t, kvh);
            float acc = 0.0f;
            for (int i = 0; i < head_dim; i++) {
                acc += qh[i] * kh[i];
            }
            ws->scores[t] = acc * scale;
        }
        softmax_inplace(ws->scores, pos + 1);

        for (int t = 0; t <= pos; t++) {
            float *vh = llm_kcache_at(vcache, cfg, layer_idx, t, kvh);
            float w = ws->scores[t];
            for (int i = 0; i < head_dim; i++) {
                ah[i] += w * vh[i];
            }
        }
    }

    if (cfg_has_attn_gate(cfg)) {
        for (int i = 0; i < attn_out_dim; i++) {
            ws->attn_out[i] *= sigmoid_approx(ws->attn_gate[i]);
        }
    }

    qmatmul(ws->proj_out, ly->u.full.o_proj, ws->attn_out);
    for (int i = 0; i < cfg->dim; i++) {
        ws->hidden[i] += ws->proj_out[i];
    }
}

static void llm_apply_ffn(const struct model_cfg *cfg, struct layer *ly, struct llm_workspace *ws) {
    rmsnorm(ws->norm_hidden, ws->hidden, ly->post_norm, cfg->dim, cfg->rms_eps);
    qmatmul(ws->ffn_gate, ly->gate_proj, ws->norm_hidden);
    qmatmul(ws->ffn_up, ly->up_proj, ws->norm_hidden);
    for (int i = 0; i < cfg->hidden_dim; i++) {
        ws->ffn_gate[i] = silu_approx(ws->ffn_gate[i]) * ws->ffn_up[i];
    }
    qmatmul(ws->proj_out, ly->down_proj, ws->ffn_gate);
    for (int i = 0; i < cfg->dim; i++) {
        ws->hidden[i] += ws->proj_out[i];
    }
}

static int llm_drive_decode(
    struct llm_runtime *rt,
    struct llm_workspace *ws,
    float *kcache,
    float *vcache,
    float *linear_conv_cache,
    float *linear_state_cache,
    int token_count,
    int predict_count,
    llm_token_forward_fn token_forward
) {
    int total_steps = token_count + predict_count - 1;
    int start_tick = uptime();

    LLM_LOG("starting forward pass\n");
    // Hint for students:
    // - A natural request-level timing start point is right before this loop.
    // - If you measure TTFT with timer_start()/timer_end(), record the start
    //   timestamp once before the first token_forward() call below.
    for (int pos = 0; pos < total_steps; pos++) {
        // Hint for students:
        // - For per-step latency, take a step start timestamp immediately
        //   before token_forward().
        // - The matching step end timestamp is immediately after it returns.
        int next = token_forward(rt, kcache, vcache, linear_conv_cache, linear_state_cache, pos, ws);
        if (next < 0) {
            return -1;
        }
        if (pos >= token_count - 1) {
            int gen_idx = pos - (token_count - 1);
            rt->seq[token_count + gen_idx] = (uint32)next;
            LLM_LOG("gen[%d] token=%d\n", gen_idx, next);
            // Hint for students:
            // - When gen_idx == 0, "next" is the first generated token.
            //   This branch is a natural TTFT stop point.
            // - When gen_idx > 0, this iteration is a natural TPOT sample.
            //   If you want steady-state TPOT, average those later generated
            //   token iterations and exclude gen_idx == 0 from the TPOT average.
        }
    }
    LLM_LOG("tick_span=%d\n", uptime() - start_tick);
    return 0;
}

static int llm_validate_request(const struct llm_runtime *rt, const struct llm_request *req) {
    if (req->token_count <= 0 || req->token_count > LLM_REQUEST_TOKEN_MAX) {
        LLM_ERR("invalid token count %d\n", req->token_count);
        return -1;
    }
    if (req->predict_count <= 0) {
        LLM_ERR("predict count must be > 0\n");
        return -1;
    }
    if (req->token_count + req->predict_count > rt->cfg.runtime_seq_len) {
        LLM_ERR("request too long for runtime_seq_len=%d\n", rt->cfg.runtime_seq_len);
        return -1;
    }
    for (int i = 0; i < req->token_count; i++) {
        if (req->tokens[i] >= (uint32)rt->cfg.vocab_size) {
            LLM_ERR("token %d out of range\n", i);
            return -1;
        }
    }
    return 0;
}

static void llm_session_reset(struct llm_session *sess, const struct llm_request *req) {
    memset(sess->rt.seq, 0, sizeof(uint32) * (uint64)sess->rt.cfg.runtime_seq_len);
    memcpy(sess->rt.seq, req->tokens, (uint)(req->token_count * (int)sizeof(uint32)));
    if (sess->kcache != 0) {
        memset(sess->kcache, 0, (uint)(sess->kv_cache_elems * sizeof(float)));
    }
    if (sess->vcache != 0) {
        memset(sess->vcache, 0, (uint)(sess->kv_cache_elems * sizeof(float)));
    }
    if (sess->linear_conv_cache != 0) {
        memset(sess->linear_conv_cache, 0, (uint)(sess->linear_conv_elems * sizeof(float)));
    }
    if (sess->linear_state_cache != 0) {
        memset(sess->linear_state_cache, 0, (uint)(sess->linear_state_elems * sizeof(float)));
    }
}

static int llm_session_run(
    struct llm_session *sess,
    const struct llm_request *req,
    llm_token_forward_fn token_forward,
    char *out,
    int out_cap
) {
    if (llm_validate_request(&sess->rt, req) < 0) {
        return -1;
    }
    llm_session_reset(sess, req);
    if (llm_drive_decode(
            &sess->rt,
            &sess->ws,
            sess->kcache,
            sess->vcache,
            sess->linear_conv_cache,
            sess->linear_state_cache,
            req->token_count,
            req->predict_count,
            token_forward
        ) < 0) {
        return -1;
    }
    return llm_format_generated_tokens(sess->rt.seq, req->token_count, req->predict_count, out, out_cap);
}

#endif

#endif
