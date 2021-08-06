/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/value_ordering.h"
#include "ui/text/text.h" // For QFIXED_MAX
#include "data/data_peer_id.h"

class HistoryItem;
using HistoryItemsList = std::vector<not_null<HistoryItem*>>;

class StorageImageLocation;
class WebFileLocation;
struct GeoPointLocation;

namespace Storage {
namespace Cache {
struct Key;
} // namespace Cache
} // namespace Storage

namespace Ui {
class InputField;
} // namespace Ui

namespace Main {
class Session;
} // namespace Main

namespace Images {
enum class Option;
using Options = base::flags<Option>;
} // namespace Images

namespace Data {

struct UploadState {
	UploadState(int size) : size(size) {
	}
	int offset = 0;
	int size = 0;
	bool waitingForAlbum = false;
};

Storage::Cache::Key DocumentCacheKey(int32 dcId, uint64 id);
Storage::Cache::Key DocumentThumbCacheKey(int32 dcId, uint64 id);
Storage::Cache::Key WebDocumentCacheKey(const WebFileLocation &location);
Storage::Cache::Key UrlCacheKey(const QString &location);
Storage::Cache::Key GeoPointCacheKey(const GeoPointLocation &location);

constexpr auto kImageCacheTag = uint8(0x01);
constexpr auto kStickerCacheTag = uint8(0x02);
constexpr auto kVoiceMessageCacheTag = uint8(0x03);
constexpr auto kVideoMessageCacheTag = uint8(0x04);
constexpr auto kAnimationCacheTag = uint8(0x05);

struct FileOrigin;

} // namespace Data

struct MessageGroupId {
	PeerId peer = 0;
	uint64 value = 0;

	MessageGroupId() = default;
	static MessageGroupId FromRaw(PeerId peer, uint64 value) {
		auto result = MessageGroupId();
		result.peer = peer;
		result.value = value;
		return result;
	}

	bool empty() const {
		return !value;
	}
	explicit operator bool() const {
		return !empty();
	}
	uint64 raw() const {
		return value;
	}

	friend inline std::pair<uint64, uint64> value_ordering_helper(MessageGroupId value) {
		return std::make_pair(value.value, value.peer.value);
	}

};

class PeerData;
class UserData;
class ChatData;
class ChannelData;
struct BotCommand;
struct BotInfo;

namespace Data {
class Folder;
} // namespace Data

using FolderId = int32;
using FilterId = int32;
using MsgId = int32;
constexpr auto StartClientMsgId = MsgId(-0x7FFFFFFF);
constexpr auto EndClientMsgId = MsgId(-0x40000000);
constexpr auto ShowAtTheEndMsgId = MsgId(-0x40000000);
constexpr auto SwitchAtTopMsgId = MsgId(-0x3FFFFFFF);
constexpr auto ShowAtProfileMsgId = MsgId(-0x3FFFFFFE);
constexpr auto ShowAndStartBotMsgId = MsgId(-0x3FFFFFFD);
constexpr auto ShowAtGameShareMsgId = MsgId(-0x3FFFFFFC);
constexpr auto ShowForChooseMessagesMsgId = MsgId(-0x3FFFFFFB);
constexpr auto ServerMaxMsgId = MsgId(0x3FFFFFFF);
constexpr auto ShowAtUnreadMsgId = MsgId(0);
constexpr inline bool IsClientMsgId(MsgId id) {
	return (id >= StartClientMsgId && id < EndClientMsgId);
}
constexpr inline bool IsServerMsgId(MsgId id) {
	return (id > 0 && id < ServerMaxMsgId);
}

struct MsgRange {
	MsgRange() = default;
	MsgRange(MsgId from, MsgId till) : from(from), till(till) {
	}

	MsgId from = 0;
	MsgId till = 0;
};
inline bool operator==(const MsgRange &a, const MsgRange &b) {
	return (a.from == b.from) && (a.till == b.till);
}
inline bool operator!=(const MsgRange &a, const MsgRange &b) {
	return !(a == b);
}

struct FullMsgId {
	constexpr FullMsgId() = default;
	constexpr FullMsgId(ChannelId channel, MsgId msg)
	: channel(channel), msg(msg) {
	}

	explicit operator bool() const {
		return msg != 0;
	}


	inline constexpr bool operator<(const FullMsgId &other) const {
		if (channel < other.channel) {
			return true;
		} else if (channel > other.channel) {
			return false;
		}
		return msg < other.msg;
	}
	inline constexpr bool operator>(const FullMsgId &other) const {
		return other < *this;
	}
	inline constexpr bool operator<=(const FullMsgId &other) const {
		return !(other < *this);
	}
	inline constexpr bool operator>=(const FullMsgId &other) const {
		return !(*this < other);
	}
	inline constexpr bool operator==(const FullMsgId &other) const {
		return (channel == other.channel) && (msg == other.msg);
	}
	inline constexpr bool operator!=(const FullMsgId &other) const {
		return !(*this == other);
	}

	ChannelId channel = NoChannel;
	MsgId msg = 0;

};

Q_DECLARE_METATYPE(FullMsgId);

using MessageIdsList = std::vector<FullMsgId>;

PeerId PeerFromMessage(const MTPmessage &message);
MTPDmessage::Flags FlagsFromMessage(const MTPmessage &message);
MsgId IdFromMessage(const MTPmessage &message);
TimeId DateFromMessage(const MTPmessage &message);

class DocumentData;
class PhotoData;
struct WebPageData;
struct GameData;
struct PollData;

class AudioMsgId;

using PhotoId = uint64;
using VideoId = uint64;
using AudioId = uint64;
using DocumentId = uint64;
using WebPageId = uint64;
using GameId = uint64;
using PollId = uint64;
using WallPaperId = uint64;
constexpr auto CancelledWebPageId = WebPageId(0xFFFFFFFFFFFFFFFFULL);

struct PreparedPhotoThumb {
	QImage image;
	QByteArray bytes;
};
using PreparedPhotoThumbs = base::flat_map<char, PreparedPhotoThumb>;

// [0] == -1 -- counting, [0] == -2 -- could not count
using VoiceWaveform = QVector<signed char>;

enum LocationType {
	UnknownFileLocation = 0,
	// 1, 2, etc are used as "version" value in mediaKey() method.

	DocumentFileLocation = 0x4e45abe9, // mtpc_inputDocumentFileLocation
	AudioFileLocation = 0x74dc404d, // mtpc_inputAudioFileLocation
	VideoFileLocation = 0x3d0364ec, // mtpc_inputVideoFileLocation
	SecureFileLocation = 0xcbc7ee28, // mtpc_inputSecureFileLocation
};

enum FileStatus {
	FileDownloadFailed = -2,
	FileUploadFailed = -1,
	FileReady = 1,
};

// Don't change the values. This type is used for serialization.
enum DocumentType {
	FileDocument = 0,
	VideoDocument = 1,
	SongDocument = 2,
	StickerDocument = 3,
	AnimatedDocument = 4,
	VoiceDocument = 5,
	RoundVideoDocument = 6,
	WallPaperDocument = 7,
};

inline constexpr auto kStickerSideSize = 512;
[[nodiscard]] bool GoodStickerDimensions(int width, int height);

using MediaKey = QPair<uint64, uint64>;

class AudioMsgId {
public:
	enum class Type {
		Unknown,
		Voice,
		Song,
		Video,
	};

	AudioMsgId() = default;
	AudioMsgId(
		not_null<DocumentData*> audio,
		FullMsgId msgId,
		uint32 externalPlayId = 0)
	: _audio(audio)
	, _contextId(msgId)
	, _externalPlayId(externalPlayId) {
		setTypeFromAudio();
	}

	[[nodiscard]] static uint32 CreateExternalPlayId();
	[[nodiscard]] static AudioMsgId ForVideo();

	[[nodiscard]] Type type() const {
		return _type;
	}
	[[nodiscard]] DocumentData *audio() const {
		return _audio;
	}
	[[nodiscard]] FullMsgId contextId() const {
		return _contextId;
	}
	[[nodiscard]] uint32 externalPlayId() const {
		return _externalPlayId;
	}
	[[nodiscard]] explicit operator bool() const {
		return (_audio != nullptr) || (_externalPlayId != 0);
	}

private:
	void setTypeFromAudio();

	DocumentData *_audio = nullptr;
	Type _type = Type::Unknown;
	FullMsgId _contextId;
	uint32 _externalPlayId = 0;

};

inline bool operator<(const AudioMsgId &a, const AudioMsgId &b) {
	if (quintptr(a.audio()) < quintptr(b.audio())) {
		return true;
	} else if (quintptr(b.audio()) < quintptr(a.audio())) {
		return false;
	} else if (a.contextId() < b.contextId()) {
		return true;
	} else if (b.contextId() < a.contextId()) {
		return false;
	}
	return (a.externalPlayId() < b.externalPlayId());
}

inline bool operator==(const AudioMsgId &a, const AudioMsgId &b) {
	return (a.audio() == b.audio())
		&& (a.contextId() == b.contextId())
		&& (a.externalPlayId() == b.externalPlayId());
}

inline bool operator!=(const AudioMsgId &a, const AudioMsgId &b) {
	return !(a == b);
}

struct MessageCursor {
	MessageCursor() = default;
	MessageCursor(int position, int anchor, int scroll)
	: position(position)
	, anchor(anchor)
	, scroll(scroll) {
	}
	MessageCursor(not_null<const Ui::InputField*> field) {
		fillFrom(field);
	}

	void fillFrom(not_null<const Ui::InputField*> field);
	void applyTo(not_null<Ui::InputField*> field);

	int position = 0;
	int anchor = 0;
	int scroll = QFIXED_MAX;

};

inline bool operator==(
		const MessageCursor &a,
		const MessageCursor &b) {
	return (a.position == b.position)
		&& (a.anchor == b.anchor)
		&& (a.scroll == b.scroll);
}

inline bool operator!=(
		const MessageCursor &a,
		const MessageCursor &b) {
	return !(a == b);
}

struct StickerSetIdentifier {
	uint64 id = 0;
	uint64 accessHash = 0;
	QString shortName;

	[[nodiscard]] bool empty() const {
		return !id && shortName.isEmpty();
	}
	[[nodiscard]] explicit operator bool() const {
		return !empty();
	}
};

enum class MessageFlag : uint32 {
	HideEdited            = (1U << 0),
	Legacy                = (1U << 1),
	HasReplyMarkup        = (1U << 2),
	HasFromId             = (1U << 3),
	HasPostAuthor         = (1U << 4),
	HasViews              = (1U << 5),
	HasReplyInfo          = (1U << 6),
	HasViaBot             = (1U << 7),
	AdminLogEntry         = (1U << 8),
	Post                  = (1U << 9),
	Silent                = (1U << 10),
	Outgoing              = (1U << 11),
	Pinned                = (1U << 12),
	MediaIsUnread         = (1U << 13),
	MentionsMe            = (1U << 14),
	IsOrWasScheduled      = (1U << 15),

	// Needs to return back to inline mode.
	HasSwitchInlineButton = (1U << 16),

	// For "shared links" indexing.
	HasTextLinks          = (1U << 17),

	// Group / channel create or migrate service message.
	IsGroupEssential      = (1U << 18),

	// Edited media is generated on the client
	// and should not update media from server.
	IsLocalUpdateMedia    = (1U << 19),

	// Sent from inline bot, need to re-set media when sent.
	FromInlineBot         = (1U << 20),

	// Generated on the client side and should be unread.
	ClientSideUnread      = (1U << 21),

	// In a supergroup.
	HasAdminBadge         = (1U << 22),

	// Outgoing message that is being sent.
	BeingSent             = (1U << 23),

	// Outgoing message and failed to be sent.
	SendingFailed         = (1U << 24),

	// No media and only a several emoji text.
	IsolatedEmoji         = (1U << 25),

	// Local message existing in the message history.
	LocalHistoryEntry     = (1U << 26),

	// Fake message for some UI element.
	FakeHistoryItem       = (1U << 27),
};
inline constexpr bool is_flag_type(MessageFlag) { return true; }
using MessageFlags = base::flags<MessageFlag>;
