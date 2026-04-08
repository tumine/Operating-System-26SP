### 任务 A
1. 
   - 测量模型资产装入内存的时间
     - 仅测量模型资产的加载时间：**起点**设在 `llmrun_support.h` 中 `llm_runtime_init` 函数的 `load_model_cfg(&rt->cfg);` 语句之前，**终点** 设在 `llm_runtime_init` 函数 `memset(rt->seq, 0, sizeof(uint32) * (uint64)rt->cfg.runtime_seq_len);` 语句之后
     - 测量端到端初始化时间：把**起点**设在 `llmrun_smol.c` 中 `session_init` 函数的*第一条语句之前*，**终点**设在 `session_init` 函数的*最后一条语句之后*
       - 相比之下，多测量了 `KV cache` 分配和清零、模型工作区内存分配消耗的时间
   - 测量 TTFT
     - 把**起点**设在 `llmrun_support.h` 中 `llm_drive_decode` 函数的 `LLM_LOG("starting forward pass\n");` 语句之后，**终点**设在 `for` 循环内 `gen_idx == 0` 的分支（对应第一个 token 生成的循环轮次）
   - 测量 TPOT
     - 对于每轮循环，把**起终点**分设在 `llmrun_support.h` 中 `llm_drive_decode` 函数的 `for` 循环内 `int next = token_forward(rt, kcache, vcache, linear_conv_cache, linear_state_cache, pos, ws);` 语句前后；在 `gen_idx == 0` 的否则分支中将单轮循环的计算结果累加到 `tpot_total` 变量中，并*输出单轮循环的 token 生成用时*
     - 在循环结束后，利用 `double tpot = (double)tpot_total / total_count;` 语句，取平均值计算得出 TPOT
2. 