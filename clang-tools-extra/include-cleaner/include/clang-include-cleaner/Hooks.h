//===--- Hooks.h - Record compiler events -------------------------- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Where Analysis.h analyzes AST nodes and recorded preprocessor events, this
// file defines ways to capture AST and preprocessor information from a parse.
//
// These are the simplest way to connect include-cleaner logic to the parser,
// but other ways are possible (for example clangd records includes separately).
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_INCLUDE_CLEANER_HOOKS_H
#define CLANG_INCLUDE_CLEANER_HOOKS_H

#include "Analysis.h"
#include "Types.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include <vector>

namespace clang {
class FileEntry;
class PPCallbacks;
namespace include_cleaner {
class PPRecorder;

// Contains recorded preprocessor events relevant to include-cleaner.
struct RecordedPP {
  // The callback (when installed into clang) tracks macros/includes in this.
  std::unique_ptr<PPCallbacks> record(AnalysisContext &Ctx);
  // FIXME: probably also want a comment handler to capture IWYU pragmas.

  // Describes where macros were used from the main file.
  std::vector<SymbolReference> MacroReferences;

  // A single #include directive from the main file.
  struct Include {
    llvm::StringRef Spelled;   // e.g. vector
    const FileEntry *Resolved; // e.g. /path/to/c++/v1/vector
    SourceLocation Location;   // of hash in #include <vector>
    unsigned Line;             // 1-based line number for #include
  };
  // The set of includes recorded from the main file.
  class RecordedIncludes {
  public:
    // All #includes seen, in the order they appear.
    llvm::ArrayRef<Include> all() const { return All; }
    // Determine #includes that match a header (that provides a used symbol).
    //
    // Matching is based on the type of Header specified:
    //  - for a physical file like /path/to/foo.h, we check Resolved
    //  - for a logical file like <vector>, we check Spelled
    llvm::SmallVector<const Include *> match(Header H) const;

  private:
    std::vector<Include> All;
    llvm::StringMap<llvm::SmallVector<unsigned>> BySpelling;
    llvm::DenseMap<const FileEntry *, llvm::SmallVector<unsigned>> ByFile;
    friend PPRecorder;
  } Includes;
};

// Contains recorded parser events relevant to include-cleaner.
struct RecordedAST {
  // The consumer (when installed into clang) tracks declarations in this.
  std::unique_ptr<ASTConsumer> record(AnalysisContext &Ctx);

  // The set of declarations written at file scope inside the main file.
  //
  // These are the roots of the subtrees that should be traversed to find uses.
  // (Traversing the TranslationUnitDecl would find uses inside headers!)
  std::vector<Decl *> TopLevelDecls;
};

} // namespace include_cleaner
} // namespace clang

#endif
