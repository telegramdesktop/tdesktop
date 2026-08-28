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
	ReplaceEmojiInRange(doc, 0, doc->characterCount());
}

void ReplaceEmojiInRange(QTextDocument *doc, int from, int to) {
	constexpr auto kEmojiScanMargin = 32;

	QSignalBlocker blocker(doc);
	const auto fontHeight = QFontMetrics(doc->defaultFont()).height();
	auto cursor = QTextCursor(doc);
	// Merge with the triggering command, so undo skips replacements.
	cursor.joinPreviousEditBlock();
	from = std::max(from - kEmojiScanMargin, 0);
	auto block = doc->findBlock(from);
	while (block.isValid() && (block.position() <= to)) {
		auto text = block.text();
		auto start = text.constData();
		auto end = start + text.size();
		auto ch = start + std::max(from - block.position(), 0);
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
	cursor.endEditBlock();
}

void SanitizeRange(QTextDocument *doc, int from, int to) {
	to = std::min(to, doc->characterCount() - 1);
	if (from >= to) {
		return;
	}
	QSignalBlocker blocker(doc);
	auto cursor = QTextCursor(doc);
	cursor.joinPreviousEditBlock();

	struct Range {
		int from = 0;
		int to = 0;
	};
	auto resets = std::vector<Range>();
	auto removals = std::vector<int>();
	auto block = doc->findBlock(from);
	while (block.isValid() && (block.position() < to)) {
		auto it = block.begin();
		while (!it.atEnd()) {
			const auto fragment = it.fragment();
			++it;
			if (!fragment.isValid()) {
				continue;
			}
			const auto start = fragment.position();
			const auto end = start + fragment.length();
			if ((end <= from) || (start >= to)) {
				continue;
			}
			const auto format = fragment.charFormat();
			if (format.isImageFormat()
				&& Ui::Emoji::FromUrl(format.toImageFormat().name())) {
				continue;
			}
			resets.push_back({
				.from = std::max(start, from),
				.to = std::min(end, to),
			});
			const auto text = fragment.text();
			for (auto i = 0; i != int(text.size()); ++i) {
				if (text[i] == QChar::ObjectReplacementCharacter) {
					removals.push_back(start + i);
				}
			}
		}
		block = block.next();
	}
	for (const auto &range : resets) {
		cursor.setPosition(range.from);
		cursor.setPosition(range.to, QTextCursor::KeepAnchor);
		cursor.setCharFormat(QTextCharFormat());
	}
	if (!resets.empty()) {
		cursor.setPosition(from);
		cursor.setPosition(to, QTextCursor::KeepAnchor);
		cursor.setBlockFormat(QTextBlockFormat());
	}
	for (auto i = removals.rbegin(); i != removals.rend(); ++i) {
		cursor.setPosition(*i);
		cursor.setPosition(*i + 1, QTextCursor::KeepAnchor);
		cursor.removeSelectedText();
	}
	cursor.endEditBlock();
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
						}
					}
					continue;
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
