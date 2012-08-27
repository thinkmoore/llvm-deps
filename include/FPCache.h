//===-- FPCache.h - Cache functionPass results ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// For some reason, FunctionPass results don't seem to be properly cached
// when requesting them multiple times from a ModulePass.
//
// This is a simple wrapper pass that explicitly caches them for us.
//
//===----------------------------------------------------------------------===//
#ifndef _FPCACHE_H_
#define _FPCACHE_H_

#include "llvm/Pass.h"
#include "llvm/Module.h"
#include "llvm/ADT/DenseMap.h"

using namespace llvm;

namespace deps {

template <class FP>
class FPCache : public ModulePass {
  DenseMap<const Function*,FP*> Cache;
protected:
  FPCache(char & ID) : ModulePass(ID) {}
public:
  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<FP>();
    AU.setPreservesAll();
  }
  virtual const char * getPassName() const { return "FunctionPass Cache"; }

  // When the pass is run, get results for all functions.
  virtual bool runOnModule(Module &M) {
    releaseMemory();
    for(Module::iterator I = M.begin(), E = M.end(); I != E; ++I) {
      if (!I->isDeclaration())
        Cache[I] = &getAnalysis<FP>(*I);
    }
    return false;
  }

  // Cache accessor
  FP & get(const Function *F) const {
    typename DenseMap<const Function*, FP*>::const_iterator I = Cache.find(F);
    assert((I != Cache.end()) && "Function not in cache!");
    return *I->second;
  }

  // Clear cache when pass is invalidated
  virtual void releaseMemory() {
    Cache.clear();
  }

};


} // end namespace deps

#endif // _FPCACHE_H_
