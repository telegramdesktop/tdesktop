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
#include "settings/settings_privacy_controllers.h"

#include "lang.h"
#include "apiwrap.h"
#include "observer_peer.h"
#include "mainwidget.h"
#include "dialogs/dialogs_indexed_list.h"
#include "auth_session.h"

namespace Settings {
namespace {

constexpr auto kBlockedPerPage = 40;

class BlockUserBoxController : public PeerListBox::Controller, private base::Subscriber {
public:
	void prepare() override;
	void rowClicked(PeerData *peer) override;
	std::unique_ptr<PeerListBox::Row> createGlobalRow(PeerData *peer) override;

private:
	void rebuildRows();
	void checkForEmptyRows();
	void updateIsBlocked(PeerListBox::Row *row, UserData *user) const;
	bool appendRow(History *history);

	class Row : public PeerListBox::Row {
	public:
		Row(History *history) : PeerListBox::Row(history->peer), _history(history) {
		}
		History *history() const {
			return _history;
		}

	private:
		History *_history = nullptr;

	};
	std::unique_ptr<Row> createRow(History *history) const;

};

void BlockUserBoxController::prepare() {
	view()->setTitle(lang(lng_blocked_list_add_title));
	view()->addButton(lang(lng_cancel), [this] { view()->closeBox(); });
	view()->setSearchMode(PeerListBox::SearchMode::Global);
	view()->setSearchNoResultsText(lang(lng_blocked_list_not_found));

	rebuildRows();

	auto &sessionData = AuthSession::Current().data();
	subscribe(sessionData.contactsLoaded(), [this](bool loaded) {
		rebuildRows();
	});
	subscribe(sessionData.moreChatsLoaded(), [this] {
		rebuildRows();
	});
	subscribe(sessionData.allChatsLoaded(), [this](bool loaded) {
		checkForEmptyRows();
	});
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::UserIsBlocked, [this](const Notify::PeerUpdate &update) {
		if (auto user = update.peer->asUser()) {
			if (auto row = view()->findRow(user)) {
				updateIsBlocked(row, user);
				view()->updateRow(row);
			}
		}
	}));
}

void BlockUserBoxController::rebuildRows() {
	auto ms = getms();
	auto wasEmpty = !view()->fullRowsCount();
	auto appendList = [this](auto chats) {
		auto count = 0;
		for_const (auto row, chats->all()) {
			auto history = row->history();
			if (history->peer->isUser()) {
				if (appendRow(history)) {
					++count;
				}
			}
		}
		return count;
	};
	auto added = appendList(App::main()->dialogsList());
	added += appendList(App::main()->contactsNoDialogsList());
	if (!wasEmpty && added > 0) {
		view()->reorderRows([](auto &&begin, auto &&end) {
			// Place dialogs list before contactsNoDialogs list.
			std::stable_partition(begin, end, [](auto &row) {
				auto history = static_cast<Row&>(*row).history();
				return history->inChatList(Dialogs::Mode::All);
			});
		});
	}
	checkForEmptyRows();
	view()->refreshRows();
}

void BlockUserBoxController::checkForEmptyRows() {
	if (view()->fullRowsCount()) {
		view()->setAboutText(QString());
	} else {
		auto &sessionData = AuthSession::Current().data();
		auto loaded = sessionData.contactsLoaded().value() && sessionData.allChatsLoaded().value();
		view()->setAboutText(lang(loaded ? lng_contacts_not_found : lng_contacts_loading));
	}
}

void BlockUserBoxController::updateIsBlocked(PeerListBox::Row *row, UserData *user) const {
	auto blocked = user->isBlocked();
	row->setDisabled(blocked);
	if (blocked) {
		row->setCustomStatus(lang(lng_blocked_list_already_blocked));
	} else {
		row->clearCustomStatus();
	}
}

void BlockUserBoxController::rowClicked(PeerData *peer) {
	auto user = peer->asUser();
	t_assert(user != nullptr);

	App::api()->blockUser(user);
	view()->closeBox();
}

std::unique_ptr<PeerListBox::Row> BlockUserBoxController::createGlobalRow(PeerData *peer) {
	if (auto user = peer->asUser()) {
		return createRow(App::history(user));
	}
	return std::unique_ptr<Row>();
}

bool BlockUserBoxController::appendRow(History *history) {
	if (auto row = view()->findRow(history->peer)) {
		updateIsBlocked(row, history->peer->asUser());
		return false;
	}
	view()->appendRow(createRow(history));
	return true;
}

std::unique_ptr<BlockUserBoxController::Row> BlockUserBoxController::createRow(History *history) const {
	auto row = std::make_unique<Row>(history);
	updateIsBlocked(row.get(), history->peer->asUser());
	return row;
}

} // namespace

void BlockedBoxController::prepare() {
	view()->setTitle(lang(lng_blocked_list_title));
	view()->addButton(lang(lng_close), [this] { view()->closeBox(); });
	view()->addLeftButton(lang(lng_blocked_list_add), [this] { blockUser(); });
	view()->setAboutText(lang(lng_contacts_loading));
	view()->refreshRows();

	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::UserIsBlocked, [this](const Notify::PeerUpdate &update) {
		if (auto user = update.peer->asUser()) {
			handleBlockedEvent(user);
		}
	}));

	preloadRows();
}

void BlockedBoxController::preloadRows() {
	if (_loadRequestId || _allLoaded) {
		return;
	}

	_loadRequestId = MTP::send(MTPcontacts_GetBlocked(MTP_int(_offset), MTP_int(kBlockedPerPage)), rpcDone(base::lambda_guarded(this, [this](const MTPcontacts_Blocked &result) {
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
			view()->onScrollToY(0);
		}
	} else if (auto row = view()->findRow(user)) {
		view()->removeRow(row);
		view()->refreshRows();
	}
}

void BlockedBoxController::blockUser() {
	Ui::show(Box<PeerListBox>(std::make_unique<BlockUserBoxController>()), KeepOtherLayers);
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
