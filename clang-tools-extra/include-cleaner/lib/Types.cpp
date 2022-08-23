//===--- Types.cpp - Data structures for used-symbol analysis -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang-include-cleaner/Types.h"
#include "clang/AST/Decl.h"
#include "clang/Basic/FileEntry.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Tooling/Inclusions/StandardLibrary.h"

namespace clang {
namespace include_cleaner {

std::string Symbol::name() const {
  switch (kind()) {
  case Macro:
    return getMacro()->Name->getName().str();
  case Declaration:
    return getDeclaration()->getNameAsString();
  }
  llvm_unreachable("Unhandled Symbol kind");
}

std::string Symbol::nodeName() const {
  if (kind() == Macro)
    return "macro";
  return getDeclaration()->getDeclKindName();
}

std::string Location::name(const SourceManager &SM) const {
  switch (K) {
  case Physical:
    return SrcLoc.printToString(SM);
  case StandardLibrary:
    return StdlibSym.name().str();
  }
  llvm_unreachable("Unhandled Location kind");
}

std::string Header::name() const {
  switch (K) {
  case Physical:
    return PhysicalFile->getName().str();
  case StandardLibrary:
    return StdlibHeader.name().str();
  case Verbatim:
    return VerbatimSpelling;
  case Builtin:
    return "<built-in>";
  case MainFile:
    return "<main-file>";
  }
  llvm_unreachable("Unhandled Header kind");
}

} // namespace include_cleaner
} // namespace clang
