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
#include "profile/profile_block_peer_list.h"

#include "ui/effects/ripple_animation.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_profile.h"
#include "styles/style_widgets.h"
#include "auth_session.h"

namespace Profile {

PeerListWidget::Item::Item(PeerData *peer) : peer(peer) {
}

PeerListWidget::Item::~Item() = default;

PeerListWidget::PeerListWidget(QWidget *parent, PeerData *peer, const QString &title, const style::ProfilePeerListItem &st, const QString &removeText)
: BlockWidget(parent, peer, title)
, _st(st)
, _removeText(removeText)
, _removeWidth(st::normalFont->width(_removeText)) {
	setMouseTracking(true);
	subscribe(Auth().downloaderTaskFinished(), [this] { update(); });
}

int PeerListWidget::resizeGetHeight(int newWidth) {
	auto newHeight = getListTop();

	newHeight += _items.size() * st::profileMemberHeight;

	return newHeight + _st.bottom;
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
	auto ms = getms();
	auto left = getListLeft();
	auto top = getListTop();
	auto memberRowWidth = rowWidth();

	auto from = floorclamp(_visibleTop - top, st::profileMemberHeight, 0, _items.size());
	auto to = ceilclamp(_visibleBottom - top, st::profileMemberHeight, 0, _items.size());
	for (auto i = from; i < to; ++i) {
		auto y = top + i * st::profileMemberHeight;
		auto selected = (_menuRowIndex >= 0) ? (i == _menuRowIndex) : (_pressed >= 0) ? (i == _pressed) : (i == _selected);
		auto selectedRemove = selected && _selectedRemove;
		if (_pressed >= 0 && !_pressedRemove) {
			selectedRemove = false;
		}
		paintItem(p, left, y, _items[i], selected, selectedRemove, ms);
	}
}

void PeerListWidget::paintItem(Painter &p, int x, int y, Item *item, bool selected, bool selectedKick, TimeMs ms) {
	if (_updateItemCallback) {
		_updateItemCallback(item);
	}

	auto memberRowWidth = rowWidth();
	if (selected) {
		paintOutlinedRect(p, x, y, memberRowWidth, st::profileMemberHeight);
	}
	if (auto &ripple = item->ripple) {
		ripple->paint(p, x + _st.button.outlineWidth, y, width(), ms);
		if (ripple->empty()) {
			ripple.reset();
		}
	}
	int skip = st::profileMemberPhotoPosition.x();

	item->peer->paintUserpicLeft(p, x + st::profileMemberPhotoPosition.x(), y + st::profileMemberPhotoPosition.y(), width(), st::profileMemberPhotoSize);

	if (item->name.isEmpty()) {
		item->name.setText(st::msgNameStyle, App::peerName(item->peer), _textNameOptions);
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
	if (item->adminState != Item::AdminState::None) {
		nameWidth -= st::profileMemberAdminIcon.width();
		auto iconLeft = nameLeft + qMin(nameWidth, item->name.maxWidth());
		auto &icon = (item->adminState == Item::AdminState::Creator)
			? (selected ? st::profileMemberCreatorIconOver : st::profileMemberCreatorIcon)
			: (selected ? st::profileMemberAdminIconOver : st::profileMemberAdminIcon);
		icon.paint(p, QPoint(iconLeft, nameTop), width());
	}
	p.setPen(st::profileMemberNameFg);
	item->name.drawLeftElided(p, nameLeft, nameTop, nameWidth, width());

	if (item->statusHasOnlineColor) {
		p.setPen(_st.statusFgActive);
	} else {
		p.setPen(selected ? _st.statusFgOver : _st.statusFg);
	}
	p.setFont(st::normalFont);
	p.drawTextLeft(x + st::profileMemberStatusPosition.x(), y + st::profileMemberStatusPosition.y(), width(), item->statusText);
}

void PeerListWidget::paintOutlinedRect(Painter &p, int x, int y, int w, int h) const {
	auto outlineWidth = _st.button.outlineWidth;
	if (outlineWidth) {
		p.fillRect(rtlrect(x, y, outlineWidth, h, width()), _st.button.outlineFgOver);
	}
	p.fillRect(rtlrect(x + outlineWidth, y, w - outlineWidth, h, width()), _st.button.textBgOver);
}

void PeerListWidget::mouseMoveEvent(QMouseEvent *e) {
	_mousePosition = e->globalPos();
	updateSelection();
}

void PeerListWidget::mousePressEvent(QMouseEvent *e) {
	_mousePosition = e->globalPos();
	updateSelection();

	_pressButton = e->button();
	_pressed = _selected;
	_pressedRemove = _selectedRemove;
	if (_pressed >= 0 && !_pressedRemove) {
		auto item = _items[_pressed];
		if (!item->ripple) {
			auto memberRowWidth = rowWidth();
			auto mask = Ui::RippleAnimation::rectMask(QSize(memberRowWidth - _st.button.outlineWidth, st::profileMemberHeight));
			item->ripple = std::make_unique<Ui::RippleAnimation>(_st.button.ripple, std::move(mask), [this, index = _pressed] {
				repaintRow(index);
			});
		}
		auto left = getListLeft() + _st.button.outlineWidth;
		auto top = getListTop() + st::profileMemberHeight * _pressed;
		item->ripple->add(e->pos() - QPoint(left, top));
	}
}

void PeerListWidget::mouseReleaseEvent(QMouseEvent *e) {
	mousePressReleased(e->button());
}

void PeerListWidget::mousePressReleased(Qt::MouseButton button) {
	repaintRow(_pressed);
	auto pressed = std::exchange(_pressed, -1);
	auto pressedRemove = base::take(_pressedRemove);
	if (pressed >= 0 && pressed < _items.size()) {
		if (auto &ripple = _items[pressed]->ripple) {
			ripple->lastStop();
		}
		if (pressed == _selected && pressedRemove == _selectedRemove && button == Qt::LeftButton) {
			InvokeQueued(this, [this, pressedRemove, peer = _items[pressed]->peer] {
				if (auto &callback = (pressedRemove ? _removedCallback : _selectedCallback)) {
					callback(peer);
				}
			});
		}
	}
	setCursor(_selectedRemove ? style::cur_pointer : style::cur_default);
	repaintSelectedRow();
}

void PeerListWidget::contextMenuEvent(QContextMenuEvent *e) {
	if (_menu) {
		_menu->deleteLater();
		_menu = nullptr;
	}
	if (_menuRowIndex >= 0) {
		repaintRow(_menuRowIndex);
		_menuRowIndex = -1;
	}

	if (e->reason() == QContextMenuEvent::Mouse) {
		_mousePosition = e->globalPos();
		updateSelection();
	}

	_menuRowIndex = _selected;
	if (_pressButton != Qt::LeftButton) {
		mousePressReleased(_pressButton);
	}

	if (_selected < 0 || _selected >= _items.size()) {
		return;
	}

	_menu = fillPeerMenu(_items[_selected]->peer);
	if (_menu) {
		_menu->setDestroyedCallback(base::lambda_guarded(this, [this, menu = _menu] {
			if (_menu == menu) {
				_menu = nullptr;
			}
			repaintRow(_menuRowIndex);
			_menuRowIndex = -1;
			_mousePosition = QCursor::pos();
			updateSelection();
		}));
		_menu->popup(e->globalPos());
		e->accept();
	}
}

void PeerListWidget::enterEventHook(QEvent *e) {
	_mousePosition = QCursor::pos();
	updateSelection();
}

void PeerListWidget::leaveEventHook(QEvent *e) {
	_mousePosition = QPoint(-1, -1);
	updateSelection();
}

void PeerListWidget::updateSelection() {
	auto selected = -1;
	auto selectedKick = false;

	auto mouse = mapFromGlobal(_mousePosition);
	if (rtl()) mouse.setX(width() - mouse.x());
	auto left = getListLeft();
	auto top = getListTop();
	auto memberRowWidth = rowWidth();
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
	repaintRow(_selected);
}

void PeerListWidget::repaintRow(int index) {
	if (index >= 0) {
		auto left = getListLeft();
		rtlupdate(left, getListTop() + index * st::profileMemberHeight, width() - left, st::profileMemberHeight);
	}
}

int PeerListWidget::getListLeft() const {
	return _st.left;
}

int PeerListWidget::rowWidth() const {
	return qMin(width() - getListLeft(), st::profileBlockWideWidthMax);
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
