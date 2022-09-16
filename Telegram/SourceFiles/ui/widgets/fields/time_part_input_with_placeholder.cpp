/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/fields/time_part_input_with_placeholder.h"

#include "lang/lang_numbers_animation.h"

namespace Ui {

void TimePartWithPlaceholder::setPhrase(
		const tr::phrase<lngtag_count> &phrase) {
	_phrase = phrase;
}

void TimePartWithPlaceholder::paintAdditionalPlaceholder(QPainter &p) {
	maybeUpdatePlaceholder();

	p.setClipRect(rect());
	const auto phRect = placeholderRect();

	if (_lastPlaceholder.width < phRect.width()) {
		placeholderAdditionalPrepare(p);
		p.drawText(
			phRect.translated(-_lastPlaceholder.leftOffset, 0),
			_lastPlaceholder.text,
			style::al_left);
	}

}

void TimePartWithPlaceholder::maybeUpdatePlaceholder() {
	const auto displayedText = getDisplayedText();
	if (displayedText == _lastPlaceholder.displayedText) {
		return;
	}
	const auto count = displayedText.toUInt();
	const auto textWithOffset = _phrase(
		tr::now,
		lt_count,
		count,
		Ui::StringWithNumbers::FromString);
	_lastPlaceholder = {
		.width = phFont()->width(textWithOffset.text),
		.text = textWithOffset.text,
		.leftOffset = phFont()->width(
			textWithOffset.text.mid(0, textWithOffset.offset)),
		.displayedText = displayedText,
	};
	if (displayedText.size() > 1 && displayedText.startsWith(_zero)) {
		_lastPlaceholder.text.insert(textWithOffset.offset, _zero);
	}

	const auto leftMargins = (width() - _lastPlaceholder.width) / 2
		+ _lastPlaceholder.leftOffset;
	setTextMargins({ leftMargins, 0, 0, 0 });
}

} // namespace Ui
