/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_types.h"
#include "data/data_flags.h"
#include "data/data_notify_settings.h"
#include "data/data_cloud_file.h"

class PeerData;
class UserData;
class ChatData;
class ChannelData;

namespace Ui {
class EmptyUserpic;
} // namespace Ui

namespace Main {
class Account;
class Session;
} // namespace Main

namespace Data {

class Session;

int PeerColorIndex(PeerId peerId);
int PeerColorIndex(int32 bareId);
style::color PeerUserpicColor(PeerId peerId);
PeerId FakePeerIdForJustName(const QString &name);

} // namespace Data

using ChatAdminRight = MTPDchatAdminRights::Flag;
using ChatRestriction = MTPDchatBannedRights::Flag;
using ChatAdminRights = MTPDchatAdminRights::Flags;
using ChatRestrictions = MTPDchatBannedRights::Flags;

namespace Data {

class CloudImageView;

class RestrictionCheckResult {
public:
	[[nodiscard]] static RestrictionCheckResult Allowed() {
		return { 0 };
	}
	[[nodiscard]] static RestrictionCheckResult WithEveryone() {
		return { 1 };
	}
	[[nodiscard]] static RestrictionCheckResult Explicit() {
		return { 2 };
	}

	explicit operator bool() const {
		return (_value != 0);
	}

	bool operator==(const RestrictionCheckResult &other) const {
		return (_value == other._value);
	}
	bool operator!=(const RestrictionCheckResult &other) const {
		return !(*this == other);
	}

	[[nodiscard]] bool isAllowed() const {
		return (*this == Allowed());
	}
	[[nodiscard]] bool isWithEveryone() const {
		return (*this == WithEveryone());
	}
	[[nodiscard]] bool isExplicit() const {
		return (*this == Explicit());
	}

private:
	RestrictionCheckResult(int value) : _value(value) {
	}

	int _value = 0;

};

struct UnavailableReason {
	QString reason;
	QString text;

	bool operator==(const UnavailableReason &other) const {
		return (reason == other.reason) && (text == other.text);
	}
	bool operator!=(const UnavailableReason &other) const {
		return !(*this == other);
	}
};

} // namespace Data

class PeerClickHandler : public ClickHandler {
public:
	PeerClickHandler(not_null<PeerData*> peer);
	void onClick(ClickContext context) const override;

	not_null<PeerData*> peer() const {
		return _peer;
	}

private:
	not_null<PeerData*> _peer;

};

class PeerData {
private:
	static constexpr auto kSettingsUnknown = MTPDpeerSettings::Flag(1U << 9);

protected:
	PeerData(not_null<Data::Session*> owner, PeerId id);
	PeerData(const PeerData &other) = delete;
	PeerData &operator=(const PeerData &other) = delete;

public:
	static constexpr auto kEssentialSettings = 0
		| MTPDpeerSettings::Flag::f_report_spam
		| MTPDpeerSettings::Flag::f_add_contact
		| MTPDpeerSettings::Flag::f_block_contact
		| MTPDpeerSettings::Flag::f_share_contact
		| kSettingsUnknown;
	using Settings = Data::Flags<
		MTPDpeerSettings::Flags,
		kEssentialSettings.value()>;

	virtual ~PeerData();

	static constexpr auto kServiceNotificationsId = peerFromUser(777000);

	[[nodiscard]] Data::Session &owner() const;
	[[nodiscard]] Main::Session &session() const;
	[[nodiscard]] Main::Account &account() const;

	[[nodiscard]] bool isUser() const {
		return peerIsUser(id);
	}
	[[nodiscard]] bool isChat() const {
		return peerIsChat(id);
	}
	[[nodiscard]] bool isChannel() const {
		return peerIsChannel(id);
	}
	[[nodiscard]] bool isSelf() const {
		return (input.type() == mtpc_inputPeerSelf);
	}
	[[nodiscard]] bool isVerified() const;
	[[nodiscard]] bool isScam() const;
	[[nodiscard]] bool isMegagroup() const;
	[[nodiscard]] bool isBroadcast() const;
	[[nodiscard]] bool isRepliesChat() const;
	[[nodiscard]] bool sharedMediaInfo() const {
		return isSelf() || isRepliesChat();
	}

	[[nodiscard]] bool isNotificationsUser() const {
		return (id == peerFromUser(333000))
			|| (id == kServiceNotificationsId);
	}
	[[nodiscard]] bool isServiceUser() const {
		return isUser() && !(id % 1000);
	}

	[[nodiscard]] std::optional<TimeId> notifyMuteUntil() const {
		return _notify.muteUntil();
	}
	bool notifyChange(const MTPPeerNotifySettings &settings) {
		return _notify.change(settings);
	}
	bool notifyChange(
			std::optional<int> muteForSeconds,
			std::optional<bool> silentPosts) {
		return _notify.change(muteForSeconds, silentPosts);
	}
	[[nodiscard]] bool notifySettingsUnknown() const {
		return _notify.settingsUnknown();
	}
	[[nodiscard]] std::optional<bool> notifySilentPosts() const {
		return _notify.silentPosts();
	}
	[[nodiscard]] MTPinputPeerNotifySettings notifySerialize() const {
		return _notify.serialize();
	}

	[[nodiscard]] bool canWrite() const;
	[[nodiscard]] Data::RestrictionCheckResult amRestricted(
		ChatRestriction right) const;
	[[nodiscard]] bool amAnonymous() const;
	[[nodiscard]] bool canRevokeFullHistory() const;
	[[nodiscard]] bool slowmodeApplied() const;
	[[nodiscard]] rpl::producer<bool> slowmodeAppliedValue() const;
	[[nodiscard]] int slowmodeSecondsLeft() const;
	[[nodiscard]] bool canSendPolls() const;

	[[nodiscard]] UserData *asUser();
	[[nodiscard]] const UserData *asUser() const;
	[[nodiscard]] ChatData *asChat();
	[[nodiscard]] const ChatData *asChat() const;
	[[nodiscard]] ChannelData *asChannel();
	[[nodiscard]] const ChannelData *asChannel() const;
	[[nodiscard]] ChannelData *asMegagroup();
	[[nodiscard]] const ChannelData *asMegagroup() const;
	[[nodiscard]] ChannelData *asBroadcast();
	[[nodiscard]] const ChannelData *asBroadcast() const;
	[[nodiscard]] ChatData *asChatNotMigrated();
	[[nodiscard]] const ChatData *asChatNotMigrated() const;
	[[nodiscard]] ChannelData *asChannelOrMigrated();
	[[nodiscard]] const ChannelData *asChannelOrMigrated() const;

	[[nodiscard]] ChatData *migrateFrom() const;
	[[nodiscard]] ChannelData *migrateTo() const;
	[[nodiscard]] not_null<PeerData*> migrateToOrMe();
	[[nodiscard]] not_null<const PeerData*> migrateToOrMe() const;

	void updateFull();
	void updateFullForced();
	void fullUpdated();
	[[nodiscard]] bool wasFullUpdated() const {
		return (_lastFullUpdate != 0);
	}

	[[nodiscard]] const Ui::Text::String &nameText() const;
	[[nodiscard]] const QString &shortName() const;
	[[nodiscard]] const Ui::Text::String &topBarNameText() const;
	[[nodiscard]] QString userName() const;

	[[nodiscard]] int32 bareId() const {
		return int32(uint32(id & 0xFFFFFFFFULL));
	}

	[[nodiscard]] const base::flat_set<QString> &nameWords() const {
		return _nameWords;
	}
	[[nodiscard]] const base::flat_set<QChar> &nameFirstLetters() const {
		return _nameFirstLetters;
	}

	void setUserpic(PhotoId photoId, const ImageLocation &location);
	void setUserpicPhoto(const MTPPhoto &data);
	void paintUserpic(
		Painter &p,
		std::shared_ptr<Data::CloudImageView> &view,
		int x,
		int y,
		int size) const;
	void paintUserpicLeft(
			Painter &p,
			std::shared_ptr<Data::CloudImageView> &view,
			int x,
			int y,
			int w,
			int size) const {
		paintUserpic(p, view, rtl() ? (w - x - size) : x, y, size);
	}
	void paintUserpicRounded(
		Painter &p,
		std::shared_ptr<Data::CloudImageView> &view,
		int x,
		int y,
		int size) const;
	void paintUserpicSquare(
		Painter &p,
		std::shared_ptr<Data::CloudImageView> &view,
		int x,
		int y,
		int size) const;
	void loadUserpic();
	[[nodiscard]] bool hasUserpic() const;
	[[nodiscard]] std::shared_ptr<Data::CloudImageView> activeUserpicView();
	[[nodiscard]] std::shared_ptr<Data::CloudImageView> createUserpicView();
	[[nodiscard]] bool useEmptyUserpic(
		std::shared_ptr<Data::CloudImageView> &view) const;
	[[nodiscard]] InMemoryKey userpicUniqueKey(
		std::shared_ptr<Data::CloudImageView> &view) const;
	void saveUserpic(
		std::shared_ptr<Data::CloudImageView> &view,
		const QString &path,
		int size) const;
	void saveUserpicRounded(
		std::shared_ptr<Data::CloudImageView> &view,
		const QString &path,
		int size) const;
	[[nodiscard]] QPixmap genUserpic(
		std::shared_ptr<Data::CloudImageView> &view,
		int size) const;
	[[nodiscard]] QPixmap genUserpicRounded(
		std::shared_ptr<Data::CloudImageView> &view,
		int size) const;
	[[nodiscard]] ImageLocation userpicLocation() const {
		return _userpic.location();
	}
	[[nodiscard]] bool userpicPhotoUnknown() const {
		return (_userpicPhotoId == kUnknownPhotoId);
	}
	[[nodiscard]] PhotoId userpicPhotoId() const {
		return userpicPhotoUnknown() ? 0 : _userpicPhotoId;
	}
	[[nodiscard]] Data::FileOrigin userpicOrigin() const;
	[[nodiscard]] Data::FileOrigin userpicPhotoOrigin() const;

	// If this string is not empty we must not allow to open the
	// conversation and we must show this string instead.
	[[nodiscard]] QString computeUnavailableReason() const;

	[[nodiscard]] ClickHandlerPtr createOpenLink();
	[[nodiscard]] const ClickHandlerPtr &openLink() {
		if (!_openLink) {
			_openLink = createOpenLink();
		}
		return _openLink;
	}

	[[nodiscard]] Image *currentUserpic(
		std::shared_ptr<Data::CloudImageView> &view) const;

	[[nodiscard]] bool canPinMessages() const;
	[[nodiscard]] bool canEditMessagesIndefinitely() const;

	[[nodiscard]] bool hasPinnedMessages() const;
	void setHasPinnedMessages(bool has);

	[[nodiscard]] bool canExportChatHistory() const;

	// Returns true if about text was changed.
	bool setAbout(const QString &newAbout);
	const QString &about() const {
		return _about;
	}

	void checkFolder(FolderId folderId);

	void setSettings(MTPDpeerSettings::Flags which) {
		_settings.set(which);
	}
	auto settings() const {
		return (_settings.current() & kSettingsUnknown)
			? std::nullopt
			: std::make_optional(_settings.current());
	}
	auto settingsValue() const {
		return (_settings.current() & kSettingsUnknown)
			? _settings.changes()
			: (_settings.value() | rpl::type_erased());
	}

	enum class BlockStatus : char {
		Unknown,
		Blocked,
		NotBlocked,
	};
	[[nodiscard]] BlockStatus blockStatus() const {
		return _blockStatus;
	}
	[[nodiscard]] bool isBlocked() const {
		return (blockStatus() == BlockStatus::Blocked);
	}
	void setIsBlocked(bool is);

	enum class LoadedStatus : char {
		Not,
		Minimal,
		Full,
	};
	[[nodiscard]] LoadedStatus loadedStatus() const {
		return _loadedStatus;
	}
	[[nodiscard]] bool isMinimalLoaded() const {
		return (loadedStatus() != LoadedStatus::Not);
	}
	[[nodiscard]] bool isFullLoaded() const {
		return (loadedStatus() == LoadedStatus::Full);
	}
	void setLoadedStatus(LoadedStatus status);

	const PeerId id;
	QString name;
	MTPinputPeer input = MTP_inputPeerEmpty();

	int nameVersion = 1;

protected:
	void updateNameDelayed(
		const QString &newName,
		const QString &newNameOrPhone,
		const QString &newUsername);
	void updateUserpic(
		PhotoId photoId,
		MTP::DcId dcId,
		const MTPFileLocation &small);
	void clearUserpic();

private:
	void fillNames();
	[[nodiscard]] not_null<Ui::EmptyUserpic*> ensureEmptyUserpic() const;
	[[nodiscard]] virtual auto unavailableReasons() const
		-> const std::vector<Data::UnavailableReason> &;

	void setUserpicChecked(PhotoId photoId, const ImageLocation &location);

	static constexpr auto kUnknownPhotoId = PhotoId(0xFFFFFFFFFFFFFFFFULL);

	const not_null<Data::Session*> _owner;

	mutable Data::CloudImage _userpic;
	PhotoId _userpicPhotoId = kUnknownPhotoId;
	mutable std::unique_ptr<Ui::EmptyUserpic> _userpicEmpty;
	Ui::Text::String _nameText;

	Data::NotifySettings _notify;

	ClickHandlerPtr _openLink;
	base::flat_set<QString> _nameWords; // for filtering
	base::flat_set<QChar> _nameFirstLetters;

	crl::time _lastFullUpdate = 0;
	bool _hasPinnedMessages = false;

	Settings _settings = { kSettingsUnknown };
	BlockStatus _blockStatus = BlockStatus::Unknown;
	LoadedStatus _loadedStatus = LoadedStatus::Not;

	QString _about;

};

namespace Data {

std::vector<ChatRestrictions> ListOfRestrictions();

std::optional<QString> RestrictionError(
	not_null<PeerData*> peer,
	ChatRestriction restriction);

void SetTopPinnedMessageId(not_null<PeerData*> peer, MsgId messageId);
[[nodiscard]] FullMsgId ResolveTopPinnedId(
	not_null<PeerData*> peer,
	PeerData *migrated);
[[nodiscard]] FullMsgId ResolveMinPinnedId(
	not_null<PeerData*> peer,
	PeerData *migrated);
[[nodiscard]] std::optional<int> ResolvePinnedCount(
	not_null<PeerData*> peer,
	PeerData *migrated);

} // namespace Data
