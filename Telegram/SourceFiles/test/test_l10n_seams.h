/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// Usage and limits: docs/helpers/test_l10n_seams.md.

#include "base/basic_types.h"

#include <optional>

namespace MTP {
class Instance;
namespace details {
class SerializedRequest;
} // namespace details
} // namespace MTP

namespace Test {

// The entry points below are what production code calls. Each is a no-op
// until its handler is installed, so the call site needs no condition
// around it, and the Release branch of this module defines them alone.

// Lang::details::Current() reports every phrase read here.
void NoteLangRead(ushort key);

// Main::AppConfig::getDouble(), getBool() and getString() ask here before
// reading the server value. Each returns nullopt until its override is
// installed.
[[nodiscard]] std::optional<double> OverrideAppConfigDouble(
	const QString &key);
[[nodiscard]] std::optional<bool> OverrideAppConfigBool(const QString &key);
[[nodiscard]] std::optional<QString> OverrideAppConfigString(
	const QString &key);

// MTP::details::Session::sendPrepared() asks here first. True means the
// holder took the request: it never enters the send map, and its callbacks
// stay registered in |instance| for the holder to answer later.
[[nodiscard]] bool HoldRequest(
	not_null<MTP::Instance*> instance,
	const MTP::details::SerializedRequest &request);

// Debug-only installers, deliberately left undefined in a Release build
// like RpcRetryProbe() in test_rpc_retry.h. Pass nullptr to remove. The
// observer runs on every phrase read on the main thread, so it must be
// cheap and must not read phrases itself. The holder runs inside
// sendPrepared(), in the middle of MTP's request bookkeeping: it must be
// cheap and must post any answer, never deliver it inside the call.
void SetLangReadObserver(void (*observer)(ushort key));
void SetAppConfigDoubleOverride(
	std::optional<double> (*override)(const QString &key));
void SetAppConfigBoolOverride(
	std::optional<bool> (*override)(const QString &key));
void SetAppConfigStringOverride(
	std::optional<QString> (*override)(const QString &key));
void SetRequestHolder(bool (*holder)(
	not_null<MTP::Instance*> instance,
	const MTP::details::SerializedRequest &request));

} // namespace Test
