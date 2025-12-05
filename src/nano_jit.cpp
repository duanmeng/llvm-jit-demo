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
  auto symbolStringPool = std::make_shared<SymbolStringPool>();
  auto executorProcessControl =
      cantFail(SelfExecutorProcessControl::Create(symbolStringPool));

  JITTargetMachineBuilder jitTargetMachineBuilder(
      executorProcessControl->getTargetTriple());
  auto dataLayout =
      cantFail(jitTargetMachineBuilder.getDefaultDataLayoutForTarget());
  auto executionSession =
      std::make_unique<ExecutionSession>(std::move(executorProcessControl));

  auto lazyCallThroughManager = cantFail(createLocalLazyCallThroughManager(
      jitTargetMachineBuilder.getTargetTriple(),
      *executionSession,
      ExecutorAddr(0)));
  auto indirectStubsManagerBuilder = createLocalIndirectStubsManagerBuilder(
      jitTargetMachineBuilder.getTargetTriple());

  // Use new (private constructor)
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
  // Initialize TargetMachine
  targetMachine_ = cantFail(jitTargetMachineBuilder.createTargetMachine());

  objectLayer_ = std::make_unique<RTDyldObjectLinkingLayer>(
      *this->executionSession_,
      []() { return std::make_unique<SectionMemoryManager>(); });

  // SimpleCompiler requires a reference to TargetMachine
  auto compiler = std::make_unique<SimpleCompiler>(*targetMachine_);

  compileLayer_ = std::make_unique<IRCompileLayer>(
      *this->executionSession_, *objectLayer_, std::move(compiler));

  compileOnDemandLayer_ = std::make_unique<CompileOnDemandLayer>(
      *this->executionSession_,
      *compileLayer_,
      *this->lazyCallThroughManager_,
      std::move(indirectStubsManagerBuilder));

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
  if (auto err = compileOnDemandLayer_->add(mainJitDylib_, std::move(module))) {
    std::string errMsg = toString(std::move(err));
    throw std::runtime_error("JIT addModule failed: " + errMsg);
  }
}

NanoJit& JitManager::get() {
  static std::unique_ptr<NanoJit> instance = nullptr;
  static std::once_flag flag;
  std::call_once(flag, []() {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    instance = NanoJit::create();
  });
  return *instance;
}

} // namespace nano_jit
