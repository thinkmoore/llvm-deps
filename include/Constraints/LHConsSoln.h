//===-- LHConsSoln.h --------------------------------------------*- C++ -*-===//
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

#ifndef LHCONSSOLN_H_
#define LHCONSSOLN_H_

#include "Constraints/ConstraintKit.h"
#include "Constraints/LHConstraints.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

#include <deque>
#include <map>
#include <set>
#include <vector>

namespace deps {

    class LHConstraint;
    class LHConstant;
    class LHConstraintKit;

    class LHConsSoln : public ConsSoln {
    public:
        LHConsSoln(LHConstraintKit & kit, const LHConstant & defaultValue, std::vector<const LHConstraint*> *constraints);
        virtual const LHConstant &subst(const ConsElem & elem);
    protected:
        LHConstraintKit &kit;

        const LHConstant & defaultValue;
        std::vector<const LHConstraint*> *constraints;

        std::deque<const LHConstraint *> queue;
        llvm::DenseSet<const LHConstraint*> queueSet;

        llvm::DenseSet<const ConsVar *> changed;
        bool solved;

        void solve(void);
        void enqueueConstraints(const std::vector<const LHConstraint*> & constraints);
        const LHConstraint &dequeueConstraint(void);

        virtual ~LHConsSoln();
        virtual void releaseMemory() = 0;
        virtual void satisfyConstraint(const LHConstraint & constraint, const ConsElem & left, const ConsElem & right) = 0;
    };

    class LHConsGreatestSoln : public LHConsSoln {
        llvm::DenseMap<const ConsVar*, std::vector<const LHConstraint*> > invalidIfDecreased;
        std::vector<const LHConstraint*> &getOrCreateInvalidIfDecreasedSet(const ConsVar *var);
        void addInvalidIfDecreased(const ConsVar *var, const LHConstraint *c);
        public:
            LHConsGreatestSoln(LHConstraintKit & kit, std::vector<const LHConstraint*> * constraints);
        protected:
            virtual void satisfyConstraint(const LHConstraint & constraint, const ConsElem & left, const ConsElem & right);
            virtual void releaseMemory() {
              invalidIfDecreased.clear();
            }
        };

    class LHConsLeastSoln : public LHConsSoln {
        llvm::DenseMap<const ConsVar *, std::vector<const LHConstraint*> > invalidIfIncreased;
        std::vector<const LHConstraint*> &getOrCreateInvalidIfIncreasedSet(const ConsVar *var);
        void addInvalidIfIncreased(const ConsVar *var, const LHConstraint *c);
        public:
            LHConsLeastSoln(LHConstraintKit & kit, std::vector<const LHConstraint*> * constraints);
        protected:
            virtual void satisfyConstraint(const LHConstraint & constraint, const ConsElem & left, const ConsElem & right);
            virtual void releaseMemory() {
              invalidIfIncreased.clear();
            }
        };

}

#endif /* LHCONSSOLN_H_ */
