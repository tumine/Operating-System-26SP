# **端侧大模型推理的存储与计算协同优化方案及 NexOS 系统实现蓝图**

## **引言**

在边缘设备和端侧硬件上部署大型语言模型（Large Language Models, LLMs）正面临着严峻的系统工程挑战。这一挑战的核心在于“内存容量墙”与“存储 I/O 瓶颈”的交织。现代大模型的参数量和推理时产生的键值缓存（Key-Value Cache, KV Cache）通常远超端侧商品化硬件所能提供的动态随机存取存储器（DRAM）容量，迫使系统必须将模型权重和状态数据卸载至次级存储介质，例如 NAND 闪存、eMMC 或 NVMe 固态硬盘（SSD）1。然而，DRAM 与闪存之间存在巨大的带宽与延迟鸿沟，导致模型在推理（特别是自回归解码阶段）时，计算单元（CPU 或 NPU）经常处于长时间的饥饿等待状态，严重拖慢了首 token 延迟（Time to First Token, TTFT）和 token 间生成速度 4。  
为了在受限的内存预算下最大化系统吞吐率并平衡 CPU 与 I/O 的利用率，业界正积极探索多维度的底层优化策略。本报告旨在针对端侧大模型部署的核心诉求，深入调研 arXiv 上相关领域的前沿研究，系统性评估三大主流优化思路：权重复用与多请求批处理、基于直接内存访问（DMA）与多缓冲区的计算 I/O 重叠技术，以及绕过操作系统文件系统缓存的 Raw I/O（Direct I/O）加载机制。在此理论基础上，本报告将结合 NexOS 微内核操作系统的架构特性，提出一个自底向上的系统实现蓝图。该蓝图将详细探讨 AI 服务进程、内存管理、文件系统、请求调度与状态管理等模块的协同重构方案，定义必要的底层数据结构与系统调用接口，并剖析一次推理请求的完整协同工作流，最后对当前 NexOS 可模拟的系统特性及受限于物理硬件的工程限制进行深入的边界分析。

## **第一部分：基于前沿研究的端侧 LLM 优化方案深度解析**

端侧大模型推理优化的本质，是在有限的 DRAM 空间内，通过算法层面的稀疏性预测、系统层面的内存流转以及底层硬件的数据通道复用，尽可能地减少无效数据的搬运，并隐藏不可避免的 I/O 延迟。通过对近期 arXiv 核心文献的调研，可以将其归纳为以下三个核心维度的技术演进。

### **1\. 权重复用、上下文稀疏性与 KV Cache 换页机制**

最直接缓解 I/O 瓶颈的方法是从根本上减少每次前向传播（Forward Pass）所需从闪存加载至 DRAM 的数据量。苹果公司提出的 "LLM in a flash" 研究指出，大型语言模型在推理时表现出极高的上下文稀疏性（Contextual Sparsity），即在给定的输入下，模型中（尤其是前馈神经网络 FFN 层）只有一小部分神经元会被强烈激活，其余大部分神经元的输出在经过 ReLU 或 SiLU 等激活函数后会归零 1。  
利用这一特性，系统可以在计算完整的矩阵乘法之前，通过低秩矩阵或预测器提前判断哪些神经元会被激活，从而直接跳过对未激活权重行的加载 5。为了将这一算法特性转化为系统吞吐量，研究引入了“滑动窗口”（Sliding Window）内存管理技术。该技术要求在 DRAM 中维护一个局部缓存，仅保留最近几个输入 token 预测所需的权重行 1。随着推理过程推进到新的 token，运行时系统执行增量加载，仅从闪存中读取当前 token 所需且未命中缓存的神经元数据，数学上表现为持续递减的数据增量趋势，从而高效回收那些已滑出窗口的 token 所占用的缓存空间 6。同时，为弥补稀疏近似带来的精度损失，ActiveFlow 等框架引入了稀疏感知自蒸馏技术，在数学层面动态调整活跃权重，以对齐密集模型的输出分布 7。  
在多轮对话和长文本推理中，KV Cache 是另一个吞吐量杀手。它随着上下文长度和并发批处理大小（Batch Size）线性增长，极易耗尽端侧设备的 DRAM 2。为了支持多请求批处理（Continuous Batching）并复用权重，必须对 KV Cache 实施磁盘换页（Offloading）。KVSwap 等框架通过将完整的 KV Cache 存储在 SSD 上，并在内存中保留高度压缩的元数据，基于注意力分数预测哪些 KV 缓存块对当前生成步骤至关重要，进而实施精准的分组预取 3。这种设计使得推理引擎能够在内存阈值被触发时，依据调度策略动态地将冷请求的 KV 数据换出，以便在有限内存中交错处理多个请求，最大化权重复用的收益 11。

### **2\. 利用 DMA 与多缓冲区实现 I/O 与计算的深度重叠**

如果说权重复用是做减法，那么 I/O 与计算的重叠（Overlap）则是做乘法。在传统的顺序执行流中，张量并行与文件读取是串行的，导致 I/O 阶段计算资源闲置，计算阶段 I/O 带宽浪费 13。为了最大化吞吐率，系统架构必须向异步、流水线化的执行模型转型，其核心依赖于双缓冲（Double-buffering）或循环队列（Ring Buffer）以及 DMA 技术。  
双缓冲流水线的核心数学原则是确保从磁盘提取权重和激活值的时间完全被当前数学运算所需的时间所掩盖。系统执行流水线必须满足特定的时间不等式，即下一层的内存加载时间必须小于或等于当前层的计算时间 14。在这种条件下，单个处理步骤的总耗时趋近于两者中的最大值，从而在宏观上隐匿了底层 SSD 的读取延迟 8。  
为实现这一目标，ActiveFlow 提出了“活跃权重 DRAM-Flash 交换流水线”，该流水线将热权重缓存、预加载的活跃权重和当前正在计算的权重在 DRAM 中进行动态空间编排 7。当 CPU/NPU 计算第 ![][image1] 层时，系统已提前将后续 ![][image2] 个网络层（被称为“层组” Layer Group）的活跃权重异步预取至 DRAM 中 7。为支撑这种高吞吐的数据流转，必须广泛使用“锁页内存”（Pinned Memory / Page-locked Memory）作为暂存区 18。锁页内存允许 DMA 控制器在 SSD 与 DRAM 之间直接执行大块连续的数据搬运，无需 CPU 介入，并且彻底规避了传输中途发生虚拟地址到物理地址转换缺页中断的致命风险 18。  
此外，"LLM in a flash" 引入的“行列捆绑”（Row-Column Bundling）数据布局优化进一步放大了 DMA 的效能。通过在磁盘上将 FFN 层中向上投影层（Up-projection）的列与向下投影层（Down-projection）的行物理拼接，系统可以在单次 DMA 传输中读取原来两倍大小的连续数据块，彻底消除随机寻道的延迟惩罚，充分榨干闪存的顺序读取带宽 1。

### **3\. 采用 Raw I/O 绕过文件系统缓存机制**

在 Linux 等传统操作系统中，标准的文件 I/O 严重依赖内核页缓存（Page Cache）。当应用程序读取模型权重时，操作系统首先将数据从磁盘 DMA 至内核空间的页缓存，随后由 CPU 将数据二次拷贝至用户空间的缓冲区。这种机制对端侧大模型推理构成了三重致命威胁。首先，页缓存会占用大量 DRAM，实际上将大模型可用的物理内存减半 20；其次，页缓存的 LRU（最近最少使用）驱逐策略完全无法感知 LLM 的计算图逻辑，可能会随机换出下一层紧接着就要使用的权重 21；最后，内核到用户态的内存拷贝消耗了本就紧缺的 CPU 周期，并严重污染了 CPU 的 L1/L2 缓存 21。  
尽管业界常用内存映射（mmap）来避免内核到用户态的内存拷贝并实现零拷贝（Zero-copy），但 mmap 仍然与页缓存深度绑定，且其本质是同步阻塞的缺页中断机制（Page Fault）21。当推理引擎访问未驻留在 DRAM 中的映射地址时，会触发缺页异常，迫使当前计算线程挂起，等待磁盘 I/O 完成 21。这会彻底破坏前文所述的计算与 I/O 重叠流水线。  
因此，唯一能够彻底释放硬件潜能的路径是采用 Raw I/O，在 POSIX 系统中通过 O\_DIRECT 标志位实现直接 I/O 25。直接 I/O 彻底旁路了操作系统的页缓存机制，允许底层的 NVMe 或存储控制器通过 DMA 将数据直接灌入用户态应用程序分配的缓冲区中 24。通过剥离内核缓冲，存储介质实际上被降维成了一层速度较慢但行为完全确定的外设内存。AI 运行时借此获得了对内存分配和预取调度的绝对统治权。结合聚合的单文件模型布局（减少元数据查找与 inode 遍历开销），直接 I/O 能够确保后台异步线程维持持续的高带宽顺序读取，同时前台计算线程保持满载运行 24。

## **第二部分：对现有优化想法落地的启示与综合评估**

用户提出的三大优化想法（权重复用与多请求批处理、DMA 多缓冲区重叠、Raw I/O 绕过文件系统）不仅在理论上自洽，且已被学术界的最新研究（如 LLM in a flash, ActiveFlow, KVSwap 等）所充分验证。这三者并非孤立的技术点，而必须作为一个紧密耦合的系统级解决方案来落地。  
这些研究对现有想法落地的最大启示在于“感知”与“确定性”。单纯的 DMA 或 Raw I/O 只是提供了管道，真正的性能飞跃来源于“模型结构感知”（例如，行列捆绑的物理布局改造 1）和“状态确定性”（基于注意力分数精确决定 KV Cache 的去留 3）。同时，研究也暴露了在端侧落地时必须直面的硬件制约。首先，频繁的 KV Cache 动态换页虽然逻辑上可行，但会引发严重的写放大（Write Amplification）效应，在端侧 SSD 的有限擦写寿命下，持续的并发换页可能会迅速损毁存储介质 12。其次，端侧设备通常采用统一内存架构（UMA），CPU、NPU 和 I/O 控制器共享同一根物理内存总线 1。即使使用了锁页内存和 DMA，当后台 DMA 大量写入数据时，可能会与前台 NPU 提取权重发生严重的内存总线争用（Bus Contention），导致预期中的流水线计算耗时 ![][image3] 被拉长 12。因此，在落地这三大想法时，系统调度器不能仅仅是一个简单的生产者-消费者队列，而必须是一个具备带宽感知能力的自适应调度引擎。

## **第三部分：基于 NexOS 微内核架构的系统实现蓝图**

要将上述复杂的 I/O 绕过、精准内存控制与异步流水线整合到一个连贯的系统中，传统的宏内核（Monolithic Kernel）往往因为抽象层级过高、内核态侵入过深而显得笨重。NexOS（及其演进版本 NexNix/NexKe）作为一个现代的多服务器微内核操作系统，为这种极限控制提供了极佳的架构土壤 29。  
NexOS 的核心哲学是保持内核的极简。内核仅负责线程调度、硬件中断响应和高效的进程间通信（IPC） 30。驱动程序、虚拟内存管理（VMM）和虚拟文件系统（VFS）均作为独立的用户态服务器（User-space Servers）运行 30。在 NexOS 中，IPC 的实现极为高效，通常采用同步消息传递机制，辅以快速的内存映射。当一个线程发送消息时，内核会获取消息数据的物理地址，随后直接修改目标线程的页目录项（PDEs）和页表项（PTEs），将数据映射到目标进程预先定义的虚拟内存区域（例如 0x3000 至 0x300000）32。内核随后临时提升接收线程的优先级，触发立即的上下文切换，使得服务能以极低延迟处理消息 32。这种高度模块化且依赖精准页表操控的系统，正是实现端侧 LLM 零拷贝传输的理想平台。  
为了在 NexOS 上落地三大优化，我们需要对系统的核心模块进行深度重构，引入专门的 AI 服务守护进程，并打通从物理存储到 NPU 寄存器的数据通路。

## **第四部分：核心模块改造与系统调用接口设计**

在本系统实现思路中，将重点涉及五个核心模块的协同设计：新增的用户态 AI Service、修改的虚拟内存管理（VMM）服务器、修改的文件系统（FS）服务器、请求调度器以及状态管理模块。

### **1\. 模块改造说明**

**AI Service (新增的用户态守护进程):**  
这是整个推理框架的大脑。作为独立的 NexOS 服务器进程，它接收来自前端应用程序的推理请求，扮演生产者-消费者模型中的“调度中心”与“消费者”。它内部集成了请求调度器与状态管理模块，负责解析 Prompt、执行滑动窗口算法进行稀疏性预测、生成需要加载的权重块元数据，并向 VMM 申请锁页内存，最后向 FS 发起异步的 Raw I/O 请求。  
**虚拟内存管理服务器 (VMM Server):** 当前的微内核 VMM 需进行关键能力扩充，以支持 DMA 传输的物理一致性要求。必须向其添加锁定物理内存页的机制，防止内核在内存紧张时将其换出到交换区（Swap）。 *新增功能：* 提供物理地址连续且按页边界对齐（Page-aligned）的内存分配池，专供 AI Service 的循环队列使用 25。  
**文件系统服务器 (FS Server):** 在 NexOS 中，文件系统运行在用户态。为了支持 Raw I/O，必须在 VFS 层建立一条绕过所有内部缓冲（如块缓存或逻辑页缓存）的旁路通道 24。 *新增功能：* 接收来自 AI Service 携带物理地址的读写指令，将其直接翻译为底层存储控制器（如 NVMe 队列）的 LBA（逻辑块寻址）命令，启动 DMA 引擎，实现数据直接灌入 AI Service 申请的锁页内存中。  
**请求调度器与状态管理 (内置于 AI Service):** 针对多请求并发与 KV Cache 换页，引入基于状态机的调度器。它需要追踪每个请求当前是处于 Prefill（预填充）、Decode（解码）、Suspended（挂起/换出）还是 Evicted（驱逐）状态。当系统 DRAM 达到高水位线时，状态管理器根据每个请求的活跃度（如生成的 token 数或等待时间）触发 KV Cache 的落盘（Offload）决策 11。

### **2\. 关键数据结构设计**

为了实现模块间的高效协同，需要在系统层面定义以下新型数据结构，以在 IPC 消息和模块内存间流转。

| 数据结构名称 | 归属模块 | 核心用途与字段描述 |
| :---- | :---- | :---- |
| LayerGroupSpec | AI Service | **用途**：定义预加载网络层组的元数据，基于稀疏性预测结果指导 I/O 批量读取 7。 **字段**：layer\_start\_idx, layer\_end\_idx, active\_channels\_bitmap (标记哪些行列捆绑块需要加载), total\_bytes。 |
| RingBufferIO | AI Service / VMM | **用途**：一个无锁的双缓冲循环队列结构，承载已锁页的物理内存，作为 DMA 直接写入的目标 15。 **字段**：head\_ptr (消费者读取偏移), tail\_ptr (生产者写入偏移), pinned\_mem\_handle (VMM 分配的物理页句柄), buffer\_size, atomic\_status\_flag。 |
| KVSwapMeta | 状态管理器 | **用途**：高度压缩的内存元数据字典，追踪并预测 KV 缓存条目的访问频率，指导 SSD 换入换出 3。 **字段**：seq\_id, layer\_idx, attention\_weight\_moving\_avg, disk\_lba\_offset (所在磁盘的绝对扇区偏移), in\_ram\_flag。 |
| DirectIOBlock | FS Server / IPC | **用途**：封装直接 I/O 请求的底层参数，用于在 AI Service 和 FS Server 间进行零拷贝 IPC 传递 25。 **字段**：target\_paddr (目标锁页内存物理地址), block\_device\_lba, transfer\_size (必须为页大小整数倍), completion\_semaphore\_id。 |
| ReqStateNode | 请求调度器 | **用途**：表示单个推理请求的生命周期与执行上下文，用于批处理调度 9。 **字段**：request\_id, current\_state (PREFILL/DECODE/SWAP\_OUT/SWAP\_IN), kv\_meta\_ptr, priority\_score。 |

### **3\. 系统调用与 IPC 接口层修改**

为支撑上述架构，NexOS 的内核 API 和系统服务器接口需扩展以下功能：

1. **sys\_alloc\_pinned(size\_t size, uint32\_t alignment)**：向 VMM Server 发送的 IPC 请求。VMM 在物理内存池中寻找连续且对齐的页面，在页表中标记为不可换出（Non-pageable），并将对应的虚拟地址与物理地址（以便后续告知 DMA）返回给 AI Service 18。  
2. **sys\_async\_direct\_read(fd, paddr, lba\_offset, size, sem\_id)**：向 FS Server 发送的 IPC 异步读请求。要求严格绕过 FS 的缓存层。FS 收到后将 paddr 和 lba\_offset 组装为 DMA 描述符下发给存储硬件。读操作不阻塞，AI Service 可以继续执行其他计算；当 DMA 完成触发硬件中断后，内核通过信号量 sem\_id 唤醒或通知 AI Service 对应的 I/O 线程 24。  
3. **sys\_async\_direct\_write(...)**：对应的异步写请求接口，主要用于当 DRAM 告急时，状态管理器快速将选定的冷请求的 KV Cache（由 KVSwapMeta 索引）刷写到 SSD 上 3。

## **第五部分：一次推理请求的协同工作流程全景剖析**

结合上述架构改造，当用户端应用发起一次大模型文本生成请求时，NexOS 内核与各服务模块的完整协同工作流如下：  
**阶段 1：请求接入与上下文初始化（调度器与状态管理）**

1. 用户应用程序通过 NexOS 的同步 IPC 机制将 Prompt 文本发送至 **AI Service**。内核捕获该消息，将文本数据所在的物理页直接映射至 AI Service 的 0x3000 \- 0x300000 消息处理区，并提升 AI Service 的线程优先级以立即响应 32。  
2. AI Service 的 **请求调度器** 接收消息，为其创建 ReqStateNode，状态初始化为 PREFILL。  
3. 调度器检查当前的内存水位线。如果物理内存不足以分配新的 KV Cache，调度器查阅 KVSwapMeta，选中当前优先级最低的休眠请求，构造 DirectIOBlock 写请求发送给 **FS Server**，通过 Raw I/O 方式将其 KV 缓存剥离至 SSD 11。  
4. 随后，AI Service 向 **VMM Server** 调用 sys\_alloc\_pinned，分配用于本次请求的页对齐 RingBufferIO 25。

**阶段 2：预填充与流水线启动（生产者启动）**

1. 对于第一个 Token 的处理，AI Service 的预测模块基于 Prompt 计算出起始层的活跃神经元位图，构建第一批次的 LayerGroupSpec 7。  
2. AI Service 的后台 I/O 线程根据 LayerGroupSpec 的行列捆绑布局，将一系列绝对 LBA 扇区偏移与 RingBufferIO 前半部的物理地址组装成 DirectIOBlock 请求，通过 IPC 发送至 **FS Server** 1。  
3. FS Server 驱动存储控制器启动 DMA，将权重数据零拷贝直接刷入 RingBufferIO 的锁页内存中。

**阶段 3：计算与 I/O 深度重叠（消费者运行）**

1. 当第一批次权重 DMA 传输完毕，底层硬件中断触发，内核释放相关信号量，唤醒 AI Service 的计算线程。  
2. **计算线程（消费者）**：指令 CPU 或 NPU 读取 RingBufferIO 前半部的数据，结合用户的 Prompt 张量，开始执行网络层的矩阵乘法（如 Attention 和 FFN 运算）。在计算过程中，生成的中间 KV 状态被直接写入分配好的物理内存中 15。  
3. **I/O 线程（生产者）**：与此同时，AI Service 计算下一层组的稀疏性，生成新的 LayerGroupSpec，并立即向 FS Server 发送针对 RingBufferIO 后半部的 sys\_async\_direct\_read 请求 7。  
4. 只要不等式 ![][image4] 成立，前台的计算与后台的磁盘 DMA 将在无锁循环队列中交替向前滚动，彻底隐藏了 NVMe 闪存的读取延迟 14。

**阶段 4：状态持久化与输出反馈**

1. 一次 Forward Pass 结束，生成了新的 Token。状态管理器更新该请求的 KVSwapMeta 字典，记录新增的上下文 Attention 分数，并按照滑动窗口机制，废弃掉 DRAM 中超出窗口大小的历史权重数据，回收页框 6。  
2. AI Service 再次通过 IPC，将生成的字符回传给用户态应用程序的接收空间 32。随后请求状态切入 DECODE 循环，直至满足停止条件或被调度器主动换出。

## **第六部分：模拟实现边界与物理硬件限制分析**

上述架构在理论上形成了完美的闭环，然而在实际落地开发中，由于工程进度与底层硬件环境的制约，我们必须明确哪些部分可以在当前 NexOS 环境中被软件模拟验证，哪些部分受限于硬件仅能停留在设计与纸面探讨阶段。

### **1\. 当前 NexOS 可完全模拟与实现的部分**

当前版本的 NexOS / NexNix 已具备完善的微内核进程调度机制和 POSIX 用户态兼容性，这为软件层面的流程跑通提供了基础 29。

* **IPC 消息传递与优先级调度**：AI Service 作为独立进程接收用户请求，内核修改 PTE/PDE 实现共享内存区域（0x3000 \- 0x300000）的数据投递，以及优先级提升机制，这部分是 NexOS 的原生强项，完全可以直接利用现有内核代码实现并进行压测 32。  
* **用户态内存管理与状态机**：VMM Server 作为用户态进程，通过拓展其代码域来模拟 sys\_alloc\_pinned 的逻辑是可行的。即便底层实际上可能并未真正锁定物理引脚，但在软件逻辑上，我们可以通过分配特定的虚拟内存块并确保其地址页对齐（如借助 posix\_memalign）来模拟循环队列 RingBufferIO 的运作 25。  
* **调度器与异步逻辑验证**：AI Service 内的多线程调度、生产者-消费者逻辑、基于 LayerGroupSpec 的滑动窗口预测算法，以及状态管理器如何依据内存容量阈值标记 ReqStateNode 的流转，这些纯软件维度的业务逻辑可以完全在现有的 NexOS 用户空间编译并执行验证。  
* **Raw I/O 模拟**：FS Server 可以在软件层面上强制实施直接 I/O 的各项约束条件，例如拒绝处理任何未遵循页大小整数倍或地址不对齐的读写请求，从而迫使上层 AI 运行时按真实的 DMA 要求进行数据对齐和重组 25。

### **2\. 受限于硬件环境只能作为设计探讨的部分**

脱离了真实的端侧物理拓扑体系，部分最核心的性能指标和底层行为无法在虚拟机（如 QEMU）或通用 x86/ARM 平台上得到准确验证。

* **真正的零拷贝与 DMA 并发验证**：若硬件模拟器未提供真实的 PCIe/NVMe 存储控制器直通（Passthrough），FS Server 下发的 Raw I/O 指令最终仍会被宿主机（Host OS）的内核缓存截获 12。在此环境下，虽然软件路径绕过了 NexOS 的缓存，但无法真实测得零拷贝带来的 CPU 利用率下降和 I/O 吞吐提升，因为宿主系统仍在默默进行内存拷贝。  
* **内存总线竞争与 ![][image4] 临界点**：在真实的端侧 SoC（片上系统）中，UMA（统一内存架构）意味着 NPU 读写显存和 NVMe 控制器 DMA 写入内存共享同一条物理总线 1。在双缓冲重叠计算时，DMA 突发写入极有可能引发总线仲裁延迟，导致 NPU 访存阻塞 12。这种底层的总线竞争（Bus Contention）效应极其难以在软件层面模拟，因此理论上的时间隐藏模型在实际部署时可能需要打折扣。  
* **硬件计算能力（NPU Tensor Cores）**：由于当前 NexOS 尚未开发针对现代端侧 NPU（如 Apple Neural Engine 或 Qualcomm Hexagon DSP）的驱动程序，矩阵乘法只能在 CPU 上缓慢进行。这将导致 ![][image3] 异常庞大，轻易覆盖了 ![][image5]，使得“计算掩盖 I/O”的设计在纯 CPU 模拟环境下显得毫无意义 14。  
* **SSD 寿命衰减与写放大评估**：KVSwap 所依赖的大规模动态 KV 缓存换页，将对闪存颗粒造成巨大的擦写压力 12。纯软件模拟无法观测到存储介质的 FTL（闪存转换层）垃圾回收机制导致的随机写入延迟飙升，更无法在短时间内评估写入放大对硬盘寿命的物理损伤风险 12。

## **结论**

在端侧有限资源内最大化大模型推理吞吐率，绝非单一算法的突破可以解决，而是一项横跨算法稀疏性预测、操作系统内存编排与底层总线调度的系统工程。通过深度结合“权重复用与滑动窗口”、“多缓冲区 DMA 计算重叠”以及“直接 I/O 旁路文件系统”三大前沿优化理念，我们可以在理论上有效粉碎端侧设备的内存容量墙与带宽瓶颈。将这一复杂系统下沉至 NexOS 微内核架构中，能够充分利用其用户态服务的高度模块化、内核精简特性以及高效的内存映射 IPC 机制，从而赋予 AI 推理运行时前所未有的底层硬件统治力。尽管当前的软件模拟环境与真实的物理芯片拓扑之间仍存在难以逾越的测试鸿沟，但这一基于状态机驱动、Raw I/O 接管与双缓冲流水线的系统实现蓝图，已为下一代端侧智能操作系统的研发提供了逻辑严密、架构清晰的演进方向。

#### **引用的著作**

1. LLM in a flash: Efficient Large Language Model Inference with Limited Memory \- arXiv, 访问时间为 七月 1, 2026， [https://arxiv.org/html/2312.11514v2](https://arxiv.org/html/2312.11514v2)  
2. KVSwap: Disk-aware KV Cache Offloading for Long-Context On-device Inference, 访问时间为 七月 1, 2026， [https://eprints.whiterose.ac.uk/id/eprint/240121/1/kvswap.pdf](https://eprints.whiterose.ac.uk/id/eprint/240121/1/kvswap.pdf)  
3. KVSwap: Disk-aware KV Cache Offloading for Long-Context On-device Inference \- arXiv, 访问时间为 七月 1, 2026， [https://arxiv.org/html/2511.11907v1](https://arxiv.org/html/2511.11907v1)  
4. LLM in a flash: Efficient Large Language Model Inference with Limited Memory \- arXiv, 访问时间为 七月 1, 2026， [https://arxiv.org/html/2312.11514v3](https://arxiv.org/html/2312.11514v3)  
5. LLM in a Flash: Efficient LLM Inference with Limited Memory | Hacker News, 访问时间为 七月 1, 2026， [https://news.ycombinator.com/item?id=38704982](https://news.ycombinator.com/item?id=38704982)  
6. LLM in a flash: Efficient Large Language Model Inference with Limited Memory \- ACL Anthology, 访问时间为 七月 1, 2026， [https://aclanthology.org/2024.acl-long.678.pdf](https://aclanthology.org/2024.acl-long.678.pdf)  
7. Scaling Up On-Device LLMs via Active-Weight Swapping Between DRAM and Flash \- arXiv, 访问时间为 七月 1, 2026， [https://arxiv.org/html/2504.08378v2](https://arxiv.org/html/2504.08378v2)  
8. The Missing Middle: Infrastructure-Aware Intelligence and the Economics of Good-Enough AI | by Gaurav Kumar Singh | Jun, 2026 | AI Advances, 访问时间为 七月 1, 2026， [https://ai.gopubby.com/the-missing-middle-infrastructure-aware-intelligence-and-the-economics-of-good-enough-ai-d895ffdc0248](https://ai.gopubby.com/the-missing-middle-infrastructure-aware-intelligence-and-the-economics-of-good-enough-ai-d895ffdc0248)  
9. KVStream: Smarter Memory Management for On-Device Language Model Inference, 访问时间为 七月 1, 2026， [https://techcommunity.microsoft.com/blog/educatordeveloperblog/kvstream-smarter-memory-management-for-on-device-language-model-inference/4529843](https://techcommunity.microsoft.com/blog/educatordeveloperblog/kvstream-smarter-memory-management-for-on-device-language-model-inference/4529843)  
10. KVSwap: Disk-aware KV Cache Offloading for Long-Context On-device Inference \- arXiv, 访问时间为 七月 1, 2026， [https://arxiv.org/html/2511.11907v2](https://arxiv.org/html/2511.11907v2)  
11. Snowflake LLM Inference: Model Hotswapping, 访问时间为 七月 1, 2026， [https://www.snowflake.com/en/blog/engineering/llm-interference-model-hotswapping/](https://www.snowflake.com/en/blog/engineering/llm-interference-model-hotswapping/)  
12. HiFC: High-efficiency Flash-based KV Cache Swapping for Scaling LLM Inference \- NIPS, 访问时间为 七月 1, 2026， [https://papers.neurips.cc/paper\_files/paper/2025/file/4431224d3762aa655f0aee4eaf04ff16-Paper-Conference.pdf](https://papers.neurips.cc/paper_files/paper/2025/file/4431224d3762aa655f0aee4eaf04ff16-Paper-Conference.pdf)  
13. \[2409.11155\] ISO: Overlap of Computation and Communication within Seqenence For LLM Inference \- arXiv, 访问时间为 七月 1, 2026， [https://arxiv.org/abs/2409.11155](https://arxiv.org/abs/2409.11155)  
14. FlightLLM: Efficient Large Language Model Inference with a Complete Mapping Flow on FPGAs \- NICS-EFC, 访问时间为 七月 1, 2026， [https://nicsefc.ee.tsinghua.edu.cn/nics\_file/pdf/3fa036e2-0d62-45c4-81e2-20d9b7189504.pdf](https://nicsefc.ee.tsinghua.edu.cn/nics_file/pdf/3fa036e2-0d62-45c4-81e2-20d9b7189504.pdf)  
15. Training Ultra Long Context Language Model with Fully Pipelined Distributed Transformer \- MLSys Proceedings, 访问时间为 七月 1, 2026， [https://proceedings.mlsys.org/paper\_files/paper/2025/file/d5a655b8b373737b4f2aea8f78e5e754-Paper-Conference.pdf](https://proceedings.mlsys.org/paper_files/paper/2025/file/d5a655b8b373737b4f2aea8f78e5e754-Paper-Conference.pdf)  
16. LLM Inference in a Flash\! \- OpenReview, 访问时间为 七月 1, 2026， [https://openreview.net/pdf?id=gSphYexssc](https://openreview.net/pdf?id=gSphYexssc)  
17. Scaling Up On-Device LLMs via Active-Weight Swapping Between DRAM and Flash \- arXiv, 访问时间为 七月 1, 2026， [https://arxiv.org/abs/2504.08378](https://arxiv.org/abs/2504.08378)  
18. Horizon-LM: A RAM-Centric Architecture for LLM Training Single-GPU Training of Hundreds-of-Billions Parameter Language Models with Mixed BF16/FP32 Precision Code: https://github.com/DLYuanGod/Horizon-LM \- arXiv, 访问时间为 七月 1, 2026， [https://arxiv.org/html/2602.04816v2](https://arxiv.org/html/2602.04816v2)  
19. LLM in a flash: Efficient LLM Inference with Limited Memory | by Anuj Dutt | Medium, 访问时间为 七月 1, 2026， [https://medium.com/@anuj.dutt9/llm-in-a-flash-efficient-llm-inference-with-limited-memory-e094ea22ec1b](https://medium.com/@anuj.dutt9/llm-in-a-flash-efficient-llm-inference-with-limited-memory-e094ea22ec1b)  
20. LLM in a flash: Efficient Large Language Model Inference with Limited Memory \- arXiv, 访问时间为 七月 1, 2026， [https://arxiv.org/html/2312.11514v1](https://arxiv.org/html/2312.11514v1)  
21. Why MMAP in llama.cpp hides true memory usage \- Hacker News, 访问时间为 七月 1, 2026， [https://news.ycombinator.com/item?id=35426679](https://news.ycombinator.com/item?id=35426679)  
22. Combining Buffered I/O and Direct I/O in Distributed File Systems | USENIX, 访问时间为 七月 1, 2026， [https://www.usenix.org/system/files/fast24-qian.pdf](https://www.usenix.org/system/files/fast24-qian.pdf)  
23. Data × LLM: From Principles to Practices \- arXiv, 访问时间为 七月 1, 2026， [https://arxiv.org/html/2505.18458v1](https://arxiv.org/html/2505.18458v1)  
24. Making Cold Start Latencies go Brrrr: A Multi-pronged Approach, 访问时间为 七月 1, 2026， [https://www.parasail.io/blogs/making-cold-start-latencies-go-brrrr-a-multi-pronged-approach/](https://www.parasail.io/blogs/making-cold-start-latencies-go-brrrr-a-multi-pronged-approach/)  
25. Optimizing read I/O with read ahead while avoiding storing data in page cache, 访问时间为 七月 1, 2026， [https://unix.stackexchange.com/questions/317126/optimizing-read-i-o-with-read-ahead-while-avoiding-storing-data-in-page-cache](https://unix.stackexchange.com/questions/317126/optimizing-read-i-o-with-read-ahead-while-avoiding-storing-data-in-page-cache)  
26. Understanding LLM Checkpoint/Restore I/O Strategies and Patterns \- arXiv, 访问时间为 七月 1, 2026， [https://arxiv.org/html/2512.24511v1](https://arxiv.org/html/2512.24511v1)  
27. Storage Performance Basics for Deep Learning | NVIDIA Technical Blog, 访问时间为 七月 1, 2026， [https://developer.nvidia.com/blog/storage-performance-basics-for-deep-learning/](https://developer.nvidia.com/blog/storage-performance-basics-for-deep-learning/)  
28. Unweight: how we compressed an LLM 22% without sacrificing quality \- The Cloudflare Blog, 访问时间为 七月 1, 2026， [https://blog.cloudflare.com/unweight-tensor-compression/](https://blog.cloudflare.com/unweight-tensor-compression/)  
29. The Nexware Project \- OSDev.org, 访问时间为 七月 1, 2026， [https://forum.osdev.org/viewtopic.php?t=37167](https://forum.osdev.org/viewtopic.php?t=37167)  
30. nexos-dev/nexnix \- GitHub, 访问时间为 七月 1, 2026， [https://github.com/nexos-dev/nexnix](https://github.com/nexos-dev/nexnix)  
31. Microkernels are easier to build than Monolithic kernels \- OSDev.org, 访问时间为 七月 1, 2026， [https://forum.osdev.org/viewtopic.php?t=56448](https://forum.osdev.org/viewtopic.php?t=56448)  
32. Standardized IPC protocol \- Page 3 \- OSDev.org, 访问时间为 七月 1, 2026， [https://forum.osdev.org/viewtopic.php?t=36680\&start=30](https://forum.osdev.org/viewtopic.php?t=36680&start=30)  
33. Are Microkernels really better? \- OSDev.org, 访问时间为 七月 1, 2026， [https://forum.osdev.org/viewtopic.php?t=37145](https://forum.osdev.org/viewtopic.php?t=37145)  
34. Training Ultra Long Context Language Model with Fully Pipelined Distributed Transformer \- arXiv, 访问时间为 七月 1, 2026， [https://arxiv.org/html/2408.16978v1](https://arxiv.org/html/2408.16978v1)

[image1]: <data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAcAAAAcCAYAAACtQ6WLAAAAm0lEQVR4XmNgGNYgGYiV0QVBgAeI/wPxHnQJGLABYmZ0QZyAEYjVgNgXXQIEeoD4JRDfAWIXNDmG3UDMzwDRuRBZwhOIOaCSJ4A4AlkSJAEClkD8E4gVkeTgoJUB4keQ41CABxD/A+LrDBBJIWTJ+QwQXSDdSgwQu+HgKhA/BGJJIL4IxCrIkgUMEGNfA7E3sgQMiAOxFbrg0AEADmAWpNx0c3gAAAAASUVORK5CYII=>

[image2]: <data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABMAAAAaCAYAAABVX2cEAAABCElEQVR4Xu2ToW7CUBSGf8LElpBgFhayGTyPgETsAUAjMXiSqT7EXmHBYDE8AQ5FIGGKbHpqFvh/DqW3N21vNemXfKLnnp6e09MCFffLI2071tPHF1qwvCANWJFfeqIr2kxlWLEB/acT2D2F7GGFjvTdOxMqMPWDWTzAEtWRuvuj3VQG0KcvXiyTZ1iyUDEZ3U6ND9hDg4yRJEZICsa80Z1znYuKzJ1rjacxVezpGlPX61tGAT268WJagBbxSWt0Aes+iJLczoQWoc3+0A79hj00yBbJy3dREY2q8y9Yh0E04qsfhN0cL6LUiHrBWnkeM1gxfTq56P8b0iU90NE15hMvotSIFRUOZ4WMK6UtAj8qAAAAAElFTkSuQmCC>

[image3]: <data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAACcAAAAaCAYAAAA0R0VGAAAB2ElEQVR4Xu2WzStFURTFl1BISYqEwkwpAzKQRBkwIANl4q+giCITE0NRMjOQj5SRkgzeCGVKRopSSsnISD7Wap/rHufVS70PT71frTpnn/vO2Xftfe99QIECBQpkjGPqk5qhxp32XWzZi927WE55pua8eQl1QD1SrV58gHrz5lmnnJoMYr2wJOSaTwd1HcSyShvVEsRmYeUbDOKanwWxnHMHS64siOcFKmnOG/83yC0l9hAu5AMNsOSOwoV8QA/DB5Ifhj9HJZVjKmljsOZTSnVRTW5eTw1TNbA9+t1YdFJjbiyKYK8k7aHxCOJrUyK35Np8uODQZtNUt5ur/KvUECw5zXWoeKWu3LiS2nLjDdir64Xqge25jRRvBt15O3UCO2Addrd1iA8TckGHRizAHNbvdUNP3to7NerG6uPohb5IVcMqpISir5HOSosEdRsGYQmqFaIEdGgC5picWcPPw+WyvkIi+hqlzSns0IhiqgLWDnJKhwrfKSV1A6vAiotpTdeIJViJ06aK2qU2qR2qD+bMHnWIuG+mEH8S5eolrHQa64+E/lBcwPpwArZHxlCPqWQRGvsN7fdpNJfLQn0ol3V97fcVeYASlIvnSL6BP6cZ1hKS3P+/fAFoM08tBxImlgAAAABJRU5ErkJggg==>

[image4]: <data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAHAAAAAaCAYAAABvj9h3AAAD7ElEQVR4Xu2ZS6hOURTHl1Dej4iEPFJSRBGJFAkTjzBQjFBKJPLIo1wDA4yIlEhG8oiRksQthShMZCC5pJSSDChv62ftXetun9t3v/t93XOyf/XvO3vvc9Y5+7HWfnwimUwmk8lkMplMJlMT11W/VDtVK4MuhryDLu9VyCsrh1SH08wKbBKr51HVKrG67wl5t0Ia3Q55fe2xzuO9ardLd1NdVr1VjXX5c1XfXLosDFOdUF1RjU/KKsGApqM8q8U6i9/IINUjsfbqNHqq1iR5s8U6Cu/zTFY9TfKKShfVDdV3sQ5sD0Sf7i49UPUwiOtIb9VNl+4UJqjGJHm7xEbb/CSf9N0kr4jMUN1T7RPzkvbQQyxseqaqPqmOJfmDVQ+SvELQItaBVKYs4DHM0e/Eoko9OSvWHgz2UkD4LNNiZa1Yx+E5PvTVC0JnIRYr1YDX8bFv0oIC008sXBI2CZ/1hvYozYAeLvax19KCErBF9VG1VNU1KasVFkO0Byv1UsAC5qf8vYApE3QenUhYJbx2BBYwdGC6gCkkhE88j/A5IikDRiNbiTjPLFbNCdcsqReE35RpqmUujY343NBwjc22bNTCPNVjMc+s1eYGsQ5cnhY4aJfRYp3N4EFsxfiljLpTBqyKY30j3Buf5bfWb/3jdXgfpw6V4GSCBv+hWhHy8NiTYh8Ul9uR46rnLr1NbIWIjdPS2gaNFCuFDQZHPWED/1m1MS1oA7YJ7HvvyL8XMAxOTrDoKAY+99NOPPtBNT3cd061I9xH3WI7cR/tgeOMC3m8r10ez0Z3otjGl4bk1IJRg2E/Uvaq+quuunxexLwJS1RfwjXQ0U1i9y5UjVKNFLPBR3obLeEasMGorDe8O3pCW9AefOsRsQHNKpQVLvl93H1cN4vVB7arZqnWi0Uwju7osBjZ4rMMWE65YJJY+7WENPC+hh0QEEpi47Iv8pP7MzGvAz6afVmlUxBs+CM5bFApYNBgg4oXnRg5KkEUi8eQRDYGcwTP5Lw1QmSKHQrYJHQ3BLyFRobU476qFomNPM4Hm6X1iO0VfrHxwuV7j+N5xOlQvVaSjYKG91MGMH9Fj4tzmfc4oAOJeFtDmnPn5nBNu+EIldYgHYaw4veHuPl5l34pdmAcQyMdySLigup+vEnMxrpwTSWxEQ+EmR+wcSCkq4HB8LpKVfNvRLUQJZhTmd/OqDaLzfEzxTopwvc1uTTrAjotRhkGMFPKJdWTeFMj4IX+MBfv8sdtpAe4dMzzXgjY8N6VHtmlNooO04Q/c6Vuvp2YI/1agvr5NmEKYTrCRhmmjkwgrtzxvCFJWaYETFGdCtqflGX+B34DDpq9zEPRTo8AAAAASUVORK5CYII=>

[image5]: <data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAC0AAAAaCAYAAAAjZdWPAAAB5ElEQVR4Xu2WTStFURSGl1BEIUqifAyUUgZMlERJJiQMDGRkQBkwQBgbGBIpf0BJjJSkfJV/IAMJpYxkZIB8vG9rnVq20q2rnHKeerp7r33vPuusvfc5VyQhISHh37EHP+A0HDA3LbbgYrcWiwUPcNb1s+AWvIc1Lt4OX13/z8iFQ0GsRTQ5VtnTAM+D2J9QB6uD2IzoNugI4uyfBbHYcCOadE4QjzXcGrE5cKnA6jLhu3AgzpSLJr0bDsQZHsJ3+X4IYwu3BivMrVERjJEM0cdetvW7Yau182CnfYZwXn632PqZsNE+q2CvtYmfMyVYXVZ5LhwwlmApfIP9FuPKrIneCBN5sjjJhyewwPoror/nPAfw0eKE152yNhPnXD9SBuvhvuh+XhW9e14gqiqZF01gx8WXRc8B6YHP1iajcNDaw3AbNsER0dVctDGuxJHoTRIWwr+J06ZP9I1J+GLiX4CIC9FqkhLRt2eUSIj/i8AV5g0TFs+vwK/AynIFSFjZF9glWsFCeOrGSLQ6rCy3R7T/fWXHRZNuhpMWS4tK+fr85oU3XP9adAtEyXErHcN1eAhrLc6E2qzNAlxZm0zASzgmevDThpMUuT6X3r/q2WeFPezzzHj4pPAJRQc1IpwjISFVPgHgdU+sKXLn6gAAAABJRU5ErkJggg==>