import Base
import LAWS.bend as Laws
import wrap.bend as Wrap

def refl_nat(+x: Nat) -> {Nat.is_eq(x, x) == True{} : Bool}:
  match x:
    case 0n:
      {==}
    case 1n+p:
      refl_nat(p)

def preserves_when(fits: Bool, tokens: List<&2, U32>, opening: Nat, span: Nat)
  -> {Wrap.tokens_or(tokens, Wrap.propose_when(fits, tokens, opening, span, 0n)) == tokens : List<&2, U32>}:
  match fits:
    case False{}:
      {==}
    case True{}:
      {==}

def attached_when(fits: Bool, tokens: List<&2, U32>, opening: Nat, span: Nat)
  -> {Wrap.attached(Wrap.propose_when(fits, tokens, opening, span, 0n)) == True{} : Bool}:
  match fits:
    case False{}:
      {==}
    case True{}:
      refl_nat(Nat.add(span, 1n+opening))

def Laws.preserves_tokens(tokens, opening, span, gap, width, limit):
  preserves_when(Nat.is_le(width, limit), tokens, opening, span)

def Laws.keeps_closer_attached(tokens, opening, span, gap, width, limit):
  attached_when(Nat.is_le(width, limit), tokens, opening, span)

def Laws.offers_short_wrap():
  {==}

def Laws.skips_inline_closer(tail, gap, fits):
  {==}

def Laws.skips_expression_after_closer(standalone, gap, fits):
  match standalone:
    case False{}:
      {==}
    case True{}:
      {==}

def Laws.keeps_nonwhitespace_gap(fits):
  {==}

def Laws.keeps_overlong_closer(gap):
  match gap:
    case False{}:
      {==}
    case True{}:
      {==}

def Laws.offers_safe_closer_fix():
  {==}
