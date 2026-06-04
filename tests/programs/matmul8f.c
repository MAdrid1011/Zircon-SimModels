#include <stdint.h>

#define MATRIX_N 8u
#define RESULT_BASE 0x4000u
#define STATUS_ADDR 0x5000u
#define STATUS_DONE 0x51f15e9du

static const volatile float kMatrixA[MATRIX_N * MATRIX_N] = {
    1.0f, 0.5f, -1.0f, 2.0f, 0.25f, -0.5f, 1.5f, -2.0f,
    -0.5f, 1.0f, 0.75f, -1.25f, 2.5f, -2.0f, 0.125f, 1.75f,
    2.0f, -1.5f, 1.25f, 0.5f, -0.75f, 1.0f, -2.5f, 0.25f,
    0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f,
    -2.0f, -1.0f, 0.5f, 1.5f, -0.25f, 0.75f, -1.25f, 2.25f,
    1.5f, -0.25f, 2.0f, -0.5f, 0.5f, -1.5f, 1.0f, 0.75f,
    0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, -0.5f, -1.0f,
    -1.75f, 2.25f, -0.75f, 1.25f, -0.5f, 0.5f, 1.0f, -2.0f,
};

static const volatile float kMatrixB[MATRIX_N * MATRIX_N] = {
    0.5f, -1.0f, 1.5f, -2.0f, 2.5f, -0.25f, 0.75f, 1.0f,
    1.0f, 0.25f, -0.5f, 0.75f, -1.25f, 1.5f, -2.0f, 2.25f,
    -1.5f, 2.0f, 0.5f, -0.25f, 1.0f, -0.75f, 1.25f, -2.5f,
    2.0f, -0.5f, 1.0f, 0.5f, -1.0f, 2.5f, -1.5f, 0.25f,
    0.25f, 0.5f, 1.0f, 2.0f, -0.5f, -1.0f, 1.5f, -2.0f,
    -0.75f, 1.25f, -2.25f, 0.5f, 1.75f, -0.25f, 2.0f, -1.0f,
    1.5f, -2.0f, 2.5f, -1.5f, 0.5f, 1.0f, -0.25f, 0.75f,
    -2.5f, 1.5f, -1.0f, 2.0f, -0.75f, 0.25f, 1.25f, -0.5f,
};

void matmul8_main(void) {
    volatile float* result = (volatile float*)RESULT_BASE;
    volatile uint32_t* status = (volatile uint32_t*)STATUS_ADDR;

    for (uint32_t i = 0; i < MATRIX_N; ++i) {
        for (uint32_t j = 0; j < MATRIX_N; ++j) {
            float sum = 0.0f;
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
