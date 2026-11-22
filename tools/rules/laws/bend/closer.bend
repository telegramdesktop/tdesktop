import Base

type Decision is Data:
  Skip{}
  Diagnose{}
  Attach{}

def decide(standalone: Bool, closing_tail: Bool, whitespace_gap: Bool,
           fits: Bool) -> Decision:
  match standalone closing_tail whitespace_gap fits:
    case True{} True{} True{} True{}:
      Attach{}
    case True{} True{} _ _:
      Diagnose{}
    case _ _ _ _:
      Skip{}

def encode(decision: Decision) -> U32:
  match decision:
    case Skip{}:
      0
    case Diagnose{}:
      1
    case Attach{}:
      2
