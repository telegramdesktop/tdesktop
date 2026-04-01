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
#include "ui/text/text_utilities.h"
#include "ui/ui_utility.h"
#include "core/mime_type.h"
#include "styles/style_chat.h"

#include <QtCore/QFileInfo>

namespace Ui {

SingleFilePreview::SingleFilePreview(
	QWidget *parent,
	const style::ComposeControls &st,
	const PreparedFile &file,
	const Text::MarkedContext &captionContext,
	AttachControls::Type type)
: AbstractSingleFilePreview(parent, st, type, captionContext) {
	preparePreview(file);
}

SingleFilePreview::SingleFilePreview(
	QWidget *parent,
	const style::ComposeControls &st,
	const PreparedFile &file,
	AttachControls::Type type)
: SingleFilePreview(parent, st, file, {}, type) {
}

void SingleFilePreview::setDisplayName(const QString &displayName) {
	AbstractSingleFilePreview::setDisplayName(displayName);
}

void SingleFilePreview::setCaption(const TextWithTags &caption) {
	AbstractSingleFilePreview::setCaption(caption);
}

void SingleFilePreview::preparePreview(const PreparedFile &file) {
	AbstractSingleFilePreview::Data data;

	auto preview = QImage();
	if (const auto image = std::get_if<PreparedFileInformation::Image>(
		&file.information->media)) {
		preview = file.preview.isNull() ? image->data : file.preview;
	} else if (const auto video = std::get_if<PreparedFileInformation::Video>(
		&file.information->media)) {
		preview = video->thumbnail;
	}
	prepareThumbFor(data, preview);
	const auto filepath = file.path;
	if (filepath.isEmpty()) {
		const auto fallbackName = u"image.png"_q;
		const auto displayName = file.displayName.isEmpty()
			? fallbackName
			: file.displayName;
		data.name = displayName;
		data.statusText = FormatImageSizeText(file.originalDimensions);
		data.fileIsImage = true;
	} else {
		auto fileinfo = QFileInfo(filepath);
		auto filename = file.displayName.isEmpty()
			? fileinfo.fileName()
			: file.displayName;
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
	auto caption = TextWithEntities{
		file.caption.text,
		TextUtilities::ConvertTextTagsToEntities(file.caption.tags),
	};
	caption = TextUtilities::SingleLine(caption);
	data.caption.setMarkedText(
		st::defaultTextStyle,
		caption,
		kMarkupTextOptions,
		captionContext());

	setData(std::move(data));
}

} // namespace Ui
