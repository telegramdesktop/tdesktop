# test_mapped_target

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness self-test.

Source contract: [test_mapped_target.h](../../test_mapped_target.h).

## Use when

Self-test for full mapped-target visibility and for the typed
`MappedRect` coordinate owners. Read the contract below and the source
header before use.

## Contract and usage

Complete-target mapped readiness measuring itself: a painted owner taller than
its `Ui::ElasticScroll`, a child rectangle that fits the owner while remaining
partly outside the scroll, then the same origin/owner/local rect capturable
after ordinary `scrollToY`, plus a sibling `Ui::ScrollArea` whose clipper is
`viewport()`, overlap-only / empty / hidden refusals, a nonpainting root that
geometry would otherwise accept, and `PaintingLayerRoot` unresolved on the
fixture.

## Typed coordinate owners

Three stages before the teardown measure the typed path
([coordinate owners of mapped rectangles](test_capture.md#coordinate-owners-of-mapped-rectangles))
on three more fixture widgets: a painted child of the owner translated by a
nonzero x and y inside the header band, painting two halves of strongly
different lightness so `LooksBlank` accepts its frame; a painted child
hanging over the owner's right edge; and a parentless `Ui::RpWidget` that is
never shown.

| Stage | Expected verdict |
| --- | --- |
| `typed coordinate owners map one translated child identically` | `MappedRect::Local(child, child->rect())` and `MappedRect::InParent(child, child->geometry())` both resolve to the same full bounds, equal to an independent `Ui::MapFrom(owner, child, child->rect())` and of the child's size. Premise: the untyped `ReadMappedTarget(owner, child, child->geometry())` resolves at the doubled position `geometry().translated(pos())`, so containment alone accepts it. The typed `Local(child, child->geometry())` is refused as `coordinate mismatch` naming the requested rect, the child's own bounds, the child and the owner; the typed `prepare` stores the same refusal, accepts no frame and logs no failure; the reverse `InParent(child, child->rect())` is refused too. |
| `typed refusals: clipped, detached, wrong owner, hidden, blank and nonpainting root` | With the `Ui::ElasticScroll` back at the top: the overhanging child is refused `not fully inside the painted owner` and the elastic child `relevant viewport`, each with its full requested size mapped; the parentless widget is refused `detached` as `InParent` and `wrong owner` as `Local`, like the stray band; a hidden child's `InParent` rect is refused `painted widget is not visible`; the typed `prepare` still refuses the uniform band as `looks blank` and the stray as `render root paints no background`; no failure is logged. |
| `typed painted crop` | The typed `prepare(owner, Local(child, child->rect()))` is accepted with the child's complete extent at the image's ratio; `InParent(child, child->geometry())` is accepted with the same mapping and a byte-equal image; the typed `CaptureMappedTarget` saves `mapped_target_typed_local`. |

Every refusal is stored and asserted as a passing Check; the stages emit no
deliberate failure. The joint check that the typed vocabulary does not
weaken the checked full-owner crop's descendant rule is stage C3 of
[test_full_owner_capture](test_full_owner_capture.md).
