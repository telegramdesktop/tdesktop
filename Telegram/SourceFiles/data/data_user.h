/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_peer.h"
#include "dialogs/dialogs_key.h"

class BotCommand {
public:
	BotCommand(const QString &command, const QString &description);

	bool setDescription(const QString &description);
	const Ui::Text::String &descriptionText() const;

	QString command;

private:
	QString _description;
	mutable Ui::Text::String _descriptionText;

};

struct BotInfo {
	bool inited = false;
	bool readsAllHistory = false;
	bool cantJoinGroups = false;
	int version = 0;
	QString description, inlinePlaceholder;
	QList<BotCommand> commands;
	Ui::Text::String text = { int(st::msgMinWidth) }; // description

	QString startToken, startGroupToken, shareGameShortName;
	Dialogs::EntryState inlineReturnTo;
};

class UserData : public PeerData {
public:
	static constexpr auto kEssentialFlags = 0
		| MTPDuser::Flag::f_self
		| MTPDuser::Flag::f_contact
		| MTPDuser::Flag::f_mutual_contact
		| MTPDuser::Flag::f_deleted
		| MTPDuser::Flag::f_bot
		| MTPDuser::Flag::f_bot_chat_history
		| MTPDuser::Flag::f_bot_nochats
		| MTPDuser::Flag::f_verified
		| MTPDuser::Flag::f_scam
		| MTPDuser::Flag::f_restricted
		| MTPDuser::Flag::f_bot_inline_geo;
	using Flags = Data::Flags<
		MTPDuser::Flags,
		kEssentialFlags.value()>;

	static constexpr auto kEssentialFullFlags = 0
		| MTPDuserFull::Flag::f_blocked
		| MTPDuserFull::Flag::f_phone_calls_available
		| MTPDuserFull::Flag::f_phone_calls_private;
	using FullFlags = Data::Flags<
		MTPDuserFull::Flags,
		kEssentialFullFlags.value()>;

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

	void setFlags(MTPDuser::Flags which) {
		_flags.set(which);
	}
	void addFlags(MTPDuser::Flags which) {
		_flags.add(which);
	}
	void removeFlags(MTPDuser::Flags which) {
		_flags.remove(which);
	}
	auto flags() const {
		return _flags.current();
	}
	auto flagsValue() const {
		return _flags.value();
	}

	void setFullFlags(MTPDuserFull::Flags which) {
		_fullFlags.set(which);
	}
	void addFullFlags(MTPDuserFull::Flags which) {
		_fullFlags.add(which);
	}
	void removeFullFlags(MTPDuserFull::Flags which) {
		_fullFlags.remove(which);
	}
	auto fullFlags() const {
		return _fullFlags.current();
	}
	auto fullFlagsValue() const {
		return _fullFlags.value();
	}

	bool isVerified() const {
		return flags() & MTPDuser::Flag::f_verified;
	}
	bool isScam() const {
		return flags() & MTPDuser::Flag::f_scam;
	}
	bool isBotInlineGeo() const {
		return flags() & MTPDuser::Flag::f_bot_inline_geo;
	}
	bool isBot() const {
		return botInfo != nullptr;
	}
	bool isSupport() const {
		return flags() & MTPDuser::Flag::f_support;
	}
	bool isInaccessible() const {
		constexpr auto inaccessible = 0
			| MTPDuser::Flag::f_deleted;
//			| MTPDuser_ClientFlag::f_inaccessible;
		return flags() & inaccessible;
	}
	bool canWrite() const {
		// Duplicated in Data::CanWriteValue().
		return !isInaccessible() && !isRepliesChat();
	}

	bool canShareThisContact() const;
	bool canAddContact() const {
		return canShareThisContact() && !isContact();
	}

	// In Data::Session::processUsers() we check only that.
	// When actually trying to share contact we perform
	// a full check by canShareThisContact() call.
	bool canShareThisContactFast() const {
		return !_phone.isEmpty();
	}

	MTPInputUser inputUser = MTP_inputUserEmpty();

	QString firstName;
	QString lastName;
	QString username;
	const QString &phone() const {
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
	FullFlags _fullFlags;

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
