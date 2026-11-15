# test_notify_override

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_notify_override.h](../../test_notify_override.h).

## Use when

Local notify-setting override and restoration. Read the contract below and the source header before use.

## Contract and usage

A local, restored override of one peer's notify settings, for a campaign that
must see a notification delivered on a chat the golden account serves muted:
its default user settings are `mute_until=2147483647`, which 777000 and every
private chat without a mute value of its own inherit, so
`System::computeSkipState` answers `Skip`. `Test::OverrideNotify(runner, peer,
values)` first captures the peer's whole `PeerNotifyState`
(`ReadPeerNotifyState` / `PeerNotifyStateText`) - known, value presence,
`mute_until`, sound, silent, show_previews and stories_muted, each with its
presence, plus `muteUnknown`, `isMuted`, `History::muted()` and exception
membership; value presence is read through an explicit-instantiation accessor
confined to the module's .cpp, because public API cannot tell an all-unset
value object from none - and then applies the capture with `values` laid over
it through `Data::NotifySettings::apply(MTPNotifyPeer,
MTPPeerNotifySettings)`, the exact local call the `updateNotifySettings`
handler makes, which forms no request. The default `UnmutedSilentNotify()` is
`mute_until=0` with sound none, so the notification is delivered silent (no
sound, no dock bounce); `MutedSilentNotify()` is its inverse. Two named gates
apply nothing and still return the never-null handle: `settings-unknown` (an
apply would make unknown settings known, and no apply can make them unknown
again) and `already-overridden` (an applied, unrestored override of the same
peer in the same live session). `NotifyOverride::restore()` applies the
capture back once and then only re-reads and compares on every later call, so
it is safe from the window's end, teardown and `Runner::onFinish` alike; a
served writer that replaced the override inside the window (an
`updateNotifySettings` push, `History::applyDialog`, `users.getFullUser`,
`ApiWrap::requestNotifySettings`) reads `intact=0`, and the capture is
restored anyway. `CheckNotifyRestored` prints `applied`, `intact` and both
whole states on either verdict, and `DefaultNotifyStateText` reads the three
defaults for a campaign's "unchanged" checks. The module registers one
`Runner::onFinish` on the first override, which restores or verifies every
handle LIFO. A handle holds a `base::weak_ptr` to the session and a `PeerId`
only; notify settings are not persisted, so an override also ends with the
process. Forum topics and the community fallback are not covered.
`Test::InterceptNotifyRequest` is the request observer the overlay pastes,
quoted in the header, as the first statement of
`MTP::details::Session::sendPrepared`: it records every request formed and,
under `Test::Active()`, suppresses every `account.updateNotifySettings`.
`Test::AppendNotifyLocalityCheck` waits more than 2 s (over
`kNotifySettingSaveTimeout`, 1 s) after the last restore, sends one raw
read-only `account.getNotifySettings` control with no handlers and decides
through `Test::DiscriminatingScan` - subject any
`account.updateNotifySettings` formed from the first override's request mark,
control the record carrying the control's own request id and constructor - and
is N/A by name without the hook. Own session-backed self-test:
`AppendNotifyOverrideSelfTest` (inverts 777000's, or Saved Messages', served
`isMuted`; both refusals; the restore, a second restore that applies nothing,
and the locality check).
