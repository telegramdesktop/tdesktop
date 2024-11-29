/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/chats_filter_tag.h"

#include "ui/emoji_config.h"
#include "ui/painter.h"
#include "styles/style_dialogs.h"

namespace Ui {

QImage ChatsFilterTag(QString roundedText, QColor color, bool active) {
	const auto &roundedFont = st::dialogRowFilterTagFont;
	const auto additionalWidth = roundedFont->spacew * 3;
	struct EmojiReplacement final {
		QPixmap pixmap;
		int from = -1;
		int length = 0;
		float64 x = -1;
	};
	auto emojiReplacements = std::vector<EmojiReplacement>();
	auto ch = roundedText.constData();
	const auto end = ch + roundedText.size();
	while (ch != end) {
		auto emojiLength = 0;
		if (const auto emoji = Ui::Emoji::Find(ch, end, &emojiLength)) {
			const auto factor = style::DevicePixelRatio();
			emojiReplacements.push_back({
				.pixmap = Ui::Emoji::SinglePixmap(
					emoji,
					st::normalFont->height * factor).scaledToHeight(
						roundedFont->ascent * factor,
						Qt::SmoothTransformation),
				.from = int(ch - roundedText.constData()),
				.length = emojiLength,
			});
			ch += emojiLength;
		} else {
			ch++;
		}
	}
	if (!emojiReplacements.empty()) {
		auto addedChars = 0;
		for (auto &e : emojiReplacements) {
			const auto pixmapWidth = e.pixmap.width()
				/ style::DevicePixelRatio();
			const auto spaces = 1 + pixmapWidth / roundedFont->spacew;
			const auto placeholder = QString(spaces, ' ');
			const auto from = e.from + addedChars;
			e.x = roundedFont->width(roundedText.mid(0, from))
				+ additionalWidth / 2.
				+ (roundedFont->width(placeholder) - pixmapWidth) / 2.;
			roundedText.replace(from, e.length, placeholder);
			addedChars += spaces - e.length;
		}
	}
	const auto roundedWidth = roundedFont->width(roundedText)
		+ additionalWidth;
	const auto rect = QRect(0, 0, roundedWidth, roundedFont->height);
	auto cache = QImage(
		rect.size() * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	cache.setDevicePixelRatio(style::DevicePixelRatio());
	cache.fill(Qt::transparent);
	{
		auto p = QPainter(&cache);
		const auto pen = QPen(active ? st::dialogsBgActive->c : color);
		p.setPen(Qt::NoPen);
		p.setBrush(active
			? st::dialogsTextFgActive->c
			: anim::with_alpha(pen.color(), .15));
		{
			auto hq = PainterHighQualityEnabler(p);
			const auto radius = roundedFont->height / 3.;
			p.drawRoundedRect(rect, radius, radius);
		}
		p.setPen(pen);
		p.setFont(roundedFont);
		p.drawText(rect, roundedText, style::al_center);
		for (const auto &e : emojiReplacements) {
			const auto h = e.pixmap.height() / style::DevicePixelRatio();
			p.drawPixmap(QPointF(e.x, (rect.height() - h) / 2), e.pixmap);
		}
	}
	return cache;
}

} // namespace Ui
