#include "IRUtils.h"

#include <cassert>
#include <iterator>

using namespace llvm;

namespace llvm {

Instruction *getEntryAllocaInsertBefore(Function &F) {
  BasicBlock &Entry = F.getEntryBlock();
  BasicBlock::iterator It = Entry.getFirstNonPHIOrDbgOrLifetime();

  while (It != Entry.end() && isa<AllocaInst>(&*It))
    ++It;

  if (It == Entry.end())
    return Entry.getTerminator();

  return &*It;
}

std::size_t getEntryAllocaInsertIndex(Function &F) {
  BasicBlock &Entry = F.getEntryBlock();
  Instruction *InsertBefore = getEntryAllocaInsertBefore(F);
  if (!InsertBefore)
    return Entry.size();

  return std::distance(Entry.begin(), InsertBefore->getIterator());
}

AllocaInst *createEntryBlockAlloca(Function &F, Type *Ty, unsigned AddrSpace,
                                   const Twine &Name, Value *ArraySize) {
  Instruction *InsertBefore = getEntryAllocaInsertBefore(F);
  assert(InsertBefore && "expected a well-formed function entry block");
  return new AllocaInst(Ty, AddrSpace, ArraySize, Name,
                        InsertBefore->getIterator());
}

} // namespace llvm
