//===-- LHConstraintKit.cpp -------------------------------------*- C++ -*-===//
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

#define DEBUG_TYPE "deps"

#include "Constraints/LHConstraintKit.h"
#include "Constraints/LHConstraints.h"
#include "Constraints/LHConsSoln.h"
#include "Constraints/PartialSolution.h"

#include "llvm/Support/Casting.h"
#include "llvm/ADT/Statistic.h"

namespace deps {

STATISTIC(explicitLHConstraints, "Number of explicit flow constraints");
STATISTIC(implicitLHConstraints, "Number of implicit flow constraints");

LHConstraintKit::LHConstraintKit() {}

LHConstraintKit::~LHConstraintKit() {
    // Delete all associated LHConsVars
    for (std::vector<const LHConsVar *>::iterator var = vars.begin(), end = vars.end();
            var != end; ++var) {
        delete(*var);
    }

    for (llvm::StringMap<PartialSolution*>::iterator I = leastSolutions.begin(),
         E = leastSolutions.end(); I != E; ++I)
      delete I->second;
    for (llvm::StringMap<PartialSolution*>::iterator I = greatestSolutions.begin(),
         E = greatestSolutions.end(); I != E; ++I)
      delete I->second;
}

const ConsVar &LHConstraintKit::newVar(const std::string description) {
    LHConsVar *var = new LHConsVar(description);
    vars.push_back(var);
    return *var;
}

const ConsElem &LHConstraintKit::lowConstant() const {
    return LHConstant::low();
}

const ConsElem &LHConstraintKit::highConstant() const {
    return LHConstant::high();
}

const ConsElem &LHConstraintKit::upperBound(const ConsElem &e1, const ConsElem &e2) {
    const LHJoin *join = LHJoin::create(e1, e2);
    std::set<LHJoin>::iterator J = joins.insert(*join).first;
    delete join;
    return *J;
}

const ConsElem *LHConstraintKit::upperBound(const ConsElem *e1, const ConsElem *e2) {
    if (e1 == NULL) return e2;
    if (e2 == NULL) return e1;

    const LHJoin *join = LHJoin::create(*e1, *e2);
    std::set<LHJoin>::iterator J = joins.insert(*join).first;
    delete join;
    return &*J;
}

const ConsElem &LHConstraintKit::upperBound(std::set<const ConsElem*> elems) {
    const LHJoin join = LHJoin(elems);
    std::set<LHJoin>::iterator J = joins.insert(join).first;
    return *J;
}

std::vector<LHConstraint> &LHConstraintKit::getOrCreateConstraintSet(
        const std::string kind) {
    return constraints.GetOrCreateValue(kind).getValue();
}

void LHConstraintKit::addConstraint(const std::string kind,
        const ConsElem &lhs, const ConsElem &rhs) {
    if (lockedConstraintKinds.find(kind) != lockedConstraintKinds.end()) {
        assert(false && "Have already started solving this kind and cannot add more constraints.");
    }

    if (kind == "default") explicitLHConstraints++;
    if (kind == "implicit") implicitLHConstraints++;

    std::vector<LHConstraint> &set = getOrCreateConstraintSet(kind);

    assert(!llvm::isa<LHJoin>(&rhs) && "We shouldn't have joins on rhs!");

    if (const LHJoin *left = llvm::dyn_cast<LHJoin>(&lhs)) {
        std::set<const ConsElem *> elems = left->elements();
        for (std::set<const ConsElem *>::iterator elem = elems.begin(),
                end = elems.end(); elem != end; ++elem) {
            const LHConstraint c(**elem, rhs);
            set.push_back(c);
        }
    } else {
        LHConstraint c(lhs, rhs);
        set.push_back(c);
    }
}

ConsSoln *LHConstraintKit::leastSolution(const std::set<std::string> kinds) {
  PartialSolution *PS = NULL;
  for (std::set<std::string>::iterator kind = kinds.begin(), end = kinds.end(); kind != end; ++kind) {
    if (!leastSolutions.count(*kind)) {
      lockedConstraintKinds.insert(*kind);
      leastSolutions[*kind] = new PartialSolution(getOrCreateConstraintSet(*kind), false);
      freeUnneededConstraints(*kind);
    }
    PartialSolution *P = leastSolutions[*kind];

    if (!PS) PS = new PartialSolution(*P);
    else PS->mergeIn(*P);
  }
  assert(PS && "No kinds given?");
  return PS;
}

ConsSoln *LHConstraintKit::greatestSolution(const std::set<std::string> kinds) {
  PartialSolution *PS = NULL;
  for (std::set<std::string>::iterator kind = kinds.begin(), end = kinds.end(); kind != end; ++kind) {
    if (!greatestSolutions.count(*kind)) {
      lockedConstraintKinds.insert(*kind);
      greatestSolutions[*kind] = new PartialSolution(getOrCreateConstraintSet(*kind), true);
      freeUnneededConstraints(*kind);
    }
    PartialSolution *P = greatestSolutions[*kind];

    if (!PS) PS = new PartialSolution(*P);
    else PS->mergeIn(*P);
  }
  assert(PS && "No kinds given?");
  return PS;
}

void LHConstraintKit::freeUnneededConstraints(std::string kind) {
  // If we have the two kinds of PartialSolutions already generated
  // for this kind, then we no longer need the original constraints
  if (lockedConstraintKinds.count(kind) &&
      leastSolutions.count(kind) &&
      greatestSolutions.count(kind)) {
    // Clear out the constraints for this kind!
    getOrCreateConstraintSet(kind).clear();
  }
}

}
