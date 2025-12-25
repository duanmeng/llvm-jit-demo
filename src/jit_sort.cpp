#include "nano_jit.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::orc;

namespace nano_jit {
namespace bool_codegen {

/// ==========================================
/// 1. Metadata Definitions
/// ==========================================

/// Enumeration of supported types for JIT compilation.
enum class JITType { INT32, INT64, DOUBLE };

/// Describes the physical layout of a column in raw memory.
struct ColumnInfo {
  JITType type; ///< The data type of the column.
  int offset; ///< Byte offset from the start of the row.
  std::string name; ///< Debug name.
};

/// Defines a sorting rule for a specific column.
struct SortKey {
  int columnIndex; ///< Index into the schema.
  bool isAscending; ///< true for ASC, false for DESC.
};

/// ==========================================
/// 2. JIT Code Generation Logic
/// ==========================================
namespace {

/// Generates a specialized boolean comparison function for std::sort.
///
/// This function performs "Meta-programming": it executes C++ logic at runtime
/// to generate a specific LLVM IR function. The generated function is
/// "hardcoded" with offsets and types, eliminating runtime schema lookups and
/// branches.
///
/// Generated Signature: bool compare(char* rowA, char* rowB)
/// Returns true if rowA < rowB (strictly), false otherwise.
std::unique_ptr<ThreadSafeModule> createBoolCompareModule(
    const std::vector<ColumnInfo>& schema,
    const std::vector<SortKey>& keys) {
  auto threadSafeContext =
      std::make_unique<ThreadSafeContext>(std::make_unique<LLVMContext>());
  auto& context = *threadSafeContext->getContext();
  auto module = std::make_unique<Module>("BoolComparatorModule", context);

  module->setDataLayout(
      cantFail(JITTargetMachineBuilder(Triple(sys::getDefaultTargetTriple()))
                   .getDefaultDataLayoutForTarget()));

  IRBuilder<> builder(context);

  /// 1. Generate a unique function name based on the sort keys.
  /// Example: "cmp_0a_1d" means Column 0 Ascending, Column 1 Descending.
  std::string funcName = "cmp";
  for (const auto& k : keys) {
    funcName +=
        "_" + std::to_string(k.columnIndex) + (k.isAscending ? "a" : "d");
  }

  /// 2. Define the function signature: i1 compare(i8* a, i8* b)
  /// We use i8* (char*) to represent opaque pointers to raw row memory.
  Type* i8PtrType = PointerType::get(context, 0);
  FunctionType* funcType = FunctionType::get(
      Type::getInt1Ty(context), // Return type: bool (i1)
      {i8PtrType, i8PtrType}, // Args: char*, char*
      false);
  Function* function = Function::Create(
      funcType, Function::ExternalLinkage, funcName, module.get());

  auto* baseA = function->getArg(0);
  auto* baseB = function->getArg(1);

  BasicBlock* entryBlock = BasicBlock::Create(context, "entry", function);
  builder.SetInsertPoint(entryBlock);

  /// 3. Loop through keys to generate the comparison cascade.
  /// NOTE: This loop runs at "Compile Time". It unrolls the logic so the
  /// generated machine code contains NO loops, only a sequence of comparisons.
  for (size_t i = 0; i < keys.size(); ++i) {
    const auto& key = keys[i];
    const auto& colInfo = schema[key.columnIndex];
    int offset = colInfo.offset;

    // Create basic blocks for control flow.
    BasicBlock* checkInverseBlock =
        BasicBlock::Create(context, "check_inv_" + std::to_string(i), function);
    BasicBlock* nextKeyBlock =
        BasicBlock::Create(context, "next_" + std::to_string(i), function);
    BasicBlock* retTrueBlock =
        BasicBlock::Create(context, "ret_true_" + std::to_string(i), function);
    BasicBlock* retFalseBlock =
        BasicBlock::Create(context, "ret_false_" + std::to_string(i), function);

    /// --- Step A: Load Values ---
    /// Calculate address = base + offset.
    auto* fieldPtrA = builder.CreateConstInBoundsGEP1_32(
        Type::getInt8Ty(context), baseA, offset);
    auto* fieldPtrB = builder.CreateConstInBoundsGEP1_32(
        Type::getInt8Ty(context), baseB, offset);

    Value* valA = nullptr;
    Value* valB = nullptr;

    /// Generate specific Load instructions based on the column type.
    /// The "if" checks here happen at compile-time, so the generated code
    /// only contains the correct Load instruction.
    if (colInfo.type == JITType::INT32) {
      valA = builder.CreateLoad(Type::getInt32Ty(context), fieldPtrA);
      valB = builder.CreateLoad(Type::getInt32Ty(context), fieldPtrB);
    } else if (colInfo.type == JITType::INT64) {
      valA = builder.CreateLoad(Type::getInt64Ty(context), fieldPtrA);
      valB = builder.CreateLoad(Type::getInt64Ty(context), fieldPtrB);
    } else if (colInfo.type == JITType::DOUBLE) {
      valA = builder.CreateLoad(Type::getDoubleTy(context), fieldPtrA);
      valB = builder.CreateLoad(Type::getDoubleTy(context), fieldPtrB);
    }

    /// --- Step B: Check "Strictly Meets Condition" (Returns True) ---
    /// If this condition is met, we know A comes before B, so return true
    /// immediately.
    Value* condTrue = nullptr;

    if (colInfo.type == JITType::DOUBLE) {
      /// Floating point: Use Ordered comparison (OLT/OGT).
      /// "Ordered" means if any operand is NaN, the result is false.
      if (key.isAscending) {
        // ASC: Return true if A < B
        condTrue = builder.CreateFCmpOLT(valA, valB);
      } else {
        // DESC: Return true if A > B
        condTrue = builder.CreateFCmpOGT(valA, valB);
      }
    } else {
      /// Integer: Use Signed comparison (SLT/SGT).
      if (key.isAscending) {
        condTrue = builder.CreateICmpSLT(valA, valB);
      } else {
        condTrue = builder.CreateICmpSGT(valA, valB);
      }
    }

    // If condTrue is met, jump to return TRUE. Else check inverse condition.
    builder.CreateCondBr(condTrue, retTrueBlock, checkInverseBlock);

    /// --- Step C: Check "Strictly Opposite Condition" (Returns False) ---
    /// If this condition is met, we know B comes before A (or A > B), so return
    /// false.
    builder.SetInsertPoint(checkInverseBlock);
    Value* condFalse = nullptr;

    if (colInfo.type == JITType::DOUBLE) {
      if (key.isAscending) {
        // ASC Inverse: Return false if B < A (equivalent to A > B)
        condFalse = builder.CreateFCmpOLT(valB, valA);
      } else {
        // DESC Inverse: Return false if B > A (equivalent to A < B)
        condFalse = builder.CreateFCmpOGT(valB, valA);
      }
    } else {
      if (key.isAscending) {
        condFalse = builder.CreateICmpSLT(valB, valA);
      } else {
        condFalse = builder.CreateICmpSGT(valB, valA);
      }
    }

    // If condFalse is met, jump to return FALSE. Else continue to next key
    // (Values are Equal).
    builder.CreateCondBr(condFalse, retFalseBlock, nextKeyBlock);

    /// --- Step D: Fill Return Blocks ---
    builder.SetInsertPoint(retTrueBlock);
    builder.CreateRet(builder.getInt1(true));

    builder.SetInsertPoint(retFalseBlock);
    builder.CreateRet(builder.getInt1(false));

    /// --- Step E: Prepare for Next Loop ---
    builder.SetInsertPoint(nextKeyBlock);
  }

  /// 4. End of Function (All keys equal)
  /// If execution reaches here, it means all keys are equal (A == B).
  /// For strict weak ordering (a < b), if a == b, we must return false.
  builder.CreateRet(builder.getInt1(false));

  return std::make_unique<ThreadSafeModule>(
      std::move(module), std::move(*threadSafeContext));
}
} // namespace

} // namespace bool_codegen
} // namespace nano_jit

/// ==========================================
/// 3. Test Main
/// ==========================================
int main(int argc, char* argv[]) {
  using namespace nano_jit;
  using namespace nano_jit::bool_codegen;

  /// Define Schema: [Int32 (0-3)] [Double (4-11)]
  std::vector<ColumnInfo> schema = {
      {JITType::INT32, 0, "id"}, {JITType::DOUBLE, 4, "score"}};
  const int ROW_SIZE = 12;

  /// Mock data structure for initialization.
  struct RawData {
    int32_t id;
    double score;
  };

  std::vector<RawData> sourceData = {
      {1, 10.0},
      {1, 20.0},
      {2, 10.0},
      {1, std::numeric_limits<double>::quiet_NaN()}, // NaN case
      {1, 5.0}};

  /// Prepare raw memory pool to simulate a RowContainer (e.g., in Velox).
  std::vector<char> memoryPool;
  std::vector<char*> rowPtrs;

  for (const auto& d : sourceData) {
    size_t startIdx = memoryPool.size();
    memoryPool.resize(startIdx + ROW_SIZE);
    char* ptr = memoryPool.data() + startIdx;
    /// Manually serialize data into the buffer.
    std::memcpy(ptr + 0, &d.id, sizeof(int32_t));
    std::memcpy(ptr + 4, &d.score, sizeof(double));
    rowPtrs.push_back(ptr);
  }

  /// Fix up pointers in case resize caused reallocation.
  for (size_t i = 0; i < sourceData.size(); ++i)
    rowPtrs[i] = memoryPool.data() + (i * ROW_SIZE);

  try {
    /// Initialize JIT engine (Singleton).
    auto& jit = JitManager::get();

    /// Define Sorting Rules:
    /// 1. ID Ascending
    /// 2. Score Descending
    /// Note: NaN handling is implicit via Ordered comparisons (NaN < X is
    /// false).
    std::vector<SortKey> keys = {
        {0, true}, // id ASC
        {1, false} // score DESC
    };

    /// Compile the specialized comparison function.
    jit.addModule(std::move(*createBoolCompareModule(schema, keys)));

    /// Lookup the compiled function pointer.
    /// Function name is deterministically generated: "cmp_0a_1d"
    auto compareFn = jit.lookup<bool (*)(char*, char*)>("cmp_0a_1d");

    std::cout
        << "=== Sorting: ID ASC, Score DESC (NaNs treated as unordered/false) ===\n";

    /// Perform standard sort using the JIT-compiled comparator.
    std::sort(rowPtrs.begin(), rowPtrs.end(), compareFn);

    /// Print results by deserializing raw memory.
    for (char* ptr : rowPtrs) {
      int32_t id = *reinterpret_cast<int32_t*>(ptr + 0);
      double score = *reinterpret_cast<double*>(ptr + 4);
      std::cout << "ID: " << id << ", Score: " << score << "\n";
    }

  } catch (const std::exception& e) {
    errs() << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
