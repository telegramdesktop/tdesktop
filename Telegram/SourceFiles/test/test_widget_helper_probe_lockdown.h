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
// connect control; only a fixture's network targets (Windows) and service
// names (macOS) follow it.
inline constexpr auto kWidgetHelperConnectControl = "connect-control";

// The prefix of a probe argument after the loopback port that names one Mach
// service for the Mac App Store probe's service listing: "service:<name>".
inline constexpr auto kWidgetHelperServicePrefix = "service:";

// The prefix of the macOS probe argument after the loopback port that asks
// for the serving variant: "rendering:<ms>", <ms> a decimal count of
// milliseconds from kWidgetHelperRenderingMinMs to
// kWidgetHelperRenderingMaxMs, given at most once, after the service names.
inline constexpr auto kWidgetHelperRenderingPrefix = "rendering:";
inline constexpr auto kWidgetHelperRenderingMinMs = 1;
inline constexpr auto kWidgetHelperRenderingMaxMs = 60000;

// The helper's lockdown hook on macOS, Linux and Windows, called once on
// serve's thread after the opening was accepted. |arguments| is everything
// after the helper's path, the token first. With no argument after the
// token it returns |lockdown|() alone: "" serves widgets, any other text
// refuses with it. Any mode but "probe" and "probe-unsandboxed" refuses
// with its name.
// On macOS, with "probe" it calls |lockdown| itself between its snapshots
// and, outside the serving variant below, always returns a non-empty
// report, so the stream is refused and no widget is served; with
// "probe-unsandboxed" it never calls |lockdown| and refuses the same way. The full listing goes to stderr, one bounded row
// each, ending with a completion marker; the returned summary stays under
// the transport's 4000 bytes and says when it is incomplete. The frameworks
// its attempts call are loaded only after the listing, so the listing
// reports what the helper holds without them; a framework that cannot be
// loaded leaves its attempt incomplete.
// In the Mac App Store build the helper is an XPC service, a child of
// launchd, so there "the parent" of its attempts and of its channel check is
// the channel's peer, Telegram (LOCAL_PEERPID of descriptor 3; getppid()
// and a recorded failure when that cannot be read). Descriptors 0 and 1 are
// accounted as launchd's /dev/null and 2 as the stderr Telegram passed
// or, when Telegram's stderr is a terminal, the one-way stream of
// its relay, and a terminal there as a defect; neither is compared
// with Telegram's descriptor, which the App Sandbox does not let it
// read. Inside the window its reference lookups must leave unchanged it
// records, by object, the launchd ports it can get without a lookup: the
// bootstrap port, the bootstrap special port, the registered ports and the
// bootstrap parent, releasing what it got. A listed entry with one of those
// objects counts as a launchd port, a defect with "probe". After the listing
// the "launchd" attempts read those routes again: the special port ("none"
// or "HELD"), the registered ports ("none" or a count), the parent's code
// and the launchd ports listed; with "probe" a held special port, a
// registered port or a parent is a defect too. The state also reads the
// process limit. The file attempts add the service's own container (HOME),
// their positive control, and the user's home, Documents and Downloads, the
// home taken from HOME ("<home>/Library/Containers/<id>/Data") with no IPC.
// After the parent attempts one row reads the service's responsible process
// and whether it is the parent. After the service attempts every
// "service:<name>" argument gets one row, "<policy>/<special>/<libxpc>": its
// App Sandbox mach-lookup reading, a lookup through the special port read
// after the lockdown, whose right is released at once (no other right is
// ever passed to a lookup), and libxpc's own lookup of the name, made by
// resuming a connection without a message: "xpc-ok" when no error came
// within 500 ms, otherwise "xpc<N>" from its invalidation reason, read on
// macOS 12 and later. The summary counts the allowed names and those
// reachable through each path; no such argument, a policy or special port
// that cannot be read, or a reason not read leaves that listing incomplete.
// After the window server attempts one row reads
// CGPreflightScreenCaptureAccess(), never requesting access or capturing.
// The direct-download helper ignores "service:<name>" arguments.
// With one well-formed "rendering:<ms>" and no malformed argument it is the
// serving variant, for both packages and both stream openings: the same
// rows up to and including the lockdown's, then, only when that lockdown
// succeeded with "probe", or with "probe-unsandboxed", which never calls
// |lockdown|, one "rendering:" row and "", so the helper answers Ready and
// serves. A detached thread of its own (8 MiB stack, "widget.probe") owns
// the probe from then on: it sleeps <ms>, then takes the same post-lockdown
// listing, marked by "rendering:" rows with CLOCK_UPTIME_RAW microseconds
// at its start and end, before the attempts load any framework, makes the
// same attempts, writes the summary as one uncut "summary:" row before the
// end row, and releases the probe; nothing calls |lockdown| again. A failed
// lockdown, a malformed, out-of-range or repeated "rendering:" argument or
// any other malformed argument refuses before Ready with the ordinary
// probe's summary. A thread that cannot start refuses the same way, after
// its failure row and the rest of the probe run in place. A helper that
// ends before the delayed probe wrote its end row leaves the listing
// incomplete.
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
// socket per target; on the Windows 7 tier it also opens its own process
// token for TOKEN_QUERY, since that tier has no pseudo token handles. A
// refusal of |lockdown| is returned with no probe line, every socket and
// that token closed. Otherwise it repeats the attempts, uses and closes
// the preopened sockets, reads its token (closing the Windows 7 one right
// after), its job and its tier, "tier: <tier>, version
// <major>.<minor>.<build>" from the shared unit's own cached choice, never
// from an argument, reads each policy only from the Windows version that
// documents its query (x86 DEP on Windows 7 through GetProcessDEPPolicy),
// writes every line to stderr as "Widget helper probe: <line>" and returns
// "", so the helper serves widgets. A malformed argument is a report line,
// never a refusal; the data directory and keychain arguments are ignored,
// and "probe-unsandboxed" refuses as not available. Lines, tiers, limits
// and controls: docs/helpers/test_widget_helper_probe.md#windows.
// Defined only in TelegramWidgetHelper's Debug build.
[[nodiscard]] std::string WidgetHelperProbeLockdown(
	const std::vector<std::string> &arguments,
	const std::function<std::string()> &lockdown);

// Each platform's half of WidgetHelperProbeLockdown(), defined in its
// test_widget_helper_probe_lockdown_<platform>.cpp of the same Debug build
// and called only by it: the refusal of a |mode| other than "probe" and
// "probe-unsandboxed", and the probe of either of them, |sandboxed| for
// "probe".
[[nodiscard]] std::string RefuseWidgetHelperMode(const std::string &mode);
[[nodiscard]] std::string RunWidgetHelperProbe(
	bool sandboxed,
	const std::vector<std::string> &arguments,
	const std::function<std::string()> &lockdown);

} // namespace Test
