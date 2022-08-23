//===--- Hooks.cpp - Record events from the compiler --------------- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang-include-cleaner/Hooks.h"
#include "AnalysisInternal.h"
#include "clang-include-cleaner/Analysis.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclGroup.h"
#include "clang/AST/DeclObjC.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/Token.h"

namespace clang {
namespace include_cleaner {

class PPRecorder : public PPCallbacks {
public:
  PPRecorder(AnalysisContext &Ctx, RecordedPP &Recorded)
      : Ctx(Ctx), Recorded(Recorded) {}

  virtual void FileChanged(SourceLocation Loc, FileChangeReason Reason,
                           SrcMgr::CharacteristicKind FileType,
                           FileID PrevFID) override {
    Active = Ctx.sourceManager().isWrittenInMainFile(Loc);
  }

  void InclusionDirective(SourceLocation Hash, const Token &IncludeTok,
                          StringRef SpelledFilename, bool IsAngled,
                          CharSourceRange FilenameRange, const FileEntry *File,
                          StringRef SearchPath, StringRef RelativePath,
                          const Module *, SrcMgr::CharacteristicKind) override {
    if (!Active)
      return;

    unsigned Index = Recorded.Includes.All.size();
    Recorded.Includes.All.emplace_back();
    RecordedPP::Include &I = Recorded.Includes.All.back();
    I.Location = Hash;
    I.Resolved = File;
    I.Line = Ctx.sourceManager().getSpellingLineNumber(Hash);
    auto BySpellingIt =
        Recorded.Includes.BySpelling.try_emplace(SpelledFilename).first;
    I.Spelled = BySpellingIt->first();

    BySpellingIt->second.push_back(Index);
    Recorded.Includes.ByFile[File].push_back(Index);
  }

  void MacroExpands(const Token &MacroName, const MacroDefinition &MD,
                    SourceRange Range, const MacroArgs *Args) override {
    if (!Active)
      return;
    recordMacroRef(MacroName, *MD.getMacroInfo());
  }

  void MacroDefined(const Token &MacroName, const MacroDirective *MD) override {
    if (!Active)
      return;

    const auto *MI = MD->getMacroInfo();
    // The tokens of a macro definition could refer to a macro.
    // Formally this reference isn't resolved until this macro is expanded,
    // but we want to treat it as a reference anyway.
    for (const auto &Tok : MI->tokens()) {
      auto *II = Tok.getIdentifierInfo();
      // Could this token be a reference to a macro? (Not param to this macro).
      if (!II || !II->hadMacroDefinition() ||
          llvm::is_contained(MI->params(), II))
        continue;
      if (const MacroInfo *MI = Ctx.preprocessor().getMacroInfo(II))
        recordMacroRef(Tok, *MI);
    }
  }

private:
  void recordMacroRef(const Token &Tok, const MacroInfo &MI) {
    if (MI.isBuiltinMacro())
      return; // __FILE__ is not a reference.
    Recorded.MacroReferences.push_back(SymbolReference{
        Tok.getLocation(),
        Ctx.cache().macro(Tok.getIdentifierInfo(), MI.getDefinitionLoc())});
  }

  bool Active = false;
  AnalysisContext &Ctx;
  RecordedPP &Recorded;
};

llvm::SmallVector<const RecordedPP::Include *>
RecordedPP::RecordedIncludes::match(Header H) const {
  llvm::SmallVector<const Include *> Result;
  switch (H.kind()) {
  case Header::Physical:
    for (unsigned I : ByFile.lookup(H.getPhysical()))
      Result.push_back(&All[I]);
    break;
  case Header::StandardLibrary:
    for (unsigned I :
         BySpelling.lookup(H.getStandardLibrary().name().trim("<>")))
      Result.push_back(&All[I]);
    break;
  case Header::Verbatim:
    for (unsigned I : BySpelling.lookup(H.getVerbatimSpelling()))
      Result.push_back(&All[I]);
    break;
  case Header::Builtin:
  case Header::MainFile:
    break;
  }
  llvm::sort(Result);
  Result.erase(std::unique(Result.begin(), Result.end()), Result.end());
  return Result;
}

class ASTRecorder : public ASTConsumer {
public:
  ASTRecorder(AnalysisContext &Ctx, RecordedAST &Recorded)
      : Ctx(Ctx), Recorded(Recorded) {}

  bool HandleTopLevelDecl(DeclGroupRef DG) override {
    for (Decl *D : DG) {
      if (!Ctx.sourceManager().isWrittenInMainFile(
              Ctx.sourceManager().getExpansionLoc(D->getLocation())))
        continue;
      if (const auto *T = llvm::dyn_cast<FunctionDecl>(D))
        if (T->getTemplateSpecializationKind() == TSK_ImplicitInstantiation)
          continue;
      if (const auto *T = llvm::dyn_cast<CXXRecordDecl>(D))
        if (T->getTemplateSpecializationKind() == TSK_ImplicitInstantiation)
          continue;
      if (const auto *T = llvm::dyn_cast<VarDecl>(D))
        if (T->getTemplateSpecializationKind() == TSK_ImplicitInstantiation)
          continue;
      // ObjCMethodDecl are not actually top-level!
      if (isa<ObjCMethodDecl>(D))
        continue;

      Recorded.TopLevelDecls.push_back(D);
    }
    return true;
  }

private:
  AnalysisContext &Ctx;
  RecordedAST &Recorded;
};

std::unique_ptr<PPCallbacks> RecordedPP::record(AnalysisContext &Ctx) {
  return std::make_unique<PPRecorder>(Ctx, *this);
}

std::unique_ptr<ASTConsumer> RecordedAST::record(AnalysisContext &Ctx) {
  return std::make_unique<ASTRecorder>(Ctx, *this);
}

} // namespace include_cleaner
} // namespace clang
