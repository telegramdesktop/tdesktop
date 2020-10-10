/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/stickers_emoji_image_loader.h"

#include "styles/style_chat.h"

namespace Stickers {

EmojiImageLoader::EmojiImageLoader(crl::weak_on_queue<EmojiImageLoader> weak)
: _weak(std::move(weak)) {
}

void EmojiImageLoader::init(
		std::shared_ptr<UniversalImages> images,
		bool largeEnabled) {
	Expects(images != nullptr);

	_images = std::move(images);
	if (largeEnabled) {
		_images->ensureLoaded();
	}
}

QImage EmojiImageLoader::prepare(EmojiPtr emoji) const {
	const auto loaded = _images->ensureLoaded();
	const auto factor = cIntRetinaFactor();
	const auto side = st::largeEmojiSize + 2 * st::largeEmojiOutline;
	auto tinted = QImage(
		QSize(st::largeEmojiSize, st::largeEmojiSize) * factor,
		QImage::Format_ARGB32_Premultiplied);
	tinted.fill(Qt::white);
	if (loaded) {
		QPainter p(&tinted);
		p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		_images->draw(
			p,
			emoji,
			st::largeEmojiSize * factor,
			0,
			0);
	}
	auto result = QImage(
		QSize(side, side) * factor,
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	if (loaded) {
		QPainter p(&result);
		const auto delta = st::largeEmojiOutline * factor;
		const auto planar = std::array<QPoint, 4>{ {
			{ 0, -1 },
			{ -1, 0 },
			{ 1, 0 },
			{ 0, 1 },
		} };
		for (const auto &shift : planar) {
			for (auto i = 0; i != delta; ++i) {
				p.drawImage(QPoint(delta, delta) + shift * (i + 1), tinted);
			}
		}
		const auto diagonal = std::array<QPoint, 4>{ {
			{ -1, -1 },
			{ 1, -1 },
			{ -1, 1 },
			{ 1, 1 },
		} };
		const auto corrected = int(std::round(delta / sqrt(2.)));
		for (const auto &shift : diagonal) {
			for (auto i = 0; i != corrected; ++i) {
				p.drawImage(QPoint(delta, delta) + shift * (i + 1), tinted);
			}
		}
		_images->draw(
			p,
			emoji,
			st::largeEmojiSize * factor,
			delta,
			delta);
	}
	return result;
}

void EmojiImageLoader::switchTo(std::shared_ptr<UniversalImages> images) {
	_images = std::move(images);
}

auto EmojiImageLoader::releaseImages() -> std::shared_ptr<UniversalImages> {
	return std::exchange(
		_images,
		std::make_shared<UniversalImages>(_images->id()));
}

} // namespace Stickers
