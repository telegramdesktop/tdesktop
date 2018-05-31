/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/share_box.h"

#include "dialogs/dialogs_indexed_list.h"
#include "styles/style_boxes.h"
#include "styles/style_history.h"
#include "observer_peer.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "base/qthelp_url.h"
#include "storage/localstorage.h"
#include "boxes/confirm_box.h"
#include "apiwrap.h"
#include "ui/toast/toast.h"
#include "ui/widgets/multi_select.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/text_options.h"
#include "history/history.h"
#include "history/history_media_types.h"
#include "history/history_message.h"
#include "window/themes/window_theme.h"
#include "boxes/peer_list_box.h"
#include "auth_session.h"
#include "messenger.h"

ShareBox::ShareBox(QWidget*, CopyCallback &&copyCallback, SubmitCallback &&submitCallback, FilterCallback &&filterCallback)
: _copyCallback(std::move(copyCallback))
, _submitCallback(std::move(submitCallback))
, _filterCallback(std::move(filterCallback))
, _select(this, st::contactsMultiSelect, langFactory(lng_participant_filter))
, _searchTimer(this) {
}

void ShareBox::prepare() {
	_select->resizeToWidth(st::boxWideWidth);
	Ui::SendPendingMoveResizeEvents(_select);

	setTitle(langFactory(lng_share_title));

	_inner = setInnerWidget(object_ptr<Inner>(this, std::move(_filterCallback)), getTopScrollSkip());
	connect(_inner, SIGNAL(mustScrollTo(int,int)), this, SLOT(onMustScrollTo(int,int)));

	createButtons();

	setDimensions(st::boxWideWidth, st::boxMaxListHeight);

	_select->setQueryChangedCallback([this](const QString &query) { onFilterUpdate(query); });
	_select->setItemRemovedCallback([this](uint64 itemId) {
		if (auto peer = App::peerLoaded(itemId)) {
			_inner->peerUnselected(peer);
			onSelectedChanged();
			update();
		}
	});
	_select->setResizedCallback([this] { updateScrollSkips(); });
	_select->setSubmittedCallback([this](Qt::KeyboardModifiers modifiers) {
		if (modifiers.testFlag(Qt::ControlModifier)
			|| modifiers.testFlag(Qt::MetaModifier)) {
			onSubmit();
		} else {
			_inner->onSelectActive();
		}
	});
	connect(_inner, SIGNAL(searchByUsername()), this, SLOT(onNeedSearchByUsername()));
	_inner->setPeerSelectedChangedCallback([this](PeerData *peer, bool checked) {
		onPeerSelectedChanged(peer, checked);
	});

	_searchTimer->setSingleShot(true);
	connect(_searchTimer, SIGNAL(timeout()), this, SLOT(onSearchByUsername()));

	_select->raise();
}

int ShareBox::getTopScrollSkip() const {
	auto result = 0;
	if (!_select->isHidden()) {
		result += _select->height();
	}
	return result;
}

void ShareBox::updateScrollSkips() {
	setInnerTopSkip(getTopScrollSkip(), true);
}

bool ShareBox::onSearchByUsername(bool searchCache) {
	auto query = _select->getQuery();
	if (query.isEmpty()) {
		if (_peopleRequest) {
			_peopleRequest = 0;
		}
		return true;
	}
	if (!query.isEmpty()) {
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
			_peopleRequest = MTP::send(
				MTPcontacts_Search(
					MTP_string(_peopleQuery),
					MTP_int(SearchPeopleLimit)),
				rpcDone(&ShareBox::peopleReceived),
				rpcFail(&ShareBox::peopleFailed));
			_peopleQueries.insert(_peopleRequest, _peopleQuery);
		}
	}
	return false;
}

void ShareBox::onNeedSearchByUsername() {
	if (!onSearchByUsername(true)) {
		_searchTimer->start(AutoSearchTimeout);
	}
}

void ShareBox::peopleReceived(
		const MTPcontacts_Found &result,
		mtpRequestId requestId) {
	Expects(result.type() == mtpc_contacts_found);

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
			_inner->peopleReceived(
				query,
				found.vmy_results.v,
				found.vresults.v);
		} break;
		}

		_peopleRequest = 0;
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

void ShareBox::setInnerFocus() {
	_select->setInnerFocus();
}

void ShareBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_select->resizeToWidth(width());
	_select->moveToLeft(0, 0);

	updateScrollSkips();

	_inner->resizeToWidth(width());
}

void ShareBox::keyPressEvent(QKeyEvent *e) {
	auto focused = focusWidget();
	if (_select == focused || _select->isAncestorOf(focusWidget())) {
		if (e->key() == Qt::Key_Up) {
			_inner->activateSkipColumn(-1);
		} else if (e->key() == Qt::Key_Down) {
			_inner->activateSkipColumn(1);
		} else if (e->key() == Qt::Key_PageUp) {
			_inner->activateSkipPage(height() - getTopScrollSkip(), -1);
		} else if (e->key() == Qt::Key_PageDown) {
			_inner->activateSkipPage(height() - getTopScrollSkip(), 1);
		} else {
			BoxContent::keyPressEvent(e);
		}
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void ShareBox::updateButtons() {
	auto hasSelected = _inner->hasSelected();
	if (_hasSelected != hasSelected) {
		_hasSelected = hasSelected;
		createButtons();
	}
}

void ShareBox::createButtons() {
	clearButtons();
	if (_hasSelected) {
		addButton(langFactory(lng_share_confirm), [this] { onSubmit(); });
	} else if (_copyCallback) {
		addButton(langFactory(lng_share_copy_link), [this] { onCopyLink(); });
	}
	addButton(langFactory(lng_cancel), [this] { closeBox(); });
}

void ShareBox::onFilterUpdate(const QString &query) {
	onScrollToY(0);
	_inner->updateFilter(query);
}

void ShareBox::addPeerToMultiSelect(PeerData *peer, bool skipAnimation) {
	using AddItemWay = Ui::MultiSelect::AddItemWay;
	auto addItemWay = skipAnimation ? AddItemWay::SkipAnimation : AddItemWay::Default;
	_select->addItem(
		peer->id,
		peer->isSelf() ? lang(lng_saved_short) : peer->shortName(),
		st::activeButtonBg,
		PaintUserpicCallback(peer, true),
		addItemWay);
}

void ShareBox::onPeerSelectedChanged(PeerData *peer, bool checked) {
	if (checked) {
		addPeerToMultiSelect(peer);
		_select->clearQuery();
	} else {
		_select->removeItem(peer->id);
	}
	onSelectedChanged();
	update();
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
	updateButtons();
	update();
}

void ShareBox::onMustScrollTo(int top, int bottom) {
	onScrollToY(top, bottom);
	//auto scrollTop = scrollArea()->scrollTop(), scrollBottom = scrollTop + scrollArea()->height();
	//auto from = scrollTop, to = scrollTop;
	//if (scrollTop > top) {
	//	to = top;
	//} else if (scrollBottom < bottom) {
	//	to = bottom - (scrollBottom - scrollTop);
	//}
	//if (from != to) {
	//	_scrollAnimation.start([this]() { scrollAnimationCallback(); }, from, to, st::shareScrollDuration, anim::sineInOut);
	//}
}

void ShareBox::scrollAnimationCallback() {
	//auto scrollTop = qRound(_scrollAnimation.current(scrollArea()->scrollTop()));
	//scrollArea()->scrollToY(scrollTop);
}

ShareBox::Inner::Inner(QWidget *parent, ShareBox::FilterCallback &&filterCallback) : TWidget(parent)
, _filterCallback(std::move(filterCallback))
, _chatsIndexed(std::make_unique<Dialogs::IndexedList>(Dialogs::SortMode::Add)) {
	_rowsTop = st::shareRowsTop;
	_rowHeight = st::shareRowHeight;
	setAttribute(Qt::WA_OpaquePaintEvent);

	const auto dialogs = App::main()->dialogsList();
	if (const auto self = App::self()) {
		if (_filterCallback(App::self())) {
			_chatsIndexed->addToEnd(App::history(self));
		}
	}
	for (const auto row : dialogs->all()) {
		if (const auto history = row->history()) {
			if (!history->peer->isSelf()
				&& _filterCallback(history->peer)) {
				_chatsIndexed->addToEnd(history);
			}
		}
	}

	_filter = qsl("a");
	updateFilter();

	using UpdateFlag = Notify::PeerUpdate::Flag;
	auto observeEvents = UpdateFlag::NameChanged | UpdateFlag::PhotoChanged;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(observeEvents, [this](const Notify::PeerUpdate &update) {
		notifyPeerUpdated(update);
	}));
	subscribe(Auth().downloaderTaskFinished(), [this] { update(); });

	subscribe(Window::Theme::Background(), [this](const Window::Theme::BackgroundUpdate &update) {
		if (update.paletteChanged()) {
			invalidateCache();
		}
	});
}

void ShareBox::Inner::invalidateCache() {
	for_const (auto data, _dataMap) {
		data->checkbox.invalidateCache();
	}
}

void ShareBox::Inner::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	loadProfilePhotos(visibleTop);
}

void ShareBox::Inner::activateSkipRow(int direction) {
	activateSkipColumn(direction * _columnCount);
}

int ShareBox::Inner::displayedChatsCount() const {
	return _filter.isEmpty() ? _chatsIndexed->size() : (_filtered.size() + d_byUsernameFiltered.size());
}

void ShareBox::Inner::activateSkipColumn(int direction) {
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

void ShareBox::Inner::activateSkipPage(int pageHeight, int direction) {
	activateSkipRow(direction * (pageHeight / _rowHeight));
}

void ShareBox::Inner::notifyPeerUpdated(const Notify::PeerUpdate &update) {
	if (update.flags & Notify::PeerUpdate::Flag::NameChanged) {
		_chatsIndexed->peerNameChanged(
			update.peer,
			update.oldNameFirstLetters);
	}

	updateChat(update.peer);
}

void ShareBox::Inner::updateChat(not_null<PeerData*> peer) {
	auto i = _dataMap.find(peer);
	if (i != _dataMap.cend()) {
		updateChatName(i.value(), peer);
		repaintChat(peer);
	}
}

void ShareBox::Inner::updateChatName(
		not_null<Chat*> chat,
		not_null<PeerData*> peer) {
	const auto text = peer->isSelf() ? lang(lng_saved_messages) : peer->name;
	chat->name.setText(st::shareNameStyle, text, Ui::NameTextOptions());
}

void ShareBox::Inner::repaintChatAtIndex(int index) {
	if (index < 0) return;

	auto row = index / _columnCount;
	auto column = index % _columnCount;
	update(rtlrect(_rowsLeft + qFloor(column * _rowWidthReal), row * _rowHeight, _rowWidth, _rowHeight, width()));
}

ShareBox::Inner::Chat *ShareBox::Inner::getChatAtIndex(int index) {
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

void ShareBox::Inner::repaintChat(not_null<PeerData*> peer) {
	repaintChatAtIndex(chatIndex(peer));
}

int ShareBox::Inner::chatIndex(not_null<PeerData*> peer) const {
	int index = 0;
	if (_filter.isEmpty()) {
		for (const auto row : _chatsIndexed->all()) {
			if (const auto history = row->history()) {
				if (history->peer == peer) {
					return index;
				}
			}
			++index;
		}
	} else {
		for (const auto row : _filtered) {
			if (const auto history = row->history()) {
				if (history->peer == peer) {
					return index;
				}
			}
			++index;
		}
		for (const auto row : d_byUsernameFiltered) {
			if (row->peer == peer) {
				return index;
			}
			++index;
		}
	}
	return -1;
}

void ShareBox::Inner::loadProfilePhotos(int yFrom) {
	if (!parentWidget()) return;
	if (yFrom < 0) {
		yFrom = 0;
	}
	if (auto part = (yFrom % _rowHeight)) {
		yFrom -= part;
	}
	int yTo = yFrom + parentWidget()->height() * 5 * _columnCount;
	if (!yTo) {
		return;
	}
	yFrom *= _columnCount;
	yTo *= _columnCount;

	Auth().downloader().clearPriorities();
	if (_filter.isEmpty()) {
		if (!_chatsIndexed->isEmpty()) {
			auto i = _chatsIndexed->cfind(yFrom, _rowHeight);
			for (auto end = _chatsIndexed->cend(); i != end; ++i) {
				if (((*i)->pos() * _rowHeight) >= yTo) {
					break;
				}
				(*i)->entry()->loadUserpic();
			}
		}
	} else if (!_filtered.isEmpty()) {
		int from = yFrom / _rowHeight;
		if (from < 0) from = 0;
		if (from < _filtered.size()) {
			int to = (yTo / _rowHeight) + 1;
			if (to > _filtered.size()) to = _filtered.size();

			for (; from < to; ++from) {
				_filtered[from]->entry()->loadUserpic();
			}
		}
	}
}

ShareBox::Inner::Chat *ShareBox::Inner::getChat(Dialogs::Row *row) {
	Expects(row->history() != nullptr);

	auto data = static_cast<Chat*>(row->attached);
	if (!data) {
		auto peer = row->history()->peer;
		auto i = _dataMap.constFind(peer);
		if (i == _dataMap.cend()) {
			data = new Chat(peer, [this, peer] { repaintChat(peer); });
			_dataMap.insert(peer, data);
			updateChatName(data, peer);
		} else {
			data = i.value();
		}
		row->attached = data;
	}
	return data;
}

void ShareBox::Inner::setActive(int active) {
	if (active != _active) {
		auto changeNameFg = [this](int index, float64 from, float64 to) {
			if (auto chat = getChatAtIndex(index)) {
				chat->nameActive.start([this, peer = chat->peer] {
					repaintChat(peer);
				}, from, to, st::shareActivateDuration);
			}
		};
		changeNameFg(_active, 1., 0.);
		_active = active;
		changeNameFg(_active, 0., 1.);
	}
	auto y = (_active < _columnCount) ? 0 : (_rowsTop + ((_active / _columnCount) * _rowHeight));
	emit mustScrollTo(y, y + _rowHeight);
}

void ShareBox::Inner::paintChat(
		Painter &p,
		TimeMs ms,
		not_null<Chat*> chat,
		int index) {
	auto x = _rowsLeft + qFloor((index % _columnCount) * _rowWidthReal);
	auto y = _rowsTop + (index / _columnCount) * _rowHeight;

	auto outerWidth = width();
	auto photoLeft = (_rowWidth - (st::sharePhotoCheckbox.imageRadius * 2)) / 2;
	auto photoTop = st::sharePhotoTop;
	chat->checkbox.paint(p, ms, x + photoLeft, y + photoTop, outerWidth);

	auto nameActive = chat->nameActive.current(ms, (index == _active) ? 1. : 0.);
	p.setPen(anim::pen(st::shareNameFg, st::shareNameActiveFg, nameActive));

	auto nameWidth = (_rowWidth - st::shareColumnSkip);
	auto nameLeft = st::shareColumnSkip / 2;
	auto nameTop = photoTop + st::sharePhotoCheckbox.imageRadius * 2 + st::shareNameTop;
	chat->name.drawLeftElided(p, x + nameLeft, y + nameTop, nameWidth, outerWidth, 2, style::al_top, 0, -1, 0, true);
}

ShareBox::Inner::Chat::Chat(PeerData *peer, base::lambda<void()> updateCallback)
: peer(peer)
, checkbox(st::sharePhotoCheckbox, updateCallback, PaintUserpicCallback(peer, true))
, name(st::sharePhotoCheckbox.imageRadius * 2) {
}

void ShareBox::Inner::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();
	auto r = e->rect();
	p.setClipRect(r);
	p.fillRect(r, st::boxBg);
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
				paintChat(p, ms, getChat(*i), indexFrom);
				++indexFrom;
			}
		} else {
			p.setFont(st::noContactsFont);
			p.setPen(st::noContactsColor);
			p.drawText(
				rect().marginsRemoved(st::boxPadding),
				lang(lng_bot_no_chats),
				style::al_center);
		}
	} else {
		if (_filtered.isEmpty()
			&& _byUsernameFiltered.empty()
			&& !_searching) {
			p.setFont(st::noContactsFont);
			p.setPen(st::noContactsColor);
			p.drawText(
				rect().marginsRemoved(st::boxPadding),
				lang(lng_bot_chats_not_found),
				style::al_center);
		} else {
			auto filteredSize = _filtered.size();
			if (filteredSize) {
				if (indexFrom < 0) indexFrom = 0;
				while (indexFrom < indexTo) {
					if (indexFrom >= _filtered.size()) {
						break;
					}
					paintChat(p, ms, getChat(_filtered[indexFrom]), indexFrom);
					++indexFrom;
				}
				indexFrom -= filteredSize;
				indexTo -= filteredSize;
			}
			if (!_byUsernameFiltered.empty()) {
				if (indexFrom < 0) indexFrom = 0;
				while (indexFrom < indexTo) {
					if (indexFrom >= d_byUsernameFiltered.size()) {
						break;
					}
					paintChat(p, ms, d_byUsernameFiltered[indexFrom], filteredSize + indexFrom);
					++indexFrom;
				}
			}
		}
	}
}

void ShareBox::Inner::enterEventHook(QEvent *e) {
	setMouseTracking(true);
}

void ShareBox::Inner::leaveEventHook(QEvent *e) {
	setMouseTracking(false);
}

void ShareBox::Inner::mouseMoveEvent(QMouseEvent *e) {
	updateUpon(e->pos());
	setCursor((_upon >= 0) ? style::cur_pointer : style::cur_default);
}

void ShareBox::Inner::updateUpon(const QPoint &pos) {
	auto x = pos.x(), y = pos.y();
	auto row = (y - _rowsTop) / _rowHeight;
	auto column = qFloor((x - _rowsLeft) / _rowWidthReal);
	auto left = _rowsLeft + qFloor(column * _rowWidthReal) + st::shareColumnSkip / 2;
	auto top = _rowsTop + row * _rowHeight + st::sharePhotoTop;
	auto xupon = (x >= left) && (x < left + (_rowWidth - st::shareColumnSkip));
	auto yupon = (y >= top) && (y < top + st::sharePhotoCheckbox.imageRadius * 2 + st::shareNameTop + st::shareNameStyle.font->height * 2);
	auto upon = (xupon && yupon) ? (row * _columnCount + column) : -1;
	if (upon >= displayedChatsCount()) {
		upon = -1;
	}
	_upon = upon;
}

void ShareBox::Inner::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		updateUpon(e->pos());
		changeCheckState(getChatAtIndex(_upon));
	}
}

void ShareBox::Inner::onSelectActive() {
	changeCheckState(getChatAtIndex(_active > 0 ? _active : 0));
}

void ShareBox::Inner::resizeEvent(QResizeEvent *e) {
	_columnSkip = (width() - _columnCount * st::sharePhotoCheckbox.imageRadius * 2) / float64(_columnCount + 1);
	_rowWidthReal = st::sharePhotoCheckbox.imageRadius * 2 + _columnSkip;
	_rowsLeft = qFloor(_columnSkip / 2);
	_rowWidth = qFloor(_rowWidthReal);
	update();
}

void ShareBox::Inner::changeCheckState(Chat *chat) {
	if (!chat) return;

	if (!_filter.isEmpty()) {
		const auto history = App::history(chat->peer);
		auto row = _chatsIndexed->getRow(history);
		if (!row) {
			const auto rowsByLetter = _chatsIndexed->addToEnd(history);
			const auto it = rowsByLetter.find(0);
			Assert(it != rowsByLetter.cend());
			row = it->second;
		}
		chat = getChat(row);
		if (!chat->checkbox.checked()) {
			_chatsIndexed->moveToTop(history);
		}
	}

	changePeerCheckState(chat, !chat->checkbox.checked());
}

void ShareBox::Inner::peerUnselected(not_null<PeerData*> peer) {
	if (auto chat = _dataMap.value(peer, nullptr)) {
		changePeerCheckState(chat, false, ChangeStateWay::SkipCallback);
	}
}

void ShareBox::Inner::setPeerSelectedChangedCallback(base::lambda<void(PeerData *peer, bool selected)> callback) {
	_peerSelectedChangedCallback = std::move(callback);
}

void ShareBox::Inner::changePeerCheckState(
		not_null<Chat*> chat,
		bool checked,
		ChangeStateWay useCallback) {
	chat->checkbox.setChecked(checked);
	if (checked) {
		_selected.insert(chat->peer);
		setActive(chatIndex(chat->peer));
	} else {
		_selected.remove(chat->peer);
	}
	if (useCallback != ChangeStateWay::SkipCallback
		&& _peerSelectedChangedCallback) {
		_peerSelectedChangedCallback(chat->peer, checked);
	}
}

bool ShareBox::Inner::hasSelected() const {
	return _selected.size();
}

void ShareBox::Inner::updateFilter(QString filter) {
	_lastQuery = filter.toLower().trimmed();

	auto words = TextUtilities::PrepareSearchWords(_lastQuery);
	filter = words.isEmpty() ? QString() : words.join(' ');
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
			QStringList::const_iterator fb = words.cbegin(), fe = words.cend(), fi;

			_filtered.clear();
			if (!words.isEmpty()) {
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
					for (const auto row : *toFilter) {
						auto &nameWords = row->entry()->chatsListNameWords();
						auto nb = nameWords.cbegin(), ne = nameWords.cend(), ni = nb;
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

void ShareBox::Inner::peopleReceived(
		const QString &query,
		const QVector<MTPPeer> &my,
		const QVector<MTPPeer> &people) {
	_lastQuery = query.toLower().trimmed();
	if (_lastQuery.at(0) == '@') {
		_lastQuery = _lastQuery.mid(1);
	}
	int32 already = _byUsernameFiltered.size();
	_byUsernameFiltered.reserve(already + my.size() + people.size());
	d_byUsernameFiltered.reserve(already + my.size() + people.size());
	const auto feedList = [&](const QVector<MTPPeer> &list) {
		for (const auto &mtpPeer : list) {
			if (const auto peer = App::peerLoaded(peerFromMTP(mtpPeer))) {
				const auto history = App::historyLoaded(peer);
				if (!_filterCallback(peer)) {
					continue;
				} else if (history && _chatsIndexed->getRow(history)) {
					continue;
				} else if (base::contains(_byUsernameFiltered, peer)) {
					continue;
				}
				auto chat = new Chat(peer, [=] { repaintChat(peer); });
				updateChatName(chat, peer);
				_byUsernameFiltered.push_back(peer);
				d_byUsernameFiltered.push_back(chat);
			}
		}
	};
	feedList(my);
	feedList(people);

	_searching = false;
	refresh();
}

void ShareBox::Inner::refresh() {
	auto count = displayedChatsCount();
	if (count) {
		auto rows = (count / _columnCount) + (count % _columnCount ? 1 : 0);
		resize(width(), _rowsTop + rows * _rowHeight);
	} else {
		resize(width(), st::noContactsHeight);
	}
	update();
}

ShareBox::Inner::~Inner() {
	for_const (auto chat, _dataMap) {
		delete chat;
	}
}

QVector<PeerData*> ShareBox::Inner::selected() const {
	QVector<PeerData*> result;
	result.reserve(_dataMap.size());
	for_const (auto chat, _dataMap) {
		if (chat->checkbox.checked()) {
			result.push_back(chat->peer);
		}
	}
	return result;
}

QString AppendShareGameScoreUrl(const QString &url, const FullMsgId &fullId) {
	auto shareHashData = QByteArray(0x10, Qt::Uninitialized);
	auto shareHashDataInts = reinterpret_cast<int32*>(shareHashData.data());
	auto channel = fullId.channel ? App::channelLoaded(fullId.channel) : static_cast<ChannelData*>(nullptr);
	auto channelAccessHash = channel ? channel->access : 0ULL;
	auto channelAccessHashInts = reinterpret_cast<int32*>(&channelAccessHash);
	shareHashDataInts[0] = Auth().userId();
	shareHashDataInts[1] = fullId.channel;
	shareHashDataInts[2] = fullId.msg;
	shareHashDataInts[3] = channelAccessHashInts[0];

	// Count SHA1() of data.
	auto key128Size = 0x10;
	auto shareHashEncrypted = QByteArray(key128Size + shareHashData.size(), Qt::Uninitialized);
	hashSha1(shareHashData.constData(), shareHashData.size(), shareHashEncrypted.data());

	// Mix in channel access hash to the first 64 bits of SHA1 of data.
	*reinterpret_cast<uint64*>(shareHashEncrypted.data()) ^= *reinterpret_cast<uint64*>(channelAccessHashInts);

	// Encrypt data.
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

void ShareGameScoreByHash(const QString &hash) {
	auto key128Size = 0x10;

	auto hashEncrypted = QByteArray::fromBase64(hash.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
	if (hashEncrypted.size() <= key128Size || (hashEncrypted.size() % 0x10) != 0) {
		Ui::show(Box<InformBox>(lang(lng_confirm_phone_link_invalid)));
		return;
	}

	// Decrypt data.
	auto hashData = QByteArray(hashEncrypted.size() - key128Size, Qt::Uninitialized);
	if (!Local::decrypt(hashEncrypted.constData() + key128Size, hashData.data(), hashEncrypted.size() - key128Size, hashEncrypted.constData())) {
		return;
	}

	// Count SHA1() of data.
	char dataSha1[20] = { 0 };
	hashSha1(hashData.constData(), hashData.size(), dataSha1);

	// Mix out channel access hash from the first 64 bits of SHA1 of data.
	auto channelAccessHash = *reinterpret_cast<uint64*>(hashEncrypted.data()) ^ *reinterpret_cast<uint64*>(dataSha1);

	// Check next 64 bits of SHA1() of data.
	auto skipSha1Part = sizeof(channelAccessHash);
	if (memcmp(dataSha1 + skipSha1Part, hashEncrypted.constData() + skipSha1Part, key128Size - skipSha1Part) != 0) {
		Ui::show(Box<InformBox>(lang(lng_share_wrong_user)));
		return;
	}

	auto hashDataInts = reinterpret_cast<int32*>(hashData.data());
	if (!AuthSession::Exists() || hashDataInts[0] != Auth().userId()) {
		Ui::show(Box<InformBox>(lang(lng_share_wrong_user)));
		return;
	}

	// Check first 32 bits of channel access hash.
	auto channelAccessHashInts = reinterpret_cast<int32*>(&channelAccessHash);
	if (channelAccessHashInts[0] != hashDataInts[3]) {
		Ui::show(Box<InformBox>(lang(lng_share_wrong_user)));
		return;
	}

	auto channelId = hashDataInts[1];
	auto msgId = hashDataInts[2];
	if (!channelId && channelAccessHash) {
		// If there is no channel id, there should be no channel access_hash.
		Ui::show(Box<InformBox>(lang(lng_share_wrong_user)));
		return;
	}

	if (auto item = App::histItemById(channelId, msgId)) {
		FastShareMessage(item);
	} else {
		auto resolveMessageAndShareScore = [msgId](ChannelData *channel) {
			Auth().api().requestMessageData(channel, msgId, [](ChannelData *channel, MsgId msgId) {
				if (auto item = App::histItemById(channel, msgId)) {
					FastShareMessage(item);
				} else {
					Ui::show(Box<InformBox>(lang(lng_edit_deleted)));
				}
			});
		};

		auto channel = channelId ? App::channelLoaded(channelId) : nullptr;
		if (channel || !channelId) {
			resolveMessageAndShareScore(channel);
		} else {
			auto requestChannelIds = MTP_vector<MTPInputChannel>(1, MTP_inputChannel(MTP_int(channelId), MTP_long(channelAccessHash)));
			auto requestChannel = MTPchannels_GetChannels(requestChannelIds);
			MTP::send(requestChannel, rpcDone([channelId, resolveMessageAndShareScore](const MTPmessages_Chats &result) {
				if (auto chats = Api::getChatsFromMessagesChats(result)) {
					App::feedChats(*chats);
				}
				if (auto channel = App::channelLoaded(channelId)) {
					resolveMessageAndShareScore(channel);
				}
			}));
		}
	}
}
