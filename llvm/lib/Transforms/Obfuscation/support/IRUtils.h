#ifndef LLVM_TRANSFORMS_OBFUSCATION_SUPPORT_IRUTILS_H
#define LLVM_TRANSFORMS_OBFUSCATION_SUPPORT_IRUTILS_H

#include "llvm/ADT/Twine.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include <cstddef>

namespace llvm {

Instruction *getEntryAllocaInsertBefore(Function &F);
std::size_t getEntryAllocaInsertIndex(Function &F);
AllocaInst *createEntryBlockAlloca(Function &F, Type *Ty, unsigned AddrSpace,
                                   const Twine &Name = "",
                                   Value *ArraySize = nullptr);

} // namespace llvm

#endif
