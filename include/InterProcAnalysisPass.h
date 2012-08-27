//===- InterProcAnalysisPass.h ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the InterProcAnalysisPass template, which can be extended
// to implement context-sensitive inter-procedural analyses.
//
// An analysis using this template is similar to a FunctionPass, but each
// function may be analyzed multiple times, once for each _context_ in which
// it is analyzed.
//
// If the module contains a main function, it will be analyzed first using
// the given initialContext. If the module does not contain a main function,
// all externally linkable functions will be analyzed in their initialContext.
// Other functions are analyzed on demand via calls to getAnalysisResult(..).
//
//===----------------------------------------------------------------------===//

#ifndef INTERPROC_ANALYSIS_PASS_H
#define INTERPROC_ANALYSIS_PASS_H

#include "assistDS/DataStructureCallGraph.h"

#include "llvm/Pass.h"
#include "llvm/Module.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <set>
#include <deque>

namespace llvm {

/// An AnalysisUnit represents a Function to be analyzed and a context
/// in which to analyze it.
template<class C>
class AnalysisUnit {
public:
  explicit AnalysisUnit(const C ctx, const Function & fun) : ctx(ctx), fun(fun) { }
  const Function &function() const { return fun; }
  const C context() const { return ctx; }
  bool operator<(const AnalysisUnit<C> & unit) const {
    if (&this->fun != &unit.fun)
      return &this->fun < &unit.fun;
    return this->ctx < unit.ctx;
  }
private:
  const C ctx;
  const Function & fun;
};

/// An AnalysisRecord stores the current analysis result for each AnalysisUnit.
/// The input used to compute the output is stored so that a fixpoint can be found.
template<class I, class O>
class AnalysisRecord {
public:
  AnalysisRecord() { }
  AnalysisRecord(const I in) : in(in) { }
  AnalysisRecord(const I in, const O out) : in(in), out(out) { }
  AnalysisRecord(const AnalysisRecord & rec) : in(rec.in), out(rec.out) { }
  AnalysisRecord & operator= (const AnalysisRecord & other) {
    in = other.in;
    out = other.out;
    return *this;
  }
  const I input() const { return in; }
  const O output() const { return out; }
  bool operator<(const AnalysisRecord<I,O> & rec) const {
    if (this->input() < rec.input()) return true;
    return this->output() < rec.output();
  }
private:
  I in;
  O out;
};

/// The InterProcWorkQueue manages AnalysisUnits that still need
/// to be processed. Each AnalysisUnit may appear at most once in
/// the work queue (though it can be added again).
template<class C>
class InterProcWorkQueue {
public:
  typedef AnalysisUnit<C> AUnitType;
  typedef typename std::set<AUnitType>::iterator AUnitIterator;

  bool empty() { return queue.empty(); }

  void enqueue(AUnitType unit) {
    if (set.find(unit) == set.end()) {
      set.insert(unit);
      queue.push_back(unit);
    }
  }

  void enqueue(AUnitIterator start, AUnitIterator end) {
    for (; start != end; ++start) {
      enqueue(*start);
    }
  }

  AUnitType dequeue() {
    assert(!queue.empty());
    AUnitType unit = *queue.begin();
    queue.pop_front();
    set.erase(unit);
    return unit;
  }

private:
  std::set<AUnitType > set;
  std::deque<AUnitType > queue;
};

/// InterProcAnalysisPass can be extended to implement interprocedural
/// analysis for different contexts and analysis input/output types.
///
/// The context type, C, must be default and copy constructible, assignable,
/// and have a valid < operator.
///
/// The input and output types must implement the operators for
/// <=, ==, and !=, and be default constructible.
/// Furthermore, each should implement:
///  const T upperBound(const T & other) const
/// Where the output should be the join of the instances.
template<class C, class I, class O>
class InterProcAnalysisPass : public ModulePass {
public:
  typedef AnalysisUnit<C> AUnitType;
  typedef typename std::set<AUnitType>::iterator AUnitIterator;

  explicit InterProcAnalysisPass(char &pid) : ModulePass(pid) {}

  /// bottomInput - This method should be implemented by the subclass as the
  /// initial input to use for each analysis unit. It should represent the
  /// "bottom" element of the p.o. of inputs
  virtual const I bottomInput() const = 0;
  /// bottomOutput - This method should be implemented by the subclass as the
  /// initial output to use for each analysis unit. It should represent the
  /// "bottom" element of the p.o. of summaries
  virtual const O bottomOutput() const = 0;

  /// initialContext - This method will be called to create an initial context
  /// for analyzing the initial functions of a module (main, or the externally
  /// linked functions).
  virtual C initialContext(const Function &) = 0;
  /// updateContext - This method will be called to create a new context
  /// by extending the current context with a new function call.
  virtual C updateContext(const C, const ImmutableCallSite &) = 0;
  virtual C updateIndirectContext(const C, const ImmutableCallSite &) = 0;

  /// runOnContext - This method should be implemented by the subclass to
  /// perform the desired analysis on the current analysis unit.
  virtual const O runOnContext(const AUnitType unit, const I input) = 0;
  /// doInitialization - This method is called before any analysis units are
  /// analyzed, allowing the pass to do initialization.
  virtual void doInitialization() { }
  /// doInitialization - This method is called after all analysis units of the
  /// module have been analyzed, allowing the pass to do required cleanup.
  virtual void doFinalization() { }

  /// getAnalysisResult - Returns the current summary for the analysis unit
  /// and schedules reanalysis of the unit if required.
  const O getAnalysisResult(const AUnitType unit, const I input) {
    // First, check if we have analyzed (or added to the queue) the
    // requested analysis unit. Create an entry if one does not exist.
    bool validOutput = true;
    if (analysisRecords.find(unit) == analysisRecords.end()) {
      analysisRecords[unit] = AnalysisRecord<I,O>(bottomInput());
      validOutput = false;
    }
    ARecord rec = analysisRecords[unit];

    // Is the existing result suitable?
    if (validOutput && input <= rec.input()) {
      // If so, just return it
      return rec.output();
    } else {
      // Otherwise, put it in the queue and return a "good enough" answer
      // If the answer improves, we'll re-analyze the caller with the new
      // answer.
      requestProcessing(unit, input);
      rec = analysisRecords[unit];
      return rec.output();
    }
  }

  /// getCurrentContext - returns the context for which we're analyzing
  /// the current function. (i.e., what context are we currently executing
  /// runOnContext(..) for?)
  const C getCurrentContext() const {
    if (currentAnalysisUnit != NULL) {
      return currentAnalysisUnit->context();
    } else {
      return C();
    }
  }

  /// getAnalysisUsage - InterProcAnalysisPass requires and preserves the
  /// call graph. Derived methods must call this implementation.
  virtual void getAnalysisUsage(AnalysisUsage &Info) const {
    Info.addRequired<DataStructureCallGraph>();
    Info.addPreserved<DataStructureCallGraph>();
  }

  /// runOnModule - the work queue "driver". Continues analyzing
  /// AnalysisUnits until there is no more work to be done.
  bool runOnModule(Module &M) {
    doInitialization();

    analyzedFunctions.clear();

    // Add the main function to the queue. If there isn't
    // a main function, add any externally linkable functions.
    addStartItemsToWorkQueue();
    // Do work until we're done.
    while (!workQueue.empty()) {
      AnalysisUnit<C> unit = workQueue.dequeue();
      processAnalysisUnit(unit);
    }

    // Some functions may not have been analyzed because they
    // appear unreachable. Analyze them now just in case.
    addUnanalyzedFunctionsToWorkQueue();
    while (!workQueue.empty()) {
      AnalysisUnit<C> unit = workQueue.dequeue();
      processAnalysisUnit(unit);
    }

    currentAnalysisUnit = NULL;

    doFinalization();
    return false;
  }

private:
  typedef AnalysisRecord<I,O> ARecord;
  typedef typename std::map<AUnitType,ARecord>::iterator ARecordIterator;
  typedef typename std::map<AUnitType, std::set<AUnitType> >::iterator DependencyIterator;

  InterProcWorkQueue<C> workQueue;
  std::map<AUnitType,ARecord> analysisRecords;
  /// For each analysis unit, we track the analysis units that requested
  /// its analysis results. If those results change, we reanalyze all
  /// dependencies.
  std::map<AUnitType, std::set<AUnitType> > dependencies;
  const AUnitType *currentAnalysisUnit;
  std::set<const Function *> analyzedFunctions;

  /// Adds entry points to the module to the work queue.
  void addStartItemsToWorkQueue() {
    const CallGraph & cg = getAnalysis<DataStructureCallGraph>();
    const CallGraphNode *root = cg.getRoot();
    const Function *rootFun = root->getFunction();

    std::set<AUnitType> startItems;
    if (rootFun) {
     // The module has a main function, start analysis here
     startItems.insert(AnalysisUnit<C>(initialContext(*rootFun), *rootFun));
    } else {
     // The module has no main, start analysis with any functions with
     // external linkage
     for (CallGraphNode::const_iterator f = root->begin(), end = root->end();
             f != end; ++f) {
       Function *startFun = f->second->getFunction();
       startItems.insert(AnalysisUnit<C>(initialContext(*startFun), *startFun));
     }
    }

    // Add each start item to the work queue with the bottom input.
    const I initInput = bottomInput();
    for (AUnitIterator item = startItems.begin(), end = startItems.end();
               item != end; ++item) {
     analysisRecords[*item] = AnalysisRecord<I,O>(initInput);
     workQueue.enqueue(*item);
    }
  }

  void addUnanalyzedFunctionsToWorkQueue() {
    std::set<AUnitType> startItems;

    // For each function in the module, check if it was analyzed already.
    // If it wasn't and it has code, add it to the workqueue
    const CallGraph & cg = getAnalysis<DataStructureCallGraph>();
    Module & m = cg.getModule();
    for (Module::iterator fun = m.begin(), end = m.end(); fun != end; ++fun) {
      if (analyzedFunctions.find(fun) == analyzedFunctions.end()) {
        if (!fun->isDeclaration()) {
          startItems.insert(AnalysisUnit<C>(initialContext(*fun), *fun));
        }
      }
    }

    // Add each start item to the work queue with the bottom input.
    const I initInput = bottomInput();
    for (AUnitIterator item = startItems.begin(), end = startItems.end();
               item != end; ++item) {
     analysisRecords[*item] = AnalysisRecord<I,O>(initInput);
     workQueue.enqueue(*item);
    }
  }

  /// Analyzes an analysis unit by invoking runOnContext(..).
  /// If the result changes, adds the invalidated dependencies
  /// to the work queue.
  void processAnalysisUnit(const AUnitType unit) {
    currentAnalysisUnit = &unit;
    ARecordIterator rec = analysisRecords.find(unit);
    assert(rec != analysisRecords.end() && "No input!");
    const O prevOutput = rec->second.output();
    const I input = rec->second.input();
    const O output = runOnContext(unit, input);
    analyzedFunctions.insert(&unit.function());

    analysisRecords[unit] = AnalysisRecord<I,O>(input, output);

    // Did the result change?
    if (prevOutput != output) {
      // need to add any consumers back to the workQueue
      workQueue.enqueue(dependencies[unit].begin(), dependencies[unit].end());
    }
  }

  /// Adds an analysis unit to the work queue. Takes care of navigating the
  /// lattice of analysis inputs.
  void requestProcessing(const AUnitType unit, const I input) {
    // If we've analyzed this record before, we need to join the currently
    // requested input with the input we used before (to reach a fixpoint
    // in the analysis).
    ARecord rec = analysisRecords[unit];
    I newInput = input.upperBound(rec.input());
    analysisRecords[unit] = AnalysisRecord<I,O>(newInput, rec.output());

    // Add the current analysis unit to the dependencies for the requested
    // analysis unit. That way we can re-analyze the current unit if the
    // result for the requested unit changes.
    dependencies[unit].insert(*currentAnalysisUnit);

    workQueue.enqueue(unit);
  }
};

}

#endif /* INTERPROC_ANALYSIS_PASS_H */
