# test_history_fixtures

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_history_fixtures.h](../../test_history_fixtures.h).

## Use when

Lifetime-owned history service-message fixtures. Read the contract below and the source header before use.

## Contract and usage

Inject a caller-supplied service action into a real history as a regular or
(negative-control) local item, with a caller-owned lifetime that removes it,
and log the menu-gating predicates.
