# test_layer_root

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness self-test.

Source contract: [test_layer_root.h](../../test_layer_root.h).

## Use when

Self-test for the painting-layer resolver. Read the contract below and the source header before use.

## Contract and usage

The painting-layer-root resolver's own self-test: a plain `Ui::GenericBox`
accepted as its own render root beside one that cleared
`Qt::WA_OpaquePaintEvent`, refused and then captured through its
`Ui::BoxLayerWidget`; `CaptureBoxLayer` saves the whole shell beside
`CaptureInLayerRoot`'s content crop, and `MisframedDetails` is quoted on a
rect outside the layer.
