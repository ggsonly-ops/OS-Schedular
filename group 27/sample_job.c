#include <stdio.h>
#include "dummy_main.h"

int main(int argc, char **argv) {
    long long sum = 0;
    for (long long i = 0; i < 1000000000; i++) {
        sum += i;
    }
    printf("Job completed! Sum = %lld\n", sum);
    return 0;
}