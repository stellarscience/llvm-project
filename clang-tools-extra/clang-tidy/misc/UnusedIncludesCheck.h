//===--- UnusedIncludesCheck.h - clang-tidy----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MISC_UNUSED_INCLUDES_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MISC_UNUSED_INCLUDES_H

#include "../ClangTidyCheck.h"

namespace clang {
namespace include_cleaner {
class AnalysisContext;
struct RecordedPP;
} // namespace include_cleaner
namespace tidy {
namespace misc {

class UnusedIncludesCheck : public ClangTidyCheck {
public:
  UnusedIncludesCheck(StringRef Name, ClangTidyContext *Context);
  ~UnusedIncludesCheck();
  void registerPPCallbacks(const SourceManager &SM, Preprocessor *,
                           Preprocessor *) override;
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
  void onEndOfTranslationUnit() override;

private:
  std::unique_ptr<include_cleaner::AnalysisContext> Ctx;
  std::unique_ptr<include_cleaner::RecordedPP> RecordedPP;
  std::vector<Decl *> Top;
};

} // namespace misc
} // namespace tidy
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MISC_UNUSED_INCLUDES_H
