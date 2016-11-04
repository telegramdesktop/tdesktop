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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "profile/profile_block_peer_list.h"

#include "styles/style_profile.h"
#include "styles/style_widgets.h"

namespace Profile {

PeerListWidget::PeerListWidget(QWidget *parent, PeerData *peer, const QString &title, const QString &removeText)
: BlockWidget(parent, peer, title)
, _removeText(removeText)
, _removeWidth(st::normalFont->width(_removeText)) {
	setMouseTracking(true);
	subscribe(FileDownload::ImageLoaded(), [this] { update(); });
}

int PeerListWidget::resizeGetHeight(int newWidth) {
	auto newHeight = getListTop();

	newHeight += _items.size() * st::profileMemberHeight;

	return newHeight + st::profileBlockMarginBottom;
}

void PeerListWidget::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;

	if (_preloadMoreCallback) {
		if (_visibleTop + PreloadHeightsCount * (_visibleBottom - _visibleTop) > height()) {
			_preloadMoreCallback();
		}
	}
}

void PeerListWidget::paintContents(Painter &p) {
	int left = getListLeft();
	int top = getListTop();
	int memberRowWidth = width() - left;
	accumulate_min(memberRowWidth, st::profileBlockWideWidthMax);

	int from = floorclamp(_visibleTop - top, st::profileMemberHeight, 0, _items.size());
	int to = ceilclamp(_visibleBottom - top, st::profileMemberHeight, 0, _items.size());
	for (int i = from; i < to; ++i) {
		int y = top + i * st::profileMemberHeight;
		bool selected = (i == _selected);
		bool selectedRemove = selected && _selectedRemove;
		if (_pressed >= 0) {
			if (_pressed != _selected) {
				selected = selectedRemove = false;
			} else if (!_pressedRemove) {
				_selectedRemove = false;
			}
		}
		paintItem(p, left, y, _items[i], selected, selectedRemove);
	}
}

void PeerListWidget::paintItem(Painter &p, int x, int y, Item *item, bool selected, bool selectedKick) {
	if (_updateItemCallback) {
		_updateItemCallback(item);
	}

	int memberRowWidth = width() - x;
	accumulate_min(memberRowWidth, st::profileBlockWideWidthMax);
	if (selected) {
		paintOutlinedRect(p, x, y, memberRowWidth, st::profileMemberHeight);
	}
	int skip = st::profileMemberPhotoPosition.x();

	item->peer->paintUserpicLeft(p, st::profileMemberPhotoSize, x + st::profileMemberPhotoPosition.x(), y + st::profileMemberPhotoPosition.y(), width());

	if (item->name.isEmpty()) {
		item->name.setText(st::semiboldFont, App::peerName(item->peer), _textNameOptions);
	}
	int nameLeft = x + st::profileMemberNamePosition.x();
	int nameTop = y + st::profileMemberNamePosition.y();
	int nameWidth = memberRowWidth - st::profileMemberNamePosition.x() - skip;
	if (item->hasRemoveLink && selected) {
		p.setFont(selectedKick ? st::normalFont->underline() : st::normalFont);
		p.setPen(st::windowActiveTextFg);
		p.drawTextLeft(nameLeft + nameWidth - _removeWidth, nameTop, width(), _removeText, _removeWidth);
		nameWidth -= _removeWidth + skip;
	}
	if (item->hasAdminStar) {
		nameWidth -= st::profileMemberAdminIcon.width();
		int iconLeft = nameLeft + qMin(nameWidth, item->name.maxWidth());
		st::profileMemberAdminIcon.paint(p, QPoint(iconLeft, nameTop), width());
	}
	p.setPen(st::profileMemberNameFg);
	item->name.drawLeftElided(p, nameLeft, nameTop, nameWidth, width());

	if (item->statusHasOnlineColor) {
		p.setPen(st::profileMemberStatusFgActive);
	} else {
		p.setPen(selected ? st::profileMemberStatusFgOver : st::profileMemberStatusFg);
	}
	p.setFont(st::normalFont);
	p.drawTextLeft(x + st::profileMemberStatusPosition.x(), y + st::profileMemberStatusPosition.y(), width(), item->statusText);
}

void PeerListWidget::paintOutlinedRect(Painter &p, int x, int y, int w, int h) const {
	int outlineWidth = st::defaultLeftOutlineButton.outlineWidth;
	p.fillRect(rtlrect(x, y, outlineWidth, h, width()), st::defaultLeftOutlineButton.outlineFgOver);
	p.fillRect(rtlrect(x + outlineWidth, y, w - outlineWidth, h, width()), st::defaultLeftOutlineButton.textBgOver);
}

void PeerListWidget::mouseMoveEvent(QMouseEvent *e) {
	_mousePosition = e->globalPos();
	updateSelection();
}

void PeerListWidget::mousePressEvent(QMouseEvent *e) {
	_mousePosition = e->globalPos();
	updateSelection();

	_pressed = _selected;
	_pressedRemove = _selectedRemove;
}

void PeerListWidget::mouseReleaseEvent(QMouseEvent *e) {
	_mousePosition = e->globalPos();
	updateSelection();

	auto pressed = _pressed;
	auto pressedRemove = _pressedRemove;
	_pressed = -1;
	_pressedRemove = false;
	if (pressed >= 0 && pressed < _items.size() && pressed == _selected && pressedRemove == _selectedRemove) {
		if (auto &callback = (pressedRemove ? _removedCallback : _selectedCallback)) {
			callback(_items[pressed]->peer);
		}
	}
	setCursor(_selectedRemove ? style::cur_pointer : style::cur_default);
	repaintSelectedRow();
}

void PeerListWidget::enterEvent(QEvent *e) {
	_mousePosition = QCursor::pos();
	updateSelection();
}

void PeerListWidget::leaveEvent(QEvent *e) {
	_mousePosition = QPoint(-1, -1);
	updateSelection();
}

void PeerListWidget::updateSelection() {
	int selected = -1;
	bool selectedKick = false;

	auto mouse = mapFromGlobal(_mousePosition);
	if (rtl()) mouse.setX(width() - mouse.x());
	int left = getListLeft();
	int top = getListTop();
	int memberRowWidth = width() - left;
	accumulate_min(memberRowWidth, st::profileBlockWideWidthMax);
	if (mouse.x() >= left && mouse.x() < left + memberRowWidth && mouse.y() >= top) {
		selected = (mouse.y() - top) / st::profileMemberHeight;
		if (selected >= _items.size()) {
			selected = -1;
		} else if (_items[selected]->hasRemoveLink) {
			int skip = st::profileMemberPhotoPosition.x();
			int nameLeft = left + st::profileMemberNamePosition.x();
			int nameTop = top + _selected * st::profileMemberHeight + st::profileMemberNamePosition.y();
			int nameWidth = memberRowWidth - st::profileMemberNamePosition.x() - skip;
			if (mouse.x() >= nameLeft + nameWidth - _removeWidth && mouse.x() < nameLeft + nameWidth) {
				if (mouse.y() >= nameTop && mouse.y() < nameTop + st::normalFont->height) {
					selectedKick = true;
				}
			}
		}
	}

	setSelected(selected, selectedKick);
}

void PeerListWidget::setSelected(int selected, bool selectedRemove) {
	if (_selected == selected && _selectedRemove == selectedRemove) {
		return;
	}

	repaintSelectedRow();
	if (_selectedRemove != selectedRemove) {
		_selectedRemove = selectedRemove;
		if (_pressed < 0) {
			setCursor(_selectedRemove ? style::cur_pointer : style::cur_default);
		}
	}
	if (_selected != selected) {
		_selected = selected;
		repaintSelectedRow();
	}
}

void PeerListWidget::repaintSelectedRow() {
	if (_selected >= 0) {
		int left = getListLeft();
		rtlupdate(left, getListTop() + _selected * st::profileMemberHeight, width() - left, st::profileMemberHeight);
	}
}

int PeerListWidget::getListLeft() const {
	return st::profileBlockTitlePosition.x() - st::profileMemberPaddingLeft;
}

void PeerListWidget::preloadPhotos() {
	int top = getListTop();
	int preloadFor = (_visibleBottom - _visibleTop) * PreloadHeightsCount;
	int from = floorclamp(_visibleTop - top, st::profileMemberHeight, 0, _items.size());
	int to = ceilclamp(_visibleBottom + preloadFor - top, st::profileMemberHeight, 0, _items.size());
	for (int i = from; i < to; ++i) {
		_items[i]->peer->loadUserpic();
	}
}

void PeerListWidget::refreshVisibility() {
	setVisible(!_items.isEmpty());
}

} // namespace Profile
