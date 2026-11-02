/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_user_privacy.h"
#include "base/weak_ptr.h"
#include "data/data_story.h"
#include "mtproto/sender.h"

class ApiWrap;
class TaskQueue;
class PhotoData;
class DocumentData;
struct FilePrepareResult;

namespace Main {
class Session;
} // namespace Main

namespace Media::Encode {
struct Job;
struct VideoSource;
struct TranscodeResult;
} // namespace Media::Encode

namespace Storage {
struct UploadedMedia;
} // namespace Storage

namespace Api {

inline constexpr auto kStoryDefaultPeriod = 86400;
inline constexpr auto kStoryVideoWidth = 720;
inline constexpr auto kStoryVideoHeight = 1280;
inline constexpr auto kStoryPhotoWidth = 1080;
inline constexpr auto kStoryPhotoHeight = 1920;
inline constexpr auto kStoryVideoMaxDuration = crl::time(60'000);
inline constexpr auto kStoryVideoMaxFileSize = 30 * 1024 * 1024;
inline constexpr auto kStoryPhotoMaxFileSize = 10 * 1024 * 1024;

struct SendStoryPhoto {
	QImage image;
	QString path;
};

struct SendStoryVideo {
	std::shared_ptr<Media::Encode::VideoSource> source;
	QImage cover;
	QString path;
	bool hasAudio = false;
	std::shared_ptr<Media::Encode::Job> job;
};

struct SendStoryRepost {
	not_null<PeerData*> peer;
	StoryId id = 0;
	PhotoData *photo = nullptr;
	DocumentData *document = nullptr;
};

using SendStoryMedia = std::variant<
	SendStoryPhoto,
	SendStoryVideo,
	SendStoryRepost>;

struct StoryToSend {
	not_null<PeerData*> peer;
	std::optional<SendStoryMedia> media;
	TextWithTags caption;
	UserPrivacy::Rule privacy;
	std::vector<int> albums;
	std::vector<Data::SuggestedReaction> reactions;
	std::vector<Data::UrlArea> urls;
	std::vector<Data::StoryLocation> locations;
	std::vector<Data::WeatherArea> weather;
	std::vector<Data::ChannelPost> channelPosts;
	StoryId editId = 0;
	int period = kStoryDefaultPeriod;
	bool pinned = true;
	bool noForwards = false;
	bool editCaption = false;
	bool editPrivacy = false;

	Fn<void(StoryId id, TimeId expiresAt)> sent;
	Fn<void(const QString &error)> failed;
};

struct StoryUpload {
	uint64 id = 0;
	not_null<PeerData*> peer;
	QImage preview;
	QString error;
	StoryId editId = 0;
	float64 progress = 0.;
	bool preparing = false;
};

struct StorySent {
	not_null<PeerData*> peer;
	StoryId id = 0;
	QString error;
	bool edit = false;
};

struct CanSendStory {
	enum class Type {
		Allowed,
		PremiumRequired,
		BoostsRequired,
		CountLimit,
		WeeklyLimit,
		MonthlyLimit,
		LiveAlready,
		Error,
	};
	Type type = Type::Allowed;
	int remains = 0;
	TimeId until = 0;
	QString error;

	[[nodiscard]] bool allowed() const {
		return (type == Type::Allowed);
	}
};

[[nodiscard]] QVector<MTPInputPrivacyRule> StoryPrivacyToInput(
	not_null<Main::Session*> session,
	const UserPrivacy::Rule &rule);

class Stories final : public base::has_weak_ptr {
public:
	explicit Stories(not_null<ApiWrap*> api);
	~Stories();

	void checkCanSend(
		not_null<PeerData*> peer,
		Fn<void(CanSendStory)> done);
	void requestChatsToSend(
		Fn<void(std::vector<not_null<PeerData*>>)> done);

	void send(StoryToSend &&story);
	void retry(uint64 uploadId);
	void remove(uint64 uploadId);

	void saveCloseFriends(
		std::vector<not_null<PeerData*>> users,
		Fn<void()> done = nullptr);

	[[nodiscard]] auto blocklistValue()
		-> rpl::producer<std::vector<not_null<PeerData*>>>;
	[[nodiscard]] bool blocklistLoaded() const;
	void reloadBlocklist();
	void saveBlocklist(
		std::vector<not_null<PeerData*>> peers,
		Fn<void()> done = nullptr);

	[[nodiscard]] const std::vector<StoryUpload> &uploads() const;
	[[nodiscard]] rpl::producer<> uploadsChanges() const;
	[[nodiscard]] rpl::producer<StorySent> sent() const;

	[[nodiscard]] bool uploadingFor(not_null<PeerData*> peer) const;

	[[nodiscard]] bool posting() const;
	void cancelPosting();
	void stopPostingWithConfirmation(Fn<void()> done);

private:
	struct Upload;

	void subscribeToUploader();
	[[nodiscard]] Upload *find(uint64 id);
	[[nodiscard]] Upload *findByUploadId(FullMsgId uploadId);
	[[nodiscard]] StoryUpload &infoFor(not_null<Upload*> upload);
	void notifyChanges();
	void start(not_null<Upload*> upload);
	void preparePhoto(not_null<Upload*> upload, QImage &&image);
	void preparePhotoFile(not_null<Upload*> upload, const QString &path);
	void transcodeVideo(not_null<Upload*> upload);
	void videoTranscoded(
		uint64 id,
		Media::Encode::TranscodeResult &&result,
		bool hasAudio);
	void encodeJob(
		not_null<Upload*> upload,
		std::shared_ptr<Media::Encode::Job> job,
		QImage cover);
	void prepareFile(
		not_null<Upload*> upload,
		const QString &path,
		QImage &&image);
	void prepared(uint64 id, std::shared_ptr<FilePrepareResult> result);
	void uploaded(
		not_null<Upload*> upload,
		const Storage::UploadedMedia &data);
	void updateProgress(not_null<Upload*> upload, float64 progress);
	void sendRequest(
		not_null<Upload*> upload,
		std::optional<MTPInputMedia> media,
		bool sameMediaAsRepost);
	void failed(not_null<Upload*> upload, const QString &error);
	void finish(not_null<Upload*> upload, StoryId id);
	void cancelWork(not_null<Upload*> upload);
	void removeTempFile(not_null<Upload*> upload);
	void requestBlocklist(int offset);

	const not_null<Main::Session*> _session;
	MTP::Sender _api;

	std::vector<std::unique_ptr<Upload>> _uploads;
	std::vector<StoryUpload> _uploadsInfo;
	std::unique_ptr<TaskQueue> _prepareQueue;
	rpl::event_stream<> _uploadsChanges;
	rpl::event_stream<StorySent> _sent;
	rpl::lifetime _uploaderLifetime;

	std::vector<not_null<PeerData*>> _blocklist;
	rpl::event_stream<> _blocklistChanges;
	mtpRequestId _blocklistRequestId = 0;
	int _blocklistCount = 0;
	bool _blocklistFull = false;
	bool _blocklistLoaded = false;

};

} // namespace Api
