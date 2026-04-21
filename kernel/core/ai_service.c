#include "types.h"
#include "defs.h"
#include "proc.h"
#include "spinlock.h"

#define AI_NREQ 8           // 最大可用请求槽数量
#define AI_MAX_TOKENS 255
#define AI_MAX_PREDICT 32
#define AI_MAX_RESULT 255

enum ai_req_state {
    AIREQ_UNUSED = 0, // 空闲
    AIREQ_NEW,        // 已创建
    AIREQ_READY,      // 准备就绪
    AIREQ_RUNNING,    // 正在执行
    AIREQ_DONE,       // 执行完成
    AIREQ_FAILED,     // 执行失败
};

struct ai_request {
    int id;                         // 请求编号
    int owner_pid;                  // 提交者 pid
    int state;                      // 请求状态
    int err;                        // 错误码
    int token_count;                // 输入 token 数量
    int predict_count;              // 生成 token 数量
    int result_len;                 // 结果长度
    uint32 tokens[AI_MAX_TOKENS];   // 输入 token 缓冲
    char result[AI_MAX_RESULT + 1]; // 输出结果缓冲
};

struct ai_service {
    struct spinlock lock;           // 服务锁
    int next_id;                    // 下一个请求将被分配的请求编号
    int worker_pid;                 // worker 进程 pid，
                                    // 用于在一些只有 worker 可调用的函数中对调用进程进行身份校验
    int worker_online;              // worker 在线标记，指示当前是否有可用的 worker 进程在线
    int q[AI_NREQ];                 // 请求队列，保存槽位下标
    int qhead;                      // 队头
    int qtail;                      // 队尾
    int qcount;                     // 队列长度，当队列长度为 0 时 worker 睡眠；当队列满时用户进程阻塞
    struct ai_request reqs[AI_NREQ]; // 请求表
} aisvc;

/*
 * Student starter guide:
 *
 * This branch keeps the current AIOS architecture:
 *
 *   user -> ai_call/ai_submit/ai_wait/ai_query
 *        -> kernel ai_service
 *        -> ai_daemon via ai_worker_* syscalls
 *        -> llm runtime
 *
 * You do not need to change user/ai_daemon.c or the llm runtime for the lab.
 * Your main job is to complete the kernel control plane in this file.
 *
 * Suggested staging:
 *
 * - Starter sync smoke path:
 *   ai_service_call() is already provided so students can quickly check
 *   the environment and syscall path before touching the real service.
 *
 * - Part 1:
 *   Build the real producer/consumer service path:
 *     ai_service_enqueue_tokens()
 *     ai_service_worker_register()
 *     ai_service_worker_get()
 *     ai_service_worker_complete()
 *
 * - Part 2:
 *   Implement isolation and result semantics:
 *     ai_find_req_locked()
 *     ai_service_query()
 *     ai_service_wait()
 *   After those semantics are stable, do a final ai_call() cleanup:
 *     ai_service_call()
 *
 * The rest of the system is intentionally kept complete so you can focus on
 * OS concerns: request objects, sleep/wakeup, ownership, and one-shot result
 * consumption.
 */

static void ai_req_reset(struct ai_request *req) {
    memset(req, 0, sizeof(*req));
    req->state = AIREQ_UNUSED;
}

static __attribute__((unused)) int ai_req_busy(const struct ai_request *req) {
    return req->state == AIREQ_NEW || req->state == AIREQ_READY || req->state == AIREQ_RUNNING;
}

// 尝试找到一个可用请求槽并将其返回
static __attribute__((unused)) struct ai_request *ai_find_slot_locked(void) {
    for (int i = 0; i < AI_NREQ; i++) {
        if (aisvc.reqs[i].state == AIREQ_UNUSED) {
            return &aisvc.reqs[i];
        }
    }
    return 0;
}

static __attribute__((unused)) struct ai_request *ai_find_req_locked(int reqid, int owner_pid) {
    for (int i = 0; i < AI_NREQ; i++) {
        struct ai_request *req = &aisvc.reqs[i];
        if (req->state == AIREQ_UNUSED || req->id != reqid) {
            continue;
        }

        // 已确定 req->id == reqid，校验请求-进程从属关系
        if (req->owner_pid != owner_pid) {
            // 当前请求的父进程不是调用进程，校验失败
            return NULL;
        }
        return req;
    }
    return NULL;
}

static __attribute__((unused)) struct ai_request *ai_find_req_by_id_locked(int reqid) {
    for (int i = 0; i < AI_NREQ; i++) {
        struct ai_request *req = &aisvc.reqs[i];
        if (req->state != AIREQ_UNUSED && req->id == reqid) {
            return req;
        }
    }
    return 0;
}

static int ai_append_text(char *out, int out_cap, int *pos, const char *text) {
    while (*text != '\0') {
        if (*pos >= out_cap - 1) {
            return -1;
        }
        out[*pos] = *text;
        (*pos)++;
        text++;
    }
    out[*pos] = '\0';
    return 0;
}

static int ai_append_u32(char *out, int out_cap, int *pos, uint32 value) {
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
    out[*pos] = '\0';
    return 0;
}

/*
 * Course-provided helper for the starter sync smoke path.
 *
 * This is intentionally not the real ai_daemon path. Students do not need to
 * discover or call it themselves: ai_service_call() uses it until the final
 * Part 2 cleanup turns ai_call() into a real submit + wait wrapper.
 */
static __attribute__((unused)) int ai_sync_smoke_placeholder(
    const uint32 *tokens,
    int token_count,
    int predict_count,
    char *out,
    int out_cap
) {
    int pos = 0;

    if (out_cap <= 0) {
        return -1;
    }
    out[0] = '\0';

    if (ai_append_text(out, out_cap, &pos, "sync-smoke ") < 0) {
        return -1;
    }
    if (ai_append_text(out, out_cap, &pos, "predict=") < 0) {
        return -1;
    }
    if (ai_append_u32(out, out_cap, &pos, (uint32)predict_count) < 0) {
        return -1;
    }
    if (ai_append_text(out, out_cap, &pos, " prompt=") < 0) {
        return -1;
    }

    for (int i = 0; i < token_count && i < 4; i++) {
        if (i > 0 && ai_append_text(out, out_cap, &pos, "-") < 0) {
            return -1;
        }
        if (ai_append_u32(out, out_cap, &pos, tokens[i]) < 0) {
            return -1;
        }
    }

    if (token_count > 4 && ai_append_text(out, out_cap, &pos, "-more") < 0) {
        return -1;
    }
    return pos;
}

void ai_service_init(void) {
    initlock(&aisvc.lock, "ai_service");
    aisvc.next_id = 1;
    aisvc.worker_pid = 0;
    aisvc.worker_online = 0;
    aisvc.qhead = 0;
    aisvc.qtail = 0;
    aisvc.qcount = 0;
    for (int i = 0; i < AI_NREQ; i++) {
        ai_req_reset(&aisvc.reqs[i]);
        aisvc.q[i] = -1;
    }
}

static int ai_service_enqueue_tokens(uint64 token_uva, int token_count, int predict_count, int *reqid_out) {
    struct proc *p = myproc();
    if (p == 0 || p->pagetable == 0) {
        return -1;
    }
    if (token_count <= 0 || token_count > AI_MAX_TOKENS || predict_count <= 0 || predict_count > AI_MAX_PREDICT) {
        return -1;
    }

    // 先将用户态数据复制到内核缓冲区
    uint32 tokens[AI_MAX_TOKENS];
    memset(tokens, 0, sizeof(tokens));
    if (copyin(p->pagetable, (char *)tokens, token_uva, (uint64)token_count * sizeof(uint32)) < 0) {
        return -1;
    }

    acquire(&aisvc.lock);

    if (!aisvc.worker_online) {
        // worker 离线，无法处理请求
        release(&aisvc.lock);
        return -1;
    }

    // 等待一个可用请求槽以存入当前请求
    struct ai_request *req;
    while ((req = ai_find_slot_locked()) == 0) {
        sleep(&aisvc.qcount, &aisvc.lock);      // 进程以 aisvc.qcount 作为等待标识符进行等待
        // 可能有多个请求同时等待请求槽，因此在需要提交请求的进程被唤醒后，
        // 需要再通过 while 循环条件尝试是否可锁定一个可用请求槽
    }

    // 成功获取到一个可用请求槽，向请求槽中写入请求的相关信息
    req->id = aisvc.next_id++;                  // 赋予 reqid
    req->owner_pid = p->pid;                    // 确定请求和父进程的从属关系
    req->token_count = token_count;
    req->predict_count = predict_count;
    req->err = 0;                               // 初始化错误码
    req->result_len = 0;                        // 初始化输出结果长度
    memmove(req->tokens, tokens, (uint64)token_count * sizeof(uint32));
                                                // 将请求的输入 tokens 复制到请求槽中
    req->state = AIREQ_READY;                   // 设置请求槽已准备好被处理

    // 将请求信息转移到请求槽中后，将该请求送入待处理请求的循环队列
    int slot = (int)(req - aisvc.reqs);
    aisvc.q[aisvc.qtail] = slot;
    aisvc.qtail = (aisvc.qtail + 1) % AI_NREQ;
    aisvc.qcount++;

    wakeup(&aisvc.qcount);                      // 唤醒所有以 aisvc.qcount 作为等待标识符的进程，
                                                // 包括没有请求可供处理时的休眠 worker 进程
    *reqid_out = req->id;                       // 将请求在请求槽中的下标返回给父进程

    release(&aisvc.lock);
    return 0;
}

int ai_service_worker_register(void) {
    struct proc *p = myproc();
    if (p == 0 || p->pagetable == 0) {
        return -1;
    }

    acquire(&aisvc.lock);
    
    // 检查 worker 是否已经注册
    if (aisvc.worker_online && aisvc.worker_pid != p->pid) {
        // 如果 worker 已经注册，并且不是当前进程，就中断注册流程
        release(&aisvc.lock);
        return -1;
    }

    // 注册当前进程为 worker，并标记 worker 上线
    // 兼容同一个进程重复注册 worker 的边界情况
    aisvc.worker_pid = p->pid;
    aisvc.worker_online = 1;

    release(&aisvc.lock);
    return 0;               // 当前进程成功注册为 worker，返回 0
}

int ai_service_worker_get(uint64 token_uva, int token_cap, uint64 reqid_uva, uint64 predict_uva) {
    struct proc *p = myproc();
    if (p == 0 || p->pagetable == 0 || token_uva == 0 || 
        token_cap <= 0 || token_cap > AI_MAX_TOKENS || 
        reqid_uva == 0 || predict_uva == 0) {
        return -1;
    }

    acquire(&aisvc.lock);

    // 先检查 worker 是否在线，再执行进程身份校验
    if (!aisvc.worker_online || p->pid != aisvc.worker_pid) {
        release(&aisvc.lock);
        return -1;
    }

    // 循环队列为空，worker 进入睡眠
    while (aisvc.qcount == 0) {
        sleep(&aisvc.qcount, &aisvc.lock);  // worker 以 aisvc.qcount 为等待标识符进行等待
    }

    // 从循环队列中取出队首请求
    int slot = aisvc.q[aisvc.qhead];
    aisvc.qhead = (aisvc.qhead + 1) % AI_NREQ;
    aisvc.qcount--;
    wakeup(&aisvc.qcount);                  // 唤醒希望提交请求的进程

    struct ai_request *req = &aisvc.reqs[slot];
    req->state = AIREQ_RUNNING;             // 标记队首请求正在被处理

    // 将请求的详细信息复制到本地内核缓冲区
    // 包括请求的传入 token 数，输出 token 数，请求 id，具体的传入 token 数组
    int token_count = req->token_count;
    int predict_count = req->predict_count;
    int reqid = req->id;
    uint32 tokens[AI_MAX_TOKENS];
    memmove(tokens, req->tokens, (uint64)token_count * sizeof(uint32));

    // 检查给定的用户缓冲区大小是否充足
    if (token_count > token_cap) {
        // 如果给定的用户态缓冲区过小，就执行报错返回
        req->state = AIREQ_FAILED;
        req->err = -1;
        wakeup(req);                // 唤醒请求的父进程
        release(&aisvc.lock);
        return -1;
    }
    
    release(&aisvc.lock);

    // 数据的复制过程较为耗时，且调用 copyout 过程可能触发缺页异常导致进程睡眠，
    // 此时不能持有 aisvc.lock，否则会引发死锁或内核 panic

    // 将 token 数组复制到用户空间
    // 使用 char* 强制类型转换，确保指针逐字节变化，能逐字节复制数据
    if (copyout(p->pagetable, token_uva, (char *)tokens, (uint64)token_count * sizeof(uint32)) < 0) {
        // 复制失败，更新请求状态并唤醒请求的父进程
        acquire(&aisvc.lock);
        req->state = AIREQ_FAILED;
        req->err = -1;
        wakeup(req);                // 唤醒请求的父进程
        release(&aisvc.lock);
        return -1;
    }

    // 将 reqid 复制到用户空间
    if (copyout(p->pagetable, reqid_uva, (char *)&reqid, sizeof(reqid)) < 0) {
        acquire(&aisvc.lock);
        req->state = AIREQ_FAILED;
        req->err = -1;
        wakeup(req);
        release(&aisvc.lock);
        return -1;
    }

    // 将 predict_count 复制到用户空间
    if (copyout(p->pagetable, predict_uva, (char *)&predict_count, sizeof(predict_count)) < 0) {
        acquire(&aisvc.lock);
        req->state = AIREQ_FAILED;
        req->err = -1;
        wakeup(req);
        release(&aisvc.lock);
        return -1;
    }

    return token_count;
}

int ai_service_worker_complete(int reqid, uint64 out_uva, int out_len, int status) {
    struct proc *p = myproc();
    if (p == 0 || p->pagetable == 0 || reqid <= 0 || out_uva == 0 || out_len <= 0) {
        return -1;
    }

    char result[AI_MAX_RESULT + 1];
    memset(result, 0, sizeof(result));

    int succ = 0;           // 成功指示：成功将结果从用户空间复制到内核缓存
    int result_len = 0;     // 结果长度

    acquire(&aisvc.lock);

    // 先检查 worker 是否在线，再执行进程身份校验
    if (!aisvc.worker_online || p->pid != aisvc.worker_pid) {
        release(&aisvc.lock);
        return -1;
    }

    // 根据 reqid 找到请求，确认请求状态
    struct ai_request *req = ai_find_req_by_id_locked(reqid);
    if (req == NULL || req->state != AIREQ_RUNNING) {
        // 未找到请求，或请求不处在 RUNNING 状态
        release(&aisvc.lock);
        return -1;
    }

    release(&aisvc.lock);

    // 离开临界区，将进行 copyin 操作，因此需要先释放锁

    // 如果状态为成功（status == 0），从用户空间复制生成的文本
    if (status == 0 && out_len >= 0 && out_len <= AI_MAX_RESULT && 
        (out_len == 0 || out_uva != 0) && 
        (out_len == 0 || copyin(p->pagetable, result, out_uva, out_len) >= 0)) {

        // 合法复制
        result[out_len] = '\0';
        succ = 1;
        result_len = out_len;
    }
    else {
        succ = 0;
        result_len = 0;
    }

    // 重新上锁并更新请求槽中的请求状态
    acquire(&aisvc.lock);

    // 先检查 worker 是否在线，再执行进程身份校验
    if (!aisvc.worker_online || p->pid != aisvc.worker_pid) {
        release(&aisvc.lock);
        return -1;
    }

    // 重新根据 reqid 查找对应的请求槽
    req = ai_find_req_by_id_locked(reqid);
    if (req == NULL || req->state != AIREQ_RUNNING) {
        release(&aisvc.lock);
        return -1;
    }

    if (succ) {
        req->state = AIREQ_DONE;
        req->err = 0;
        req->result_len = result_len;
        memmove(req->result, result, result_len + 1);      // 把请求的结果复制到请求槽中
    }
    else {
        // 状态为失败
        req->state = AIREQ_FAILED;
        req->err = -1;
        req->result[0] = '\0';
        req->result_len = 0;
    }
    // 唤醒请求的父进程
    wakeup(req);

    release(&aisvc.lock);
    return 0;
}

void ai_service_proc_exit(int pid) {
    if (pid <= 0) {
        return;
    }

    acquire(&aisvc.lock);
    if (!aisvc.worker_online || aisvc.worker_pid != pid) {
        release(&aisvc.lock);
        return;
    }

    aisvc.worker_online = 0;
    aisvc.worker_pid = 0;
    for (int i = 0; i < AI_NREQ; i++) {
        struct ai_request *req = &aisvc.reqs[i];
        if (req->state == AIREQ_RUNNING) {
            req->err = -1;
            req->result_len = 0;
            req->state = AIREQ_FAILED;
            wakeup(req);
        }
    }
    wakeup(&aisvc.qcount);
    release(&aisvc.lock);
}

int ai_service_submit(uint64 token_uva, int token_count, int predict_count) {
    int reqid = -1;
    if (ai_service_enqueue_tokens(token_uva, token_count, predict_count, &reqid) < 0) {
        return -1;
    }
    return reqid;
}

int ai_service_query(int reqid, uint64 st_uva) {
    struct proc *p = myproc();
    if (p == 0 || p->pagetable == 0 || reqid <= 0) {
        return -1;
    }

    // 涉及到对共享对象 aisvc 的操作，需要先上锁
    acquire(&aisvc.lock);
    
    struct ai_request *req = ai_find_req_locked(reqid, p->pid);
    if (!req) {
        // 请求-进程关系校验失败或请求不存在
        release(&aisvc.lock);
        return -1;
    }

    struct ai_status st = {
        .reqid = req->id,
        .state = req->state,
        .err = req->err,
        .result_len = req->result_len
    };

    release(&aisvc.lock);

    // 将请求信息复制到用户态
    if (copyout(p->pagetable, st_uva, (char *)&st, sizeof(st)) < 0) {
        return -1;
    }

    return 0;
}

int ai_service_wait(int reqid, uint64 out_uva, int out_cap) {
    struct proc *p = myproc();
    if (p == 0 || p->pagetable == 0 || reqid <= 0 || out_cap <= 0 || out_cap > AI_MAX_RESULT + 1) {
        return -1;
    }

    // 涉及到对共享对象 aisvc 的操作，需要先上锁
    acquire(&aisvc.lock);
    
    struct ai_request *req = ai_find_req_locked(reqid, p->pid);
    if (!req) {
        // 请求-进程关系校验失败或请求不存在
        release(&aisvc.lock);
        return -1;
    }

    // 等待 req 完成请求执行
    while (ai_req_busy(req)) {
        sleep(req, &aisvc.lock);    // 以当前 req 作为等待标识符，等待请求处理完成
    }

    // 检查请求状态
    if (req->state == AIREQ_FAILED) {
        // 请求失败，回收槽位后返回错误
        ai_req_reset(req);
        wakeup(&aisvc.qcount);  // 唤醒等待空闲槽位的进程
        release(&aisvc.lock);
        return -1;
    }

    if (req->state != AIREQ_DONE) {
        // 请求并未处在 DONE 状态
        release(&aisvc.lock);
        return -1;
    }

    // 请求处在 DONE 状态，取请求结果
    char result[AI_MAX_RESULT + 1];
    int result_len = req->result_len;
    memmove(result, req->result, result_len + 1);

    // 成功取回结果后，回收请求槽
    ai_req_reset(req);
    wakeup(&aisvc.qcount);  // 唤醒等待空闲槽位的进程

    release(&aisvc.lock);

    // 将取出的请求结果复制到用户空间
    if (copyout(p->pagetable, out_uva, result, (uint64)result_len + 1) < 0) {
        return -1;
    }

    return result_len;
}

int ai_service_call(uint64 token_uva, int token_count, int predict_count, uint64 out_uva, int out_cap) {
    int reqid = ai_service_submit(token_uva, token_count, predict_count);
    if (reqid < 0) {
        return -1;
    }
    return ai_service_wait(reqid, out_uva, out_cap);
}
