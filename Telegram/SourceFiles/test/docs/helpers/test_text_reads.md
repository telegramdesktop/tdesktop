# test_text_reads

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_text_reads.h](../../test_text_reads.h).

## Use when

Space-normalized text assertions. Read the contract below and the source header before use.

## Contract and usage

Space-class-normalizing text comparison: `Test::NormalizeSpaces` maps U+00A0
and U+202F to U+0020 and changes nothing else, `Test::CheckTextReads` compares
a read-back against an expectation through it and prints both raw strings and
their whitespace code points on either verdict; with its own self-test, in
which a real `Ui::FlatLabel`'s narrow-no-break-space read-back is accepted
beside two deliberate FAIL negative controls - a different minute and a
different day - that keep the check from becoming permissive.
