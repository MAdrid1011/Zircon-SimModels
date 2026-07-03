#include "zircon/RVCPU.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

// CSR instructions share the I-type layout under the SYSTEM opcode (0x73):
//   imm[11:0] = csr | rs1/zimm | funct3 | rd | opcode.
uint32_t encodeCsr(uint32_t csr, uint32_t rs1OrZimm, uint32_t funct3, uint32_t rd) {
    return (csr << 20) | (rs1OrZimm << 15) | (funct3 << 12) | (rd << 7) | 0x73u;
}

constexpr uint32_t kCsrrw = 0x1;
constexpr uint32_t kCsrrs = 0x2;
constexpr uint32_t kCsrrc = 0x3;
constexpr uint32_t kCsrrwi = 0x5;
constexpr uint32_t kCsrrsi = 0x6;
constexpr uint32_t kCsrrci = 0x7;

constexpr uint32_t kCsr = 0x340; // mscratch: a plain read/write scratch register.

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        ++failures;
    }
}

void step(zircon::RVCPU& cpu, uint32_t inst) {
    const zircon::StepResult result = cpu.step(inst);
    check(result.status == zircon::StepStatus::Retired, "CSR instruction should retire");
}

} // namespace

int main() {
    // CSRRW returns the old CSR value in rd and writes rs1 into the CSR.
    {
        zircon::RVCPU cpu(0);
        cpu.setGPR(1, 0xdeadbeefu);
        step(cpu, encodeCsr(kCsr, 1, kCsrrw, 2));
        check(cpu.getGPR(2) == 0u, "CSRRW reads back the initial CSR value (0)");
        check(cpu.getCSR(kCsr) == 0xdeadbeefu, "CSRRW writes rs1 into the CSR");
        check(cpu.getPC() == 4u, "CSRRW advances PC by 4");
    }

    // CSRRS sets bits; CSRRC clears bits. Both return the prior CSR value.
    {
        zircon::RVCPU cpu(0);
        cpu.setCSR(kCsr, 0x0000ff00u);
        cpu.setGPR(3, 0x000000ffu);
        step(cpu, encodeCsr(kCsr, 3, kCsrrs, 4));
        check(cpu.getGPR(4) == 0x0000ff00u, "CSRRS returns the old CSR value");
        check(cpu.getCSR(kCsr) == 0x0000ffffu, "CSRRS sets the masked bits");

        cpu.setGPR(5, 0x0000f000u);
        step(cpu, encodeCsr(kCsr, 5, kCsrrc, 6));
        check(cpu.getGPR(6) == 0x0000ffffu, "CSRRC returns the old CSR value");
        check(cpu.getCSR(kCsr) == 0x00000fffu, "CSRRC clears the masked bits");
    }

    // Immediate variants use the 5-bit zero-extended rs1 field as the source.
    {
        zircon::RVCPU cpu(0);
        step(cpu, encodeCsr(kCsr, 0x15, kCsrrwi, 1));
        check(cpu.getCSR(kCsr) == 0x15u, "CSRRWI writes the zero-extended immediate");
        check(cpu.getGPR(1) == 0u, "CSRRWI returns the initial CSR value");

        step(cpu, encodeCsr(kCsr, 0x02, kCsrrsi, 2));
        check(cpu.getCSR(kCsr) == 0x17u, "CSRRSI sets immediate bits");
        check(cpu.getGPR(2) == 0x15u, "CSRRSI returns the old CSR value");

        step(cpu, encodeCsr(kCsr, 0x04, kCsrrci, 3));
        check(cpu.getCSR(kCsr) == 0x13u, "CSRRCI clears immediate bits");
        check(cpu.getGPR(3) == 0x17u, "CSRRCI returns the old CSR value");
    }

    // CSRRS/CSRRC with rs1=x0 (and CSRRSI/CSRRCI with zimm=0) must not write the
    // CSR, but still read it into rd. This is the canonical "csrr" read pseudo-op.
    {
        zircon::RVCPU cpu(0);
        cpu.setCSR(kCsr, 0xa5a5a5a5u);
        step(cpu, encodeCsr(kCsr, 0, kCsrrs, 7)); // csrr x7, mscratch
        check(cpu.getGPR(7) == 0xa5a5a5a5u, "csrr reads the CSR into rd");
        check(cpu.getCSR(kCsr) == 0xa5a5a5a5u, "CSRRS with rs1=x0 does not write the CSR");

        step(cpu, encodeCsr(kCsr, 0, kCsrrci, 8)); // zimm=0 -> no write
        check(cpu.getCSR(kCsr) == 0xa5a5a5a5u, "CSRRCI with zimm=0 does not write the CSR");
    }

    // funct3==0 (ecall/ebreak/privileged) remains an illegal instruction.
    {
        zircon::RVCPU cpu(0);
        bool threw = false;
        try {
            cpu.step(0x00000073u); // ecall
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "ecall (SYSTEM funct3=0) throws std::invalid_argument");
    }

    if (failures != 0) {
        std::cerr << failures << " CSR check(s) failed\n";
        return 1;
    }

    std::cout << "CSR instruction tests passed\n";
    return 0;
}
