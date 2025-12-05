#include "nano_jit.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/InitLLVM.h" // [Fix] Added missing header
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"

using namespace llvm;
using namespace llvm::orc;

namespace nano_jit {
namespace expr_codegen {

/// Business data structure for expression evaluation.
struct ComplexStruct {
  int a;
  double b;
};

/// Creates the integer sum module.
std::unique_ptr<ThreadSafeModule> createSumInt() {
  auto threadSafeContext =
      std::make_unique<ThreadSafeContext>(std::make_unique<LLVMContext>());
  auto module =
      std::make_unique<Module>("SumIntMod", *threadSafeContext->getContext());
  module->setDataLayout(
      cantFail(JITTargetMachineBuilder(Triple(sys::getDefaultTargetTriple()))
                   .getDefaultDataLayoutForTarget()));
  auto& context = *threadSafeContext->getContext();
  IRBuilder<> builder(context);

  auto* int32Type = Type::getInt32Ty(context);
  auto* functionType =
      FunctionType::get(int32Type, {int32Type, int32Type}, false);
  auto* function = Function::Create(
      functionType, Function::ExternalLinkage, "sum_int", module.get());

  auto* entryBlock = BasicBlock::Create(context, "entry", function);
  builder.SetInsertPoint(entryBlock);
  builder.CreateRet(
      builder.CreateAdd(function->getArg(0), function->getArg(1)));

  return std::make_unique<ThreadSafeModule>(
      std::move(module), std::move(*threadSafeContext));
}

/// Creates the double sum module.
std::unique_ptr<ThreadSafeModule> createSumDouble() {
  auto threadSafeContext =
      std::make_unique<ThreadSafeContext>(std::make_unique<LLVMContext>());
  auto module = std::make_unique<Module>(
      "SumDoubleMod", *threadSafeContext->getContext());
  module->setDataLayout(
      cantFail(JITTargetMachineBuilder(Triple(sys::getDefaultTargetTriple()))
                   .getDefaultDataLayoutForTarget()));
  auto& context = *threadSafeContext->getContext();
  IRBuilder<> builder(context);

  auto* doubleType = Type::getDoubleTy(context);
  auto* functionType =
      FunctionType::get(doubleType, {doubleType, doubleType}, false);
  auto* function = Function::Create(
      functionType, Function::ExternalLinkage, "sum_double", module.get());

  auto* entryBlock = BasicBlock::Create(context, "entry", function);
  builder.SetInsertPoint(entryBlock);
  builder.CreateRet(
      builder.CreateFAdd(function->getArg(0), function->getArg(1)));

  return std::make_unique<ThreadSafeModule>(
      std::move(module), std::move(*threadSafeContext));
}

/// Creates the struct sum module.
std::unique_ptr<ThreadSafeModule> createSumStruct() {
  auto threadSafeContext =
      std::make_unique<ThreadSafeContext>(std::make_unique<LLVMContext>());
  auto module = std::make_unique<Module>(
      "SumStructMod", *threadSafeContext->getContext());
  module->setDataLayout(
      cantFail(JITTargetMachineBuilder(Triple(sys::getDefaultTargetTriple()))
                   .getDefaultDataLayoutForTarget()));
  auto& context = *threadSafeContext->getContext();
  IRBuilder<> builder(context);

  auto* structType = StructType::get(
      context, {Type::getInt32Ty(context), Type::getDoubleTy(context)});
  auto* ptrType = PointerType::getUnqual(context); // Opaque Pointer

  // void (ptr result, ptr a, ptr b)
  auto* functionType = FunctionType::get(
      Type::getVoidTy(context), {ptrType, ptrType, ptrType}, false);
  auto* function = Function::Create(
      functionType, Function::ExternalLinkage, "sum_struct", module.get());

  auto* entryBlock = BasicBlock::Create(context, "entry", function);
  builder.SetInsertPoint(entryBlock);

  auto* resPtr = function->getArg(0);
  auto* aPtr = function->getArg(1);
  auto* bPtr = function->getArg(2);

  // int a = a->a + b->a
  auto* aVal = builder.CreateLoad(
      Type::getInt32Ty(context), builder.CreateStructGEP(structType, aPtr, 0));
  auto* bVal = builder.CreateLoad(
      Type::getInt32Ty(context), builder.CreateStructGEP(structType, bPtr, 0));
  builder.CreateStore(
      builder.CreateAdd(aVal, bVal),
      builder.CreateStructGEP(structType, resPtr, 0));

  // double b = a->b + b->b
  auto* aValD = builder.CreateLoad(
      Type::getDoubleTy(context), builder.CreateStructGEP(structType, aPtr, 1));
  auto* bValD = builder.CreateLoad(
      Type::getDoubleTy(context), builder.CreateStructGEP(structType, bPtr, 1));
  builder.CreateStore(
      builder.CreateFAdd(aValD, bValD),
      builder.CreateStructGEP(structType, resPtr, 1));

  builder.CreateRetVoid();

  return std::make_unique<ThreadSafeModule>(
      std::move(module), std::move(*threadSafeContext));
}

} // namespace expr_codegen
} // namespace nano_jit

int main(int argc, char* argv[]) {
  const InitLLVM initLLVM(argc, argv);
  outs() << "=== Expression Sum JIT Demo ===\n";

  // Import the specific codegen namespace
  using namespace nano_jit::expr_codegen;

  try {
    auto& jit = nano_jit::JitManager::get();

    // Compile all modules
    jit.addModule(std::move(*createSumInt()));
    jit.addModule(std::move(*createSumDouble()));
    jit.addModule(std::move(*createSumStruct()));

    // 1. Int
    const auto sumInt = jit.lookup<int (*)(int, int)>("sum_int");
    outs() << "[INT] 10 + 32 = " << sumInt(10, 32) << "\n";

    // 2. Double
    const auto sumDouble = jit.lookup<double (*)(double, double)>("sum_double");
    outs() << "[DOUBLE] 3.14 + 2.71 = " << sumDouble(3.14, 2.71) << "\n";

    // 3. Struct
    const auto sumStruct =
        jit.lookup<void (*)(ComplexStruct*, ComplexStruct*, ComplexStruct*)>(
            "sum_struct");
    ComplexStruct s1 = {100, 1.5};
    ComplexStruct s2 = {200, 2.5};
    ComplexStruct res;
    sumStruct(&res, &s1, &s2);
    outs() << "[STRUCT] {100, 1.5} + {200, 2.5} = {" << res.a << ", " << res.b
           << "}\n";
  } catch (const std::exception& e) {
    errs() << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
