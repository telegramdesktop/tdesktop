/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/top_background_gradient.h"

#include "apiwrap.h"
#include "api/api_peer_colors.h"
#include "data/data_emoji_statuses.h"
#include "data/data_credits.h"
#include "data/data_peer.h"
#include "data/data_star_gift.h"
#include "data/stickers/data_custom_emoji.h"
#include "main/main_session.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/custom_emoji_helper.h"
#include "styles/style_layers.h"
#include "styles/style_chat_helpers.h"

namespace Ui {
namespace {

void PrepareImage(
		QImage &image,
		not_null<Ui::Text::CustomEmoji*> emoji,
		const PatternPoint &point,
		const QColor &patternColor) {
	if (!image.isNull() || !emoji->ready()) {
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	const auto size = Emoji::GetSizeNormal() / ratio;
	image = QImage(
		2 * QSize(size, size) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);
	auto p = QPainter(&image);
	auto hq = PainterHighQualityEnabler(p);
	p.setOpacity(point.opacity);
	if (point.scale < 1.) {
		p.translate(size, size);
		p.scale(point.scale, point.scale);
		p.translate(-size, -size);
	}
	const auto shift = (2 * size - (Emoji::GetSizeLarge() / ratio)) / 2;
	emoji->paint(p, {
		.textColor = patternColor,
		.position = QPoint(shift, shift),
		.paused = true,
	});
}

} // namespace

QImage CreateTopBgGradient(
		QSize size,
		QColor centerColor,
		QColor edgeColor,
		bool rounded,
		QPoint offset) {
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(size * ratio, QImage::Format_ARGB32_Premultiplied);
	if (!rounded) {
		result.fill(Qt::transparent);
	}
	result.setDevicePixelRatio(ratio);

	auto p = QPainter(&result);
	auto hq = PainterHighQualityEnabler(p);
	auto gradient = QRadialGradient(
		rect::center(QRect(offset, size)),
		size.height() / 2);
	gradient.setStops({
		{ 0., centerColor },
		{ 1., edgeColor },
	});
	p.setBrush(gradient);
	p.setPen(Qt::NoPen);
	p.drawRect(Rect(size));
	p.end();

	if (rounded) {
		const auto mask = Images::CornersMask(st::boxRadius);
		return Images::Round(std::move(result), mask, RectPart::FullTop);
	}
	return result;
}

QImage CreateTopBgGradient(QSize size, const Data::UniqueGift &gift) {
	return CreateTopBgGradient(size, gift.backdrop);
}

QImage CreateTopBgGradient(
		QSize size,
		const Data::UniqueGiftBackdrop &backdrop) {
	return CreateTopBgGradient(
		size,
		backdrop.centerColor,
		backdrop.edgeColor);
}

QImage CreateTopBgGradient(
		QSize size,
		not_null<PeerData*> peer,
		QPoint offset) {
	if (const auto collectible = peer->emojiStatusId().collectible) {
		return CreateTopBgGradient(
			size,
			collectible->centerColor,
			collectible->edgeColor,
			false,
			offset);
	}
	if (const auto color = peer->session().api().peerColors().colorProfileFor(
			peer)) {
		if (color->bg.size() > 1) {
			return CreateTopBgGradient(
				size,
				color->bg[1],
				color->bg[0],
				false,
				offset);
		}
	}
	return QImage();
}

const std::vector<PatternPoint> &PatternBgPoints() {
	static const auto kSmall = 0.7;
	static const auto kFaded = 0.2;
	static const auto kLarge = 0.85;
	static const auto kOpaque = 0.3;
	static const auto result = std::vector<PatternPoint>{
		{ { 0.5, 0.066 }, kSmall, kFaded },

		{ { 0.177, 0.168 }, kSmall, kFaded },
		{ { 0.822, 0.168 }, kSmall, kFaded },

		{ { 0.37, 0.168 }, kLarge, kOpaque },
		{ { 0.63, 0.168 }, kLarge, kOpaque },

		{ { 0.277, 0.308 }, kSmall, kOpaque },
		{ { 0.723, 0.308 }, kSmall, kOpaque },

		{ { 0.13, 0.42 }, kSmall, kFaded },
		{ { 0.87, 0.42 }, kSmall, kFaded },

		{ { 0.27, 0.533 }, kLarge, kOpaque },
		{ { 0.73, 0.533 }, kLarge, kOpaque },

		{ { 0.2, 0.73 }, kSmall, kFaded },
		{ { 0.8, 0.73 }, kSmall, kFaded },

		{ { 0.302, 0.825 }, kLarge, kOpaque },
		{ { 0.698, 0.825 }, kLarge, kOpaque },

		{ { 0.5, 0.876 }, kLarge, kFaded },

		{ { 0.144, 0.936 }, kSmall, kFaded },
		{ { 0.856, 0.936 }, kSmall, kFaded },
	};
	return result;
}

const std::vector<PatternPoint> &PatternBgPointsSmall() {
	static const auto kSmall = 0.45;
	static const auto kFaded = 0.2;
	static const auto kLarge = 0.55;
	static const auto kOpaque = 0.3;
	static const auto result = std::vector<PatternPoint>{
		{ { 0.5, 0.066 }, kSmall, kFaded },

		{ { 0.177, 0.168 }, kSmall, kFaded },
		{ { 0.822, 0.168 }, kSmall, kFaded },

		{ { 0.37, 0.168 }, kLarge, kOpaque },
		{ { 0.63, 0.168 }, kLarge, kOpaque },

		{ { 0.277, 0.308 }, kSmall, kOpaque },
		{ { 0.723, 0.308 }, kSmall, kOpaque },

		{ { 0.13, 0.42 }, kSmall, kFaded },
		{ { 0.87, 0.42 }, kSmall, kFaded },

		{ { 0.27, 0.533 }, kLarge, kOpaque },
		{ { 0.73, 0.533 }, kLarge, kOpaque },

		{ { 0.2, 0.73 }, kSmall, kFaded },
		{ { 0.8, 0.73 }, kSmall, kFaded },

		{ { 0.302, 0.825 }, kLarge, kOpaque },
		{ { 0.698, 0.825 }, kLarge, kOpaque },

		{ { 0.5, 0.876 }, kLarge, kFaded },

		{ { 0.144, 0.936 }, kSmall, kFaded },
		{ { 0.856, 0.936 }, kSmall, kFaded },
	};
	return result;
}

void PaintBgPoints(
		QPainter &p,
		const std::vector<PatternPoint> &points,
		base::flat_map<float64, QImage> &cache,
		not_null<Ui::Text::CustomEmoji*> emoji,
		const Data::UniqueGift &gift,
		const QRect &rect,
		float64 shown) {
	PaintBgPoints(
		p,
		points,
		cache,
		emoji,
		gift.backdrop,
		rect,
		shown);
}

void PaintBgPoints(
		QPainter &p,
		const std::vector<PatternPoint> &points,
		base::flat_map<float64, QImage> &cache,
		not_null<Ui::Text::CustomEmoji*> emoji,
		const Data::UniqueGiftBackdrop &backdrop,
		const QRect &rect,
		float64 shown) {
	PaintBgPoints(
		p,
		points,
		cache,
		emoji,
		backdrop.patternColor,
		rect,
		shown);
}

void PaintBgPoints(
		QPainter &p,
		const std::vector<PatternPoint> &points,
		base::flat_map<float64, QImage> &cache,
		not_null<Ui::Text::CustomEmoji*> emoji,
		const QColor &patternColor,
		const QRect &rect,
		float64 shown) {
	const auto origin = rect.topLeft();
	const auto width = rect.width();
	const auto height = rect.height();
	const auto ratio = style::DevicePixelRatio();
	const auto paintPoint = [&](const PatternPoint &point) {
		const auto key = (1. + point.opacity) * 10. + point.scale;
		auto &image = cache[key];
		PrepareImage(image, emoji, point, patternColor);
		if (!image.isNull()) {
			const auto position = origin + QPoint(
				int(point.position.x() * width),
				int(point.position.y() * height));
			if (shown < 1.) {
				p.save();
				p.translate(position);
				p.scale(shown, shown);
				p.translate(-position);
			}
			const auto size = image.size() / ratio;
			p.drawImage(
				position - QPoint(size.width() / 2, size.height() / 2),
				image);
			if (shown < 1.) {
				p.restore();
			}
		}
	};
	for (const auto &point : points) {
		paintPoint(point);
	}
}

} // namespace Ui
