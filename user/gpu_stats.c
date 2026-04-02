#include "user.h"
#include "fcntl.h"
#include "gpu.h"

int main(int argc, char *argv[])
{
    int fd = open("/gpu", O_RDWR);
    if (fd < 0)
    {
        fprintf(2, "[ioctl] cannot open gpu device\n");
        exit(1);
    }

    // 获取统计信息
    struct gpu_stats st;
    if (ioctl(fd, GPU_IOC_GET_STATS, (uint64)&st) < 0)      // 调用失败，返回非零退出码
    {
        fprintf(2, "[ioctl] GPU_IOC_GET_STATS failed\n");
        close(fd);
        exit(1);
    }

    printf("gpu matmul_ops: %llu\n", st.matmul_ops);
    close(fd);
    exit(0);
}
