/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_media_common.h"

#include "ui/text/format_values.h"
#include "data/data_document.h"
#include "history/view/history_view_element.h"
#include "history/view/media/history_view_media_grouped.h"
#include "history/view/media/history_view_photo.h"
#include "history/view/media/history_view_gif.h"
#include "history/view/media/history_view_document.h"
#include "history/view/media/history_view_sticker.h"
#include "history/view/media/history_view_theme_document.h"
#include "styles/style_chat.h"

namespace HistoryView {

int documentMaxStatusWidth(DocumentData *document) {
	auto result = st::normalFont->width(Ui::FormatDownloadText(document->size, document->size));
	const auto duration = document->getDuration();
	if (const auto song = document->song()) {
		accumulate_max(result, st::normalFont->width(Ui::FormatPlayedText(duration, duration)));
		accumulate_max(result, st::normalFont->width(Ui::FormatDurationAndSizeText(duration, document->size)));
	} else if (const auto voice = document->voice()) {
		accumulate_max(result, st::normalFont->width(Ui::FormatPlayedText(duration, duration)));
		accumulate_max(result, st::normalFont->width(Ui::FormatDurationAndSizeText(duration, document->size)));
	} else if (document->isVideoFile()) {
		accumulate_max(result, st::normalFont->width(Ui::FormatDurationAndSizeText(duration, document->size)));
	} else {
		accumulate_max(result, st::normalFont->width(Ui::FormatSizeText(document->size)));
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

std::unique_ptr<Media> CreateAttach(
		not_null<Element*> parent,
		DocumentData *document,
		PhotoData *photo,
		const std::vector<std::unique_ptr<Data::Media>> &collage,
		const QString &webpageUrl) {
	if (!collage.empty()) {
		return std::make_unique<GroupedMedia>(parent, collage);
	} else if (document) {
		if (document->sticker()) {
			return std::make_unique<UnwrappedMedia>(
				parent,
				std::make_unique<Sticker>(parent, document));
		} else if (document->isAnimation() || document->isVideoFile()) {
			return std::make_unique<Gif>(parent, parent->data(), document);
		} else if (document->isWallPaper() || document->isTheme()) {
			return std::make_unique<ThemeDocument>(
				parent,
				document,
				webpageUrl);
		}
		return std::make_unique<Document>(parent, parent->data(), document);
	} else if (photo) {
		return std::make_unique<Photo>(
			parent,
			parent->data(),
			photo);
	}
	return nullptr;
}

int unitedLineHeight() {
	return qMax(st::webPageTitleFont->height, st::webPageDescriptionFont->height);
}

} // namespace HistoryView
