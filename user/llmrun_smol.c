#include "llmrun_support.h"

static int token_forward(
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

static void usage(const char *prog) {
    fprintf(2, "usage: %s [--asset-dir DIR] [--predict N] token0 token1 ...\n", prog);
    fprintf(2, "       %s [--asset-dir DIR] [--predict N] --prompt-file FILE\n", prog);
    fprintf(2, "       %s [--asset-dir DIR] --stdin\n", prog);
    fprintf(2, "prompt file format: whitespace-separated token ids\n");
    fprintf(2, "stdin service format: <predict> <token0> <token1> ...\n");
    fprintf(2, "interactive mode: type exit or quit to return to the shell\n");
}

static int parse_predict_arg(const char *text) {
    uint32 value = 0;
    if (llm_parse_u32_token(text, &value) < 0 || value == 0) {
        return -1;
    }
    if (value > 0x7fffffffU) {
        return -1;
    }
    return (int)value;
}

static void session_init(struct llm_session *sess, const char *asset_dir) {
    memset(sess, 0, sizeof(*sess));
    // Hint for students:
    // - If you want end-to-end startup/load time for this program, take the
    //   start timestamp at the beginning of session_init().
    // - A natural end timestamp is after llm_runtime_init() and the cache /
    //   workspace allocations below, when sess is fully ready to serve decode.
    if (chdir(asset_dir) < 0) {
        LLM_ERR("failed to chdir to %s\n", asset_dir);
        exit(1);
    }

    llm_runtime_init(&sess->rt, MODEL_KIND_SMOL, 0);
    sess->kv_cache_elems = llm_kv_cache_elems(&sess->rt.cfg);
    llm_alloc_kv_caches(&sess->rt.cfg, &sess->kcache, &sess->vcache);
    llm_workspace_init_full(&sess->rt.cfg, &sess->ws);
    // Hint for students: this is a natural stop point for end-to-end load time.
}

static void parse_cli_tokens(int argc, char *argv[], int argi, struct llm_request *req, int predict_count) {
    memset(req, 0, sizeof(*req));
    req->predict_count = predict_count;
    for (int i = argi; i < argc; i++) {
        uint32 value = 0;
        if (req->token_count >= LLM_REQUEST_TOKEN_MAX || llm_parse_u32_token(argv[i], &value) < 0) {
            LLM_ERR("invalid token id %s\n", argv[i]);
            exit(1);
        }
        req->tokens[req->token_count++] = value;
    }
    if (req->token_count <= 0) {
        LLM_ERR("no token ids supplied\n");
        exit(1);
    }
}

static void run_once(struct llm_session *sess, const struct llm_request *req) {
    char result[LLM_RESULT_TEXT_MAX];
    memset(result, 0, sizeof(result));
    if (llm_session_run(sess, req, token_forward, result, sizeof(result)) < 0) {
        LLM_ERR("request failed\n");
        exit(1);
    }
    printf("%s\n", result);
}

static void run_stdin_service(struct llm_session *sess) {
    char line[LLM_REQUEST_LINE_MAX];
    struct llm_request req;

    LLM_LOG("stdin service ready; format: <predict> <token0> <token1> ...; type exit or quit to return to sh\n");
    for (;;) {
        memset(line, 0, sizeof(line));
        gets(line, sizeof(line));
        if (line[0] == '\0') {
            return;
        }
        char *cmd = llm_trim_line(line);
        if (cmd[0] == '\0') {
            continue;
        }
        if (llm_is_exit_command(cmd)) {
            LLM_LOG("leaving stdin service\n");
            return;
        }
        int parsed = llm_parse_request_line(cmd, &req);
        if (parsed == 0) {
            continue;
        }
        if (parsed < 0) {
            LLM_ERR("bad request line; expected: <predict> <token0> <token1> ...\n");
            continue;
        }

        char result[LLM_RESULT_TEXT_MAX];
        memset(result, 0, sizeof(result));
        if (llm_session_run(sess, &req, token_forward, result, sizeof(result)) < 0) {
            LLM_ERR("request failed\n");
            continue;
        }
        printf("%s\n", result);
    }
}

int main(int argc, char *argv[]) {
    llm_set_prog_name_from_argv(argc > 0 ? argv[0] : 0, "llmrun_smol");

    const char *asset_dir = "/AI/SMOL";
    const char *prompt_file = 0;
    int predict_count = 1;
    int force_stdin = 0;
    int argi = 1;

    while (argi < argc) {
        if (strcmp(argv[argi], "--asset-dir") == 0) {
            if (argi + 1 >= argc) {
                usage(argv[0]);
                exit(1);
            }
            asset_dir = argv[argi + 1];
            argi += 2;
            continue;
        }
        if (strcmp(argv[argi], "--predict") == 0) {
            if (argi + 1 >= argc) {
                usage(argv[0]);
                exit(1);
            }
            predict_count = parse_predict_arg(argv[argi + 1]);
            if (predict_count <= 0) {
                LLM_ERR("bad --predict value %s\n", argv[argi + 1]);
                exit(1);
            }
            argi += 2;
            continue;
        }
        if (strcmp(argv[argi], "--prompt-file") == 0) {
            if (argi + 1 >= argc) {
                usage(argv[0]);
                exit(1);
            }
            prompt_file = argv[argi + 1];
            argi += 2;
            continue;
        }
        if (strcmp(argv[argi], "--stdin") == 0) {
            force_stdin = 1;
            argi++;
            continue;
        }
        if (strcmp(argv[argi], "--help") == 0) {
            usage(argv[0]);
            exit(0);
        }
        break;
    }

    struct llm_request req;
    int have_request = 0;
    if (force_stdin && prompt_file != 0) {
        LLM_ERR("--stdin cannot be combined with --prompt-file\n");
        exit(1);
    }
    if (prompt_file != 0 && argi < argc) {
        LLM_ERR("cannot mix --prompt-file with inline token ids\n");
        exit(1);
    }
    if (!force_stdin) {
        if (prompt_file != 0) {
            if (llm_load_prompt_file(prompt_file, &req, predict_count) < 0) {
                exit(1);
            }
            have_request = 1;
        } else if (argi < argc) {
            parse_cli_tokens(argc, argv, argi, &req, predict_count);
            have_request = 1;
        }
    }

    struct llm_session sess;
    session_init(&sess, asset_dir);

    if (have_request) {
        run_once(&sess, &req);
    } else {
        run_stdin_service(&sess);
    }
    exit(0);
}
