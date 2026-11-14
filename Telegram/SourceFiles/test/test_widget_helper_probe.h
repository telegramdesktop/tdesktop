/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "test/test_widget_helper_probe_lockdown.h"

#include <string>

// The Debug probe of the widget helper's lockdown on macOS, Windows and
// Linux, its half in Telegram: the probe mode test, the arguments a client
// appends after the helper's token and, on Windows and Linux, the lockdown
// hook the same-binary helper gives SandboxRemoteEngineEnd::serve(). On
// macOS the probe runs inside TelegramWidgetHelper's Debug build
// (test_widget_helper_probe_lockdown.h): a probe helper lists every
// descriptor and Mach right it holds after the real lockdown, attempts what
// the sandbox must deny and refuses the stream with a summary instead of
// serving any widget. On Windows a probe helper runs the native probe of
// Platform::WidgetHelper::PrepareProbe() and the harness's own network
// attempts around the real lockdown, writes their report to stderr and goes
// on serving widgets. On Linux a probe helper attempts what the lockdown
// must deny in forked copies of itself, then locks itself down for real,
// writes its readings to stderr and goes on serving widgets. Its only
// product caller is the `_DEBUG` block of tgw_remote.cpp; Release builds
// never reach it, the harness ones included.
// Reference: docs/helpers/test_widget_helper_probe.md.
//
// Everything here is harness-only: declared here and deliberately left
// undefined in a build without the harness, because no production code
// reads it.

namespace Test {

// The probe's positive-control fixtures, passed by a disposable overlay. An
// empty account or a zero port leaves the claims that need it incomplete.
struct WidgetHelperProbeFixture {
	// The account of a generic password of kWidgetHelperProbeKeychainService
	// (macOS only).
	QString keychainAccount;
	// The port of live loopback listeners; zero means none. On macOS a TCP
	// listener on 127.0.0.1 that receives "widget-helper-probe <pid> <mode>"
	// from each connection that completes. On Windows TCP and UDP listeners
	// on 127.0.0.1 and [::1], which receive "widget-helper-probe <pid>
	// <phase> <mode>" from each completed connection or datagram.
	int loopbackPort = 0;
	// Preopen unconnected sockets before the lockdown and use them
	// afterwards: on macOS one TCP socket for |loopbackPort|, on Windows one
	// socket per network target, all closed before Ready. That run holds
	// injected descriptors or handles and is never a clean inventory.
	bool connectControl = false;
	// Windows only: "tcp:<address>" or "udp:<address>", the address numeric
	// as WSAStringToAddressW reads it, port included, IPv6 in brackets with
	// an optional scope ("udp:[fe80::1%4]:5000"), each listened to by the
	// caller. The macOS probe reports them as malformed arguments, and the
	// Linux one ignores them.
	QStringList networkTargets;
};

// True for "probe" (lock down, then probe) and "probe-unsandboxed" (the same
// probe without the lockdown: the control, always a separate helper).
[[nodiscard]] bool IsWidgetHelperProbeMode(const QString &mode);

// The argv tail after the token for a probe |mode|: "mode cwd
// application-support", the client's working directory and
// QStandardPaths::GenericDataLocation, then with a |fixture| the keychain
// account and the loopback port, then "connect-control" when asked, then
// the network targets. Empty for any other |mode|: a normal helper gets no
// arguments. Main thread.
[[nodiscard]] QStringList WidgetHelperProbeArguments(const QString &mode);
[[nodiscard]] QStringList WidgetHelperProbeArguments(
	const QString &mode,
	const WidgetHelperProbeFixture &fixture);

#ifndef Q_OS_MAC
// The same-binary helper's lockdown hook, called once on serve's thread
// after the opening was accepted. |arguments| is everything after
// `-widgethelper`, the token first. With no argument after the token it
// returns |lockdown|() alone: "" serves widgets, any other text refuses with
// it. Any mode but "probe" and "probe-unsandboxed" refuses with its name.
// On Windows, with "probe" it calls Platform::WidgetHelper::PrepareProbe()
// with the client's working directory argument (native separators, empty
// when the argument is empty or missing), then takes the harness's own
// network controls: its own WSAStartup, a fresh socket of each family and
// protocol, the loopback port's and the network targets' attempts, native
// AFD endpoint opens and, with "connect-control", one preopened socket per
// target. Then |lockdown|: its failure text refuses the stream and no probe
// line is written. On success it repeats the attempts, uses and closes the
// preopened sockets, writes every line of the product probe's report, then
// its own, to stderr as "Widget helper probe: <line>" and returns "", so
// the helper serves widgets. A malformed argument is a report line, never a
// refusal; the application-support and keychain arguments are ignored, and
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
#endif // !Q_OS_MAC

} // namespace Test
