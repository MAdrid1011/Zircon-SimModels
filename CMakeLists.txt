cmake_minimum_required(VERSION 3.16)

project(ZirconSimModels
    VERSION 0.1.0
    LANGUAGES CXX
)

include(CTest)

add_library(zircon_rvcpu STATIC
    src/RVCPU.cpp
)

add_library(zircon_ddr STATIC
    src/DDR.cpp
)

add_library(Zircon::RVCPU ALIAS zircon_rvcpu)
add_library(Zircon::DDR ALIAS zircon_ddr)

foreach(target zircon_rvcpu zircon_ddr)
    target_compile_features(${target}
        PUBLIC
            cxx_std_17
    )

    target_include_directories(${target}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

option(ZIRCON_BUILD_TESTS "Build Zircon tests" ON)

if(ZIRCON_BUILD_TESTS AND BUILD_TESTING)
    find_program(ZIRCON_RISCV_CLANG
        NAMES clang
        HINTS /opt/homebrew/opt/llvm/bin
    )
    find_program(ZIRCON_LLVM_OBJCOPY
        NAMES llvm-objcopy
        HINTS /opt/homebrew/opt/llvm/bin
    )
    find_program(ZIRCON_LLVM_OBJDUMP
        NAMES llvm-objdump
        HINTS /opt/homebrew/opt/llvm/bin
    )

    if(ZIRCON_RISCV_CLANG AND ZIRCON_LLVM_OBJCOPY AND ZIRCON_LLVM_OBJDUMP)
        set(ZIRCON_TEST_PROGRAM_DIR "${CMAKE_CURRENT_BINARY_DIR}/programs")

        function(add_zircon_rv32_program target source march mabi)
            set(elf "${ZIRCON_TEST_PROGRAM_DIR}/${target}.elf")
            set(bin "${ZIRCON_TEST_PROGRAM_DIR}/${target}.bin")
            set(dump "${ZIRCON_TEST_PROGRAM_DIR}/${target}.dump")

            add_custom_command(
                OUTPUT "${elf}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${ZIRCON_TEST_PROGRAM_DIR}"
                COMMAND "${ZIRCON_RISCV_CLANG}"
                    --target=riscv32-unknown-elf
                    -fuse-ld=lld
                    -march=${march}
                    -mabi=${mabi}
                    -mno-relax
                    -msmall-data-limit=0
                    -O1
                    -ffreestanding
                    -fno-builtin
                    -fno-pic
                    -fno-pie
                    -fno-stack-protector
                    -ffp-contract=off
                    -nostdlib
                    -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/tests/programs/riscv32.ld
                    -Wl,--no-relax
                    -Wl,--build-id=none
                    -o "${elf}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/programs/start.S"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/programs/${source}"
                DEPENDS
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/programs/start.S"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/programs/${source}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/programs/riscv32.ld"
                VERBATIM
            )

            add_custom_command(
                OUTPUT "${bin}"
                COMMAND "${ZIRCON_LLVM_OBJCOPY}" -O binary
                    "${elf}"
                    "${bin}"
                DEPENDS "${elf}"
                VERBATIM
            )

            add_custom_command(
                OUTPUT "${dump}"
                COMMAND "${CMAKE_COMMAND}"
                    -DZIRCON_LLVM_OBJDUMP=${ZIRCON_LLVM_OBJDUMP}
                    -DZIRCON_DUMP_INPUT=${elf}
                    -DZIRCON_DUMP_OUTPUT=${dump}
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateObjdump.cmake"
                DEPENDS "${elf}"
                VERBATIM
            )

            add_custom_target(${target}_program
                DEPENDS "${bin}" "${dump}"
            )

            set(${target}_BIN "${bin}" PARENT_SCOPE)
        endfunction()

        add_zircon_rv32_program(matmul8_int matmul8.c rv32im ilp32)
        add_zircon_rv32_program(matmul8_float matmul8f.c rv32imf ilp32f)
        add_zircon_rv32_program(isa_rv32imf isa_rv32imf.c rv32imf ilp32f)

        add_executable(test_rvcpu_ddr_native_matmul
            tests/RVCPU_DDR_Native_Matmul.cpp
        )
        add_dependencies(test_rvcpu_ddr_native_matmul matmul8_int_program)
        target_link_libraries(test_rvcpu_ddr_native_matmul
            PRIVATE
                Zircon::RVCPU
                Zircon::DDR
        )
        target_compile_definitions(test_rvcpu_ddr_native_matmul
            PRIVATE
                ZIRCON_MATMUL8_BIN="${matmul8_int_BIN}"
        )

        add_test(NAME rvcpu_ddr_native_matmul
            COMMAND test_rvcpu_ddr_native_matmul
        )

        add_executable(test_rvcpu_ddr_native_float_matmul
            tests/RVCPU_DDR_Native_FloatMatmul.cpp
        )
        add_dependencies(test_rvcpu_ddr_native_float_matmul matmul8_float_program)
        target_link_libraries(test_rvcpu_ddr_native_float_matmul
            PRIVATE
                Zircon::RVCPU
                Zircon::DDR
        )
        target_compile_definitions(test_rvcpu_ddr_native_float_matmul
            PRIVATE
                ZIRCON_MATMUL8F_BIN="${matmul8_float_BIN}"
        )
        if(NOT MSVC)
            target_compile_options(test_rvcpu_ddr_native_float_matmul
                PRIVATE
                    -ffp-contract=off
            )
        endif()

        add_test(NAME rvcpu_ddr_native_float_matmul
            COMMAND test_rvcpu_ddr_native_float_matmul
        )

        add_executable(test_rvcpu_ddr_native_isa
            tests/RVCPU_DDR_Native_ISA.cpp
        )
        add_dependencies(test_rvcpu_ddr_native_isa isa_rv32imf_program)
        target_link_libraries(test_rvcpu_ddr_native_isa
            PRIVATE
                Zircon::RVCPU
                Zircon::DDR
        )
        target_compile_definitions(test_rvcpu_ddr_native_isa
            PRIVATE
                ZIRCON_ISA_RV32IMF_BIN="${isa_rv32imf_BIN}"
        )

        add_test(NAME rvcpu_ddr_native_isa
            COMMAND test_rvcpu_ddr_native_isa
        )
    else()
        message(WARNING "LLVM clang, llvm-objcopy, or llvm-objdump was not found. RVCPU DDR native matmul tests will not be built.")
    endif()
endif()
