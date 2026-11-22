import Base
import wrap.bend as Wrap

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
