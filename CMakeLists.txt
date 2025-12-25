cmake_minimum_required(VERSION 3.15)
project(NanoJitEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==========================================
# 1. Find LLVM
# ==========================================
if (APPLE)
    # Try to find llvm@19 specifically
    execute_process(COMMAND brew --prefix llvm@19 OUTPUT_VARIABLE BREW_LLVM19_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

    if (BREW_LLVM19_PREFIX)
        set(LLVM_DIR "${BREW_LLVM19_PREFIX}/lib/cmake/llvm")
    else ()
        execute_process(COMMAND brew --prefix llvm OUTPUT_VARIABLE BREW_LLVM_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if (BREW_LLVM_PREFIX)
            set(LLVM_DIR "${BREW_LLVM_PREFIX}/lib/cmake/llvm")
        endif ()
    endif ()
endif ()

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} in ${LLVM_DIR}")
message(STATUS "Native Arch: ${LLVM_NATIVE_ARCH}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# ==========================================
# 2. Component Configuration (FIXED)
# ==========================================
# Remove 'Native' from this list.
# Keep '${LLVM_NATIVE_ARCH}' which resolves to the actual backend (e.g., AArch64).
set(LLVM_COMPONENTS
        Core
        OrcJIT
        Support
        ExecutionEngine
        Target
        ${LLVM_NATIVE_ARCH}
)

# Map components to actual library names (e.g., LLVMAArch64CodeGen, LLVMOrcJIT)
llvm_map_components_to_libnames(llvm_libs ${LLVM_COMPONENTS})

# ==========================================
# 3. Build Targets
# ==========================================

# --- Core Library: NanoJit ---
add_library(nano_jit src/nano_jit.cpp)
target_include_directories(nano_jit PUBLIC src)
target_link_libraries(nano_jit PUBLIC ${llvm_libs})

# macOS Linker Fix: Ensure linker finds system libraries (zstd, ncurses, etc.)
if (APPLE)
    target_link_libraries(nano_jit PUBLIC "-L${LLVM_LIBRARY_DIRS}")
endif ()

add_executable(jit_sort src/jit_sort.cpp)
target_link_libraries(jit_sort PRIVATE nano_jit)
