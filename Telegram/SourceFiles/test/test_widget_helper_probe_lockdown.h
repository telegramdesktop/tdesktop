/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <functional>
#include <string>
#include <vector>

// The helper half of the widget helper's Debug probe on macOS, Linux and
// Windows and the names both halves share. Qt-free: TelegramWidgetHelper's
// Debug build compiles it, its Release build contains no probe. The client
// half is test_widget_helper_probe.h.
// Reference: docs/helpers/test_widget_helper_probe.md.

namespace Test {

// The generic password the probe's keychain attempt reads: this service and
// the run's synthetic account. The overlay that passes an account owns the
// item: it adds it only when absent, never replaces one and deletes it.
inline constexpr auto kWidgetHelperProbeKeychainService
	= "org.telegram.desktop.widget-helper-probe";

// Lock down, then probe.
inline constexpr auto kWidgetHelperProbeMode = "probe";

// The same probe without the lockdown: the control, always a separate
// helper.
inline constexpr auto kWidgetHelperUnsandboxedMode = "probe-unsandboxed";

// The probe argument after the loopback port when the run preopens its
// connect control; only a fixture's network targets (Windows) follow it.
inline constexpr auto kWidgetHelperConnectControl = "connect-control";

// The helper's lockdown hook on macOS, Linux and Windows, called once on
// serve's thread after the opening was accepted. |arguments| is everything
// after the helper's path, the token first. With no argument after the
// token it returns |lockdown|() alone: "" serves widgets, any other text
// refuses with it. Any mode but "probe" and "probe-unsandboxed" refuses
// with its name.
// On macOS, with "probe" it calls |lockdown| itself between its snapshots
// and always returns a non-empty report, so the stream is refused and no
// widget is served; with "probe-unsandboxed" it never calls |lockdown| and
// refuses the same way. The full listing goes to stderr, one bounded row
// each, ending with a completion marker; the returned summary stays under
// the transport's 4000 bytes and says when it is incomplete. The frameworks
// its attempts call are loaded only after the listing, so the listing
// reports what the helper holds without them; a framework that cannot be
// loaded leaves its attempt incomplete.
// On Linux, with "probe" it first runs three forked copies of the helper,
// one at a time, each reaped within 2 s or killed: the first makes the
// seccomp install of |lockdown| fail through an outer filter, so every step
// before it applies, Landlock and the empty root included, then opens the
// working directory, the client's one and the data directory; the other two
// call |lockdown| and attempt execve and socket, which must kill them. Then
// it calls |lockdown| itself: its failure text refuses the stream after a
// "lockdown: FAILED" line. On success it reads its seccomp, no-new-privs and
// capability state through a descriptor opened before the lockdown, opens
// the same directories, lists its descriptors and returns "", so the helper
// serves widgets. Every line goes to stderr as "Widget helper probe:
// <line>". With "probe-unsandboxed" it does the same without any |lockdown|
// call or outer filter, and the helper serves widgets unconfined. The
// arguments after the data directory are ignored there.
// On Windows, with "probe" it takes its native controls, opening the
// client's working directory argument (separators made native; empty or
// missing names none), and its network ones: its own Winsock start, the
// loopback port's four targets and the numeric targets after it, AFD
// endpoint opens and, with "connect-control" and Winsock, one preopened
// socket per target. A refusal of |lockdown| is returned with no probe line
// and every socket closed. Otherwise it repeats the attempts, uses and
// closes the preopened sockets, writes every line to stderr as "Widget
// helper probe: <line>" and returns "", so the helper serves widgets. A
// malformed argument is a report line, never a refusal; the data directory
// and keychain arguments are ignored, and "probe-unsandboxed" refuses as
// not available. Lines, limits and controls:
// docs/helpers/test_widget_helper_probe.md#windows.
// Defined only in TelegramWidgetHelper's Debug build.
[[nodiscard]] std::string WidgetHelperProbeLockdown(
	const std::vector<std::string> &arguments,
	const std::function<std::string()> &lockdown);

} // namespace Test
