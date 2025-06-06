//===-- abi-ppc64.cpp -----------------------------------------------------===//
//
//                         LDC ? the LLVM D compiler
//
// This file is distributed under the BSD-style LDC license. See the LICENSE
// file for details.
//
//===----------------------------------------------------------------------===//
//
// The ABI implementation used for 32/64 bit big-endian PowerPC targets.
//
// The System V Application Binary Interface PowerPC Processor Supplement can be
// found here:
// http://refspecs.linuxfoundation.org/elf/elfspec_ppc.pdf
//
// The PowerOpen 64bit ABI can be found here:
// http://refspecs.linuxfoundation.org/ELF/ppc64/PPC-elf64abi-1.9.html
// http://refspecs.linuxfoundation.org/ELF/ppc64/PPC-elf64abi-1.9.pdf
//
//===----------------------------------------------------------------------===//

#include "gen/abi/abi.h"
#include "gen/abi/generic.h"
#include "gen/dvalue.h"
#include "gen/irstate.h"
#include "gen/llvmhelpers.h"
#include "gen/tollvm.h"

using namespace dmd;

struct PPCTargetABI : TargetABI {
  CompositeToArray32 compositeToArray32;
  CompositeToArray64 compositeToArray64;
  IntegerRewrite integerRewrite;
  const bool Is64Bit;

  explicit PPCTargetABI(const bool Is64Bit) : Is64Bit(Is64Bit) {}

  llvm::UWTableKind defaultUnwindTableKind() override {
    return llvm::UWTableKind::Async;
  }

  bool returnInArg(TypeFunction *tf, bool) override {
    Type *rt = tf->next->toBasetype();

    // The ABI specifies that aggregates of size 8 bytes or less are
    // returned in r3/r4 (ppc) or in r3 (ppc64). Looking at the IR
    // generated by clang this seems not to be implemented. Regardless
    // of size, the aggregate is always returned as sret.
    return DtoIsInMemoryOnly(rt);
  }

  bool passByVal(TypeFunction *, Type *t) override {
    // On ppc, aggregates are always passed as an indirect value.
    // On ppc64, they are always passed by value. However, clang
    // used byval for type > 64 bytes.
    return DtoIsInMemoryOnly(t) && isPOD(t) && (!Is64Bit || size(t) > 64);
  }

  void rewriteArgument(IrFuncTy &fty, IrFuncTyArg &arg) override {
    TargetABI::rewriteArgument(fty, arg);
    if (arg.rewrite)
      return;

    Type *ty = arg.type->toBasetype();

    if (ty->ty == TY::Tstruct || ty->ty == TY::Tsarray) {
      if (canRewriteAsInt(ty, Is64Bit)) {
        integerRewrite.applyTo(arg);
      } else {
        if (Is64Bit) {
          compositeToArray64.applyTo(arg);
        } else {
          compositeToArray32.applyTo(arg);
        }
      }
    } else if (ty->isIntegral()) {
      arg.attrs.addAttribute(ty->isUnsigned() ? LLAttribute::ZExt
                                              : LLAttribute::SExt);
    }
  }
};

// The public getter for abi.cpp
TargetABI *getPPCTargetABI(bool Is64Bit) { return new PPCTargetABI(Is64Bit); }
