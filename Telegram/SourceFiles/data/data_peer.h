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

enum LangKey : int;

namespace Ui {
class EmptyUserpic;
} // namespace Ui

class AuthSession;
class PeerData;
class UserData;
class ChatData;
class ChannelData;

namespace Data {

class Feed;
class Session;

int PeerColorIndex(PeerId peerId);
int PeerColorIndex(int32 bareId);
style::color PeerUserpicColor(PeerId peerId);

} // namespace Data

using ChatAdminRight = MTPDchatAdminRights::Flag;
using ChatRestriction = MTPDchatBannedRights::Flag;
using ChatAdminRights = MTPDchatAdminRights::Flags;
using ChatRestrictions = MTPDchatBannedRights::Flags;

namespace Data {

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
protected:
	PeerData(not_null<Data::Session*> owner, PeerId id);
	PeerData(const PeerData &other) = delete;
	PeerData &operator=(const PeerData &other) = delete;

public:
	virtual ~PeerData();

	[[nodiscard]] Data::Session &owner() const;
	[[nodiscard]] AuthSession &session() const;

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
	[[nodiscard]] bool isMegagroup() const;

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

	[[nodiscard]] UserData *asUser();
	[[nodiscard]] const UserData *asUser() const;
	[[nodiscard]] ChatData *asChat();
	[[nodiscard]] const ChatData *asChat() const;
	[[nodiscard]] ChannelData *asChannel();
	[[nodiscard]] const ChannelData *asChannel() const;
	[[nodiscard]] ChannelData *asMegagroup();
	[[nodiscard]] const ChannelData *asMegagroup() const;
	[[nodiscard]] ChatData *asChatNotMigrated();
	[[nodiscard]] const ChatData *asChatNotMigrated() const;
	[[nodiscard]] ChannelData *asChannelOrMigrated();
	[[nodiscard]] const ChannelData *asChannelOrMigrated() const;

	[[nodiscard]] ChatData *migrateFrom() const;
	[[nodiscard]] ChannelData *migrateTo() const;
	[[nodiscard]] not_null<PeerData*> migrateToOrMe();
	[[nodiscard]] not_null<const PeerData*> migrateToOrMe() const;
	[[nodiscard]] Data::Feed *feed() const;

	void updateFull();
	void updateFullForced();
	void fullUpdated();
	[[nodiscard]] bool wasFullUpdated() const {
		return (_lastFullUpdate != 0);
	}

	[[nodiscard]] const Text &dialogName() const;
	[[nodiscard]] const QString &shortName() const;
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

	void setUserpic(
		PhotoId photoId,
		const StorageImageLocation &location,
		ImagePtr userpic);
	void setUserpicPhoto(const MTPPhoto &data);
	void paintUserpic(
		Painter &p,
		int x,
		int y,
		int size) const;
	void paintUserpicLeft(
			Painter &p,
			int x,
			int y,
			int w,
			int size) const {
		paintUserpic(p, rtl() ? (w - x - size) : x, y, size);
	}
	void paintUserpicRounded(
		Painter &p,
		int x,
		int y,
		int size) const;
	void paintUserpicSquare(
		Painter &p,
		int x,
		int y,
		int size) const;
	void loadUserpic(bool loadFirst = false, bool prior = true);
	[[nodiscard]] bool userpicLoaded() const;
	[[nodiscard]] bool useEmptyUserpic() const;
	[[nodiscard]] StorageKey userpicUniqueKey() const;
	void saveUserpic(const QString &path, int size) const;
	void saveUserpicRounded(const QString &path, int size) const;
	[[nodiscard]] QPixmap genUserpic(int size) const;
	[[nodiscard]] QPixmap genUserpicRounded(int size) const;
	[[nodiscard]] StorageImageLocation userpicLocation() const {
		return _userpicLocation;
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
	[[nodiscard]] virtual QString unavailableReason() const {
		return QString();
	}

	[[nodiscard]] ClickHandlerPtr createOpenLink();
	[[nodiscard]] const ClickHandlerPtr &openLink() {
		if (!_openLink) {
			_openLink = createOpenLink();
		}
		return _openLink;
	}

	[[nodiscard]] ImagePtr currentUserpic() const;

	[[nodiscard]] bool canPinMessages() const;
	[[nodiscard]] MsgId pinnedMessageId() const {
		return _pinnedMessageId;
	}
	void setPinnedMessageId(MsgId messageId);
	void clearPinnedMessage() {
		setPinnedMessageId(0);
	}

	// Returns true if about text was changed.
	bool setAbout(const QString &newAbout);
	const QString &about() const {
		return _about;
	}

	enum LoadedStatus {
		NotLoaded = 0x00,
		MinimalLoaded = 0x01,
		FullLoaded = 0x02,
	};

	const PeerId id;
	QString name;
	Text nameText;
	LoadedStatus loadedStatus = NotLoaded;
	MTPinputPeer input;

	int nameVersion = 1;

protected:
	void updateNameDelayed(
		const QString &newName,
		const QString &newNameOrPhone,
		const QString &newUsername);
	void updateUserpic(PhotoId photoId, const MTPFileLocation &location);
	void clearUserpic();

private:
	void fillNames();
	std::unique_ptr<Ui::EmptyUserpic> createEmptyUserpic() const;
	void refreshEmptyUserpic() const;

	void setUserpicChecked(
		PhotoId photoId,
		const StorageImageLocation &location,
		ImagePtr userpic);

	static constexpr auto kUnknownPhotoId = PhotoId(0xFFFFFFFFFFFFFFFFULL);

	not_null<Data::Session*> _owner;

	ImagePtr _userpic;
	PhotoId _userpicPhotoId = kUnknownPhotoId;
	mutable std::unique_ptr<Ui::EmptyUserpic> _userpicEmpty;
	StorageImageLocation _userpicLocation;

	Data::NotifySettings _notify;

	ClickHandlerPtr _openLink;
	base::flat_set<QString> _nameWords; // for filtering
	base::flat_set<QChar> _nameFirstLetters;

	TimeMs _lastFullUpdate = 0;
	MsgId _pinnedMessageId = 0;

	QString _about;

};

namespace Data {

std::optional<LangKey> RestrictionErrorKey(
	not_null<PeerData*> peer,
	ChatRestriction restriction);

} // namespace Data
