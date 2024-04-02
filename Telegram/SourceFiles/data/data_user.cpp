/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_user.h"

#include "storage/localstorage.h"
#include "storage/storage_user_photos.h"
#include "main/main_session.h"
#include "data/business/data_business_common.h"
#include "data/business/data_business_info.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_peer_bot_command.h"
#include "data/data_photo.h"
#include "data/data_stories.h"
#include "data/data_wall_paper.h"
#include "data/notify/data_notify_settings.h"
#include "history/history.h"
#include "api/api_peer_photo.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "styles/style_chat.h"

namespace {

// User with hidden last seen stays online in UI for such amount of seconds.
constexpr auto kSetOnlineAfterActivity = TimeId(30);

using UpdateFlag = Data::PeerUpdate::Flag;

} // namespace

BotInfo::BotInfo() = default;

Data::LastseenStatus LastseenFromMTP(
		const MTPUserStatus &status,
		Data::LastseenStatus currentStatus) {
	return status.match([](const MTPDuserStatusEmpty &data) {
		return Data::LastseenStatus::LongAgo();
	}, [&](const MTPDuserStatusRecently &data) {
		return currentStatus.isLocalOnlineValue()
			? Data::LastseenStatus::OnlineTill(
				currentStatus.onlineTill(),
				true,
				data.is_by_me())
			: Data::LastseenStatus::Recently(data.is_by_me());
	}, [](const MTPDuserStatusLastWeek &data) {
		return Data::LastseenStatus::WithinWeek(data.is_by_me());
	}, [](const MTPDuserStatusLastMonth &data) {
		return Data::LastseenStatus::WithinMonth(data.is_by_me());
	}, [](const MTPDuserStatusOnline& data) {
		return Data::LastseenStatus::OnlineTill(data.vexpires().v);
	}, [](const MTPDuserStatusOffline &data) {
		return Data::LastseenStatus::OnlineTill(data.vwas_online().v);
	});
}

UserData::UserData(not_null<Data::Session*> owner, PeerId id)
: PeerData(owner, id)
, _flags((id == owner->session().userPeerId()) ? Flag::Self : Flag(0)) {
}

UserData::~UserData() = default;

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

Data::LastseenStatus UserData::lastseen() const {
	return _lastseen;
}

bool UserData::updateLastseen(Data::LastseenStatus value) {
	if (_lastseen == value) {
		return false;
	}
	_lastseen = value;
	owner().maybeStopWatchForOffline(this);
	return true;
}

// see Serialize::readPeer as well
void UserData::setPhoto(const MTPUserProfilePhoto &photo) {
	photo.match([&](const MTPDuserProfilePhoto &data) {
		if (data.is_personal()) {
			addFlags(UserDataFlag::PersonalPhoto);
		} else {
			removeFlags(UserDataFlag::PersonalPhoto);
		}
		updateUserpic(
			data.vphoto_id().v,
			data.vdc_id().v,
			data.is_has_video());
	}, [&](const MTPDuserProfilePhotoEmpty &) {
		removeFlags(UserDataFlag::PersonalPhoto);
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

bool UserData::hasPrivateForwardName() const {
	return !_privateForwardName.isEmpty();
}

QString UserData::privateForwardName() const {
	return _privateForwardName;
}

void UserData::setPrivateForwardName(const QString &name) {
	_privateForwardName = name;
}

bool UserData::hasActiveStories() const {
	return flags() & Flag::HasActiveStories;
}

bool UserData::hasUnreadStories() const {
	return flags() & Flag::HasUnreadStories;
}

void UserData::setStoriesState(StoriesState state) {
	Expects(state != StoriesState::Unknown);

	const auto was = flags();
	switch (state) {
	case StoriesState::None:
		_flags.remove(Flag::HasActiveStories | Flag::HasUnreadStories);
		break;
	case StoriesState::HasRead:
		_flags.set(
			(flags() & ~Flag::HasUnreadStories) | Flag::HasActiveStories);
		break;
	case StoriesState::HasUnread:
		_flags.add(Flag::HasActiveStories | Flag::HasUnreadStories);
		break;
	}
	if (flags() != was) {
		if (const auto history = owner().historyLoaded(this)) {
			history->updateChatListEntryPostponed();
		}
		session().changes().peerUpdated(this, UpdateFlag::StoriesState);
	}
}

const Data::BusinessDetails &UserData::businessDetails() const {
	static const auto empty = Data::BusinessDetails();
	return _businessDetails ? *_businessDetails : empty;
}

void UserData::setBusinessDetails(Data::BusinessDetails details) {
	details.hours = details.hours.normalized();
	if ((!details && !_businessDetails)
		|| (details && _businessDetails && details == *_businessDetails)) {
		return;
	}
	_businessDetails = details
		? std::make_unique<Data::BusinessDetails>(std::move(details))
		: nullptr;
	session().changes().peerUpdated(this, UpdateFlag::BusinessDetails);
}

ChannelId UserData::personalChannelId() const {
	return _personalChannelId;
}

MsgId UserData::personalChannelMessageId() const {
	return _personalChannelMessageId;
}

void UserData::setPersonalChannel(ChannelId channelId, MsgId messageId) {
	if (_personalChannelId != channelId
		|| _personalChannelMessageId != messageId) {
		_personalChannelId = channelId;
		_personalChannelMessageId = messageId;
		session().changes().peerUpdated(this, UpdateFlag::PersonalChannel);
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

		const auto description = qs(d.vdescription().value_or_empty());
		if (botInfo->description != description) {
			botInfo->description = description;
			++botInfo->descriptionVersion;
		}
		if (const auto photo = d.vdescription_photo()) {
			const auto parsed = owner().processPhoto(*photo);
			if (botInfo->photo != parsed) {
				botInfo->photo = parsed;
				++botInfo->descriptionVersion;
			}
		} else if (botInfo->photo) {
			botInfo->photo = nullptr;
			++botInfo->descriptionVersion;
		}
		if (const auto document = d.vdescription_document()) {
			const auto parsed = owner().processDocument(*document);
			if (botInfo->document != parsed) {
				botInfo->document = parsed;
				++botInfo->descriptionVersion;
			}
		} else if (botInfo->document) {
			botInfo->document = nullptr;
			++botInfo->descriptionVersion;
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
	}
	const auto till = lastseen().onlineTill();
	if (till < when + 1
		&& updateLastseen(
			Data::LastseenStatus::OnlineTill(
				when + kSetOnlineAfterActivity,
				!till || lastseen().isLocalOnlineValue(),
				lastseen().isHiddenByMe()))) {
		session().changes().peerUpdated(this, UpdateFlag::OnlineStatus);
	}
}

void UserData::setAccessHash(uint64 accessHash) {
	if (accessHash == kInaccessibleAccessHashOld) {
		_accessHash = 0;
		_flags.add(Flag::Deleted);
		invalidateEmptyUserpic();
	} else {
		_accessHash = accessHash;
	}
}

void UserData::setFlags(UserDataFlags which) {
	if ((which & UserDataFlag::Deleted)
		!= (flags() & UserDataFlag::Deleted)) {
		invalidateEmptyUserpic();
	}
	_flags.set((flags() & UserDataFlag::Self)
		| (which & ~UserDataFlag::Self));
}

void UserData::addFlags(UserDataFlags which) {
	setFlags(flags() | which);
}

void UserData::removeFlags(UserDataFlags which) {
	setFlags(flags() & ~which);
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

bool UserData::applyMinPhoto() const {
	return !(flags() & UserDataFlag::DiscardMinPhoto);
}

bool UserData::hasPersonalPhoto() const {
	return (flags() & UserDataFlag::PersonalPhoto);
}

bool UserData::hasStoriesHidden() const {
	return (flags() & UserDataFlag::StoriesHidden);
}

bool UserData::someRequirePremiumToWrite() const {
	return (flags() & UserDataFlag::SomeRequirePremiumToWrite);
}

bool UserData::meRequiresPremiumToWrite() const {
	return (flags() & UserDataFlag::MeRequiresPremiumToWrite);
}

bool UserData::requirePremiumToWriteKnown() const {
	return (flags() & UserDataFlag::RequirePremiumToWriteKnown);
}

bool UserData::canSendIgnoreRequirePremium() const {
	return !isInaccessible() && !isRepliesChat();
}

bool UserData::readDatesPrivate() const {
	return (flags() & UserDataFlag::ReadDatesPrivate);
}

bool UserData::canAddContact() const {
	return canShareThisContact() && !isContact();
}

bool UserData::canReceiveGifts() const {
	return flags() & UserDataFlag::CanReceiveGifts;
}

bool UserData::canShareThisContactFast() const {
	return !_phone.isEmpty();
}

QString UserData::username() const {
	return _username.username();
}

QString UserData::editableUsername() const {
	return _username.editableUsername();
}

const std::vector<QString> &UserData::usernames() const {
	return _username.usernames();
}

bool UserData::isUsernameEditable(QString username) const {
	return _username.isEditable(username);
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


Data::Birthday UserData::birthday() const {
	return _birthday;
}

void UserData::setBirthday(Data::Birthday value) {
	if (_birthday != value) {
		_birthday = value;
		session().changes().peerUpdated(this, UpdateFlag::Birthday);
	}
}

void UserData::setBirthday(const tl::conditional<MTPBirthday> &value) {
	if (!value) {
		setBirthday(Data::Birthday());
	} else {
		const auto &data = value->data();
		setBirthday(Data::Birthday(
			data.vday().v,
			data.vmonth().v,
			data.vyear().value_or_empty()));
	}
}

bool UserData::hasCalls() const {
	return (callsStatus() != CallsStatus::Disabled)
		&& (callsStatus() != CallsStatus::Unknown);
}

namespace Data {

void ApplyUserUpdate(not_null<UserData*> user, const MTPDuserFull &update) {
	const auto profilePhoto = update.vprofile_photo()
		? user->owner().processPhoto(*update.vprofile_photo()).get()
		: nullptr;
	const auto personalPhoto = update.vpersonal_photo()
		? user->owner().processPhoto(*update.vpersonal_photo()).get()
		: nullptr;
	if (personalPhoto && profilePhoto) {
		user->session().api().peerPhoto().registerNonPersonalPhoto(
			user,
			profilePhoto);
	} else {
		user->session().api().peerPhoto().unregisterNonPersonalPhoto(user);
	}
	if (const auto photo = update.vfallback_photo()) {
		const auto data = user->owner().processPhoto(*photo);
		if (!data->isNull()) { // Sometimes there is photoEmpty :shrug:
			user->session().storage().add(Storage::UserPhotosSetBack(
				peerToUser(user->id),
				data->id
			));
		}
	}
	user->setBarSettings(update.vsettings());
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
		| Flag::VoiceMessagesForbidden
		| Flag::ReadDatesPrivate
		| Flag::RequirePremiumToWriteKnown
		| Flag::MeRequiresPremiumToWrite;
	user->setFlags((user->flags() & ~mask)
		| (update.is_phone_calls_private()
			? Flag::PhoneCallsPrivate
			: Flag())
		| (update.is_phone_calls_available() ? Flag::HasPhoneCalls : Flag())
		| (canReceiveGifts ? Flag::CanReceiveGifts : Flag())
		| (update.is_can_pin_message() ? Flag::CanPinMessages : Flag())
		| (update.is_blocked() ? Flag::Blocked : Flag())
		| (update.is_voice_messages_forbidden()
			? Flag::VoiceMessagesForbidden
			: Flag())
		| (update.is_read_dates_private() ? Flag::ReadDatesPrivate : Flag())
		| Flag::RequirePremiumToWriteKnown
		| (update.is_contact_require_premium()
			? Flag::MeRequiresPremiumToWrite
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
	user->setTranslationDisabled(update.is_translations_disabled());
	user->setPrivateForwardName(
		update.vprivate_forward_name().value_or_empty());

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

	if (const auto paper = update.vwallpaper()) {
		user->setWallPaper(
			Data::WallPaper::Create(&user->session(), *paper),
			update.is_wallpaper_overridden());
	} else {
		user->setWallPaper({});
	}

	user->setBusinessDetails(FromMTP(
		&user->owner(),
		update.vbusiness_work_hours(),
		update.vbusiness_location(),
		update.vbusiness_intro()));
	user->setBirthday(update.vbirthday());
	user->setPersonalChannel(
		update.vpersonal_channel_id().value_or_empty(),
		update.vpersonal_channel_message().value_or_empty());
	if (user->isSelf()) {
		user->owner().businessInfo().applyAwaySettings(
			FromMTP(&user->owner(), update.vbusiness_away_message()));
		user->owner().businessInfo().applyGreetingSettings(
			FromMTP(&user->owner(), update.vbusiness_greeting_message()));
	}

	user->owner().stories().apply(user, update.vstories());

	user->fullUpdated();
}

} // namespace Data
