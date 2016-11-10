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
#include "boxes/contactsbox.h"

#include "dialogs/dialogs_indexed_list.h"
#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"
#include "styles/style_history.h"
#include "lang.h"
#include "boxes/addcontactbox.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "application.h"
#include "ui/buttons/checkbox.h"
#include "ui/buttons/icon_button.h"
#include "ui/filedialog.h"
#include "ui/widgets/multi_select.h"
#include "ui/effects/widget_slide_wrap.h"
#include "boxes/photocropbox.h"
#include "boxes/confirmbox.h"
#include "observer_peer.h"
#include "apiwrap.h"

QString cantInviteError() {
	return lng_cant_invite_not_contact(lt_more_info, textcmdLink(qsl("https://telegram.me/spambot"), lang(lng_cant_more_info)));
}

ContactsBox::ContactsBox() : ItemListBox(st::contactsScroll)
, _inner(this, CreatingGroupNone)
, _select(this, new Ui::MultiSelect(this, st::contactsMultiSelect, lang(lng_participant_filter)), QMargins(0, 0, 0, 0), [this] { updateScrollSkips(); })
, _next(this, lang(lng_create_group_next), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, _topShadow(this) {
	init();
}

ContactsBox::ContactsBox(const QString &name, const QImage &photo) : ItemListBox(st::boxScroll)
, _inner(this, CreatingGroupGroup)
, _select(this, new Ui::MultiSelect(this, st::contactsMultiSelect, lang(lng_participant_filter)), QMargins(0, 0, 0, 0), [this] { updateScrollSkips(); })
, _next(this, lang(lng_create_group_create), st::defaultBoxButton)
, _cancel(this, lang(lng_create_group_back), st::cancelBoxButton)
, _topShadow(this)
, _creationName(name)
, _creationPhoto(photo) {
	init();
}

ContactsBox::ContactsBox(ChannelData *channel) : ItemListBox(st::boxScroll)
, _inner(this, channel, MembersFilter::Recent, MembersAlreadyIn())
, _select(this, new Ui::MultiSelect(this, st::contactsMultiSelect, lang(lng_participant_filter)), QMargins(0, 0, 0, 0), [this] { updateScrollSkips(); })
, _next(this, lang(lng_participant_invite), st::defaultBoxButton)
, _cancel(this, lang(lng_create_group_skip), st::cancelBoxButton)
, _topShadow(this) {
	init();
}

ContactsBox::ContactsBox(ChannelData *channel, MembersFilter filter, const MembersAlreadyIn &already) : ItemListBox((filter == MembersFilter::Admins) ? st::contactsScroll : st::boxScroll)
, _inner(this, channel, filter, already)
, _select(this, new Ui::MultiSelect(this, st::contactsMultiSelect, lang(lng_participant_filter)), QMargins(0, 0, 0, 0), [this] { updateScrollSkips(); })
, _next(this, lang(lng_participant_invite), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, _topShadow(this) {
	init();
}

ContactsBox::ContactsBox(ChatData *chat, MembersFilter filter) : ItemListBox(st::boxScroll)
, _inner(this, chat, filter)
, _select(this, new Ui::MultiSelect(this, st::contactsMultiSelect, lang(lng_participant_filter)), QMargins(0, 0, 0, 0), [this] { updateScrollSkips(); })
, _next(this, lang((filter == MembersFilter::Admins) ? lng_settings_save : lng_participant_invite), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, _topShadow(this) {
	init();
}

ContactsBox::ContactsBox(UserData *bot) : ItemListBox(st::contactsScroll)
, _inner(this, bot)
, _select(this, new Ui::MultiSelect(this, st::contactsMultiSelect, lang(lng_participant_filter)), QMargins(0, 0, 0, 0), [this] { updateScrollSkips(); })
, _next(this, lang(lng_create_group_next), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, _topShadow(this) {
	init();
}

void ContactsBox::init() {
	_select->resizeToWidth(st::boxWideWidth);
	myEnsureResized(_select);

	auto inviting = (_inner->creating() == CreatingGroupGroup) || (_inner->channel() && _inner->membersFilter() == MembersFilter::Recent) || _inner->chat();
	auto topSkip = getTopScrollSkip();
	auto bottomSkip = inviting ? (st::boxButtonPadding.top() + _next->height() + st::boxButtonPadding.bottom()) : st::boxScrollSkip;
	ItemListBox::init(_inner, bottomSkip, topSkip);

	if (_inner->creating() == CreatingGroupNone && !_inner->chat() && !_inner->channel() && !_inner->bot()) {
		_add.create(this, st::contactsAdd);
		_add->setClickedCallback([] {
			App::wnd()->onShowAddContact();
		});
	}

	_inner->setPeerSelectedChangedCallback([this](PeerData *peer, bool checked) {
		onPeerSelectedChanged(peer, checked);
	});
	for (auto i : _inner->selected()) {
		addPeerToMultiSelect(i, true);
	}
	_inner->setAllAdminsChangedCallback([this] {
		if (_inner->allAdmins()) {
			_select->entity()->clearQuery();
			_select->slideUp();
			_inner->setFocus();
		} else {
			_select->slideDown();
			_select->entity()->setInnerFocus();
		}
		updateScrollSkips();
	});

	if (_inner->channel() && _inner->membersFilter() == MembersFilter::Admins) {
		_next->hide();
		_cancel->hide();
	} else if (_inner->chat() && _inner->membersFilter() == MembersFilter::Admins) {
		connect(_next, SIGNAL(clicked()), this, SLOT(onSaveAdmins()));
		_bottomShadow.create(this);
	} else if (_inner->chat() || _inner->channel()) {
		connect(_next, SIGNAL(clicked()), this, SLOT(onInvite()));
		_bottomShadow.create(this);
	} else if (_inner->creating() != CreatingGroupNone) {
		connect(_next, SIGNAL(clicked()), this, SLOT(onCreate()));
		_bottomShadow.create(this);
	} else {
		_next->hide();
		_cancel->hide();
	}
	connect(_cancel, SIGNAL(clicked()), this, SLOT(onClose()));
	connect(scrollArea(), SIGNAL(scrolled()), this, SLOT(onScroll()));
	_select->entity()->setQueryChangedCallback([this](const QString &query) { onFilterUpdate(query); });
	_select->entity()->setItemRemovedCallback([this](uint64 itemId) {
		if (auto peer = App::peerLoaded(itemId)) {
			_inner->peerUnselected(peer);
			update();
		}
	});
	_select->entity()->setSubmittedCallback([this](bool) { onSubmit(); });
	connect(_inner, SIGNAL(mustScrollTo(int, int)), scrollArea(), SLOT(scrollToY(int, int)));
	connect(_inner, SIGNAL(searchByUsername()), this, SLOT(onNeedSearchByUsername()));
	connect(_inner, SIGNAL(adminAdded()), this, SIGNAL(adminAdded()));

	_searchTimer.setSingleShot(true);
	connect(&_searchTimer, SIGNAL(timeout()), this, SLOT(onSearchByUsername()));

	prepare();
}

bool ContactsBox::onSearchByUsername(bool searchCache) {
	auto q = _select->entity()->getQuery();
	if (q.isEmpty()) {
		if (_peopleRequest) {
			_peopleRequest = 0;
		}
		return true;
	}
	if (q.size() >= MinUsernameLength) {
		if (searchCache) {
			PeopleCache::const_iterator i = _peopleCache.constFind(q);
			if (i != _peopleCache.cend()) {
				_peopleQuery = q;
				_peopleRequest = 0;
				peopleReceived(i.value(), 0);
				return true;
			}
		} else if (_peopleQuery != q) {
			_peopleQuery = q;
			_peopleFull = false;
			_peopleRequest = MTP::send(MTPcontacts_Search(MTP_string(_peopleQuery), MTP_int(SearchPeopleLimit)), rpcDone(&ContactsBox::peopleReceived), rpcFail(&ContactsBox::peopleFailed));
			_peopleQueries.insert(_peopleRequest, _peopleQuery);
		}
	}
	return false;
}

void ContactsBox::onNeedSearchByUsername() {
	if (!onSearchByUsername(true)) {
		_searchTimer.start(AutoSearchTimeout);
	}
}

void ContactsBox::peopleReceived(const MTPcontacts_Found &result, mtpRequestId req) {
	QString q = _peopleQuery;

	PeopleQueries::iterator i = _peopleQueries.find(req);
	if (i != _peopleQueries.cend()) {
		q = i.value();
		_peopleCache[q] = result;
		_peopleQueries.erase(i);
	}

	if (_peopleRequest == req) {
		switch (result.type()) {
		case mtpc_contacts_found: {
			App::feedUsers(result.c_contacts_found().vusers);
			App::feedChats(result.c_contacts_found().vchats);
			_inner->peopleReceived(q, result.c_contacts_found().vresults.c_vector().v);
		} break;
		}

		_peopleRequest = 0;
		_inner->updateSelection();
		onScroll();
	}
}

bool ContactsBox::peopleFailed(const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (_peopleRequest == req) {
		_peopleRequest = 0;
		_peopleFull = true;
	}
	return true;
}

void ContactsBox::showAll() {
	if (_inner->chat() && _inner->membersFilter() == MembersFilter::Admins && _inner->allAdmins()) {
		_select->hideFast();
	} else {
		_select->showFast();
	}
	if (_inner->channel() && _inner->membersFilter() == MembersFilter::Admins) {
		_next->hide();
		_cancel->hide();
	} else if (_inner->chat() || _inner->channel()) {
		_next->show();
		_cancel->show();
	} else if (_inner->creating() != CreatingGroupNone) {
		_next->show();
		_cancel->show();
	} else {
		_next->hide();
		_cancel->hide();
	}
	_topShadow->show();
	if (_bottomShadow) _bottomShadow->show();
	ItemListBox::showAll();
}

void ContactsBox::doSetInnerFocus() {
	if (_select->isHidden()) {
		_inner->setFocus();
	} else {
		_select->entity()->setInnerFocus();
	}
}

void ContactsBox::onSubmit() {
	_inner->chooseParticipant();
}

void ContactsBox::keyPressEvent(QKeyEvent *e) {
	auto focused = focusWidget();
	if (_select == focused || _select->isAncestorOf(focusWidget())) {
		if (e->key() == Qt::Key_Down) {
			_inner->selectSkip(1);
		} else if (e->key() == Qt::Key_Up) {
			_inner->selectSkip(-1);
		} else if (e->key() == Qt::Key_PageDown) {
			_inner->selectSkipPage(scrollArea()->height(), 1);
		} else if (e->key() == Qt::Key_PageUp) {
			_inner->selectSkipPage(scrollArea()->height(), -1);
		} else {
			ItemListBox::keyPressEvent(e);
		}
	} else {
		ItemListBox::keyPressEvent(e);
	}
}

void ContactsBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	bool addingAdmin = _inner->channel() && _inner->membersFilter() == MembersFilter::Admins;
	if (_inner->chat() && _inner->membersFilter() == MembersFilter::Admins) {
		paintTitle(p, lang(lng_channel_admins));
	} else if (_inner->chat() || _inner->creating() != CreatingGroupNone) {
		QString title(lang(addingAdmin ? lng_channel_add_admin : lng_profile_add_participant));
		QString additional((addingAdmin || (_inner->channel() && !_inner->channel()->isMegagroup())) ? QString() : QString("%1 / %2").arg(_inner->selectedCount()).arg(Global::MegagroupSizeMax()));
		paintTitle(p, title, additional);
	} else if (_inner->sharingBotGame()) {
		paintTitle(p, lang(lng_bot_choose_chat));
	} else if (_inner->bot()) {
		paintTitle(p, lang(lng_bot_choose_group));
	} else {
		paintTitle(p, lang(lng_contacts_header));
	}
}

int ContactsBox::getTopScrollSkip() const {
	auto result = titleHeight();
	if (!_select->isHidden()) {
		result += _select->height();
	}
	return result;
}

void ContactsBox::updateScrollSkips() {
	auto oldScrollHeight = scrollArea()->height();
	auto inviting = (_inner->creating() == CreatingGroupGroup) || (_inner->channel() && _inner->membersFilter() == MembersFilter::Recent) || _inner->chat();
	auto topSkip = getTopScrollSkip();
	auto bottomSkip = inviting ? (st::boxButtonPadding.top() + _next->height() + st::boxButtonPadding.bottom()) : st::boxScrollSkip;
	setScrollSkips(bottomSkip, topSkip);
	auto scrollHeightDelta = scrollArea()->height() - oldScrollHeight;
	if (scrollHeightDelta) {
		scrollArea()->scrollToY(scrollArea()->scrollTop() - scrollHeightDelta);
	}

	_topShadow->setGeometry(0, topSkip, width(), st::lineWidth);
}

void ContactsBox::resizeEvent(QResizeEvent *e) {
	ItemListBox::resizeEvent(e);

	_select->resizeToWidth(width());
	_select->moveToLeft(0, titleHeight());

	updateScrollSkips();

	if (_add) {
		_add->moveToRight(st::contactsAddPosition.x(), height() - st::contactsAddPosition.y() - _add->height());
	}

	_inner->resize(width(), _inner->height());
	_next->moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _next->height());
	_cancel->moveToRight(st::boxButtonPadding.right() + _next->width() + st::boxButtonPadding.left(), _next->y());
	if (_bottomShadow) _bottomShadow->setGeometry(0, height() - st::boxButtonPadding.bottom() - _next->height() - st::boxButtonPadding.top() - st::lineWidth, width(), st::lineWidth);
}

void ContactsBox::closePressed() {
	if (_inner->channel() && !_inner->hasAlreadyMembersInChannel()) {
		Ui::showPeerHistory(_inner->channel(), ShowAtTheEndMsgId);
	}
}

void ContactsBox::onFilterUpdate(const QString &filter) {
	scrollArea()->scrollToY(0);
	_inner->updateFilter(filter);
}

void ContactsBox::addPeerToMultiSelect(PeerData *peer, bool skipAnimation) {
	using AddItemWay = Ui::MultiSelect::AddItemWay;
	auto addItemWay = skipAnimation ? AddItemWay::SkipAnimation : AddItemWay::Default;
	_select->entity()->addItem(peer->id, peer->shortName(), st::activeButtonBg, PaintUserpicCallback(peer), addItemWay);
}

void ContactsBox::onPeerSelectedChanged(PeerData *peer, bool checked) {
	if (checked) {
		addPeerToMultiSelect(peer);
		_select->entity()->clearQuery();
	} else {
		_select->entity()->removeItem(peer->id);
	}
	update();
}

void ContactsBox::onInvite() {
	QVector<UserData*> users(_inner->selected());
	if (users.isEmpty()) {
		_select->entity()->setInnerFocus();
		return;
	}

	App::main()->addParticipants(_inner->chat() ? (PeerData*)_inner->chat() : _inner->channel(), users);
	if (_inner->chat()) {
		Ui::hideLayer();
		Ui::showPeerHistory(_inner->chat(), ShowAtTheEndMsgId);
	} else {
		onClose();
	}
}

void ContactsBox::onCreate() {
	if (_saveRequestId) return;

	auto users = _inner->selectedInputs();
	if (users.isEmpty() || (users.size() == 1 && users.at(0).type() == mtpc_inputUserSelf)) {
		_select->entity()->setInnerFocus();
		return;
	}
	_saveRequestId = MTP::send(MTPmessages_CreateChat(MTP_vector<MTPInputUser>(users), MTP_string(_creationName)), rpcDone(&ContactsBox::creationDone), rpcFail(&ContactsBox::creationFail));
}

void ContactsBox::onSaveAdmins() {
	if (_saveRequestId) return;

	_inner->saving(true);
	_saveRequestId = MTP::send(MTPmessages_ToggleChatAdmins(_inner->chat()->inputChat, MTP_bool(!_inner->allAdmins())), rpcDone(&ContactsBox::saveAdminsDone), rpcFail(&ContactsBox::saveAdminsFail));
}

void ContactsBox::saveAdminsDone(const MTPUpdates &result) {
	App::main()->sentUpdatesReceived(result);
	saveSelectedAdmins();
}

void ContactsBox::saveSelectedAdmins() {
	if (_inner->allAdmins() && !_inner->chat()->participants.isEmpty()) {
		onClose();
	} else {
		_saveRequestId = MTP::send(MTPmessages_GetFullChat(_inner->chat()->inputChat), rpcDone(&ContactsBox::getAdminsDone), rpcFail(&ContactsBox::saveAdminsFail));
	}
}

void ContactsBox::getAdminsDone(const MTPmessages_ChatFull &result) {
	App::api()->processFullPeer(_inner->chat(), result);
	if (_inner->allAdmins()) {
		onClose();
		return;
	}
	ChatData::Admins curadmins = _inner->chat()->admins;
	QVector<UserData*> newadmins = _inner->selected(), appoint;
	if (!newadmins.isEmpty()) {
		appoint.reserve(newadmins.size());
		for (int32 i = 0, l = newadmins.size(); i < l; ++i) {
			ChatData::Admins::iterator c = curadmins.find(newadmins.at(i));
			if (c == curadmins.cend()) {
				if (newadmins.at(i)->id != peerFromUser(_inner->chat()->creator)) {
					appoint.push_back(newadmins.at(i));
				}
			} else {
				curadmins.erase(c);
			}
		}
	}
	_saveRequestId = 0;

	for_const (UserData *user, curadmins) {
		MTP::send(MTPmessages_EditChatAdmin(_inner->chat()->inputChat, user->inputUser, MTP_boolFalse()), rpcDone(&ContactsBox::removeAdminDone, user), rpcFail(&ContactsBox::editAdminFail), 0, 10);
	}
	for_const (UserData *user, appoint) {
		MTP::send(MTPmessages_EditChatAdmin(_inner->chat()->inputChat, user->inputUser, MTP_boolTrue()), rpcDone(&ContactsBox::setAdminDone, user), rpcFail(&ContactsBox::editAdminFail), 0, 10);
	}
	MTP::sendAnything();

	_saveRequestId = curadmins.size() + appoint.size();
	if (!_saveRequestId) {
		onClose();
	}
}

void ContactsBox::setAdminDone(UserData *user, const MTPBool &result) {
	if (mtpIsTrue(result)) {
		if (_inner->chat()->noParticipantInfo()) {
			App::api()->requestFullPeer(_inner->chat());
		} else {
			_inner->chat()->admins.insert(user);
		}
	}
	--_saveRequestId;
	if (!_saveRequestId) {
		emit App::main()->peerUpdated(_inner->chat());
		onClose();
	}
}

void ContactsBox::removeAdminDone(UserData *user, const MTPBool &result) {
	if (mtpIsTrue(result)) {
		_inner->chat()->admins.remove(user);
	}
	--_saveRequestId;
	if (!_saveRequestId) {
		emit App::main()->peerUpdated(_inner->chat());
		onClose();
	}
}

bool ContactsBox::saveAdminsFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return true;
	_saveRequestId = 0;
	_inner->saving(false);
	if (error.type() == qstr("CHAT_NOT_MODIFIED")) {
		saveSelectedAdmins();
	}
	return false;
}

bool ContactsBox::editAdminFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return true;
	--_saveRequestId;
	_inner->chat()->invalidateParticipants();
	if (!_saveRequestId) {
		if (error.type() == qstr("USER_RESTRICTED")) {
			Ui::showLayer(new InformBox(lang(lng_cant_do_this)));
			return true;
		}
		onClose();
	}
	return false;
}

void ContactsBox::onScroll() {
	_inner->loadProfilePhotos(scrollArea()->scrollTop());
}

void ContactsBox::creationDone(const MTPUpdates &updates) {
	Ui::hideLayer();

	App::main()->sentUpdatesReceived(updates);
	const QVector<MTPChat> *v = 0;
	switch (updates.type()) {
	case mtpc_updates: v = &updates.c_updates().vchats.c_vector().v; break;
	case mtpc_updatesCombined: v = &updates.c_updatesCombined().vchats.c_vector().v; break;
	default: LOG(("API Error: unexpected update cons %1 (ContactsBox::creationDone)").arg(updates.type())); break;
	}

	PeerData *peer = 0;
	if (v && !v->isEmpty() && v->front().type() == mtpc_chat) {
		peer = App::chat(v->front().c_chat().vid.v);
		if (peer) {
			if (!_creationPhoto.isNull()) {
				App::app()->uploadProfilePhoto(_creationPhoto, peer->id);
			}
			Ui::showPeerHistory(peer, ShowAtUnreadMsgId);
		}
	} else {
		LOG(("API Error: chat not found in updates (ContactsBox::creationDone)"));
	}
}

bool ContactsBox::creationFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_saveRequestId = 0;
	if (error.type() == "NO_CHAT_TITLE") {
		onClose();
		return true;
	} else if (error.type() == "USERS_TOO_FEW") {
		_select->entity()->setInnerFocus();
		return true;
	} else if (error.type() == "PEER_FLOOD") {
		Ui::showLayer(new InformBox(cantInviteError()), KeepOtherLayers);
		return true;
	} else if (error.type() == qstr("USER_RESTRICTED")) {
		Ui::showLayer(new InformBox(lang(lng_cant_do_this)));
		return true;
	}
	return false;
}

ContactsBox::Inner::ContactData::ContactData(PeerData *peer, base::lambda_wrap<void()> updateCallback)
: checkbox(std_::make_unique<Ui::RoundImageCheckbox>(st::contactsPhotoCheckbox, std_::move(updateCallback), PaintUserpicCallback(peer))) {
}

ContactsBox::Inner::Inner(QWidget *parent, CreatingGroupType creating) : TWidget(parent)
, _rowHeight(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom())
, _creating(creating)
, _allAdmins(this, lang(lng_chat_all_members_admins), false, st::contactsAdminCheckbox)
, _contacts(App::main()->contactsList())
, _addContactLnk(this, lang(lng_add_contact_button)) {
	init();
}

ContactsBox::Inner::Inner(QWidget *parent, ChannelData *channel, MembersFilter membersFilter, const MembersAlreadyIn &already) : TWidget(parent)
, _rowHeight(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom())
, _channel(channel)
, _membersFilter(membersFilter)
, _creating(CreatingGroupChannel)
, _already(already)
, _allAdmins(this, lang(lng_chat_all_members_admins), false, st::contactsAdminCheckbox)
, _contacts(App::main()->contactsList())
, _addContactLnk(this, lang(lng_add_contact_button)) {
	init();
}

namespace {
	bool _sortByName(UserData *a, UserData *b) {
		return a->name.compare(b->name, Qt::CaseInsensitive) < 0;
	}
}

ContactsBox::Inner::Inner(QWidget *parent, ChatData *chat, MembersFilter membersFilter) : TWidget(parent)
, _rowHeight(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom())
, _chat(chat)
, _membersFilter(membersFilter)
, _allAdmins(this, lang(lng_chat_all_members_admins), !_chat->adminsEnabled(), st::contactsAdminCheckbox)
, _aboutWidth(st::boxWideWidth - st::contactsPadding.left() - st::contactsPadding.right())
, _aboutAllAdmins(st::boxTextFont, lang(lng_chat_about_all_admins), _defaultOptions, _aboutWidth)
, _aboutAdmins(st::boxTextFont, lang(lng_chat_about_admins), _defaultOptions, _aboutWidth)
, _customList((membersFilter == MembersFilter::Recent) ? std_::unique_ptr<Dialogs::IndexedList>() : std_::make_unique<Dialogs::IndexedList>(Dialogs::SortMode::Add))
, _contacts((membersFilter == MembersFilter::Recent) ? App::main()->contactsList() : _customList.get())
, _addContactLnk(this, lang(lng_add_contact_button)) {
	initList();
	if (membersFilter == MembersFilter::Admins) {
		_aboutHeight = st::contactsAboutSkip + qMax(_aboutAllAdmins.countHeight(_aboutWidth), _aboutAdmins.countHeight(_aboutWidth)) + st::contactsAboutHeight;
		if (_contacts->isEmpty()) {
			App::api()->requestFullPeer(_chat);
		}
	}
	init();
}

template <typename FilterCallback>
void ContactsBox::Inner::addDialogsToList(FilterCallback callback) {
	auto v = App::main()->dialogsList();
	for_const (auto row, *v) {
		auto peer = row->history()->peer;
		if (callback(peer)) {
			_contacts->addToEnd(row->history());
		}
	}
}

ContactsBox::Inner::Inner(QWidget *parent, UserData *bot) : TWidget(parent)
, _rowHeight(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom())
, _bot(bot)
, _allAdmins(this, lang(lng_chat_all_members_admins), false, st::contactsAdminCheckbox)
, _customList(std_::make_unique<Dialogs::IndexedList>(Dialogs::SortMode::Add))
, _contacts(_customList.get())
, _addContactLnk(this, lang(lng_add_contact_button)) {
	if (sharingBotGame()) {
		addDialogsToList([](PeerData *peer) {
			if (peer->canWrite()) {
				if (auto channel = peer->asChannel()) {
					return !channel->isBroadcast();
				}
				return true;
			}
			return false;
		});
	} else {
		addDialogsToList([](PeerData *peer) {
			if (peer->isChat() && peer->asChat()->canEdit()) {
				return true;
			} else if (peer->isMegagroup() && (peer->asChannel()->amCreator() || peer->asChannel()->amEditor())) {
				return true;
			}
			return false;
		});
	}
	init();
}

void ContactsBox::Inner::init() {
	subscribe(FileDownload::ImageLoaded(), [this] { update(); });
	connect(_addContactLnk, SIGNAL(clicked()), App::wnd(), SLOT(onShowAddContact()));
	connect(_allAdmins, SIGNAL(changed()), this, SLOT(onAllAdminsChanged()));

	setAttribute(Qt::WA_OpaquePaintEvent);

	for_const (auto row, _contacts->all()) {
		row->attached = nullptr;
	}

	_filter = qsl("a");
	updateFilter();

	connect(App::main(), SIGNAL(dialogRowReplaced(Dialogs::Row*,Dialogs::Row*)), this, SLOT(onDialogRowReplaced(Dialogs::Row*,Dialogs::Row*)));
	connect(App::main(), SIGNAL(peerUpdated(PeerData*)), this, SLOT(peerUpdated(PeerData *)));
	connect(App::main(), SIGNAL(peerNameChanged(PeerData*,const PeerData::Names&,const PeerData::NameFirstChars&)), this, SLOT(onPeerNameChanged(PeerData*,const PeerData::Names&,const PeerData::NameFirstChars&)));
	connect(App::main(), SIGNAL(peerPhotoChanged(PeerData*)), this, SLOT(peerUpdated(PeerData*)));
}

void ContactsBox::Inner::initList() {
	if (!_chat || _membersFilter != MembersFilter::Admins) return;

	QList<UserData*> admins, others;
	admins.reserve(_chat->admins.size() + 1);
	if (!_chat->participants.isEmpty()) {
		others.reserve(_chat->participants.size());
	}

	for (ChatData::Participants::const_iterator i = _chat->participants.cbegin(), e = _chat->participants.cend(); i != e; ++i) {
		if (i.key()->id == peerFromUser(_chat->creator)) continue;
		if (!_allAdmins->checked() && _chat->admins.contains(i.key())) {
			admins.push_back(i.key());
			if (!_checkedContacts.contains(i.key())) {
				_checkedContacts.insert(i.key());
			}
		} else {
			others.push_back(i.key());
		}
	}
	std::sort(admins.begin(), admins.end(), _sortByName);
	std::sort(others.begin(), others.end(), _sortByName);
	if (UserData *creator = App::userLoaded(_chat->creator)) {
		if (_chat->participants.contains(creator)) {
			admins.push_front(creator);
		}
	}
	for (int32 i = 0, l = admins.size(); i < l; ++i) {
		_contacts->addToEnd(App::history(admins.at(i)->id));
	}
	for (int32 i = 0, l = others.size(); i < l; ++i) {
		_contacts->addToEnd(App::history(others.at(i)->id));
	}
}

void ContactsBox::Inner::onPeerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars) {
	if (bot()) {
		_contacts->peerNameChanged(peer, oldNames, oldChars);
	}
	peerUpdated(peer);
}

void ContactsBox::Inner::onAddBot() {
	if (auto &info = _bot->botInfo) {
		if (!info->shareGameShortName.isEmpty()) {
			MTPmessages_SendMedia::Flags sendFlags = 0;

			auto history = App::historyLoaded(_addToPeer);
			auto afterRequestId = history ? history->sendRequestId : 0;
			auto randomId = rand_value<uint64>();
			auto requestId = MTP::send(MTPmessages_SendMedia(MTP_flags(sendFlags), _addToPeer->input, MTP_int(0), MTP_inputMediaGame(MTP_inputGameShortName(_bot->inputUser, MTP_string(info->shareGameShortName))), MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, afterRequestId);
			if (history) {
				history->sendRequestId = requestId;
			}
		} else if (!info->startGroupToken.isEmpty()) {
			MTP::send(MTPmessages_StartBot(_bot->inputUser, _addToPeer->input, MTP_long(rand_value<uint64>()), MTP_string(info->startGroupToken)), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::addParticipantFail, _bot));
		} else {
			App::main()->addParticipants(_addToPeer, QVector<UserData*>(1, _bot));
		}
	} else {
		App::main()->addParticipants(_addToPeer, QVector<UserData*>(1, _bot));
	}
	Ui::hideLayer();
	Ui::showPeerHistory(_addToPeer, ShowAtUnreadMsgId);
}

void ContactsBox::Inner::onAddAdmin() {
	if (_addAdminRequestId) return;
	_addAdminRequestId = MTP::send(MTPchannels_EditAdmin(_channel->inputChannel, _addAdmin->inputUser, MTP_channelRoleEditor()), rpcDone(&Inner::addAdminDone), rpcFail(&Inner::addAdminFail));
}

void ContactsBox::Inner::onNoAddAdminBox(QObject *obj) {
	if (obj == _addAdminBox) {
		_addAdminBox = 0;
	}
}

void ContactsBox::Inner::onAllAdminsChanged() {
	if (_saving && _allAdmins->checked() != _allAdminsChecked) {
		_allAdmins->setChecked(_allAdminsChecked);
	} else if (_allAdminsChangedCallback) {
		_allAdminsChangedCallback();
	}
	update();
}

void ContactsBox::Inner::addAdminDone(const MTPUpdates &result, mtpRequestId req) {
	if (App::main()) App::main()->sentUpdatesReceived(result);
	if (req != _addAdminRequestId) return;

	_addAdminRequestId = 0;
	if (_addAdmin && _channel && _channel->isMegagroup()) {
		Notify::PeerUpdate update(_channel);
		if (_channel->mgInfo->lastParticipants.indexOf(_addAdmin) < 0) {
			_channel->mgInfo->lastParticipants.push_front(_addAdmin);
			update.flags |= Notify::PeerUpdate::Flag::MembersChanged;
		}
		_channel->mgInfo->lastAdmins.insert(_addAdmin);
		update.flags |= Notify::PeerUpdate::Flag::AdminsChanged;
		if (_addAdmin->botInfo) {
			_channel->mgInfo->bots.insert(_addAdmin);
			if (_channel->mgInfo->botStatus != 0 && _channel->mgInfo->botStatus < 2) {
				_channel->mgInfo->botStatus = 2;
			}
		}
		Notify::peerUpdatedDelayed(update);
	}
	if (_addAdminBox) _addAdminBox->onClose();
	emit adminAdded();
}

bool ContactsBox::Inner::addAdminFail(const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (req != _addAdminRequestId) return true;

	_addAdminRequestId = 0;
	if (_addAdminBox) _addAdminBox->onClose();
	if (error.type() == "USERS_TOO_MUCH") {
		Ui::showLayer(new MaxInviteBox(_channel->inviteLink()), KeepOtherLayers);
	} else if (error.type() == "ADMINS_TOO_MUCH") {
		Ui::showLayer(new InformBox(lang(lng_channel_admins_too_much)), KeepOtherLayers);
	} else if (error.type() == qstr("USER_RESTRICTED")) {
		Ui::showLayer(new InformBox(lang(lng_cant_do_this)), KeepOtherLayers);
	} else  {
		emit adminAdded();
	}
	return true;
}

void ContactsBox::Inner::saving(bool flag) {
	_saving = flag;
	_allAdminsChecked = _allAdmins->checked();
	update();
}

void ContactsBox::Inner::peerUpdated(PeerData *peer) {
	if (_chat && (!peer || peer == _chat)) {
		bool inited = false;
		if (_membersFilter == MembersFilter::Admins && _contacts->isEmpty() && !_chat->participants.isEmpty()) {
			initList();
			inited = true;
		}
		if (!_chat->canEdit()) {
			Ui::hideLayer();
		} else if (!_chat->participants.isEmpty()) {
			for (ContactsData::iterator i = _contactsData.begin(), e = _contactsData.end(); i != e; ++i) {
				delete i.value();
			}
			_contactsData.clear();
			for_const (auto row, _contacts->all()) {
				row->attached = nullptr;
			}
			if (!_filter.isEmpty()) {
				for (int32 j = 0, s = _filtered.size(); j < s; ++j) {
					_filtered[j]->attached = 0;
				}
			}
		}
		if (inited) {
			_filter += 'a';
			updateFilter(_lastQuery);
		}
		update();
	} else {
		ContactsData::iterator i = _contactsData.find(peer);
		if (i != _contactsData.cend()) {
			for_const (auto row, _contacts->all()) {
				if (row->attached == i.value()) {
					row->attached = nullptr;
					update(0, _aboutHeight + _rowHeight * row->pos(), width(), _rowHeight);
				}
			}
			if (!_filter.isEmpty()) {
				for (int32 j = 0, s = _filtered.size(); j < s; ++j) {
					if (_filtered[j]->attached == i.value()) {
						_filtered[j]->attached = 0;
						update(0, _rowHeight * j, width(), _rowHeight);
					}
				}
			}
			delete i.value();
			_contactsData.erase(i);
		}
	}
}

void ContactsBox::Inner::loadProfilePhotos(int32 yFrom) {
	if (!parentWidget()) return;
	int32 yTo = yFrom + parentWidget()->height() * 5;
	MTP::clearLoaderPriorities();

	if (yTo < 0) return;
	if (yFrom < 0) yFrom = 0;

	if (_filter.isEmpty()) {
		if (!_contacts->isEmpty()) {
			auto i = _contacts->cfind(yFrom - _aboutHeight, _rowHeight);
			for (auto end = _contacts->cend(); i != end; ++i) {
				if ((_aboutHeight + (*i)->pos() * _rowHeight) >= yTo) {
					break;
				}
				(*i)->history()->peer->loadUserpic();
			}
		}
	} else if (!_filtered.isEmpty()) {
		int32 from = yFrom / _rowHeight;
		if (from < 0) from = 0;
		if (from < _filtered.size()) {
			int32 to = (yTo / _rowHeight) + 1;
			if (to > _filtered.size()) to = _filtered.size();

			for (; from < to; ++from) {
				_filtered[from]->history()->peer->loadUserpic();
			}
		}
	}
}

ContactsBox::Inner::ContactData *ContactsBox::Inner::contactData(Dialogs::Row *row) {
	ContactData *data = (ContactData*)row->attached;
	if (!data) {
		PeerData *peer = row->history()->peer;
		ContactsData::const_iterator i = _contactsData.constFind(peer);
		if (i == _contactsData.cend()) {
			data = usingMultiSelect() ? new ContactData(peer, [this, peer] { updateRowWithPeer(peer); }) : new ContactData();
			_contactsData.insert(peer, data);
			if (peer->isUser()) {
				if (_chat) {
					if (_membersFilter == MembersFilter::Recent) {
						data->disabledChecked = _chat->participants.contains(peer->asUser());
					}
				} else if (_creating == CreatingGroupGroup) {
					data->disabledChecked = (peerToUser(peer->id) == MTP::authedId());
				} else if (_channel) {
					data->disabledChecked = (peerToUser(peer->id) == MTP::authedId()) || _already.contains(peer->asUser());
				}
			}
			if (usingMultiSelect() && _checkedContacts.contains(peer)) {
				data->checkbox->setChecked(true, Ui::RoundImageCheckbox::SetStyle::Fast);
			}
			data->name.setText(st::contactsNameFont, peer->name, _textNameOptions);
			if (peer->isUser()) {
				data->statusText = App::onlineText(peer->asUser(), _time);
				data->statusHasOnlineColor = App::onlineColorUse(peer->asUser(), _time);
			} else if (peer->isChat()) {
				ChatData *chat = peer->asChat();
				if (!chat->amIn()) {
					data->statusText = lang(lng_chat_status_unaccessible);
				} else {
					data->statusText = lng_chat_status_members(lt_count, chat->count);
				}
			} else if (peer->isMegagroup()) {
				data->statusText = lang(lng_group_status);
			} else if (peer->isChannel()) {
				data->statusText = lang(lng_channel_status);
			}
		} else {
			data = i.value();
		}
		row->attached = data;
	}
	return data;
}

void ContactsBox::Inner::paintDialog(Painter &p, uint64 ms, PeerData *peer, ContactData *data, bool sel) {
	UserData *user = peer->asUser();

	if (_chat && _membersFilter == MembersFilter::Admins) {
		if (_allAdmins->checked() || peer->id == peerFromUser(_chat->creator) || _saving) {
			sel = false;
		}
	} else {
		if (data->disabledChecked || selectedCount() >= Global::MegagroupSizeMax()) {
			sel = false;
		}
	}

	auto paintDisabledCheck = data->disabledChecked;
	if (_chat && _membersFilter == MembersFilter::Admins) {
		if (peer->id == peerFromUser(_chat->creator) || _allAdmins->checked()) {
			paintDisabledCheck = true;
		}
	}

	auto checkedRatio = 0.;
	p.fillRect(0, 0, width(), _rowHeight, sel ? st::contactsBgOver : st::contactsBg);
	if (paintDisabledCheck) {
		paintDisabledCheckUserpic(p, peer, st::contactsPadding.left(), st::contactsPadding.top(), width());
	} else if (usingMultiSelect()) {
		checkedRatio = data->checkbox->checkedAnimationRatio();
		data->checkbox->paint(p, ms, st::contactsPadding.left(), st::contactsPadding.top(), width());
	} else {
		peer->paintUserpicLeft(p, st::contactsPhotoSize, st::contactsPadding.left(), st::contactsPadding.top(), width());
	}

	int namex = st::contactsPadding.left() + st::contactsPhotoSize + st::contactsPadding.left();
	int namew = width() - namex - st::contactsPadding.right();
	if (peer->isVerified()) {
		auto icon = &st::dialogsVerifiedIcon;
		namew -= icon->width();
		icon->paint(p, namex + qMin(data->name.maxWidth(), namew), st::contactsPadding.top() + st::contactsNameTop, width());
	}
	p.setPen(anim::pen(st::contactsNameFg, st::contactsNameCheckedFg, checkedRatio));
	data->name.drawLeftElided(p, namex, st::contactsPadding.top() + st::contactsNameTop, namew, width());

	bool uname = (user || peer->isChannel()) && (data->statusText.at(0) == '@');
	p.setFont(st::contactsStatusFont);
	if (uname && !_lastQuery.isEmpty() && peer->userName().startsWith(_lastQuery, Qt::CaseInsensitive)) {
		int availw = width() - namex - st::contactsPadding.right();
		QString first = '@' + peer->userName().mid(0, _lastQuery.size()), second = peer->userName().mid(_lastQuery.size());
		int w = st::contactsStatusFont->width(first);
		if (w >= availw || second.isEmpty()) {
			p.setPen(st::contactsStatusFgOnline);
			p.drawTextLeft(namex, st::contactsPadding.top() + st::contactsStatusTop, width(), st::contactsStatusFont->elided(first, availw));
		} else {
			second = st::contactsStatusFont->elided(second, availw - w);
			int32 secondw = st::contactsStatusFont->width(second);
			p.setPen(st::contactsStatusFgOnline);
			p.drawTextLeft(namex, st::contactsPadding.top() + st::contactsStatusTop, width() - secondw, first);
			p.setPen(sel ? st::contactsStatusFgOver : st::contactsStatusFg);
			p.drawTextLeft(namex + w, st::contactsPadding.top() + st::contactsStatusTop, width() + w, second);
		}
	} else {
		if ((user && (uname || data->statusHasOnlineColor)) || (peer->isChannel() && uname)) {
			p.setPen(st::contactsStatusFgOnline);
		} else {
			p.setPen(sel ? st::contactsStatusFgOver : st::contactsStatusFg);
		}
		p.drawTextLeft(namex, st::contactsPadding.top() + st::contactsStatusTop, width(), data->statusText);
	}
}

// Emulates Ui::RoundImageCheckbox::paint() in a checked state.
void ContactsBox::Inner::paintDisabledCheckUserpic(Painter &p, PeerData *peer, int x, int y, int outerWidth) const {
	auto userpicRadius = st::contactsPhotoCheckbox.imageSmallRadius;
	auto userpicShift = st::contactsPhotoCheckbox.imageRadius - userpicRadius;
	auto userpicDiameter = st::contactsPhotoCheckbox.imageRadius * 2;
	auto userpicLeft = x + userpicShift;
	auto userpicTop = y + userpicShift;
	auto userpicEllipse = rtlrect(x, y, userpicDiameter, userpicDiameter, outerWidth);
	auto userpicBorderPen = st::contactsPhotoDisabledCheckFg->p;
	userpicBorderPen.setWidth(st::contactsPhotoCheckbox.selectWidth);

	auto iconDiameter = 2 * st::contactsPhotoCheckbox.checkRadius;
	auto iconLeft = x + userpicDiameter + st::contactsPhotoCheckbox.selectWidth - iconDiameter;
	auto iconTop = y + userpicDiameter + st::contactsPhotoCheckbox.selectWidth - iconDiameter;
	auto iconEllipse = rtlrect(iconLeft, iconTop, iconDiameter, iconDiameter, outerWidth);
	auto iconBorderPen = st::contactsPhotoCheckbox.checkBorder->p;
	iconBorderPen.setWidth(st::contactsPhotoCheckbox.selectWidth);

	peer->paintUserpicLeft(p, userpicRadius * 2, userpicLeft, userpicTop, width());

	p.setRenderHint(QPainter::HighQualityAntialiasing, true);

	p.setPen(userpicBorderPen);
	p.setBrush(Qt::NoBrush);
	p.drawEllipse(userpicEllipse);

	p.setPen(iconBorderPen);
	p.setBrush(st::contactsPhotoDisabledCheckFg);
	p.drawEllipse(iconEllipse);

	p.setRenderHint(QPainter::HighQualityAntialiasing, false);

	st::contactsPhotoCheckbox.checkIcon.paint(p, iconEllipse.topLeft(), outerWidth);
}

void ContactsBox::Inner::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	Painter p(this);

	p.setClipRect(r);
	_time = unixtime();
	p.fillRect(r, st::contactsBg);

	uint64 ms = getms();
	int32 yFrom = r.y(), yTo = r.y() + r.height();
	if (_filter.isEmpty()) {
		if (!_contacts->isEmpty() || !_byUsername.isEmpty()) {
			if (_aboutHeight) {
				p.fillRect(0, 0, width(), _aboutHeight - st::contactsPadding.bottom() - st::lineWidth, st::contactsAboutBg);
				p.fillRect(0, _aboutHeight - st::contactsPadding.bottom() - st::lineWidth, width(), st::lineWidth, st::shadowColor);
				p.setPen(st::boxTextFg);
				p.drawTextLeft(st::contactsPadding.left(), st::contactsAllAdminsTop, width(), lang(lng_chat_all_members_admins));
				int aboutw = width() - st::contactsPadding.left() - st::contactsPadding.right();
				(_allAdmins->checked() ? _aboutAllAdmins : _aboutAdmins).draw(p, st::contactsPadding.left(), st::contactsAboutSkip + st::contactsAboutTop, aboutw);

				yFrom -= _aboutHeight;
				yTo -= _aboutHeight;
				p.translate(0, _aboutHeight);
			}
			if (!_contacts->isEmpty()) {
				auto i = _contacts->cfind(yFrom, _rowHeight);
				p.translate(0, (*i)->pos() * _rowHeight);
				for (auto end = _contacts->cend(); i != end; ++i) {
					if ((*i)->pos() * _rowHeight >= yTo) {
						break;
					}
					paintDialog(p, ms, (*i)->history()->peer, contactData(*i), (*i == _sel));
					p.translate(0, _rowHeight);
				}
				yFrom -= _contacts->size() * _rowHeight;
				yTo -= _contacts->size() * _rowHeight;
			}
			if (!_byUsername.isEmpty()) {
				p.fillRect(0, 0, width(), st::searchedBarHeight, st::searchedBarBG->b);
				p.setFont(st::searchedBarFont->f);
				p.setPen(st::searchedBarColor->p);
				p.drawText(QRect(0, 0, width(), st::searchedBarHeight), lang(lng_search_global_results), style::al_center);

				yFrom -= st::searchedBarHeight;
				yTo -= st::searchedBarHeight;
				p.translate(0, st::searchedBarHeight);

				int32 from = floorclamp(yFrom, _rowHeight, 0, _byUsername.size());
				int32 to = ceilclamp(yTo, _rowHeight, 0, _byUsername.size());
				p.translate(0, from * _rowHeight);
				for (; from < to; ++from) {
					paintDialog(p, ms, _byUsername[from], d_byUsername[from], (_byUsernameSel == from));
					p.translate(0, _rowHeight);
				}
			}
		} else {
			QString text;
			int32 skip = 0;
			if (bot()) {
				text = lang((cDialogsReceived() && !_searching) ? (sharingBotGame() ? lng_bot_no_chats : lng_bot_no_groups) : lng_contacts_loading);
			} else if (_chat && _membersFilter == MembersFilter::Admins) {
				text = lang(lng_contacts_loading);
				p.fillRect(0, 0, width(), _aboutHeight - st::contactsPadding.bottom() - st::lineWidth, st::contactsAboutBg);
				p.fillRect(0, _aboutHeight - st::contactsPadding.bottom() - st::lineWidth, width(), st::lineWidth, st::shadowColor);
				p.setPen(st::boxTextFg);
				p.drawTextLeft(st::contactsPadding.left(), st::contactsAllAdminsTop, width(), lang(lng_chat_all_members_admins));
				int aboutw = width() - st::contactsPadding.left() - st::contactsPadding.right();
				(_allAdmins->checked() ? _aboutAllAdmins : _aboutAdmins).draw(p, st::contactsPadding.left(), st::contactsAboutSkip + st::contactsAboutTop, aboutw);
				p.translate(0, _aboutHeight);
			} else if (cContactsReceived() && !_searching) {
				text = lang(lng_no_contacts);
				skip = st::noContactsFont->height;
			} else {
				text = lang(lng_contacts_loading);
			}
			p.setFont(st::noContactsFont->f);
			p.setPen(st::noContactsColor->p);
			p.drawText(QRect(0, 0, width(), st::noContactsHeight - skip), text, style::al_center);
		}
	} else {
		if (_filtered.isEmpty() && _byUsernameFiltered.isEmpty()) {
			p.setFont(st::noContactsFont->f);
			p.setPen(st::noContactsColor->p);
			QString text;
			if (bot()) {
				text = lang((cDialogsReceived() && !_searching) ? (sharingBotGame() ? lng_bot_chats_not_found : lng_bot_groups_not_found) : lng_contacts_loading);
			} else if (_chat && _membersFilter == MembersFilter::Admins) {
				text = lang(_chat->participants.isEmpty() ? lng_contacts_loading : lng_contacts_not_found);
			} else {
				text = lang((cContactsReceived() && !_searching) ? lng_contacts_not_found : lng_contacts_loading);
			}
			p.drawText(QRect(0, 0, width(), st::noContactsHeight), text, style::al_center);
		} else {
			if (!_filtered.isEmpty()) {
				int32 from = floorclamp(yFrom, _rowHeight, 0, _filtered.size());
				int32 to = ceilclamp(yTo, _rowHeight, 0, _filtered.size());
				p.translate(0, from * _rowHeight);
				for (; from < to; ++from) {
					paintDialog(p, ms, _filtered[from]->history()->peer, contactData(_filtered[from]), (_filteredSel == from));
					p.translate(0, _rowHeight);
				}
			}
			if (!_byUsernameFiltered.isEmpty()) {
				p.fillRect(0, 0, width(), st::searchedBarHeight, st::searchedBarBG->b);
				p.setFont(st::searchedBarFont->f);
				p.setPen(st::searchedBarColor->p);
				p.drawText(QRect(0, 0, width(), st::searchedBarHeight), lang(lng_search_global_results), style::al_center);
				p.translate(0, st::searchedBarHeight);

				yFrom -= _filtered.size() * _rowHeight + st::searchedBarHeight;
				yTo -= _filtered.size() * _rowHeight + st::searchedBarHeight;
				int32 from = floorclamp(yFrom, _rowHeight, 0, _byUsernameFiltered.size());
				int32 to = ceilclamp(yTo, _rowHeight, 0, _byUsernameFiltered.size());
				p.translate(0, from * _rowHeight);
				for (; from < to; ++from) {
					paintDialog(p, ms, _byUsernameFiltered[from], d_byUsernameFiltered[from], (_byUsernameSel == from));
					p.translate(0, _rowHeight);
				}
			}
		}
	}
}

void ContactsBox::Inner::enterEvent(QEvent *e) {
	setMouseTracking(true);
}

int ContactsBox::Inner::getSelectedRowTop() const {
	if (_filter.isEmpty()) {
		if (_sel) {
			return _aboutHeight + (_sel->pos() * _rowHeight);
		} else if (_byUsernameSel >= 0) {
			return _aboutHeight + (_contacts->size() * _rowHeight) + st::searchedBarHeight + (_byUsernameSel * _rowHeight);
		}
	} else {
		if (_filteredSel >= 0) {
			return (_filteredSel * _rowHeight);
		} else if (_byUsernameSel >= 0) {
			return (_filtered.size() * _rowHeight + st::searchedBarHeight + _byUsernameSel * _rowHeight);
		}
	}
	return -1;
}

void ContactsBox::Inner::updateSelectedRow() {
	auto rowTop = getSelectedRowTop();
	if (rowTop >= 0) {
		updateRowWithTop(rowTop);
	}
}

void ContactsBox::Inner::updateRowWithTop(int rowTop) {
	update(0, rowTop, width(), _rowHeight);
}

int ContactsBox::Inner::getRowTopWithPeer(PeerData *peer) const {
	if (_filter.isEmpty()) {
		for (auto i = _contacts->cbegin(), end = _contacts->cend(); i != end; ++i) {
			if ((*i)->history()->peer == peer) {
				return _aboutHeight + ((*i)->pos() * _rowHeight);
			}
		}
		for (auto i = 0, count = _byUsername.size(); i != count; ++i) {
			if (_byUsername[i] == peer) {
				return _aboutHeight + (_contacts->size() * _rowHeight) + st::searchedBarHeight + (i * _rowHeight);
			}
		}
	} else {
		for (auto i = 0, count = _filtered.size(); i != count; ++i) {
			if (_filtered[i]->history()->peer == peer) {
				return (i * _rowHeight);
			}
		}
		for (auto i = 0, count = _byUsernameFiltered.size(); i != count; ++i) {
			if (_byUsernameFiltered[i] == peer) {
				return (_contacts->size() * _rowHeight) + st::searchedBarHeight + (i * _rowHeight);
			}
		}
	}
	return -1;
}

void ContactsBox::Inner::updateRowWithPeer(PeerData *peer) {
	auto rowTop = getRowTopWithPeer(peer);
	if (rowTop >= 0) {
		updateRowWithTop(rowTop);
	}
}

void ContactsBox::Inner::leaveEvent(QEvent *e) {
	_mouseSel = false;
	setMouseTracking(false);
	if (_sel || _filteredSel >= 0 || _byUsernameSel >= 0) {
		updateSelectedRow();
		_sel = 0;
		_filteredSel = _byUsernameSel = -1;
	}
}

void ContactsBox::Inner::mouseMoveEvent(QMouseEvent *e) {
	_mouseSel = true;
	_lastMousePos = e->globalPos();
	updateSelection();
}

void ContactsBox::Inner::mousePressEvent(QMouseEvent *e) {
	_mouseSel = true;
	_lastMousePos = e->globalPos();
	updateSelection();
	if (e->button() == Qt::LeftButton) {
		chooseParticipant();
	}
}

void ContactsBox::Inner::chooseParticipant() {
	if (_saving) return;
	bool addingAdmin = (_channel && _membersFilter == MembersFilter::Admins);
	if (!addingAdmin && usingMultiSelect()) {
		_time = unixtime();
		if (_filter.isEmpty()) {
			if (_byUsernameSel >= 0 && _byUsernameSel < _byUsername.size()) {
				auto data = d_byUsername[_byUsernameSel];
				auto peer = _byUsername[_byUsernameSel];
				if (data->disabledChecked) return;

				changeCheckState(data, peer);
			} else if (_sel) {
				auto data = contactData(_sel);
				auto peer = _sel->history()->peer;
				if (data->disabledChecked) return;

				changeCheckState(_sel);
			}
		} else {
			if (_byUsernameSel >= 0 && _byUsernameSel < _byUsernameFiltered.size()) {
				auto data = d_byUsernameFiltered[_byUsernameSel];
				auto peer = _byUsernameFiltered[_byUsernameSel];
				if (data->disabledChecked) return;

				int i = 0, l = d_byUsername.size();
				for (; i < l; ++i) {
					if (d_byUsername[i] == data) {
						break;
					}
				}
				if (i == l) {
					d_byUsername.push_back(data);
					_byUsername.push_back(peer);
					for (i = 0, l = _byUsernameDatas.size(); i < l;) {
						if (_byUsernameDatas[i] == data) {
							_byUsernameDatas.removeAt(i);
							--l;
						} else {
							++i;
						}
					}
				}

				changeCheckState(data, peer);
			} else if (_filteredSel >= 0 && _filteredSel < _filtered.size()) {
				auto data = contactData(_filtered[_filteredSel]);
				auto peer = _filtered[_filteredSel]->history()->peer;
				if (data->disabledChecked) return;

				changeCheckState(data, peer);
			}
		}
	} else {
		PeerData *peer = 0;
		if (_filter.isEmpty()) {
			if (_byUsernameSel >= 0 && _byUsernameSel < _byUsername.size()) {
				peer = _byUsername[_byUsernameSel];
			} else if (_sel) {
				peer = _sel->history()->peer;
			}
		} else {
			if (_byUsernameSel >= 0 && _byUsernameSel < _byUsernameFiltered.size()) {
				peer = _byUsernameFiltered[_byUsernameSel];
			} else {
				if (_filteredSel < 0 || _filteredSel >= _filtered.size()) return;
				peer = _filtered[_filteredSel]->history()->peer;
			}
		}
		if (peer) {
			if (addingAdmin) {
				_addAdmin = peer->asUser();
				if (_addAdminRequestId) {
					MTP::cancel(_addAdminRequestId);
					_addAdminRequestId = 0;
				}
				if (_addAdminBox) _addAdminBox->deleteLater();
				_addAdminBox = new ConfirmBox(lng_channel_admin_sure(lt_user, _addAdmin->firstName));
				connect(_addAdminBox, SIGNAL(confirmed()), this, SLOT(onAddAdmin()));
				connect(_addAdminBox, SIGNAL(destroyed(QObject*)), this, SLOT(onNoAddAdminBox(QObject*)));
				Ui::showLayer(_addAdminBox, KeepOtherLayers);
			} else if (sharingBotGame()) {
				_addToPeer = peer;
				auto confirmText = [peer] {
					if (peer->isUser()) {
						return lng_bot_sure_share_game(lt_user, App::peerName(peer));
					}
					return lng_bot_sure_share_game_group(lt_group, peer->name);
				};
				auto box = std_::make_unique<ConfirmBox>(confirmText());
				connect(box.get(), SIGNAL(confirmed()), this, SLOT(onAddBot()));
				Ui::showLayer(box.release(), KeepOtherLayers);
			} else if (bot() && (peer->isChat() || peer->isMegagroup())) {
				_addToPeer = peer;
				auto box = std_::make_unique<ConfirmBox>(lng_bot_sure_invite(lt_group, peer->name));
				connect(box.get(), SIGNAL(confirmed()), this, SLOT(onAddBot()));
				Ui::showLayer(box.release(), KeepOtherLayers);
			} else {
				Ui::hideSettingsAndLayer(true);
				App::main()->choosePeer(peer->id, ShowAtUnreadMsgId);
			}
		}
	}
	update();
}

void ContactsBox::Inner::changeCheckState(Dialogs::Row *row) {
	changeCheckState(contactData(row), row->history()->peer);
}

void ContactsBox::Inner::changeCheckState(ContactData *data, PeerData *peer) {
	t_assert(usingMultiSelect());

	if (_chat && _membersFilter == MembersFilter::Admins && _allAdmins->checked()) {
	} else if (data->checkbox->checked()) {
		changePeerCheckState(data, peer, false);
	} else if (selectedCount() < ((_channel && _channel->isMegagroup()) ? Global::MegagroupSizeMax() : Global::ChatSizeMax())) {
		changePeerCheckState(data, peer, true);
	} else if (_channel && !_channel->isMegagroup()) {
		Ui::showLayer(new MaxInviteBox(_channel->inviteLink()), KeepOtherLayers);
	} else if (!_channel && selectedCount() >= Global::ChatSizeMax() && selectedCount() < Global::MegagroupSizeMax()) {
		Ui::showLayer(new InformBox(lng_profile_add_more_after_upgrade(lt_count, Global::MegagroupSizeMax())), KeepOtherLayers);
	}
}

void ContactsBox::Inner::peerUnselected(PeerData *peer) {
	// If data is nullptr we simply won't do anything.
	auto data = _contactsData.value(peer, nullptr);
	changePeerCheckState(data, peer, false, ChangeStateWay::SkipCallback);
}

void ContactsBox::Inner::setPeerSelectedChangedCallback(base::lambda_unique<void(PeerData *peer, bool selected)> callback) {
	_peerSelectedChangedCallback = std_::move(callback);
}

void ContactsBox::Inner::changePeerCheckState(ContactData *data, PeerData *peer, bool checked, ChangeStateWay useCallback) {
	if (data) {
		data->checkbox->setChecked(checked);
	}
	if (checked) {
		_checkedContacts.insert(peer);
	} else {
		_checkedContacts.remove(peer);
	}
	if (useCallback != ChangeStateWay::SkipCallback && _peerSelectedChangedCallback) {
		_peerSelectedChangedCallback(peer, checked);
	}
}

int32 ContactsBox::Inner::selectedCount() const {
	auto result = _checkedContacts.size();
	if (_chat) {
		result += qMax(_chat->count, 1);
	} else if (_channel) {
		result += qMax(_channel->membersCount(), _already.size());
	} else if (_creating == CreatingGroupGroup) {
		result += 1;
	}
	return result;
}

void ContactsBox::Inner::updateSelection() {
	if (!_mouseSel) return;

	QPoint p(mapFromGlobal(_lastMousePos));
	bool in = parentWidget()->rect().contains(parentWidget()->mapFromGlobal(_lastMousePos));
	if (_filter.isEmpty()) {
		if (_aboutHeight) {
			p.setY(p.y() - _aboutHeight);
		}
		Dialogs::Row *newSel = (in && (p.y() >= 0) && (p.y() < _contacts->size() * _rowHeight)) ? _contacts->rowAtY(p.y(), _rowHeight) : nullptr;
		int32 byUsernameSel = (in && p.y() >= _contacts->size() * _rowHeight + st::searchedBarHeight) ? ((p.y() - _contacts->size() * _rowHeight - st::searchedBarHeight) / _rowHeight) : -1;
		if (byUsernameSel >= _byUsername.size()) byUsernameSel = -1;
		if (newSel != _sel || byUsernameSel != _byUsernameSel) {
			updateSelectedRow();
			_sel = newSel;
			_byUsernameSel = byUsernameSel;
			updateSelectedRow();
		}
	} else {
		int32 newFilteredSel = (in && p.y() >= 0 && p.y() < _filtered.size() * _rowHeight) ? (p.y() / _rowHeight) : -1;
		int32 byUsernameSel = (in && p.y() >= _filtered.size() * _rowHeight + st::searchedBarHeight) ? ((p.y() - _filtered.size() * _rowHeight - st::searchedBarHeight) / _rowHeight) : -1;
		if (byUsernameSel >= _byUsernameFiltered.size()) byUsernameSel = -1;
		if (newFilteredSel != _filteredSel || byUsernameSel != _byUsernameSel) {
			updateSelectedRow();
			_filteredSel = newFilteredSel;
			_byUsernameSel = byUsernameSel;
			updateSelectedRow();
		}
	}
}

void ContactsBox::Inner::updateFilter(QString filter) {
	_lastQuery = filter.toLower().trimmed();
	filter = textSearchKey(filter);

	_time = unixtime();
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
		d_byUsernameFiltered.clear();
		for (int i = 0, l = _byUsernameDatas.size(); i < l; ++i) {
			delete _byUsernameDatas[i];
		}
		_byUsernameDatas.clear();

		if (_filter.isEmpty()) {
			_sel = 0;
			refresh();
		} else {
			if (!_addContactLnk->isHidden()) _addContactLnk->hide();
			if (!_allAdmins->isHidden()) _allAdmins->hide();
			QStringList::const_iterator fb = f.cbegin(), fe = f.cend(), fi;

			_filtered.clear();
			if (!f.isEmpty()) {
				const Dialogs::List *toFilter = nullptr;
				if (!_contacts->isEmpty()) {
					for (fi = fb; fi != fe; ++fi) {
						auto found = _contacts->filtered(fi->at(0));
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
							row->attached = nullptr;
							_filtered.push_back(row);
						}
					}
				}

				_byUsernameFiltered.reserve(_byUsername.size());
				d_byUsernameFiltered.reserve(d_byUsername.size());
				for (int32 i = 0, l = _byUsername.size(); i < l; ++i) {
					const PeerData::Names &names(_byUsername[i]->names);
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
						_byUsernameFiltered.push_back(_byUsername[i]);
						d_byUsernameFiltered.push_back(d_byUsername[i]);
					}
				}
			}
			_filteredSel = -1;
			if (!_filtered.isEmpty()) {
				for (_filteredSel = 0; (_filteredSel < _filtered.size()) && contactData(_filtered[_filteredSel])->disabledChecked;) {
					++_filteredSel;
				}
				if (_filteredSel == _filtered.size()) _filteredSel = -1;
			}
			_byUsernameSel = -1;
			if (_filteredSel < 0 && !_byUsernameFiltered.isEmpty()) {
				for (_byUsernameSel = 0; (_byUsernameSel < _byUsernameFiltered.size()) && d_byUsernameFiltered[_byUsernameSel]->disabledChecked;) {
					++_byUsernameSel;
				}
				if (_byUsernameSel == _byUsernameFiltered.size()) _byUsernameSel = -1;
			}
			_mouseSel = false;
			refresh();

			if ((!bot() || sharingBotGame()) && (!_chat || _membersFilter != MembersFilter::Admins)) {
				_searching = true;
				emit searchByUsername();
			}
		}
		update();
		loadProfilePhotos(0);
	}
}

void ContactsBox::Inner::onDialogRowReplaced(Dialogs::Row *oldRow, Dialogs::Row *newRow) {
	if (!_filter.isEmpty()) {
		for (FilteredDialogs::iterator i = _filtered.begin(), e = _filtered.end(); i != e;) {
			if (*i == oldRow) { // this row is shown in filtered and maybe is in contacts!
				if (newRow) {
					*i = newRow;
					++i;
				} else {
					i = _filtered.erase(i);
				}
			} else {
				++i;
			}
		}
		if (_filteredSel >= _filtered.size()) {
			_filteredSel = -1;
		}
	} else {
		if (_sel == oldRow) {
			_sel = newRow;
		}
	}
	_mouseSel = false;
	int cnt = (_filter.isEmpty() ? _contacts->size() : _filtered.size());
	int newh = cnt ? (cnt * _rowHeight) : st::noContactsHeight;
	resize(width(), newh);
}

void ContactsBox::Inner::peopleReceived(const QString &query, const QVector<MTPPeer> &people) {
	_lastQuery = query.toLower().trimmed();
	if (_lastQuery.at(0) == '@') _lastQuery = _lastQuery.mid(1);
	int32 already = _byUsernameFiltered.size();
	_byUsernameFiltered.reserve(already + people.size());
	d_byUsernameFiltered.reserve(already + people.size());
	for (QVector<MTPPeer>::const_iterator i = people.cbegin(), e = people.cend(); i != e; ++i) {
		auto peerId = peerFromMTP(*i);
		int j = 0;
		for (; j < already; ++j) {
			if (_byUsernameFiltered[j]->id == peerId) break;
		}
		if (j == already) {
			auto peer = App::peer(peerId);
			if (!peer) continue;

			if (_channel || _chat || _creating != CreatingGroupNone) {
				if (peer->isUser()) {
					if (peer->asUser()->botInfo) {
						if (_chat || _creating == CreatingGroupGroup) { // skip bot's that can't be invited to groups
							if (peer->asUser()->botInfo->cantJoinGroups) continue;
						}
						if (_channel) {
							if (!_channel->isMegagroup() && _membersFilter != MembersFilter::Admins) continue;
						}
					}
				} else {
					continue; // skip
				}
			} else if (sharingBotGame()) {
				if (!peer->canWrite()) {
					continue;
				}
				if (auto channel = peer->asChannel()) {
					if (channel->isBroadcast()) {
						continue;
					}
				}
			}

			auto data = usingMultiSelect() ? new ContactData(peer, [this, peer] { updateRowWithPeer(peer); }) : new ContactData();
			_byUsernameDatas.push_back(data);
			data->disabledChecked = _chat ? _chat->participants.contains(peer->asUser()) : ((_creating == CreatingGroupGroup || _channel) ? (peer == App::self()) : false);
			if (usingMultiSelect() && _checkedContacts.contains(peer)) {
				data->checkbox->setChecked(true, Ui::RoundImageCheckbox::SetStyle::Fast);
			}
			data->name.setText(st::contactsNameFont, peer->name, _textNameOptions);
			data->statusText = '@' + peer->userName();

			_byUsernameFiltered.push_back(peer);
			d_byUsernameFiltered.push_back(data);
		}
	}
	_searching = false;
	refresh();
}

void ContactsBox::Inner::refresh() {
	if (_filter.isEmpty()) {
		if (_chat && _membersFilter == MembersFilter::Admins) {
			if (_allAdmins->isHidden()) _allAdmins->show();
		} else {
			if (!_allAdmins->isHidden()) _allAdmins->hide();
		}
		if (!_contacts->isEmpty() || !_byUsername.isEmpty()) {
			if (!_addContactLnk->isHidden()) _addContactLnk->hide();
			resize(width(), _aboutHeight + (_contacts->size() * _rowHeight) + (_byUsername.isEmpty() ? 0 : (st::searchedBarHeight + _byUsername.size() * _rowHeight)));
		} else if (_chat && _membersFilter == MembersFilter::Admins) {
			if (!_addContactLnk->isHidden()) _addContactLnk->hide();
			resize(width(), _aboutHeight + st::noContactsHeight);
		} else {
			if (cContactsReceived() && !bot()) {
				if (_addContactLnk->isHidden()) _addContactLnk->show();
			} else {
				if (!_addContactLnk->isHidden()) _addContactLnk->hide();
			}
			resize(width(), st::noContactsHeight);
		}
	} else {
		if (!_allAdmins->isHidden()) _allAdmins->hide();
		if (_filtered.isEmpty() && _byUsernameFiltered.isEmpty()) {
			if (!_addContactLnk->isHidden()) _addContactLnk->hide();
			resize(width(), st::noContactsHeight);
		} else {
			resize(width(), (_filtered.size() * _rowHeight) + (_byUsernameFiltered.isEmpty() ? 0 : (st::searchedBarHeight + _byUsernameFiltered.size() * _rowHeight)));
		}
	}
	update();
}

ChatData *ContactsBox::Inner::chat() const {
	return _chat;
}

ChannelData *ContactsBox::Inner::channel() const {
	return _channel;
}

MembersFilter ContactsBox::Inner::membersFilter() const {
	return _membersFilter;
}

UserData *ContactsBox::Inner::bot() const {
	return _bot;
}

bool ContactsBox::Inner::sharingBotGame() const {
	return (_bot && _bot->botInfo) ? !_bot->botInfo->shareGameShortName.isEmpty() : false;
}

CreatingGroupType ContactsBox::Inner::creating() const {
	return _creating;
}

ContactsBox::Inner::~Inner() {
	for (auto contactData : base::take(_contactsData)) {
		delete contactData;
	}
	if (_bot) {
		if (auto &info = _bot->botInfo) {
			info->startGroupToken = QString();
			info->shareGameShortName = QString();
		}
	}
}

void ContactsBox::Inner::resizeEvent(QResizeEvent *e) {
	_addContactLnk->move((width() - _addContactLnk->width()) / 2, (st::noContactsHeight + st::noContactsFont->height) / 2);
	_allAdmins->moveToLeft(st::contactsPadding.left(), st::contactsAllAdminsTop);
}

void ContactsBox::Inner::selectSkip(int32 dir) {
	_time = unixtime();
	_mouseSel = false;
	if (_filter.isEmpty()) {
		int cur = 0;
		if (_sel) {
			for (auto i = _contacts->cbegin(); *i != _sel; ++i) {
				++cur;
			}
		} else if (_byUsernameSel >= 0) {
			cur = (_contacts->size() + _byUsernameSel);
		} else {
			cur = -1;
		}
		cur += dir;
		if (cur <= 0) {
			_sel = (!_contacts->isEmpty()) ? *_contacts->cbegin() : nullptr;
			_byUsernameSel = (_contacts->isEmpty() && !_byUsername.isEmpty()) ? 0 : -1;
		} else if (cur >= _contacts->size()) {
			if (_byUsername.isEmpty()) {
				_sel = _contacts->isEmpty() ? nullptr : *(_contacts->cend() - 1);
				_byUsernameSel = -1;
			} else {
				_sel = nullptr;
				_byUsernameSel = cur - _contacts->size();
				if (_byUsernameSel >= _byUsername.size()) _byUsernameSel = _byUsername.size() - 1;
			}
		} else {
			for (auto i = _contacts->cbegin(); ; ++i) {
				_sel = *i;
				if (!cur) {
					break;
				} else {
					--cur;
				}
			}
			_byUsernameSel = -1;
		}
		if (dir > 0) {
			for (auto i = _contacts->cfind(_sel), end = _contacts->cend(); i != end && contactData(*i)->disabledChecked; ++i) {
				_sel = *i;
			}
			if (_sel && contactData(_sel)->disabledChecked) {
				_sel = nullptr;
			}
			if (!_sel) {
				if (!_byUsername.isEmpty()) {
					if (_byUsernameSel < 0) _byUsernameSel = 0;
					for (; _byUsernameSel < _byUsername.size() && d_byUsername[_byUsernameSel]->disabledChecked;) {
						++_byUsernameSel;
					}
					if (_byUsernameSel == _byUsername.size()) _byUsernameSel = -1;
				}
			}
		} else {
			while (_byUsernameSel >= 0 && d_byUsername[_byUsernameSel]->disabledChecked) {
				--_byUsernameSel;
			}
			if (_byUsernameSel < 0) {
				if (!_contacts->isEmpty()) {
					if (!_sel) _sel = *(_contacts->cend() - 1);
					if (_sel) {
						for (auto i = _contacts->cfind(_sel), b = _contacts->cbegin(); i != b && contactData(*i)->disabledChecked; --i) {
							_sel = *i;
						}
						if (contactData(_sel)->disabledChecked) {
							_sel = nullptr;
						}
					}
				}
			}
		}
		if (_sel) {
			emit mustScrollTo(_aboutHeight + _sel->pos() * _rowHeight, _aboutHeight + (_sel->pos() + 1) * _rowHeight);
		} else if (_byUsernameSel >= 0) {
			emit mustScrollTo(_aboutHeight + (_contacts->size() + _byUsernameSel) * _rowHeight + st::searchedBarHeight, _aboutHeight + (_contacts->size() + _byUsernameSel + 1) * _rowHeight + st::searchedBarHeight);
		}
	} else {
		int cur = (_filteredSel >= 0) ? _filteredSel : ((_byUsernameSel >= 0) ? (_filtered.size() + _byUsernameSel) : -1);
		cur += dir;
		if (cur <= 0) {
			_filteredSel = _filtered.isEmpty() ? -1 : 0;
			_byUsernameSel = (_filtered.isEmpty() && !_byUsernameFiltered.isEmpty()) ? 0 : -1;
		} else if (cur >= _filtered.size()) {
			_filteredSel = -1;
			_byUsernameSel = cur - _filtered.size();
			if (_byUsernameSel >= _byUsernameFiltered.size()) _byUsernameSel = _byUsernameFiltered.size() - 1;
		} else {
			_filteredSel = cur;
			_byUsernameSel = -1;
		}
		if (dir > 0) {
			while (_filteredSel >= 0 && _filteredSel < _filtered.size() && contactData(_filtered[_filteredSel])->disabledChecked) {
				++_filteredSel;
			}
			if (_filteredSel < 0 || _filteredSel >= _filtered.size()) {
				_filteredSel = -1;
				if (!_byUsernameFiltered.isEmpty()) {
					if (_byUsernameSel < 0) _byUsernameSel = 0;
					for (; _byUsernameSel < _byUsernameFiltered.size() && d_byUsernameFiltered[_byUsernameSel]->disabledChecked;) {
						++_byUsernameSel;
					}
					if (_byUsernameSel == _byUsernameFiltered.size()) _byUsernameSel = -1;
				}
			}
		} else {
			while (_byUsernameSel >= 0 && d_byUsernameFiltered[_byUsernameSel]->disabledChecked) {
				--_byUsernameSel;
			}
			if (_byUsernameSel < 0) {
				if (!_filtered.isEmpty()) {
					if (_filteredSel < 0) _filteredSel = _filtered.size() - 1;
					for (; _filteredSel >= 0 && contactData(_filtered[_filteredSel])->disabledChecked;) {
						--_filteredSel;
					}
				}
			}
		}
		if (_filteredSel >= 0) {
			emit mustScrollTo(_filteredSel * _rowHeight, (_filteredSel + 1) * _rowHeight);
		} else if (_byUsernameSel >= 0) {
			int skip = _filtered.size() * _rowHeight + st::searchedBarHeight;
			emit mustScrollTo(skip + _byUsernameSel * _rowHeight, skip + (_byUsernameSel + 1) * _rowHeight);
		}
	}
	update();
}

void ContactsBox::Inner::selectSkipPage(int32 h, int32 dir) {
	int32 points = h / _rowHeight;
	if (!points) return;
	selectSkip(points * dir);
}

QVector<UserData*> ContactsBox::Inner::selected() {
	QVector<UserData*> result;
	if (!usingMultiSelect()) {
		return result;
	}

	for_const (auto row, *_contacts) {
		if (_checkedContacts.contains(row->history()->peer)) {
			contactData(row); // fill _contactsData
		}
	}
	result.reserve(_contactsData.size());
	for (auto i = _contactsData.cbegin(), e = _contactsData.cend(); i != e; ++i) {
		if (i.value()->checkbox->checked() && i.key()->isUser()) {
			result.push_back(i.key()->asUser());
		}
	}
	for (int i = 0, l = _byUsername.size(); i < l; ++i) {
		if (d_byUsername[i]->checkbox->checked() && _byUsername[i]->isUser()) {
			result.push_back(_byUsername[i]->asUser());
		}
	}
	return result;
}

QVector<MTPInputUser> ContactsBox::Inner::selectedInputs() {
	QVector<MTPInputUser> result;
	if (!usingMultiSelect()) {
		return result;
	}

	for_const (auto row, *_contacts) {
		if (_checkedContacts.contains(row->history()->peer)) {
			contactData(row); // fill _contactsData
		}
	}
	result.reserve(_contactsData.size());
	for (auto i = _contactsData.cbegin(), e = _contactsData.cend(); i != e; ++i) {
		if (i.value()->checkbox->checked() && i.key()->isUser()) {
			result.push_back(i.key()->asUser()->inputUser);
		}
	}
	for (int i = 0, l = _byUsername.size(); i < l; ++i) {
		if (d_byUsername[i]->checkbox->checked() && _byUsername[i]->isUser()) {
			result.push_back(_byUsername[i]->asUser()->inputUser);
		}
	}
	return result;
}

bool ContactsBox::Inner::allAdmins() const {
	return _allAdmins->checked();
}
