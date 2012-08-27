//===- Infoflow.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a constraint-based interprocedural (2-call site sensitive)
// information flow analysis for a two-level security lattice (Untainted--Tainted).
// While the analysis is context-sensitive, the Infoflow pass interface is not.
//
//===----------------------------------------------------------------------===//

#ifndef INFOFLOW_H
#define INFOFLOW_H

#include "CallSensitiveAnalysisPass.h"
#include "CallContext.h"
#include "Constraints/LHConsSoln.h"
#include "Constraints/LHConstraintKit.h"
#include "FPCache.h"
#include "FlowRecord.h"
#include "InfoflowSignature.h"
#include "PointsToInterface.h"
#include "SourceSinkAnalysis.h"

#include "llvm/Instructions.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/ADT/DenseMap.h"

#include <deque>
#include <map>
#include <set>

namespace deps {

using namespace llvm;

/// Unit is the interprocedural analysis input type.
/// The analysis uses unit since each function should only
/// be analyzed once per context.
class Unit {
public:
  const Unit & operator=(const Unit & u) { return u; }
  bool operator<=(const Unit &) const { return true; }
  bool operator==(const Unit &) const { return true; }
  bool operator!=(const Unit &) const { return false; }
  const Unit upperBound(const Unit & u) const { return u; }
};

class PDTCache : public FPCache<PostDominatorTree> {
public:
  static char ID;
  PDTCache() : FPCache<PostDominatorTree>(ID) {}
  virtual const char * getPassName() const { return "PostDom Cache"; }
};

class Infoflow;

/// InfoflowSolution wraps a constraint set solution with
/// the information required to extract taint summaries for
/// values and locations.
class InfoflowSolution {
public:
  InfoflowSolution(Infoflow & infoflow, ConsSoln * s,
                   const ConsElem & high, bool defaultTainted,
                   DenseMap<const Value *, const ConsElem *> & valueMap,
                   DenseMap<const AbstractLoc *, const ConsElem *> & locMap,
                   DenseMap<const Function *, const ConsElem *> & vargMap) :
                   infoflow(infoflow), soln(s), highConstant(high),
                   defaultTainted(defaultTainted), valueMap(valueMap),
                   locMap(locMap), vargMap(vargMap) { }
  ~InfoflowSolution();

  /// isTainted - returns true if the security level of the value is High.
  bool isTainted(const Value &);
  /// isDirectPtrTainted - returns true if the security level of the memory
  /// pointed to is High.
  bool isDirectPtrTainted(const Value &);
  /// isReachPtrTainted - returns true if the security level of memory reachable
  /// from the pointer is High.
  bool isReachPtrTainted(const Value &);
  /// isVargTainted - returns true if the security level of the varargs of
  /// the function is High.
  bool isVargTainted(const Function &);
private:
  InfoflowSolution & operator=(const InfoflowSolution& rhs);

  const Infoflow & infoflow;
  ConsSoln * soln;
  const ConsElem & highConstant;
  bool defaultTainted;
  DenseMap<const Value *, const ConsElem *> & valueMap;
  DenseMap<const AbstractLoc *, const ConsElem *> & locMap;
  DenseMap<const Function *, const ConsElem *> & vargMap;
};

/// A constraint-based, context-sensitive, interprocedural information
/// flow analysis pass. Exposes information-flow constraints with a
/// Tainted/Untainted security lattice that can be used with constraint
/// kinds to implement arbitrary security lattices.
class Infoflow :
  public CallSensitiveAnalysisPass<Unit,Unit,1,CallerContext> {
  friend class InfoflowSolution;

  public:
    static char ID;
    Infoflow ();
  virtual ~Infoflow() { delete kit; delete signatureRegistrar; }
    const char *getPassName() const { return "Infoflow"; }
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      CallSensitiveAnalysisPass<Unit,Unit,1,CallerContext>::getAnalysisUsage(AU);
      AU.addRequired<SourceSinkAnalysis>();
      AU.addRequired<PDTCache>();
      AU.addRequired<PointsToInterface>();
      AU.setPreservesAll();
    }

    virtual void releaseMemory() {
      // Clear out all the maps
      valueConstraintMap.clear();
      vargConstraintMap.clear();
      summarySinkValueConstraintMap.clear();
      summarySourceValueConstraintMap.clear();
      summarySinkVargConstraintMap.clear();
      summarySourceVargConstraintMap.clear();

      // And free the kit and all its constraints
      delete kit;
      kit = NULL;
    }

    bool DropAtSinks() const;

    /// registerSignatures - Register an information flow signature
    /// to be used when calling external code.
    virtual void registerSignatures();

    //////////////////////////////////////////////////////////////
    /// Adding taint sources and taint constraints
    ///-----------------------------------------------------------
    /// The following methods allow the user to add constraints
    /// to the default set computed by the information flow
    /// analysis. The first argument specifies the constraint
    /// 'kind' the new constraint should be added to. When
    /// solving for an information flow solution, the user
    /// may specify a set of constraints to include.

    /// Adds the constraint "TAINTED <= VALUE" to the given kind
    void setUntainted(std::string, const Value &);
    /// Adds the constraint "VALUE <= UNTAINTED" to the given kind
    void setTainted(std::string, const Value &);
    /// Adds the constraint "TAINTED <= LOC" for all locations
    /// the value could point to to the given kind
    void setDirectPtrUntainted(std::string, const Value &);
    /// Adds the constraint "LOC <= UNTAINTED" for all locations
    /// the value could point to to the given kind
    void setDirectPtrTainted(std::string, const Value &);
    /// Adds the constraint "TAINTED <= LOC" for all locations
    /// reachable through the given pointer to the given kind
    void setReachPtrUntainted(std::string, const Value &);
    /// Adds the constraint "LOC <= UNTAINTED" for all locations
    /// reachable through the given pointer to the given kind
    void setReachPtrTainted(std::string, const Value &);
    /// Adds the constraint "TAINTED <= VARGS OF FUN" to the given kind
    void setVargUntainted(std::string, const Function &);
    /// Adds the constraint "VARGS OF FUN <= UNTAINTED" to the given kind
    void setVargTainted(std::string, const Function &);

    //////////////////////////////////////////////////////////////
    /// Solving sets of contraints
    ///-----------------------------------------------------------
    /// The following methods return solutions to a set of
    /// information flow constraints. Users can request the
    /// greatest or least fixed point of the constraint set.
    /// The list of kinds specifies additional constraints that
    /// should be satisfied in addition to the default constraints
    /// computed by the information flow analysis.

    /// Solve the default information flow constraints combined
    /// with any constraints from the given kinds, and if implicit
    /// is true, the implicit kind. Returns the least fixed point 
    /// of the constraint system: unconstrained values and locations
    /// are considered UNTAINTED.
    ///
    /// Once a solution is requested for a given kind, no further
    /// constriants may be added to that kind.
    InfoflowSolution *leastSolution(std::set<std::string> kinds, bool implicit, bool sinks);

    /// Solve the default information flow constraints combined
    /// with any constraints from the given kinds, and if implicit
    /// is true, the implicit kind. Returns the greatest fixed point 
    /// of the constraint system: unconstrained values and locations 
    /// are considered TAINTED.
    ///
    /// Once a solution is requested for a given kind, no further
    /// constriants may be added to that kind.
    InfoflowSolution *greatestSolution(std::set<std::string> kinds, bool implicit);


    typedef std::vector<FlowRecord> Flows;
    Flows getInstructionFlows(const Instruction &);

    // Solve the given kind using two threads.
    void solveMT(std::string kind="default") {
      assert(kit);
      kit->solveMT(kind);
    }
    std::vector<InfoflowSolution*> solveLeastMT(std::vector<std::string> kinds, bool useDefaultSinks);
  private:
    virtual void doInitialization();
    virtual void doFinalization();
    virtual const Unit bottomInput() const;
    virtual const Unit bottomOutput() const;
    virtual const Unit runOnContext(const AUnitType unit, const Unit input);

    LHConstraintKit *kit;

    PointsToInterface *pti;
    SourceSinkAnalysis *sourceSinkAnalysis;

    SignatureRegistrar *signatureRegistrar;

    FlowRecord currentContextFlowRecord(bool implicit) const;

    const std::set<const AbstractLoc *> &locsForValue(const Value & value) const;
    const std::set<const AbstractLoc *> &reachableLocsForValue(const Value & value) const;

    DenseMap<ContextID, DenseMap<const Value *, const ConsElem *> > valueConstraintMap;
    DenseMap<const AbstractLoc *, const ConsElem *> locConstraintMap;
    DenseMap<ContextID, DenseMap<const Function *, const ConsElem *> > vargConstraintMap;

    DenseMap<const Value *, const ConsElem *> summarySinkValueConstraintMap;
    DenseMap<const Value *, const ConsElem *> summarySourceValueConstraintMap;
    DenseMap<const Function *, const ConsElem *> summarySinkVargConstraintMap;
    DenseMap<const Function *, const ConsElem *> summarySourceVargConstraintMap;

    DenseMap<const Value *, const ConsElem *> &getOrCreateValueConstraintMap(const ContextID);
    DenseMap<const Function *, const ConsElem *> &getOrCreateVargConstraintMap(const ContextID);

    virtual const Unit signatureForExternalCall(const ImmutableCallSite & cs, const Unit input);

    void constrainFlowRecord(const FlowRecord &);

    const std::string kindFromImplicitSink(bool implicit, bool sink) const;

    const ConsElem &getOrCreateConsElem(const ContextID, const Value &);
    void putOrConstrainConsElem(bool imp, bool sink, const ContextID, const Value &, const ConsElem &);
    const ConsElem &getOrCreateConsElemSummarySource(const Value &);
    void putOrConstrainConsElemSummarySource(std::string, const Value &, const ConsElem &);
    const ConsElem &getOrCreateConsElemSummarySink(const Value &);
    void putOrConstrainConsElemSummarySink(std::string, const Value &, const ConsElem &);
    const ConsElem &getOrCreateConsElem(const Value &);
    const ConsElem &getOrCreateConsElem(const AbstractLoc &);
    void putOrConstrainConsElem(bool imp, bool sink, const Value &, const ConsElem &);
    void putOrConstrainConsElem(bool imp, bool sink, const AbstractLoc &, const ConsElem &);

    const ConsElem &getOrCreateVargConsElem(const ContextID, const Function &);
    void putOrConstrainVargConsElem(bool imp, bool sink, const ContextID, const Function &, const ConsElem &);
    const ConsElem &getOrCreateVargConsElemSummarySource(const Function &);
    void putOrConstrainVargConsElemSummarySource(std::string, const Function &, const ConsElem &);
    const ConsElem &getOrCreateVargConsElemSummarySink(const Function &);
    void putOrConstrainVargConsElemSummarySink(std::string, const Function &, const ConsElem &);
    const ConsElem &getOrCreateVargConsElem(const Function &);
    void putOrConstrainVargConsElem(bool imp, bool sink, const Function &, const ConsElem &);

    void generateFunctionConstraints(const Function &);
    void generateBasicBlockConstraints(const BasicBlock &, Flows &);
    void getInstructionFlowsInternal(const Instruction &, bool callees, Flows &);

    void operandsAndPCtoValue(const Instruction &, Flows &);

    void constrainMemoryLocation(bool imp, bool sink, const Value &, const ConsElem &);
    void constrainReachableMemoryLocations(bool imp, bool sink, const Value &, const ConsElem &);
    const ConsElem &getOrCreateMemoryConsElem(const Value &);
    const ConsElem &getOrCreateReachableMemoryConsElem(const Value &);

    void constrainConditionalSuccessors(const TerminatorInst &, FlowRecord & rec);

    void constrainAtomicCmpXchgInst(const AtomicCmpXchgInst &, Flows &);
    void constrainAtomicRMWInst(const AtomicRMWInst &, Flows &);
    void constrainBinaryOperator(const BinaryOperator &, Flows &);
    void constrainCallInst(const CallInst &, bool callees, Flows &);
    void constrainCmpInst(const CmpInst &, Flows &);
    void constrainExtractElementInst(const ExtractElementInst &, Flows &);
    void constrainFenceInst(const FenceInst &, Flows &);
    void constrainGetElementPtrInst(const GetElementPtrInst &, Flows &);
    void constrainInsertElementInst(const InsertElementInst &, Flows &);
    void constrainInsertValueInst(const InsertValueInst &, Flows &);
    void constrainLandingPadInst(const LandingPadInst &, Flows &);
    void constrainPHINode(const PHINode &, Flows &);
    void constrainSelectInst(const SelectInst &, Flows &);
    void constrainShuffleVectorInst(const ShuffleVectorInst &, Flows &);
    void constrainStoreInst(const StoreInst &, Flows &);
    void constrainTerminatorInst(const TerminatorInst &, bool callees, Flows &);
    void constrainUnaryInstruction(const UnaryInstruction &, Flows &);
    void constrainAllocaInst(const AllocaInst &, Flows &);
    void constrainCastInst(const CastInst &, Flows &);
    void constrainExtractValueInst(const ExtractValueInst &, Flows &);
    void constrainLoadInst(const LoadInst &, Flows &);
    void constrainVAArgInst(const VAArgInst &, Flows &);
    void constrainBranchInst(const BranchInst &, Flows &);
    void constrainIndirectBrInst(const IndirectBrInst &, Flows &);
    void constrainInvokeInst(const InvokeInst &, bool callees, Flows &);
    void constrainReturnInst(const ReturnInst &, Flows &);
    void constrainResumeInst(const ResumeInst &, Flows &);
    void constrainSwitchInst(const SwitchInst &, Flows &);
    void constraintUnreachableInst(const UnreachableInst &, Flows &);

    void constrainCallSite(const ImmutableCallSite & cs, bool callees, Flows &);
    void constrainCallee(const ContextID calleeContext, const Function & callee, const ImmutableCallSite & cs, Flows &);
    void constrainIntrinsic(const IntrinsicInst &, Flows &);
};

}

#endif /* INFOFLOW_H */
