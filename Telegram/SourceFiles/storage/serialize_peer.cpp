/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/serialize_peer.h"

#include "storage/serialize_common.h"
#include "main/main_session.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "ui/image/image.h"
#include "app.h"

namespace Serialize {
namespace {

constexpr auto kModernImageLocationTag = std::numeric_limits<qint32>::min();

} // namespace

std::optional<StorageImageLocation> readLegacyStorageImageLocationOrTag(
		int streamAppVersion,
		QDataStream &stream) {
	qint32 width, height, dc, local;
	quint64 volume, secret;
	QByteArray fileReference;
	stream >> width;
	if (width == kModernImageLocationTag) {
		return std::nullopt;
	}
	stream >> height >> dc >> volume >> local >> secret;
	if (streamAppVersion >= 1003013) {
		stream >> fileReference;
	}
	if (stream.status() != QDataStream::Ok) {
		return std::nullopt;
	}
	return StorageImageLocation(
		StorageFileLocation(
			dc,
			UserId(0),
			MTP_inputFileLocation(
				MTP_long(volume),
				MTP_int(local),
				MTP_long(secret),
				MTP_bytes(fileReference))),
		width,
		height);
}

int storageImageLocationSize(const StorageImageLocation &location) {
	// Modern image location tag + (size + content) of the serialization.
	return sizeof(qint32) * 2 + location.serializeSize();
}

void writeStorageImageLocation(
		QDataStream &stream,
		const StorageImageLocation &location) {
	stream << kModernImageLocationTag << location.serialize();
}

std::optional<StorageImageLocation> readStorageImageLocation(
		int streamAppVersion,
		QDataStream &stream) {
	const auto legacy = readLegacyStorageImageLocationOrTag(
		streamAppVersion,
		stream);
	if (legacy) {
		return legacy;
	}
	auto serialized = QByteArray();
	stream >> serialized;
	return (stream.status() == QDataStream::Ok)
		? StorageImageLocation::FromSerialized(serialized)
		: std::nullopt;
}

int imageLocationSize(const ImageLocation &location) {
	// Modern image location tag + (size + content) of the serialization.
	return sizeof(qint32) * 2 + location.serializeSize();
}

void writeImageLocation(QDataStream &stream, const ImageLocation &location) {
	stream << kModernImageLocationTag << location.serialize();
}

std::optional<ImageLocation> readImageLocation(
		int streamAppVersion,
		QDataStream &stream) {
	const auto legacy = readLegacyStorageImageLocationOrTag(
		streamAppVersion,
		stream);
	if (legacy) {
		return ImageLocation(
			DownloadLocation{ legacy->file() },
			legacy->width(),
			legacy->height());
	}
	auto serialized = QByteArray();
	stream >> serialized;
	return (stream.status() == QDataStream::Ok)
		? ImageLocation::FromSerialized(serialized)
		: std::nullopt;
}

uint32 peerSize(not_null<PeerData*> peer) {
	uint32 result = sizeof(quint64)
		+ sizeof(quint64)
		+ imageLocationSize(peer->userpicLocation());
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
	writeImageLocation(stream, peer->userpicLocation());
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
			const auto botInlinePlaceholder = user->isBot()
				? user->botInfo->inlinePlaceholder
				: QString();
			stream << botInlinePlaceholder;
		}
		stream
			<< qint32(user->onlineTill)
			<< qint32(user->isContact() ? 1 : 0)
			<< qint32(user->isBot() ? user->botInfo->version : -1);
	} else if (const auto chat = peer->asChat()) {
		stream
			<< chat->name
			<< qint32(chat->count)
			<< qint32(chat->date)
			<< qint32(chat->version())
			<< qint32(chat->creator)
			<< qint32(0)
			<< quint32(chat->flags())
			<< chat->inviteLink();
	} else if (const auto channel = peer->asChannel()) {
		stream
			<< channel->name
			<< quint64(channel->access)
			<< qint32(channel->date)
			<< qint32(channel->version())
			<< qint32(0)
			<< quint32(channel->flags())
			<< channel->inviteLink();
	}
}

PeerData *readPeer(
		not_null<Main::Session*> session,
		int streamAppVersion,
		QDataStream &stream) {
	quint64 peerId = 0, photoId = 0;
	stream >> peerId >> photoId;
	if (!peerId) {
		return nullptr;
	}

	const auto userpic = readImageLocation(streamAppVersion, stream);
	auto userpicAccessHash = uint64(0);
	if (!userpic) {
		return nullptr;
	}

	const auto selfId = session->userPeerId();
	const auto loaded = (peerId == selfId)
		? session->user().get()
		: session->data().peerLoaded(peerId);
	const auto apply = !loaded || !loaded->isFullLoaded();
	const auto result = loaded ? loaded : session->data().peer(peerId).get();
	if (apply) {
		result->setLoadedStatus(PeerData::LoadedStatus::Full);
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

		userpicAccessHash = access;

		const auto showPhone = !user->isServiceUser()
			&& (user->id != selfId)
			&& (contact <= 0);
		const auto pname = (showPhone && !phone.isEmpty())
			? App::formatPhone(phone)
			: QString();

		if (apply) {
			user->setPhone(phone);
			user->setName(first, last, pname, username);

			user->setFlags(MTPDuser::Flags::from_raw(flags));
			user->setAccessHash(access);
			user->onlineTill = onlineTill;
			user->setIsContact(contact == 1);
			user->setBotInfoVersion(botInfoVersion);
			if (!inlinePlaceholder.isEmpty() && user->isBot()) {
				user->botInfo->inlinePlaceholder = inlinePlaceholder;
			}

			if (user->id == selfId) {
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
		if (apply) {
			chat->setName(name);
			chat->count = count;
			chat->date = date;

			// We don't save participants, admin status and banned rights.
			// So we don't restore the version field, info is still unknown.
			chat->setVersion(0);

			chat->creator = creator;
			chat->setFlags(MTPDchat::Flags::from_raw(flags));
			chat->setInviteLink(inviteLink);

			chat->input = MTP_inputPeerChat(MTP_int(peerToChat(chat->id)));
		}
	} else if (const auto channel = result->asChannel()) {
		QString name, inviteLink;
		quint64 access;
		qint32 date, version, oldForbidden;
		quint32 flags;
		stream >> name >> access >> date >> version >> oldForbidden >> flags >> inviteLink;

		userpicAccessHash = access;

		if (oldForbidden) {
			flags |= quint32(MTPDchannel_ClientFlag::f_forbidden);
		}
		if (apply) {
			channel->setName(name, QString());
			channel->access = access;
			channel->date = date;

			// We don't save participants, admin status and banned rights.
			// So we don't restore the version field, info is still unknown.
			channel->setVersion(0);

			channel->setFlags(MTPDchannel::Flags::from_raw(flags));
			channel->setInviteLink(inviteLink);

			channel->input = MTP_inputPeerChannel(MTP_int(peerToChannel(channel->id)), MTP_long(access));
			channel->inputChannel = MTP_inputChannel(MTP_int(peerToChannel(channel->id)), MTP_long(access));
		}
	}
	if (apply) {
		using LocationType = StorageFileLocation::Type;
		const auto location = (userpic->valid() && userpic->isLegacy())
			? userpic->convertToModern(
				LocationType::PeerPhoto,
				result->id,
				userpicAccessHash)
			: *userpic;
		result->setUserpic(photoId, location);
	}
	return result;
}

QString peekUserPhone(int streamAppVersion, QDataStream &stream) {
	quint64 peerId = 0, photoId = 0;
	stream >> peerId >> photoId;
	DEBUG_LOG(("peekUserPhone.id: %1").arg(peerId));
	if (!peerId
		|| !peerIsUser(peerId)
		|| !readStorageImageLocation(streamAppVersion, stream)) {
		return QString();
	}

	QString first, last, phone;
	stream >> first >> last >> phone;
	DEBUG_LOG(("peekUserPhone.data: %1 %2 %3"
		).arg(first, last, phone));
	return phone;
}

} // namespace Serialize
