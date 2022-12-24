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
#include "data/data_peer_bot_command.h"
#include "data/data_emoji_statuses.h"
#include "data/data_user_names.h"
#include "data/notify/data_notify_settings.h"
#include "ui/text/text_options.h"
#include "lang/lang_keys.h"
#include "styles/style_chat.h"

namespace {

// User with hidden last seen stays online in UI for such amount of seconds.
constexpr auto kSetOnlineAfterActivity = TimeId(30);

using UpdateFlag = Data::PeerUpdate::Flag;

} // namespace

BotInfo::BotInfo() : text(st::msgMinWidth) {
}

UserData::UserData(not_null<Data::Session*> owner, PeerId id)
: PeerData(owner, id)
, _flags((id == owner->session().userPeerId()) ? Flag::Self : Flag(0)) {
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
		updateUserpic(
			data.vphoto_id().v,
			data.vdc_id().v,
			data.is_has_video());
	}, [&](const MTPDuserProfilePhotoEmpty &) {
		clearUserpic();
	});
}

void UserData::setEmojiStatus(const MTPEmojiStatus &status) {
	const auto parsed = Data::ParseEmojiStatus(status);
	setEmojiStatus(parsed.id, parsed.until);
}

void UserData::setEmojiStatus(DocumentId emojiStatusId, TimeId until) {
	if (_emojiStatusId != emojiStatusId) {
		_emojiStatusId = emojiStatusId;
		session().changes().peerUpdated(this, UpdateFlag::EmojiStatus);
	}
	owner().emojiStatuses().registerAutomaticClear(this, until);
}

DocumentId UserData::emojiStatusId() const {
	return _emojiStatusId;
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

void UserData::setUsernames(const Data::Usernames &newUsernames) {
	const auto wasUsername = username();
	const auto wasUsernames = usernames();
	_username.setUsernames(newUsernames);
	const auto nowUsername = username();
	const auto nowUsernames = usernames();
	session().changes().peerUpdated(
		this,
		UpdateFlag()
		| ((wasUsername != nowUsername)
			? UpdateFlag::Username
			: UpdateFlag())
		| (!ranges::equal(wasUsernames, nowUsernames)
			? UpdateFlag::Usernames
			: UpdateFlag()));
}

void UserData::setUsername(const QString &username) {
	_username.setUsername(username);
}

void UserData::setPhone(const QString &newPhone) {
	if (_phone != newPhone) {
		_phone = newPhone;
	}
}

void UserData::setBotInfoVersion(int version) {
	if (version < 0) {
		// We don't support bots becoming non-bots.
		if (botInfo) {
			botInfo->version = -1;
		}
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
		if (!isBot()) {
			return;
		} else if (d.vuser_id() && peerFromUser(*d.vuser_id()) != id) {
			return;
		}

		QString desc = qs(d.vdescription().value_or_empty());
		if (botInfo->description != desc) {
			botInfo->description = desc;
			botInfo->text = Ui::Text::String(st::msgMinWidth);
		}

		auto commands = d.vcommands()
			? ranges::views::all(
				d.vcommands()->v
			) | ranges::views::transform(
				Data::BotCommandFromTL
			) | ranges::to_vector
			: std::vector<Data::BotCommand>();
		const auto changedCommands = !ranges::equal(
			botInfo->commands,
			commands);
		botInfo->commands = std::move(commands);

		const auto changedButton = Data::ApplyBotMenuButton(
			botInfo.get(),
			d.vmenu_button());
		botInfo->inited = true;

		if (changedCommands || changedButton) {
			owner().botCommandsChanged(this);
		}
	} break;
	}
}

void UserData::setNameOrPhone(const QString &newNameOrPhone) {
	nameOrPhone = newNameOrPhone;
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

void UserData::setFlags(UserDataFlags which) {
	_flags.set((flags() & UserDataFlag::Self)
		| (which & ~UserDataFlag::Self));
}

void UserData::addFlags(UserDataFlags which) {
	_flags.add(which & ~UserDataFlag::Self);
}

void UserData::removeFlags(UserDataFlags which) {
	_flags.remove(which & ~UserDataFlag::Self);
}

bool UserData::isVerified() const {
	return flags() & UserDataFlag::Verified;
}

bool UserData::isScam() const {
	return flags() & UserDataFlag::Scam;
}

bool UserData::isFake() const {
	return flags() & UserDataFlag::Fake;
}

bool UserData::isPremium() const {
	return flags() & UserDataFlag::Premium;
}

bool UserData::isBotInlineGeo() const {
	return flags() & UserDataFlag::BotInlineGeo;
}

bool UserData::isBot() const {
	return botInfo != nullptr;
}

bool UserData::isSupport() const {
	return flags() & UserDataFlag::Support;
}

bool UserData::isInaccessible() const {
	return flags() & UserDataFlag::Deleted;
}

bool UserData::canWrite() const {
	// Duplicated in Data::CanWriteValue().
	return !isInaccessible() && !isRepliesChat();
}

bool UserData::applyMinPhoto() const {
	return !(flags() & UserDataFlag::DiscardMinPhoto);
}

bool UserData::canAddContact() const {
	return canShareThisContact() && !isContact();
}

bool UserData::canReceiveGifts() const {
	return flags() & UserDataFlag::CanReceiveGifts;
}

bool UserData::canReceiveVoices() const {
	return !(flags() & UserDataFlag::VoiceMessagesForbidden);
}

bool UserData::canShareThisContactFast() const {
	return !_phone.isEmpty();
}

QString UserData::username() const {
	return _username.username();
}

QString UserData::editableUsername() const {
	return _username.editableUsername();;
}

const std::vector<QString> &UserData::usernames() const {
	return _username.usernames();
}

const QString &UserData::phone() const {
	return _phone;
}

UserData::ContactStatus UserData::contactStatus() const {
	return _contactStatus;
}

bool UserData::isContact() const {
	return (contactStatus() == ContactStatus::Contact);
}

UserData::CallsStatus UserData::callsStatus() const {
	return _callsStatus;
}

int UserData::commonChatsCount() const {
	return _commonChatsCount;
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
	if (const auto photo = update.vprofile_photo()) {
		user->owner().processPhoto(*photo);
	}
	user->setSettings(update.vsettings());
	user->owner().notifySettings().apply(user, update.vnotify_settings());

	user->setMessagesTTL(update.vttl_period().value_or_empty());
	if (const auto info = update.vbot_info()) {
		user->setBotInfo(*info);
	} else {
		user->setBotInfoVersion(-1);
	}
	if (const auto pinned = update.vpinned_msg_id()) {
		SetTopPinnedMessageId(user, pinned->v);
	}
	const auto canReceiveGifts = (update.vflags().v
			& MTPDuserFull::Flag::f_premium_gifts)
		&& update.vpremium_gifts();
	using Flag = UserDataFlag;
	const auto mask = Flag::Blocked
		| Flag::HasPhoneCalls
		| Flag::PhoneCallsPrivate
		| Flag::CanReceiveGifts
		| Flag::CanPinMessages
		| Flag::VoiceMessagesForbidden;
	user->setFlags((user->flags() & ~mask)
		| (update.is_phone_calls_private() ? Flag::PhoneCallsPrivate : Flag())
		| (update.is_phone_calls_available() ? Flag::HasPhoneCalls : Flag())
		| (canReceiveGifts ? Flag::CanReceiveGifts : Flag())
		| (update.is_can_pin_message() ? Flag::CanPinMessages : Flag())
		| (update.is_blocked() ? Flag::Blocked : Flag())
		| (update.is_voice_messages_forbidden()
			? Flag::VoiceMessagesForbidden
			: Flag()));
	user->setIsBlocked(update.is_blocked());
	user->setCallsStatus(update.is_phone_calls_private()
		? UserData::CallsStatus::Private
		: update.is_phone_calls_available()
		? UserData::CallsStatus::Enabled
		: UserData::CallsStatus::Disabled);
	user->setAbout(qs(update.vabout().value_or_empty()));
	user->setCommonChatsCount(update.vcommon_chats_count().v);
	user->checkFolder(update.vfolder_id().value_or_empty());
	user->setThemeEmoji(qs(update.vtheme_emoticon().value_or_empty()));

	if (const auto info = user->botInfo.get()) {
		const auto group = update.vbot_group_admin_rights()
			? ChatAdminRightsInfo(*update.vbot_group_admin_rights()).flags
			: ChatAdminRights();
		const auto channel = update.vbot_broadcast_admin_rights()
			? ChatAdminRightsInfo(
				*update.vbot_broadcast_admin_rights()).flags
			: ChatAdminRights();
		if (info->groupAdminRights != group
			|| info->channelAdminRights != channel) {
			info->groupAdminRights = group;
			info->channelAdminRights = channel;
			user->session().changes().peerUpdated(
				user,
				Data::PeerUpdate::Flag::Rights);
		}
	}

	user->fullUpdated();
}

} // namespace Data
