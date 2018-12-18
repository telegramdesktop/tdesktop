/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/media/history_media_common.h"

#include "layout.h"
#include "data/data_document.h"
#include "history/view/history_view_element.h"
#include "history/media/history_media_grouped.h"
#include "history/media/history_media_photo.h"
#include "history/media/history_media_gif.h"
#include "history/media/history_media_document.h"
#include "history/media/history_media_sticker.h"
#include "history/media/history_media_video.h"
#include "styles/style_history.h"

int documentMaxStatusWidth(DocumentData *document) {
	auto result = st::normalFont->width(formatDownloadText(document->size, document->size));
	if (const auto song = document->song()) {
		accumulate_max(result, st::normalFont->width(formatPlayedText(song->duration, song->duration)));
		accumulate_max(result, st::normalFont->width(formatDurationAndSizeText(song->duration, document->size)));
	} else if (const auto voice = document->voice()) {
		accumulate_max(result, st::normalFont->width(formatPlayedText(voice->duration, voice->duration)));
		accumulate_max(result, st::normalFont->width(formatDurationAndSizeText(voice->duration, document->size)));
	} else if (document->isVideoFile()) {
		accumulate_max(result, st::normalFont->width(formatDurationAndSizeText(document->duration(), document->size)));
	} else {
		accumulate_max(result, st::normalFont->width(formatSizeText(document->size)));
	}
	return result;
}

void PaintInterpolatedIcon(
		Painter &p,
		const style::icon &a,
		const style::icon &b,
		float64 b_ratio,
		QRect rect) {
	PainterHighQualityEnabler hq(p);
	p.save();
	p.translate(rect.center());
	p.setOpacity(b_ratio);
	p.scale(b_ratio, b_ratio);
	b.paintInCenter(p, rect.translated(-rect.center()));
	p.restore();

	p.save();
	p.translate(rect.center());
	p.setOpacity(1. - b_ratio);
	p.scale(1. - b_ratio, 1. - b_ratio);
	a.paintInCenter(p, rect.translated(-rect.center()));
	p.restore();
}

std::unique_ptr<HistoryMedia> CreateAttach(
		not_null<HistoryView::Element*> parent,
		DocumentData *document,
		PhotoData *photo,
		const std::vector<std::unique_ptr<Data::Media>> &collage) {
	if (!collage.empty()) {
		return std::make_unique<HistoryGroupedMedia>(parent, collage);
	} else if (document) {
		if (document->sticker()) {
			return std::make_unique<HistorySticker>(parent, document);
		} else if (document->isAnimation()) {
			return std::make_unique<HistoryGif>(
				parent,
				document);
		} else if (document->isVideoFile()) {
			return std::make_unique<HistoryVideo>(
				parent,
				parent->data(),
				document);
		}
		return std::make_unique<HistoryDocument>(
			parent,
			document);
	} else if (photo) {
		return std::make_unique<HistoryPhoto>(
			parent,
			parent->data(),
			photo);
	}
	return nullptr;
}

int unitedLineHeight() {
	return qMax(st::webPageTitleFont->height, st::webPageDescriptionFont->height);
}
