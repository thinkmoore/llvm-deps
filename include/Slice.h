//===-- Slice.h -------------------------------------------------*- C++ -*-===//
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

#ifndef SLICE_H_
#define SLICE_H_

#include "Infoflow.h"
#include "SourceSinkAnalysis.h"
#include "FlowRecord.h"

#include <deque>
#include <map>
#include <set>

namespace deps {

using namespace llvm;

class Slice {
public:
  Slice(Infoflow & info,
        std::string name,
        FlowRecord sourcesAndSinks,
	bool cutAfterSinks);
  bool valueInSlice(const Value & value);
  bool directPtrInSlice(const Value & value);
  bool reachPtrInSlice(const Value & value);
  bool vargInSlice(const Function & fun);
  ~Slice();
private:
  bool cutAfterSinks;
  Infoflow & infoflow;
  InfoflowSolution *forward;
  InfoflowSolution *backward;
};

class MultiSlice {
public:
  MultiSlice(Infoflow & info,
             InfoflowSolution *backward,
             std::string kindPrefix,
             FlowRecord sinks,
             std::vector<const Value*> & sources,
	     bool cutAfterSinks);

  ~MultiSlice();

  bool sourceReachable(const Value* Source, const FlowRecord &FR);

  // XXX: These arguments are backwards from above
  bool valueInSlice(const Value & value, const Value* source) {
    assert(forward.count(source));
    return forward[source]->isTainted(value) && !backward->isTainted(value);
  }
  bool directPtrInSlice(const Value & value, const Value* source) {
    assert(forward.count(source));
    return forward[source]->isDirectPtrTainted(value) &&
          !backward->isDirectPtrTainted(value);
  }
  bool reachPtrInSlice(const Value & value, const Value* source) {
    assert(forward.count(source));
    return forward[source]->isReachPtrTainted(value) &&
           !backward->isReachPtrTainted(value);
  }
  bool vargInSlice(const Function & fun, const Value* source) {
    assert(forward.count(source));
    return forward[source]->isVargTainted(fun) &&
           !backward->isVargTainted(fun);
  }
private:
  bool cutAfterSinks;
  Infoflow & infoflow;
  // InfoflowSolutions for each overflow, sigh :).
  llvm::DenseMap<const Value*, InfoflowSolution*> forward;
  InfoflowSolution *backward;
};

}

#endif /* SLICE_H_ */
