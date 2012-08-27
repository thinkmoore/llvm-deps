//===-- LHConsSoln.cpp ------------------------------------------*- C++ -*-===//
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

#include "Constraints/LHConsSoln.h"
#include "Constraints/LHConstraint.h"
#include "Constraints/LHConstraintKit.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

namespace deps {

    LHConsSoln::LHConsSoln(LHConstraintKit & kit, const LHConstant & defaultValue, std::vector<const LHConstraint*> *constraints) :
          kit(kit), defaultValue(defaultValue), constraints(constraints), solved(false) {}

    const LHConstant &LHConsSoln::subst(const ConsElem & elem) {
        solve();
        if (const LHConsVar *var = llvm::dyn_cast<LHConsVar>(&elem)) {
            const LHConstant & other = (defaultValue == LHConstant::low()) ?
                                        LHConstant::high() : LHConstant::low();
            return changed.count(var) ? other : defaultValue;
        } else if (const LHConstant *var = llvm::dyn_cast<LHConstant>(&elem)) {
            return *var;
        } else if (const LHJoin *join = llvm::dyn_cast<LHJoin>(&elem)) {
            const std::set<const ConsElem *> & elements = join->elements();
            const LHConstant *substVal = &defaultValue;
            for (std::set<const ConsElem *>::iterator elem = elements.begin(), end = elements.end(); elem != end; ++elem) {
                substVal = &(substVal->join(subst(**elem)));
            }
            return *substVal;
        } else {
            assert("Unknown security policy");
        }
        llvm_unreachable("Unknown security policy");
    }

    void LHConsSoln::enqueueConstraints(const std::vector<const LHConstraint*> & constraints) {
        for (std::vector<const LHConstraint*>::const_iterator elem = constraints.begin(), end = constraints.end(); elem != end; ++elem) {
            if (queueSet.insert(*elem).second)
              queue.push_back(*elem);
        }
    }

    const LHConstraint &LHConsSoln::dequeueConstraint() {
        const LHConstraint & front = *(queue.front());
        queue.pop_front();
        queueSet.erase(&front);
        return front;
    }

    void LHConsSoln::solve(void) {
        if (solved) return;

        //llvm::errs() << "Solving ";
        //llvm::errs() << constraints.size();
        //llvm::errs() << " constraints\n";
        solved = true;
        // Add all of the constraint to the queue
        enqueueConstraints(*constraints);

        unsigned int number = 0;
        while (!queue.empty()) {
            number++;
            const LHConstraint &c = dequeueConstraint();
            const ConsElem & left = c.lhs();
            const ConsElem & right = c.rhs();
            if (subst(left).leq(subst(right))) {
                // The constraint is already satisfied!
            } else {
                // Need to satisfy the constraint
                satisfyConstraint(c, left, right);
            }
        }
        //llvm::errs() << "Solved after ";
        //llvm::errs() << number;
        //llvm::errs() << " iterations\n";

        // Free contrainsts and other unneeded datastructures
        delete constraints;
        constraints = NULL;
        releaseMemory();
    }

    LHConsSoln::~LHConsSoln() {
      delete constraints;
    }


    LHConsLeastSoln::LHConsLeastSoln(LHConstraintKit & kit, std::vector<const LHConstraint*> * constraints) :
                LHConsSoln(kit, LHConstant::low(), constraints) {
        std::set<const ConsVar *> leftVariables;
        for (std::vector<const LHConstraint*>::iterator cons = constraints->begin(), end = constraints->end();
                cons != end; ++cons) {
            leftVariables.clear();
            (*cons)->lhs().variables(leftVariables);

            for (std::set<const ConsVar *>::iterator var = leftVariables.begin(),
                    end = leftVariables.end(); var != end; ++var) {
                addInvalidIfIncreased(*var, *cons);
            }
        }
    }

    void LHConsLeastSoln::satisfyConstraint(const LHConstraint & constraint, const ConsElem & left, const ConsElem & right) {
        std::set<const ConsVar *> vars;
        right.variables(vars);
        const LHConstant & L = subst(left);
        for (std::set<const ConsVar *>::iterator var = vars.begin(), end = vars.end(); var != end; ++var) {
            const LHConstant & R = subst(**var);
            if (!L.leq(R)) {
                changed.insert(*var);
                // add every constraint that may now be invalidated by changing var
                enqueueConstraints(getOrCreateInvalidIfIncreasedSet(*var));
            }
        }
    }

    std::vector<const LHConstraint*> &LHConsLeastSoln::getOrCreateInvalidIfIncreasedSet(const ConsVar *var) {
      return invalidIfIncreased[var];
    }

    void LHConsLeastSoln::addInvalidIfIncreased(const ConsVar *var, const LHConstraint *c) {
        std::vector<const LHConstraint*> &varSet = getOrCreateInvalidIfIncreasedSet(var);
        varSet.push_back(c);
    }



    LHConsGreatestSoln::LHConsGreatestSoln(LHConstraintKit & kit, std::vector<const LHConstraint*> * constraints) :
                LHConsSoln(kit, LHConstant::high(), constraints) {
        std::set<const ConsVar *> rightVariables;
        for (std::vector<const LHConstraint*>::iterator cons = constraints->begin(), end = constraints->end();
                cons != end; ++cons) {
            rightVariables.clear();
            (*cons)->rhs().variables(rightVariables);

            for (std::set<const ConsVar *>::iterator var = rightVariables.begin(),
                        end = rightVariables.end(); var != end; ++var) {
                addInvalidIfDecreased(*var, *cons);
            }
        }
    }

    void LHConsGreatestSoln::satisfyConstraint(const LHConstraint & constraint, const ConsElem & left, const ConsElem & right) {
        std::set<const ConsVar *> vars;
        left.variables(vars);
        const LHConstant & R = subst(right);
        for (std::set<const ConsVar *>::iterator var = vars.begin(), end = vars.end(); var != end; ++var) {
            const LHConstant &L = subst(**var);
            if (L.leq(R)) {
                // nothing to do, the variable is already low enough
            } else if (R.leq(L)) {
                // lower the variable var
                changed.insert(*var);
                // add every constraint that may now be invalidated by changing var
                enqueueConstraints(getOrCreateInvalidIfDecreasedSet(*var));
            } else {
                assert(false && "Meets not supported yet... not sure what to do...");
            }
        }
    }

    std::vector<const LHConstraint*> &LHConsGreatestSoln::getOrCreateInvalidIfDecreasedSet(const ConsVar *var) {
      return invalidIfDecreased[var];
    }

    void LHConsGreatestSoln::addInvalidIfDecreased(const ConsVar *var, const LHConstraint *c) {
        std::vector<const LHConstraint*> &varSet = getOrCreateInvalidIfDecreasedSet(var);
        varSet.push_back(c);
    }
}
