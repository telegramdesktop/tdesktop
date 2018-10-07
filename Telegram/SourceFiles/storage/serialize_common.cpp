/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/serialize_common.h"

#include "auth_session.h"

namespace Serialize {

void writeStorageImageLocation(
		QDataStream &stream,
		const StorageImageLocation &location) {
	stream
		<< qint32(location.width())
		<< qint32(location.height())
		<< qint32(location.dc())
		<< quint64(location.volume())
		<< qint32(location.local())
		<< quint64(location.secret());
	stream << location.fileReference();
}

StorageImageLocation readStorageImageLocation(
		int streamAppVersion,
		QDataStream &stream) {
	qint32 width, height, dc, local;
	quint64 volume, secret;
	QByteArray fileReference;
	stream >> width >> height >> dc >> volume >> local >> secret;
	if (streamAppVersion >= 1003013) {
		stream >> fileReference;
	}
	return StorageImageLocation(
		width,
		height,
		dc,
		volume,
		local,
		secret,
		fileReference);
}

int storageImageLocationSize(const StorageImageLocation &location) {
	// width + height + dc + volume + local + secret + fileReference
	return sizeof(qint32)
		+ sizeof(qint32)
		+ sizeof(qint32)
		+ sizeof(quint64)
		+ sizeof(qint32)
		+ sizeof(quint64)
		+ bytearraySize(location.fileReference());
}

uint32 peerSize(not_null<PeerData*> peer) {
	uint32 result = sizeof(quint64)
		+ sizeof(quint64)
		+ storageImageLocationSize(peer->userpicLocation());
	if (peer->isUser()) {
		UserData *user = peer->asUser();

		// first + last + phone + username + access
		result += stringSize(user->firstName) + stringSize(user->lastName) + stringSize(user->phone()) + stringSize(user->username) + sizeof(quint64);

		// flags
		if (AppVersion >= 9012) {
			result += sizeof(qint32);
		}

		// onlineTill + contact + botInfoVersion
		result += sizeof(qint32) + sizeof(qint32) + sizeof(qint32);
	} else if (peer->isChat()) {
		ChatData *chat = peer->asChat();

		// name + count + date + version + admin + old forbidden + left + inviteLink
		result += stringSize(chat->name) + sizeof(qint32) + sizeof(qint32) + sizeof(qint32) + sizeof(qint32) + sizeof(qint32) + sizeof(quint32) + stringSize(chat->inviteLink());
	} else if (peer->isChannel()) {
		ChannelData *channel = peer->asChannel();

		// name + access + date + version + old forbidden + flags + inviteLink
		result += stringSize(channel->name) + sizeof(quint64) + sizeof(qint32) + sizeof(qint32) + sizeof(qint32) + sizeof(quint32) + stringSize(channel->inviteLink());
	}
	return result;
}

void writePeer(QDataStream &stream, PeerData *peer) {
	stream << quint64(peer->id) << quint64(peer->userpicPhotoId());
	writeStorageImageLocation(stream, peer->userpicLocation());
	if (const auto user = peer->asUser()) {
		stream
			<< user->firstName
			<< user->lastName
			<< user->phone()
			<< user->username
			<< quint64(user->accessHash());
		if (AppVersion >= 9012) {
			stream << qint32(user->flags());
		}
		if (AppVersion >= 9016) {
			const auto botInlinePlaceholder = user->botInfo
				? user->botInfo->inlinePlaceholder
				: QString();
			stream << botInlinePlaceholder;
		}
		const auto contactSerialized = [&] {
			switch (user->contactStatus()) {
			case UserData::ContactStatus::Contact: return 1;
			case UserData::ContactStatus::CanAdd: return 0;
			case UserData::ContactStatus::PhoneUnknown: return -1;
			}
			Unexpected("contactStatus in _writePeer()");
		}();
		stream
			<< qint32(user->onlineTill)
			<< qint32(contactSerialized)
			<< qint32(user->botInfo ? user->botInfo->version : -1);
	} else if (const auto chat = peer->asChat()) {
		stream
			<< chat->name
			<< qint32(chat->count)
			<< qint32(chat->date)
			<< qint32(chat->version)
			<< qint32(chat->creator)
			<< qint32(0)
			<< quint32(chat->flags())
			<< chat->inviteLink();
	} else if (const auto channel = peer->asChannel()) {
		stream
			<< channel->name
			<< quint64(channel->access)
			<< qint32(channel->date)
			<< qint32(channel->version)
			<< qint32(0)
			<< quint32(channel->flags())
			<< channel->inviteLink();
	}
}

PeerData *readPeer(int streamAppVersion, QDataStream &stream) {
	quint64 peerId = 0, photoId = 0;
	stream >> peerId >> photoId;
	if (!peerId) {
		return nullptr;
	}

	auto photoLoc = readStorageImageLocation(
		streamAppVersion,
		stream);

	PeerData *result = App::peerLoaded(peerId);
	bool wasLoaded = (result != nullptr);
	if (!wasLoaded) {
		result = App::peer(peerId);
		result->loadedStatus = PeerData::FullLoaded;
	}
	if (const auto user = result->asUser()) {
		QString first, last, phone, username, inlinePlaceholder;
		quint64 access;
		qint32 flags = 0, onlineTill, contact, botInfoVersion;
		stream >> first >> last >> phone >> username >> access;
		if (streamAppVersion >= 9012) {
			stream >> flags;
		}
		if (streamAppVersion >= 9016) {
			stream >> inlinePlaceholder;
		}
		stream >> onlineTill >> contact >> botInfoVersion;

		const auto showPhone = !isServiceUser(user->id)
			&& (user->id != Auth().userPeerId())
			&& (contact <= 0);
		const auto pname = (showPhone && !phone.isEmpty())
			? App::formatPhone(phone)
			: QString();

		if (!wasLoaded) {
			user->setPhone(phone);
			user->setName(first, last, pname, username);

			user->setFlags(MTPDuser::Flags::from_raw(flags));
			user->setAccessHash(access);
			user->onlineTill = onlineTill;
			user->setContactStatus((contact > 0)
				? UserData::ContactStatus::Contact
				: (contact == 0)
				? UserData::ContactStatus::CanAdd
				: UserData::ContactStatus::PhoneUnknown);
			user->setBotInfoVersion(botInfoVersion);
			if (!inlinePlaceholder.isEmpty() && user->botInfo) {
				user->botInfo->inlinePlaceholder = inlinePlaceholder;
			}

			if (user->id == Auth().userPeerId()) {
				user->input = MTP_inputPeerSelf();
				user->inputUser = MTP_inputUserSelf();
			} else {
				user->input = MTP_inputPeerUser(MTP_int(peerToUser(user->id)), MTP_long(user->accessHash()));
				user->inputUser = MTP_inputUser(MTP_int(peerToUser(user->id)), MTP_long(user->accessHash()));
			}
		}
	} else if (const auto chat = result->asChat()) {
		QString name, inviteLink;
		qint32 count, date, version, creator, oldForbidden;
		quint32 flagsData, flags;
		stream >> name >> count >> date >> version >> creator >> oldForbidden >> flagsData >> inviteLink;

		if (streamAppVersion >= 9012) {
			flags = flagsData;
		} else {
			// flagsData was haveLeft
			flags = (flagsData == 1)
				? MTPDchat::Flags(MTPDchat::Flag::f_left)
				: MTPDchat::Flags(0);
		}
		if (oldForbidden) {
			flags |= quint32(MTPDchat_ClientFlag::f_forbidden);
		}
		if (!wasLoaded) {
			chat->setName(name);
			chat->count = count;
			chat->date = date;
			chat->version = version;
			chat->creator = creator;
			chat->setFlags(MTPDchat::Flags::from_raw(flags));
			chat->setInviteLink(inviteLink);

			chat->input = MTP_inputPeerChat(MTP_int(peerToChat(chat->id)));
			chat->inputChat = MTP_int(peerToChat(chat->id));
		}
	} else if (const auto channel = result->asChannel()) {
		QString name, inviteLink;
		quint64 access;
		qint32 date, version, oldForbidden;
		quint32 flags;
		stream >> name >> access >> date >> version >> oldForbidden >> flags >> inviteLink;
		if (oldForbidden) {
			flags |= quint32(MTPDchannel_ClientFlag::f_forbidden);
		}
		if (!wasLoaded) {
			channel->setName(name, QString());
			channel->access = access;
			channel->date = date;
			channel->version = version;
			channel->setFlags(MTPDchannel::Flags::from_raw(flags));
			channel->setInviteLink(inviteLink);

			channel->input = MTP_inputPeerChannel(MTP_int(peerToChannel(channel->id)), MTP_long(access));
			channel->inputChannel = MTP_inputChannel(MTP_int(peerToChannel(channel->id)), MTP_long(access));
		}
	}
	if (!wasLoaded) {
		result->setUserpic(
			photoId,
			photoLoc,
			photoLoc.isNull() ? ImagePtr() : ImagePtr(photoLoc));
	}
	return result;
}

QString peekUserPhone(int streamAppVersion, QDataStream &stream) {
	quint64 peerId = 0, photoId = 0;
	stream >> peerId >> photoId;
	if (!peerId || !peerIsUser(peerId)) {
		return QString();
	}

	const auto photoLoc = readStorageImageLocation(
		streamAppVersion,
		stream);
	QString first, last, phone;
	stream >> first >> last >> phone;
	return phone;
}

} // namespace Serialize
