/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_inner_widget.h"

#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_layout.h"
#include "dialogs/dialogs_search_from_controllers.h"
#include "history/feed/history_feed_section.h"
#include "history/history.h"
#include "history/history_item.h"
#include "core/shortcuts.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/text_options.h"
#include "data/data_drafts.h"
#include "data/data_feed.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "storage/localstorage.h"
#include "apiwrap.h"
#include "window/themes/window_theme.h"
#include "observer_peer.h"
#include "chat_helpers/stickers.h"
#include "auth_session.h"
#include "window/notifications_manager.h"
#include "window/window_controller.h"
#include "window/window_peer_menu.h"
#include "ui/widgets/multi_select.h"
#include "ui/empty_userpic.h"
#include "styles/style_dialogs.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_window.h"

namespace {

constexpr auto kHashtagResultsLimit = 5;
constexpr auto kStartReorderThreshold = 30;

} // namespace

struct DialogsInner::ImportantSwitch {
	Dialogs::RippleRow row;
};

struct DialogsInner::HashtagResult {
	HashtagResult(const QString &tag) : tag(tag) {
	}
	QString tag;
	Dialogs::RippleRow row;
};

struct DialogsInner::PeerSearchResult {
	PeerSearchResult(not_null<PeerData*> peer) : peer(peer) {
	}
	not_null<PeerData*> peer;
	Dialogs::RippleRow row;
};

DialogsInner::DialogsInner(QWidget *parent, not_null<Window::Controller*> controller, QWidget *main) : SplittedWidget(parent)
, _controller(controller)
, _dialogs(std::make_unique<Dialogs::IndexedList>(Dialogs::SortMode::Date))
, _contactsNoDialogs(std::make_unique<Dialogs::IndexedList>(Dialogs::SortMode::Name))
, _contacts(std::make_unique<Dialogs::IndexedList>(Dialogs::SortMode::Name))
, _a_pinnedShifting(animation(this, &DialogsInner::step_pinnedShifting))
, _addContactLnk(this, lang(lng_add_contact_button))
, _cancelSearchInChat(this, st::dialogsCancelSearchInPeer)
, _cancelSearchFromUser(this, st::dialogsCancelSearchInPeer) {

#ifdef OS_MAC_OLD
	// Qt 5.3.2 build is working with glitches otherwise.
	setAttribute(Qt::WA_OpaquePaintEvent, false);
#endif // OS_MAC_OLD

	if (Global::DialogsModeEnabled()) {
		_dialogsImportant = std::make_unique<Dialogs::IndexedList>(Dialogs::SortMode::Date);
		_importantSwitch = std::make_unique<ImportantSwitch>();
	}
	connect(main, SIGNAL(dialogRowReplaced(Dialogs::Row*, Dialogs::Row*)), this, SLOT(onDialogRowReplaced(Dialogs::Row*, Dialogs::Row*)));
	connect(_addContactLnk, SIGNAL(clicked()), App::wnd(), SLOT(onShowAddContact()));
	_cancelSearchInChat->setClickedCallback([this] { cancelSearchInChat(); });
	_cancelSearchInChat->hide();
	_cancelSearchFromUser->setClickedCallback([this] { searchFromUserChanged.notify(nullptr); });
	_cancelSearchFromUser->hide();

	subscribe(Auth().downloaderTaskFinished(), [this] { update(); });
	subscribe(Auth().data().contactsLoaded(), [this](bool) { refresh(); });
	Auth().data().itemRemoved(
	) | rpl::start_with_next(
		[this](auto item) { itemRemoved(item); },
		lifetime());
	Auth().data().itemRepaintRequest(
	) | rpl::start_with_next([=](auto item) {
		const auto history = item->history();
		if (history->textCachedFor == item) {
			history->updateChatListEntry();
		}
		if (const auto feed = history->peer->feed()) {
			if (feed->textCachedFor == item) {
				feed->updateChatListEntry();
			}
		}
	}, lifetime());
	subscribe(App::histories().sendActionAnimationUpdated(), [this](const Histories::SendActionAnimationUpdate &update) {
		auto updateRect = Dialogs::Layout::RowPainter::sendActionAnimationRect(update.width, update.height, getFullWidth(), update.textUpdated);
		updateDialogRow(
			Dialogs::RowDescriptor(update.history, FullMsgId()),
			updateRect,
			UpdateRowSection::Default | UpdateRowSection::Filtered);
	});

	subscribe(Window::Theme::Background(), [=](const Window::Theme::BackgroundUpdate &data) {
		if (data.paletteChanged()) {
			Dialogs::Layout::clearUnreadBadgesCache();
		}
	});

	using UpdateFlag = Notify::PeerUpdate::Flag;
	auto changes = UpdateFlag::ChatPinnedChanged
		| UpdateFlag::NameChanged
		| UpdateFlag::PhotoChanged
		| UpdateFlag::UserIsContact
		| UpdateFlag::UserOccupiedChanged;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(changes, [this](const Notify::PeerUpdate &update) {
		if (update.flags & UpdateFlag::ChatPinnedChanged) {
			stopReorderPinned();
		}
		if (update.flags & UpdateFlag::NameChanged) {
			handlePeerNameChange(update.peer, update.oldNameFirstLetters);
		}
		if (update.flags & (UpdateFlag::PhotoChanged | UpdateFlag::UserOccupiedChanged)) {
			this->update();
			emit App::main()->dialogsUpdated();
		}
		if (update.flags & UpdateFlag::UserIsContact) {
			if (const auto user = update.peer->asUser()) {
				userIsContactUpdated(user);
			}
		}
	}));
	Auth().data().feedUpdated(
	) | rpl::start_with_next([=](const Data::FeedUpdate &update) {
		updateDialogRow(
			Dialogs::RowDescriptor(update.feed, FullMsgId()),
			QRect(0, 0, getFullWidth(), st::dialogsRowHeight));
	}, lifetime());

	_controller->activeChatEntryValue(
	) | rpl::combine_previous(
	) | rpl::start_with_next([=](
			Dialogs::RowDescriptor previous,
			Dialogs::RowDescriptor next) {
		const auto rect = QRect(0, 0, getFullWidth(), st::dialogsRowHeight);
		updateDialogRow(previous, rect);
		updateDialogRow(next, rect);
	}, lifetime());
	refresh();

	setupShortcuts();
}

int DialogsInner::dialogsOffset() const {
	return _dialogsImportant ? st::dialogsImportantBarHeight : 0;
}

int DialogsInner::proxyPromotedCount() const {
	auto result = 0;
	for_const (auto row, *shownDialogs()) {
		if (row->entry()->useProxyPromotion()) {
			++result;
		} else {
			break;
		}
	}
	return result;
}

int DialogsInner::pinnedOffset() const {
	return dialogsOffset() + proxyPromotedCount() * st::dialogsRowHeight;
}

int DialogsInner::filteredOffset() const {
	return _hashtagResults.size() * st::mentionHeight;
}

int DialogsInner::peerSearchOffset() const {
	return filteredOffset() + (_filterResults.size() * st::dialogsRowHeight) + st::searchedBarHeight;
}

int DialogsInner::searchedOffset() const {
	auto result = peerSearchOffset() + (_peerSearchResults.empty() ? 0 : ((_peerSearchResults.size() * st::dialogsRowHeight) + st::searchedBarHeight));
	if (_searchInChat) {
		result += searchInChatSkip();
	}
	return result;
}

int DialogsInner::searchInChatSkip() const {
	auto result = st::searchedBarHeight + st::dialogsSearchInHeight;
	if (_searchFromUser) {
		result += st::lineWidth + st::dialogsSearchInHeight;
	}
	return result;
}

void DialogsInner::paintRegion(Painter &p, const QRegion &region, bool paintingOther) {
	QRegion original(rtl() ? region.translated(-otherWidth(), 0) : region);
	if (App::wnd() && App::wnd()->contentOverlapped(this, original)) return;

	if (!App::main()) return;

	auto r = region.boundingRect();
	if (!paintingOther) {
		p.setClipRect(r);
	}
	const auto activeEntry = _controller->activeChatEntryCurrent();
	auto fullWidth = getFullWidth();
	auto ms = getms();
	if (_state == State::Default) {
		if (_a_pinnedShifting.animating()) {
			_a_pinnedShifting.step(ms, false);
		}

		auto rows = shownDialogs();
		auto dialogsClip = r;
		if (_dialogsImportant) {
			auto selected = isPressed() ? _importantSwitchPressed : _importantSwitchSelected;
			Dialogs::Layout::paintImportantSwitch(p, Global::DialogsMode(), fullWidth, selected, paintingOther);
			dialogsClip.translate(0, -st::dialogsImportantBarHeight);
			p.translate(0, st::dialogsImportantBarHeight);
		}
		auto otherStart = rows->size() * st::dialogsRowHeight;
		const auto active = activeEntry.key;
		const auto selected = _menuKey
			? _menuKey
			: (isPressed()
				? (_pressed
					? _pressed->key()
					: Dialogs::Key())
				: (_selected
					? _selected->key()
					: Dialogs::Key()));
		if (otherStart) {
			auto reorderingPinned = (_aboveIndex >= 0 && !_pinnedRows.empty());
			auto &list = rows->all();
			if (reorderingPinned) {
				dialogsClip = dialogsClip.marginsAdded(QMargins(0, st::dialogsRowHeight, 0, st::dialogsRowHeight));
			}

			const auto promoted = proxyPromotedCount();
			const auto paintDialog = [&](not_null<Dialogs::Row*> row) {
				const auto pinned = row->pos() - promoted;
				const auto count = _pinnedRows.size();
				const auto xadd = 0;
				const auto yadd = base::in_range(pinned, 0, count)
					? qRound(_pinnedRows[pinned].yadd.current())
					: 0;
				if (xadd || yadd) {
					p.translate(xadd, yadd);
				}
				const auto isActive = (row->key() == active);
				const auto isSelected = (row->key() == selected);
				Dialogs::Layout::RowPainter::paint(
					p,
					row,
					fullWidth,
					isActive,
					isSelected,
					paintingOther,
					ms);
				if (xadd || yadd) {
					p.translate(-xadd, -yadd);
				}
			};

			auto i = list.cfind(dialogsClip.top(), st::dialogsRowHeight);
			if (i != list.cend()) {
				auto lastPaintedPos = (*i)->pos();

				// If we're reordering pinned chats we need to fill this area background first.
				if (reorderingPinned) {
					p.fillRect(0, promoted * st::dialogsRowHeight, fullWidth, st::dialogsRowHeight * _pinnedRows.size(), st::dialogsBg);
				}

				p.translate(0, lastPaintedPos * st::dialogsRowHeight);
				for (auto e = list.cend(); i != e; ++i) {
					auto row = (*i);
					if (lastPaintedPos * st::dialogsRowHeight >= dialogsClip.top() + dialogsClip.height()) {
						break;
					}

					// Skip currently dragged chat to paint it above others after.
					if (lastPaintedPos != promoted + _aboveIndex
						|| _aboveIndex < 0) {
						paintDialog(row);
					}

					p.translate(0, st::dialogsRowHeight);
					++lastPaintedPos;
				}

				// Paint the dragged chat above all others.
				if (_aboveIndex >= 0) {
					auto i = list.cfind(promoted + _aboveIndex, 1);
					auto pos = (i == list.cend()) ? -1 : (*i)->pos();
					if (pos == promoted + _aboveIndex) {
						p.translate(0, (pos - lastPaintedPos) * st::dialogsRowHeight);
						paintDialog(*i);
						p.translate(0, (lastPaintedPos - pos) * st::dialogsRowHeight);
					}
				}
			}
		}
		if (!otherStart) {
			p.fillRect(dialogsClip, st::dialogsBg);
			if (!paintingOther) {
				p.setFont(st::noContactsFont);
				p.setPen(st::noContactsColor);
				p.drawText(QRect(0, 0, fullWidth, st::noContactsHeight - (Auth().data().contactsLoaded().value() ? st::noContactsFont->height : 0)), lang(Auth().data().contactsLoaded().value() ? lng_no_chats : lng_contacts_loading), style::al_center);
			}
		}
	} else if (_state == State::Filtered) {
		if (!_hashtagResults.empty()) {
			auto from = floorclamp(r.y(), st::mentionHeight, 0, _hashtagResults.size());
			auto to = ceilclamp(r.y() + r.height(), st::mentionHeight, 0, _hashtagResults.size());
			p.translate(0, from * st::mentionHeight);
			if (from < _hashtagResults.size()) {
				auto htagwidth = fullWidth - st::dialogsPadding.x() * 2;

				p.setFont(st::mentionFont);
				for (; from < to; ++from) {
					auto &result = _hashtagResults[from];
					bool selected = (from == (isPressed() ? _hashtagPressed : _hashtagSelected));
					p.fillRect(0, 0, fullWidth, st::mentionHeight, selected ? st::mentionBgOver : st::dialogsBg);
					result->row.paintRipple(p, 0, 0, fullWidth, ms);
					if (!paintingOther) {
						auto &tag = result->tag;
						if (selected) {
							int skip = (st::mentionHeight - st::smallCloseIconOver.height()) / 2;
							st::smallCloseIconOver.paint(p, QPoint(fullWidth - st::smallCloseIconOver.width() - skip, skip), width());
						}
						auto first = (_hashtagFilter.size() < 2) ? QString() : ('#' + tag.mid(0, _hashtagFilter.size() - 1));
						auto second = (_hashtagFilter.size() < 2) ? ('#' + tag) : tag.mid(_hashtagFilter.size() - 1);
						auto firstwidth = st::mentionFont->width(first);
						auto secondwidth = st::mentionFont->width(second);
						if (htagwidth < firstwidth + secondwidth) {
							if (htagwidth < firstwidth + st::mentionFont->elidew) {
								first = st::mentionFont->elided(first + second, htagwidth);
								second = QString();
							} else {
								second = st::mentionFont->elided(second, htagwidth - firstwidth);
							}
						}

						p.setFont(st::mentionFont);
						if (!first.isEmpty()) {
							p.setPen(selected ? st::mentionFgOverActive : st::mentionFgActive);
							p.drawText(st::dialogsPadding.x(), st::mentionTop + st::mentionFont->ascent, first);
						}
						if (!second.isEmpty()) {
							p.setPen(selected ? st::mentionFgOver : st::mentionFg);
							p.drawText(st::dialogsPadding.x() + firstwidth, st::mentionTop + st::mentionFont->ascent, second);
						}
					}
					p.translate(0, st::mentionHeight);
				}
			}
		}
		if (!_filterResults.isEmpty()) {
			auto skip = filteredOffset();
			auto from = floorclamp(r.y() - skip, st::dialogsRowHeight, 0, _filterResults.size());
			auto to = ceilclamp(r.y() + r.height() - skip, st::dialogsRowHeight, 0, _filterResults.size());
			p.translate(0, from * st::dialogsRowHeight);
			if (from < _filterResults.size()) {
				for (; from < to; ++from) {
					const auto row = _filterResults[from];
					const auto key = row->key();
					const auto active = (activeEntry.key == key)
						&& !activeEntry.fullId;
					const auto selected = _menuKey
						? (key == _menuKey)
						: (from == (isPressed()
							? _filteredPressed
							: _filteredSelected));
					Dialogs::Layout::RowPainter::paint(
						p,
						_filterResults[from],
						fullWidth,
						active,
						selected,
						paintingOther,
						ms);
					p.translate(0, st::dialogsRowHeight);
				}
			}
		}

		if (!_peerSearchResults.empty()) {
			p.fillRect(0, 0, fullWidth, st::searchedBarHeight, st::searchedBarBg);
			if (!paintingOther) {
				p.setFont(st::searchedBarFont);
				p.setPen(st::searchedBarFg);
				p.drawTextLeft(st::searchedBarPosition.x(), st::searchedBarPosition.y(), width(), lang(lng_search_global_results));
			}
			p.translate(0, st::searchedBarHeight);

			auto skip = peerSearchOffset();
			auto from = floorclamp(r.y() - skip, st::dialogsRowHeight, 0, _peerSearchResults.size());
			auto to = ceilclamp(r.y() + r.height() - skip, st::dialogsRowHeight, 0, _peerSearchResults.size());
			p.translate(0, from * st::dialogsRowHeight);
			if (from < _peerSearchResults.size()) {
				const auto activePeer = activeEntry.key.peer();
				for (; from < to; ++from) {
					const auto &result = _peerSearchResults[from];
					const auto peer = result->peer;
					const auto active = !activeEntry.fullId
						&& activePeer
						&& ((peer == activePeer)
							|| (peer->migrateTo() == activePeer));
					const auto selected = (from == (isPressed()
						? _peerSearchPressed
						: _peerSearchSelected));
					paintPeerSearchResult(p, result.get(), fullWidth, active, selected, paintingOther, ms);
					p.translate(0, st::dialogsRowHeight);
				}
			}
		}

		if (_searchInChat) {
			paintSearchInChat(p, fullWidth, paintingOther, ms);
			p.translate(0, searchInChatSkip());
			if (_waitingForSearch && _searchResults.empty()) {
				p.fillRect(
					0,
					0,
					fullWidth,
					st::searchedBarHeight,
					st::searchedBarBg);
				if (!paintingOther) {
					p.setFont(st::searchedBarFont);
					p.setPen(st::searchedBarFg);
					p.drawTextLeft(
						st::searchedBarPosition.x(),
						st::searchedBarPosition.y(),
						width(),
						lang(lng_dlg_search_for_messages));
				}
				p.translate(0, st::searchedBarHeight);
			}
		}

		const auto showUnreadInSearchResults = uniqueSearchResults();
		if (!_waitingForSearch || !_searchResults.empty()) {
			const auto text = _searchResults.empty()
				? lang(lng_search_no_results)
				: showUnreadInSearchResults
				? qsl("Search results")
				: lng_search_found_results(
					lt_count,
					_searchedMigratedCount + _searchedCount);
			p.fillRect(0, 0, fullWidth, st::searchedBarHeight, st::searchedBarBg);
			if (!paintingOther) {
				p.setFont(st::searchedBarFont);
				p.setPen(st::searchedBarFg);
				p.drawTextLeft(st::searchedBarPosition.x(), st::searchedBarPosition.y(), width(), text);
			}
			p.translate(0, st::searchedBarHeight);

			auto skip = searchedOffset();
			auto from = floorclamp(r.y() - skip, st::dialogsRowHeight, 0, _searchResults.size());
			auto to = ceilclamp(r.y() + r.height() - skip, st::dialogsRowHeight, 0, _searchResults.size());
			p.translate(0, from * st::dialogsRowHeight);
			if (from < _searchResults.size()) {
				for (; from < to; ++from) {
					const auto &result = _searchResults[from];
					const auto active = isSearchResultActive(result.get(), activeEntry);
					const auto selected = (from == (isPressed()
						? _searchedPressed
						: _searchedSelected));
					Dialogs::Layout::RowPainter::paint(
						p,
						result.get(),
						fullWidth,
						active,
						selected,
						paintingOther,
						ms,
						showUnreadInSearchResults);
					p.translate(0, st::dialogsRowHeight);
				}
			}
		}
	}
}

bool DialogsInner::isSearchResultActive(
		not_null<Dialogs::FakeRow*> result,
		const Dialogs::RowDescriptor &entry) const {
	const auto item = result->item();
	const auto peer = item->history()->peer;
	return (item->fullId() == entry.fullId)
		|| (peer->migrateTo()
			&& (peer->migrateTo()->bareId() == entry.fullId.channel)
			&& (item->id == -entry.fullId.msg))
		|| (uniqueSearchResults() && peer == entry.key.peer());
}

void DialogsInner::paintPeerSearchResult(
		Painter &p,
		not_null<const PeerSearchResult*> result,
		int fullWidth,
		bool active,
		bool selected,
		bool onlyBackground,
		TimeMs ms) const {
	QRect fullRect(0, 0, fullWidth, st::dialogsRowHeight);
	p.fillRect(fullRect, active ? st::dialogsBgActive : (selected ? st::dialogsBgOver : st::dialogsBg));
	if (!active) {
		result->row.paintRipple(p, 0, 0, fullWidth, ms);
	}
	if (onlyBackground) return;

	auto peer = result->peer;
	auto userpicPeer = (peer->migrateTo() ? peer->migrateTo() : peer);
	userpicPeer->paintUserpicLeft(p, st::dialogsPadding.x(), st::dialogsPadding.y(), getFullWidth(), st::dialogsPhotoSize);

	auto nameleft = st::dialogsPadding.x() + st::dialogsPhotoSize + st::dialogsPhotoPadding;
	auto namewidth = fullWidth - nameleft - st::dialogsPadding.x();
	QRect rectForName(nameleft, st::dialogsPadding.y() + st::dialogsNameTop, namewidth, st::msgNameFont->height);

	// draw chat icon
	if (auto chatTypeIcon = Dialogs::Layout::ChatTypeIcon(peer, active, selected)) {
		chatTypeIcon->paint(p, rectForName.topLeft(), fullWidth);
		rectForName.setLeft(rectForName.left() + st::dialogsChatTypeSkip);
	}
	if (peer->isVerified()) {
		auto icon = &(active ? st::dialogsVerifiedIconActive : (selected ? st::dialogsVerifiedIconOver : st::dialogsVerifiedIcon));
		rectForName.setWidth(rectForName.width() - icon->width());
		icon->paint(p, rectForName.topLeft() + QPoint(qMin(peer->dialogName().maxWidth(), rectForName.width()), 0), fullWidth);
	}

	QRect tr(nameleft, st::dialogsPadding.y() + st::msgNameFont->height + st::dialogsSkip, namewidth, st::dialogsTextFont->height);
	p.setFont(st::dialogsTextFont);
	QString username = peer->userName();
	if (!active && username.toLower().startsWith(_peerSearchQuery)) {
		auto first = '@' + username.mid(0, _peerSearchQuery.size());
		auto second = username.mid(_peerSearchQuery.size());
		auto w = st::dialogsTextFont->width(first);
		if (w >= tr.width()) {
			p.setPen(st::dialogsTextFgService);
			p.drawText(tr.left(), tr.top() + st::dialogsTextFont->ascent, st::dialogsTextFont->elided(first, tr.width()));
		} else {
			p.setPen(st::dialogsTextFgService);
			p.drawText(tr.left(), tr.top() + st::dialogsTextFont->ascent, first);
			p.setPen(st::dialogsTextFg);
			p.drawText(tr.left() + w, tr.top() + st::dialogsTextFont->ascent, st::dialogsTextFont->elided(second, tr.width() - w));
		}
	} else {
		p.setPen(active ? st::dialogsTextFgActive : st::dialogsTextFgService);
		p.drawText(tr.left(), tr.top() + st::dialogsTextFont->ascent, st::dialogsTextFont->elided('@' + username, tr.width()));
	}

	p.setPen(active ? st::dialogsTextFgActive : st::dialogsNameFg);
	peer->dialogName().drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
}

void DialogsInner::paintSearchInChat(
		Painter &p,
		int fullWidth,
		bool onlyBackground,
		TimeMs ms) const {
	auto height = searchInChatSkip();

	auto top = st::searchedBarHeight;
	p.fillRect(0, 0, fullWidth, top, st::searchedBarBg);
	if (!onlyBackground) {
		p.setFont(st::searchedBarFont);
		p.setPen(st::searchedBarFg);
		p.drawTextLeft(st::searchedBarPosition.x(), st::searchedBarPosition.y(), width(), lang(lng_dlg_search_in));
	}

	auto fullRect = QRect(0, top, fullWidth, height - top);
	p.fillRect(fullRect, st::dialogsBg);
	if (_searchFromUser) {
		p.fillRect(QRect(0, top + st::dialogsSearchInHeight, fullWidth, st::lineWidth), st::shadowFg);
	}
	if (onlyBackground) return;

	p.setPen(st::dialogsNameFg);
	if (const auto peer = _searchInChat.peer()) {
		if (peer->isSelf()) {
			paintSearchInSaved(p, top, fullWidth, _searchInChatText);
		} else {
			paintSearchInPeer(p, peer, top, fullWidth, _searchInChatText);
		}
	} else if (const auto feed = _searchInChat.feed()) {
		paintSearchInFeed(p, feed, top, fullWidth, _searchInChatText);
	} else {
		Unexpected("Empty Dialogs::Key in paintSearchInChat.");
	}
	if (const auto from = _searchFromUser) {
		top += st::dialogsSearchInHeight + st::lineWidth;
		p.setPen(st::dialogsTextFg);
		p.setTextPalette(st::dialogsSearchFromPalette);
		paintSearchInPeer(p, from, top, fullWidth, _searchFromUserText);
		p.restoreTextPalette();
	}
}
template <typename PaintUserpic>
void DialogsInner::paintSearchInFilter(
		Painter &p,
		PaintUserpic paintUserpic,
		int top,
		int fullWidth,
		const style::icon *icon,
		const Text &text) const {
	const auto savedPen = p.pen();
	const auto userpicLeft = st::dialogsPadding.x();
	const auto userpicTop = top
		+ (st::dialogsSearchInHeight - st::dialogsSearchInPhotoSize) / 2;
	paintUserpic(p, userpicLeft, userpicTop, st::dialogsSearchInPhotoSize);

	const auto nameleft = st::dialogsPadding.x()
		+ st::dialogsSearchInPhotoSize
		+ st::dialogsSearchInPhotoPadding;
	const auto namewidth = fullWidth
		- nameleft
		- st::dialogsPadding.x() * 2
		- st::dialogsCancelSearch.width;
	auto rectForName = QRect(
		nameleft,
		top + (st::dialogsSearchInHeight - st::msgNameFont->height) / 2,
		namewidth,
		st::msgNameFont->height);
	if (icon) {
		icon->paint(p, rectForName.topLeft(), fullWidth);
		rectForName.setLeft(rectForName.left() + st::dialogsChatTypeSkip);
	}
	p.setPen(savedPen);
	text.drawLeftElided(
		p,
		rectForName.left(),
		rectForName.top(),
		rectForName.width(),
		getFullWidth());
}

void DialogsInner::paintSearchInPeer(
		Painter &p,
		not_null<PeerData*> peer,
		int top,
		int fullWidth,
		const Text &text) const {
	const auto paintUserpic = [&](Painter &p, int x, int y, int size) {
		peer->paintUserpicLeft(p, x, y, fullWidth, size);
	};
	const auto icon = Dialogs::Layout::ChatTypeIcon(peer, false, false);
	paintSearchInFilter(p, paintUserpic, top, fullWidth, icon, text);
}

void DialogsInner::paintSearchInSaved(
		Painter &p,
		int top,
		int fullWidth,
		const Text &text) const {
	const auto paintUserpic = [&](Painter &p, int x, int y, int size) {
		Ui::EmptyUserpic::PaintSavedMessages(p, x, y, fullWidth, size);
	};
	paintSearchInFilter(p, paintUserpic, top, fullWidth, nullptr, text);
}

void DialogsInner::paintSearchInFeed(
		Painter &p,
		not_null<Data::Feed*> feed,
		int top,
		int fullWidth,
		const Text &text) const {
	const auto paintUserpic = [&](Painter &p, int x, int y, int size) {
		feed->paintUserpicLeft(p, x, y, fullWidth, size);
	};
	const auto icon = Dialogs::Layout::FeedTypeIcon(feed, false, false);
	paintSearchInFilter(p, paintUserpic, top, fullWidth, icon, text);
}

void DialogsInner::activate() {
}

void DialogsInner::mouseMoveEvent(QMouseEvent *e) {
	if (_mouseLastGlobalPosition != e->globalPos()) {
		_mouseLastGlobalPosition = e->globalPos();
		_mouseSelection = true;
	}
	updateSelected(e->pos());
}

void DialogsInner::clearIrrelevantState() {
	if (_state == State::Default) {
		_hashtagSelected = -1;
		setHashtagPressed(-1);
		_hashtagDeleteSelected = _hashtagDeletePressed = false;
		_filteredSelected = -1;
		setFilteredPressed(-1);
		_peerSearchSelected = -1;
		setPeerSearchPressed(-1);
		_searchedSelected = -1;
		setSearchedPressed(-1);
	} else if (_state == State::Filtered) {
		_importantSwitchSelected = false;
		setImportantSwitchPressed(false);
		_selected = nullptr;
		setPressed(nullptr);
	}
}

void DialogsInner::updateSelected(QPoint localPos) {
	if (updateReorderPinned(localPos)) {
		return;
	}

	if (!_mouseSelection) {
		return;
	}

	int w = width(), mouseY = localPos.y();
	clearIrrelevantState();
	if (_state == State::Default) {
		auto importantSwitchSelected = (_dialogsImportant && mouseY >= 0 && mouseY < dialogsOffset());
		mouseY -= dialogsOffset();
		auto selected = importantSwitchSelected ? nullptr : shownDialogs()->rowAtY(mouseY, st::dialogsRowHeight);
		if (_selected != selected || _importantSwitchSelected != importantSwitchSelected) {
			updateSelectedRow();
			_selected = selected;
			_importantSwitchSelected = importantSwitchSelected;
			updateSelectedRow();
			setCursor((_selected || _importantSwitchSelected) ? style::cur_pointer : style::cur_default);
		}
	} else if (_state == State::Filtered) {
		auto wasSelected = isSelected();
		if (_hashtagResults.empty()) {
			_hashtagSelected = -1;
			_hashtagDeleteSelected = false;
		} else {
			auto skip = 0;
			auto hashtagSelected = (mouseY >= skip) ? ((mouseY - skip) / st::mentionHeight) : -1;
			if (hashtagSelected < 0 || hashtagSelected >= _hashtagResults.size()) {
				hashtagSelected = -1;
			}
			if (_hashtagSelected != hashtagSelected) {
				updateSelectedRow();
				_hashtagSelected = hashtagSelected;
				updateSelectedRow();
			}
			_hashtagDeleteSelected = (_hashtagSelected >= 0) && (localPos.x() >= w - st::mentionHeight);
		}
		if (!_filterResults.isEmpty()) {
			auto skip = filteredOffset();
			auto filteredSelected = (mouseY >= skip) ? ((mouseY - skip) / st::dialogsRowHeight) : -1;
			if (filteredSelected < 0 || filteredSelected >= _filterResults.size()) {
				filteredSelected = -1;
			}
			if (_filteredSelected != filteredSelected) {
				updateSelectedRow();
				_filteredSelected = filteredSelected;
				updateSelectedRow();
			}
		}
		if (!_peerSearchResults.empty()) {
			auto skip = peerSearchOffset();
			auto peerSearchSelected = (mouseY >= skip) ? ((mouseY - skip) / st::dialogsRowHeight) : -1;
			if (peerSearchSelected < 0 || peerSearchSelected >= _peerSearchResults.size()) {
				peerSearchSelected = -1;
			}
			if (_peerSearchSelected != peerSearchSelected) {
				updateSelectedRow();
				_peerSearchSelected = peerSearchSelected;
				updateSelectedRow();
			}
		}
		if (!_waitingForSearch && !_searchResults.empty()) {
			auto skip = searchedOffset();
			auto searchedSelected = (mouseY >= skip) ? ((mouseY - skip) / st::dialogsRowHeight) : -1;
			if (searchedSelected < 0 || searchedSelected >= _searchResults.size()) {
				searchedSelected = -1;
			}
			if (_searchedSelected != searchedSelected) {
				updateSelectedRow();
				_searchedSelected = searchedSelected;
				updateSelectedRow();
			}
		}
		if (wasSelected != isSelected()) {
			setCursor(wasSelected ? style::cur_default : style::cur_pointer);
		}
	}
}

void DialogsInner::mousePressEvent(QMouseEvent *e) {
	_mouseSelection = true;
	updateSelected(e->pos());

	_pressButton = e->button();
	setPressed(_selected);
	setImportantSwitchPressed(_importantSwitchSelected);
	setHashtagPressed(_hashtagSelected);
	_hashtagDeletePressed = _hashtagDeleteSelected;
	setFilteredPressed(_filteredSelected);
	setPeerSearchPressed(_peerSearchSelected);
	setSearchedPressed(_searchedSelected);
	if (_importantSwitchPressed) {
		_importantSwitch->row.addRipple(e->pos(), QSize(getFullWidth(), st::dialogsImportantBarHeight), [this] {
			update(0, 0, getFullWidth(), st::dialogsImportantBarHeight);
		});
	} else if (_pressed) {
		auto row = _pressed;
		row->addRipple(e->pos() - QPoint(0, dialogsOffset() + _pressed->pos() * st::dialogsRowHeight), QSize(getFullWidth(), st::dialogsRowHeight), [this, row] {
			if (!_a_pinnedShifting.animating()) {
				row->entry()->updateChatListEntry();
			}
		});
		_dragStart = e->pos();
	} else if (base::in_range(_hashtagPressed, 0, _hashtagResults.size()) && !_hashtagDeletePressed) {
		auto row = &_hashtagResults[_hashtagPressed]->row;
		row->addRipple(e->pos(), QSize(getFullWidth(), st::mentionHeight), [this, index = _hashtagPressed] {
			update(0, index * st::mentionHeight, getFullWidth(), st::mentionHeight);
		});
	} else if (base::in_range(_filteredPressed, 0, _filterResults.size())) {
		const auto row = _filterResults[_filteredPressed];
		const auto list = Global::DialogsMode();
		row->addRipple(
			e->pos() - QPoint(0, filteredOffset() + _filteredPressed * st::dialogsRowHeight),
			QSize(getFullWidth(), st::dialogsRowHeight),
			[=] { repaintDialogRow(list, row); });
	} else if (base::in_range(_peerSearchPressed, 0, _peerSearchResults.size())) {
		auto &result = _peerSearchResults[_peerSearchPressed];
		auto row = &result->row;
		row->addRipple(
			e->pos() - QPoint(0, peerSearchOffset() + _peerSearchPressed * st::dialogsRowHeight),
			QSize(getFullWidth(), st::dialogsRowHeight),
			[this, peer = result->peer] { updateSearchResult(peer); });
	} else if (base::in_range(_searchedPressed, 0, _searchResults.size())) {
		auto &row = _searchResults[_searchedPressed];
		row->addRipple(e->pos() - QPoint(0, searchedOffset() + _searchedPressed * st::dialogsRowHeight), QSize(getFullWidth(), st::dialogsRowHeight), [this, index = _searchedPressed] {
			rtlupdate(0, searchedOffset() + index * st::dialogsRowHeight, getFullWidth(), st::dialogsRowHeight);
		});
	}
	if (anim::Disabled()
		&& (!_pressed || !_pressed->entry()->isPinnedDialog())) {
		mousePressReleased(e->button());
	}
}

void DialogsInner::checkReorderPinnedStart(QPoint localPosition) {
	if (_pressed != nullptr && !_dragging && _state == State::Default) {
		if (qAbs(localPosition.y() - _dragStart.y()) >= ConvertScale(kStartReorderThreshold)) {
			_dragging = _pressed;
			if (updateReorderIndexGetCount() < 2) {
				_dragging = nullptr;
			} else {
				_pinnedOrder = Auth().data().pinnedDialogsOrder();
				_pinnedRows[_draggingIndex].yadd = anim::value(0, localPosition.y() - _dragStart.y());
				_pinnedRows[_draggingIndex].animStartTime = getms();
				_a_pinnedShifting.start();
			}
		}
	}
}

int DialogsInner::shownPinnedCount() const {
	auto result = 0;
	for_const (auto row, *shownDialogs()) {
		if (row->entry()->useProxyPromotion()) {
			continue;
		} else if (!row->entry()->isPinnedDialog()) {
			break;
		}
		++result;
	}
	return result;
}

int DialogsInner::countPinnedIndex(Dialogs::Row *ofRow) {
	if (!ofRow || !ofRow->entry()->isPinnedDialog()) {
		return -1;
	}
	auto result = 0;
	for_const (auto row, *shownDialogs()) {
		if (row->entry()->useProxyPromotion()) {
			continue;
		} else if (!row->entry()->isPinnedDialog()) {
			break;
		} else if (row == ofRow) {
			return result;
		}
		++result;
	}
	return -1;
}

void DialogsInner::savePinnedOrder() {
	const auto &newOrder = Auth().data().pinnedDialogsOrder();
	if (newOrder.size() != _pinnedOrder.size()) {
		return; // Something has changed in the set of pinned chats.
	}
	for (const auto &pinned : newOrder) {
		if (!base::contains(_pinnedOrder, pinned)) {
			return; // Something has changed in the set of pinned chats.
		}
	}
	Auth().api().savePinnedOrder();
}

void DialogsInner::finishReorderPinned() {
	auto wasDragging = (_dragging != nullptr);
	if (wasDragging) {
		savePinnedOrder();
		_dragging = nullptr;
	}

	_draggingIndex = -1;
	if (!_a_pinnedShifting.animating()) {
		_pinnedRows.clear();
		_aboveIndex = -1;
	}
	if (wasDragging) {
		emit draggingScrollDelta(0);
	}
}

void DialogsInner::stopReorderPinned() {
	_a_pinnedShifting.stop();
	finishReorderPinned();
}

int DialogsInner::updateReorderIndexGetCount() {
	auto index = countPinnedIndex(_dragging);
	if (index < 0) {
		finishReorderPinned();
		return 0;
	}

	auto count = shownPinnedCount();
	Assert(index < count);
	if (count < 2) {
		stopReorderPinned();
		return 0;
	}

	_draggingIndex = index;
	_aboveIndex = _draggingIndex;
	while (count > _pinnedRows.size()) {
		_pinnedRows.push_back(PinnedRow());
	}
	while (count < _pinnedRows.size()) {
		_pinnedRows.pop_back();
	}
	return count;
}

bool DialogsInner::updateReorderPinned(QPoint localPosition) {
	checkReorderPinnedStart(localPosition);
	auto pinnedCount = updateReorderIndexGetCount();
	if (pinnedCount < 2) {
		return false;
	}

	auto yaddWas = _pinnedRows[_draggingIndex].yadd.current();
	auto shift = 0;
	auto ms = getms();
	auto rowHeight = st::dialogsRowHeight;
	if (_dragStart.y() > localPosition.y() && _draggingIndex > 0) {
		shift = -floorclamp(_dragStart.y() - localPosition.y() + (rowHeight / 2), rowHeight, 0, _draggingIndex);

		for (auto from = _draggingIndex, to = _draggingIndex + shift; from > to; --from) {
			shownDialogs()->movePinned(_dragging, -1);
			std::swap(_pinnedRows[from], _pinnedRows[from - 1]);
			_pinnedRows[from].yadd = anim::value(_pinnedRows[from].yadd.current() - rowHeight, 0);
			_pinnedRows[from].animStartTime = ms;
		}
	} else if (_dragStart.y() < localPosition.y() && _draggingIndex + 1 < pinnedCount) {
		shift = floorclamp(localPosition.y() - _dragStart.y() + (rowHeight / 2), rowHeight, 0, pinnedCount - _draggingIndex - 1);

		for (auto from = _draggingIndex, to = _draggingIndex + shift; from < to; ++from) {
			shownDialogs()->movePinned(_dragging, 1);
			std::swap(_pinnedRows[from], _pinnedRows[from + 1]);
			_pinnedRows[from].yadd = anim::value(_pinnedRows[from].yadd.current() + rowHeight, 0);
			_pinnedRows[from].animStartTime = ms;
		}
	}
	if (shift) {
		_draggingIndex += shift;
		_aboveIndex = _draggingIndex;
		_dragStart.setY(_dragStart.y() + shift * rowHeight);
		if (!_a_pinnedShifting.animating()) {
			_a_pinnedShifting.start();
		}
	}
	_aboveTopShift = qCeil(_pinnedRows[_aboveIndex].yadd.current());
	_pinnedRows[_draggingIndex].yadd = anim::value(yaddWas - shift * rowHeight, localPosition.y() - _dragStart.y());
	if (!_pinnedRows[_draggingIndex].animStartTime) {
		_pinnedRows[_draggingIndex].yadd.finish();
	}
	_a_pinnedShifting.step(ms, true);

	auto countDraggingScrollDelta = [this, localPosition] {
		if (localPosition.y() < _visibleTop) {
			return localPosition.y() - _visibleTop;
		}
		return 0;
	};

	emit draggingScrollDelta(countDraggingScrollDelta());
	return true;
}

void DialogsInner::step_pinnedShifting(TimeMs ms, bool timer) {
	if (anim::Disabled()) {
		ms += st::stickersRowDuration;
	}

	auto wasAnimating = false;
	auto animating = false;
	auto updateMin = -1;
	auto updateMax = 0;
	for (auto i = 0, l = static_cast<int>(_pinnedRows.size()); i != l; ++i) {
		auto start = _pinnedRows[i].animStartTime;
		if (start) {
			wasAnimating = true;
			if (updateMin < 0) updateMin = i;
			updateMax = i;
			if (start + st::stickersRowDuration > ms && ms >= start) {
				_pinnedRows[i].yadd.update(float64(ms - start) / st::stickersRowDuration, anim::sineInOut);
				animating = true;
			} else {
				_pinnedRows[i].yadd.finish();
				_pinnedRows[i].animStartTime = 0;
			}
		}
	}
	if (timer || (wasAnimating && !animating)) {
		updateReorderIndexGetCount();
		if (_draggingIndex >= 0) {
			if (updateMin < 0 || updateMin > _draggingIndex) {
				updateMin = _draggingIndex;
			}
			if (updateMax < _draggingIndex) updateMax = _draggingIndex;
		}
		if (updateMin >= 0) {
			auto top = pinnedOffset();
			auto updateFrom = top + st::dialogsRowHeight * (updateMin - 1);
			auto updateHeight = st::dialogsRowHeight * (updateMax - updateMin + 3);
			if (base::in_range(_aboveIndex, 0, _pinnedRows.size())) {
				// Always include currently dragged chat in its current and old positions.
				auto aboveRowBottom = top + (_aboveIndex + 1) * st::dialogsRowHeight;
				auto aboveTopShift = qCeil(_pinnedRows[_aboveIndex].yadd.current());
				accumulate_max(updateHeight, (aboveRowBottom - updateFrom) + _aboveTopShift);
				accumulate_max(updateHeight, (aboveRowBottom - updateFrom) + aboveTopShift);
				_aboveTopShift = aboveTopShift;
			}
			update(0, updateFrom, getFullWidth(), updateHeight);
		}
	}
	if (!animating) {
		_aboveIndex = _draggingIndex;
		_a_pinnedShifting.stop();
	}
}

void DialogsInner::mouseReleaseEvent(QMouseEvent *e) {
	mousePressReleased(e->button());
}

void DialogsInner::mousePressReleased(Qt::MouseButton button) {
	auto wasDragging = (_dragging != nullptr);
	if (wasDragging) {
		updateReorderIndexGetCount();
		if (_draggingIndex >= 0) {
			auto localPosition = mapFromGlobal(QCursor::pos());
			_pinnedRows[_draggingIndex].yadd.start(0.);
			_pinnedRows[_draggingIndex].animStartTime = getms();
			if (!_a_pinnedShifting.animating()) {
				_a_pinnedShifting.start();
			}
		}
		finishReorderPinned();
	}

	auto importantSwitchPressed = _importantSwitchPressed;
	setImportantSwitchPressed(false);
	auto pressed = _pressed;
	setPressed(nullptr);
	auto hashtagPressed = _hashtagPressed;
	setHashtagPressed(-1);
	auto hashtagDeletePressed = _hashtagDeletePressed;
	_hashtagDeletePressed = false;
	auto filteredPressed = _filteredPressed;
	setFilteredPressed(-1);
	auto peerSearchPressed = _peerSearchPressed;
	setPeerSearchPressed(-1);
	auto searchedPressed = _searchedPressed;
	setSearchedPressed(-1);
	if (wasDragging) {
		updateSelected();
	}
	updateSelectedRow();
	if (!wasDragging && button == Qt::LeftButton) {
		if (importantSwitchPressed && importantSwitchPressed == _importantSwitchSelected) {
			chooseRow();
		} else if (pressed && pressed == _selected) {
			chooseRow();
		} else if (hashtagPressed >= 0 && hashtagPressed == _hashtagSelected && hashtagDeletePressed == _hashtagDeleteSelected) {
			chooseRow();
		} else if (filteredPressed >= 0 && filteredPressed == _filteredSelected) {
			chooseRow();
		} else if (peerSearchPressed >= 0 && peerSearchPressed == _peerSearchSelected) {
			chooseRow();
		} else if (searchedPressed >= 0 && searchedPressed == _searchedSelected) {
			chooseRow();
		}
	}
}

void DialogsInner::setImportantSwitchPressed(bool pressed) {
	if (_importantSwitchPressed != pressed) {
		if (_importantSwitchPressed) {
			_importantSwitch->row.stopLastRipple();
		}
		_importantSwitchPressed = pressed;
	}
}

void DialogsInner::setPressed(Dialogs::Row *pressed) {
	if (_pressed != pressed) {
		if (_pressed) {
			_pressed->stopLastRipple();
		}
		_pressed = pressed;
	}
}

void DialogsInner::setHashtagPressed(int pressed) {
	if (base::in_range(_hashtagPressed, 0, _hashtagResults.size())) {
		_hashtagResults[_hashtagPressed]->row.stopLastRipple();
	}
	_hashtagPressed = pressed;
}

void DialogsInner::setFilteredPressed(int pressed) {
	if (base::in_range(_filteredPressed, 0, _filterResults.size())) {
		_filterResults[_filteredPressed]->stopLastRipple();
	}
	_filteredPressed = pressed;
}

void DialogsInner::setPeerSearchPressed(int pressed) {
	if (base::in_range(_peerSearchPressed, 0, _peerSearchResults.size())) {
		_peerSearchResults[_peerSearchPressed]->row.stopLastRipple();
	}
	_peerSearchPressed = pressed;
}

void DialogsInner::setSearchedPressed(int pressed) {
	if (base::in_range(_searchedPressed, 0, _searchResults.size())) {
		_searchResults[_searchedPressed]->stopLastRipple();
	}
	_searchedPressed = pressed;
}

void DialogsInner::resizeEvent(QResizeEvent *e) {
	_addContactLnk->move((width() - _addContactLnk->width()) / 2, (st::noContactsHeight + st::noContactsFont->height) / 2);
	auto widthForCancelButton = qMax(width() + otherWidth(), st::columnMinimalWidthLeft);
	_cancelSearchInChat->moveToLeft(widthForCancelButton - st::dialogsSearchInSkip - _cancelSearchInChat->width(), st::searchedBarHeight + (st::dialogsSearchInHeight - st::dialogsCancelSearchInPeer.height) / 2);
	_cancelSearchFromUser->moveToLeft(widthForCancelButton - st::dialogsSearchInSkip - _cancelSearchFromUser->width(), st::searchedBarHeight + st::dialogsSearchInHeight + st::lineWidth + (st::dialogsSearchInHeight - st::dialogsCancelSearchInPeer.height) / 2);
}

void DialogsInner::onDialogRowReplaced(
		Dialogs::Row *oldRow,
		Dialogs::Row *newRow) {
	if (_state == State::Filtered) {
		for (auto i = _filterResults.begin(); i != _filterResults.end();) {
			if (*i == oldRow) { // this row is shown in filtered and maybe is in contacts!
				if (newRow) {
					*i = newRow;
					++i;
				} else {
					i = _filterResults.erase(i);
				}
			} else {
				++i;
			}
		}
	}
	if (_selected == oldRow) {
		_selected = newRow;
	}
	if (_pressed == oldRow) {
		setPressed(newRow);
	}
	if (_dragging == oldRow) {
		if (newRow) {
			_dragging = newRow;
		} else {
			stopReorderPinned();
		}
	}
}

void DialogsInner::createDialog(Dialogs::Key key) {
	if (const auto history = key.history()) {
		if (history->peer->loadedStatus
			!= PeerData::LoadedStatus::FullLoaded) {
			LOG(("API Error: "
				"DialogsInner::createDialog() called for a non loaded peer!"
				));
			return;
		}
	}

	const auto entry = key.entry();
	auto creating = !entry->inChatList(Dialogs::Mode::All);
	if (creating) {
		const auto mainRow = entry->addToChatList(
			Dialogs::Mode::All,
			_dialogs.get());
		_contactsNoDialogs->del(key, mainRow);
	}
	if (_dialogsImportant
		&& !entry->inChatList(Dialogs::Mode::Important)
		&& entry->toImportant()) {
		if (Global::DialogsMode() == Dialogs::Mode::Important) {
			creating = true;
		}
		entry->addToChatList(
			Dialogs::Mode::Important,
			_dialogsImportant.get());
	}

	auto changed = entry->adjustByPosInChatList(
		Dialogs::Mode::All,
		_dialogs.get());

	if (_dialogsImportant) {
		if (!entry->toImportant()) {
			if (Global::DialogsMode() == Dialogs::Mode::Important) {
				return;
			}
		} else {
			const auto importantChanged = entry->adjustByPosInChatList(
				Dialogs::Mode::Important,
				_dialogsImportant.get());
			if (Global::DialogsMode() == Dialogs::Mode::Important) {
				changed = importantChanged;
			}
		}
	}

	const auto from = dialogsOffset() + changed.movedFrom * st::dialogsRowHeight;
	const auto to = dialogsOffset() + changed.movedTo * st::dialogsRowHeight;
	if (!_dragging) {
		// Don't jump in chats list scroll position while dragging.
		emit dialogMoved(from, to);
	}

	if (creating) {
		refresh();
	} else if (_state == State::Default && changed.movedFrom != changed.movedTo) {
		update(0, qMin(from, to), getFullWidth(), qAbs(from - to) + st::dialogsRowHeight);
	}
}

void DialogsInner::removeDialog(Dialogs::Key key) {
	if (key == _menuKey && _menu) {
		InvokeQueued(this, [this] { _menu = nullptr; });
	}
	if (_selected && _selected->key() == key) {
		_selected = nullptr;
	}
	if (_pressed && _pressed->key() == key) {
		setPressed(nullptr);
	}
	const auto entry = key.entry();
	entry->removeFromChatList(
		Dialogs::Mode::All,
		_dialogs.get());
	if (_dialogsImportant) {
		entry->removeFromChatList(
			Dialogs::Mode::Important,
			_dialogsImportant.get());
	}
	if (const auto history = key.history()) {
		Auth().notifications().clearFromHistory(history);
		Local::removeSavedPeer(history->peer);
	}
	if (_contacts->contains(key)) {
		if (!_contactsNoDialogs->contains(key)) {
			_contactsNoDialogs->addByName(key);
		}
	}

	emit App::main()->dialogsUpdated();

	refresh();
}

void DialogsInner::repaintDialogRow(
		Dialogs::Mode list,
		not_null<Dialogs::Row*> row) {
	if (_state == State::Default) {
		if (Global::DialogsMode() == list) {
			auto position = row->pos();
			auto top = dialogsOffset();
			if (base::in_range(position, 0, _pinnedRows.size())) {
				top += qRound(_pinnedRows[position].yadd.current());
			}
			update(0, top + position * st::dialogsRowHeight, getFullWidth(), st::dialogsRowHeight);
		}
	} else if (_state == State::Filtered) {
		if (list == Dialogs::Mode::All) {
			for (auto i = 0, l = _filterResults.size(); i != l; ++i) {
				if (_filterResults[i]->key() == row->key()) {
					update(
						0,
						filteredOffset() + i * st::dialogsRowHeight,
						getFullWidth(),
						st::dialogsRowHeight);
					break;
				}
			}
		}
	}
}

void DialogsInner::repaintDialogRow(
		not_null<History*> history,
		MsgId messageId) {
	updateDialogRow(
		Dialogs::RowDescriptor(
			history,
			FullMsgId(history->channelId(), messageId)),
		QRect(0, 0, getFullWidth(), st::dialogsRowHeight));
}

void DialogsInner::updateSearchResult(not_null<PeerData*> peer) {
	if (_state == State::Filtered) {
		if (!_peerSearchResults.empty()) {
			auto index = 0, add = peerSearchOffset();
			for_const (auto &result, _peerSearchResults) {
				if (result->peer == peer) {
					rtlupdate(0, add + index * st::dialogsRowHeight, getFullWidth(), st::dialogsRowHeight);
					break;
				}
				++index;
			}
		}
	}
}

void DialogsInner::updateDialogRow(
		Dialogs::RowDescriptor row,
		QRect updateRect,
		UpdateRowSections sections) {
	if (IsServerMsgId(-row.fullId.msg)) {
		if (const auto peer = row.key.peer()) {
			if (const auto from = peer->migrateFrom()) {
				if (const auto migrated = App::historyLoaded(from)) {
					row = Dialogs::RowDescriptor(
						migrated,
						FullMsgId(0, -row.fullId.msg));
				}
			}
		}
	}

	const auto updateRow = [&](int rowTop) {
		rtlupdate(
			updateRect.x(),
			rowTop + updateRect.y(),
			updateRect.width(),
			updateRect.height());
	};
	if (_state == State::Default) {
		if (sections & UpdateRowSection::Default) {
			if (const auto dialog = shownDialogs()->getRow(row.key)) {
				const auto position = dialog->pos();
				auto top = dialogsOffset();
				if (base::in_range(position, 0, _pinnedRows.size())) {
					top += qRound(_pinnedRows[position].yadd.current());
				}
				updateRow(top + position * st::dialogsRowHeight);
			}
		}
	} else if (_state == State::Filtered) {
		if ((sections & UpdateRowSection::Filtered)
			&& !_filterResults.isEmpty()) {
			const auto add = filteredOffset();
			auto index = 0;
			for (const auto result : _filterResults) {
				if (result->key() == row.key) {
					updateRow(add + index * st::dialogsRowHeight);
					break;
				}
				++index;
			}
		}
		if ((sections & UpdateRowSection::PeerSearch)
			&& !_peerSearchResults.empty()) {
			if (const auto peer = row.key.peer()) {
				const auto add = peerSearchOffset();
				auto index = 0;
				for (const auto &result : _peerSearchResults) {
					if (result->peer == peer) {
						updateRow(add + index * st::dialogsRowHeight);
						break;
					}
					++index;
				}
			}
		}
		if ((sections & UpdateRowSection::MessageSearch)
			&& !_searchResults.empty()) {
			const auto add = searchedOffset();
			auto index = 0;
			for (const auto &result : _searchResults) {
				auto item = result->item();
				if (item->fullId() == row.fullId) {
					updateRow(add + index * st::dialogsRowHeight);
					break;
				}
				++index;
			}
		}
	}
}

void DialogsInner::enterEventHook(QEvent *e) {
	setMouseTracking(true);
	updateSelected();
}

void DialogsInner::updateSelectedRow(Dialogs::Key key) {
	if (_state == State::Default) {
		if (key) {
			const auto entry = key.entry();
			if (!entry->inChatList(Global::DialogsMode())) {
				return;
			}
			auto position = entry->posInChatList(Global::DialogsMode());
			auto top = dialogsOffset();
			if (base::in_range(position, 0, _pinnedRows.size())) {
				top += qRound(_pinnedRows[position].yadd.current());
			}
			update(0, top + position * st::dialogsRowHeight, getFullWidth(), st::dialogsRowHeight);
		} else if (_selected) {
			update(0, dialogsOffset() + _selected->pos() * st::dialogsRowHeight, getFullWidth(), st::dialogsRowHeight);
		} else if (_importantSwitchSelected) {
			update(0, 0, getFullWidth(), st::dialogsImportantBarHeight);
		}
	} else if (_state == State::Filtered) {
		if (key) {
			for (auto i = 0, l = _filterResults.size(); i != l; ++i) {
				if (_filterResults[i]->key() == key) {
					update(0, filteredOffset() + i * st::dialogsRowHeight, getFullWidth(), st::dialogsRowHeight);
					break;
				}
			}
		} else if (_hashtagSelected >= 0) {
			update(0, _hashtagSelected * st::mentionHeight, getFullWidth(), st::mentionHeight);
		} else if (_filteredSelected >= 0) {
			update(0, filteredOffset() + _filteredSelected * st::dialogsRowHeight, getFullWidth(), st::dialogsRowHeight);
		} else if (_peerSearchSelected >= 0) {
			update(0, peerSearchOffset() + _peerSearchSelected * st::dialogsRowHeight, getFullWidth(), st::dialogsRowHeight);
		} else if (_searchedSelected >= 0) {
			update(0, searchedOffset() + _searchedSelected * st::dialogsRowHeight, getFullWidth(), st::dialogsRowHeight);
		}
	}
}

Dialogs::IndexedList *DialogsInner::shownDialogs() const {
	return (Global::DialogsMode() == Dialogs::Mode::Important)
		? _dialogsImportant.get()
		: _dialogs.get();
}

void DialogsInner::leaveEventHook(QEvent *e) {
	setMouseTracking(false);
	clearSelection();
}

void DialogsInner::dragLeft() {
	setMouseTracking(false);
	clearSelection();
}

void DialogsInner::clearSelection() {
	_mouseSelection = false;
	if (_importantSwitchSelected
		|| _selected
		|| _filteredSelected >= 0
		|| _hashtagSelected >= 0
		|| _peerSearchSelected >= 0
		|| _searchedSelected >= 0) {
		updateSelectedRow();
		_importantSwitchSelected = false;
		_selected = nullptr;
		_filteredSelected = _searchedSelected = _peerSearchSelected = _hashtagSelected = -1;
		setCursor(style::cur_default);
	}
}

void DialogsInner::contextMenuEvent(QContextMenuEvent *e) {
	_menu = nullptr;

	if (e->reason() == QContextMenuEvent::Mouse) {
		_mouseSelection = true;
		updateSelected();
	}

	const auto key = [&]() -> Dialogs::Key {
		if (_state == State::Default) {
			if (_selected) {
				return _selected->key();
			}
		} else if (_state == State::Filtered) {
			if (base::in_range(_filteredSelected, 0, _filterResults.size())) {
				return _filterResults[_filteredSelected]->key();
			}
		}
		return Dialogs::Key();
	}();
	if (!key) return;

	_menuKey = key;
	if (_pressButton != Qt::LeftButton) {
		mousePressReleased(_pressButton);
	}

	_menu = base::make_unique_q<Ui::PopupMenu>(this);
	if (const auto history = key.history()) {
		Window::FillPeerMenu(
			_controller,
			history->peer,
			[&](const QString &text, Fn<void()> callback) {
				return _menu->addAction(text, std::move(callback));
			},
			Window::PeerMenuSource::ChatsList);
	} else if (const auto feed = key.feed()) {
		Window::FillFeedMenu(
			_controller,
			feed,
			[&](const QString &text, Fn<void()> callback) {
				return _menu->addAction(text, std::move(callback));
			},
			Window::PeerMenuSource::ChatsList);
	}
	connect(_menu.get(), &QObject::destroyed, [this] {
		if (_menuKey) {
			updateSelectedRow(base::take(_menuKey));
		}
		auto localPos = mapFromGlobal(QCursor::pos());
		if (rect().contains(localPos)) {
			_mouseSelection = true;
			setMouseTracking(true);
			updateSelected(localPos);
		}
	});
	_menu->popup(e->globalPos());
	e->accept();
}

void DialogsInner::onParentGeometryChanged() {
	auto localPos = mapFromGlobal(QCursor::pos());
	if (rect().contains(localPos)) {
		setMouseTracking(true);
		updateSelected(localPos);
	}
}

void DialogsInner::handlePeerNameChange(
		not_null<PeerData*> peer,
		const base::flat_set<QChar> &oldLetters) {
	_dialogs->peerNameChanged(Dialogs::Mode::All, peer, oldLetters);
	if (_dialogsImportant) {
		_dialogsImportant->peerNameChanged(
			Dialogs::Mode::Important,
			peer,
			oldLetters);
	}
	_contactsNoDialogs->peerNameChanged(peer, oldLetters);
	_contacts->peerNameChanged(peer, oldLetters);
	update();
}

void DialogsInner::onFilterUpdate(QString newFilter, bool force) {
	const auto mentionsSearch = (newFilter == qstr("@"));
	const auto words = mentionsSearch
		? QStringList(newFilter)
		: TextUtilities::PrepareSearchWords(newFilter);
	newFilter = words.isEmpty() ? QString() : words.join(' ');
	if (newFilter != _filter || force) {
		_filter = newFilter;
		if (_filter.isEmpty() && !_searchFromUser) {
			clearFilter();
		} else {
			QStringList::const_iterator fb = words.cbegin(), fe = words.cend(), fi;

			_state = State::Filtered;
			_waitingForSearch = true;
			_filterResults.clear();
			_filterResultsGlobal.clear();
			if (!_searchInChat && !words.isEmpty()) {
				const Dialogs::List *toFilter = nullptr;
				if (!_dialogs->isEmpty()) {
					for (fi = fb; fi != fe; ++fi) {
						auto found = _dialogs->filtered(fi->at(0));
						if (found->isEmpty()) {
							toFilter = nullptr;
							break;
						}
						if (!toFilter || toFilter->size() > found->size()) {
							toFilter = found;
						}
					}
				}
				const Dialogs::List *toFilterContacts = nullptr;
				if (!_contactsNoDialogs->isEmpty()) {
					for (fi = fb; fi != fe; ++fi) {
						auto found = _contactsNoDialogs->filtered(fi->at(0));
						if (found->isEmpty()) {
							toFilterContacts = nullptr;
							break;
						}
						if (!toFilterContacts || toFilterContacts->size() > found->size()) {
							toFilterContacts = found;
						}
					}
				}
				_filterResults.reserve((toFilter ? toFilter->size() : 0)
					+ (toFilterContacts ? toFilterContacts->size() : 0));
				if (toFilter) {
					for (const auto row : *toFilter) {
						const auto &nameWords = row->entry()->chatsListNameWords();
						auto nb = nameWords.cbegin(), ne = nameWords.cend(), ni = nb;
						for (fi = fb; fi != fe; ++fi) {
							auto filterWord = *fi;
							for (ni = nb; ni != ne; ++ni) {
								if (ni->startsWith(filterWord)) {
									break;
								}
							}
							if (ni == ne) {
								break;
							}
						}
						if (fi == fe) {
							_filterResults.push_back(row);
						}
					}
				}
				if (toFilterContacts) {
					for (const auto row : *toFilterContacts) {
						const auto &nameWords = row->entry()->chatsListNameWords();
						auto nb = nameWords.cbegin(), ne = nameWords.cend(), ni = nb;
						for (fi = fb; fi != fe; ++fi) {
							auto filterWord = *fi;
							for (ni = nb; ni != ne; ++ni) {
								if (ni->startsWith(filterWord)) {
									break;
								}
							}
							if (ni == ne) {
								break;
							}
						}
						if (fi == fe) {
							_filterResults.push_back(row);
						}
					}
				}
			}
			refresh(true);
		}
		setMouseSelection(false, true);
	}
	if (_state != State::Default) {
		emit searchMessages();
	}
}

void DialogsInner::onHashtagFilterUpdate(QStringRef newFilter) {
	if (newFilter.isEmpty() || newFilter.at(0) != '#' || _searchInChat) {
		_hashtagFilter = QString();
		if (!_hashtagResults.empty()) {
			_hashtagResults.clear();
			refresh(true);
			setMouseSelection(false, true);
		}
		return;
	}
	_hashtagFilter = newFilter.toString();
	if (cRecentSearchHashtags().isEmpty() && cRecentWriteHashtags().isEmpty()) {
		Local::readRecentHashtagsAndBots();
	}
	auto &recent = cRecentSearchHashtags();
	_hashtagResults.clear();
	if (!recent.isEmpty()) {
		_hashtagResults.reserve(qMin(recent.size(), kHashtagResultsLimit));
		for (auto i = recent.cbegin(), e = recent.cend(); i != e; ++i) {
			if (i->first.startsWith(_hashtagFilter.midRef(1), Qt::CaseInsensitive) && i->first.size() + 1 != newFilter.size()) {
				_hashtagResults.push_back(std::make_unique<HashtagResult>(i->first));
				if (_hashtagResults.size() == kHashtagResultsLimit) break;
			}
		}
	}
	refresh(true);
	setMouseSelection(false, true);
}

DialogsInner::~DialogsInner() {
	clearSearchResults();
}

void DialogsInner::clearSearchResults(bool clearPeerSearchResults) {
	if (clearPeerSearchResults) _peerSearchResults.clear();
	_searchResults.clear();
	_searchedCount = _searchedMigratedCount = 0;
	_lastSearchDate = 0;
	_lastSearchPeer = 0;
	_lastSearchId = _lastSearchMigratedId = 0;
}

PeerData *DialogsInner::updateFromParentDrag(QPoint globalPos) {
	_mouseSelection = true;
	updateSelected(mapFromGlobal(globalPos));
	const auto getPeerFromRow = [](Dialogs::Row *row) -> PeerData* {
		if (const auto history = row ? row->history() : nullptr) {
			return history->peer;
		}
		return nullptr;
	};
	if (_state == State::Default) {
		return getPeerFromRow(_selected);
	} else if (_state == State::Filtered) {
		if (base::in_range(_filteredSelected, 0, _filterResults.size())) {
			return getPeerFromRow(_filterResults[_filteredSelected]);
		} else if (base::in_range(_peerSearchSelected, 0, _peerSearchResults.size())) {
			return _peerSearchResults[_peerSearchSelected]->peer;
		} else if (base::in_range(_searchedSelected, 0, _searchResults.size())) {
			return _searchResults[_searchedSelected]->item()->history()->peer;
		}
	}
	return nullptr;
}

void DialogsInner::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
	loadPeerPhotos();
	if (_visibleTop + PreloadHeightsCount * (_visibleBottom - _visibleTop) >= height()) {
		if (_loadMoreCallback) {
			_loadMoreCallback();
		}
	}
}

void DialogsInner::itemRemoved(not_null<const HistoryItem*> item) {
	int wasCount = _searchResults.size();
	for (auto i = _searchResults.begin(); i != _searchResults.end();) {
		if ((*i)->item() == item) {
			i = _searchResults.erase(i);
			if (item->history() == _searchInMigrated) {
				if (_searchedMigratedCount > 0) --_searchedMigratedCount;
			} else {
				if (_searchedCount > 0) --_searchedCount;
			}
		} else {
			++i;
		}
	}
	if (wasCount != _searchResults.size()) {
		refresh();
	}
}

void DialogsInner::dialogsReceived(const QVector<MTPDialog> &added) {
	for (const auto &dialog : added) {
		switch (dialog.type()) {
		case mtpc_dialog: applyDialog(dialog.c_dialog()); break;
		//case mtpc_dialogFeed: applyFeedDialog(dialog.c_dialogFeed()); break; // #feed
		default: Unexpected("Type in DialogsInner::dialogsReceived");
		}
	}
	refresh();
}

void DialogsInner::applyDialog(const MTPDdialog &dialog) {
	const auto peerId = peerFromMTP(dialog.vpeer);
	if (!peerId) {
		return;
	}

	const auto history = App::history(peerId);
	history->applyDialog(dialog);

	if (!history->useProxyPromotion() && !history->isPinnedDialog()) {
		const auto date = history->chatsListTimeId();
		if (date != 0) {
			addSavedPeersAfter(ParseDateTime(date));
		}
	}
	_contactsNoDialogs->del(history);
	if (const auto from = history->peer->migrateFrom()) {
		if (const auto historyFrom = App::historyLoaded(from)) {
			removeDialog(historyFrom);
		}
	} else if (const auto to = history->peer->migrateTo()) {
		if (to->amIn()) {
			removeDialog(history);
		}
	}
}
// #feed
//void DialogsInner::applyFeedDialog(const MTPDdialogFeed &dialog) {
//	const auto feedId = dialog.vfeed_id.v;
//	const auto feed = Auth().data().feed(feedId);
//	feed->applyDialog(dialog);
//
//	if (!feed->useProxyPromotion() && !feed->isPinnedDialog()) {
//		const auto date = feed->chatsListDate();
//		if (!date.isNull()) {
//			addSavedPeersAfter(date);
//		}
//	}
//}

void DialogsInner::addSavedPeersAfter(const QDateTime &date) {
	auto &saved = cRefSavedPeersByTime();
	while (!saved.isEmpty() && (date.isNull() || date < saved.lastKey())) {
		const auto lastDate = saved.lastKey();
		const auto lastPeer = saved.last();
		saved.remove(lastDate, lastPeer);

		const auto history = App::history(lastPeer);
		history->setChatsListTimeId(ServerTimeFromParsed(lastDate));
		_contactsNoDialogs->del(history);
	}
}

void DialogsInner::addAllSavedPeers() {
	addSavedPeersAfter(QDateTime());
}

bool DialogsInner::uniqueSearchResults() const {
	return Auth().supportMode()
		&& _filter.startsWith('#')
		&& !_searchInChat;
}

bool DialogsInner::hasHistoryInSearchResults(not_null<History*> history) const {
	using Result = std::unique_ptr<Dialogs::FakeRow>;
	return ranges::find(
		_searchResults,
		history,
		[](const Result &result) { return result->item()->history(); }
	) != end(_searchResults);
}

bool DialogsInner::searchReceived(
		const QVector<MTPMessage> &messages,
		DialogsSearchRequestType type,
		int fullCount) {
	const auto uniquePeers = uniqueSearchResults();
	if (type == DialogsSearchFromStart || type == DialogsSearchPeerFromStart) {
		clearSearchResults(false);
	}
	auto isGlobalSearch = (type == DialogsSearchFromStart || type == DialogsSearchFromOffset);
	auto isMigratedSearch = (type == DialogsSearchMigratedFromStart || type == DialogsSearchMigratedFromOffset);

	auto unknownUnreadCounts = std::vector<not_null<History*>>();
	TimeId lastDateFound = 0;
	for_const (auto message, messages) {
		auto msgId = idFromMessage(message);
		auto peerId = peerFromMessage(message);
		auto lastDate = dateFromMessage(message);
		if (const auto peer = App::peerLoaded(peerId)) {
			if (lastDate) {
				const auto item = App::histories().addNewMessage(
					message,
					NewMessageExisting);
				const auto history = item->history();
				if (!uniquePeers || !hasHistoryInSearchResults(history)) {
					_searchResults.push_back(
						std::make_unique<Dialogs::FakeRow>(
							_searchInChat,
							item));
					if (uniquePeers && !history->unreadCountKnown()) {
						unknownUnreadCounts.push_back(history);
					}
				}
				lastDateFound = lastDate;
				if (isGlobalSearch) {
					_lastSearchDate = lastDateFound;
				}
			}
			if (isGlobalSearch) {
				_lastSearchPeer = peer;
			}
		} else {
			LOG(("API Error: a search results with not loaded peer %1").arg(peerId));
		}
		if (isMigratedSearch) {
			_lastSearchMigratedId = msgId;
		} else {
			_lastSearchId = msgId;
		}
	}
	if (isMigratedSearch) {
		_searchedMigratedCount = fullCount;
	} else {
		_searchedCount = fullCount;
	}
	if (_waitingForSearch
		&& (!_searchResults.empty()
			|| !_searchInMigrated
			|| type == DialogsSearchMigratedFromStart
			|| type == DialogsSearchMigratedFromOffset)) {
		_waitingForSearch = false;
	}

	refresh();

	if (!unknownUnreadCounts.empty()) {
		Auth().api().requestDialogEntries(std::move(unknownUnreadCounts));
	}
	return lastDateFound != 0;
}

void DialogsInner::peerSearchReceived(
		const QString &query,
		const QVector<MTPPeer> &my,
		const QVector<MTPPeer> &result) {
	if (_state != State::Filtered) {
		return;
	}

	const auto alreadyAdded = [&](not_null<PeerData*> peer) {
		for (const auto &row : _filterResults) {
			if (const auto history = row->history()) {
				if (history->peer == peer) {
					return true;
				}
			}
		}
		return false;
	};
	_peerSearchQuery = query.toLower().trimmed();
	_peerSearchResults.clear();
	_peerSearchResults.reserve(result.size());
	for (const auto &mtpPeer : my) {
		if (const auto peer = App::peerLoaded(peerFromMTP(mtpPeer))) {
			if (alreadyAdded(peer)) {
				continue;
			}
			const auto prev = nullptr, next = nullptr;
			const auto position = 0;
			auto row = std::make_unique<Dialogs::Row>(
				App::history(peer),
				prev,
				next,
				position);
			const auto [i, ok] = _filterResultsGlobal.emplace(
				peer,
				std::move(row));
			_filterResults.push_back(i->second.get());
		} else {
			LOG(("API Error: "
				"user %1 was not loaded in DialogsInner::peopleReceived()"
				).arg(peer->id));
		}
	}
	for (const auto &mtpPeer : result) {
		if (const auto peer = App::peerLoaded(peerFromMTP(mtpPeer))) {
			if (const auto history = App::historyLoaded(peer)) {
				if (history->inChatList(Dialogs::Mode::All)) {
					continue; // skip existing chats
				}
			}
			_peerSearchResults.push_back(std::make_unique<PeerSearchResult>(
				peer));
		} else {
			LOG(("API Error: "
				"user %1 was not loaded in DialogsInner::peopleReceived()"
				).arg(peer->id));
		}
	}
	refresh();
}

void DialogsInner::userIsContactUpdated(not_null<UserData*> user) {
	if (user->loadedStatus != PeerData::FullLoaded) {
		LOG(("API Error: "
			"notify_userIsContactChanged() called for a not loaded user!"));
		return;
	}
	if (user->contactStatus() == UserData::ContactStatus::Contact) {
		const auto history = App::history(user->id);
		_contacts->addByName(history);
		if (!shownDialogs()->getRow(history)
			&& !_dialogs->contains(history)) {
			_contactsNoDialogs->addByName(history);
		}
	} else if (const auto history = App::historyLoaded(user)) {
		if (_selected && _selected->history() == history) {
			_selected = nullptr;
		}
		if (_pressed && _pressed->history() == history) {
			setPressed(nullptr);
		}
		_contactsNoDialogs->del(history);
		_contacts->del(history);
	}
	refresh();
}

void DialogsInner::notify_historyMuteUpdated(History *history) {
	if (!_dialogsImportant || !history->inChatList(Dialogs::Mode::All)) return;

	if (!history->toImportant()) {
		if (Global::DialogsMode() == Dialogs::Mode::Important) {
			if (_selected && _selected->history() == history) {
				_selected = nullptr;
			}
			if (_pressed && _pressed->history() == history) {
				setPressed(nullptr);
			}
		}
		history->removeFromChatList(Dialogs::Mode::Important, _dialogsImportant.get());
		if (Global::DialogsMode() != Dialogs::Mode::Important) {
			return;
		}
		refresh();
	} else {
		bool creating = !history->inChatList(Dialogs::Mode::Important);
		if (creating) {
			history->addToChatList(Dialogs::Mode::Important, _dialogsImportant.get());
		}

		auto changed = history->adjustByPosInChatList(Dialogs::Mode::All, _dialogs.get());

		if (Global::DialogsMode() != Dialogs::Mode::Important) {
			return;
		}

		int from = dialogsOffset() + changed.movedFrom * st::dialogsRowHeight;
		int to = dialogsOffset() + changed.movedTo * st::dialogsRowHeight;
		if (!_dragging) {
			// Don't jump in chats list scroll position while dragging.
			emit dialogMoved(from, to);
		}

		if (creating) {
			refresh();
		} else if (_state == State::Default && changed.movedFrom != changed.movedTo) {
			update(0, qMin(from, to), getFullWidth(), qAbs(from - to) + st::dialogsRowHeight);
		}
	}
}

void DialogsInner::refresh(bool toTop) {
	int32 h = 0;
	if (_state == State::Default) {
		if (shownDialogs()->isEmpty()) {
			h = st::noContactsHeight;
			if (Auth().data().contactsLoaded().value()) {
				if (_addContactLnk->isHidden()) _addContactLnk->show();
			} else {
				if (!_addContactLnk->isHidden()) _addContactLnk->hide();
			}
		} else {
			h = dialogsOffset() + shownDialogs()->size() * st::dialogsRowHeight;
			if (!_addContactLnk->isHidden()) _addContactLnk->hide();
		}
	} else if (_state == State::Filtered) {
		if (!_addContactLnk->isHidden()) _addContactLnk->hide();
		if (_waitingForSearch) {
			h = searchedOffset() + (_searchResults.size() * st::dialogsRowHeight) + ((_searchResults.empty() && !_searchInChat) ? -st::searchedBarHeight : 0);
		} else {
			h = searchedOffset() + (_searchResults.size() * st::dialogsRowHeight);
		}
	}
	setHeight(h);
	if (toTop) {
		stopReorderPinned();
		emit mustScrollTo(0, 0);
		loadPeerPhotos();
	}
	_controller->dialogsListDisplayForced().set(
		_searchInChat || !_filter.isEmpty(),
		true);
	update();
}

void DialogsInner::setMouseSelection(bool mouseSelection, bool toTop) {
	_mouseSelection = mouseSelection;
	if (!_mouseSelection && toTop) {
		if (_state == State::Default) {
			_selected = nullptr;
			_importantSwitchSelected = false;
		} else if (_state == State::Filtered) {
			_filteredSelected
				= _peerSearchSelected
				= _searchedSelected
				= _hashtagSelected = -1;
		}
		setCursor(style::cur_default);
	}
}

DialogsInner::State DialogsInner::state() const {
	return _state;
}

bool DialogsInner::hasFilteredResults() const {
	return !_filterResults.isEmpty() && _hashtagResults.empty();
}

void DialogsInner::searchInChat(Dialogs::Key key, UserData *from) {
	_searchInMigrated = nullptr;
	if (const auto peer = key.peer()) {
		if (const auto migrateTo = peer->migrateTo()) {
			return searchInChat(App::history(migrateTo), from);
		} else if (const auto migrateFrom = peer->migrateFrom()) {
			_searchInMigrated = App::history(migrateFrom);
		}
	}
	_searchInChat = key;
	_searchFromUser = from;
	if (_searchInChat) {
		onHashtagFilterUpdate(QStringRef());
		_cancelSearchInChat->show();
		refreshSearchInChatLabel();
	} else {
		_cancelSearchInChat->hide();
	}
	if (_searchFromUser) {
		_cancelSearchFromUser->show();
	} else {
		_cancelSearchFromUser->hide();
	}
	_controller->dialogsListDisplayForced().set(
		_searchInChat || !_filter.isEmpty(),
		true);
}

void DialogsInner::refreshSearchInChatLabel() {
	const auto dialog = [&] {
		if (const auto peer = _searchInChat.peer()) {
			if (peer->isSelf()) {
				return lang(lng_saved_messages);
			}
			return peer->name;
		} else if (const auto feed = _searchInChat.feed()) {
			return feed->chatsListName();
		}
		return QString();
	}();
	if (!dialog.isEmpty()) {
		_searchInChatText.setText(
			st::msgNameStyle,
			dialog,
			Ui::DialogTextOptions());
	}
	const auto from = [&] {
		if (const auto from = _searchFromUser) {
			return App::peerName(from);
		}
		return QString();
	}();
	if (!from.isEmpty()) {
		const auto fromUserText = lng_dlg_search_from(
			lt_user,
			textcmdLink(1, from));
		_searchFromUserText.setText(
			st::dialogsSearchFromStyle,
			fromUserText,
			Ui::DialogTextOptions());
	}
}

void DialogsInner::clearFilter() {
	if (_state == State::Filtered || _searchInChat) {
		if (_searchInChat) {
			_state = State::Filtered;
			_waitingForSearch = true;
		} else {
			_state = State::Default;
		}
		_hashtagResults.clear();
		_filterResults.clear();
		_filterResultsGlobal.clear();
		_peerSearchResults.clear();
		_searchResults.clear();
		_lastSearchDate = 0;
		_lastSearchPeer = 0;
		_lastSearchId = _lastSearchMigratedId = 0;
		_filter = QString();
		refresh(true);
	}
}

void DialogsInner::selectSkip(int32 direction) {
	if (_state == State::Default) {
		if (_importantSwitchSelected) {
			if (!shownDialogs()->isEmpty() && direction > 0) {
				_selected = *shownDialogs()->cbegin();
				_importantSwitchSelected = false;
			} else {
				return;
			}
		} else if (!_selected) {
			if (_dialogsImportant) {
				_importantSwitchSelected = true;
			} else if (!shownDialogs()->isEmpty() && direction > 0) {
				_selected = *shownDialogs()->cbegin();
			} else {
				return;
			}
		} else if (direction > 0) {
			auto next = shownDialogs()->cfind(_selected);
			if (++next != shownDialogs()->cend()) {
				_selected = *next;
			}
		} else {
			auto prev = shownDialogs()->cfind(_selected);
			if (prev != shownDialogs()->cbegin()) {
				_selected = *(--prev);
			} else if (_dialogsImportant) {
				_importantSwitchSelected = true;
				_selected = nullptr;
			}
		}
		if (_importantSwitchSelected || _selected) {
			int fromY = _importantSwitchSelected ? 0 : (dialogsOffset() + _selected->pos() * st::dialogsRowHeight);
			emit mustScrollTo(fromY, fromY + st::dialogsRowHeight);
		}
	} else if (_state == State::Filtered) {
		if (_hashtagResults.empty() && _filterResults.isEmpty() && _peerSearchResults.empty() && _searchResults.empty()) return;
		if ((_hashtagSelected < 0 || _hashtagSelected >= _hashtagResults.size()) &&
			(_filteredSelected < 0 || _filteredSelected >= _filterResults.size()) &&
			(_peerSearchSelected < 0 || _peerSearchSelected >= _peerSearchResults.size()) &&
			(_searchedSelected < 0 || _searchedSelected >= _searchResults.size())) {
			if (_hashtagResults.empty() && _filterResults.isEmpty() && _peerSearchResults.empty()) {
				_searchedSelected = 0;
			} else if (_hashtagResults.empty() && _filterResults.isEmpty()) {
				_peerSearchSelected = 0;
			} else if (_hashtagResults.empty()) {
				_filteredSelected = 0;
			} else {
				_hashtagSelected = 0;
			}
		} else {
			int32 cur = base::in_range(_hashtagSelected, 0, _hashtagResults.size())
				? _hashtagSelected
				: (base::in_range(_filteredSelected, 0, _filterResults.size())
					? (_hashtagResults.size() + _filteredSelected)
					: (base::in_range(_peerSearchSelected, 0, _peerSearchResults.size())
						? (_peerSearchSelected + _filterResults.size() + _hashtagResults.size())
						: (_searchedSelected + _peerSearchResults.size() + _filterResults.size() + _hashtagResults.size())));
			cur = snap(cur + direction, 0, static_cast<int>(_hashtagResults.size() + _filterResults.size() + _peerSearchResults.size() + _searchResults.size()) - 1);
			if (cur < _hashtagResults.size()) {
				_hashtagSelected = cur;
				_filteredSelected = _peerSearchSelected = _searchedSelected = -1;
			} else if (cur < _hashtagResults.size() + _filterResults.size()) {
				_filteredSelected = cur - _hashtagResults.size();
				_hashtagSelected = _peerSearchSelected = _searchedSelected = -1;
			} else if (cur < _hashtagResults.size() + _filterResults.size() + _peerSearchResults.size()) {
				_peerSearchSelected = cur - _hashtagResults.size() - _filterResults.size();
				_hashtagSelected = _filteredSelected = _searchedSelected = -1;
			} else {
				_hashtagSelected = _filteredSelected = _peerSearchSelected = -1;
				_searchedSelected = cur - _hashtagResults.size() - _filterResults.size() - _peerSearchResults.size();
			}
		}
		if (base::in_range(_hashtagSelected, 0, _hashtagResults.size())) {
			emit mustScrollTo(_hashtagSelected * st::mentionHeight, (_hashtagSelected + 1) * st::mentionHeight);
		} else if (base::in_range(_filteredSelected, 0, _filterResults.size())) {
			emit mustScrollTo(filteredOffset() + _filteredSelected * st::dialogsRowHeight, filteredOffset() + (_filteredSelected + 1) * st::dialogsRowHeight);
		} else if (base::in_range(_peerSearchSelected, 0, _peerSearchResults.size())) {
			emit mustScrollTo(peerSearchOffset() + _peerSearchSelected * st::dialogsRowHeight + (_peerSearchSelected ? 0 : -st::searchedBarHeight), peerSearchOffset() + (_peerSearchSelected + 1) * st::dialogsRowHeight);
		} else {
			emit mustScrollTo(searchedOffset() + _searchedSelected * st::dialogsRowHeight + (_searchedSelected ? 0 : -st::searchedBarHeight), searchedOffset() + (_searchedSelected + 1) * st::dialogsRowHeight);
		}
	}
	update();
}

void DialogsInner::scrollToEntry(const Dialogs::RowDescriptor &entry) {
	int32 fromY = -1;
	if (_state == State::Default) {
		if (auto row = shownDialogs()->getRow(entry.key)) {
			fromY = dialogsOffset() + row->pos() * st::dialogsRowHeight;
		}
	} else if (_state == State::Filtered) {
		if (entry.fullId.msg) {
			for (int32 i = 0, c = _searchResults.size(); i < c; ++i) {
				if (_searchResults[i]->item()->fullId() == entry.fullId) {
					fromY = searchedOffset() + i * st::dialogsRowHeight;
					break;
				}
			}
		}
		if (fromY < 0) {
			for (auto i = 0, c = _filterResults.size(); i != c; ++i) {
				if (_filterResults[i]->key() == entry.key) {
					fromY = filteredOffset() + (i * st::dialogsRowHeight);
					break;
				}
			}
		}
	}
	if (fromY >= 0) {
		emit mustScrollTo(fromY, fromY + st::dialogsRowHeight);
	}
}

void DialogsInner::selectSkipPage(int32 pixels, int32 direction) {
	int toSkip = pixels / int(st::dialogsRowHeight);
	if (_state == State::Default) {
		if (!_selected) {
			if (direction > 0 && !shownDialogs()->isEmpty()) {
				_selected = *shownDialogs()->cbegin();
				_importantSwitchSelected = false;
			} else {
				return;
			}
		}
		if (direction > 0) {
			for (auto i = shownDialogs()->cfind(_selected), end = shownDialogs()->cend(); i != end && (toSkip--); ++i) {
				_selected = *i;
			}
		} else {
			for (auto i = shownDialogs()->cfind(_selected), b = shownDialogs()->cbegin(); i != b && (toSkip--);) {
				_selected = *(--i);
			}
			if (toSkip && _dialogsImportant) {
				_importantSwitchSelected = true;
				_selected = nullptr;
			}
		}
		if (_importantSwitchSelected || _selected) {
			int fromY = (_importantSwitchSelected ? 0 : (dialogsOffset() + _selected->pos() * st::dialogsRowHeight));
			emit mustScrollTo(fromY, fromY + st::dialogsRowHeight);
		}
	} else {
		return selectSkip(direction * toSkip);
	}
	update();
}

void DialogsInner::loadPeerPhotos() {
	if (!parentWidget()) return;

	auto yFrom = _visibleTop;
	auto yTo = _visibleTop + (_visibleBottom - _visibleTop) * (PreloadHeightsCount + 1);
	Auth().downloader().clearPriorities();
	if (_state == State::Default) {
		auto otherStart = shownDialogs()->size() * st::dialogsRowHeight;
		if (yFrom < otherStart) {
			for (auto i = shownDialogs()->cfind(yFrom, st::dialogsRowHeight), end = shownDialogs()->cend(); i != end; ++i) {
				if (((*i)->pos() * st::dialogsRowHeight) >= yTo) {
					break;
				}
				(*i)->entry()->loadUserpic();
			}
			yFrom = 0;
		} else {
			yFrom -= otherStart;
		}
		yTo -= otherStart;
	} else if (_state == State::Filtered) {
		int32 from = (yFrom - filteredOffset()) / st::dialogsRowHeight;
		if (from < 0) from = 0;
		if (from < _filterResults.size()) {
			int32 to = (yTo / int32(st::dialogsRowHeight)) + 1, w = width();
			if (to > _filterResults.size()) to = _filterResults.size();

			for (; from < to; ++from) {
				_filterResults[from]->entry()->loadUserpic();
			}
		}

		from = (yFrom > filteredOffset() + st::searchedBarHeight ? ((yFrom - filteredOffset() - st::searchedBarHeight) / int32(st::dialogsRowHeight)) : 0) - _filterResults.size();
		if (from < 0) from = 0;
		if (from < _peerSearchResults.size()) {
			int32 to = (yTo > filteredOffset() + st::searchedBarHeight ? ((yTo - filteredOffset() - st::searchedBarHeight) / int32(st::dialogsRowHeight)) : 0) - _filterResults.size() + 1, w = width();
			if (to > _peerSearchResults.size()) to = _peerSearchResults.size();

			for (; from < to; ++from) {
				_peerSearchResults[from]->peer->loadUserpic();
			}
		}
		from = (yFrom > filteredOffset() + ((_peerSearchResults.empty() ? 0 : st::searchedBarHeight) + st::searchedBarHeight) ? ((yFrom - filteredOffset() - (_peerSearchResults.empty() ? 0 : st::searchedBarHeight) - st::searchedBarHeight) / int32(st::dialogsRowHeight)) : 0) - _filterResults.size() - _peerSearchResults.size();
		if (from < 0) from = 0;
		if (from < _searchResults.size()) {
			int32 to = (yTo > filteredOffset() + (_peerSearchResults.empty() ? 0 : st::searchedBarHeight) + st::searchedBarHeight ? ((yTo - filteredOffset() - (_peerSearchResults.empty() ? 0 : st::searchedBarHeight) - st::searchedBarHeight) / int32(st::dialogsRowHeight)) : 0) - _filterResults.size() - _peerSearchResults.size() + 1, w = width();
			if (to > _searchResults.size()) to = _searchResults.size();

			for (; from < to; ++from) {
				_searchResults[from]->item()->history()->peer->loadUserpic();
			}
		}
	}
}

bool DialogsInner::switchImportantChats() {
	if (!_importantSwitchSelected
		|| !_dialogsImportant
		|| (_state != State::Default)) {
		return false;
	}
	clearSelection();
	if (Global::DialogsMode() == Dialogs::Mode::All) {
		Global::SetDialogsMode(Dialogs::Mode::Important);
	}
	else {
		Global::SetDialogsMode(Dialogs::Mode::All);
	}
	Local::writeUserSettings();
	refresh();
	_importantSwitchSelected = true;
	return true;
}

bool DialogsInner::chooseHashtag() {
	if (_state != State::Filtered) {
		return false;
	} else if ((_hashtagSelected < 0)
		|| (_hashtagSelected >= _hashtagResults.size())) {
		return false;
	}
	const auto &hashtag = _hashtagResults[_hashtagSelected];
	if (_hashtagDeleteSelected) {
		auto recent = cRecentSearchHashtags();
		for (auto i = recent.begin(); i != recent.cend();) {
			if (i->first == hashtag->tag) {
				i = recent.erase(i);
			} else {
				++i;
			}
		}
		cSetRecentSearchHashtags(recent);
		Local::writeRecentHashtagsAndBots();
		emit refreshHashtags();

		_mouseSelection = true;
		updateSelected();
	} else {
		Local::saveRecentSearchHashtags('#' + hashtag->tag);
		emit completeHashtag(hashtag->tag);
	}
	return true;
}

DialogsInner::ChosenRow DialogsInner::computeChosenRow() const {
	auto msgId = ShowAtUnreadMsgId;
	if (_state == State::Default) {
		if (_selected) {
			return {
				_selected->key(),
				Data::UnreadMessagePosition
			};
		}
	} else if (_state == State::Filtered) {
		if (base::in_range(_filteredSelected, 0, _filterResults.size())) {
			return {
				_filterResults[_filteredSelected]->key(),
				Data::UnreadMessagePosition
			};
		} else if (base::in_range(_peerSearchSelected, 0, _peerSearchResults.size())) {
			return {
				App::history(_peerSearchResults[_peerSearchSelected]->peer),
				Data::UnreadMessagePosition
			};
		} else if (base::in_range(_searchedSelected, 0, _searchResults.size())) {
			const auto result = _searchResults[_searchedSelected].get();
			if (const auto feed = result->searchInChat().feed()) {
				return {
					feed,
					result->item()->position()
				};
			} else {
				return {
					result->item()->history(),
					result->item()->position()
				};
			}
		}
	}
	return ChosenRow();
}

bool DialogsInner::chooseRow() {
	if (switchImportantChats()) {
		return true;
	} else if (chooseHashtag()) {
		return true;
	}
	const auto chosen = computeChosenRow();
	if (chosen.key) {
		if (IsServerMsgId(chosen.message.fullId.msg)) {
			Local::saveRecentSearchHashtags(_filter);
		}
		const auto openSearchResult = !App::main()->selectingPeer()
			&& (_state == State::Filtered)
			&& base::in_range(_filteredSelected, 0, _filterResults.size());
		if (const auto history = chosen.key.history()) {
			App::main()->choosePeer(
				history->peer->id,
				(uniqueSearchResults()
					? ShowAtUnreadMsgId
					: chosen.message.fullId.msg));
		} else if (const auto feed = chosen.key.feed()) {
			_controller->showSection(
				HistoryFeed::Memento(feed, chosen.message),
				Window::SectionShow::Way::ClearStack);
		}
		if (openSearchResult && !Auth().supportMode()) {
			emit clearSearchQuery();
		}
		updateSelectedRow();
		_selected = nullptr;
		_hashtagSelected
			= _filteredSelected
			= _peerSearchSelected
			= _searchedSelected
			= -1;
		return true;
	}
	return false;
}

void DialogsInner::destroyData() {
	_selected = nullptr;
	_hashtagSelected = -1;
	_hashtagResults.clear();
	_filteredSelected = -1;
	_filterResults.clear();
	_filterResultsGlobal.clear();
	_filter.clear();
	_searchedSelected = _peerSearchSelected = -1;
	clearSearchResults();
	_contacts = nullptr;
	_contactsNoDialogs = nullptr;
	_dialogs = nullptr;
	if (_dialogsImportant) {
		_dialogsImportant = nullptr;
	}
}

Dialogs::RowDescriptor DialogsInner::chatListEntryBefore(
		const Dialogs::RowDescriptor &which) const {
	if (!which.key) {
		return Dialogs::RowDescriptor();
	}
	if (_state == State::Default) {
		if (const auto row = shownDialogs()->getRow(which.key)) {
			const auto i = shownDialogs()->cfind(row);
			if (i != shownDialogs()->cbegin()) {
				return Dialogs::RowDescriptor(
					(*(i - 1))->key(),
					FullMsgId(NoChannel, ShowAtUnreadMsgId));
			}
		}
		return Dialogs::RowDescriptor();
	}

	const auto whichHistory = which.key.history();
	const auto whichFullId = which.fullId;
	if (!whichHistory) {
		return Dialogs::RowDescriptor();
	}
	if (!_searchResults.empty()) {
		for (auto b = _searchResults.cbegin(), i = b + 1, e = _searchResults.cend(); i != e; ++i) {
			if (isSearchResultActive(i->get(), which)) {
				const auto j = i - 1;
				return Dialogs::RowDescriptor(
					(*j)->item()->history(),
					(*j)->item()->fullId());
			}
		}
		if (isSearchResultActive(_searchResults[0].get(), which)) {
			if (_peerSearchResults.empty()) {
				if (_filterResults.isEmpty()) {
					return Dialogs::RowDescriptor();
				}
				return Dialogs::RowDescriptor(
					_filterResults.back()->key(),
					FullMsgId(NoChannel, ShowAtUnreadMsgId));
			}
			return Dialogs::RowDescriptor(
				App::history(_peerSearchResults.back()->peer),
				FullMsgId(NoChannel, ShowAtUnreadMsgId));
		}
	}
	if (!_peerSearchResults.empty()
		&& _peerSearchResults[0]->peer == whichHistory->peer) {
		if (_filterResults.isEmpty()) {
			return Dialogs::RowDescriptor();
		}
		return Dialogs::RowDescriptor(
			_filterResults.back()->key(),
			FullMsgId(NoChannel, ShowAtUnreadMsgId));
	}
	if (!_peerSearchResults.empty()) {
		for (auto b = _peerSearchResults.cbegin(), i = b + 1, e = _peerSearchResults.cend(); i != e; ++i) {
			if ((*i)->peer == whichHistory->peer) {
				return Dialogs::RowDescriptor(
					App::history((*(i - 1))->peer),
					FullMsgId(NoChannel, ShowAtUnreadMsgId));
			}
		}
	}
	if (_filterResults.isEmpty() || _filterResults[0]->key() == which.key) {
		return Dialogs::RowDescriptor();
	}

	for (auto b = _filterResults.cbegin(), i = b + 1, e = _filterResults.cend(); i != e; ++i) {
		if ((*i)->key() == which.key) {
			return Dialogs::RowDescriptor(
				(*(i - 1))->key(),
				FullMsgId(NoChannel, ShowAtUnreadMsgId));
		}
	}
	return Dialogs::RowDescriptor();
}

Dialogs::RowDescriptor DialogsInner::chatListEntryAfter(
		const Dialogs::RowDescriptor &which) const {
	if (!which.key) {
		return Dialogs::RowDescriptor();
	}
	if (_state == State::Default) {
		if (const auto row = shownDialogs()->getRow(which.key)) {
			const auto i = shownDialogs()->cfind(row) + 1;
			if (i != shownDialogs()->cend()) {
				return Dialogs::RowDescriptor(
					(*i)->key(),
					FullMsgId(NoChannel, ShowAtUnreadMsgId));
			}
		}
		return Dialogs::RowDescriptor();
	}

	const auto whichHistory = which.key.history();
	const auto whichFullId = which.fullId;
	if (!whichHistory) {
		return Dialogs::RowDescriptor();
	}
	for (auto i = _searchResults.cbegin(), e = _searchResults.cend(); i != e; ++i) {
		if (isSearchResultActive(i->get(), which)) {
			if (++i != e) {
				return Dialogs::RowDescriptor(
					(*i)->item()->history(),
					(*i)->item()->fullId());
			}
			return Dialogs::RowDescriptor();
		}
	}
	for (auto i = _peerSearchResults.cbegin(), e = _peerSearchResults.cend(); i != e; ++i) {
		if ((*i)->peer == whichHistory->peer) {
			++i;
			if (i != e) {
				return Dialogs::RowDescriptor(
					App::history((*i)->peer),
					FullMsgId(NoChannel, ShowAtUnreadMsgId));
			} else if (!_searchResults.empty()) {
				return Dialogs::RowDescriptor(
					_searchResults.front()->item()->history(),
					_searchResults.front()->item()->fullId());
			}
			return Dialogs::RowDescriptor();
		}
	}
	for (auto i = _filterResults.cbegin(), e = _filterResults.cend(); i != e; ++i) {
		if ((*i)->key() == which.key) {
			++i;
			if (i != e) {
				return Dialogs::RowDescriptor(
					(*i)->key(),
					FullMsgId(NoChannel, ShowAtUnreadMsgId));
			} else if (!_peerSearchResults.empty()) {
				return Dialogs::RowDescriptor(
					App::history(_peerSearchResults.front()->peer),
					FullMsgId(NoChannel, ShowAtUnreadMsgId));
			} else if (!_searchResults.empty()) {
				return Dialogs::RowDescriptor(
					_searchResults.front()->item()->history(),
					_searchResults.front()->item()->fullId());
			}
			return Dialogs::RowDescriptor();
		}
	}
	return Dialogs::RowDescriptor();
}

Dialogs::RowDescriptor DialogsInner::chatListEntryFirst() const {
	if (_state == State::Default) {
		const auto i = shownDialogs()->cbegin();
		if (i != shownDialogs()->cend()) {
			return Dialogs::RowDescriptor(
				(*i)->key(),
				FullMsgId(NoChannel, ShowAtUnreadMsgId));
		}
		return Dialogs::RowDescriptor();
	} else if (!_filterResults.empty()) {
		return Dialogs::RowDescriptor(
			_filterResults.front()->key(),
			FullMsgId(NoChannel, ShowAtUnreadMsgId));
	} else if (!_peerSearchResults.empty()) {
		return Dialogs::RowDescriptor(
			App::history(_peerSearchResults.front()->peer),
			FullMsgId(NoChannel, ShowAtUnreadMsgId));
	} else if (!_searchResults.empty()) {
		return Dialogs::RowDescriptor(
			_searchResults.front()->item()->history(),
			_searchResults.front()->item()->fullId());
	}
	return Dialogs::RowDescriptor();
}

Dialogs::RowDescriptor DialogsInner::chatListEntryLast() const {
	if (_state == State::Default) {
		const auto i = shownDialogs()->cend();
		if (i != shownDialogs()->cbegin()) {
			return Dialogs::RowDescriptor(
				(*(i - 1))->key(),
				FullMsgId(NoChannel, ShowAtUnreadMsgId));
		}
		return Dialogs::RowDescriptor();
	} else if (!_searchResults.empty()) {
		return Dialogs::RowDescriptor(
			_searchResults.back()->item()->history(),
			_searchResults.back()->item()->fullId());
	} else if (!_peerSearchResults.empty()) {
		return Dialogs::RowDescriptor(
			App::history(_peerSearchResults.back()->peer),
			FullMsgId(NoChannel, ShowAtUnreadMsgId));
	} else if (!_filterResults.empty()) {
		return Dialogs::RowDescriptor(
			_filterResults.back()->key(),
			FullMsgId(NoChannel, ShowAtUnreadMsgId));
	}
	return Dialogs::RowDescriptor();
}

Dialogs::IndexedList *DialogsInner::contactsList() {
	return _contacts.get();
}

Dialogs::IndexedList *DialogsInner::dialogsList() {
	return _dialogs.get();
}

Dialogs::IndexedList *DialogsInner::contactsNoDialogsList() {
	return _contactsNoDialogs.get();
}

int32 DialogsInner::lastSearchDate() const {
	return _lastSearchDate;
}

PeerData *DialogsInner::lastSearchPeer() const {
	return _lastSearchPeer;
}

MsgId DialogsInner::lastSearchId() const {
	return _lastSearchId;
}

MsgId DialogsInner::lastSearchMigratedId() const {
	return _lastSearchMigratedId;
}

void DialogsInner::setupShortcuts() {
	Shortcuts::Requests(
	) | rpl::filter([=] {
		return isActiveWindow() && !Ui::isLayerShown();
	}) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;

		if (App::main()->selectingPeer()) {
			return;
		}
		const auto row = _controller->activeChatEntryCurrent();
		if (row.key) {
			const auto prev = computeJump(chatListEntryBefore(row), -1);
			const auto next = computeJump(chatListEntryAfter(row), 1);
			request->check(Command::ChatPrevious) && request->handle([=] {
				return jumpToDialogRow(prev);
			});
			request->check(Command::ChatNext) && request->handle([=] {
				return jumpToDialogRow(next);
			});
		}
		request->check(Command::ChatFirst) && request->handle([=] {
			return jumpToDialogRow(computeJump(chatListEntryFirst(), 1));
		});
		request->check(Command::ChatLast) && request->handle([=] {
			return jumpToDialogRow(computeJump(chatListEntryLast(), -1));
		});
		if (Auth().supportMode() && row.key.history()) {
			request->check(
				Command::SupportScrollToCurrent
			) && request->handle([=] {
				scrollToEntry(row);
				return true;
			});
		}
	}, lifetime());
}

Dialogs::RowDescriptor DialogsInner::computeJump(
		const Dialogs::RowDescriptor &to,
		int skipDirection) {
	auto result = to;
	if (Auth().supportMode()) {
		while (result.key
			&& !result.key.entry()->chatListUnreadCount()
			&& !result.key.entry()->chatListUnreadMark()) {
			result = (skipDirection > 0)
				? chatListEntryAfter(result)
				: chatListEntryBefore(result);
		}
	}
	return result;
}

bool DialogsInner::jumpToDialogRow(const Dialogs::RowDescriptor &to) {
	if (const auto history = to.key.history()) {
		Ui::showPeerHistory(
			history,
			(uniqueSearchResults()
				? ShowAtUnreadMsgId
				: to.fullId.msg));
		return true;
	} else if (const auto feed = to.key.feed()) {
		if (const auto item = App::histItemById(to.fullId)) {
			_controller->showSection(
				HistoryFeed::Memento(feed, item->position()));
		} else {
			_controller->showSection(HistoryFeed::Memento(feed));
		}
	}
	return false;
}
