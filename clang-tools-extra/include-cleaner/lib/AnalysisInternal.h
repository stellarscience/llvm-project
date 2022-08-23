//===--- AnalysisInternal.h - Analysis building blocks ------------- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides smaller, testable pieces of the used-header analysis.
// We find the headers by chaining together several mappings.
//
// AST => AST node => Symbol => Location => Header
//                   /
// Macro expansion =>
//
// The individual steps are declared here.
// (AST => AST Node => Symbol is one API to avoid materializing DynTypedNodes).
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_INCLUDE_CLEANER_ANALYSISINTERNAL_H
#define CLANG_INCLUDE_CLEANER_ANALYSISINTERNAL_H

#include "clang-include-cleaner/Analysis.h"
#include "clang-include-cleaner/Types.h"
#include "clang/Tooling/Inclusions/StandardLibrary.h"

namespace clang {
namespace include_cleaner {

// FIXME: Right now we cache nothing, this is just used as an arena for macros.
// Verify we're burning time in repeated analysis and cache partial operations.
class Cache {
public:
  Symbol macro(const IdentifierInfo *Name, const SourceLocation Def) {
    auto &DMS = DefinedMacros[Name->getName()];
    // Linear search. We probably only saw ~1 definition of each macro name.
    for (const DefinedMacro &DM : DMS)
      if (DM.Definition == Def)
        return &DM;
    DMS.push_back(DefinedMacro{Name, Def});
    return &DMS.back();
  }

  tooling::stdlib::Recognizer StdlibRecognizer;

private:
  llvm::StringMap<llvm::SmallVector<DefinedMacro>> DefinedMacros;
};

enum class Hint : uint16_t {
  None = 0,
  Complete = 1,  // Provides a complete definition that is often needed.
                 // e.g. classes, templates.
  NameMatch = 1, // Header name matches the symbol name.
  LLVM_MARK_AS_BITMASK_ENUM(Hint::Complete)
};
LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

template <typename T> struct Hinted {
  Hinted(T Value, Hint H = Hint::None) : Value(Value), Hint(H) {}
  T Value;
  include_cleaner::Hint Hint;

  T &operator*() { return Value; }
  const T &operator*() const { return Value; }
  std::remove_reference_t<T> *operator->() { return &Value; }
  const std::remove_reference_t<T> *operator->() const { return &Value; }
};

// Traverses a subtree of the AST, reporting declarations referenced.
void walkAST(AnalysisContext &, Decl &Root,
             llvm::function_ref<void(SourceLocation, Hinted<NamedDecl &>)>);

// Finds the locations where a declaration is provided.
llvm::SmallVector<Hinted<Location>> locateDecl(AnalysisContext &,
                                               const NamedDecl &);

// Finds the locations where a macro is provided.
Hinted<Location> locateMacro(AnalysisContext &, const DefinedMacro &);

// Finds the headers that provide a location.
llvm::SmallVector<Hinted<Header>> includableHeader(AnalysisContext &,
                                                   const Location &);

} // namespace include_cleaner
} // namespace clang

#endif
