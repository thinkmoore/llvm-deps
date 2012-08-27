//===-- LHConstraints.cpp ---------------------------------------*- C++ -*-===//
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

#include "Constraints/LHConstraints.h"
#include "llvm/Support/Casting.h"

namespace deps {

LHConstant *LHConstant::lowSingleton = NULL;
LHConstant *LHConstant::highSingleton = NULL;

LHConstant::LHConstant(LHLevel level) : level(level) { }

const LHConstant &LHConstant::low() {
    if (LHConstant::lowSingleton == NULL) {
        LHConstant::lowSingleton = new LHConstant(LOW);
    }
    return *LHConstant::lowSingleton;
}
const LHConstant &LHConstant::high() {
    if (LHConstant::highSingleton == NULL) {
        LHConstant::highSingleton = new LHConstant(HIGH);
    }
    return *LHConstant::highSingleton;
}

bool LHConstant::leq(const ConsElem &elem) const {
    const LHConstant *other;
    // TODO not sure if dyn_cast support is set up properly
    if ((other = llvm::dyn_cast<LHConstant>(&elem))) {
        return (this->level == LOW) || (other->level == HIGH);
    } else {
        return false;
    }
}

const LHConstant &LHConstant::join(const LHConstant & other) const {
    return (level == LOW) ? other : *this;
}

bool LHConstant::operator== (const ConsElem& elem) const {
    if (const LHConstant *other = llvm::dyn_cast<const LHConstant>(&elem)) {
        return (this->level == other->level);
    } else {
        return false;
    }
}


LHConsVar::LHConsVar(const std::string description) : desc(description) { }

bool LHConsVar::leq(const ConsElem &elem) const {
    return false;
}

bool LHConsVar::operator== (const ConsElem& elem) const {
    if (const LHConsVar *other = llvm::dyn_cast<const LHConsVar>(&elem)) {
        return this == other;
    } else {
        return false;
    }
}

LHJoin::LHJoin(std::set<const ConsElem *> elements) : elems(elements) { }

bool LHJoin::leq(const ConsElem &other) const {
    for (std::set<const ConsElem *>::iterator elem = elems.begin(), end = elems.end(); elem != end; ++elem) {
        if (!(*elem)->leq(other)) {
            return false;
        }
    }
    return true;
}

void LHJoin::variables(std::set<const ConsVar*> & vars) const {
    for (std::set<const ConsElem *>::iterator elem = elems.begin(), end = elems.end(); elem != end; ++elem) {
        (*elem)->variables(vars);
    }
}

const LHJoin *LHJoin::create(const ConsElem &e1, const ConsElem &e2) {
    std::set<const ConsElem *> elements;

    if (const LHJoin *j1 = llvm::dyn_cast<LHJoin>(&e1)) {
        const std::set<const ConsElem *> & j1elements = j1->elements();
        elements.insert(j1elements.begin(), j1elements.end());
    } else {
        elements.insert(&e1);
    }

    if (const LHJoin *j2 = llvm::dyn_cast<LHJoin>(&e2)) {
        const std::set<const ConsElem *> & j2elements = j2->elements();
        elements.insert(j2elements.begin(), j2elements.end());
    } else {
        elements.insert(&e2);
    }

    return new LHJoin(elements);
}

bool LHJoin::operator== (const ConsElem& elem) const {
    if (const LHJoin *other = llvm::dyn_cast<const LHJoin>(&elem)) {
        return this == other;
    } else {
        return false;
    }
}

}
