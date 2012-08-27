//===-- CallSensitiveAnalysisPass.h -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the CallSensitiveAnalysisPass template, which extends the
// InterProcAnalysisPass template to implement a k-callsite sensitive
// interprocedural analysis.
//
//===----------------------------------------------------------------------===//

#ifndef CALLSENSITIVEANALYSISPASS_H_
#define CALLSENSITIVEANALYSISPASS_H_

#include "CallContext.h"
#include "InterProcAnalysisPass.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/IntrinsicInst.h"

#include "dsa/DSGraph.h"

namespace llvm {

/// CallSensitiveAnalysisPass can be extended to implement a k-callsite
/// sensitive interprocedural analysis. The parameters to the template
/// are an input and output type for the user's analysis and a non-negative
/// integer k.
///
/// The input and output types must implement the operators for
/// <=, ==, and !=, and be default constructible.
/// Furthermore, each should implement:
///  const T upperBound(const T & other) const
/// Where the output should be the join of the instances.
template<class I, class O, int K, class C>
class CallSensitiveAnalysisPass :
   public InterProcAnalysisPass<ContextID, I, O> {
public:
  typedef AnalysisUnit<ContextID> AUnitType;

  explicit CallSensitiveAnalysisPass(char &pid, bool collapseExtContext, bool collapseIndContext) :
    InterProcAnalysisPass<ContextID, I, O>(pid),
    collapseInd(collapseIndContext), collapseExt(collapseExtContext) { }


  /// bottomInput - This method should be implemented by the subclass as the
  /// initial input to use for each analysis unit. It should represent the
  /// "bottom" element of the p.o. of inputs
  virtual const I bottomInput() const = 0;
  /// bottomOutput - This method should be implemented by the subclass as the
  /// initial output to use for each analysis unit. It should represent the
  /// "bottom" element of the p.o. of summaries
  virtual const O bottomOutput() const = 0;

  /// runOnContext - This method should be implemented by the subclass to
  /// perform the desired analysis on the current analysis unit.
  virtual const O runOnContext(const AUnitType unit, const I input) = 0;
  /// signatureForExternalCall - This method should be implemented by the
  /// subclass to provide analysis results for calls that cannot be analysed
  /// directly.
  virtual const O signatureForExternalCall(const ImmutableCallSite & cs, const I input) = 0;

  bool functionIsCallable(const ImmutableCallSite &cs, const Function *F) {
    // If we don't have enough actual arguments, invalid callee
    if (cs.arg_size() < F->arg_size()) return false;
    // And for non-vararg we need exact argument match
    if (!F->isVarArg() && (cs.arg_size() != F->arg_size())) return false;

    // Otherwise use DSA's filtering
    return llvm::functionIsCallable(cs, F);
  }

  /// getCallResult - Analyzes all possible callees and returns a summary
  /// by joining their analysis results.
  virtual const O getCallResult(const ImmutableCallSite & cs,  const I input) {
    // if the call is an intrinsic, jump straight to using a signature
    if (isa<IntrinsicInst>(cs.getInstruction())) {
      return signatureForExternalCall(cs, input);
    }

    // Compute what context callee's should be analyzed in.
    ContextID newContext = updateContext(this->getCurrentContext(), cs);

    // Fast-path direct calls
    if (const Function *F = cs.getCalledFunction()) {
        if (!F->isDeclaration()) {
          return this->getAnalysisResult(AUnitType(newContext, *F), input);
        } else {
          return signatureForExternalCall(cs, input);
        }
    }

    // FIXME: Why does this need explicit instantiation?
    const CallGraph & cg = this->template getAnalysis<DataStructureCallGraph>();
    const CallGraphNode *caller = cg[cs.getCaller()];

    // Query the call graph for callees of the given call site.
    std::set<CallGraphNode *> callees;
    for (CallGraphNode::const_iterator rec = caller->begin(), end = caller->end();
            rec != end; ++rec) {
      if (rec->first == (const Value*)cs.getInstruction()) {
        callees.insert(rec->second);
      }
    }

    typedef std::set<CallGraphNode *>::const_iterator CGNIterator;

    O output = bottomOutput();
    bool addExternalCallingNodes = false;
    bool useExternalSignature = false;

    ContextID indirectContext =
      collapseInd ? updateIndirectContext(this->getCurrentContext(), cs) : newContext;
    ContextID externalContext =
      collapseExt ? updateIndirectContext(this->getCurrentContext(), cs) : newContext;

    // For each callee, determine whether it's a function we have code for,
    // an indirect call, or an external function (which we will need a
    // signature for).
    for (CGNIterator callee = callees.begin(), end = callees.end();
            callee != end; ++callee) {
      const Function *function = (*callee)->getFunction();
      if (function) {
        if (!functionIsCallable(cs, function)) continue;
        // The callee is a function in the module
        // If we have code, request analysis and add it to the output
        // otherwise use a signature
        if (!function->isDeclaration()) {
          AUnitType unit = AUnitType(indirectContext, *function);
          output = output.upperBound(this->getAnalysisResult(unit, input));
        } else {
          useExternalSignature = true;
        }
      } else {
        // The callee is either indirect or to an external function
        // TODO: Revisit handling of these calls
        if (*callee == cg.getCallsExternalNode()) {
          // Call to an external function: use a signature
          useExternalSignature = true;
        } else {
          // Indirect call, could call any function external calling node can call
          assert(*callee == cg.getExternalCallingNode());
          addExternalCallingNodes = true;
        }
      }
    }

    // If the call is indirect, we need to add the analysis result for any
    // function the call graph tells us it could invoke.
    if (addExternalCallingNodes) {

      for (CallGraphNode::iterator callee = cg.getExternalCallingNode()->begin(),
                                      end = cg.getExternalCallingNode()->end();
              callee != end; ++callee) {
          const Function *function = callee->second->getFunction();
          if (!functionIsCallable(cs, function)) continue;
          // If we have code, analyze it, otherwise use a signature
          if (!function->isDeclaration()) {
            AUnitType unit = AUnitType(externalContext, *function);
            output = output.upperBound(this->getAnalysisResult(unit, input));
          } else {
            useExternalSignature = true;
          }
      }
    }

    // If we call any declarations, use signature for this callsite
    // This only needs to be done once, since we don't specify callee.
    if (useExternalSignature)
      output = output.upperBound(signatureForExternalCall(cs, input));

    return output;
  }

  /// invokableCode - returns a set of function pointers to code that could
  /// be analyzed by calling getCallResult().
  /// XXX Hacky... should refactor so that we don't have to keep this code
  /// in sink with getCallResult...
  virtual std::set<std::pair<const Function *, const ContextID> > invokableCode(const ImmutableCallSite & cs) {

    ContextID newContext = updateContext(this->getCurrentContext(), cs);
    ContextID indirectContext =
      collapseInd ? updateIndirectContext(this->getCurrentContext(), cs) : newContext;
    ContextID externalContext =
      collapseExt ? updateIndirectContext(this->getCurrentContext(), cs) : newContext;

    // Fast-path direct calls
    if (const Function *F = cs.getCalledFunction()) {
      std::set<std::pair<const Function*, const ContextID> > single;
      if (!F->isDeclaration())
        single.insert(std::pair<const Function *, const ContextID>(F,newContext));
      return single;
    }

    std::set<std::pair<const Function *, const ContextID> > callees;

    // FIXME: Why does this need explicit instantiation?
    const CallGraph & cg = this->template getAnalysis<DataStructureCallGraph>();
    const CallGraphNode *caller = cg[cs.getCaller()];

    // Query the call graph for callees of the given call site.
    std::set<CallGraphNode *> calleeGraphNodes;
    for (CallGraphNode::const_iterator rec = caller->begin(), end = caller->end();
            rec != end; ++rec) {
      if (rec->first == (const Value*)cs.getInstruction()) {
        calleeGraphNodes.insert(rec->second);
      }
    }

    typedef std::set<CallGraphNode *>::const_iterator CGNIterator;
    bool addExternalCallingNodes = false;

    // For each callee, determine whether it's a function we have code for,
    // an indirect call, or an external function (which we will need a
    // signature for).
    for (CGNIterator callee = calleeGraphNodes.begin(), end = calleeGraphNodes.end();
            callee != end; ++callee) {
        const Function *function = (*callee)->getFunction();
        if (function) {
          if (!functionIsCallable(cs, function)) continue;
          // The callee is a function in the module
          // If we have code, it would be analyzed by getCallResult()
          // otherwise use a signature
          if (!function->isDeclaration()) {
            callees.insert(std::pair<const Function *, const ContextID>(function,indirectContext));
        } else {
          // The callee is either indirect or to an external function
          if (*callee == cg.getExternalCallingNode()) {
            // Indirect call, could call any function external calling node can call
            addExternalCallingNodes = true;
          }
        }
      }
    }

    // If the call is indirect, so we will add the analysis result for any
    // function the call graph tells us it could invoke (that we have code for)
    if (addExternalCallingNodes) {
      for (CallGraphNode::iterator callee = cg.getExternalCallingNode()->begin(),
                                      end = cg.getExternalCallingNode()->end();
              callee != end; ++callee) {
          const Function *function = callee->second->getFunction();
          if (!functionIsCallable(cs, function)) continue;
          if (!function->isDeclaration()) {
            callees.insert(std::pair<const Function *, const ContextID>(function,externalContext));
          }
      }
    }

    return callees;
  }

  virtual bool usesExternalSignature(const ImmutableCallSite &cs) {
    // XXX Hacked copy of getCallResult to figure out when we should...
    // if the call is an intrinsic, jump straight to using a signature
    if (isa<IntrinsicInst>(cs.getInstruction())) {
      return true;
    }
    
    // Fast-path direct calls
    if (const Function *F = cs.getCalledFunction()) {
      if (F->isDeclaration()) {
	return true;
      }
    } else {
      // FIXME: Why does this need explicit instantiation?
      const CallGraph & cg = this->template getAnalysis<DataStructureCallGraph>();
      const CallGraphNode *caller = cg[cs.getCaller()];
      
      // Query the call graph for callees of the given call site.
      std::set<CallGraphNode *> callees;
      for (CallGraphNode::const_iterator rec = caller->begin(), end = caller->end();
	   rec != end; ++rec) {
	if (rec->first == (const Value*)cs.getInstruction()) {
	  callees.insert(rec->second);
	}
      }
      
      typedef std::set<CallGraphNode *>::const_iterator CGNIterator;
      
      bool addExternalCallingNodes = false;
      
      // For each callee, determine whether it's a function we have code for,
      // an indirect call, or an external function (which we will need a
      // signature for).
      for (CGNIterator callee = callees.begin(), end = callees.end();
	   callee != end; ++callee) {
	const Function *function = (*callee)->getFunction();
	if (function) {
	  if (!functionIsCallable(cs, function)) continue;
	  // The callee is a function in the module
	  // If we have code, request analysis and add it to the output
	  // otherwise use a signature
	  if (function->isDeclaration()) {
	    return true;
	  }
	} else {
	  // The callee is either indirect or to an external function
	  // TODO: Revisit handling of these calls
	  if (*callee == cg.getCallsExternalNode()) {
	    // Call to an external function: use a signature
	    return true;
	  } else {
	    // Indirect call, could call any function external calling node can call
	    assert(*callee == cg.getExternalCallingNode());
	    addExternalCallingNodes = true;
	  }
	}
      }
      
      // If the call is indirect, we need to add the analysis result for any
      // function the call graph tells us it could invoke.
      if (addExternalCallingNodes) {
	for (CallGraphNode::iterator callee = cg.getExternalCallingNode()->begin(),
	       end = cg.getExternalCallingNode()->end();
	     callee != end; ++callee) {
	  const Function *function = callee->second->getFunction();
	  if (!functionIsCallable(cs, function)) continue;
	  // If we have code, analyze it, otherwise use a signature
	  if (function->isDeclaration()) {
	    return true;
	  }
	}
      }
    }
    return false;
  }


  /// The default initialContext is empty (the caller is the OS or an external
  /// process--there is no call hierarchy yet).
  ContextID initialContext(const Function &) {
    return DefaultID;
  }

  /// To update a CallSiteContext, add the new call site to the list of call sites.
  /// If there are more than K callsites in the list, drop the oldest.
  ContextID updateContext(const ContextID c, const ImmutableCallSite &cs) {
    C newContext = CM.getContextFor(c);
    newContext.push_back(cs);
    while (newContext.size() > K) {
      newContext.pop_front();
    }
    return CM.getIDFor(newContext);
  }

  ContextID updateIndirectContext(const ContextID c, const ImmutableCallSite &cs) {
    return DefaultID;
  }

  /// getAnalysisUsage - Derived methods must call this implementation.
  virtual void getAnalysisUsage(AnalysisUsage &Info) const {
    InterProcAnalysisPass<ContextID,I,O>::getAnalysisUsage(Info);
  }

protected:
  ContextManager<C> CM;
private:
  bool collapseInd;
  bool collapseExt;
};

}

#endif /* CALLSENSITIVEANALYSISPASS_H_ */
