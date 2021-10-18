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

struct UserpicState {
	PeerShortInfoUserpic current;
	std::shared_ptr<Data::CloudImageView> userpicView;
	std::shared_ptr<Data::PhotoMedia> photoView;
	InMemoryKey userpicKey;
	PhotoId photoId = 0;
	bool waitingFull = false;
	bool waitingLoad = false;
};

void GenerateImage(
		not_null<UserpicState*> state,
		QImage image,
		bool blurred = false) {
	using namespace Images;
	const auto size = st::shortInfoWidth;
	const auto factor = style::DevicePixelRatio();
	const auto options = Option::Smooth
		| Option::RoundedSmall
		| Option::RoundedTopLeft
		| Option::RoundedTopRight
		| (blurred ? Option::Blurred : Option());
	state->current.photo = Images::prepare(
		std::move(image),
		size * factor,
		size * factor,
		options,
		size,
		size);
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
	if (!state->userpicView) {
		GenerateImage(
			state,
			peer->generateUserpicImage(
				state->userpicView,
				st::shortInfoWidth * style::DevicePixelRatio(),
				ImageRoundRadius::None),
			false);
		state->photoView = nullptr;
		return;
	}
	peer->loadUserpic();
	state->current.photoLoadingProgress = 0.;
	const auto image = state->userpicView->image();
	if (!image) {
		state->current.photo = QImage();
		state->waitingLoad = true;
		return;
	}
	GenerateImage(state, image, true);
	state->photoView = nullptr;
}

void ProcessFullPhoto(
		not_null<PeerData*> peer,
		not_null<UserpicState*> state,
		not_null<PhotoData*> photo) {
	using PhotoSize = Data::PhotoSize;
	const auto video = photo->hasVideo();
	const auto origin = peer->userpicPhotoOrigin();
	const auto was = base::take(state->current.videoDocument);
	const auto view = photo->createMediaView();
	if (!video) {
		view->wanted(PhotoSize::Large, origin);
	}
	if (const auto image = view->image(PhotoSize::Large)) {
		GenerateImage(state, image);
		state->photoView = nullptr;
		state->current.photoLoadingProgress = 1.;
	} else {
		if (const auto thumbnail = view->image(PhotoSize::Thumbnail)) {
			GenerateImage(state, thumbnail, true);
		} else if (const auto small = view->image(PhotoSize::Small)) {
			GenerateImage(state, small, true);
		} else {
			ProcessUserpic(peer, state);
			if (state->current.photo.isNull()) {
				if (const auto blurred = view->thumbnailInline()) {
					GenerateImage(state, blurred, true);
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
			| UpdateFlag::About)
	) | rpl::map([=] {
		const auto user = peer->asUser();
		const auto username = peer->userName();
		return PeerShortInfoFields{
			.name = peer->name,
			.phone = user ? Ui::FormatPhone(user->phone()) : QString(),
			.link = ((user || username.isEmpty())
				? QString()
				: peer->session().createInternalLinkFull(username)),
			.about = Info::Profile::AboutWithEntities(peer, peer->about()),
			.username = ((user && !username.isEmpty())
				? ('@' + username)
				: QString()),
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

[[nodiscard]] rpl::producer<PeerShortInfoUserpic> UserpicValue(
		not_null<PeerData*> peer) {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		const auto state = lifetime.make_state<UserpicState>();
		const auto push = [=] {
			state->waitingLoad = false;
			const auto nowPhotoId = peer->userpicPhotoId();
			const auto photo = (nowPhotoId
				&& (nowPhotoId != PeerData::kUnknownPhotoId)
				&& (state->photoId != nowPhotoId || state->photoView))
				? peer->owner().photo(nowPhotoId).get()
				: nullptr;
			state->waitingFull = !(state->photoId == nowPhotoId)
				&& ((nowPhotoId == PeerData::kUnknownPhotoId)
					|| (nowPhotoId && photo->isNull()));
			if (state->waitingFull) {
				peer->updateFullForced();
			}
			const auto oldPhotoId = state->waitingFull
				? state->photoId
				: std::exchange(state->photoId, nowPhotoId);
			const auto changedPhotoId = (state->photoId != oldPhotoId);
			const auto changedUserpic = (state->userpicKey
				!= peer->userpicUniqueKey(state->userpicView));
			if (!changedPhotoId
				&& !changedUserpic
				&& !state->photoView
				&& (!state->current.photo.isNull()
					|| state->current.videoDocument)) {
				return;
			} else if (photo && !photo->isNull()) {
				ProcessFullPhoto(peer, state, photo);
			} else {
				ProcessUserpic(peer, state);
			}
			consumer.put_next_copy(state->current);
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

		peer->session().downloaderTaskFinished(
		) | rpl::filter([=] {
			return state->waitingLoad
				&& (state->photoView
					? (!!state->photoView->image(Data::PhotoSize::Large))
					: (state->userpicView && state->userpicView->image()));
		}) | rpl::start_with_next([=] {
			push();
		}, lifetime);

		return lifetime;
	};
}

object_ptr<Ui::BoxContent> PrepareShortInfoBox(
		not_null<PeerData*> peer,
		Fn<void()> open,
		Fn<bool()> videoPaused) {
	const auto type = peer->isUser()
		? PeerShortInfoType::User
		: peer->isBroadcast()
		? PeerShortInfoType::Channel
		: PeerShortInfoType::Group;
	auto result = Box<PeerShortInfoBox>(
		type,
		FieldsValue(peer),
		StatusValue(peer),
		UserpicValue(peer),
		std::move(videoPaused));

	result->openRequests(
	) | rpl::start_with_next(open, result->lifetime());

	return result;
}

object_ptr<Ui::BoxContent> PrepareShortInfoBox(
		not_null<PeerData*> peer,
		not_null<Window::SessionNavigation*> navigation) {
	const auto open = [=] { navigation->showPeerHistory(peer); };
	const auto videoIsPaused = [=] {
		return navigation->parentController()->isGifPausedAtLeastFor(
			Window::GifPauseReason::Layer);
	};
	return PrepareShortInfoBox(
		peer,
		open,
		videoIsPaused);
}
