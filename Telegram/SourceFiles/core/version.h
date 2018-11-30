/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#define TDESKTOP_REQUESTED_ALPHA_VERSION (1004007006ULL)

#ifdef TDESKTOP_OFFICIAL_TARGET
#define TDESKTOP_ALPHA_VERSION TDESKTOP_REQUESTED_ALPHA_VERSION
#else // TDESKTOP_OFFICIAL_TARGET
#define TDESKTOP_ALPHA_VERSION (0ULL)
#endif // TDESKTOP_OFFICIAL_TARGET

constexpr auto AppVersion = 1004007;
constexpr auto AppVersionStr = "1.4.7";
constexpr auto AppBetaVersion = false;
constexpr auto AppAlphaVersion = TDESKTOP_ALPHA_VERSION;
