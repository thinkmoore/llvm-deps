//===-- MTSolve.cpp ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Solve for greatest and least using two threads.
//
//===----------------------------------------------------------------------===//

#include "Infoflow.h"
#include "Constraints/LHConstraintKit.h"
#include "Constraints/LHConstraints.h"
#include "Constraints/PartialSolution.h"
#include "Constraints/SolverThread.h"

#include "llvm/Support/Threading.h"
#include "llvm/Support/Atomic.h"
#include <cassert>

using namespace llvm;

namespace deps {

SolverThread* SolverThread::spawn(Constraints &C, bool greatest) {
  SolverThread *T = new SolverThread(C, greatest);

  if (::pthread_create(&T->thread, NULL, solve, T)) {
    assert(0 && "Failed to create thread?!");
  }

  return T;
}

void* SolverThread::solve(void* arg) {
  assert(arg);
  SolverThread *T = (SolverThread*)arg;
  PartialSolution *P = new PartialSolution(T->C, T->greatest);
  return (void*)P;
}

void SolverThread::join(PartialSolution*& P) {
  ::pthread_join(thread, (void**)&P);
}

void LHConstraintKit::solveMT(std::string kind) {
  assert(lockedConstraintKinds.insert(kind).second && "Already solved");
  assert(!leastSolutions.count(kind));
  assert(!greatestSolutions.count(kind));

  PartialSolution*& G = greatestSolutions[kind];
  PartialSolution*& L = leastSolutions[kind];

  Constraints &C = getOrCreateConstraintSet(kind);

  SolverThread *TG = SolverThread::spawn(C, true);
  SolverThread *TL = SolverThread::spawn(C, false);

  TG->join(G);
  TL->join(L);

  delete TG;
  delete TL;

  assert(leastSolutions.count(kind));
  assert(greatestSolutions.count(kind));

  // Cleanup
  freeUnneededConstraints(kind);
}


struct MergeInfo {
  PartialSolution * Default;
  PartialSolution * DefaultSinks;
  bool useDefaultSinks;
  std::vector<PartialSolution*> Mergees;
};

void* merge(void *arg) {
  MergeInfo *MI = (MergeInfo*)arg;
  for (std::vector<PartialSolution*>::iterator I = MI->Mergees.begin(),
	 E = MI->Mergees.end(); I != E; ++I) {
    (*I)->mergeIn(*MI->Default);
    if (MI->useDefaultSinks) {
      (*I)->mergeIn(*MI->DefaultSinks);
    }
  }
  return NULL;
}

std::vector<PartialSolution*>
LHConstraintKit::solveLeastMT(std::vector<std::string> kinds, bool useDefaultSinks) {
  assert(leastSolutions.count("default"));

  PartialSolution *P = leastSolutions["default"];
  PartialSolution *DS = leastSolutions["default-sinks"];
  std::vector<PartialSolution*> ToMerge;
  for (std::vector<std::string>::iterator kind = kinds.begin(), end = kinds.end();
       kind != end; ++kind) {
    assert(lockedConstraintKinds.insert(*kind).second && "Already solved");
    assert(!leastSolutions.count(*kind));
    leastSolutions[*kind] = new PartialSolution(getOrCreateConstraintSet(*kind), false);
    ToMerge.push_back(new PartialSolution(*leastSolutions[*kind]));
  }

  // Make copy of the set of solutions for returning when we're done
  std::vector<PartialSolution*> Merged(ToMerge);

  const unsigned T = 16;
  // Now kick off up to 'T' jobs, each with a vector of merges to do
  MergeInfo MI[T];

  // Make sure all threads know the default solution:
  for (unsigned i = 0; i < T; ++i) {
    MI[i].Default = P;
    MI[i].DefaultSinks = DS;
    MI[i].useDefaultSinks = useDefaultSinks;
  }

  // And round-robin hand out merge jobs:
  unsigned TID = 0;
  while (!ToMerge.empty()) {
    MergeInfo* M = &MI[TID];
    TID = (TID + 1) % T;

    M->Mergees.push_back(ToMerge.back());
    ToMerge.pop_back();
  }

  // Finally, kick off all the threads
  // with non-empty work queues
  std::vector<pthread_t> Threads;
  for (unsigned i = 0; i < T; ++i) {
    MergeInfo *M = &MI[i];
    if (!M->Mergees.empty()) {
      pthread_t Thread;
      if (::pthread_create(&Thread, NULL, merge, M)) {
        assert(0 && "Failed to create thread!");
      }
      Threads.push_back(Thread);
    }
  }

  // Wait for them all to finish
  while (!Threads.empty()) {
    ::pthread_join(Threads.back(), NULL);
    Threads.pop_back();
  }

  return Merged;
}

std::vector<InfoflowSolution*>
Infoflow::solveLeastMT(std::vector<std::string> kinds, bool useDefaultSinks) {
  std::vector<PartialSolution*> PS = kit->solveLeastMT(kinds, useDefaultSinks);

  std::vector<InfoflowSolution*> Solns;
  for (std::vector<PartialSolution*>::iterator I = PS.begin(), E = PS.end();
       I != E; ++I) {
    Solns.push_back(new InfoflowSolution(*this,
                                         *I,
                                         kit->highConstant(),
                                         false, /* default to untainted */
                                         summarySinkValueConstraintMap,
                                         locConstraintMap,
                                         summarySinkVargConstraintMap));
  }

  return Solns;
}

} // end namespace deps
