# test_hover

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness self-test.

Source contract: [test_hover.h](../../test_hover.h).

## Use when

Self-test for click/drag hover cleanup. Read the contract below and the source header before use.

## Contract and usage

The input helpers' own self-test: a clicked-then-dragged and a never-touched
`Ui::RoundButton` measured against the two fills their style names, proving a
completed synthetic click and drag leave no hover.
