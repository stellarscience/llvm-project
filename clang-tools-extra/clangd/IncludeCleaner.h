//===--- IncludeCleaner.h - Unused/Missing Headers Analysis -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Include Cleaner is clangd functionality for providing diagnostics for misuse
/// of transitive headers and unused includes. It is inspired by
/// Include-What-You-Use tool (https://include-what-you-use.org/). Our goal is
/// to provide useful warnings in most popular scenarios but not 1:1 exact
/// feature compatibility.
///
/// FIXME(kirillbobyrev): Add support for IWYU pragmas.
/// FIXME(kirillbobyrev): Add support for standard library headers.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANGD_INCLUDECLEANER_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANGD_INCLUDECLEANER_H

#include "Headers.h"
#include "ParsedAST.h"
#include "clang-include-cleaner/Types.h"
#include "llvm/ADT/DenseSet.h"
#include <vector>

namespace clang {
namespace clangd {

/// Retrieves headers that are referenced from the main file but not used.
/// In unclear cases, headers are not marked as unused.
std::vector<const Inclusion *>
getUnused(ParsedAST &AST,
          const llvm::DenseSet<IncludeStructure::HeaderID> &ReferencedFiles);

std::vector<const Inclusion *> computeUnusedIncludes(ParsedAST &AST);

std::vector<Diag> issueUnusedIncludesDiagnostics(ParsedAST &AST,
                                                 llvm::StringRef Code);

// Does an include-cleaner header spec match a clangd recorded inclusion?
bool match(const include_cleaner::Header &H, const Inclusion &I,
           const IncludeStructure &S);

/// Affects whether standard library includes should be considered for
/// removal. This is off by default for now due to implementation limitations:
/// - macros are not tracked
/// - symbol names without a unique associated header are not tracked
/// - references to std-namespaced C types are not properly tracked:
///   instead of std::size_t -> <cstddef> we see ::size_t -> <stddef.h>
/// FIXME: remove this hack once the implementation is good enough.
void setIncludeCleanerAnalyzesStdlib(bool B);

} // namespace clangd
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANGD_INCLUDECLEANER_H
