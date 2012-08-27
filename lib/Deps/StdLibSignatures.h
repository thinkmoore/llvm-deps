//===-- StdLibSignatures.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Support code for more readable StdLib summary information.
// (Only meant to be included from StdLibSignatures.cpp)
//
//===----------------------------------------------------------------------===//

#ifndef _STDLIB_SIGNATURES_H_
#define _STDLIB_SIGNATURES_H_

namespace deps {

// What part of the call are we talking about?
enum TSpecifier {
  Ret,
  Arg0,
  Arg1,
  Arg2,
  Arg3,
  Arg4,
  AllArgs,
  VarArgs
};

// Value-only, Direct-pointer, or all reachable?
enum TClass { V, D, R };

// Are we descring source or sink?
enum TEnd { T_Source, T_Sink };

// Struct combining these enums
struct TaintDecl {
  TSpecifier which;
  TClass what;
  TEnd end;
};
// Subclasses for source/sink distinction
struct Source : public TaintDecl {
  Source(TSpecifier TS, TClass TC=V) {
    which = TS; what = TC; end = T_Source;
  }
};
struct Sink : public TaintDecl {
  Sink(TSpecifier TS=Ret, TClass TC=V) {
    which = TS; what = TC; end = T_Sink;
  }
};

// Summary object: Name, Sources/Sinks
// This is what we want for each stdlib call
struct CallSummary {
  std::string Name;
  std::vector<TaintDecl> Sources;
  std::vector<TaintDecl> Sinks;

  // No flows
  CallSummary(std::string name) : Name(name) {}
  // Two args (need at least 1 source and 1 sink)
  CallSummary(std::string name, TaintDecl TD1, TaintDecl TD2) : Name(name) {
    process(TD1); process(TD2);
    verify();
  }
  // Three element description
  CallSummary(std::string name, TaintDecl TD1, TaintDecl TD2,
                   TaintDecl TD3) : Name(name) {
    process(TD1); process(TD2); process(TD3);
    verify();
  }
  // Four element description
  CallSummary(std::string name, TaintDecl TD1, TaintDecl TD2,
                   TaintDecl TD3, TaintDecl TD4) : Name(name) {
    process(TD1); process(TD2); process(TD3); process(TD4);
    verify();
  }

  // Add the description to the appropriate list
  void process(TaintDecl TD) {
    if (TD.end == T_Source)
      Sources.push_back(TD);
    else
      Sinks.push_back(TD);
  }

  // Quick verification that things are as they should be
  void verify() {
    assert(!Sources.empty() && "Must have a source!");
    assert(!Sinks.empty() && "Must have a sink!");
  }
};
}


#endif // _STDLIB_SIGNATURES_H_
