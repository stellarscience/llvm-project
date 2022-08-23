set(LLVM_LINK_COMPONENTS Support)

add_clang_library(clangIncludeCleaner
  Analysis.cpp
  Headers.cpp
  Hooks.cpp
  Locations.cpp
  Types.cpp
  WalkAST.cpp

  LINK_LIBS
  clangBasic
  clangLex
  clangAST
  )

