//===--- Types.h - Data structures for used-symbol analysis -------- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Find referenced files is mostly a matter of translating:
//    AST Node => declaration => source location => file
//
// clang has types for these (DynTypedNode, Decl, SourceLocation, FileID), but
// there are special cases: macros are not declarations, the concrete file where
// a standard library symbol was defined doesn't matter, etc.
//
// We define some slightly more abstract sum types to handle these cases while
// keeping the API clean. For example, Symbol is Decl+DefinedMacro.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_INCLUDE_CLEANER_TYPES_H
#define CLANG_INCLUDE_CLEANER_TYPES_H

#include "clang/AST/DeclBase.h"
#include "clang/Tooling/Inclusions/StandardLibrary.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/PointerSumType.h"

namespace clang {
class IdentifierInfo;
class MacroDirective;
namespace include_cleaner {

// Identifies a macro, along with a particular definition of it.
// We generally consider redefined macros to be different symbols.
struct DefinedMacro {
  const IdentifierInfo *Name;
  const SourceLocation Definition;
};

// A Symbol is an entity that can be referenced.
// It is either a declaration (NamedDecl) or a macro (DefinedMacro).
class Symbol {
public:
  enum Kind {
    Macro,
    Declaration,
  };
  Symbol(NamedDecl *ND) : Target(ND) {}
  Symbol(const DefinedMacro *M) : Target(M) {}

  std::string name() const;
  std::string nodeName() const;
  Kind kind() const { return Target.is<NamedDecl *>() ? Declaration : Macro; }

  NamedDecl *getDeclaration() const { return Target.get<NamedDecl *>(); }
  const DefinedMacro *getMacro() const {
    return Target.get<const DefinedMacro *>();
  }

private:
  llvm::PointerUnion<const DefinedMacro *, NamedDecl *> Target;
};

// A usage of a Symbol seen in our source code.
struct SymbolReference {
  // The point in the code where the reference occurred.
  // We could track the DynTypedNode we found it in if it's important.
  SourceLocation Location;
  Symbol Target;
};

// A Location is a place where a symbol can be provided.
// It is either a physical part of the TU (SourceLocation) or a logical location
// in the standard library (stdlib::Symbol).
class Location {
public:
  enum Kind : uint8_t {
    Physical,
    StandardLibrary,
  };

  Location(SourceLocation S) : K(Physical), SrcLoc(S) {}
  Location(tooling::stdlib::Symbol S) : K(StandardLibrary), StdlibSym(S) {}

  std::string name(const SourceManager &SM) const;
  Kind kind() const { return K; }

  SourceLocation getPhysical() const {
    assert(kind() == Physical);
    return SrcLoc;
  };
  tooling::stdlib::Symbol getStandardLibrary() const {
    assert(kind() == StandardLibrary);
    return StdlibSym;
  };

private:
  Kind K;
  union {
    SourceLocation SrcLoc;
    tooling::stdlib::Symbol StdlibSym;
  };
};

// A Header is an includable file that can provide access to Locations.
// It is either a physical file (FileEntry), a logical location in the standard
// library (stdlib::Header), or a verbatim header spelling (StringRef).
class Header {
public:
  enum Kind : uint8_t {
    Physical,
    StandardLibrary,
    Verbatim,
    Builtin,
    MainFile,
  };

  Header(const FileEntry *FE) : K(Physical), PhysicalFile(FE) {}
  Header(tooling::stdlib::Header H) : K(StandardLibrary), StdlibHeader(H) {}
  Header(const char *V) : K(Verbatim), VerbatimSpelling(V) {}
  static Header builtin() { return Header{Builtin}; };
  static Header mainFile() { return Header{MainFile}; };

  std::string name() const;
  Kind kind() const { return K; }

  const FileEntry *getPhysical() const {
    assert(kind() == Physical);
    return PhysicalFile;
  };
  tooling::stdlib::Header getStandardLibrary() const {
    assert(kind() == StandardLibrary);
    return StdlibHeader;
  };
  llvm::StringRef getVerbatimSpelling() const {
    assert(kind() == Verbatim);
    return VerbatimSpelling;
  };

private:
  Header(Kind K) : K(K) {}

  Kind K;
  union {
    const FileEntry *PhysicalFile;
    tooling::stdlib::Header StdlibHeader;
    const char *VerbatimSpelling;
  };

  friend bool operator==(const Header &L, const Header &R) {
    if (L.kind() != R.kind())
      return false;
    switch (L.kind()) {
    case Physical:
      return L.getPhysical() == R.getPhysical();
    case StandardLibrary:
      return L.getStandardLibrary() == R.getStandardLibrary();
    case Verbatim:
      return L.getVerbatimSpelling() == R.getVerbatimSpelling();
    case Builtin:
    case MainFile:
      return true; // no payload
    }
    llvm_unreachable("unhandled Header kind");
  }

  friend bool operator<(const Header &L, const Header &R) {
    if (L.kind() != R.kind())
      return L.kind() < R.kind();
    switch (L.kind()) {
    case Physical:
      return L.getPhysical() == R.getPhysical();
    case StandardLibrary:
      return L.getStandardLibrary() < R.getStandardLibrary();
    case Verbatim:
      return L.getVerbatimSpelling() < R.getVerbatimSpelling();
    case Builtin:
    case MainFile:
      return false; // no payload
    }
    llvm_unreachable("unhandled Header kind");
  }

  friend llvm::hash_code hash_value(const Header &H) {
    switch (H.K) {
    case Header::Physical:
      return llvm::hash_combine(H.K, H.getPhysical());
    case Header::StandardLibrary:
      // FIXME: make StdlibHeader hashable instead.
      return llvm::hash_combine(H.K, H.getStandardLibrary().name());
    case Header::Verbatim:
      return llvm::hash_combine(H.K, llvm::StringRef(H.VerbatimSpelling));
    case Header::Builtin:
    case Header::MainFile:
      return llvm::hash_value(H.K);
    }
  }
};

template <typename T> struct DefaultDenseMapInfo {
  static T isEqual(const T &L, const T &R) { return L == R; }
  static unsigned getHashValue(const T &V) { return hash_value(V); }
};

} // namespace include_cleaner
} // namespace clang

namespace llvm {
template <> struct DenseMapInfo<clang::include_cleaner::Header> {
  using Header = clang::include_cleaner::Header;
  static Header getTombstoneKey() { return Header("__tombstone__"); }
  static Header getEmptyKey() { return Header("__empty__"); }
  static bool isEqual(const Header &L, const Header &R) { return L == R; }
  static unsigned getHashValue(const Header &V) { return hash_value(V); }
};
} // namespace llvm

#endif
