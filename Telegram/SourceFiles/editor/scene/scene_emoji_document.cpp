/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene/scene_emoji_document.h"

#include "ui/emoji_config.h"
#include "ui/painter.h"

#include <QTextBlock>
#include <QTextCursor>

namespace Editor {

EmojiDocument::EmojiDocument(QObject *parent)
: QTextDocument(parent) {
}

QVariant EmojiDocument::loadResource(int type, const QUrl &name) {
	if (type != QTextDocument::ImageResource
		|| name.scheme() != u"emoji"_q) {
		return QTextDocument::loadResource(type, name);
	}
	const auto i = _cache.find(name);
	if (i != _cache.end()) {
		return i->second;
	}
	auto result = QVariant();
	if (const auto emoji = Ui::Emoji::FromUrl(name.toDisplayString())) {
		const auto factor = style::DevicePixelRatio();
		const auto logical = QFontMetrics(defaultFont()).height();
		const auto source = Ui::Emoji::GetSizeLarge();
		auto image = QImage(
			QSize(logical, logical) * factor,
			QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(factor);
		image.fill(Qt::transparent);
		{
			auto p = QPainter(&image);
			auto hq = PainterHighQualityEnabler(p);
			const auto sourceLogical = source / float64(factor);
			const auto scale = logical / sourceLogical;
			p.scale(scale, scale);
			Ui::Emoji::Draw(p, emoji, source, 0, 0);
		}
		result = QVariant(QPixmap::fromImage(std::move(image)));
	}
	_cache.emplace(name, result);
	return result;
}

void ReplaceEmoji(QTextDocument *doc) {
	QSignalBlocker blocker(doc);
	const auto fontHeight = QFontMetrics(doc->defaultFont()).height();
	auto cursor = QTextCursor(doc);
	auto block = doc->begin();
	while (block.isValid()) {
		auto text = block.text();
		auto start = text.constData();
		auto end = start + text.size();
		auto ch = start;
		while (ch < end) {
			auto emojiLength = 0;
			const auto emoji = Ui::Emoji::Find(ch, end, &emojiLength);
			if (!emoji || emojiLength <= 0) {
				++ch;
				continue;
			}
			const auto pos = block.position() + int(ch - start);
			cursor.setPosition(pos);
			cursor.setPosition(
				pos + emojiLength,
				QTextCursor::KeepAnchor);

			auto format = QTextImageFormat();
			format.setName(emoji->toUrl());
			format.setWidth(fontHeight);
			format.setHeight(fontHeight);
			format.setVerticalAlignment(
				QTextCharFormat::AlignBaseline);
			cursor.insertImage(format);

			block = doc->findBlock(pos);
			text = block.text();
			start = text.constData();
			end = start + text.size();
			ch = start + (pos - block.position()) + 1;
			continue;
		}
		block = block.next();
	}
}

QString RecoverTextFromDocument(QTextDocument *doc) {
	auto result = QString();
	auto block = doc->begin();
	while (block.isValid()) {
		if (block != doc->begin()) {
			result += '\n';
		}
		auto it = block.begin();
		while (!it.atEnd()) {
			const auto fragment = it.fragment();
			if (!fragment.isValid()) {
				++it;
				continue;
			}
			const auto text = fragment.text();
			const auto format = fragment.charFormat();
			for (const auto &ch : text) {
				if (ch == QChar::ObjectReplacementCharacter) {
					if (format.isImageFormat()) {
						const auto name = format.toImageFormat().name();
						if (const auto emoji = Ui::Emoji::FromUrl(name)) {
							result += emoji->text();
							continue;
						}
					}
				}
				result += ch;
			}
			++it;
		}
		block = block.next();
	}
	return result;
}

} // namespace Editor
