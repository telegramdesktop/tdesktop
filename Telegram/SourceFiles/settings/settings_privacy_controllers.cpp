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
#include "auth_session.h"
#include "storage/localstorage.h"
#include "boxes/confirmbox.h"

namespace Settings {
namespace {

constexpr auto kBlockedPerPage = 40;

class BlockUserBoxController : public ChatsListBoxController {
public:
	void rowClicked(PeerListBox::Row *row) override;

protected:
	void prepareViewHook() override;
	std::unique_ptr<Row> createRow(History *history) override;
	void updateRowHook(Row *row) override {
		updateIsBlocked(row, row->peer()->asUser());
		view()->updateRow(row);
	}

private:
	void updateIsBlocked(PeerListBox::Row *row, UserData *user) const;

};

void BlockUserBoxController::prepareViewHook() {
	view()->setTitle(lang(lng_blocked_list_add_title));
	view()->addButton(lang(lng_cancel), [this] { view()->closeBox(); });

	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::UserIsBlocked, [this](const Notify::PeerUpdate &update) {
		if (auto user = update.peer->asUser()) {
			if (auto row = view()->findRow(user)) {
				updateIsBlocked(row, user);
				view()->updateRow(row);
			}
		}
	}));
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

void BlockUserBoxController::rowClicked(PeerListBox::Row *row) {
	auto user = row->peer()->asUser();
	Expects(user != nullptr);

	App::api()->blockUser(user);
	view()->closeBox();
}

std::unique_ptr<BlockUserBoxController::Row> BlockUserBoxController::createRow(History *history) {
	if (auto user = history->peer->asUser()) {
		auto row = std::make_unique<Row>(history);
		updateIsBlocked(row.get(), user);
		return row;
	}
	return std::unique_ptr<Row>();
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
		default: Unexpected("Bad type() in MTPcontacts_GetBlocked() result.");
		}
	})), rpcFail(base::lambda_guarded(this, [this](const RPCError &error) {
		if (MTP::isDefaultHandledError(error)) {
			return false;
		}
		_loadRequestId = 0;
		return true;
	})));
}

void BlockedBoxController::rowClicked(PeerListBox::Row *row) {
	Ui::showPeerHistoryAsync(row->peer()->id, ShowAtUnreadMsgId);
}

void BlockedBoxController::rowActionClicked(PeerListBox::Row *row) {
	auto user = row->peer()->asUser();
	Expects(user != nullptr);

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
	case Exception::Always: return lng_edit_privacy_lastseen_always(lt_count, count);
	case Exception::Never: return lng_edit_privacy_lastseen_never(lt_count, count);
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
	if (someAreDisallowed && !AuthSession::Current().data().lastSeenWarningSeen()) {
		auto weakBox = std::make_shared<QPointer<ConfirmBox>>();
		auto callback = [weakBox, saveCallback = std::move(saveCallback)]() mutable {
			if (auto box = *weakBox) {
				box->closeBox();
			}
			saveCallback();
			AuthSession::Current().data().setLastSeenWarningSeen(true);
			Local::writeUserSettings();
		};
		auto box = Box<ConfirmBox>(lang(lng_edit_privacy_lastseen_warning), lang(lng_continue), lang(lng_cancel), std::move(callback));
		*weakBox = Ui::show(std::move(box), KeepOtherLayers);
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
	case Exception::Always: return lng_edit_privacy_groups_always(lt_count, count);
	case Exception::Never: return lng_edit_privacy_groups_never(lt_count, count);
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

} // namespace Settings
