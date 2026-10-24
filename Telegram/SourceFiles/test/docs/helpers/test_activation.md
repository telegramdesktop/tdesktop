# test_activation

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness self-test.

Source contract: [test_activation.h](../../test_activation.h).

## Use when

Self-test for injected window activation. Read the contract below and the source header before use.

## Contract and usage

The window-activation helper's own self-test: the application deliberately
de-activated and re-activated through the same QPA seam inside one stage, the
wrapper-versus-inner `hasFocus()` contrast and the
wrapper-versus-`rawTextEdit()` typing contrast measured on one real
`Ui::InputField`.
