#include <stdio.h>
#include <mylib.h>

int main(void) {
    printf("mylib version: %s\n", mylib_version());
    printf("2 + 3 = %d\n", mylib_add(2, 3));
    printf("4 * 5 = %d\n", mylib_mul(4, 5));
    return 0;
}