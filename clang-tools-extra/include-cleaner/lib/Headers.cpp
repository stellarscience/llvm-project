//===--- Headers.cpp - Find headers that provide locations ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AnalysisInternal.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Preprocessor.h"

namespace clang {
namespace include_cleaner {

llvm::SmallVector<Hinted<Header>> includableHeader(AnalysisContext &Ctx,
                                                   const Location &Loc) {
  switch (Loc.kind()) {
  case Location::Physical: {
    FileID FID = Ctx.sourceManager().getFileID(
        Ctx.sourceManager().getExpansionLoc(Loc.getPhysical()));
    if (FID == Ctx.sourceManager().getMainFileID())
      return {Header::mainFile()};
    if (FID == Ctx.preprocessor().getPredefinesFileID())
      return {Header::builtin()};
    // FIXME: if the file is not self-contained, find its umbrella header:
    //   - files that lack header guards (e.g. *.def)
    //   - IWYU private pragmas (and maybe export?)
    //   - #pragma clang include_instead
    //   - headers containing "#error ... include" clangd isDontIncludeMeHeader
    //   - apple framework header layout
    if (auto *FE = Ctx.sourceManager().getFileEntryForID(FID))
      return {{FE}};
    return {};
  }
  case Location::StandardLibrary:
    // FIXME: some symbols are provided by multiple stdlib headers:
    //   - for historical reasons, like size_t
    //   - some headers are guaranteed to include others (<initializer_list>)
    //   - ::printf is de facto provided by cstdio and stdio.h, etc
    return {{Loc.getStandardLibrary().header()}};
  }
}

} // namespace include_cleaner
} // namespace clang
