/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/update_verify.h"
#include "core/version.h"

#include <QtCore/QString>

// These are set by the build, see Telegram/cmake/telegram_options.cmake:
// the channel as Core::Updates::Channel numeric value, the per-channel
// build counter and the discovery coordinates of the canary channels.

#ifndef TDESKTOP_UPDATE_CHANNEL
#define TDESKTOP_UPDATE_CHANNEL 0
#endif // TDESKTOP_UPDATE_CHANNEL

#ifndef TDESKTOP_CANARY_COUNTER
#define TDESKTOP_CANARY_COUNTER 0
#endif // TDESKTOP_CANARY_COUNTER

#ifndef TDESKTOP_CANARY_PRIVATE_CHANNEL_ID
#define TDESKTOP_CANARY_PRIVATE_CHANNEL_ID 0
#endif // TDESKTOP_CANARY_PRIVATE_CHANNEL_ID

#ifndef TDESKTOP_CANARY_METADATA_MSG_ID
#define TDESKTOP_CANARY_METADATA_MSG_ID 0
#endif // TDESKTOP_CANARY_METADATA_MSG_ID

namespace Core {

inline constexpr auto BuildUpdateChannel = Updates::Channel(
	TDESKTOP_UPDATE_CHANNEL);
inline constexpr auto CanaryBuildCounter = quint32(TDESKTOP_CANARY_COUNTER);
inline constexpr auto BuildIsCanary
	= (BuildUpdateChannel == Updates::Channel::CanaryPublic)
	|| (BuildUpdateChannel == Updates::Channel::CanaryPrivate);

static_assert(!BuildIsCanary || CanaryBuildCounter > 0);
static_assert(BuildIsCanary || CanaryBuildCounter == 0);

// The string-valued defines are passed as bare tokens and stringified,
// so they are only ever defined when non-empty: stringifying an empty
// macro is a hard error on MSVC (C4003 under /WX).
#ifdef TDESKTOP_CANARY_COMMIT
inline constexpr auto CanaryCommitHash
	= QT_STRINGIFY(TDESKTOP_CANARY_COMMIT);
#else // TDESKTOP_CANARY_COMMIT
inline constexpr auto CanaryCommitHash = "";
#endif // TDESKTOP_CANARY_COMMIT

#ifdef TDESKTOP_CANARY_PUBLIC_CHANNEL
inline constexpr auto CanaryPublicChannelUsername
	= QT_STRINGIFY(TDESKTOP_CANARY_PUBLIC_CHANNEL);
#else // TDESKTOP_CANARY_PUBLIC_CHANNEL
inline constexpr auto CanaryPublicChannelUsername = "";
#endif // TDESKTOP_CANARY_PUBLIC_CHANNEL
inline constexpr auto CanaryPrivateChannelId
	= qint64(TDESKTOP_CANARY_PRIVATE_CHANNEL_ID);
inline constexpr auto CanaryMetadataMessageId
	= int(TDESKTOP_CANARY_METADATA_MSG_ID);

[[nodiscard]] inline constexpr quint64 RunningUpdateVersion() {
	return Updates::MakeUpdateVersion(
		quint32(AppVersion),
		CanaryBuildCounter);
}

[[nodiscard]] inline QString CanaryVersionSuffix() {
	return BuildIsCanary
		? QStringLiteral(" canary #%1").arg(CanaryBuildCounter)
		: QString();
}

[[nodiscard]] inline QString CanaryTitleLabel() {
	return (BuildUpdateChannel == Updates::Channel::CanaryPrivate)
		? QStringLiteral("PRIVATE #%1").arg(CanaryBuildCounter)
		: (BuildUpdateChannel == Updates::Channel::CanaryPublic)
		? QStringLiteral("CANARY #%1").arg(CanaryBuildCounter)
		: QString();
}

} // namespace Core
