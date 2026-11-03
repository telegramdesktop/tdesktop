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

namespace Test {

// Lang::details::Current() reports every phrase read here. No-op until an
// observer is installed, so the call site needs no condition around it,
// and the Release branch of this module defines this function alone.
void NoteLangRead(ushort key);

// Main::AppConfig::getDouble() asks here before reading the server value.
// Returns nullopt until an override is installed; the Release branch of
// this module defines this function alone.
[[nodiscard]] std::optional<double> OverrideAppConfigDouble(
	const QString &key);

// Debug-only installers, deliberately left undefined in a Release build
// like RpcRetryProbe() in test_rpc_retry.h. Pass nullptr to remove. The
// observer runs on every phrase read on the main thread, so it must be
// cheap and must not read phrases itself.
void SetLangReadObserver(void (*observer)(ushort key));
void SetAppConfigDoubleOverride(
	std::optional<double> (*override)(const QString &key));

} // namespace Test
