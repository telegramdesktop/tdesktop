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
} // namespace Ui

namespace Storage {

enum class MimeDataState {
	None,
	Files,
	PhotoFiles,
	Image,
};

std::optional<Ui::PreparedList> PreparedFileFromFilesDialog(
	FileDialog::OpenResult &&result,
	bool isAlbum,
	Fn<void(tr::phrase<>)> errorCallback,
	int previewWidth);
MimeDataState ComputeMimeDataState(const QMimeData *data);
bool ValidateDragData(not_null<const QMimeData*> data, bool isAlbum);
Ui::PreparedList PrepareMediaList(const QList<QUrl> &files, int previewWidth);
Ui::PreparedList PrepareMediaList(const QStringList &files, int previewWidth);
Ui::PreparedList PrepareMediaFromImage(
	QImage &&image,
	QByteArray &&content,
	int previewWidth);

} // namespace Storage
