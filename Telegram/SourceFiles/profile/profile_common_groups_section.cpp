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
#include "profile/profile_common_groups_section.h"

#include "profile/profile_section_memento.h"
#include "profile/profile_back_button.h"
#include "styles/style_widgets.h"
#include "styles/style_profile.h"
#include "styles/style_window.h"
#include "styles/style_settings.h"
#include "ui/effects/ripple_animation.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "mainwidget.h"
#include "observer_peer.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"

namespace Profile {
namespace CommonGroups {
namespace {

constexpr int kCommonGroupsPerPage = 40;

} // namespace

object_ptr<Window::SectionWidget> SectionMemento::createWidget(QWidget *parent, not_null<Window::Controller*> controller, const QRect &geometry) {
	auto result = object_ptr<Widget>(parent, controller, _user);
	result->setInternalState(geometry, this);
	return std::move(result);
}

FixedBar::FixedBar(QWidget *parent) : TWidget(parent)
, _backButton(this, lang(lng_profile_common_groups_section)) {
	_backButton->moveToLeft(0, 0);
	connect(_backButton, SIGNAL(clicked()), this, SLOT(onBack()));
}

void FixedBar::onBack() {
	App::main()->showBackFromStack();
}

int FixedBar::resizeGetHeight(int newWidth) {
	auto newHeight = 0;

	auto buttonLeft = newWidth;
	_backButton->resizeToWidth(newWidth);
	_backButton->moveToLeft(0, 0);
	newHeight += _backButton->height();

	return newHeight;
}

void FixedBar::setAnimatingMode(bool enabled) {
	if (_animatingMode != enabled) {
		_animatingMode = enabled;
		setCursor(_animatingMode ? style::cur_pointer : style::cur_default);
		if (_animatingMode) {
			setAttribute(Qt::WA_OpaquePaintEvent, false);
			hideChildren();
		} else {
			setAttribute(Qt::WA_OpaquePaintEvent);
			showChildren();
		}
		show();
	}
}

void FixedBar::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		onBack();
	} else {
		TWidget::mousePressEvent(e);
	}
}

InnerWidget::Item::Item(PeerData *peer) : peer(peer) {
}

InnerWidget::Item::~Item() = default;

InnerWidget::InnerWidget(QWidget *parent, not_null<UserData*> user) : TWidget(parent)
, _user(user) {
	setMouseTracking(true);
	setAttribute(Qt::WA_OpaquePaintEvent);
	_rowHeight = st::profileCommonGroupsPadding.top() + st::profileCommonGroupsPhotoSize + st::profileCommonGroupsPadding.bottom();
	_contentTop = st::profileCommonGroupsSkip;
}

void InnerWidget::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;

	checkPreloadMore();
}

void InnerWidget::checkPreloadMore() {
	if (_visibleTop + PreloadHeightsCount * (_visibleBottom - _visibleTop) > height()) {
		preloadMore();
	}
}

void InnerWidget::saveState(not_null<SectionMemento*> memento) {
	if (auto count = _items.size()) {
		QList<not_null<PeerData*>> groups;
		groups.reserve(count);
		for_const (auto item, _items) {
			groups.push_back(item->peer);
		}
		memento->setCommonGroups(groups);
	}
}

void InnerWidget::restoreState(not_null<SectionMemento*> memento) {
	auto list = memento->getCommonGroups();
	_allLoaded = false;
	if (!list.empty()) {
		showInitial(list);
	}
}

void InnerWidget::showInitial(const QList<not_null<PeerData*>> &list) {
	for_const (auto group, list) {
		if (auto item = computeItem(group)) {
			_items.push_back(item);
		}
		_preloadGroupId = group->bareId();
	}
	updateSize();
}

void InnerWidget::preloadMore() {
	if (_preloadRequestId || _allLoaded) {
		return;
	}
	auto request = MTPmessages_GetCommonChats(user()->inputUser, MTP_int(_preloadGroupId), MTP_int(kCommonGroupsPerPage));
	_preloadRequestId = MTP::send(request, ::rpcDone(base::lambda_guarded(this, [this](const MTPmessages_Chats &result) {
		_preloadRequestId = 0;
		_preloadGroupId = 0;
		_allLoaded = true;
		if (auto chats = Api::getChatsFromMessagesChats(result)) {
			auto &list = chats->v;
			if (!list.empty()) {
				_items.reserve(_items.size() + list.size());
				for_const (auto &chatData, list) {
					if (auto chat = App::feedChat(chatData)) {
						auto found = false;
						for_const (auto item, _items) {
							if (item->peer == chat) {
								found = true;
								break;
							}
						}
						if (!found) {
							if (auto item = computeItem(chat)) {
								_items.push_back(item);
							}
						}
						_preloadGroupId = chat->bareId();
						_allLoaded = false;
					}
				}
				updateSize();
			}
		}
	})));
}

void InnerWidget::updateSize() {
	TWidget::resizeToWidth(width());
	checkPreloadMore();
}

int InnerWidget::resizeGetHeight(int newWidth) {
	update();

	auto contentLeftMin = st::profileCommonGroupsLeftMin;
	auto contentLeftMax = st::profileCommonGroupsLeftMax;
	auto widthWithMin = st::windowMinWidth;
	auto widthWithMax = st::profileCommonGroupsWidthMax + 2 * contentLeftMax;
	_contentLeft = anim::interpolate(contentLeftMax, contentLeftMin, qMax(widthWithMax - newWidth, 0) / float64(widthWithMax - widthWithMin));
	_contentWidth = qMin(newWidth - 2 * _contentLeft, st::profileCommonGroupsWidthMax);

	auto newHeight = _contentTop;
	newHeight += _items.size() * _rowHeight;
	newHeight += st::profileCommonGroupsSkip;
	return qMax(newHeight, _minHeight);
}

void InnerWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();
	auto clip = e->rect();
	p.fillRect(clip, st::profileBg);

	auto from = floorclamp(clip.y() - _contentTop, _rowHeight, 0, _items.size());
	auto to = ceilclamp(clip.y() + clip.height() - _contentTop, _rowHeight, 0, _items.size());
	for (auto i = from; i != to; ++i) {
		paintRow(p, i, ms);
	}
}

void InnerWidget::paintRow(Painter &p, int index, TimeMs ms) {
	auto item = _items[index];
	auto selected = (_pressed >= 0) ? (index == _pressed) : (index == _selected);

	auto x = _contentLeft;
	auto y = _contentTop + index * _rowHeight;
	if (selected) {
		p.fillRect(myrtlrect(x, y, _contentWidth, _rowHeight), st::profileCommonGroupsBgOver);
	}
	if (auto &ripple = item->ripple) {
		ripple->paint(p, x, y, width(), ms);
		if (ripple->empty()) {
			ripple.reset();
		}
	}

	x += st::profileCommonGroupsPadding.left();
	y += st::profileCommonGroupsPadding.top();
	item->peer->paintUserpic(p, rtl() ? (width() - x - st::profileCommonGroupsPhotoSize) : x, y, st::profileCommonGroupsPhotoSize);

	p.setPen(st::profileMemberNameFg);
	x += st::profileCommonGroupsPhotoSize + st::profileCommonGroupsNameLeft;
	y += st::profileCommonGroupsNameTop;
	auto nameWidth = _contentWidth - (x - _contentLeft) - st::profileCommonGroupsPadding.right();
	if (item->name.isEmpty()) {
		item->name.setText(st::msgNameStyle, App::peerName(item->peer), _textNameOptions);
	}
	_items[index]->name.drawLeftElided(p, x, y, nameWidth, width());
}

void InnerWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		emit cancelled();
	}
}

void InnerWidget::updateSelected(QPoint localPos) {
	auto selected = -1;
	auto selectedKick = false;

	if (rtl()) localPos.setX(width() - localPos.x());
	if (localPos.x() >= _contentLeft && localPos.x() < _contentLeft + _contentWidth && localPos.y() >= _contentTop) {
		selected = (localPos.y() - _contentTop) / _rowHeight;
		if (selected >= _items.size()) {
			selected = -1;
		}
	}

	if (_selected != selected) {
		updateRow(_selected);
		_selected = selected;
		updateRow(_selected);
		if (_pressed < 0) {
			setCursor((_selected >= 0) ? style::cur_pointer : style::cur_default);
		}
	}
}

void InnerWidget::updateRow(int index) {
	rtlupdate(_contentLeft, _contentTop + index * _rowHeight, _contentWidth, _rowHeight);
}

void InnerWidget::mousePressEvent(QMouseEvent *e) {
	_pressed = _selected;
	if (_pressed >= 0) {
		auto item = _items[_pressed];
		if (!item->ripple) {
			auto mask = Ui::RippleAnimation::rectMask(QSize(_contentWidth, _rowHeight));
			item->ripple = std::make_unique<Ui::RippleAnimation>(st::profileCommonGroupsRipple, std::move(mask), [this, index = _pressed] {
				updateRow(index);
			});
		}
		auto left = _contentLeft;
		auto top = _contentTop + _rowHeight * _pressed;
		item->ripple->add(e->pos() - QPoint(left, top));
	}
}

void InnerWidget::mouseMoveEvent(QMouseEvent *e) {
	updateSelected(e->pos());
}

void InnerWidget::mouseReleaseEvent(QMouseEvent *e) {
	updateRow(_pressed);
	auto pressed = std::exchange(_pressed, -1);
	if (pressed >= 0 && pressed < _items.size()) {
		if (auto &ripple = _items[pressed]->ripple) {
			ripple->lastStop();
		}
		if (pressed == _selected) {
			Ui::showPeerHistory(_items[pressed]->peer, ShowAtUnreadMsgId, Ui::ShowWay::Forward);
		}
	}
	setCursor(_selected ? style::cur_pointer : style::cur_default);
	updateRow(_selected);
}

InnerWidget::Item *InnerWidget::computeItem(PeerData *group) {
	// Skip groups that migrated to supergroups.
	if (group->migrateTo()) {
		return nullptr;
	}

	auto it = _dataMap.constFind(group);
	if (it == _dataMap.cend()) {
		it = _dataMap.insert(group, new Item(group));
	}
	return it.value();
}

InnerWidget::~InnerWidget() {
	for (auto item : base::take(_dataMap)) {
		delete item;
	}
}

Widget::Widget(QWidget *parent, not_null<Window::Controller*> controller, not_null<UserData*> user) : Window::SectionWidget(parent, controller)
, _scroll(this, st::settingsScroll)
, _fixedBar(this)
, _fixedBarShadow(this, st::shadowFg) {
	_fixedBar->move(0, 0);
	_fixedBar->resizeToWidth(width());
	_fixedBar->show();

	_fixedBarShadow->raise();
	updateAdaptiveLayout();
	subscribe(Adaptive::Changed(), [this]() { updateAdaptiveLayout(); });

	_inner = _scroll->setOwnedWidget(object_ptr<InnerWidget>(this, user));
	_scroll->move(0, _fixedBar->height());
	_scroll->show();

	connect(_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));
	connect(_inner, SIGNAL(cancelled()), _fixedBar, SLOT(onBack()));
}

void Widget::updateAdaptiveLayout() {
	_fixedBarShadow->moveToLeft(Adaptive::OneColumn() ? 0 : st::lineWidth, _fixedBar->height());
}

not_null<UserData*> Widget::user() const {
	return _inner->user();
}

QPixmap Widget::grabForShowAnimation(const Window::SectionSlideParams &params) {
	if (params.withTopBarShadow) _fixedBarShadow->hide();
	auto result = myGrab(this);
	if (params.withTopBarShadow) _fixedBarShadow->show();
	return result;
}

void Widget::doSetInnerFocus() {
	_inner->setFocus();
}

bool Widget::showInternal(not_null<Window::SectionMemento*> memento) {
	if (auto profileMemento = dynamic_cast<SectionMemento*>(memento.get())) {
		if (profileMemento->getUser() == user()) {
			restoreState(profileMemento);
			return true;
		}
	}
	return false;
}

void Widget::setInternalState(const QRect &geometry, not_null<SectionMemento*> memento) {
	setGeometry(geometry);
	myEnsureResized(this);
	restoreState(memento);
}

std::unique_ptr<Window::SectionMemento> Widget::createMemento() {
	auto result = std::make_unique<SectionMemento>(user());
	saveState(result.get());
	return std::move(result);
}

void Widget::saveState(not_null<SectionMemento*> memento) {
	memento->setScrollTop(_scroll->scrollTop());
	_inner->saveState(memento);
}

void Widget::restoreState(not_null<SectionMemento*> memento) {
	_inner->restoreState(memento);
	auto scrollTop = memento->getScrollTop();
	_scroll->scrollToY(scrollTop);
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
}

void Widget::resizeEvent(QResizeEvent *e) {
	if (!width() || !height()) {
		return;
	}

	int newScrollTop = _scroll->scrollTop() + topDelta();
	_fixedBar->resizeToWidth(width());
	_fixedBarShadow->resize(width(), st::lineWidth);

	QSize scrollSize(width(), height() - _fixedBar->height());
	if (_scroll->size() != scrollSize) {
		_scroll->resize(scrollSize);
		_inner->resizeToWidth(scrollSize.width(), _scroll->height());
	}

	if (!_scroll->isHidden()) {
		if (topDelta()) {
			_scroll->scrollToY(newScrollTop);
		}
		int scrollTop = _scroll->scrollTop();
		_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	}
}

void Widget::onScroll() {
	int scrollTop = _scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
}

void Widget::showAnimatedHook() {
	_fixedBar->setAnimatingMode(true);
}

void Widget::showFinishedHook() {
	_fixedBar->setAnimatingMode(false);
}

bool Widget::wheelEventFromFloatPlayer(QEvent *e, Window::Column myColumn, Window::Column playerColumn) {
	return _scroll->viewportEvent(e);
}

QRect Widget::rectForFloatPlayer(Window::Column myColumn, Window::Column playerColumn) {
	return mapToGlobal(_scroll->geometry());
}

} // namespace CommonGroups
} // namespace Profile
