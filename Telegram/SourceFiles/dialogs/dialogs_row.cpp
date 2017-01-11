/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "dialogs/dialogs_row.h"

#include "styles/style_dialogs.h"
#include "ui/effects/ripple_animation.h"
#include "mainwidget.h"

namespace Dialogs {

RippleRow::RippleRow() = default;
RippleRow::~RippleRow() = default;

void RippleRow::addRipple(QPoint origin, QSize size, base::lambda_copy<void()> &&updateCallback) {
	if (!_ripple) {
		auto mask = Ui::RippleAnimation::rectMask(size);
		_ripple = std_::make_unique<Ui::RippleAnimation>(st::dialogsRipple, std_::move(mask), std_::move(updateCallback));
	}
	_ripple->add(origin);
}

void RippleRow::stopLastRipple() {
	if (_ripple) {
		_ripple->lastStop();
	}
}

void RippleRow::paintRipple(Painter &p, int x, int y, int outerWidth, TimeMs ms, const QColor *colorOverride) const {
	if (_ripple) {
		_ripple->paint(p, x, y, outerWidth, ms, colorOverride);
		if (_ripple->empty()) {
			_ripple.reset();
		}
	}
}

FakeRow::FakeRow(HistoryItem *item) : _item(item), _cache(st::dialogsTextWidthMin) {
}

} // namespace Dialogs