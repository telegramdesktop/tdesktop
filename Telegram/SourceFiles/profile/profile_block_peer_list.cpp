/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "profile/profile_block_peer_list.h"

#include "ui/effects/ripple_animation.h"
#include "ui/text/text_options.h"
#include "data/data_peer.h"
#include "data/data_cloud_file.h"
#include "main/main_session.h"
#include "styles/style_profile.h"
#include "styles/style_widgets.h"

namespace Profile {

PeerListWidget::Item::Item(not_null<PeerData*> peer) : peer(peer) {
}

PeerListWidget::Item::~Item() = default;

PeerListWidget::PeerListWidget(
	QWidget *parent,
	PeerData *peer,
	const QString &title,
	const style::PeerListItem &st,
	const QString &removeText)
: BlockWidget(parent, peer, title)
, _st(st)
, _removeText(removeText)
, _removeWidth(st::normalFont->width(_removeText)) {
	setMouseTracking(true);

	peer->session().downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());
}

int PeerListWidget::resizeGetHeight(int newWidth) {
	auto newHeight = getListTop();

	newHeight += _items.size() * _st.height;

	return newHeight + _st.bottom;
}

void PeerListWidget::visibleTopBottomUpdated(int visibleTop, int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;

	if (_preloadMoreCallback) {
		if (_visibleTop + PreloadHeightsCount * (_visibleBottom - _visibleTop) > height()) {
			_preloadMoreCallback();
		}
	}
}

void PeerListWidget::paintContents(Painter &p) {
	auto left = getListLeft();
	auto top = getListTop();
	auto memberRowWidth = rowWidth();

	auto from = floorclamp(_visibleTop - top, _st.height, 0, _items.size());
	auto to = ceilclamp(_visibleBottom - top, _st.height, 0, _items.size());
	for (auto i = from; i < to; ++i) {
		auto y = top + i * _st.height;
		auto selected = (_pressed >= 0) ? (i == _pressed) : (i == _selected);
		auto selectedRemove = selected && _selectedRemove;
		if (_pressed >= 0 && !_pressedRemove) {
			selectedRemove = false;
		}
		paintItem(p, left, y, _items[i], selected, selectedRemove);
	}
}

void PeerListWidget::paintItem(Painter &p, int x, int y, Item *item, bool selected, bool selectedKick) {
	if (_updateItemCallback) {
		_updateItemCallback(item);
	}

	auto memberRowWidth = rowWidth();
	if (selected) {
		paintItemRect(p, x, y, memberRowWidth, _st.height);
	}
	if (auto &ripple = item->ripple) {
		ripple->paint(p, x, y, width());
		if (ripple->empty()) {
			ripple.reset();
		}
	}
	int skip = _st.photoPosition.x();

	item->peer->paintUserpicLeft(p, item->userpic, x + _st.photoPosition.x(), y + _st.photoPosition.y(), width(), _st.photoSize);

	if (item->name.isEmpty()) {
		item->name.setText(
			st::msgNameStyle,
			item->peer->name,
			Ui::NameTextOptions());
	}
	int nameLeft = x + _st.namePosition.x();
	int nameTop = y + _st.namePosition.y();
	int nameWidth = memberRowWidth - _st.namePosition.x() - skip;
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
	p.drawTextLeft(x + _st.statusPosition.x(), y + _st.statusPosition.y(), width(), item->statusText);
}

void PeerListWidget::paintItemRect(Painter &p, int x, int y, int w, int h) const {
	p.fillRect(style::rtlrect(x, y, w, h, width()), _st.button.textBgOver);
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
			auto mask = Ui::RippleAnimation::rectMask(QSize(memberRowWidth, _st.height));
			item->ripple = std::make_unique<Ui::RippleAnimation>(_st.button.ripple, std::move(mask), [this, index = _pressed] {
				repaintRow(index);
			});
		}
		auto left = getListLeft();
		auto top = getListTop() + _st.height * _pressed;
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
		selected = (mouse.y() - top) / _st.height;
		if (selected >= _items.size()) {
			selected = -1;
		} else if (_items[selected]->hasRemoveLink) {
			int skip = _st.photoPosition.x();
			int nameLeft = left + _st.namePosition.x();
			int nameTop = top + _selected * _st.height + _st.namePosition.y();
			int nameWidth = memberRowWidth - _st.namePosition.x() - skip;
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
		rtlupdate(left, getListTop() + index * _st.height, width() - left, _st.height);
	}
}

int PeerListWidget::getListLeft() const {
	return _st.left;
}

int PeerListWidget::rowWidth() const {
	return _st.maximalWidth
		? qMin(width() - getListLeft(), _st.maximalWidth)
		: width() - getListLeft();
}

void PeerListWidget::preloadPhotos() {
	int top = getListTop();
	int preloadFor = (_visibleBottom - _visibleTop) * PreloadHeightsCount;
	int from = floorclamp(_visibleTop - top, _st.height, 0, _items.size());
	int to = ceilclamp(_visibleBottom + preloadFor - top, _st.height, 0, _items.size());
	for (int i = from; i < to; ++i) {
		_items[i]->peer->loadUserpic();
	}
}

void PeerListWidget::refreshVisibility() {
	setVisible(!_items.isEmpty());
}

} // namespace Profile
