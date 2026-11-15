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
// appends after the helper's token and, on Windows only, the lockdown hook
// the same-binary helper gives SandboxRemoteEngineEnd::serve(). On macOS
// and Linux the probe runs inside TelegramWidgetHelper's Debug build
// (test_widget_helper_probe_lockdown.h): on macOS a probe helper lists every
// descriptor and Mach right it holds after the real lockdown, attempts what
// the sandbox must deny and refuses the stream with a summary instead of
// serving any widget; on Linux a probe helper attempts what the lockdown
// must deny in forked copies of itself, then locks itself down for real,
// writes its readings to stderr and goes on serving widgets. On Windows a
// probe helper runs the native probe of
// Platform::WidgetHelper::PrepareProbe() and the harness's own network
// attempts around the real lockdown and, once that succeeded, writes their
// report to stderr and goes on serving widgets. Its only product caller is
// the `_DEBUG` block of tgw_remote.cpp; Release builds never reach it, the
// harness ones included.
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
	// socket per network target whose creation and nonblocking switch
	// succeeded, none without Winsock, all closed before Ready. That run
	// holds injected descriptors or handles and is never a clean inventory.
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

#ifdef Q_OS_WIN
// The same-binary helper's lockdown hook on Windows, called once on serve's
// thread after the opening was accepted. |arguments| is everything after
// `-widgethelper`, the token first. With no argument after the token it
// returns |lockdown|() alone: "" serves widgets, any other text refuses with
// it. Any mode but "probe" and "probe-unsandboxed" refuses with its name.
// With "probe" it calls Platform::WidgetHelper::PrepareProbe() with the
// client's working directory argument (native separators, empty when the
// argument is empty or missing), then takes the harness's own
// network controls: its own WSAStartup; only when that bind and start
// succeeded, a fresh socket of each family and protocol and the loopback
// port's and the network targets' attempts; native AFD endpoint opens,
// which need no Winsock; and, with "connect-control" and Winsock, one
// preopened socket per target whose creation and nonblocking switch
// succeeded. Then |lockdown|: its failure text refuses the stream and no
// probe line is written. On success it repeats the attempts it made, uses
// and closes the preopened sockets, writes every line of the product
// probe's report, then its own, to stderr as "Widget helper probe: <line>"
// and returns "", so the helper serves widgets. A malformed argument is a
// report line, never a refusal, though a target that does not parse, has
// no port or is past the limit gets one only with Winsock; the
// application-support and keychain arguments are ignored, and
// "probe-unsandboxed" refuses as not available.
[[nodiscard]] std::string WidgetHelperLockdown(
	const QStringList &arguments,
	Fn<QString()> lockdown);
#endif // Q_OS_WIN

} // namespace Test
