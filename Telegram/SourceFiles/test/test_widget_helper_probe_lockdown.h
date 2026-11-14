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

// The helper half of the widget helper's Debug probe on macOS and the names
// both halves share. Qt-free: TelegramWidgetHelper's Debug build compiles
// it, its Release build contains no probe. The client half is
// test_widget_helper_probe.h.
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

// The last probe argument when the run preopens its connect control.
inline constexpr auto kWidgetHelperConnectControl = "connect-control";

// The helper's lockdown hook on macOS, called once on serve's thread after
// the opening was accepted. |arguments| is everything after the helper's
// path, the token first. With no argument after the token it returns
// |lockdown|() alone: "" serves widgets, any other text refuses with it.
// Any mode but "probe" and "probe-unsandboxed" refuses with its name.
// With "probe" it calls |lockdown| itself between its snapshots and always
// returns a non-empty report, so the stream is refused and no widget is
// served; with "probe-unsandboxed" it never calls |lockdown| and refuses the
// same way. The full listing goes to stderr, one bounded row each, ending
// with a completion marker; the returned summary stays under the transport's
// 4000 bytes and says when it is incomplete. The frameworks its attempts
// call are loaded only after the listing, so the listing reports what the
// helper holds without them; a framework that cannot be loaded leaves its
// attempt incomplete. Defined only in TelegramWidgetHelper's Debug build.
[[nodiscard]] std::string WidgetHelperProbeLockdown(
	const std::vector<std::string> &arguments,
	const std::function<std::string()> &lockdown);

} // namespace Test
