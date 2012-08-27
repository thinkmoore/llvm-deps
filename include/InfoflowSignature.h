//===-- InfoflowSignature.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines an interface for creating and registering information
// flow signatures. A signature registrar maintains a list of signatures.
// When a call to unknown code is encountered, the registrar invokes the
// signatures in order until a summary of the call's information flows is
// generated.
//
//===----------------------------------------------------------------------===//

#ifndef INFOFLOWSIGNATURE_H_
#define INFOFLOWSIGNATURE_H_

#include "CallContext.h"
#include <set>
#include <vector>

namespace deps {

using namespace llvm;

class FlowRecord;

/// The public interface of an information flow signature.
class Signature {
public:
  /// Accept should return true if this signature is valid for the given call
  /// site and false otherwise. If a signature accepts a call site, it's
  /// result may be used as a summary for the given call.
  virtual bool accept(const ContextID ctxt, const ImmutableCallSite cs) const = 0;
  /// Process takes as input an llvm call site and returns a FlowRecord
  /// summarizing the information flows that occur as a result of the call.
  /// Process may only be invoked if the signature accepted the call site.
  virtual std::vector<FlowRecord> process(const ContextID ctxt, const ImmutableCallSite cs) const = 0;

  virtual ~Signature() {}
};

struct SigInfo;

/// A SignatureRegistrar maintains a list of signatures and finds an
/// appropriate signature for a given call site.
class SignatureRegistrar {
public:
  typedef std::vector<const Signature *>::iterator sig_iterator;
  SignatureRegistrar();
  ~SignatureRegistrar();
  /// Do not call directly. Used by RegisterSignature to register
  /// new signature types.
  void registerSignature(const SigInfo si);

  /// For a given call site, returns a summary of the information flows
  /// that may occur as a result of the call.
  /// Currently uses the first signature to accept the call, in order
  /// of signature registration.
  std::vector<FlowRecord> process(const ContextID ctxt, const ImmutableCallSite cs);
private:
  std::vector<const Signature *> sigs;
};

////////////////////////////////////////////////////////////////////////////////
/// Signature registration
////////////////////////////////////////////////////////////////////////////////

/// A helper structure for quickly registering signatures.
struct SigInfo {
public:
  typedef Signature* (*SigCtor_t)();
  SigInfo(const SigCtor_t ctor) : ctor(ctor) { }
  Signature *makeSignature() const { return ctor(); }
private:
  const SigCtor_t ctor;
};

/// A helper template
template<class sig>
Signature *callDefaultCtor() { return new sig(); }

/// Based on LLVM's RegisterPass template.
/// To register a signature of class MySignature with theRegistrar, invoke:
/// RegisterSignature<MySignature> TMP(theRegistrar);
/// where TMP is a fresh TMP variable.
template<class signature>
struct RegisterSignature : SigInfo {
    RegisterSignature(SignatureRegistrar & registrar)
    : SigInfo(SigInfo::SigCtor_t(callDefaultCtor<signature>)) {
    registrar.registerSignature(*this);
  }
};

}

#endif /* INFOFLOWSIGNATURE_H_ */
