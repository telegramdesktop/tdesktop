/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/mime_type.h"

class DocumentData;
class HistoryItem;

namespace Test {

// Every branch value Data::ResolveDocument and LaunchWithWarning read
// before the process hands a document to the operating system, taken from
// those same product functions rather than re-derived here.
struct OpenHandoff {
	QString branch;
	QString path;
	QString extension;
	QString imageMime;
	QString imageSource;
	Core::NameType nameType = Core::NameType::Unknown;
	bool isNull = false;
	bool isTheme = false;
	bool loadedFull = false;
	bool canBePlayed = false;
	bool isAudioFile = false;
	bool isVoiceMessage = false;
	bool isVideoMessage = false;
	bool isSong = false;
	bool isVideoFile = false;
	bool isImageInApp = false;
	bool sizeOverImageLimit = false;
	bool imageReadable = false;
	bool pathExists = false;
	bool markdownCandidate = false;
	bool ipRevealing = false;
	bool ipRevealWarning = false;
	bool noWarningExtension = false;
	bool peerVerified = false;
	bool launcherWouldWarn = false;
	bool forcesOpenWith = false;
};

// Only the engaged expectations are asserted.
struct OpenHandoffExpected {
	std::optional<QString> branch;
	std::optional<QString> path;
	std::optional<Core::NameType> nameType;
	std::optional<bool> pathExists;
	std::optional<bool> canBePlayed;
	std::optional<bool> markdownCandidate;
	std::optional<bool> launcherWouldWarn;
};

// Reads the branch values without launching anything. It does not perform
// the real path's one destructive side effect: it never calls
// saveFromDataSilent(), so a cache-only document can report an empty path.
[[nodiscard]] OpenHandoff DescribeOpenHandoff(
	not_null<DocumentData*> document,
	HistoryItem *item);

void LogOpenHandoff(const QString &surface, const OpenHandoff &handoff);

// Describe + log + assert the engaged expectations as ordinary harness
// results. This is the scenario-facing entry point.
OpenHandoff CheckOpenHandoff(
	const QString &surface,
	not_null<DocumentData*> document,
	HistoryItem *item,
	const OpenHandoffExpected &expected = {});

} // namespace Test
