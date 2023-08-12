//===-- gen/funcgenstate.h - Function code generation state -----*- C++ -*-===//
//
//                         LDC – the LLVM D compiler
//
// This file is distributed under the BSD-style LDC license. See the LICENSE
// file for details.
//
//===----------------------------------------------------------------------===//
//
// "Global" transitory state kept while emitting LLVM IR for the body of a
// single function, with FuncGenState being the top-level such entity.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "gen/irstate.h"
#include "gen/pgo_ASTbased.h"
#include "gen/trycatchfinally.h"
#include "gen/variable_lifetime.h"
#include "llvm/ADT/DenseMap.h"
#include <vector>

class Identifier;
struct IRState;
class Statement;

namespace llvm {
class AllocaInst;
class BasicBlock;
class Constant;
class MDNode;
class Value;
}

/// Stores information needed to correctly jump to a given label or loop/switch
/// statement (break/continue can be labeled, but are not necessarily).
struct JumpTarget {
  /// The basic block to ultimately branch to.
  llvm::BasicBlock *targetBlock = nullptr;

  /// The index of the target in the stack of active cleanup scopes.
  ///
  /// When generating code for a jump to this label, the cleanups between
  /// the current depth and that of the level will be emitted. Note that
  /// we need to handle only one direction (towards the root of the stack)
  /// because D forbids gotos into try or finally blocks.
  // TODO: We might not be able to detect illegal jumps across try-finally
  // blocks by only storing the index.
  CleanupCursor cleanupScope;

  /// Keeps target of the associated loop or switch statement so we can
  /// handle both unlabeled and labeled jumps.
  Statement *targetStatement = nullptr;

  JumpTarget() = default;
  JumpTarget(llvm::BasicBlock *targetBlock, CleanupCursor cleanupScope,
             Statement *targetStatement);
};

/// Keeps track of labels and implicit loop targets for goto/break/continue.
class JumpTargets {
public:
  explicit JumpTargets(TryCatchFinallyScopes &scopes);

  /// Registers a loop statement to be used as a target for break/continue
  /// statements in the current scope.
  void pushLoopTarget(Statement *loopStatement,
                      llvm::BasicBlock *continueTarget,
                      llvm::BasicBlock *breakTarget);

  /// Pops the last pushed loop target, so it is no longer taken into
  /// consideration for resolving breaks/continues.
  void popLoopTarget();

  /// Registers a statement to be used as a target for break statements in the
  /// current scope (currently applies only to switch statements).
  void pushBreakTarget(Statement *switchStatement,
                       llvm::BasicBlock *targetBlock);

  /// Unregisters the last registered break target.
  void popBreakTarget();

  /// Adds a label to serve as a target for goto statements.
  ///
  /// Also causes in-flight forward references to this label to be resolved.
  void addLabelTarget(Identifier *labelName, llvm::BasicBlock *targetBlock);

  /// Terminates the current basic block with an unconditional branch to the
  /// given label, along with the cleanups to execute on the way there.
  ///
  /// Legal forward references (i.e. within the same function, and not into
  /// a cleanup scope) will be resolved.
  void jumpToLabel(Loc loc, Identifier *labelName);

  /// Terminates the current basic block with an unconditional branch to the
  /// continue target generated by the given loop statement, along with
  /// the cleanups to execute on the way there.
  void continueWithLoop(Statement *loopStatement) {
    jumpToStatement(continueTargets, loopStatement);
  }

  /// Terminates the current basic block with an unconditional branch to the
  /// closest loop continue target, along with the cleanups to execute on
  /// the way there.
  void continueWithClosest() { jumpToClosest(continueTargets); }

  /// Terminates the current basic block with an unconditional branch to the
  /// break target generated by the given loop or switch statement, along with
  /// the cleanups to execute on the way there.
  void breakToStatement(Statement *loopOrSwitchStatement) {
    jumpToStatement(breakTargets, loopOrSwitchStatement);
  }

  /// Terminates the current basic block with an unconditional branch to the
  /// closest break statement target, along with the cleanups to execute on
  /// the way there.
  void breakToClosest() { jumpToClosest(breakTargets); }

private:
  /// Unified implementation for labeled break/continue.
  void jumpToStatement(std::vector<JumpTarget> &targets,
                       Statement *loopOrSwitchStatement);

  /// Unified implementation for unlabeled break/continue.
  void jumpToClosest(std::vector<JumpTarget> &targets);

  TryCatchFinallyScopes &scopes;

  using LabelTargetMap = llvm::DenseMap<Identifier *, JumpTarget>;
  /// The labels we have encountered in this function so far, accessed by
  /// their associated identifier (i.e. the name of the label).
  LabelTargetMap labelTargets;

  ///
  std::vector<JumpTarget> breakTargets;

  ///
  std::vector<JumpTarget> continueTargets;
};

/// Tracks the basic blocks corresponding to the switch `case`s (and `default`s)
/// in a given function.
///
/// Since the bb for a given case must already be known when a jump to it is
/// to be emitted (at which point the former might not have been emitted yet,
/// e.g. when goto-ing forward), we lazily create them as needed.
class SwitchCaseTargets {
public:
  /// Returns the basic block associated with the given case/default statement,
  /// asserting that it has already been created.
  llvm::BasicBlock *get(Statement *stmt);

  /// Returns the basic block associated with the given case/default statement
  /// or creates one with the given name if it does not already exist
  llvm::BasicBlock *getOrCreate(Statement *stmt, const llvm::Twine &name,
                                IRState &irs);

private:
  llvm::DenseMap<Statement *, llvm::BasicBlock *> targetBBs;
};

/// The "global" transitory state necessary for emitting the body of a certain
/// function.
///
/// For general metadata associated with a function that persists for the entire
/// IRState lifetime (i.e. llvm::Module emission process) see IrFunction.
class FuncGenState {
public:
  FuncGenState(IrFunction &irFunc, IRState &irs);

  FuncGenState(FuncGenState const &) = delete;
  FuncGenState &operator=(FuncGenState const &) = delete;

  // The function code is being generated for.
  IrFunction &irFunc;

  TryCatchFinallyScopes scopes;

  LocalVariableLifetimeAnnotator localVariableLifetimeAnnotator;

  JumpTargets jumpTargets;

  // PGO information
  CodeGenPGO pgo;

  /// Tracks basic blocks corresponding to switch cases.
  SwitchCaseTargets switchTargets;

  /// The marker at which to insert `alloca`s in the function entry bb.
  llvm::Instruction *allocapoint = nullptr;

  /// alloca for the nested context of this function
  llvm::Value *nestedVar = nullptr;

  /// The basic block with the return instruction.
  llvm::BasicBlock *retBlock = nullptr;

  /// A stack slot containing the return value, for functions that return by
  /// value.
  llvm::AllocaInst *retValSlot = nullptr;

  /// Emits a call or invoke to the given callee, depending on whether there
  /// are catches/cleanups active or not.
  llvm::CallBase *callOrInvoke(llvm::Value *callee,
                               llvm::FunctionType *calleeType,
                               llvm::ArrayRef<llvm::Value *> args,
                               const char *name = "", bool isNothrow = false);

private:
  IRState &irs;
};
