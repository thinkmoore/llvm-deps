//===-- CallContext.cpp ---------------------------------------------------===//
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

#include "CallContext.h"

#include "llvm/Function.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

void CallerContext::dump() const {
  for (unsigned i = 0; i < callers.size(); ++i)
    errs() << callers[i]->getName() << " ";
}

void CallSiteContext::dump() const {
  for (unsigned i = 0; i < sites.size(); ++i)
    errs() << sites[i].getCaller()->getName() << " ";
}
