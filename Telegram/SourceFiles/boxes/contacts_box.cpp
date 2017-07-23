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
#include "boxes/contacts_box.h"

#include "dialogs/dialogs_indexed_list.h"
#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"
#include "styles/style_history.h"
#include "styles/style_profile.h"
#include "lang/lang_keys.h"
#include "boxes/add_contact_box.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "messenger.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "core/file_utilities.h"
#include "ui/widgets/multi_select.h"
#include "ui/widgets/scroll_area.h"
#include "ui/effects/widget_slide_wrap.h"
#include "ui/effects/ripple_animation.h"
#include "boxes/photo_crop_box.h"
#include "boxes/confirm_box.h"
#include "boxes/edit_participant_box.h"
#include "window/themes/window_theme.h"
#include "observer_peer.h"
#include "apiwrap.h"
#include "auth_session.h"
#include "storage/file_download.h"

// Not used for now.
//
//MembersAddButton::MembersAddButton(QWidget *parent, const style::TwoIconButton &st) : RippleButton(parent, st.ripple)
//, _st(st) {
//	resize(_st.width, _st.height);
//	setCursor(style::cur_pointer);
//}
//
//void MembersAddButton::paintEvent(QPaintEvent *e) {
//	Painter p(this);
//
//	auto ms = getms();
//	auto over = isOver();
//	auto down = isDown();
//
//	((over || down) ? _st.iconBelowOver : _st.iconBelow).paint(p, _st.iconPosition, width());
//	paintRipple(p, _st.rippleAreaPosition.x(), _st.rippleAreaPosition.y(), ms);
//	((over || down) ? _st.iconAboveOver : _st.iconAbove).paint(p, _st.iconPosition, width());
//}
//
//QImage MembersAddButton::prepareRippleMask() const {
//	return Ui::RippleAnimation::ellipseMask(QSize(_st.rippleAreaSize, _st.rippleAreaSize));
//}
//
//QPoint MembersAddButton::prepareRippleStartPosition() const {
//	return mapFromGlobal(QCursor::pos()) - _st.rippleAreaPosition;
//}

QString PeerFloodErrorText(PeerFloodType type) {
	auto link = textcmdLink(Messenger::Instance().createInternalLinkFull(qsl("spambot")), lang(lng_cant_more_info));
	if (type == PeerFloodType::InviteGroup) {
		return lng_cant_invite_not_contact(lt_more_info, link);
	}
	return lng_cant_send_to_not_contact(lt_more_info, link);
}

ContactsBox::ContactsBox(QWidget*, ChatData *chat, MembersFilter filter)
: _chat(chat)
, _membersFilter(filter)
, _select(createMultiSelect())
, _searchTimer(this) {
}

ContactsBox::ContactsBox(QWidget*, ChannelData *channel)
: _channel(channel)
, _creating(CreatingGroupChannel)
, _select(createMultiSelect())
, _searchTimer(this) {
}

ContactsBox::ContactsBox(QWidget*, ChannelData *channel, MembersFilter filter, const MembersAlreadyIn &already)
: _channel(channel)
, _membersFilter(filter)
, _alreadyIn(already)
, _select(createMultiSelect())
, _searchTimer(this) {
}

ContactsBox::ContactsBox(QWidget*, UserData *bot)
: _bot(bot)
, _select(createMultiSelect())
, _searchTimer(this) {
}

ContactsBox::ContactsBox(QWidget*, const QString &name, const QImage &photo)
: _creating(CreatingGroupGroup)
, _select(createMultiSelect())
, _searchTimer(this)
, _creationName(name)
, _creationPhoto(photo) {
}

ContactsBox::ContactsBox(QWidget*)
: _select(createMultiSelect())
, _searchTimer(this) {
}

void ContactsBox::prepare() {
	_select->resizeToWidth(st::boxWideWidth);
	myEnsureResized(_select);

	auto createInner = [this] {
		if (_chat) {
			return object_ptr<Inner>(this, _chat, _membersFilter);
		} else if (_channel) {
			return object_ptr<Inner>(this, _channel, _membersFilter, _alreadyIn);
		} else if (_bot) {
			return object_ptr<Inner>(this, _bot);
		}
		return object_ptr<Inner>(this, _creating);
	};
	_inner = setInnerWidget(createInner(), getTopScrollSkip());

	updateTitle();
	if (_chat) {
		if (_membersFilter == MembersFilter::Admins) {
			addButton(langFactory(lng_settings_save), [this] { saveChatAdmins(); });
		} else {
			addButton(langFactory(lng_participant_invite), [this] { inviteParticipants(); });
		}
		addButton(langFactory(lng_cancel), [this] { closeBox(); });
	} else if (_channel) {
		if (_membersFilter != MembersFilter::Admins) {
			addButton(langFactory(lng_participant_invite), [this] { inviteParticipants(); });
		}
		addButton(langFactory((_creating == CreatingGroupChannel) ? lng_create_group_skip : lng_cancel), [this] { closeBox(); });
	} else if (_bot) {
		addButton(langFactory(lng_close), [this] { closeBox(); });
	} else if (_creating == CreatingGroupGroup) {
		addButton(langFactory(lng_create_group_create), [this] { createGroup(); });
		addButton(langFactory(lng_create_group_back), [this] { closeBox(); });
	} else {
		addButton(langFactory(lng_close), [this] { closeBox(); });
		addLeftButton(langFactory(lng_profile_add_contact), [] { App::wnd()->onShowAddContact(); });
	}

	_inner->setPeerSelectedChangedCallback([this](PeerData *peer, bool checked) {
		onPeerSelectedChanged(peer, checked);
	});
	for (auto i : _inner->selected()) {
		addPeerToMultiSelect(i, true);
	}
	_inner->setAllAdminsChangedCallback([this] {
		_select->toggleAnimated(!_inner->allAdmins());
		if (_inner->allAdmins()) {
			_select->entity()->clearQuery();
			_inner->setFocus();
		} else {
			_select->entity()->setInnerFocus();
		}
		updateScrollSkips();
	});
	_select->toggleFast(!_inner->chat() || (_inner->membersFilter() != MembersFilter::Admins) || !_inner->allAdmins());
	_select->entity()->setQueryChangedCallback([this](const QString &query) { onFilterUpdate(query); });
	_select->entity()->setItemRemovedCallback([this](uint64 itemId) {
		if (auto peer = App::peerLoaded(itemId)) {
			_inner->peerUnselected(peer);
			update();
		}
	});
	_select->entity()->setSubmittedCallback([this](bool) { onSubmit(); });
	connect(_inner, SIGNAL(mustScrollTo(int, int)), this, SLOT(onScrollToY(int, int)));
	connect(_inner, SIGNAL(searchByUsername()), this, SLOT(onNeedSearchByUsername()));
	connect(_inner, SIGNAL(adminAdded()), this, SIGNAL(adminAdded()));

	_searchTimer->setSingleShot(true);
	connect(_searchTimer, SIGNAL(timeout()), this, SLOT(onSearchByUsername()));

	setDimensions(st::boxWideWidth, st::boxMaxListHeight);

	_select->raise();
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

void ContactsBox::updateTitle() {
	if (_chat && _membersFilter == MembersFilter::Admins) {
		setTitle(langFactory(lng_channel_admins));
	} else if (_chat || _creating != CreatingGroupNone) {
		auto addingAdmin = _channel && (_membersFilter == MembersFilter::Admins);
		auto additional = (addingAdmin || (_inner->channel() && !_inner->channel()->isMegagroup())) ? QString() : QString("%1 / %2").arg(_inner->selectedCount()).arg(Global::MegagroupSizeMax());
		setTitle(langFactory(addingAdmin ? lng_channel_add_admin : lng_profile_add_participant));
		setAdditionalTitle([additional] { return additional; });
	} else if (_inner->sharingBotGame()) {
		setTitle(langFactory(lng_bot_choose_chat));
	} else if (_inner->bot()) {
		setTitle(langFactory(lng_bot_choose_group));
	} else {
		setTitle(langFactory(lng_contacts_header));
	}
}

void ContactsBox::onNeedSearchByUsername() {
	if (!onSearchByUsername(true)) {
		_searchTimer->start(AutoSearchTimeout);
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
			_inner->peopleReceived(q, result.c_contacts_found().vresults.v);
		} break;
		}

		_peopleRequest = 0;
		_inner->updateSelection();
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

void ContactsBox::setInnerFocus() {
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
			_inner->selectSkipPage(height() - getTopScrollSkip(), 1);
		} else if (e->key() == Qt::Key_PageUp) {
			_inner->selectSkipPage(height() - getTopScrollSkip(), -1);
		} else {
			BoxContent::keyPressEvent(e);
		}
	} else {
		BoxContent::keyPressEvent(e);
	}
}

object_ptr<Ui::WidgetSlideWrap<Ui::MultiSelect>> ContactsBox::createMultiSelect() {
	auto entity = object_ptr<Ui::MultiSelect>(this, st::contactsMultiSelect, langFactory(lng_participant_filter));
	auto margins = style::margins(0, 0, 0, 0);
	auto callback = [this] { updateScrollSkips(); };
	return object_ptr<Ui::WidgetSlideWrap<Ui::MultiSelect>>(this, std::move(entity), margins, std::move(callback));
}

int ContactsBox::getTopScrollSkip() const {
	auto result = 0;
	if (!_select->isHidden()) {
		result += _select->height();
	}
	return result;
}

void ContactsBox::updateScrollSkips() {
	setInnerTopSkip(getTopScrollSkip(), true);
}

void ContactsBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_select->resizeToWidth(width());
	_select->moveToLeft(0, 0);

	updateScrollSkips();

	_inner->resize(width(), _inner->height());
}

void ContactsBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	for (auto rect : e->region().rects()) {
		p.fillRect(rect, st::contactsBg);
	}
}

void ContactsBox::closeHook() {
	if (_channel && _creating == CreatingGroupChannel) {
		Ui::showPeerHistory(_channel, ShowAtTheEndMsgId);
	}
}

void ContactsBox::onFilterUpdate(const QString &filter) {
	onScrollToY(0);
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
	updateTitle();
}

void ContactsBox::inviteParticipants() {
	auto users = _inner->selected();
	if (users.empty()) {
		_select->entity()->setInnerFocus();
		return;
	}

	App::main()->addParticipants(_inner->chat() ? (PeerData*)_inner->chat() : _inner->channel(), users);
	if (_inner->chat()) {
		Ui::hideLayer();
		Ui::showPeerHistory(_inner->chat(), ShowAtTheEndMsgId);
	} else {
		closeBox();
	}
}

void ContactsBox::createGroup() {
	if (_saveRequestId) return;

	auto users = _inner->selectedInputs();
	if (users.empty() || (users.size() == 1 && users.at(0).type() == mtpc_inputUserSelf)) {
		_select->entity()->setInnerFocus();
		return;
	}
	_saveRequestId = MTP::send(MTPmessages_CreateChat(MTP_vector<MTPInputUser>(users), MTP_string(_creationName)), rpcDone(&ContactsBox::creationDone), rpcFail(&ContactsBox::creationFail));
}

void ContactsBox::saveChatAdmins() {
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
		closeBox();
	} else {
		_saveRequestId = MTP::send(MTPmessages_GetFullChat(_inner->chat()->inputChat), rpcDone(&ContactsBox::getAdminsDone), rpcFail(&ContactsBox::saveAdminsFail));
	}
}

void ContactsBox::getAdminsDone(const MTPmessages_ChatFull &result) {
	App::api()->processFullPeer(_inner->chat(), result);
	if (_inner->allAdmins()) {
		closeBox();
		return;
	}
	auto curadmins = _inner->chat()->admins;
	auto newadmins = _inner->selected();
	auto appoint = decltype(newadmins)();
	if (!newadmins.empty()) {
		appoint.reserve(newadmins.size());
		for (auto &user : newadmins) {
			auto c = curadmins.find(user);
			if (c == curadmins.cend()) {
				if (user->id != peerFromUser(_inner->chat()->creator)) {
					appoint.push_back(user);
				}
			} else {
				curadmins.erase(c);
			}
		}
	}
	_saveRequestId = 0;

	for_const (auto user, curadmins) {
		MTP::send(MTPmessages_EditChatAdmin(_inner->chat()->inputChat, user->inputUser, MTP_boolFalse()), rpcDone(&ContactsBox::removeAdminDone, user), rpcFail(&ContactsBox::editAdminFail), 0, 10);
	}
	for_const (auto user, appoint) {
		MTP::send(MTPmessages_EditChatAdmin(_inner->chat()->inputChat, user->inputUser, MTP_boolTrue()), rpcDone(&ContactsBox::setAdminDone, user), rpcFail(&ContactsBox::editAdminFail), 0, 10);
	}
	MTP::sendAnything();

	_saveRequestId = curadmins.size() + appoint.size();
	if (!_saveRequestId) {
		closeBox();
	}
}

void ContactsBox::setAdminDone(gsl::not_null<UserData*> user, const MTPBool &result) {
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
		closeBox();
	}
}

void ContactsBox::removeAdminDone(gsl::not_null<UserData*> user, const MTPBool &result) {
	if (mtpIsTrue(result)) {
		_inner->chat()->admins.remove(user);
	}
	--_saveRequestId;
	if (!_saveRequestId) {
		emit App::main()->peerUpdated(_inner->chat());
		closeBox();
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
			Ui::show(Box<InformBox>(lang(lng_cant_do_this)));
			return true;
		}
		closeBox();
	}
	return false;
}

void ContactsBox::creationDone(const MTPUpdates &updates) {
	Ui::hideLayer();

	App::main()->sentUpdatesReceived(updates);
	const QVector<MTPChat> *v = 0;
	switch (updates.type()) {
	case mtpc_updates: v = &updates.c_updates().vchats.v; break;
	case mtpc_updatesCombined: v = &updates.c_updatesCombined().vchats.v; break;
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
		closeBox();
		return true;
	} else if (error.type() == "USERS_TOO_FEW") {
		_select->entity()->setInnerFocus();
		return true;
	} else if (error.type() == "PEER_FLOOD") {
		Ui::show(Box<InformBox>(PeerFloodErrorText(PeerFloodType::InviteGroup)), KeepOtherLayers);
		return true;
	} else if (error.type() == qstr("USER_RESTRICTED")) {
		Ui::show(Box<InformBox>(lang(lng_cant_do_this)));
		return true;
	}
	return false;
}

ContactsBox::Inner::ContactData::ContactData() = default;

ContactsBox::Inner::ContactData::ContactData(PeerData *peer, base::lambda<void()> updateCallback)
: checkbox(std::make_unique<Ui::RoundImageCheckbox>(st::contactsPhotoCheckbox, updateCallback, PaintUserpicCallback(peer))) {
}

ContactsBox::Inner::ContactData::~ContactData() = default;

ContactsBox::Inner::Inner(QWidget *parent, CreatingGroupType creating) : TWidget(parent)
, _rowHeight(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom())
, _creating(creating)
, _allAdmins(this, lang(lng_chat_all_members_admins), false, st::defaultBoxCheckbox)
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
, _allAdmins(this, lang(lng_chat_all_members_admins), false, st::defaultBoxCheckbox)
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
, _allAdmins(this, lang(lng_chat_all_members_admins), !_chat->adminsEnabled(), st::defaultBoxCheckbox)
, _aboutWidth(st::boxWideWidth - st::contactsPadding.left() - st::contactsPadding.right())
, _aboutAllAdmins(st::defaultTextStyle, lang(lng_chat_about_all_admins), _defaultOptions, _aboutWidth)
, _aboutAdmins(st::defaultTextStyle, lang(lng_chat_about_admins), _defaultOptions, _aboutWidth)
, _customList((membersFilter == MembersFilter::Recent) ? std::unique_ptr<Dialogs::IndexedList>() : std::make_unique<Dialogs::IndexedList>(Dialogs::SortMode::Add))
, _contacts((membersFilter == MembersFilter::Recent) ? App::main()->contactsList() : _customList.get())
, _addContactLnk(this, lang(lng_add_contact_button)) {
	initList();
	if (membersFilter == MembersFilter::Admins) {
		_aboutHeight = st::contactsAboutTop + qMax(_aboutAllAdmins.countHeight(_aboutWidth), _aboutAdmins.countHeight(_aboutWidth)) + st::contactsAboutBottom;
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
, _allAdmins(this, lang(lng_chat_all_members_admins), false, st::defaultBoxCheckbox)
, _customList(std::make_unique<Dialogs::IndexedList>(Dialogs::SortMode::Add))
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
			} else if (peer->isMegagroup()) {
				return true;
			}
			return false;
		});
	}
	init();
}

void ContactsBox::Inner::init() {
	subscribe(AuthSession::CurrentDownloaderTaskFinished(), [this] { update(); });
	connect(_addContactLnk, SIGNAL(clicked()), App::wnd(), SLOT(onShowAddContact()));
	subscribe(_allAdmins->checkedChanged, [this](bool checked) { onAllAdminsChanged(); });

	_rowsTop = st::contactsMarginTop;
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

	subscribe(Window::Theme::Background(), [this](const Window::Theme::BackgroundUpdate &update) {
		if (update.paletteChanged()) {
			invalidateCache();
		}
	});
}

void ContactsBox::Inner::invalidateCache() {
	for_const (auto data, _contactsData) {
		if (data->checkbox) {
			data->checkbox->invalidateCache();
		}
	}
	for_const (auto data, _byUsernameDatas) {
		if (data->checkbox) {
			data->checkbox->invalidateCache();
		}
	}
	for_const (auto data, d_byUsername) {
		if (data->checkbox) {
			data->checkbox->invalidateCache();
		}
	}
}

void ContactsBox::Inner::initList() {
	if (!_chat || _membersFilter != MembersFilter::Admins) return;

	QList<UserData*> admins, others;
	admins.reserve(_chat->admins.size() + 1);
	if (!_chat->participants.isEmpty()) {
		others.reserve(_chat->participants.size());
	}

	for (auto i = _chat->participants.cbegin(), e = _chat->participants.cend(); i != e; ++i) {
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
	if (auto creator = App::userLoaded(_chat->creator)) {
		if (_chat->participants.contains(creator)) {
			admins.push_front(creator);
		}
	}
	for_const (auto user, admins) {
		_contacts->addToEnd(App::history(user->id));
	}
	for_const (auto user, others) {
		_contacts->addToEnd(App::history(user->id));
	}
}

void ContactsBox::Inner::onPeerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars) {
	if (bot()) {
		_contacts->peerNameChanged(peer, oldNames, oldChars);
	}
	peerUpdated(peer);
}

void ContactsBox::Inner::addBot() {
	if (auto &info = _bot->botInfo) {
		if (!info->shareGameShortName.isEmpty()) {
			auto history = App::historyLoaded(_addToPeer);
			auto afterRequestId = history ? history->sendRequestId : 0;
			auto randomId = rand_value<uint64>();
			auto requestId = MTP::send(MTPmessages_SendMedia(MTP_flags(0), _addToPeer->input, MTP_int(0), MTP_inputMediaGame(MTP_inputGameShortName(_bot->inputUser, MTP_string(info->shareGameShortName))), MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, afterRequestId);
			if (history) {
				history->sendRequestId = requestId;
			}
		} else if (!info->startGroupToken.isEmpty()) {
			MTP::send(MTPmessages_StartBot(_bot->inputUser, _addToPeer->input, MTP_long(rand_value<uint64>()), MTP_string(info->startGroupToken)), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::addParticipantFail, { _bot, _addToPeer }));
		} else {
			App::main()->addParticipants(_addToPeer, std::vector<gsl::not_null<UserData*>>(1, _bot));
		}
	} else {
		App::main()->addParticipants(_addToPeer, std::vector<gsl::not_null<UserData*>>(1, _bot));
	}
	Ui::hideLayer();
	Ui::showPeerHistory(_addToPeer, ShowAtUnreadMsgId);
}

void ContactsBox::Inner::onAllAdminsChanged() {
	if (_saving && _allAdmins->checked() != _allAdminsChecked) {
		_allAdmins->setChecked(_allAdminsChecked);
	} else if (_allAdminsChangedCallback) {
		_allAdminsChangedCallback();
	}
	update();
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
					update(0, _rowsTop + _aboutHeight + _rowHeight * row->pos(), width(), _rowHeight);
				}
			}
			if (!_filter.isEmpty()) {
				for (int32 j = 0, s = _filtered.size(); j < s; ++j) {
					if (_filtered[j]->attached == i.value()) {
						_filtered[j]->attached = 0;
						update(0, _rowsTop + _rowHeight * j, width(), _rowHeight);
					}
				}
			}
			delete i.value();
			_contactsData.erase(i);
		}
	}
}

void ContactsBox::Inner::loadProfilePhotos() {
	if (_visibleTop >= _visibleBottom) return;

	auto yFrom = _visibleTop - _rowsTop;
	auto yTo = yFrom + (_visibleBottom - _visibleTop) * 5;
	AuthSession::Current().downloader().clearPriorities();

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
		auto from = yFrom / _rowHeight;
		if (from < 0) from = 0;
		if (from < _filtered.size()) {
			auto to = (yTo / _rowHeight) + 1;
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
					data->disabledChecked = (peer->id == AuthSession::CurrentUserPeerId());
				} else if (_channel) {
					data->disabledChecked = (peer->id == AuthSession::CurrentUserPeerId()) || _already.contains(peer->asUser());
				}
			}
			if (usingMultiSelect() && _checkedContacts.contains(peer)) {
				data->checkbox->setChecked(true, Ui::RoundImageCheckbox::SetStyle::Fast);
			}
			data->name.setText(st::contactsNameStyle, peer->name, _textNameOptions);
			if (peer->isUser()) {
				data->statusText = App::onlineText(peer->asUser(), _time);
				data->statusHasOnlineColor = App::onlineColorUse(peer->asUser(), _time);
			} else if (peer->isChat()) {
				auto chat = peer->asChat();
				if (!chat->amIn()) {
					data->statusText = lang(lng_chat_status_unaccessible);
				} else if (chat->count > 0) {
					data->statusText = lng_chat_status_members(lt_count, chat->count);
				} else {
					data->statusText = lang(lng_group_status);
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

bool ContactsBox::Inner::isRowDisabled(PeerData *peer, ContactData *data) const {
	if (_chat && _membersFilter == MembersFilter::Admins) {
		return (_saving || _allAdmins->checked() || peer->id == peerFromUser(_chat->creator));
	}
	return (data->disabledChecked || selectedCount() >= Global::MegagroupSizeMax());
}

void ContactsBox::Inner::paintDialog(Painter &p, TimeMs ms, PeerData *peer, ContactData *data, bool selected) {
	auto user = peer->asUser();

	if (isRowDisabled(peer, data)) {
		selected = false;
	}

	auto paintDisabledCheck = data->disabledChecked;
	if (_chat && _membersFilter == MembersFilter::Admins) {
		if (peer->id == peerFromUser(_chat->creator) || _allAdmins->checked()) {
			paintDisabledCheck = true;
		}
	}

	auto checkedRatio = 0.;
	p.fillRect(0, 0, width(), _rowHeight, selected ? st::contactsBgOver : st::contactsBg);
	if (data->ripple) {
		data->ripple->paint(p, 0, 0, width(), ms);
		if (data->ripple->empty()) {
			data->ripple.reset();
		}
	}
	if (paintDisabledCheck) {
		paintDisabledCheckUserpic(p, peer, st::contactsPadding.left(), st::contactsPadding.top(), width());
	} else if (usingMultiSelect()) {
		checkedRatio = data->checkbox->checkedAnimationRatio();
		data->checkbox->paint(p, ms, st::contactsPadding.left(), st::contactsPadding.top(), width());
	} else {
		peer->paintUserpicLeft(p, st::contactsPadding.left(), st::contactsPadding.top(), width(), st::contactsPhotoSize);
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
			p.setPen(selected ? st::contactsStatusFgOver : st::contactsStatusFg);
			p.drawTextLeft(namex + w, st::contactsPadding.top() + st::contactsStatusTop, width() + w, second);
		}
	} else {
		if ((user && (uname || data->statusHasOnlineColor)) || (peer->isChannel() && uname)) {
			p.setPen(st::contactsStatusFgOnline);
		} else {
			p.setPen(selected ? st::contactsStatusFgOver : st::contactsStatusFg);
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

	auto iconDiameter = st::contactsPhotoCheckbox.check.size;
	auto iconLeft = x + userpicDiameter + st::contactsPhotoCheckbox.selectWidth - iconDiameter;
	auto iconTop = y + userpicDiameter + st::contactsPhotoCheckbox.selectWidth - iconDiameter;
	auto iconEllipse = rtlrect(iconLeft, iconTop, iconDiameter, iconDiameter, outerWidth);
	auto iconBorderPen = st::contactsPhotoCheckbox.check.border->p;
	iconBorderPen.setWidth(st::contactsPhotoCheckbox.selectWidth);

	peer->paintUserpicLeft(p, userpicLeft, userpicTop, outerWidth, userpicRadius * 2);

	{
		PainterHighQualityEnabler hq(p);

		p.setPen(userpicBorderPen);
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(userpicEllipse);

		p.setPen(iconBorderPen);
		p.setBrush(st::contactsPhotoDisabledCheckFg);
		p.drawEllipse(iconEllipse);
	}

	st::contactsPhotoCheckbox.check.check.paint(p, iconEllipse.topLeft(), outerWidth);
}

void ContactsBox::Inner::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	Painter p(this);

	p.setClipRect(r);
	_time = unixtime();
	p.fillRect(r, st::contactsBg);

	auto ms = getms();
	auto yFrom = r.y(), yTo = r.y() + r.height();
	auto skip = _rowsTop;
	if (_filter.isEmpty()) {
		skip += _aboutHeight;
		if (!_contacts->isEmpty() || !_byUsername.isEmpty()) {
			if (_aboutHeight) {
				auto infoTop = _allAdmins->bottomNoMargins() + st::contactsAllAdminsTop - st::lineWidth;

				auto infoRect = rtlrect(0, infoTop, width(), _aboutHeight - infoTop - st::contactsPadding.bottom(), width());
				p.fillRect(infoRect, st::contactsAboutBg);
				auto dividerFillTop = rtlrect(0, infoRect.y(), width(), st::profileDividerTop.height(), width());
				st::profileDividerTop.fill(p, dividerFillTop);
				auto dividerFillBottom = rtlrect(0, infoRect.y() + infoRect.height() - st::profileDividerBottom.height(), width(), st::profileDividerBottom.height(), width());
				st::profileDividerBottom.fill(p, dividerFillBottom);

				int aboutw = width() - st::contactsPadding.left() - st::contactsPadding.right();
				p.setPen(st::contactsAboutFg);
				(_allAdmins->checked() ? _aboutAllAdmins : _aboutAdmins).draw(p, st::contactsPadding.left(), st::contactsAboutTop, aboutw);
			}
			yFrom -= skip;
			yTo -= skip;
			p.translate(0, skip);
			if (!_contacts->isEmpty()) {
				auto i = _contacts->cfind(yFrom, _rowHeight);
				p.translate(0, (*i)->pos() * _rowHeight);
				for (auto end = _contacts->cend(); i != end; ++i) {
					if ((*i)->pos() * _rowHeight >= yTo) {
						break;
					}
					auto selected = _pressed ? (*i == _pressed) : (*i == _selected);
					paintDialog(p, ms, (*i)->history()->peer, contactData(*i), selected);
					p.translate(0, _rowHeight);
				}
				yFrom -= _contacts->size() * _rowHeight;
				yTo -= _contacts->size() * _rowHeight;
			}
			if (!_byUsername.isEmpty()) {
				p.fillRect(0, 0, width(), st::searchedBarHeight, st::searchedBarBg);
				p.setFont(st::searchedBarFont);
				p.setPen(st::searchedBarFg);
				p.drawTextLeft(st::searchedBarPosition.x(), st::searchedBarPosition.y(), width(), lang(lng_search_global_results), style::al_center);

				yFrom -= st::searchedBarHeight;
				yTo -= st::searchedBarHeight;
				p.translate(0, st::searchedBarHeight);

				auto from = floorclamp(yFrom, _rowHeight, 0, _byUsername.size());
				auto to = ceilclamp(yTo, _rowHeight, 0, _byUsername.size());
				p.translate(0, from * _rowHeight);
				for (; from < to; ++from) {
					auto selected = (_searchedPressed >= 0) ? (_searchedPressed == from) : (_searchedSelected == from);
					paintDialog(p, ms, _byUsername[from], d_byUsername[from], selected);
					p.translate(0, _rowHeight);
				}
			}
		} else {
			QString text;
			skip = 0;
			if (bot()) {
				text = lang((AuthSession::Current().data().allChatsLoaded().value() && !_searching) ? (sharingBotGame() ? lng_bot_no_chats : lng_bot_no_groups) : lng_contacts_loading);
			} else if (_chat && _membersFilter == MembersFilter::Admins) {
				text = lang(lng_contacts_loading);
				p.fillRect(0, 0, width(), _aboutHeight - st::contactsPadding.bottom() - st::lineWidth, st::contactsAboutBg);
				p.fillRect(0, _aboutHeight - st::contactsPadding.bottom() - st::lineWidth, width(), st::lineWidth, st::shadowFg);

				int aboutw = width() - st::contactsPadding.left() - st::contactsPadding.right();
				(_allAdmins->checked() ? _aboutAllAdmins : _aboutAdmins).draw(p, st::contactsPadding.left(), st::contactsAboutTop, aboutw);
				p.translate(0, _aboutHeight);
			} else if (AuthSession::Current().data().contactsLoaded().value() && !_searching) {
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
			p.setFont(st::noContactsFont);
			p.setPen(st::noContactsColor);
			QString text;
			if (bot()) {
				text = lang((AuthSession::Current().data().allChatsLoaded().value() && !_searching) ? (sharingBotGame() ? lng_bot_chats_not_found : lng_bot_groups_not_found) : lng_contacts_loading);
			} else if (_chat && _membersFilter == MembersFilter::Admins) {
				text = lang(_chat->participants.isEmpty() ? lng_contacts_loading : lng_contacts_not_found);
			} else {
				text = lang((AuthSession::Current().data().contactsLoaded().value() && !_searching) ? lng_contacts_not_found : lng_contacts_loading);
			}
			p.drawText(QRect(0, 0, width(), st::noContactsHeight), text, style::al_center);
		} else {
			yFrom -= skip;
			yTo -= skip;
			p.translate(0, skip);
			if (!_filtered.isEmpty()) {
				int32 from = floorclamp(yFrom, _rowHeight, 0, _filtered.size());
				int32 to = ceilclamp(yTo, _rowHeight, 0, _filtered.size());
				p.translate(0, from * _rowHeight);
				for (; from < to; ++from) {
					auto selected = (_filteredPressed >= 0) ? (_filteredPressed == from) : (_filteredSelected == from);
					paintDialog(p, ms, _filtered[from]->history()->peer, contactData(_filtered[from]), selected);
					p.translate(0, _rowHeight);
				}
			}
			if (!_byUsernameFiltered.isEmpty()) {
				p.fillRect(0, 0, width(), st::searchedBarHeight, st::searchedBarBg);
				p.setFont(st::searchedBarFont);
				p.setPen(st::searchedBarFg);
				p.drawTextLeft(st::searchedBarPosition.x(), st::searchedBarPosition.y(), width(), lang(lng_search_global_results), style::al_center);
				p.translate(0, st::searchedBarHeight);

				yFrom -= _filtered.size() * _rowHeight + st::searchedBarHeight;
				yTo -= _filtered.size() * _rowHeight + st::searchedBarHeight;
				int32 from = floorclamp(yFrom, _rowHeight, 0, _byUsernameFiltered.size());
				int32 to = ceilclamp(yTo, _rowHeight, 0, _byUsernameFiltered.size());
				p.translate(0, from * _rowHeight);
				for (; from < to; ++from) {
					auto selected = (_searchedPressed >= 0) ? (_searchedPressed == from) : (_searchedSelected == from);
					paintDialog(p, ms, _byUsernameFiltered[from], d_byUsernameFiltered[from], selected);
					p.translate(0, _rowHeight);
				}
			}
		}
	}
}

void ContactsBox::Inner::enterEventHook(QEvent *e) {
	setMouseTracking(true);
}

int ContactsBox::Inner::getSelectedRowTop() const {
	if (_filter.isEmpty()) {
		if (_selected) {
			return _rowsTop + _aboutHeight + (_selected->pos() * _rowHeight);
		} else if (_searchedSelected >= 0) {
			return _rowsTop + _aboutHeight + (_contacts->size() * _rowHeight) + st::searchedBarHeight + (_searchedSelected * _rowHeight);
		}
	} else {
		if (_filteredSelected >= 0) {
			return _rowsTop + (_filteredSelected * _rowHeight);
		} else if (_searchedSelected >= 0) {
			return _rowsTop + (_filtered.size() * _rowHeight + st::searchedBarHeight + _searchedSelected * _rowHeight);
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
				return _rowsTop + _aboutHeight + ((*i)->pos() * _rowHeight);
			}
		}
		for (auto i = 0, count = _byUsername.size(); i != count; ++i) {
			if (_byUsername[i] == peer) {
				return _rowsTop + _aboutHeight + (_contacts->size() * _rowHeight) + st::searchedBarHeight + (i * _rowHeight);
			}
		}
	} else {
		for (auto i = 0, count = _filtered.size(); i != count; ++i) {
			if (_filtered[i]->history()->peer == peer) {
				return _rowsTop + (i * _rowHeight);
			}
		}
		for (auto i = 0, count = _byUsernameFiltered.size(); i != count; ++i) {
			if (_byUsernameFiltered[i] == peer) {
				return _rowsTop + (_contacts->size() * _rowHeight) + st::searchedBarHeight + (i * _rowHeight);
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

void ContactsBox::Inner::leaveEventHook(QEvent *e) {
	_mouseSelection = false;
	setMouseTracking(false);
	if (_selected || _filteredSelected >= 0 || _searchedSelected >= 0) {
		updateSelectedRow();
		_selected = nullptr;
		_filteredSelected = _searchedSelected = -1;
	}
}

void ContactsBox::Inner::mouseMoveEvent(QMouseEvent *e) {
	auto position = e->globalPos();
	if (_mouseSelection || _lastMousePos != position) {
		_mouseSelection = true;
		_lastMousePos = e->globalPos();
		updateSelection();
	}
}

void ContactsBox::Inner::mousePressEvent(QMouseEvent *e) {
	_mouseSelection = true;
	_lastMousePos = e->globalPos();
	updateSelection();

	setPressed(_selected);
	setFilteredPressed(_filteredSelected);
	setSearchedPressed(_searchedSelected);
	if (_selected) {
		addRipple(_selected->history()->peer, contactData(_selected));
	} else if (_filteredSelected >= 0 && _filteredSelected < _filtered.size()) {
		addRipple(_filtered[_filteredSelected]->history()->peer, contactData(_filtered[_filteredSelected]));
	} else if (_searchedSelected >= 0) {
		if (_filter.isEmpty() && _searchedSelected < d_byUsername.size()) {
			addRipple(_byUsername[_searchedSelected], d_byUsername[_searchedSelected]);
		} else if (!_filter.isEmpty() && _searchedSelected < d_byUsernameFiltered.size()) {
			addRipple(_byUsernameFiltered[_searchedSelected], d_byUsernameFiltered[_searchedSelected]);
		}
	}
}

void ContactsBox::Inner::mouseReleaseEvent(QMouseEvent *e) {
	auto pressed = _pressed;
	setPressed(nullptr);
	auto filteredPressed = _filteredPressed;
	setFilteredPressed(-1);
	auto searchedPressed = _searchedPressed;
	setSearchedPressed(-1);
	updateSelectedRow();
	if (e->button() == Qt::LeftButton) {
		if (pressed && pressed == _selected) {
			chooseParticipant();
		} else if (filteredPressed >= 0 && filteredPressed == _filteredSelected) {
			chooseParticipant();
		} else if (searchedPressed >= 0 && searchedPressed == _searchedSelected) {
			chooseParticipant();
		}
	}
}

void ContactsBox::Inner::addRipple(PeerData *peer, ContactData *data) {
	if (isRowDisabled(peer, data)) return;

	auto rowTop = getSelectedRowTop();
	if (!data->ripple) {
		auto mask = Ui::RippleAnimation::rectMask(QSize(width(), _rowHeight));
		data->ripple = std::make_unique<Ui::RippleAnimation>(st::contactsRipple, std::move(mask), [this, data] {
			updateRowWithTop(data->rippleRowTop);
		});
	}
	data->rippleRowTop = rowTop;
	data->ripple->add(mapFromGlobal(QCursor::pos()) - QPoint(0, rowTop));
}

void ContactsBox::Inner::stopLastRipple(ContactData *data) {
	if (data->ripple) {
		data->ripple->lastStop();
	}
}

void ContactsBox::Inner::setPressed(Dialogs::Row *pressed) {
	if (_pressed != pressed) {
		if (_pressed) {
			stopLastRipple(contactData(_pressed));
		}
		_pressed = pressed;
	}
}

void ContactsBox::Inner::setFilteredPressed(int pressed) {
	if (_filteredPressed >= 0 && _filteredPressed < _filtered.size()) {
		stopLastRipple(contactData(_filtered[_filteredPressed]));
	}
	_filteredPressed = pressed;
}

void ContactsBox::Inner::setSearchedPressed(int pressed) {
	if (_searchedPressed >= 0) {
		if (_searchedPressed < d_byUsername.size()) {
			stopLastRipple(d_byUsername[_searchedPressed]);
		}
		if (_searchedPressed < d_byUsernameFiltered.size()) {
			stopLastRipple(d_byUsernameFiltered[_searchedPressed]);
		}
	}
	_searchedPressed = pressed;
}

void ContactsBox::Inner::changeMultiSelectCheckState() {
	_time = unixtime();
	if (_filter.isEmpty()) {
		if (_searchedSelected >= 0 && _searchedSelected < _byUsername.size()) {
			auto data = d_byUsername[_searchedSelected];
			auto peer = _byUsername[_searchedSelected];
			if (data->disabledChecked) return;

			changeCheckState(data, peer);
		} else if (_selected) {
			auto data = contactData(_selected);
			auto peer = _selected->history()->peer;
			if (data->disabledChecked) return;

			changeCheckState(_selected);
		}
	} else {
		if (_searchedSelected >= 0 && _searchedSelected < _byUsernameFiltered.size()) {
			auto data = d_byUsernameFiltered[_searchedSelected];
			auto peer = _byUsernameFiltered[_searchedSelected];
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
		} else if (_filteredSelected >= 0 && _filteredSelected < _filtered.size()) {
			auto data = contactData(_filtered[_filteredSelected]);
			auto peer = _filtered[_filteredSelected]->history()->peer;
			if (data->disabledChecked) return;

			changeCheckState(data, peer);
		}
	}
}

PeerData *ContactsBox::Inner::selectedPeer() const {
	if (_filter.isEmpty()) {
		if (_searchedSelected >= 0 && _searchedSelected < _byUsername.size()) {
			return _byUsername[_searchedSelected];
		} else if (_selected) {
			return _selected->history()->peer;
		}
	} else {
		if (_searchedSelected >= 0 && _searchedSelected < _byUsernameFiltered.size()) {
			return _byUsernameFiltered[_searchedSelected];
		} else if (_filteredSelected >= 0 && _filteredSelected < _filtered.size()) {
			return _filtered[_filteredSelected]->history()->peer;
		}
	}
	return nullptr;
}

void ContactsBox::Inner::chooseParticipant() {
	if (_saving) {
		return;
	}

	if (usingMultiSelect()) {
		changeMultiSelectCheckState();
	} else {
		if (_channel && _membersFilter == MembersFilter::Admins) {
			Unexpected("Not supported any more");
		} else if (sharingBotGame()) {
			shareBotGameToSelected();
		} else if (bot()) {
			addBotToSelectedGroup();
		} else if (auto peer = selectedPeer()) {
			Ui::hideSettingsAndLayer(true);
			App::main()->choosePeer(peer->id, ShowAtUnreadMsgId);
		}
	}
	update();
}

void ContactsBox::Inner::shareBotGameToSelected() {
	_addToPeer = selectedPeer();
	if (!_addToPeer) {
		return;
	}

	auto confirmText = [this] {
		if (_addToPeer->isUser()) {
			return lng_bot_sure_share_game(lt_user, App::peerName(_addToPeer));
		}
		return lng_bot_sure_share_game_group(lt_group, _addToPeer->name);
	};
	Ui::show(Box<ConfirmBox>(confirmText(), base::lambda_guarded(this, [this] {
		addBot();
	})), KeepOtherLayers);
}

void ContactsBox::Inner::addBotToSelectedGroup() {
	_addToPeer = selectedPeer();
	if (!_addToPeer) {
		return;
	}

	if (auto megagroup = _addToPeer->asMegagroup()) {
		if (!megagroup->canAddMembers()) {
			Ui::show(Box<InformBox>(lang(lng_error_cant_add_member)), KeepOtherLayers);
			return;
		}
	}
	if (_addToPeer->isChat() || _addToPeer->isMegagroup()) {
		Ui::show(Box<ConfirmBox>(lng_bot_sure_invite(lt_group, _addToPeer->name), base::lambda_guarded(this, [this] {
			addBot();
		})), KeepOtherLayers);
	}
}

void ContactsBox::Inner::changeCheckState(Dialogs::Row *row) {
	changeCheckState(contactData(row), row->history()->peer);
}

void ContactsBox::Inner::changeCheckState(ContactData *data, PeerData *peer) {
	Expects(usingMultiSelect());

	if (isRowDisabled(peer, data)) {
	} else if (data->checkbox->checked()) {
		changePeerCheckState(data, peer, false);
	} else if (selectedCount() < ((_channel && _channel->isMegagroup()) ? Global::MegagroupSizeMax() : Global::ChatSizeMax())) {
		changePeerCheckState(data, peer, true);
	} else if (_channel && !_channel->isMegagroup()) {
		Ui::show(Box<MaxInviteBox>(_channel), KeepOtherLayers);
	} else if (!_channel && selectedCount() >= Global::ChatSizeMax() && selectedCount() < Global::MegagroupSizeMax()) {
		Ui::show(Box<InformBox>(lng_profile_add_more_after_upgrade(lt_count, Global::MegagroupSizeMax())), KeepOtherLayers);
	}
}

void ContactsBox::Inner::peerUnselected(PeerData *peer) {
	// If data is nullptr we simply won't do anything.
	auto data = _contactsData.value(peer, nullptr);
	changePeerCheckState(data, peer, false, ChangeStateWay::SkipCallback);
}

void ContactsBox::Inner::setPeerSelectedChangedCallback(base::lambda<void(PeerData *peer, bool selected)> callback) {
	_peerSelectedChangedCallback = std::move(callback);
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

int ContactsBox::Inner::selectedCount() const {
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

void ContactsBox::Inner::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
	loadProfilePhotos();
}

void ContactsBox::Inner::updateSelection() {
	if (!_mouseSelection) return;

	auto p = mapFromGlobal(_lastMousePos);
	auto in = parentWidget()->rect().contains(parentWidget()->mapFromGlobal(_lastMousePos));
	p.setY(p.y() - _rowsTop);
	if (_filter.isEmpty()) {
		_filteredSelected = -1;
		setFilteredPressed(-1);
		if (_aboutHeight) {
			p.setY(p.y() - _aboutHeight);
		}
		auto selected = (in && (p.y() >= 0) && (p.y() < _contacts->size() * _rowHeight)) ? _contacts->rowAtY(p.y(), _rowHeight) : nullptr;
		auto searchedSelected = (in && (p.y() >= _contacts->size() * _rowHeight + st::searchedBarHeight)) ? ((p.y() - _contacts->size() * _rowHeight - st::searchedBarHeight) / _rowHeight) : -1;
		if (searchedSelected >= _byUsername.size()) searchedSelected = -1;
		if (_selected != selected || _searchedSelected != searchedSelected) {
			updateSelectedRow();
			_selected = selected;
			_searchedSelected = searchedSelected;
			updateSelectedRow();
		}
	} else {
		_selected = nullptr;
		setPressed(nullptr);
		auto filteredSelected = (in && (p.y() >= 0) && (p.y() < _filtered.size() * _rowHeight)) ? (p.y() / _rowHeight) : -1;
		auto searchedSelected = (in && (p.y() >= _filtered.size() * _rowHeight + st::searchedBarHeight)) ? ((p.y() - _filtered.size() * _rowHeight - st::searchedBarHeight) / _rowHeight) : -1;
		if (searchedSelected >= _byUsernameFiltered.size()) searchedSelected = -1;
		if (_filteredSelected != filteredSelected || _searchedSelected != searchedSelected) {
			updateSelectedRow();
			_filteredSelected = filteredSelected;
			_searchedSelected = searchedSelected;
			updateSelectedRow();
		}
	}
}

void ContactsBox::Inner::updateFilter(QString filter) {
	_lastQuery = filter.toLower().trimmed();

	auto words = TextUtilities::PrepareSearchWords(_lastQuery);
	filter = words.isEmpty() ? QString() : words.join(' ');

	_time = unixtime();
	if (_filter != filter) {
		_filter = filter;

		_byUsernameFiltered.clear();
		d_byUsernameFiltered.clear();
		clearSearchedContactDatas();

		_selected = nullptr;
		setPressed(nullptr);
		_filteredSelected = -1;
		setFilteredPressed(-1);
		_searchedSelected = -1;
		setSearchedPressed(-1);
		if (_filter.isEmpty()) {
			refresh();
		} else {
			if (!_addContactLnk->isHidden()) _addContactLnk->hide();
			if (!_allAdmins->isHidden()) _allAdmins->hide();
			QStringList::const_iterator fb = words.cbegin(), fe = words.cend(), fi;

			_filtered.clear();
			if (!words.isEmpty()) {
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
			if (!_filtered.isEmpty()) {
				for (_filteredSelected = 0; (_filteredSelected < _filtered.size()) && contactData(_filtered[_filteredSelected])->disabledChecked;) {
					++_filteredSelected;
				}
				if (_filteredSelected == _filtered.size()) _filteredSelected = -1;
			}
			if (_filteredSelected < 0 && !_byUsernameFiltered.isEmpty()) {
				for (_searchedSelected = 0; (_searchedSelected < _byUsernameFiltered.size()) && d_byUsernameFiltered[_searchedSelected]->disabledChecked;) {
					++_searchedSelected;
				}
				if (_searchedSelected == _byUsernameFiltered.size()) _searchedSelected = -1;
			}
			_mouseSelection = false;
			refresh();

			if ((!bot() || sharingBotGame()) && (!_chat || _membersFilter != MembersFilter::Admins)) {
				_searching = true;
				emit searchByUsername();
			}
		}
		update();
		loadProfilePhotos();
	}
}

void ContactsBox::Inner::clearSearchedContactDatas() {
	for (auto data : base::take(_byUsernameDatas)) {
		delete data;
	}
}

void ContactsBox::Inner::onDialogRowReplaced(Dialogs::Row *oldRow, Dialogs::Row *newRow) {
	if (!_filter.isEmpty()) {
		for (auto i = _filtered.begin(), e = _filtered.end(); i != e;) {
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
		if (_filteredSelected >= _filtered.size()) {
			_filteredSelected = -1;
		}
		if (_filteredPressed >= _filtered.size()) {
			_filteredPressed = -1;
		}
	} else {
		if (_selected == oldRow) {
			_selected = newRow;
		}
		if (_pressed == oldRow) {
			setPressed(newRow);
		}
	}
	refresh();
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
			data->name.setText(st::contactsNameStyle, peer->name, _textNameOptions);
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
			resize(width(), _rowsTop + _aboutHeight + (_contacts->size() * _rowHeight) + (_byUsername.isEmpty() ? 0 : (st::searchedBarHeight + _byUsername.size() * _rowHeight)) + st::contactsMarginBottom);
		} else if (_chat && _membersFilter == MembersFilter::Admins) {
			if (!_addContactLnk->isHidden()) _addContactLnk->hide();
			resize(width(), _rowsTop + _aboutHeight + st::noContactsHeight + st::contactsMarginBottom);
		} else {
			if (AuthSession::Current().data().contactsLoaded().value() && !bot()) {
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
			resize(width(), _rowsTop + (_filtered.size() * _rowHeight) + (_byUsernameFiltered.isEmpty() ? 0 : (st::searchedBarHeight + _byUsernameFiltered.size() * _rowHeight)) + st::contactsMarginBottom);
		}
	}
	loadProfilePhotos();
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
	clearSearchedContactDatas();
	for (auto data : base::take(d_byUsername)) {
		delete data;
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
	_mouseSelection = false;
	if (_filter.isEmpty()) {
		int cur = 0;
		if (_selected) {
			for (auto i = _contacts->cbegin(); *i != _selected; ++i) {
				++cur;
			}
		} else if (_searchedSelected >= 0) {
			cur = (_contacts->size() + _searchedSelected);
		} else {
			cur = -1;
		}
		cur += dir;
		if (cur <= 0) {
			_selected = (!_contacts->isEmpty()) ? *_contacts->cbegin() : nullptr;
			_searchedSelected = (_contacts->isEmpty() && !_byUsername.isEmpty()) ? 0 : -1;
		} else if (cur >= _contacts->size()) {
			if (_byUsername.isEmpty()) {
				_selected = _contacts->isEmpty() ? nullptr : *(_contacts->cend() - 1);
				_searchedSelected = -1;
			} else {
				_selected = nullptr;
				_searchedSelected = cur - _contacts->size();
				if (_searchedSelected >= _byUsername.size()) _searchedSelected = _byUsername.size() - 1;
			}
		} else {
			for (auto i = _contacts->cbegin(); ; ++i) {
				_selected = *i;
				if (!cur) {
					break;
				} else {
					--cur;
				}
			}
			_searchedSelected = -1;
		}
		if (dir > 0) {
			for (auto i = _contacts->cfind(_selected), end = _contacts->cend(); i != end && contactData(*i)->disabledChecked; ++i) {
				_selected = *i;
			}
			if (_selected && contactData(_selected)->disabledChecked) {
				_selected = nullptr;
			}
			if (!_selected) {
				if (!_byUsername.isEmpty()) {
					if (_searchedSelected < 0) _searchedSelected = 0;
					for (; _searchedSelected < _byUsername.size() && d_byUsername[_searchedSelected]->disabledChecked;) {
						++_searchedSelected;
					}
					if (_searchedSelected == _byUsername.size()) _searchedSelected = -1;
				}
			}
		} else {
			while (_searchedSelected >= 0 && d_byUsername[_searchedSelected]->disabledChecked) {
				--_searchedSelected;
			}
			if (_searchedSelected < 0) {
				if (!_contacts->isEmpty()) {
					if (!_selected) _selected = *(_contacts->cend() - 1);
					if (_selected) {
						for (auto i = _contacts->cfind(_selected), b = _contacts->cbegin(); i != b && contactData(*i)->disabledChecked; --i) {
							_selected = *i;
						}
						if (contactData(_selected)->disabledChecked) {
							_selected = nullptr;
						}
					}
				}
			}
		}
		if (_selected) {
			emit mustScrollTo(_rowsTop + _aboutHeight + _selected->pos() * _rowHeight, _rowsTop + _aboutHeight + (_selected->pos() + 1) * _rowHeight);
		} else if (_searchedSelected >= 0) {
			emit mustScrollTo(_rowsTop + _aboutHeight + (_contacts->size() + _searchedSelected) * _rowHeight + st::searchedBarHeight, _rowsTop + _aboutHeight + (_contacts->size() + _searchedSelected + 1) * _rowHeight + st::searchedBarHeight);
		}
	} else {
		int cur = (_filteredSelected >= 0) ? _filteredSelected : ((_searchedSelected >= 0) ? (_filtered.size() + _searchedSelected) : -1);
		cur += dir;
		if (cur <= 0) {
			_filteredSelected = _filtered.isEmpty() ? -1 : 0;
			_searchedSelected = (_filtered.isEmpty() && !_byUsernameFiltered.isEmpty()) ? 0 : -1;
		} else if (cur >= _filtered.size()) {
			_filteredSelected = -1;
			_searchedSelected = cur - _filtered.size();
			if (_searchedSelected >= _byUsernameFiltered.size()) _searchedSelected = _byUsernameFiltered.size() - 1;
		} else {
			_filteredSelected = cur;
			_searchedSelected = -1;
		}
		if (dir > 0) {
			while (_filteredSelected >= 0 && _filteredSelected < _filtered.size() && contactData(_filtered[_filteredSelected])->disabledChecked) {
				++_filteredSelected;
			}
			if (_filteredSelected < 0 || _filteredSelected >= _filtered.size()) {
				_filteredSelected = -1;
				if (!_byUsernameFiltered.isEmpty()) {
					if (_searchedSelected < 0) _searchedSelected = 0;
					for (; _searchedSelected < _byUsernameFiltered.size() && d_byUsernameFiltered[_searchedSelected]->disabledChecked;) {
						++_searchedSelected;
					}
					if (_searchedSelected == _byUsernameFiltered.size()) _searchedSelected = -1;
				}
			}
		} else {
			while (_searchedSelected >= 0 && d_byUsernameFiltered[_searchedSelected]->disabledChecked) {
				--_searchedSelected;
			}
			if (_searchedSelected < 0) {
				if (!_filtered.isEmpty()) {
					if (_filteredSelected < 0) _filteredSelected = _filtered.size() - 1;
					for (; _filteredSelected >= 0 && contactData(_filtered[_filteredSelected])->disabledChecked;) {
						--_filteredSelected;
					}
				}
			}
		}
		if (_filteredSelected >= 0) {
			emit mustScrollTo(_rowsTop + _filteredSelected * _rowHeight, _rowsTop + (_filteredSelected + 1) * _rowHeight);
		} else if (_searchedSelected >= 0) {
			int skip = _filtered.size() * _rowHeight + st::searchedBarHeight;
			emit mustScrollTo(_rowsTop + skip + _searchedSelected * _rowHeight, _rowsTop + skip + (_searchedSelected + 1) * _rowHeight);
		}
	}
	update();
}

void ContactsBox::Inner::selectSkipPage(int32 h, int32 dir) {
	int32 points = h / _rowHeight;
	if (!points) return;
	selectSkip(points * dir);
}

std::vector<gsl::not_null<UserData*>> ContactsBox::Inner::selected() {
	std::vector<gsl::not_null<UserData*>> result;
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
		if (i.value()->checkbox->checked()) {
			if (auto user = i.key()->asUser()) {
				result.push_back(user);
			}
		}
	}
	for (int i = 0, l = _byUsername.size(); i < l; ++i) {
		if (d_byUsername[i]->checkbox->checked()) {
			if (auto user = _byUsername[i]->asUser()) {
				result.push_back(user);
			}
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
