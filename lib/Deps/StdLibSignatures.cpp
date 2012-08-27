//===-- StdLibSignatures.cpp ----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a library of information flow signatures for
// common stdlib functions as well as a few other common runtime calls.
//
//===----------------------------------------------------------------------===//

#include "SignatureLibrary.h"

// Support code that makes the table descriptions work
#include "StdLibSignatures.h"

#include "FlowRecord.h"

#include "llvm/Function.h"
#include "llvm/Support/CallSite.h"

namespace deps {

// typdef for more compact table writing
typedef CallSummary C;

// Descriptions of various StdLib calls
// TODO: Review these again, carefully, and remove this line :)
static const C CallTable[] = {
//===-- Allocation --------------------------------------------------------===//
  C("calloc", Source(AllArgs), Sink(Ret, V), Sink(Ret, D)),
  C("free"),
  C("malloc", Source(AllArgs), Sink(Ret, V), Sink(Ret, D)),
  C("realloc", Source(AllArgs, D), Sink(AllArgs, D), Sink(Ret, D)),
//===-- I/O ---------------------------------------------------------------===//
  C("_IO_getc", Source(AllArgs), Sink(Ret)),
  C("_IO_putc", Source(AllArgs), Sink(Ret)),
  C("close", Source(AllArgs), Sink(Ret)),
  C("fclose", Source(AllArgs), Sink(Ret)),
  C("feof", Source(AllArgs), Sink(Ret)),
  C("ferror", Source(AllArgs), Sink(Ret)),
  C("fflush", Source(Arg0), Sink(Ret)),
  C("fgetc", Source(AllArgs), Sink(Ret)),
  C("fileno", Source(AllArgs), Sink(Ret)),
  C("fopen", Source(AllArgs, D), Source(AllArgs, V), Sink(Ret)),
  C("fprintf", Source(AllArgs, D), Source(AllArgs), Sink(Ret)),
  C("fputc", Source(AllArgs), Sink(Ret)),
  C("fputs", Source(AllArgs, D), Source(AllArgs), Sink(Ret)),
  C("fread", Source(AllArgs), Sink(Ret), Sink(Arg0, D)),
  C("fwrite", Source(Arg0, D), Source(AllArgs), Sink(Ret)),
  C("getc", Source(AllArgs), Sink(Ret)),
  C("open", Source(AllArgs), Sink(Ret)),
  C("printf", Source(AllArgs, D), Source(AllArgs), Sink(Ret)),
  C("putc", Source(AllArgs), Sink(Ret)),
  C("putchar", Source(AllArgs), Sink(Ret)),
  C("puts", Source(Arg0, D), Sink(Ret)),
  C("read", Source(AllArgs), Sink(Ret), Sink(Arg1, D)),
  C("ungetc", Source(AllArgs), Sink(Ret)),
  C("vprintf", Source(AllArgs), Source(AllArgs, D), Sink(Ret)),

  // TODO:
  C("fseek"),
  C("ftell"),
  C("lseek"),
  C("write"),
//===-- String/Memory -----------------------------------------------------===//
  C("strlen",   Source(AllArgs, D), Sink(Ret)),
  C("strcpy",   Source(Arg1, D), Sink(Arg0, D)),
  C("strcmp",   Source(AllArgs, D), Sink(Ret)),
  C("strncmp",  Source(AllArgs, D), Sink(Ret)),
  C("strchr",   Source(AllArgs, D), Sink(Ret)),
  C("strrchr",  Source(AllArgs, D), Sink(Ret)),
  C("memchr",   Source(AllArgs, D), Sink(Ret)),
  C("sprintf",  Source(AllArgs, D), Sink(Arg0, R), Sink(Ret)),
  C("snprintf", Source(AllArgs, D), Sink(Arg0, R), Sink(Ret)),
  C("strtod",   Source(AllArgs, D), Sink(Arg1, R)),
  // TODO:
  C("atof"),
  C("memcmp"),
  C("strcat"),
  C("strcspn"),
  C("strerror"),
  C("strncat"),
  C("strncpy"),
  C("strpbrk"),
  C("strspn"),
  C("strstr"),
  C("strtok"),
  C("strtol"),
  C("strtoul"),
  C("vsprintf"),
//===-- System ------------------------------------------------------------===//
  C("abort"),
  C("clock"),
  C("exit"),
  C("_exit"),
  C("fork"),
  C("signal"),
  C("unlink"),
  C("time"),

  // TODO:
  C("getcwd"),
  C("getenv"),
  C("getpagesize"),
  C("getpwd"),
  C("localtime"),
  C("strftime"),
//===-- Math --------------------------------------------------------------===//
  C("ceil",  Source(AllArgs), Sink(Ret)),
  C("cos",   Source(AllArgs), Sink(Ret)),
  C("exp",   Source(AllArgs), Sink(Ret)),
  C("floor", Source(AllArgs), Sink(Ret)),
  C("log",   Source(AllArgs), Sink(Ret)),
  C("pow",   Source(AllArgs), Sink(Ret)),
  C("powf",  Source(AllArgs), Sink(Ret)),
  C("sin",   Source(AllArgs), Sink(Ret)),
  C("sqrt",  Source(AllArgs), Sink(Ret)),
  C("tan",   Source(AllArgs), Sink(Ret)),

  // TODO:
  C("exp2"),
  C("fabs"),
  C("ldexp"),
  C("log10"),
//===-- Misc --------------------------------------------------------------===//
  C("__errno_location"),
  C("qsort"),
  C("____jf_return_arg", Source(AllArgs), Sink(Ret)),
//===-- TODO/Unsorted -----------------------------------------------------===//

  // Common C++ stuff (some, we'll probably want more)
  C("_ZNSo3putEc"),
  C("_ZNSo5flushEv"),
  C("_ZNSolsEi"),
  C("_ZSt17__throw_bad_allocv"),
  C("_ZSt9terminatev"),
  C("_ZdaPv"),
  C("_Znam"),
  C("__cxa_allocate_exception"),
  C("__cxa_begin_catch"),
  C("__cxa_end_catch"),
  C("__cxa_free_exception"),
  C("__cxa_throw"),

  // Not sure
  C("__isoc99_fscanf"),
  C("__ctype_b_loc"),

  // Exceptions
  C("_setjmp"),
  C("longjmp"),
  C("setjmp"),
//===-- End-of-list Sentinel ----------------------------------------------===//
  C("") // Sentinel
};

// Compare summaries by their name
static bool NameCompare(const CallSummary *LHS, const CallSummary *RHS) {
  return LHS->Name < RHS->Name;
}
// Same, but used for searching to find entry matching given StringRef name
static bool NameSearch(const CallSummary *LHS, const StringRef RHS) {
  return LHS->Name < RHS;
}

void
StdLib::initCalls() {
  // Store and sort pointers to the summaries for faster/easier searching
  unsigned i = 0;
  while(!CallTable[i].Name.empty())
    Calls.push_back(&CallTable[i++]);

  // Sort so we can use binary search to find
  std::sort(Calls.begin(), Calls.end(), NameCompare);
}

// Helper to locate a matching entry, if any
bool
StdLib::findEntry(const ImmutableCallSite cs, const CallSummary *& S) const {
  const Function *F = cs.getCalledFunction();
  if (!F) return false;

  StringRef Name = F->getName();

  std::vector<const CallSummary*>::const_iterator I =
    std::lower_bound(Calls.begin(), Calls.end(), Name, NameSearch);

  if (I == Calls.end() || (*I)->Name != Name) return false;

  // Found it! Set output argument and return
  S = *I;
  return true;
}

// Helper to find the set of values described by a TSpecifier
std::set<const Value*>
getValues(const ImmutableCallSite cs, TSpecifier TS) {
  std::set<const Value*> Values;
  switch (TS) {
    case Ret:
      assert(!cs.getInstruction()->getType()->isVoidTy());
      Values.insert(cs.getInstruction());
      break;
    case Arg0:
      assert(0 < cs.arg_size());
      Values.insert(cs.getArgument(0));
      break;
    case Arg1:
      assert(1 < cs.arg_size());
      Values.insert(cs.getArgument(1));
      break;
    case Arg2:
      assert(2 < cs.arg_size());
      Values.insert(cs.getArgument(2));
      break;
    case Arg3:
      assert(3 < cs.arg_size());
      Values.insert(cs.getArgument(3));
      break;
    case Arg4:
      assert(4 < cs.arg_size());
      Values.insert(cs.getArgument(4));
      break;
    case AllArgs:
      assert(!cs.arg_empty());
      for (unsigned i = 0; i < cs.arg_size(); ++i)
        Values.insert(cs.getArgument(i));
      break;
    case VarArgs: {
      const Value *Callee = cs.getCalledValue()->stripPointerCasts();
      FunctionType *CalleeType =
        dyn_cast<FunctionType>(
          dyn_cast<PointerType>(Callee->getType())->getElementType()
          );
      for (unsigned i = CalleeType->getNumParams(); i < cs.arg_size(); ++i)
        Values.insert(cs.getArgument(i));
      break;
    }
  }
  return Values;
}

bool
StdLib::accept(const ContextID ctxt, const ImmutableCallSite cs) const {
  const CallSummary *S;
  return findEntry(cs, S);
}

std::vector<FlowRecord>
StdLib::process(const ContextID ctxt, const ImmutableCallSite cs) const {
  const CallSummary *S;
  bool found = findEntry(cs, S);
  assert(found);

  std::vector<FlowRecord> flows;

  // If there are no flows for this call, return empty vector
  // Similarly if the call has no arguments we already know
  // it has no sources so just return empty vector.
  if (S->Sources.empty() || cs.arg_empty()) {
    return flows;
  }

  // Otherwise, build up a flow record for this summary
  FlowRecord flow(ctxt, ctxt);

  // First, process all sources
  for (std::vector<TaintDecl>::const_iterator I = S->Sources.begin(),
       E = S->Sources.end(); I != E; ++I) {
    const TaintDecl & TD = *I;
    assert(TD.end == T_Source);

    std::set<const Value*> Values = getValues(cs, TD.which);
    switch (TD.what) {
      case V: // Value
        flow.addSourceValue(Values.begin(), Values.end());
        break;
      case D: // DirectPtr
        flow.addSourceDirectPtr(Values.begin(), Values.end());
        break;
      case R: // Reachable
        flow.addSourceReachablePtr(Values.begin(), Values.end());
        break;
    }
  }

  // Then add all sinks
  for (std::vector<TaintDecl>::const_iterator I = S->Sinks.begin(),
       E = S->Sinks.end(); I != E; ++I) {
    const TaintDecl & TD = *I;
    assert(TD.end == T_Sink);

    std::set<const Value*> Values = getValues(cs, TD.which);
    switch (TD.what) {
      case V: // Value
        flow.addSinkValue(Values.begin(), Values.end());
        break;
      case D: // DirectPtr
        flow.addSinkDirectPtr(Values.begin(), Values.end());
        break;
      case R: // Reachable
        flow.addSinkReachablePtr(Values.begin(), Values.end());
        break;
    }
  }

  // Finally, stash it in the vector and return
  flows.push_back(flow);
  return flows;
}

} // end namespace deps
