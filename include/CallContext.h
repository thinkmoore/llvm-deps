//===-- CallContext.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines ContextManager for uniqueing and generating opaque ContextID's,
// as well as myriad of Context types to use with CallSensitiveAnalysisPass.
//
//===----------------------------------------------------------------------===//

#ifndef CALLCONTEXT_H
#define CALLCONTEXT_H

#include "llvm/Support/CallSite.h"

#include <deque>
#include <set>
#include <stdint.h>

namespace llvm {

// ContextManage -
// Provide lightweight canonical reference's to Context objects.
// Goal is to abstract away which Context type is being used,
// as well as make them cheap to copy spuriously.
// Similar to LLVM's FoldingSet.
typedef uintptr_t ContextID;
// DefaultID - Special-case (what ContextID zero-init's to)
// that is used to respresent an empty context.
static const ContextID DefaultID = 0;
template<class C>
class ContextManager {
public:

  // getIDFor - Return a canonical ContextID for the given Context
  ContextID getIDFor(C & c) {
    // Try to find a match in our set of contexts...
    typename CSet::iterator I = Contexts.find((ContextID)&c);
    // If we don't already have on like this, add a copy
    if (I == Contexts.end()) {
      I = Contexts.insert((ContextID)new C(c)).first;
    }
    // Return the reference to the canonical object
    return *I;
  }

  // getContextFor - Return the Context object corresponding to the given ID
  const C & getContextFor(ContextID ID) const {
    // Special-case for 'default' ContextIDs
    if (ID == DefaultID) return initial;

    // Otherwise just return what the ID points to
    assert(Contexts.count(ID));
    return *(C*)ID;
  }

  // Free all allocated context objects
  void clear() {
    for(typename CSet::iterator I = Contexts.begin(), E = Contexts.end();
        I != E; ++I)
      delete (C*)*I;
    Contexts.clear();
  }

  // Destructor
  ~ContextManager() {
    clear();
  }

private:
  // Comparator type, compares the ID's by comparing their dereferenced values
  struct CompareID {
    bool operator()(const ContextID &A, const ContextID &B) const {
      return *(C*)A < *(C*)B;
    }
  };

  typedef std::set<ContextID,CompareID> CSet;
  CSet Contexts;
  C initial;
};

// Context types:

class CallerContext {
  /// CallerContext is a thin wrapper around stl's deque with
  /// a custom < operator to support using CallerContext's
  /// as keys in a map.
  typedef std::deque<const Function*> callers_type;
public:
  /// CallerContext's are ordered by lexicographical comparison
  /// over the deque of functions.
  bool operator<(const CallerContext & that) const {
    return std::lexicographical_compare(callers.begin(), callers.end(),
                                        that.callers.begin(),
                                        that.callers.end());
  }

  void push_back(const ImmutableCallSite &cs) {
    callers.push_back(cs.getInstruction()->getParent()->getParent());
  }
  size_t size() const { return callers.size(); }
  void pop_front() { callers.pop_front(); }
  void dump() const;

private:
  callers_type callers;
};

class CallSiteContext {
  /// CallSiteContext is a thin wrapper around stl's deque with
  /// a custom < operator to support using CallSiteContext's
  /// as keys in a map.
  typedef std::deque<ImmutableCallSite> sites_type;
public:
  /// CallSiteContext's are ordered by lexicographical comparison
  /// over the deque of callsites, using the instruction
  /// address for CallSite comparison.
  static bool compareCS(const ImmutableCallSite & LHS,
                        const ImmutableCallSite & RHS) {
    return LHS.getInstruction() < RHS.getInstruction();
  }
  bool operator<(const CallSiteContext & that) const {
    return std::lexicographical_compare(sites.begin(), sites.end(),
                                        that.sites.begin(),
                                        that.sites.end(),compareCS);
  }

  void push_back(const ImmutableCallSite &cs) { sites.push_back(cs); }
  size_t size() const { return sites.size(); }
  void pop_front() { sites.pop_front(); }
  void dump() const;

private:
  sites_type sites;
};

}

#endif // CALLCONTEXT_H
