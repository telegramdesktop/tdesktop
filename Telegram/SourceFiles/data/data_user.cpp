/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_user.h"

#include "storage/localstorage.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "ui/text/text_options.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"

namespace {

// User with hidden last seen stays online in UI for such amount of seconds.
constexpr auto kSetOnlineAfterActivity = TimeId(30);

using UpdateFlag = Data::PeerUpdate::Flag;

} // namespace

UserData::UserData(not_null<Data::Session*> owner, PeerId id)
: PeerData(owner, id) {
}

bool UserData::canShareThisContact() const {
	return canShareThisContactFast()
		|| !owner().findContactPhone(peerToUser(id)).isEmpty();
}

void UserData::setIsContact(bool is) {
	const auto status = is
		? ContactStatus::Contact
		: ContactStatus::NotContact;
	if (_contactStatus != status) {
		_contactStatus = status;
		session().changes().peerUpdated(this, UpdateFlag::IsContact);
	}
}

// see Serialize::readPeer as well
void UserData::setPhoto(const MTPUserProfilePhoto &photo) {
	photo.match([&](const MTPDuserProfilePhoto &data) {
		updateUserpic(data.vphoto_id().v, data.vdc_id().v);
	}, [&](const MTPDuserProfilePhotoEmpty &) {
		clearUserpic();
	});
}

auto UserData::unavailableReasons() const
-> const std::vector<Data::UnavailableReason> & {
	return _unavailableReasons;
}

void UserData::setUnavailableReasons(
		std::vector<Data::UnavailableReason> &&reasons) {
	if (_unavailableReasons != reasons) {
		_unavailableReasons = std::move(reasons);
		session().changes().peerUpdated(
			this,
			UpdateFlag::UnavailableReason);
	}
}

void UserData::setCommonChatsCount(int count) {
	if (_commonChatsCount != count) {
		_commonChatsCount = count;
		session().changes().peerUpdated(this, UpdateFlag::CommonChats);
	}
}

void UserData::setName(const QString &newFirstName, const QString &newLastName, const QString &newPhoneName, const QString &newUsername) {
	bool changeName = !newFirstName.isEmpty() || !newLastName.isEmpty();

	QString newFullName;
	if (changeName && newFirstName.trimmed().isEmpty()) {
		firstName = newLastName;
		lastName = QString();
		newFullName = firstName;
	} else {
		if (changeName) {
			firstName = newFirstName;
			lastName = newLastName;
		}
		newFullName = lastName.isEmpty() ? firstName : tr::lng_full_name(tr::now, lt_first_name, firstName, lt_last_name, lastName);
	}
	updateNameDelayed(newFullName, newPhoneName, newUsername);
}

void UserData::setPhone(const QString &newPhone) {
	if (_phone != newPhone) {
		_phone = newPhone;
	}
}

void UserData::setBotInfoVersion(int version) {
	if (version < 0) {
		// We don't support bots becoming non-bots.
	} else if (!botInfo) {
		botInfo = std::make_unique<BotInfo>();
		botInfo->version = version;
		owner().userIsBotChanged(this);
	} else if (botInfo->version < version) {
		if (!botInfo->commands.empty()) {
			botInfo->commands.clear();
			owner().botCommandsChanged(this);
		}
		botInfo->description.clear();
		botInfo->version = version;
		botInfo->inited = false;
	}
}

void UserData::setBotInfo(const MTPBotInfo &info) {
	switch (info.type()) {
	case mtpc_botInfo: {
		const auto &d = info.c_botInfo();
		if (peerFromUser(d.vuser_id().v) != id || !isBot()) {
			return;
		}

		QString desc = qs(d.vdescription());
		if (botInfo->description != desc) {
			botInfo->description = desc;
			botInfo->text = Ui::Text::String(st::msgMinWidth);
		}
		const auto changedCommands = Data::UpdateBotCommands(
			botInfo->commands,
			d.vcommands());

		botInfo->inited = true;

		if (changedCommands) {
			owner().botCommandsChanged(this);
		}
	} break;
	}
}

void UserData::setNameOrPhone(const QString &newNameOrPhone) {
	if (nameOrPhone != newNameOrPhone) {
		nameOrPhone = newNameOrPhone;
		phoneText.setText(
			st::msgNameStyle,
			nameOrPhone,
			Ui::NameTextOptions());
	}
}

void UserData::madeAction(TimeId when) {
	if (isBot() || isServiceUser() || when <= 0) {
		return;
	} else if (onlineTill <= 0 && -onlineTill < when) {
		onlineTill = -when - kSetOnlineAfterActivity;
		session().changes().peerUpdated(this, UpdateFlag::OnlineStatus);
	} else if (onlineTill > 0 && onlineTill < when + 1) {
		onlineTill = when + kSetOnlineAfterActivity;
		session().changes().peerUpdated(this, UpdateFlag::OnlineStatus);
	}
}

void UserData::setAccessHash(uint64 accessHash) {
	if (accessHash == kInaccessibleAccessHashOld) {
		_accessHash = 0;
		_flags.add(Flag::Deleted);
	} else {
		_accessHash = accessHash;
	}
}

void UserData::setCallsStatus(CallsStatus callsStatus) {
	if (callsStatus != _callsStatus) {
		_callsStatus = callsStatus;
		session().changes().peerUpdated(this, UpdateFlag::HasCalls);
	}
}

bool UserData::hasCalls() const {
	return (callsStatus() != CallsStatus::Disabled)
		&& (callsStatus() != CallsStatus::Unknown);
}

namespace Data {

void ApplyUserUpdate(not_null<UserData*> user, const MTPDuserFull &update) {
	user->owner().processUser(update.vuser());
	if (const auto photo = update.vprofile_photo()) {
		user->owner().processPhoto(*photo);
	}
	user->setSettings(update.vsettings());
	user->session().api().applyNotifySettings(
		MTP_inputNotifyPeer(user->input),
		update.vnotify_settings());

	user->setMessagesTTL(update.vttl_period().value_or_empty());
	if (const auto info = update.vbot_info()) {
		user->setBotInfo(*info);
	} else {
		user->setBotInfoVersion(-1);
	}
	if (const auto pinned = update.vpinned_msg_id()) {
		SetTopPinnedMessageId(user, pinned->v);
	}
	using Flag = UserDataFlag;
	const auto mask = Flag::Blocked
		| Flag::HasPhoneCalls
		| Flag::PhoneCallsPrivate
		| Flag::CanPinMessages;
	user->setFlags((user->flags() & ~mask)
		| (update.is_phone_calls_private() ? Flag::PhoneCallsPrivate : Flag())
		| (update.is_phone_calls_available() ? Flag::HasPhoneCalls : Flag())
		| (update.is_can_pin_message() ? Flag::CanPinMessages : Flag())
		| (update.is_blocked() ? Flag::Blocked : Flag()));
	user->setIsBlocked(update.is_blocked());
	user->setCallsStatus(update.is_phone_calls_private()
		? UserData::CallsStatus::Private
		: update.is_phone_calls_available()
		? UserData::CallsStatus::Enabled
		: UserData::CallsStatus::Disabled);
	user->setAbout(qs(update.vabout().value_or_empty()));
	user->setCommonChatsCount(update.vcommon_chats_count().v);
	user->checkFolder(update.vfolder_id().value_or_empty());
	user->fullUpdated();
}

} // namespace Data
