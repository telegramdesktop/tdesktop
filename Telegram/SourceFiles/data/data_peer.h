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

	Data::Session &owner() const;
	AuthSession &session() const;

	bool isUser() const {
		return peerIsUser(id);
	}
	bool isChat() const {
		return peerIsChat(id);
	}
	bool isChannel() const {
		return peerIsChannel(id);
	}
	bool isSelf() const {
		return (input.type() == mtpc_inputPeerSelf);
	}
	bool isVerified() const;
	bool isMegagroup() const;

	std::optional<TimeId> notifyMuteUntil() const {
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
	bool notifySettingsUnknown() const {
		return _notify.settingsUnknown();
	}
	std::optional<bool> notifySilentPosts() const {
		return _notify.silentPosts();
	}
	MTPinputPeerNotifySettings notifySerialize() const {
		return _notify.serialize();
	}

	bool canWrite() const;
	UserData *asUser();
	const UserData *asUser() const;
	ChatData *asChat();
	const ChatData *asChat() const;
	ChannelData *asChannel();
	const ChannelData *asChannel() const;
	ChannelData *asMegagroup();
	const ChannelData *asMegagroup() const;

	ChatData *migrateFrom() const;
	ChannelData *migrateTo() const;
	Data::Feed *feed() const;

	void updateFull();
	void updateFullForced();
	void fullUpdated();
	bool wasFullUpdated() const {
		return (_lastFullUpdate != 0);
	}

	const Text &dialogName() const;
	const QString &shortName() const;
	QString userName() const;

	const PeerId id;
	int32 bareId() const {
		return int32(uint32(id & 0xFFFFFFFFULL));
	}

	QString name;
	Text nameText;

	const base::flat_set<QString> &nameWords() const {
		return _nameWords;
	}
	const base::flat_set<QChar> &nameFirstLetters() const {
		return _nameFirstLetters;
	}

	enum LoadedStatus {
		NotLoaded = 0x00,
		MinimalLoaded = 0x01,
		FullLoaded = 0x02,
	};
	LoadedStatus loadedStatus = NotLoaded;
	MTPinputPeer input;

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
	bool userpicLoaded() const;
	bool useEmptyUserpic() const;
	StorageKey userpicUniqueKey() const;
	void saveUserpic(const QString &path, int size) const;
	void saveUserpicRounded(const QString &path, int size) const;
	QPixmap genUserpic(int size) const;
	QPixmap genUserpicRounded(int size) const;
	StorageImageLocation userpicLocation() const {
		return _userpicLocation;
	}
	bool userpicPhotoUnknown() const {
		return (_userpicPhotoId == kUnknownPhotoId);
	}
	PhotoId userpicPhotoId() const {
		return userpicPhotoUnknown() ? 0 : _userpicPhotoId;
	}
	Data::FileOrigin userpicOrigin() const;
	Data::FileOrigin userpicPhotoOrigin() const;

	int nameVersion = 1;

	// If this string is not empty we must not allow to open the
	// conversation and we must show this string instead.
	virtual QString unavailableReason() const {
		return QString();
	}

	ClickHandlerPtr createOpenLink();
	const ClickHandlerPtr &openLink() {
		if (!_openLink) {
			_openLink = createOpenLink();
		}
		return _openLink;
	}

	ImagePtr currentUserpic() const;

	bool canPinMessages() const;
	MsgId pinnedMessageId() const {
		return _pinnedMessageId;
	}
	void setPinnedMessageId(MsgId messageId);
	void clearPinnedMessage() {
		setPinnedMessageId(0);
	}

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

};
