import Base
import wrap.bend as Wrap

law legacy_closer_stays_attached:
  for opening: Nat
  for span: Nat
  for gap: Nat
  {Wrap.attached(Wrap.propose_when(True{}, [1, 2, 3, 4], opening, span, gap)) == True{} : Bool}
