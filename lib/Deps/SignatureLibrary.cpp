//===-- SignatureLibrary.cpp -----------------------------------*- C++ -*-===//
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

#include "SignatureLibrary.h"
#include "FlowRecord.h"

#include "llvm/Function.h"
#include "llvm/Instruction.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

namespace deps {

bool
TaintReachable::accept(const ContextID ctxt, const ImmutableCallSite cs) const { return true; }

std::vector<FlowRecord>
TaintReachable::process(const ContextID ctxt, const ImmutableCallSite cs) const {
  DEBUG(errs() << "Using taint reachable signature for: " << *cs.getInstruction() << "\n");

  FlowRecord exp(false,ctxt,ctxt);
  FlowRecord imp(true,ctxt,ctxt);

  // implicit from the pc of the call site and the function pointer
  imp.addSourceValue(*cs->getParent());
  imp.addSourceValue(*cs.getCalledValue());

  // Sources and sinks of the args
  for (ImmutableCallSite::arg_iterator arg = cs.arg_begin(), end = cs.arg_end();
       arg != end; ++arg) {
      // every argument's value is a source
      exp.addSourceValue(**arg);
      // if the argument is a pointer, everything it reaches is a source
      // and everything it reaches is a sink
      if ((*arg)->getType()->isPointerTy()) {
	exp.addSourceReachablePtr(**arg);
	imp.addSourceValue(**arg);

	exp.addSinkReachablePtr(**arg);
	imp.addSinkReachablePtr(**arg);
      }
  }

  // if the function has a return value it is a sink
  if (!cs->getType()->isVoidTy()) {
    imp.addSinkValue(*cs.getInstruction());
    exp.addSinkValue(*cs.getInstruction());
  }

  std::vector<FlowRecord> flows;
  flows.push_back(imp);
  flows.push_back(exp);
  return flows;
}

bool
ArgsToRet::accept(const ContextID ctxt, const ImmutableCallSite cs) const { return true; }

std::vector<FlowRecord>
ArgsToRet::process(const ContextID ctxt, const ImmutableCallSite cs) const {
  DEBUG(errs() << "Using ArgsToRet reachable signature for: " << *cs.getInstruction() << "\n");

  std::vector<FlowRecord> flows;

  if (!cs->getType()->isVoidTy()) {
    FlowRecord exp(false,ctxt,ctxt);

    // Sources and sinks of the args
    for (ImmutableCallSite::arg_iterator arg = cs.arg_begin(), end = cs.arg_end();
	 arg != end; ++arg) {
      // every argument's value is a source
      exp.addSourceValue(**arg);
    }

    // if the function has a return value it is a sink
    exp.addSinkValue(*cs.getInstruction());

    flows.push_back(exp);
  }

  return flows;
}


bool
NoFlows::accept(const ContextID ctxt, const ImmutableCallSite cs) const { return true; }

std::vector<FlowRecord>
NoFlows::process(const ContextID ctxt, const ImmutableCallSite cs) const {
  DEBUG(errs() << "Using no flows signature...\n");
  return std::vector<FlowRecord>();
}

bool
OverflowChecks::accept(const ContextID ctxt, const ImmutableCallSite cs) const {
  const Function * F = cs.getCalledFunction();
  return F && F->getName().startswith("____jf_check");
}

std::vector<FlowRecord>
OverflowChecks::process(const ContextID ctxt, const ImmutableCallSite cs) const {
  DEBUG(errs() << "Using OverflowChecks signature...\n");

  FlowRecord exp(false,ctxt,ctxt);
  FlowRecord imp(true,ctxt,ctxt);

  imp.addSourceValue(*cs->getParent());

  // Add all argument values as sources
  for (ImmutableCallSite::arg_iterator arg = cs.arg_begin(), end = cs.arg_end();
       arg != end; ++arg)
    exp.addSourceValue(**arg);
  assert(!cs->getType()->isVoidTy() && "Found 'void' overflow check?");

  // And the return value as a sink
  exp.addSinkValue(*cs.getInstruction());
  imp.addSinkValue(*cs.getInstruction());

  std::vector<FlowRecord> flows;
  flows.push_back(imp);
  flows.push_back(exp);
  return flows;
}

}
