#include "nano_jit.h"

#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"

#include <stdexcept>

namespace nano_jit {

using namespace llvm;
using namespace llvm::orc;

std::unique_ptr<NanoJit> NanoJit::create() {
  /// Ensure LLVM targets are initialized only once globally.
  /// This registers the native CPU architecture (e.g., x86, ARM) so LLVM can
  /// generate code for it.
  static std::once_flag initFlag;
  std::call_once(initFlag, []() {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();
  });

  auto symbolStringPool = std::make_shared<SymbolStringPool>();

  /// Create the ExecutorProcessControl to manage memory permissions in the
  /// current process.
  auto executorProcessControl =
      cantFail(SelfExecutorProcessControl::Create(symbolStringPool));

  /// Build the target machine configuration based on the host process.
  JITTargetMachineBuilder jitTargetMachineBuilder(
      executorProcessControl->getTargetTriple());

  auto dataLayout =
      cantFail(jitTargetMachineBuilder.getDefaultDataLayoutForTarget());

  auto executionSession =
      std::make_unique<ExecutionSession>(std::move(executorProcessControl));

  /// Create managers for lazy compilation stubs.
  auto lazyCallThroughManager = cantFail(createLocalLazyCallThroughManager(
      jitTargetMachineBuilder.getTargetTriple(),
      *executionSession,
      ExecutorAddr(0)));

  auto indirectStubsManagerBuilder = createLocalIndirectStubsManagerBuilder(
      jitTargetMachineBuilder.getTargetTriple());

  /// Construct the NanoJit instance using the private constructor.
  return std::unique_ptr<NanoJit>(new NanoJit(
      std::move(symbolStringPool),
      std::move(executionSession),
      std::move(jitTargetMachineBuilder),
      std::move(dataLayout),
      std::move(lazyCallThroughManager),
      std::move(indirectStubsManagerBuilder)));
}

NanoJit::NanoJit(
    std::shared_ptr<SymbolStringPool> symbolStringPool,
    std::unique_ptr<ExecutionSession> executionSession,
    JITTargetMachineBuilder jitTargetMachineBuilder,
    DataLayout dataLayout,
    std::unique_ptr<LazyCallThroughManager> lazyCallThroughManager,
    std::function<std::unique_ptr<IndirectStubsManager>()>
        indirectStubsManagerBuilder)
    : symbolStringPool_(std::move(symbolStringPool)),
      executionSession_(std::move(executionSession)),
      dataLayout_(std::move(dataLayout)),
      mangleAndInterner_(*this->executionSession_, this->dataLayout_),
      lazyCallThroughManager_(std::move(lazyCallThroughManager)),
      mainJitDylib_(this->executionSession_->createBareJITDylib("")) {
  /// Initialize the TargetMachine for code generation.
  targetMachine_ = cantFail(jitTargetMachineBuilder.createTargetMachine());

  /// Layer 1: Object Linking Layer.
  /// Links the generated object code into the running process's memory.
  objectLayer_ = std::make_unique<RTDyldObjectLinkingLayer>(
      *this->executionSession_,
      []() { return std::make_unique<SectionMemoryManager>(); });

  /// Compiler instance used by the IRCompileLayer.
  auto compiler = std::make_unique<SimpleCompiler>(*targetMachine_);

  /// Layer 2: IR Compile Layer.
  /// Compiles LLVM IR into object files.
  compileLayer_ = std::make_unique<IRCompileLayer>(
      *this->executionSession_, *objectLayer_, std::move(compiler));

  /// Layer 3: Compile On Demand Layer.
  /// Partitions modules and handles lazy compilation.
  compileOnDemandLayer_ = std::make_unique<CompileOnDemandLayer>(
      *this->executionSession_,
      *compileLayer_,
      *this->lazyCallThroughManager_,
      std::move(indirectStubsManagerBuilder));

  /// Expose symbols from the current process (e.g., C standard library
  /// functions like strcmp) to the JIT environment.
  mainJitDylib_.addGenerator(
      cantFail(DynamicLibrarySearchGenerator::GetForCurrentProcess(
          this->dataLayout_.getGlobalPrefix())));
}

NanoJit::~NanoJit() {
  if (executionSession_) {
    cantFail(executionSession_->endSession());
  }
}

void NanoJit::addModule(ThreadSafeModule module) {
  /// Add the module to the CompileOnDemandLayer.
  /// The code will not be compiled immediately, but when a symbol is first
  /// looked up or called.
  if (auto err = compileOnDemandLayer_->add(mainJitDylib_, std::move(module))) {
    std::string errMsg = toString(std::move(err));
    throw std::runtime_error("JIT addModule failed: " + errMsg);
  }
}

NanoJit& JitManager::get() {
  static std::unique_ptr<NanoJit> instance = nullptr;
  static std::once_flag flag;
  /// Thread-safe initialization of the singleton instance.
  std::call_once(flag, []() { instance = NanoJit::create(); });
  return *instance;
}

} // namespace nano_jit
