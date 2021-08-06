/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_peer.h"
#include "dialogs/dialogs_key.h"

struct BotInfo {
	bool inited = false;
	bool readsAllHistory = false;
	bool cantJoinGroups = false;
	int version = 0;
	QString description, inlinePlaceholder;
	std::vector<BotCommand> commands;
	Ui::Text::String text = { int(st::msgMinWidth) }; // description

	QString startToken, startGroupToken, shareGameShortName;
	Dialogs::EntryState inlineReturnTo;
};

enum class UserDataFlag {
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
};
inline constexpr bool is_flag_type(UserDataFlag) { return true; };
using UserDataFlags = base::flags<UserDataFlag>;

class UserData : public PeerData {
public:
	using Flag = UserDataFlag;
	using Flags = Data::Flags<UserDataFlags>;

	UserData(not_null<Data::Session*> owner, PeerId id);
	void setPhoto(const MTPUserProfilePhoto &photo);

	void setName(
		const QString &newFirstName,
		const QString &newLastName,
		const QString &newPhoneName,
		const QString &newUsername);

	void setPhone(const QString &newPhone);
	void setBotInfoVersion(int version);
	void setBotInfo(const MTPBotInfo &info);

	void setNameOrPhone(const QString &newNameOrPhone);

	void madeAction(TimeId when); // pseudo-online

	uint64 accessHash() const {
		return _accessHash;
	}
	void setAccessHash(uint64 accessHash);

	void setFlags(UserDataFlags which) {
		_flags.set(which);
	}
	void addFlags(UserDataFlags which) {
		_flags.add(which);
	}
	void removeFlags(UserDataFlags which) {
		_flags.remove(which);
	}
	auto flags() const {
		return _flags.current();
	}
	auto flagsValue() const {
		return _flags.value();
	}

	[[nodiscard]] bool isVerified() const {
		return flags() & UserDataFlag::Verified;
	}
	[[nodiscard]] bool isScam() const {
		return flags() & UserDataFlag::Scam;
	}
	[[nodiscard]] bool isFake() const {
		return flags() & UserDataFlag::Fake;
	}
	[[nodiscard]] bool isBotInlineGeo() const {
		return flags() & UserDataFlag::BotInlineGeo;
	}
	[[nodiscard]] bool isBot() const {
		return botInfo != nullptr;
	}
	[[nodiscard]] bool isSupport() const {
		return flags() & UserDataFlag::Support;
	}
	[[nodiscard]] bool isInaccessible() const {
		return flags() & UserDataFlag::Deleted;
	}
	[[nodiscard]] bool canWrite() const {
		// Duplicated in Data::CanWriteValue().
		return !isInaccessible() && !isRepliesChat();
	}

	[[nodiscard]] bool canShareThisContact() const;
	[[nodiscard]] bool canAddContact() const {
		return canShareThisContact() && !isContact();
	}

	// In Data::Session::processUsers() we check only that.
	// When actually trying to share contact we perform
	// a full check by canShareThisContact() call.
	[[nodiscard]] bool canShareThisContactFast() const {
		return !_phone.isEmpty();
	}

	MTPInputUser inputUser = MTP_inputUserEmpty();

	QString firstName;
	QString lastName;
	QString username;
	[[nodiscard]] const QString &phone() const {
		return _phone;
	}
	QString nameOrPhone;
	Ui::Text::String phoneText;
	TimeId onlineTill = 0;

	enum class ContactStatus : char {
		Unknown,
		Contact,
		NotContact,
	};
	[[nodiscard]] ContactStatus contactStatus() const {
		return _contactStatus;
	}
	[[nodiscard]] bool isContact() const {
		return (contactStatus() == ContactStatus::Contact);
	}
	void setIsContact(bool is);

	enum class CallsStatus : char {
		Unknown,
		Enabled,
		Disabled,
		Private,
	};
	CallsStatus callsStatus() const {
		return _callsStatus;
	}
	bool hasCalls() const;
	void setCallsStatus(CallsStatus callsStatus);

	std::unique_ptr<BotInfo> botInfo;

	void setUnavailableReasons(
		std::vector<Data::UnavailableReason> &&reasons);

	int commonChatsCount() const {
		return _commonChatsCount;
	}
	void setCommonChatsCount(int count);

private:
	auto unavailableReasons() const
		-> const std::vector<Data::UnavailableReason> & override;

	Flags _flags;

	std::vector<Data::UnavailableReason> _unavailableReasons;
	QString _phone;
	ContactStatus _contactStatus = ContactStatus::Unknown;
	CallsStatus _callsStatus = CallsStatus::Unknown;
	int _commonChatsCount = 0;

	uint64 _accessHash = 0;
	static constexpr auto kInaccessibleAccessHashOld
		= 0xFFFFFFFFFFFFFFFFULL;

};

namespace Data {

void ApplyUserUpdate(not_null<UserData*> user, const MTPDuserFull &update);

} // namespace Data
