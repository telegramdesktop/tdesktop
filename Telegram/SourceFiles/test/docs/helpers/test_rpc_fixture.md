# test_rpc_fixture

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_rpc_fixture.h](../../test_rpc_fixture.h).

## Use when

Boxed controlled RPC replies. Read the contract below and the source header before use.

## Contract and usage

Type-safe controlled RPC replies for a registered request. Successful answers
take the generated request's boxed `ResponseType` (`tl::boxed`), which writes
the constructor word; a bare factory such as `MTP_messages_messages(...)`
omits that word and must not be `.write()`'d into the reply.
`DiagnoseControlledRpcResult` requires a complete decode with no leftover
primes (truncation and trailing words included) **before**
`MTP::Instance::processCallback`. `DeliverControlledRpcSuccess` /
`DeliverControlledRpcPrepared` also require `hasCallback` and refuse delivery
on a diagnosis so the parser stays registered. `DeliverControlledRpcError` is
the explicit boxed `rpc_error` route (registered or not).
`DeliverControlledRpcStale` is the explicit canceled/unknown-id success route:
it still calls `processCallback` after a valid boxed body. Own self-test over
`messages.getMessages` (empty and nonempty boxed `messages.Messages`,
bare/truncated/trailing/incompatible rejects, code-400 error,
cancel-then-stale).
