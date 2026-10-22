# test_gated_stage

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness self-test.

Source contract: [test_gated_stage.h](../../test_gated_stage.h).

## Use when

Self-test for explicit gated skips. Read the contract below and the source header before use.

## Contract and usage

The first-class gated skip's own self-test: a stage whose `skipReason` returns
a reason, writing one `TEST_RESULT: N/A:` row and skipping `run`, `until` and
`then` without waiting - its never-ready `until` under a one-second timeout is
the falsifier - beside a stage whose gate returns an empty string and runs
normally in the tick that begins it.
