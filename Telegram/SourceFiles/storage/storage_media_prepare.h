/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/file_utilities.h"
#include "lang/lang_keys.h"

namespace Ui {
struct PreparedFileInformation;
struct PreparedFile;
struct PreparedList;
enum class AlbumType;
} // namespace Ui

namespace Storage {

enum class MimeDataState {
	None,
	Files,
	PhotoFiles,
	Image,
};

[[nodiscard]] std::optional<Ui::PreparedList> PreparedFileFromFilesDialog(
	FileDialog::OpenResult &&result,
	Fn<bool(const Ui::PreparedList&)> checkResult,
	Fn<void(tr::phrase<>)> errorCallback,
	int previewWidth);
[[nodiscard]] MimeDataState ComputeMimeDataState(const QMimeData *data);
[[nodiscard]] bool ValidatePhotoEditorMediaDragData(
	not_null<const QMimeData*> data);
[[nodiscard]] bool ValidateEditMediaDragData(
	not_null<const QMimeData*> data,
	Ui::AlbumType albumType);
[[nodiscard]] Ui::PreparedList PrepareMediaList(
	const QList<QUrl> &files,
	int previewWidth);
[[nodiscard]] Ui::PreparedList PrepareMediaList(
	const QStringList &files,
	int previewWidth);
[[nodiscard]] Ui::PreparedList PrepareMediaFromImage(
	QImage &&image,
	QByteArray &&content,
	int previewWidth);
void PrepareDetails(Ui::PreparedFile &file, int previewWidth);
void UpdateImageDetails(Ui::PreparedFile &file, int previewWidth);

[[nodiscard]] bool ApplyModifications(const Ui::PreparedList &list);

} // namespace Storage
