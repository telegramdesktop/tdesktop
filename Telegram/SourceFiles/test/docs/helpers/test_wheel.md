# test_wheel

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness self-test.

Source contract: [test_wheel.h](../../test_wheel.h).

## Use when

Self-test for wheel delivery. Read the contract below and the source header before use.

## Contract and usage

The wheel helper's own self-test: a covering `Ui::AbstractButton` inside a
`Ui::ScrollArea` whose subject wheel moves the area and names the viewport,
beside the same wheel aimed at the viewport (today's working target), a wheel
aimed at the `QAbstractScrollArea` itself that is not certified delivered, and
a parentless-window button that refuses by name.
