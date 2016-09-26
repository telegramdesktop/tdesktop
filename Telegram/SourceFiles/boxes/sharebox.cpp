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
#include "boxes/sharebox.h"

#include "dialogs/dialogs_indexed_list.h"
#include "styles/style_boxes.h"
#include "observer_peer.h"
#include "lang.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "core/qthelp_url.h"
#include "localstorage.h"
#include "boxes/confirmbox.h"
#include "apiwrap.h"
#include "ui/toast/toast.h"

ShareBox::ShareBox(CopyCallback &&copyCallback, SubmitCallback &&submitCallback) : ItemListBox(st::boxScroll)
, _copyCallback(std_::move(copyCallback))
, _submitCallback(std_::move(submitCallback))
, _inner(this)
, _filter(this, st::boxSearchField, lang(lng_participant_filter))
, _filterCancel(this, st::boxSearchCancel)
, _copy(this, lang(lng_share_copy_link), st::defaultBoxButton)
, _share(this, lang(lng_share_confirm), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, _topShadow(this)
, _bottomShadow(this) {
	int topSkip = st::boxTitleHeight + _filter->height();
	int bottomSkip = st::boxButtonPadding.top() + _share->height() + st::boxButtonPadding.bottom();
	init(_inner, bottomSkip, topSkip);

	connect(_inner, SIGNAL(selectedChanged()), this, SLOT(onSelectedChanged()));
	connect(_inner, SIGNAL(mustScrollTo(int,int)), this, SLOT(onMustScrollTo(int,int)));
	connect(_copy, SIGNAL(clicked()), this, SLOT(onCopyLink()));
	connect(_share, SIGNAL(clicked()), this, SLOT(onSubmit()));
	connect(_cancel, SIGNAL(clicked()), this, SLOT(onClose()));
	connect(scrollArea(), SIGNAL(scrolled()), this, SLOT(onScroll()));
	connect(_filter, SIGNAL(changed()), this, SLOT(onFilterUpdate()));
	connect(_filter, SIGNAL(submitted(bool)), _inner, SLOT(onSelectActive()));
	connect(_filterCancel, SIGNAL(clicked()), this, SLOT(onFilterCancel()));
	connect(_inner, SIGNAL(filterCancel()), this, SLOT(onFilterCancel()));
	connect(_inner, SIGNAL(searchByUsername()), this, SLOT(onNeedSearchByUsername()));

	_filterCancel->setAttribute(Qt::WA_OpaquePaintEvent);

	_searchTimer.setSingleShot(true);
	connect(&_searchTimer, SIGNAL(timeout()), this, SLOT(onSearchByUsername()));

	updateButtonsVisibility();

	prepare();
}

bool ShareBox::onSearchByUsername(bool searchCache) {
	auto query = _filter->getLastText().trimmed();
	if (query.isEmpty()) {
		if (_peopleRequest) {
			_peopleRequest = 0;
		}
		return true;
	}
	if (query.size() >= MinUsernameLength) {
		if (searchCache) {
			auto i = _peopleCache.constFind(query);
			if (i != _peopleCache.cend()) {
				_peopleQuery = query;
				_peopleRequest = 0;
				peopleReceived(i.value(), 0);
				return true;
			}
		} else if (_peopleQuery != query) {
			_peopleQuery = query;
			_peopleFull = false;
			_peopleRequest = MTP::send(MTPcontacts_Search(MTP_string(_peopleQuery), MTP_int(SearchPeopleLimit)), rpcDone(&ShareBox::peopleReceived), rpcFail(&ShareBox::peopleFailed));
			_peopleQueries.insert(_peopleRequest, _peopleQuery);
		}
	}
	return false;
}

void ShareBox::onNeedSearchByUsername() {
	if (!onSearchByUsername(true)) {
		_searchTimer.start(AutoSearchTimeout);
	}
}

void ShareBox::peopleReceived(const MTPcontacts_Found &result, mtpRequestId requestId) {
	auto query = _peopleQuery;

	auto i = _peopleQueries.find(requestId);
	if (i != _peopleQueries.cend()) {
		query = i.value();
		_peopleCache[query] = result;
		_peopleQueries.erase(i);
	}

	if (_peopleRequest == requestId) {
		switch (result.type()) {
		case mtpc_contacts_found: {
			auto &found = result.c_contacts_found();
			App::feedUsers(found.vusers);
			App::feedChats(found.vchats);
			_inner->peopleReceived(query, found.vresults.c_vector().v);
		} break;
		}

		_peopleRequest = 0;
		onScroll();
	}
}

bool ShareBox::peopleFailed(const RPCError &error, mtpRequestId requestId) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (_peopleRequest == requestId) {
		_peopleRequest = 0;
		_peopleFull = true;
	}
	return true;
}

void ShareBox::doSetInnerFocus() {
	_filter->setFocus();
}

void ShareBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, lang(lng_share_title));
}

void ShareBox::resizeEvent(QResizeEvent *e) {
	ItemListBox::resizeEvent(e);
	_filter->resize(width(), _filter->height());
	_filter->moveToLeft(0, st::boxTitleHeight);
	_filterCancel->moveToRight(0, st::boxTitleHeight);
	_inner->resizeToWidth(width());
	moveButtons();
	_topShadow->setGeometry(0, st::boxTitleHeight + _filter->height(), width(), st::lineWidth);
	_bottomShadow->setGeometry(0, height() - st::boxButtonPadding.bottom() - _share->height() - st::boxButtonPadding.top() - st::lineWidth, width(), st::lineWidth);
}

void ShareBox::keyPressEvent(QKeyEvent *e) {
	if (_filter->hasFocus()) {
		if (e->key() == Qt::Key_Up) {
			_inner->activateSkipColumn(-1);
		} else if (e->key() == Qt::Key_Down) {
			_inner->activateSkipColumn(1);
		} else if (e->key() == Qt::Key_PageUp) {
			_inner->activateSkipPage(scrollArea()->height(), -1);
		} else if (e->key() == Qt::Key_PageDown) {
			_inner->activateSkipPage(scrollArea()->height(), 1);
		} else {
			ItemListBox::keyPressEvent(e);
		}
	} else {
		ItemListBox::keyPressEvent(e);
	}
}

void ShareBox::moveButtons() {
	_copy->moveToRight(st::boxButtonPadding.right(), _share->y());
	_share->moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _share->height());
	_cancel->moveToRight(st::boxButtonPadding.right() + _share->width() + st::boxButtonPadding.left(), _share->y());
}

void ShareBox::updateButtonsVisibility() {
	auto hasSelected = _inner->hasSelected();
	_copy->setVisible(!hasSelected);
	_share->setVisible(hasSelected);
	_cancel->setVisible(hasSelected);
}

void ShareBox::onFilterCancel() {
	_filter->setText(QString());
}

void ShareBox::onFilterUpdate() {
	_filterCancel->setVisible(!_filter->getLastText().isEmpty());
	_inner->updateFilter(_filter->getLastText());
}

void ShareBox::onSubmit() {
	if (_submitCallback) {
		_submitCallback(_inner->selected());
	}
}

void ShareBox::onCopyLink() {
	if (_copyCallback) {
		_copyCallback();
	}
}

void ShareBox::onSelectedChanged() {
	updateButtonsVisibility();
	moveButtons();
	update();
}

void ShareBox::onMustScrollTo(int top, int bottom) {
	auto scrollTop = scrollArea()->scrollTop(), scrollBottom = scrollTop + scrollArea()->height();
	auto from = scrollTop, to = scrollTop;
	if (scrollTop > top) {
		to = top;
	} else if (scrollBottom < bottom) {
		to = bottom - (scrollBottom - scrollTop);
	}
	if (from != to) {
		_scrollAnimation.start([this]() {
			scrollArea()->scrollToY(_scrollAnimation.current(scrollArea()->scrollTop()));
		}, from, to, st::shareScrollDuration, anim::sineInOut);
	}
}

void ShareBox::onScroll() {
	auto scroll = scrollArea();
	auto scrollTop = scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + scroll->height());
}

namespace internal {

ShareInner::ShareInner(QWidget *parent) : ScrolledWidget(parent)
, _chatsIndexed(std_::make_unique<Dialogs::IndexedList>(Dialogs::SortMode::Add)) {
	_rowsTop = st::shareRowsTop;
	_rowHeight = st::shareRowHeight;
	setAttribute(Qt::WA_OpaquePaintEvent);

	auto dialogs = App::main()->dialogsList();
	for_const (auto row, dialogs->all()) {
		auto history = row->history();
		if (history->peer->canWrite()) {
			_chatsIndexed->addToEnd(history);
		}
	}

	_filter = qsl("a");
	updateFilter();

	prepareWideCheckIcons();

	using UpdateFlag = Notify::PeerUpdate::Flag;
	auto observeEvents = UpdateFlag::NameChanged | UpdateFlag::PhotoChanged;

	Notify::registerPeerObserver(observeEvents, this, [this](const Notify::PeerUpdate &update) {
		notifyPeerUpdated(update);
	});

	subscribe(FileDownload::ImageLoaded(), [this] { update(); });
}

void ShareInner::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	loadProfilePhotos(visibleTop);
}

void ShareInner::activateSkipRow(int direction) {
	activateSkipColumn(direction * _columnCount);
}

int ShareInner::displayedChatsCount() const {
	return _filter.isEmpty() ? _chatsIndexed->size() : (_filtered.size() + d_byUsernameFiltered.size());
}

void ShareInner::activateSkipColumn(int direction) {
	if (_active < 0) {
		if (direction > 0) {
			setActive(0);
		}
		return;
	}
	auto count = displayedChatsCount();
	auto active = _active + direction;
	if (active < 0) {
		active = (_active > 0) ? 0 : -1;
	}
	if (active >= count) {
		active = count - 1;
	}
	setActive(active);
}

void ShareInner::activateSkipPage(int pageHeight, int direction) {
	activateSkipRow(direction * (pageHeight / _rowHeight));
}

void ShareInner::notifyPeerUpdated(const Notify::PeerUpdate &update) {
	if (update.flags & Notify::PeerUpdate::Flag::NameChanged) {
		_chatsIndexed->peerNameChanged(update.peer, update.oldNames, update.oldNameFirstChars);
	}

	updateChat(update.peer);
}

void ShareInner::updateChat(PeerData *peer) {
	auto i = _dataMap.find(peer);
	if (i != _dataMap.cend()) {
		updateChatName(i.value(), peer);
		repaintChat(peer);
	}
}

void ShareInner::updateChatName(Chat *chat, PeerData *peer) {
	chat->name.setText(st::shareNameFont, peer->name, _textNameOptions);
}

void ShareInner::repaintChatAtIndex(int index) {
	if (index < 0) return;

	auto row = index / _columnCount;
	auto column = index % _columnCount;
	update(rtlrect(_rowsLeft + qFloor(column * _rowWidthReal), row * _rowHeight, _rowWidth, _rowHeight, width()));
}

ShareInner::Chat *ShareInner::getChatAtIndex(int index) {
	if (index < 0) return nullptr;
	auto row = ([this, index]() -> Dialogs::Row* {
		if (_filter.isEmpty()) return _chatsIndexed->rowAtY(index, 1);
		return (index < _filtered.size()) ? _filtered[index] : nullptr;
	})();
	if (row) {
		return static_cast<Chat*>(row->attached);
	}

	if (!_filter.isEmpty()) {
		index -= _filtered.size();
		if (index >= 0 && index < d_byUsernameFiltered.size()) {
			return d_byUsernameFiltered[index];
		}
	}
	return nullptr;
}

void ShareInner::repaintChat(PeerData *peer) {
	repaintChatAtIndex(chatIndex(peer));
}

int ShareInner::chatIndex(PeerData *peer) const {
	int index = 0;
	if (_filter.isEmpty()) {
		for_const (auto row, _chatsIndexed->all()) {
			if (row->history()->peer == peer) {
				return index;
			}
			++index;
		}
	} else {
		for_const (auto row, _filtered) {
			if (row->history()->peer == peer) {
				return index;
			}
			++index;
		}
		for_const (auto row, d_byUsernameFiltered) {
			if (row->peer == peer) {
				return index;
			}
			++index;
		}
	}
	return -1;
}

void ShareInner::loadProfilePhotos(int yFrom) {
	if (yFrom < 0) {
		yFrom = 0;
	}
	if (auto part = (yFrom % _rowHeight)) {
		yFrom -= part;
	}
	int yTo = yFrom + (parentWidget() ? parentWidget()->height() : App::wnd()->height()) * 5 * _columnCount;
	if (!yTo) {
		return;
	}
	yFrom *= _columnCount;
	yTo *= _columnCount;

	MTP::clearLoaderPriorities();
	if (_filter.isEmpty()) {
		if (!_chatsIndexed->isEmpty()) {
			auto i = _chatsIndexed->cfind(yFrom, _rowHeight);
			for (auto end = _chatsIndexed->cend(); i != end; ++i) {
				if (((*i)->pos() * _rowHeight) >= yTo) {
					break;
				}
				(*i)->history()->peer->loadUserpic();
			}
		}
	} else if (!_filtered.isEmpty()) {
		int from = yFrom / _rowHeight;
		if (from < 0) from = 0;
		if (from < _filtered.size()) {
			int to = (yTo / _rowHeight) + 1;
			if (to > _filtered.size()) to = _filtered.size();

			for (; from < to; ++from) {
				_filtered[from]->history()->peer->loadUserpic();
			}
		}
	}
}

ShareInner::Chat *ShareInner::getChat(Dialogs::Row *row) {
	auto data = static_cast<Chat*>(row->attached);
	if (!data) {
		auto peer = row->history()->peer;
		auto i = _dataMap.constFind(peer);
		if (i == _dataMap.cend()) {
			_dataMap.insert(peer, data = new Chat(peer));
			updateChatName(data, peer);
		} else {
			data = i.value();
		}
		row->attached = data;
	}
	return data;
}

void ShareInner::setActive(int active) {
	if (active != _active) {
		auto changeNameFg = [this](int index, style::color from, style::color to) {
			if (auto chat = getChatAtIndex(index)) {
				chat->nameFg.start([this, chat] {
					repaintChat(chat->peer);
				}, from->c, to->c, st::shareActivateDuration);
			}
		};
		changeNameFg(_active, st::shareNameActiveFg, st::shareNameFg);
		_active = active;
		changeNameFg(_active, st::shareNameFg, st::shareNameActiveFg);
	}
	auto y = (_active < _columnCount) ? 0 : (_rowsTop + ((_active / _columnCount) * _rowHeight));
	emit mustScrollTo(y, y + _rowHeight);
}

void ShareInner::paintChat(Painter &p, Chat *chat, int index) {
	auto x = _rowsLeft + qFloor((index % _columnCount) * _rowWidthReal);
	auto y = _rowsTop + (index / _columnCount) * _rowHeight;

	auto selectionLevel = chat->selection.current(chat->selected ? 1. : 0.);

	auto w = width();
	auto photoLeft = (_rowWidth - (st::sharePhotoRadius * 2)) / 2;
	auto photoTop = st::sharePhotoTop;
	if (chat->selection.animating()) {
		p.setRenderHint(QPainter::SmoothPixmapTransform, true);
		auto userpicRadius = qRound(WideCacheScale * (st::sharePhotoRadius + (st::sharePhotoSmallRadius - st::sharePhotoRadius) * selectionLevel));
		auto userpicShift = WideCacheScale * st::sharePhotoRadius - userpicRadius;
		auto userpicLeft = x + photoLeft - (WideCacheScale - 1) * st::sharePhotoRadius + userpicShift;
		auto userpicTop = y + photoTop - (WideCacheScale - 1) * st::sharePhotoRadius + userpicShift;
		auto to = QRect(userpicLeft, userpicTop, userpicRadius * 2, userpicRadius * 2);
		auto from = QRect(QPoint(0, 0), chat->wideUserpicCache.size());
		p.drawPixmapLeft(to, w, chat->wideUserpicCache, from);
		p.setRenderHint(QPainter::SmoothPixmapTransform, false);
	} else {
		if (!chat->wideUserpicCache.isNull()) {
			chat->wideUserpicCache = QPixmap();
		}
		auto userpicRadius = chat->selected ? st::sharePhotoSmallRadius : st::sharePhotoRadius;
		auto userpicShift = st::sharePhotoRadius - userpicRadius;
		auto userpicLeft = x + photoLeft + userpicShift;
		auto userpicTop = y + photoTop + userpicShift;
		chat->peer->paintUserpicLeft(p, userpicRadius * 2, userpicLeft, userpicTop, w);
	}

	if (selectionLevel > 0) {
		p.setRenderHint(QPainter::HighQualityAntialiasing, true);
		p.setOpacity(snap(selectionLevel, 0., 1.));
		p.setBrush(Qt::NoBrush);
		QPen pen = st::shareSelectFg;
		pen.setWidth(st::shareSelectWidth);
		p.setPen(pen);
		p.drawEllipse(myrtlrect(x + photoLeft, y + photoTop, st::sharePhotoRadius * 2, st::sharePhotoRadius * 2));
		p.setOpacity(1.);
		p.setRenderHint(QPainter::HighQualityAntialiasing, false);
	}

	removeFadeOutedIcons(chat);
	p.setRenderHint(QPainter::SmoothPixmapTransform, true);
	for (auto &icon : chat->icons) {
		auto fadeIn = icon.fadeIn.current(1.);
		auto fadeOut = icon.fadeOut.current(1.);
		auto iconRadius = qRound(WideCacheScale * (st::shareCheckSmallRadius + fadeOut * (st::shareCheckRadius - st::shareCheckSmallRadius)));
		auto iconShift = WideCacheScale * st::shareCheckRadius - iconRadius;
		auto iconLeft = x + photoLeft + 2 * st::sharePhotoRadius + st::shareSelectWidth - 2 * st::shareCheckRadius - (WideCacheScale - 1) * st::shareCheckRadius + iconShift;
		auto iconTop = y + photoTop + 2 * st::sharePhotoRadius + st::shareSelectWidth - 2 * st::shareCheckRadius - (WideCacheScale - 1) * st::shareCheckRadius + iconShift;
		auto to = QRect(iconLeft, iconTop, iconRadius * 2, iconRadius * 2);
		auto from = QRect(QPoint(0, 0), _wideCheckIconCache.size());
		auto opacity = fadeIn * fadeOut;
		p.setOpacity(opacity);
		if (fadeOut < 1.) {
			p.drawPixmapLeft(to, w, icon.wideCheckCache, from);
		} else {
			auto divider = qRound((WideCacheScale - 2) * st::shareCheckRadius + fadeIn * 3 * st::shareCheckRadius);
			p.drawPixmapLeft(QRect(iconLeft, iconTop, divider, iconRadius * 2), w, _wideCheckIconCache, QRect(0, 0, divider * cIntRetinaFactor(), _wideCheckIconCache.height()));
			p.drawPixmapLeft(QRect(iconLeft + divider, iconTop, iconRadius * 2 - divider, iconRadius * 2), w, _wideCheckCache, QRect(divider * cIntRetinaFactor(), 0, _wideCheckCache.width() - divider * cIntRetinaFactor(), _wideCheckCache.height()));
		}
	}
	p.setRenderHint(QPainter::SmoothPixmapTransform, false);
	p.setOpacity(1.);

	if (chat->nameFg.animating()) {
		p.setPen(chat->nameFg.current());
	} else {
		p.setPen((index == _active) ? st::shareNameActiveFg : st::shareNameFg);
	}

	auto nameWidth = (_rowWidth - st::shareColumnSkip);
	auto nameLeft = st::shareColumnSkip / 2;
	auto nameTop = photoTop + st::sharePhotoRadius * 2 + st::shareNameTop;
	chat->name.drawLeftElided(p, x + nameLeft, y + nameTop, nameWidth, w, 2, style::al_top, 0, -1, 0, true);
}

ShareInner::Chat::Chat(PeerData *peer) : peer(peer), name(st::sharePhotoRadius * 2) {
}

void ShareInner::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto r = e->rect();
	p.setClipRect(r);
	p.fillRect(r, st::white);
	auto yFrom = r.y(), yTo = r.y() + r.height();
	auto rowFrom = yFrom / _rowHeight;
	auto rowTo = (yTo + _rowHeight - 1) / _rowHeight;
	auto indexFrom = rowFrom * _columnCount;
	auto indexTo = rowTo * _columnCount;
	if (_filter.isEmpty()) {
		if (!_chatsIndexed->isEmpty()) {
			auto i = _chatsIndexed->cfind(indexFrom, 1);
			for (auto end = _chatsIndexed->cend(); i != end; ++i) {
				if (indexFrom >= indexTo) {
					break;
				}
				paintChat(p, getChat(*i), indexFrom);
				++indexFrom;
			}
		} else {
			// empty
			p.setFont(st::noContactsFont);
			p.setPen(st::noContactsColor);
		}
	} else {
		if (_filtered.isEmpty() && _byUsernameFiltered.isEmpty()) {
			// empty
			p.setFont(st::noContactsFont);
			p.setPen(st::noContactsColor);
		} else {
			auto filteredSize = _filtered.size();
			if (filteredSize) {
				if (indexFrom < 0) indexFrom = 0;
				while (indexFrom < indexTo) {
					if (indexFrom >= _filtered.size()) {
						break;
					}
					paintChat(p, getChat(_filtered[indexFrom]), indexFrom);
					++indexFrom;
				}
				indexFrom -= filteredSize;
				indexTo -= filteredSize;
			}
			if (!_byUsernameFiltered.isEmpty()) {
				if (indexFrom < 0) indexFrom = 0;
				while (indexFrom < indexTo) {
					if (indexFrom >= d_byUsernameFiltered.size()) {
						break;
					}
					paintChat(p, d_byUsernameFiltered[indexFrom], filteredSize + indexFrom);
					++indexFrom;
				}
			}
		}
	}
}

void ShareInner::enterEvent(QEvent *e) {
	setMouseTracking(true);
}

void ShareInner::leaveEvent(QEvent *e) {
	setMouseTracking(false);
}

void ShareInner::mouseMoveEvent(QMouseEvent *e) {
	updateUpon(e->pos());
	setCursor((_upon >= 0) ? style::cur_pointer : style::cur_default);
}

void ShareInner::updateUpon(const QPoint &pos) {
	auto x = pos.x(), y = pos.y();
	auto row = (y - _rowsTop) / _rowHeight;
	auto column = qFloor((x - _rowsLeft) / _rowWidthReal);
	auto left = _rowsLeft + qFloor(column * _rowWidthReal) + st::shareColumnSkip / 2;
	auto top = _rowsTop + row * _rowHeight + st::sharePhotoTop;
	auto xupon = (x >= left) && (x < left + (_rowWidth - st::shareColumnSkip));
	auto yupon = (y >= top) && (y < top + st::sharePhotoRadius * 2 + st::shareNameTop + st::shareNameFont->height * 2);
	auto upon = (xupon && yupon) ? (row * _columnCount + column) : -1;
	if (upon >= displayedChatsCount()) {
		upon = -1;
	}
	_upon = upon;
}

void ShareInner::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		updateUpon(e->pos());
		changeCheckState(getChatAtIndex(_upon));
	}
}

void ShareInner::onSelectActive() {
	changeCheckState(getChatAtIndex(_active > 0 ? _active : 0));
}

void ShareInner::resizeEvent(QResizeEvent *e) {
	_columnSkip = (width() - _columnCount * st::sharePhotoRadius * 2) / float64(_columnCount + 1);
	_rowWidthReal = st::sharePhotoRadius * 2 + _columnSkip;
	_rowsLeft = qFloor(_columnSkip / 2);
	_rowWidth = qFloor(_rowWidthReal);
	update();
}

struct AnimBumpy {
	AnimBumpy(float64 bump) : bump(bump)
		, dt0(bump - sqrt(bump * (bump - 1.)))
		, k(1 / (2 * dt0 - 1)) {
	}
	float64 bump;
	float64 dt0;
	float64 k;
};

float64 anim_bumpy(const float64 &delta, const float64 &dt) {
	static AnimBumpy data = { 1.25 };
	return delta * (data.bump - data.k * (dt - data.dt0) * (dt - data.dt0));
}

void ShareInner::changeCheckState(Chat *chat) {
	if (!chat) return;

	if (!_filter.isEmpty()) {
		auto row = _chatsIndexed->getRow(chat->peer->id);
		if (!row) {
			row = _chatsIndexed->addToEnd(App::history(chat->peer)).value(0);
		}
		chat = getChat(row);
		if (!chat->selected) {
			_chatsIndexed->moveToTop(chat->peer);
		}
		emit filterCancel();
	}

	chat->selected = !chat->selected;
	if (chat->selected) {
		_selected.insert(chat->peer);
		chat->icons.push_back(Chat::Icon());
		chat->icons.back().fadeIn.start([this, chat] {
			repaintChat(chat->peer);
		}, 0, 1, st::shareSelectDuration);
	} else {
		_selected.remove(chat->peer);
		prepareWideCheckIconCache(&chat->icons.back());
		chat->icons.back().fadeOut.start([this, chat] {
			repaintChat(chat->peer);
			removeFadeOutedIcons(chat); // this call can destroy current lambda
		}, 1, 0, st::shareSelectDuration);
	}
	prepareWideUserpicCache(chat);
	chat->selection.start([this, chat] {
		repaintChat(chat->peer);
	}, chat->selected ? 0 : 1, chat->selected ? 1 : 0, st::shareSelectDuration, anim_bumpy);
	if (chat->selected) {
		setActive(chatIndex(chat->peer));
	}
	emit selectedChanged();
}

void ShareInner::removeFadeOutedIcons(Chat *chat) {
	while (!chat->icons.empty() && !chat->icons.front().fadeIn.animating() && !chat->icons.front().fadeOut.animating()) {
		if (chat->icons.size() > 1 || !chat->selected) {
			chat->icons.erase(chat->icons.begin());
		} else {
			break;
		}
	}
}

void ShareInner::prepareWideUserpicCache(Chat *chat) {
	if (chat->wideUserpicCache.isNull()) {
		auto size = st::sharePhotoRadius * 2;
		auto wideSize = size * WideCacheScale;
		QImage cache(wideSize * cIntRetinaFactor(), wideSize * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
		cache.setDevicePixelRatio(cRetinaFactor());
		{
			Painter p(&cache);
			p.setCompositionMode(QPainter::CompositionMode_Source);
			p.fillRect(0, 0, wideSize, wideSize, Qt::transparent);
			p.setCompositionMode(QPainter::CompositionMode_SourceOver);
			chat->peer->paintUserpic(p, size, (wideSize - size) / 2, (wideSize - size) / 2);
		}
		chat->wideUserpicCache = App::pixmapFromImageInPlace(std_::move(cache));
		chat->wideUserpicCache.setDevicePixelRatio(cRetinaFactor());
	}
}

void ShareInner::prepareWideCheckIconCache(Chat::Icon *icon) {
	QImage wideCache(_wideCheckCache.width(), _wideCheckCache.height(), QImage::Format_ARGB32_Premultiplied);
	wideCache.setDevicePixelRatio(cRetinaFactor());
	{
		Painter p(&wideCache);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		auto iconRadius = WideCacheScale * st::shareCheckRadius;
		auto divider = qRound((WideCacheScale - 2) * st::shareCheckRadius + icon->fadeIn.current(1.) * 3 * st::shareCheckRadius);
		p.drawPixmapLeft(QRect(0, 0, divider, iconRadius * 2), width(), _wideCheckIconCache, QRect(0, 0, divider * cIntRetinaFactor(), _wideCheckIconCache.height()));
		p.drawPixmapLeft(QRect(divider, 0, iconRadius * 2 - divider, iconRadius * 2), width(), _wideCheckCache, QRect(divider * cIntRetinaFactor(), 0, _wideCheckCache.width() - divider * cIntRetinaFactor(), _wideCheckCache.height()));
	}
	icon->wideCheckCache = App::pixmapFromImageInPlace(std_::move(wideCache));
	icon->wideCheckCache.setDevicePixelRatio(cRetinaFactor());
}

void ShareInner::prepareWideCheckIcons() {
	auto size = st::shareCheckRadius * 2;
	auto wideSize = size * WideCacheScale;
	QImage cache(wideSize * cIntRetinaFactor(), wideSize * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	cache.setDevicePixelRatio(cRetinaFactor());
	{
		Painter p(&cache);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.fillRect(0, 0, wideSize, wideSize, Qt::transparent);
		p.setCompositionMode(QPainter::CompositionMode_SourceOver);
		p.setRenderHint(QPainter::HighQualityAntialiasing, true);
		auto pen = st::shareCheckBorder->p;
		pen.setWidth(st::shareSelectWidth);
		p.setPen(pen);
		p.setBrush(st::shareCheckBg);
		auto ellipse = QRect((wideSize - size) / 2, (wideSize - size) / 2, size, size);
		p.drawEllipse(ellipse);
	}
	QImage cacheIcon = cache;
	{
		Painter p(&cacheIcon);
		auto ellipse = QRect((wideSize - size) / 2, (wideSize - size) / 2, size, size);
		st::shareCheckIcon.paint(p, ellipse.topLeft(), wideSize);
	}
	_wideCheckCache = App::pixmapFromImageInPlace(std_::move(cache));
	_wideCheckCache.setDevicePixelRatio(cRetinaFactor());
	_wideCheckIconCache = App::pixmapFromImageInPlace(std_::move(cacheIcon));
	_wideCheckIconCache.setDevicePixelRatio(cRetinaFactor());
}

bool ShareInner::hasSelected() const {
	return _selected.size();
}

void ShareInner::updateFilter(QString filter) {
	_lastQuery = filter.toLower().trimmed();
	filter = textSearchKey(filter);

	QStringList f;
	if (!filter.isEmpty()) {
		QStringList filterList = filter.split(cWordSplit(), QString::SkipEmptyParts);
		int l = filterList.size();

		f.reserve(l);
		for (int i = 0; i < l; ++i) {
			QString filterName = filterList[i].trimmed();
			if (filterName.isEmpty()) continue;
			f.push_back(filterName);
		}
		filter = f.join(' ');
	}
	if (_filter != filter) {
		_filter = filter;

		_byUsernameFiltered.clear();
		for (int i = 0, l = d_byUsernameFiltered.size(); i < l; ++i) {
			delete d_byUsernameFiltered[i];
		}
		d_byUsernameFiltered.clear();

		if (_filter.isEmpty()) {
			refresh();
		} else {
			QStringList::const_iterator fb = f.cbegin(), fe = f.cend(), fi;

			_filtered.clear();
			if (!f.isEmpty()) {
				const Dialogs::List *toFilter = nullptr;
				if (!_chatsIndexed->isEmpty()) {
					for (fi = fb; fi != fe; ++fi) {
						auto found = _chatsIndexed->filtered(fi->at(0));
						if (found->isEmpty()) {
							toFilter = nullptr;
							break;
						}
						if (!toFilter || toFilter->size() > found->size()) {
							toFilter = found;
						}
					}
				}
				if (toFilter) {
					_filtered.reserve(toFilter->size());
					for_const (auto row, *toFilter) {
						auto &names = row->history()->peer->names;
						PeerData::Names::const_iterator nb = names.cbegin(), ne = names.cend(), ni;
						for (fi = fb; fi != fe; ++fi) {
							auto filterName = *fi;
							for (ni = nb; ni != ne; ++ni) {
								if (ni->startsWith(*fi)) {
									break;
								}
							}
							if (ni == ne) {
								break;
							}
						}
						if (fi == fe) {
							_filtered.push_back(row);
						}
					}
				}
			}
			refresh();

			_searching = true;
			emit searchByUsername();
		}
		setActive(-1);
		update();
		loadProfilePhotos(0);
	}
}

void ShareInner::peopleReceived(const QString &query, const QVector<MTPPeer> &people) {
	_lastQuery = query.toLower().trimmed();
	if (_lastQuery.at(0) == '@') _lastQuery = _lastQuery.mid(1);
	int32 already = _byUsernameFiltered.size();
	_byUsernameFiltered.reserve(already + people.size());
	d_byUsernameFiltered.reserve(already + people.size());
	for_const (auto &mtpPeer, people) {
		auto peerId = peerFromMTP(mtpPeer);
		int j = 0;
		for (; j < already; ++j) {
			if (_byUsernameFiltered[j]->id == peerId) break;
		}
		if (j == already) {
			auto *peer = App::peer(peerId);
			if (!peer || !peer->canWrite()) continue;

			auto chat = new Chat(peer);
			updateChatName(chat, peer);
			if (auto row = _chatsIndexed->getRow(peer->id)) {
				continue;
			}

			_byUsernameFiltered.push_back(peer);
			d_byUsernameFiltered.push_back(chat);
		}
	}
	_searching = false;
	refresh();
}

void ShareInner::refresh() {
	auto count = displayedChatsCount();
	if (count) {
		auto rows = (count / _columnCount) + (count % _columnCount ? 1 : 0);
		resize(width(), _rowsTop + rows * _rowHeight);
	} else {
		resize(width(), st::noContactsHeight);
	}
	update();
}

ShareInner::~ShareInner() {
	for_const (auto chat, _dataMap) {
		delete chat;
	}
}

QVector<PeerData*> ShareInner::selected() const {
	QVector<PeerData*> result;
	result.reserve(_dataMap.size());
	for_const (auto chat, _dataMap) {
		if (chat->selected) {
			result.push_back(chat->peer);
		}
	}
	return result;
}

} // namespace internal

QString appendShareGameScoreUrl(const QString &url, const FullMsgId &fullId) {
	auto shareHashData = QByteArray(0x10, Qt::Uninitialized);
	auto ints = reinterpret_cast<int32*>(shareHashData.data());
	ints[0] = MTP::authedId();
	ints[1] = fullId.channel;
	ints[2] = fullId.msg;
	ints[3] = 0;

	auto key128Size = 0x10;
	auto shareHashEncrypted = QByteArray(key128Size + shareHashData.size(), Qt::Uninitialized);
	hashSha1(shareHashData.constData(), shareHashData.size(), shareHashEncrypted.data());
	if (!Local::encrypt(shareHashData.constData(), shareHashEncrypted.data() + key128Size, shareHashData.size(), shareHashEncrypted.constData())) {
		return url;
	}

	auto shareHash = shareHashEncrypted.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
	auto shareUrl = qsl("tg://share_game_score?hash=") + QString::fromLatin1(shareHash);

	auto shareComponent = qsl("tgShareScoreUrl=") + qthelp::url_encode(shareUrl);

	auto hashPosition = url.indexOf('#');
	if (hashPosition < 0) {
		return url + '#' + shareComponent;
	}
	auto hash = url.mid(hashPosition + 1);
	if (hash.indexOf('=') >= 0 || hash.indexOf('?') >= 0) {
		return url + '&' + shareComponent;
	}
	if (!hash.isEmpty()) {
		return url + '?' + shareComponent;
	}
	return url + shareComponent;
}

namespace {

void shareGameScoreFromItem(HistoryItem *item) {
	struct ShareGameScoreData {
		ShareGameScoreData(const FullMsgId &msgId) : msgId(msgId) {
		}
		FullMsgId msgId;
		OrderedSet<mtpRequestId> requests;
	};
	auto data = MakeShared<ShareGameScoreData>(item->fullId());

	auto copyCallback = [data]() {
		if (auto main = App::main()) {
			if (auto item = App::histItemById(data->msgId)) {
				if (auto bot = item->getMessageBot()) {
					if (auto markup = item->Get<HistoryMessageReplyMarkup>()) {
						for (int i = 0, rowsCount = markup->rows.size(); i != rowsCount; ++i) {
							auto &row = markup->rows[i];
							for (int j = 0, buttonsCount = row.size(); j != buttonsCount; ++j) {
								auto &button = row[j];
								if (button.type == HistoryMessageReplyMarkup::Button::Type::Game) {
									auto strData = QString::fromUtf8(button.data);
									auto parts = strData.split(',');
									t_assert(parts.size() > 1);

									QApplication::clipboard()->setText(qsl("https://telegram.me/") + bot->username + qsl("?start=") + parts[1]);

									Ui::Toast::Config toast;
									toast.text = lang(lng_share_game_link_copied);
									Ui::Toast::Show(App::wnd(), toast);
									return;
								}
							}
						}
					}
				}
			}
		}
	};
	auto submitCallback = [data](const QVector<PeerData*> &result) {
		if (!data->requests.empty()) {
			return; // Share clicked already.
		}

		auto doneCallback = [data](const MTPUpdates &updates, mtpRequestId requestId) {
			if (auto main = App::main()) {
				main->sentUpdatesReceived(updates);
			}
			data->requests.remove(requestId);
			if (data->requests.empty()) {
				Ui::Toast::Config toast;
				toast.text = lang(lng_share_done);
				Ui::Toast::Show(App::wnd(), toast);

				Ui::hideLayer();
			}
		};

		MTPmessages_ForwardMessages::Flags sendFlags = MTPmessages_ForwardMessages::Flag::f_with_my_score;
		MTPVector<MTPint> msgIds = MTP_vector<MTPint>(1, MTP_int(data->msgId.msg));
		if (auto main = App::main()) {
			if (auto item = App::histItemById(data->msgId)) {
				for_const (auto peer, result) {
					MTPVector<MTPlong> random = MTP_vector<MTPlong>(1, rand_value<MTPlong>());
					auto request = MTPmessages_ForwardMessages(MTP_flags(sendFlags), item->history()->peer->input, msgIds, random, peer->input);
					auto callback = doneCallback;
					auto requestId = MTP::send(request, rpcDone(std_::move(callback)));
					data->requests.insert(requestId);
				}
			}
		}
	};
	Ui::showLayer(new ShareBox(std_::move(copyCallback), std_::move(submitCallback)));
}

} // namespace

void shareGameScoreByHash(const QString &hash) {
	auto key128Size = 0x10;

	auto hashEncrypted = QByteArray::fromBase64(hash.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
	if (hashEncrypted.size() <= key128Size || (hashEncrypted.size() % 0x10) != 0) {
		Ui::showLayer(new InformBox(lang(lng_confirm_phone_link_invalid)));
		return;
	}

	auto hashData = QByteArray(hashEncrypted.size() - key128Size, Qt::Uninitialized);
	if (!Local::decrypt(hashEncrypted.constData() + key128Size, hashData.data(), hashEncrypted.size() - key128Size, hashEncrypted.constData())) {
		return;
	}

	char checkSha1[20] = { 0 };
	if (memcmp(hashSha1(hashData.constData(), hashData.size(), checkSha1), hashEncrypted.constData(), key128Size) != 0) {
		Ui::showLayer(new InformBox(lang(lng_share_wrong_user)));
		return;
	}
	auto ints = reinterpret_cast<int32*>(hashData.data());
	if (ints[0] != MTP::authedId()) {
		Ui::showLayer(new InformBox(lang(lng_share_wrong_user)));
		return;
	}

	auto channelId = ints[1];
	auto msgId = ints[2];
	if (auto item = App::histItemById(channelId, msgId)) {
		shareGameScoreFromItem(item);
	} else if (App::api()) {
		auto channel = channelId ? App::channelLoaded(channelId) : nullptr;
		if (channel || !channelId) {
			App::api()->requestMessageData(channel, msgId, [](ChannelData *channel, MsgId msgId) {
				if (auto item = App::histItemById(channel, msgId)) {
					shareGameScoreFromItem(item);
				} else {
					Ui::showLayer(new InformBox(lang(lng_edit_deleted)));
				}
			});
		}
	}
}
