/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "ui/widgets/fields/time_part_input.h"

#include "lang_auto.h"

namespace Ui {

class TimePartWithPlaceholder final : public TimePart {
public:
	using Ui::TimePart::TimePart;

	void setPhrase(const tr::phrase<lngtag_count> &phrase);

protected:
	void paintAdditionalPlaceholder(QPainter &p) override;

private:
	void maybeUpdatePlaceholder();

	const QChar _zero = QChar('0');
	tr::phrase<lngtag_count> _phrase;

	struct {
		int width = 0;
		QString text;
		int leftOffset = 0;
		QString displayedText;
	} _lastPlaceholder;

};

} // namespace Ui
