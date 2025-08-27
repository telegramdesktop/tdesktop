/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/credits_amount.h"
#include "data/components/credits.h"
#include "data/data_birthday.h"
#include "data/data_peer.h"
#include "data/data_chat_participant_status.h"
#include "data/data_lastseen_status.h"
#include "data/data_user_names.h"
#include "dialogs/dialogs_key.h"
#include "base/flags.h"

namespace Data {
struct BotCommand;
struct BusinessDetails;
} // namespace Data

namespace Api {
enum class DisallowedGiftType : uchar;
using DisallowedGiftTypes = base::flags<DisallowedGiftType>;
} // namespace Api

struct StarRefProgram {
	CreditsAmount revenuePerUser;
	TimeId endDate = 0;
	ushort commission = 0;
	uint8 durationMonths = 0;

	friend inline constexpr bool operator==(
		StarRefProgram,
		StarRefProgram) = default;
};

struct BotVerifierSettings {
	DocumentId iconId = 0;
	QString company;
	QString customDescription;
	bool canModifyDescription = false;

	explicit operator bool() const {
		return iconId != 0;
	}

	friend inline bool operator==(
		const BotVerifierSettings &a,
		const BotVerifierSettings &b) = default;
};

struct BotInfo {
	BotInfo();

	QString description;
	QString inlinePlaceholder;
	std::vector<Data::BotCommand> commands;

	PhotoData *photo = nullptr;
	DocumentData *document = nullptr;

	QString botMenuButtonText;
	QString botMenuButtonUrl;
	QString privacyPolicyUrl;

	QColor botAppColorTitleDay = QColor(0, 0, 0, 0);
	QColor botAppColorTitleNight = QColor(0, 0, 0, 0);
	QColor botAppColorBodyDay = QColor(0, 0, 0, 0);
	QColor botAppColorBodyNight = QColor(0, 0, 0, 0);

	QString startToken;
	Dialogs::EntryState inlineReturnTo;

	ChatAdminRights groupAdminRights;
	ChatAdminRights channelAdminRights;

	StarRefProgram starRefProgram;
	std::unique_ptr<BotVerifierSettings> verifierSettings;

	int version = 0;
	int descriptionVersion = 0;
	int activeUsers = 0;
	bool inited : 1 = false;
	bool readsAllHistory : 1 = false;
	bool cantJoinGroups : 1 = false;
	bool supportsAttachMenu : 1 = false;
	bool canEditInformation : 1 = false;
	bool canManageEmojiStatus : 1 = false;
	bool supportsBusiness : 1 = false;
	bool hasMainApp : 1 = false;
};

enum class UserDataFlag : uint32 {
	Contact = (1 << 0),
	MutualContact = (1 << 1),
	Deleted = (1 << 2),
	Verified = (1 << 3),
	Scam = (1 << 4),
	Fake = (1 << 5),
	BotInlineGeo = (1 << 6),
	Blocked = (1 << 7),
	HasPhoneCalls = (1 << 8),
	PhoneCallsPrivate = (1 << 9),
	Support = (1 << 10),
	CanPinMessages = (1 << 11),
	DiscardMinPhoto = (1 << 12),
	Self = (1 << 13),
	Premium = (1 << 14),
	//CanReceiveGifts = (1 << 15),
	VoiceMessagesForbidden = (1 << 16),
	PersonalPhoto = (1 << 17),
	StoriesHidden = (1 << 18),
	HasActiveStories = (1 << 19),
	HasUnreadStories = (1 << 20),
	RequiresPremiumToWrite = (1 << 21),
	HasRequirePremiumToWrite = (1 << 22),
	HasStarsPerMessage = (1 << 23),
	MessageMoneyRestrictionsKnown = (1 << 24),
	ReadDatesPrivate = (1 << 25),
	StoriesCorrespondent = (1 << 26),
};
inline constexpr bool is_flag_type(UserDataFlag) { return true; };
using UserDataFlags = base::flags<UserDataFlag>;

[[nodiscard]] Data::LastseenStatus LastseenFromMTP(
	const MTPUserStatus &status,
	Data::LastseenStatus currentStatus);

class UserData final : public PeerData {
public:
	using Flag = UserDataFlag;
	using Flags = Data::Flags<UserDataFlags>;

	UserData(not_null<Data::Session*> owner, PeerId id);
	~UserData();

	void setPhoto(const MTPUserProfilePhoto &photo);

	void setName(
		const QString &newFirstName,
		const QString &newLastName,
		const QString &newPhoneName,
		const QString &newUsername);
	void setUsernames(const Data::Usernames &newUsernames);

	void setUsername(const QString &username);
	void setPhone(const QString &newPhone);
	void setBotInfoVersion(int version);
	void setBotInfo(const MTPBotInfo &info);

	void setNameOrPhone(const QString &newNameOrPhone);

	void madeAction(TimeId when); // pseudo-online

	uint64 accessHash() const {
		return _accessHash;
	}
	void setAccessHash(uint64 accessHash);

	auto flags() const {
		return _flags.current();
	}
	auto flagsValue() const {
		return _flags.value();
	}
	void setFlags(UserDataFlags which);
	void addFlags(UserDataFlags which);
	void removeFlags(UserDataFlags which);

	[[nodiscard]] bool isVerified() const;
	[[nodiscard]] bool isScam() const;
	[[nodiscard]] bool isFake() const;
	[[nodiscard]] bool isPremium() const;
	[[nodiscard]] bool isBotInlineGeo() const;
	[[nodiscard]] bool isBot() const;
	[[nodiscard]] bool isSupport() const;
	[[nodiscard]] bool isInaccessible() const;
	[[nodiscard]] bool applyMinPhoto() const;
	[[nodiscard]] bool hasPersonalPhoto() const;
	[[nodiscard]] bool hasStoriesHidden() const;
	[[nodiscard]] bool hasRequirePremiumToWrite() const;
	[[nodiscard]] bool hasStarsPerMessage() const;
	[[nodiscard]] bool requiresPremiumToWrite() const;
	[[nodiscard]] bool messageMoneyRestrictionsKnown() const;
	[[nodiscard]] bool canSendIgnoreMoneyRestrictions() const;
	[[nodiscard]] bool readDatesPrivate() const;

	void setStoriesCorrespondent(bool is);
	[[nodiscard]] bool storiesCorrespondent() const;

	void setStarsPerMessage(int stars);
	[[nodiscard]] int starsPerMessage() const;

	void setStarsRating(Data::StarsRating value);
	[[nodiscard]] Data::StarsRating starsRating() const;

	[[nodiscard]] bool canShareThisContact() const;
	[[nodiscard]] bool canAddContact() const;

	// In Data::Session::processUsers() we check only that.
	// When actually trying to share contact we perform
	// a full check by canShareThisContact() call.
	[[nodiscard]] bool canShareThisContactFast() const;

	[[nodiscard]] const QString &phone() const;
	[[nodiscard]] QString username() const;
	[[nodiscard]] QString editableUsername() const;
	[[nodiscard]] const std::vector<QString> &usernames() const;
	[[nodiscard]] bool isUsernameEditable(QString username) const;

	void setBotVerifyDetails(Ui::BotVerifyDetails details);
	void setBotVerifyDetailsIcon(DocumentId iconId);
	[[nodiscard]] Ui::BotVerifyDetails *botVerifyDetails() const {
		return _botVerifyDetails.get();
	}

	enum class ContactStatus : char {
		Unknown,
		Contact,
		NotContact,
	};
	[[nodiscard]] ContactStatus contactStatus() const;
	[[nodiscard]] bool isContact() const;
	void setIsContact(bool is);

	[[nodiscard]] Data::LastseenStatus lastseen() const;
	bool updateLastseen(Data::LastseenStatus value);

	enum class CallsStatus : char {
		Unknown,
		Enabled,
		Disabled,
		Private,
	};
	CallsStatus callsStatus() const;
	bool hasCalls() const;
	void setCallsStatus(CallsStatus callsStatus);

	[[nodiscard]] Data::Birthday birthday() const;
	void setBirthday(Data::Birthday value);
	void setBirthday(const tl::conditional<MTPBirthday> &value);

	[[nodiscard]] int commonChatsCount() const;
	void setCommonChatsCount(int count);

	[[nodiscard]] int peerGiftsCount() const;
	void setPeerGiftsCount(int count);

	[[nodiscard]] bool hasPrivateForwardName() const;
	[[nodiscard]] QString privateForwardName() const;
	void setPrivateForwardName(const QString &name);

	[[nodiscard]] bool hasActiveStories() const;
	[[nodiscard]] bool hasUnreadStories() const;
	void setStoriesState(StoriesState state);

	[[nodiscard]] const Data::BusinessDetails &businessDetails() const;
	void setBusinessDetails(Data::BusinessDetails details);

	void setStarRefProgram(StarRefProgram program);

	[[nodiscard]] ChannelId personalChannelId() const;
	[[nodiscard]] MsgId personalChannelMessageId() const;
	void setPersonalChannel(ChannelId channelId, MsgId messageId);

	MTPInputUser inputUser = MTP_inputUserEmpty();

	QString firstName;
	QString lastName;
	QString nameOrPhone;

	std::unique_ptr<BotInfo> botInfo;

	[[nodiscard]] Api::DisallowedGiftTypes disallowedGiftTypes() const {
		return _disallowedGiftTypes;
	}
	void setDisallowedGiftTypes(Api::DisallowedGiftTypes types);

private:
	auto unavailableReasons() const
		-> const std::vector<Data::UnavailableReason> & override;

	void setUnavailableReasonsList(
		std::vector<Data::UnavailableReason> &&reasons) override;

	Flags _flags;
	Data::LastseenStatus _lastseen;
	Data::Birthday _birthday;
	int _commonChatsCount = 0;
	int _peerGiftsCount = 0;
	int _starsPerMessage = 0;
	ContactStatus _contactStatus = ContactStatus::Unknown;
	CallsStatus _callsStatus = CallsStatus::Unknown;

	Data::UsernamesInfo _username;

	std::unique_ptr<Data::BusinessDetails> _businessDetails;
	std::vector<Data::UnavailableReason> _unavailableReasons;
	QString _phone;
	QString _privateForwardName;
	std::unique_ptr<Ui::BotVerifyDetails> _botVerifyDetails;
	Data::StarsRating _starsRating;

	ChannelId _personalChannelId = 0;
	MsgId _personalChannelMessageId = 0;

	uint64 _accessHash = 0;
	static constexpr auto kInaccessibleAccessHashOld
		= 0xFFFFFFFFFFFFFFFFULL;

	Api::DisallowedGiftTypes _disallowedGiftTypes;

};

namespace Data {

void ApplyUserUpdate(not_null<UserData*> user, const MTPDuserFull &update);

[[nodiscard]] StarRefProgram ParseStarRefProgram(
	const MTPStarRefProgram *program);

[[nodiscard]] Ui::BotVerifyDetails ParseBotVerifyDetails(
	const MTPBotVerification *info);

} // namespace Data
