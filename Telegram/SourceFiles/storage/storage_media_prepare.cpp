/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_media_prepare.h"

#include "editor/photo_editor_common.h"
#include "platform/platform_file_utilities.h"
#include "storage/localimageloader.h"
#include "core/mime_type.h"
#include "ui/image/image_prepare.h"
#include "ui/chat/attach/attach_extensions.h"
#include "ui/chat/attach/attach_prepare.h"
#include "app.h"

#include <QtCore/QSemaphore>
#include <QtCore/QMimeData>

namespace Storage {
namespace {

using Ui::PreparedFileInformation;
using Ui::PreparedFile;
using Ui::PreparedList;

using Image = PreparedFileInformation::Image;

bool HasExtensionFrom(const QString &file, const QStringList &extensions) {
	for (const auto &extension : extensions) {
		const auto ext = file.right(extension.size());
		if (ext.compare(extension, Qt::CaseInsensitive) == 0) {
			return true;
		}
	}
	return false;
}

bool ValidPhotoForAlbum(
		const Image &image,
		const QString &mime) {
	if (image.animated
		|| Core::IsMimeSticker(mime)
		|| (!mime.isEmpty() && !mime.startsWith(u"image/"))) {
		return false;
	}
	const auto width = image.data.width();
	const auto height = image.data.height();
	return Ui::ValidateThumbDimensions(width, height);
}

bool ValidVideoForAlbum(const PreparedFileInformation::Video &video) {
	const auto width = video.thumbnail.width();
	const auto height = video.thumbnail.height();
	return Ui::ValidateThumbDimensions(width, height);
}

QSize PrepareShownDimensions(const QImage &preview) {
	constexpr auto kMaxWidth = 1280;
	constexpr auto kMaxHeight = 1280;

	const auto result = preview.size();
	return (result.width() > kMaxWidth || result.height() > kMaxHeight)
		? result.scaled(kMaxWidth, kMaxHeight, Qt::KeepAspectRatio)
		: result;
}

void PrepareDetailsInParallel(PreparedList &result, int previewWidth) {
	Expects(result.files.size() <= Ui::MaxAlbumItems());

	if (result.files.empty()) {
		return;
	}
	QSemaphore semaphore;
	for (auto &file : result.files) {
		crl::async([=, &semaphore, &file] {
			PrepareDetails(file, previewWidth);
			semaphore.release();
		});
	}
	semaphore.acquire(result.files.size());
}

} // namespace

bool ValidatePhotoEditorMediaDragData(not_null<const QMimeData*> data) {
	if (data->urls().size() > 1) {
		return false;
	} else if (data->hasImage()) {
		return true;
	}

	if (data->hasUrls()) {
		const auto url = data->urls().front();
		if (url.isLocalFile()) {
			using namespace Core;
			const auto info = QFileInfo(Platform::File::UrlToLocal(url));
			const auto filename = info.fileName();
			return FileIsImage(filename, MimeTypeForFile(info).name())
				&& HasExtensionFrom(filename, Ui::ExtensionsForCompression());
		}
	}

	return false;
}

bool ValidateEditMediaDragData(
		not_null<const QMimeData*> data,
		Ui::AlbumType albumType) {
	if (data->urls().size() > 1) {
		return false;
	} else if (data->hasImage()) {
		return (albumType != Ui::AlbumType::Music);
	}

	if (albumType == Ui::AlbumType::PhotoVideo && data->hasUrls()) {
		const auto url = data->urls().front();
		if (url.isLocalFile()) {
			using namespace Core;
			const auto info = QFileInfo(Platform::File::UrlToLocal(url));
			return IsMimeAcceptedForPhotoVideoAlbum(MimeTypeForFile(info).name());
		}
	}

	return true;
}

MimeDataState ComputeMimeDataState(const QMimeData *data) {
	if (!data || data->hasFormat(qsl("application/x-td-forward"))) {
		return MimeDataState::None;
	}

	if (data->hasImage()) {
		return MimeDataState::Image;
	}

	const auto uriListFormat = qsl("text/uri-list");
	if (!data->hasFormat(uriListFormat)) {
		return MimeDataState::None;
	}

	const auto &urls = data->urls();
	if (urls.isEmpty()) {
		return MimeDataState::None;
	}

	const auto imageExtensions = Ui::ImageExtensions();
	auto files = QStringList();
	auto allAreSmallImages = true;
	for (const auto &url : urls) {
		if (!url.isLocalFile()) {
			return MimeDataState::None;
		}
		const auto file = Platform::File::UrlToLocal(url);

		const auto info = QFileInfo(file);
		if (info.isDir()) {
			return MimeDataState::None;
		}

		const auto filesize = info.size();
		if (filesize > kFileSizeLimit) {
			return MimeDataState::None;
		} else if (allAreSmallImages) {
			if (filesize > App::kImageSizeLimit) {
				allAreSmallImages = false;
			} else if (!HasExtensionFrom(file, imageExtensions)) {
				allAreSmallImages = false;
			}
		}
	}
	return allAreSmallImages
		? MimeDataState::PhotoFiles
		: MimeDataState::Files;
}

PreparedList PrepareMediaList(const QList<QUrl> &files, int previewWidth) {
	auto locals = QStringList();
	locals.reserve(files.size());
	for (const auto &url : files) {
		if (!url.isLocalFile()) {
			return {
				PreparedList::Error::NonLocalUrl,
				url.toDisplayString()
			};
		}
		locals.push_back(Platform::File::UrlToLocal(url));
	}
	return PrepareMediaList(locals, previewWidth);
}

PreparedList PrepareMediaList(const QStringList &files, int previewWidth) {
	auto result = PreparedList();
	result.files.reserve(files.size());
	for (const auto &file : files) {
		const auto fileinfo = QFileInfo(file);
		const auto filesize = fileinfo.size();
		if (fileinfo.isDir()) {
			return {
				PreparedList::Error::Directory,
				file
			};
		} else if (filesize <= 0) {
			return {
				PreparedList::Error::EmptyFile,
				file
			};
		} else if (filesize > kFileSizeLimit) {
			return {
				PreparedList::Error::TooLargeFile,
				file
			};
		}
		if (result.files.size() < Ui::MaxAlbumItems()) {
			result.files.emplace_back(file);
			result.files.back().size = filesize;
		} else {
			result.filesToProcess.emplace_back(file);
			result.files.back().size = filesize;
		}
	}
	PrepareDetailsInParallel(result, previewWidth);
	return result;
}

PreparedList PrepareMediaFromImage(
		QImage &&image,
		QByteArray &&content,
		int previewWidth) {
	auto result = PreparedList();
	auto file = PreparedFile(QString());
	file.content = content;
	if (file.content.isEmpty()) {
		file.information = std::make_unique<PreparedFileInformation>();
		const auto animated = false;
		FileLoadTask::FillImageInformation(
			std::move(image),
			animated,
			file.information);
	}
	result.files.push_back(std::move(file));
	PrepareDetailsInParallel(result, previewWidth);
	return result;
}

std::optional<PreparedList> PreparedFileFromFilesDialog(
		FileDialog::OpenResult &&result,
		Fn<bool(const Ui::PreparedList&)> checkResult,
		Fn<void(tr::phrase<>)> errorCallback,
		int previewWidth) {
	if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
		return std::nullopt;
	}

	auto list = result.remoteContent.isEmpty()
		? PrepareMediaList(result.paths, previewWidth)
		: PrepareMediaFromImage(
			QImage(),
			std::move(result.remoteContent),
			previewWidth);
	if (list.error != PreparedList::Error::None) {
		errorCallback(tr::lng_send_media_invalid_files);
		return std::nullopt;
	} else if (!checkResult(list)) {
		return std::nullopt;
	} else {
		return list;
	}
}

void PrepareDetails(PreparedFile &file, int previewWidth) {
	if (!file.path.isEmpty()) {
		file.information = FileLoadTask::ReadMediaInformation(
			file.path,
			QByteArray(),
			Core::MimeTypeForFile(QFileInfo(file.path)).name());
	} else if (!file.content.isEmpty()) {
		file.information = FileLoadTask::ReadMediaInformation(
			QString(),
			file.content,
			Core::MimeTypeForData(file.content).name());
	} else {
		Assert(file.information != nullptr);
	}

	using Video = PreparedFileInformation::Video;
	using Song = PreparedFileInformation::Song;
	if (const auto image = std::get_if<Image>(
			&file.information->media)) {
		if (ValidPhotoForAlbum(*image, file.information->filemime)) {
			UpdateImageDetails(file, previewWidth);
			file.type = PreparedFile::Type::Photo;
		} else if (Core::IsMimeSticker(file.information->filemime)) {
			file.type = PreparedFile::Type::None;
		}
	} else if (const auto video = std::get_if<Video>(
			&file.information->media)) {
		if (ValidVideoForAlbum(*video)) {
			auto blurred = Images::prepareBlur(Images::prepareOpaque(video->thumbnail));
			file.shownDimensions = PrepareShownDimensions(video->thumbnail);
			file.preview = std::move(blurred).scaledToWidth(
				previewWidth * cIntRetinaFactor(),
				Qt::SmoothTransformation);
			Assert(!file.preview.isNull());
			file.preview.setDevicePixelRatio(cRetinaFactor());
			file.type = PreparedFile::Type::Video;
		}
	} else if (const auto song = std::get_if<Song>(&file.information->media)) {
		file.type = PreparedFile::Type::Music;
	}
}

void UpdateImageDetails(PreparedFile &file, int previewWidth) {
	const auto image = std::get_if<Image>(&file.information->media);
	if (!image) {
		return;
	}
	const auto &preview = image->modifications
		? Editor::ImageModified(image->data, image->modifications)
		: image->data;
	file.shownDimensions = PrepareShownDimensions(preview);
	file.preview = Images::prepareOpaque(preview.scaledToWidth(
		std::min(previewWidth, style::ConvertScale(preview.width()))
			* cIntRetinaFactor(),
		Qt::SmoothTransformation));
	Assert(!file.preview.isNull());
	file.preview.setDevicePixelRatio(cRetinaFactor());
}

bool ApplyModifications(const PreparedList &list) {
	auto applied = false;
	for (auto &file : list.files) {
		const auto image = std::get_if<Image>(&file.information->media);
		if (!image || !image->modifications) {
			continue;
		}
		applied = true;
		image->data = Editor::ImageModified(
			std::move(image->data),
			image->modifications);
	}
	return applied;
}

} // namespace Storage

