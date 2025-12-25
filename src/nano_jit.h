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
#include "llvm/Target/TargetMachine.h"

#include <functional>
#include <string>

namespace nano_jit {

/// ==========================================
/// NanoJit Engine (RAII Encapsulation)
/// ==========================================
/// A lightweight wrapper around LLVM ORC JIT v2.
/// It manages the lifetime of the JIT session, layers, and symbol tables.
class NanoJit {
 public:
  /// Static factory method to create a NanoJit instance.
  /// This method handles the initialization of LLVM targets and JIT layers.
  /// Uses a "Fail-fast" strategy: the program will crash/terminate if
  /// initialization fails.
  /// @return A unique pointer to the initialized NanoJit instance.
  static std::unique_ptr<NanoJit> create();

  /// Destructor.
  /// Safely ends the execution session and cleans up JIT resources.
  ~NanoJit();

  /// Adds an LLVM IR module to the JIT engine.
  /// The module is transferred to the JIT's ownership and compiled on demand.
  /// @param module A ThreadSafeModule containing the LLVM IR to be compiled.
  /// @throws std::runtime_error If adding the module fails.
  void addModule(llvm::orc::ThreadSafeModule module);

  /// Looks up a symbol by name in the JIT engine and casts it to the requested
  /// type. This triggers the compilation of the relevant IR if it hasn't been
  /// compiled yet.
  /// @tparam T The function pointer type to cast the result to.
  /// @param name The name of the symbol to look up (e.g., function name).
  /// @return A function pointer to the compiled machine code.
  /// @throws std::runtime_error If the symbol cannot be found.
  template <typename T>
  T lookup(llvm::StringRef name);

 private:
  /// Private constructor.
  /// Instances should be created via the static create() method.
  NanoJit(
      std::shared_ptr<llvm::orc::SymbolStringPool> symbolStringPool,
      std::unique_ptr<llvm::orc::ExecutionSession> executionSession,
      llvm::orc::JITTargetMachineBuilder jitTargetMachineBuilder,
      llvm::DataLayout dataLayout,
      std::unique_ptr<llvm::orc::LazyCallThroughManager> lazyCallThroughManager,
      std::function<std::unique_ptr<llvm::orc::IndirectStubsManager>()>
          indirectStubsManagerBuilder);

  /// Shared pool for interning string symbols.
  std::shared_ptr<llvm::orc::SymbolStringPool> symbolStringPool_;

  /// The main session manager for the JIT.
  std::unique_ptr<llvm::orc::ExecutionSession> executionSession_;

  /// Abstraction of the target hardware (CPU).
  std::unique_ptr<llvm::TargetMachine> targetMachine_;

  /// Data layout of the target architecture (endianness, pointer size,
  /// alignment).
  llvm::DataLayout dataLayout_;

  /// Utility for name mangling.
  llvm::orc::MangleAndInterner mangleAndInterner_;

  /// Layer responsible for linking object files.
  std::unique_ptr<llvm::orc::ObjectLayer> objectLayer_;

  /// Layer responsible for compiling IR to object files.
  std::unique_ptr<llvm::orc::IRCompileLayer> compileLayer_;

  /// Layer responsible for partitioning modules and compiling them lazily.
  std::unique_ptr<llvm::orc::CompileOnDemandLayer> compileOnDemandLayer_;

  /// Manager for lazy compilation callbacks.
  std::unique_ptr<llvm::orc::LazyCallThroughManager> lazyCallThroughManager_;

  /// The main JIT dynamic library where symbols are defined.
  llvm::orc::JITDylib& mainJitDylib_;
};

/// ==========================================
/// Singleton Wrapper
/// ==========================================
/// Provides a global access point to a shared NanoJit instance.
/// Thread-safe initialization is guaranteed.
class JitManager {
 public:
  /// Returns the singleton instance of NanoJit.
  /// Initializes the instance on the first call.
  static NanoJit& get();

  JitManager(const JitManager&) = delete;
  void operator=(const JitManager&) = delete;
};

/// Template implementation for symbol lookup.
template <typename T>
T NanoJit::lookup(llvm::StringRef name) {
  /// Perform lookup in the main JIT dynamic library.
  auto symbolOrErr =
      executionSession_->lookup({&mainJitDylib_}, mangleAndInterner_(name));

  if (!symbolOrErr) {
    std::string errMsg = toString(symbolOrErr.takeError());
    throw std::runtime_error(
        "JIT lookup failed for symbol '" + name.str() + "': " + errMsg);
  }

  /// Convert the generic executor address to a specific function pointer.
  return symbolOrErr->getAddress().toPtr<T>();
}

} // namespace nano_jit
