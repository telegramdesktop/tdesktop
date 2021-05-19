/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/image/image_location.h"

#include "ui/image/image.h"
#include "platform/platform_specific.h"
#include "storage/cache/storage_cache_types.h"
#include "storage/serialize_common.h"
#include "data/data_file_origin.h"
#include "base/overload.h"
#include "main/main_session.h"

#include <QtCore/QBuffer>

namespace {

constexpr auto kDocumentBaseCacheTag = 0x0000000000010000ULL;
constexpr auto kDocumentBaseCacheMask = 0x000000000000FF00ULL;
constexpr auto kPhotoBaseCacheTag = 0x0000000000020000ULL;
constexpr auto kPhotoBaseCacheMask = 0x000000000000FF00ULL;

constexpr auto kNonStorageLocationToken = quint8(0x10);
constexpr auto kLegacyInMessagePeerIdFlag = quint8(0x08);
constexpr auto kModernLocationFlag = quint8(0x20);
constexpr auto kInMessageFieldsFlag = quint8(0x40);

enum class NonStorageLocationType : quint8 {
	Web,
	Geo,
	Url,
	Memory,
};

MTPInputPeer GenerateInputPeer(
		PeerId id,
		uint64 accessHash,
		PeerId inMessagePeerId,
		int32 inMessageId,
		UserId self) {
	const auto bareId = [&] {
		return peerToBareMTPInt(id);
	};
	if (inMessageId && peerIsUser(inMessagePeerId)) {
		return MTP_inputPeerUserFromMessage(
			GenerateInputPeer(id, accessHash, 0, 0, self),
			MTP_int(inMessageId),
			MTP_int(peerToUser(inMessagePeerId).bare)); // #TODO ids
	} else if (inMessageId && peerIsChannel(inMessagePeerId)) {
		return MTP_inputPeerChannelFromMessage(
			GenerateInputPeer(id, accessHash, 0, 0, self),
			MTP_int(inMessageId),
			MTP_int(peerToChannel(inMessagePeerId).bare)); // #TODO ids
	} else if (!id) {
		return MTP_inputPeerEmpty();
	} else if (id == peerFromUser(self)) {
		return MTP_inputPeerSelf();
	} else if (peerIsUser(id)) {
		return MTP_inputPeerUser(bareId(), MTP_long(accessHash));
	} else if (peerIsChat(id)) {
		return MTP_inputPeerChat(bareId());
	} else if (peerIsChannel(id)) {
		return MTP_inputPeerChannel(bareId(), MTP_long(accessHash));
	} else {
		return MTP_inputPeerEmpty();
	}
}

} // namespace

WebFileLocation WebFileLocation::Null;

StorageFileLocation::StorageFileLocation(
	int32 dcId,
	UserId self,
	const MTPInputFileLocation &tl)
: _dcId(dcId) {
	tl.match([&](const MTPDinputFileLocation &data) {
		_type = Type::Legacy;
		_volumeId = data.vvolume_id().v;
		_localId = data.vlocal_id().v;
		_accessHash = data.vsecret().v;
		_fileReference = data.vfile_reference().v;
	}, [&](const MTPDinputEncryptedFileLocation &data) {
		_type = Type::Encrypted;
		_id = data.vid().v;
		_accessHash = data.vaccess_hash().v;
	}, [&](const MTPDinputDocumentFileLocation &data) {
		_type = Type::Document;
		_id = data.vid().v;
		_accessHash = data.vaccess_hash().v;
		_fileReference = data.vfile_reference().v;
		_sizeLetter = data.vthumb_size().v.isEmpty()
			? uint8(0)
			: uint8(data.vthumb_size().v[0]);
	}, [&](const MTPDinputSecureFileLocation &data) {
		_type = Type::Secure;
		_id = data.vid().v;
		_accessHash = data.vaccess_hash().v;
	}, [&](const MTPDinputTakeoutFileLocation &data) {
		_type = Type::Takeout;
	}, [&](const MTPDinputPhotoFileLocation &data) {
		_type = Type::Photo;
		_id = data.vid().v;
		_accessHash = data.vaccess_hash().v;
		_fileReference = data.vfile_reference().v;
		_sizeLetter = data.vthumb_size().v.isEmpty()
			? char(0)
			: data.vthumb_size().v[0];
	}, [&](const MTPDinputPhotoLegacyFileLocation &data) {
		_type = Type::Legacy;
		_volumeId = data.vvolume_id().v;
		_localId = data.vlocal_id().v;
		_accessHash = data.vsecret().v;
		_fileReference = data.vfile_reference().v;
	}, [&](const MTPDinputPeerPhotoFileLocation &data) {
		_type = Type::PeerPhoto;
		const auto fillPeer = base::overload([&](
				const MTPDinputPeerEmpty &data) {
			_id = 0;
		}, [&](const MTPDinputPeerSelf &data) {
			_id = peerFromUser(self).value;
		}, [&](const MTPDinputPeerChat &data) {
			_id = peerFromChat(data.vchat_id()).value;
		}, [&](const MTPDinputPeerUser &data) {
			_id = peerFromUser(data.vuser_id()).value;
			_accessHash = data.vaccess_hash().v;
		}, [&](const MTPDinputPeerChannel &data) {
			_id = peerFromChannel(data.vchannel_id()).value;
			_accessHash = data.vaccess_hash().v;
		});
		data.vpeer().match(fillPeer, [&](
				const MTPDinputPeerUserFromMessage &data) {
			data.vpeer().match(fillPeer, [&](auto &&) {
				// Bad data provided.
				_id = _accessHash = 0;
			});
			_inMessagePeerId = peerFromUser(data.vuser_id());
			_inMessageId = data.vmsg_id().v;
		}, [&](const MTPDinputPeerChannelFromMessage &data) {
			data.vpeer().match(fillPeer, [&](auto &&) {
				// Bad data provided.
				_id = _accessHash = 0;
			});
			_inMessagePeerId = peerFromChannel(data.vchannel_id());
			_inMessageId = data.vmsg_id().v;
		});
		_volumeId = data.vphoto_id().v;
		_sizeLetter = data.is_big() ? 'c' : 'a';

		// _localId place is used in serialization.
		Ensures(_localId == 0);
	}, [&](const MTPDinputStickerSetThumb &data) {
		_type = Type::StickerSetThumb;
		data.vstickerset().match([&](const MTPDinputStickerSetEmpty &data) {
			_id = 0;
		}, [&](const MTPDinputStickerSetID &data) {
			_id = data.vid().v;
			_accessHash = data.vaccess_hash().v;
		}, [&](const auto &data) {
			Unexpected("InputStickerSet type in StorageFileLocation.");
		});
		_volumeId = 0;
		_localId = data.vthumb_version().v;
	}, [&](const MTPDinputGroupCallStream &data) {
		_type = Type::GroupCallStream;
		data.vcall().match([&](const MTPDinputGroupCall &data) {
			_id = data.vid().v;
			_accessHash = data.vaccess_hash().v;
		});
		_volumeId = data.vtime_ms().v;
		_localId = data.vscale().v;
	});
}

StorageFileLocation StorageFileLocation::convertToModernPeerPhoto(
		uint64 id,
		uint64 accessHash,
		uint64 photoId) const {
	if (_type != Type::Legacy && _type != Type::PeerPhoto) {
		return *this;
	} else if (!photoId) {
		return StorageFileLocation();
	}

	auto result = *this;
	result._type = Type::PeerPhoto;
	result._id = id;
	result._accessHash = accessHash;
	result._sizeLetter = uint8('a');
	result._volumeId = photoId;
	result._localId = 0;
	result._inMessagePeerId = 0;
	result._inMessageId = 0;
	return result;
}

int32 StorageFileLocation::dcId() const {
	return _dcId;
}

uint64 StorageFileLocation::objectId() const {
	return _id;
}

MTPInputFileLocation StorageFileLocation::tl(UserId self) const {
	switch (_type) {
	case Type::Legacy:
		return MTP_inputFileLocation(
			MTP_long(_volumeId),
			MTP_int(_localId),
			MTP_long(_accessHash),
			MTP_bytes(_fileReference));

	case Type::Encrypted:
		return MTP_inputSecureFileLocation(
			MTP_long(_id),
			MTP_long(_accessHash));

	case Type::Document:
		return MTP_inputDocumentFileLocation(
			MTP_long(_id),
			MTP_long(_accessHash),
			MTP_bytes(_fileReference),
			MTP_string(_sizeLetter
				? std::string(1, char(_sizeLetter))
				: std::string()));

	case Type::Secure:
		return MTP_inputSecureFileLocation(
			MTP_long(_id),
			MTP_long(_accessHash));

	case Type::Takeout:
		return MTP_inputTakeoutFileLocation();

	case Type::Photo:
		return MTP_inputPhotoFileLocation(
			MTP_long(_id),
			MTP_long(_accessHash),
			MTP_bytes(_fileReference),
			MTP_string(std::string(1, char(_sizeLetter))));

	case Type::PeerPhoto:
		return MTP_inputPeerPhotoFileLocation(
			MTP_flags((_sizeLetter == 'c')
				? MTPDinputPeerPhotoFileLocation::Flag::f_big
				: MTPDinputPeerPhotoFileLocation::Flag(0)),
			GenerateInputPeer(
				PeerId(_id),
				_accessHash,
				_inMessagePeerId,
				_inMessageId,
				self),
			MTP_long(_volumeId));

	case Type::StickerSetThumb:
		return MTP_inputStickerSetThumb(
			MTP_inputStickerSetID(MTP_long(_id), MTP_long(_accessHash)),
			MTP_int(_localId));

	case Type::GroupCallStream:
		return MTP_inputGroupCallStream(
			MTP_inputGroupCall(MTP_long(_id), MTP_long(_accessHash)),
			MTP_long(_volumeId),
			MTP_int(_localId));

	}
	Unexpected("Type in StorageFileLocation::tl.");
}

QByteArray StorageFileLocation::serialize() const {
	auto result = QByteArray();
	if (valid()) {
		result.reserve(serializeSize());
		auto buffer = QBuffer(&result);
		buffer.open(QIODevice::WriteOnly);
		auto stream = QDataStream(&buffer);
		stream.setVersion(QDataStream::Qt_5_1);

		Assert(!(quint8(_type) & kModernLocationFlag)
			&& !(quint8(_type) & kInMessageFieldsFlag));
		auto typeWithFlags = quint8(_type);
		typeWithFlags |= kModernLocationFlag;
		auto field1 = qint32(_localId);
		auto field2 = qint32(0);
		if (_inMessagePeerId != 0) {
			Assert(field1 == 0);
			typeWithFlags |= kInMessageFieldsFlag;
			field1 = qint32(uint32(_inMessagePeerId.value >> 32));
			field2 = qint32(uint32(_inMessagePeerId.value & 0xFFFFFFFFULL));
		}
		Assert(typeWithFlags != kNonStorageLocationToken);
		stream
			<< quint16(_dcId)
			<< typeWithFlags
			<< quint8(_sizeLetter)
			<< field1
			<< quint64(_id)
			<< quint64(_accessHash)
			<< quint64(_volumeId)
			<< field2
			<< qint32(_inMessageId)
			<< _fileReference;
	}
	return result;
}

int StorageFileLocation::serializeSize() const {
	return valid()
		? int(sizeof(uint64) * 5 + Serialize::bytearraySize(_fileReference))
		: 0;
}

std::optional<StorageFileLocation> StorageFileLocation::FromSerialized(
		const QByteArray &serialized) {
	if (serialized.isEmpty()) {
		return StorageFileLocation();
	}

	quint16 dcId = 0;
	quint8 typeWithFlags = 0;
	quint8 sizeLetter = 0;
	qint32 field1 = 0;
	quint64 id = 0;
	quint64 accessHash = 0;
	quint64 volumeId = 0;
	qint32 field2 = 0;
	qint32 inMessageId = 0;
	QByteArray fileReference;
	auto stream = QDataStream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);
	stream
		>> dcId
		>> typeWithFlags;
	if (typeWithFlags == kNonStorageLocationToken) {
		return std::nullopt;
	}
	stream
		>> sizeLetter
		>> field1
		>> id
		>> accessHash
		>> volumeId;
	const auto modern = ((typeWithFlags & kModernLocationFlag) != 0);
	const auto inMessageFields
		= ((typeWithFlags & kInMessageFieldsFlag) != 0);
	if (modern) {
		stream >> field2 >> inMessageId;
		typeWithFlags &= ~kModernLocationFlag;
		if (inMessageFields) {
			typeWithFlags &= ~kInMessageFieldsFlag;
		}
	} else if (typeWithFlags & kLegacyInMessagePeerIdFlag) {
		typeWithFlags &= ~kLegacyInMessagePeerIdFlag;
		stream >> field2 >> inMessageId;
	}
	stream >> fileReference;

	auto result = StorageFileLocation();
	result._dcId = dcId;
	result._type = Type(typeWithFlags);
	result._sizeLetter = sizeLetter;
	result._accessHash = accessHash;
	result._volumeId = volumeId;
	result._inMessageId = inMessageId;
	result._fileReference = fileReference;

	if (modern) {
		result._id = id;
		if (inMessageFields) {
			result._localId = 0;
			result._inMessagePeerId = PeerId(
				(uint64(uint32(field1)) << 32) | uint64(uint32(field2)));
		} else {
			result._localId = field1;
			result._inMessagePeerId = 0;
		}
	} else {
		result._id = (result._type == Type::PeerPhoto)
			? DeserializePeerId(id).value
			: id;
		result._localId = (result._type == Type::PeerPhoto)
			? 0
			: field1;
		result._inMessagePeerId = (field2 && result._type == Type::PeerPhoto)
			? ((field2 > 0)
				? peerFromUser(UserId(field2))
				: peerFromChannel(ChannelId(-field2)))
			: PeerId();
	}
	if (result._type == Type::StickerSetThumb && result._volumeId != 0) {
		// Legacy field values that cannot be converted to modern.
		// No information about thumb_version, which is required.
		return std::nullopt;
	}

	return (stream.status() == QDataStream::Ok && result.valid())
		? std::make_optional(result)
		: std::nullopt;
}

StorageFileLocation::Type StorageFileLocation::type() const {
	return _type;
}

bool StorageFileLocation::valid() const {
	switch (_type) {
	case Type::Legacy:
		return (_dcId != 0) && (_volumeId != 0) && (_localId != 0);

	case Type::Encrypted:
	case Type::Secure:
	case Type::Document:
		return (_dcId != 0) && (_id != 0);

	case Type::Photo:
		return (_dcId != 0) && (_id != 0) && (_sizeLetter != 0);

	case Type::Takeout:
		return true;

	case Type::PeerPhoto:
	case Type::StickerSetThumb:
		return (_dcId != 0) && (_id != 0);

	case Type::GroupCallStream:
		return (_dcId != 0) && (_id != 0) && (_volumeId != 0);
	}
	return false;
}

bool StorageFileLocation::isLegacy() const {
	return (_type == Type::Legacy);
}

bool StorageFileLocation::isDocumentThumbnail() const {
	return (_type == Type::Document) && (_sizeLetter != 0);
}

Storage::Cache::Key StorageFileLocation::cacheKey() const {
	using Key = Storage::Cache::Key;

	// Skip '1' for legacy document cache keys.
	// Skip '2' because it is used for good (fullsize) document thumbnails.
	const auto shifted = ((uint64(_type) + 3) << 8);
	const auto sliced = uint64(_dcId) & 0xFFULL;
	switch (_type) {
	case Type::Legacy:
	case Type::PeerPhoto:
	case Type::StickerSetThumb:
		return Key{
			shifted | sliced | (uint64(uint32(_localId)) << 16),
			_volumeId };

	case Type::Encrypted:
	case Type::Secure:
		return Key{ shifted | sliced, _id };

	case Type::Document:
		// Keep old cache keys for documents.
		if (_sizeLetter == 0) {
			return Data::DocumentCacheKey(_dcId, _id);
			//return Key{ 0x100ULL | sliced, _id };
		}
		[[fallthrough]];
	case Type::Photo:
		return Key{ shifted | sliced | (uint64(_sizeLetter) << 16), _id };

	case Type::Takeout:
		return Key{ shifted, 0 };

	case Type::GroupCallStream:
		return Key{
			(shifted
				| sliced
				| (uint32(_localId) << 16)
				| (_volumeId << 20)),
			_id };
	}
	return Key();
}

Storage::Cache::Key StorageFileLocation::bigFileBaseCacheKey() const {
	switch (_type) {
	case Type::Document: {
		const auto high = kDocumentBaseCacheTag
			| ((uint64(_dcId) << 16) & kDocumentBaseCacheMask)
			| (_id >> 48);
		const auto low = (_id << 16);

		Ensures((low & 0xFFULL) == 0);
		return Storage::Cache::Key{ high, low };
	}

	case Type::StickerSetThumb: {
		const auto high = (uint64(uint32(_localId)) << 24)
			| ((uint64(_type) + 1) << 16)
			| ((uint64(_dcId) & 0xFFULL) << 8)
			| (_volumeId >> 56);
		const auto low = (_volumeId << 8);

		Ensures((low & 0xFFULL) == 0);
		return Storage::Cache::Key{ high, low };
	}

	case Type::Photo: {
		const auto high = kPhotoBaseCacheTag
			| ((uint64(_dcId) << 16) & kPhotoBaseCacheMask)
			| (_id >> 48);
		const auto low = (_id << 16);

		Ensures((low & 0xFFULL) == 0);
		return Storage::Cache::Key{ high, low };
	}

	case Type::Legacy:
	case Type::PeerPhoto:
	case Type::Encrypted:
	case Type::Secure:
	case Type::Takeout:
	case Type::GroupCallStream:
		Unexpected("Not implemented file location type.");

	};
	Unexpected("Invalid file location type.");
}

QByteArray StorageFileLocation::fileReference() const {
	return _fileReference;
}

bool StorageFileLocation::refreshFileReference(
		const Data::UpdatedFileReferences &updates) {
	const auto i = (_type == Type::Document)
		? updates.data.find(Data::DocumentFileLocationId{ _id })
		: (_type == Type::Photo)
		? updates.data.find(Data::PhotoFileLocationId{ _id })
		: end(updates.data);
	return (i != end(updates.data))
		? refreshFileReference(i->second)
		: false;
}

bool StorageFileLocation::refreshFileReference(const QByteArray &data) {
	if (data.isEmpty() || _fileReference == data) {
		return false;
	}
	_fileReference = data;
	return true;
}

const StorageFileLocation &StorageFileLocation::Invalid() {
	static auto result = StorageFileLocation();
	return result;
}

bool operator==(const StorageFileLocation &a, const StorageFileLocation &b) {
	const auto valid = a.valid();
	if (valid != b.valid()) {
		return false;
	} else if (!valid) {
		return true;
	}
	const auto type = a._type;
	if (type != b._type) {
		return false;
	}

	using Type = StorageFileLocation::Type;
	switch (type) {
	case Type::Legacy:
		return (a._dcId == b._dcId)
			&& (a._volumeId == b._volumeId)
			&& (a._localId == b._localId);

	case Type::Encrypted:
	case Type::Secure:
		return (a._dcId == b._dcId) && (a._id == b._id);

	case Type::Photo:
	case Type::Document:
		return (a._dcId == b._dcId)
			&& (a._id == b._id)
			&& (a._sizeLetter == b._sizeLetter);

	case Type::Takeout:
		return true;

	case Type::PeerPhoto:
		return (a._dcId == b._dcId)
			&& (a._volumeId == b._volumeId)
			&& (a._localId == b._localId)
			&& (a._id == b._id)
			&& (a._sizeLetter == b._sizeLetter);

	case Type::StickerSetThumb:
		return (a._dcId == b._dcId)
			&& (a._volumeId == b._volumeId)
			&& (a._localId == b._localId)
			&& (a._id == b._id);

	case Type::GroupCallStream:
		return (a._dcId == b._dcId)
			&& (a._id == b._id)
			&& (a._localId == b._localId);
	};
	Unexpected("Type in StorageFileLocation::operator==.");
}

bool operator<(const StorageFileLocation &a, const StorageFileLocation &b) {
	const auto valid = a.valid();
	if (valid != b.valid()) {
		return !valid;
	} else if (!valid) {
		return false;
	}
	const auto type = a._type;
	if (type != b._type) {
		return (type < b._type);
	}

	using Type = StorageFileLocation::Type;
	switch (type) {
	case Type::Legacy:
		return std::tie(a._localId, a._volumeId, a._dcId)
			< std::tie(b._localId, b._volumeId, b._dcId);

	case Type::Encrypted:
	case Type::Secure:
		return std::tie(a._id, a._dcId) < std::tie(b._id, b._dcId);

	case Type::Photo:
	case Type::Document:
		return std::tie(a._id, a._dcId, a._sizeLetter)
			< std::tie(b._id, b._dcId, b._sizeLetter);

	case Type::Takeout:
		return false;

	case Type::PeerPhoto:
		return std::tie(
			a._id,
			a._sizeLetter,
			a._localId,
			a._volumeId,
			a._dcId)
			< std::tie(
				b._id,
				b._sizeLetter,
				b._localId,
				b._volumeId,
				b._dcId);

	case Type::StickerSetThumb:
		return std::tie(a._id, a._localId, a._volumeId, a._dcId)
			< std::tie(b._id, b._localId, b._volumeId, b._dcId);

	case Type::GroupCallStream:
		return std::tie(a._id, a._localId, a._dcId)
			< std::tie(b._id, b._localId, b._dcId);
	};
	Unexpected("Type in StorageFileLocation::operator==.");
}

InMemoryKey inMemoryKey(const StorageFileLocation &location) {
	const auto key = location.cacheKey();
	return { key.high, key.low };
}

InMemoryKey inMemoryKey(const WebFileLocation &location) {
	auto result = InMemoryKey();
	const auto &url = location.url();
	const auto sha = hashSha1(url.data(), url.size());
	bytes::copy(
		bytes::object_as_span(&result),
		bytes::make_span(sha).subspan(0, sizeof(result)));
	return result;
}

InMemoryKey inMemoryKey(const GeoPointLocation &location) {
	return InMemoryKey(
		(uint64(std::round(std::abs(location.lat + 360.) * 1000000)) << 32)
		| uint64(std::round(std::abs(location.lon + 360.) * 1000000)),
		(uint64(location.width) << 32) | uint64(location.height));
}

InMemoryKey inMemoryKey(const PlainUrlLocation &location) {
	auto result = InMemoryKey();
	const auto &url = location.url;
	const auto sha = hashSha1(url.data(), url.size() * sizeof(QChar));
	bytes::copy(
		bytes::object_as_span(&result),
		bytes::make_span(sha).subspan(0, sizeof(result)));
	return result;
}

InMemoryKey inMemoryKey(const InMemoryLocation &location) {
	auto result = InMemoryKey();
	const auto &data = location.bytes;
	const auto sha = hashSha1(data.data(), data.size());
	bytes::copy(
		bytes::object_as_span(&result),
		bytes::make_span(sha).subspan(0, sizeof(result)));
	return result;
}

InMemoryKey inMemoryKey(const DownloadLocation &location) {
	return v::match(location.data, [](const auto &data) {
		return inMemoryKey(data);
	});
}

StorageImageLocation::StorageImageLocation(
	const StorageFileLocation &file,
	int width,
	int height)
: _file(file)
, _width(width)
, _height(height) {
}

QByteArray StorageImageLocation::serialize() const {
	auto result = _file.serialize();
	if (!result.isEmpty() || (_width > 0) || (_height > 0)) {
		result.reserve(result.size() + 2 * sizeof(qint32));
		auto buffer = QBuffer(&result);
		buffer.open(QIODevice::Append);
		auto stream = QDataStream(&buffer);
		stream.setVersion(QDataStream::Qt_5_1);
		stream << qint32(_width) << qint32(_height);
	}
	return result;
}

int StorageImageLocation::serializeSize() const {
	const auto partial = _file.serializeSize();
	return (partial > 0 || _width > 0 || _height > 0)
		? (partial + 2 * sizeof(qint32))
		: 0;
}

std::optional<StorageImageLocation> StorageImageLocation::FromSerialized(
		const QByteArray &serialized) {
	if (const auto file = StorageFileLocation::FromSerialized(serialized)) {
		const auto my = 2 * sizeof(qint32);
		const auto full = serialized.size();
		if (!full) {
			return StorageImageLocation(*file, 0, 0);
		} else if (full >= my) {
			qint32 width = 0;
			qint32 height = 0;

			const auto dimensions = QByteArray::fromRawData(
				serialized.data() + full - my, my);
			auto stream = QDataStream(dimensions);
			stream.setVersion(QDataStream::Qt_5_1);
			stream >> width >> height;

			return (stream.status() == QDataStream::Ok)
				? StorageImageLocation(*file, width, height)
				: std::optional<StorageImageLocation>();
		}
	}
	return std::nullopt;
}

QByteArray DownloadLocation::serialize() const {
	if (!valid() || v::is<StorageFileLocation>(data)) {
		return v::get<StorageFileLocation>(data).serialize();
	}
	auto result = QByteArray();
	auto buffer = QBuffer(&result);
	buffer.open(QIODevice::WriteOnly);
	auto stream = QDataStream(&buffer);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << quint16(0) << kNonStorageLocationToken;

	v::match(data, [&](const StorageFileLocation &data) {
		Unexpected("Variant in DownloadLocation::serialize.");
	}, [&](const WebFileLocation &data) {
		stream
			<< quint8(NonStorageLocationType::Web)
			<< data.url()
			<< quint64(data.accessHash());
	}, [&](const GeoPointLocation &data) {
		stream
			<< quint8(NonStorageLocationType::Geo)
			<< qreal(data.lat)
			<< qreal(data.lon)
			<< quint64(data.access)
			<< qint32(data.width)
			<< qint32(data.height)
			<< qint32(data.zoom)
			<< qint32(data.scale);
	}, [&](const PlainUrlLocation &data) {
		stream << quint8(NonStorageLocationType::Url) << data.url.toUtf8();
	}, [&](const InMemoryLocation &data) {
		stream << quint8(NonStorageLocationType::Memory) << data.bytes;
	});
	buffer.close();
	return result;
}

int DownloadLocation::serializeSize() const {
	if (!valid() || v::is<StorageFileLocation>(data)) {
		return v::get<StorageFileLocation>(data).serializeSize();
	}
	auto result = sizeof(quint16) + sizeof(quint8) + sizeof(quint8);
	v::match(data, [&](const StorageFileLocation &data) {
		Unexpected("Variant in DownloadLocation::serializeSize.");
	}, [&](const WebFileLocation &data) {
		result += Serialize::bytearraySize(data.url()) + sizeof(quint64);
	}, [&](const GeoPointLocation &data) {
		result += 2 * sizeof(qreal) + sizeof(quint64) + 4 * sizeof(qint32);
	}, [&](const PlainUrlLocation &data) {
		result += Serialize::bytearraySize(data.url.toUtf8());
	}, [&](const InMemoryLocation &data) {
		result += Serialize::bytearraySize(data.bytes);
	});
	return result;
}

std::optional<DownloadLocation> DownloadLocation::FromSerialized(
		const QByteArray &serialized) {
	quint16 dcId = 0;
	quint8 token = 0;
	auto stream = QDataStream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);
	stream >> dcId >> token;
	if (dcId != 0 || token != kNonStorageLocationToken) {
		const auto storage = StorageFileLocation::FromSerialized(serialized);
		return storage
			? std::make_optional(DownloadLocation{ *storage })
			: std::nullopt;
	}
	quint8 type = 0;
	stream >> type;
	switch (NonStorageLocationType(type)) {
	case NonStorageLocationType::Web: {
		QByteArray url;
		quint64 accessHash = 0;
		stream >> url >> accessHash;
		return (stream.status() == QDataStream::Ok)
			? std::make_optional(
				DownloadLocation{ WebFileLocation(url, accessHash) })
			: std::nullopt;
	} break;

	case NonStorageLocationType::Geo: {
		qreal lat = 0.;
		qreal lon = 0.;
		quint64 access = 0;
		qint32 width = 0;
		qint32 height = 0;
		qint32 zoom = 0;
		qint32 scale = 0;
		stream >> lat >> lon >> access >> width >> height >> zoom >> scale;
		return (stream.status() == QDataStream::Ok)
			? std::make_optional(
				DownloadLocation{ GeoPointLocation{
					.lat = lat,
					.lon = lon,
					.access = access,
					.width = width,
					.height = height,
					.zoom = zoom,
					.scale = scale } })
			: std::nullopt;
	} break;

	case NonStorageLocationType::Url: {
		QByteArray utf;
		stream >> utf;
		const auto url = base::FromUtf8Safe(utf);
		return (stream.status() == QDataStream::Ok)
			? std::make_optional(DownloadLocation{ PlainUrlLocation{ url } })
			: std::nullopt;
	} break;

	case NonStorageLocationType::Memory: {
		QByteArray bytes;
		stream >> bytes;
		return (stream.status() == QDataStream::Ok)
			? std::make_optional(
				DownloadLocation{ InMemoryLocation{ bytes } })
			: std::nullopt;
	} break;
	}
	return std::nullopt;
}

DownloadLocation DownloadLocation::convertToModernPeerPhoto(
		uint64 id,
		uint64 accessHash,
		uint64 photoId) const {
	if (!v::is<StorageFileLocation>(data)) {
		return *this;
	}
	auto &file = v::get<StorageFileLocation>(data);
	return DownloadLocation{
		file.convertToModernPeerPhoto(id, accessHash, photoId)
	};
}

Storage::Cache::Key DownloadLocation::cacheKey() const {
	return v::match(data, [](const GeoPointLocation &data) {
		return Data::GeoPointCacheKey(data);
	}, [](const StorageFileLocation &data) {
		return data.valid()
			? data.cacheKey()
			: Storage::Cache::Key();
	}, [](const WebFileLocation &data) {
		return data.isNull()
			? Storage::Cache::Key()
			: Data::WebDocumentCacheKey(data);
	}, [](const PlainUrlLocation &data) {
		return data.url.isEmpty()
			? Storage::Cache::Key()
			: Data::UrlCacheKey(data.url);
	}, [](const InMemoryLocation &data) {
		return Storage::Cache::Key();
	});
}

Storage::Cache::Key DownloadLocation::bigFileBaseCacheKey() const {
	return v::is<StorageFileLocation>(data)
		? v::get<StorageFileLocation>(data).bigFileBaseCacheKey()
		: Storage::Cache::Key();
}

bool DownloadLocation::valid() const {
	return v::match(data, [](const GeoPointLocation &data) {
		return true;
	}, [](const StorageFileLocation &data) {
		return data.valid();
	}, [](const WebFileLocation &data) {
		return !data.isNull();
	}, [](const PlainUrlLocation &data) {
		return !data.url.isEmpty();
	}, [](const InMemoryLocation &data) {
		return !data.bytes.isEmpty();
	});
}

bool DownloadLocation::isLegacy() const {
	return v::is<StorageFileLocation>(data)
		? v::get<StorageFileLocation>(data).isLegacy()
		: false;
}

QByteArray DownloadLocation::fileReference() const {
	if (!v::is<StorageFileLocation>(data)) {
		return QByteArray();
	}
	return v::get<StorageFileLocation>(data).fileReference();
}

bool DownloadLocation::refreshFileReference(const QByteArray &data) {
	if (!v::is<StorageFileLocation>(this->data)) {
		return false;
	}
	auto &file = v::get<StorageFileLocation>(this->data);
	return file.refreshFileReference(data);
}

bool DownloadLocation::refreshFileReference(
	const Data::UpdatedFileReferences &updates) {
	if (!v::is<StorageFileLocation>(data)) {
		return false;
	}
	auto &file = v::get<StorageFileLocation>(data);
	return file.refreshFileReference(updates);
}

ImageLocation::ImageLocation(
	const DownloadLocation &file,
	int width,
	int height)
: _file(file)
, _width(width)
, _height(height) {
}

QByteArray ImageLocation::serialize() const {
	auto result = _file.serialize();
	if (!result.isEmpty() || (_width > 0) || (_height > 0)) {
		result.reserve(result.size() + 2 * sizeof(qint32));
		auto buffer = QBuffer(&result);
		buffer.open(QIODevice::Append);
		auto stream = QDataStream(&buffer);
		stream.setVersion(QDataStream::Qt_5_1);
		stream << qint32(_width) << qint32(_height);
	}
	return result;
}

int ImageLocation::serializeSize() const {
	const auto partial = _file.serializeSize();
	return (partial > 0 || _width > 0 || _height > 0)
		? (partial + 2 * sizeof(qint32))
		: 0;
}

std::optional<ImageLocation> ImageLocation::FromSerialized(
		const QByteArray &serialized) {
	if (const auto file = DownloadLocation::FromSerialized(serialized)) {
		const auto my = 2 * sizeof(qint32);
		const auto full = serialized.size();
		if (!full) {
			return ImageLocation(*file, 0, 0);
		} else if (full >= my) {
			qint32 width = 0;
			qint32 height = 0;

			const auto dimensions = QByteArray::fromRawData(
				serialized.data() + full - my, my);
			auto stream = QDataStream(dimensions);
			stream.setVersion(QDataStream::Qt_5_1);
			stream >> width >> height;

			return (stream.status() == QDataStream::Ok)
				? ImageLocation(*file, width, height)
				: std::optional<ImageLocation>();
		}
	}
	return std::nullopt;
}
