//===--- Policy.h - Tuning what is considered used ----------------- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_INCLUDE_CLEANER_POLICY_H
#define CLANG_INCLUDE_CLEANER_POLICY_H

namespace clang {
namespace include_cleaner {

// Provides some fine-tuning of include-cleaner's choices about what is used.
//
// Changing the policy serves two purposes:
// - marking more things used reduces the false-positives for "unused include",
//   while marking fewer things improves "missing include" in the same way.
// - different coding styles may make different decisions about which includes
//   are required.
struct Policy {
  // Does construction count as use of the type, when the type is not named?
  // e.g. printVector({x, y, z});  - is std::vector used?
  bool Construction = false;
  // Is member access tracked as a reference?
  bool Members = false;
  // Are operator calls tracked as references?
  bool Operators = false;
};

} // namespace include_cleaner
} // namespace clang

#endif
