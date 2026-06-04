#include <stdint.h>

#define MATRIX_N 8u
#define RESULT_BASE 0x2000u
#define STATUS_ADDR 0x3000u
#define STATUS_DONE 0x51f15e9du

static const volatile int32_t kMatrixA[MATRIX_N * MATRIX_N] = {
    1, 2, 3, 4, 5, 6, 7, 8,
    -1, 0, 1, 2, 3, 4, 5, 6,
    7, 6, 5, 4, 3, 2, 1, 0,
    2, -3, 4, -5, 6, -7, 8, -9,
    9, 8, 7, 6, 5, 4, 3, 2,
    -2, -1, 0, 1, 2, 3, 4, 5,
    5, 5, 5, 5, 5, 5, 5, 5,
    3, 1, 4, 1, 5, 9, 2, 6,
};

static const volatile int32_t kMatrixB[MATRIX_N * MATRIX_N] = {
    8, 7, 6, 5, 4, 3, 2, 1,
    1, 3, 5, 7, 9, 11, 13, 15,
    2, 0, -2, 4, -4, 6, -6, 8,
    -1, -2, -3, -4, -5, -6, -7, -8,
    4, 1, 4, 1, 4, 1, 4, 1,
    0, 1, 0, 1, 0, 1, 0, 1,
    6, 5, 4, 3, 2, 1, 0, -1,
    -3, 2, -1, 4, -5, 6, -7, 8,
};

void matmul8_main(void) {
    volatile int32_t* result = (volatile int32_t*)RESULT_BASE;
    volatile uint32_t* status = (volatile uint32_t*)STATUS_ADDR;

    for (uint32_t i = 0; i < MATRIX_N; ++i) {
        for (uint32_t j = 0; j < MATRIX_N; ++j) {
            int32_t sum = 0;
            for (uint32_t k = 0; k < MATRIX_N; ++k) {
                sum += kMatrixA[i * MATRIX_N + k] * kMatrixB[k * MATRIX_N + j];
            }
            result[i * MATRIX_N + j] = sum;
        }
    }

    *status = STATUS_DONE;

    for (;;) {
    }
}
