/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/exteraGramDesktop/exteraGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "base/const_string.h"

#define TDESKTOP_REQUESTED_ALPHA_VERSION (0ULL)

#ifdef TDESKTOP_ALLOW_CLOSED_ALPHA
#define TDESKTOP_ALPHA_VERSION TDESKTOP_REQUESTED_ALPHA_VERSION
#else // TDESKTOP_ALLOW_CLOSED_ALPHA
#define TDESKTOP_ALPHA_VERSION (0ULL)
#endif // TDESKTOP_ALLOW_CLOSED_ALPHA

// used in Updater.cpp and Setup.iss for Windows
<<<<<<< HEAD
constexpr auto AppId = "{4356CE01-4137-4C55-9F8B-FB4EEBB6EC0C}"_cs;
constexpr auto AppNameOld = "exteraGram Win (Unofficial)"_cs;
constexpr auto AppName = "exteraGram Desktop"_cs;
constexpr auto AppFile = "exteraGram"_cs;
constexpr auto AppVersion = 4008012;
constexpr auto AppVersionStr = "4.8.12";
constexpr auto AppBetaVersion = false;
=======
constexpr auto AppId = "{53F49750-6209-4FBF-9CA8-7A333C87D1ED}"_cs;
constexpr auto AppNameOld = "Telegram Win (Unofficial)"_cs;
constexpr auto AppName = "Telegram Desktop"_cs;
constexpr auto AppFile = "Telegram"_cs;
constexpr auto AppVersion = 4008012;
constexpr auto AppVersionStr = "4.8.12";
constexpr auto AppBetaVersion = true;
>>>>>>> ff2df4b1e5f55014990775761d64305fb51db02a
constexpr auto AppAlphaVersion = TDESKTOP_ALPHA_VERSION;
