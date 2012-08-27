//===- SourceSinkAnalysis.cpp ---------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Identify source and sink values in programs.
//
//===----------------------------------------------------------------------===//

#include "SourceSinkAnalysis.h"
#include "llvm/Function.h"
#include "llvm/Module.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Support/raw_ostream.h"
#include <string>

// For __cxa_demangle (demangling c++ function names)
// Requires libstdc++
#include <cxxabi.h>

using std::set;
using std::string;

namespace deps {

static RegisterPass<SourceSinkAnalysis>
X("sourcesinkanalysis", "Source and sink identification");

char SourceSinkAnalysis::ID;

bool SourceSinkAnalysis::runOnModule(Module &M) {
  for (Module::iterator fun = M.begin(), fend = M.end(); fun != fend; ++fun) {
    //errs() << "Adding sources and sinks from " << fun->getName() << "\n";

    std::set<const Value *> sources;
    std::set<const Value *> sinks;

    std::set<const Value *> directPtrSources;
    std::set<const Value *> directPtrSinks;

    std::set<const Value *> reachPtrSources;
    std::set<const Value *> reachPtrSinks;

    // Add the arguments to the function as sources if necessary (e.g. main)
    identifySourcesForFunction(*fun, sources, directPtrSources, reachPtrSources);
    // Add sources and sinks that result from instructions
    for (inst_iterator I = inst_begin(*fun), E = inst_end(*fun); I != E; ++I) {
      Instruction *inst = &*I;

      // XXX don't consider these to be sinks for performance reasons in jitflow
      /*
      if (LoadInst * load = dyn_cast<LoadInst>(inst)) {
	// The pointer operand of a load is a sensitive sink
        //sinks.insert(load->getPointerOperand());
      } else if (StoreInst * store = dyn_cast<StoreInst>(inst)) {
        // The pointer operand of a store is a sensitive sink
        sinks.insert(store->getPointerOperand());
      } else 
      */
      if (CallInst *call = dyn_cast<CallInst>(inst)) {
        // Calls may add sources or sinks (for example input returned
        // via a system call may be a source or the argument to a system
        // call may be a sink
	
        // Handle intrinsics here rather than defering to SourceSinkAnalysis
        if (const IntrinsicInst *intr = dyn_cast<IntrinsicInst>(call)) {
          switch(intr->getIntrinsicID()) {
          case Intrinsic::memcpy:
          case Intrinsic::memmove:
            // Destination pointer
            sinks.insert(intr->getArgOperand(0));
            // Source pointer
            sinks.insert(intr->getArgOperand(1));
            // Length
            sinks.insert(intr->getArgOperand(2));
            break;
          case Intrinsic::memset:
            // Destination pointer
            sinks.insert(intr->getArgOperand(0));
            // Value to set memory to
            sinks.insert(intr->getArgOperand(1));
            // Length
            sinks.insert(intr->getArgOperand(2));
            break;
          default:
            break;
          }
        } else {
          const CallSite cs(call);
          identifySourcesForCallSite(cs, sources, directPtrSources, reachPtrSources);
          identifySinksForCallSite(cs, sinks, directPtrSinks, reachPtrSinks);
        }
      } else if (const AllocaInst *ai = dyn_cast<AllocaInst>(inst)) {
        // The size operand of an alloca is a sensitive sink.
        if (!ai->isStaticAlloca())
          sinks.insert(ai->getArraySize());
      }
    }
    
    // Use the source and sink sets to generate constraints
    //errs() << "sources:\n";
    for (std::set<const Value *>::iterator value = sources.begin(), end = sources.end();
	  value != end; ++value) {
        sourcesAndSinks.addSourceValue(**value);
        //infoflow->setTainted("source", **value);
        //errs() << "\t" << **value << "\n";
    }
    //errs() << "direct sources:\n";
    for (std::set<const Value *>::iterator value = directPtrSources.begin(), end = directPtrSources.end();
	  value != end; ++value) {
        sourcesAndSinks.addSourceDirectPtr(**value);
        //infoflow->setDirectPtrTainted("source", **value);
        //errs() << "\t" << **value << "\n";
    }
    //errs() << "reachable sources:\n";
    for (std::set<const Value *>::iterator value = reachPtrSources.begin(), end = reachPtrSources.end();
          value != end; ++value) {
        sourcesAndSinks.addSourceReachablePtr(**value);
        //infoflow->setReachPtrTainted("source", **value);
        //errs() << "\t" << **value << "\n";
    }

    //errs() << "sinks:\n";
    for (std::set<const Value *>::iterator value = sinks.begin(), end = sinks.end();
          value != end; ++value) {
        sourcesAndSinks.addSinkValue(**value);
        //infoflow->setUntainted("sink", **value);
        //errs() << "\t" << **value << "\n";
    }
    //errs() << "direct sinks:\n";
    for (std::set<const Value *>::iterator value = directPtrSinks.begin(), end = directPtrSinks.end();
          value != end; ++value) {
        sourcesAndSinks.addSinkDirectPtr(**value);
        //infoflow->setDirectPtrUntainted("sink", **value);
        //errs() << "\t" << **value << "\n";
    }
    //errs() << "reachable sinks:\n";
    for (std::set<const Value *>::iterator value = reachPtrSinks.begin(), end = reachPtrSinks.end();
          value != end; ++value) {
        sourcesAndSinks.addSinkReachablePtr(**value);
        //infoflow->setReachPtrUntainted("sink", **value);
        //errs() << "\t" << **value << "\n";
    }
    //errs() << "\n";
  }

  return false;
}

const FlowRecord & SourceSinkAnalysis::getSourcesAndSinks() const {
  return sourcesAndSinks;
}

bool SourceSinkAnalysis::valueIsSink(const Value & v) const {
  return sourcesAndSinks.valueIsSink(v);
}

bool SourceSinkAnalysis::vargIsSink(const Function & f) const {
  return sourcesAndSinks.vargIsSink(f);
}

bool SourceSinkAnalysis::directPtrIsSink(const Value & v) const {
  return sourcesAndSinks.directPtrIsSink(v);
}

bool SourceSinkAnalysis::reachPtrIsSink(const Value & v) const {
  return sourcesAndSinks.reachPtrIsSink(v);
}


//
// Structure which records which arguments should be marked tainted.
//
typedef struct CallTaintSummary {
  static const unsigned NumArguments = 10;
  bool TaintsReturnValue;
  bool TaintsArgument[NumArguments];
  bool TaintsVarargArguments;
} CallTaintSummary;

//
// Holds an entry for a single function, summarizing which arguments are
// tainted values and which point to tainted memory.
//
typedef struct CallTaintEntry {
  const char *Name;
  CallTaintSummary ValueSummary;
  CallTaintSummary DirectPointerSummary;
  CallTaintSummary RootPointerSummary;
} CallTaintEntry;

#define TAINTS_NOTHING \
  { false, { }, false }
#define TAINTS_ALL_ARGS { \
    false, \
    { true, true, true, true, true, true, true, true, true, true }, \
    true \
  }
#define TAINTS_VARARGS \
  { false, { }, true }
#define TAINTS_RETURN_VAL \
  { true, { }, false }

#define TAINTS_ARG_1 \
  { false, { true }, false }
#define TAINTS_ARG_2 \
  { false, { false, true }, false }
#define TAINTS_ARG_3 \
  { false, { false, false, true }, false }
#define TAINTS_ARG_4 \
  { false, { false, false, false, true }, false }

#define TAINTS_ARG_1_2 \
  { false, { true, true, false }, false }
#define TAINTS_ARG_1_3 \
  { false, { true, false, true }, false }
#define TAINTS_ARG_1_4 \
  { false, { true, false, false, true }, false }

#define TAINTS_ARG_2_3 \
  { false, { false, true, true, false }, false }
#define TAINTS_ARG_3_4 \
  { false, { false, false, true, true }, false }

#define TAINTS_ARG_1_2_3 \
  { false, { true, true, true }, false }

#define TAINTS_ARG_1_AND_VARARGS \
  { false, { true }, true }
#define TAINTS_ARG_3_AND_RETURN_VAL \
  { true, { false, false, true }, false }

static const struct CallTaintEntry SourceTaintSummaries[] = {
  // function  tainted values   tainted direct memory tainted root ptrs
  { "fopen",   TAINTS_RETURN_VAL,  TAINTS_RETURN_VAL, TAINTS_NOTHING },
  { "freopen", TAINTS_RETURN_VAL,  TAINTS_ARG_3_AND_RETURN_VAL, TAINTS_NOTHING },
  { "fflush",  TAINTS_RETURN_VAL,  TAINTS_ARG_1,      TAINTS_NOTHING },
  { "fclose",  TAINTS_RETURN_VAL,  TAINTS_ARG_1,      TAINTS_NOTHING },
  { "setbuf",  TAINTS_NOTHING,     TAINTS_ARG_1_2,    TAINTS_NOTHING },
  { "setvbuf", TAINTS_RETURN_VAL,  TAINTS_ARG_1_2,    TAINTS_NOTHING },
  { "fread",   TAINTS_RETURN_VAL,  TAINTS_ARG_1_4,    TAINTS_NOTHING },
  { "fwrite",  TAINTS_RETURN_VAL,  TAINTS_ARG_4,      TAINTS_NOTHING },
  { "fgetc",   TAINTS_RETURN_VAL,  TAINTS_ARG_1,      TAINTS_NOTHING },
  { "getc",    TAINTS_RETURN_VAL,  TAINTS_ARG_1,      TAINTS_NOTHING },
  { "fgets",   TAINTS_RETURN_VAL,  TAINTS_ARG_1_3,    TAINTS_NOTHING },
  { "fputc",   TAINTS_RETURN_VAL,  TAINTS_ARG_2,      TAINTS_NOTHING }, 
  { "putc",    TAINTS_RETURN_VAL,  TAINTS_ARG_2,      TAINTS_NOTHING },
  { "fputs",   TAINTS_RETURN_VAL,  TAINTS_ARG_1_2,    TAINTS_NOTHING },
  { "getchar", TAINTS_RETURN_VAL,  TAINTS_NOTHING,    TAINTS_NOTHING },
  { "gets",    TAINTS_RETURN_VAL,  TAINTS_ARG_1,      TAINTS_NOTHING },
  { "putchar", TAINTS_RETURN_VAL,  TAINTS_NOTHING,    TAINTS_NOTHING },
  { "puts",    TAINTS_RETURN_VAL,  TAINTS_NOTHING,    TAINTS_NOTHING },
  { "ungetc",  TAINTS_RETURN_VAL,  TAINTS_ARG_2,      TAINTS_NOTHING },
  { "printf",  TAINTS_RETURN_VAL,  TAINTS_VARARGS,    TAINTS_NOTHING },
  { "fprintf", TAINTS_RETURN_VAL,  TAINTS_ARG_1_AND_VARARGS, TAINTS_NOTHING },
  { "scanf",   TAINTS_RETURN_VAL,  TAINTS_VARARGS,    TAINTS_NOTHING },
  { "fscanf",  TAINTS_RETURN_VAL,  TAINTS_ARG_1_AND_VARARGS, TAINTS_NOTHING },
  { "vscanf",  TAINTS_RETURN_VAL,  TAINTS_ARG_2,      TAINTS_NOTHING },
  { "vfscanf", TAINTS_RETURN_VAL,  TAINTS_ARG_1_3,    TAINTS_NOTHING },
  { "vprintf", TAINTS_RETURN_VAL,  TAINTS_ARG_2,      TAINTS_NOTHING },
  { "vfprintf",TAINTS_RETURN_VAL,  TAINTS_ARG_1_3,    TAINTS_NOTHING },
  { "ftell",   TAINTS_RETURN_VAL,  TAINTS_NOTHING,    TAINTS_NOTHING },
  { "feof",    TAINTS_RETURN_VAL,  TAINTS_NOTHING,    TAINTS_NOTHING },
  { "ferror",  TAINTS_RETURN_VAL,  TAINTS_NOTHING,    TAINTS_NOTHING },
  { "remove",  TAINTS_RETURN_VAL,  TAINTS_NOTHING,    TAINTS_NOTHING },
  { "fgetpos", TAINTS_RETURN_VAL,  TAINTS_ARG_1_2,    TAINTS_NOTHING },
  { "fseek",   TAINTS_RETURN_VAL,  TAINTS_ARG_1,      TAINTS_NOTHING },
  { "fsetpos", TAINTS_RETURN_VAL,  TAINTS_ARG_1,      TAINTS_NOTHING },
  { "rewind",  TAINTS_NOTHING,     TAINTS_ARG_1,      TAINTS_NOTHING },
  { "clearerr",TAINTS_NOTHING,     TAINTS_ARG_1,      TAINTS_NOTHING },
  { "perror",  TAINTS_NOTHING,     TAINTS_NOTHING,    TAINTS_NOTHING },
  { "rename",  TAINTS_RETURN_VAL,  TAINTS_NOTHING,    TAINTS_NOTHING },
  { "tmpfile", TAINTS_RETURN_VAL,  TAINTS_RETURN_VAL, TAINTS_NOTHING },
  { "tmpnam",  TAINTS_RETURN_VAL,  TAINTS_ARG_1,      TAINTS_NOTHING },
  { "getenv",  TAINTS_RETURN_VAL,  TAINTS_RETURN_VAL, TAINTS_NOTHING },
  { 0,         TAINTS_NOTHING,     TAINTS_NOTHING,    TAINTS_NOTHING }
};

static const struct CallTaintEntry SinkTaintSummaries[] = {
#if 0
  // function  tainted values   tainted direct memory tainted root ptrs
  { "fopen",   TAINTS_NOTHING,     TAINTS_ARG_1_2,    TAINTS_NOTHING },
  { "freopen", TAINTS_NOTHING,     TAINTS_ARG_1_2_3,  TAINTS_NOTHING },
  { "fflush",  TAINTS_NOTHING,     TAINTS_ARG_1,      TAINTS_NOTHING },
  { "fclose",  TAINTS_NOTHING,     TAINTS_ARG_1,      TAINTS_NOTHING },
  { "setbuf",  TAINTS_NOTHING,     TAINTS_ARG_1_2,    TAINTS_NOTHING },
  { "setvbuf", TAINTS_ARG_3_4,     TAINTS_ARG_1_2,    TAINTS_NOTHING },
  { "fread",   TAINTS_ARG_2_3,     TAINTS_ARG_4,      TAINTS_NOTHING },
  { "fwrite",  TAINTS_ARG_2_3,     TAINTS_ARG_1_4,    TAINTS_NOTHING },
  { "fgetc",   TAINTS_NOTHING,     TAINTS_ARG_1,      TAINTS_NOTHING },
  { "getc",    TAINTS_NOTHING,     TAINTS_ARG_1,      TAINTS_NOTHING },
  { "fgets",   TAINTS_ARG_2,       TAINTS_ARG_3,      TAINTS_NOTHING },
  { "fputc",   TAINTS_ARG_1,       TAINTS_ARG_2,      TAINTS_NOTHING },
  { "putc",    TAINTS_ARG_1,       TAINTS_ARG_2,      TAINTS_NOTHING },
  { "fputs",   TAINTS_NOTHING,     TAINTS_ARG_1_2,    TAINTS_NOTHING },
  { "getchar", TAINTS_NOTHING,     TAINTS_NOTHING,    TAINTS_NOTHING },
  { "gets",    TAINTS_NOTHING,     TAINTS_NOTHING,    TAINTS_NOTHING },
  { "putchar", TAINTS_ARG_1,       TAINTS_NOTHING,    TAINTS_NOTHING },
  { "puts",    TAINTS_NOTHING,     TAINTS_ARG_1,      TAINTS_NOTHING },
  { "ungetc",  TAINTS_ARG_1,       TAINTS_ARG_2,      TAINTS_NOTHING },
  { "printf",  TAINTS_VARARGS,     TAINTS_ALL_ARGS,   TAINTS_NOTHING },
  { "fprintf", TAINTS_VARARGS,     TAINTS_ALL_ARGS,   TAINTS_NOTHING },
  { "scanf",   TAINTS_NOTHING,     TAINTS_ARG_1,      TAINTS_NOTHING },
  { "fscanf",  TAINTS_NOTHING,     TAINTS_ARG_1_2,    TAINTS_NOTHING },
  { "vscanf",  TAINTS_NOTHING,     TAINTS_ARG_1,      TAINTS_NOTHING },
  { "vfscanf", TAINTS_NOTHING,     TAINTS_ARG_1_2,    TAINTS_NOTHING },
  { "vprintf", TAINTS_NOTHING,     TAINTS_ALL_ARGS,   TAINTS_NOTHING },
  { "vfprintf",TAINTS_NOTHING,     TAINTS_ALL_ARGS,   TAINTS_NOTHING },
  { "ftell",   TAINTS_NOTHING,     TAINTS_ARG_1,      TAINTS_NOTHING },
  { "feof",    TAINTS_NOTHING,     TAINTS_ARG_1,      TAINTS_NOTHING },
  { "ferror",  TAINTS_NOTHING,     TAINTS_ARG_1,      TAINTS_NOTHING },
  { "remove",  TAINTS_NOTHING,     TAINTS_ARG_1,      TAINTS_NOTHING },
  { "fgetpos", TAINTS_NOTHING,     TAINTS_ARG_1,      TAINTS_NOTHING },
  { "fseek",   TAINTS_ARG_2_3,     TAINTS_ARG_1,      TAINTS_NOTHING },
  { "fsetpos", TAINTS_NOTHING,     TAINTS_ARG_1_2,    TAINTS_NOTHING },
  { "rewind",  TAINTS_NOTHING,     TAINTS_ARG_1,      TAINTS_NOTHING },
  { "clearerr",TAINTS_NOTHING,     TAINTS_ARG_1,      TAINTS_NOTHING },
  { "perror",  TAINTS_NOTHING,     TAINTS_ARG_1,      TAINTS_NOTHING },
  { "rename",  TAINTS_NOTHING,     TAINTS_ARG_1_2,    TAINTS_NOTHING },
  { "tmpfile", TAINTS_NOTHING,     TAINTS_NOTHING,    TAINTS_NOTHING },
  { "tmpnam",  TAINTS_NOTHING,     TAINTS_NOTHING,    TAINTS_NOTHING },
  { "malloc",  TAINTS_ARG_1,       TAINTS_NOTHING,    TAINTS_NOTHING },
  { "calloc",  TAINTS_ARG_1_2,     TAINTS_NOTHING,    TAINTS_NOTHING },
  { "realloc", TAINTS_ARG_2,       TAINTS_ARG_1,      TAINTS_NOTHING },
  { "system",  TAINTS_NOTHING,     TAINTS_ARG_1,      TAINTS_NOTHING },
  { "strdup",  TAINTS_NOTHING,     TAINTS_ARG_1,      TAINTS_NOTHING },
  { "____jf_return_arg", TAINTS_NOTHING, TAINTS_NOTHING, TAINTS_NOTHING },
#else
  { "system",  TAINTS_ALL_ARGS,    TAINTS_ARG_1,      TAINTS_NOTHING },

  { "exec",    TAINTS_ALL_ARGS,    TAINTS_ALL_ARGS,   TAINTS_NOTHING },
  { "execlp",  TAINTS_ALL_ARGS,    TAINTS_ALL_ARGS,   TAINTS_NOTHING },
  { "execle",  TAINTS_ALL_ARGS,    TAINTS_ALL_ARGS,   TAINTS_NOTHING },
  { "execv",   TAINTS_ALL_ARGS,    TAINTS_ALL_ARGS,   TAINTS_NOTHING },
  { "execvp",  TAINTS_ALL_ARGS,    TAINTS_ALL_ARGS,   TAINTS_NOTHING },
  { "execvpe", TAINTS_ALL_ARGS,    TAINTS_ALL_ARGS,   TAINTS_NOTHING },

  { "malloc",  TAINTS_ARG_1,       TAINTS_NOTHING,    TAINTS_NOTHING },
  { "calloc",  TAINTS_ARG_1_2,     TAINTS_NOTHING,    TAINTS_NOTHING },
  { "realloc", TAINTS_ARG_2,       TAINTS_ARG_1,      TAINTS_NOTHING },

  { "remove",  TAINTS_ALL_ARGS,    TAINTS_ARG_1,      TAINTS_NOTHING },
  { "unlink",  TAINTS_ALL_ARGS,    TAINTS_ARG_1,      TAINTS_NOTHING },

  { 0,         TAINTS_NOTHING,     TAINTS_NOTHING,    TAINTS_NOTHING }
#endif
};
CallTaintEntry nothing = { 0, TAINTS_NOTHING, TAINTS_NOTHING, TAINTS_NOTHING };

//
// Search the table of external function information for the function of the
// given name.
//
static const CallTaintEntry *
findEntryForFunction(const CallTaintEntry *Summaries,
                     const string &FuncName) {
  unsigned Index;

  if (StringRef(FuncName).startswith("____jf_check"))
    return &nothing;

  for (Index = 0; Summaries[Index].Name; ++Index) {
    if (Summaries[Index].Name == FuncName)
      return &Summaries[Index];
  }
  // Return the default summary.
  return &Summaries[Index];
}

//
// Add into the value set all values from the call site specified by the taint
// summary.
//
static void
determineTaintedValues(const CallTaintSummary *Summary,
                       const CallSite &CS,
                       set<const Value *> &S) {
  const Value *Callee = CS.getCalledValue();
  FunctionType *CalleeType =
    dyn_cast<FunctionType>(
      dyn_cast<PointerType>(Callee->getType())->getElementType()
    );

  // Add return value if it is tainted.
  if (Summary->TaintsReturnValue)
    S.insert(CS.getInstruction());

  // Add all tainted arguments.
  for (unsigned ArgIndex = 0; ArgIndex < Summary->NumArguments; ++ArgIndex) {
    if (Summary->TaintsArgument[ArgIndex] && ArgIndex < CS.arg_size())
      S.insert(CS.getArgument(ArgIndex));
  }

  // Add the vararg arguments if they are tainted.
  if (Summary->TaintsVarargArguments) {
    unsigned NumArgs = CS.arg_size(), VarArgIndex = CalleeType->getNumParams();

    for (; VarArgIndex < NumArgs; VarArgIndex++)
      S.insert(CS.getArgument(VarArgIndex));
  }
}

//
// Remove the non-pointer type values from the set.
//
static void filterOutNonPointers(set<const Value *> &S) {
  set<const Value *>::iterator SetIt = S.begin(), SetItEnd = S.end();
  while (SetIt != SetItEnd) {
    if (!isa<PointerType>((*SetIt)->getType()))
      S.erase(SetIt++);
    else
      SetIt++;
  }
}

//
// Given a call site and a list of entries in a taint table, fill the
// TaintedValues argument with all values that are tainted according to the
// table, and fill the TaintedRootPointers with all pointers which reach
// tainted memory according to the table.
//
static void identifyTaintForCallSite(
  const CallSite &CS,
  const CallTaintEntry *EntryList,
  set<const Value *> &TaintedValues,
  set<const Value *> &TaintedDirectPointers,
  set<const Value *> &TaintedRootPointers
) {
  Function *CalledFunction = CS.getCalledFunction();

  // Only determine taint for external functions.
  if (CalledFunction && !CalledFunction->empty())
    return;

  // Get the name of the callee.
  string FunctionName =
    CalledFunction != 0 ? CalledFunction->getName().str() : "";

  // Get the entry for the function in the source taint table.
  const CallTaintEntry *Entry;
  Entry = findEntryForFunction(EntryList, FunctionName);


  // Determine the directly tainted sources.
  determineTaintedValues(&Entry->ValueSummary, CS, TaintedValues);

  // Determine the pointers only whose directly reachable memory is tainted.
  set<const Value *> TaintedDirectMemoryValues;
  determineTaintedValues(
    &Entry->DirectPointerSummary, CS, TaintedDirectMemoryValues
  );

  // determineTaintedValues() might return non-pointer entries, so filter
  // those out.
  filterOutNonPointers(TaintedDirectMemoryValues);

  // Insert the computed direct memory taint pointers into the argument.
  TaintedDirectPointers.insert(
    TaintedDirectMemoryValues.begin(), TaintedDirectMemoryValues.end()
  );


  // Determine the tainted root pointer sources.
  set<const Value *> TaintedRootValues;
  determineTaintedValues(&Entry->RootPointerSummary, CS, TaintedRootValues);

  // determineTaintedValues() might return non-pointer entries, so filter
  // those out.
  filterOutNonPointers(TaintedRootValues);

  // Insert the computed tainted root pointer values into the argument.
  TaintedRootPointers.insert(
    TaintedRootValues.begin(), TaintedRootValues.end()
  );
}

// checkCXXSinks: Attempt to demangle and match the callee's name,
// and if we succeed add all arguments as value and direct memory.
// Please forgive the mess...
bool checkCXXSinks(
  const CallSite &CS,
  set<const Value *> &TaintedValues,
  set<const Value *> &TaintedDirectPointers,
  set<const Value *> &TaintedRootPointers) {
  const Function *F = CS.getCalledFunction();
  if (!F) return false;
  StringRef Name = F->getName();

  // Check C++ names
  size_t len = 0;
  int status;
  char* demangled = abi::__cxa_demangle(Name.str().c_str(), NULL, &len, &status);
  if (!status) {
    StringRef Demangled(demangled);
    // Pick up common allocation/free/output logics
    // TODO: Refactor and add support for cin/etc as sources?
#if 0
    if (Demangled.startswith("std::ios_base") ||
        Demangled.startswith("operator new") ||
        Demangled.startswith("operator delete") ||
        Demangled.startswith("std::basic_ostream") ||
        Demangled.startswith("std::ostream")) {
#else
    if (
        Demangled.startswith("operator new") ||
        Demangled.startswith("operator delete")) {
#endif
      TaintedValues.insert(CS.arg_begin(), CS.arg_end());
      std::set<const Value*> PtrArgs(CS.arg_begin(), CS.arg_end());
      filterOutNonPointers(PtrArgs);
      TaintedDirectPointers.insert(PtrArgs.begin(), PtrArgs.end());
      free(demangled);
      return true;
    }
  }

  free(demangled);

  return false;
}

void SourceSinkAnalysis::identifySourcesForCallSite(
  const CallSite &CS,
  set<const Value *> &TaintedValues,
  set<const Value *> &TaintedDirectPointers,
  set<const Value *> &TaintedRootPointers
) {
  identifyTaintForCallSite(
    CS,
    SourceTaintSummaries,
    TaintedValues,
    TaintedDirectPointers,
    TaintedRootPointers
  );
}

void SourceSinkAnalysis::identifySinksForCallSite(
  const CallSite &CS,
  set<const Value *> &TaintedValues,
  set<const Value *> &TaintedDirectPointers,
  set<const Value *> &TaintedRootPointers
) {

  // If we recognize this as a C++-specific sink, we're done
  if (checkCXXSinks(CS, TaintedValues, TaintedDirectPointers, TaintedRootPointers))
    return;

  // Otherwise, check our function summary table
  identifyTaintForCallSite(
    CS,
    SinkTaintSummaries,
    TaintedValues,
    TaintedDirectPointers,
    TaintedRootPointers
  );
}

void SourceSinkAnalysis::identifySourcesForFunction(
  const Function &F,
  set<const Value *> &TaintedValues,
  set<const Value *> &TaintedDirectPointers,
  set<const Value *> &TaintedRootPointers
) {
  if (F.getName().str() != "main")
    return;

  // Taint all arguments, and taint the reachable memory of all pointers.
  Function::const_arg_iterator ArgIt = F.arg_begin(), ArgItEnd = F.arg_end();
  for (; ArgIt != ArgItEnd; ++ArgIt) {
    if (isa<PointerType>(ArgIt->getType()))
      TaintedRootPointers.insert(&*ArgIt);
    TaintedValues.insert(&*ArgIt);
  }
}

}
