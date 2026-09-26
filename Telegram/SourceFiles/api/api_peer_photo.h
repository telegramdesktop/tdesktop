/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"

class ApiWrap;
class PeerData;
class UserData;

namespace Data {
struct FileOrigin;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Media::Encode {
struct VideoSource;
struct TranscodeResult;
} // namespace Media::Encode

namespace Api {

class PeerPhoto final {
public:
	using UserPhotoId = PhotoId;
	explicit PeerPhoto(not_null<ApiWrap*> api);
	~PeerPhoto();

	enum class EmojiListType {
		Profile,
		Group,
		Background,
		NoChannelStatus,
	};

	struct UserPhoto {
		QImage image;
		DocumentId markupDocumentId = 0;
		std::vector<QColor> markupColors;
		std::shared_ptr<Media::Encode::VideoSource> video;
	};

	struct UploadProgress {
		not_null<PeerData*> peer;
		float64 progress = 0.;
	};

	void upload(
		not_null<PeerData*> peer,
		UserPhoto &&photo,
		Fn<void()> done = nullptr);
	void uploadFallback(not_null<PeerData*> peer, UserPhoto &&photo);
	void updateSelf(
		not_null<PhotoData*> photo,
		Data::FileOrigin origin,
		Fn<void()> done = nullptr);
	void suggest(not_null<PeerData*> peer, UserPhoto &&photo);
	void clear(not_null<PhotoData*> photo);
	void clearPersonal(not_null<UserData*> user);
	void set(not_null<PeerData*> peer, not_null<PhotoData*> photo);

	struct UploadCallbacks {
		Fn<void(float64)> progress;
		Fn<void()> done;
		Fn<void()> failed;
	};
	void subscribeToUpload(
		not_null<PeerData*> peer,
		rpl::lifetime &lifetime,
		UploadCallbacks callbacks);

	[[nodiscard]] auto uploadProgress() const
		-> rpl::producer<UploadProgress>;
	[[nodiscard]] auto uploadDone() const
		-> rpl::producer<not_null<PeerData*>>;
	[[nodiscard]] auto uploadFailed() const
		-> rpl::producer<not_null<PeerData*>>;
	void cancelUpload(not_null<PeerData*> peer);

	void requestUserPhotos(not_null<UserData*> user, UserPhotoId afterId);

	void requestEmojiList(EmojiListType type);
	using EmojiList = std::vector<DocumentId>;
	[[nodiscard]] rpl::producer<EmojiList> emojiListValue(EmojiListType type);

	// Non-personal photo in case a personal photo is set.
	void registerNonPersonalPhoto(
		not_null<UserData*> user,
		not_null<PhotoData*> photo);
	void unregisterNonPersonalPhoto(not_null<UserData*> user);
	[[nodiscard]] PhotoData *nonPersonalPhoto(
		not_null<UserData*> user) const;

private:
	enum class UploadType {
		Default,
		Suggestion,
		Fallback,
	};
	struct EmojiListData {
		rpl::variable<EmojiList> list;
		mtpRequestId requestId = 0;
	};

	struct ReadyFiles {
		std::optional<MTPInputFile> file;
		std::optional<MTPVideoSize> videoSize;
		std::optional<MTPInputFile> video;
		float64 videoStartTs = 0.;
	};
	void ready(FullMsgId msgId, ReadyFiles &&files);
	void upload(
		not_null<PeerData*> peer,
		UserPhoto &&photo,
		UploadType type,
		Fn<void()> done);
	void uploadWithVideo(
		not_null<PeerData*> peer,
		UserPhoto &&photo,
		UploadType type,
		Fn<void()> done);
	void videoTranscoded(
		FullMsgId msgId,
		Media::Encode::TranscodeResult &&result);
	void checkVideoUploadDone(FullMsgId msgId);
	void clearUpload(FullMsgId msgId);

	[[nodiscard]] EmojiListData &emojiList(EmojiListType type);
	[[nodiscard]] const EmojiListData &emojiList(EmojiListType type) const;

	const not_null<Main::Session*> _session;
	MTP::Sender _api;

	struct UploadValue {
		not_null<PeerData*> peer;
		UploadType type = UploadType::Default;
		Fn<void()> done;
		PhotoId photoId = 0;

		FullMsgId videoId;
		float64 videoStartTs = 0.;
		std::optional<MTPInputFile> photoFile;
		std::optional<MTPInputFile> videoFile;
		std::shared_ptr<std::atomic<bool>> cancelTranscode;
		bool transcoding = false;
		bool waitingPhoto = false;
		bool waitingVideo = false;
	};

	base::flat_map<FullMsgId, UploadValue> _uploads;
	base::flat_map<FullMsgId, FullMsgId> _videoToPhotoId;
	rpl::event_stream<UploadProgress> _uploadProgress;
	rpl::event_stream<not_null<PeerData*>> _uploadDone;
	rpl::event_stream<not_null<PeerData*>> _uploadFailed;

	base::flat_map<not_null<UserData*>, mtpRequestId> _userPhotosRequests;

	base::flat_map<
		not_null<UserData*>,
		not_null<PhotoData*>> _nonPersonalPhotos;

	EmojiListData _profileEmojiList;
	EmojiListData _groupEmojiList;
	EmojiListData _backgroundEmojiList;
	EmojiListData _noChannelStatusEmojiList;

};

} // namespace Api
