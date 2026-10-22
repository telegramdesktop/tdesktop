# test_rpc_retry

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_rpc_retry.h](../../test_rpc_retry.h).

## Use when

Observe server-error resends. Read the contract below and the source header before use.

## Contract and usage

The permanent MTP resend seam: `Test::RecordRpcRetry` records one `rpc retry
code=<code> type=<type> request=<constructor>` row for every code-500 or
negative-code answer the transport auto-resends without calling the request's
fail handler, read through the `Test::RpcRetryProbe()` accessor; with its own
self-test for the recorded 500, the non-500 that reaches `.fail()` instead,
and the answer for a request id this process never sent. Its synthesized
`rpc_error` answers now go through `test_rpc_fixture.h` rather than a
hand-rolled `processCallback`.
