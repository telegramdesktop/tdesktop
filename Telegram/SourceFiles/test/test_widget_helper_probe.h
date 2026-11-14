/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <string>

// The Debug probe of the widget helper's lockdown on macOS, Windows and
// Linux: the arguments a client appends after `-widgethelper <token>` and
// the lockdown hook the helper gives SandboxRemoteEngineEnd::serve(). On
// macOS a probe helper lists every descriptor and Mach right it holds after
// the real lockdown, attempts what the sandbox must deny and refuses the
// stream with a summary instead of serving any widget. On Windows a probe
// helper runs the native probe of Platform::WidgetHelper::PrepareProbe()
// around the real lockdown, writes its report to stderr and goes on serving
// widgets. On Linux a probe helper attempts what the lockdown must deny in
// forked copies of itself, then locks itself down for real, writes its
// readings to stderr and goes on serving widgets. Its only product caller
// is the `_DEBUG` block of tgw_remote.cpp; Release builds never reach it,
// the harness ones included.
// Reference: docs/helpers/test_widget_helper_probe.md.
//
// Everything here is harness-only: declared here and deliberately left
// undefined in a build without the harness, because no production code
// reads it.

namespace Test {

// The generic password the probe's keychain attempt reads: this service and
// the run's synthetic account. The overlay that passes an account owns the
// item: it adds it only when absent, never replaces one and deletes it.
inline constexpr auto kWidgetHelperProbeKeychainService
	= "org.telegram.desktop.widget-helper-probe";

// The probe's positive-control fixtures, passed by a disposable overlay. An
// empty account or a zero port leaves the claims that need it incomplete.
struct WidgetHelperProbeFixture {
	// The account of a generic password of kWidgetHelperProbeKeychainService.
	QString keychainAccount;
	// A live loopback TCP listener on 127.0.0.1 that receives the token
	// "widget-helper-probe <pid> <mode>" from each connection that completes.
	int loopbackPort = 0;
	// Preopen one unconnected TCP socket before the lockdown and connect it
	// to |loopbackPort| afterwards. That run lists the injected descriptor
	// and is never a clean inventory.
	bool connectControl = false;
};

// True for "probe" (lock down, then probe) and "probe-unsandboxed" (the same
// probe without the lockdown: the control, always a separate helper).
[[nodiscard]] bool IsWidgetHelperProbeMode(const QString &mode);

// The argv tail after the token for a probe |mode|: "mode cwd
// application-support", the client's working directory and
// QStandardPaths::GenericDataLocation, then with a |fixture| the keychain
// account and the loopback port, then "connect-control" when asked. Empty
// for any other |mode|: a normal helper gets no arguments. Main thread.
[[nodiscard]] QStringList WidgetHelperProbeArguments(const QString &mode);
[[nodiscard]] QStringList WidgetHelperProbeArguments(
	const QString &mode,
	const WidgetHelperProbeFixture &fixture);

// The helper's lockdown hook, called once on serve's thread after the
// opening was accepted. |arguments| is everything after `-widgethelper`, the
// token first. With no argument after the token it returns |lockdown|()
// alone: "" serves widgets, any other text refuses with it. Any mode but
// "probe" and "probe-unsandboxed" refuses with its name.
// On macOS, with "probe" it calls |lockdown| itself between its snapshots
// and always returns a non-empty report, so the stream is refused and no
// widget is served; with "probe-unsandboxed" it never calls |lockdown| and
// refuses the same way. The full listing goes to stderr, one bounded row
// each, ending with a completion marker; the returned summary stays under
// the transport's 4000 bytes and says when it is incomplete.
// On Windows, with "probe" it calls Platform::WidgetHelper::PrepareProbe(),
// then |lockdown|: its failure text refuses the stream and no probe line is
// written; on success every line of the probe's report goes to stderr as
// "Widget helper probe: <line>" and it returns "", so the helper serves
// widgets. The arguments after the mode are ignored there, and
// "probe-unsandboxed" refuses as not available.
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
// On the non-Linux all-other platforms a probe mode refuses as not
// available.
[[nodiscard]] std::string WidgetHelperLockdown(
	const QStringList &arguments,
	Fn<QString()> lockdown);

} // namespace Test
