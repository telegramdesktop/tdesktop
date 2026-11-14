# test_wallet_ready

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_wallet_ready.h](../../test_wallet_ready.h).

## Use when

Judge served wallet freshness. Read the contract below and the source header before use.

## Contract and usage

Judge one reading of the wallet's freshness stamp — the one `applyState()`
writes when the server's wallet state reaches this client — refuse a stamp
that was never written, wait for a stamped state under a bounded deadline that
names what it observed, and re-run the instrument's own
positive/negative/recovery self-test.
