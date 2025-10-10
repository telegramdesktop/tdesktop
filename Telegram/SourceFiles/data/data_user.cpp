/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_user.h"

#include "api/api_credits.h"
#include "api/api_global_privacy.h"
#include "api/api_sensitive_content.h"
#include "api/api_statistics.h"
#include "api/api_text_entities.h"
#include "base/timer_rpl.h"
#include "core/application.h"
#include "storage/localstorage.h"
#include "storage/storage_account.h"
#include "storage/storage_user_photos.h"
#include "main/main_session.h"
#include "data/business/data_business_common.h"
#include "data/business/data_business_info.h"
#include "data/components/credits.h"
#include "data/data_cloud_themes.h"
#include "data/data_forum.h"
#include "data/data_forum_icons.h"
#include "data/data_saved_music.h"
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
#include "window/notifications_manager.h"
#include "styles/style_chat.h"

namespace {

// User with hidden last seen stays online in UI for such amount of seconds.
constexpr auto kSetOnlineAfterActivity = TimeId(30);

using UpdateFlag = Data::PeerUpdate::Flag;

bool ApplyBotVerifierSettings(
		not_null<BotInfo*> info,
		const MTPBotVerifierSettings *settings) {
	if (!settings) {
		const auto taken = base::take(info->verifierSettings);
		return taken != nullptr;
	}
	const auto &data = settings->data();
	const auto parsed = BotVerifierSettings{
		.iconId = DocumentId(data.vicon().v),
		.company = qs(data.vcompany()),
		.customDescription = qs(data.vcustom_description().value_or_empty()),
		.canModifyDescription = data.is_can_modify_custom_description(),
	};
	if (!info->verifierSettings) {
		info->verifierSettings = std::make_unique<BotVerifierSettings>(
			parsed);
		return true;
	} else if (*info->verifierSettings != parsed) {
		*info->verifierSettings = parsed;
		return true;
	}
	return false;
}

[[nodiscard]] Data::StarsRating ParseStarsRating(
		const MTPStarsRating *rating) {
	if (!rating) {
		return {};
	}
	const auto &data = rating->data();
	return {
		.level = data.vlevel().v,
		.stars = int(data.vstars().v),
		.thisLevelStars = int(data.vcurrent_level_stars().v),
		.nextLevelStars = int(data.vnext_level_stars().value_or_empty()),
	};
}

} // namespace

BotInfo::BotInfo() = default;

BotInfo::~BotInfo() = default;

void BotInfo::ensureForum(not_null<UserData*> that) {
	if (!_forum) {
		const auto history = that->owner().history(that);
		_forum = std::make_unique<Data::Forum>(history);
		history->forumChanged(nullptr);
	}
}

Data::Forum *BotInfo::forum() const {
	return _forum.get();
}

std::unique_ptr<Data::Forum> BotInfo::takeForumData() {
	if (auto result = base::take(_forum)) {
		result->history()->forumChanged(result.get());
		return result;
	}
	return nullptr;
}

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

void UserData::setUnavailableReasonsList(
		std::vector<Data::UnavailableReason> &&reasons) {
	_unavailableReasons = std::move(reasons);
}

void UserData::setCommonChatsCount(int count) {
	if (_commonChatsCount != count) {
		_commonChatsCount = count;
		session().changes().peerUpdated(this, UpdateFlag::CommonChats);
	}
}

int UserData::peerGiftsCount() const {
	return _peerGiftsCount;
}

void UserData::setPeerGiftsCount(int count) {
	if (_peerGiftsCount != count) {
		_peerGiftsCount = count;
		session().changes().peerUpdated(this, UpdateFlag::PeerGifts);
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

void UserData::setStarRefProgram(StarRefProgram program) {
	const auto info = botInfo.get();
	if (info && info->starRefProgram != program) {
		info->starRefProgram = program;
		session().changes().peerUpdated(
			this,
			Data::PeerUpdate::Flag::StarRefProgram);
	}
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

void UserData::setName(
		const QString &newFirstName,
		const QString &newLastName,
		const QString &newPhoneName,
		const QString &newUsername) {
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
		newFullName = lastName.isEmpty()
			? firstName
			: tr::lng_full_name(
				tr::now,
				lt_first_name,
				firstName,
				lt_last_name,
				lastName);
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

		const auto privacy = qs(d.vprivacy_policy_url().value_or_empty());
		const auto privacyChanged = (botInfo->privacyPolicyUrl != privacy);
		botInfo->privacyPolicyUrl = privacy;

		if (const auto settings = d.vapp_settings()) {
			const auto &data = settings->data();
			botInfo->botAppColorTitleDay = Ui::MaybeColorFromSerialized(
				data.vheader_color()).value_or(QColor(0, 0, 0, 0));
			botInfo->botAppColorTitleNight = Ui::MaybeColorFromSerialized(
				data.vheader_dark_color()).value_or(QColor(0, 0, 0, 0));
			botInfo->botAppColorBodyDay = Ui::MaybeColorFromSerialized(
				data.vbackground_color()).value_or(QColor(0, 0, 0, 0));
			botInfo->botAppColorBodyNight = Ui::MaybeColorFromSerialized(
				data.vbackground_dark_color()).value_or(QColor(0, 0, 0, 0));
		} else {
			botInfo->botAppColorTitleDay
				= botInfo->botAppColorTitleNight
				= botInfo->botAppColorBodyDay
				= botInfo->botAppColorBodyNight
				= QColor(0, 0, 0, 0);
		}
		const auto changedVerifierSettings = ApplyBotVerifierSettings(
			botInfo.get(),
			d.vverifier_settings());

		if (changedCommands
			|| changedButton
			|| privacyChanged
			|| changedVerifierSettings) {
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
	if (!isBot()) {
		which &= ~Flag::Forum;
	}
	const auto diff = flags() ^ which;
	if (diff & Flag::Deleted) {
		invalidateEmptyUserpic();
	}
	// Let Data::Forum live till the end of _flags.set.
	// That way the data can be used in changes handler.
	// Example: render frame for forum auto-closing animation.
	const auto takenForum = (botInfo
		&& (diff & Flag::Forum)
		&& !(which & Flag::Forum))
		? botInfo->takeForumData()
		: nullptr;
	if ((diff & Flag::Forum) && (which & Flag::Forum)) {
		if (const auto info = botInfo.get()) {
			info->ensureForum(this);
		}
	}
	_flags.set((flags() & Flag::Self) | (which & ~Flag::Self));
	if (diff & Flag::Forum) {
		if (const auto history = this->owner().historyLoaded(this)) {
			if (diff & Flag::Forum) {
				Core::App().notifications().clearFromHistory(history);
				history->updateChatListEntryHeight();
				if (history->inChatList()) {
					if (const auto forum = this->forum()) {
						forum->preloadTopics();
					}
				}
			}
		}
	}
	if (const auto raw = takenForum.get()) {
		owner().forumIcons().clearUserpicsReset(raw);
	}
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

bool UserData::hasRequirePremiumToWrite() const {
	return (flags() & UserDataFlag::HasRequirePremiumToWrite);
}

bool UserData::hasStarsPerMessage() const {
	return (flags() & UserDataFlag::HasStarsPerMessage);
}

bool UserData::requiresPremiumToWrite() const {
	return !isSelf() && (flags() & UserDataFlag::RequiresPremiumToWrite);
}

bool UserData::messageMoneyRestrictionsKnown() const {
	return (flags() & UserDataFlag::MessageMoneyRestrictionsKnown);
}

bool UserData::canSendIgnoreMoneyRestrictions() const {
	return !isInaccessible() && !isRepliesChat() && !isVerifyCodes();
}

bool UserData::readDatesPrivate() const {
	return (flags() & UserDataFlag::ReadDatesPrivate);
}

int UserData::starsPerMessage() const {
	return _starsPerMessage;
}

void UserData::setStoriesCorrespondent(bool is) {
	if (is) {
		_flags.add(UserDataFlag::StoriesCorrespondent);
	} else {
		_flags.remove(UserDataFlag::StoriesCorrespondent);
	}
}

bool UserData::storiesCorrespondent() const {
	return (_flags.current() & UserDataFlag::StoriesCorrespondent);
}

void UserData::setStarsPerMessage(int stars) {
	if (_starsPerMessage != stars) {
		_starsPerMessage = stars;
		session().changes().peerUpdated(this, UpdateFlag::StarsPerMessage);
	}
	checkTrustedPayForMessage();
}

void UserData::setStarsRating(Data::StarsRating value) {
	if (_starsRating != value) {
		_starsRating = value;
		session().changes().peerUpdated(this, UpdateFlag::StarsRating);
	}
}

Data::StarsRating UserData::starsRating() const {
	return _starsRating;
}

bool UserData::canAddContact() const {
	return canShareThisContact() && !isContact();
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

void UserData::setBotVerifyDetails(Ui::BotVerifyDetails details) {
	if (!details) {
		if (_botVerifyDetails) {
			_botVerifyDetails = nullptr;
			session().changes().peerUpdated(this, UpdateFlag::VerifyInfo);
		}
	} else if (!_botVerifyDetails) {
		_botVerifyDetails = std::make_unique<Ui::BotVerifyDetails>(details);
		session().changes().peerUpdated(this, UpdateFlag::VerifyInfo);
	} else if (*_botVerifyDetails != details) {
		*_botVerifyDetails = details;
		session().changes().peerUpdated(this, UpdateFlag::VerifyInfo);
	}
}

void UserData::setBotVerifyDetailsIcon(DocumentId iconId) {
	if (!iconId) {
		setBotVerifyDetails({});
	} else {
		auto info = _botVerifyDetails
			? *_botVerifyDetails
			: Ui::BotVerifyDetails();
		info.iconId = iconId;
		setBotVerifyDetails(info);
	}
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

		if (isSelf()) {
			session().api().sensitiveContent().reload(true);
		}
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

void UserData::setDisallowedGiftTypes(Api::DisallowedGiftTypes types) {
	if (_disallowedGiftTypes != types) {
		_disallowedGiftTypes = types;
		session().changes().peerUpdated(this, UpdateFlag::GiftSettings);
	}
}

const TextWithEntities &UserData::note() const {
	return _note;
}

void UserData::setNote(const TextWithEntities &note) {
	if (_note != note) {
		_note = note;
		session().changes().peerUpdated(this, UpdateFlag::ContactNote);
	}
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
	if (const auto info = user->botInfo.get()) {
		info->canManageEmojiStatus = update.is_bot_can_manage_emoji_status();
		user->setStarRefProgram(
			Data::ParseStarRefProgram(update.vstarref_program()));
	}
	if (const auto pinned = update.vpinned_msg_id()) {
		SetTopPinnedMessageId(user, pinned->v);
	}
	user->setStarsPerMessage(
		update.vsend_paid_messages_stars().value_or_empty());
	using Flag = UserDataFlag;
	const auto mask = Flag::Blocked
		| Flag::HasPhoneCalls
		| Flag::PhoneCallsPrivate
		| Flag::CanPinMessages
		| Flag::VoiceMessagesForbidden
		| Flag::ReadDatesPrivate
		| (update.is_contact_require_premium()
			? Flag::HasRequirePremiumToWrite
			: Flag())
		| (user->starsPerMessage() ? Flag::HasStarsPerMessage : Flag())
		| Flag::MessageMoneyRestrictionsKnown
		| Flag::RequiresPremiumToWrite;
	user->setFlags((user->flags() & ~mask)
		| (update.is_phone_calls_private()
			? Flag::PhoneCallsPrivate
			: Flag())
		| (update.is_phone_calls_available() ? Flag::HasPhoneCalls : Flag())
		| (update.is_can_pin_message() ? Flag::CanPinMessages : Flag())
		| (update.is_blocked() ? Flag::Blocked : Flag())
		| (update.is_voice_messages_forbidden()
			? Flag::VoiceMessagesForbidden
			: Flag())
		| (update.is_read_dates_private() ? Flag::ReadDatesPrivate : Flag())
		| (user->starsPerMessage() ? Flag::HasStarsPerMessage : Flag())
		| Flag::MessageMoneyRestrictionsKnown
		| (update.is_contact_require_premium()
			? (Flag::RequiresPremiumToWrite | Flag::HasRequirePremiumToWrite)
			: Flag()));
	user->setIsBlocked(update.is_blocked());
	user->setCallsStatus(update.is_phone_calls_private()
		? UserData::CallsStatus::Private
		: update.is_phone_calls_available()
		? UserData::CallsStatus::Enabled
		: UserData::CallsStatus::Disabled);
	user->setAbout(qs(update.vabout().value_or_empty()));
	user->setCommonChatsCount(update.vcommon_chats_count().v);
	user->setPeerGiftsCount(update.vstargifts_count().value_or_empty());
	user->checkFolder(update.vfolder_id().value_or_empty());
	if (const auto theme = update.vtheme()) {
		theme->match([&](const MTPDchatTheme &data) {
			user->setThemeToken(qs(data.vemoticon()));
		}, [&](const MTPDchatThemeUniqueGift &data) {
			user->setThemeToken(
				user->owner().cloudThemes().processGiftThemeGetToken(data));
		});
	} else {
		user->setThemeToken(QString());
	}
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
		if (info->canEditInformation) {
			static constexpr auto kTimeout = crl::time(60000);
			const auto id = user->id;
			const auto weak = base::make_weak(&user->session());
			const auto creditsLoadLifetime
				= std::make_shared<rpl::lifetime>();
			const auto creditsLoad
				= creditsLoadLifetime->make_state<Api::CreditsStatus>(user);
			creditsLoad->request({}, [=](Data::CreditsStatusSlice slice) {
				if (const auto strong = weak.get()) {
					strong->credits().apply(id, slice.balance);
				}
				creditsLoadLifetime->destroy();
			});
			base::timer_once(kTimeout) | rpl::start_with_next([=] {
				creditsLoadLifetime->destroy();
			}, *creditsLoadLifetime);
			const auto currencyLoadLifetime
				= std::make_shared<rpl::lifetime>();
			const auto currencyLoad
				= currencyLoadLifetime->make_state<Api::EarnStatistics>(user);
			const auto apply = [=](const CreditsAmount &balance) {
				if (const auto strong = weak.get()) {
					strong->credits().applyCurrency(id, balance);
				}
				currencyLoadLifetime->destroy();
			};
			currencyLoad->request() | rpl::start_with_error_done(
				[=](const QString &error) {
					apply(CreditsAmount(0, CreditsType::Ton));
				},
				[=] { apply(currencyLoad->data().currentBalance); },
				*currencyLoadLifetime);
			base::timer_once(kTimeout) | rpl::start_with_next([=] {
				currencyLoadLifetime->destroy();
			}, *currencyLoadLifetime);
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
	user->setBotVerifyDetails(
		ParseBotVerifyDetails(update.vbot_verification()));
	user->setStarsRating(ParseStarsRating(update.vstars_rating()));
	if (user->isSelf()) {
		user->owner().setPendingStarsRating({
			.value = ParseStarsRating(update.vstars_my_pending_rating()),
			.date = update.vstars_my_pending_rating_date().value_or_empty(),
		});
	}

	if (const auto gifts = update.vdisallowed_gifts()) {
		const auto &data = gifts->data();
		user->setDisallowedGiftTypes(Api::DisallowedGiftType()
			| (data.is_disallow_unlimited_stargifts()
				? Api::DisallowedGiftType::Unlimited
				: Api::DisallowedGiftType())
			| (data.is_disallow_limited_stargifts()
				? Api::DisallowedGiftType::Limited
				: Api::DisallowedGiftType())
			| (data.is_disallow_unique_stargifts()
				? Api::DisallowedGiftType::Unique
				: Api::DisallowedGiftType())
			| (data.is_disallow_premium_gifts()
				? Api::DisallowedGiftType::Premium
				: Api::DisallowedGiftType())
			| (update.is_display_gifts_button()
				? Api::DisallowedGiftType::SendHide
				: Api::DisallowedGiftType()));
	} else {
		user->setDisallowedGiftTypes(Api::DisallowedGiftTypes()
			| (update.is_display_gifts_button()
				? Api::DisallowedGiftType::SendHide
				: Api::DisallowedGiftType()));
	}

	user->owner().stories().apply(user, update.vstories());
	user->owner().savedMusic().apply(user, update.vsaved_music());

	if (const auto note = update.vnote()) {
		user->setNote(TextWithEntities{
			qs(note->data().vtext()),
			Api::EntitiesFromMTP(
				&user->session(),
				note->data().ventities().v)
		});
	} else {
		user->setNote(TextWithEntities());
	}

	user->fullUpdated();
}

StarRefProgram ParseStarRefProgram(const MTPStarRefProgram *program) {
	if (!program) {
		return {};
	}
	auto result = StarRefProgram();
	const auto &data = program->data();
	result.commission = data.vcommission_permille().v;
	result.durationMonths = data.vduration_months().value_or_empty();
	result.revenuePerUser = CreditsAmountFromTL(
		data.vdaily_revenue_per_user());
	result.endDate = data.vend_date().value_or_empty();
	return result;
}

Ui::BotVerifyDetails ParseBotVerifyDetails(const MTPBotVerification *info) {
	if (!info) {
		return {};
	}
	const auto &data = info->data();
	const auto description = qs(data.vdescription());
	const auto flags = TextParseLinks;
	return {
		.botId = UserId(data.vbot_id().v),
		.iconId = DocumentId(data.vicon().v),
		.description = TextUtilities::ParseEntities(description, flags),
	};
}

} // namespace Data
