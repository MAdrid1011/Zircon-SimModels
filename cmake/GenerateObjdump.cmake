if(NOT DEFINED ZIRCON_LLVM_OBJDUMP)
    message(FATAL_ERROR "ZIRCON_LLVM_OBJDUMP is not set")
endif()

if(NOT DEFINED ZIRCON_DUMP_INPUT)
    message(FATAL_ERROR "ZIRCON_DUMP_INPUT is not set")
endif()

if(NOT DEFINED ZIRCON_DUMP_OUTPUT)
    message(FATAL_ERROR "ZIRCON_DUMP_OUTPUT is not set")
endif()

execute_process(
    COMMAND "${ZIRCON_LLVM_OBJDUMP}"
        --disassemble
        --full-contents
        --symbolize-operands
        "${ZIRCON_DUMP_INPUT}"
    OUTPUT_FILE "${ZIRCON_DUMP_OUTPUT}"
    ERROR_VARIABLE objdump_error
    RESULT_VARIABLE objdump_result
)

if(NOT objdump_result EQUAL 0)
    message(FATAL_ERROR "llvm-objdump failed: ${objdump_error}")
endif()
