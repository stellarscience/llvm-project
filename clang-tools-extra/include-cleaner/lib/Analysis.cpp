//===--- Analysis.cpp - Analyze used files --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang-include-cleaner/Analysis.h"
#include "AnalysisInternal.h"
#include "clang/Lex/Preprocessor.h"

namespace clang {
namespace include_cleaner {

AnalysisContext::AnalysisContext(const Policy &P, const Preprocessor &PP)
    : P(P), SM(&PP.getSourceManager()), PP(&PP), C(std::make_unique<Cache>()) {}
AnalysisContext::~AnalysisContext() = default;

static bool prefer(AnalysisContext &Ctx, Hint L, Hint R) {
  return std::make_tuple(bool(L & Hint::NameMatch), bool(L & Hint::Complete)) >
         std::make_tuple(bool(R & Hint::NameMatch), bool(R & Hint::Complete));
}

// Is this hint actually useful?
static void addNameMatchHint(const IdentifierInfo *II,
                             llvm::SmallVector<Hinted<Header>> &H) {
  if (!II)
    return;
  for (auto &HH : H)
    if (HH->kind() == Header::Physical &&
        II->getName().equals_insensitive(HH->getPhysical()->getName()))
      HH.Hint |= Hint::NameMatch;
}

static llvm::SmallVector<Header>
rank(AnalysisContext &Ctx, llvm::SmallVector<Hinted<Header>> &Candidates) {
  // Sort by Header, so we can deduplicate (and combine flags).
  llvm::stable_sort(Candidates,
                    [&](const Hinted<Header> &L, const Hinted<Header> &R) {
                      return *L < *R;
                    });
  // Like unique(), but merge hints.
  auto *Write = Candidates.begin();
  for (auto *Read = Candidates.begin(); Read != Candidates.end(); ++Write) {
    *Write = *Read;
    for (++Read; Read != Candidates.end() && Read->Value == Write->Value;
         ++Read)
      Write->Hint |= Read->Hint;
  }
  Candidates.erase(Write, Candidates.end());
  // Now sort by hints.
  llvm::stable_sort(Candidates,
                    [&](const Hinted<Header> &L, const Hinted<Header> &R) {
                      return prefer(Ctx, L.Hint, R.Hint);
                    });
  // Drop hints to return clean result list.
  llvm::SmallVector<Header> Result;
  for (const auto &H : Candidates)
    Result.push_back(*H);
  return Result;
}

template <typename T> void addHint(Hint H, T &Items) {
  for (auto &Item : Items)
    Item.Hint |= H;
}

void walkUsed(AnalysisContext &Ctx, llvm::ArrayRef<Decl *> ASTRoots,
              llvm::ArrayRef<SymbolReference> MacroRefs,
              UsedSymbolVisitor Callback) {
  for (Decl *Root : ASTRoots) {
    walkAST(Ctx, *Root, [&](SourceLocation RefLoc, Hinted<NamedDecl &> ND) {
      auto Locations = locateDecl(Ctx, *ND);
      llvm::SmallVector<Hinted<Header>> Headers;
      for (const auto &Loc : Locations) {
        auto LocHeaders = includableHeader(Ctx, *Loc);
        addHint(Loc.Hint, LocHeaders);
        Headers.append(std::move(LocHeaders));
      }
      addHint(ND.Hint, Headers);
      addNameMatchHint(ND.Value.getDeclName().getAsIdentifierInfo(), Headers);
      Callback(RefLoc, &ND.Value, rank(Ctx, Headers));
    });
  }
  for (const SymbolReference &MacroRef : MacroRefs) {
    assert(MacroRef.Target.kind() == Symbol::Macro);
    auto Loc = locateMacro(Ctx, *MacroRef.Target.getMacro());
    auto Headers = includableHeader(Ctx, *Loc);
    addHint(Loc.Hint, Headers);
    addNameMatchHint(MacroRef.Target.getMacro()->Name, Headers);
    Callback(MacroRef.Location, MacroRef.Target, rank(Ctx, Headers));
  }
}

Symbol AnalysisContext::macro(const IdentifierInfo *II, SourceLocation Loc) {
  return cache().macro(II, Loc);
}

} // namespace include_cleaner
} // namespace clang
