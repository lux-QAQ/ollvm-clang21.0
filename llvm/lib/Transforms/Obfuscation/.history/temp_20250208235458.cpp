template <typename FolderTy = ConstantFolder,
          typename InserterTy = IRBuilderDefaultInserter>
class IRBuilder : public IRBuilderBase {
private:
  FolderTy Folder;
  InserterTy Inserter;

public:
  IRBuilder(LLVMContext &C, FolderTy Folder, InserterTy Inserter = InserterTy(),
            MDNode *FPMathTag = nullptr,
            ArrayRef<OperandBundleDef> OpBundles = {})
      : IRBuilderBase(C, this->Folder, this->Inserter, FPMathTag, OpBundles),
        Folder(Folder), Inserter(Inserter) {}

  explicit IRBuilder(LLVMContext &C, MDNode *FPMathTag = nullptr,
                     ArrayRef<OperandBundleDef> OpBundles = {})
      : IRBuilderBase(C, this->Folder, this->Inserter, FPMathTag, OpBundles) {}

  explicit IRBuilder(BasicBlock *TheBB, FolderTy Folder,
                     MDNode *FPMathTag = nullptr,
                     ArrayRef<OperandBundleDef> OpBundles = {})
      : IRBuilderBase(TheBB->getContext(), this->Folder, this->Inserter,
                      FPMathTag, OpBundles),
        Folder(Folder) {
    SetInsertPoint(TheBB);
  }

  explicit IRBuilder(BasicBlock *TheBB, MDNode *FPMathTag = nullptr,
                     ArrayRef<OperandBundleDef> OpBundles = {})
      : IRBuilderBase(TheBB->getContext(), this->Folder, this->Inserter,
                      FPMathTag, OpBundles) {
    SetInsertPoint(TheBB);
  }

  explicit IRBuilder(Instruction *IP, MDNode *FPMathTag = nullptr,
                     ArrayRef<OperandBundleDef> OpBundles = {})
      : IRBuilderBase(IP->getContext(), this->Folder, this->Inserter, FPMathTag,
                      OpBundles) {
    SetInsertPoint(IP);
  }

  IRBuilder(BasicBlock *TheBB, BasicBlock::iterator IP, FolderTy Folder,
            MDNode *FPMathTag = nullptr,
            ArrayRef<OperandBundleDef> OpBundles = {})
      : IRBuilderBase(TheBB->getContext(), this->Folder, this->Inserter,
                      FPMathTag, OpBundles),
        Folder(Folder) {
    SetInsertPoint(TheBB, IP);
  }

  IRBuilder(BasicBlock *TheBB, BasicBlock::iterator IP,
            MDNode *FPMathTag = nullptr,
            ArrayRef<OperandBundleDef> OpBundles = {})
      : IRBuilderBase(TheBB->getContext(), this->Folder, this->Inserter,
                      FPMathTag, OpBundles) {
    SetInsertPoint(TheBB, IP);
  }

  /// Avoid copying the full IRBuilder. Prefer using InsertPointGuard
  /// or FastMathFlagGuard instead.
  IRBuilder(const IRBuilder &) = delete;

  InserterTy &getInserter() { return Inserter; }
  const InserterTy &getInserter() const { return Inserter; }
};