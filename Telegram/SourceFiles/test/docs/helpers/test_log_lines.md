# test_log_lines

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness self-test.

Source contract: [test_log_lines.h](../../test_log_lines.h).

## Use when

Self-test for one physical record per log call. Read the contract below and the source header before use.

## Contract and usage

The one-physical-line guarantee's own self-test: one `LogRaw`-family call
driven with each of the eleven separator forms in turn, with one payload
mixing them all, with a trailing separator and with a separator-only payload,
each read back out of `test_log.txt` by byte offset and asserted to have added
exactly one physical line under an independent transcription of
`str.splitlines()` - beside a separator-free control that must read one line
under any writer, and a last stage whose payload's middle line would be
byte-equal to `TEST_COMPLETE` and which asserts no produced line is.
