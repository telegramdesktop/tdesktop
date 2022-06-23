/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/peer_list_dummy.h"

#include "styles/style_widgets.h"

PeerListDummy::PeerListDummy(
	QWidget *parent,
	int count,
	const style::PeerList &st)
: _st(st)
, _count(count) {
	resize(width(), _count * _st.item.height);
}

void PeerListDummy::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	PainterHighQualityEnabler hq(p);

	const auto fill = e->rect();
	const auto bottom = fill.top() + fill.height();
	const auto from = std::clamp(fill.top() / _st.item.height, 0, _count);
	const auto till = std::clamp(
		(bottom + _st.item.height - 1) / _st.item.height,
		0,
		_count);
	p.translate(0, _st.item.height * from);
	p.setPen(Qt::NoPen);
	for (auto i = from; i != till; ++i) {
		p.setBrush(st::windowBgOver);
		p.drawEllipse(
			_st.item.photoPosition.x(),
			_st.item.photoPosition.y(),
			_st.item.photoSize,
			_st.item.photoSize);

		const auto small = int(1.5 * _st.item.photoSize);
		const auto large = 2 * small;
		const auto second = (i % 2) ? large : small;
		const auto height = _st.item.nameStyle.font->height / 2;
		const auto radius = height / 2;
		const auto left = _st.item.namePosition.x();
		const auto top = _st.item.namePosition.y()
			+ (_st.item.nameStyle.font->height - height) / 2;
		const auto skip = _st.item.namePosition.x()
			- _st.item.photoPosition.x()
			- _st.item.photoSize;
		const auto next = left + small + skip;
		p.drawRoundedRect(left, top, small, height, radius, radius);
		p.drawRoundedRect(next, top, second, height, radius, radius);

		p.translate(0, _st.item.height);
	}
}
