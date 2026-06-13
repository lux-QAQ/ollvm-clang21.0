// For open-source license, please refer to
// [License](https://github.com/HikariObfuscator/Hikari/wiki/License).
//===----------------------------------------------------------------------===//
#include "llvm/IR/Instructions.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Obfuscation/CryptoUtils.h"
#include "llvm/Transforms/Obfuscation/Split.h"
#include "llvm/Transforms/Obfuscation/Utils.h"
#include "support/IRUtils.h"
#include <algorithm>

using namespace llvm;

static cl::opt<uint32_t> SplitNum("split_num", cl::init(2),
                                  cl::desc("Split <split_num> time each BB"));
static uint32_t SplitNumTemp = 2;

namespace {
struct SplitBasicBlock : public FunctionPass {
  static char ID; // Pass identification, replacement for typeid
  bool flag;
  SplitBasicBlock() : FunctionPass(ID) { this->flag = true; }
  SplitBasicBlock(bool flag) : FunctionPass(ID) { this->flag = flag; }

  bool runOnFunction(Function &F) override {
    if (!toObfuscateUint32Option(&F, "split_num", &SplitNumTemp))
      SplitNumTemp = SplitNum;

    // Check if the number of applications is correct
    if (!((SplitNumTemp > 1) && (SplitNumTemp <= 10))) {
      errs()
          << "Split application basic block percentage -split_num=x must be 1 "
             "< x <= 10";
      return false;
    }

    // Do we obfuscate
    if (toObfuscate(flag, &F, "split")) {
      errs() << "Running BasicBlockSplit On " << F.getName() << "\n";
      split(&F);
    }

    return true;
  }
  void split(Function *F) {
    SmallVector<BasicBlock *, 16> origBB;
    size_t split_ctr = 0;

    // Save all basic blocks
    for (BasicBlock &BB : *F)
      origBB.emplace_back(&BB);

    for (BasicBlock *currBB : origBB) {
      if (currBB->size() < 2 || containsPHI(currBB) ||
          containsSwiftError(currBB))
        continue;

      // Generate splits point (count number of the LLVM instructions in the
      // current BB)
      SmallVector<size_t, 32> llvm_inst_ord;
      size_t first_split_ord = 1;
      if (currBB->isEntryBlock())
        first_split_ord =
            std::max(first_split_ord, getEntryAllocaInsertIndex(*F));

      for (size_t i = first_split_ord; i < currBB->size(); ++i)
        llvm_inst_ord.emplace_back(i);
      if (llvm_inst_ord.empty())
        continue;

      if ((size_t)SplitNumTemp > llvm_inst_ord.size())
        split_ctr = llvm_inst_ord.size();
      else
        split_ctr = (size_t)SplitNumTemp;

      // Shuffle
      split_point_shuffle(llvm_inst_ord);
      std::sort(llvm_inst_ord.begin(), llvm_inst_ord.begin() + split_ctr);

      // Split
      size_t llvm_inst_prev_offset = 0;
      BasicBlock::iterator curr_bb_it = currBB->begin();
      BasicBlock *curr_bb_offset = currBB;

      for (size_t i = 0; i < split_ctr; ++i) {
        for (size_t j = 0; j < llvm_inst_ord[i] - llvm_inst_prev_offset; ++j)
          ++curr_bb_it;

        llvm_inst_prev_offset = llvm_inst_ord[i];

        curr_bb_offset = curr_bb_offset->splitBasicBlock(
            curr_bb_it, curr_bb_offset->getName() + ".split");
      }
    }
  }

  bool containsPHI(BasicBlock *BB) {
    for (Instruction &I : *BB)
      if (isa<PHINode>(&I))
        return true;
    return false;
  }

  bool containsSwiftError(BasicBlock *BB) {
    for (Instruction &I : *BB)
      if (AllocaInst *AI = dyn_cast<AllocaInst>(&I))
        if (AI->isSwiftError())
          return true;
    return false;
  }

  void split_point_shuffle(SmallVector<size_t, 32> &vec) {
    int n = vec.size();
    for (int i = n - 1; i > 0; --i)
      std::swap(vec[i], vec[cryptoutils->get_range(i + 1)]);
  }
};
} // namespace

char SplitBasicBlock::ID = 0;
INITIALIZE_PASS(SplitBasicBlock, "splitobf", "Enable BasicBlockSpliting.",
                false, false)

FunctionPass *llvm::createSplitBasicBlockPass(bool flag) {
  return new SplitBasicBlock(flag);
}
