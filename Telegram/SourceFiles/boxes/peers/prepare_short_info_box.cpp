/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/prepare_short_info_box.h"

#include "boxes/peers/peer_short_info_box.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_streaming.h"
#include "data/data_file_origin.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_peer_values.h"
#include "data/data_user_photos.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "info/profile/info_profile_values.h"
#include "ui/text/format_values.h"
#include "base/unixtime.h"
#include "lang/lang_keys.h"
#include "styles/style_info.h"

namespace {

constexpr auto kOverviewLimit = 48;

struct UserpicState {
	PeerShortInfoUserpic current;
	std::optional<UserPhotosSlice> userSlice;
	PhotoId userpicPhotoId = PeerData::kUnknownPhotoId;
	Ui::PeerUserpicView userpicView;
	std::shared_ptr<Data::PhotoMedia> photoView;
	std::vector<std::shared_ptr<Data::PhotoMedia>> photoPreloads;
	InMemoryKey userpicKey;
	PhotoId photoId = PeerData::kUnknownPhotoId;
	std::array<QImage, 4> roundMask;
	int size = 0;
	bool waitingFull = false;
	bool waitingLoad = false;
};

void GenerateImage(
		not_null<UserpicState*> state,
		QImage image,
		bool blurred = false) {
	using namespace Images;
	const auto size = state->size;
	const auto ratio = style::DevicePixelRatio();
	const auto options = blurred ? Option::Blur : Option();
	state->current.photo = Images::Round(
		Images::Prepare(
			std::move(image),
			QSize(size, size) * ratio,
			{ .options = options, .outer = { size, size } }),
		state->roundMask,
		RectPart::TopLeft | RectPart::TopRight);
}

void GenerateImage(
		not_null<UserpicState*> state,
		not_null<Image*> image,
		bool blurred = false) {
	GenerateImage(state, image->original(), blurred);
}

void ProcessUserpic(
		not_null<PeerData*> peer,
		not_null<UserpicState*> state) {
	state->current.videoDocument = nullptr;
	state->userpicKey = peer->userpicUniqueKey(state->userpicView);
	if (!state->userpicView.cloud) {
		GenerateImage(
			state,
			PeerData::GenerateUserpicImage(
				peer,
				state->userpicView,
				st::shortInfoWidth * style::DevicePixelRatio(),
				0),
			false);
		state->current.photoLoadingProgress = 1.;
		state->photoView = nullptr;
		return;
	}
	peer->loadUserpic();
	if (Ui::PeerUserpicLoading(state->userpicView)) {
		state->current.photoLoadingProgress = 0.;
		state->current.photo = QImage();
		state->waitingLoad = true;
		return;
	}
	GenerateImage(state, *state->userpicView.cloud, true);
	state->current.photoLoadingProgress = peer->userpicPhotoId() ? 0. : 1.;
	state->photoView = nullptr;
}

void Preload(
		not_null<PeerData*> peer,
		not_null<UserpicState*> state) {
	auto taken = base::take(state->photoPreloads);
	if (state->userSlice && state->userSlice->size() > 0) {
		const auto preload = [&](int index) {
			const auto photo = peer->owner().photo(
				(*state->userSlice)[index]);
			const auto current = (peer->userpicPhotoId() == photo->id);
			const auto origin = current
				? peer->userpicPhotoOrigin()
				: Data::FileOriginUserPhoto(peerToUser(peer->id), photo->id);
			state->photoPreloads.push_back(photo->createMediaView());
			if (photo->hasVideo()) {
				state->photoPreloads.back()->videoWanted(
					Data::PhotoSize::Large,
					origin);
			} else {
				state->photoPreloads.back()->wanted(
					Data::PhotoSize::Large,
					origin);
			}
		};
		const auto skip = (state->userSlice->size() == state->current.count)
			? 0
			: 1;
		if (state->current.index - skip > 0) {
			preload(state->current.index - skip - 1);
		} else if (!state->current.index && state->current.count > 1) {
			preload(state->userSlice->size() - 1);
		}
		if (state->current.index - skip + 1 < state->userSlice->size()) {
			preload(state->current.index - skip + 1);
		} else if (!skip && state->current.index > 0) {
			preload(0);
		}
	}
}

void ProcessFullPhoto(
		not_null<PeerData*> peer,
		not_null<UserpicState*> state,
		not_null<PhotoData*> photo) {
	using PhotoSize = Data::PhotoSize;
	const auto current = (peer->userpicPhotoId() == photo->id);
	const auto video = photo->hasVideo();
	const auto originCurrent = peer->userpicPhotoOrigin();
	const auto originOther = peer->isUser()
		? Data::FileOriginUserPhoto(peerToUser(peer->id), photo->id)
		: originCurrent;
	const auto origin = current ? originCurrent : originOther;
	const auto was = base::take(state->current.videoDocument);
	const auto view = photo->createMediaView();
	if (!video) {
		view->wanted(PhotoSize::Large, origin);
	}
	if (const auto image = view->image(PhotoSize::Large)) {
		GenerateImage(state, image);
		Preload(peer, state);
		state->photoView = nullptr;
		state->current.photoLoadingProgress = 1.;
	} else {
		if (const auto thumbnail = view->image(PhotoSize::Thumbnail)) {
			GenerateImage(state, thumbnail, true);
		} else if (const auto small = view->image(PhotoSize::Small)) {
			GenerateImage(state, small, true);
		} else {
			if (current) {
				ProcessUserpic(peer, state);
			}
			if (!current || state->current.photo.isNull()) {
				if (const auto blurred = view->thumbnailInline()) {
					GenerateImage(state, blurred, true);
				} else {
					state->current.photo = QImage();
				}
			}
		}
		state->waitingLoad = !video;
		state->photoView = view;
		state->current.photoLoadingProgress = photo->progress();
	}
	if (!video) {
		return;
	}
	state->current.videoDocument = peer->owner().streaming().sharedDocument(
		photo,
		origin);
	state->current.videoStartPosition = photo->videoStartPosition();
	state->photoView = nullptr;
	state->current.photoLoadingProgress = 1.;
}

} // namespace

[[nodiscard]] rpl::producer<PeerShortInfoFields> FieldsValue(
		not_null<PeerData*> peer) {
	using UpdateFlag = Data::PeerUpdate::Flag;
	return peer->session().changes().peerFlagsValue(
		peer,
		(UpdateFlag::Name
			| UpdateFlag::PhoneNumber
			| UpdateFlag::Username
			| UpdateFlag::About
			| UpdateFlag::Birthday)
	) | rpl::map([=] {
		const auto user = peer->asUser();
		const auto username = peer->username();
		return PeerShortInfoFields{
			.name = peer->name(),
			.phone = user ? Ui::FormatPhone(user->phone()) : QString(),
			.link = ((user || username.isEmpty())
				? QString()
				: peer->session().createInternalLinkFull(username)),
			.about = Info::Profile::AboutWithEntities(peer, peer->about()),
			.username = ((user && !username.isEmpty())
				? ('@' + username)
				: QString()),
			.birthday = user ? user->birthday() : Data::Birthday(),
			.isBio = (user && !user->isBot()),
		};
	});
}

[[nodiscard]] rpl::producer<QString> StatusValue(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		const auto now = base::unixtime::now();
		return [=](auto consumer) {
			auto lifetime = rpl::lifetime();
			const auto timer = lifetime.make_state<base::Timer>();
			const auto push = [=] {
				consumer.put_next(Data::OnlineText(user, now));
				timer->callOnce(Data::OnlineChangeTimeout(user, now));
			};
			timer->setCallback(push);
			push();
			return lifetime;
		};
	}
	return peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::Members
	) | rpl::map([=] {
		const auto chat = peer->asChat();
		const auto channel = peer->asChannel();
		const auto count = std::max({
			chat ? chat->count : channel->membersCount(),
			chat ? int(chat->participants.size()) : 0,
			0,
		});
		return (chat && !chat->amIn())
			? tr::lng_chat_status_unaccessible(tr::now)
			: (count > 0)
			? ((channel && channel->isBroadcast())
				? tr::lng_chat_status_subscribers(
					tr::now,
					lt_count_decimal,
					count)
				: tr::lng_chat_status_members(
					tr::now,
					lt_count_decimal,
					count))
			: ((channel && channel->isBroadcast())
				? tr::lng_channel_status(tr::now)
				: tr::lng_group_status(tr::now));
	});
}

void ValidatePhotoId(
		not_null<UserpicState*> state,
		PhotoId oldUserpicPhotoId) {
	if (state->userSlice) {
		const auto count = state->userSlice->size();
		const auto hasOld = state->userSlice->indexOf(
			oldUserpicPhotoId).has_value();
		const auto hasNew = state->userSlice->indexOf(
			state->userpicPhotoId).has_value();
		const auto shift = (hasNew ? 0 : 1);
		const auto fullCount = count + shift;
		state->current.count = fullCount;
		if (hasOld && !hasNew && state->current.index + 1 < fullCount) {
			++state->current.index;
		} else if (!hasOld && hasNew && state->current.index > 0) {
			--state->current.index;
		}
		const auto index = state->current.index;
		if (!index || index >= fullCount) {
			state->current.index = 0;
			state->photoId = state->userpicPhotoId;
		} else {
			state->photoId = (*state->userSlice)[index - shift];
		}
	} else {
		state->photoId = state->userpicPhotoId;
	}
}

bool ProcessCurrent(
		not_null<PeerData*> peer,
		not_null<UserpicState*> state) {
	const auto userpicPhotoId = peer->userpicPhotoId();
	const auto userpicPhoto = (userpicPhotoId
		&& (userpicPhotoId != PeerData::kUnknownPhotoId)
		&& (state->userpicPhotoId != userpicPhotoId))
		? peer->owner().photo(userpicPhotoId).get()
		: (state->photoId == userpicPhotoId && state->photoView)
		? state->photoView->owner().get()
		: nullptr;
	state->waitingFull = (state->userpicPhotoId != userpicPhotoId)
		&& ((userpicPhotoId == PeerData::kUnknownPhotoId)
			|| (userpicPhotoId && userpicPhoto->isNull()));
	if (state->waitingFull) {
		peer->updateFullForced();
	}
	const auto oldUserpicPhotoId = state->waitingFull
		? state->userpicPhotoId
		: std::exchange(state->userpicPhotoId, userpicPhotoId);
	const auto changedUserpic = (state->userpicKey
		!= peer->userpicUniqueKey(state->userpicView));

	const auto wasIndex = state->current.index;
	const auto wasCount = state->current.count;
	const auto wasPhotoId = state->photoId;
	ValidatePhotoId(state, oldUserpicPhotoId);
	const auto changedInSlice = (state->current.index != wasIndex)
		|| (state->current.count != wasCount);
	const auto changedPhotoId = (state->photoId != wasPhotoId);
	const auto photo = (state->photoId == state->userpicPhotoId
		&& userpicPhoto)
		? userpicPhoto
		: (state->photoId
			&& (state->photoId != PeerData::kUnknownPhotoId)
			&& changedPhotoId)
		? peer->owner().photo(state->photoId).get()
		: state->photoView
		? state->photoView->owner().get()
		: nullptr;
	state->current.additionalStatus = (!peer->isUser())
		? QString()
		: ((state->photoId == userpicPhotoId)
			&& peer->asUser()->hasPersonalPhoto())
		? tr::lng_profile_photo_by_you(tr::now)
		: ((state->current.index == (state->current.count - 1))
			&& SyncUserFallbackPhotoViewer(peer->asUser()))
		? tr::lng_profile_public_photo(tr::now)
		: QString();
	state->waitingLoad = false;
	if (!changedPhotoId
		&& (state->current.index > 0 || !changedUserpic)
		&& !state->photoView
		&& (!state->current.photo.isNull()
			|| state->current.videoDocument)) {
		return changedInSlice;
	} else if (photo && !photo->isNull()) {
		ProcessFullPhoto(peer, state, photo);
	} else if (state->current.index > 0) {
		return changedInSlice;
	} else {
		ProcessUserpic(peer, state);
	}
	return true;
}

[[nodiscard]] PreparedShortInfoUserpic UserpicValue(
		not_null<PeerData*> peer,
		const style::ShortInfoCover &st,
		rpl::producer<UserPhotosSlice> slices,
		Fn<bool(not_null<UserpicState*>)> customProcess) {
	const auto moveRequests = std::make_shared<rpl::event_stream<int>>();
	auto move = [=](int shift) {
		moveRequests->fire_copy(shift);
	};
	const auto size = st.size;
	const auto radius = st.radius;
	auto value = [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		const auto state = lifetime.make_state<UserpicState>();
		state->size = size;
		state->roundMask = Images::CornersMask(radius);
		const auto push = [=](bool force = false) {
			if (customProcess(state) || force) {
				consumer.put_next_copy(state->current);
			}
		};
		using UpdateFlag = Data::PeerUpdate::Flag;
		peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Photo | UpdateFlag::FullInfo
		) | rpl::filter([=](const Data::PeerUpdate &update) {
			return (update.flags & UpdateFlag::Photo) || state->waitingFull;
		}) | rpl::start_with_next([=] {
			push();
		}, lifetime);

		rpl::duplicate(
			slices
		) | rpl::start_with_next([=](UserPhotosSlice &&slice) {
			state->userSlice = std::move(slice);
			push();
		}, lifetime);

		moveRequests->events(
		) | rpl::filter([=] {
			return (state->current.count > 1);
		}) | rpl::start_with_next([=](int shift) {
			state->current.index = std::clamp(
				((state->current.index + shift + state->current.count)
					% state->current.count),
				0,
				state->current.count - 1);
			push(true);
		}, lifetime);

		peer->session().downloaderTaskFinished(
		) | rpl::filter([=] {
			return state->waitingLoad
				&& (state->photoView
					? (!!state->photoView->image(Data::PhotoSize::Large))
					: (!Ui::PeerUserpicLoading(state->userpicView)));
		}) | rpl::start_with_next([=] {
			push();
		}, lifetime);

		return lifetime;
	};
	return { .value = std::move(value), .move = std::move(move) };
}

object_ptr<Ui::BoxContent> PrepareShortInfoBox(
		not_null<PeerData*> peer,
		Fn<void()> open,
		Fn<bool()> videoPaused,
		const style::ShortInfoBox *stOverride) {
	const auto type = peer->isSelf()
		? PeerShortInfoType::Self
		: peer->isUser()
		? PeerShortInfoType::User
		: peer->isBroadcast()
		? PeerShortInfoType::Channel
		: PeerShortInfoType::Group;
	auto userpic = PrepareShortInfoUserpic(peer, st::shortInfoCover);
	auto result = Box<PeerShortInfoBox>(
		type,
		FieldsValue(peer),
		StatusValue(peer),
		std::move(userpic.value),
		std::move(videoPaused),
		stOverride);

	result->openRequests(
	) | rpl::start_with_next(open, result->lifetime());

	result->moveRequests(
	) | rpl::start_with_next(userpic.move, result->lifetime());

	return result;
}

object_ptr<Ui::BoxContent> PrepareShortInfoBox(
		not_null<PeerData*> peer,
		not_null<Window::SessionNavigation*> navigation,
		const style::ShortInfoBox *stOverride) {
	const auto open = [=] { navigation->showPeerHistory(peer); };
	const auto videoIsPaused = [=] {
		return navigation->parentController()->isGifPausedAtLeastFor(
			Window::GifPauseReason::Layer);
	};
	return PrepareShortInfoBox(
		peer,
		open,
		videoIsPaused,
		stOverride);
}

rpl::producer<QString> PrepareShortInfoStatus(not_null<PeerData*> peer) {
	return StatusValue(peer);
}

PreparedShortInfoUserpic PrepareShortInfoUserpic(
		not_null<PeerData*> peer,
		const style::ShortInfoCover &st) {
	auto slices = peer->isUser()
		? UserPhotosReversedViewer(
			&peer->session(),
			UserPhotosSlice::Key(peerToUser(peer->asUser()->id), PhotoId()),
			kOverviewLimit,
			kOverviewLimit)
		: rpl::never<UserPhotosSlice>();
	auto process = [=](not_null<UserpicState*> state) {
		return ProcessCurrent(peer, state);
	};
	return UserpicValue(peer, st, std::move(slices), std::move(process));
}

PreparedShortInfoUserpic PrepareShortInfoFallbackUserpic(
		not_null<PeerData*> peer,
		const style::ShortInfoCover &st) {
	Expects(peer->isUser());

	const auto photoId = SyncUserFallbackPhotoViewer(peer->asUser());
	auto slices = photoId
		? rpl::single<UserPhotosSlice>(UserPhotosSlice(
			Storage::UserPhotosKey(peerToUser(peer->id), *photoId),
			std::deque<PhotoId>({ *photoId }),
			1,
			1,
			1))
		: (rpl::never<UserPhotosSlice>() | rpl::type_erased());
	auto process = [=](not_null<UserpicState*> state) {
		if (photoId) {
			ProcessFullPhoto(peer, state, peer->owner().photo(*photoId));
			return true;
		}
		return false;
	};
	return UserpicValue(peer, st, std::move(slices), std::move(process));
}
