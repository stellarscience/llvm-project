//===--- UnusedIncludesCheck.cpp - clang-tidy------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "UnusedIncludesCheck.h"
#include "clang-include-cleaner/Analysis.h"
#include "clang-include-cleaner/Hooks.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Lex/Preprocessor.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace misc {

UnusedIncludesCheck::UnusedIncludesCheck(StringRef Name,
                                         ClangTidyContext *Context)
    : ClangTidyCheck(Name, Context) {}

void UnusedIncludesCheck::registerPPCallbacks(const SourceManager &SM,
                                              Preprocessor *PP,
                                              Preprocessor *) {
  Ctx = std::make_unique<include_cleaner::AnalysisContext>(
      include_cleaner::Policy{}, *PP);
  RecordedPP = std::make_unique<include_cleaner::RecordedPP>();
  PP->addPPCallbacks(RecordedPP->record(*Ctx));
}

void UnusedIncludesCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(
      translationUnitDecl(forEach(decl(isExpansionInMainFile()).bind("top"))),
      this);
}

void UnusedIncludesCheck::check(const MatchFinder::MatchResult &Result) {
  Top.push_back(const_cast<Decl *>(Result.Nodes.getNodeAs<Decl>("top")));
}

void UnusedIncludesCheck::onEndOfTranslationUnit() {
  llvm::DenseSet<const include_cleaner::RecordedPP::Include *> Used;
  llvm::DenseSet<include_cleaner::Header> Seen;
  include_cleaner::walkUsed(
      *Ctx, Top, RecordedPP->MacroReferences,
      [&](SourceLocation Loc, include_cleaner::Symbol Sym,
          llvm::ArrayRef<include_cleaner::Header> Headers) {
        for (const auto &Header : Headers) {
          if (!Seen.insert(Header).second)
            continue;
          for (const auto *I : RecordedPP->Includes.match(Header))
            Used.insert(I);
        }
      });
  for (const auto &I : RecordedPP->Includes.all()) {
    if (!Used.contains(&I)) {
      const auto &SM = Ctx->sourceManager();
      FileID FID = SM.getFileID(I.Location);
      diag(I.Location, "include is unused")
          << FixItHint::CreateRemoval(CharSourceRange::getCharRange(
                 SM.translateLineCol(FID, I.Line, 1),
                 SM.translateLineCol(FID, I.Line + 1, 1)));
    }
  }
}

UnusedIncludesCheck::~UnusedIncludesCheck() = default;

} // namespace misc
} // namespace tidy
} // namespace clang
