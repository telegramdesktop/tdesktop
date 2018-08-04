/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/utils.h"

#define BETA_VERSION_MACRO (0ULL)

constexpr int AppVersion = 1003012;
constexpr str_const AppVersionStr = "1.3.12";
constexpr bool AppAlphaVersion = true;
constexpr uint64 AppBetaVersion = BETA_VERSION_MACRO;
