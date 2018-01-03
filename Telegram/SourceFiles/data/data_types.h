/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {

struct UploadState {
	UploadState(int size) : size(size) {
	}
	int offset = 0;
	int size = 0;
	bool waitingForAlbum = false;
};

} // namespace Data

class PeerData;
class UserData;
class ChatData;
class ChannelData;

using UserId = int32;
using ChatId = int32;
using ChannelId = int32;

constexpr auto NoChannel = ChannelId(0);

using PeerId = uint64;

constexpr auto PeerIdMask         = PeerId(0xFFFFFFFFULL);
constexpr auto PeerIdTypeMask     = PeerId(0x300000000ULL);
constexpr auto PeerIdUserShift    = PeerId(0x000000000ULL);
constexpr auto PeerIdChatShift    = PeerId(0x100000000ULL);
constexpr auto PeerIdChannelShift = PeerId(0x200000000ULL);

inline bool peerIsUser(const PeerId &id) {
	return (id & PeerIdTypeMask) == PeerIdUserShift;
}
inline bool peerIsChat(const PeerId &id) {
	return (id & PeerIdTypeMask) == PeerIdChatShift;
}
inline bool peerIsChannel(const PeerId &id) {
	return (id & PeerIdTypeMask) == PeerIdChannelShift;
}
inline PeerId peerFromUser(UserId user_id) {
	return PeerIdUserShift | uint64(uint32(user_id));
}
inline PeerId peerFromChat(ChatId chat_id) {
	return PeerIdChatShift | uint64(uint32(chat_id));
}
inline PeerId peerFromChannel(ChannelId channel_id) {
	return PeerIdChannelShift | uint64(uint32(channel_id));
}
inline PeerId peerFromUser(const MTPint &user_id) {
	return peerFromUser(user_id.v);
}
inline PeerId peerFromChat(const MTPint &chat_id) {
	return peerFromChat(chat_id.v);
}
inline PeerId peerFromChannel(const MTPint &channel_id) {
	return peerFromChannel(channel_id.v);
}
inline int32 peerToBareInt(const PeerId &id) {
	return int32(uint32(id & PeerIdMask));
}
inline UserId peerToUser(const PeerId &id) {
	return peerIsUser(id) ? peerToBareInt(id) : 0;
}
inline ChatId peerToChat(const PeerId &id) {
	return peerIsChat(id) ? peerToBareInt(id) : 0;
}
inline ChannelId peerToChannel(const PeerId &id) {
	return peerIsChannel(id) ? peerToBareInt(id) : NoChannel;
}
inline MTPint peerToBareMTPInt(const PeerId &id) {
	return MTP_int(peerToBareInt(id));
}
inline PeerId peerFromMTP(const MTPPeer &peer) {
	switch (peer.type()) {
	case mtpc_peerUser: return peerFromUser(peer.c_peerUser().vuser_id);
	case mtpc_peerChat: return peerFromChat(peer.c_peerChat().vchat_id);
	case mtpc_peerChannel: return peerFromChannel(peer.c_peerChannel().vchannel_id);
	}
	return 0;
}
inline MTPpeer peerToMTP(const PeerId &id) {
	if (peerIsUser(id)) {
		return MTP_peerUser(peerToBareMTPInt(id));
	} else if (peerIsChat(id)) {
		return MTP_peerChat(peerToBareMTPInt(id));
	} else if (peerIsChannel(id)) {
		return MTP_peerChannel(peerToBareMTPInt(id));
	}
	return MTP_peerUser(MTP_int(0));
}

using MsgId = int32;
constexpr auto StartClientMsgId = MsgId(-0x7FFFFFFF);
constexpr auto EndClientMsgId = MsgId(-0x40000000);
constexpr auto ShowAtTheEndMsgId = MsgId(-0x40000000);
constexpr auto SwitchAtTopMsgId = MsgId(-0x3FFFFFFF);
constexpr auto ShowAtProfileMsgId = MsgId(-0x3FFFFFFE);
constexpr auto ShowAndStartBotMsgId = MsgId(-0x3FFFFFD);
constexpr auto ShowAtGameShareMsgId = MsgId(-0x3FFFFFC);
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
	FullMsgId() = default;
	FullMsgId(ChannelId channel, MsgId msg) : channel(channel), msg(msg) {
	}
	explicit operator bool() const {
		return msg != 0;
	}
	ChannelId channel = NoChannel;
	MsgId msg = 0;
};
inline bool operator==(const FullMsgId &a, const FullMsgId &b) {
	return (a.channel == b.channel) && (a.msg == b.msg);
}
inline bool operator!=(const FullMsgId &a, const FullMsgId &b) {
	return !(a == b);
}
inline bool operator<(const FullMsgId &a, const FullMsgId &b) {
	if (a.msg < b.msg) return true;
	if (a.msg > b.msg) return false;
	return a.channel < b.channel;
}

using MessageIdsList = std::vector<FullMsgId>;

inline PeerId peerFromMessage(const MTPmessage &msg) {
	auto compute = [](auto &message) {
		auto from_id = message.has_from_id() ? peerFromUser(message.vfrom_id) : 0;
		auto to_id = peerFromMTP(message.vto_id);
		auto out = message.is_out();
		return (out || !peerIsUser(to_id)) ? to_id : from_id;
	};
	switch (msg.type()) {
	case mtpc_message: return compute(msg.c_message());
	case mtpc_messageService: return compute(msg.c_messageService());
	}
	return 0;
}
inline MTPDmessage::Flags flagsFromMessage(const MTPmessage &msg) {
	switch (msg.type()) {
	case mtpc_message: return msg.c_message().vflags.v;
	case mtpc_messageService: return mtpCastFlags(msg.c_messageService().vflags.v);
	}
	return 0;
}
inline MsgId idFromMessage(const MTPmessage &msg) {
	switch (msg.type()) {
	case mtpc_messageEmpty: return msg.c_messageEmpty().vid.v;
	case mtpc_message: return msg.c_message().vid.v;
	case mtpc_messageService: return msg.c_messageService().vid.v;
	}
	Unexpected("Type in idFromMessage()");
}
inline TimeId dateFromMessage(const MTPmessage &msg) {
	switch (msg.type()) {
	case mtpc_message: return msg.c_message().vdate.v;
	case mtpc_messageService: return msg.c_messageService().vdate.v;
	}
	return 0;
}

class DocumentData;
class PhotoData;
struct WebPageData;
struct GameData;

class AudioMsgId;
class PhotoClickHandler;
class PhotoOpenClickHandler;
class PhotoSaveClickHandler;
class PhotoCancelClickHandler;
class DocumentClickHandler;
class DocumentSaveClickHandler;
class DocumentOpenClickHandler;
class DocumentCancelClickHandler;
class GifOpenClickHandler;
class VoiceSeekClickHandler;

using PhotoId = uint64;
using VideoId = uint64;
using AudioId = uint64;
using DocumentId = uint64;
using WebPageId = uint64;
using GameId = uint64;
constexpr auto CancelledWebPageId = WebPageId(0xFFFFFFFFFFFFFFFFULL);

using PreparedPhotoThumbs = QMap<char, QPixmap>;

// [0] == -1 -- counting, [0] == -2 -- could not count
using VoiceWaveform = QVector<char>;

enum ActionOnLoad {
	ActionOnLoadNone,
	ActionOnLoadOpen,
	ActionOnLoadOpenWith,
	ActionOnLoadPlayInline
};

enum LocationType {
	UnknownFileLocation = 0,
	// 1, 2, etc are used as "version" value in mediaKey() method.

	DocumentFileLocation = 0x4e45abe9, // mtpc_inputDocumentFileLocation
	AudioFileLocation = 0x74dc404d, // mtpc_inputAudioFileLocation
	VideoFileLocation = 0x3d0364ec, // mtpc_inputVideoFileLocation
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
};

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
		DocumentData *audio,
		const FullMsgId &msgId,
		uint32 playId = 0)
	: _audio(audio)
	, _contextId(msgId)
	, _playId(playId) {
		setTypeFromAudio();
	}

	Type type() const {
		return _type;
	}
	DocumentData *audio() const {
		return _audio;
	}
	FullMsgId contextId() const {
		return _contextId;
	}
	uint32 playId() const {
		return _playId;
	}

	explicit operator bool() const {
		return _audio != nullptr;
	}

private:
	void setTypeFromAudio();

	DocumentData *_audio = nullptr;
	Type _type = Type::Unknown;
	FullMsgId _contextId;
	uint32 _playId = 0;

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
	return (a.playId() < b.playId());
}

inline bool operator==(const AudioMsgId &a, const AudioMsgId &b) {
	return (a.audio() == b.audio())
		&& (a.contextId() == b.contextId())
		&& (a.playId() == b.playId());
}

inline bool operator!=(const AudioMsgId &a, const AudioMsgId &b) {
	return !(a == b);
}

inline MsgId clientMsgId() {
	static MsgId CurrentClientMsgId = StartClientMsgId;
	Assert(CurrentClientMsgId < EndClientMsgId);
	return CurrentClientMsgId++;
}

struct MessageCursor {
	MessageCursor() = default;
	MessageCursor(int position, int anchor, int scroll)
		: position(position)
		, anchor(anchor)
		, scroll(scroll) {
	}
	MessageCursor(const QTextEdit *edit) {
		fillFrom(edit);
	}

	void fillFrom(const QTextEdit *edit);
	void applyTo(QTextEdit *edit);

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

struct SendAction {
	enum class Type {
		Typing,
		RecordVideo,
		UploadVideo,
		RecordVoice,
		UploadVoice,
		RecordRound,
		UploadRound,
		UploadPhoto,
		UploadFile,
		ChooseLocation,
		ChooseContact,
		PlayGame,
	};
	SendAction(
		Type type,
		TimeMs until,
		int progress = 0)
	: type(type)
	, until(until)
	, progress(progress) {
	}
	Type type = Type::Typing;
	TimeMs until = 0;
	int progress = 0;

};

class FileClickHandler : public LeftButtonClickHandler {
public:
	FileClickHandler(FullMsgId context) : _context(context) {
	}

	void setMessageId(FullMsgId context) {
		_context = context;
	}

	FullMsgId context() const {
		return _context;
	}

protected:
	HistoryItem *getActionItem() const;

private:
	FullMsgId _context;

};
