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
#include "dialogswidget.h"

#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_layout.h"
#include "styles/style_dialogs.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_history.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "data/data_drafts.h"
#include "lang/lang_keys.h"
#include "application.h"
#include "mainwindow.h"
#include "dialogswidget.h"
#include "mainwidget.h"
#include "boxes/add_contact_box.h"
#include "boxes/contacts_box.h"
#include "boxes/confirm_box.h"
#include "boxes/about_box.h"
#include "storage/localstorage.h"
#include "apiwrap.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/input_fields.h"
#include "window/themes/window_theme.h"
#include "autoupdater.h"
#include "observer_peer.h"
#include "auth_session.h"
#include "messenger.h"
#include "window/notifications_manager.h"
#include "ui/effects/widget_fade_wrap.h"
#include "window/window_controller.h"

namespace {

constexpr auto kHashtagResultsLimit = 5;
constexpr auto kStartReorderThreshold = 30;

// Debug an assertion violation.
auto MustNotChangeDraggingIndex = false;

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
	PeerSearchResult(PeerData *peer) : peer(peer) {
	}
	PeerData *peer;
	Dialogs::RippleRow row;
};

DialogsInner::DialogsInner(QWidget *parent, gsl::not_null<Window::Controller*> controller, QWidget *main) : SplittedWidget(parent)
, _controller(controller)
, _dialogs(std::make_unique<Dialogs::IndexedList>(Dialogs::SortMode::Date))
, _contactsNoDialogs(std::make_unique<Dialogs::IndexedList>(Dialogs::SortMode::Name))
, _contacts(std::make_unique<Dialogs::IndexedList>(Dialogs::SortMode::Name))
, _a_pinnedShifting(animation(this, &DialogsInner::step_pinnedShifting))
, _addContactLnk(this, lang(lng_add_contact_button))
, _cancelSearchInPeer(this, st::dialogsCancelSearchInPeer) {

#ifdef OS_MAC_OLD
	// Qt 5.3.2 build is working with glitches otherwise.
	setAttribute(Qt::WA_OpaquePaintEvent, false);
#endif // OS_MAC_OLD

	if (Global::DialogsModeEnabled()) {
		_dialogsImportant = std::make_unique<Dialogs::IndexedList>(Dialogs::SortMode::Date);
		_importantSwitch = std::make_unique<ImportantSwitch>();
	}
	connect(main, SIGNAL(peerNameChanged(PeerData*, const PeerData::Names&, const PeerData::NameFirstChars&)), this, SLOT(onPeerNameChanged(PeerData*, const PeerData::Names&, const PeerData::NameFirstChars&)));
	connect(main, SIGNAL(peerPhotoChanged(PeerData*)), this, SLOT(onPeerPhotoChanged(PeerData*)));
	connect(main, SIGNAL(dialogRowReplaced(Dialogs::Row*,Dialogs::Row*)), this, SLOT(onDialogRowReplaced(Dialogs::Row*,Dialogs::Row*)));
	connect(_addContactLnk, SIGNAL(clicked()), App::wnd(), SLOT(onShowAddContact()));
	connect(_cancelSearchInPeer, SIGNAL(clicked()), this, SIGNAL(cancelSearchInPeer()));
	_cancelSearchInPeer->hide();

	subscribe(AuthSession::CurrentDownloaderTaskFinished(), [this] { update(); });
	subscribe(Global::RefItemRemoved(), [this](HistoryItem *item) {
		itemRemoved(item);
	});
	subscribe(App::histories().sendActionAnimationUpdated(), [this](const Histories::SendActionAnimationUpdate &update) {
		auto updateRect = Dialogs::Layout::RowPainter::sendActionAnimationRect(update.width, update.height, getFullWidth(), update.textUpdated);
		updateDialogRow(update.history->peer, MsgId(0), updateRect, UpdateRowSection::Default | UpdateRowSection::Filtered);
	});

	subscribe(Window::Theme::Background(), [this](const Window::Theme::BackgroundUpdate &data) {
		if (data.paletteChanged()) {
			Dialogs::Layout::clearUnreadBadgesCache();
		}
	});

	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::PinnedChanged, [this](const Notify::PeerUpdate &update) {
		stopReorderPinned();
	}));

	refresh();
}

int DialogsInner::dialogsOffset() const {
	return _dialogsImportant ? st::dialogsImportantBarHeight : 0;
}

int DialogsInner::filteredOffset() const {
	return _hashtagResults.size() * st::mentionHeight;
}

int DialogsInner::peerSearchOffset() const {
	return filteredOffset() + (_filterResults.size() * st::dialogsRowHeight) + st::searchedBarHeight;
}

int DialogsInner::searchedOffset() const {
	int result = peerSearchOffset() + (_peerSearchResults.empty() ? 0 : ((_peerSearchResults.size() * st::dialogsRowHeight) + st::searchedBarHeight));
	if (_searchInPeer) result += st::dialogsRowHeight;
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
	auto fullWidth = getFullWidth();
	auto ms = getms();
	if (_state == DefaultState) {
		_a_pinnedShifting.step(ms, false);

		auto rows = shownDialogs();
		auto dialogsClip = r;
		if (_dialogsImportant) {
			auto selected = isPressed() ? _importantSwitchPressed : _importantSwitchSelected;
			Dialogs::Layout::paintImportantSwitch(p, Global::DialogsMode(), fullWidth, selected, paintingOther);
			dialogsClip.translate(0, -st::dialogsImportantBarHeight);
			p.translate(0, st::dialogsImportantBarHeight);
		}
		auto otherStart = rows->size() * st::dialogsRowHeight;
		auto active = App::main()->activePeer();
		auto selected = _menuPeer ? _menuPeer : (isPressed() ? (_pressed ? _pressed->history()->peer : nullptr) : (_selected ? _selected->history()->peer : nullptr));
		if (otherStart) {
			auto reorderingPinned = (_aboveIndex >= 0 && !_pinnedRows.empty());
			auto &list = rows->all();
			if (reorderingPinned) {
				dialogsClip = dialogsClip.marginsAdded(QMargins(0, st::dialogsRowHeight, 0, st::dialogsRowHeight));
			}

			auto i = list.cfind(dialogsClip.top(), st::dialogsRowHeight);
			if (i != list.cend()) {
				auto lastPaintedPos = (*i)->pos();

				// If we're reordering pinned chats we need to fill this area background first.
				if (reorderingPinned) {
					p.fillRect(0, 0, fullWidth, st::dialogsRowHeight * _pinnedRows.size(), st::dialogsBg);
				}

				p.translate(0, lastPaintedPos * st::dialogsRowHeight);
				for (auto e = list.cend(); i != e; ++i) {
					auto row = (*i);
					if (lastPaintedPos * st::dialogsRowHeight >= dialogsClip.top() + dialogsClip.height()) {
						break;
					}

					// Skip currently dragged chat to paint it above others after.
					if (lastPaintedPos != _aboveIndex) {
						paintDialog(p, row, fullWidth, active, selected, paintingOther, ms);
					}

					p.translate(0, st::dialogsRowHeight);
					++lastPaintedPos;
				}

				// Paint the dragged chat above all others.
				if (_aboveIndex >= 0) {
					auto i = list.cfind(_aboveIndex, 1);
					auto pos = (i == list.cend()) ? -1 : (*i)->pos();
					if (pos == _aboveIndex) {
						p.translate(0, (pos - lastPaintedPos) * st::dialogsRowHeight);
						paintDialog(p, *i, fullWidth, active, selected, paintingOther, ms);
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
				p.drawText(QRect(0, 0, fullWidth, st::noContactsHeight - (AuthSession::Current().data().contactsLoaded().value() ? st::noContactsFont->height : 0)), lang(AuthSession::Current().data().contactsLoaded().value() ? lng_no_chats : lng_contacts_loading), style::al_center);
			}
		}
	} else if (_state == FilteredState || _state == SearchedState) {
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
				auto activePeer = App::main()->activePeer();
				auto activeMsgId = App::main()->activeMsgId();
				for (; from < to; ++from) {
					auto row = _filterResults[from];
					auto peer = row->history()->peer;
					auto active = ((peer == activePeer) || (peer->migrateTo() && peer->migrateTo() == activePeer)) && !activeMsgId;
					auto selected = _menuPeer ? (peer == _menuPeer) : (from == (isPressed() ? _filteredPressed : _filteredSelected));
					Dialogs::Layout::RowPainter::paint(p, _filterResults[from], fullWidth, active, selected, paintingOther, ms);
					p.translate(0, st::dialogsRowHeight);
				}
			}
		}

		if (!_peerSearchResults.empty()) {
			p.fillRect(0, 0, fullWidth, st::searchedBarHeight, st::searchedBarBg);
			if (!paintingOther) {
				p.setFont(st::searchedBarFont);
				p.setPen(st::searchedBarFg);
				p.drawTextLeft(st::searchedBarPosition.x(), st::searchedBarPosition.y(), width(), lang(lng_search_global_results), style::al_center);
			}
			p.translate(0, st::searchedBarHeight);

			auto skip = peerSearchOffset();
			auto from = floorclamp(r.y() - skip, st::dialogsRowHeight, 0, _peerSearchResults.size());
			auto to = ceilclamp(r.y() + r.height() - skip, st::dialogsRowHeight, 0, _peerSearchResults.size());
			p.translate(0, from * st::dialogsRowHeight);
			if (from < _peerSearchResults.size()) {
				auto activePeer = App::main()->activePeer();
				auto activeMsgId = App::main()->activeMsgId();
				for (; from < to; ++from) {
					auto &result = _peerSearchResults[from];
					auto peer = result->peer;
					auto active = ((peer == activePeer) || (peer->migrateTo() && peer->migrateTo() == activePeer)) && !activeMsgId;
					auto selected = false ? (peer == _menuPeer) : (from == (isPressed() ? _peerSearchPressed : _peerSearchSelected));
					paintPeerSearchResult(p, result.get(), fullWidth, active, selected, paintingOther, ms);
					p.translate(0, st::dialogsRowHeight);
				}
			}
		}

		if (_searchInPeer) {
			paintSearchInPeer(p, fullWidth, paintingOther);
			p.translate(0, st::dialogsRowHeight);
			if (_state == FilteredState && _searchResults.empty()) {
				p.fillRect(0, 0, fullWidth, st::searchedBarHeight, st::searchedBarBg);
				if (!paintingOther) {
					p.setFont(st::searchedBarFont);
					p.setPen(st::searchedBarFg);
					p.drawTextLeft(st::searchedBarPosition.x(), st::searchedBarPosition.y(), width(), lang(lng_dlg_search_for_messages), style::al_center);
				}
				p.translate(0, st::searchedBarHeight);
			}
		}

		if (_state == SearchedState || !_searchResults.empty()) {
			auto text = _searchResults.empty() ? lang(lng_search_no_results) : lng_search_found_results(lt_count, _searchedMigratedCount + _searchedCount);
			p.fillRect(0, 0, fullWidth, st::searchedBarHeight, st::searchedBarBg);
			if (!paintingOther) {
				p.setFont(st::searchedBarFont);
				p.setPen(st::searchedBarFg);
				p.drawTextLeft(st::searchedBarPosition.x(), st::searchedBarPosition.y(), width(), text, style::al_center);
			}
			p.translate(0, st::searchedBarHeight);

			auto skip = searchedOffset();
			auto from = floorclamp(r.y() - skip, st::dialogsRowHeight, 0, _searchResults.size());
			auto to = ceilclamp(r.y() + r.height() - skip, st::dialogsRowHeight, 0, _searchResults.size());
			p.translate(0, from * st::dialogsRowHeight);
			if (from < _searchResults.size()) {
				auto activePeer = App::main()->activePeer();
				auto activeMsgId = App::main()->activeMsgId();
				for (; from < to; ++from) {
					auto &result = _searchResults[from];
					auto item = result->item();
					auto peer = item->history()->peer;
					auto active = (peer == activePeer && item->id == activeMsgId) || (peer->migrateTo() && peer->migrateTo() == activePeer && item->id == -activeMsgId);
					auto selected = false ? (peer == _menuPeer) : (from == (isPressed() ? _searchedPressed : _searchedSelected));
					Dialogs::Layout::RowPainter::paint(p, result.get(), fullWidth, active, selected, paintingOther, ms);
					p.translate(0, st::dialogsRowHeight);
				}
			}
		}
	}
}

void DialogsInner::paintDialog(Painter &p, Dialogs::Row *row, int fullWidth, PeerData *active, PeerData *selected, bool onlyBackground, TimeMs ms) {
	auto pos = row->pos();
	auto xadd = 0, yadd = 0;
	if (pos < _pinnedRows.size()) {
		yadd = qRound(_pinnedRows[pos].yadd.current());
	}
	if (xadd || yadd) p.translate(xadd, yadd);
	auto isActive = (row->history()->peer == active) || (row->history()->peer->migrateTo() && row->history()->peer->migrateTo() == active);
	auto isSelected = (row->history()->peer == selected);
	Dialogs::Layout::RowPainter::paint(p, row, fullWidth, isActive, isSelected, onlyBackground, ms);
	if (xadd || yadd) p.translate(-xadd, -yadd);
}

void DialogsInner::paintPeerSearchResult(Painter &p, const PeerSearchResult *result, int fullWidth, bool active, bool selected, bool onlyBackground, TimeMs ms) const {
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

void DialogsInner::paintSearchInPeer(Painter &p, int fullWidth, bool onlyBackground) const {
	QRect fullRect(0, 0, fullWidth, st::dialogsRowHeight);
	p.fillRect(fullRect, st::dialogsBg);
	if (onlyBackground) return;

	_searchInPeer->paintUserpicLeft(p, st::dialogsPadding.x(), st::dialogsPadding.y(), getFullWidth(), st::dialogsPhotoSize);

	auto nameleft = st::dialogsPadding.x() + st::dialogsPhotoSize + st::dialogsPhotoPadding;
	auto namewidth = fullWidth - nameleft - st::dialogsPadding.x() * 2 - st::dialogsCancelSearch.width;
	QRect rectForName(nameleft, st::dialogsPadding.y() + st::dialogsNameTop, namewidth, st::msgNameFont->height);

	if (auto chatTypeIcon = Dialogs::Layout::ChatTypeIcon(_searchInPeer, false, false)) {
		chatTypeIcon->paint(p, rectForName.topLeft(), fullWidth);
		rectForName.setLeft(rectForName.left() + st::dialogsChatTypeSkip);
	}

	QRect tr(nameleft, st::dialogsPadding.y() + st::msgNameFont->height + st::dialogsSkip, namewidth, st::dialogsTextFont->height);
	p.setFont(st::dialogsTextFont);
	p.setPen(st::dialogsTextFg);
	p.drawText(tr.left(), tr.top() + st::dialogsTextFont->ascent, st::dialogsTextFont->elided(lang((_searchInPeer->isChannel() && !_searchInPeer->isMegagroup()) ? lng_dlg_search_channel : lng_dlg_search_chat), tr.width()));

	p.setPen(st::dialogsNameFg);
	_searchInPeer->nameText.drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
}

void DialogsInner::activate() {
}

void DialogsInner::mouseMoveEvent(QMouseEvent *e) {
	_mouseSelection = true;
	updateSelected(e->pos());
}

void DialogsInner::clearIrrelevantState() {
	if (_state == DefaultState) {
		_hashtagSelected = -1;
		setHashtagPressed(-1);
		_hashtagDeleteSelected = _hashtagDeletePressed = false;
		_filteredSelected = -1;
		setFilteredPressed(-1);
		_peerSearchSelected = -1;
		setPeerSearchPressed(-1);
		_searchedSelected = -1;
		setSearchedPressed(-1);
	} else if (_state == FilteredState || _state == SearchedState) {
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
	if (_state == DefaultState) {
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
	} else if (_state == FilteredState || _state == SearchedState) {
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
		if (_state == SearchedState && !_searchResults.empty()) {
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
				row->history()->updateChatListEntry();
			}
		});
		_dragStart = e->pos();
	} else if (_hashtagPressed >= 0 && _hashtagPressed < _hashtagResults.size() && !_hashtagDeletePressed) {
		auto row = &_hashtagResults[_hashtagPressed]->row;
		row->addRipple(e->pos(), QSize(getFullWidth(), st::mentionHeight), [this, index = _hashtagPressed] {
			update(0, index * st::mentionHeight, getFullWidth(), st::mentionHeight);
		});
	} else if (_filteredPressed >= 0 && _filteredPressed < _filterResults.size()) {
		auto row = _filterResults[_filteredPressed];
		row->addRipple(e->pos() - QPoint(0, filteredOffset() + _filteredPressed * st::dialogsRowHeight), QSize(getFullWidth(), st::dialogsRowHeight), [row] {
			if (auto main = App::main()) {
				main->dlgUpdated(row->history()->peer, 0);
			}
		});
	} else if (_peerSearchPressed >= 0 && _peerSearchPressed < _peerSearchResults.size()) {
		auto &result = _peerSearchResults[_peerSearchPressed];
		auto row = &result->row;
		row->addRipple(e->pos() - QPoint(0, peerSearchOffset() + _peerSearchPressed * st::dialogsRowHeight), QSize(getFullWidth(), st::dialogsRowHeight), [peer = result->peer] {
			if (auto main = App::main()) {
				main->dlgUpdated(peer, 0);
			}
		});
	} else if (_searchedPressed >= 0 && _searchedPressed < _searchResults.size()) {
		auto &row = _searchResults[_searchedPressed];
		row->addRipple(e->pos() - QPoint(0, searchedOffset() + _searchedPressed * st::dialogsRowHeight), QSize(getFullWidth(), st::dialogsRowHeight), [this, index = _searchedPressed] {
			rtlupdate(0, searchedOffset() + index * st::dialogsRowHeight, getFullWidth(), st::dialogsRowHeight);
		});
	}
}

void DialogsInner::checkReorderPinnedStart(QPoint localPosition) {
	if (_pressed != nullptr && !_dragging && _state == DefaultState) {
		if (qAbs(localPosition.y() - _dragStart.y()) >= convertScale(kStartReorderThreshold)) {
			_dragging = _pressed;
			if (updateReorderIndexGetCount() < 2) {
				_dragging = nullptr;
			} else {
				_pinnedOrder = App::histories().getPinnedOrder();
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
		if (!row->history()->isPinnedDialog()) {
			break;
		}
		++result;
	}
	return result;
}

int DialogsInner::countPinnedIndex(Dialogs::Row *ofRow) {
	if (!ofRow || !ofRow->history()->isPinnedDialog()) {
		return -1;
	}
	auto result = 0;
	for_const (auto row, *shownDialogs()) {
		if (!row->history()->isPinnedDialog()) {
			break;
		} else if (row == ofRow) {
			return result;
		}
		++result;
	}
	return -1;
}

void DialogsInner::savePinnedOrder() {
	auto newOrder = App::histories().getPinnedOrder();
	if (newOrder.size() != _pinnedOrder.size()) {
		return; // Something has changed in the set of pinned chats.
	}
	for_const (auto history, newOrder) {
		if (_pinnedOrder.indexOf(history) < 0) {
			return; // Something has changed in the set of pinned chats.
		}
	}
	App::histories().savePinnedToServer();
}

void DialogsInner::finishReorderPinned() {
	auto wasDragging = (_dragging != nullptr);
	if (wasDragging) {
		savePinnedOrder();
		_dragging = nullptr;
	}

	// Debug an assertion violation.
	if (MustNotChangeDraggingIndex) {
		Unexpected("Must not change draggingIndex while reordering pinned chats.");
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
	t_assert(index < count);
	if (count < 2) {
		stopReorderPinned();
		return 0;
	}

	// Debug an assertion violation.
	if (MustNotChangeDraggingIndex) {
		Unexpected("Must not change draggingIndex while reordering pinned chats.");
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

		// Debug an assertion violation.
		if (shift < 0) {
			auto index = 0;
			for_const (auto row, *shownDialogs()) {
				if (++index >= _draggingIndex + shift) {
					t_assert(row->history()->isPinnedDialog());
					if (index >= _draggingIndex) {
						break;
					}
				}
			}
		}
		MustNotChangeDraggingIndex = true;

		for (auto from = _draggingIndex, to = _draggingIndex + shift; from > to; --from) {
			shownDialogs()->movePinned(_dragging, -1);
			std::swap(_pinnedRows[from], _pinnedRows[from - 1]);
			_pinnedRows[from].yadd = anim::value(_pinnedRows[from].yadd.current() - rowHeight, 0);
			_pinnedRows[from].animStartTime = ms;
		}

		// Debug an assertion violation.
		MustNotChangeDraggingIndex = false;
	} else if (_dragStart.y() < localPosition.y() && _draggingIndex + 1 < pinnedCount) {
		shift = floorclamp(localPosition.y() - _dragStart.y() + (rowHeight / 2), rowHeight, 0, pinnedCount - _draggingIndex - 1);

		// Debug an assertion violation.
		MustNotChangeDraggingIndex = true;
		if (shift > 0) {
			auto index = 0;
			for_const (auto row, *shownDialogs()) {
				if (++index >= _draggingIndex) {
					t_assert(row->history()->isPinnedDialog());
					if (index >= _draggingIndex + shift) {
						break;
					}
				}
			}
		}

		for (auto from = _draggingIndex, to = _draggingIndex + shift; from < to; ++from) {

			// Debug an assertion violation.
			{
				auto swapPinnedIndexWith = shownDialogs()->find(_dragging);
				t_assert(swapPinnedIndexWith != shownDialogs()->cend());
				auto prevPos = (*swapPinnedIndexWith)->pos();
				++swapPinnedIndexWith;
				if (!(*swapPinnedIndexWith)->history()->isPinnedDialog()) {
					SignalHandlers::setCrashAnnotation("DebugInfoBefore", QString("from: %1, to: %2, current: %3, prevPos: %4, nowPos: %5").arg(_draggingIndex).arg(_draggingIndex + shift).arg(from).arg(prevPos).arg((*swapPinnedIndexWith)->pos()));
				} else {
					SignalHandlers::setCrashAnnotation("DebugInfoBefore", QString());
				}
			}

			shownDialogs()->movePinned(_dragging, 1);
			std::swap(_pinnedRows[from], _pinnedRows[from + 1]);
			_pinnedRows[from].yadd = anim::value(_pinnedRows[from].yadd.current() + rowHeight, 0);
			_pinnedRows[from].animStartTime = ms;
		}

		// Debug an assertion violation.
		MustNotChangeDraggingIndex = false;
	}
	if (shift) {
		// Debug an assertion violation.
		if (MustNotChangeDraggingIndex) {
			Unexpected("Must not change draggingIndex while reordering pinned chats.");
		}

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
	auto animating = false;
	auto updateMin = -1;
	auto updateMax = 0;
	for (auto i = 0, l = static_cast<int>(_pinnedRows.size()); i != l; ++i) {
		auto start = _pinnedRows[i].animStartTime;
		if (start) {
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
	if (timer) {
		updateReorderIndexGetCount();
		if (_draggingIndex >= 0) {
			if (updateMin < 0 || updateMin > _draggingIndex) {
				updateMin = _draggingIndex;
			}
			if (updateMax < _draggingIndex) updateMax = _draggingIndex;
		}
		if (updateMin >= 0) {
			auto top = _dialogsImportant ? st::dialogsImportantBarHeight : 0;
			auto updateFrom = top + st::dialogsRowHeight * (updateMin - 1);
			auto updateHeight = st::dialogsRowHeight * (updateMax - updateMin + 3);
			if (_aboveIndex >= 0 && _aboveIndex < _pinnedRows.size()) {
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
			choosePeer();
		} else if (pressed && pressed == _selected) {
			choosePeer();
		} else if (hashtagPressed >= 0 && hashtagPressed == _hashtagSelected && hashtagDeletePressed == _hashtagDeleteSelected) {
			choosePeer();
		} else if (filteredPressed >= 0 && filteredPressed == _filteredSelected) {
			choosePeer();
		} else if (peerSearchPressed >= 0 && peerSearchPressed == _peerSearchSelected) {
			choosePeer();
		} else if (searchedPressed >= 0 && searchedPressed == _searchedSelected) {
			choosePeer();
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
	if (_hashtagPressed >= 0 && _hashtagPressed < _hashtagResults.size()) {
		_hashtagResults[_hashtagPressed]->row.stopLastRipple();
	}
	_hashtagPressed = pressed;
}

void DialogsInner::setFilteredPressed(int pressed) {
	if (_filteredPressed >= 0 && _filteredPressed < _filterResults.size()) {
		_filterResults[_filteredPressed]->stopLastRipple();
	}
	_filteredPressed = pressed;
}

void DialogsInner::setPeerSearchPressed(int pressed) {
	if (_peerSearchPressed >= 0 && _peerSearchPressed < _peerSearchResults.size()) {
		_peerSearchResults[_peerSearchPressed]->row.stopLastRipple();
	}
	_peerSearchPressed = pressed;
}

void DialogsInner::setSearchedPressed(int pressed) {
	if (_searchedPressed >= 0 && _searchedPressed < _searchResults.size()) {
		_searchResults[_searchedPressed]->stopLastRipple();
	}
	_searchedPressed = pressed;
}

void DialogsInner::resizeEvent(QResizeEvent *e) {
	_addContactLnk->move((width() - _addContactLnk->width()) / 2, (st::noContactsHeight + st::noContactsFont->height) / 2);
	auto widthForCancelButton = qMax(width() + otherWidth(), st::dialogsWidthMin);
	_cancelSearchInPeer->moveToLeft(widthForCancelButton - st::dialogsFilterSkip - st::dialogsFilterPadding.x() - _cancelSearchInPeer->width(), (st::dialogsRowHeight - st::dialogsCancelSearchInPeer.height) / 2);
}

void DialogsInner::onDialogRowReplaced(Dialogs::Row *oldRow, Dialogs::Row *newRow) {
	if (_state == FilteredState || _state == SearchedState) {
		for (FilteredDialogs::iterator i = _filterResults.begin(); i != _filterResults.end();) {
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

void DialogsInner::createDialog(History *history) {
	if (history->peer->loadedStatus != PeerData::LoadedStatus::FullLoaded) {
		LOG(("API Error: DialogsInner::createDialog() called for a non loaded peer!"));
		return;
	}

	bool creating = !history->inChatList(Dialogs::Mode::All);
	if (creating) {
		auto mainRow = history->addToChatList(Dialogs::Mode::All, _dialogs.get());
		_contactsNoDialogs->del(history->peer, mainRow);
	}
	if (_dialogsImportant && !history->inChatList(Dialogs::Mode::Important) && !history->mute()) {
		if (Global::DialogsMode() == Dialogs::Mode::Important) {
			creating = true;
		}
		history->addToChatList(Dialogs::Mode::Important, _dialogsImportant.get());
	}

	auto changed = history->adjustByPosInChatList(Dialogs::Mode::All, _dialogs.get());

	if (_dialogsImportant) {
		if (history->mute()) {
			if (Global::DialogsMode() == Dialogs::Mode::Important) {
				return;
			}
		} else {
			auto importantChanged = history->adjustByPosInChatList(Dialogs::Mode::Important, _dialogsImportant.get());
			if (Global::DialogsMode() == Dialogs::Mode::Important) {
				changed = importantChanged;
			}
		}
	}

	int from = dialogsOffset() + changed.movedFrom * st::dialogsRowHeight;
	int to = dialogsOffset() + changed.movedTo * st::dialogsRowHeight;
	if (!_dragging) {
		// Don't jump in chats list scroll position while dragging.
		emit dialogMoved(from, to);
	}

	if (creating) {
		refresh();
	} else if (_state == DefaultState && changed.movedFrom != changed.movedTo) {
		update(0, qMin(from, to), getFullWidth(), qAbs(from - to) + st::dialogsRowHeight);
	}
}

void DialogsInner::removeDialog(History *history) {
	if (!history) return;
	if (history->peer == _menuPeer && _menu) {
		_menu->deleteLater();
	}
	if (_selected && _selected->history() == history) {
		_selected = nullptr;
	}
	if (_pressed && _pressed->history() == history) {
		setPressed(nullptr);
	}
	history->removeFromChatList(Dialogs::Mode::All, _dialogs.get());
	if (_dialogsImportant) {
		history->removeFromChatList(Dialogs::Mode::Important, _dialogsImportant.get());
	}
	AuthSession::Current().notifications().clearFromHistory(history);
	if (_contacts->contains(history->peer->id)) {
		if (!_contactsNoDialogs->contains(history->peer->id)) {
			_contactsNoDialogs->addByName(history);
		}
	}

	Local::removeSavedPeer(history->peer);

	emit App::main()->dialogsUpdated();

	refresh();
}

void DialogsInner::dlgUpdated(Dialogs::Mode list, Dialogs::Row *row) {
	if (_state == DefaultState) {
		if (Global::DialogsMode() == list) {
			auto position = row->pos();
			auto top = dialogsOffset();
			if (position >= 0 && position < _pinnedRows.size()) {
				top += qRound(_pinnedRows[position].yadd.current());
			}
			update(0, top + position * st::dialogsRowHeight, getFullWidth(), st::dialogsRowHeight);
		}
	} else if (_state == FilteredState || _state == SearchedState) {
		if (list == Dialogs::Mode::All) {
			for (int32 i = 0, l = _filterResults.size(); i < l; ++i) {
				if (_filterResults.at(i)->history() == row->history()) {
					update(0, filteredOffset() + i * st::dialogsRowHeight, getFullWidth(), st::dialogsRowHeight);
					break;
				}
			}
		}
	}
}

void DialogsInner::dlgUpdated(PeerData *peer, MsgId msgId) {
	updateDialogRow(peer, msgId, QRect(0, 0, getFullWidth(), st::dialogsRowHeight));
}

void DialogsInner::updateDialogRow(PeerData *peer, MsgId msgId, QRect updateRect, UpdateRowSections sections) {
	auto updateRow = [this, updateRect](int rowTop) {
		rtlupdate(updateRect.x(), rowTop + updateRect.y(), updateRect.width(), updateRect.height());
	};
	if (_state == DefaultState) {
		if (sections & UpdateRowSection::Default) {
			if (auto row = shownDialogs()->getRow(peer->id)) {
				auto position = row->pos();
				auto top = dialogsOffset();
				if (position >= 0 && position < _pinnedRows.size()) {
					top += qRound(_pinnedRows[position].yadd.current());
				}
				updateRow(top + position * st::dialogsRowHeight);
			}
		}
	} else if (_state == FilteredState || _state == SearchedState) {
		if ((sections & UpdateRowSection::Filtered) && !_filterResults.isEmpty()) {
			auto index = 0, add = filteredOffset();
			for_const (auto row, _filterResults) {
				if (row->history()->peer == peer) {
					updateRow(add + index * st::dialogsRowHeight);
					break;
				}
				++index;
			}
		}
		if ((sections & UpdateRowSection::PeerSearch) && !_peerSearchResults.empty()) {
			auto index = 0, add = peerSearchOffset();
			for_const (auto &result, _peerSearchResults) {
				if (result->peer == peer) {
					updateRow(add + index * st::dialogsRowHeight);
					break;
				}
				++index;
			}
		}
		if ((sections & UpdateRowSection::MessageSearch) && !_searchResults.empty()) {
			auto index = 0, add = searchedOffset();
			for_const (auto &result, _searchResults) {
				auto item = result->item();
				if (item->history()->peer == peer && item->id == msgId) {
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

void DialogsInner::updateSelectedRow(PeerData *peer) {
	if (_state == DefaultState) {
		if (peer) {
			if (auto h = App::historyLoaded(peer->id)) {
				if (h->inChatList(Global::DialogsMode())) {
					auto position = h->posInChatList(Global::DialogsMode());
					auto top = dialogsOffset();
					if (position >= 0 && position < _pinnedRows.size()) {
						top += qRound(_pinnedRows[position].yadd.current());
					}
					update(0, top + position * st::dialogsRowHeight, getFullWidth(), st::dialogsRowHeight);
				}
			}
		} else if (_selected) {
			update(0, dialogsOffset() + _selected->pos() * st::dialogsRowHeight, getFullWidth(), st::dialogsRowHeight);
		} else if (_importantSwitchSelected) {
			update(0, 0, getFullWidth(), st::dialogsImportantBarHeight);
		}
	} else if (_state == FilteredState || _state == SearchedState) {
		if (peer) {
			for (int32 i = 0, l = _filterResults.size(); i != l; ++i) {
				if (_filterResults.at(i)->history()->peer == peer) {
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
	if (_importantSwitchSelected || _selected || _filteredSelected >= 0 || _hashtagSelected >= 0 || _peerSearchSelected >= 0 || _searchedSelected >= 0) {
		updateSelectedRow();
		_importantSwitchSelected = false;
		_selected = nullptr;
		_filteredSelected = _searchedSelected = _peerSearchSelected = _hashtagSelected = -1;
		setCursor(style::cur_default);
	}
}

void DialogsInner::contextMenuEvent(QContextMenuEvent *e) {
	if (_menu) {
		_menu->deleteLater();
		_menu = nullptr;
	}
	if (_menuPeer) {
		updateSelectedRow(_menuPeer);
		_menuPeer = nullptr;
	}

	if (e->reason() == QContextMenuEvent::Mouse) {
		_mouseSelection = true;
		updateSelected();
	}

	History *history = nullptr;
	if (_state == DefaultState) {
		if (_selected) history = _selected->history();
	} else if (_state == FilteredState || _state == SearchedState) {
		if (_filteredSelected >= 0 && _filteredSelected < _filterResults.size()) {
			history = _filterResults[_filteredSelected]->history();
		}
	}
	if (!history) return;
	_menuPeer = history->peer;

	if (_pressButton != Qt::LeftButton) {
		mousePressReleased(_pressButton);
	}

	_menu = new Ui::PopupMenu(nullptr);
	App::main()->fillPeerMenu(_menuPeer, [this](const QString &text, base::lambda<void()> callback) {
		return _menu->addAction(text, std::move(callback));
	}, true);
	connect(_menu, SIGNAL(destroyed(QObject*)), this, SLOT(onMenuDestroyed(QObject*)));
	_menu->popup(e->globalPos());
	e->accept();
}

void DialogsInner::onMenuDestroyed(QObject *obj) {
	if (_menu == obj) {
		_menu = nullptr;
		if (_menuPeer) {
			updateSelectedRow(base::take(_menuPeer));
		}
		auto localPos = mapFromGlobal(QCursor::pos());
		if (rect().contains(localPos)) {
			_mouseSelection = true;
			setMouseTracking(true);
			updateSelected(localPos);
		}
	}
}

void DialogsInner::onParentGeometryChanged() {
	auto localPos = mapFromGlobal(QCursor::pos());
	if (rect().contains(localPos)) {
		setMouseTracking(true);
		updateSelected(localPos);
	}
}

void DialogsInner::onPeerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars) {
	_dialogs->peerNameChanged(Dialogs::Mode::All, peer, oldNames, oldChars);
	if (_dialogsImportant) {
		_dialogsImportant->peerNameChanged(Dialogs::Mode::Important, peer, oldNames, oldChars);
	}
	_contactsNoDialogs->peerNameChanged(peer, oldNames, oldChars);
	_contacts->peerNameChanged(peer, oldNames, oldChars);
	update();
}

void DialogsInner::onPeerPhotoChanged(PeerData *peer) {
	update();
}

void DialogsInner::onFilterUpdate(QString newFilter, bool force) {
	auto words = TextUtilities::PrepareSearchWords(newFilter);
	newFilter = words.isEmpty() ? QString() : words.join(' ');
	if (newFilter != _filter || force) {
		_filter = newFilter;
		if (!_searchInPeer && _filter.isEmpty()) {
			_state = DefaultState;
			_hashtagResults.clear();
			_filterResults.clear();
			_peerSearchResults.clear();
			_searchResults.clear();
			_lastSearchDate = 0;
			_lastSearchPeer = 0;
			_lastSearchId = _lastSearchMigratedId = 0;
		} else {
			QStringList::const_iterator fb = words.cbegin(), fe = words.cend(), fi;

			_state = FilteredState;
			_filterResults.clear();
			if (!_searchInPeer && !words.isEmpty()) {
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
				_filterResults.reserve((toFilter ? toFilter->size() : 0) + (toFilterContacts ? toFilterContacts->size() : 0));
				if (toFilter) {
					for_const (auto row, *toFilter) {
						const PeerData::Names &names(row->history()->peer->names);
						PeerData::Names::const_iterator nb = names.cbegin(), ne = names.cend(), ni;
						for (fi = fb; fi != fe; ++fi) {
							QString filterName(*fi);
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
							_filterResults.push_back(row);
						}
					}
				}
				if (toFilterContacts) {
					for_const (auto row, *toFilterContacts) {
						const PeerData::Names &names(row->history()->peer->names);
						PeerData::Names::const_iterator nb = names.cbegin(), ne = names.cend(), ni;
						for (fi = fb; fi != fe; ++fi) {
							QString filterName(*fi);
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
							_filterResults.push_back(row);
						}
					}
				}
			}
		}
		refresh(true);
		setMouseSelection(false, true);
	}
	if (_state != DefaultState) {
		emit searchMessages();
	}
}

void DialogsInner::onHashtagFilterUpdate(QStringRef newFilter) {
	if (newFilter.isEmpty() || newFilter.at(0) != '#' || _searchInPeer) {
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
	if (_state == DefaultState) {
		if (_selected) return _selected->history()->peer;
	} else if (_state == FilteredState || _state == SearchedState) {
		if (_filteredSelected >= 0 && _filteredSelected < _filterResults.size()) {
			return _filterResults[_filteredSelected]->history()->peer;
		} else if (_peerSearchSelected >= 0 && _peerSearchSelected < _peerSearchResults.size()) {
			return _peerSearchResults[_peerSearchSelected]->peer;
		} else if (_searchedSelected >= 0 && _searchedSelected < _searchResults.size()) {
			return _searchResults[_searchedSelected]->item()->history()->peer;
		}
	}
	return nullptr;
}

void DialogsInner::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
	loadPeerPhotos();
	if (_visibleTop + PreloadHeightsCount * (_visibleBottom - _visibleTop) >= height()) {
		if (_loadMoreCallback) {
			_loadMoreCallback();
		}
	}
}

void DialogsInner::itemRemoved(HistoryItem *item) {
	int wasCount = _searchResults.size();
	for (auto i = _searchResults.begin(); i != _searchResults.end();) {
		if ((*i)->item() == item) {
			i = _searchResults.erase(i);
			if (item->history()->peer == _searchInMigrated) {
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
	for_const (auto &dialog, added) {
		if (dialog.type() != mtpc_dialog) {
			continue;
		}

		auto &d = dialog.c_dialog();
		auto peerId = peerFromMTP(d.vpeer);
		if (!peerId) {
			continue;
		}

		auto history = App::historyFromDialog(peerId, d.vunread_count.v, d.vread_inbox_max_id.v, d.vread_outbox_max_id.v);
		auto peer = history->peer;
		if (auto channel = peer->asChannel()) {
			if (d.has_pts()) {
				channel->ptsReceived(d.vpts.v);
			}
			if (!channel->amCreator()) {
				if (auto topMsg = App::histItemById(channel, d.vtop_message.v)) {
					if (topMsg->date <= date(channel->date) && App::api()) {
						App::api()->requestSelfParticipant(channel);
					}
				}
			}
		}
		App::main()->applyNotifySetting(MTP_notifyPeer(d.vpeer), d.vnotify_settings, history);

		if (!history->isPinnedDialog() && !history->lastMsgDate.isNull()) {
			addSavedPeersAfter(history->lastMsgDate);
		}
		_contactsNoDialogs->del(peer);
		if (peer->migrateFrom()) {
			removeDialog(App::historyLoaded(peer->migrateFrom()->id));
		} else if (peer->migrateTo() && peer->migrateTo()->amIn()) {
			removeDialog(history);
		}

		if (d.has_draft() && d.vdraft.type() == mtpc_draftMessage) {
			auto &draft = d.vdraft.c_draftMessage();
			Data::applyPeerCloudDraft(peerId, draft);
		}
	}
	Notify::unreadCounterUpdated();
	refresh();
}

void DialogsInner::addSavedPeersAfter(const QDateTime &date) {
	SavedPeersByTime &saved(cRefSavedPeersByTime());
	while (!saved.isEmpty() && (date.isNull() || date < saved.lastKey())) {
		History *history = App::history(saved.last()->id);
		history->setChatsListDate(saved.lastKey());
		_contactsNoDialogs->del(history->peer);
		saved.remove(saved.lastKey(), saved.last());
	}
}

void DialogsInner::addAllSavedPeers() {
	addSavedPeersAfter(QDateTime());
}

bool DialogsInner::searchReceived(const QVector<MTPMessage> &messages, DialogsSearchRequestType type, int32 fullCount) {
	if (type == DialogsSearchFromStart || type == DialogsSearchPeerFromStart) {
		clearSearchResults(false);
	}
	auto isGlobalSearch = (type == DialogsSearchFromStart || type == DialogsSearchFromOffset);
	auto isMigratedSearch = (type == DialogsSearchMigratedFromStart || type == DialogsSearchMigratedFromOffset);

	TimeId lastDateFound = 0;
	for_const (auto message, messages) {
		auto msgId = idFromMessage(message);
		auto peerId = peerFromMessage(message);
		auto lastDate = dateFromMessage(message);
		if (auto peer = App::peerLoaded(peerId)) {
			if (lastDate) {
				auto item = App::histories().addNewMessage(message, NewMessageExisting);
				_searchResults.push_back(std::make_unique<Dialogs::FakeRow>(item));
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
	if (_state == FilteredState && (!_searchResults.empty() || !_searchInMigrated || type == DialogsSearchMigratedFromStart || type == DialogsSearchMigratedFromOffset)) {
		_state = SearchedState;
	}
	refresh();
	return lastDateFound != 0;
}

void DialogsInner::peerSearchReceived(const QString &query, const QVector<MTPPeer> &result) {
	_peerSearchQuery = query.toLower().trimmed();
	_peerSearchResults.clear();
	_peerSearchResults.reserve(result.size());
	for (auto i = result.cbegin(), e = result.cend(); i != e; ++i) {
		auto peerId = peerFromMTP(*i);
		if (auto history = App::historyLoaded(peerId)) {
			if (history->inChatList(Dialogs::Mode::All)) {
				continue; // skip existing chats
			}
		}
		if (auto peer = App::peerLoaded(peerId)) {
			_peerSearchResults.push_back(std::make_unique<PeerSearchResult>(App::peer(peerId)));
		} else {
			LOG(("API Error: user %1 was not loaded in DialogsInner::peopleReceived()").arg(peerId));
		}
	}
	refresh();
}

void DialogsInner::contactsReceived(const QVector<MTPContact> &result) {
	for_const (auto contact, result) {
		if (contact.type() != mtpc_contact) continue;

		auto userId = contact.c_contact().vuser_id.v;
		if (userId == AuthSession::CurrentUserId() && App::self()) {
			if (App::self()->contact < 1) {
				App::self()->contact = 1;
				Notify::userIsContactChanged(App::self());
			}
		}
	}
	refresh();
}

void DialogsInner::notify_userIsContactChanged(UserData *user, bool fromThisApp) {
	if (user->loadedStatus != PeerData::FullLoaded) {
		LOG(("API Error: notify_userIsContactChanged() called for a not loaded user!"));
		return;
	}
	if (user->contact > 0) {
		auto history = App::history(user->id);
		_contacts->addByName(history);
		if (auto row = shownDialogs()->getRow(user->id)) {
			if (fromThisApp) {
				_selected = row;
				_importantSwitchSelected = false;
			}
		} else if (!_dialogs->contains(user->id)) {
			_contactsNoDialogs->addByName(history);
		}
	} else {
		if (_selected && _selected->history()->peer == user) {
			_selected = nullptr;
		}
		if (_pressed && _pressed->history()->peer == user) {
			setPressed(nullptr);
		}
		_contactsNoDialogs->del(user);
		_contacts->del(user);
	}
	refresh();
}

void DialogsInner::notify_historyMuteUpdated(History *history) {
	if (!_dialogsImportant || !history->inChatList(Dialogs::Mode::All)) return;

	if (history->mute()) {
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
		} else if (_state == DefaultState && changed.movedFrom != changed.movedTo) {
			update(0, qMin(from, to), getFullWidth(), qAbs(from - to) + st::dialogsRowHeight);
		}
	}
}

void DialogsInner::refresh(bool toTop) {
	int32 h = 0;
	if (_state == DefaultState) {
		if (shownDialogs()->isEmpty()) {
			h = st::noContactsHeight;
			if (AuthSession::Current().data().contactsLoaded().value()) {
				if (_addContactLnk->isHidden()) _addContactLnk->show();
			} else {
				if (!_addContactLnk->isHidden()) _addContactLnk->hide();
			}
		} else {
			h = dialogsOffset() + shownDialogs()->size() * st::dialogsRowHeight;
			if (!_addContactLnk->isHidden()) _addContactLnk->hide();
		}
	} else {
		if (!_addContactLnk->isHidden()) _addContactLnk->hide();
		if (_state == FilteredState) {
			h = searchedOffset() + (_searchResults.size() * st::dialogsRowHeight) + ((_searchResults.empty() && !_searchInPeer) ? -st::searchedBarHeight : 0);
		} else if (_state == SearchedState) {
			h = searchedOffset() + (_searchResults.size() * st::dialogsRowHeight);
		}
	}
	setHeight(h);
	if (toTop) {
		stopReorderPinned();
		emit mustScrollTo(0, 0);
		loadPeerPhotos();
	}
	_controller->dialogsListDisplayForced().set(_searchInPeer || !_filter.isEmpty(), true);
	update();
}

void DialogsInner::setMouseSelection(bool mouseSelection, bool toTop) {
	_mouseSelection = mouseSelection;
	if (!_mouseSelection && toTop) {
		if (_state == DefaultState) {
			_selected = nullptr;
			_importantSwitchSelected = false;
		} else if (_state == FilteredState || _state == SearchedState) { // don't select first elem in search
			_filteredSelected = _peerSearchSelected = _searchedSelected = _hashtagSelected = -1;
			setCursor(style::cur_default);
		}
	}
}

void DialogsInner::setState(State newState) {
	_state = newState;
	clearIrrelevantState();
	if (_state == DefaultState) {
		clearSearchResults();
	} else if (_state == FilteredState || _state == SearchedState) {
		_hashtagResults.clear();
		_hashtagSelected = -1;
		_filterResults.clear();
		_filteredSelected = -1;
	}
	onFilterUpdate(_filter, true);
}

DialogsInner::State DialogsInner::state() const {
	return _state;
}

bool DialogsInner::hasFilteredResults() const {
	return !_filterResults.isEmpty() && _hashtagResults.empty();
}

void DialogsInner::searchInPeer(PeerData *peer) {
	_searchInPeer = peer ? (peer->migrateTo() ? peer->migrateTo() : peer) : nullptr;
	_searchInMigrated = _searchInPeer ? _searchInPeer->migrateFrom() : nullptr;
	if (_searchInPeer) {
		onHashtagFilterUpdate(QStringRef());
		_cancelSearchInPeer->show();
	} else {
		_cancelSearchInPeer->hide();
	}
	_controller->dialogsListDisplayForced().set(_searchInPeer || !_filter.isEmpty(), true);
}

void DialogsInner::clearFilter() {
	if (_state == FilteredState || _state == SearchedState) {
		if (_searchInPeer) {
			_state = FilteredState;
		} else {
			_state = DefaultState;
		}
		_hashtagResults.clear();
		_filterResults.clear();
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
	if (_state == DefaultState) {
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
	} else if (_state == FilteredState || _state == SearchedState) {
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
			int32 cur = (_hashtagSelected >= 0 && _hashtagSelected < _hashtagResults.size()) ? _hashtagSelected : ((_filteredSelected >= 0 && _filteredSelected < _filterResults.size()) ? (_hashtagResults.size() + _filteredSelected) : ((_peerSearchSelected >= 0 && _peerSearchSelected < _peerSearchResults.size()) ? (_peerSearchSelected + _filterResults.size() + _hashtagResults.size()) : (_searchedSelected + _peerSearchResults.size() + _filterResults.size() + _hashtagResults.size())));
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
		if (_hashtagSelected >= 0 && _hashtagSelected < _hashtagResults.size()) {
			emit mustScrollTo(_hashtagSelected * st::mentionHeight, (_hashtagSelected + 1) * st::mentionHeight);
		} else if (_filteredSelected >= 0 && _filteredSelected < _filterResults.size()) {
			emit mustScrollTo(filteredOffset() + _filteredSelected * st::dialogsRowHeight, filteredOffset() + (_filteredSelected + 1) * st::dialogsRowHeight);
		} else if (_peerSearchSelected >= 0 && _peerSearchSelected < _peerSearchResults.size()) {
			emit mustScrollTo(peerSearchOffset() + _peerSearchSelected * st::dialogsRowHeight + (_peerSearchSelected ? 0 : -st::searchedBarHeight), peerSearchOffset() + (_peerSearchSelected + 1) * st::dialogsRowHeight);
		} else {
			emit mustScrollTo(searchedOffset() + _searchedSelected * st::dialogsRowHeight + (_searchedSelected ? 0 : -st::searchedBarHeight), searchedOffset() + (_searchedSelected + 1) * st::dialogsRowHeight);
		}
	}
	update();
}

void DialogsInner::scrollToPeer(const PeerId &peer, MsgId msgId) {
	int32 fromY = -1;
	if (_state == DefaultState) {
		if (auto row = shownDialogs()->getRow(peer)) {
			fromY = dialogsOffset() + row->pos() * st::dialogsRowHeight;
		}
	} else if (_state == FilteredState || _state == SearchedState) {
		if (msgId) {
			for (int32 i = 0, c = _searchResults.size(); i < c; ++i) {
				if (_searchResults[i]->item()->history()->peer->id == peer && _searchResults[i]->item()->id == msgId) {
					fromY = searchedOffset() + i * st::dialogsRowHeight;
					break;
				}
			}
		}
		if (fromY < 0) {
			for (int32 i = 0, c = _filterResults.size(); i < c; ++i) {
				if (_filterResults[i]->history()->peer->id == peer) {
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
	if (_state == DefaultState) {
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
	AuthSession::Current().downloader().clearPriorities();
	if (_state == DefaultState) {
		auto otherStart = shownDialogs()->size() * st::dialogsRowHeight;
		if (yFrom < otherStart) {
			for (auto i = shownDialogs()->cfind(yFrom, st::dialogsRowHeight), end = shownDialogs()->cend(); i != end; ++i) {
				if (((*i)->pos() * st::dialogsRowHeight) >= yTo) {
					break;
				}
				(*i)->history()->peer->loadUserpic();
			}
			yFrom = 0;
		} else {
			yFrom -= otherStart;
		}
		yTo -= otherStart;
	} else if (_state == FilteredState || _state == SearchedState) {
		int32 from = (yFrom - filteredOffset()) / st::dialogsRowHeight;
		if (from < 0) from = 0;
		if (from < _filterResults.size()) {
			int32 to = (yTo / int32(st::dialogsRowHeight)) + 1, w = width();
			if (to > _filterResults.size()) to = _filterResults.size();

			for (; from < to; ++from) {
				_filterResults[from]->history()->peer->loadUserpic();
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

bool DialogsInner::choosePeer() {
	History *history = nullptr;
	MsgId msgId = ShowAtUnreadMsgId;
	if (_state == DefaultState) {
		if (_importantSwitchSelected && _dialogsImportant) {
			clearSelection();
			if (Global::DialogsMode() == Dialogs::Mode::All) {
				Global::SetDialogsMode(Dialogs::Mode::Important);
			} else {
				Global::SetDialogsMode(Dialogs::Mode::All);
			}
			Local::writeUserSettings();
			refresh();
			_importantSwitchSelected = true;
			return true;
		} else if (_selected) {
			history = _selected->history();
		}
	} else if (_state == FilteredState || _state == SearchedState) {
		if (_hashtagSelected >= 0 && _hashtagSelected < _hashtagResults.size()) {
			auto &hashtag = _hashtagResults[_hashtagSelected];
			if (_hashtagDeleteSelected) {
				RecentHashtagPack recent(cRecentSearchHashtags());
				for (RecentHashtagPack::iterator i = recent.begin(); i != recent.cend();) {
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
				saveRecentHashtags('#' + hashtag->tag);
				emit completeHashtag(hashtag->tag);
			}
			return true;
		}
		if (_filteredSelected >= 0 && _filteredSelected < _filterResults.size()) {
			history = _filterResults[_filteredSelected]->history();
		} else if (_peerSearchSelected >= 0 && _peerSearchSelected < _peerSearchResults.size()) {
			history = App::history(_peerSearchResults[_peerSearchSelected]->peer->id);
		} else if (_searchedSelected >= 0 && _searchedSelected < _searchResults.size()) {
			history = _searchResults[_searchedSelected]->item()->history();
			msgId = _searchResults[_searchedSelected]->item()->id;
		}
	}
	if (history) {
		if (msgId > 0) {
			saveRecentHashtags(_filter);
		}
		bool chosen = (!App::main()->selectingPeer(true) && (_state == FilteredState || _state == SearchedState) && _filteredSelected >= 0 && _filteredSelected < _filterResults.size());
		App::main()->choosePeer(history->peer->id, msgId);
		if (chosen) {
			emit searchResultChosen();
		}
		updateSelectedRow();
		_selected = nullptr;
		_hashtagSelected = _filteredSelected = _peerSearchSelected = _searchedSelected = -1;
		return true;
	}
	return false;
}

void DialogsInner::saveRecentHashtags(const QString &text) {
	auto found = false;
	QRegularExpressionMatch m;
	auto recent = cRecentSearchHashtags();
	for (int32 i = 0, next = 0; (m = TextUtilities::RegExpHashtag().match(text, i)).hasMatch(); i = next) {
		i = m.capturedStart();
		next = m.capturedEnd();
		if (m.hasMatch()) {
			if (!m.capturedRef(1).isEmpty()) {
				++i;
			}
			if (!m.capturedRef(2).isEmpty()) {
				--next;
			}
		}
		if (!found && cRecentWriteHashtags().isEmpty() && cRecentSearchHashtags().isEmpty()) {
			Local::readRecentHashtagsAndBots();
			recent = cRecentSearchHashtags();
		}
		found = true;
		incrementRecentHashtag(recent, text.mid(i + 1, next - i - 1));
	}
	if (found) {
		cSetRecentSearchHashtags(recent);
		Local::writeRecentHashtagsAndBots();
	}
}

void DialogsInner::destroyData() {
	_selected = nullptr;
	_hashtagSelected = -1;
	_hashtagResults.clear();
	_filteredSelected = -1;
	_filterResults.clear();
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

void DialogsInner::peerBefore(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) const {
	if (!inPeer) {
		outPeer = nullptr;
		outMsg = 0;
		return;
	}
	if (_state == DefaultState) {
		if (auto row = shownDialogs()->getRow(inPeer->id)) {
			auto i = shownDialogs()->cfind(row);
			if (i != shownDialogs()->cbegin()) {
				outPeer = (*(--i))->history()->peer;
				outMsg = ShowAtUnreadMsgId;
				return;
			}
		}
		outPeer = nullptr;
		outMsg = 0;
		return;
	} else if (_state == FilteredState || _state == SearchedState) {
		if (inMsg && !_searchResults.empty()) {
			for (auto b = _searchResults.cbegin(), i = b + 1, e = _searchResults.cend(); i != e; ++i) {
				if ((*i)->item()->history()->peer == inPeer && (*i)->item()->id == inMsg) {
					auto j = i - 1;
					outPeer = (*j)->item()->history()->peer;
					outMsg = (*j)->item()->id;
					return;
				}
			}
			if (_searchResults.at(0)->item()->history()->peer == inPeer && _searchResults.at(0)->item()->id == inMsg) {
				outMsg = ShowAtUnreadMsgId;
				if (_peerSearchResults.empty()) {
					if (_filterResults.isEmpty()) {
						outPeer = nullptr;
					} else {
						outPeer = _filterResults.back()->history()->peer;
					}
				} else {
					outPeer = _peerSearchResults.back()->peer;
				}
				return;
			}
		}
		if (!_peerSearchResults.empty() && _peerSearchResults[0]->peer == inPeer) {
			outPeer = _filterResults.isEmpty() ? 0 : _filterResults.back()->history()->peer;
			outMsg = ShowAtUnreadMsgId;
			return;
		}
		if (!_peerSearchResults.empty()) {
			for (auto b = _peerSearchResults.cbegin(), i = b + 1, e = _peerSearchResults.cend(); i != e; ++i) {
				if ((*i)->peer == inPeer) {
					outPeer = (*(i - 1))->peer;
					outMsg = ShowAtUnreadMsgId;
					return;
				}
			}
		}
		if (_filterResults.isEmpty() || _filterResults.at(0)->history()->peer == inPeer) {
			outPeer = nullptr;
			outMsg = 0;
			return;
		}

		for (auto b = _filterResults.cbegin(), i = b + 1, e = _filterResults.cend(); i != e; ++i) {
			if ((*i)->history()->peer == inPeer) {
				outPeer = (*(i - 1))->history()->peer;
				outMsg = ShowAtUnreadMsgId;
				return;
			}
		}
	}
	outPeer = nullptr;
	outMsg = 0;
}

void DialogsInner::peerAfter(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) const {
	if (!inPeer) {
		outPeer = nullptr;
		outMsg = 0;
		return;
	}
	if (_state == DefaultState) {
		if (auto row = shownDialogs()->getRow(inPeer->id)) {
			auto i = shownDialogs()->cfind(row) + 1;
			if (i != shownDialogs()->cend()) {
				outPeer = (*i)->history()->peer;
				outMsg = ShowAtUnreadMsgId;
				return;
			}
		}
		outPeer = nullptr;
		outMsg = 0;
		return;
	} else if (_state == FilteredState || _state == SearchedState) {
		if (inMsg) {
			for (auto i = _searchResults.cbegin(), e = _searchResults.cend(); i != e; ++i) {
				if ((*i)->item()->history()->peer == inPeer && (*i)->item()->id == inMsg) {
					++i;
					outPeer = (i == e) ? nullptr : (*i)->item()->history()->peer;
					outMsg = (i == e) ? 0 : (*i)->item()->id;
					return;
				}
			}
		}
		for (auto i = _peerSearchResults.cbegin(), e = _peerSearchResults.cend(); i != e; ++i) {
			if ((*i)->peer == inPeer) {
				++i;
				if (i == e && !_searchResults.empty()) {
					outPeer = _searchResults.front()->item()->history()->peer;
					outMsg = _searchResults.front()->item()->id;
				} else {
					outPeer = (i == e) ? nullptr : (*i)->peer;
					outMsg = ShowAtUnreadMsgId;
				}
				return;
			}
		}
		for (FilteredDialogs::const_iterator i = _filterResults.cbegin(), e = _filterResults.cend(); i != e; ++i) {
			if ((*i)->history()->peer == inPeer) {
				++i;
				if (i == e && !_peerSearchResults.empty()) {
					outPeer = _peerSearchResults.front()->peer;
					outMsg = ShowAtUnreadMsgId;
				} else if (i == e && !_searchResults.empty()) {
					outPeer = _searchResults.front()->item()->history()->peer;
					outMsg = _searchResults.front()->item()->id;
				} else {
					outPeer = (i == e) ? nullptr : (*i)->history()->peer;
					outMsg = ShowAtUnreadMsgId;
				}
				return;
			}
		}
	}
	outPeer = nullptr;
	outMsg = 0;
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

class DialogsWidget::UpdateButton : public Ui::RippleButton {
public:
	UpdateButton(QWidget *parent);

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

private:
	QString _text;
	const style::FlatButton &_st;

};

DialogsWidget::UpdateButton::UpdateButton(QWidget *parent) : RippleButton(parent, st::dialogsUpdateButton.ripple)
, _text(lang(lng_update_telegram).toUpper())
, _st(st::dialogsUpdateButton) {
	resize(st::dialogsWidthMin, _st.height);
}

void DialogsWidget::UpdateButton::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	update();
}

void DialogsWidget::UpdateButton::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	QRect r(0, height() - _st.height, width(), _st.height);
	p.fillRect(r, isOver() ? _st.overBgColor : _st.bgColor);

	paintRipple(p, 0, 0, getms());

	p.setFont(isOver() ? _st.overFont : _st.font);
	p.setRenderHint(QPainter::TextAntialiasing);
	p.setPen(isOver() ? _st.overColor : _st.color);

	if (width() >= st::dialogsWidthMin) {
		r.setTop(_st.textTop);
		p.drawText(r, _text, style::al_top);
	} else {
		(isOver() ? st::dialogsInstallUpdateOver : st::dialogsInstallUpdate).paintInCenter(p, r);
	}
}

DialogsWidget::DialogsWidget(QWidget *parent, gsl::not_null<Window::Controller*> controller) : Window::AbstractSectionWidget(parent, controller)
, _mainMenuToggle(this, st::dialogsMenuToggle)
, _filter(this, st::dialogsFilter, langFactory(lng_dlg_filter))
, _jumpToDate(this, object_ptr<Ui::IconButton>(this, st::dialogsCalendar))
, _cancelSearch(this, st::dialogsCancelSearch)
, _lockUnlock(this, st::dialogsLock)
, _scroll(this, st::dialogsScroll) {
	_inner = _scroll->setOwnedWidget(object_ptr<DialogsInner>(this, controller, parent));
	connect(_inner, SIGNAL(draggingScrollDelta(int)), this, SLOT(onDraggingScrollDelta(int)));
	connect(_inner, SIGNAL(mustScrollTo(int,int)), _scroll, SLOT(scrollToY(int,int)));
	connect(_inner, SIGNAL(dialogMoved(int,int)), this, SLOT(onDialogMoved(int,int)));
	connect(_inner, SIGNAL(searchMessages()), this, SLOT(onNeedSearchMessages()));
	connect(_inner, SIGNAL(searchResultChosen()), this, SLOT(onCancel()));
	connect(_inner, SIGNAL(completeHashtag(QString)), this, SLOT(onCompleteHashtag(QString)));
	connect(_inner, SIGNAL(refreshHashtags()), this, SLOT(onFilterCursorMoved()));
	connect(_inner, SIGNAL(cancelSearchInPeer()), this, SLOT(onCancelSearchInPeer()));
	connect(_scroll, SIGNAL(geometryChanged()), _inner, SLOT(onParentGeometryChanged()));
	connect(_scroll, SIGNAL(scrolled()), this, SLOT(onListScroll()));
	connect(_filter, SIGNAL(cancelled()), this, SLOT(onCancel()));
	connect(_filter, SIGNAL(changed()), this, SLOT(onFilterUpdate()));
	connect(_filter, SIGNAL(cursorPositionChanged(int,int)), this, SLOT(onFilterCursorMoved(int,int)));

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	Sandbox::connect(SIGNAL(updateLatest()), this, SLOT(onCheckUpdateStatus()));
	Sandbox::connect(SIGNAL(updateFailed()), this, SLOT(onCheckUpdateStatus()));
	Sandbox::connect(SIGNAL(updateReady()), this, SLOT(onCheckUpdateStatus()));
	onCheckUpdateStatus();
#endif // !TDESKTOP_DISABLE_AUTOUPDATE

	subscribe(Adaptive::Changed(), [this] { updateForwardBar(); });

	_cancelSearch->setClickedCallback([this] { onCancelSearch(); });
	_jumpToDate->entity()->setClickedCallback([this] { if (_searchInPeer) App::main()->showJumpToDate(_searchInPeer, QDate()); });
	_lockUnlock->setVisible(Global::LocalPasscode());
	subscribe(Global::RefLocalPasscodeChanged(), [this] { updateLockUnlockVisibility(); });
	_lockUnlock->setClickedCallback([this] {
		_lockUnlock->setIconOverride(&st::dialogsUnlockIcon, &st::dialogsUnlockIconOver);
		Messenger::Instance().setupPasscode();
		_lockUnlock->setIconOverride(nullptr);
	});
	_mainMenuToggle->setClickedCallback([this] { showMainMenu(); });

	_chooseByDragTimer.setSingleShot(true);
	connect(&_chooseByDragTimer, SIGNAL(timeout()), this, SLOT(onChooseByDrag()));

	setAcceptDrops(true);

	_searchTimer.setSingleShot(true);
	connect(&_searchTimer, SIGNAL(timeout()), this, SLOT(onSearchMessages()));

	_inner->setLoadMoreCallback([this] {
		if (_inner->state() == DialogsInner::SearchedState || (_inner->state() == DialogsInner::FilteredState && _searchInMigrated && _searchFull && !_searchFullMigrated)) {
			onSearchMore();
		} else {
			loadDialogs();
		}
	});

	_filter->setFocusPolicy(Qt::StrongFocus);
	_filter->customUpDown(true);

	updateJumpToDateVisibility(true);
}

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
void DialogsWidget::onCheckUpdateStatus() {
	if (Sandbox::updatingState() == Application::UpdatingReady) {
		if (_updateTelegram) return;
		_updateTelegram.create(this);
		_updateTelegram->show();
		_updateTelegram->setClickedCallback([] {
			checkReadyUpdate();
			App::restart();
		});
	} else {
		if (!_updateTelegram) return;
		_updateTelegram.destroy();
	}
	updateControlsGeometry();
}
#endif // TDESKTOP_DISABLE_AUTOUPDATE

void DialogsWidget::activate() {
	_filter->setFocus();
	_inner->activate();
}

void DialogsWidget::createDialog(History *history) {
	auto creating = !history->inChatList(Dialogs::Mode::All);
	_inner->createDialog(history);
	if (creating && history->peer->migrateFrom()) {
		if (auto migrated = App::historyLoaded(history->peer->migrateFrom()->id)) {
			if (migrated->inChatList(Dialogs::Mode::All)) {
				removeDialog(migrated);
			}
		}
	}
}

void DialogsWidget::dlgUpdated(Dialogs::Mode list, Dialogs::Row *row) {
	_inner->dlgUpdated(list, row);
}

void DialogsWidget::dlgUpdated(PeerData *peer, MsgId msgId) {
	_inner->dlgUpdated(peer, msgId);
}

void DialogsWidget::dialogsToUp() {
	if (_filter->getLastText().trimmed().isEmpty()) {
		_scroll->scrollToY(0);
	}
}

void DialogsWidget::startWidthAnimation() {
	if (!_widthAnimationCache.isNull()) {
		return;
	}
	auto scrollGeometry = _scroll->geometry();
	auto grabGeometry = QRect(scrollGeometry.x(), scrollGeometry.y(), st::dialogsWidthMin, scrollGeometry.height());
	_scroll->setGeometry(grabGeometry);
	myEnsureResized(_scroll);
	auto image = QImage(grabGeometry.size() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(cRetinaFactor());
	image.fill(Qt::transparent);
	_scroll->render(&image, QPoint(0, 0), QRect(QPoint(0, 0), grabGeometry.size()), QWidget::DrawChildren | QWidget::IgnoreMask);
	_widthAnimationCache = App::pixmapFromImageInPlace(std::move(image));
	_scroll->setGeometry(scrollGeometry);
	_scroll->hide();
}

void DialogsWidget::stopWidthAnimation() {
	_widthAnimationCache = QPixmap();
	if (!_a_show.animating()) {
		_scroll->show();
	}
	update();
}

void DialogsWidget::showFast() {
	show();
	updateForwardBar();
}

void DialogsWidget::showAnimated(Window::SlideDirection direction, const Window::SectionSlideParams &params) {
	_showDirection = direction;

	_a_show.finish();

	_cacheUnder = params.oldContentCache;
	show();
	updateForwardBar();
	_cacheOver = App::main()->grabForShowAnimation(params);

	_scroll->hide();
	_mainMenuToggle->hide();
	if (_forwardCancel) _forwardCancel->hide();
	_filter->hide();
	_cancelSearch->hideFast();
	_jumpToDate->hideFast();
	_lockUnlock->hide();

	int delta = st::slideShift;
	if (_showDirection == Window::SlideDirection::FromLeft) {
		std::swap(_cacheUnder, _cacheOver);
	}
	_a_show.start([this] { animationCallback(); }, 0., 1., st::slideDuration, Window::SlideAnimation::transition());
}

bool DialogsWidget::wheelEventFromFloatPlayer(QEvent *e, Window::Column myColumn, Window::Column playerColumn) {
	return _scroll->viewportEvent(e);
}

QRect DialogsWidget::rectForFloatPlayer(Window::Column myColumn, Window::Column playerColumn) {
	return mapToGlobal(_scroll->geometry());
}

void DialogsWidget::animationCallback() {
	update();
	if (!_a_show.animating()) {
		_cacheUnder = _cacheOver = QPixmap();

		_scroll->show();
		_mainMenuToggle->show();
		if (_forwardCancel) _forwardCancel->show();
		_filter->show();
		updateLockUnlockVisibility();
		updateJumpToDateVisibility(true);

		onFilterUpdate();
		if (App::wnd()) App::wnd()->setInnerFocus();
	}
}

void DialogsWidget::onCancel() {
	if (!onCancelSearch() || (!_searchInPeer && !App::main()->selectingPeer())) {
		emit cancelled();
	}
}

void DialogsWidget::notify_userIsContactChanged(UserData *user, bool fromThisApp) {
	if (fromThisApp) {
		_filter->setText(QString());
		_filter->updatePlaceholder();
		onFilterUpdate();
	}
	_inner->notify_userIsContactChanged(user, fromThisApp);
}

void DialogsWidget::notify_historyMuteUpdated(History *history) {
	_inner->notify_historyMuteUpdated(history);
}

void DialogsWidget::unreadCountsReceived(const QVector<MTPDialog> &dialogs) {
}

void DialogsWidget::dialogsReceived(const MTPmessages_Dialogs &dialogs, mtpRequestId requestId) {
	if (_dialogsRequestId != requestId) return;

	const QVector<MTPDialog> *dialogsList = 0;
	const QVector<MTPMessage> *messagesList = 0;
	switch (dialogs.type()) {
	case mtpc_messages_dialogs: {
		auto &data = dialogs.c_messages_dialogs();
		App::feedUsers(data.vusers);
		App::feedChats(data.vchats);
		messagesList = &data.vmessages.v;
		dialogsList = &data.vdialogs.v;
		_dialogsFull = true;
	} break;
	case mtpc_messages_dialogsSlice: {
		auto &data = dialogs.c_messages_dialogsSlice();
		App::feedUsers(data.vusers);
		App::feedChats(data.vchats);
		messagesList = &data.vmessages.v;
		dialogsList = &data.vdialogs.v;
	} break;
	}

	if (!AuthSession::Current().data().contactsLoaded().value() && !_contactsRequestId) {
		_contactsRequestId = MTP::send(MTPcontacts_GetContacts(MTP_string("")), rpcDone(&DialogsWidget::contactsReceived), rpcFail(&DialogsWidget::contactsFailed));
	}

	if (dialogsList) {
		TimeId lastDate = 0;
		PeerId lastPeer = 0;
		MsgId lastMsgId = 0;
		for (int i = dialogsList->size(); i > 0;) {
			auto &dialog = dialogsList->at(--i);
			if (dialog.type() != mtpc_dialog) {
				continue;
			}

			auto &dialogData = dialog.c_dialog();
			if (auto peer = peerFromMTP(dialogData.vpeer)) {
				auto history = App::history(peer);
				history->setPinnedDialog(dialogData.is_pinned());

				if (!lastDate) {
					if (!lastPeer) lastPeer = peer;
					if (auto msgId = dialogData.vtop_message.v) {
						if (!lastMsgId) lastMsgId = msgId;
						for (int j = messagesList->size(); j > 0;) {
							auto &message = messagesList->at(--j);
							if (idFromMessage(message) == msgId && peerFromMessage(message) == peer) {
								if (auto date = dateFromMessage(message)) {
									lastDate = date;
								}
								break;
							}
						}
					}
				}
			}
		}
		if (lastDate) {
			_dialogsOffsetDate = lastDate;
			_dialogsOffsetId = lastMsgId;
			_dialogsOffsetPeer = App::peer(lastPeer);
		} else {
			_dialogsFull = true;
		}

		t_assert(messagesList != nullptr);
		App::feedMsgs(*messagesList, NewMessageLast);

		unreadCountsReceived(*dialogsList);
		_inner->dialogsReceived(*dialogsList);
		onListScroll();
	} else {
		_dialogsFull = true;
	}

	_dialogsRequestId = 0;
	loadDialogs();

	AuthSession::Current().data().moreChatsLoaded().notify();
	if (_dialogsFull) {
		AuthSession::Current().data().allChatsLoaded().set(true);
	}
}

void DialogsWidget::pinnedDialogsReceived(const MTPmessages_PeerDialogs &dialogs, mtpRequestId requestId) {
	if (_pinnedDialogsRequestId != requestId) return;

	if (dialogs.type() == mtpc_messages_peerDialogs) {
		App::histories().clearPinned();

		auto &dialogsData = dialogs.c_messages_peerDialogs();
		App::feedUsers(dialogsData.vusers);
		App::feedChats(dialogsData.vchats);
		auto &list = dialogsData.vdialogs.v;
		for (auto i = list.size(); i > 0;) {
			auto &dialog = list[--i];
			if (dialog.type() != mtpc_dialog) {
				continue;
			}

			auto &dialogData = dialog.c_dialog();
			if (auto peer = peerFromMTP(dialogData.vpeer)) {
				auto history = App::history(peer);
				history->setPinnedDialog(dialogData.is_pinned());
			}
		}
		App::feedMsgs(dialogsData.vmessages, NewMessageLast);
		unreadCountsReceived(list);
		_inner->dialogsReceived(list);
		onListScroll();
	}

	_pinnedDialogsRequestId = 0;
	_pinnedDialogsReceived = true;

	AuthSession::Current().data().moreChatsLoaded().notify();
}

bool DialogsWidget::dialogsFailed(const RPCError &error, mtpRequestId requestId) {
	if (MTP::isDefaultHandledError(error)) return false;

	LOG(("RPC Error: %1 %2: %3").arg(error.code()).arg(error.type()).arg(error.description()));
	if (_dialogsRequestId == requestId) {
		_dialogsRequestId = 0;
	} else if (_pinnedDialogsRequestId == requestId) {
		_pinnedDialogsRequestId = 0;
	}
	return true;
}

void DialogsWidget::onDraggingScrollDelta(int delta) {
	_draggingScrollDelta = _scroll ? delta : 0;
	if (_draggingScrollDelta) {
		if (!_draggingScrollTimer) {
			_draggingScrollTimer.create(this);
			_draggingScrollTimer->setSingleShot(false);
			connect(_draggingScrollTimer, SIGNAL(timeout()), this, SLOT(onDraggingScrollTimer()));
		}
		_draggingScrollTimer->start(15);
	} else {
		_draggingScrollTimer.destroy();
	}
}

void DialogsWidget::onDraggingScrollTimer() {
	auto delta = (_draggingScrollDelta > 0) ? qMin(_draggingScrollDelta * 3 / 20 + 1, int32(MaxScrollSpeed)) : qMax(_draggingScrollDelta * 3 / 20 - 1, -int32(MaxScrollSpeed));
	_scroll->scrollToY(_scroll->scrollTop() + delta);
}

bool DialogsWidget::onSearchMessages(bool searchCache) {
	QString q = _filter->getLastText().trimmed();
	if (q.isEmpty()) {
		MTP::cancel(base::take(_searchRequest));
		MTP::cancel(base::take(_peerSearchRequest));
		return true;
	}
	if (searchCache) {
		SearchCache::const_iterator i = _searchCache.constFind(q);
		if (i != _searchCache.cend()) {
			_searchQuery = q;
			_searchFull = _searchFullMigrated = false;
			MTP::cancel(base::take(_searchRequest));
			searchReceived(_searchInPeer ? DialogsSearchPeerFromStart : DialogsSearchFromStart, i.value(), 0);
			return true;
		}
	} else if (_searchQuery != q) {
		_searchQuery = q;
		_searchFull = _searchFullMigrated = false;
		MTP::cancel(base::take(_searchRequest));
		if (_searchInPeer) {
			_searchRequest = MTP::send(MTPmessages_Search(MTP_flags(0), _searchInPeer->input, MTP_string(_searchQuery), MTP_inputUserEmpty(), MTP_inputMessagesFilterEmpty(), MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(SearchPerPage)), rpcDone(&DialogsWidget::searchReceived, DialogsSearchPeerFromStart), rpcFail(&DialogsWidget::searchFailed, DialogsSearchPeerFromStart));
		} else {
			_searchRequest = MTP::send(MTPmessages_SearchGlobal(MTP_string(_searchQuery), MTP_int(0), MTP_inputPeerEmpty(), MTP_int(0), MTP_int(SearchPerPage)), rpcDone(&DialogsWidget::searchReceived, DialogsSearchFromStart), rpcFail(&DialogsWidget::searchFailed, DialogsSearchFromStart));
		}
		_searchQueries.insert(_searchRequest, _searchQuery);
	}
	if (!_searchInPeer && q.size() >= MinUsernameLength) {
		if (searchCache) {
			auto i = _peerSearchCache.constFind(q);
			if (i != _peerSearchCache.cend()) {
				_peerSearchQuery = q;
				_peerSearchRequest = 0;
				peerSearchReceived(i.value(), 0);
				return true;
			}
		} else if (_peerSearchQuery != q) {
			_peerSearchQuery = q;
			_peerSearchFull = false;
			_peerSearchRequest = MTP::send(MTPcontacts_Search(MTP_string(_peerSearchQuery), MTP_int(SearchPeopleLimit)), rpcDone(&DialogsWidget::peerSearchReceived), rpcFail(&DialogsWidget::peopleFailed));
			_peerSearchQueries.insert(_peerSearchRequest, _peerSearchQuery);
		}
	}
	return false;
}

void DialogsWidget::onNeedSearchMessages() {
	if (!onSearchMessages(true)) {
		_searchTimer.start(AutoSearchTimeout);
	}
}

void DialogsWidget::onChooseByDrag() {
	_inner->choosePeer();
}

void DialogsWidget::showMainMenu() {
	App::wnd()->showMainMenu();
}

void DialogsWidget::searchMessages(const QString &query, PeerData *inPeer) {
	if ((_filter->getLastText() != query) || (inPeer && inPeer != _searchInPeer && inPeer->migrateTo() != _searchInPeer)) {
		if (inPeer) {
			onCancelSearch();
			setSearchInPeer(inPeer);
		}
		_filter->setText(query);
		_filter->updatePlaceholder();
		onFilterUpdate(true);
		_searchTimer.stop();
		onSearchMessages();

		_inner->saveRecentHashtags(query);
	}
}

void DialogsWidget::onSearchMore() {
	if (!_searchRequest) {
		if (!_searchFull) {
			auto offsetDate = _inner->lastSearchDate();
			auto offsetPeer = _inner->lastSearchPeer();
			auto offsetId = _inner->lastSearchId();
			if (_searchInPeer) {
				_searchRequest = MTP::send(MTPmessages_Search(MTP_flags(0), _searchInPeer->input, MTP_string(_searchQuery), MTP_inputUserEmpty(), MTP_inputMessagesFilterEmpty(), MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(offsetId), MTP_int(SearchPerPage)), rpcDone(&DialogsWidget::searchReceived, offsetId ? DialogsSearchPeerFromOffset : DialogsSearchPeerFromStart), rpcFail(&DialogsWidget::searchFailed, offsetId ? DialogsSearchPeerFromOffset : DialogsSearchPeerFromStart));
			} else {
				_searchRequest = MTP::send(MTPmessages_SearchGlobal(MTP_string(_searchQuery), MTP_int(offsetDate), offsetPeer ? offsetPeer->input : MTP_inputPeerEmpty(), MTP_int(offsetId), MTP_int(SearchPerPage)), rpcDone(&DialogsWidget::searchReceived, offsetId ? DialogsSearchFromOffset : DialogsSearchFromStart), rpcFail(&DialogsWidget::searchFailed, offsetId ? DialogsSearchFromOffset : DialogsSearchFromStart));
			}
			if (!offsetId) {
				_searchQueries.insert(_searchRequest, _searchQuery);
			}
		} else if (_searchInMigrated && !_searchFullMigrated) {
			auto offsetMigratedId = _inner->lastSearchMigratedId();
			_searchRequest = MTP::send(MTPmessages_Search(MTP_flags(0), _searchInMigrated->input, MTP_string(_searchQuery), MTP_inputUserEmpty(), MTP_inputMessagesFilterEmpty(), MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(offsetMigratedId), MTP_int(SearchPerPage)), rpcDone(&DialogsWidget::searchReceived, offsetMigratedId ? DialogsSearchMigratedFromOffset : DialogsSearchMigratedFromStart), rpcFail(&DialogsWidget::searchFailed, offsetMigratedId ? DialogsSearchMigratedFromOffset : DialogsSearchMigratedFromStart));
		}
	}
}

void DialogsWidget::loadDialogs() {
	if (_dialogsRequestId) return;
	if (_dialogsFull) {
		_inner->addAllSavedPeers();
		return;
	}

	auto firstLoad = !_dialogsOffsetDate;
	auto loadCount = firstLoad ? DialogsFirstLoad : DialogsPerPage;
	auto flags = MTPmessages_GetDialogs::Flag::f_exclude_pinned;
	_dialogsRequestId = MTP::send(MTPmessages_GetDialogs(MTP_flags(flags), MTP_int(_dialogsOffsetDate), MTP_int(_dialogsOffsetId), _dialogsOffsetPeer ? _dialogsOffsetPeer->input : MTP_inputPeerEmpty(), MTP_int(loadCount)), rpcDone(&DialogsWidget::dialogsReceived), rpcFail(&DialogsWidget::dialogsFailed));
	if (!_pinnedDialogsReceived) {
		loadPinnedDialogs();
	}
}

void DialogsWidget::loadPinnedDialogs() {
	if (_pinnedDialogsRequestId) return;

	_pinnedDialogsReceived = false;
	_pinnedDialogsRequestId = MTP::send(MTPmessages_GetPinnedDialogs(), rpcDone(&DialogsWidget::pinnedDialogsReceived), rpcFail(&DialogsWidget::dialogsFailed));
}

void DialogsWidget::contactsReceived(const MTPcontacts_Contacts &result) {
	_contactsRequestId = 0;
	if (result.type() == mtpc_contacts_contacts) {
		auto &d = result.c_contacts_contacts();
		App::feedUsers(d.vusers);
		_inner->contactsReceived(d.vcontacts.v);
	}
	AuthSession::Current().data().contactsLoaded().set(true);
}

bool DialogsWidget::contactsFailed(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;
	_contactsRequestId = 0;
	return true;
}

void DialogsWidget::searchReceived(DialogsSearchRequestType type, const MTPmessages_Messages &result, mtpRequestId req) {
	if (_inner->state() == DialogsInner::FilteredState || _inner->state() == DialogsInner::SearchedState) {
		if (type == DialogsSearchFromStart || type == DialogsSearchPeerFromStart) {
			auto i = _searchQueries.find(req);
			if (i != _searchQueries.cend()) {
				_searchCache[i.value()] = result;
				_searchQueries.erase(i);
			}
		}
	}

	if (_searchRequest == req) {
		switch (result.type()) {
		case mtpc_messages_messages: {
			auto &d = result.c_messages_messages();
			App::feedUsers(d.vusers);
			App::feedChats(d.vchats);
			auto &msgs = d.vmessages.v;
			if (!_inner->searchReceived(msgs, type, msgs.size())) {
				if (type == DialogsSearchMigratedFromStart || type == DialogsSearchMigratedFromOffset) {
					_searchFullMigrated = true;
				} else {
					_searchFull = true;
				}
			}
		} break;

		case mtpc_messages_messagesSlice: {
			auto &d = result.c_messages_messagesSlice();
			App::feedUsers(d.vusers);
			App::feedChats(d.vchats);
			auto &msgs = d.vmessages.v;
			if (!_inner->searchReceived(msgs, type, d.vcount.v)) {
				if (type == DialogsSearchMigratedFromStart || type == DialogsSearchMigratedFromOffset) {
					_searchFullMigrated = true;
				} else {
					_searchFull = true;
				}
			}
		} break;

		case mtpc_messages_channelMessages: {
			auto &d = result.c_messages_channelMessages();
			if (_searchInPeer && _searchInPeer->isChannel()) {
				_searchInPeer->asChannel()->ptsReceived(d.vpts.v);
			} else {
				LOG(("API Error: received messages.channelMessages when no channel was passed! (DialogsWidget::searchReceived)"));
			}
			App::feedUsers(d.vusers);
			App::feedChats(d.vchats);
			auto &msgs = d.vmessages.v;
			if (!_inner->searchReceived(msgs, type, d.vcount.v)) {
				if (type == DialogsSearchMigratedFromStart || type == DialogsSearchMigratedFromOffset) {
					_searchFullMigrated = true;
				} else {
					_searchFull = true;
				}
			}
		} break;
		}

		_searchRequest = 0;
		onListScroll();
		update();
	}
}

void DialogsWidget::peerSearchReceived(const MTPcontacts_Found &result, mtpRequestId req) {
	auto q = _peerSearchQuery;
	if (_inner->state() == DialogsInner::FilteredState || _inner->state() == DialogsInner::SearchedState) {
		auto i = _peerSearchQueries.find(req);
		if (i != _peerSearchQueries.cend()) {
			q = i.value();
			_peerSearchCache[q] = result;
			_peerSearchQueries.erase(i);
		}
	}
	if (_peerSearchRequest == req) {
		switch (result.type()) {
		case mtpc_contacts_found: {
			auto &d = result.c_contacts_found();
			App::feedUsers(d.vusers);
			App::feedChats(d.vchats);
			_inner->peerSearchReceived(q, d.vresults.v);
		} break;
		}

		_peerSearchRequest = 0;
		onListScroll();
	}
}

bool DialogsWidget::searchFailed(DialogsSearchRequestType type, const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (_searchRequest == req) {
		_searchRequest = 0;
		if (type == DialogsSearchMigratedFromStart || type == DialogsSearchMigratedFromOffset) {
			_searchFullMigrated = true;
		} else {
			_searchFull = true;
		}
	}
	return true;
}

bool DialogsWidget::peopleFailed(const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (_peerSearchRequest == req) {
		_peerSearchRequest = 0;
		_peerSearchFull = true;
	}
	return true;
}

void DialogsWidget::dragEnterEvent(QDragEnterEvent *e) {
	if (App::main()->selectingPeer()) return;

	_dragInScroll = false;
	_dragForward = e->mimeData()->hasFormat(qsl("application/x-td-forward-selected"));
	if (!_dragForward) _dragForward = e->mimeData()->hasFormat(qsl("application/x-td-forward-pressed-link"));
	if (!_dragForward) _dragForward = e->mimeData()->hasFormat(qsl("application/x-td-forward-pressed"));
	if (_dragForward && Adaptive::OneColumn()) _dragForward = false;
	if (_dragForward) {
		e->setDropAction(Qt::CopyAction);
		e->accept();
		updateDragInScroll(_scroll->geometry().contains(e->pos()));
	} else if (App::main() && App::main()->getDragState(e->mimeData()) != DragStateNone) {
		e->setDropAction(Qt::CopyAction);
		e->accept();
	}
	_chooseByDragTimer.stop();
}

void DialogsWidget::dragMoveEvent(QDragMoveEvent *e) {
	if (_scroll->geometry().contains(e->pos())) {
		if (_dragForward) {
			updateDragInScroll(true);
		} else {
			_chooseByDragTimer.start(ChoosePeerByDragTimeout);
		}
		PeerData *p = _inner->updateFromParentDrag(mapToGlobal(e->pos()));
		if (p) {
			e->setDropAction(Qt::CopyAction);
		} else {
			e->setDropAction(Qt::IgnoreAction);
		}
	} else {
		if (_dragForward) updateDragInScroll(false);
		_inner->dragLeft();
		e->setDropAction(Qt::IgnoreAction);
	}
	e->accept();
}

void DialogsWidget::dragLeaveEvent(QDragLeaveEvent *e) {
	if (_dragForward) {
		updateDragInScroll(false);
	} else {
		_chooseByDragTimer.stop();
	}
	_inner->dragLeft();
	e->accept();
}

void DialogsWidget::updateDragInScroll(bool inScroll) {
	if (_dragInScroll != inScroll) {
		_dragInScroll = inScroll;
		if (_dragInScroll) {
			App::main()->showForwardLayer(SelectedItemSet());
		} else {
			App::main()->dialogsCancelled();
		}
	}
}

void DialogsWidget::dropEvent(QDropEvent *e) {
	_chooseByDragTimer.stop();
	if (_scroll->geometry().contains(e->pos())) {
		if (auto peer = _inner->updateFromParentDrag(mapToGlobal(e->pos()))) {
			e->acceptProposedAction();
			App::main()->onFilesOrForwardDrop(peer->id, e->mimeData());
		}
	}
}

void DialogsWidget::onListScroll() {
	auto scrollTop = _scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
}

void DialogsWidget::onFilterUpdate(bool force) {
	if (_a_show.animating() && !force) return;

	auto filterText = _filter->getLastText();
	_inner->onFilterUpdate(filterText, force);
	if (filterText.isEmpty()) {
		_searchCache.clear();
		_searchQueries.clear();
		_searchQuery = QString();
		_cancelSearch->hideAnimated();
	} else {
		_cancelSearch->showAnimated();
	}
	updateJumpToDateVisibility();

	if (filterText.size() < MinUsernameLength) {
		_peerSearchCache.clear();
		_peerSearchQueries.clear();
		_peerSearchQuery = QString();
	}
}

void DialogsWidget::searchInPeer(PeerData *peer) {
	onCancelSearch();
	setSearchInPeer(peer);
	onFilterUpdate(true);
}

void DialogsWidget::setSearchInPeer(PeerData *peer) {
	auto newSearchInPeer = peer ? (peer->migrateTo() ? peer->migrateTo() : peer) : nullptr;
	_searchInMigrated = newSearchInPeer ? newSearchInPeer->migrateFrom() : nullptr;
	if (newSearchInPeer != _searchInPeer) {
		_searchInPeer = newSearchInPeer;
		controller()->searchInPeerChanged().notify(_searchInPeer, true);
		updateJumpToDateVisibility();
	}
	_inner->searchInPeer(_searchInPeer);
}

void DialogsWidget::onFilterCursorMoved(int from, int to) {
	if (to < 0) to = _filter->cursorPosition();
	QString t = _filter->getLastText();
	QStringRef r;
	for (int start = to; start > 0;) {
		--start;
		if (t.size() <= start) break;
		if (t.at(start) == '#') {
			r = t.midRef(start, to - start);
			break;
		}
		if (!t.at(start).isLetterOrNumber() && t.at(start) != '_') break;
	}
	_inner->onHashtagFilterUpdate(r);
}

void DialogsWidget::onCompleteHashtag(QString tag) {
	QString t = _filter->getLastText(), r;
	int cur = _filter->cursorPosition();
	for (int start = cur; start > 0;) {
		--start;
		if (t.size() <= start) break;
		if (t.at(start) == '#') {
			if (cur == start + 1 || t.midRef(start + 1, cur - start - 1) == tag.midRef(0, cur - start - 1)) {
				for (; cur < t.size() && cur - start - 1 < tag.size(); ++cur) {
					if (t.at(cur) != tag.at(cur - start - 1)) break;
				}
				if (cur - start - 1 == tag.size() && cur < t.size() && t.at(cur) == ' ') ++cur;
				r = t.mid(0, start + 1) + tag + ' ' + t.mid(cur);
				_filter->setText(r);
				_filter->setCursorPosition(start + 1 + tag.size() + 1);
				onFilterUpdate(true);
				return;
			}
			break;
		}
		if (!t.at(start).isLetterOrNumber() && t.at(start) != '_') break;
	}
	_filter->setText(t.mid(0, cur) + '#' + tag + ' ' + t.mid(cur));
	_filter->setCursorPosition(cur + 1 + tag.size() + 1);
	onFilterUpdate(true);
}

void DialogsWidget::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void DialogsWidget::updateLockUnlockVisibility() {
	if (!_a_show.animating()) {
		_lockUnlock->setVisible(Global::LocalPasscode());
	}
	updateControlsGeometry();
}

void DialogsWidget::updateJumpToDateVisibility(bool fast) {
	if (_a_show.animating()) return;

	auto jumpToDateVisible = (_searchInPeer && _filter->getLastText().isEmpty());
	if (fast) {
		_jumpToDate->toggleFast(jumpToDateVisible);
	} else {
		_jumpToDate->toggleAnimated(jumpToDateVisible);
	}
}

void DialogsWidget::updateControlsGeometry() {
	auto filterAreaTop = 0;
	if (_forwardCancel) {
		_forwardCancel->moveToLeft(0, filterAreaTop);
		filterAreaTop += st::dialogsForwardHeight;
	}
	auto smallLayoutWidth = (st::dialogsPadding.x() + st::dialogsPhotoSize + st::dialogsPadding.x());
	auto smallLayoutRatio = (width() < st::dialogsWidthMin) ? (st::dialogsWidthMin - width()) / float64(st::dialogsWidthMin - smallLayoutWidth) : 0.;
	auto filterLeft = st::dialogsFilterPadding.x() + _mainMenuToggle->width() + st::dialogsFilterPadding.x();
	auto filterRight = (Global::LocalPasscode() ? (st::dialogsFilterPadding.x() + _lockUnlock->width()) : st::dialogsFilterSkip) + st::dialogsFilterPadding.x();
	auto filterWidth = qMax(width(), st::dialogsWidthMin) - filterLeft - filterRight;
	auto filterAreaHeight = st::dialogsFilterPadding.y() + _mainMenuToggle->height() + st::dialogsFilterPadding.y();
	auto filterTop = filterAreaTop + (filterAreaHeight - _filter->height()) / 2;
	filterLeft = anim::interpolate(filterLeft, smallLayoutWidth, smallLayoutRatio);
	_filter->setGeometryToLeft(filterLeft, filterTop, filterWidth, _filter->height());
	auto mainMenuLeft = anim::interpolate(st::dialogsFilterPadding.x(), (smallLayoutWidth - _mainMenuToggle->width()) / 2, smallLayoutRatio);
	_mainMenuToggle->moveToLeft(mainMenuLeft, filterAreaTop + st::dialogsFilterPadding.y());
	_lockUnlock->moveToLeft(filterLeft + filterWidth + st::dialogsFilterPadding.x(), filterAreaTop + st::dialogsFilterPadding.y());
	_cancelSearch->moveToLeft(filterLeft + filterWidth - _cancelSearch->width(), _filter->y());
	_jumpToDate->moveToLeft(filterLeft + filterWidth - _jumpToDate->width(), _filter->y());

	auto scrollTop = filterAreaTop + filterAreaHeight;
	auto addToScroll = App::main() ? App::main()->contentScrollAddToY() : 0;
	auto newScrollTop = _scroll->scrollTop() + addToScroll;
	auto scrollHeight = height() - scrollTop;
	if (_updateTelegram) {
		auto updateHeight = _updateTelegram->height();
		_updateTelegram->setGeometry(0, height() - updateHeight, width(), updateHeight);
		scrollHeight -= updateHeight;
	}
	auto wasScrollHeight = _scroll->height();
	_scroll->setGeometry(0, scrollTop, width(), scrollHeight);
	if (scrollHeight != wasScrollHeight) {
		controller()->floatPlayerAreaUpdated().notify(true);
	}
	if (addToScroll) {
		_scroll->scrollToY(newScrollTop);
	} else {
		onListScroll();
	}
}

void DialogsWidget::updateForwardBar() {
	auto selecting = App::main()->selectingPeer();
	auto oneColumnSelecting = (Adaptive::OneColumn() && selecting);
	if (!oneColumnSelecting == !_forwardCancel) {
		return;
	}
	if (oneColumnSelecting) {
		_forwardCancel.create(this, st::dialogsForwardCancel);
		_forwardCancel->setClickedCallback([] { Global::RefPeerChooseCancel().notify(true); });
		if (!_a_show.animating()) _forwardCancel->show();
	} else {
		_forwardCancel.destroyDelayed();
	}
	updateControlsGeometry();
	update();
}

void DialogsWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		e->ignore();
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		if (!_inner->choosePeer()) {
			if (_inner->state() == DialogsInner::DefaultState || _inner->state() == DialogsInner::SearchedState || (_inner->state() == DialogsInner::FilteredState && _inner->hasFilteredResults())) {
				_inner->selectSkip(1);
				_inner->choosePeer();
			} else {
				onSearchMessages();
			}
		}
	} else if (e->key() == Qt::Key_Down) {
		_inner->setMouseSelection(false);
		_inner->selectSkip(1);
	} else if (e->key() == Qt::Key_Up) {
		_inner->setMouseSelection(false);
		_inner->selectSkip(-1);
	} else if (e->key() == Qt::Key_PageDown) {
		_inner->setMouseSelection(false);
		_inner->selectSkipPage(_scroll->height(), 1);
	} else if (e->key() == Qt::Key_PageUp) {
		_inner->setMouseSelection(false);
		_inner->selectSkipPage(_scroll->height(), -1);
	} else {
		e->ignore();
	}
}

void DialogsWidget::paintEvent(QPaintEvent *e) {
	if (App::wnd() && App::wnd()->contentOverlapped(this, e)) return;

	Painter p(this);
	QRect r(e->rect());
	if (r != rect()) {
		p.setClipRect(r);
	}
	auto progress = _a_show.current(getms(), 1.);
	if (_a_show.animating()) {
		auto retina = cIntRetinaFactor();
		auto fromLeft = (_showDirection == Window::SlideDirection::FromLeft);
		auto coordUnder = fromLeft ? anim::interpolate(-st::slideShift, 0, progress) : anim::interpolate(0, -st::slideShift, progress);
		auto coordOver = fromLeft ? anim::interpolate(0, width(), progress) : anim::interpolate(width(), 0, progress);
		auto shadow = fromLeft ? (1. - progress) : progress;
		if (coordOver > 0) {
			p.drawPixmap(QRect(0, 0, coordOver, _cacheUnder.height() / retina), _cacheUnder, QRect(-coordUnder * retina, 0, coordOver * retina, _cacheUnder.height()));
			p.setOpacity(shadow);
			p.fillRect(0, 0, coordOver, _cacheUnder.height() / retina, st::slideFadeOutBg);
			p.setOpacity(1);
		}
		p.drawPixmap(QRect(coordOver, 0, _cacheOver.width() / retina, _cacheOver.height() / retina), _cacheOver, QRect(0, 0, _cacheOver.width(), _cacheOver.height()));
		p.setOpacity(shadow);
		st::slideShadow.fill(p, QRect(coordOver - st::slideShadow.width(), 0, st::slideShadow.width(), _cacheOver.height() / retina));
		return;
	}
	auto aboveTop = 0;
	if (_forwardCancel) {
		p.fillRect(0, aboveTop, width(), st::dialogsForwardHeight, st::dialogsForwardBg);
		p.setPen(st::dialogsForwardFg);
		p.setFont(st::dialogsForwardFont);
		p.drawTextLeft(st::dialogsForwardTextLeft, st::dialogsForwardTextTop, width(), lang(lng_forward_choose));
		aboveTop += st::dialogsForwardHeight;
	}
	auto above = QRect(0, aboveTop, width(), _scroll->y() - aboveTop);
	if (above.intersects(r)) {
		p.fillRect(above.intersected(r), st::dialogsBg);
	}

	auto belowTop = _scroll->y() + qMin(_scroll->height(), _inner->height());
	if (!_widthAnimationCache.isNull()) {
		p.drawPixmapLeft(0, _scroll->y(), width(), _widthAnimationCache);
		belowTop = _scroll->y() + (_widthAnimationCache.height() / cIntRetinaFactor());
	}

	auto below = QRect(0, belowTop, width(), height() - belowTop);
	if (below.intersects(r)) {
		p.fillRect(below.intersected(r), st::dialogsBg);
	}
}

void DialogsWidget::destroyData() {
	_inner->destroyData();
}

void DialogsWidget::peerBefore(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) const {
	return _inner->peerBefore(inPeer, inMsg, outPeer, outMsg);
}

void DialogsWidget::peerAfter(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) const {
	return _inner->peerAfter(inPeer, inMsg, outPeer, outMsg);
}

void DialogsWidget::scrollToPeer(const PeerId &peer, MsgId msgId) {
	_inner->scrollToPeer(peer, msgId);
}

void DialogsWidget::removeDialog(History *history) {
	_inner->removeDialog(history);
	onFilterUpdate();
}

Dialogs::IndexedList *DialogsWidget::contactsList() {
	return _inner->contactsList();
}

Dialogs::IndexedList *DialogsWidget::dialogsList() {
	return _inner->dialogsList();
}

Dialogs::IndexedList *DialogsWidget::contactsNoDialogsList() {
	return _inner->contactsNoDialogsList();
}

bool DialogsWidget::onCancelSearch() {
	bool clearing = !_filter->getLastText().isEmpty();
	if (_searchRequest) {
		MTP::cancel(_searchRequest);
		_searchRequest = 0;
	}
	if (_searchInPeer && !clearing) {
		if (Adaptive::OneColumn()) {
			Ui::showPeerHistory(_searchInPeer, ShowAtUnreadMsgId);
		}
		setSearchInPeer(nullptr);
		clearing = true;
	}
	_inner->clearFilter();
	_filter->clear();
	_filter->updatePlaceholder();
	onFilterUpdate();
	return clearing;
}

void DialogsWidget::onCancelSearchInPeer() {
	if (_searchRequest) {
		MTP::cancel(_searchRequest);
		_searchRequest = 0;
	}
	if (_searchInPeer) {
		if (Adaptive::OneColumn() && !App::main()->selectingPeer()) {
			Ui::showPeerHistory(_searchInPeer, ShowAtUnreadMsgId);
		}
		setSearchInPeer(nullptr);
	}
	_inner->clearFilter();
	_filter->clear();
	_filter->updatePlaceholder();
	onFilterUpdate();
	if (!Adaptive::OneColumn() && !App::main()->selectingPeer()) {
		emit cancelled();
	}
}

void DialogsWidget::onDialogMoved(int movedFrom, int movedTo) {
	int32 st = _scroll->scrollTop();
	if (st > movedTo && st < movedFrom) {
		_scroll->scrollToY(st + st::dialogsRowHeight);
	}
}
