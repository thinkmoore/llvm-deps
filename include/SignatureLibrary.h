//===-- SignatureLibrary.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a library of information flow signatures that can be
// registered with the Infoflow signature registrar.
//
//===----------------------------------------------------------------------===//

#ifndef SIGNATURELIBRARY_H_
#define SIGNATURELIBRARY_H_

#include "InfoflowSignature.h"

namespace deps {

using namespace llvm;

/// Description: A conservative signature that taints all reachable sinks with
///   all reachable sources (ignoring that callees may be memory-unsafe).
/// Accepts: all call sites.
/// Flows:
///   - Implicit flow from pc and function pointer at the call site to all sinks
///   - All arguments are sources
///   - The reachable objects from all pointer arguments are sources
///   - The reachable objects from all pointer arguments are sinks
///   - The return value (if applicable) is a sink
class TaintReachable : public Signature {
public:
  virtual bool accept(const ContextID ctxt, const ImmutableCallSite cs) const;
  virtual std::vector<FlowRecord> process(const ContextID ctxt, const ImmutableCallSite cs) const;
};

/// Description: A dummy signature that assumes no information flows happen
///   as a result of the call. (Not very useful.)
/// Accepts: all call sites.
/// Flows:
///   - None
class NoFlows : public Signature {
public:
  virtual bool accept(const ContextID ctxt, const ImmutableCallSite cs) const;
  virtual std::vector<FlowRecord> process(const ContextID ctxt, const ImmutableCallSite cs) const;
};

class ArgsToRet : public Signature {
public:
  virtual bool accept(const ContextID ctxt, const ImmutableCallSite cs) const;
  virtual std::vector<FlowRecord> process(const ContextID ctxt, const ImmutableCallSite cs) const;
};


// StdLib - Signature generation for StdLib calls
struct CallSummary;
class StdLib: public Signature {
  std::vector<const CallSummary*> Calls;
  void initCalls();
  bool findEntry(const ImmutableCallSite cs, const CallSummary *& S) const;
public:
  StdLib() : Signature() { initCalls(); }
  virtual bool accept(const ContextID ctxt, const ImmutableCallSite cs) const;
  virtual std::vector<FlowRecord> process(const ContextID ctxt, const ImmutableCallSite cs) const;
};

// OverflowChecks
// Description: Signatures for the "____jf_check" family of overflow checks
// Accepts: All callees starting with "____jf_check"
// Flows: From all arguments to return value, no direct/reachable pointers.
class OverflowChecks : public Signature {
public:
  virtual bool accept(const ContextID ctxt, const ImmutableCallSite cs) const;
  virtual std::vector<FlowRecord> process(const ContextID ctxt, const ImmutableCallSite cs) const;
};

}

#endif /* SIGNATURELIBRARY_H_ */
