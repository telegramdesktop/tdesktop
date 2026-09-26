/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peer_list_section_headers.h"

#include "ui/painter.h"
#include "styles/style_boxes.h"

void PeerListSectionHeaders::rebuild(
		int count,
		int rowHeight,
		Fn<QString(int index)> section) {
	_count = count;
	_rowHeight = rowHeight;
	_total = 0;
	_prefix.resize(count);
	auto previous = QString();
	for (auto i = 0; i != count; ++i) {
		const auto letter = section(i);
		const auto header = !letter.isEmpty()
			&& (i == 0 || letter != previous);
		if (header) {
			++_total;
		}
		_prefix[i] = _total;
		previous = letter;
	}
}

void PeerListSectionHeaders::clear() {
	_prefix.clear();
	_total = 0;
	_count = 0;
}

bool PeerListSectionHeaders::hasHeader(int index) const {
	if (index < 0 || index >= int(_prefix.size())) {
		return false;
	}
	return (index == 0)
		? (_prefix[0] > 0)
		: (_prefix[index] > _prefix[index - 1]);
}

int PeerListSectionHeaders::contentTop(int index) const {
	const auto headers = (index >= 0 && index < int(_prefix.size()))
		? _prefix[index]
		: 0;
	return index * _rowHeight + headers * st::contactsSortHeaderHeight;
}

int PeerListSectionHeaders::fullHeight() const {
	return _count * _rowHeight + _total * st::contactsSortHeaderHeight;
}

int PeerListSectionHeaders::rowFromY(int y) const {
	auto low = 0, high = _count;
	while (low < high) {
		const auto middle = (low + high) / 2;
		if (contentTop(middle) <= y) {
			low = middle + 1;
		} else {
			high = middle;
		}
	}
	const auto index = low - 1;
	if (index < 0 || index >= _count) {
		return -1;
	}
	return (y < contentTop(index) + _rowHeight) ? index : -1;
}

void PeerListSectionHeaders::paint(
		Painter &p,
		int outerWidth,
		const QString &text) const {
	p.setFont(st::contactsSortHeaderFont);
	p.setPen(st::contactsSortHeaderFg);
	p.drawTextLeft(
		st::contactsSortHeaderPosition.x(),
		st::contactsSortHeaderPosition.y(),
		outerWidth,
		text);
}
