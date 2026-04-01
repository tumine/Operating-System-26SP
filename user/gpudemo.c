#include "user.h"
#include "types.h"
#include "fcntl.h"
#include "gpu.h"

static void usage(void) {
    fprintf(2, "usage: gpudemo m n k <A(m*k items)> <B(k*n items)>\n");
}

static int parse_int(const char *s, int *out) {
    if (s == 0 || *s == '\0') {
        return -1;
    }
    int sign = 1;
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    if (*s < '0' || *s > '9') {
        return -1;
    }
    int n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    if (*s != '\0') {
        return -1;
    }
    *out = sign * n;
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        usage();
        exit(1);
    }

    int m = 0;
    int n = 0;
    int k = 0;
    if (parse_int(argv[1], &m) < 0 || parse_int(argv[2], &n) < 0 || parse_int(argv[3], &k) < 0) {
        fprintf(2, "gpudemo: invalid m/n/k\n");
        exit(1);
    }
    if (m <= 0 || n <= 0 || k <= 0) {
        fprintf(2, "gpudemo: m, n, k must be positive\n");
        exit(1);
    }

    int need_a = m * k;
    int need_b = k * n;
    int expected_argc = 4 + need_a + need_b;
    if (argc != expected_argc) {
        usage();
        fprintf(2, "gpudemo: got %d args, expected %d\n", argc - 1, expected_argc - 1);
        exit(1);
    }

    int fd = open("/gpu", O_RDWR);
    if (fd < 0) {
        fprintf(2, "gpudemo: failed to open /gpu\n");
        exit(1);
    }

    struct gpu_info info;
    if (ioctl(fd, GPU_IOC_GET_INFO, (uint64)&info) < 0) {
        fprintf(2, "gpudemo: GPU_IOC_GET_INFO failed\n");
        close(fd);
        exit(1);
    }

    if (m > info.max_dim || n > info.max_dim || k > info.max_dim) {
        fprintf(2, "gpudemo: dimension exceeds max_dim=%d\n", info.max_dim);
        close(fd);
        exit(1);
    }

    struct gpu_mem mem_a;
    struct gpu_mem mem_b;
    struct gpu_mem mem_c;
    mem_a.npages = 1;
    mem_b.npages = 1;
    mem_c.npages = 1;
    mem_a.uva = 0;
    mem_b.uva = 0;
    mem_c.uva = 0;

    if (ioctl(fd, GPU_IOC_ALLOC, (uint64)&mem_a) < 0 ||
        ioctl(fd, GPU_IOC_ALLOC, (uint64)&mem_b) < 0 ||
        ioctl(fd, GPU_IOC_ALLOC, (uint64)&mem_c) < 0) {
        fprintf(2, "gpudemo: GPU_IOC_ALLOC failed\n");
        close(fd);
        exit(1);
    }

    int *A = (int *)mem_a.uva;
    int *B = (int *)mem_b.uva;
    int *C = (int *)mem_c.uva;

    int argi = 4;
    for (int i = 0; i < need_a; i++) {
        if (parse_int(argv[argi++], &A[i]) < 0) {
            fprintf(2, "gpudemo: invalid A element\n");
            close(fd);
            exit(1);
        }
    }
    for (int i = 0; i < need_b; i++) {
        if (parse_int(argv[argi++], &B[i]) < 0) {
            fprintf(2, "gpudemo: invalid B element\n");
            close(fd);
            exit(1);
        }
    }
    for (int i = 0; i < m * n; i++) {
        C[i] = 0;
    }

    struct gpu_matmul args;
    args.m = m;
    args.n = n;
    args.k = k;
    args.buf_a = (uint64)A;
    args.buf_b = (uint64)B;
    args.buf_c = (uint64)C;

    if (ioctl(fd, GPU_IOC_MATMUL, (uint64)&args) < 0) {
        fprintf(2, "gpudemo: GPU_IOC_MATMUL failed\n");
        close(fd);
        exit(1);
    }

    struct gpu_stats st;
    if (ioctl(fd, GPU_IOC_GET_STATS, (uint64)&st) < 0) {
        fprintf(2, "gpudemo: GPU_IOC_GET_STATS failed\n");
        close(fd);
        exit(1);
    }
    printf("STATS: %d\n", (int)st.matmul_ops);

    printf("RESULT:");
    for (int i = 0; i < m * n; i++) {
        printf(" %d", C[i]);
    }
    printf("\n");

    (void)ioctl(fd, GPU_IOC_FREE, (uint64)&mem_a);
    (void)ioctl(fd, GPU_IOC_FREE, (uint64)&mem_b);
    (void)ioctl(fd, GPU_IOC_FREE, (uint64)&mem_c);

    close(fd);
    exit(0);
}
