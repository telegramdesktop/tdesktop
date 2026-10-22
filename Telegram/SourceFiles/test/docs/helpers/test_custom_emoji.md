# test_custom_emoji

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_custom_emoji.h](../../test_custom_emoji.h).

## Use when

Explicitly registered ready emoji fixture. Read the contract below and the source header before use.

## Contract and usage

Supply an always-ready `Ui::Text::CustomEmoji` that fills the large-emoji box
with worst-case ink, handed out only for document ids the scenario itself
registered.
