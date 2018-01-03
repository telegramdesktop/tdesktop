/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_row.h"

#include "styles/style_dialogs.h"
#include "ui/effects/ripple_animation.h"
#include "mainwidget.h"

namespace Dialogs {

RippleRow::RippleRow() = default;
RippleRow::~RippleRow() = default;

void RippleRow::addRipple(QPoint origin, QSize size, base::lambda<void()> updateCallback) {
	if (!_ripple) {
		auto mask = Ui::RippleAnimation::rectMask(size);
		_ripple = std::make_unique<Ui::RippleAnimation>(st::dialogsRipple, std::move(mask), std::move(updateCallback));
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

FakeRow::FakeRow(PeerData *searchInPeer, not_null<HistoryItem*> item)
: _searchInPeer(searchInPeer)
, _item(item)
, _cache(st::dialogsTextWidthMin) {
}

} // namespace Dialogs