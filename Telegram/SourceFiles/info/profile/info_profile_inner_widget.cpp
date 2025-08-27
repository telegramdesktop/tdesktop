/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_inner_widget.h"

#include "info/info_controller.h"
#include "info/profile/info_profile_widget.h"
#include "info/profile/info_profile_cover.h"
#include "info/profile/info_profile_icon.h"
#include "info/profile/info_profile_members.h"
#include "info/profile/info_profile_actions.h"
#include "info/media/info_media_buttons.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_file_origin.h"
#include "data/data_user.h"
#include "data/data_saved_sublist.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "api/api_peer_photo.h"
#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/ui_utility.h"
#include "styles/style_info.h"

namespace Info {
namespace Profile {

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller,
	Origin origin)
: RpWidget(parent)
, _controller(controller)
, _peer(_controller->key().peer())
, _migrated(_controller->migrated())
, _topic(_controller->key().topic())
, _sublist(_controller->key().sublist())
, _content(setupContent(this, origin)) {
	_content->heightValue(
	) | rpl::start_with_next([this](int height) {
		if (!_inResize) {
			resizeToWidth(width());
			updateDesiredHeight();
		}
	}, lifetime());
}

object_ptr<Ui::RpWidget> InnerWidget::setupContent(
		not_null<RpWidget*> parent,
		Origin origin) {
	if (const auto user = _peer->asUser()) {
		user->session().changes().peerFlagsValue(
			user,
			Data::PeerUpdate::Flag::FullInfo
		) | rpl::start_with_next([=] {
			auto &photos = user->session().api().peerPhoto();
			if (const auto original = photos.nonPersonalPhoto(user)) {
				// Preload it for the edit contact box.
				_nonPersonalView = original->createMediaView();
				const auto id = peerToUser(user->id);
				original->load(Data::FileOriginFullUser{ id });
			}
		}, lifetime());
	}

	auto result = object_ptr<Ui::VerticalLayout>(parent);
	_cover = AddCover(result, _controller, _peer, _topic, _sublist);
	if (_topic && _topic->creating()) {
		return result;
	}

	AddDetails(result, _controller, _peer, _topic, _sublist, origin);
	result->add(setupSharedMedia(result.data()));
	if (_topic || _sublist) {
		return result;
	}
	{
		auto buttons = SetupChannelMembersAndManage(
			_controller,
			result.data(),
			_peer);
		if (buttons) {
			result->add(std::move(buttons));
		}
	}
	if (auto actions = SetupActions(_controller, result.data(), _peer)) {
		result->add(object_ptr<Ui::BoxContentDivider>(result));
		result->add(std::move(actions));
	}
	if (_peer->isChat() || _peer->isMegagroup()) {
		if (!_peer->isMonoforum()) {
			setupMembers(result.data());
		}
	}
	return result;
}

void InnerWidget::setupMembers(not_null<Ui::VerticalLayout*> container) {
	auto wrap = container->add(object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		container,
		object_ptr<Ui::VerticalLayout>(container)));
	const auto inner = wrap->entity();
	inner->add(object_ptr<Ui::BoxContentDivider>(inner));
	_members = inner->add(object_ptr<Members>(inner, _controller));
	_members->scrollToRequests(
	) | rpl::start_with_next([this](Ui::ScrollToRequest request) {
		auto min = (request.ymin < 0)
			? request.ymin
			: MapFrom(this, _members, QPoint(0, request.ymin)).y();
		auto max = (request.ymin < 0)
			? MapFrom(this, _members, QPoint()).y()
			: (request.ymax < 0)
			? request.ymax
			: MapFrom(this, _members, QPoint(0, request.ymax)).y();
		_scrollToRequests.fire({ min, max });
	}, _members->lifetime());
	_cover->setOnlineCount(_members->onlineCountValue());

	using namespace rpl::mappers;
	wrap->toggleOn(
		_members->fullCountValue() | rpl::map(_1 > 0),
		anim::type::instant);
}

object_ptr<Ui::RpWidget> InnerWidget::setupSharedMedia(
		not_null<RpWidget*> parent) {
	using namespace rpl::mappers;
	using MediaType = Media::Type;

	auto content = object_ptr<Ui::VerticalLayout>(parent);
	auto tracker = Ui::MultiSlideTracker();
	auto addMediaButton = [&](
			MediaType type,
			const style::icon &icon) {
		auto result = Media::AddButton(
			content,
			_controller,
			_peer,
			_topic ? _topic->rootId() : MsgId(),
			_sublist ? _sublist->sublistPeer()->id : PeerId(),
			_migrated,
			type,
			tracker);
		object_ptr<Profile::FloatingIcon>(
			result,
			icon,
			st::infoSharedMediaButtonIconPosition);
	};
	auto addCommonGroupsButton = [&](
			not_null<UserData*> user,
			const style::icon &icon) {
		auto result = Media::AddCommonGroupsButton(
			content,
			_controller,
			user,
			tracker);
		object_ptr<Profile::FloatingIcon>(
			result,
			icon,
			st::infoSharedMediaButtonIconPosition);
	};
	const auto addSimilarPeersButton = [&](
			not_null<PeerData*> peer,
			const style::icon &icon) {
		auto result = Media::AddSimilarPeersButton(
			content,
			_controller,
			peer,
			tracker);
		object_ptr<Profile::FloatingIcon>(
			result,
			icon,
			st::infoSharedMediaButtonIconPosition);
	};
	auto addStoriesButton = [&](
			not_null<PeerData*> peer,
			const style::icon &icon) {
		if (peer->isChat()) {
			return;
		}
		auto result = Media::AddStoriesButton(
			content,
			_controller,
			peer,
			tracker);
		object_ptr<Profile::FloatingIcon>(
			result,
			icon,
			st::infoSharedMediaButtonIconPosition);
	};
	auto addSavedSublistButton = [&](
			not_null<PeerData*> peer,
			const style::icon &icon) {
		auto result = Media::AddSavedSublistButton(
			content,
			_controller,
			peer,
			tracker);
		object_ptr<Profile::FloatingIcon>(
			result,
			icon,
			st::infoSharedMediaButtonIconPosition);
	};
	auto addPeerGiftsButton = [&](
			not_null<PeerData*> peer,
			const style::icon &icon) {
		auto result = Media::AddPeerGiftsButton(
			content,
			_controller,
			peer,
			tracker);
		object_ptr<Profile::FloatingIcon>(
			result,
			icon,
			st::infoSharedMediaButtonIconPosition);
	};

	if (!_topic) {
		addStoriesButton(_peer, st::infoIconMediaStories);
		addPeerGiftsButton(_peer, st::infoIconMediaGifts);
		addSavedSublistButton(_peer, st::infoIconMediaSaved);
	}
	addMediaButton(MediaType::Photo, st::infoIconMediaPhoto);
	addMediaButton(MediaType::Video, st::infoIconMediaVideo);
	addMediaButton(MediaType::File, st::infoIconMediaFile);
	addMediaButton(MediaType::MusicFile, st::infoIconMediaAudio);
	addMediaButton(MediaType::Link, st::infoIconMediaLink);
	addMediaButton(MediaType::RoundVoiceFile, st::infoIconMediaVoice);
	addMediaButton(MediaType::GIF, st::infoIconMediaGif);
	if (const auto bot = _peer->asBot()) {
		addCommonGroupsButton(bot, st::infoIconMediaGroup);
		addSimilarPeersButton(bot, st::infoIconMediaBot);
	} else if (const auto channel = _peer->asBroadcast()) {
		addSimilarPeersButton(channel, st::infoIconMediaChannel);
	} else if (const auto user = _peer->asUser()) {
		addCommonGroupsButton(user, st::infoIconMediaGroup);
	}

	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent)
	);

	result->setDuration(
		st::infoSlideDuration
	)->toggleOn(
		tracker.atLeastOneShownValue()
	);

	auto layout = result->entity();

	layout->add(object_ptr<Ui::BoxContentDivider>(layout));
	layout->add(object_ptr<Ui::FixedHeightWidget>(
		layout,
		st::infoSharedMediaBottomSkip)
	)->setAttribute(Qt::WA_TransparentForMouseEvents);
	layout->add(std::move(content));
	layout->add(object_ptr<Ui::FixedHeightWidget>(
		layout,
		st::infoSharedMediaBottomSkip)
	)->setAttribute(Qt::WA_TransparentForMouseEvents);

	_sharedMediaWrap = result;
	return result;
}

int InnerWidget::countDesiredHeight() const {
	return _content->height() + (_members
		? (_members->desiredHeight() - _members->height())
		: 0);
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	setChildVisibleTopBottom(_content, visibleTop, visibleBottom);
}

void InnerWidget::saveState(not_null<Memento*> memento) {
	if (_members) {
		memento->setMembersState(_members->saveState());
	}
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	if (_members) {
		_members->restoreState(memento->membersState());
	}
	if (_sharedMediaWrap) {
		_sharedMediaWrap->finishAnimating();
	}
}

rpl::producer<Ui::ScrollToRequest> InnerWidget::scrollToRequests() const {
	return _scrollToRequests.events();
}

rpl::producer<int> InnerWidget::desiredHeightValue() const {
	return _desiredHeight.events_starting_with(countDesiredHeight());
}

int InnerWidget::resizeGetHeight(int newWidth) {
	_inResize = true;
	auto guard = gsl::finally([&] { _inResize = false; });

	_content->resizeToWidth(newWidth);
	_content->moveToLeft(0, 0);
	updateDesiredHeight();
	return _content->heightNoMargins();
}

} // namespace Profile
} // namespace Info
