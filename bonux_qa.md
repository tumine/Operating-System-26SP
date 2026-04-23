# Prefix Cache 分析

## 1. 你具体缓存了什么状态

`daemon_prefix_cache` 结构体（第13-27行）缓存了以下状态：

| 字段 | 说明 |
|------|------|
| `model_kind` | 模型类型（仅支持 Smol） |
| `cached_prefix_len` | 前缀 token 数量 |
| `cached_tokens` | 前缀 token 序列（uint32 数组） |
| `cached_kcache` | K 缓存快照（float 数组） |
| `cached_vcache` | V 缓存快照（float 数组） |

**关键点**：缓存的是 **KV Cache 激活状态**，而非模型权重。`prefix_cache_save_after_prefill`（第258-291行）在 prefill 完成后，将每层的 K/V 激活值完整保存下来。

---

## 2. 你在什么条件下允许恢复缓存

`prefix_cache_should_bypass`（第203-217行）会在以下条件时**跳过缓存**（bypass）：

1. **模型不支持**：不是 Smol 模型，或缓存内存未分配
2. ** token 数太少**：`token_count <= 1`（只有1个 token 时无需复用）
3. ** token 序列过长**：`token_count > token_capacity`（超出缓存容量）

`prefix_cache_try_restore`（第219-256行）恢复缓存的条件：

1. **前缀匹配**：通过 `prefix_cache_prefix_match_len` 检查当前 prompt 与缓存的前缀 token 是否一致
2. **匹配长度 > 0**：至少要有部分匹配才能复用
3. **匹配长度约束**：`reuse_len <= token_count - 1`（最后一个 prompt token 不复用，避免需要额外保存 logits）

恢复流程（第244-249行）：
- 把缓存的 K/V slice 复制回运行时 kcache/vcache
- 清除 reuse_len 之后的缓存内容（防止脏数据）

---

## 3. 为什么 prefix cache 更像"减少重复 prefill"的优化

**Prefill 阶段的核心开销**：对输入 prompt 的每个 token 依次执行 self-attention 和 FFN 层，填充 KV Cache。

**Prefix Cache 的工作方式**：

```
首次请求：prompt [A B C D] → 完整执行 prefill（4步）→ 保存 KV Cache
再次请求：prompt [A B C X] → 发现前缀匹配 [A B C] → 从第4步恢复，跳过前3步 prefill
```

**为什么不是"减少 decode"而是"减少 prefill"**：

1. **缓存时机**：`prefix_cache_save_after_prefill`（第258行）在 prefill 结束后立即保存，此时 decode 还未开始。注释（第287-290行）解释：如果等 decode 结束后再保存，cache 已被生成阶段的输出污染，需要先拷贝再清零。

2. **恢复位置**：恢复后从 `reuse_len` 位置继续执行（第252行，`resume_pos_out = reuse_len`），这意味着**跳过了已匹配前缀的 prefill 步骤**。

3. **缓存内容**：保存的是 `prompt_n - 1` 个 token 的 KV 状态（第261行），正好是 prefill 阶段产生的激活值，而非 decode 阶段的隐状态。

4. **统计指标**：代码中有 `saves`（保存次数）和 `restores`（恢复次数）跟踪缓存操作，专门衡量 prefill 复用的效果。

**本质**：这是一个 **KV Cache 快照复用**机制，类似于操作系统中的磁盘缓存——把已经计算过的 KV 激活直接恢复，避免对相同前缀重复做矩阵运算和 attention 计算。