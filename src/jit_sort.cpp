#include "nano_jit.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/InitLLVM.h" // [Fix] Added missing header
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"

using namespace llvm;
using namespace llvm::orc;

namespace nano_jit {
namespace sort_codegen {

/// Business data structure for sorting.
struct Row {
  int id;
  double score;
};

// Helper function to create comparison logic for sorting.
static Value* createCompare(
    IRBuilder<>& irBuilder,
    Value* left,
    Value* right,
    StructType* rowType) {
  auto* idPtrA = irBuilder.CreateStructGEP(rowType, left, 0);
  auto* idPtrB = irBuilder.CreateStructGEP(rowType, right, 0);
  auto* idA =
      irBuilder.CreateLoad(Type::getInt32Ty(irBuilder.getContext()), idPtrA);
  auto* idB =
      irBuilder.CreateLoad(Type::getInt32Ty(irBuilder.getContext()), idPtrB);

  auto* idEq = irBuilder.CreateICmpEQ(idA, idB);
  auto* idLess = irBuilder.CreateICmpSLT(idA, idB);

  auto* scorePtrA = irBuilder.CreateStructGEP(rowType, left, 1);
  auto* scorePtrB = irBuilder.CreateStructGEP(rowType, right, 1);
  auto* scoreA = irBuilder.CreateLoad(
      Type::getDoubleTy(irBuilder.getContext()), scorePtrA);
  auto* scoreB = irBuilder.CreateLoad(
      Type::getDoubleTy(irBuilder.getContext()), scorePtrB);
  auto* scoreLess = irBuilder.CreateFCmpOLT(scoreA, scoreB);

  return irBuilder.CreateSelect(idEq, scoreLess, idLess);
}

/// Creates the bubble sort module.
std::unique_ptr<ThreadSafeModule> createBubbleSortModule() {
  auto threadSafeContext =
      std::make_unique<ThreadSafeContext>(std::make_unique<LLVMContext>());
  auto module =
      std::make_unique<Module>("SortModule", *threadSafeContext->getContext());

  module->setDataLayout(
      cantFail(JITTargetMachineBuilder(Triple(sys::getDefaultTargetTriple()))
                   .getDefaultDataLayoutForTarget()));

  auto& context = *threadSafeContext->getContext();
  IRBuilder<> builder(context);

  auto* rowType = StructType::get(
      context, {Type::getInt32Ty(context), Type::getDoubleTy(context)});
  auto* rowPtrType = PointerType::getUnqual(context);

  auto* functionType = FunctionType::get(
      Type::getVoidTy(context), {rowPtrType, Type::getInt32Ty(context)}, false);
  auto* function = Function::Create(
      functionType, Function::ExternalLinkage, "my_sort", module.get());

  auto* dataBase = function->getArg(0);
  auto* size = function->getArg(1);

  auto* entryBlock = BasicBlock::Create(context, "entry", function);
  auto* loopOuterCondBlock =
      BasicBlock::Create(context, "loop_outer_cond", function);
  auto* loopOuterBodyBlock =
      BasicBlock::Create(context, "loop_outer_body", function);
  auto* loopInnerCondBlock =
      BasicBlock::Create(context, "loop_inner_cond", function);
  auto* loopInnerBodyBlock =
      BasicBlock::Create(context, "loop_inner_body", function);
  auto* loopOuterEndBlock =
      BasicBlock::Create(context, "loop_outer_end", function);
  auto* exitBlock = BasicBlock::Create(context, "exit", function);

  builder.SetInsertPoint(entryBlock);
  auto* sizeGt1 = builder.CreateICmpSGT(size, builder.getInt32(1));
  builder.CreateCondBr(sizeGt1, loopOuterCondBlock, exitBlock);

  builder.SetInsertPoint(loopOuterCondBlock);
  auto* i = builder.CreatePHI(Type::getInt32Ty(context), 2, "i");
  i->addIncoming(builder.getInt32(0), entryBlock);
  auto* sizeMinus1 = builder.CreateSub(size, builder.getInt32(1));
  auto* outerCond = builder.CreateICmpSLT(i, sizeMinus1);
  builder.CreateCondBr(outerCond, loopOuterBodyBlock, exitBlock);

  builder.SetInsertPoint(loopOuterBodyBlock);
  builder.CreateBr(loopInnerCondBlock);

  builder.SetInsertPoint(loopInnerCondBlock);
  auto* j = builder.CreatePHI(Type::getInt32Ty(context), 2, "j");
  j->addIncoming(builder.getInt32(0), loopOuterBodyBlock);
  auto* limitInner = builder.CreateSub(sizeMinus1, i);
  auto* innerCond = builder.CreateICmpSLT(j, limitInner);
  builder.CreateCondBr(innerCond, loopInnerBodyBlock, loopOuterEndBlock);

  builder.SetInsertPoint(loopInnerBodyBlock);
  auto* ptrJ = builder.CreateGEP(rowType, dataBase, j);
  auto* jPlus1 = builder.CreateAdd(j, builder.getInt32(1));
  auto* ptrJ1 = builder.CreateGEP(rowType, dataBase, jPlus1);

  auto* shouldSwap = createCompare(builder, ptrJ1, ptrJ, rowType);

  auto* swapBlock = BasicBlock::Create(context, "swap", function);
  auto* noSwapBlock = BasicBlock::Create(context, "noswap", function);
  builder.CreateCondBr(shouldSwap, swapBlock, noSwapBlock);

  builder.SetInsertPoint(swapBlock);
  auto* idPtrJ = builder.CreateStructGEP(rowType, ptrJ, 0);
  auto* idPtrJ1 = builder.CreateStructGEP(rowType, ptrJ1, 0);
  auto* valIdJ = builder.CreateLoad(Type::getInt32Ty(context), idPtrJ);
  auto* valIdJ1 = builder.CreateLoad(Type::getInt32Ty(context), idPtrJ1);
  builder.CreateStore(valIdJ1, idPtrJ);
  builder.CreateStore(valIdJ, idPtrJ1);
  auto* scorePtrJ = builder.CreateStructGEP(rowType, ptrJ, 1);
  auto* scorePtrJ1 = builder.CreateStructGEP(rowType, ptrJ1, 1);
  auto* valScoreJ = builder.CreateLoad(Type::getDoubleTy(context), scorePtrJ);
  auto* valScoreJ1 = builder.CreateLoad(Type::getDoubleTy(context), scorePtrJ1);
  builder.CreateStore(valScoreJ1, scorePtrJ);
  builder.CreateStore(valScoreJ, scorePtrJ1);
  builder.CreateBr(noSwapBlock);

  builder.SetInsertPoint(noSwapBlock);
  auto* nextJ = builder.CreateAdd(j, builder.getInt32(1));
  j->addIncoming(nextJ, noSwapBlock);
  builder.CreateBr(loopInnerCondBlock);

  builder.SetInsertPoint(loopOuterEndBlock);
  auto* nextI = builder.CreateAdd(i, builder.getInt32(1));
  i->addIncoming(nextI, loopOuterEndBlock);
  builder.CreateBr(loopOuterCondBlock);

  builder.SetInsertPoint(exitBlock);
  builder.CreateRetVoid();

  return std::make_unique<ThreadSafeModule>(
      std::move(module), std::move(*threadSafeContext));
}

} // namespace sort_codegen
} // namespace nano_jit

int main(int argc, char* argv[]) {
  const InitLLVM initLLVM(argc, argv);

  using namespace nano_jit::sort_codegen;

  std::vector<Row> data = {{2, 5.5}, {1, 9.0}, {2, 3.3}, {1, 8.0}, {3, 1.0}};

  outs() << "=== Bubble Sort JIT Demo ===\n";
  outs() << "Before sort:\n";
  for (const auto& r : data)
    outs() << "{" << r.id << ", " << r.score << "} ";
  outs() << "\n";

  try {
    auto& jit = nano_jit::JitManager::get();
    jit.addModule(std::move(*createBubbleSortModule()));

    const auto jitSort = jit.lookup<void (*)(Row*, int)>("my_sort");
    jitSort(data.data(), data.size());
  } catch (const std::exception& e) {
    errs() << "Error: " << e.what() << "\n";
    return 1;
  }

  outs() << "After sort:\n";
  for (const auto& r : data)
    outs() << "{" << r.id << ", " << r.score << "} ";
  outs() << "\n";

  return 0;
}
