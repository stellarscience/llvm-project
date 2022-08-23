set(LLVM_LINK_COMPONENTS support)

add_clang_tool(clang-include-cleaner
  ClangIncludeCleaner.cpp
  )

clang_target_link_libraries(clang-include-cleaner
  PRIVATE
  clangBasic
  clangFrontend
  clangTooling
  )

target_link_libraries(clang-include-cleaner
  PRIVATE
  clangIncludeCleaner
  )


