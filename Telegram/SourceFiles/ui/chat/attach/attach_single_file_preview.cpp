/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_single_file_preview.h"

#include "ui/chat/attach/attach_prepare.h"
#include "ui/text/format_song_name.h"
#include "ui/text/format_values.h"
#include "core/mime_type.h"
#include "styles/style_chat.h"

#include <QtCore/QFileInfo>

namespace Ui {

SingleFilePreview::SingleFilePreview(
	QWidget *parent,
	const PreparedFile &file,
	AttachControls::Type type)
: AbstractSingleFilePreview(parent, type) {
	preparePreview(file);
}

void SingleFilePreview::preparePreview(const PreparedFile &file) {
	AbstractSingleFilePreview::Data data;

	auto preview = QImage();
	if (const auto image = std::get_if<PreparedFileInformation::Image>(
		&file.information->media)) {
		preview = image->data;
	} else if (const auto video = std::get_if<PreparedFileInformation::Video>(
		&file.information->media)) {
		preview = video->thumbnail;
	}
	prepareThumbFor(data, preview);
	const auto filepath = file.path;
	if (filepath.isEmpty()) {
		auto filename = "image.png";
		data.name = filename;
		data.statusText = FormatImageSizeText(preview.size()
			/ preview.devicePixelRatio());
		data.fileIsImage = true;
	} else {
		auto fileinfo = QFileInfo(filepath);
		auto filename = fileinfo.fileName();
		data.fileIsImage = Core::FileIsImage(
			filename,
			Core::MimeTypeForFile(fileinfo).name());

		auto songTitle = QString();
		auto songPerformer = QString();
		if (file.information) {
			if (const auto song = std::get_if<PreparedFileInformation::Song>(
					&file.information->media)) {
				songTitle = song->title;
				songPerformer = song->performer;
				data.fileIsAudio = true;

				if (auto cover = song->cover; !cover.isNull()) {
					data.fileThumb = Ui::PrepareSongCoverForThumbnail(
						cover,
						st::attachPreviewLayout.thumbSize);
				}
			}
		}

		data.name = Text::FormatSongName(filename, songTitle, songPerformer)
			.string();
		data.statusText = FormatSizeText(fileinfo.size());
	}

	setData(data);
}

} // namespace Ui
