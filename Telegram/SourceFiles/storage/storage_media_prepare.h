/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/file_utilities.h"

namespace tr {
template <typename ...>
struct phrase;
} // namespace tr

namespace Ui {
struct PreparedFileInformation;
struct PreparedFile;
struct PreparedList;
enum class AlbumType;
} // namespace Ui

namespace Editor {
struct PhotoModifications;
} // namespace Editor

namespace Storage {

enum class MimeDataState {
	None,
	Files,
	PhotoFiles,
	MediaFiles,
	//PremiumFile,
	Image,
};

[[nodiscard]] std::optional<Ui::PreparedList> PreparedFileFromFilesDialog(
	FileDialog::OpenResult &&result,
	Fn<bool(const Ui::PreparedList&)> checkResult,
	Fn<void(tr::phrase<>)> errorCallback,
	int previewWidth,
	bool premium);
[[nodiscard]] MimeDataState ComputeMimeDataState(const QMimeData *data);
[[nodiscard]] bool ValidatePhotoEditorMediaDragData(
	not_null<const QMimeData*> data);
[[nodiscard]] bool ValidateEditMediaDragData(
	not_null<const QMimeData*> data,
	Ui::AlbumType albumType);
[[nodiscard]] Ui::PreparedList PrepareMediaList(
	const QList<QUrl> &files,
	int previewWidth,
	bool premium,
	Fn<void(const Ui::PreparedList &)> errorCallback = nullptr);
[[nodiscard]] Ui::PreparedList PrepareMediaList(
	const QStringList &files,
	int previewWidth,
	bool premium,
	Fn<void(const Ui::PreparedList &)> errorCallback = nullptr);
[[nodiscard]] Ui::PreparedList PrepareMediaFromImage(
	QImage &&image,
	QByteArray &&content,
	int previewWidth);
void PrepareDetails(Ui::PreparedFile &file, int previewWidth, int sideLimit);
struct VideoDetails {
	QSize originalDimensions;
	QSize shownDimensions;
	QImage preview;
};

[[nodiscard]] VideoDetails ComputeVideoDetails(
	const QImage &thumbnail,
	const Editor::PhotoModifications &geometry,
	int previewWidth,
	int sideLimit);
void ApplyVideoDetails(Ui::PreparedFile &file, VideoDetails &&details);
void UpdateVideoDetails(
	Ui::PreparedFile &file,
	int previewWidth,
	int sideLimit);
void UpdateImageDetails(
	Ui::PreparedFile &file,
	int previewWidth,
	int sideLimit);

bool ApplyModifications(Ui::PreparedList &list, bool composeAnimated = false);

} // namespace Storage
