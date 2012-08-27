//===- constraints/ConstraintKit.h - Constraint Solver Interface *- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares a simple abstract interface for an information flow
// constraint solver.
//
//===----------------------------------------------------------------------===//

#ifndef CONSTRAINTKIT_H_
#define CONSTRAINTKIT_H_

#include "Constraints/DepsTypes.h"
#include <set>
#include <string>

namespace deps {

class ConsVar;

/// Interface for elements that can appear in constraints.
class ConsElem {
public:
    /// Compare two elements for constraint satisfaction
    virtual bool leq(const ConsElem &elem) const = 0;
    virtual void variables(std::set<const ConsVar *> &) const = 0;
    virtual bool operator== (const ConsElem &elem) const = 0;

    /// Support for llvm-style RTTI (isa<>, dyn_cast<>, etc.)
    virtual DepsType type() const = 0;

    virtual ~ConsElem() {}
};

/// Interface distinguishing constraint variables
class ConsVar : public ConsElem { };

/// Interface for querying the results of solving a constraint set
class ConsSoln {
public:
    /// Substitute the given constraint element under the solution's
    /// environment (e.g., get the assignment of a variable)
    virtual const ConsElem &subst(const ConsElem&) = 0;

    virtual ~ConsSoln() {}
};

/// Interface for creating and solving constraints problems.
class ConstraintKit {
public:
    /// Create a new constraint variable
    virtual const ConsVar &newVar(const std::string) = 0;
    /// Create a new constraint element by taking the upper bound of two
    /// existing elements
    virtual const ConsElem &upperBound(const ConsElem&, const ConsElem&) = 0;
    /// Create a new constraint element by taking the upper bound of two
    /// existing elements. Arguments and return type may be null.
    virtual const ConsElem *upperBound(const ConsElem *e1, const ConsElem *e2) = 0;
    /// Constrain the left hand side with the right hand side and put it
    /// in the set "kind"
    virtual void addConstraint(const std::string kind, const ConsElem &lhs, const ConsElem &rhs) = 0;
    /// Find the lfp of the constraints in the "kinds" sets (caller must delete)
    virtual ConsSoln *leastSolution(const std::set<std::string> kinds) = 0;
    /// Find the gfp of the constraints in the "kinds" sets (caller must delete)
    virtual ConsSoln *greatestSolution(const std::set<std::string> kinds) = 0;

    virtual ~ConstraintKit() {}
};

}

#endif /* CONSTRAINTKIT_H_ */
