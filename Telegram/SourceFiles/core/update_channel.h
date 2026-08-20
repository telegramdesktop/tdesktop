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

#ifndef TDESKTOP_CANARY_COMMIT
#define TDESKTOP_CANARY_COMMIT
#endif // TDESKTOP_CANARY_COMMIT

#ifndef TDESKTOP_CANARY_PUBLIC_CHANNEL
#define TDESKTOP_CANARY_PUBLIC_CHANNEL
#endif // TDESKTOP_CANARY_PUBLIC_CHANNEL

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

inline constexpr auto CanaryCommitHash
	= QT_STRINGIFY(TDESKTOP_CANARY_COMMIT);
inline constexpr auto CanaryPublicChannelUsername
	= QT_STRINGIFY(TDESKTOP_CANARY_PUBLIC_CHANNEL);
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
	if (!BuildIsCanary) {
		return QString();
	}
	auto result = QStringLiteral(" canary #%1").arg(CanaryBuildCounter);
	if (CanaryCommitHash[0] != '\0') {
		result += QStringLiteral(" · ")
			+ QLatin1String(CanaryCommitHash);
	}
	return result;
}

} // namespace Core
