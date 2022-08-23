//===--- Analysis.h - Analyze used files --------------------------- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_INCLUDE_CLEANER_ANALYSIS_H
#define CLANG_INCLUDE_CLEANER_ANALYSIS_H

#include "clang-include-cleaner/Policy.h"
#include "clang-include-cleaner/Types.h"

namespace clang {
namespace include_cleaner {
class Cache;

// Bundles the policy, compiler state, and caches for one include-cleaner run.
// (This is needed everywhere, but shouldn't be used to propagate state around!)
class AnalysisContext {
public:
  AnalysisContext(const Policy &, const Preprocessor &);
  AnalysisContext(AnalysisContext &&) = delete;
  AnalysisContext &operator=(AnalysisContext &&) = delete;
  ~AnalysisContext();

  const Policy &policy() const { return P; }

  const SourceManager &sourceManager() const { return *SM; }
  const Preprocessor &preprocessor() const { return *PP; }

  // Only for internal use (the Cache class definition is not exposed).
  // This allows us to reuse e.g. mappings from symbols to their locations.
  Cache &cache() { return *C; }
  // FIXME: does this need to be public?
  Symbol macro(const IdentifierInfo *, SourceLocation);

private:
  Policy P;
  const SourceManager *SM;
  const Preprocessor *PP;
  std::unique_ptr<Cache> C;
};

// A UsedSymbolVisitor is a callback invoked for each symbol reference seen.
//
// References occur at a particular location, refer to a single symbol, and
// that symbol may be provided by any of several headers.
//
// The first element of ProvidedBy is the *preferred* header, e.g. to insert.
using UsedSymbolVisitor =
    llvm::function_ref<void(SourceLocation UsedAt, Symbol UsedSymbol,
                            llvm::ArrayRef<Header> ProvidedBy)>;

// Find and report all references to symbols in a region of code.
//
// The AST traversal is rooted at ASTRoots - typically top-level declarations
// of a single source file. MacroRefs are additional recorded references to
// macros, which do not appear in the AST.
//
// This is the main entrypoint of the include-cleaner library, and can be used:
//  - to diagnose missing includes: a referenced symbol is provided by
//    headers which don't match any #include in the main file
//  - to diagnose unused includes: an #include in the main file does not match
//    the headers for any referenced symbol
//
// Mapping between Header and #include directives is not provided here, but see
// RecordedPP::Includes::match() in Hooks.h.
void walkUsed(AnalysisContext &, llvm::ArrayRef<Decl *> ASTRoots,
              llvm::ArrayRef<SymbolReference> MacroRefs,
              UsedSymbolVisitor Callback);

} // namespace include_cleaner
} // namespace clang

#endif
