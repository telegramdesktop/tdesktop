/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text.h" // Ui::kQFixedMax.
#include "data/data_peer_id.h"
#include "data/data_msg_id.h"
#include "base/qt/qt_compare.h"

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

namespace Images {
enum class Option;
using Options = base::flags<Option>;
} // namespace Images

namespace Data {

struct FileOrigin;
struct EmojiStatusCollectible;

struct UploadState {
	explicit UploadState(int64 size) : size(size) {
	}
	int64 offset = 0;
	int64 size = 0;
	bool waitingForAlbum = false;
};

Storage::Cache::Key DocumentCacheKey(int32 dcId, uint64 id);
Storage::Cache::Key DocumentThumbCacheKey(int32 dcId, uint64 id);
Storage::Cache::Key WebDocumentCacheKey(const WebFileLocation &location);
Storage::Cache::Key UrlCacheKey(const QString &location);
Storage::Cache::Key GeoPointCacheKey(const GeoPointLocation &location);
Storage::Cache::Key AudioAlbumThumbCacheKey(
	const AudioAlbumThumbLocation &location);

constexpr auto kImageCacheTag = uint8(0x01);
constexpr auto kStickerCacheTag = uint8(0x02);
constexpr auto kVoiceMessageCacheTag = uint8(0x03);
constexpr auto kVideoMessageCacheTag = uint8(0x04);
constexpr auto kAnimationCacheTag = uint8(0x05);

} // namespace Data

struct MessageGroupId {
	uint64 peerAndScheduledFlag = 0;
	uint64 value = 0;

	MessageGroupId() = default;
	static MessageGroupId FromRaw(
			PeerId peer,
			uint64 value,
			bool scheduled) {
		auto result = MessageGroupId();
		result.peerAndScheduledFlag = peer.value
			| (scheduled ? (1ULL << 55) : 0);
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

	friend inline constexpr auto operator<=>(
		MessageGroupId,
		MessageGroupId) noexcept = default;

};

class PeerData;
class UserData;
class ChatData;
class ChannelData;
struct BotInfo;

namespace Data {
class Folder;
} // namespace Data

using FolderId = int32;
using FilterId = int32;

using MessageIdsList = std::vector<FullMsgId>;

[[nodiscard]] PeerId PeerFromMessage(const MTPmessage &message);
[[nodiscard]] MTPDmessage::Flags FlagsFromMessage(
	const MTPmessage &message);
[[nodiscard]] MsgId IdFromMessage(const MTPmessage &message);
[[nodiscard]] TimeId DateFromMessage(const MTPmessage &message);
[[nodiscard]] BusinessShortcutId BusinessShortcutIdFromMessage(
	const MTPmessage &message);

[[nodiscard]] inline MTPint MTP_int(MsgId id) noexcept {
	return MTP_int(id.bare);
}

class DocumentData;
class PhotoData;
struct WebPageData;
struct GameData;
struct BotAppData;
struct PollData;
struct TodoListData;

using PhotoId = uint64;
using VideoId = uint64;
using AudioId = uint64;
using DocumentId = uint64;
using WebPageId = uint64;
using GameId = uint64;
using PollId = uint64;
using TodoListId = FullMsgId;
using WallPaperId = uint64;
using CallId = uint64;
using BotAppId = uint64;
using EffectId = uint64;
using CollectibleId = uint64;

struct EmojiStatusId {
	DocumentId documentId = 0;
	std::shared_ptr<Data::EmojiStatusCollectible> collectible;

	explicit operator bool() const {
		return documentId || collectible;
	}

	friend inline auto operator<=>(
		const EmojiStatusId &,
		const EmojiStatusId &) = default;
	friend inline bool operator==(
		const EmojiStatusId &,
		const EmojiStatusId &) = default;
};

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

enum FileStatus : signed char {
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
	int scroll = Ui::kQFixedMax;

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

enum class MessageFlag : uint64 {
	HideEdited            = (1ULL << 0),
	Legacy                = (1ULL << 1),
	HasReplyMarkup        = (1ULL << 2),
	HasFromId             = (1ULL << 3),
	HasPostAuthor         = (1ULL << 4),
	HasViews              = (1ULL << 5),
	HasReplyInfo          = (1ULL << 6),
	CanViewReactions      = (1ULL << 7),
	AdminLogEntry         = (1ULL << 8),
	Post                  = (1ULL << 9),
	Silent                = (1ULL << 10),
	Outgoing              = (1ULL << 11),
	Pinned                = (1ULL << 12),
	MediaIsUnread         = (1ULL << 13),
	HasUnreadReaction     = (1ULL << 14),
	MentionsMe            = (1ULL << 15),
	IsOrWasScheduled      = (1ULL << 16),
	NoForwards            = (1ULL << 17),
	InvertMedia           = (1ULL << 18),

	// Needs to return back to inline mode.
	HasSwitchInlineButton = (1ULL << 19),

	// For "shared links" indexing.
	HasTextLinks          = (1ULL << 20),

	// Group / channel create or migrate service message.
	IsGroupEssential      = (1ULL << 21),

	// Edited media is generated on the client
	// and should not update media from server.
	IsLocalUpdateMedia    = (1ULL << 22),

	// Sent from inline bot, need to re-set media when sent.
	FromInlineBot         = (1ULL << 23),

	// Generated on the client side and should be unread.
	ClientSideUnread      = (1ULL << 24),

	// In a supergroup.
	HasAdminBadge         = (1ULL << 25),

	// Outgoing message that is being sent.
	BeingSent             = (1ULL << 26),

	// Outgoing message and failed to be sent.
	SendingFailed         = (1ULL << 27),

	// No media and only a several emoji or an only custom emoji text.
	SpecialOnlyEmoji      = (1ULL << 28),

	// Message existing in the message history.
	HistoryEntry          = (1ULL << 29),

	// Local message, not existing on the server.
	Local                 = (1ULL << 30),

	// Fake message for some UI element.
	FakeHistoryItem       = (1ULL << 31),

	// Contact sign-up message, notification should be skipped for Silent.
	IsContactSignUp       = (1ULL << 32),

	// Optimization for item text custom emoji repainting.
	CustomEmojiRepainting = (1ULL << 33),

	// Profile photo suggestion, views have special media type.
	IsUserpicSuggestion   = (1ULL << 34),

	OnlyEmojiAndSpaces    = (1ULL << 35),
	OnlyEmojiAndSpacesSet = (1ULL << 36),

	// Fake message with some info, like bot cover and information.
	FakeAboutView         = (1ULL << 37),

	StoryItem             = (1ULL << 38),

	InHighlightProcess    = (1ULL << 39),

	// If not set then we need to refresh _displayFrom value.
	DisplayFromChecked    = (1ULL << 40),
	DisplayFromProfiles   = (1ULL << 41),

	ShowSimilarChannels   = (1ULL << 42),

	Sponsored             = (1ULL << 43),

	ReactionsAreTags      = (1ULL << 44),

	ShortcutMessage       = (1ULL << 45),

	EffectWatched         = (1ULL << 46),

	SensitiveContent      = (1ULL << 47),
	HasRestrictions       = (1ULL << 48),

	EstimatedDate         = (1ULL << 49),

	ReactionsAllowed      = (1ULL << 50),

	HideDisplayDate       = (1ULL << 51),

	StarsPaidSuggested    = (1ULL << 52),
	TonPaidSuggested      = (1ULL << 53),

	StoryInProfile        = (1ULL << 54),
};
inline constexpr bool is_flag_type(MessageFlag) { return true; }
using MessageFlags = base::flags<MessageFlag>;

enum class MediaWebPageFlag : uint8 {
	ForceLargeMedia = (1 << 0),
	ForceSmallMedia = (1 << 1),
	Manual = (1 << 2),
	Safe = (1 << 3),
	Sponsored = (1 << 4),
};
inline constexpr bool is_flag_type(MediaWebPageFlag) { return true; }
using MediaWebPageFlags = base::flags<MediaWebPageFlag>;

namespace Data {

enum class ForwardOptions {
	PreserveInfo,
	NoSenderNames,
	NoNamesAndCaptions,
};

struct ForwardDraft {
	MessageIdsList ids;
	ForwardOptions options = ForwardOptions::PreserveInfo;

	friend inline auto operator<=>(
		const ForwardDraft&,
		const ForwardDraft&) = default;
};

struct ResolvedForwardDraft {
	HistoryItemsList items;
	ForwardOptions options = ForwardOptions::PreserveInfo;
};

} // namespace Data
