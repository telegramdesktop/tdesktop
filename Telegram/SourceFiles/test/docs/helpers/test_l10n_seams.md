# test_l10n_seams

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Internal dependency.
Source contract: [test_l10n_seams.h](../../test_l10n_seams.h).

## Use when

Only the localization capture from the l10n-tdesktop repository (built with
`-D TDESKTOP_L10N_CAPTURE=<its path>`) installs these. It
needs to know which phrases a screen read, it forces app config values (limits
so that every plural form of a language appears on some screen, and flags or
strings a screen depends on), and it keeps its screens from writing to the
server. An overlay that wants a temporary translation uses
[test_lang_pack](test_lang_pack.md) instead.

## Preconditions and limits

`NoteLangRead` is called from `Lang::details::Current()`, which every `tr::`
phrase goes through, so the observer sees reads of phrases the screen built,
including ones never painted (accessibility names, hidden rows). Separating
what a screen shows from background reads is the capture's job.

`OverrideAppConfigDouble` is asked by `Main::AppConfig::getDouble()`, which
also serves the integer and limit getters. `OverrideAppConfigBool` and
`OverrideAppConfigString` are asked by `getBool()` and `getString()` the same
way. Values read through other getters (string arrays such as
`whitelisted_domains`, maps, number arrays) are not overridden. A value the
session copies when the config arrives (`premium_purchase_blocked` behind
`Main::Session::premiumCanBuy()`, the freeze dates) keeps the server value until
the next config refresh, and a producer built on `AppConfig::value()` does not
fire again: install the override before the screen reads the value.

`HoldRequest` is the first statement of `MTP::details::Session::sendPrepared`,
the single main-thread funnel into a session's send map: every first send and
every resend of an `MTP::Instance` reaches it, and its request is the one
`storeRequest` registered, so `request->requestId` is the key
`MTP::Instance::hasCallback`, `processCallback` and `cancel` take. A holder
that returns true takes the request: it never reaches the network, and its
callbacks stay registered until the holder answers it, which it must do on a
later main-thread turn (`crl::on_main`), never inside the call. Answer with
`DeliverControlledRpcSuccess` or `DeliverControlledRpcError` from
[test_rpc_fixture](test_rpc_fixture.md); a held request nobody answers waits
forever. The capture's holder (`Kit::Requests` in
`l10n_synthetic_requests.cpp` in l10n-tdesktop) answers what a screen registered,
passes reads, fails requests about its fake peers locally, and fails every
other request with `400 L10N_BLOCKED`, recording it for the screen. It never
holds a `messages.getPeerDialogs` batch, which would leave real histories
waiting.

## Minimal use

```cpp
SetLangReadObserver([](ushort key) { Reads().insert(key); });
SetAppConfigDoubleOverride([](const QString &key) {
	return (key == u"channels_limit_default"_q)
		? std::optional<double>(3)
		: std::nullopt;
});
SetAppConfigBoolOverride([](const QString &key) {
	return (key == u"wallet_available"_q)
		? std::optional<bool>(false)
		: std::nullopt;
});
SetRequestHolder([](
		not_null<MTP::Instance*> instance,
		const MTP::details::SerializedRequest &request) {
	if (!IsWrite(request)) {
		return false;
	}
	const auto id = request->requestId;
	crl::on_main(instance.get(), [=] {
		if (instance->hasCallback(id)) {
			const auto delivery = DeliverControlledRpcError(
				instance,
				id,
				400,
				u"BLOCKED"_q);
			// ... check delivery.route ...
		}
	});
	return true;
});
// ... open screens ...
SetLangReadObserver(nullptr);
SetAppConfigDoubleOverride(nullptr);
SetAppConfigBoolOverride(nullptr);
SetRequestHolder(nullptr);
```

## Readings and refusals

The observer receives the generated key index; names come from
`lang_auto.indices` of the same build. Plural phrases report their base index
plus the form shift.

The holder reads the method from the constructor word at
`SerializedRequest::kMessageBodyPosition`; the body's length in bytes is the
word before it. Wrappers that `SessionPrivate` adds later (`invokeWithLayer`,
`initConnection`, `invokeAfterMsg`) are not there yet. A held request's sender
sees exactly what a server reply would give it: `.done()` for a success, and
`.fail()` for an error. `MTP::Instance` retries no `400` of an unknown type,
so only the sender's own fail handler can send it again.

## Lifetime and cleanup

Every installer stores a plain function pointer and keeps it until it is
replaced or cleared with `nullptr`. Clear them all in the scenario's finish,
or later reads and sends in the same process keep calling them. Clearing the
holder does not answer what it already took: post those answers first.

## Verification

No self-test. A capture run of any catalog screen reports the keys it read in
`l10n_screens.jsonl`; an empty `reads` list on a screen with text means the
observer was not installed. The same line's `blocked` list names the methods
the holder refused for that screen, and the log has a
`L10N: <id>: BLOCKED <method>` note for each.
