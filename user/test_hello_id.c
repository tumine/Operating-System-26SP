#include "user.h"

int main(void) {
    int tag = 67890;
    int ret = hello_id(tag);
    printf("hello_id(%d) returned %d\n", tag, ret);
    exit(0);
}