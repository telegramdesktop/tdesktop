import Base
import wrap.bend as Wrap
import closer.bend as Closer

law preserves_tokens:
  for tokens: List<&2, U32>
  for opening: Nat
  for span: Nat
  for gap: Nat
  for width: Nat
  for limit: Nat
  {Wrap.tokens_or(tokens, Wrap.propose(tokens, opening, span, gap, width, limit)) == tokens : List<&2, U32>}

law keeps_closer_attached:
  for tokens: List<&2, U32>
  for opening: Nat
  for span: Nat
  for gap: Nat
  for width: Nat
  for limit: Nat
  {Wrap.attached(Wrap.propose(tokens, opening, span, gap, width, limit)) == True{} : Bool}

law offers_short_wrap:
  {Wrap.propose([1, 2, 3, 4], 2n, 0n, 1n, 24n, 78n) == Some{Wrap.layout{[1, 2, 3, 4], 3n, 3n, 3n}} : Maybe<Wrap.Layout>}

law skips_inline_closer:
  for tail: Bool
  for gap: Bool
  for fits: Bool
  {Closer.decide(False{}, tail, gap, fits) == Closer.Skip{} : Closer.Decision}

law skips_expression_after_closer:
  for standalone: Bool
  for gap: Bool
  for fits: Bool
  {Closer.decide(standalone, False{}, gap, fits) == Closer.Skip{} : Closer.Decision}

law keeps_nonwhitespace_gap:
  for fits: Bool
  {Closer.decide(True{}, True{}, False{}, fits) == Closer.Diagnose{} : Closer.Decision}

law keeps_overlong_closer:
  for gap: Bool
  {Closer.decide(True{}, True{}, gap, False{}) == Closer.Diagnose{} : Closer.Decision}

law offers_safe_closer_fix:
  {Closer.decide(True{}, True{}, True{}, True{}) == Closer.Attach{} : Closer.Decision}
