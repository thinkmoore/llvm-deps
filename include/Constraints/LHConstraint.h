//===-- LHConstraint.h ------------------------------------------*- C++ -*-===//
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

#ifndef LHCONSTRAINT_H_
#define LHCONSTRAINT_H_

#include "llvm/ADT/DenseMap.h"

namespace deps {

    class ConsElem;

    class LHConstraint {
    public:
        LHConstraint(const ConsElem &lhs, const ConsElem &rhs) : left(&lhs), right(&rhs) {}
        LHConstraint(const ConsElem *lhs, const ConsElem *rhs) : left(lhs), right(rhs) {}
        const ConsElem & lhs() const { return *left; }
        const ConsElem & rhs() const { return *right; }
    private:
        const ConsElem * left;
        const ConsElem * right;
        friend struct llvm::DenseMapInfo<LHConstraint>;
    };

}




#endif /* LHCONSTRAINT_H_ */
