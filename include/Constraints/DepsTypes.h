//===-- DepTypes.h ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// TODO Description
//
//===----------------------------------------------------------------------===//

#ifndef DEPSTYPES_H
#define DEPTSTYPES_H

/// Support for llvm-style RTTI (isa<>, dyn_cast<>, etc.)
namespace deps {
    enum DepsType {
        DT_LHConstant,
        DT_LHConsVar,
        DT_LHJoin
    };
}

#endif /* DEPSTYPES_H */
