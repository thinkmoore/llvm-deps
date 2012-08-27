//===- Slice.cpp - Sample Pass --------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// TODO description
//
//===----------------------------------------------------------------------===//

#include "Slice.h"

#include <sstream>

namespace deps {

using namespace llvm;

Slice::Slice(Infoflow & info,
    std::string name,
    FlowRecord rec,
    bool cutSinks) : cutAfterSinks(cutSinks), infoflow(info) {
  std::string sourceKind = name + "-sources";
  std::string sinkKind = name + "-sinks";

  // Use the source and sink sets to generate constraints
  for (FlowRecord::value_iterator value = rec.source_value_begin(), end = rec.source_value_end();
        value != end; ++value) {
      infoflow.setTainted(sourceKind, **value);
  }
  for (FlowRecord::value_iterator value = rec.source_directptr_begin(), end = rec.source_directptr_end();
        value != end; ++value) {
      infoflow.setDirectPtrTainted(sourceKind, **value);
  }
  for (FlowRecord::value_iterator value = rec.source_reachptr_begin(), end = rec.source_reachptr_end();
        value != end; ++value) {
      infoflow.setReachPtrTainted(sourceKind, **value);
  }
  for (FlowRecord::fun_iterator fun = rec.source_varg_begin(), end = rec.source_varg_end();
       fun != end; ++fun) {
    infoflow.setVargTainted(sourceKind, **fun);
  }

  for (FlowRecord::value_iterator value = rec.sink_value_begin(), end = rec.sink_value_end();
        value != end; ++value) {
      infoflow.setUntainted(sinkKind, **value);
  }
  for (FlowRecord::value_iterator value = rec.sink_directptr_begin(), end = rec.sink_directptr_end();
          value != end; ++value) {
        infoflow.setDirectPtrUntainted(sinkKind, **value);
    }
  for (FlowRecord::value_iterator value = rec.sink_reachptr_begin(), end = rec.sink_reachptr_end();
        value != end; ++value) {
      infoflow.setReachPtrUntainted(sinkKind, **value);
  }
  for (FlowRecord::fun_iterator fun = rec.sink_varg_begin(), end = rec.sink_varg_end();
       fun != end; ++fun) {
    infoflow.setVargUntainted(sinkKind, **fun);
  }

  std::set<std::string> sourceKinds;
  sourceKinds.insert(sourceKind);
  forward = infoflow.leastSolution(sourceKinds, false, !cutAfterSinks);

  std::set<std::string> sinkKinds;
  sinkKinds.insert(sinkKind);
  backward = infoflow.greatestSolution(sinkKinds, false);
}

Slice::~Slice() {
  delete forward;
  delete backward;
}

bool
Slice::valueInSlice(const Value & value) {
  return forward->isTainted(value) && !backward->isTainted(value);
}

bool
Slice::directPtrInSlice(const Value & value) {
  return forward->isDirectPtrTainted(value) && !backward->isDirectPtrTainted(value);
}

bool
Slice::reachPtrInSlice(const Value & value) {
  return forward->isReachPtrTainted(value) && !backward->isReachPtrTainted(value);
}

bool
Slice::vargInSlice(const Function & fun) {
  return forward->isVargTainted(fun) && !backward->isVargTainted(fun);
}

bool
MultiSlice::sourceReachable(const Value *Overflow, const FlowRecord & record) {
  assert(forward.count(Overflow));
  // Is one of the flow sources reachable from the given overflow
  // Check sources that are values
  for (FlowRecord::value_iterator source = record.source_value_begin(), end = record.source_value_end();
        source != end; ++source) {
    if (valueInSlice(**source, Overflow)) return true;
  }
  // Check sources that are directly pointed to locations
  for (FlowRecord::value_iterator source = record.source_directptr_begin(), end = record.source_directptr_end();
        source != end; ++source) {
    if (directPtrInSlice(**source, Overflow)) return true;
  }
  // Check sources that are all reachable locations
  for (FlowRecord::value_iterator source = record.source_reachptr_begin(), end = record.source_reachptr_end();
        source != end; ++source) {
    if (reachPtrInSlice(**source, Overflow)) return true;
  }
  // Check sources that are varargs
  for (FlowRecord::fun_iterator source = record.source_varg_begin(), end = record.source_varg_end();
       source != end; ++source) {
    if (vargInSlice(**source, Overflow)) return true;
  }
  return false;
}

MultiSlice::MultiSlice(Infoflow & info, InfoflowSolution *backward,
                       std::string kindPrefix,
                       FlowRecord sinks,
                       std::vector<const Value*> & sources,
		       bool cutSinks) : cutAfterSinks(cutSinks), infoflow(info), backward(backward) {
  std::string sourceKindPrefix = kindPrefix + "-sources";

  // Give each overflow a unique id, even across MultiSlice objects.
  static uint64_t unique_id = 0;

  // Add constraints for these sources:
  DenseMap<const Value*,std::string> kindMap;
  std::vector<std::string> sourceKinds;
  for (std::vector<const Value*>::iterator src = sources.begin(),
       end = sources.end(); src != end; ++src) {
    std::stringstream SS;
    SS << unique_id++;
    std::string sourceKind = sourceKindPrefix + SS.str();
    kindMap[*src] = sourceKind;
    sourceKinds.push_back(sourceKind);
    infoflow.setTainted(sourceKind, **src);
  }

  // Ask IF to solve these all at once
  std::vector<InfoflowSolution*> Solns = infoflow.solveLeastMT(sourceKinds, !cutAfterSinks);

  // Now extract the InfoflowSolution objects:
  unsigned index = 0;
  for (std::vector<const Value*>::iterator src = sources.begin(),
       end = sources.end(); src != end; ++src, ++index) {
    forward[*src] = Solns[index];
  }
}

MultiSlice::~MultiSlice() {
  for (DenseMap<const Value*, InfoflowSolution*>::iterator I = forward.begin(),
       E = forward.end(); I != E; ++I)
    delete I->second;
}

} // end namespace deps

