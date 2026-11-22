import Base

type Layout is Data:
  layout{tokens: List<&2, U32>, first_arg: Nat, last_arg: Nat, closer: Nat}

def tokens_or(original: List<&2, U32>, edit: Maybe<Layout>) -> List<&2, U32>:
  match edit:
    case None{}:
      original
    case Some{layout{tokens, _, _, _}}:
      tokens

def attached(edit: Maybe<Layout>) -> Bool:
  match edit:
    case None{}:
      True{}
    case Some{layout{_, _, last, closer}}:
      Nat.is_eq(closer, last)

def propose_when(fits: Bool, tokens: List<&2, U32>, opening: Nat,
                 span: Nat, gap: Nat) -> Maybe<Layout>:
  match fits:
    case False{}:
      None{}
    case True{}:
      +first: Nat = 1n+opening
      +last = Nat.add(span, first)
      Some{layout{tokens, first, last, Nat.add(gap, last)}}

def propose(tokens: List<&2, U32>, opening: Nat, span: Nat, gap: Nat,
            width: Nat, limit: Nat) -> Maybe<Layout>:
  propose_when(Nat.is_le(width, limit), tokens, opening, span, 0n)
