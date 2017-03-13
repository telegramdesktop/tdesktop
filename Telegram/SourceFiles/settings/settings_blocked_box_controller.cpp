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
#include "settings/settings_blocked_box_controller.h"

#include "lang.h"
#include "apiwrap.h"
#include "observer_peer.h"

namespace Settings {
namespace {

constexpr auto kPerPage = 40;

} // namespace

void BlockedBoxController::prepare() {
	view()->setTitle(lang(lng_blocked_list_title));
	view()->addButton(lang(lng_close), [this] { view()->closeBox(); });
	view()->setAboutText(lang(lng_contacts_loading));
	view()->refreshRows();

	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::UserIsBlocked, [this](const Notify::PeerUpdate &update) {
		if (!update.peer->isUser()) {
			return;
		}
		handleBlockedEvent(update.peer->asUser());
	}));

	preloadRows();
}

void BlockedBoxController::preloadRows() {
	if (_loadRequestId || _allLoaded) {
		return;
	}

	_loadRequestId = MTP::send(MTPcontacts_GetBlocked(MTP_int(_offset), MTP_int(kPerPage)), rpcDone(base::lambda_guarded(this, [this](const MTPcontacts_Blocked &result) {
		_loadRequestId = 0;

		if (!_offset) {
			view()->setAboutText(lang(lng_blocked_list_about));
		}

		auto handleContactsBlocked = [](auto &list) {
			App::feedUsers(list.vusers);
			return list.vblocked.v;
		};
		switch (result.type()) {
		case mtpc_contacts_blockedSlice: {
			receivedUsers(handleContactsBlocked(result.c_contacts_blockedSlice()));
		} break;
		case mtpc_contacts_blocked: {
			_allLoaded = true;
			receivedUsers(handleContactsBlocked(result.c_contacts_blocked()));
		} break;
		default: t_assert(!"Bad type() in MTPcontacts_GetBlocked() result.");
		}
	})), rpcFail(base::lambda_guarded(this, [this](const RPCError &error) {
		if (MTP::isDefaultHandledError(error)) {
			return false;
		}
		_loadRequestId = 0;
		return true;
	})));
}

void BlockedBoxController::rowClicked(PeerData *peer) {
	Ui::showPeerHistoryAsync(peer->id, ShowAtUnreadMsgId);
}

void BlockedBoxController::rowActionClicked(PeerData *peer) {
	auto user = peer->asUser();
	t_assert(user != nullptr);

	App::api()->unblockUser(user);
}

void BlockedBoxController::receivedUsers(const QVector<MTPContactBlocked> &result) {
	if (result.empty()) {
		_allLoaded = true;
	}

	for_const (auto &item, result) {
		++_offset;
		if (item.type() != mtpc_contactBlocked) {
			continue;
		}
		auto &contactBlocked = item.c_contactBlocked();
		auto userId = contactBlocked.vuser_id.v;
		if (auto user = App::userLoaded(userId)) {
			appendRow(user);
			user->setBlockStatus(UserData::BlockStatus::Blocked);
		}
	}
	view()->refreshRows();
}

void BlockedBoxController::handleBlockedEvent(UserData *user) {
	if (user->isBlocked()) {
		if (prependRow(user)) {
			view()->refreshRows();
		}
	} else if (auto row = view()->findRow(user)) {
		view()->removeRow(row);
		view()->refreshRows();
	}
}

bool BlockedBoxController::appendRow(UserData *user) {
	if (view()->findRow(user)) {
		return false;
	}
	view()->appendRow(createRow(user));
	return true;
}

bool BlockedBoxController::prependRow(UserData *user) {
	if (view()->findRow(user)) {
		return false;
	}
	view()->prependRow(createRow(user));
	return true;
}

std::unique_ptr<PeerListBox::Row> BlockedBoxController::createRow(UserData *user) const {
	auto row = std::make_unique<PeerListBox::Row>(user);
	row->setActionLink(lang(lng_blocked_list_unblock));
	auto status = [user]() -> QString {
		if (user->botInfo) {
			return lang(lng_status_bot);
		} else if (user->phone().isEmpty()) {
			return lang(lng_blocked_list_unknown_phone);
		}
		return App::formatPhone(user->phone());
	};
	row->setCustomStatus(status());
	return row;
}

} // namespace Settings
