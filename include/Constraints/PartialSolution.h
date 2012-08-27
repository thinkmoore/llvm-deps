//===-- PartialSolution.h ---------------------------------------*- C++ -*-===//
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

#ifndef _PARTIAL_SOLUTION_H_
#define _PARTIAL_SOLUTION_H_

#include "Constraints/ConstraintKit.h"
#include "Constraints/LHConstraintKit.h"
#include "Constraints/LHConstraints.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"

#include <vector>

namespace deps {

class PartialSolution : public ConsSoln {
public:
  // Exported types:
  typedef llvm::SmallPtrSet<const ConsVar*,1> VarSet;
  typedef llvm::DenseMap<const ConsVar*, std::vector<const ConsVar*> > PMap;
  typedef std::vector<LHConstraint> Constraints;

  // Constructor, constraints aren't stored
  PartialSolution(Constraints & C, bool initial) : initial(initial) {
    initialize(C);
    propagate();
  }

  // copy constructor
  PartialSolution(PartialSolution &P);

  // Merge in another PartialSolution with this, and re-solve.
  void mergeIn(PartialSolution &P);

  // Evaluate the given ConsElem in our solution environment
  const LHConstant& subst(const ConsElem& E);

private:
  // Construct propagation map and seed VSet
  void initialize(Constraints & C);

  // Solve by propagation
  void propagate();
  // Query VSets of this and all chained solutions
  bool isChanged(const ConsVar*);

  // Member variables:

  // TODO: This data really should be refactored out!!
  // Used to store by-value the PropagationMap when the non-merge ctor is used.
  PMap P;
  // Set of variables with non-default values
  VarSet VSet;

  // Chained solutions
  std::vector<PartialSolution*> Chained;

  // Do we consider variables 'high' intially?
  bool initial;
};

} // end namespace deps
#endif // _PARTIAL_SOLUTION_H_
