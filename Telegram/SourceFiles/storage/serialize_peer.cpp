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
#include "ui/text/format_values.h" // Ui::FormatPhone

namespace Serialize {
namespace {

constexpr auto kModernImageLocationTag = std::numeric_limits<qint32>::min();
constexpr auto kVersionTag = uint64(0x77FF'FFFF'FFFF'FFFFULL);
constexpr auto kVersion = 2;

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
			UserId(0), // self
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
	uint32 result = sizeof(quint64) // id
		+ sizeof(quint64) // version tag
		+ sizeof(qint32) // version
		+ sizeof(quint64) // userpic photo id
		+ imageLocationSize(peer->userpicLocation())
		+ sizeof(qint32); // userpic has video
	if (const auto user = peer->asUser()) {
		const auto botInlinePlaceholder = user->isBot()
			? user->botInfo->inlinePlaceholder
			: QString();
		result += stringSize(user->firstName)
			+ stringSize(user->lastName)
			+ stringSize(user->phone())
			+ stringSize(user->username())
			+ sizeof(quint64) // access
			+ sizeof(qint32) // flags
			+ stringSize(botInlinePlaceholder)
			+ sizeof(quint32) // lastseen
			+ sizeof(qint32) // contact
			+ sizeof(qint32); // botInfoVersion
	} else if (const auto chat = peer->asChat()) {
		result += stringSize(chat->name())
			+ sizeof(qint32) // count
			+ sizeof(qint32) // date
			+ sizeof(qint32) // version
			+ sizeof(qint32) // creator id 1
			+ sizeof(qint32) // creator id 2
			+ sizeof(quint32) // flags
			+ stringSize(chat->inviteLink());
	} else if (const auto channel = peer->asChannel()) {
		result += stringSize(channel->name())
			+ sizeof(quint64) // access
			+ sizeof(qint32) // date
			+ sizeof(qint32) // version
			+ sizeof(qint32) // old forbidden
			+ sizeof(quint32) // flags
			+ stringSize(channel->inviteLink());
	}
	return result;
}

void writePeer(QDataStream &stream, not_null<PeerData*> peer) {
	stream
		<< SerializePeerId(peer->id)
		<< quint64(kVersionTag)
		<< qint32(kVersion)
		<< quint64(peer->userpicPhotoId());
	writeImageLocation(stream, peer->userpicLocation());
	stream << qint32(peer->userpicHasVideo() ? 1 : 0);
	if (const auto user = peer->asUser()) {
		const auto botInlinePlaceholder = user->isBot()
			? user->botInfo->inlinePlaceholder
			: QString();
		stream
			<< user->firstName
			<< user->lastName
			<< user->phone()
			<< user->username()
			<< quint64(user->accessHash())
			<< qint32(user->flags())
			<< botInlinePlaceholder
			<< quint32(user->lastseen().serialize())
			<< qint32(user->isContact() ? 1 : 0)
			<< qint32(user->isBot() ? user->botInfo->version : -1);
	} else if (const auto chat = peer->asChat()) {
		auto field1 = qint32(uint32(chat->creator.bare & 0xFFFFFFFFULL));
		auto field2 = qint32(uint32(chat->creator.bare >> 32) << 8);
		stream
			<< chat->name()
			<< qint32(chat->count)
			<< qint32(chat->date)
			<< qint32(chat->version())
			<< field1
			<< field2
			<< quint32(chat->flags())
			<< chat->inviteLink();
	} else if (const auto channel = peer->asChannel()) {
		stream
			<< channel->name()
			<< quint64(channel->access)
			<< qint32(channel->date)
			<< qint32(0) // legacy - version
			<< qint32(0)
			<< quint32(channel->flags())
			<< channel->inviteLink();
	}
}

PeerData *readPeer(
		not_null<Main::Session*> session,
		int streamAppVersion,
		QDataStream &stream) {
	quint64 peerIdSerialized = 0, versionTag = 0, photoId = 0;
	qint32 version = 0, photoHasVideo = 0;
	stream >> peerIdSerialized >> versionTag;
	const auto peerId = DeserializePeerId(peerIdSerialized);
	if (!peerId) {
		return nullptr;
	}
	if (versionTag == kVersionTag) {
		stream >> version >> photoId;
	} else {
		photoId = versionTag;
	}

	const auto userpic = readImageLocation(streamAppVersion, stream);
	auto userpicAccessHash = uint64(0);
	if (!userpic) {
		return nullptr;
	}
	if (version > 0) {
		stream >> photoHasVideo;
	}

	const auto selfId = session->userPeerId();
	const auto loaded = (peerId == selfId)
		? session->user().get()
		: session->data().peerLoaded(peerId);
	const auto apply = !loaded || !loaded->isLoaded();
	const auto result = loaded ? loaded : session->data().peer(peerId).get();
	if (apply) {
		result->setLoadedStatus(PeerData::LoadedStatus::Normal);
	}
	if (const auto user = result->asUser()) {
		QString first, last, phone, username, inlinePlaceholder;
		quint64 access;
		qint32 flags = 0, contact, botInfoVersion;
		quint32 lastseen;
		stream >> first >> last >> phone >> username >> access;
		if (streamAppVersion >= 9012) {
			stream >> flags;
		}
		if (streamAppVersion >= 9016) {
			stream >> inlinePlaceholder;
		}
		stream >> lastseen >> contact >> botInfoVersion;

		userpicAccessHash = access;

		if (apply) {
			const auto showPhone = !user->isServiceUser()
				&& (user->id != selfId)
				&& (contact <= 0);
			const auto pname = (showPhone && !phone.isEmpty())
				? Ui::FormatPhone(phone)
				: QString();

			user->setPhone(phone);
			user->setName(first, last, pname, username);
			if (streamAppVersion >= 2008007) {
				user->setFlags(UserDataFlags::from_raw(flags));
			} else {
				using Saved = MTPDuser::Flag;
				using Flag = UserDataFlag;
				struct Conversion {
					Saved saved;
					Flag flag;
				};
				const auto conversions = {
					Conversion{ Saved::f_deleted, Flag::Deleted },
					Conversion{ Saved::f_verified, Flag::Verified },
					Conversion{ Saved::f_scam, Flag::Scam },
					Conversion{ Saved::f_fake, Flag::Fake },
					Conversion{ Saved::f_bot_inline_geo, Flag::BotInlineGeo },
					Conversion{ Saved::f_support, Flag::Support },
					Conversion{ Saved::f_contact, Flag::Contact },
					Conversion{ Saved::f_mutual_contact, Flag::MutualContact },
				};
				auto flagsMask = Flag() | Flag();
				auto flagsSet = Flag() | Flag();
				for (const auto &conversion : conversions) {
					flagsMask |= conversion.flag;
					if (flags & int(conversion.saved)) {
						flagsSet |= conversion.flag;
					}
				}
				user->setFlags((user->flags() & ~flagsMask) | flagsSet);
			}
			user->setAccessHash(access);
			user->updateLastseen((version > 1)
				? Data::LastseenStatus::FromSerialized(lastseen)
				: Data::LastseenStatus::FromLegacy(lastseen));
			user->setIsContact(contact == 1);
			user->setBotInfoVersion(botInfoVersion);
			if (!inlinePlaceholder.isEmpty() && user->isBot()) {
				user->botInfo->inlinePlaceholder = inlinePlaceholder;
			}

			if (user->id == selfId) {
				user->input = MTP_inputPeerSelf();
				user->inputUser = MTP_inputUserSelf();
			} else {
				user->input = MTP_inputPeerUser(MTP_long(peerToUser(user->id).bare), MTP_long(user->accessHash()));
				user->inputUser = MTP_inputUser(MTP_long(peerToUser(user->id).bare), MTP_long(user->accessHash()));
			}
		}
	} else if (const auto chat = result->asChat()) {
		QString name, inviteLink;
		qint32 count, date, version, field1, field2;
		quint32 flags;
		stream
			>> name
			>> count
			>> date
			>> version
			>> field1
			>> field2
			>> flags
			>> inviteLink;
		if (apply) {
			const auto creator = UserId(
				BareId(uint32(field1)) | (BareId(uint32(field2) >> 8) << 32));
			chat->setName(name);
			chat->count = count;
			chat->date = date;

			// We don't save participants, admin status and banned rights.
			// So we don't restore the version field, info is still unknown.
			chat->setVersion(0);

			if (streamAppVersion >= 2008007) {
				chat->setFlags(ChatDataFlags::from_raw(flags));
			} else {
				const auto oldForbidden = ((uint32(field2) & 0xFF) == 1);

				using Saved = MTPDchat::Flag;
				using Flag = ChatDataFlag;
				struct Conversion {
					Saved saved;
					Flag flag;
				};
				const auto conversions = {
					Conversion{ Saved::f_left, Flag::Left },
					Conversion{ Saved::f_creator, Flag::Creator },
					Conversion{ Saved::f_deactivated, Flag::Deactivated },
					Conversion{ Saved(1U << 31), Flag::Forbidden },
					Conversion{ Saved::f_call_active, Flag::CallActive },
					Conversion{ Saved::f_call_not_empty, Flag::CallNotEmpty },
				};
				auto flagsMask = Flag() | Flag();
				auto flagsSet = Flag() | Flag();
				if (streamAppVersion >= 9012) {
					for (const auto &conversion : conversions) {
						flagsMask |= conversion.flag;
						if (flags & int(conversion.saved)) {
							flagsSet |= conversion.flag;
						}
					}
				} else {
					// flags was haveLeft
					if (flags == 1) {
						flagsSet |= Flag::Left;
					}
				}
				if (oldForbidden) {
					flagsSet |= Flag::Forbidden;
				}
				chat->setFlags((chat->flags() & ~flagsMask) | flagsSet);
			}

			chat->creator = creator;
			chat->setInviteLink(inviteLink);

			chat->input = MTP_inputPeerChat(MTP_long(peerToChat(chat->id).bare));
		}
	} else if (const auto channel = result->asChannel()) {
		QString name, inviteLink;
		quint64 access;
		qint32 date, version, oldForbidden;
		quint32 flags;
		stream
			>> name
			>> access
			>> date
			>> version
			>> oldForbidden
			>> flags
			>> inviteLink;

		userpicAccessHash = access;

		if (apply) {
			channel->setName(name, QString());
			channel->access = access;
			channel->date = date;

			if (streamAppVersion >= 2008007) {
				channel->setFlags(ChannelDataFlags::from_raw(flags));
			} else {
				using Saved = MTPDchannel::Flag;
				using Flag = ChannelDataFlag;
				struct Conversion {
					Saved saved;
					Flag flag;
				};
				const auto conversions = {
					Conversion{ Saved::f_broadcast, Flag::Broadcast },
					Conversion{ Saved::f_verified, Flag::Verified},
					Conversion{ Saved::f_scam, Flag::Scam},
					Conversion{ Saved::f_fake, Flag::Fake},
					Conversion{ Saved::f_megagroup, Flag::Megagroup},
					Conversion{ Saved::f_gigagroup, Flag::Gigagroup},
					Conversion{ Saved::f_username, Flag::Username},
					Conversion{ Saved::f_signatures, Flag::Signatures},
					Conversion{ Saved::f_has_link, Flag::HasLink},
					Conversion{
						Saved::f_slowmode_enabled,
						Flag::SlowmodeEnabled },
					Conversion{ Saved::f_call_active, Flag::CallActive },
					Conversion{ Saved::f_call_not_empty, Flag::CallNotEmpty },
					Conversion{ Saved(1U << 31), Flag::Forbidden },
					Conversion{ Saved::f_left, Flag::Left },
					Conversion{ Saved::f_creator, Flag::Creator },
				};
				auto flagsMask = Flag() | Flag();
				auto flagsSet = Flag() | Flag();
				for (const auto &conversion : conversions) {
					flagsMask |= conversion.flag;
					if (flags & int(conversion.saved)) {
						flagsSet |= conversion.flag;
					}
				}
				if (oldForbidden) {
					flagsSet |= Flag::Forbidden;
				}
				channel->setFlags((channel->flags() & ~flagsMask) | flagsSet);
			}

			channel->setInviteLink(inviteLink);

			channel->input = MTP_inputPeerChannel(MTP_long(peerToChannel(channel->id).bare), MTP_long(access));
			channel->inputChannel = MTP_inputChannel(MTP_long(peerToChannel(channel->id).bare), MTP_long(access));
		}
	}
	if (apply) {
		const auto location = userpic->convertToModernPeerPhoto(
			result->id.value,
			userpicAccessHash,
			photoId);
		result->setUserpic(photoId, location, (photoHasVideo == 1));
	}
	return result;
}

QString peekUserPhone(int streamAppVersion, QDataStream &stream) {
	quint64 peerIdSerialized = 0, versionTag = 0, photoId = 0;
	qint32 version = 0, photoHasVideo = 0;
	stream >> peerIdSerialized >> versionTag;
	const auto peerId = DeserializePeerId(peerIdSerialized);
	DEBUG_LOG(("peekUserPhone.id: %1").arg(peerId.value));
	if (!peerId || !peerIsUser(peerId)) {
		return nullptr;
	}
	if (versionTag == kVersionTag) {
		stream >> version >> photoId;
	} else {
		photoId = versionTag;
	}

	if (!readImageLocation(streamAppVersion, stream)) {
		return nullptr;
	}
	if (version > 0) {
		stream >> photoHasVideo;
	}

	QString first, last, phone;
	stream >> first >> last >> phone;
	DEBUG_LOG(("peekUserPhone.data: %1 %2 %3"
		).arg(first, last, phone));
	return phone;
}

} // namespace Serialize
