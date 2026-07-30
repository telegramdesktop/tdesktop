/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_open_handoff.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_document_resolver.h"
#include "data/data_peer.h"
#include "history/history.h"
#include "history/history_item.h"
#include "iv/markdown/iv_markdown_common.h"
#include "test/test_launch_fuse.h"
#include "test/test_log.h"

namespace Test {
namespace {

[[nodiscard]] QString ImageSourceText(Data::ImageOpenSource source) {
	switch (source) {
	case Data::ImageOpenSource::None: return u"none"_q;
	case Data::ImageOpenSource::Location: return u"location"_q;
	case Data::ImageOpenSource::Bytes: return u"bytes"_q;
	}
	Unexpected("Data::ImageOpenSource value in ImageSourceText.");
}

[[nodiscard]] QString ResolveBranch(
		not_null<DocumentData*> document,
		const OpenHandoff &handoff) {
	if (handoff.isTheme && handoff.loadedFull) {
		return u"theme"_q;
	} else if (handoff.canBePlayed) {
		return (handoff.isAudioFile
			|| handoff.isVoiceMessage
			|| handoff.isVideoMessage)
			? u"player"_q
			: u"viewer"_q;
	} else if (handoff.isImageInApp) {
		return u"image"_q;
	} else if (!handoff.path.isEmpty()) {
		return handoff.markdownCandidate
			? u"markdown-or-launcher"_q
			: u"launcher"_q;
	} else if (document->status == FileReady
		|| document->status == FileDownloadFailed) {
		return u"download"_q;
	}
	return u"nothing"_q;
}

[[nodiscard]] QString FlagText(bool value) {
	return value ? u"1"_q : u"0"_q;
}

void CheckExpectedString(
		const std::optional<QString> &wanted,
		const QString &actual,
		const QString &what) {
	if (!wanted.has_value()) {
		return;
	}
	Check(
		*wanted == actual,
		what,
		u"expected %1, actual %2"_q.arg(*wanted, actual));
}

void CheckExpectedFlag(
		const std::optional<bool> &wanted,
		bool actual,
		const QString &what) {
	if (!wanted.has_value()) {
		return;
	}
	Check(
		*wanted == actual,
		what,
		u"expected %1, actual %2"_q.arg(
			FlagText(*wanted),
			FlagText(actual)));
}

void CheckExpectedNameType(
		const std::optional<Core::NameType> &wanted,
		Core::NameType actual,
		const QString &what) {
	if (!wanted.has_value()) {
		return;
	}
	Check(
		*wanted == actual,
		what,
		u"expected %1, actual %2"_q.arg(
			QString::number(int(*wanted)),
			QString::number(int(actual))));
}

} // namespace

OpenHandoff DescribeOpenHandoff(
		not_null<DocumentData*> document,
		HistoryItem *item) {
	auto result = OpenHandoff();
	result.isNull = document->isNull();
	if (result.isNull) {
		result.branch = u"none"_q;
		return result;
	}
	const auto media = document->createMediaView();
	static_cast<void>(document->location(true));
	result.isTheme = document->isTheme();
	result.loadedFull = media->loaded(true);
	result.canBePlayed = media->canBePlayed();
	result.isAudioFile = document->isAudioFile();
	result.isVoiceMessage = document->isVoiceMessage();
	result.isVideoMessage = document->isVideoMessage();
	result.isSong = document->isSong();
	result.isVideoFile = document->isVideoFile();

	{
		const auto image = Data::CheckImageOpenInApp(document, media);
		result.sizeOverImageLimit = image.sizeOverLimit;
		result.imageMime = image.mime;
		result.imageSource = ImageSourceText(image.source);
		result.imageReadable = image.readable;
		result.isImageInApp = image.openInApp;
	}

	result.path = document->filepath(true);
	result.pathExists = !result.path.isEmpty()
		&& QFileInfo::exists(result.path);
	const auto pathInfo = QFileInfo(result.path);
	result.markdownCandidate = Iv::Markdown::IsReadableLocalFile(pathInfo)
		&& Iv::Markdown::LooksLikeMarkdownFile(pathInfo.fileName());
	result.nameType = Core::DetectNameType(result.path);
	result.ipRevealing = (result.nameType != Core::NameType::Executable)
		&& Core::IsIpRevealingPath(result.path);
	result.extension = Core::FileExtension(result.path).toLower();
	result.forcesOpenWith = result.extension.isEmpty();

	const auto &settings = Core::App().settings();
	result.ipRevealWarning = settings.ipRevealWarning();
	result.noWarningExtension = settings.noWarningExtensions().contains(
		result.extension);
	result.peerVerified = item
		&& item->history()->peer->isVerified();
	result.launcherWouldWarn = Data::LauncherWouldWarn(
		settings,
		result.nameType,
		result.ipRevealing,
		result.extension,
		item);

	result.branch = ResolveBranch(document, result);
	if (result.path.isEmpty() && !media->bytes().isEmpty()) {
		Note(u"open handoff: path empty with cached bytes; the helper "
			u"skips saveFromDataSilent(), so ResolveDocument would "
			u"resolve a path here when the download path is not asked "
			u"for"_q);
	}
	return result;
}

void LogOpenHandoff(const QString &surface, const OpenHandoff &handoff) {
	auto values = QStringList();
	const auto add = [&](const QString &key, const QString &value) {
		values.push_back(key + u"="_q + value);
	};
	const auto addFlag = [&](const QString &key, bool value) {
		add(key, FlagText(value));
	};
	add(u"surface"_q, surface);
	add(u"branch"_q, handoff.branch);
	addFlag(u"isNull"_q, handoff.isNull);
	addFlag(u"isTheme"_q, handoff.isTheme);
	addFlag(u"loaded"_q, handoff.loadedFull);
	addFlag(u"canBePlayed"_q, handoff.canBePlayed);
	addFlag(u"isAudioFile"_q, handoff.isAudioFile);
	addFlag(u"isVoiceMessage"_q, handoff.isVoiceMessage);
	addFlag(u"isVideoMessage"_q, handoff.isVideoMessage);
	addFlag(u"isSong"_q, handoff.isSong);
	addFlag(u"isVideoFile"_q, handoff.isVideoFile);
	addFlag(u"isImage"_q, handoff.isImageInApp);
	addFlag(u"sizeOverImageLimit"_q, handoff.sizeOverImageLimit);
	addFlag(u"imageReadable"_q, handoff.imageReadable);
	add(u"imageSource"_q, handoff.imageSource);
	add(u"imageMime"_q, handoff.imageMime);
	addFlag(u"markdown"_q, handoff.markdownCandidate);
	add(u"nameType"_q, QString::number(int(handoff.nameType)));
	add(u"extension"_q, handoff.extension);
	addFlag(u"ipRevealing"_q, handoff.ipRevealing);
	addFlag(u"ipRevealWarning"_q, handoff.ipRevealWarning);
	addFlag(u"noWarningExtension"_q, handoff.noWarningExtension);
	addFlag(u"peerVerified"_q, handoff.peerVerified);
	addFlag(u"forcesOpenWith"_q, handoff.forcesOpenWith);
	addFlag(u"launcherWouldWarn"_q, handoff.launcherWouldWarn);
	addFlag(u"pathExists"_q, handoff.pathExists);
	LogRaw(u"OPEN_HANDOFF: "_q + values.join(u" "_q));
	LogRaw(u"OPEN_HANDOFF_PATH: surface=%1 path=%2"_q.arg(
		surface,
		handoff.path));
	LogRaw(u"OPEN_HANDOFF_DONE: surface=%1 launched=0 blockedSoFar=%2"_q.arg(
		surface,
		QString::number(BlockedLaunches().size())));
}

OpenHandoff CheckOpenHandoff(
		const QString &surface,
		not_null<DocumentData*> document,
		HistoryItem *item,
		const OpenHandoffExpected &expected) {
	auto result = DescribeOpenHandoff(document, item);
	LogOpenHandoff(surface, result);
	CheckExpectedString(
		expected.branch,
		result.branch,
		u"%1 branch"_q.arg(surface));
	CheckExpectedString(
		expected.path,
		result.path,
		u"%1 path"_q.arg(surface));
	CheckExpectedNameType(
		expected.nameType,
		result.nameType,
		u"%1 nameType"_q.arg(surface));
	CheckExpectedFlag(
		expected.pathExists,
		result.pathExists,
		u"%1 pathExists"_q.arg(surface));
	CheckExpectedFlag(
		expected.canBePlayed,
		result.canBePlayed,
		u"%1 canBePlayed"_q.arg(surface));
	CheckExpectedFlag(
		expected.markdownCandidate,
		result.markdownCandidate,
		u"%1 markdown"_q.arg(surface));
	CheckExpectedFlag(
		expected.launcherWouldWarn,
		result.launcherWouldWarn,
		u"%1 launcherWouldWarn"_q.arg(surface));
	return result;
}

} // namespace Test
