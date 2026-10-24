# test_mapped_target

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness self-test.

Source contract: [test_mapped_target.h](../../test_mapped_target.h).

## Use when

Self-test for full mapped-target visibility. Read the contract below and the source header before use.

## Contract and usage

Complete-target mapped readiness measuring itself: a painted owner taller than
its `Ui::ElasticScroll`, a child rectangle that fits the owner while remaining
partly outside the scroll, then the same origin/owner/local rect capturable
after ordinary `scrollToY`, plus a sibling `Ui::ScrollArea` whose clipper is
`viewport()`, overlap-only / empty / hidden refusals, a nonpainting root that
geometry would otherwise accept, and `PaintingLayerRoot` unresolved on the
fixture.
