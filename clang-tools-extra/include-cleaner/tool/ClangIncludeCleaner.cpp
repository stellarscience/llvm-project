//===--- ClangIncludeCleaner.cpp - Standalone used-header analysis --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// clang-include-cleaner finds violations of include-what-you-use policy.
//
// It scans a file, finding referenced symbols and headers providing them.
//   - if a reference is satisfied only by indirect #include dependencies,
//     this violates the policy and direct #includes are suggested.
//   - if some #include directive doesn't satisfy any references, this violates
//     the policy (don't include what you don't use!) and removal is suggested.
//
// With the -satisfied flag, it will also explain things that were OK:
// satisfied references and used #includes.
//
// This tool doesn't fix broken code where missing #includes prevent parsing,
// try clang-include-fixer for this instead.
//
//===----------------------------------------------------------------------===//

#include "clang-include-cleaner/Analysis.h"
#include "clang-include-cleaner/Hooks.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"

llvm::cl::OptionCategory OptionsCat{"clang-include-cleaner"};
llvm::cl::opt<bool> ShowSatisfied{
    "satisfied",
    llvm::cl::cat(OptionsCat),
    llvm::cl::desc(
        "Show references whose header is included, and used includes"),
    llvm::cl::init(false),
};
llvm::cl::opt<bool> Recover{
    "recover",
    llvm::cl::cat(OptionsCat),
    llvm::cl::desc("Suppress further errors for the same header"),
    llvm::cl::init(true),
};

namespace clang {
namespace include_cleaner {
namespace {

class Action : public clang::ASTFrontendAction {
public:
  bool BeginSourceFileAction(CompilerInstance &CI) override {
    Diag = &CI.getDiagnostics();
    ID.emplace(Diag);
    Ctx.emplace(Policy{}, CI.getPreprocessor());
    CI.getPreprocessor().addPPCallbacks(PP.record(*Ctx));
    return true;
  }

  void EndSourceFile() override {
    llvm::DenseSet<Header> Recovered;
    llvm::DenseMap<const RecordedPP::Include *, Symbol> Used;
    walkUsed(*Ctx, AST.TopLevelDecls, PP.MacroReferences,
             [&](SourceLocation Loc, Symbol Sym, ArrayRef<Header> Headers) {
               diagnoseReference(Loc, Sym, Headers, Recovered, Used);
             });
    diagnoseIncludes(PP.Includes.all(), Used);
    Ctx.reset();

    ASTFrontendAction::EndSourceFile();
  }

  virtual std::unique_ptr<ASTConsumer>
  CreateASTConsumer(CompilerInstance &CI, StringRef InFile) override {
    return AST.record(*Ctx);
  }

private:
  // The diagnostics that we issue.
  struct CustomDiagnosticIDs {
    // References
    unsigned Satisfied;
    unsigned Unsatisfied;
    unsigned NoHeader;
    unsigned NoteHeader;
    // #includes
    unsigned Used;
    unsigned Unused;

    CustomDiagnosticIDs(DiagnosticsEngine *D) {
      auto SatisfiedLevel = ShowSatisfied ? DiagnosticsEngine::Remark
                                          : DiagnosticsEngine::Ignored;
      auto Error = DiagnosticsEngine::Error;
      auto Note = DiagnosticsEngine::Note;
      auto Warn = DiagnosticsEngine::Warning;

      Satisfied = D->getCustomDiagID(SatisfiedLevel, "%0 '%1' provided by %2");
      Unsatisfied = D->getCustomDiagID(Error, "no header included for %0 '%1'");
      NoHeader = D->getCustomDiagID(Warn, "unknown header provides %0 '%1'");
      NoteHeader = D->getCustomDiagID(Note, "provided by %0");
      Used = D->getCustomDiagID(SatisfiedLevel, "include provides %0 '%1'");
      Unused = D->getCustomDiagID(Error, "include is unused");
    }
  };

  void
  diagnoseReference(SourceLocation Loc, Symbol Sym, ArrayRef<Header> Headers,
                    llvm::DenseSet<Header> &Recovered,
                    llvm::DenseMap<const RecordedPP::Include *, Symbol> &Used) {
    bool Diagnosed = false;
    for (const auto &H : Headers) {
      if (H.kind() == Header::Builtin || H.kind() == Header::MainFile) {
        if (!Diagnosed) {
          Diag->Report(Loc, ID->Satisfied)
              << Sym.nodeName() << Sym.name() << H.name();
          Diagnosed = true;
        }
      }
      for (const auto *I : PP.Includes.match(H)) {
        Used.try_emplace(I, Sym);
        if (!Diagnosed) {
          Diag->Report(Loc, ID->Satisfied)
              << Sym.nodeName() << Sym.name() << I->Spelled;
          Diagnosed = true;
        }
      }
    }
    if (Diagnosed)
      return;
    for (const auto &H : Headers) {
      if (Recovered.contains(H)) {
        Diag->Report(Loc, ID->Satisfied)
            << Sym.nodeName() << Sym.name() << H.name();
        return;
      }
    }
    Diag->Report(Loc, Headers.empty() ? ID->NoHeader : ID->Unsatisfied)
        << Sym.nodeName() << Sym.name();
    for (const auto &H : Headers) {
      Recovered.insert(H);
      Diag->Report(ID->NoteHeader) << H.name();
    }
  }

  void diagnoseIncludes(
      ArrayRef<RecordedPP::Include> Includes,
      const llvm::DenseMap<const RecordedPP::Include *, Symbol> &Used) {
    for (const auto &I : Includes) {
      auto It = Used.find(&I);
      if (It == Used.end())
        Diag->Report(I.Location, ID->Unused);
      else
        Diag->Report(I.Location, ID->Used)
            << It->second.nodeName() << It->second.name();
    }
  }

  llvm::Optional<AnalysisContext> Ctx;
  RecordedPP PP;
  RecordedAST AST;
  DiagnosticsEngine *Diag;
  llvm::Optional<CustomDiagnosticIDs> ID;
};

} // namespace
} // namespace include_cleaner
} // namespace clang

int main(int Argc, const char **Argv) {
  llvm::InitLLVM X(Argc, Argv);
  auto OptionsParser =
      clang::tooling::CommonOptionsParser::create(Argc, Argv, OptionsCat);
  if (!OptionsParser) {
    llvm::errs() << toString(OptionsParser.takeError());
    return 1;
  }

  return clang::tooling::ClangTool(OptionsParser->getCompilations(),
                                   OptionsParser->getSourcePathList())
      .run(clang::tooling::newFrontendActionFactory<
               clang::include_cleaner::Action>()
               .get());
}
