//===--- Locations.cpp - Find the locations that provide symbols ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AnalysisInternal.h"
#include "clang-include-cleaner/Analysis.h"
#include "clang-include-cleaner/Types.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/SmallVector.h"

namespace clang {
namespace include_cleaner {

Hint declHint(const NamedDecl &D) {
  Hint H = Hint::None;
  if (auto *TD = llvm::dyn_cast<TagDecl>(&D))
    if (TD->isThisDeclarationADefinition())
      H |= Hint::Complete;
  if (auto *CTD = llvm::dyn_cast<ClassTemplateDecl>(&D))
    if (CTD->isThisDeclarationADefinition())
      H |= Hint::Complete;
  // A function template being defined is similar to a class being defined.
  if (auto *FTD = llvm::dyn_cast<FunctionTemplateDecl>(&D))
    if (FTD->isThisDeclarationADefinition())
      H |= Hint::Complete;
  return H;
}

llvm::SmallVector<Hinted<Location>> locateDecl(AnalysisContext &Ctx,
                                               const NamedDecl &ND) {
  if (auto StdlibSym = Ctx.cache().StdlibRecognizer(&ND))
    return {{*StdlibSym}};

  llvm::SmallVector<Hinted<Location>> Result;
  // Is accepting all the redecls too naive?
  for (const Decl *RD : ND.redecls()) {
    // `friend X` is not an interesting location for X unless it's acting as a
    // forward-declaration.
    if (RD->getFriendObjectKind() == Decl::FOK_Declared)
      continue;
    SourceLocation Loc = RD->getLocation();
    if (Loc.isValid())
      Result.push_back({Loc, declHint(*cast<NamedDecl>(RD))});
  }
  return Result;
}

Hinted<Location> locateMacro(AnalysisContext &Ctx, const DefinedMacro &M) {
  return {M.Definition};
}

} // namespace include_cleaner
} // namespace clang
