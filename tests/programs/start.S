    .section .text.start, "ax", @progbits
    .globl _start
    .type _start, @function

_start:
    lui sp, %hi(__stack_top)
    addi sp, sp, %lo(__stack_top)
    jal ra, matmul8_main

1:
    jal zero, 1b

    .size _start, . - _start
