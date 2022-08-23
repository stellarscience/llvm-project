//===--- WalkAST.cpp - Find declaration references in the AST -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AnalysisInternal.h"
#include "clang-include-cleaner/Analysis.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/SaveAndRestore.h"

namespace clang {
namespace include_cleaner {
namespace {

using DeclCallback =
    llvm::function_ref<void(SourceLocation, Hinted<NamedDecl &>)>;

// Traverses part of the AST, looking for references and reporting them.
class ASTWalker : public RecursiveASTVisitor<ASTWalker> {
public:
  ASTWalker(AnalysisContext &Ctx, DeclCallback Callback)
      : Ctx(Ctx), Callback(Callback) {}

  bool VisitDeclRefExpr(DeclRefExpr *E) {
    if (!Ctx.policy().Operators)
      if (auto *FD = E->getDecl()->getAsFunction())
        if (FD->isOverloadedOperator())
          return true;
    report(E->getLocation(), E->getFoundDecl());
    return true;
  }

  bool VisitMemberExpr(MemberExpr *ME) {
    if (Ctx.policy().Members)
      report(ME->getMemberLoc(), ME->getFoundDecl().getDecl());
    return true;
  }

  bool VisitTagType(TagType *TT) {
    report(LocationOfType, TT->getDecl());
    return true;
  }

  bool VisitFunctionDecl(FunctionDecl *FD) {
    // Count function definitions as a reference to their declarations.
    if (FD->isThisDeclarationADefinition() && FD->getCanonicalDecl() != FD)
      report(FD->getLocation(), FD->getCanonicalDecl());
    return true;
  }

  bool VisitCXXConstructExpr(CXXConstructExpr *E) {
    if (!Ctx.policy().Construction)
      return true;
    SaveAndRestore<SourceLocation> Loc(LocationOfType, E->getLocation());
    LocationOfType = E->getLocation();
    return TraverseType(E->getType());
  }

  // We handle TypeLocs by saving their loc and consuming it in Visit*Type().
  //
  // Handling Visit*TypeLoc() directly would be simpler, but sometimes unwritten
  // types count as references (e.g. implicit conversions, with no TypeLoc).
  // Stashing the location and visiting the contained type lets us handle both
  // cases in VisitTagType() etc.
  bool TraverseTypeLoc(TypeLoc TL) {
    SaveAndRestore<SourceLocation> Loc(LocationOfType, TL.getBeginLoc());
    // The base implementation calls:
    //  - Visit*TypeLoc()      - does nothing
    //  - Visit*Type()         - where we handle type references
    //  - TraverseTypeLoc for each lexically nested type.
    return Base::TraverseTypeLoc(TL);
  }

  bool VisitTemplateSpecializationType(TemplateSpecializationType *TST) {
    report(LocationOfType,
           TST->getTemplateName().getAsTemplateDecl()); // Primary template.
    report(LocationOfType, TST->getAsCXXRecordDecl());  // Specialization
    return true;
  }

  bool VisitUsingType(UsingType *UT) {
    report(LocationOfType, UT->getFoundDecl());
    return true;
  }

  bool VisitTypedefType(TypedefType *TT) {
    report(LocationOfType, TT->getDecl());
    return true;
  }

  bool VisitUsingDecl(UsingDecl *UD) {
    for (const auto *USD : UD->shadows())
      report(UD->getLocation(), USD->getTargetDecl());
    return true;
  }

  bool VisitOverloadExpr(OverloadExpr *E) {
    if (llvm::isa<UnresolvedMemberExpr>(E) && !Ctx.policy().Members)
      return true;
    for (auto *Candidate : E->decls())
      report(E->getExprLoc(), Candidate);
    return true;
  }

private:
  void report(SourceLocation Loc, NamedDecl *ND) {
    while (Loc.isMacroID()) {
      auto DecLoc = Ctx.sourceManager().getDecomposedLoc(Loc);
      const SrcMgr::ExpansionInfo &Expansion =
          Ctx.sourceManager().getSLocEntry(DecLoc.first).getExpansion();
      if (!Expansion.isMacroArgExpansion())
        return; // Names within macro bodies are not considered references.
      Loc = Expansion.getSpellingLoc().getLocWithOffset(DecLoc.second);
    }
    // FIXME: relevant ranking hints?
    if (ND)
      Callback(Loc, *cast<NamedDecl>(ND->getCanonicalDecl()));
  }

  using Base = RecursiveASTVisitor;

  AnalysisContext &Ctx;
  DeclCallback Callback;

  SourceLocation LocationOfType;
};

} // namespace

void walkAST(AnalysisContext &Ctx, Decl &Root, DeclCallback Callback) {
  ASTWalker(Ctx, Callback).TraverseDecl(&Root);
}

} // namespace include_cleaner
} // namespace clang
