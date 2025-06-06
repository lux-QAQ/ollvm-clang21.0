// For open-source license, please refer to
// [License](https://github.com/HikariObfuscator/Hikari/wiki/License).
//===----------------------------------------------------------------------===//
//===- BogusControlFlow.cpp - BogusControlFlow Obfuscation
// pass-------------------------===//
//
// This file implements BogusControlFlow's pass, inserting bogus control flow.
// It adds bogus flow to a given basic block this way:
//
// Before :
// 	         		     entry
//      			       |
//  	    	  	 ______v______
//   	    		|   Original  |
//   	    		|_____________|
//             		       |
// 		        	       v
//		        	     return
//
// After :
//           		     entry
//             		       |
//            		   ____v_____
//      			  |condition*| (false)
//           		  |__________|----+
//           		 (true)|          |
//             		       |          |
//           		 ______v______    |
// 		        +-->|   Original* |   |
// 		        |   |_____________| (true)
// 		        |   (false)|    !-----------> return
// 		        |    ______v______    |
// 		        |   |   Altered   |<--!
// 		        |   |_____________|
// 		        |__________|
//
//  * The results of these terminator's branch's conditions are always true, but
//  these predicates are
//    opacificated. For this, we declare two global values: x and y, and replace
//    the FCMP_TRUE predicate with (y < 10 || x * (x + 1) % 2 == 0) (this could
//    be improved, as the global values give a hint on where are the opaque
//    predicates)
//
//  The altered bloc is a copy of the original's one with junk instructions
//  added accordingly to the type of instructions we found in the bloc
//
//  Each basic block of the function is choosen if a random number in the range
//  [0,100] is smaller than the choosen probability rate. The default value
//  is 30. This value can be modify using the option -boguscf-prob=[value].
//  Value must be an integer in the range [0, 100], otherwise the default value
//  is taken. Exemple: -boguscf -boguscf-prob=60
//
//  The pass can also be loop many times on a function, including on the basic
//  blocks added in a previous loop. Be careful if you use a big probability
//  number and choose to run the loop many times wich may cause the pass to run
//  for a very long time. The default value is one loop, but you can change it
//  with -boguscf-loop=[value]. Value must be an integer greater than 1,
//  otherwise the default value is taken. Exemple: -boguscf -boguscf-loop=2
//
//
//  Defined debug types:
//  - "gen" : general informations
//  - "opt" : concerning the given options (parameter)
//  - "cfg" : printing the various function's cfg before transformation
//	      and after transformation if it has been modified, and all
//	      the functions at end of the pass, after doFinalization.
//
//  To use them all, simply use the -debug option.
//  To use only one of them, follow the pass' command by -debug-only=name.
//  Exemple, -boguscf -debug-only=cfg
//
//
//  Stats:
//  The following statistics will be printed if you use
//  the -stats command:
//
// a. Number of functions in this module
// b. Number of times we run on each function
// c. Initial number of basic blocks in this module
// d. Number of modified basic blocks
// e. Number of added basic blocks in this module
// f. Final number of basic blocks in this module
//
// file   : lib/Transforms/Obfuscation/BogusControlFlow.cpp
// date   : june 2012
// version: 1.0
// author : julie.michielin@gmail.com
// modifications: pjunod, Rinaldini Julien
// project: Obfuscator
// option : -boguscf
//
//===----------------------------------------------------------------------------------===//

#include "llvm/Transforms/Obfuscation/BogusControlFlow.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Transforms/Obfuscation/CryptoUtils.h"
#include "llvm/Transforms/Obfuscation/Utils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <memory> // Required for std::unique_ptr

using namespace llvm;

// Options for the pass
static const uint32_t defaultObfRate = 70, defaultObfTime = 1;

static cl::opt<uint32_t>
    ObfProbRate("bcf_prob",
                cl::desc("Choose the probability [%] each basic blocks will be "
                         "obfuscated by the -bcf pass"),
                cl::value_desc("probability rate"), cl::init(defaultObfRate),
                cl::Optional);
static uint32_t ObfProbRateTemp = defaultObfRate;

static cl::opt<uint32_t>
    ObfTimes("bcf_loop",
             cl::desc("Choose how many time the -bcf pass loop on a function"),
             cl::value_desc("number of times"), cl::init(defaultObfTime),
             cl::Optional);
static uint32_t ObfTimesTemp = defaultObfTime;

static cl::opt<uint32_t> ConditionExpressionComplexity(
    "bcf_cond_compl",
    cl::desc("The complexity of the expression used to generate branching "
             "condition"),
    cl::value_desc("Complexity"), cl::init(3), cl::Optional);
static uint32_t ConditionExpressionComplexityTemp = 3;

static cl::opt<bool>
    OnlyJunkAssembly("bcf_onlyjunkasm",
                     cl::desc("only add junk assembly to altered basic block"),
                     cl::value_desc("only add junk assembly"), cl::init(false),
                     cl::Optional);
static bool OnlyJunkAssemblyTemp = false;

static cl::opt<bool> JunkAssembly(
    "bcf_junkasm",
    cl::desc("Whether to add junk assembly to altered basic block"),
    cl::value_desc("add junk assembly"), cl::init(false), cl::Optional);
static bool JunkAssemblyTemp = false;

static cl::opt<uint32_t> MaxNumberOfJunkAssembly(
    "bcf_junkasm_maxnum",
    cl::desc("The maximum number of junk assembliy per altered basic block"),
    cl::value_desc("max number of junk assembly"), cl::init(4), cl::Optional);
static uint32_t MaxNumberOfJunkAssemblyTemp = 4;

static cl::opt<uint32_t> MinNumberOfJunkAssembly(
    "bcf_junkasm_minnum",
    cl::desc("The minimum number of junk assembliy per altered basic block"),
    cl::value_desc("min number of junk assembly"), cl::init(2), cl::Optional);
static uint32_t MinNumberOfJunkAssemblyTemp = 2;

static cl::opt<bool> CreateFunctionForOpaquePredicate(
    "bcf_createfunc", cl::desc("Create function for each opaque predicate"),
    cl::value_desc("create function"), cl::init(false), cl::Optional);
static bool CreateFunctionForOpaquePredicateTemp = false;

static const Instruction::BinaryOps ops[] = {
    Instruction::Add, Instruction::Sub, Instruction::And, Instruction::Or,
    Instruction::Xor, Instruction::Mul, Instruction::UDiv};
static const CmpInst::Predicate preds[] = {
    CmpInst::ICMP_EQ,  CmpInst::ICMP_NE,  CmpInst::ICMP_UGT,
    CmpInst::ICMP_UGE, CmpInst::ICMP_ULT, CmpInst::ICMP_ULE};

namespace llvm {
static bool OnlyUsedBy(Value *V, Value *Usr) {
  for (User *U : V->users())
    if (U != Usr)
      return false;
  return true;
}
static void RemoveDeadConstant(Constant *C) {
  assert(C->use_empty() && "Constant is not dead!");
  SmallVector<Constant *, 4> Operands;
  for (Value *Op : C->operands())
    if (OnlyUsedBy(Op, C))
      Operands.emplace_back(cast<Constant>(Op));
  if (GlobalVariable *GV = dyn_cast<GlobalVariable>(C)) {
    if (!GV->hasLocalLinkage())
      return; // Don't delete non-static globals.
    GV->eraseFromParent();
  } else if (!isa<Function>(C))
    if (isa<ArrayType>(C->getType()) || isa<StructType>(C->getType()) ||
        isa<VectorType>(C->getType()))
      C->destroyConstant();

  // If the constant referenced anything, see if we can delete it as well.
  for (Constant *O : Operands)
    RemoveDeadConstant(O);
}
struct BogusControlFlow : public FunctionPass {
  static char ID; // Pass identification
  bool flag;
  SmallVector<const ICmpInst *, 8> needtoedit;
  BogusControlFlow() : FunctionPass(ID) { this->flag = true; }
  BogusControlFlow(bool flag) : FunctionPass(ID) { this->flag = flag; }
  /* runOnFunction
   *
   * Overwrite FunctionPass method to apply the transformation
   * to the function. See header for more details.
   */
  bool runOnFunction(Function &F) override {
    if (!toObfuscateUint32Option(&F, "bcf_loop", &ObfTimesTemp))
      ObfTimesTemp = ObfTimes;

    // Check if the percentage is correct
    if (ObfTimesTemp <= 0) {
      errs() << "BogusControlFlow application number -bcf_loop=x must be x > 0";
      return false;
    }

    if (!toObfuscateUint32Option(&F, "bcf_prob", &ObfProbRateTemp))
      ObfProbRateTemp = ObfProbRate;

    // Check if the number of applications is correct
    if (!((ObfProbRate > 0) && (ObfProbRate <= 100))) {
      errs() << "BogusControlFlow application basic blocks percentage "
                "-bcf_prob=x must be 0 < x <= 100";
      return false;
    }

    if (!toObfuscateUint32Option(&F, "bcf_junkasm_maxnum",
                                 &MaxNumberOfJunkAssemblyTemp))
      MaxNumberOfJunkAssemblyTemp = MaxNumberOfJunkAssembly;
    if (!toObfuscateUint32Option(&F, "bcf_junkasm_minnum",
                                 &MinNumberOfJunkAssemblyTemp))
      MinNumberOfJunkAssemblyTemp = MinNumberOfJunkAssembly;

    // Check if the number of applications is correct
    if (MaxNumberOfJunkAssemblyTemp < MinNumberOfJunkAssemblyTemp) {
      errs() << "BogusControlFlow application numbers of junk asm "
                "-bcf_junkasm_maxnum=x must be x >= bcf_junkasm_minnum";
      return false;
    }

    // If fla annotations
    if (toObfuscate(flag, &F, "bcf") && !F.isPresplitCoroutine() &&
        !readAnnotationMetadata(&F, "bcfopfunc")) {
      errs() << "Running BogusControlFlow On " << F.getName() << "\n";
      bogus(F);
      doF(F);
    }

    return true;
  } // end of runOnFunction()

  void bogus(Function &F) {
    if (!toObfuscateBoolOption(&F, "bcf_junkasm", &JunkAssemblyTemp))
      JunkAssemblyTemp = JunkAssembly;
    if (!toObfuscateBoolOption(&F, "bcf_onlyjunkasm", &OnlyJunkAssemblyTemp))
      OnlyJunkAssemblyTemp = OnlyJunkAssembly;

    uint32_t NumObfTimes = ObfTimesTemp;

    // Real begining of the pass
    // Loop for the number of time we run the pass on the function
    do {
      // Put all the function's block in a list
      std::list<BasicBlock *> basicBlocks;
      for (BasicBlock &BB : F)
        if (!BB.isEHPad() && !BB.isLandingPad() && !containsSwiftError(&BB) &&
            !containsMustTailCall(&BB) && !containsCoroBeginInst(&BB))
          basicBlocks.emplace_back(&BB);

      while (!basicBlocks.empty()) {
        // Basic Blocks' selection
        if (cryptoutils->get_range(100) <= ObfProbRateTemp) {
          // Add bogus flow to the given Basic Block (see description)
          BasicBlock *basicBlock = basicBlocks.front();
          addBogusFlow(basicBlock, F);
        }
        // remove the block from the list
        basicBlocks.pop_front();
      } // end of while(!basicBlocks.empty())
    } while (--NumObfTimes > 0);
  }

  bool containsCoroBeginInst(BasicBlock *b) {
    for (Instruction &I : *b)
      if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(&I))
        if (II->getIntrinsicID() == Intrinsic::coro_begin)
          return true;
    return false;
  }

  bool containsMustTailCall(BasicBlock *b) {
    for (Instruction &I : *b)
      if (CallInst *CI = dyn_cast<CallInst>(&I))
        if (CI->isMustTailCall())
          return true;
    return false;
  }

  bool containsSwiftError(BasicBlock *b) {
    for (Instruction &I : *b)
      if (AllocaInst *AI = dyn_cast<AllocaInst>(&I))
        if (AI->isSwiftError())
          return true;
    return false;
  }

  /* addBogusFlow
   *
   * Add bogus flow to a given basic block, according to the header's
   * description
   */
  void addBogusFlow(BasicBlock *basicBlock, Function &F) {

    // Split the block: first part with only the phi nodes and debug info and
    // terminator
    //                  created by splitBasicBlock. (-> No instruction)
    //                  Second part with every instructions from the original
    //                  block
    // We do this way, so we don't have to adjust all the phi nodes, metadatas
    // and so on for the first block. We have to let the phi nodes in the first
    // part, because they actually are updated in the second part according to
    // them.
    BasicBlock::iterator i1 = basicBlock->begin();
    if (basicBlock->getFirstNonPHIOrDbgOrLifetime()!= basicBlock->end())
      i1 = (BasicBlock::iterator)basicBlock->getFirstNonPHIOrDbgOrLifetime();

    // https://github.com/eshard/obfuscator-llvm/commit/85c8719c86bcb4784f5a436e28f3496e91cd6292
    /* TODO: find a real fix or try with the probe-stack inline-asm when its
     * ready. See https://github.com/Rust-for-Linux/linux/issues/355. Sometimes
     * moving an alloca from the entry block to the second block causes a
     * segfault when using the "probe-stack" attribute (observed with with Rust
     * programs). To avoid this issue we just split the entry block after the
     * allocas in this case.
     */
    if (F.hasFnAttribute("probe-stack") && basicBlock->isEntryBlock()) {
      // Find the first non alloca instruction
      while ((i1 != basicBlock->end()) && isa<AllocaInst>(i1))
        i1++;

      // If there are no other kind of instruction we just don't split that
      // entry block
      if (i1 == basicBlock->end())
        return;
    }

    BasicBlock *originalBB = basicBlock->splitBasicBlock(i1, "originalBB");

    // Creating the altered basic block on which the first basicBlock will jump
    BasicBlock *alteredBB =
        createAlteredBasicBlock(originalBB, "alteredBB", &F);

    if (!alteredBB) {
      // If alteredBB could not be created, it's not safe to proceed.
      // The function `basicBlock` is already split, and its terminator points to `originalBB`.
      // To prevent further errors, we simply return. The block won't be obfuscated.
      errs() << "Error: Could not create altered basic block. Skipping BCF for this block.\n";
      return;
    }


    // Now that all the blocks are created,
    // we modify the terminators to adjust the control flow.

    if (!OnlyJunkAssemblyTemp) {
      // alteredBB is guaranteed to be non-null here
      if (alteredBB->getTerminator())
        alteredBB->getTerminator()->eraseFromParent();
    }
    // basicBlock's terminator is the unconditional branch to originalBB created by splitBasicBlock.
    // We need to remove it to insert our conditional branch.
    if (basicBlock->getTerminator()) {
        basicBlock->getTerminator()->eraseFromParent();
    } else {
        // This should not happen if splitBasicBlock was successful.
        errs() << "Error: basicBlock has no terminator after split. Cannot apply BCF.\n";
        return;
    }


    // Preparing a condition..
    // For now, the condition is an always true comparaison between 2 float
    // This will be complicated after the pass (in doFinalization())

    // We need to use ConstantInt instead of ConstantFP as ConstantFP results in
    // strange dead-loop when injected into Xcode
    Value *LHS = ConstantInt::get(Type::getInt32Ty(F.getContext()), 1);
    Value *RHS = ConstantInt::get(Type::getInt32Ty(F.getContext()), 1);

    // The always true condition. End of the first block
#if LLVM_VERSION_MAJOR >= 19
    ICmpInst *condition = new ICmpInst(basicBlock->end(), ICmpInst::ICMP_EQ,
                                       LHS, RHS, "BCFPlaceHolderPred");
#else
    ICmpInst *condition = new ICmpInst(*basicBlock, ICmpInst::ICMP_EQ, LHS, RHS,
                                       "BCFPlaceHolderPred");
#endif
    needtoedit.emplace_back(condition);

    // Jump to the original basic block if the condition is true or
    // to the altered block if false.
    BranchInst::Create(originalBB, alteredBB, condition, basicBlock);

    // The altered block loop back on the original one.
    BranchInst::Create(originalBB, alteredBB);

    // The end of the originalBB is modified to give the impression that
    // sometimes it continues in the loop, and sometimes it return the desired
    // value (of course it's always true, so it always use the original
    // terminator..
    //  but this will be obfuscated too;) )

    // iterate on instruction just before the terminator of the originalBB
    BasicBlock::iterator i = originalBB->end();

    // Split at this point (we only want the terminator in the second part)
    // Need to check if originalBB is empty or only has one instruction (the terminator)
    if (i != originalBB->begin()) {
        --i; // Now i points to the last instruction (terminator)
             // We want to split *before* this terminator.
        BasicBlock *originalBBpart2 =
            originalBB->splitBasicBlock(i, "originalBBpart2");
        // the first part go either on the return statement or on the begining
        // of the altered block.. So we erase the terminator created when splitting.
        if (originalBB->getTerminator()) {
            originalBB->getTerminator()->eraseFromParent();
        } else {
            // Should not happen if splitBasicBlock worked
             errs() << "Error: originalBB has no terminator after split. Cannot apply BCF.\n";
             return;
        }
        // We add at the end a new always true condition
    #if LLVM_VERSION_MAJOR >= 19
        ICmpInst *condition2 = new ICmpInst(originalBB->end(), CmpInst::ICMP_EQ,
                                            LHS, RHS, "BCFPlaceHolderPred");
    #else
        ICmpInst *condition2 = new ICmpInst(*originalBB, CmpInst::ICMP_EQ, LHS, RHS,
                                            "BCFPlaceHolderPred");
    #endif
        needtoedit.emplace_back(condition2);
        // Do random behavior to avoid pattern recognition.
        // This is achieved by jumping to a random BB
        switch (cryptoutils->get_range(2)) {
        case 0: {
          BranchInst::Create(originalBBpart2, originalBB, condition2, originalBB);
          break;
        }
        case 1: {
          BranchInst::Create(originalBBpart2, alteredBB, condition2, originalBB);
          break;
        }
        default:
          llvm_unreachable("wtf?");
        }
    } else {
        // originalBB is empty or contains only a terminator, cannot split further in this way.
        // This situation should ideally not occur if originalBB was supposed to have content.
        // Or, if it's valid, the BCF structure might need adjustment.
        // For now, skip this part of BCF if originalBB is too small.
        errs() << "Warning: originalBB is too small to split for second predicate in BCF.\n";
    }
  } // end of addBogusFlow()

  /* createAlteredBasicBlock
   *
   * This function return a basic block similar to a given one.
   * It's inserted just after the given basic block.
   * The instructions are similar but junk instructions are added between
   * the cloned one. The cloned instructions' phi nodes, metadatas, uses and
   * debug locations are adjusted to fit in the cloned basic block and
   * behave nicely.
   */
  BasicBlock *createAlteredBasicBlock(BasicBlock *basicBlock,
                                      const Twine &Name = "gen",
                                      Function *F = nullptr) {
    if (!F) {
        errs() << "Error: Function is null in createAlteredBasicBlock.\n";
        return nullptr;
    }
    if (!basicBlock) {
        errs() << "Error: BasicBlock is null in createAlteredBasicBlock.\n";
        return nullptr;
    }
    
    BasicBlock *alteredBB = nullptr;
    if (OnlyJunkAssemblyTemp) {
      alteredBB = BasicBlock::Create(F->getContext(), Name, F); // Use Name if provided
    } else {
      ValueToValueMapTy VMap;
      alteredBB = CloneBasicBlock(basicBlock, VMap, Name, F);
      if (!alteredBB) {
          errs() << "Error: CloneBasicBlock failed in createAlteredBasicBlock.\n";
          return nullptr; 
      }

      BasicBlock::iterator ji = basicBlock->begin();
      for (BasicBlock::iterator i = alteredBB->begin(), e = alteredBB->end();
           i != e; ++i) {
        Instruction &currentInst = *i;
        for (User::op_iterator opi = currentInst.op_begin(), ope = currentInst.op_end();
             opi != ope; ++opi) {
          Value *v = MapValue(*opi, VMap, RF_NoModuleLevelChanges, 0);
          if (v != 0)
            *opi = v;
        }
        if (PHINode *pn = dyn_cast<PHINode>(&currentInst)) {
          for (unsigned j = 0, num = pn->getNumIncomingValues(); j != num; ++j) {
            Value *v = MapValue(pn->getIncomingBlock(j), VMap, RF_None, 0);
            if (v != 0)
              pn->setIncomingBlock(j, cast<BasicBlock>(v));
          }
        }
        SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
        currentInst.getAllMetadata(MDs);
        if (ji != basicBlock->end()) {
            currentInst.setDebugLoc(ji->getDebugLoc());
            ji++;
        } else {
            // basicBlock might be shorter than alteredBB if instructions are added later?
            // Or ji simply reached end. Clear DebugLoc if no corresponding original instruction.
            currentInst.setDebugLoc(DebugLoc());
        }
      } 

      for (BasicBlock::iterator i = alteredBB->begin(), e = alteredBB->end();
           i != e; ++i) {
        Instruction &currentInstRef = *i;
        if (currentInstRef.isBinaryOp()) { 
          unsigned int opcode = currentInstRef.getOpcode();
          Instruction *op = nullptr, *op1 = nullptr;
          // Twine *var = new Twine("_"); // REMOVED

          if (opcode == Instruction::Add || opcode == Instruction::Sub ||
              opcode == Instruction::Mul || opcode == Instruction::UDiv ||
              opcode == Instruction::SDiv || opcode == Instruction::URem ||
              opcode == Instruction::SRem || opcode == Instruction::Shl ||
              opcode == Instruction::LShr || opcode == Instruction::AShr ||
              opcode == Instruction::And || opcode == Instruction::Or ||
              opcode == Instruction::Xor) {
            for (int random = (int)cryptoutils->get_range(10); random < 10; ++random) {
              switch (cryptoutils->get_range(4)) { 
              case 0: break;
              case 1:
                op = BinaryOperator::CreateNeg(currentInstRef.getOperand(0), "", &currentInstRef);
                op1 = BinaryOperator::Create(Instruction::Add, op, currentInstRef.getOperand(1), "gen", &currentInstRef);
                break;
              case 2:
                op1 = BinaryOperator::Create(Instruction::Sub, currentInstRef.getOperand(0), currentInstRef.getOperand(1), "", &currentInstRef);
                op = BinaryOperator::Create(Instruction::Mul, op1, currentInstRef.getOperand(1), "gen", &currentInstRef);
                break;
              case 3:
                op = BinaryOperator::Create(Instruction::Shl, currentInstRef.getOperand(0), currentInstRef.getOperand(1), "", &currentInstRef);
                break;
              }
            }
          }
          if (opcode == Instruction::FAdd || opcode == Instruction::FSub ||
              opcode == Instruction::FMul || opcode == Instruction::FDiv ||
              opcode == Instruction::FRem) {
            for (int random = (int)cryptoutils->get_range(10); random < 10; ++random) {
              switch (cryptoutils->get_range(3)) { 
              case 0: break;
              case 1:
                op = UnaryOperator::CreateFNeg(currentInstRef.getOperand(0), "", &currentInstRef);
                op1 = BinaryOperator::Create(Instruction::FAdd, op, currentInstRef.getOperand(1), "gen", &currentInstRef);
                break;
              case 2:
                op = BinaryOperator::Create(Instruction::FSub, currentInstRef.getOperand(0), currentInstRef.getOperand(1), "", &currentInstRef);
                op1 = BinaryOperator::Create(Instruction::FMul, op, currentInstRef.getOperand(1), "gen", &currentInstRef);
                break;
              }
            }
          }
          if (opcode == Instruction::ICmp) { 
            if (ICmpInst *currentI = dyn_cast<ICmpInst>(&currentInstRef)) {
                switch (cryptoutils->get_range(3)) { 
                case 0: break;
                case 1: currentI->swapOperands(); break;
                case 2: 
                  switch (cryptoutils->get_range(10)) {
                  case 0: currentI->setPredicate(ICmpInst::ICMP_EQ); break; 
                  case 1: currentI->setPredicate(ICmpInst::ICMP_NE); break; 
                  case 2: currentI->setPredicate(ICmpInst::ICMP_UGT); break; 
                  case 3: currentI->setPredicate(ICmpInst::ICMP_UGE); break; 
                  case 4: currentI->setPredicate(ICmpInst::ICMP_ULT); break; 
                  case 5: currentI->setPredicate(ICmpInst::ICMP_ULE); break; 
                  case 6: currentI->setPredicate(ICmpInst::ICMP_SGT); break; 
                  case 7: currentI->setPredicate(ICmpInst::ICMP_SGE); break; 
                  case 8: currentI->setPredicate(ICmpInst::ICMP_SLT); break; 
                  case 9: currentI->setPredicate(ICmpInst::ICMP_SLE); break; 
                  }
                  break;
                }
            }
          }
          if (opcode == Instruction::FCmp) { 
            if (FCmpInst *currentI = dyn_cast<FCmpInst>(&currentInstRef)) {
                switch (cryptoutils->get_range(3)) { 
                case 0: break;
                case 1: currentI->swapOperands(); break;
                case 2: 
                  switch (cryptoutils->get_range(10)) {
                  case 0: currentI->setPredicate(FCmpInst::FCMP_OEQ); break; 
                  case 1: currentI->setPredicate(FCmpInst::FCMP_ONE); break; 
                  case 2: currentI->setPredicate(FCmpInst::FCMP_UGT); break; 
                  case 3: currentI->setPredicate(FCmpInst::FCMP_UGE); break; 
                  case 4: currentI->setPredicate(FCmpInst::FCMP_ULT); break; 
                  case 5: currentI->setPredicate(FCmpInst::FCMP_ULE); break; 
                  case 6: currentI->setPredicate(FCmpInst::FCMP_OGT); break; 
                  case 7: currentI->setPredicate(FCmpInst::FCMP_OGE); break; 
                  case 8: currentI->setPredicate(FCmpInst::FCMP_OLT); break; 
                  case 9: currentI->setPredicate(FCmpInst::FCMP_OLE); break; 
                  }
                  break;
                }
            }
          }
        }
      }
      SmallVector<CallInst *, 4> toRemove;
      SmallVector<Constant *, 4> DeadConstants;
      if (alteredBB) { 
          for (Instruction &I : *alteredBB) {
            if (CallInst *CI = dyn_cast<CallInst>(&I)) {
              if (CI->getCalledFunction() != nullptr &&
    #if LLVM_VERSION_MAJOR >= 18
                  CI->getCalledFunction()->getName().starts_with("llvm.dbg"))
    #else
                  CI->getCalledFunction()->getName().startswith("llvm.dbg"))
    #endif
                toRemove.emplace_back(CI);
            }
          }
      }
      for (CallInst *CI : toRemove) {
        Value *Arg1 = CI->getArgOperand(0);
        Value *Arg2 = CI->getArgOperand(1);
        assert(CI->use_empty() && "llvm.dbg intrinsic should have void result");
        CI->eraseFromParent();
        if (Arg1->use_empty()) {
          if (Constant *C = dyn_cast<Constant>(Arg1))
            DeadConstants.emplace_back(C);
          else
            RecursivelyDeleteTriviallyDeadInstructions(Arg1);
        }
        if (Arg2->use_empty())
          if (Constant *C = dyn_cast<Constant>(Arg2))
            DeadConstants.emplace_back(C);
      }
      while (!DeadConstants.empty()) {
        Constant *C = DeadConstants.back();
        DeadConstants.pop_back();
        if (GlobalVariable *GV = dyn_cast<GlobalVariable>(C)) {
          if (GV->hasLocalLinkage())
            RemoveDeadConstant(GV);
        } else {
          RemoveDeadConstant(C);
        }
      }
    } 

    if (!alteredBB) {
        // This should have been caught earlier if OnlyJunkAssemblyTemp was false and CloneBasicBlock failed.
        // If OnlyJunkAssemblyTemp was true, alteredBB should have been created.
        errs() << "Error: alteredBB is unexpectedly null before junk assembly insertion.\n";
        return nullptr;
    }

    if (JunkAssemblyTemp || OnlyJunkAssemblyTemp) {
      std::string junk = "";
      for (uint32_t c = cryptoutils->get_range(MinNumberOfJunkAssemblyTemp, MaxNumberOfJunkAssemblyTemp); // Renamed loop var
           c > 0; c--)
        junk += ".long " + std::to_string(cryptoutils->get_uint32_t()) + "\n";
      
      if (!(&(alteredBB->getContext()))) {
          errs() << "Error: alteredBB context is null before creating InlineAsm.\n";
          return alteredBB; 
      }

      InlineAsm *IA = InlineAsm::get(
          FunctionType::get(Type::getVoidTy(alteredBB->getContext()), false),
          junk, "", true, false);
      if (OnlyJunkAssemblyTemp) {
        CallInst::Create(IA, {}, "", alteredBB);
      } else {
        BasicBlock::iterator insert_instr_iter = alteredBB->getFirstNonPHIOrDbgOrLifetime();
        if (insert_instr_iter != alteredBB->end()) {
            CallInst::Create(IA, {}, "", &*insert_instr_iter);
        } else {
            CallInst::Create(IA, {}, "", alteredBB); // Append if no specific insertion point
        }
      }
      // Ensure basicBlock and its parent are valid before calling turnOffOptimization
      if (basicBlock && basicBlock->getParent()) {
          turnOffOptimization(basicBlock->getParent());
      }
    }
    return alteredBB;
  } // end of createAlteredBasicBlock()

  /* doF
   *
   * This part obfuscate the always true predicates generated in addBogusFlow()
   * of the function.
   */
  bool doF(Function &F) {
    if (!toObfuscateBoolOption(&F, "bcf_createfunc",
                               &CreateFunctionForOpaquePredicateTemp))
      CreateFunctionForOpaquePredicateTemp = CreateFunctionForOpaquePredicate;
    if (!toObfuscateUint32Option(&F, "bcf_cond_compl",
                                 &ConditionExpressionComplexityTemp))
      ConditionExpressionComplexityTemp = ConditionExpressionComplexity;

    SmallVector<Instruction *, 8> toEdit, toDelete;
    // Looking for the conditions and branches to transform
    for (BasicBlock &BB : F) {
      Instruction *tbb = BB.getTerminator();
      if (BranchInst *br = dyn_cast<BranchInst>(tbb)) {
        if (br->isConditional()) {
          ICmpInst *cond = dyn_cast<ICmpInst>(br->getCondition());
          if (cond && std::find(needtoedit.begin(), needtoedit.end(), cond) !=
                          needtoedit.end()) {
            toDelete.emplace_back(cond); // The condition
            toEdit.emplace_back(tbb);    // The branch using the condition
          }
        }
      }
    }
    Module &M = *F.getParent();
    Type *I1Ty = Type::getInt1Ty(M.getContext());
    Type *I32Ty = Type::getInt32Ty(M.getContext());
    // Replacing all the branches we found
    for (Instruction *i : toEdit) {
      if (!i || !i->getParent()){
          errs() << "Warning: Instruction or its parent is null in doF loop.\n";
          continue;
      }
      // Previously We Use LLVM EE To Calculate LHS and RHS
      // Since IRBuilder<> uses ConstantFolding to fold constants.
      // The return instruction is already returning constants
      // The variable names below are the artifact from the Emulation Era
      Function *emuFunction = Function::Create(
          FunctionType::get(I32Ty, false),
          GlobalValue::LinkageTypes::PrivateLinkage, "HikariBCFEmuFunction", M);
      BasicBlock *emuEntryBlock =
          BasicBlock::Create(emuFunction->getContext(), "", emuFunction);

      std::unique_ptr<IRBuilder<>> IRBOp_ptr; // Use unique_ptr
      Function *opFunction = nullptr;
      if (CreateFunctionForOpaquePredicateTemp) {
        opFunction = Function::Create(FunctionType::get(I1Ty, false),
                                      GlobalValue::LinkageTypes::PrivateLinkage,
                                      "HikariBCFOpaquePredicateFunction", M);
        BasicBlock *opTrampBlock =
            BasicBlock::Create(opFunction->getContext(), "", opFunction);
        BasicBlock *opEntryBlock =
            BasicBlock::Create(opFunction->getContext(), "", opFunction);
        // Insert a br to make it can be obfuscated by IndirectBranch
        BranchInst::Create(opEntryBlock, opTrampBlock);
        writeAnnotationMetadata(opFunction, "bcfopfunc");
        IRBOp_ptr = std::make_unique<IRBuilder<>>(opEntryBlock); // Assign to unique_ptr
      }
      
      std::unique_ptr<IRBuilder<>> IRBReal_ptr;
      BasicBlock *currentBB_doF = i->getParent(); // Renamed to avoid conflict
      BasicBlock::iterator insertPtIter_doF = currentBB_doF->getFirstNonPHIOrDbgOrLifetime(); // Renamed

      if (insertPtIter_doF != currentBB_doF->end()) {
          IRBReal_ptr = std::make_unique<IRBuilder<>>(&*insertPtIter_doF);
      } else {
          // Block is empty or only PHIs/Debug. Insert before the terminator `i`.
          IRBReal_ptr = std::make_unique<IRBuilder<>>(i);
      }
      
      IRBuilder<> IRBEmu(emuEntryBlock); // Stack allocated, no change needed
      // First,Construct a real RHS that will be used in the actual condition
      Constant *RealRHS = ConstantInt::get(I32Ty, cryptoutils->get_uint32_t());
      // Prepare Initial LHS and RHS to bootstrap the emulator
      Constant *LHSC =
          ConstantInt::get(I32Ty, cryptoutils->get_range(1, UINT32_MAX));
      Constant *RHSC =
          ConstantInt::get(I32Ty, cryptoutils->get_range(1, UINT32_MAX));
      GlobalVariable *LHSGV =
          new GlobalVariable(M, Type::getInt32Ty(M.getContext()), false,
                             GlobalValue::PrivateLinkage, LHSC, "LHSGV");
      GlobalVariable *RHSGV =
          new GlobalVariable(M, Type::getInt32Ty(M.getContext()), false,
                             GlobalValue::PrivateLinkage, RHSC, "RHSGV");
      
      LoadInst *LHS_load, *RHS_load; // Renamed to avoid conflict
      if (CreateFunctionForOpaquePredicateTemp && IRBOp_ptr.get()) {
          LHS_load = IRBOp_ptr.get()->CreateLoad(LHSGV->getValueType(), LHSGV, "Initial LHS");
          RHS_load = IRBOp_ptr.get()->CreateLoad(RHSGV->getValueType(), RHSGV, "Initial RHS");
      } else {
          LHS_load = IRBReal_ptr.get()->CreateLoad(LHSGV->getValueType(), LHSGV, "Initial LHS");
          RHS_load = IRBReal_ptr.get()->CreateLoad(RHSGV->getValueType(), RHSGV, "Initial RHS");
      }


      // To Speed-Up Evaluation
      Value *emuLHS = LHSC;
      Value *emuRHS = RHSC;
      Instruction::BinaryOps initialOp =
          ops[cryptoutils->get_range(sizeof(ops) / sizeof(ops[0]))];
      Value *emuLast =
          IRBEmu.CreateBinOp(initialOp, emuLHS, emuRHS, "EmuInitialCondition");
      
      Value *Last; // Renamed to avoid conflict
      if (CreateFunctionForOpaquePredicateTemp && IRBOp_ptr.get()) {
          Last = IRBOp_ptr.get()->CreateBinOp(initialOp, LHS_load, RHS_load, "InitialCondition");
      } else {
          Last = IRBReal_ptr.get()->CreateBinOp(initialOp, LHS_load, RHS_load, "InitialCondition");
      }

      for (uint32_t j = 0; j < ConditionExpressionComplexityTemp; j++) { // Renamed loop var
        Constant *newTmp =
            ConstantInt::get(I32Ty, cryptoutils->get_range(1, UINT32_MAX));
        Instruction::BinaryOps initialOp2 =
            ops[cryptoutils->get_range(sizeof(ops) / sizeof(ops[0]))];
        emuLast = IRBEmu.CreateBinOp(initialOp2, emuLast, newTmp,
                                     "EmuInitialCondition");
        if (CreateFunctionForOpaquePredicateTemp && IRBOp_ptr.get()) {
            Last = IRBOp_ptr.get()->CreateBinOp(initialOp2, Last, newTmp, "InitialCondition");
        } else {
            Last = IRBReal_ptr.get()->CreateBinOp(initialOp2, Last, newTmp, "InitialCondition");
        }
      }
      // Randomly Generate Predicate
      CmpInst::Predicate pred =
          preds[cryptoutils->get_range(sizeof(preds) / sizeof(preds[0]))];
      if (CreateFunctionForOpaquePredicateTemp && IRBOp_ptr.get() && opFunction) {
        IRBOp_ptr.get()->CreateRet(IRBOp_ptr.get()->CreateICmp(pred, Last, RealRHS));
        Last = IRBReal_ptr.get()->CreateCall(opFunction);
      } else {
        Last = IRBReal_ptr.get()->CreateICmp(pred, Last, RealRHS);
      }
      emuLast = IRBEmu.CreateICmp(pred, emuLast, RealRHS);
      ReturnInst *RI = IRBEmu.CreateRet(emuLast);
      ConstantInt *emuCI = cast<ConstantInt>(RI->getReturnValue());
      APInt emulateResult = emuCI->getValue();

      BranchInst *currentBranchInst = cast<BranchInst>(i); // Renamed
      if (emulateResult == 1) {
        // Our ConstantExpr evaluates to true;
        BranchInst::Create(currentBranchInst->getSuccessor(0),
                           currentBranchInst->getSuccessor(1), Last,
                           currentBranchInst->getParent());
      } else {
        // False, swap operands
        BranchInst::Create(currentBranchInst->getSuccessor(1),
                           currentBranchInst->getSuccessor(0), Last,
                           currentBranchInst->getParent());
      }
      emuFunction->eraseFromParent();
      i->eraseFromParent(); // erase the branch
    }
    // Erase all the associated conditions we found
    for (Instruction *cond_to_delete : toDelete) { // Renamed loop var
      cond_to_delete->eraseFromParent();
    }
    return true;
  } // end of doFinalization
}; // end of struct BogusControlFlow : public FunctionPass
} // namespace llvm

char BogusControlFlow::ID = 0;
INITIALIZE_PASS(BogusControlFlow, "bcfobf", "Enable BogusControlFlow.", false,
                false)
FunctionPass *llvm::createBogusControlFlowPass(bool flag) {
  return new BogusControlFlow(flag);
}