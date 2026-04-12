### 任务 A
1. 
   - 测量 `Model Loading Time`
     - 仅测量模型资产的加载时间：**起点**设在 `llmrun_support.h` 中 `llm_runtime_init` 函数的 `load_model_cfg(&rt->cfg);` 语句之前，**终点** 设在 `llm_runtime_init` 函数 `memset(rt->seq, 0, sizeof(uint32) * (uint64)rt->cfg.runtime_seq_len);` 语句之后
     - 测量端到端初始化时间：把**起点**设在 `llmrun_smol.c` 中 `session_init` 函数的*第一条语句之前*，**终点**设在 `session_init` 函数的*最后一条语句之后*
       - 相比之下，多测量了 `KV cache` 分配和清零、模型工作区内存分配消耗的时间
   - 测量 `TTFT`
     - 把**起点**设在 `llmrun_support.h` 中 `llm_drive_decode` 函数的 `LLM_LOG("starting forward pass\n");` 语句之后，**终点**设在 `for` 循环内 `gen_idx == 0` 的分支（对应第一个 token 生成的循环轮次）
   - 测量 `TPOT`
     - 对于每轮循环，把**起终点**分设在 `llmrun_support.h` 中 `llm_drive_decode` 函数的 `for` 循环内 `int next = token_forward(rt, kcache, vcache, linear_conv_cache, linear_state_cache, pos, ws);` 语句前后；在 `gen_idx == 0` 的否则分支中将单轮循环的计算结果累加到 `tpot_total` 变量中，并*输出单轮循环的 token 生成用时*
     - 在循环结束后，利用 `double tpot = (double)tpot_total / total_count;` 语句，取平均值计算得出 `TPOT`
2. 
   测试数据记录表：

   | 输入长度 | 输出长度 | Model Loading Time (us) | TTFT (us) | TPOT (us) |
   |:--------:|:--------:|:-----------------------:|:---------:|:---------:|
   | 8 | 8 | 2259443 | 17476034 | 2213623 |
   | 8 | 16 | 2232256 | 17190833 | 2123948 |
   | 8 | 32 | 2299628 | 17805992 | 2218257 |
   | 8 | 128 | 2289159 | 16908962 | 2138732 |
   | 16 | 8 | 2294955 | 35047067 | 2226477 |
   | 16 | 16 | 2257970 | 34479389 | 2160836 |
   | 16 | 32 | 2172756 | 33583974 | 2099692 |
   | 32 | 8 | 2237370 | 68155390 | 2124890 |
   | 32 | 16 | 2261376 | 68476008 | 2146982 |
   | 32 | 32 | 2188746 | 67435579 | 2119661 |

   其中 8/16 tokens 输入的测试**直接使用命令行**调用 `llmrun_smol --predict`，32 tokens 输入的测试使用 `llmrun_smol --stdin` 后**在进程中输入具体参数**

   使用的测试命令：

   | 测试编号 | 命令 | 输入 | 输出 |
   |:--------:|------|:----:|:----:|
   | 1 | `llmrun_smol --predict 8 1 2 3 4 5 6 7 8` | 8 | 8 |
   | 2 | `llmrun_smol --predict 16 1 2 3 4 5 6 7 8` | 8 | 16 |
   | 3 | `llmrun_smol --predict 32 1 2 3 4 5 6 7 8` | 8 | 32 |
   | 4 | `llmrun_smol --predict 128 1 2 3 4 5 6 7 8` | 8 | 128 |
   | 5 | `llmrun_smol --predict 8 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16` | 16 | 8 |
   | 6 | `llmrun_smol --predict 16 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16` | 16 | 16 |
   | 7 | `llmrun_smol --predict 32 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16` | 16 | 32 |
   | 8 | `llmrun_smol --predict 8 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10 9 8 7 6 5 4 3 2 1` | 32 | 8 |
   | 9 | `llmrun_smol --predict 16 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10 9 8 7 6 5 4 3 2 1` | 32 | 16 |
   | 10 | `llmrun_smol --predict 32 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10 9 8 7 6 5 4 3 2 1` | 32 | 32 |

3. 
   1. 哪些指标主要受输入长度影响？哪些指标主要受输出长度影响？
     - `TTFT` 主要受输入长度影响，`Model Loading Time` 基本不受输入/输出长度影响；`TPOT` 在输入规模较小时几乎不受输出长度的影响，但**理论上与输出长度成正相关**

   2. 哪些阶段反映系统的计算压力？哪些反映访存（内存带宽）压力？
   - `TTFT` 主要反映计算压力
     - `TTFT` 主要对应 Prefill 阶段的工作，需要对整段输入执行完整的层计算，因此主要受计算吞吐限制
   - TPOT 主要反映访存压力
     - `TPOT` 主要对应 Decode 阶段的工作，随生成 token 数增加，需要从 KV Cache 中读取的数据量逐渐增加，访存压力逐渐增大
     - 在**输出长度规模较小**（百级）时，读取的历史缓存主要取决于固定大小的模型权重，`TPOT` 随生成 token 数增加而增加的趋势极其微小而被每次运行时的其它偶然误差因素掩盖
   - `Model Loading Time` 主要包括从外存向内存写入造成的时间延迟，**对系统存储 I/O 造成的压力较大**，与系统计算压力关系不大

### 任务 B
1. 
   测试数据记录表：
  <table>
    <thead>
      <tr>
        <th rowspan="2">输入长度</th>
        <th rowspan="2">输出长度</th>
        <th colspan="2">Model Loading Time (us)</th>
        <th colspan="2">TTFT (us)</th>
        <th colspan="2">TPOT (us)</th>
      </tr>
      <tr>
        <th>Qwen</th>
        <th>Smol</th>
        <th>Qwen</th>
        <th>Smol</th>
        <th>Qwen</th>
        <th>Smol</th>
      </tr>
    </thead>
    <tbody>
      <tr>
        <td rowspan="3" align="center">4</td>
        <td align="center">4</td>
        <td align="center">12095779</td>
        <td align="center">2192168</td>
        <td align="center">46126192</td>
        <td align="center">8525436</td>
        <td align="center">11428424</td>
        <td align="center">2113945</td>
      </tr>
      <tr>
        <td align="center">8</td>
        <td align="center">12134238</td>
        <td align="center">2183354</td>
        <td align="center">44883160</td>
        <td align="center">8600081</td>
        <td align="center">11161416</td>
        <td align="center">2151572</td>
      </tr>
      <tr>
        <td align="center">16</td>
        <td align="center">11933865</td>
        <td align="center">2172106</td>
        <td align="center">44592737</td>
        <td align="center">8827543</td>
        <td align="center">11078618</td>
        <td align="center">2210261</td>
      </tr>
      <tr>
        <td rowspan="3" align="center">8</td>
        <td align="center">4</td>
        <td align="center">12298940</td>
        <td align="center">2188967</td>
        <td align="center">89551796</td>
        <td align="center">17310485</td>
        <td align="center">11133178</td>
        <td align="center">2311533</td>
      </tr>
      <tr>
        <td align="center">8</td>
        <td align="center">12189074</td>
        <td align="center">2259443</td>
        <td align="center">89423254</td>
        <td align="center">17476034</td>
        <td align="center">11076665</td>
        <td align="center">2213623</td>
      </tr>
      <tr>
        <td align="center">16</td>
        <td align="center">12307713</td>
        <td align="center">2232256</td>
        <td align="center">90469045</td>
        <td align="center">17190833</td>
        <td align="center">11237100</td>
        <td align="center">2123948</td>
      </tr>
      <tr>
        <td rowspan="3" align="center">16</td>
        <td align="center">4</td>
        <td align="center">12090780</td>
        <td align="center">2164712</td>
        <td align="center">176908532</td>
        <td align="center">35681330</td>
        <td align="center">10969766</td>
        <td align="center">2246885</td>
      </tr>
      <tr>
        <td align="center">8</td>
        <td align="center">11852346</td>
        <td align="center">2294955</td>
        <td align="center">180541475</td>
        <td align="center">35047067</td>
        <td align="center">11262723</td>
        <td align="center">2226477</td>
      </tr>
      <tr>
        <td align="center">16</td>
        <td align="center">12227243</td>
        <td align="center">2257970</td>
        <td align="center">178032419</td>
        <td align="center">34479389</td>
        <td align="center">11119402</td>
        <td align="center">2160836</td>
      </tr>
    </tbody>
  </table>

  
- 在 `Model Loading Time` 的表现上，`Qwen3.5-0.8B` 的用时平均约为 `SmolLM2-135M` 的 5.47 倍；
- 在 `TTFT` 的表现上，随着输入的 token 数增加，`Qwen3.5-0.8B` 与 `SmolLM2-135M` 的用时比值**缓慢减小**（$4\,\text{tokens},5.23x\rightarrow 8\,\text{tokens},5.19x\rightarrow 16\,\text{tokens},5.08x$）
  - 大模型的**固定开销**（权重加载、框架调度等）更大，但**计算效率**（矩阵乘法等）相比小模型有所优化

2. `Qwen3.5-0.8B` 和 `SmolLM2-135M` 的部分关键维度差异
<table>
  <thead>
    <tr>
      <th>关键维度</th>
      <th>Qwen3.5-0.8B</th>
      <th>SmolLM2-135M</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td align="center">模型整体参数量</td>
      <td align="center">~800M</td>
      <td align="center">~135M</td>
    </tr>
    <tr>
      <td align="center">Transformer 层数</td>
      <td align="center">24</td>
      <td align="center">30</td>
    </tr>
    <tr>
      <td align="center">隐藏层维度</td>
      <td align="center">1024</td>
      <td align="center">576</td>
    </tr>
    <tr>
      <td align="center">MLP 中间维度</td>
      <td align="center">3584</td>
      <td align="center">1536</td>
    </tr>
    <tr>
      <td align="center">注意力头数</td>
      <td align="center">8</td>
      <td align="center">9</td>
    </tr>
    <tr>
      <td align="center">词表大小</td>
      <td align="center">~248k</td>
      <td align="center">49152</td>
    </tr>
  </tbody>
</table>

- `Model Loading Time` 的影响因素：总参数量
  - `Qwen3.5-0.8B` 的权重是 `SmolLM2-135M` 的约 5.9 倍，即两种模型加载时权重文件**向内存写入的总字节数存在 5.9 倍的关系**
  - 一些其它时间开销主要用于**权重格式解析**、**GPU 显存分配**等，这些开销都与模型总参数量大致成正比
- `TTFT` 的影响因素：前向计算量
  - Self-Attention 层上，`Qwen3.5-0.8B` 的 Q/K/V 投影矩阵为 $1024\times 1024$，而 `SmolLM2-135M` 为 $576\times 576$
  - MLP 层上，`Qwen3.5-0.8B` 的升维/降维矩阵为 $1024\times 3584$，而 `SmolLM2-135M` 为 $576\times 1536$
  - 综合 Transformer 层数上的差异，可知 `Qwen3.5-0.8B` 的总矩阵运算算力需求仍然显著高于 `Qwen3.5-0.8B`
- `TPOT` 的影响因素：显存带宽
  - 在生成后续 token 时，需要将所有权重和历史 KV Cache 从显存读取到计算单元中
  - `Qwen3.5-0.8B` 的隐藏层维度和 MLP 中间维度显著高于 `SmolLM2-135M`，意味着在每个 token 的产生过程中，`Qwen3.5-0.8B` 需要读取的权重切块、激活值规模更大，对显存带宽的压力更显著
