## NanoJit: High-Performance LLVM ORC JIT Engine
NanoJit is a lightweight, production-ready JIT compilation infrastructure built on top of
LLVM ORC v2 (On-Request Compilation). It is designed to serve as the dynamic code generation
backend for distributed query engines (e.g., Presto, Velox), providing a robust pipeline to
transform LLVM IR into executable machine code at runtime.

### 1. Architecture & Design
NanoJit abstracts the complexity of the raw LLVM ORC layer hierarchy into a cohesive Execution
Session Container. Its design focuses on resource isolation, lazy materialization, and strict
lifecycle management.

#### 1.1 The Compilation Pipeline
NanoJit constructs a specific layer stack designed for Lazy Compilation, ensuring that code is
only compiled when it is actually executed. This is critical for complex query plans where many
generated functions might never be called on specific data paths.

```mermaid
graph TD
    User[User Code] -->|addModule| COD
    subgraph NanoJit Engine
        ES[ExecutionSession]
        subgraph Layer Stack
            COD[CompileOnDemandLayer] -->|Partitioning| Compile
            Compile[IRCompileLayer] -->|SimpleCompiler| Object
            Object[RTDyldObjectLinkingLayer] -->|Link/Load| Mem[SectionMemoryManager]
        end
    end
    Mem -->|Executable Address| User

```
#### 1.2 Layer Hierarchy & Pipeline Implementation
NanoJit constructs a specific "Lazy Compilation" layer stack. Data flows from high-level IR to
executable memory through the following components:

1. `CompileOnDemandLayer` (The Lazy Gatekeeper)
- Implementation: This is the top-level layer. When `addModule()` is called, this layer does not
compile the code. Instead, it extracts the function declarations and installs Stubs (trampolines)
in the symbol table.
- Behavior: Compilation is triggered only when a function is called for the first time (via the stub).
This significantly reduces startup latency for queries with many conditional branches.
2. `IRCompileLayer` (The Compiler)
- Implementation: Wraps `llvm::orc::SimpleCompiler`.
- TargetMachine Management: NanoJit explicitly owns the `TargetMachine` via `std::unique_ptr`.
This is crucial because `SimpleCompiler` holds a reference to it, and the `TargetMachine` must
outlive the compiler to avoid dangling pointer crashes (a common pitfall in LLVM 19+).
3. `RTDyldObjectLinkingLayer` (The Linker)
- Implementation: Uses `RuntimeDyld` to link generated Object Files into memory.
- Memory Management: Configured with a `SectionMemoryManager`, which allocates executable memory
pages (RWX or RX) required for code execution.
4. `ExecutionSession` & `JITDylib`
- Session: The context holding string pools and global error states.
- JITDylib: Acts as a dynamic library symbol table. NanoJit configures a `DynamicLibrarySearchGenerator`
to allow JIT-ed code to resolve symbols from the host process (e.g., calling `printf` or C++ runtime functions).
#### 1.3 Error Handling Strategy
- Initialization (Fail-Fast): Critical components (NativeTarget, Layer creation) use `llvm::cantFail`.
If the environment is invalid (e.g., unsupported Arch), the process crashes immediately to prevent "Zombie Nodes" in a cluster.
- Runtime (Exceptions): `addModule` and `lookup` throw `std::runtime_error`. This ensures that a bad
query (malformed IR) only fails the specific request, isolating the fault from the rest of the worker process.
### 2. Singleton Manager: JitManager
The `JitManager` class provides a thread-safe, global access point to the JIT engine.
#### 2.1 Design & Implementation
- Pattern: Meyers' Singleton.
- Thread Safety: Uses `std::call_once` and `std::once_flag` to ensure initialization happens exactly once, even under high concurrency.
- Initialization Logic:
    1. `InitializeNativeTarget()`: Sets up the target architecture (e.g., AArch64, X86).
    2. `InitializeNativeTargetAsmPrinter()`: Enables assembly printing (required for code emission).
    3. `NanoJit::create()`: Instantiates the engine.
       This design ensures that the heavy lifting of LLVM target initialization occurs only on the first use, keeping the application startup fast.
### 3. Implementation Scenario: jit_sum (Expression Evaluation)
This module demonstrates how to JIT-compile mathematical expressions for different data types, simulating a SQL projection or aggregation scenario.
#### 3.1 IR Generation Details
The code uses `IRBuilder` to generate three distinct functions:
1. Integer Arithmetic (`sum_int`):
- Signature: `i32 (i32, i32)`
- IR: Generates an `add` instruction.
- Use Case: Simple integer counters or ID manipulation.
2. Floating Point Arithmetic (`sum_double`):
- Signature: `double (double, double)`
- IR: Generates an `fadd` instruction.
- Use Case: Scientific calculations or financial metrics.
3. Struct Manipulation (`sum_struct`):
- Signature: `void (ComplexStruct*, ComplexStruct*, ComplexStruct*)`
- IR Logic:
    - Uses `CreateStructGEP` (GetElementPtr) to calculate memory offsets for fields `a` (int) and `b` (double).
    - Loads values from input pointers.
    - Performs mixed-type arithmetic.
    - Stores results back to the result pointer.
- Significance: Demonstrates ABI compatibility between JIT-compiled code and host C++ structs.
### 4. Implementation Scenario: jit_sort (Algorithmic Logic)
This module demonstrates compiling complex control flow (loops, branches) to perform an in-memory Bubble
Sort on a dataset. This simulates a custom operator or UDF (User Defined Function) in a database engine.
#### 4.1 Data Structure
The JIT engine interacts with a C++ struct:

```C++
struct Row {
  int id;      // Primary Sort Key
  double score; // Secondary Sort Key
};

```

#### 4.2 IR Generation Logic (createBubbleSortModule)
The implementation manually constructs the Control Flow Graph (CFG) for the algorithm:
1. Basic Blocks:
- `entry`: Function entry.
- `loop_outer_cond` / `loop_outer_body`: Controls the `i` loop.
- `loop_inner_cond` / `loop_inner_body`: Controls the `j` loop.
- `swap` / `noswap`: Conditional execution based on comparison.
2. PHI Nodes:
- Uses `CreatePHI` to manage loop variables (`i` and `j`). This is essential in SSA (Static Single Assignment) form to handle variable updates across loop iterations.
3. Comparison Logic (`createCompare`):
- Implements a multi-key comparator.
- First compares `id`. If equal, compares `score`.
- Uses `CreateSelect` to implement the conditional logic without branching (branchless optimization for the comparator itself).
4. Memory Access:
- Calculates array offsets using `CreateGEP`.
- Swaps elements by loading all fields into registers and storing them back to swapped addresses.
#### 4.3 Execution Flow
1. Host C++ code creates a `std::vector<Row>`.
2. `NanoJit` compiles the `my_sort` function.
3. Host calls `lookup` to get the function pointer `void (*)(Row*, int)`.
4. The JIT-compiled machine code modifies the host memory directly, sorting the vector in place.

### 5. How To Integration

```C++
#include "nano_jit.h"
#include "llvm/IR/IRBuilder.h"
// ... include other LLVM IR headers ...

using namespace nano_jit;
using namespace llvm;
using namespace llvm::orc;

void executeJitTask() {
    // 1. Get the JIT Instance
    auto& jit = JitManager::get();

    // 2. Create Module & Context (ThreadSafe)
    auto tsCtx = std::make_unique<ThreadSafeContext>(std::make_unique<LLVMContext>());
    auto module = std::make_unique<Module>("MyModule", *tsCtx->getContext());
    
    // ... (Populate Module with IRBuilder) ...

    // 3. Add to JIT (Transfer ownership)
    try {
        jit.addModule(ThreadSafeModule(std::move(module), std::move(*tsCtx)));
        
        // 4. Lookup and Execute
        auto funcPtr = jit.lookup<int (*)(int)>("my_compute_function");
        int result = funcPtr(42);
        
    } catch (const std::exception& e) {
        // Handle runtime failure (query specific)
        std::cerr << "JIT Error: " << e.what() << std::endl;
    }
}

```