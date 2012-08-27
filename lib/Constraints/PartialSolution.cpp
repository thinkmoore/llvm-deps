//===-- PartialSolution.cpp -------------------------------------*- C++ -*-===//
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

#include "Constraints/PartialSolution.h"

#include "llvm/Support/Casting.h"

#include <deque>

using namespace deps;
using namespace llvm;

// Helper function
static const LHConstant & boolToLHC(bool B) {
  return B ? LHConstant::high() : LHConstant::low();
}

bool PartialSolution::isChanged(const ConsVar *V) {
  for (std::vector<PartialSolution*>::iterator CI = Chained.begin(),
      CE = Chained.end(); CI != CE; ++CI) {
    if ((*CI)->VSet.count(V)) return true;
  }
  return false;
}

const LHConstant& PartialSolution::subst(const ConsElem& E) {
  // If this is a variable, look it up in VSet:
  if (const LHConsVar* V = dyn_cast<LHConsVar>(&E))
    return boolToLHC(initial != isChanged(V));

  // If this is already a constant, return it
  if (const LHConstant *LHC = dyn_cast<LHConstant>(&E))
    return *LHC;

  // Otherwise, this better be a join (asserting cast)
  const LHJoin *J = cast<LHJoin>(&E);
  // Find all elements of the join, and evaluate it recursively
  const std::set<const ConsElem *> & elements = J->elements();

  // XXX: LHConsSoln starts with substVal as the defaultValue,
  // which ...seems wrong?  Seems like this would make all join's
  // evaluate to 'high' unconditionally when solving for greatest.
  // Curiously, however, I'm not seeing any differences in the solutions
  // produced across various CINT2006 benchmarks.  Oh well.
  const LHConstant *substVal = &LHConstant::low();

  for (std::set<const ConsElem *>::iterator elem = elements.begin(),
       end = elements.end(); elem != end; ++elem) {
    substVal = &(substVal->join(subst(**elem)));
  }

  return *substVal;
}

// Copy constructor
PartialSolution::PartialSolution(PartialSolution &P) {
  initial = P.initial;

  // Chain to it
  Chained.push_back(this);
  Chained.insert(Chained.end(), P.Chained.begin(), P.Chained.end());
  std::sort(Chained.begin(), Chained.end());
  Chained.erase(std::unique(Chained.begin(), Chained.end()), Chained.end());

  assert(std::find(Chained.begin(), Chained.end(), &P) != Chained.end());

}

// Merging constructor
void PartialSolution::mergeIn(PartialSolution &P) {
  // Sanity check
  assert(initial == P.initial);

  // Chain to it
  Chained.insert(Chained.end(), P.Chained.begin(), P.Chained.end());

  std::sort(Chained.begin(), Chained.end());
  Chained.erase(std::unique(Chained.begin(), Chained.end()), Chained.end());

  assert(std::find(Chained.begin(), Chained.end(), &P) != Chained.end());

  // And propagate
  propagate();
}

// Only run for normal constructor.
// Scan constraints for non-initial, building up seed VarSet.
void PartialSolution::initialize(Constraints & C) {

  // Add ourselves to the chained list
  Chained.push_back(this);

  // Build propagation map
  std::set<const ConsVar *> vars;
  std::set<const ConsVar *> targets;
  // Build propagation map
  for (Constraints::iterator I = C.begin(), E = C.end(); I != E; ++I) {
    vars.clear();
    targets.clear();
    const ConsElem &From = initial ? I->rhs() : I->lhs();
    const ConsElem &To = initial ? I->lhs() : I->rhs();

    From.variables(vars);
    To.variables(targets);

    if (targets.empty()) continue;

    for (std::set<const ConsVar *>::iterator var = vars.begin(),
         end = vars.end(); var != end; ++var) {
      // Update PMap for this var
      P[*var].insert(P[*var].end(), targets.begin(), targets.end());
    }

    // Initialize varset:
    if (initial) {
      if (subst(From).leq(LHConstant::low())) {
        // A <= B, 'B' is low
        // Mark all in 'A' as low also
        VSet.insert(targets.begin(), targets.end());
      }
    } else {
      if (!subst(From).leq(LHConstant::low())) {
        // A <= B, 'A' is high
        // Mark all in 'B' as high also
        VSet.insert(targets.begin(), targets.end());
      }
    }
  }

}

void PartialSolution::propagate() {
  std::deque<const ConsVar *> workList;

  assert(!Chained.empty());
  assert(std::find(Chained.begin(), Chained.end(), this) != Chained.end());

  // Enqueue all known changed variables
  for (std::vector<PartialSolution*>::iterator CI = Chained.begin(),
      CE = Chained.end(); CI != CE; ++CI) {
    workList.insert(workList.end(),
                    (*CI)->VSet.begin(), (*CI)->VSet.end());
  }

  // Compute transitive closure of the non-default variables,
  // using all propagation maps in PMaps.
  while (!workList.empty()) {
    // Dequeue variable
    const ConsVar * V = workList.front();
    workList.pop_front();

    for (std::vector<PartialSolution*>::iterator CI = Chained.begin(),
         CE = Chained.end(); CI != CE; ++CI) {
      PartialSolution &PS = **CI;

      PMap::iterator I = PS.P.find(V);
      if (I == PS.P.end()) continue; // Not in map

      std::vector<const ConsVar*> &Updates = I->second;
      // For each such variable...
      for (std::vector<const ConsVar*>::iterator I = Updates.begin(),
           E = Updates.end(); I != E; ++I) {
        // If we haven't changed it already, add it to the worklist:
        if (!isChanged(*I)) {
          VSet.insert(*I);
          workList.push_back(*I);
        }
      }
    }
  }
}

