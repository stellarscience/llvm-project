//===--- IncludeCleaner.cpp - Unused/Missing Headers Analysis ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IncludeCleaner.h"
#include "Config.h"
#include "Headers.h"
#include "ParsedAST.h"
#include "Protocol.h"
#include "SourceCode.h"
#include "clang-include-cleaner/Analysis.h"
#include "clang-include-cleaner/Types.h"
#include "support/Logger.h"
#include "support/Trace.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Tooling/Syntax/Tokens.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Path.h"

namespace clang {
namespace clangd {

static bool AnalyzeStdlib = false;
void setIncludeCleanerAnalyzesStdlib(bool B) { AnalyzeStdlib = B; }

namespace {

// Returns the range starting at '#' and ending at EOL. Escaped newlines are not
// handled.
clangd::Range getDiagnosticRange(llvm::StringRef Code, unsigned HashOffset) {
  clangd::Range Result;
  Result.end = Result.start = offsetToPosition(Code, HashOffset);

  // Span the warning until the EOL or EOF.
  Result.end.character +=
      lspLength(Code.drop_front(HashOffset).take_until([](char C) {
        return C == '\n' || C == '\r';
      }));
  return Result;
}

// Finds locations of macros referenced from within the main file. That includes
// references that were not yet expanded, e.g `BAR` in `#define FOO BAR`.
std::vector<include_cleaner::SymbolReference>
findReferencedMacros(ParsedAST &AST, include_cleaner::AnalysisContext &Ctx) {
  trace::Span Tracer("IncludeCleaner::findReferencedMacros");
  std::vector<include_cleaner::SymbolReference> Result;
  // FIXME(kirillbobyrev): The macros from the main file are collected in
  // ParsedAST's MainFileMacros. However, we can't use it here because it
  // doesn't handle macro references that were not expanded, e.g. in macro
  // definitions or preprocessor-disabled sections.
  //
  // Extending MainFileMacros to collect missing references and switching to
  // this mechanism (as opposed to iterating through all tokens) will improve
  // the performance of findReferencedMacros and also improve other features
  // relying on MainFileMacros.
  for (const syntax::Token &Tok :
       AST.getTokens().spelledTokens(AST.getSourceManager().getMainFileID())) {
    auto Macro = locateMacroAt(Tok, AST.getPreprocessor());
    if (!Macro)
      continue;
    auto Loc = Macro->Info->getDefinitionLoc();
    if (Loc.isValid())
      Result.push_back(include_cleaner::SymbolReference{
          Tok.location(),
          Ctx.macro(AST.getPreprocessor().getIdentifierInfo(Macro->Name),
                    Loc)});
  }
  return Result;
}

static bool mayConsiderUnused(const Inclusion &Inc, ParsedAST &AST) {
  if (Inc.BehindPragmaKeep)
    return false;

  // FIXME(kirillbobyrev): We currently do not support the umbrella headers.
  // System headers are likely to be standard library headers.
  // Until we have good support for umbrella headers, don't warn about them.
  if (Inc.Written.front() == '<') {
    if (AnalyzeStdlib && tooling::stdlib::Header::named(Inc.Written))
      return true;
    return false;
  }
  // Headers without include guards have side effects and are not
  // self-contained, skip them.
  assert(Inc.HeaderID);
  auto FE = AST.getSourceManager().getFileManager().getFile(
      AST.getIncludeStructure().getRealPath(
          static_cast<IncludeStructure::HeaderID>(*Inc.HeaderID)));
  assert(FE);
  if (!AST.getPreprocessor().getHeaderSearchInfo().isFileMultipleIncludeGuarded(
          *FE)) {
    dlog("{0} doesn't have header guard and will not be considered unused",
         (*FE)->getName());
    return false;
  }
  return true;
}

} // namespace

std::vector<const Inclusion *>
getUnused(ParsedAST &AST,
          const llvm::DenseSet<IncludeStructure::HeaderID> &ReferencedFiles) {
  trace::Span Tracer("IncludeCleaner::getUnused");
  std::vector<const Inclusion *> Unused;
  for (const Inclusion &MFI : AST.getIncludeStructure().MainFileIncludes) {
    if (!MFI.HeaderID)
      continue;
    auto IncludeID = static_cast<IncludeStructure::HeaderID>(*MFI.HeaderID);
    bool Used = ReferencedFiles.contains(IncludeID);
    if (!Used && !mayConsiderUnused(MFI, AST)) {
      dlog("{0} was not used, but is not eligible to be diagnosed as unused",
           MFI.Written);
      continue;
    }
    if (!Used)
      Unused.push_back(&MFI);
    dlog("{0} is {1}", MFI.Written, Used ? "USED" : "UNUSED");
  }
  return Unused;
}

bool match(const include_cleaner::Header &H, const Inclusion &I,
           const IncludeStructure &S) {
  switch (H.kind()) {
  case include_cleaner::Header::Physical:
    if (auto HID = S.getID(H.getPhysical()))
      if (static_cast<unsigned>(*HID) == I.HeaderID)
        return true;
    break;
  case include_cleaner::Header::StandardLibrary:
    return I.Written == H.getStandardLibrary().name();
  case include_cleaner::Header::Verbatim:
    return llvm::StringRef(I.Written).trim("\"<>") == H.getVerbatimSpelling();
  case include_cleaner::Header::Builtin:
  case include_cleaner::Header::MainFile:
    break;
  }
  return false;
}

std::vector<const Inclusion *> computeUnusedIncludes(ParsedAST &AST) {
  include_cleaner::AnalysisContext Ctx(include_cleaner::Policy{},
                                       AST.getPreprocessor());
  llvm::DenseSet<const Inclusion *> Used;
  include_cleaner::walkUsed(
      Ctx, AST.getLocalTopLevelDecls(),
      /*MacroRefs=*/findReferencedMacros(AST, Ctx),
      [&](SourceLocation Loc, include_cleaner::Symbol Sym,
          llvm::ArrayRef<include_cleaner::Header> Headers) {
        for (const auto &I : AST.getIncludeStructure().MainFileIncludes)
          for (const auto &H : Headers)
            if (match(H, I, AST.getIncludeStructure()))
              Used.insert(&I);
      });
  std::vector<const Inclusion *> Unused;
  for (const auto &I : AST.getIncludeStructure().MainFileIncludes) {
    if (!Used.contains(&I) && mayConsiderUnused(I, AST))
      Unused.push_back(&I);
  }
  return Unused;
}

std::vector<Diag> issueUnusedIncludesDiagnostics(ParsedAST &AST,
                                                 llvm::StringRef Code) {
  const Config &Cfg = Config::current();
  if (Cfg.Diagnostics.UnusedIncludes != Config::UnusedIncludesPolicy::Strict ||
      Cfg.Diagnostics.SuppressAll ||
      Cfg.Diagnostics.Suppress.contains("unused-includes"))
    return {};
  trace::Span Tracer("IncludeCleaner::issueUnusedIncludesDiagnostics");
  std::vector<Diag> Result;
  std::string FileName =
      AST.getSourceManager()
          .getFileEntryForID(AST.getSourceManager().getMainFileID())
          ->getName()
          .str();
  for (const auto *Inc : computeUnusedIncludes(AST)) {
    Diag D;
    D.Message =
        llvm::formatv("included header {0} is not used",
                      llvm::sys::path::filename(
                          Inc->Written.substr(1, Inc->Written.size() - 2),
                          llvm::sys::path::Style::posix));
    D.Name = "unused-includes";
    D.Source = Diag::DiagSource::Clangd;
    D.File = FileName;
    D.Severity = DiagnosticsEngine::Warning;
    D.Tags.push_back(Unnecessary);
    D.Range = getDiagnosticRange(Code, Inc->HashOffset);
    // FIXME(kirillbobyrev): Removing inclusion might break the code if the
    // used headers are only reachable transitively through this one. Suggest
    // including them directly instead.
    // FIXME(kirillbobyrev): Add fix suggestion for adding IWYU pragmas
    // (keep/export) remove the warning once we support IWYU pragmas.
    D.Fixes.emplace_back();
    D.Fixes.back().Message = "remove #include directive";
    D.Fixes.back().Edits.emplace_back();
    D.Fixes.back().Edits.back().range.start.line = Inc->HashLine;
    D.Fixes.back().Edits.back().range.end.line = Inc->HashLine + 1;
    D.InsideMainFile = true;
    Result.push_back(std::move(D));
  }
  return Result;
}

} // namespace clangd
} // namespace clang
