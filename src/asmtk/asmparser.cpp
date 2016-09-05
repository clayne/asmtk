// [AsmTk]
// Assembler toolkit based on AsmJit.
//
// [License]
// Zlib - See LICENSE.md file in the package.

// [Dependencies]
#include "./asmparser.h"

namespace asmtk {

using namespace asmjit;

// ============================================================================
// [asmtk::X86RegInfo]
// ============================================================================

// TODO: Information about a register should be part of asmjit.
struct X86RegInfo {
  RegInfo info;
  uint32_t count;
};

#define DEFINE_REG(opType, regType, regKind, regSize, count) \
  {{{ uint8_t(opType), uint8_t(regType), uint8_t(regKind), uint8_t(regSize) }}, count }
static const X86RegInfo x86RegInfo[X86Reg::kRegCount] = {
  DEFINE_REG(Operand::kOpNone, X86Reg::kRegNone        , 0                , 0 , 0  ),
  DEFINE_REG(Operand::kOpNone, X86Reg::kRegNone        , 0                , 0 , 0  ),
  DEFINE_REG(Operand::kOpReg , X86Reg::kRegRip         , X86Reg::kKindRip , 8 , 1  ),
  DEFINE_REG(Operand::kOpReg , X86Reg::kRegSeg         , X86Reg::kKindSeg , 2 , 7  ),
  DEFINE_REG(Operand::kOpReg , X86Reg::kRegGpbLo       , X86Reg::kKindGp  , 1 , 16 ),
  DEFINE_REG(Operand::kOpReg , X86Reg::kRegGpbHi       , X86Reg::kKindGp  , 1 , 4  ),
  DEFINE_REG(Operand::kOpReg , X86Reg::kRegGpw         , X86Reg::kKindGp  , 2 , 16 ),
  DEFINE_REG(Operand::kOpReg , X86Reg::kRegGpd         , X86Reg::kKindGp  , 4 , 16 ),
  DEFINE_REG(Operand::kOpReg , X86Reg::kRegGpq         , X86Reg::kKindGp  , 8 , 16 ),
  DEFINE_REG(Operand::kOpReg , X86Reg::kRegFp          , X86Reg::kKindFp  , 10, 8  ),
  DEFINE_REG(Operand::kOpReg , X86Reg::kRegMm          , X86Reg::kKindMm  , 8 , 8  ),
  DEFINE_REG(Operand::kOpReg , X86Reg::kRegK           , X86Reg::kKindK   , 8 , 8  ),
  DEFINE_REG(Operand::kOpReg , X86Reg::kRegXmm         , X86Reg::kKindVec , 16, 32 ),
  DEFINE_REG(Operand::kOpReg , X86Reg::kRegYmm         , X86Reg::kKindVec , 32, 32 ),
  DEFINE_REG(Operand::kOpReg , X86Reg::kRegZmm         , X86Reg::kKindVec , 64, 32 ),
  DEFINE_REG(Operand::kOpNone, X86Reg::kRegNone        , 0                , 0 , 0  ),
  DEFINE_REG(Operand::kOpReg , X86Reg::kRegBnd         , X86Reg::kKindBnd , 16, 4  ),
  DEFINE_REG(Operand::kOpReg , X86Reg::kRegCr          , X86Reg::kKindCr  , 8 , 16 ),
  DEFINE_REG(Operand::kOpReg , X86Reg::kRegDr          , X86Reg::kKindDr  , 8 , 16 )
};
#undef DEFINE_REG

enum X86Alias {
  kX86AliasStart = 0x00010000U,

  kX86AliasCmpsb = kX86AliasStart,
  kX86AliasCmpsd,
  kX86AliasCmpsq,
  kX86AliasCmpsw,

  kX86AliasMovsb,
  kX86AliasMovsd,
  kX86AliasMovsq,
  kX86AliasMovsw,

  kX86AliasLodsb,
  kX86AliasLodsd,
  kX86AliasLodsq,
  kX86AliasLodsw,

  kX86AliasScasb,
  kX86AliasScasd,
  kX86AliasScasq,
  kX86AliasScasw,

  kX86AliasStosb,
  kX86AliasStosd,
  kX86AliasStosq,
  kX86AliasStosw
};

// ============================================================================
// [asmtk::AsmParser]
// ============================================================================

AsmParser::AsmParser(CodeEmitter* emitter) : _emitter(emitter) {}
AsmParser::~AsmParser() {}

// ============================================================================
// [asmtk::AsmParser - Parse]
// ============================================================================

static void strToLower(uint8_t* dst, const uint8_t* src, size_t len) {
  for (size_t i = 0; i < len; i++)
    dst[i] = Utils::toLower<uint8_t>(static_cast<uint8_t>(src[i]));
}

#define COMB_CHAR_2(a, b) \
  ((uint32_t(a) << 8) | uint32_t(b))

#define COMB_CHAR_4(a, b, c, d) \
  ((uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(c) << 8) | uint32_t(d))

static bool x86ParseRegister(Operand_& op, const uint8_t* s, size_t len) {
  enum {
    kMinRegLen = 2,
    kMaxRegLen = 5
  };

  if (len < kMinRegLen || len > kMaxRegLen) return false;
  const uint8_t* sEnd = s + len;

  uint32_t c0 = Utils::toLower<uint32_t>(s[0]);
  uint32_t c1 = Utils::toLower<uint32_t>(s[1]);
  uint32_t c2 = len > 2 ? Utils::toLower<uint32_t>(s[2]) : uint32_t(0);
  uint32_t cn = (c0 << 8) + c1;

  uint32_t regType = X86Reg::kRegNone;
  uint32_t regId = 0;

  static const uint8_t abcdTo0312[] = { 0, 3, 1, 2 };

  // [AL|BL|CL|DL]
  // [AH|BH|CH|DH]
  // [AX|BX|CX|DX]
  // [ES|CS|SS|DS|FS|GS]
  if (len == 2) {
    if (c0 >= 'a' && c0 <= 'd') {
      regId = abcdTo0312[c0 - 'a'];
      if (c1 == 'l') { regType = X86Reg::kRegGpbLo; goto Done; }
      if (c1 == 'h') { regType = X86Reg::kRegGpbHi; goto Done; }
      if (c1 == 'x') { regType = X86Reg::kRegGpw  ; goto Done; }
    }

    if (c1 == 's') {
      regType = X86Reg::kRegSeg;
      if (c0 == 'e') { regId = X86Seg::kIdEs; goto Done; }
      if (c0 == 'c') { regId = X86Seg::kIdCs; goto Done; }
      if (c0 == 's') { regId = X86Seg::kIdSs; goto Done; }
      if (c0 == 'd') { regId = X86Seg::kIdDs; goto Done; }
      if (c0 == 'f') { regId = X86Seg::kIdFs; goto Done; }
      if (c0 == 'g') { regId = X86Seg::kIdGs; goto Done; }
    }

    regType = X86Reg::kRegGpw;
    goto TrySpBpSiDi;
  }

  // [SP|BP|SI|DI]
  // [SPL|BPL|SIL|DIL]
  // [EAX|EBX|ECX|EDX|ESP|EBP|EDI|ESI]
  // [RAX|RBX|RCX|RDX|RSP|RBP|RDI|RSI]
  // [RIP]
  if (len == 3) {
    if (c2 == 'l') {
      regType = X86Reg::kRegGpbLo;
      goto TrySpBpSiDi;
    }

    if (c0 == 'e' || c0 == 'r') {
      cn = (c1 << 8) | c2;

      if (c0 == 'r' && cn == COMB_CHAR_2('i', 'p')) {
        regType = X86Reg::kRegRip;
        goto Done;
      }

      regType = (c0 == 'e') ? X86Reg::kRegGpd : X86Reg::kRegGpq;
      if (cn == COMB_CHAR_2('a', 'x')) { regId = X86Gp::kIdAx; goto Done; }
      if (cn == COMB_CHAR_2('d', 'x')) { regId = X86Gp::kIdDx; goto Done; }
      if (cn == COMB_CHAR_2('b', 'x')) { regId = X86Gp::kIdBx; goto Done; }
      if (cn == COMB_CHAR_2('c', 'x')) { regId = X86Gp::kIdCx; goto Done; }
TrySpBpSiDi:
      if (cn == COMB_CHAR_2('s', 'p')) { regId = X86Gp::kIdSp; goto Done; }
      if (cn == COMB_CHAR_2('b', 'p')) { regId = X86Gp::kIdBp; goto Done; }
      if (cn == COMB_CHAR_2('s', 'i')) { regId = X86Gp::kIdSi; goto Done; }
      if (cn == COMB_CHAR_2('d', 'i')) { regId = X86Gp::kIdDi; goto Done; }
    }
  }

  // [R?|R?B|R?W|R?D]
  if (c0 == 'r') {
    s++;
    regType = X86Reg::kRegGpq;

    // Handle 'b', 'w', and 'd' suffixes.
    c2 = Utils::toLower<uint32_t>(sEnd[-1]);
    if (c2 == 'b')
      regType = X86Reg::kRegGpbLo;
    else if (c2 == 'w')
      regType = X86Reg::kRegGpw;
    else if (c2 == 'd')
      regType = X86Reg::kRegGpd;
    sEnd -= (regType != X86Reg::kRegGpq);
  }
  // [XMM?|YMM?|ZMM?]
  else if (c0 >= 'x' && c0 <= 'z' && c1 == 'm' && c2 == 'm') {
    s += 3;
    regType = X86Reg::kRegXmm + (c0 - 'x');
  }
  // [K?]
  else if (c0 == 'k') {
    s++;
    regType = X86Reg::kRegK;
  }
  // [ST?|FP?]
  else if ((c0 == 's' && c1 == 't') | (c0 == 'f' && c1 == 'p')) {
    s += 2;
    regType = X86Reg::kRegFp;
  }
  // [MM?]
  else if (c0 == 'm' && c1 == 'm') {
    s += 2;
    regType = X86Reg::kRegMm;
  }
  // [BND?]
  else if (c0 == 'b' && c1 == 'n' && c2 == 'd') {
    s += 3;
    regType = X86Reg::kRegBnd;
  }
  // [CR?]
  else if (c0 == 'c' && c1 == 'r') {
    s += 2;
    regType = X86Reg::kRegCr;
  }
  // [DR?]
  else if (c0 == 'd' && c1 == 'r') {
    s += 2;
    regType = X86Reg::kRegDr;
  }
  else {
    return false;
  }

  // Parse the register index.
  regId = static_cast<uint32_t>(s[0]) - '0';
  if (regId >= 10) return false;

  if (++s < sEnd) {
    c0 = static_cast<uint32_t>(*s++) - '0';
    if (c0 >= 10) return false;
    regId = regId * 10 + c0;

    // Maximum register
    if (regId >= 32) return false;
  }

  // Fail if the whole input wasn't parsed.
  if (s != sEnd) return false;

  // Fail if the register index is greater than allowed.
  if (regId >= x86RegInfo[regType].count) return false;

Done:
  op._initReg(x86RegInfo[regType].info.signature, regId);
  return true;
}

static uint32_t x86ParseSize(const uint8_t* s, size_t len) {
  enum {
    kMinSizeLen = 4,
    kMaxSizeLen = 7
  };

  if (len < kMinSizeLen || len > kMaxSizeLen)
    return 0;

  // Start from the end.
  uint32_t d = (Utils::toLower<uint32_t>(s[len - 4]) << 24) +
               (Utils::toLower<uint32_t>(s[len - 3]) << 16) +
               (Utils::toLower<uint32_t>(s[len - 2]) <<  8) +
               (Utils::toLower<uint32_t>(s[len - 1]) <<  0) ;

  if (d == COMB_CHAR_4('w', 'o', 'r', 'd')) {
    // Parsed 'word'.
    if (len == 4) return 2;

    // Sizes of length '5':
    d = (Utils::toLower<uint32_t>(s[0]) << 24);
    if (len == 5) {
      if (d == COMB_CHAR_4('d', 0, 0, 0)) return 4;
      if (d == COMB_CHAR_4('q', 0, 0, 0)) return 8;
      if (d == COMB_CHAR_4('t', 0, 0, 0)) return 10;
      if (d == COMB_CHAR_4('o', 0, 0, 0)) return 16;
    }

    // Sizes of length '6':
    d += (Utils::toLower<uint32_t>(s[0]) << 16);
    if (len == 6) {
      if (d == COMB_CHAR_4('m', 'm', 0, 0)) return 8;
      if (d == COMB_CHAR_4('d', 'q', 0, 0)) return 16;
      if (d == COMB_CHAR_4('q', 'q', 0, 0)) return 32;
    }

    // Sizes of length '7':
    d += (Utils::toLower<uint32_t>(s[0]) << 8);
    if (len == 7) {
      if (d == COMB_CHAR_4('x', 'm', 'm', 0)) return 16;
      if (d == COMB_CHAR_4('y', 'm', 'm', 0)) return 32;
      if (d == COMB_CHAR_4('z', 'm', 'm', 0)) return 64;
    }
  }

  // Parsed 'byte'.
  if (d == COMB_CHAR_4('b', 'y', 't', 'e'))
    return len == 4 ? 1 : 0;
  else
    return 0;
}

static Error asmHandleSymbol(AsmParser& parser, Operand_& dst, const uint8_t* name, size_t len) {
  Label L = parser._emitter->getLabelByName(reinterpret_cast<const char*>(name), len);

  if (!L.isValid()) {
    L = parser._emitter->newNamedLabel(reinterpret_cast<const char*>(name), len);
    if (!L.isValid()) return DebugUtils::errored(kErrorNoHeapMemory);
  }

  dst = L;
  return kErrorOk;
}

static Error x86ParseOperand(AsmParser& parser, Operand_& dst, AsmToken* token) {
  uint32_t type = token->type;
  uint32_t memSize = 0;
  Operand seg;

  // Symbol, could be register, memory operand size, or label.
  if (type == AsmToken::kSym) {
    // Try register.
    if (x86ParseRegister(dst, token->data, token->len)) {
      if (!dst.as<X86Reg>().isSeg())
        return kErrorOk;

      // A segment register followed by a colon (':') describes a segment of a
      // memory operand - in such case we store the segment and jump to MemOp.
      AsmToken tTmp;
      if (parser._tokenizer.next(token) == AsmToken::kColon &&
          parser._tokenizer.next(&tTmp) == AsmToken::kLBracket) {
        seg = dst;
        goto MemOp;
      }
      parser._tokenizer.back(token);
      return kErrorOk;
    }

    // Try memory size specifier.
    memSize = x86ParseSize(token->data, token->len);
    if (memSize) {
      type = parser._tokenizer.next(token);

      // The specifier may be followed by 'ptr', skip it in such case.
      if (type == AsmToken::kSym &&
          token->len == 3 &&
          Utils::toLower<uint32_t>(token->data[0]) == 'p' &&
          Utils::toLower<uint32_t>(token->data[1]) == 't' &&
          Utils::toLower<uint32_t>(token->data[2]) == 'r') {
        type = parser._tokenizer.next(token);
      }

      // Jump to memory operand if we encountered '['.
      if (type == AsmToken::kLBracket)
        goto MemOp;

      // Parse segment prefix otherwise.
      if (type == AsmToken::kSym) {
        // Segment register.
        if (!x86ParseRegister(seg, token->data, token->len) || !seg.as<X86Reg>().isSeg())
          return DebugUtils::errored(kErrorInvalidAddress);

        type = parser._tokenizer.next(token);
        if (type != AsmToken::kColon)
          return DebugUtils::errored(kErrorInvalidAddress);

        type = parser._tokenizer.next(token);
        if (type == AsmToken::kLBracket)
          goto MemOp;
      }

      return DebugUtils::errored(kErrorInvalidAddress);
    }

    // Must be label/symbol.
    return asmHandleSymbol(parser, dst, token->data, token->len);
  }

  // Memory address - parse opening '['.
  if (type == AsmToken::kLBracket) {
MemOp:
    Operand base;
    Operand index;
    uint32_t shift = 0;
    uint64_t disp = 0;

    // Parse "base + index * scale" part.
    uint32_t flags = 0;
    uint32_t opType = AsmToken::kInvalid;

MemRepeat:
    type = parser._tokenizer.next(token);

    if (type == AsmToken::kSym) {
      if (!x86ParseRegister(base, token->data, token->len)) {
        if (token->len == 3 &&
            !(flags & Mem::kFlagAbs) &&
            Utils::toLower<uint32_t>(token->data[0]) == 'a' &&
            Utils::toLower<uint32_t>(token->data[1]) == 'b' &&
            Utils::toLower<uint32_t>(token->data[2]) == 's') {
          flags |= Mem::kFlagAbs;
          goto MemRepeat;
        }

        return DebugUtils::errored(kErrorInvalidAddress);
      }

      opType = parser._tokenizer.next(token);
      if (opType == AsmToken::kMul) {
        index = base;
        base.reset();
        goto MemMul;
      }
      else if (opType == AsmToken::kAdd) {
        type = parser._tokenizer.next(token);
        if (type == AsmToken::kSym) {
          if (!x86ParseRegister(index, token->data, token->len))
            return DebugUtils::errored(kErrorInvalidAddress);

          opType = parser._tokenizer.next(token);
          if (opType == AsmToken::kMul) {
MemMul:
            type = parser._tokenizer.next(token);
            if (type != AsmToken::kU64)
              return DebugUtils::errored(kErrorInvalidAddress);

            switch (token->u64) {
              case 1: shift = 0; break;
              case 2: shift = 1; break;
              case 4: shift = 2; break;
              case 8: shift = 3; break;
              default:
                return DebugUtils::errored(kErrorInvalidAddress);
            }
            opType = parser._tokenizer.next(token);
          }
        }
        else if (type == AsmToken::kU64) {
          disp = token->u64;
          opType = parser._tokenizer.next(token);
        }
        else {
          return DebugUtils::errored(kErrorInvalidAddress);
        }
      }
      else if (opType == AsmToken::kSub) {
        goto MemDisp;
      }
      else if (opType != AsmToken::kRBracket) {
        return DebugUtils::errored(kErrorInvalidAddress);
      }
    }
    else if (type == AsmToken::kAdd || type == AsmToken::kSub) {
      opType = type;
    }
    else if (type == AsmToken::kU64) {
      disp = token->u64;
      opType = parser._tokenizer.next(token);
    }
    else {
      return DebugUtils::errored(kErrorInvalidAddress);
    }

    for (;;) {
      // Parse closing ']'.
      if (opType == AsmToken::kRBracket) {
        if (!Utils::isInt32<int64_t>(static_cast<int32_t>(disp)))
          return DebugUtils::errored(kErrorInvalidAddress);

        int32_t disp32 = static_cast<int32_t>(static_cast<int64_t>(disp));
        if (base.isReg() && !index.isReg())
          dst = x86::ptr(base.as<X86Gp>(), disp32);
        else if (base.isReg() && index.isReg())
          dst = x86::ptr(base.as<X86Gp>(), index.as<X86Gp>(), shift, disp32);
        else if (!base.isReg() && index.isReg())
          dst = x86::ptr(uint64_t(disp32), index.as<X86Gp>(), shift);
        else
          dst = x86::ptr(uint64_t(disp32));

        dst.as<X86Mem>().setSize(memSize);
        dst.as<X86Mem>().addFlags(flags);
        if (seg.isReg()) dst.as<X86Mem>().setSegment(seg.as<X86Seg>());

        return kErrorOk;
      }

      // Displacement.
      if (opType != AsmToken::kAdd && opType != AsmToken::kSub)
        return DebugUtils::errored(kErrorInvalidAddress);

MemDisp:
      type = parser._tokenizer.next(token);
      if (type != AsmToken::kU64)
        return DebugUtils::errored(kErrorInvalidAddress);

      if (opType == AsmToken::kAdd)
        disp += token->u64;
      else
        disp -= token->u64;

      opType = parser._tokenizer.next(token);
    }
  }

  // Immediate.
  if (type == AsmToken::kU64 || type == AsmToken::kSub) {
    bool negative = (type == AsmToken::kSub);
    if (negative) {
      type = parser._tokenizer.next(token);
      if (type != AsmToken::kU64)
        return DebugUtils::errored(kErrorInvalidState);
    }

    dst = imm(negative ? -token->i64 : token->i64);
    return kErrorOk;
  }

  return DebugUtils::errored(kErrorInvalidState);
}

static uint32_t x86ParseOption(const uint8_t* s, size_t len) {
  enum {
    kMinOptionLen = 3,
    kMaxOptionLen = 5
  };

  if (len < kMinOptionLen || len > kMaxOptionLen)
    return 0;

  uint32_t d0 = (static_cast<uint32_t>(s[0]) << 24) +
                (static_cast<uint32_t>(s[1]) << 16) +
                (static_cast<uint32_t>(s[2]) <<  8) ;

  // Options of length '3':
  if (len == 3) {
    if (d0 == COMB_CHAR_4('r', 'e', 'p', 0)) return X86Inst::kOptionRep;
    if (d0 == COMB_CHAR_4('r', 'e', 'x', 0)) return X86Inst::kOptionRex;
    return 0;
  }

  // Options of length '4':
  d0 += static_cast<uint32_t>(s[3]);
  if (len == 4) {
    if (d0 == COMB_CHAR_4('r', 'e', 'p', 'z')) return X86Inst::kOptionRep;
    if (d0 == COMB_CHAR_4('r', 'e', 'p', 'e')) return X86Inst::kOptionRep;
    if (d0 == COMB_CHAR_4('l', 'o', 'n', 'g')) return X86Inst::kOptionLongForm;
    return 0;
  }

  // Options of length '5':
  if (len == 5) {
    uint32_t d1 = static_cast<uint32_t>(s[4]) << 24;

    if (d0 == COMB_CHAR_4('r', 'e', 'p', 'n') &&
        d1 == COMB_CHAR_4('z', 0, 0, 0)) return X86Inst::kOptionRepnz;

    if (d0 == COMB_CHAR_4('r', 'e', 'p', 'n') &&
        d1 == COMB_CHAR_4('e', 0, 0, 0)) return X86Inst::kOptionRepnz;

    if (d0 == COMB_CHAR_4('s', 'h', 'o', 'r') &&
        d1 == COMB_CHAR_4('t',  0 ,  0 ,  0 )) return X86Inst::kOptionShortForm;

    if (d0 == COMB_CHAR_4('m', 'o', 'd', 'm') &&
        d1 == COMB_CHAR_4('r',  0 ,  0 ,  0 )) return X86Inst::kOptionModMR;
    return 0;
  }

  // Should be unreachable.
  return 0;
}

static uint32_t x86ParseAlias(const uint8_t* s, size_t len) {
  if (len < 3)
    return kInvalidInst;

  uint32_t d0 = (static_cast<uint32_t>(s[0]) << 24) +
                (static_cast<uint32_t>(s[1]) << 16) +
                (static_cast<uint32_t>(s[2]) <<  8) ;
  if (len == 3) {
    if (d0 == COMB_CHAR_4('s', 'a', 'l', 0)) return X86Inst::kIdShl;
    return kInvalidInst;
  }

  d0 += (static_cast<uint32_t>(s[3]) << 0);
  if (len == 4) {
    return kInvalidInst;
  }

  uint32_t d1 = (static_cast<uint32_t>(s[4]) << 24);
  if (len == 5) {
    uint32_t base = 0;

    if (d0 == COMB_CHAR_4('c', 'm', 'p', 's'))
      base = kX86AliasCmpsb;
    else if (d0 == COMB_CHAR_4('l', 'o', 'd', 's'))
      base = kX86AliasLodsb;
    else if (d0 == COMB_CHAR_4('m', 'o', 'v', 's'))
      base = kX86AliasMovsb;
    else if (d0 == COMB_CHAR_4('s', 'c', 'a', 's'))
      base = kX86AliasScasb;
    else if (d0 == COMB_CHAR_4('s', 't', 'o', 's'))
      base = kX86AliasStosb;
    else
      return base;

    if (d1 == COMB_CHAR_4('b', 0, 0, 0)) return base + 0;
    if (d1 == COMB_CHAR_4('d', 0, 0, 0)) return base + 1;
    if (d1 == COMB_CHAR_4('q', 0, 0, 0)) return base + 2;
    if (d1 == COMB_CHAR_4('w', 0, 0, 0)) return base + 3;

    return kInvalidInst;
  }

  return kInvalidInst;
}

static Error x86ParseInstruction(AsmParser& parser, uint32_t& instId, uint32_t& options, AsmToken* token) {
  for (;;) {
    size_t len = token->len;
    uint8_t lower[32];

    if (len > ASMJIT_ARRAY_SIZE(lower))
      return DebugUtils::errored(kErrorInvalidInstruction);

    strToLower(lower, token->data, len);

    // First try to match alias, as there are some tricky aliases.
    instId = x86ParseAlias(lower, len);
    if (instId != kInvalidInst) return kErrorOk;

    // Secondly, match any instruction as defined by AsmJit.
    instId = X86Inst::getIdByName(reinterpret_cast<char*>(lower), len);
    if (instId != kInvalidInst) return kErrorOk;

    // Maybe it's instruction option?
    uint32_t option = x86ParseOption(lower, len);
    if (!(option))
      return DebugUtils::errored(kErrorInvalidInstruction);

    // Refuse to parse the same option specified multiple times.
    if (ASMJIT_UNLIKELY(options & option))
      return DebugUtils::errored(kErrorInvalidInstruction);
    options |= option;

    if (parser._tokenizer.next(token) != AsmToken::kSym)
      return DebugUtils::errored(kErrorInvalidInstruction);
  }
}

static void x86FixupInstruction(AsmParser& parser, uint32_t& instId, uint32_t& options, Operand_& opExtra, Operand_* opArray, uint32_t& opCount) {
  uint32_t i;

  if (instId >= kX86AliasStart) {
    X86Emitter* emitter = static_cast<X86Emitter*>(parser._emitter);
    uint32_t memSize = 0;
    bool isStr = false;

    switch (instId) {
      case kX86AliasCmpsb: memSize = 1; instId = X86Inst::kIdCmps; isStr = true; break;
      case kX86AliasCmpsd: memSize = 4;
        isStr = opCount == 0 || (opCount == 2 && opArray[0].isMem() && opArray[1].isMem());
        instId = isStr ? X86Inst::kIdCmps : X86Inst::kIdCmpsd;
        break;
      case kX86AliasCmpsq: memSize = 8; instId = X86Inst::kIdCmps; isStr = true; break;
      case kX86AliasCmpsw: memSize = 2; instId = X86Inst::kIdCmps; isStr = true; break;

      case kX86AliasMovsb: memSize = 1; instId = X86Inst::kIdMovs; isStr = true; break;
      case kX86AliasMovsd: memSize = 4;
        isStr = opCount == 0 || (opCount == 2 && opArray[0].isMem() && opArray[1].isMem());
        instId = isStr ? X86Inst::kIdMovs : X86Inst::kIdMovsd;
        break;
      case kX86AliasMovsq: memSize = 8; instId = X86Inst::kIdMovs; isStr = true; break;
      case kX86AliasMovsw: memSize = 2; instId = X86Inst::kIdMovs; isStr = true; break;

      case kX86AliasLodsb: memSize = 1; instId = X86Inst::kIdLods; isStr = true; break;
      case kX86AliasLodsd: memSize = 4; instId = X86Inst::kIdLods; isStr = true; break;
      case kX86AliasLodsq: memSize = 8; instId = X86Inst::kIdLods; isStr = true; break;
      case kX86AliasLodsw: memSize = 2; instId = X86Inst::kIdLods; isStr = true; break;

      case kX86AliasScasb: memSize = 1; instId = X86Inst::kIdScas; isStr = true; break;
      case kX86AliasScasd: memSize = 4; instId = X86Inst::kIdScas; isStr = true; break;
      case kX86AliasScasq: memSize = 8; instId = X86Inst::kIdScas; isStr = true; break;
      case kX86AliasScasw: memSize = 2; instId = X86Inst::kIdScas; isStr = true; break;

      case kX86AliasStosb: memSize = 1; instId = X86Inst::kIdStos; isStr = true; break;
      case kX86AliasStosd: memSize = 4; instId = X86Inst::kIdStos; isStr = true; break;
      case kX86AliasStosq: memSize = 8; instId = X86Inst::kIdStos; isStr = true; break;
      case kX86AliasStosw: memSize = 2; instId = X86Inst::kIdStos; isStr = true; break;
        break;
    }

    if (isStr) {
      if (opCount == 0) {
        uint32_t sign = memSize == 1 ? X86Reg::signatureOf<X86Reg::kRegGpbLo>() :
                        memSize == 2 ? X86Reg::signatureOf<X86Reg::kRegGpw>() :
                        memSize == 4 ? X86Reg::signatureOf<X86Reg::kRegGpd>() :
                                       X86Reg::signatureOf<X86Reg::kRegGpq>() ;

        // String instructions aliases.
        opCount = 2;
        switch (instId) {
          case X86Inst::kIdCmps: opArray[0] = emitter->ptr_zsi(); opArray[1] = emitter->ptr_zdi(); break;
          case X86Inst::kIdMovs: opArray[0] = emitter->ptr_zdi(); opArray[1] = emitter->ptr_zsi(); break;
          case X86Inst::kIdLods:
          case X86Inst::kIdScas: opArray[0] = Reg::fromSignature(sign, X86Gp::kIdAx); opArray[1] = emitter->ptr_zdi(); break;
          case X86Inst::kIdStos: opArray[0] = emitter->ptr_zdi(); opArray[1] = Reg::fromSignature(sign, X86Gp::kIdAx); break;
        }
      }

      for (i = 0; i < opCount; i++) {
        if (opArray[i].isMem()) {
          X86Mem& mem = opArray[i].as<X86Mem>();

          if (mem.getSize() == 0)
            mem.setSize(memSize);

          if (mem.getBaseId() == X86Gp::kIdDi && mem.getSegmentId() == X86Seg::kIdEs)
            mem.resetSegment();
        }
      }
    }
  }

  for (i = 0; i < opCount; i++) {
    Operand_& op = opArray[i];

    // If the parsed memory segment is the default one, remove it. AsmJit
    // always emits segment-override if the segment is specified, this is
    // good on AsmJit side, but causes problems here as it's not necessary
    // to emit 'ds:' everywhere if the input contains it (and it's common).
    if (op.isMem() && op.as<X86Mem>().hasSegment()) {
      X86Mem& mem = op.as<X86Mem>();

      // Default to `ds` segment for most instructions.
      uint32_t defaultSeg = X86Seg::kIdDs;

      // Default to `ss` segment if the operand has esp|rsp or ebp|rbp base.
      if (mem.hasBaseReg()) {
        if (mem.getBaseId() == X86Gp::kIdSp || mem.getBaseId() == X86Gp::kIdBp)
          defaultSeg = X86Seg::kIdSs;
      }

      if (mem.getSegmentId() == defaultSeg)
        mem.resetSegment();
    }
  }
}

Error AsmParser::parse(const char* input, size_t len) {
  if (len == kInvalidIndex) len = ::strlen(input);
  if (len == 0) return kErrorOk;

  uint32_t archType = _emitter->getArchType();
  _tokenizer.setInput(reinterpret_cast<const uint8_t*>(input), len);

  for (;;) {
    AsmToken token;
    uint32_t tType = _tokenizer.next(&token);

    if (tType == AsmToken::kSym) {
      AsmToken tmp;

      tType = _tokenizer.next(&tmp);
      if (tType == AsmToken::kColon) {
        // Parse bound label.
        Label dst;
        ASMJIT_PROPAGATE(asmHandleSymbol(*this, dst, token.data, token.len));
        ASMJIT_PROPAGATE(_emitter->bind(dst));

        tType = _tokenizer.next(&token);
      }
      else {
        // Parse instruction.
        _tokenizer.back(&tmp);

        uint32_t instId = 0;
        uint32_t options = 0;
        ASMJIT_PROPAGATE(x86ParseInstruction(*this, instId, options, &token));

        Operand opExtra;
        Operand_ opArray[6];
        uint32_t opCount = 0;

        // Parse operands.
        for (;;) {
          tType = _tokenizer.next(&token);

          // Instruction without operands...
          if ((tType == AsmToken::kNL || tType == AsmToken::kEnd) && opCount == 0)
            break;

          // Parse operand.
          ASMJIT_PROPAGATE(x86ParseOperand(*this, opArray[opCount], &token));

          // Parse {} options introduced by AVX-512.
          tType = _tokenizer.next(&token);
          if (tType == AsmToken::kLCurl) {
            do {
              tType = _tokenizer.next(&token);
              if (tType == AsmToken::kSym || tType == AsmToken::kNSym) {
                // TODO: Only accepts lowercase, must be fixed.
                if (token.len == 2 && token.data[0] == 'k' && (uint8_t)(token.data[1] - '0') < 8) {
                  if (opCount != 0 || !opExtra.isNone())
                    return DebugUtils::errored(kErrorInvalidState);
                  opExtra = X86KReg(token.data[1] - '0');
                  options |= X86Inst::kOptionOpExtra;
                }
                else if (token.is('z')) {
                  if (opCount != 0 || (options & X86Inst::kOptionKZ))
                    return DebugUtils::errored(kErrorInvalidState);
                  options |= X86Inst::kOptionKZ;
                }
                else if (token.is('1', 't', 'o', 'x')) {
                  if (!opArray[opCount].isMem() || (options & X86Inst::kOption1ToX))
                    return DebugUtils::errored(kErrorInvalidState);
                  options |= X86Inst::kOption1ToX;
                }
                else if (token.is('s', 'a', 'e')) {
                  if (opCount != 0 || (options & X86Inst::kOptionSAE))
                    return DebugUtils::errored(kErrorInvalidState);
                  options |= X86Inst::kOptionSAE;
                }
                else if (token.is('r', 'n')) {
                  if (opCount != 0 || (options & X86Inst::kOptionER))
                    return DebugUtils::errored(kErrorInvalidState);
                  options |= X86Inst::kOptionER | X86Inst::kOptionRN_SAE;
                }
                else if (token.is('r', 'd')) {
                  if (opCount != 0 || (options & X86Inst::kOptionER))
                    return DebugUtils::errored(kErrorInvalidState);
                  options |= X86Inst::kOptionER | X86Inst::kOptionRD_SAE;
                }
                else if (token.is('r', 'u')) {
                  if (opCount != 0 || (options & X86Inst::kOptionER))
                    return DebugUtils::errored(kErrorInvalidState);
                  options |= X86Inst::kOptionER | X86Inst::kOptionRU_SAE;
                }
                else if (token.is('r', 'z')) {
                  if (opCount != 0 || (options & X86Inst::kOptionER))
                    return DebugUtils::errored(kErrorInvalidState);
                  options |= X86Inst::kOptionER | X86Inst::kOptionRZ_SAE;
                }
              }
              else {
                return DebugUtils::errored(kErrorInvalidState);
              }

              tType = _tokenizer.next(&token);
              if (tType != AsmToken::kRCurl)
                return DebugUtils::errored(kErrorInvalidState);

              tType = _tokenizer.next(&token);
            } while (tType == AsmToken::kLCurl);
          }

          opCount++;
          if (tType == AsmToken::kComma) {
            if (opCount == ASMJIT_ARRAY_SIZE(opArray))
              return DebugUtils::errored(kErrorInvalidState);
            continue;
          }

          if (tType == AsmToken::kNL || tType == AsmToken::kEnd)
            break;

          return DebugUtils::errored(kErrorInvalidState);
        }

        for (uint32_t i = opCount; i < 4; i++)
          opArray[i].reset();

        x86FixupInstruction(*this, instId, options, opExtra, opArray, opCount);
        ASMJIT_PROPAGATE(X86Inst::validate(archType, instId, options, opExtra, opArray, opCount));

        _emitter->setOptions(options);
        if (!opExtra.isNone()) _emitter->setOpExtra(opExtra);
        if (opCount > 4) _emitter->setOp4(opArray[4]);
        if (opCount > 5) _emitter->setOp5(opArray[5]);

        ASMJIT_PROPAGATE(_emitter->_emit(instId, opArray[0], opArray[1], opArray[2], opArray[3]));
      }
    }

    if (tType == AsmToken::kNL)
      continue;

    if (tType == AsmToken::kEnd)
      break;

    return DebugUtils::errored(kErrorInvalidState);
  }

  return kErrorOk;
}

} // asmtk namespace
