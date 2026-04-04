#define LLMRUN_SUPPORT_NO_RUNTIME_IMPL 1
#include "llmrun_support.h"
#include "fcntl.h"

static char *heap_end;
static const char *current_prog_name = "llmrun";

struct asset_blob {
    const char *logical_name;
    char actual_path[64];
    char *data;
    uint64 size;
    int chunk_count;
};

static void asset_blob_init(struct asset_blob *blob, const char *logical_name) {
    memset(blob, 0, sizeof(*blob));
    blob->logical_name = logical_name;
}

const char *llm_prog_name(void) {
    return current_prog_name;
}

void llm_set_prog_name(const char *name) {
    if (name != 0 && name[0] != '\0') {
        current_prog_name = name;
    }
}

void llm_set_prog_name_from_argv(const char *argv0, const char *fallback) {
    const char *name = argv0;
    if (name != 0) {
        const char *p = name;
        while (*p != '\0') {
            if (*p == '/') {
                name = p + 1;
            }
            p++;
        }
    }
    if (name == 0 || name[0] == '\0') {
        llm_set_prog_name(fallback);
    } else {
        llm_set_prog_name(name);
    }
}

int max_int(int a, int b) {
    return a > b ? a : b;
}

int cfg_has_attn_gate(const struct model_cfg *cfg) {
    return (cfg->flags & CFG_FLAG_ATTN_GATE) != 0;
}

int cfg_has_qk_norm(const struct model_cfg *cfg) {
    return (cfg->flags & CFG_FLAG_QK_NORM) != 0;
}

int cfg_attn_out_dim(const struct model_cfg *cfg) {
    return cfg->n_heads * cfg->head_dim;
}

int cfg_full_q_rows(const struct model_cfg *cfg) {
    int rows = cfg_attn_out_dim(cfg);
    if (cfg_has_attn_gate(cfg)) {
        rows *= 2;
    }
    return rows;
}

int cfg_kv_dim(const struct model_cfg *cfg) {
    return cfg->head_dim * cfg->n_kv_heads;
}

int cfg_kv_mul(const struct model_cfg *cfg) {
    return cfg->n_heads / cfg->n_kv_heads;
}

int cfg_linear_key_dim(const struct model_cfg *cfg) {
    return cfg->linear_num_k_heads * cfg->linear_key_head_dim;
}

int cfg_linear_value_dim(const struct model_cfg *cfg) {
    return cfg->linear_num_v_heads * cfg->linear_value_head_dim;
}

int cfg_linear_conv_dim(const struct model_cfg *cfg) {
    return cfg_linear_key_dim(cfg) * 2 + cfg_linear_value_dim(cfg);
}

uint64 llm_kv_cache_elems(const struct model_cfg *cfg) {
    return (uint64)cfg->n_layers * (uint64)cfg->runtime_seq_len * (uint64)cfg->n_kv_heads * (uint64)cfg->head_dim;
}

uint64 llm_qwen_linear_conv_elems(const struct model_cfg *cfg) {
    return (uint64)cfg->n_layers * (uint64)cfg_linear_conv_dim(cfg) * (uint64)cfg->linear_conv_kernel;
}

uint64 llm_qwen_linear_state_elems(const struct model_cfg *cfg) {
    return (uint64)cfg->n_layers * (uint64)cfg->linear_num_v_heads *
           (uint64)cfg->linear_key_head_dim * (uint64)cfg->linear_value_head_dim;
}

static void fill_legacy_cfg(struct model_cfg *cfg) {
    cfg->model_kind = MODEL_KIND_SMOL;
    cfg->flags = 0;
    cfg->dim = LEGACY_DIM;
    cfg->hidden_dim = LEGACY_HIDDEN_DIM;
    cfg->n_layers = LEGACY_N_LAYERS;
    cfg->n_heads = LEGACY_N_HEADS;
    cfg->n_kv_heads = LEGACY_N_KV_HEADS;
    cfg->head_dim = LEGACY_HEAD_DIM;
    cfg->vocab_size = LEGACY_VOCAB_SIZE;
    cfg->seq_len = LEGACY_SEQ_LEN;
    cfg->runtime_seq_len = LEGACY_RUNTIME_SEQ_LEN;
    cfg->rms_eps = LEGACY_RMS_EPS;
    cfg->rope_theta = LEGACY_ROPE_THETA;
    cfg->rope_rotary_dim = LEGACY_HEAD_DIM;
    cfg->linear_num_k_heads = 0;
    cfg->linear_num_v_heads = 0;
    cfg->linear_key_head_dim = 0;
    cfg->linear_value_head_dim = 0;
    cfg->linear_conv_kernel = 0;
}

void *xmalloc(uint64 n) {
    if (n == 0) {
        return 0;
    }
    uint64 aligned = (n + 15ULL) & ~15ULL;
    if (heap_end == 0) {
        heap_end = (char *)sbrk(0);
    }
    void *p = sbrk((int)aligned);
    if (p == (void *)-1) {
        LLM_ERR("sbrk failed for %lu bytes\n", (unsigned long)aligned);
        exit(1);
    }
    heap_end += aligned;
    return p;
}

int read_full(int fd, void *buf, int n) {
    char *p = (char *)buf;
    int done = 0;
    while (done < n) {
        int r = read(fd, p + done, n - done);
        if (r <= 0) {
            return -1;
        }
        done += r;
    }
    return 0;
}

static void asset_chunk_path(char *out, int out_sz, const char *logical_name, int index) {
    char digits[8];
    int nd = 0;
    int x = index;
    if (out == 0 || logical_name == 0 || out_sz < 8 || x < 0) {
        LLM_ERR("bad chunk path buffer\n");
        exit(1);
    }
    do {
        digits[nd++] = (char)('0' + (x % 10));
        x /= 10;
    } while (x != 0 && nd < (int)sizeof(digits));

    int p = 0;
    for (const char *s = logical_name; *s != '\0'; s++) {
        if (p >= out_sz - 1) {
            LLM_ERR("chunk path overflow\n");
            exit(1);
        }
        out[p++] = *s;
    }
    if (p >= out_sz - 1) {
        LLM_ERR("chunk path overflow\n");
        exit(1);
    }
    out[p++] = '/';
    for (int i = nd; i < 5; i++) {
        if (p >= out_sz - 1) {
            LLM_ERR("chunk path overflow\n");
            exit(1);
        }
        out[p++] = '0';
    }
    for (int i = nd - 1; i >= 0; i--) {
        if (p >= out_sz - 1) {
            LLM_ERR("chunk path overflow\n");
            exit(1);
        }
        out[p++] = digits[i];
    }
    out[p] = '\0';
}

static int asset_blob_load_file(struct asset_blob *blob, const struct stat *st) {
    int fd = open(blob->logical_name, O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    char *buf = (char *)xmalloc(st->size);
    if (read_full(fd, buf, (int)st->size) < 0) {
        close(fd);
        LLM_ERR("failed to read %s\n", blob->logical_name);
        exit(1);
    }
    close(fd);
    strcpy(blob->actual_path, blob->logical_name);
    blob->data = buf;
    blob->size = st->size;
    blob->chunk_count = 1;
    return 1;
}

static int asset_blob_load_dir(struct asset_blob *blob) {
    char path[64];
    struct stat st;
    int chunks = 0;
    uint64 total = 0;

    for (;;) {
        asset_chunk_path(path, sizeof(path), blob->logical_name, chunks);
        if (stat(path, &st) < 0) {
            break;
        }
        if (st.type != T_FILE) {
            LLM_ERR("chunk %s is not a file\n", path);
            exit(1);
        }
        total += st.size;
        chunks++;
    }

    if (chunks == 0) {
        return 0;
    }

    char *buf = (char *)xmalloc(total);
    uint64 off = 0;
    for (int i = 0; i < chunks; i++) {
        asset_chunk_path(path, sizeof(path), blob->logical_name, i);
        if (stat(path, &st) < 0) {
            LLM_ERR("missing chunk %s\n", path);
            exit(1);
        }
        int fd = open(path, O_RDONLY);
        if (fd < 0) {
            LLM_ERR("failed to open %s\n", path);
            exit(1);
        }
        if (read_full(fd, buf + off, (int)st.size) < 0) {
            close(fd);
            LLM_ERR("failed to read %s\n", path);
            exit(1);
        }
        close(fd);
        off += st.size;
    }

    strcpy(blob->actual_path, blob->logical_name);
    blob->data = buf;
    blob->size = total;
    blob->chunk_count = chunks;
    return 1;
}

static int asset_blob_load(struct asset_blob *blob) {
    struct stat st;
    if (stat(blob->logical_name, &st) < 0) {
        return 0;
    }
    if (st.type == T_FILE) {
        return asset_blob_load_file(blob, &st);
    }
    if (st.type == T_DIR) {
        return asset_blob_load_dir(blob);
    }
    LLM_ERR("unsupported asset type for %s\n", blob->logical_name);
    exit(1);
}

char *load_file(const char *path, uint64 *size_out) {
    struct asset_blob blob;
    asset_blob_init(&blob, path);
    if (!asset_blob_load(&blob)) {
        return 0;
    }
    if (size_out) {
        *size_out = blob.size;
    }
    if (blob.chunk_count > 1) {
        LLM_LOG("loaded %s from %d chunks (%lu bytes)\n", blob.logical_name, blob.chunk_count, (unsigned long)blob.size);
    }
    return blob.data;
}

static void validate_cfg(const struct model_cfg *cfg) {
    if (cfg->dim <= 0 || cfg->hidden_dim <= 0 || cfg->n_layers <= 0 || cfg->n_heads <= 0 || cfg->n_kv_heads <= 0 ||
        cfg->head_dim <= 0 || cfg->vocab_size <= 0 || cfg->seq_len <= 0 || cfg->runtime_seq_len <= 0) {
        LLM_ERR("invalid model config\n");
        exit(1);
    }
    if ((cfg->head_dim & 1) != 0) {
        LLM_ERR("head_dim must be even for RoPE\n");
        exit(1);
    }
    if (cfg->n_heads % cfg->n_kv_heads != 0) {
        LLM_ERR("head/kv-head mismatch\n");
        exit(1);
    }
    if (cfg->runtime_seq_len > cfg->seq_len) {
        LLM_ERR("runtime_seq_len exceeds seq_len\n");
        exit(1);
    }
    if (cfg->rope_rotary_dim <= 0 || cfg->rope_rotary_dim > cfg->head_dim || (cfg->rope_rotary_dim & 1) != 0) {
        LLM_ERR("invalid rotary dimension\n");
        exit(1);
    }
    if (cfg->linear_num_k_heads < 0 || cfg->linear_num_v_heads < 0 || cfg->linear_key_head_dim < 0 ||
        cfg->linear_value_head_dim < 0 || cfg->linear_conv_kernel < 0) {
        LLM_ERR("invalid linear config\n");
        exit(1);
    }
    if (cfg->linear_num_k_heads != 0 || cfg->linear_num_v_heads != 0 || cfg->linear_key_head_dim != 0 ||
        cfg->linear_value_head_dim != 0 || cfg->linear_conv_kernel != 0) {
        if (cfg->linear_num_k_heads <= 0 || cfg->linear_num_v_heads <= 0 || cfg->linear_key_head_dim <= 0 ||
            cfg->linear_value_head_dim <= 0 || cfg->linear_conv_kernel <= 0) {
            LLM_ERR("incomplete linear config\n");
            exit(1);
        }
        if (cfg->linear_num_v_heads % cfg->linear_num_k_heads != 0) {
            LLM_ERR("linear head mismatch\n");
            exit(1);
        }
    }
}

void load_model_cfg(struct model_cfg *cfg) {
    uint64 sz = 0;
    char *raw = load_file("CFG.BIN", &sz);
    if (raw == 0) {
        fill_legacy_cfg(cfg);
        LLM_LOG("CFG.BIN missing, using legacy SmolLM defaults\n");
        validate_cfg(cfg);
        return;
    }
    if (sz < 8) {
        LLM_ERR("bad CFG.BIN size (%lu)\n", (unsigned long)sz);
        exit(1);
    }

    uint32 magic = ((uint32 *)raw)[0];
    uint32 version = ((uint32 *)raw)[1];
    if (magic != LLM_CFG_MAGIC) {
        LLM_ERR("unsupported CFG.BIN header\n");
        exit(1);
    }

    if (version == LLM_CFG_VERSION_V1) {
        struct model_cfg_disk_v1 *disk = (struct model_cfg_disk_v1 *)raw;
        if (sz != sizeof(*disk)) {
            LLM_ERR("bad CFG.BIN v1 size (%lu)\n", (unsigned long)sz);
            exit(1);
        }
        cfg->model_kind = MODEL_KIND_SMOL;
        cfg->flags = 0;
        cfg->dim = (int)disk->dim;
        cfg->hidden_dim = (int)disk->hidden_dim;
        cfg->n_layers = (int)disk->n_layers;
        cfg->n_heads = (int)disk->n_heads;
        cfg->n_kv_heads = (int)disk->n_kv_heads;
        cfg->head_dim = cfg->dim / cfg->n_heads;
        cfg->vocab_size = (int)disk->vocab_size;
        cfg->seq_len = (int)disk->seq_len;
        cfg->runtime_seq_len = (int)disk->runtime_seq_len;
        cfg->rms_eps = disk->rms_eps;
        cfg->rope_theta = disk->rope_theta;
        cfg->rope_rotary_dim = cfg->head_dim;
        cfg->linear_num_k_heads = 0;
        cfg->linear_num_v_heads = 0;
        cfg->linear_key_head_dim = 0;
        cfg->linear_value_head_dim = 0;
        cfg->linear_conv_kernel = 0;
    } else if (version == LLM_CFG_VERSION_V2) {
        struct model_cfg_disk_v2 *disk = (struct model_cfg_disk_v2 *)raw;
        if (sz != sizeof(*disk)) {
            LLM_ERR("bad CFG.BIN v2 size (%lu)\n", (unsigned long)sz);
            exit(1);
        }
        cfg->model_kind = disk->model_kind;
        cfg->flags = disk->flags;
        cfg->dim = (int)disk->dim;
        cfg->hidden_dim = (int)disk->hidden_dim;
        cfg->n_layers = (int)disk->n_layers;
        cfg->n_heads = (int)disk->n_heads;
        cfg->n_kv_heads = (int)disk->n_kv_heads;
        cfg->head_dim = (int)disk->head_dim;
        cfg->vocab_size = (int)disk->vocab_size;
        cfg->seq_len = (int)disk->seq_len;
        cfg->runtime_seq_len = (int)disk->runtime_seq_len;
        cfg->rms_eps = disk->rms_eps;
        cfg->rope_theta = disk->rope_theta;
        cfg->rope_rotary_dim = (int)disk->rope_rotary_dim;
        cfg->linear_num_k_heads = (int)disk->linear_num_k_heads;
        cfg->linear_num_v_heads = (int)disk->linear_num_v_heads;
        cfg->linear_key_head_dim = (int)disk->linear_key_head_dim;
        cfg->linear_value_head_dim = (int)disk->linear_value_head_dim;
        cfg->linear_conv_kernel = (int)disk->linear_conv_kernel;
    } else {
        LLM_ERR("unsupported CFG.BIN version %lu\n", (unsigned long)version);
        exit(1);
    }
    validate_cfg(cfg);
}

uint32 *load_layer_kinds(const struct model_cfg *cfg) {
    uint64 sz = 0;
    char *raw = load_file("LTY.BIN", &sz);
    uint32 *kinds = (uint32 *)xmalloc((uint64)cfg->n_layers * sizeof(uint32));
    for (int i = 0; i < cfg->n_layers; i++) {
        kinds[i] = LAYER_KIND_FULL;
    }
    if (raw == 0) {
        if (cfg->model_kind == MODEL_KIND_QWEN) {
            LLM_ERR("missing LTY.BIN for Qwen\n");
            exit(1);
        }
        return kinds;
    }
    if (sz < 4) {
        LLM_ERR("bad LTY.BIN size\n");
        exit(1);
    }
    uint32 count = *(uint32 *)raw;
    if (count != (uint32)cfg->n_layers || sz != 4ULL + (uint64)count * sizeof(uint32)) {
        LLM_ERR("bad LTY.BIN layout\n");
        exit(1);
    }
    uint32 *vals = (uint32 *)(raw + 4);
    for (int i = 0; i < cfg->n_layers; i++) {
        if (vals[i] != LAYER_KIND_FULL && vals[i] != LAYER_KIND_LINEAR) {
            LLM_ERR("unsupported layer kind %d\n", (int)vals[i]);
            exit(1);
        }
        kinds[i] = vals[i];
    }
    return kinds;
}

float fsqrt1(float x) {
    float out;
    asm volatile("fsqrt.s %0, %1" : "=f"(out) : "f"(x));
    return out;
}

static float poly_exp_pos(float x) {
    if (x < 0.0f) {
        x = 0.0f;
    } else if (x > 8.0f) {
        x = 8.0f;
    }
    return 1.0f + x * (1.0f + x * (0.5f + x * ((1.0f / 6.0f) + x * ((1.0f / 24.0f) + x * (1.0f / 120.0f)))));
}

float exp_approx(float x) {
    if (x >= 0.0f) {
        return poly_exp_pos(x);
    }
    return 1.0f / poly_exp_pos(-x);
}

static float log1p_unit_approx(float x) {
    float x2 = x * x;
    float x3 = x2 * x;
    float x4 = x3 * x;
    float x5 = x4 * x;
    return x - 0.5f * x2 + (1.0f / 3.0f) * x3 - 0.25f * x4 + 0.2f * x5;
}

float softplus_approx(float x) {
    if (x >= 0.0f) {
        return x + log1p_unit_approx(exp_approx(-x));
    }
    return log1p_unit_approx(exp_approx(x));
}

float sigmoid_approx(float x) {
    return 1.0f / (1.0f + exp_approx(-x));
}

float silu_approx(float x) {
    return x * sigmoid_approx(x);
}

void softmax_inplace(float *x, int n) {
    float maxv = x[0];
    for (int i = 1; i < n; i++) {
        if (x[i] > maxv) {
            maxv = x[i];
        }
    }

    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        float d = x[i] - maxv;
        if (d < -8.0f) {
            d = -8.0f;
        } else if (d > 0.0f) {
            d = 0.0f;
        }
        x[i] = exp_approx(d);
        sum += x[i];
    }

    float inv = 1.0f / sum;
    for (int i = 0; i < n; i++) {
        x[i] *= inv;
    }
}

void rmsnorm(float *out, const float *x, const float *weight, int n, float eps) {
    float ss = 0.0f;
    for (int i = 0; i < n; i++) {
        ss += x[i] * x[i];
    }
    float inv = 1.0f / fsqrt1(ss / (float)n + eps);
    for (int i = 0; i < n; i++) {
        out[i] = x[i] * inv * weight[i];
    }
}

void l2norm_inplace(float *x, int n, float eps) {
    float ss = 0.0f;
    for (int i = 0; i < n; i++) {
        ss += x[i] * x[i];
    }
    float inv = 1.0f / fsqrt1(ss + eps);
    for (int i = 0; i < n; i++) {
        x[i] *= inv;
    }
}

static inline void dot4_accum(
    const int8 *row0,
    const int8 *row1,
    const int8 *row2,
    const int8 *row3,
    const float *x,
    int cols,
    float *acc0,
    float *acc1,
    float *acc2,
    float *acc3
) {
    int c = 0;
    for (; c + 7 < cols; c += 8) {
        float x0 = x[c];
        float x1 = x[c + 1];
        float x2 = x[c + 2];
        float x3 = x[c + 3];
        float x4 = x[c + 4];
        float x5 = x[c + 5];
        float x6 = x[c + 6];
        float x7 = x[c + 7];

        *acc0 += (float)row0[c] * x0 + (float)row0[c + 1] * x1 + (float)row0[c + 2] * x2 + (float)row0[c + 3] * x3 +
                 (float)row0[c + 4] * x4 + (float)row0[c + 5] * x5 + (float)row0[c + 6] * x6 + (float)row0[c + 7] * x7;
        *acc1 += (float)row1[c] * x0 + (float)row1[c + 1] * x1 + (float)row1[c + 2] * x2 + (float)row1[c + 3] * x3 +
                 (float)row1[c + 4] * x4 + (float)row1[c + 5] * x5 + (float)row1[c + 6] * x6 + (float)row1[c + 7] * x7;
        *acc2 += (float)row2[c] * x0 + (float)row2[c + 1] * x1 + (float)row2[c + 2] * x2 + (float)row2[c + 3] * x3 +
                 (float)row2[c + 4] * x4 + (float)row2[c + 5] * x5 + (float)row2[c + 6] * x6 + (float)row2[c + 7] * x7;
        *acc3 += (float)row3[c] * x0 + (float)row3[c + 1] * x1 + (float)row3[c + 2] * x2 + (float)row3[c + 3] * x3 +
                 (float)row3[c + 4] * x4 + (float)row3[c + 5] * x5 + (float)row3[c + 6] * x6 + (float)row3[c + 7] * x7;
    }
    for (; c < cols; c++) {
        float xv = x[c];
        *acc0 += (float)row0[c] * xv;
        *acc1 += (float)row1[c] * xv;
        *acc2 += (float)row2[c] * xv;
        *acc3 += (float)row3[c] * xv;
    }
}

static inline float dot1_accum(const int8 *row, const float *x, int cols) {
    float acc = 0.0f;
    int c = 0;
    for (; c + 7 < cols; c += 8) {
        acc += (float)row[c] * x[c] + (float)row[c + 1] * x[c + 1] + (float)row[c + 2] * x[c + 2] +
               (float)row[c + 3] * x[c + 3] + (float)row[c + 4] * x[c + 4] + (float)row[c + 5] * x[c + 5] +
               (float)row[c + 6] * x[c + 6] + (float)row[c + 7] * x[c + 7];
    }
    for (; c < cols; c++) {
        acc += (float)row[c] * x[c];
    }
    return acc;
}

void qmatmul(float *out, struct qmat m, const float *x) {
    int rows = m.rows;
    int cols = m.cols;
    int r = 0;
    for (; r + 3 < rows; r += 4) {
        const int8 *row0 = m.q + (uint64)r * (uint64)cols;
        const int8 *row1 = row0 + cols;
        const int8 *row2 = row1 + cols;
        const int8 *row3 = row2 + cols;
        float acc0 = 0.0f;
        float acc1 = 0.0f;
        float acc2 = 0.0f;
        float acc3 = 0.0f;
        dot4_accum(row0, row1, row2, row3, x, cols, &acc0, &acc1, &acc2, &acc3);
        out[r] = acc0 * m.scales[r];
        out[r + 1] = acc1 * m.scales[r + 1];
        out[r + 2] = acc2 * m.scales[r + 2];
        out[r + 3] = acc3 * m.scales[r + 3];
    }
    for (; r < rows; r++) {
        const int8 *row = m.q + (uint64)r * (uint64)cols;
        out[r] = dot1_accum(row, x, cols) * m.scales[r];
    }
}

void apply_rope_partial(float *vec, const float *cosv, const float *sinv, int rotary_dim) {
    int half = rotary_dim / 2;
    for (int i = 0; i < half; i++) {
        float x1 = vec[i];
        float x2 = vec[i + half];
        vec[i] = x1 * cosv[i] - x2 * sinv[i];
        vec[i + half] = x1 * sinv[i] + x2 * cosv[i];
    }
}

static struct qmat blob_qmat(char **p, int rows, int cols) {
    struct qmat m;
    m.rows = rows;
    m.cols = cols;
    m.scales = (float *)(*p);
    *p += (uint64)rows * sizeof(float);
    m.q = (int8 *)(*p);
    *p += (uint64)rows * (uint64)cols;
    return m;
}

static float *blob_vec(char **p, int n) {
    float *v = (float *)(*p);
    *p += (uint64)n * sizeof(float);
    return v;
}

static void parse_full_layer_blob(struct layer *ly, char *blob, uint64 size, const struct model_cfg *cfg) {
    char *p = blob;
    ly->input_norm = blob_vec(&p, cfg->dim);
    ly->post_norm = blob_vec(&p, cfg->dim);
    ly->u.full.q_norm = 0;
    ly->u.full.k_norm = 0;
    if (cfg_has_qk_norm(cfg)) {
        ly->u.full.q_norm = blob_vec(&p, cfg->head_dim);
        ly->u.full.k_norm = blob_vec(&p, cfg->head_dim);
    }
    ly->u.full.q_proj = blob_qmat(&p, cfg_full_q_rows(cfg), cfg->dim);
    ly->u.full.k_proj = blob_qmat(&p, cfg_kv_dim(cfg), cfg->dim);
    ly->u.full.v_proj = blob_qmat(&p, cfg_kv_dim(cfg), cfg->dim);
    ly->u.full.o_proj = blob_qmat(&p, cfg->dim, cfg_attn_out_dim(cfg));
    ly->gate_proj = blob_qmat(&p, cfg->hidden_dim, cfg->dim);
    ly->up_proj = blob_qmat(&p, cfg->hidden_dim, cfg->dim);
    ly->down_proj = blob_qmat(&p, cfg->dim, cfg->hidden_dim);
    if ((uint64)(p - blob) != size) {
        LLM_ERR("full layer blob size mismatch (%lu vs %lu)\n", (unsigned long)(p - blob), (unsigned long)size);
        exit(1);
    }
}

static void parse_linear_layer_blob(struct layer *ly, char *blob, uint64 size, const struct model_cfg *cfg) {
    char *p = blob;
    ly->input_norm = blob_vec(&p, cfg->dim);
    ly->post_norm = blob_vec(&p, cfg->dim);
    ly->u.linear.conv_weight = blob_vec(&p, cfg_linear_conv_dim(cfg) * cfg->linear_conv_kernel);
    ly->u.linear.dt_bias = blob_vec(&p, cfg->linear_num_v_heads);
    ly->u.linear.a_log = blob_vec(&p, cfg->linear_num_v_heads);
    ly->u.linear.norm = blob_vec(&p, cfg->linear_value_head_dim);
    ly->u.linear.qkv_proj = blob_qmat(&p, cfg_linear_conv_dim(cfg), cfg->dim);
    ly->u.linear.z_proj = blob_qmat(&p, cfg_linear_value_dim(cfg), cfg->dim);
    ly->u.linear.a_proj = blob_qmat(&p, cfg->linear_num_v_heads, cfg->dim);
    ly->u.linear.b_proj = blob_qmat(&p, cfg->linear_num_v_heads, cfg->dim);
    ly->u.linear.out_proj = blob_qmat(&p, cfg->dim, cfg_linear_value_dim(cfg));
    ly->gate_proj = blob_qmat(&p, cfg->hidden_dim, cfg->dim);
    ly->up_proj = blob_qmat(&p, cfg->hidden_dim, cfg->dim);
    ly->down_proj = blob_qmat(&p, cfg->dim, cfg->hidden_dim);
    if ((uint64)(p - blob) != size) {
        LLM_ERR("linear layer blob size mismatch (%lu vs %lu)\n", (unsigned long)(p - blob), (unsigned long)size);
        exit(1);
    }
}

void parse_layer_blob(struct layer *ly, char *blob, uint64 size, const struct model_cfg *cfg, uint32 kind) {
    ly->kind = kind;
    if (kind == LAYER_KIND_FULL) {
        parse_full_layer_blob(ly, blob, size, cfg);
    } else if (kind == LAYER_KIND_LINEAR) {
        parse_linear_layer_blob(ly, blob, size, cfg);
    } else {
        LLM_ERR("unsupported layer kind %d\n", (int)kind);
        exit(1);
    }
}

void make_layer_path(int layer, char *out, int out_sz) {
    char digits[12];
    int nd = 0;
    int x = layer;
    if (out_sz < 8 || layer < 0) {
        LLM_ERR("bad layer path buffer\n");
        exit(1);
    }
    if (x == 0) {
        digits[nd++] = '0';
    } else {
        while (x > 0 && nd < (int)sizeof(digits)) {
            digits[nd++] = (char)('0' + (x % 10));
            x /= 10;
        }
    }
    if (2 + nd + 4 >= out_sz) {
        LLM_ERR("layer index too large\n");
        exit(1);
    }
    int p = 0;
    out[p++] = 'L';
    for (int i = nd; i < 2; i++) {
        out[p++] = '0';
    }
    for (int i = nd - 1; i >= 0; i--) {
        out[p++] = digits[i];
    }
    out[p++] = '.';
    out[p++] = 'B';
    out[p++] = 'I';
    out[p++] = 'N';
    out[p] = '\0';
}

int argmax_embed(struct qmat emb, const float *x) {
    int best = 0;
    float bestv = -1.0e30f;
    int rows = emb.rows;
    int cols = emb.cols;
    int r = 0;
    for (; r + 3 < rows; r += 4) {
        const int8 *row0 = emb.q + (uint64)r * (uint64)cols;
        const int8 *row1 = row0 + cols;
        const int8 *row2 = row1 + cols;
        const int8 *row3 = row2 + cols;
        float acc0 = 0.0f;
        float acc1 = 0.0f;
        float acc2 = 0.0f;
        float acc3 = 0.0f;
        dot4_accum(row0, row1, row2, row3, x, cols, &acc0, &acc1, &acc2, &acc3);
        acc0 *= emb.scales[r];
        acc1 *= emb.scales[r + 1];
        acc2 *= emb.scales[r + 2];
        acc3 *= emb.scales[r + 3];
        if (acc0 > bestv) {
            bestv = acc0;
            best = r;
        }
        if (acc1 > bestv) {
            bestv = acc1;
            best = r + 1;
        }
        if (acc2 > bestv) {
            bestv = acc2;
            best = r + 2;
        }
        if (acc3 > bestv) {
            bestv = acc3;
            best = r + 3;
        }
    }
    for (; r < rows; r++) {
        const int8 *row = emb.q + (uint64)r * (uint64)cols;
        float score = dot1_accum(row, x, cols) * emb.scales[r];
        if (score > bestv) {
            bestv = score;
            best = r;
        }
    }
    return best;
}

int llm_parse_u32_token(const char *text, uint32 *value_out) {
    uint64 value = 0;
    if (text == 0 || text[0] == '\0') {
        return -1;
    }
    for (const char *p = text; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return -1;
        }
        value = value * 10ULL + (uint64)(*p - '0');
        if (value > 0xffffffffULL) {
            return -1;
        }
    }
    *value_out = (uint32)value;
    return 0;
}

static int llm_is_space_char(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

int llm_load_prompt_file(const char *path, struct llm_request *req, int predict_count) {
    uint64 size = 0;
    char *raw = load_file(path, &size);
    if (raw == 0) {
        LLM_ERR("failed to open prompt file %s\n", path);
        return -1;
    }

    char *buf = (char *)xmalloc(size + 1);
    if (size != 0) {
        memcpy(buf, raw, (uint)size);
    }
    buf[size] = '\0';

    memset(req, 0, sizeof(*req));
    req->predict_count = predict_count;

    char *p = buf;
    while (*p != '\0') {
        uint64 value = 0;
        int has_digit = 0;
        char *token_start;

        while (llm_is_space_char(*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        if (req->token_count >= LLM_REQUEST_TOKEN_MAX) {
            LLM_ERR("prompt file %s exceeds token limit %d\n", path, LLM_REQUEST_TOKEN_MAX);
            return -1;
        }

        token_start = p;
        while (*p != '\0' && !llm_is_space_char(*p)) {
            if (*p < '0' || *p > '9') {
                LLM_ERR("invalid token id in %s near byte %d\n", path, (int)(token_start - buf));
                return -1;
            }
            has_digit = 1;
            value = value * 10ULL + (uint64)(*p - '0');
            if (value > 0xffffffffULL) {
                LLM_ERR("token id overflow in %s near byte %d\n", path, (int)(token_start - buf));
                return -1;
            }
            p++;
        }
        if (!has_digit) {
            LLM_ERR("bad prompt file %s\n", path);
            return -1;
        }
        req->tokens[req->token_count++] = (uint32)value;
    }

    if (req->token_count <= 0) {
        LLM_ERR("no token ids found in %s\n", path);
        return -1;
    }
    return 0;
}

char *llm_trim_line(char *line) {
    char *start = line;
    char *end;

    if (line == 0) {
        return "";
    }
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') {
        start++;
    }
    end = start + strlen(start);
    while (end > start) {
        char c = end[-1];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            break;
        }
        *--end = '\0';
    }
    return start;
}

int llm_is_exit_command(const char *line) {
    return strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0;
}

static char *skip_ws(char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    return p;
}

static char *next_word(char **cursor) {
    char *p = skip_ws(*cursor);
    if (*p == '\0') {
        *cursor = p;
        return 0;
    }
    char *start = p;
    while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
        p++;
    }
    if (*p != '\0') {
        *p = '\0';
        p++;
    }
    *cursor = p;
    return start;
}

int llm_parse_request_line(char *line, struct llm_request *req) {
    char *cursor = line;
    char *word;
    uint32 value = 0;

    memset(req, 0, sizeof(*req));
    word = next_word(&cursor);
    if (word == 0) {
        return 0;
    }
    if (llm_parse_u32_token(word, &value) < 0 || value == 0) {
        return -1;
    }
    req->predict_count = (int)value;

    while ((word = next_word(&cursor)) != 0) {
        if (req->token_count >= LLM_REQUEST_TOKEN_MAX) {
            return -1;
        }
        if (llm_parse_u32_token(word, &value) < 0) {
            return -1;
        }
        req->tokens[req->token_count++] = value;
    }

    if (req->token_count <= 0) {
        return -1;
    }
    return 1;
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

int llm_format_generated_tokens(const uint32 *seq, int prompt_n, int gen_n, char *out, int out_cap) {
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
