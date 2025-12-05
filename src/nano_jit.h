#pragma once

#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/Orc/CompileOnDemandLayer.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/IndirectionUtils.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Mangler.h"
#include "llvm/Target/TargetMachine.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace nano_jit {

// ==========================================
// NanoJit Engine (RAII Encapsulation)
// ==========================================
class NanoJit {
 public:
  /// Static factory method to create a NanoJit instance.
  /// Uses "Fail-fast" strategy: crashes if initialization fails.
  static std::unique_ptr<NanoJit> create();

  /// Destructor. Ends the execution session.
  ~NanoJit();

  /// Adds an IR module to the JIT engine.
  /// Throws std::runtime_error on failure.
  void addModule(llvm::orc::ThreadSafeModule module);

  /// Looks up a symbol by name in the JIT engine.
  /// Throws std::runtime_error on failure.
  template <typename T>
  T lookup(llvm::StringRef name);

 private:
  // Private constructor.
  NanoJit(
      std::shared_ptr<llvm::orc::SymbolStringPool> symbolStringPool,
      std::unique_ptr<llvm::orc::ExecutionSession> executionSession,
      llvm::orc::JITTargetMachineBuilder jitTargetMachineBuilder,
      llvm::DataLayout dataLayout,
      std::unique_ptr<llvm::orc::LazyCallThroughManager> lazyCallThroughManager,
      std::function<std::unique_ptr<llvm::orc::IndirectStubsManager>()>
          indirectStubsManagerBuilder);

  // Data members.
  std::shared_ptr<llvm::orc::SymbolStringPool> symbolStringPool_;
  std::unique_ptr<llvm::orc::ExecutionSession> executionSession_;
  std::unique_ptr<llvm::TargetMachine> targetMachine_;

  llvm::DataLayout dataLayout_;
  llvm::orc::MangleAndInterner mangleAndInterner_;

  std::unique_ptr<llvm::orc::ObjectLayer> objectLayer_;
  std::unique_ptr<llvm::orc::IRCompileLayer> compileLayer_;
  std::unique_ptr<llvm::orc::CompileOnDemandLayer> compileOnDemandLayer_;

  std::unique_ptr<llvm::orc::LazyCallThroughManager> lazyCallThroughManager_;
  llvm::orc::JITDylib& mainJitDylib_;
};

// ==========================================
// Singleton Wrapper
// ==========================================
class JitManager {
 public:
  /// Returns the singleton instance of NanoJit.
  static NanoJit& get();

  JitManager(const JitManager&) = delete;
  void operator=(const JitManager&) = delete;
};

// Template implementation must remain in header
template <typename T>
T NanoJit::lookup(llvm::StringRef name) {
  auto symbolOrErr =
      executionSession_->lookup({&mainJitDylib_}, mangleAndInterner_(name));

  if (!symbolOrErr) {
    std::string errMsg = toString(symbolOrErr.takeError());
    throw std::runtime_error(
        "JIT lookup failed for symbol '" + name.str() + "': " + errMsg);
  }

  return symbolOrErr->getAddress().toPtr<T>();
}

} // namespace nano_jit
