//===- FlowRecord.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines FlowRecords. A FlowRecord relates information flow
// sources to sinks and is a relatively concise way of encoding constraints
// (e.g. from a signature).
//
// There are four types of sources/sinks:
//  - Values: An actual llvm value
//  - DirectPtr: A memory location directly pointed to by a pointer.
//               Represented with the llvm pointer value itself.
//  - ReachPtr: All of the memory locations reachable by a pointer.
//              Represented with the llvm pointer value itself.
//  - Varg: The vararg list of a function
//===----------------------------------------------------------------------===//

#ifndef FLOWRECORD_H
#define FLOWRECORD_H

#include "CallContext.h"
#include "llvm/Value.h"
#include "llvm/ADT/SmallPtrSet.h"

#include <set>

namespace deps {

using namespace llvm;

/// A FlowRecord relates information flow sources to sinks. There are three
/// types of sources/sinks:
///  - Values: An actual llvm value
///  - DirectPtr: A memory location directly pointed to by a pointer.
///               Represented with the llvm pointer value itself.
///  - ReachPtr: All of the memory locations reachable by a pointer.
///              Represented with the llvm pointer value itself.
///  - Varg: ...
class FlowRecord {
public:
  typedef SmallPtrSet<const Value *,1> value_set;
  typedef value_set::const_iterator value_iterator;
  typedef SmallPtrSet<const Function *, 1> fun_set;
  typedef fun_set::const_iterator fun_iterator;

  FlowRecord() : implicit(false), sourceCtxt(DefaultID), sinkCtxt(DefaultID) { }
  FlowRecord(bool type) : implicit(type), sourceCtxt(DefaultID), sinkCtxt(DefaultID) { }
  FlowRecord(const ContextID source, const ContextID sink) :
    implicit(false), sourceCtxt(source), sinkCtxt(sink) { }
  FlowRecord(bool type, const ContextID source, const ContextID sink) :
    implicit(type), sourceCtxt(source), sinkCtxt(sink) { }

  bool isImplicit() const { return implicit; }

  ContextID sourceContext() const { return sourceCtxt; }
  ContextID sinkContext() const { return sinkCtxt; }

  void addSourceValue(const Value & V) { valueSources.insert(&V); }
  void addSourceDirectPtr(const Value & V) { directPtrSources.insert(&V); }
  void addSourceReachablePtr(const Value & V) { reachPtrSources.insert(&V); }

  void addSinkValue(const Value & V) { valueSinks.insert(&V); }
  void addSinkDirectPtr(const Value & V) { directPtrSinks.insert(&V); }
  void addSinkReachablePtr(const Value & V) { reachPtrSinks.insert(&V); }

  template <typename it>
  void addSourceValue(it begin, it end) { valueSources.insert(begin, end); }
  template <typename it>
  void addSourceDirectPtr(it begin, it end) { directPtrSources.insert(begin, end); }
  template <typename it>
  void addSourceReachablePtr(it begin, it end) { reachPtrSources.insert(begin, end); }

  template <typename it>
  void addSinkValue(it begin, it end) { valueSinks.insert(begin, end); }
  template <typename it>
  void addSinkDirectPtr(it begin, it end) { directPtrSinks.insert(begin, end); }
  template <typename it>
  void addSinkReachablePtr(it begin, it end) { reachPtrSinks.insert(begin, end); }

  void addSourceVarg(const Function & F) { vargSources.insert(&F); }
  void addSinkVarg(const Function & F) { vargSinks.insert(&F); }
  template <typename it>
  void addSourceVarg(it begin, it end) { vargSources.insert(begin, end); }
  template <typename it>
  void addSinkVarg(it begin, it end) { vargSinks.insert(begin, end); }

  bool valueIsSink(const Value & V) const { return valueSinks.count(&V); }
  bool vargIsSink(const Function & F) const { return vargSinks.count(&F); }
  bool directPtrIsSink(const Value & V) const { return directPtrSinks.count(&V); }
  bool reachPtrIsSink(const Value & V) const { return reachPtrSinks.count(&V); }

  value_iterator source_value_begin() const { return valueSources.begin(); }
  value_iterator source_value_end() const { return valueSources.end(); }
  value_iterator source_directptr_begin() const { return directPtrSources.begin(); }
  value_iterator source_directptr_end() const { return directPtrSources.end(); }
  value_iterator source_reachptr_begin() const { return reachPtrSources.begin(); }
  value_iterator source_reachptr_end() const { return reachPtrSources.end(); }
  fun_iterator source_varg_begin() const { return vargSources.begin(); }
  fun_iterator source_varg_end() const { return vargSources.end(); }

  value_iterator sink_value_begin() const { return valueSinks.begin(); }
  value_iterator sink_value_end() const { return valueSinks.end(); }
  value_iterator sink_directptr_begin() const { return directPtrSinks.begin(); }
  value_iterator sink_directptr_end() const { return directPtrSinks.end(); }
  value_iterator sink_reachptr_begin() const { return reachPtrSinks.begin(); }
  value_iterator sink_reachptr_end() const { return reachPtrSinks.end(); }
  fun_iterator sink_varg_begin() const { return vargSinks.begin(); }
  fun_iterator sink_varg_end() const { return vargSinks.end(); }

private:
  bool implicit;
  value_set valueSources, directPtrSources, reachPtrSources;
  value_set valueSinks, directPtrSinks, reachPtrSinks;
  fun_set vargSources, vargSinks;
  ContextID sourceCtxt;
  ContextID sinkCtxt;
};

}

#endif /* FLOWRECORD_H */
