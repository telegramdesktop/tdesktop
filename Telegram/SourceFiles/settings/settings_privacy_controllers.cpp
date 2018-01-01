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

#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "observer_peer.h"
#include "mainwidget.h"
#include "auth_session.h"
#include "storage/localstorage.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/confirm_box.h"

namespace Settings {
namespace {

constexpr auto kBlockedPerPage = 40;

class BlockUserBoxController : public ChatsListBoxController {
public:
	void rowClicked(not_null<PeerListRow*> row) override;

	void setBlockUserCallback(base::lambda<void(not_null<UserData*> user)> callback) {
		_blockUserCallback = std::move(callback);
	}

protected:
	void prepareViewHook() override;
	std::unique_ptr<Row> createRow(not_null<History*> history) override;
	void updateRowHook(not_null<Row*> row) override {
		updateIsBlocked(row, row->peer()->asUser());
		delegate()->peerListUpdateRow(row);
	}

private:
	void updateIsBlocked(not_null<PeerListRow*> row, UserData *user) const;

	base::lambda<void(not_null<UserData*> user)> _blockUserCallback;

};

void BlockUserBoxController::prepareViewHook() {
	delegate()->peerListSetTitle(langFactory(lng_blocked_list_add_title));
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::UserIsBlocked, [this](const Notify::PeerUpdate &update) {
		if (auto user = update.peer->asUser()) {
			if (auto row = delegate()->peerListFindRow(user->id)) {
				updateIsBlocked(row, user);
				delegate()->peerListUpdateRow(row);
			}
		}
	}));
}

void BlockUserBoxController::updateIsBlocked(not_null<PeerListRow*> row, UserData *user) const {
	auto blocked = user->isBlocked();
	row->setDisabledState(blocked ? PeerListRow::State::DisabledChecked : PeerListRow::State::Active);
	if (blocked) {
		row->setCustomStatus(lang(lng_blocked_list_already_blocked));
	} else {
		row->clearCustomStatus();
	}
}

void BlockUserBoxController::rowClicked(not_null<PeerListRow*> row) {
	_blockUserCallback(row->peer()->asUser());
}

std::unique_ptr<BlockUserBoxController::Row> BlockUserBoxController::createRow(not_null<History*> history) {
	if (history->peer->isSelf()) {
		return nullptr;
	}
	if (auto user = history->peer->asUser()) {
		auto row = std::make_unique<Row>(history);
		updateIsBlocked(row.get(), user);
		return row;
	}
	return nullptr;
}

} // namespace

void BlockedBoxController::prepare() {
	delegate()->peerListSetTitle(langFactory(lng_blocked_list_title));
	setDescriptionText(lang(lng_contacts_loading));
	delegate()->peerListRefreshRows();

	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::UserIsBlocked, [this](const Notify::PeerUpdate &update) {
		if (auto user = update.peer->asUser()) {
			handleBlockedEvent(user);
		}
	}));

	loadMoreRows();
}

void BlockedBoxController::loadMoreRows() {
	if (_loadRequestId || _allLoaded) {
		return;
	}

	_loadRequestId = request(MTPcontacts_GetBlocked(MTP_int(_offset), MTP_int(kBlockedPerPage))).done([this](const MTPcontacts_Blocked &result) {
		_loadRequestId = 0;

		if (!_offset) {
			setDescriptionText(lang(lng_blocked_list_about));
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
		default: Unexpected("Bad type() in MTPcontacts_GetBlocked() result.");
		}
	}).fail([this](const RPCError &error) {
		_loadRequestId = 0;
	}).send();
}

void BlockedBoxController::rowClicked(not_null<PeerListRow*> row) {
	InvokeQueued(App::main(), [peerId = row->peer()->id] {
		Ui::showPeerHistory(peerId, ShowAtUnreadMsgId);
	});
}

void BlockedBoxController::rowActionClicked(not_null<PeerListRow*> row) {
	auto user = row->peer()->asUser();
	Expects(user != nullptr);

	Auth().api().unblockUser(user);
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
	delegate()->peerListRefreshRows();
}

void BlockedBoxController::handleBlockedEvent(UserData *user) {
	if (user->isBlocked()) {
		if (prependRow(user)) {
			delegate()->peerListRefreshRows();
			delegate()->peerListScrollToTop();
		}
	} else if (auto row = delegate()->peerListFindRow(user->id)) {
		delegate()->peerListRemoveRow(row);
		delegate()->peerListRefreshRows();
	}
}

void BlockedBoxController::BlockNewUser() {
	auto controller = std::make_unique<BlockUserBoxController>();
	auto initBox = [controller = controller.get()](not_null<PeerListBox*> box) {
		controller->setBlockUserCallback([box](not_null<UserData*> user) {
			Auth().api().blockUser(user);
			box->closeBox();
		});
		box->addButton(langFactory(lng_cancel), [box] { box->closeBox(); });
	};
	Ui::show(
		Box<PeerListBox>(std::move(controller), std::move(initBox)),
		LayerOption::KeepOther);
}

bool BlockedBoxController::appendRow(UserData *user) {
	if (delegate()->peerListFindRow(user->id)) {
		return false;
	}
	delegate()->peerListAppendRow(createRow(user));
	return true;
}

bool BlockedBoxController::prependRow(UserData *user) {
	if (delegate()->peerListFindRow(user->id)) {
		return false;
	}
	delegate()->peerListPrependRow(createRow(user));
	return true;
}

std::unique_ptr<PeerListRow> BlockedBoxController::createRow(UserData *user) const {
	auto row = std::make_unique<PeerListRowWithLink>(user);
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
	return std::move(row);
}

MTPInputPrivacyKey LastSeenPrivacyController::key() {
	return MTP_inputPrivacyKeyStatusTimestamp();
}

QString LastSeenPrivacyController::title() {
	return lang(lng_edit_privacy_lastseen_title);
}

QString LastSeenPrivacyController::description() {
	return lang(lng_edit_privacy_lastseen_description);
}

QString LastSeenPrivacyController::warning() {
	return lang(lng_edit_privacy_lastseen_warning);
}

QString LastSeenPrivacyController::exceptionLinkText(Exception exception, int count) {
	switch (exception) {
	case Exception::Always: return (count > 0) ? lng_edit_privacy_lastseen_always(lt_count, count) : lang(lng_edit_privacy_lastseen_always_empty);
	case Exception::Never: return (count > 0) ? lng_edit_privacy_lastseen_never(lt_count, count) : lang(lng_edit_privacy_lastseen_never_empty);
	}
	Unexpected("Invalid exception value.");
}

QString LastSeenPrivacyController::exceptionBoxTitle(Exception exception) {
	switch (exception) {
	case Exception::Always: return lang(lng_edit_privacy_lastseen_always_title);
	case Exception::Never: return lang(lng_edit_privacy_lastseen_never_title);
	}
	Unexpected("Invalid exception value.");
}

QString LastSeenPrivacyController::exceptionsDescription() {
	return lang(lng_edit_privacy_lastseen_exceptions);
}

void LastSeenPrivacyController::confirmSave(bool someAreDisallowed, base::lambda_once<void()> saveCallback) {
	if (someAreDisallowed && !Auth().data().lastSeenWarningSeen()) {
		auto weakBox = std::make_shared<QPointer<ConfirmBox>>();
		auto callback = [weakBox, saveCallback = std::move(saveCallback)]() mutable {
			if (auto box = *weakBox) {
				box->closeBox();
			}
			saveCallback();
			Auth().data().setLastSeenWarningSeen(true);
			Local::writeUserSettings();
		};
		auto box = Box<ConfirmBox>(lang(lng_edit_privacy_lastseen_warning), lang(lng_continue), lang(lng_cancel), std::move(callback));
		*weakBox = Ui::show(std::move(box), LayerOption::KeepOther);
	} else {
		saveCallback();
	}
}

MTPInputPrivacyKey GroupsInvitePrivacyController::key() {
	return MTP_inputPrivacyKeyChatInvite();
}

QString GroupsInvitePrivacyController::title() {
	return lang(lng_edit_privacy_groups_title);
}

bool GroupsInvitePrivacyController::hasOption(Option option) {
	return (option != Option::Nobody);
}

QString GroupsInvitePrivacyController::description() {
	return lang(lng_edit_privacy_groups_description);
}

QString GroupsInvitePrivacyController::exceptionLinkText(Exception exception, int count) {
	switch (exception) {
	case Exception::Always: return (count > 0) ? lng_edit_privacy_groups_always(lt_count, count) : lang(lng_edit_privacy_groups_always_empty);
	case Exception::Never: return (count > 0) ? lng_edit_privacy_groups_never(lt_count, count) : lang(lng_edit_privacy_groups_never_empty);
	}
	Unexpected("Invalid exception value.");
}

QString GroupsInvitePrivacyController::exceptionBoxTitle(Exception exception) {
	switch (exception) {
	case Exception::Always: return lang(lng_edit_privacy_groups_always_title);
	case Exception::Never: return lang(lng_edit_privacy_groups_never_title);
	}
	Unexpected("Invalid exception value.");
}

QString GroupsInvitePrivacyController::exceptionsDescription() {
	return lang(lng_edit_privacy_groups_exceptions);
}

MTPInputPrivacyKey CallsPrivacyController::key() {
	return MTP_inputPrivacyKeyPhoneCall();
}

QString CallsPrivacyController::title() {
	return lang(lng_edit_privacy_calls_title);
}

QString CallsPrivacyController::description() {
	return lang(lng_edit_privacy_calls_description);
}

QString CallsPrivacyController::exceptionLinkText(Exception exception, int count) {
	switch (exception) {
	case Exception::Always: return (count > 0) ? lng_edit_privacy_calls_always(lt_count, count) : lang(lng_edit_privacy_calls_always_empty);
	case Exception::Never: return (count > 0) ? lng_edit_privacy_calls_never(lt_count, count) : lang(lng_edit_privacy_calls_never_empty);
	}
	Unexpected("Invalid exception value.");
}

QString CallsPrivacyController::exceptionBoxTitle(Exception exception) {
	switch (exception) {
	case Exception::Always: return lang(lng_edit_privacy_calls_always_title);
	case Exception::Never: return lang(lng_edit_privacy_calls_never_title);
	}
	Unexpected("Invalid exception value.");
}

QString CallsPrivacyController::exceptionsDescription() {
	return lang(lng_edit_privacy_calls_exceptions);
}

} // namespace Settings
