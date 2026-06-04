cmake_minimum_required(VERSION 3.16)

project(Zircon
    VERSION 0.1.0
    LANGUAGES CXX
)

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
