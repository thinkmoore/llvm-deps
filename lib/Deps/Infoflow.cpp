//===- Infoflow.cpp ---------------------------------------------*- C++ -*-===//
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

#include "Infoflow.h"
#include "SignatureLibrary.h"

#include "llvm/Module.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/CommandLine.h"

namespace deps {

using namespace llvm;

static cl::opt<bool> DepsCollapseExtContext(
  "deps-collapse-external", cl::desc("Use the default context for all ExternalCallingNode calls"),
  cl::init(true));
static cl::opt<bool> DepsCollapseIndContext(
  "deps-collapse-indirect", cl::desc("Use the default context for all indirect calls"),
  cl::init(true));
static cl::opt<bool> DepsDropAtSink(
  "deps-drop-sink-flows", cl::desc("Cut dependencies from sinks to other values"),
  cl::init(false));

typedef Infoflow::Flows Flows;

char Infoflow::ID = 42;
char PDTCache::ID = 0;

static RegisterPass<Infoflow>
X ("infoflow", "Compute information flow constraints", true, true);

static RegisterPass<PDTCache>
Y ("pdtcache", "Cache PostDom Analysis Results", true, true);
  
Infoflow::Infoflow () : 
    CallSensitiveAnalysisPass<Unit,Unit,1,CallerContext>(ID, DepsCollapseExtContext, DepsCollapseIndContext),
    kit(new LHConstraintKit()) { }

void
Infoflow::doInitialization() {
  // Get the PointsToInterface
  pti = &getAnalysis<PointsToInterface>();
  sourceSinkAnalysis = &getAnalysis<SourceSinkAnalysis>();

  signatureRegistrar = new SignatureRegistrar();
  registerSignatures();
}

void
Infoflow::doFinalization() {
  //  delete signatureRegistrar;
  // now deleted in destructor, because we need the registrar
  // for computing propagatesTaint
}

void Infoflow::registerSignatures() {
  RegisterSignature<OverflowChecks> OverflowChecks(*signatureRegistrar);
  RegisterSignature<StdLib>         StdLib(*signatureRegistrar);
  // For now, if we don't know the call don't bother with this.
  // It's expensive for the crazy amount of external calls to various
  // libraries that one encounters, and we don't have time to fix that.
  //RegisterSignature<TaintReachable> TaintReachable(*signatureRegistrar);
  //RegisterSignature<NoFlows> NoFlows(*signatureRegistrar);
  RegisterSignature<ArgsToRet> ArgsToRet(*signatureRegistrar);
}

const Unit
Infoflow::bottomInput() const {
  return Unit();
}

const Unit
Infoflow::bottomOutput() const {
  return Unit();
}

const Unit
Infoflow::runOnContext(const Infoflow::AUnitType unit, const Unit input) {
  DEBUG(errs() << "Running on " << unit.function().getName() << " in context [";
  CM.getContextFor(unit.context()).dump();
  errs() << "]\n");
  generateFunctionConstraints(unit.function());
  return Unit();
}

void
Infoflow::constrainFlowRecord(const FlowRecord &record) {
  const ConsElem *sourceElem = NULL;
  const ConsElem *sinkSourceElem = NULL;
  
  // First, build up the set of ConsElem's that represent the sources:
  std::set<const ConsElem*> Sources;
  std::set<const ConsElem*> sinkSources;
  {
    // For variables and vargs elements, add all of these directly to 'Sources'
    for (FlowRecord::value_iterator source = record.source_value_begin(), end = record.source_value_end();
        source != end; ++source) {
      if (!DepsDropAtSink || !sourceSinkAnalysis->valueIsSink(**source)) {
	Sources.insert(&getOrCreateConsElem(record.sourceContext(), **source));
      } else {
	sinkSources.insert(&getOrCreateConsElem(record.sourceContext(), **source));
      }
    }
    for (FlowRecord::fun_iterator source = record.source_varg_begin(), end = record.source_varg_end();
        source != end; ++source) {
      if (!DepsDropAtSink || !sourceSinkAnalysis->vargIsSink(**source)) {
	Sources.insert(&getOrCreateVargConsElem(record.sourceContext(), **source));
      } else {
	sinkSources.insert(&getOrCreateVargConsElem(record.sourceContext(), **source));
      }
    }

    // For memory-based sources, build up the set of memory locations that act
    // as sources for this record...
    std::set<const AbstractLoc*> SourceLocs;
    std::set<const AbstractLoc*> sinkSourceLocs;
    for (FlowRecord::value_iterator source = record.source_directptr_begin(), end = record.source_directptr_end();
        source != end; ++source) {
      const std::set<const AbstractLoc *> & locs = locsForValue(**source);
      if (!DepsDropAtSink || !sourceSinkAnalysis->directPtrIsSink(**source)) {
	SourceLocs.insert(locs.begin(), locs.end());
      } else {
	sinkSourceLocs.insert(locs.begin(), locs.end());
      }
    }
    for (FlowRecord::value_iterator source = record.source_reachptr_begin(), end = record.source_reachptr_end();
        source != end; ++source) {
      const std::set<const AbstractLoc *> & locs = reachableLocsForValue(**source);
      if (!DepsDropAtSink || !sourceSinkAnalysis->reachPtrIsSink(**source)) {
	SourceLocs.insert(locs.begin(), locs.end());
      } else {
	sinkSourceLocs.insert(locs.begin(), locs.end());
      }
    }

    // ...And convert those locs into ConsElem's and store them into Sources
    for(std::set<const AbstractLoc*>::const_iterator I = SourceLocs.begin(),
        E = SourceLocs.end(); I != E; ++I) {
      Sources.insert(&getOrCreateConsElem(**I));
    }
    for(std::set<const AbstractLoc*>::const_iterator I = sinkSourceLocs.begin(),
        E = sinkSourceLocs.end(); I != E; ++I) {
      sinkSources.insert(&getOrCreateConsElem(**I));
    }
  }

  bool regFlow = !Sources.empty();
  bool sinkFlow = !sinkSources.empty();

  // This assert *should* be true expect that we're getting
  // DirectPtr sources that don't have any corresponding abstract locations
  //assert((DepsDropAtSink || regFlow) && "FlowRecord must have at least one source!");

  // Take join of all sources, this is sourceElem
  if (regFlow)
    sourceElem = &kit->upperBound(Sources);
  if (sinkFlow)
    sinkSourceElem = &kit->upperBound(sinkSources);

  // Now we want to add constraints that *each* sink is greater
  // than the join of all the sources, computed above as sourceElem.
  bool implicit = record.isImplicit();

  // For values and varargs, just do this directly
  for (FlowRecord::value_iterator sink = record.sink_value_begin(), end = record.sink_value_end();
      sink != end; ++sink) {
    if (regFlow)
      putOrConstrainConsElem(implicit, false, record.sinkContext(), **sink, *sourceElem);
    if (sinkFlow)
      putOrConstrainConsElem(implicit, true, record.sinkContext(), **sink, *sinkSourceElem);
  }
  for (FlowRecord::fun_iterator sink = record.sink_varg_begin(), end = record.sink_varg_end();
      sink != end; ++sink) {
    if (regFlow)
      putOrConstrainVargConsElem(implicit, false, record.sinkContext(), **sink, *sourceElem);
    if (sinkFlow)
      putOrConstrainVargConsElem(implicit, true, record.sinkContext(), **sink, *sinkSourceElem);
  }

  // To try to save constraint generation, gather memory locations as before:
  std::set<const AbstractLoc*> SinkLocs;
  for (FlowRecord::value_iterator sink = record.sink_directptr_begin(), end = record.sink_directptr_end();
      sink != end; ++sink) {
    const std::set<const AbstractLoc *> & locs = locsForValue(**sink);
    SinkLocs.insert(locs.begin(), locs.end());
  }
  for (FlowRecord::value_iterator sink = record.sink_reachptr_begin(), end = record.sink_reachptr_end();
      sink != end; ++sink) {
    const std::set<const AbstractLoc *> & locs = reachableLocsForValue(**sink);
    SinkLocs.insert(locs.begin(), locs.end());
  }

  // And add constraints for each of the sink memory locations
  for (std::set<const AbstractLoc *>::iterator loc = SinkLocs.begin(), end = SinkLocs.end();
      loc != end ; ++loc) {
    if (regFlow)
      putOrConstrainConsElem(implicit, false, **loc, *sourceElem);
    if (sinkFlow)
      putOrConstrainConsElem(implicit, true, **loc, *sinkSourceElem);
  }
}

const Unit
Infoflow::signatureForExternalCall(const ImmutableCallSite & cs, const Unit input) {
  std::vector<FlowRecord> flowRecords = signatureRegistrar->process(this->getCurrentContext(), cs);

  // For each flow record returned by the signature, update the constraint sets
  for (std::vector<FlowRecord>::iterator rec = flowRecords.begin(), rend = flowRecords.end();
          rec != rend; ++rec) {
    constrainFlowRecord(*rec);
  }

  return bottomOutput();
}

///////////////////////////////////////////////////////////////////////////////
/// InfoflowSolution
///////////////////////////////////////////////////////////////////////////////

InfoflowSolution::~InfoflowSolution() {
  if (soln != NULL) delete soln;
}

bool
InfoflowSolution::isTainted(const Value & value) {
  DenseMap<const Value *, const ConsElem *>::iterator entry = valueMap.find(&value);
  if (entry != valueMap.end()) {
    const ConsElem & elem = *(entry->second);
    return (soln->subst(elem) == highConstant);
  } else {
    DEBUG(errs() << "not in solution: " << value << "\n");
    return defaultTainted;
  }
}

bool
InfoflowSolution::isDirectPtrTainted(const Value & value) {
  const std::set<const AbstractLoc *> & locs = infoflow.locsForValue(value);
  for (std::set<const AbstractLoc *>::const_iterator loc = locs.begin(), end = locs.end();
        loc != end; ++loc) {
    DenseMap<const AbstractLoc *, const ConsElem *>::iterator entry = locMap.find(*loc);
    if (entry != locMap.end()) {
      const ConsElem & elem = *(entry->second);
      if (soln->subst(elem) == highConstant) {
        return true;
      }
    } else {
      assert(false && "abstract location not in solution!");
      return defaultTainted;
    }
  }
  return false;
}

bool
InfoflowSolution::isReachPtrTainted(const Value & value) {
  const std::set<const AbstractLoc *> & locs = infoflow.reachableLocsForValue(value);
  for (std::set<const AbstractLoc *>::iterator loc = locs.begin(), end = locs.end();
        loc != end; ++loc) {
    DenseMap<const AbstractLoc *, const ConsElem *>::iterator entry = locMap.find(*loc);
    if (entry != locMap.end()) {
      const ConsElem & elem = *(entry->second);
      if (soln->subst(elem) == highConstant) {
        return true;
      }
    } else {
      assert(false && "abstract location not in solution!");
      return defaultTainted;
    }
  }
  return false;
}

bool
InfoflowSolution::isVargTainted(const Function & fun) {
  DenseMap<const Function *, const ConsElem *>::iterator entry = vargMap.find(&fun);
  if (entry != vargMap.end()) {
    const ConsElem & elem = *(entry->second);
    return (soln->subst(elem) == highConstant);
  } else {
    DEBUG(errs() << "not in solution: varargs of " << fun.getName() << "\n");
    return defaultTainted;
  }
}

///////////////////////////////////////////////////////////////////////////////
/// Infoflow
///////////////////////////////////////////////////////////////////////////////
bool
Infoflow::DropAtSinks() const  { return DepsDropAtSink; }

void
Infoflow::setUntainted(std::string kind, const Value & value) {
  assert(kind != "default" && "Cannot add constraints to the default kind");
  assert(kind != "implicit" && "Cannot add constraints to the implicit kind");
  const ConsElem & current = getOrCreateConsElemSummarySink(value);
  kit->addConstraint(kind, current, kit->lowConstant());
}

void
Infoflow::setTainted(std::string kind, const Value & value) {
  assert(kind != "default" && "Cannot add constraints to the default kind");
  assert(kind != "implicit" && "Cannot add constraints to the implicit kind");
  putOrConstrainConsElemSummarySource(kind, value, kit->highConstant());
}

void
Infoflow::setVargUntainted(std::string kind, const Function & fun) {
  assert(kind != "default" && "Cannot add constraints to the default kind");
  assert(kind != "implicit" && "Cannot add constraints to the implicit kind");
  const ConsElem & current = getOrCreateVargConsElemSummarySink(fun);
  kit->addConstraint(kind, current, kit->lowConstant());
}

void
Infoflow::setVargTainted(std::string kind, const Function & fun) {
  assert(kind != "default" && "Cannot add constraints to the default kind");
  assert(kind != "implicit" && "Cannot add constraints to the implicit kind");
  putOrConstrainVargConsElemSummarySource(kind, fun, kit->highConstant());
}

void
Infoflow::setDirectPtrUntainted(std::string kind, const Value & value) {
  assert(kind != "default" && "Cannot add constraints to the default kind");
  assert(kind != "implicit" && "Cannot add constraints to the implicit kind");
  const std::set<const AbstractLoc *> & locs = locsForValue(value);
  for (std::set<const AbstractLoc *>::iterator loc = locs.begin(), end = locs.end();
        loc != end; ++loc) {
    const ConsElem & current = getOrCreateConsElem(**loc);
    kit->addConstraint(kind, current, kit->lowConstant());
  }
}

void
Infoflow::setDirectPtrTainted(std::string kind, const Value & value) {
  assert(kind != "default" && "Cannot add constraints to the default kind");
  assert(kind != "implicit" && "Cannot add constraints to the implicit kind");
  const std::set<const AbstractLoc *> & locs = locsForValue(value);
  for (std::set<const AbstractLoc *>::iterator loc = locs.begin(), end = locs.end();
        loc != end; ++loc) {
    const ConsElem & current = getOrCreateConsElem(**loc);
    kit->addConstraint(kind, kit->highConstant(), current);
  }
}

void
Infoflow::setReachPtrUntainted(std::string kind, const Value & value) {
  assert(kind != "default" && "Cannot add constraints to the default kind");
  assert(kind != "implicit" && "Cannot add constraints to the implicit kind");
  const std::set<const AbstractLoc *> & locs = reachableLocsForValue(value);
  for (std::set<const AbstractLoc *>::iterator loc = locs.begin(), end = locs.end();
        loc != end; ++loc) {
    const ConsElem & current = getOrCreateConsElem(**loc);
    kit->addConstraint(kind, current, kit->lowConstant());
  }
}

void
Infoflow::setReachPtrTainted(std::string kind, const Value & value) {
  assert(kind != "default" && "Cannot add constraints to the default kind");
  assert(kind != "implicit" && "Cannot add constraints to the implicit kind");
  const std::set<const AbstractLoc *> & locs = reachableLocsForValue(value);
  for (std::set<const AbstractLoc *>::iterator loc = locs.begin(), end = locs.end();
        loc != end; ++loc) {
    const ConsElem & current = getOrCreateConsElem(**loc);
    kit->addConstraint(kind, kit->highConstant(), current);
  }
}

InfoflowSolution *
Infoflow::leastSolution(std::set<std::string> kinds, bool implicit, bool sinks) {
  kinds.insert("default");
  if (sinks) kinds.insert("default-sinks");
  if (implicit) kinds.insert("implicit");
  if (implicit && sinks) kinds.insert("implicit-sinks");
  return new InfoflowSolution(*this,
                              kit->leastSolution(kinds),
                              kit->highConstant(),
                              false, /* default to untainted */
                              summarySinkValueConstraintMap,
                              locConstraintMap,
                              summarySinkVargConstraintMap);
}

InfoflowSolution *
Infoflow::greatestSolution(std::set<std::string> kinds, bool implicit) {
  kinds.insert("default");
  kinds.insert("default-sinks");
  if (implicit) {
    kinds.insert("implicit");
    kinds.insert("implicit-sinks");
  }
  return new InfoflowSolution(*this,
                              kit->greatestSolution(kinds),
                              kit->highConstant(),
                              true, /* default to tainted */
                              summarySourceValueConstraintMap,
                              locConstraintMap,
                              summarySourceVargConstraintMap);
}

const std::set<const AbstractLoc *> &
Infoflow::locsForValue(const Value & value) const {
  return *pti->getAbstractLocSetForValue(&value);
}

const std::set<const AbstractLoc *> &
Infoflow::reachableLocsForValue(const Value & value) const {
  return *pti->getReachableAbstractLocSetForValue(&value);
}

const std::string
Infoflow::kindFromImplicitSink(bool implicit, bool sink) const {
  if (implicit) {
    if (sink) {
      return "implicit-sinks";
    } else {
      return "implicit";
    }
  } else {
    if (sink) {
      return "default-sinks";
    } else {
      return "default";
    }
  }
}

DenseMap<const Value *, const ConsElem *> &
Infoflow::getOrCreateValueConstraintMap(const ContextID context) {
  return valueConstraintMap[context];
}

const ConsElem &
Infoflow::getOrCreateConsElemSummarySource(const Value &value) {
  DenseMap<const Value *, const ConsElem *>::iterator curElem = summarySourceValueConstraintMap.find(&value);
  if (curElem == summarySourceValueConstraintMap.end()) {
      //errs() << "Created a constraint variable...\n";
      const ConsElem & elem = kit->newVar(value.getName());
      summarySourceValueConstraintMap.insert(std::make_pair(&value, &elem));
      return elem;
  } else {
      return *(curElem->second);
  }
}

void
Infoflow::putOrConstrainConsElemSummarySource(std::string kind, const Value &value, const ConsElem &lub) {
  //errs() << "Adding a constraint...\n";
  const ConsElem & current = getOrCreateConsElemSummarySource(value);
  kit->addConstraint(kind, lub, current);
}

const ConsElem &
Infoflow::getOrCreateConsElemSummarySink(const Value &value) {
  DenseMap<const Value *, const ConsElem *>::iterator curElem = summarySinkValueConstraintMap.find(&value);
  if (curElem == summarySinkValueConstraintMap.end()) {
      //errs() << "Created a constraint variable...\n";
      const ConsElem & elem = kit->newVar(value.getName());
      summarySinkValueConstraintMap.insert(std::make_pair(&value, &elem));
      return elem;
  } else {
      return *(curElem->second);
  }
}

void
Infoflow::putOrConstrainConsElemSummarySink(std::string kind, const Value &value, const ConsElem &lub) {
  //errs() << "Adding a constraint...\n";
  const ConsElem & current = getOrCreateConsElemSummarySink(value);
  kit->addConstraint(kind, lub, current);
}

const ConsElem &
Infoflow::getOrCreateConsElem(const ContextID ctxt, const Value &value) {
  DenseMap<const Value *, const ConsElem *> & valueMap = getOrCreateValueConstraintMap(ctxt);
  DenseMap<const Value *, const ConsElem *>::iterator curElem = valueMap.find(&value);
  if (curElem == valueMap.end()) {
      const ConsElem & elem = kit->newVar(value.getName());
      valueMap.insert(std::make_pair(&value, &elem));

      // Hook up the summaries for non-context sensitive interface
      const ConsElem & summarySource = getOrCreateConsElemSummarySource(value);
      kit->addConstraint("default",summarySource,elem);
      putOrConstrainConsElemSummarySink("default", value, elem);
      
      return elem;
  } else {
      return *(curElem->second);
  }
}

void
Infoflow::putOrConstrainConsElem(bool implicit, bool sink, const ContextID ctxt, const Value &value, const ConsElem &lub) {
  const ConsElem & current = getOrCreateConsElem(ctxt, value);
  kit->addConstraint(kindFromImplicitSink(implicit,sink), lub, current);
}

const ConsElem &
Infoflow::getOrCreateConsElem(const Value &value) {
  return getOrCreateConsElem(this->getCurrentContext(), value);
}

void
Infoflow::putOrConstrainConsElem(bool implicit, bool sink, const Value &value, const ConsElem &lub) {
  return putOrConstrainConsElem(implicit, sink, this->getCurrentContext(), value, lub);
}

DenseMap<const Function *, const ConsElem *> &
Infoflow::getOrCreateVargConstraintMap(const ContextID context) {
  return vargConstraintMap[context];
}

const ConsElem &
Infoflow::getOrCreateVargConsElemSummarySource(const Function &value) {
  DenseMap<const Function *, const ConsElem *>::iterator curElem = summarySourceVargConstraintMap.find(&value);
  if (curElem == summarySourceVargConstraintMap.end()) {
      //errs() << "Created a constraint variable...\n";
      const ConsElem & elem = kit->newVar(value.getName());
      summarySourceVargConstraintMap.insert(std::make_pair(&value, &elem));
      return elem;
  } else {
      return *(curElem->second);
  }
}

void
Infoflow::putOrConstrainVargConsElemSummarySource(std::string kind, const Function &value, const ConsElem &lub) {
  //errs() << "Adding a constraint...\n";
  const ConsElem & current = getOrCreateVargConsElemSummarySource(value);
  kit->addConstraint(kind, lub, current);
}

const ConsElem &
Infoflow::getOrCreateVargConsElemSummarySink(const Function &value) {
  DenseMap<const Function *, const ConsElem *>::iterator curElem = summarySinkVargConstraintMap.find(&value);
  if (curElem == summarySinkVargConstraintMap.end()) {
      //errs() << "Created a constraint variable...\n";
      const ConsElem & elem = kit->newVar(value.getName());
      summarySinkVargConstraintMap.insert(std::make_pair(&value, &elem));
      return elem;
  } else {
      return *(curElem->second);
  }
}

void
Infoflow::putOrConstrainVargConsElemSummarySink(std::string kind, const Function &value, const ConsElem &lub) {
  //errs() << "Adding a constraint...\n";
  const ConsElem & current = getOrCreateVargConsElemSummarySink(value);
  kit->addConstraint(kind, lub, current);
}

const ConsElem &
Infoflow::getOrCreateVargConsElem(const ContextID ctxt, const Function &value) {
  DenseMap<const Function *, const ConsElem *> & valueMap = getOrCreateVargConstraintMap(ctxt);
  DenseMap<const Function *, const ConsElem *>::iterator curElem = valueMap.find(&value);
  if (curElem == valueMap.end()) {
      const ConsElem & elem = kit->newVar(value.getName());
      valueMap.insert(std::make_pair(&value, &elem));

      // Hook up the summaries for non-context sensitive interface
      const ConsElem & summarySource = getOrCreateConsElemSummarySource(value);
      kit->addConstraint("default",summarySource,elem);
      putOrConstrainVargConsElemSummarySink("default", value, elem);

      return elem;
  } else {
      return *(curElem->second);
  }
}

void
Infoflow::putOrConstrainVargConsElem(bool implicit, bool sink, const ContextID ctxt, const Function &value, const ConsElem &lub) {
  const ConsElem & current = getOrCreateVargConsElem(ctxt, value);
  kit->addConstraint(kindFromImplicitSink(implicit,sink), lub, current);
}

const ConsElem &
Infoflow::getOrCreateVargConsElem(const Function &value) {
  return getOrCreateVargConsElem(this->getCurrentContext(), value);
}

void
Infoflow::putOrConstrainVargConsElem(bool implicit, bool sink, const Function &value, const ConsElem &lub) {
  return putOrConstrainVargConsElem(implicit, sink, this->getCurrentContext(), value, lub);
}

const ConsElem &
Infoflow::getOrCreateConsElem(const AbstractLoc &loc) {
    DenseMap<const AbstractLoc *, const ConsElem *>::iterator curElem = locConstraintMap.find(&loc);
    if (curElem == locConstraintMap.end()) {
        //errs() << "Created a constraint variable...\n";
        const ConsElem & elem = kit->newVar("an absloc");
        locConstraintMap.insert(std::make_pair(&loc, &elem));

        return elem;
    } else {
        return *(curElem->second);
    }
}

void
Infoflow::putOrConstrainConsElem(bool implicit, bool sink, const AbstractLoc &loc, const ConsElem &lub) {
  const ConsElem & current = getOrCreateConsElem(loc);
  kit->addConstraint(kindFromImplicitSink(implicit,sink), lub, current);
}

void
Infoflow::generateFunctionConstraints(const Function& f) {
    std::vector<FlowRecord> flows;
    for (Function::const_iterator bb = f.begin(), end = f.end(); bb != end; ++bb) {
        // Build constraints for basic blocks
        // The pc of the entry block will be tainted at any call sites
      generateBasicBlockConstraints(*bb, flows);
    }

    for (std::vector<FlowRecord>::iterator flow = flows.begin(), flowend = flows.end();
        flow != flowend; ++flow) {
      constrainFlowRecord(*flow);
    }
}

void
Infoflow::generateBasicBlockConstraints(const BasicBlock & bb, Flows & flows) {
    // Build constraints for instructions
    for (BasicBlock::const_iterator inst = bb.begin(), end = bb.end(); inst != end; ++inst) {
        getInstructionFlowsInternal(*inst, true, flows);
    }
}

void
Infoflow::constrainMemoryLocation(bool implicit, bool sink, const Value & value, const ConsElem & level) {
    const std::set<const AbstractLoc *> & locs = locsForValue(value);
    for (std::set<const AbstractLoc *>::iterator loc = locs.begin(), end = locs.end();
            loc != end ; ++loc) {
      putOrConstrainConsElem(implicit, sink, **loc, level);
    }
}

void
Infoflow::constrainReachableMemoryLocations(bool implicit, bool sink, const Value & value, const ConsElem & level) {
    const std::set<const AbstractLoc *> & locs = reachableLocsForValue(value);
    for (std::set<const AbstractLoc *>::iterator loc = locs.begin(), end = locs.end();
            loc != end ; ++loc) {
      putOrConstrainConsElem(implicit, sink, **loc, level);
    }
}

const ConsElem &
Infoflow::getOrCreateMemoryConsElem(const Value & value) {
    const ConsElem *join = NULL;
    const std::set<const AbstractLoc *> & locs = locsForValue(value);
    for (std::set<const AbstractLoc *>::iterator loc = locs.begin(), end = locs.end();
            loc != end ; ++loc) {
        if (join == NULL) {
            join = &getOrCreateConsElem(**loc);
        } else {
            join = &kit->upperBound(*join, getOrCreateConsElem(**loc));
        }
    }
    return *join;
}

const ConsElem &
Infoflow::getOrCreateReachableMemoryConsElem(const Value & value) {
    const ConsElem *join = NULL;
    const std::set<const AbstractLoc *> & locs = reachableLocsForValue(value);
    for (std::set<const AbstractLoc *>::iterator loc = locs.begin(), end = locs.end();
            loc != end ; ++loc) {
        if (join == NULL) {
            join = &getOrCreateConsElem(**loc);
        } else {
            join = &kit->upperBound(*join, getOrCreateConsElem(**loc));
        }
    }
    return *join;
}

FlowRecord
Infoflow::currentContextFlowRecord(bool implicit) const {
  const ContextID currentContext = this->getCurrentContext();
  return FlowRecord(implicit,currentContext,currentContext);
}

/// Helper function that computes the join of all operands to an instruction
/// and the pc, and then makes the result of the instruction at least as high.
void
Infoflow::operandsAndPCtoValue(const Instruction & inst, Flows &flows) {
  FlowRecord exp = currentContextFlowRecord(false);
  FlowRecord imp = currentContextFlowRecord(true);
  // pc
  imp.addSourceValue(*inst.getParent());
  // operands
  for (User::const_op_iterator op = inst.op_begin(), end = inst.op_end();
          op != end; ++op) {
    exp.addSourceValue(*op->get());
  }
  // to value
  exp.addSinkValue(inst);
  imp.addSinkValue(inst);

  flows.push_back(exp);
  flows.push_back(imp);
}

void
Infoflow::constrainConditionalSuccessors(const TerminatorInst & term, FlowRecord & rec) {
    std::set<const BasicBlock *> visited;
    std::deque<const BasicBlock *> workqueue;
    const BasicBlock *bb = term.getParent();

    for (unsigned i = 0, end = term.getNumSuccessors(); i < end; ++i) {
        workqueue.push_back(term.getSuccessor(i));
    }

    PostDominatorTree &pdt = getAnalysis<PDTCache>().get(term.getParent()->getParent());

    while (!workqueue.empty()) {
        const BasicBlock *cur = workqueue.front();
        workqueue.pop_front();
        visited.insert(cur);

        if (!pdt.dominates(cur, bb)) {
            rec.addSinkValue(*cur);

            const TerminatorInst *t = cur->getTerminator();
            for (unsigned i = 0, end = t->getNumSuccessors(); i < end; ++i) {
              if (visited.find(cur) == visited.end()) {
                workqueue.push_back(t->getSuccessor(i));
              }
            }
        }
    }
}

Flows
Infoflow::getInstructionFlows(const Instruction & inst) {
  Flows flows;
  getInstructionFlowsInternal(inst, false, flows);
  return flows;
}

void
Infoflow::getInstructionFlowsInternal(const Instruction & inst, bool callees,
                                      Flows & flows) {
    if (const AtomicCmpXchgInst *i = dyn_cast<AtomicCmpXchgInst>(&inst)) {
        return Infoflow::constrainAtomicCmpXchgInst(*i, flows);
    } else if (const AtomicRMWInst *i = dyn_cast<AtomicRMWInst>(&inst)) {
      return Infoflow::constrainAtomicRMWInst(*i, flows);
    } else if (const BinaryOperator *i = dyn_cast<BinaryOperator>(&inst)) {
      return Infoflow::constrainBinaryOperator(*i, flows);
    } else if (const CallInst *i = dyn_cast<CallInst>(&inst)) {
      return Infoflow::constrainCallInst(*i, callees, flows);
    } else if (const CmpInst *i = dyn_cast<CmpInst>(&inst)) {
      return Infoflow::constrainCmpInst(*i, flows);
    } else if (const ExtractElementInst *i = dyn_cast<ExtractElementInst>(&inst)) {
      return Infoflow::constrainExtractElementInst(*i, flows);
    } else if (const FenceInst *i = dyn_cast<FenceInst>(&inst)) {
      return Infoflow::constrainFenceInst(*i, flows);
    } else if (const GetElementPtrInst *i = dyn_cast<GetElementPtrInst>(&inst)) {
      return Infoflow::constrainGetElementPtrInst(*i, flows);
    } else if (const InsertElementInst *i = dyn_cast<InsertElementInst>(&inst)) {
      return Infoflow::constrainInsertElementInst(*i, flows);
    } else if (const InsertValueInst *i = dyn_cast<InsertValueInst>(&inst)) {
      return Infoflow::constrainInsertValueInst(*i, flows);
    } else if (const LandingPadInst *i = dyn_cast<LandingPadInst>(&inst)) {
      return Infoflow::constrainLandingPadInst(*i, flows);
    } else if (const PHINode *i = dyn_cast<PHINode>(&inst)) {
      return Infoflow::constrainPHINode(*i, flows);
    } else if (const SelectInst *i = dyn_cast<SelectInst>(&inst)) {
      return Infoflow::constrainSelectInst(*i, flows);
    } else if (const ShuffleVectorInst *i = dyn_cast<ShuffleVectorInst>(&inst)) {
      return Infoflow::constrainShuffleVectorInst(*i, flows);
    } else if (const StoreInst *i = dyn_cast<StoreInst>(&inst)) {
      return Infoflow::constrainStoreInst(*i, flows);
    } else if (const TerminatorInst *i = dyn_cast<TerminatorInst>(&inst)) {
	return Infoflow::constrainTerminatorInst(*i, callees, flows);
    } else if (const UnaryInstruction *i = dyn_cast<UnaryInstruction>(&inst)) {
      return Infoflow::constrainUnaryInstruction(*i, flows);
    } else {
      assert(false && "Unsupported instruction type!");
    }
}

void
Infoflow::constrainUnaryInstruction(const UnaryInstruction & inst, Flows & flows)
{
    if (const AllocaInst *i = dyn_cast<AllocaInst>(&inst)) {
      return Infoflow::constrainAllocaInst(*i, flows);
    } else if (const CastInst *i = dyn_cast<CastInst>(&inst)) {
      return Infoflow::constrainCastInst(*i, flows);
    } else if (const ExtractValueInst *i = dyn_cast<ExtractValueInst>(&inst)) {
      return Infoflow::constrainExtractValueInst(*i, flows);
    } else if (const LoadInst *i = dyn_cast<LoadInst>(&inst)) {
      return Infoflow::constrainLoadInst(*i, flows);
    } else if (const VAArgInst *i = dyn_cast<VAArgInst>(&inst)) {
      return Infoflow::constrainVAArgInst(*i, flows);
    } else {
      assert(false && "Unsupported unary instruction type!");
    }
}

void
Infoflow::constrainTerminatorInst(const TerminatorInst & inst, bool callees, Flows & flows)
{
    if (const BranchInst *i = dyn_cast<BranchInst>(&inst)) {
      return Infoflow::constrainBranchInst(*i, flows);
    } else if (const IndirectBrInst *i = dyn_cast<IndirectBrInst>(&inst)) {
      return Infoflow::constrainIndirectBrInst(*i, flows);
    } else if (const InvokeInst *i = dyn_cast<InvokeInst>(&inst)) {
      return Infoflow::constrainInvokeInst(*i, callees, flows);
    } else if (const ReturnInst *i = dyn_cast<ReturnInst>(&inst)) {
      return Infoflow::constrainReturnInst(*i, flows);
    } else if (const ResumeInst *i = dyn_cast<ResumeInst>(&inst)) {
      return Infoflow::constrainResumeInst(*i, flows);
    } else if (const SwitchInst *i = dyn_cast<SwitchInst>(&inst)) {
      return Infoflow::constrainSwitchInst(*i, flows);
    } else if (const UnreachableInst *i = dyn_cast<UnreachableInst>(&inst)) {
      return Infoflow::constraintUnreachableInst(*i, flows);
    } else {
      assert(false && "Unsupported terminator instruction type!");
    }
}

///////////////////////////////////////////////////////////////////////////////
/// Atomic memory operations
///////////////////////////////////////////////////////////////////////////////

/// AtomicRMWInst updates a memory location atomically by applying a fixed operation
/// to the current memory value and a value operand.
/// XXX pc, ptr, and operand to memory
void
Infoflow::constrainAtomicRMWInst(const AtomicRMWInst & inst, Flows & flows)
{
    // Flow into memory location:
    FlowRecord expToMem = currentContextFlowRecord(false);
    FlowRecord impToMem = currentContextFlowRecord(true);
    // pc
    impToMem.addSourceValue(*inst.getParent());
    // operands
    expToMem.addSourceValue(*inst.getValOperand());
    impToMem.addSourceValue(*inst.getPointerOperand());
    // current value is already accounted for
    // into memory (don't need to include current value... already accounted for)
    impToMem.addSinkDirectPtr(*inst.getPointerOperand());
    expToMem.addSinkDirectPtr(*inst.getPointerOperand());

    flows.push_back(impToMem);
    flows.push_back(expToMem);
}

/// The 'cmpxchg' instruction is used to atomically modify memory. It loads a
/// value in memory and compares it to a given value. If they are equal, it
/// stores a new value into the memory. The contents of memory at the location
/// specified by the '<pointer>' operand is read and compared to '<cmp>'; if
/// the read value is the equal, '<new>' is written. The original value at the
/// location is returned.
/// There are two flows:
/// 1) pc and cmp and new operands to memory
/// 2) pc and memory to result
void
Infoflow::constrainAtomicCmpXchgInst(const AtomicCmpXchgInst & inst, Flows &flows)
{
    // Flow into memory location:
    FlowRecord expToMem = currentContextFlowRecord(false);
    FlowRecord impToMem = currentContextFlowRecord(true);
    // pc and ptr
    impToMem.addSourceValue(*inst.getParent());
    impToMem.addSourceValue(*inst.getPointerOperand());
    // cmp and new operands
    expToMem.addSourceValue(*inst.getCompareOperand());
    expToMem.addSourceValue(*inst.getNewValOperand());
    // to memory
    expToMem.addSinkDirectPtr(*inst.getPointerOperand());
    impToMem.addSinkDirectPtr(*inst.getPointerOperand());

    // Flow from memory location:
    FlowRecord expFromMem = currentContextFlowRecord(false);
    FlowRecord impFromMem = currentContextFlowRecord(true);
    // pc and ptr
    impFromMem.addSourceValue(*inst.getParent());
    impFromMem.addSourceValue(*inst.getPointerOperand());
    // memory
    expFromMem.addSourceDirectPtr(*inst.getPointerOperand());
    // to result
    expFromMem.addSinkValue(inst);
    impFromMem.addSinkValue(inst);

    flows.push_back(expToMem);
    flows.push_back(impToMem);
    flows.push_back(expFromMem);
    flows.push_back(impFromMem);
}

///////////////////////////////////////////////////////////////////////////////
/// Value operations
///////////////////////////////////////////////////////////////////////////////

/// Result is boolean depending on two operand values and pc
void
Infoflow::constrainCmpInst(const CmpInst & inst, Flows & flows)
{
  return operandsAndPCtoValue(inst, flows);
}

/// 'select' instruction is used to choose one value based on a condition,
/// without branching. Flow from operands and pc to value.
void
Infoflow::constrainSelectInst(const SelectInst & inst, Flows & flows)
{
  return operandsAndPCtoValue(inst, flows);
}

/// Binary operators compute a result from two operands
/// Flow from pc and operands to result
void
Infoflow::constrainBinaryOperator(const BinaryOperator & inst, Flows & flows)
{
  return operandsAndPCtoValue(inst, flows);
}

/// Instructions in this category are the conversion instructions (casting)
/// which all take a single operand and a type. They perform various bit
/// conversions on the operand. Flow is from operands and pc to value.
void
Infoflow::constrainCastInst(const CastInst & inst, Flows & flows)
{
  return operandsAndPCtoValue(inst, flows);
}

///////////////////////////////////////////////////////////////////////////////
/// Control flow operations
///////////////////////////////////////////////////////////////////////////////

/// Value of PHI node depends on values of incoming edges (the operands)
/// and on pc.
void
Infoflow::constrainPHINode(const PHINode & inst, Flows & flows)
{
  return operandsAndPCtoValue(inst, flows);
}

/// Conditional branches cause a flow from the condition and pc to all
/// successors that do not post-dominate the current instruction.
void
Infoflow::constrainBranchInst(const BranchInst & inst, Flows & flows)
{
  // Only additional flow for conditional branch
  if (!inst.isConditional()) return;

  FlowRecord flow = currentContextFlowRecord(true);
  // pc
  flow.addSourceValue(*inst.getParent());
  // cond
  flow.addSourceValue(*inst.getCondition());
  constrainConditionalSuccessors(inst, flow);

  flows.push_back(flow);
}

/// The 'indirectbr' instruction implements an indirect branch to a label within
/// the current function, whose address is specified by "address". The rest of
/// the arguments indicate the full set of possible destinations that the address
/// may point to. Blocks are allowed to occur multiple times in the destination
/// list, though this isn't particularly useful.
///
/// Flow from the pc and address to the pc of all successor basic blocks (that
/// aren't post-dominators)
void
Infoflow::constrainIndirectBrInst(const IndirectBrInst & inst, Flows & flows)
{
  FlowRecord flow = currentContextFlowRecord(true);
  // pc
  flow.addSourceValue(*inst.getParent());
  // addr
  flow.addSourceValue(*inst.getAddress());

  constrainConditionalSuccessors(inst, flow);

  flows.push_back(flow);
}

/// The 'switch' instruction is used to transfer control flow to one of several
/// different places. It is a generalization of the 'br' instruction, allowing
/// a branch to occur to one of many possible destinations.
/// The 'switch' instruction uses three parameters: an integer comparison value
/// 'value', a default 'label' destination, and an array of pairs of comparison
/// value constants and 'label's.
/// This table is searched for the given value. If the value is found, control
/// flow is transferred to the corresponding destination; otherwise, control
/// flow is transferred to the default destination.
///
/// Flow from the pc and address to the pc of all successor basic blocks (that
/// aren't post-dominators)
void
Infoflow::constrainSwitchInst(const SwitchInst & inst, Flows & flows)
{
  FlowRecord flow = currentContextFlowRecord(true);
  // pc
  flow.addSourceValue(*inst.getParent());
  // condition
  flow.addSourceValue(*inst.getCondition());

  constrainConditionalSuccessors(inst, flow);

  flows.push_back(flow);
}

/// 'unreachable' instruction has no defined semantics. This instruction is used
/// to inform the optimizer that a particular portion of the code is not reachable.
/// Since this instruction is never executed and has no semantics, there is no flow.
void
Infoflow::constraintUnreachableInst(const UnreachableInst & inst, Flows & flows) {
  // Intentionally blank
}

///////////////////////////////////////////////////////////////////////////////
/// Memory operations
///////////////////////////////////////////////////////////////////////////////

/// Compute a pointer value, depending on the pc and operands.
void
Infoflow::constrainGetElementPtrInst(const GetElementPtrInst & inst, Flows & flows)
{
  return operandsAndPCtoValue(inst, flows);
}

/// Store a value into a memory location. Flow from pc, pointer value, and value
/// into the memory location. Has no return value.
void
Infoflow::constrainStoreInst(const StoreInst & inst, Flows & flows)
{
  FlowRecord exp = currentContextFlowRecord(false);
  FlowRecord imp = currentContextFlowRecord(true);
  // pc
  imp.addSourceValue(*inst.getParent());
  // ptr
  imp.addSourceValue(*inst.getPointerOperand());
  // value
  exp.addSourceValue(*inst.getValueOperand());
  // to memory
  exp.addSinkDirectPtr(*inst.getPointerOperand());
  imp.addSinkDirectPtr(*inst.getPointerOperand());

  flows.push_back(imp);
  flows.push_back(exp);
}

/// Load the value from the memory at the pointer operand into the result.
/// Flow from pc, ptr value, and memory to result.
void
Infoflow::constrainLoadInst(const LoadInst & inst, Flows & flows)
{
  FlowRecord exp = currentContextFlowRecord(false);
  FlowRecord imp = currentContextFlowRecord(true);
  // pc
  imp.addSourceValue(*inst.getParent());
  // ptr
  imp.addSourceValue(*inst.getPointerOperand());
  // from memory
  exp.addSourceDirectPtr(*inst.getPointerOperand());
  // to value
  exp.addSinkValue(inst);
  imp.addSinkValue(inst);

  flows.push_back(exp);
  flows.push_back(imp);
}

/// Memory barrier instruction. Treat as noop.
void
Infoflow::constrainFenceInst(const FenceInst & inst, Flows & flows)
{
  // TODO: support for multi-threaded info. flow?
  assert(false && "Unsupported instruction type: fence");
}

/// 'alloca' instruction allocates memory on the stack frame of the currently
/// executing function, to be automatically released when this function returns
/// to its caller. Returns a pointer. Flow from pc and operands to value.
void
Infoflow::constrainAllocaInst(const AllocaInst & inst, Flows & flows)
{
  return operandsAndPCtoValue(inst, flows);
}

/// The 'va_arg' instruction is used to access arguments passed through the
/// "variable argument" area of a function call. It is used to implement the
/// va_arg macro in C. This instruction takes a va_list* value and the type
/// of the argument. It returns a value of the specified argument type and
/// increments the va_list to point to the next argument. The actual type of
/// va_list is target specific.
/// There are two flows:
/// from the pc, va_list ptr, and the va_list value(s) to the result
/// from the pc and va_list ptr to all following calls that could alias
void
Infoflow::constrainVAArgInst(const VAArgInst & inst, Flows & flows)
{
  FlowRecord exp = currentContextFlowRecord(false);
  FlowRecord imp = currentContextFlowRecord(true);
  // pc
  imp.addSourceValue(*inst.getParent());
  // ptr
  imp.addSourceValue(*inst.getPointerOperand());
  // from memory
  exp.addSourceDirectPtr(*inst.getPointerOperand());
  // from va_list representation
  imp.addSourceVarg(*inst.getParent()->getParent());
  // to value
  exp.addSinkValue(inst);
  imp.addSinkValue(inst);
  // to VA list
  imp.addSinkDirectPtr(*inst.getPointerOperand());
  // to va_list representation
  imp.addSinkVarg(*inst.getParent()->getParent());

  flows.push_back(exp);
  flows.push_back(imp);
}

///////////////////////////////////////////////////////////////////////////////
/// Vector operations
///////////////////////////////////////////////////////////////////////////////

/// The 'shufflevector' instruction constructs a permutation of elements from
/// two input vectors, returning a vector with the same element type as the
/// input and length that is the same as the shuffle mask.
/// Flow is from all operands and pc to result
void
Infoflow::constrainShuffleVectorInst(const ShuffleVectorInst & inst, Flows & flows)
{
  return operandsAndPCtoValue(inst, flows);
}

/// The 'insertelement' instruction inserts a scalar element into a vector at
/// a specified index. The result is a vector of the same type as val. Its
/// element values are those of val except at position idx, where it gets the
/// value elt. If idx exceeds the length of val, the results are undefined.
/// Flow is from pc and all operands into the result.
void
Infoflow::constrainInsertElementInst(const InsertElementInst & inst, Flows & flows)
{
  return operandsAndPCtoValue(inst, flows);
}

/// The 'extractelement' instruction extracts a single scalar element from a
/// vector at a specified index. Flow is from operands and pc to value.
void
Infoflow::constrainExtractElementInst(const ExtractElementInst & inst, Flows & flows)
{
  return operandsAndPCtoValue(inst, flows);
}

///////////////////////////////////////////////////////////////////////////////
/// Aggregate operations
///////////////////////////////////////////////////////////////////////////////

/// The 'insertvalue' instruction inserts a value into a member field in an
/// aggregate value. The first operand of an 'insertvalue' instruction is a
/// value of struct or array type. The second operand is a first-class value
/// to insert. The following operands are constant indices indicating the
/// position at which to insert the value. The result is the same as the
/// original value with the element replaced. Flow from pc and all operands
/// to result.
void
Infoflow::constrainInsertValueInst(const InsertValueInst & inst, Flows & flows)
{
  return operandsAndPCtoValue(inst, flows);
}

/// The 'extractvalue' instruction extracts the value of a member field from
/// an aggregate value. The first operand of an 'extractvalue' instruction is
/// a value of struct or array type. The operands are constant indices to
/// specify which value to extract. The result is the value at the position
/// in the aggregate specified by the index operands.
/// Flow from operands and pc to value. Note no need for abstract locs since
/// the aggregate is a value not a pointer.
void
Infoflow::constrainExtractValueInst(const ExtractValueInst & inst, Flows & flows)
{
  return operandsAndPCtoValue(inst, flows);
}

///////////////////////////////////////////////////////////////////////////////
/// Function invocation and exception handling
///////////////////////////////////////////////////////////////////////////////

void
Infoflow::constrainCallInst(const CallInst & inst, bool analyzeCallees, Flows & flows)
{
    // TODO: filter out and handle Intrinsics here instead of deferring
    // to the Signature mechanism...
  if (const IntrinsicInst *intr = dyn_cast<IntrinsicInst>(&inst)) {
    return constrainIntrinsic(*intr, flows);
  } else {
    return constrainCallSite(ImmutableCallSite(&inst), analyzeCallees, flows);
  }
}

void
Infoflow::constrainInvokeInst(const InvokeInst & inst, bool analyzeCallees, Flows & flows)
{
    constrainCallSite(ImmutableCallSite(&inst), analyzeCallees, flows);

    // Since an invoke instruction may not return to the same program point
    // there is an additional information flow to all nodes that are not
    // post-dominators

    // 1) pc of function should be at least as high as current pc + function pointer
    // Handle flow due to the possibility of multiple return sites
    FlowRecord flow = currentContextFlowRecord(true);
    // pc
    flow.addSourceValue(*inst.getParent());
    // condition
    flow.addSourceValue(*inst.getCalledValue());
    // Gather constraints
    constrainConditionalSuccessors(inst, flow);

    flows.push_back(flow);
}

void
Infoflow::constrainCallSite(const ImmutableCallSite & cs, bool analyzeCallees, Flows & flows) {
  // For all functions that could possibly be invoked by this call
  // 1) pc of function should be at least as high as current pc + function pointer
  // 2) levels of params should be as high as corresponding args
  // Result should be at least as high as the possible return values

  // Invoke the analysis on callees, if we're actually generating constraints
  // XXX HACK if we're not doing analysis on callees, we need to add any signature flows here
  if (analyzeCallees) {
    this->getCallResult(cs, Unit());
  } else if (usesExternalSignature(cs)) {
    Flows recs = signatureRegistrar->process(this->getCurrentContext(), cs);
    flows.insert(flows.end(), recs.begin(), recs.end());
  }

  std::set<std::pair<const Function *, const ContextID> > callees = this->invokableCode(cs);
  // Do constraints for each callee
  for (std::set<std::pair<const Function *, const ContextID> >::iterator callee = callees.begin(), end = callees.end();
       callee != end; ++callee) {
    constrainCallee((*callee).second, *((*callee).first), cs, flows);
  }
}

void
Infoflow::constrainCallee(const ContextID calleeContext, const Function & callee, const ImmutableCallSite & cs, Flows & flows) {
    const ContextID callerContext = this->getCurrentContext();
 
    // 1) pc of function should be at least as high as current pc + function pointer
    FlowRecord pcFlow = FlowRecord(true,callerContext, calleeContext);
    // caller pc
    pcFlow.addSourceValue(*cs->getParent());
    // caller ptr
    pcFlow.addSourceValue(*cs.getCalledValue());
    // to callee pc
    pcFlow.addSinkValue(callee.getEntryBlock());
    flows.push_back(pcFlow);

    // 2) levels of params should be as high as corresponding args
    unsigned int numArgs = cs.arg_size();
    unsigned int numParams = callee.arg_size();

    // Check arities for sanity...
    assert((!callee.isVarArg() || numArgs >= numParams)
	   && "variable arity function called with two few arguments");
    assert((callee.isVarArg() || numArgs == numParams)
	   && "function called with the wrong number of arguments");

    // The level of each non-vararg param should be as high as the corresponding argument
    Function::const_arg_iterator param = callee.arg_begin();
    for (unsigned int i = 0; i < numParams; i++) {
        FlowRecord argFlow = FlowRecord(false,callerContext, calleeContext);
	argFlow.addSourceValue(*cs.getArgument(i));
	argFlow.addSinkValue(*param);
	flows.push_back(argFlow);
	++param;
    }
    // The remaining arguments provide a bound on the vararg structure
    if (numArgs > numParams) {
      FlowRecord varargFlow = FlowRecord(false, callerContext, calleeContext);
	for (unsigned int i = numParams; i < numArgs; i++) {
	    varargFlow.addSourceValue(*cs.getArgument(i));
	}
	varargFlow.addSinkVarg(callee);
	flows.push_back(varargFlow);
    }

    // 3) result should be at least as high as the possible return values
    for (Function::const_iterator block = callee.begin(), end = callee.end();
	 block != end; ++block) {
        const TerminatorInst * terminator = block->getTerminator();
        if (terminator) {
	    if (const ReturnInst * retInst = dyn_cast<ReturnInst>(terminator)) {
	      FlowRecord retFlow = FlowRecord(false, calleeContext, callerContext);
		retFlow.addSourceValue(*retInst);
		retFlow.addSinkValue(*cs.getInstruction());
		flows.push_back(retFlow);
	    }
	}
    }

}

void
Infoflow::constrainReturnInst(const ReturnInst & inst, Flows & flows)
{
  if (inst.getNumOperands() != 0) {
    operandsAndPCtoValue(inst, flows);
  }
}


// TODO: Revisit and understand this instruction. Something to do with exception handling.
void
Infoflow::constrainLandingPadInst(const LandingPadInst & inst, Flows & flows)
{
    return operandsAndPCtoValue(inst, flows);
}

// TODO: Revisit and correct
void
Infoflow::constrainResumeInst(const ResumeInst & inst, Flows & flows)
{
    return operandsAndPCtoValue(inst, flows);
}

///////////////////////////////////////////////////////////////////////////////
/// Intrinsics
///////////////////////////////////////////////////////////////////////////////

void
constrainMemcpyOrmove(const IntrinsicInst & intr, Flows & flows) {
  FlowRecord imp = FlowRecord(true);
  FlowRecord exp = FlowRecord(false);
  // Flow from data at source pointer, length, and alignment into 
  // data at destination pointer
  exp.addSourceDirectPtr(*intr.getArgOperand(1));
  imp.addSourceValue(*intr.getArgOperand(1));
  imp.addSourceValue(*intr.getArgOperand(2));
  imp.addSourceValue(*intr.getArgOperand(3));
  exp.addSinkDirectPtr(*intr.getArgOperand(0));
  imp.addSinkDirectPtr(*intr.getArgOperand(0));
  flows.push_back(exp);
  flows.push_back(imp);
}

void
constrainMemset(const IntrinsicInst & intr, Flows & flows) {
  FlowRecord exp = FlowRecord(false);
  FlowRecord imp = FlowRecord(true);
  // Flow from value, length, and alignment into 
  // data at destination pointer
  exp.addSourceValue(*intr.getArgOperand(1));
  imp.addSourceValue(*intr.getArgOperand(2));
  imp.addSourceValue(*intr.getArgOperand(3));
  exp.addSinkDirectPtr(*intr.getArgOperand(0));
  imp.addSinkDirectPtr(*intr.getArgOperand(0));
  flows.push_back(exp);
  flows.push_back(imp);
}

void
Infoflow::constrainIntrinsic(const IntrinsicInst & intr, Flows & flows) {
  switch (intr.getIntrinsicID()) {
  // Vararg intrinsics 
  case Intrinsic::vastart:
  case Intrinsic::vaend:
  case Intrinsic::vacopy:
    // These should be nops because the actual flows are taken care of as part
    // of function invocation and the va_arg instruction
    return ;
  // StdLib memory intrinsics
  case Intrinsic::memcpy:
  case Intrinsic::memmove:
    return constrainMemcpyOrmove(intr, flows);   
  case Intrinsic::memset:
    return constrainMemset(intr, flows);
  // StdLib math intrinsics
  case Intrinsic::sqrt:
  case Intrinsic::powi:
  case Intrinsic::sin:
  case Intrinsic::cos:
  case Intrinsic::pow:
  case Intrinsic::exp:
  case Intrinsic::log:
  case Intrinsic::fma:
    return this->operandsAndPCtoValue(intr, flows);
  // Unsupported intrinsics
  default:
    DEBUG(errs() << "Unsupported intrinsic: " << Intrinsic::getName(intr.getIntrinsicID()) << "\n");
  }
}

}
