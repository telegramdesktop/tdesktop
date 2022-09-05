/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_media_common.h"

#include "ui/text/format_values.h"
#include "data/data_document.h"
#include "data/data_wall_paper.h"
#include "data/data_media_types.h"
#include "history/view/history_view_element.h"
#include "history/view/media/history_view_media_grouped.h"
#include "history/view/media/history_view_photo.h"
#include "history/view/media/history_view_gif.h"
#include "history/view/media/history_view_document.h"
#include "history/view/media/history_view_sticker.h"
#include "history/view/media/history_view_theme_document.h"
#include "media/streaming/media_streaming_utility.h"
#include "styles/style_chat.h"

namespace HistoryView {

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
		PhotoData *photo) {
	return CreateAttach(parent, document, photo, {}, {});
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
			const auto skipPremiumEffect = true;
			return std::make_unique<UnwrappedMedia>(
				parent,
				std::make_unique<Sticker>(
					parent,
					document,
					skipPremiumEffect));
		} else if (document->isAnimation() || document->isVideoFile()) {
			return std::make_unique<Gif>(parent, parent->data(), document);
		} else if (document->isWallPaper() || document->isTheme()) {
			return std::make_unique<ThemeDocument>(
				parent,
				document,
				ThemeDocument::ParamsFromUrl(webpageUrl));
		}
		return std::make_unique<Document>(parent, parent->data(), document);
	} else if (photo) {
		return std::make_unique<Photo>(
			parent,
			parent->data(),
			photo);
	} else if (const auto params = ThemeDocument::ParamsFromUrl(webpageUrl)) {
		return std::make_unique<ThemeDocument>(parent, nullptr, params);
	}
	return nullptr;
}

int UnitedLineHeight() {
	return std::max(st::semiboldFont->height, st::normalFont->height);
}

QImage PrepareWithBlurredBackground(
		QSize outer,
		::Media::Streaming::ExpandDecision resize,
		Image *large,
		Image *blurred) {
	const auto ratio = style::DevicePixelRatio();
	if (resize.expanding) {
		return Images::Prepare(large->original(), resize.result * ratio, {
			.outer = outer,
		});
	}
	auto background = QImage(
		outer * ratio,
		QImage::Format_ARGB32_Premultiplied);
	background.setDevicePixelRatio(ratio);
	if (!blurred) {
		background.fill(Qt::black);
		if (!large) {
			return background;
		}
	}
	auto p = QPainter(&background);
	if (blurred) {
		using namespace ::Media::Streaming;
		FillBlurredBackground(p, outer, blurred->original());
	}
	if (large) {
		auto image = large->original().scaled(
			resize.result * ratio,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		image.setDevicePixelRatio(ratio);
		p.drawImage(
			(outer.width() - resize.result.width()) / 2,
			(outer.height() - resize.result.height()) / 2,
			image);
	}
	p.end();
	return background;
}

} // namespace HistoryView
