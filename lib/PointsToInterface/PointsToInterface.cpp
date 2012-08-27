//===- PointsToInterface.cpp ----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implementation of the basic points-to interface.
//
//===----------------------------------------------------------------------===//

#include "PointsToInterface.h"

#include "dsa/DSGraphTraits.h"
#include "llvm/Module.h"
#include "llvm/ADT/DepthFirstIterator.h"

namespace deps {

static RegisterPass<deps::PointsToInterface>
X("pointstointerface", "Basic points-to interface");

char PointsToInterface::ID;

const AbstractLocSet PointsToInterface::EmptySet;

//
// To preserve soundness, we need to go over the computed equivalence classes
// and merge together those which contain incomplete DSNodes. We don't merge
// the classes explicitly, but only merge the sets of of leader nodes of the
// classes.
//
void PointsToInterface::mergeAllIncomplete() {
  // This node is a representative of an equivalence class which contains an
  // incomplete node.
  const DSNode *IncompleteRepresentative = 0;

  // Iterate over all nodes in the classes, and find and merge the class
  // leaders for those nodes which are incomplete.
  EquivalenceClasses<const DSNode *>::iterator EqIt = Classes->begin();
  EquivalenceClasses<const DSNode *>::iterator EqItEnd = Classes->end();
  for (; EqIt != EqItEnd; ++EqIt) {
    // Insert all leaders into the merged leader equivalence classes.
    if (EqIt->isLeader())
      MergedLeaders.insert(EqIt->getData());

    // Get the DSNode and check if the node is incomplete.
    const DSNode *N = EqIt->getData();
    if (!N->isIncompleteNode() && !N->isExternalNode() && !N->isUnknownNode())
      continue;

    // Get the leader of this node's class.
    const DSNode *Leader = Classes->getLeaderValue(N);
    if (IncompleteRepresentative == 0)
      IncompleteRepresentative = Leader;

    // Merge the leader with the class that contains other leaders with
    // incomplete class members.
    MergedLeaders.unionSets(Leader, IncompleteRepresentative);
  }
}

bool PointsToInterface::runOnModule(Module &M) {
  EquivsAnalysis = &getAnalysis<DSNodeEquivs>();
  Classes = &EquivsAnalysis->getEquivalenceClasses();
  mergeAllIncomplete();

  // Does not modify module.
  return false;
}

//
// Given a value in the program, returns a pointer to a set of abstract
// locations that the value points to.
//
const AbstractLocSet *
PointsToInterface::getAbstractLocSetForValue(const Value *V) {
  const DSNode *MergedLeader = getMergedLeaderForValue(V);

  // If the class for the value doesn't exist, return the empty set.
  if (MergedLeader == 0)
    return &EmptySet;

  // Find or build the set for the merged class leader.
  if (ClassForLeader.find(MergedLeader) == ClassForLeader.end())
    ClassForLeader[MergedLeader].insert(MergedLeader);

  return &ClassForLeader[MergedLeader];
}

//
// Given a value in the program, returns a pointer to a set of abstract
// locations that are reachable from the value.
//
const AbstractLocSet *
PointsToInterface::getReachableAbstractLocSetForValue(const Value *V) {
  const DSNode *MergedLeader = getMergedLeaderForValue(V);

  // If the class for the value doesn't exist, return the empty set.
  if (MergedLeader == 0)
    return &EmptySet;

  // Check if the reachable set has been computed.
  if (ReachablesForLeader.find(MergedLeader) != ReachablesForLeader.end())
    return &ReachablesForLeader[MergedLeader];

  // Otherwise, for each element in each equivalence class merged in with
  // MergedLeader, we need to compute its reachable set.
  AbstractLocSet ReachableSet;

  EquivalenceClasses<const DSNode *>::member_iterator
    MIt = MergedLeaders.member_begin(MergedLeaders.findValue(MergedLeader)),
    MItEnd = MergedLeaders.member_end();

  for (; MIt != MItEnd; ++MIt) {
    const DSNode *Leader = *MIt;

    EquivalenceClasses<const DSNode *>::member_iterator
      ClassesIt = Classes->member_begin(Classes->findValue(Leader)),
      ClassesItEnd = Classes->member_end();

    for (; ClassesIt != ClassesItEnd; ++ClassesIt) {
      const DSNode *Node = *ClassesIt;
      findReachableAbstractLocSetForNode(ReachableSet, Node);
    }
  }

  // ReachableSet now contains all DSNodes reachable from V. We cut this down
  // to the set of merged equivalence class leaders of these nodes.
  AbstractLocSet &Result = ReachablesForLeader[MergedLeader];
  AbstractLocSet::iterator ReachableIt = ReachableSet.begin();
  AbstractLocSet::iterator ReachableEnd = ReachableSet.end();
  for (; ReachableIt != ReachableEnd; ++ReachableIt) {
    const DSNode *Node = *ReachableIt;
    const DSNode *ClassLeader = Classes->getLeaderValue(Node);
    const DSNode *MergedLeader = MergedLeaders.getLeaderValue(ClassLeader);
    Result.insert(MergedLeader);
  }

  return &Result;
}

//
// Return the DSNode that represents the leader of the given value's DSNode
// equivalence class after the classes have been merged to account for
// incomplete nodes. Returns null if the value's DSNode doesn't exist.
//
const DSNode *
PointsToInterface::getMergedLeaderForValue(const Value *V) {
  const DSNode *Node;

  if (LeaderForValue.count(V))
    return LeaderForValue[V];

  // Get the node for V and return null if it doesn't exist.
  Node = EquivsAnalysis->getMemberForValue(V);
  if (Node == 0)
    return LeaderForValue[V] = 0;

  // Search for the equivalence class of Node.
  assert(Classes->findValue(Node) != Classes->end() && "Class not found!"); 
  const DSNode *NodeLeader = Classes->getLeaderValue(Node);
  const DSNode *MergedLeader = MergedLeaders.getLeaderValue(NodeLeader);

  return LeaderForValue[V] = MergedLeader;
}

//
// Add to the given set all nodes reachable from the given DSNode that are not
// in the set already.
//
void
PointsToInterface::findReachableAbstractLocSetForNode(AbstractLocSet &Set,
                                                      const DSNode *Node) {
  df_ext_iterator<const DSNode *> DFIt = df_ext_begin(Node, Set);
  df_ext_iterator<const DSNode *> DFEnd = df_ext_end(Node, Set);
  for (; DFIt != DFEnd; ++DFIt)
    ;
}

}
