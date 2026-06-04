#include <stdint.h>

#define STATUS_ADDR 0x6000u
#define FAIL_CODE_ADDR 0x6004u
#define SCRATCH_ADDR 0x7000u
#define STATUS_PASS 0x51f15e9du
#define STATUS_FAIL 0xdeadc0deu

static void fail(uint32_t code) {
    *(volatile uint32_t*)FAIL_CODE_ADDR = code;
    *(volatile uint32_t*)STATUS_ADDR = STATUS_FAIL;
    for (;;) {
    }
}

static void pass(void) {
    *(volatile uint32_t*)FAIL_CODE_ADDR = 0;
    *(volatile uint32_t*)STATUS_ADDR = STATUS_PASS;
    for (;;) {
    }
}

static void expect_u32(uint32_t code, uint32_t actual, uint32_t expected) {
    if (actual != expected) {
        fail(code);
    }
}

static uint32_t op_add(uint32_t lhs, uint32_t rhs) {
    uint32_t out;
    __asm__ volatile("add %0, %1, %2" : "=r"(out) : "r"(lhs), "r"(rhs));
    return out;
}

static uint32_t op_sub(uint32_t lhs, uint32_t rhs) {
    uint32_t out;
    __asm__ volatile("sub %0, %1, %2" : "=r"(out) : "r"(lhs), "r"(rhs));
    return out;
}

static uint32_t op_sll(uint32_t lhs, uint32_t rhs) {
    uint32_t out;
    __asm__ volatile("sll %0, %1, %2" : "=r"(out) : "r"(lhs), "r"(rhs));
    return out;
}

static uint32_t op_slt(int32_t lhs, int32_t rhs) {
    uint32_t out;
    __asm__ volatile("slt %0, %1, %2" : "=r"(out) : "r"(lhs), "r"(rhs));
    return out;
}

static uint32_t op_sltu(uint32_t lhs, uint32_t rhs) {
    uint32_t out;
    __asm__ volatile("sltu %0, %1, %2" : "=r"(out) : "r"(lhs), "r"(rhs));
    return out;
}

static uint32_t op_xor(uint32_t lhs, uint32_t rhs) {
    uint32_t out;
    __asm__ volatile("xor %0, %1, %2" : "=r"(out) : "r"(lhs), "r"(rhs));
    return out;
}

static uint32_t op_srl(uint32_t lhs, uint32_t rhs) {
    uint32_t out;
    __asm__ volatile("srl %0, %1, %2" : "=r"(out) : "r"(lhs), "r"(rhs));
    return out;
}

static uint32_t op_sra(uint32_t lhs, uint32_t rhs) {
    uint32_t out;
    __asm__ volatile("sra %0, %1, %2" : "=r"(out) : "r"(lhs), "r"(rhs));
    return out;
}

static uint32_t op_or(uint32_t lhs, uint32_t rhs) {
    uint32_t out;
    __asm__ volatile("or %0, %1, %2" : "=r"(out) : "r"(lhs), "r"(rhs));
    return out;
}

static uint32_t op_and(uint32_t lhs, uint32_t rhs) {
    uint32_t out;
    __asm__ volatile("and %0, %1, %2" : "=r"(out) : "r"(lhs), "r"(rhs));
    return out;
}

static uint32_t op_mul(uint32_t lhs, uint32_t rhs) {
    uint32_t out;
    __asm__ volatile("mul %0, %1, %2" : "=r"(out) : "r"(lhs), "r"(rhs));
    return out;
}

static uint32_t op_mulh(int32_t lhs, int32_t rhs) {
    uint32_t out;
    __asm__ volatile("mulh %0, %1, %2" : "=r"(out) : "r"(lhs), "r"(rhs));
    return out;
}

static uint32_t op_mulhsu(int32_t lhs, uint32_t rhs) {
    uint32_t out;
    __asm__ volatile("mulhsu %0, %1, %2" : "=r"(out) : "r"(lhs), "r"(rhs));
    return out;
}

static uint32_t op_mulhu(uint32_t lhs, uint32_t rhs) {
    uint32_t out;
    __asm__ volatile("mulhu %0, %1, %2" : "=r"(out) : "r"(lhs), "r"(rhs));
    return out;
}

static uint32_t op_div(int32_t lhs, int32_t rhs) {
    uint32_t out;
    __asm__ volatile("div %0, %1, %2" : "=r"(out) : "r"(lhs), "r"(rhs));
    return out;
}

static uint32_t op_divu(uint32_t lhs, uint32_t rhs) {
    uint32_t out;
    __asm__ volatile("divu %0, %1, %2" : "=r"(out) : "r"(lhs), "r"(rhs));
    return out;
}

static uint32_t op_rem(int32_t lhs, int32_t rhs) {
    uint32_t out;
    __asm__ volatile("rem %0, %1, %2" : "=r"(out) : "r"(lhs), "r"(rhs));
    return out;
}

static uint32_t op_remu(uint32_t lhs, uint32_t rhs) {
    uint32_t out;
    __asm__ volatile("remu %0, %1, %2" : "=r"(out) : "r"(lhs), "r"(rhs));
    return out;
}

static float f_from_bits(uint32_t bits) {
    float out;
    __asm__ volatile("fmv.w.x %0, %1" : "=f"(out) : "r"(bits));
    return out;
}

static uint32_t f_bits(float value) {
    uint32_t out;
    __asm__ volatile("fmv.x.w %0, %1" : "=r"(out) : "f"(value));
    return out;
}

static uint32_t f_add(uint32_t lhs, uint32_t rhs) {
    float out;
    __asm__ volatile("fadd.s %0, %1, %2" : "=f"(out) : "f"(f_from_bits(lhs)), "f"(f_from_bits(rhs)));
    return f_bits(out);
}

static uint32_t f_sub(uint32_t lhs, uint32_t rhs) {
    float out;
    __asm__ volatile("fsub.s %0, %1, %2" : "=f"(out) : "f"(f_from_bits(lhs)), "f"(f_from_bits(rhs)));
    return f_bits(out);
}

static uint32_t f_mul(uint32_t lhs, uint32_t rhs) {
    float out;
    __asm__ volatile("fmul.s %0, %1, %2" : "=f"(out) : "f"(f_from_bits(lhs)), "f"(f_from_bits(rhs)));
    return f_bits(out);
}

static uint32_t f_div(uint32_t lhs, uint32_t rhs) {
    float out;
    __asm__ volatile("fdiv.s %0, %1, %2" : "=f"(out) : "f"(f_from_bits(lhs)), "f"(f_from_bits(rhs)));
    return f_bits(out);
}

static uint32_t f_sqrt(uint32_t value) {
    float out;
    __asm__ volatile("fsqrt.s %0, %1" : "=f"(out) : "f"(f_from_bits(value)));
    return f_bits(out);
}

static uint32_t f_sgnj(uint32_t lhs, uint32_t rhs) {
    float out;
    __asm__ volatile("fsgnj.s %0, %1, %2" : "=f"(out) : "f"(f_from_bits(lhs)), "f"(f_from_bits(rhs)));
    return f_bits(out);
}

static uint32_t f_sgnjn(uint32_t lhs, uint32_t rhs) {
    float out;
    __asm__ volatile("fsgnjn.s %0, %1, %2" : "=f"(out) : "f"(f_from_bits(lhs)), "f"(f_from_bits(rhs)));
    return f_bits(out);
}

static uint32_t f_sgnjx(uint32_t lhs, uint32_t rhs) {
    float out;
    __asm__ volatile("fsgnjx.s %0, %1, %2" : "=f"(out) : "f"(f_from_bits(lhs)), "f"(f_from_bits(rhs)));
    return f_bits(out);
}

static uint32_t f_min(uint32_t lhs, uint32_t rhs) {
    float out;
    __asm__ volatile("fmin.s %0, %1, %2" : "=f"(out) : "f"(f_from_bits(lhs)), "f"(f_from_bits(rhs)));
    return f_bits(out);
}

static uint32_t f_max(uint32_t lhs, uint32_t rhs) {
    float out;
    __asm__ volatile("fmax.s %0, %1, %2" : "=f"(out) : "f"(f_from_bits(lhs)), "f"(f_from_bits(rhs)));
    return f_bits(out);
}

static uint32_t f_le(uint32_t lhs, uint32_t rhs) {
    uint32_t out;
    __asm__ volatile("fle.s %0, %1, %2" : "=r"(out) : "f"(f_from_bits(lhs)), "f"(f_from_bits(rhs)));
    return out;
}

static uint32_t f_lt(uint32_t lhs, uint32_t rhs) {
    uint32_t out;
    __asm__ volatile("flt.s %0, %1, %2" : "=r"(out) : "f"(f_from_bits(lhs)), "f"(f_from_bits(rhs)));
    return out;
}

static uint32_t f_eq(uint32_t lhs, uint32_t rhs) {
    uint32_t out;
    __asm__ volatile("feq.s %0, %1, %2" : "=r"(out) : "f"(f_from_bits(lhs)), "f"(f_from_bits(rhs)));
    return out;
}

static uint32_t f_cvt_w(uint32_t value) {
    uint32_t out;
    __asm__ volatile("fcvt.w.s %0, %1, rtz" : "=r"(out) : "f"(f_from_bits(value)));
    return out;
}

static uint32_t f_cvt_wu(uint32_t value) {
    uint32_t out;
    __asm__ volatile("fcvt.wu.s %0, %1, rtz" : "=r"(out) : "f"(f_from_bits(value)));
    return out;
}

static uint32_t f_cvt_s_w(int32_t value) {
    float out;
    __asm__ volatile("fcvt.s.w %0, %1" : "=f"(out) : "r"(value));
    return f_bits(out);
}

static uint32_t f_cvt_s_wu(uint32_t value) {
    float out;
    __asm__ volatile("fcvt.s.wu %0, %1" : "=f"(out) : "r"(value));
    return f_bits(out);
}

static uint32_t f_class(uint32_t value) {
    uint32_t out;
    __asm__ volatile("fclass.s %0, %1" : "=r"(out) : "f"(f_from_bits(value)));
    return out;
}

static uint32_t f_madd(uint32_t a, uint32_t b, uint32_t c) {
    float out;
    __asm__ volatile("fmadd.s %0, %1, %2, %3" : "=f"(out) : "f"(f_from_bits(a)), "f"(f_from_bits(b)), "f"(f_from_bits(c)));
    return f_bits(out);
}

static uint32_t f_msub(uint32_t a, uint32_t b, uint32_t c) {
    float out;
    __asm__ volatile("fmsub.s %0, %1, %2, %3" : "=f"(out) : "f"(f_from_bits(a)), "f"(f_from_bits(b)), "f"(f_from_bits(c)));
    return f_bits(out);
}

static uint32_t f_nmsub(uint32_t a, uint32_t b, uint32_t c) {
    float out;
    __asm__ volatile("fnmsub.s %0, %1, %2, %3" : "=f"(out) : "f"(f_from_bits(a)), "f"(f_from_bits(b)), "f"(f_from_bits(c)));
    return f_bits(out);
}

static uint32_t f_nmadd(uint32_t a, uint32_t b, uint32_t c) {
    float out;
    __asm__ volatile("fnmadd.s %0, %1, %2, %3" : "=f"(out) : "f"(f_from_bits(a)), "f"(f_from_bits(b)), "f"(f_from_bits(c)));
    return f_bits(out);
}

static void test_immediates(void) {
    uint32_t out;
    __asm__ volatile("addi %0, %1, -2048" : "=r"(out) : "r"(0x800u));
    expect_u32(0x0101u, out, 0);
    __asm__ volatile("slti %0, %1, -1" : "=r"(out) : "r"(0));
    expect_u32(0x0102u, out, 0);
    __asm__ volatile("slti %0, %1, 1" : "=r"(out) : "r"(0xffffffffu));
    expect_u32(0x0103u, out, 1);
    __asm__ volatile("sltiu %0, %1, -1" : "=r"(out) : "r"(0xfffffffeu));
    expect_u32(0x0104u, out, 1);
    __asm__ volatile("xori %0, %1, -1" : "=r"(out) : "r"(0x12345678u));
    expect_u32(0x0105u, out, 0xedcba987u);
    __asm__ volatile("ori %0, %1, 0x55" : "=r"(out) : "r"(0x12340000u));
    expect_u32(0x0106u, out, 0x12340055u);
    __asm__ volatile("andi %0, %1, 0x0f0" : "=r"(out) : "r"(0xffff00ffu));
    expect_u32(0x0107u, out, 0x000000f0u);
    __asm__ volatile("slli %0, %1, 31" : "=r"(out) : "r"(1));
    expect_u32(0x0108u, out, 0x80000000u);
    __asm__ volatile("srli %0, %1, 31" : "=r"(out) : "r"(0x80000000u));
    expect_u32(0x0109u, out, 1);
    __asm__ volatile("srai %0, %1, 31" : "=r"(out) : "r"(0x80000000u));
    expect_u32(0x010au, out, 0xffffffffu);
}

static void test_register_ops(void) {
    expect_u32(0x0201u, op_add(0xffffffffu, 2), 1);
    expect_u32(0x0202u, op_sub(0, 1), 0xffffffffu);
    expect_u32(0x0203u, op_sll(1, 33), 2);
    expect_u32(0x0204u, op_slt((int32_t)0x80000000u, 0), 1);
    expect_u32(0x0205u, op_sltu(0xffffffffu, 0), 0);
    expect_u32(0x0206u, op_xor(0x55aa00ffu, 0x0f0ff0f0u), 0x5aa5f00fu);
    expect_u32(0x0207u, op_srl(0x80000000u, 31), 1);
    expect_u32(0x0208u, op_sra(0x80000000u, 31), 0xffffffffu);
    expect_u32(0x0209u, op_or(0x550000aau, 0x00aa5500u), 0x55aa55aau);
    expect_u32(0x020au, op_and(0xff00ff00u, 0x0f0f0f0fu), 0x0f000f00u);
}

static void test_m_extension(void) {
    expect_u32(0x0301u, op_mul(0x80000000u, 2), 0);
    expect_u32(0x0302u, op_mulh(-2, 0x40000000), 0xffffffffu);
    expect_u32(0x0303u, op_mulhsu(-2, 0x80000000u), 0xffffffffu);
    expect_u32(0x0304u, op_mulhu(0xffffffffu, 0xffffffffu), 0xfffffffeu);
    expect_u32(0x0305u, op_div(-7, 3), 0xfffffffeu);
    expect_u32(0x0306u, op_div(0x80000000u, 0xffffffffu), 0x80000000u);
    expect_u32(0x0307u, op_div(7, 0), 0xffffffffu);
    expect_u32(0x0308u, op_divu(0xffffffffu, 2), 0x7fffffffu);
    expect_u32(0x0309u, op_divu(7, 0), 0xffffffffu);
    expect_u32(0x030au, op_rem(-7, 3), 0xffffffffu);
    expect_u32(0x030bu, op_rem(0x80000000u, 0xffffffffu), 0);
    expect_u32(0x030cu, op_rem(7, 0), 7);
    expect_u32(0x030du, op_remu(0xffffffffu, 2), 1);
    expect_u32(0x030eu, op_remu(7, 0), 7);
}

static void test_load_store(void) {
    uint32_t base = SCRATCH_ADDR;
    uint32_t out;
    __asm__ volatile("sw %1, 0(%2)\n\tlw %0, 0(%2)" : "=r"(out) : "r"(0x807f80ffu), "r"(base) : "memory");
    expect_u32(0x0401u, out, 0x807f80ffu);
    __asm__ volatile("lb %0, 0(%1)" : "=r"(out) : "r"(base) : "memory");
    expect_u32(0x0402u, out, 0xffffffffu);
    __asm__ volatile("lbu %0, 0(%1)" : "=r"(out) : "r"(base) : "memory");
    expect_u32(0x0403u, out, 0xffu);
    __asm__ volatile("lh %0, 0(%1)" : "=r"(out) : "r"(base) : "memory");
    expect_u32(0x0404u, out, 0xffff80ffu);
    __asm__ volatile("lhu %0, 0(%1)" : "=r"(out) : "r"(base) : "memory");
    expect_u32(0x0405u, out, 0x80ffu);
    __asm__ volatile("sb %1, 4(%2)\n\tlbu %0, 4(%2)" : "=r"(out) : "r"(0x123456abu), "r"(base) : "memory");
    expect_u32(0x0406u, out, 0xabu);
    __asm__ volatile("sh %1, 6(%2)\n\tlhu %0, 6(%2)" : "=r"(out) : "r"(0xffffcdefu), "r"(base) : "memory");
    expect_u32(0x0407u, out, 0xcdefu);
}

static void test_branch_jump_upper(void) {
    uint32_t out;
    __asm__ volatile(
        "addi %0, zero, 0\n\t"
        "beq %1, %2, 1f\n\t"
        "addi %0, zero, 1\n\t"
        "1:"
        : "=&r"(out)
        : "r"(5), "r"(5));
    expect_u32(0x0501u, out, 0);
    __asm__ volatile(
        "addi %0, zero, 0\n\t"
        "bne %1, %2, 1f\n\t"
        "addi %0, zero, 1\n\t"
        "1:"
        : "=&r"(out)
        : "r"(5), "r"(5));
    expect_u32(0x0502u, out, 1);
    __asm__ volatile(
        "addi %0, zero, 0\n\t"
        "blt %1, %2, 1f\n\t"
        "addi %0, zero, 1\n\t"
        "1:"
        : "=&r"(out)
        : "r"(-1), "r"(1));
    expect_u32(0x0503u, out, 0);
    __asm__ volatile(
        "addi %0, zero, 0\n\t"
        "bge %1, %2, 1f\n\t"
        "addi %0, zero, 1\n\t"
        "1:"
        : "=&r"(out)
        : "r"(-1), "r"(1));
    expect_u32(0x0504u, out, 1);
    __asm__ volatile(
        "addi %0, zero, 0\n\t"
        "bltu %1, %2, 1f\n\t"
        "addi %0, zero, 1\n\t"
        "1:"
        : "=&r"(out)
        : "r"(1), "r"(0xffffffffu));
    expect_u32(0x0505u, out, 0);
    __asm__ volatile(
        "addi %0, zero, 0\n\t"
        "bgeu %1, %2, 1f\n\t"
        "addi %0, zero, 1\n\t"
        "1:"
        : "=&r"(out)
        : "r"(1), "r"(0xffffffffu));
    expect_u32(0x0506u, out, 1);
}

static void test_pc_ops(void) {
    uint32_t diff;
    uint32_t tmp;
    __asm__ volatile("auipc %0, 0\n\tauipc %1, 0\n\tsub %0, %1, %0" : "=&r"(diff), "=&r"(tmp));
    expect_u32(0x0601u, diff, 4);
    __asm__ volatile(
        "addi %0, zero, 0\n\t"
        "auipc t0, 0\n\t"
        "addi t0, t0, 16\n\t"
        "jalr zero, 0(t0)\n\t"
        "addi %0, zero, 1\n\t"
        "addi %0, zero, 2\n\t"
        : "=&r"(tmp)
        :
        : "t0");
    expect_u32(0x0602u, tmp, 2);
}

static void test_float_ops(void) {
    expect_u32(0x0701u, f_add(0x3fc00000u, 0x40100000u), 0x40700000u);
    expect_u32(0x0702u, f_sub(0x3fc00000u, 0x40100000u), 0xbf400000u);
    expect_u32(0x0703u, f_mul(0xc0000000u, 0x3f000000u), 0xbf800000u);
    expect_u32(0x0704u, f_div(0x40e00000u, 0x40000000u), 0x40600000u);
    expect_u32(0x0705u, f_sqrt(0x40800000u), 0x40000000u);
    expect_u32(0x0706u, f_sgnj(0xbfc00000u, 0x40000000u), 0x3fc00000u);
    expect_u32(0x0707u, f_sgnjn(0xbfc00000u, 0x40000000u), 0xbfc00000u);
    expect_u32(0x0708u, f_sgnjx(0x3fc00000u, 0xc0000000u), 0xbfc00000u);
    expect_u32(0x0709u, f_min(0xbf800000u, 0x40000000u), 0xbf800000u);
    expect_u32(0x070au, f_max(0xbf800000u, 0x40000000u), 0x40000000u);
    expect_u32(0x070bu, f_le(0x3f800000u, 0x40000000u), 1);
    expect_u32(0x070cu, f_lt(0x3f800000u, 0x40000000u), 1);
    expect_u32(0x070du, f_eq(0x3f800000u, 0x3f800000u), 1);
    expect_u32(0x070eu, f_eq(0x7fc00000u, 0x7fc00000u), 0);
    expect_u32(0x070fu, f_cvt_w(0xc0700000u), 0xfffffffdu);
    expect_u32(0x0710u, f_cvt_wu(0x40700000u), 3);
    expect_u32(0x0711u, f_cvt_s_w(-7), 0xc0e00000u);
    expect_u32(0x0712u, f_cvt_s_wu(0xffffffffu), 0x4f800000u);
    expect_u32(0x0713u, f_bits(f_from_bits(0xdeadbeefu)), 0xdeadbeefu);
    expect_u32(0x0714u, f_class(0xff800000u), 1u << 0);
    expect_u32(0x0715u, f_class(0xbf800000u), 1u << 1);
    expect_u32(0x0716u, f_class(0x80000001u), 1u << 2);
    expect_u32(0x0717u, f_class(0x80000000u), 1u << 3);
    expect_u32(0x0718u, f_class(0x00000000u), 1u << 4);
    expect_u32(0x0719u, f_class(0x00000001u), 1u << 5);
    expect_u32(0x071au, f_class(0x3f800000u), 1u << 6);
    expect_u32(0x071bu, f_class(0x7f800000u), 1u << 7);
    expect_u32(0x071cu, f_class(0x7fa00000u), 1u << 8);
    expect_u32(0x071du, f_class(0x7fc00000u), 1u << 9);
    expect_u32(0x071eu, f_madd(0x40000000u, 0x40400000u, 0x40800000u), 0x41200000u);
    expect_u32(0x071fu, f_msub(0x40000000u, 0x40400000u, 0x40800000u), 0x40000000u);
    expect_u32(0x0720u, f_nmsub(0x40000000u, 0x40400000u, 0x40800000u), 0xc0000000u);
    expect_u32(0x0721u, f_nmadd(0x40000000u, 0x40400000u, 0x40800000u), 0xc1200000u);
}

void matmul8_main(void) {
    test_immediates();
    test_register_ops();
    test_m_extension();
    test_load_store();
    test_branch_jump_upper();
    test_pc_ops();
    test_float_ops();
    pass();
}
