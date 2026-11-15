# test_wallet_intercept

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_wallet_intercept.h](../../test_wallet_intercept.h).

## Use when

Controlled wallet request interception. Read the contract below and the source header before use.

## Contract and usage

The controlled Gram wallet fixture's shared request intercept, so a wallet
campaign calls it instead of copying `GramIntercept` overlay code. The overlay
pastes one hook, quoted in the header, as the first statement of
`MTP::details::Session::sendPrepared`:
`Test::InterceptWalletRequest(_instance, request)`, returning when it answers
true. That is the single main-thread funnel into a session's send map, every
`Instance::Private` resend included, and the module itself adds no production
seam. `ControlledWalletIntercept()` is the process-global `WalletIntercept`:
`arm(&session->mtp())` suppresses and records every request of that instance;
`seal()` suppresses `wallet.sendTransfer` and a provider POST
`/api/v2/jsonRPC` whose JSON `method` is `sendBoc` (the product's
`IsSendBocRequest`, here `IsWalletTransferRequest`) from any instance for the
rest of the process; `disarm()` lets every other constructor pass. Held
`toncenter.performApiRequest` provider calls are released through their real
registered callbacks with a controlled `rpc_error 400 TIMEOUT`, which
`Wallet::Api::IsTimeoutError` classifies as the host's own expiry
(`kTimeout`), from the first drain through every box and press; a campaign
turns releasing off only for a deliberate hold and then answers those calls
itself, posting from `onRegistered`. `AppendWalletEngineDrain` releases
everything held, leaves the release on and waits for the pure `readiness()`:
the engine worker idle by the overlay's own counters (an absent worker is an
idle reading, a missing reading refuses), no provider call held, and none
registered, released or answered for 1500 ms, longer than the 1000 ms
comment-recipient retry delay. `CheckNoEarlierProviderCallHeld` gates the
press. A refusal of either is a fixture gate (`TEST_FLAW`), never a product
`FAIL`. `teardown(quiesce)` runs seal, stop releasing, release the held
provider calls, cancel the other suppressed requests, the caller's quiesce,
disarm - the seal before the disarm, because the source run disarmed first and
its queued fixture send's routed `wallet.sendTransfer` reached test DC 2 - and
is idempotent from `Runner::onFinish`. The overlay fixes the fixture route
when the send is queued (`Test::GramOwns(this)` captured before
`_engine->run`), so a fixture send that enters the engine after disarm never
takes the real engine path. Own self-test on private intercepts, with no
network, no account, no session and no `MTP::Instance`:
`AppendWalletInterceptSelfTest`.
