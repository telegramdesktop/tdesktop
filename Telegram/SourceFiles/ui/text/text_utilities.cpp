/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/text/text_utilities.h"

#include "base/algorithm.h"

#include <QtCore/QRegularExpression>

namespace Ui {
namespace Text {
namespace {

TextWithEntities WithSingleEntity(
		const QString &text,
		EntityType type,
		const QString &data = QString()) {
	auto result = TextWithEntities{ text };
	result.entities.push_back({ type, 0, text.size(), data });
	return result;
}

} // namespace

TextWithEntities Bold(const QString &text) {
	return WithSingleEntity(text, EntityType::Bold);
}

TextWithEntities Italic(const QString &text) {
	return WithSingleEntity(text, EntityType::Italic);
}

TextWithEntities Link(const QString &text, const QString &url) {
	return WithSingleEntity(text, EntityType::CustomUrl, url);
}

TextWithEntities RichLangValue(const QString &text) {
	static const auto kStart = QRegularExpression("(\\*\\*|__)");

	auto result = TextWithEntities();
	auto offset = 0;
	while (offset < text.size()) {
		const auto m = kStart.match(text, offset);
		if (!m.hasMatch()) {
			result.text.append(text.midRef(offset));
			break;
		}
		const auto position = m.capturedStart();
		const auto from = m.capturedEnd();
		const auto tag = m.capturedRef();
		const auto till = text.indexOf(tag, from + 1);
		if (till <= from) {
			offset = from;
			continue;
		}
		if (position > offset) {
			result.text.append(text.midRef(offset, position - offset));
		}
		const auto type = (tag == qstr("__"))
			? EntityType::Italic
			: EntityType::Bold;
		result.entities.push_back({ type, result.text.size(), till - from });
		result.text.append(text.midRef(from, till - from));
		offset = till + tag.size();
	}
	return result;
}

} // namespace Text
} // namespace Ui
