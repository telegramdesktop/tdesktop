/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_stories.h"

#include "data/data_stories.h"

#include "api/api_text_entities.h"
#include "apiwrap.h"
#include "base/random.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_message_reaction_id.h"
#include "data/data_photo.h"
#include "data/data_premium_limits.h"
#include "data/data_story.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_app_config.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "styles/style_layers.h"
#include "window/window_controller.h"
#include "ui/boxes/confirm_box.h"
#include "ui/color_int_conversion.h"
#include "core/application.h"
#include "media/media_video_encode.h"
#include "storage/file_upload.h"
#include "storage/localimageloader.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/text/text_utilities.h"

#include <QtCore/QDir>
#include <QtCore/QFile>

namespace Api {
namespace {

constexpr auto kTranscodeShare = 0.3;
constexpr auto kPreviewSide = 160;
constexpr auto kBlocklistPerPage = 100;

class PrepareStoryTask final : public Task {
public:
	PrepareStoryTask(
		FileLoadTask::Args &&args,
		Fn<void(std::shared_ptr<FilePrepareResult>)> done);

	void process() override;
	void finish() override;

private:
	FileLoadTask _task;
	Fn<void(std::shared_ptr<FilePrepareResult>)> _done;

};

PrepareStoryTask::PrepareStoryTask(
	FileLoadTask::Args &&args,
	Fn<void(std::shared_ptr<FilePrepareResult>)> done)
: _task(std::move(args))
, _done(std::move(done)) {
}

void PrepareStoryTask::process() {
	_task.process({ .generateGoodThumbnail = true });
}

void PrepareStoryTask::finish() {
	_done(_task.peekResult());
}

[[nodiscard]] QImage PreparePreview(QImage image) {
	if (image.isNull()) {
		return image;
	}
	return image.scaled(
		kPreviewSide,
		kPreviewSide,
		Qt::KeepAspectRatio,
		Qt::SmoothTransformation);
}

[[nodiscard]] StoryId StoryIdFromUpdates(const MTPUpdates &updates) {
	auto result = StoryId();
	const auto check = [&](const MTPUpdate &update) {
		update.match([&](const MTPDupdateStory &data) {
			data.vstory().match([&](const MTPDstoryItem &data) {
				result = data.vid().v;
			}, [&](const MTPDstoryItemSkipped &data) {
				result = data.vid().v;
			}, [](const MTPDstoryItemDeleted &) {
			});
		}, [](const auto &) {
		});
	};
	updates.match([&](const MTPDupdates &data) {
		for (const auto &update : data.vupdates().v) {
			check(update);
		}
	}, [&](const MTPDupdatesCombined &data) {
		for (const auto &update : data.vupdates().v) {
			check(update);
		}
	}, [&](const MTPDupdateShort &data) {
		check(data.vupdate());
	}, [](const auto &) {
	});
	return result;
}

[[nodiscard]] CanSendStory CanSendStoryFromError(const QString &error) {
	using Type = CanSendStory::Type;
	const auto weekly = u"STORY_SEND_FLOOD_WEEKLY_"_q;
	const auto monthly = u"STORY_SEND_FLOOD_MONTHLY_"_q;
	if (error == u"PREMIUM_ACCOUNT_REQUIRED"_q) {
		return { .type = Type::PremiumRequired, .error = error };
	} else if (error.contains(u"BOOSTS_REQUIRED"_q)) {
		return { .type = Type::BoostsRequired, .error = error };
	} else if (error == u"STORIES_TOO_MUCH"_q) {
		return { .type = Type::CountLimit, .error = error };
	} else if (error.startsWith(weekly)) {
		return {
			.type = Type::WeeklyLimit,
			.until = error.mid(weekly.size()).toInt(),
			.error = error,
		};
	} else if (error.startsWith(monthly)) {
		return {
			.type = Type::MonthlyLimit,
			.until = error.mid(monthly.size()).toInt(),
			.error = error,
		};
	} else if (error.startsWith(u"STORY_LIVE_ALREADY_"_q)) {
		return { .type = Type::LiveAlready, .error = error };
	}
	return { .type = Type::Error, .error = error };
}

[[nodiscard]] MTPMediaAreaCoordinates AreaCoordinatesToMTP(
		const Data::StoryArea &area) {
	using Flag = MTPDmediaAreaCoordinates::Flag;
	const auto &geometry = area.geometry;
	return MTP_mediaAreaCoordinates(
		MTP_flags(area.radius ? Flag::f_radius : Flag()),
		MTP_double(geometry.center().x() * 100.),
		MTP_double(geometry.center().y() * 100.),
		MTP_double(geometry.width() * 100.),
		MTP_double(geometry.height() * 100.),
		MTP_double(area.rotation),
		MTP_double(area.radius));
}

[[nodiscard]] MTPMediaArea LocationToMTP(const Data::StoryLocation &data) {
	if (!data.venueId.isEmpty()) {
		return MTP_mediaAreaVenue(
			AreaCoordinatesToMTP(data.area),
			data.point.toMTP(),
			MTP_string(data.title),
			MTP_string(data.address),
			MTP_string(data.provider),
			MTP_string(data.venueId),
			MTP_string(data.venueType));
	}
	using Flag = MTPDmediaAreaGeoPoint::Flag;
	using AddressFlag = MTPDgeoPointAddress::Flag;
	const auto addressFlags = (!data.city.isEmpty()
		? AddressFlag::f_city
		: AddressFlag());
	return MTP_mediaAreaGeoPoint(
		MTP_flags((!data.countryIso2.isEmpty() || !data.city.isEmpty()
			? Flag::f_address
			: Flag())),
		AreaCoordinatesToMTP(data.area),
		data.point.toMTP(),
		MTP_geoPointAddress(
			MTP_flags(addressFlags),
			MTP_string(data.countryIso2),
			MTP_string(QString()),
			MTP_string(data.city),
			MTP_string(QString())));
}

[[nodiscard]] MTPVector<MTPMediaArea> MediaAreasToMTP(
		const StoryToSend &story) {
	auto result = QVector<MTPMediaArea>();
	result.reserve(
		story.reactions.size()
		+ story.urls.size()
		+ story.locations.size()
		+ story.weather.size()
		+ story.channelPosts.size());
	for (const auto &reaction : story.reactions) {
		using Flag = MTPDmediaAreaSuggestedReaction::Flag;
		result.push_back(MTP_mediaAreaSuggestedReaction(
			MTP_flags((reaction.dark ? Flag::f_dark : Flag())
				| (reaction.flipped ? Flag::f_flipped : Flag())),
			AreaCoordinatesToMTP(reaction.area),
			Data::ReactionToMTP(reaction.reaction)));
	}
	for (const auto &url : story.urls) {
		result.push_back(MTP_mediaAreaUrl(
			AreaCoordinatesToMTP(url.area),
			MTP_string(url.url)));
	}
	for (const auto &location : story.locations) {
		result.push_back(LocationToMTP(location));
	}
	for (const auto &weather : story.weather) {
		result.push_back(MTP_mediaAreaWeather(
			AreaCoordinatesToMTP(weather.area),
			MTP_string(weather.emoji),
			MTP_double(weather.millicelsius / 1000.),
			MTP_int(Ui::Color32ToSerialized(weather.color))));
	}
	for (const auto &post : story.channelPosts) {
		const auto postPeer = story.peer->owner().peer(post.itemId.peer);
		if (const auto channel = postPeer->asChannel()) {
			result.push_back(MTP_inputMediaAreaChannelPost(
				AreaCoordinatesToMTP(post.area),
				channel->inputChannel(),
				MTP_int(post.itemId.msg)));
		}
	}
	return MTP_vector<MTPMediaArea>(result);
}

} // namespace

struct Stories::Upload {
	explicit Upload(StoryToSend &&story) : story(std::move(story)) {
	}

	StoryToSend story;
	std::shared_ptr<std::atomic<bool>> cancelPreparing;
	std::shared_ptr<FilePrepareResult> prepared;
	FullMsgId uploadId;
	QString tempPath;
	uint64 id = 0;
	uint64 generation = 0;
	mtpRequestId requestId = 0;
	bool hasAudio = false;
	bool refreshedReference = false;
	bool uploading = false;
};

QVector<MTPInputPrivacyRule> StoryPrivacyToInput(
		not_null<Main::Session*> session,
		const UserPrivacy::Rule &rule) {
	using Option = UserPrivacy::Option;
	const auto collectUsers = [](const UserPrivacy::Exceptions &list) {
		auto result = QVector<MTPInputUser>();
		result.reserve(list.peers.size());
		for (const auto &peer : list.peers) {
			if (const auto user = peer->asUser()) {
				result.push_back(user->inputUser());
			}
		}
		return result;
	};
	const auto collectChats = [](const UserPrivacy::Exceptions &list) {
		auto result = QVector<MTPlong>();
		result.reserve(list.peers.size());
		for (const auto &peer : list.peers) {
			if (!peer->isUser()) {
				result.push_back(peerToBareMTPInt(peer->id));
			}
		}
		return result;
	};
	auto result = QVector<MTPInputPrivacyRule>();
	switch (rule.option) {
	case Option::Everyone:
		result.push_back(MTP_inputPrivacyValueAllowAll());
		break;
	case Option::Contacts:
		result.push_back(MTP_inputPrivacyValueAllowContacts());
		break;
	case Option::CloseFriends:
		result.push_back(MTP_inputPrivacyValueAllowCloseFriends());
		break;
	case Option::Nobody:
		break;
	}
	if (rule.option == Option::Nobody) {
		auto users = collectUsers(rule.always);
		const auto chats = collectChats(rule.always);
		if (users.empty() && chats.empty()) {
			users.push_back(session->user()->inputUser());
		}
		if (!users.empty()) {
			result.push_back(MTP_inputPrivacyValueAllowUsers(
				MTP_vector<MTPInputUser>(users)));
		}
		if (!chats.empty()) {
			result.push_back(MTP_inputPrivacyValueAllowChatParticipants(
				MTP_vector<MTPlong>(chats)));
		}
	} else if (rule.option != Option::CloseFriends) {
		const auto users = collectUsers(rule.never);
		const auto chats = collectChats(rule.never);
		if (!users.empty()) {
			result.push_back(MTP_inputPrivacyValueDisallowUsers(
				MTP_vector<MTPInputUser>(users)));
		}
		if (!chats.empty()) {
			result.push_back(MTP_inputPrivacyValueDisallowChatParticipants(
				MTP_vector<MTPlong>(chats)));
		}
	}
	return result;
}

Stories::Stories(not_null<ApiWrap*> api)
: _session(&api->session())
, _api(&api->instance()) {
	crl::on_main(_session, [=] {
		subscribeToUploader();
	});
}

void Stories::subscribeToUploader() {
	const auto &uploader = _session->uploader();
	uploader.photoReady(
	) | rpl::on_next([=](const Storage::UploadedMedia &data) {
		if (const auto upload = findByUploadId(data.fullId)) {
			uploaded(upload, data);
		}
	}, _uploaderLifetime);
	uploader.documentReady(
	) | rpl::on_next([=](const Storage::UploadedMedia &data) {
		if (const auto upload = findByUploadId(data.fullId)) {
			uploaded(upload, data);
		}
	}, _uploaderLifetime);
	uploader.photoProgress(
	) | rpl::on_next([=](const FullMsgId &id) {
		const auto upload = findByUploadId(id);
		if (!upload || !upload->prepared) {
			return;
		}
		const auto photo = _session->data().photo(upload->prepared->id);
		const auto &state = photo->uploadingData;
		if (state && state->size > 0) {
			updateProgress(upload, state->offset / float64(state->size));
		}
	}, _uploaderLifetime);
	uploader.documentProgress(
	) | rpl::on_next([=](const FullMsgId &id) {
		const auto upload = findByUploadId(id);
		if (!upload || !upload->prepared) {
			return;
		}
		const auto document = _session->data().document(
			upload->prepared->id);
		const auto &state = document->uploadingData;
		if (state && state->size > 0) {
			updateProgress(upload, state->offset / float64(state->size));
		}
	}, _uploaderLifetime);
	uploader.photoFailed(
	) | rpl::on_next([=](const FullMsgId &id) {
		if (const auto upload = findByUploadId(id)) {
			failed(upload, u"UPLOAD_FAILED"_q);
		}
	}, _uploaderLifetime);
	uploader.documentFailed(
	) | rpl::on_next([=](const FullMsgId &id) {
		if (const auto upload = findByUploadId(id)) {
			failed(upload, u"UPLOAD_FAILED"_q);
		}
	}, _uploaderLifetime);
}

Stories::~Stories() {
	for (const auto &upload : _uploads) {
		if (const auto cancel = base::take(upload->cancelPreparing)) {
			cancel->store(true);
		}
		removeTempFile(upload.get());
	}
}

void Stories::checkCanSend(
		not_null<PeerData*> peer,
		Fn<void(CanSendStory)> done) {
	const auto premium = _session->premium();
	const auto &config = _session->appConfig();
	const auto premiumOnly = config.storiesPostingPremiumOnly();
	if (!config.storiesPostingAvailable()) {
		done({ .type = CanSendStory::Type::Error });
		return;
	} else if (peer->isSelf() && premiumOnly && !premium) {
		done({ .type = CanSendStory::Type::PremiumRequired });
		return;
	}
	_api.request(MTPstories_CanSendStory(
		peer->input()
	)).done([=](const MTPstories_CanSendStoryCount &result) {
		const auto remains = result.data().vcount_remains().v;
		done((remains > 0)
			? CanSendStory{ .remains = remains }
			: CanSendStory{ .type = CanSendStory::Type::CountLimit });
	}).fail([=](const MTP::Error &error) {
		done(CanSendStoryFromError(error.type()));
	}).send();
}

void Stories::requestChatsToSend(
		Fn<void(std::vector<not_null<PeerData*>>)> done) {
	_api.request(MTPstories_GetChatsToSend(
	)).done([=](const MTPmessages_Chats &result) {
		const auto &chats = result.match([](const auto &data) {
			return data.vchats().v;
		});
		auto list = std::vector<not_null<PeerData*>>();
		list.reserve(chats.size());
		for (const auto &chat : chats) {
			if (const auto peer = _session->data().processChat(chat)) {
				if (peer->canPostStories()) {
					list.push_back(peer);
				}
			}
		}
		done(std::move(list));
	}).fail([=] {
		done({});
	}).send();
}

void Stories::send(StoryToSend &&story) {
	auto preview = QImage();
	if (story.media) {
		v::match(*story.media, [&](const SendStoryPhoto &data) {
			preview = PreparePreview(data.image);
		}, [&](const SendStoryVideo &data) {
			preview = PreparePreview(data.cover);
		}, [&](const SendStoryRepost &) {
		});
	}
	const auto id = base::RandomValue<uint64>();
	auto upload = std::make_unique<Upload>(std::move(story));
	upload->id = id;
	_uploadsInfo.push_back({
		.id = id,
		.peer = upload->story.peer,
		.preview = std::move(preview),
		.editId = upload->story.editId,
		.preparing = true,
	});
	_uploads.push_back(std::move(upload));
	start(_uploads.back().get());
	notifyChanges();
}

void Stories::retry(uint64 uploadId) {
	const auto upload = find(uploadId);
	if (!upload || infoFor(upload).error.isEmpty()) {
		return;
	}
	auto &info = infoFor(upload);
	info.error = QString();
	info.progress = 0.;
	info.preparing = true;
	if (upload->prepared) {
		prepared(upload->id, upload->prepared);
	} else {
		start(upload);
	}
	notifyChanges();
}

void Stories::remove(uint64 uploadId) {
	const auto upload = find(uploadId);
	if (!upload) {
		return;
	}
	auto failed = base::take(upload->story.failed);
	const auto error = infoFor(upload).error;
	cancelWork(upload);
	removeTempFile(upload);
	_uploadsInfo.erase(
		ranges::remove(_uploadsInfo, uploadId, &StoryUpload::id),
		end(_uploadsInfo));
	_uploads.erase(
		ranges::remove(_uploads, uploadId, [](const auto &entry) {
			return entry->id;
		}),
		end(_uploads));
	notifyChanges();
	if (failed) {
		failed(error);
	}
}

void Stories::saveCloseFriends(
		std::vector<not_null<PeerData*>> users,
		Fn<void()> done) {
	auto ids = QVector<MTPlong>();
	ids.reserve(users.size());
	for (const auto &peer : users) {
		if (peer->isUser()) {
			ids.push_back(peerToBareMTPInt(peer->id));
		}
	}
	_api.request(MTPcontacts_EditCloseFriends(
		MTP_vector<MTPlong>(ids)
	)).done([=] {
		_session->data().enumerateUsers([&](not_null<UserData*> user) {
			const auto is = ranges::contains(users, user);
			if (user->isCloseFriend() != is) {
				user->setIsCloseFriend(is);
			}
		});
		if (done) {
			done();
		}
	}).send();
}

auto Stories::blocklistValue()
-> rpl::producer<std::vector<not_null<PeerData*>>> {
	if (!_blocklistLoaded && !_blocklistRequestId) {
		reloadBlocklist();
	}
	return _blocklistChanges.events_starting_with({}) | rpl::map([=] {
		return _blocklist;
	});
}

bool Stories::blocklistLoaded() const {
	return _blocklistLoaded && _blocklistFull;
}

void Stories::reloadBlocklist() {
	if (_blocklistRequestId) {
		_api.request(base::take(_blocklistRequestId)).cancel();
	}
	_blocklistFull = false;
	requestBlocklist(0);
}

void Stories::requestBlocklist(int offset) {
	using Flag = MTPcontacts_GetBlocked::Flag;
	_blocklistRequestId = _api.request(MTPcontacts_GetBlocked(
		MTP_flags(Flag::f_my_stories_from),
		MTP_int(offset),
		MTP_int(kBlocklistPerPage)
	)).done([=](const MTPcontacts_Blocked &result) {
		_blocklistRequestId = 0;
		auto &owner = _session->data();
		auto list = offset
			? _blocklist
			: std::vector<not_null<PeerData*>>();
		const auto append = [&](const auto &data) {
			owner.processUsers(data.vusers());
			owner.processChats(data.vchats());
			for (const auto &blocked : data.vblocked().v) {
				const auto peerId = peerFromMTP(blocked.data().vpeer_id());
				const auto peer = owner.peerLoaded(peerId);
				if (peer && !ranges::contains(list, not_null(peer))) {
					list.push_back(peer);
				}
			}
		};
		result.match([&](const MTPDcontacts_blocked &data) {
			append(data);
			_blocklistCount = int(list.size());
			_blocklistFull = true;
		}, [&](const MTPDcontacts_blockedSlice &data) {
			append(data);
			_blocklistCount = std::max(data.vcount().v, int(list.size()));
			_blocklistFull = (int(list.size()) >= _blocklistCount)
				|| data.vblocked().v.isEmpty();
		});
		_blocklist = std::move(list);
		_blocklistLoaded = true;
		_blocklistChanges.fire({});
		if (!_blocklistFull) {
			requestBlocklist(int(_blocklist.size()));
		}
	}).fail([=] {
		_blocklistRequestId = 0;
		_blocklistLoaded = (offset > 0);
		_blocklistFull = true;
		_blocklistChanges.fire({});
	}).send();
}

void Stories::saveBlocklist(
		std::vector<not_null<PeerData*>> peers,
		Fn<void()> done) {
	using Flag = MTPcontacts_SetBlocked::Flag;
	auto ids = QVector<MTPInputPeer>();
	ids.reserve(peers.size());
	for (const auto &peer : peers) {
		ids.push_back(peer->input());
	}
	const auto limit = std::max(int(_blocklist.size()), int(peers.size()));
	_blocklist = std::move(peers);
	_blocklistCount = int(_blocklist.size());
	_blocklistFull = true;
	_blocklistLoaded = true;
	_blocklistChanges.fire({});
	_api.request(MTPcontacts_SetBlocked(
		MTP_flags(Flag::f_my_stories_from),
		MTP_vector<MTPInputPeer>(ids),
		MTP_int(limit)
	)).done([=] {
		if (done) {
			done();
		}
	}).fail([=] {
		reloadBlocklist();
	}).send();
}

const std::vector<StoryUpload> &Stories::uploads() const {
	return _uploadsInfo;
}

rpl::producer<> Stories::uploadsChanges() const {
	return _uploadsChanges.events();
}

rpl::producer<StorySent> Stories::sent() const {
	return _sent.events();
}

bool Stories::uploadingFor(not_null<PeerData*> peer) const {
	return ranges::contains(_uploadsInfo, peer, &StoryUpload::peer);
}

bool Stories::posting() const {
	return ranges::any_of(_uploadsInfo, [](const StoryUpload &info) {
		return info.error.isEmpty();
	});
}

void Stories::stopPostingWithConfirmation(Fn<void()> done) {
	const auto window = Core::App().activePrimaryWindow();
	if (!window) {
		done();
		return;
	}
	window->show(Ui::MakeConfirmBox({
		.text = tr::lng_story_sure_stop(),
		.confirmed = [=](Fn<void()> close) {
			close();
			done();
		},
		.confirmText = tr::lng_story_stop_button(),
		.confirmStyle = &st::attentionBoxButton,
		.title = tr::lng_story_sure_stop_title(),
	}));
	window->activate();
}

void Stories::cancelPosting() {
	auto ids = std::vector<uint64>();
	for (const auto &info : _uploadsInfo) {
		if (info.error.isEmpty()) {
			ids.push_back(info.id);
		}
	}
	for (const auto id : ids) {
		remove(id);
	}
}

Stories::Upload *Stories::find(uint64 id) {
	const auto i = ranges::find(_uploads, id, [](const auto &entry) {
		return entry->id;
	});
	return (i != end(_uploads)) ? i->get() : nullptr;
}

Stories::Upload *Stories::findByUploadId(FullMsgId uploadId) {
	if (!uploadId) {
		return nullptr;
	}
	const auto i = ranges::find_if(_uploads, [&](const auto &entry) {
		return entry->uploading && (entry->uploadId == uploadId);
	});
	return (i != end(_uploads)) ? i->get() : nullptr;
}

StoryUpload &Stories::infoFor(not_null<Upload*> upload) {
	const auto i = ranges::find(_uploadsInfo, upload->id, &StoryUpload::id);
	Assert(i != end(_uploadsInfo));
	return *i;
}

void Stories::notifyChanges() {
	_uploadsChanges.fire({});
}

void Stories::start(not_null<Upload*> upload) {
	++upload->generation;
	auto &story = upload->story;
	if (!story.media) {
		if (story.editId) {
			sendRequest(upload, std::nullopt, false);
		} else {
			failed(upload, u"MEDIA_EMPTY"_q);
		}
		return;
	}
	v::match(*story.media, [&](SendStoryPhoto &data) {
		if (data.path.isEmpty()) {
			preparePhoto(upload, base::duplicate(data.image));
		} else {
			preparePhotoFile(upload, data.path);
		}
	}, [&](SendStoryVideo &data) {
		if (data.path.isEmpty()) {
			transcodeVideo(upload);
			return;
		}
		upload->hasAudio = data.hasAudio;
		prepareFile(upload, data.path, QImage());
	}, [&](SendStoryRepost &data) {
		if (data.photo) {
			sendRequest(
				upload,
				MTP_inputMediaPhoto(
					MTP_flags(0),
					data.photo->mtpInput(),
					MTPint(),
					MTPInputDocument()),
				true);
		} else if (data.document) {
			sendRequest(
				upload,
				MTP_inputMediaDocument(
					MTP_flags(0),
					data.document->mtpInput(),
					MTPInputPhoto(),
					MTPint(),
					MTPint(),
					MTPstring()),
				true);
		} else {
			failed(upload, u"MEDIA_EMPTY"_q);
		}
	});
}

void Stories::preparePhoto(not_null<Upload*> upload, QImage &&image) {
	if (image.isNull()) {
		failed(upload, u"MEDIA_EMPTY"_q);
		return;
	}
	const auto limit = QSize(kStoryPhotoWidth, kStoryPhotoHeight);
	if (image.width() > limit.width() || image.height() > limit.height()) {
		image = image.scaled(
			limit,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	}
	prepareFile(upload, QString(), std::move(image));
}

void Stories::preparePhotoFile(
		not_null<Upload*> upload,
		const QString &path) {
	const auto id = upload->id;
	const auto generation = upload->generation;
	if (!_prepareQueue) {
		_prepareQueue = std::make_unique<TaskQueue>();
	}
	const auto weak = base::make_weak(this);
	_prepareQueue->addTask(std::make_unique<PrepareStoryTask>(
		FileLoadTask::Args{
			.session = _session,
			.filepath = path,
			.type = SendMediaType::Photo,
			.to = FileLoadTo(
				upload->story.peer->id,
				SendOptions(),
				FullReplyTo(),
				MsgId()),
			.sendLargePhotos = true,
		},
		[=](std::shared_ptr<FilePrepareResult> result) {
			const auto strong = weak.get();
			const auto upload = strong ? strong->find(id) : nullptr;
			if (upload && (upload->generation == generation)) {
				strong->prepared(id, std::move(result));
			}
		}));
}

void Stories::transcodeVideo(not_null<Upload*> upload) {
	const auto &data = std::get<SendStoryVideo>(*upload->story.media);
	if (!data.source && !data.job) {
		failed(upload, u"MEDIA_EMPTY"_q);
		return;
	}
	if (data.job) {
		encodeJob(upload, data.job, data.cover);
		return;
	}
	const auto id = upload->id;
	const auto generation = upload->generation;
	const auto cancel = std::make_shared<std::atomic<bool>>(false);
	upload->cancelPreparing = cancel;
	updateProgress(upload, 0.);

	const auto source = data.source;
	crl::async([=, weak = base::make_weak(this)] {
		auto lastReported = -1.;
		auto result = Media::Encode::TranscodeVideo(*source, [&](
				float64 value) {
			if (value - lastReported >= 0.01 || value >= 1.) {
				lastReported = value;
				crl::on_main(weak, [=] {
					const auto upload = find(id);
					if (upload
						&& (upload->generation == generation)
						&& infoFor(upload).preparing) {
						updateProgress(upload, value * kTranscodeShare);
					}
				});
			}
			return !cancel->load();
		});
		const auto hasAudio = result.empty()
			? false
			: Media::Encode::ProbeSource(result.path).hasAudio;
		crl::on_main([=, result = std::move(result)]() mutable {
			const auto strong = weak.get();
			const auto upload = strong ? strong->find(id) : nullptr;
			if (!upload
				|| (upload->generation != generation)
				|| cancel->load()) {
				if (!result.path.isEmpty()) {
					QFile::remove(result.path);
				}
				return;
			}
			strong->videoTranscoded(id, std::move(result), hasAudio);
		});
	});
}

void Stories::encodeJob(
		not_null<Upload*> upload,
		std::shared_ptr<Media::Encode::Job> job,
		QImage cover) {
	const auto id = upload->id;
	const auto generation = upload->generation;
	const auto still = std::get_if<Media::Encode::StillSource>(&job->source);
	const auto hasAudio = !still || !still->music.empty();
	auto cancel = upload->cancelPreparing;
	if (!cancel) {
		cancel = std::make_shared<std::atomic<bool>>(false);
		upload->cancelPreparing = cancel;
	}
	crl::async([=, weak = base::make_weak(this)] {
		auto copy = *job;
		auto lastReported = -1.;
		const auto encoded = Media::Encode::Run(std::move(copy), [&](
				float64 value) {
			if (value - lastReported >= 0.01 || value >= 1.) {
				lastReported = value;
				crl::on_main(weak, [=] {
					const auto upload = find(id);
					if (upload
						&& (upload->generation == generation)
						&& infoFor(upload).preparing) {
						updateProgress(upload, value * kTranscodeShare);
					}
				});
			}
			return !cancel->load();
		});
		auto result = Media::Encode::TranscodeResult();
		if (!encoded.empty()) {
			const auto path = QDir::tempPath()
				+ u"/tdesktop_story_%1.mp4"_q.arg(id);
			auto file = QFile(path);
			if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)
				&& (file.write(encoded.bytes) == encoded.bytes.size())) {
				result.path = path;
				result.cover = cover;
				result.dimensions = encoded.dimensions;
				result.duration = encoded.duration;
			}
		}
		crl::on_main([=, result = std::move(result)]() mutable {
			const auto strong = weak.get();
			const auto upload = strong ? strong->find(id) : nullptr;
			if (!upload
				|| (upload->generation != generation)
				|| cancel->load()) {
				if (!result.path.isEmpty()) {
					QFile::remove(result.path);
				}
				return;
			}
			strong->videoTranscoded(id, std::move(result), hasAudio);
		});
	});
}

void Stories::videoTranscoded(
		uint64 id,
		Media::Encode::TranscodeResult &&result,
		bool hasAudio) {
	const auto upload = find(id);
	if (!upload) {
		if (!result.path.isEmpty()) {
			QFile::remove(result.path);
		}
		return;
	}
	upload->cancelPreparing = nullptr;
	if (result.empty()) {
		failed(upload, u"TRANSCODE_FAILED"_q);
		return;
	}
	removeTempFile(upload);
	upload->tempPath = result.path;
	upload->hasAudio = hasAudio;
	prepareFile(upload, result.path, QImage());
}

void Stories::prepareFile(
		not_null<Upload*> upload,
		const QString &path,
		QImage &&image) {
	const auto id = upload->id;
	const auto generation = upload->generation;
	const auto photo = path.isEmpty();
	auto information = std::unique_ptr<Ui::PreparedFileInformation>();
	if (photo) {
		information = std::make_unique<Ui::PreparedFileInformation>();
		information->filemime = u"image/jpeg"_q;
		information->media = Ui::PreparedFileInformation::Image{
			.data = std::move(image),
		};
	}
	if (!_prepareQueue) {
		_prepareQueue = std::make_unique<TaskQueue>();
	}
	const auto weak = base::make_weak(this);
	_prepareQueue->addTask(std::make_unique<PrepareStoryTask>(
		FileLoadTask::Args{
			.session = _session,
			.filepath = path,
			.information = std::move(information),
			.type = (photo ? SendMediaType::Photo : SendMediaType::File),
			.to = FileLoadTo(
				upload->story.peer->id,
				SendOptions(),
				FullReplyTo(),
				MsgId()),
			.sendLargePhotos = true,
		},
		[=](std::shared_ptr<FilePrepareResult> result) {
			const auto strong = weak.get();
			const auto upload = strong ? strong->find(id) : nullptr;
			if (upload && (upload->generation == generation)) {
				strong->prepared(id, std::move(result));
			}
		}));
}

void Stories::prepared(
		uint64 id,
		std::shared_ptr<FilePrepareResult> result) {
	const auto upload = find(id);
	if (!upload) {
		return;
	}
	const auto photo = v::is<SendStoryPhoto>(*upload->story.media);
	const auto expected = photo ? SendMediaType::Photo : SendMediaType::File;
	if (!result
		|| (result->type != expected)
		|| (result->filesize <= 0)
		|| (!photo && (result->document.type() != mtpc_document))) {
		failed(upload, u"PREPARE_FAILED"_q);
		return;
	}
	upload->prepared = result;
	upload->uploadId = FullMsgId(
		upload->story.peer->id,
		_session->data().nextLocalMessageId());
	upload->uploading = true;
	auto &info = infoFor(upload);
	info.preparing = false;
	notifyChanges();
	_session->uploader().upload(upload->uploadId, result);
}

void Stories::uploaded(
		not_null<Upload*> upload,
		const Storage::UploadedMedia &data) {
	upload->uploading = false;
	const auto &prepared = upload->prepared;
	if (!prepared) {
		failed(upload, u"UPLOAD_FAILED"_q);
		return;
	}
	if (prepared->type == SendMediaType::Photo) {
		sendRequest(
			upload,
			MTP_inputMediaUploadedPhoto(
				MTP_flags(0),
				data.info.file,
				MTP_vector<MTPInputDocument>(),
				MTP_int(0),
				MTPInputDocument()),
			false);
		return;
	}
	const auto attributes = prepared->document.match([](
			const MTPDdocument &data) {
		return data.vattributes();
	}, [](const MTPDdocumentEmpty &) {
		return MTPVector<MTPDocumentAttribute>(
			MTP_vector<MTPDocumentAttribute>());
	});
	using Flag = MTPDinputMediaUploadedDocument::Flag;
	const auto flags = (data.info.thumb ? Flag::f_thumb : Flag())
		| (upload->hasAudio ? Flag() : Flag::f_nosound_video);
	sendRequest(
		upload,
		MTP_inputMediaUploadedDocument(
			MTP_flags(flags),
			data.info.file,
			data.info.thumb.value_or(MTPInputFile()),
			MTP_string(u"video/mp4"_q),
			attributes,
			MTP_vector<MTPInputDocument>(),
			MTPInputPhoto(),
			MTP_int(0),
			MTP_int(0)),
		false);
}

void Stories::updateProgress(
		not_null<Upload*> upload,
		float64 progress) {
	auto &info = infoFor(upload);
	const auto video = upload->story.media
		&& v::match(*upload->story.media, [](const SendStoryVideo &data) {
			return data.source || data.job;
		}, [](const auto &) {
			return false;
		});
	const auto value = (video && !info.preparing)
		? (kTranscodeShare + (1. - kTranscodeShare) * progress)
		: progress;
	if (info.progress != value) {
		info.progress = value;
		notifyChanges();
	}
}

void Stories::sendRequest(
		not_null<Upload*> upload,
		std::optional<MTPInputMedia> media,
		bool sameMediaAsRepost) {
	const auto id = upload->id;
	const auto &story = upload->story;
	const auto peer = story.peer;
	const auto limits = Data::PremiumLimits(_session);
	const auto captionLimit = limits.storyCaptionLengthCurrent();
	auto caption = TextWithEntities{
		story.caption.text,
		TextUtilities::ConvertTextTagsToEntities(story.caption.tags),
	};
	if (caption.text.size() > captionLimit) {
		caption = Ui::Text::Mid(caption, 0, captionLimit);
	}
	if (!_session->appConfig().storiesEntitiesAvailable()
		|| (_session->appConfig().storiesEntitiesPremiumOnly()
			&& !_session->premium())) {
		caption.entities.clear();
	}
	const auto entities = EntitiesToMTP(
		_session,
		caption.entities,
		ConvertOption::SkipLocal);
	const auto rules = StoryPrivacyToInput(_session, story.privacy);

	auto &info = infoFor(upload);
	info.preparing = false;
	info.progress = 1.;
	notifyChanges();

	const auto done = [=](const MTPUpdates &result) {
		const auto upload = find(id);
		if (!upload) {
			return;
		}
		upload->requestId = 0;
		const auto storyId = StoryIdFromUpdates(result);
		const auto peer = upload->story.peer;
		_session->api().applyUpdates(result);
		auto &stories = _session->data().stories();
		if (stories.source(peer->id)) {
			finish(upload, storyId);
			return;
		}
		stories.requestPeerStories(peer, [=] {
			if (const auto upload = find(id)) {
				finish(upload, storyId);
			}
		});
	};
	const auto fail = [=](const MTP::Error &error) {
		const auto upload = find(id);
		if (!upload) {
			return;
		}
		upload->requestId = 0;
		const auto reposting = upload->story.media
			? std::get_if<SendStoryRepost>(&*upload->story.media)
			: nullptr;
		if (reposting
			&& !upload->refreshedReference
			&& error.type().startsWith(u"FILE_REFERENCE_"_q)) {
			upload->refreshedReference = true;
			_session->api().refreshFileReference(
				Data::FileOrigin(Data::FileOriginStory{
					.peer = reposting->peer->id,
					.story = reposting->id,
				}),
				[=](const auto &) {
					if (const auto upload = find(id)) {
						start(upload);
					}
				});
			return;
		}
		failed(upload, error.type());
	};
	const auto areas = MediaAreasToMTP(story);
	if (story.editId) {
		using Flag = MTPstories_EditStory::Flag;
		const auto flags = (media ? Flag::f_media : Flag())
			| ((media && !areas.v.isEmpty())
				? Flag::f_media_areas
				: Flag())
			| (story.editCaption ? Flag::f_caption : Flag())
			| (story.editPrivacy ? Flag::f_privacy_rules : Flag());
		upload->requestId = _api.request(MTPstories_EditStory(
			MTP_flags(flags),
			peer->input(),
			MTP_int(story.editId),
			media.value_or(MTPInputMedia()),
			areas,
			MTP_string(caption.text),
			entities,
			MTP_vector<MTPInputPrivacyRule>(rules),
			MTPInputDocument()
		)).done(done).fail(fail).send();
		return;
	}
	Assert(media.has_value());

	const auto repost = std::get_if<SendStoryRepost>(&*story.media);
	using Flag = MTPstories_SendStory::Flag;
	const auto flags = (caption.text.isEmpty() ? Flag() : Flag::f_caption)
		| (caption.entities.isEmpty() ? Flag() : Flag::f_entities)
		| (story.pinned ? Flag::f_pinned : Flag())
		| (story.noForwards ? Flag::f_noforwards : Flag())
		| Flag::f_period
		| (repost ? Flag::f_fwd_from_id : Flag())
		| ((repost && !sameMediaAsRepost) ? Flag::f_fwd_modified : Flag())
		| (story.albums.empty() ? Flag() : Flag::f_albums)
		| (areas.v.isEmpty() ? Flag() : Flag::f_media_areas);
	auto albums = QVector<MTPint>();
	albums.reserve(story.albums.size());
	for (const auto album : story.albums) {
		albums.push_back(MTP_int(album));
	}
	upload->requestId = _api.request(MTPstories_SendStory(
		MTP_flags(flags),
		peer->input(),
		*media,
		areas,
		MTP_string(caption.text),
		entities,
		MTP_vector<MTPInputPrivacyRule>(rules),
		MTP_long(base::RandomValue<uint64>()),
		MTP_int(story.period),
		(repost ? repost->peer->input() : MTPInputPeer()),
		MTP_int(repost ? repost->id : 0),
		MTP_vector<MTPint>(albums),
		MTPInputDocument()
	)).done(done).fail(fail).send();
}

void Stories::failed(not_null<Upload*> upload, const QString &error) {
	cancelWork(upload);
	auto &info = infoFor(upload);
	info.error = error.isEmpty() ? u"UNKNOWN_ERROR"_q : error;
	info.preparing = false;
	info.progress = 0.;
	notifyChanges();
	_sent.fire({
		.peer = upload->story.peer,
		.error = info.error,
		.edit = (upload->story.editId != 0),
	});
}

void Stories::finish(not_null<Upload*> upload, StoryId id) {
	const auto peer = upload->story.peer;
	const auto edit = (upload->story.editId != 0);
	auto sent = base::take(upload->story.sent);
	upload->story.failed = nullptr;
	auto expiresAt = TimeId(0);
	if (sent && !edit) {
		if (const auto story = _session->data().stories().lookup(
				{ peer->id, id })) {
			expiresAt = (*story)->expires();
		}
	}
	remove(upload->id);
	_sent.fire({ .peer = peer, .id = id, .edit = edit });
	if (sent) {
		sent(id, expiresAt);
	}
}

void Stories::cancelWork(not_null<Upload*> upload) {
	++upload->generation;
	if (const auto cancel = base::take(upload->cancelPreparing)) {
		cancel->store(true);
	}
	if (base::take(upload->uploading)) {
		_session->uploader().cancel(upload->uploadId);
	}
	if (const auto requestId = base::take(upload->requestId)) {
		_api.request(requestId).cancel();
	}
}

void Stories::removeTempFile(not_null<Upload*> upload) {
	if (const auto path = base::take(upload->tempPath); !path.isEmpty()) {
		QFile::remove(path);
	}
}

} // namespace Api
