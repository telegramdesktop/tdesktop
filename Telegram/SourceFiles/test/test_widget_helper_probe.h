/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "test/test_widget_helper_probe_lockdown.h"

// The Debug probe of the widget helper's lockdown on macOS, Windows and
// Linux, its half in Telegram: the probe mode test and the arguments a
// client appends after the helper's token. On all three the probe runs
// inside TelegramWidgetHelper's Debug build
// (test_widget_helper_probe_lockdown.h): on macOS a probe helper lists every
// descriptor and Mach right it holds after the real lockdown, attempts what
// the sandbox must deny and refuses the stream with a summary instead of
// serving any widget, or, in its serving variant, serves widgets and takes
// that listing and those attempts after a delay while it renders them; on
// Linux a probe helper attempts what the lockdown
// must deny in forked copies of itself, then locks itself down for real,
// writes its readings to stderr and goes on serving widgets; on Windows a
// probe helper runs its native probe and the harness's own network attempts
// around the real lockdown and, once that succeeded, writes their report to
// stderr and goes on serving widgets. Its only product caller is the
// `_DEBUG` block of tgw_remote.cpp; Release builds never reach it, the
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
	// macOS only: Mach service names, each passed as "service:<name>". The
	// Mac App Store probe lists each one's App Sandbox mach-lookup reading
	// and looks every one up through the bootstrap special port and through
	// libxpc's own named lookup; the direct-download and Linux probes ignore
	// them, and the Windows one reports them as malformed arguments.
	QStringList serviceNames;
	// macOS only: nonzero asks for the serving variant, passed last as
	// "rendering:<ms>". A helper whose real lockdown succeeded, or the
	// unsandboxed control, answers Ready and serves widgets, and takes its
	// post-lockdown listing and attempts that many milliseconds later on a
	// thread of its own; the helper accepts kWidgetHelperRenderingMinMs to
	// kWidgetHelperRenderingMaxMs and refuses any other value, passed as
	// given, as a malformed argument before Ready. Zero passes nothing: the
	// probe refuses the stream after its listing, as it always did.
	int renderingDelayMs = 0;
};

// True for "probe" (lock down, then probe) and "probe-unsandboxed" (the same
// probe without the lockdown: the control, always a separate helper).
[[nodiscard]] bool IsWidgetHelperProbeMode(const QString &mode);

// The argv tail after the token for a probe |mode|: "mode cwd
// application-support", the client's working directory and
// QStandardPaths::GenericDataLocation, then with a |fixture| the keychain
// account and the loopback port, then "connect-control" when asked, then
// the network targets, then "service:<name>" for each service name, then,
// on macOS, "rendering:<ms>" for a nonzero renderingDelayMs. Empty for any
// other |mode|: a normal helper gets no arguments. Main thread.
[[nodiscard]] QStringList WidgetHelperProbeArguments(const QString &mode);
[[nodiscard]] QStringList WidgetHelperProbeArguments(
	const QString &mode,
	const WidgetHelperProbeFixture &fixture);

} // namespace Test
