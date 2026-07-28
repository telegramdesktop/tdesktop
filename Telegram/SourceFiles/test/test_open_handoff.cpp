/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_open_handoff.h"

#include "test/test_launch_fuse.h"
#include "test/test_log.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/file_location.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_peer.h"
#include "history/history.h"
#include "history/history_item.h"
#include "iv/markdown/iv_markdown_common.h"
#include "ui/image/image_prepare.h"

#include <QtCore/QBuffer>

namespace Test {
namespace {

struct ImageBranch {
	QString mime;
	QString source;
	bool sizeOverLimit = false;
	bool readable = false;
	bool inApp = false;
};

[[nodiscard]] ImageBranch DetectImageBranch(
		not_null<DocumentData*> document,
		const std::shared_ptr<Data::DocumentMedia> &media,
		const Core::FileLocation &location) {
	auto result = ImageBranch();
	result.source = u"none"_q;
	if (document->size >= Images::kReadBytesLimit) {
		result.sizeOverLimit = true;
		return result;
	}
	const auto prefix = u"image/"_q;
	if (!location.isEmpty() && location.accessEnable()) {
		const auto guard = gsl::finally([&] {
			location.accessDisable();
		});
		const auto path = location.name();
		result.source = u"location"_q;
		result.mime = Core::MimeTypeForFile(QFileInfo(path)).name();
		result.readable = QImageReader(path).canRead();
		result.inApp = result.mime.startsWith(prefix) && result.readable;
	} else if (document->mimeString().startsWith(prefix)
		&& !media->bytes().isEmpty()) {
		auto bytes = media->bytes();
		auto buffer = QBuffer(&bytes);
		result.source = u"bytes"_q;
		result.mime = document->mimeString();
		result.readable = QImageReader(&buffer).canRead();
		result.inApp = result.readable;
	}
	return result;
}

[[nodiscard]] bool LauncherWouldWarn(
		Core::NameType nameType,
		bool isIpReveal,
		const QString &extension,
		HistoryItem *item) {
	if (item && item->history()->peer->isVerified()) {
		return false;
	}
	const auto &settings = Core::App().settings();
	return (isIpReveal && settings.ipRevealWarning())
		|| ((nameType == Core::NameType::Executable
			|| nameType == Core::NameType::Unknown)
			&& !settings.noWarningExtensions().contains(extension));
}

[[nodiscard]] bool MarkdownCandidate(const QString &path) {
	const auto info = QFileInfo(path);
	return info.exists()
		&& info.isFile()
		&& info.isReadable()
		&& Iv::Markdown::LooksLikeMarkdownFile(info.fileName());
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
	const auto &location = document->location(true);
	result.isTheme = document->isTheme();
	result.loadedFull = media->loaded(true);
	result.canBePlayed = media->canBePlayed();
	result.isAudioFile = document->isAudioFile();
	result.isVoiceMessage = document->isVoiceMessage();
	result.isVideoMessage = document->isVideoMessage();
	result.isSong = document->isSong();
	result.isVideoFile = document->isVideoFile();

	const auto image = DetectImageBranch(document, media, location);
	result.sizeOverImageLimit = image.sizeOverLimit;
	result.imageMime = image.mime;
	result.imageSource = image.source;
	result.imageReadable = image.readable;
	result.isImageInApp = image.inApp;

	result.path = document->filepath(true);
	result.pathExists = !result.path.isEmpty()
		&& QFileInfo::exists(result.path);
	result.markdownCandidate = MarkdownCandidate(result.path);
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
	result.launcherWouldWarn = LauncherWouldWarn(
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
