/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_inner_widget.h"

#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/profile/info_profile_widget.h"
#include "info/profile/info_profile_icon.h"
#include "info/profile/info_profile_members.h"
#include "info/profile/info_profile_music_button.h"
#include "info/profile/info_profile_top_bar.h"
#include "info/profile/info_profile_actions.h"
#include "info/media/info_media_buttons.h"
#include "info/saved/info_saved_music_widget.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_file_origin.h"
#include "data/data_user.h"
#include "data/data_saved_music.h"
#include "data/data_saved_sublist.h"
#include "info/saved/info_saved_music_common.h"
#include "info_profile_actions.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "api/api_peer_photo.h"
#include "lang/lang_keys.h"
#include "ui/text/format_song_document_name.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/vertical_list.h"
#include "ui/ui_utility.h"
#include "styles/style_info.h"

namespace Info {
namespace Profile {

namespace {

void AddAboutVerification(
		not_null<Ui::VerticalLayout*> layout,
		not_null<PeerData*> peer) {
	const auto inner = layout->add(object_ptr<Ui::VerticalLayout>(layout));
	peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::VerifyInfo
	) | rpl::on_next([=] {
		const auto info = peer->botVerifyDetails();
		while (inner->count()) {
			delete inner->widgetAt(0);
		}
		if (!info) {
			Ui::AddDivider(inner);
		} else {
			auto hasMainApp = false;
			if (const auto user = peer->asUser()) {
				if (user->botInfo) {
					hasMainApp = user->botInfo->hasMainApp;
				}
			}
			if (!hasMainApp && !info->description.empty()) {
				Ui::AddDividerText(inner, rpl::single(info->description));
			}
		}
		inner->resizeToWidth(inner->width());
	}, inner->lifetime());
}

} // namespace

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
	) | rpl::on_next([this](int height) {
		if (!_inResize) {
			resizeToWidth(width());
			updateDesiredHeight();
		}
	}, lifetime());
}

rpl::producer<> InnerWidget::backRequest() const {
	return _backClicks.events();
}

object_ptr<Ui::RpWidget> InnerWidget::setupContent(
		not_null<RpWidget*> parent,
		Origin origin) {
	if (const auto user = _peer->asUser()) {
		user->session().changes().peerFlagsValue(
			user,
			Data::PeerUpdate::Flag::FullInfo
		) | rpl::on_next([=] {
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
	setupSavedMusic(result);
	if (_topic && _topic->creating()) {
		return result;
	}

	auto mainTracker = Ui::MultiSlideTracker();
	auto sharedTracker = Ui::MultiSlideTracker();
	auto dividerOverridden = rpl::variable<bool>(false);
	AddDetails(
		result,
		_controller,
		_peer,
		_topic,
		_sublist,
		origin,
		mainTracker,
		dividerOverridden);
	auto showDivider = rpl::combine(
		mainTracker.atLeastOneShownValue(),
		dividerOverridden.value()
	) | rpl::map([](bool main, bool dividerOverridden) {
		return dividerOverridden ? false : main;
	}) | rpl::distinct_until_changed();
	result->add(
		setupSharedMedia(
			result.data(),
			rpl::duplicate(showDivider),
			sharedTracker));
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
	auto showNext = rpl::combine(
		std::move(showDivider),
		sharedTracker.atLeastOneShownValue()
	) | rpl::map([](bool show, bool shared) {
		return show || shared;
	}) | rpl::distinct_until_changed();
	if (auto actions = SetupActions(_controller, result.data(), _peer)) {
		addAboutVerificationOrDivider(result, rpl::duplicate(showNext));
		result->add(std::move(actions));
	}
	if (!_aboutVerificationAdded) {
		AddAboutVerification(result, _peer);
	}
	if (_peer->isChat() || _peer->isMegagroup()) {
		if (!_peer->isMonoforum()) {
			setupMembers(result.data(), rpl::duplicate(showNext));
		}
	}
	return result;
}

void InnerWidget::setupMembers(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<bool> showDivider) {
	auto wrap = container->add(object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		container,
		object_ptr<Ui::VerticalLayout>(container)));
	const auto inner = wrap->entity();
	addAboutVerificationOrDivider(inner, std::move(showDivider));
	_members = inner->add(object_ptr<Members>(inner, _controller));
	_members->scrollToRequests(
	) | rpl::on_next([this](Ui::ScrollToRequest request) {
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
	_members->onlineCountValue(
	) | rpl::on_next([=](int count) {
		_onlineCount.fire_copy(count);
	}, _members->lifetime());

	using namespace rpl::mappers;
	wrap->toggleOn(
		_members->fullCountValue() | rpl::map(_1 > 0),
		anim::type::instant);
}

void InnerWidget::setupSavedMusic(not_null<Ui::VerticalLayout*> container) {
	Info::Saved::SetupSavedMusic(
		container,
		_controller,
		_sublist ? _sublist->sublistPeer() : _peer,
		_topBarColor.value());
}

void InnerWidget::addAboutVerificationOrDivider(
		not_null<Ui::VerticalLayout*> content,
		rpl::producer<bool> showDivider) {
	if (rpl::variable<bool>(rpl::duplicate(showDivider)).current()) {
		if (_aboutVerificationAdded) {
			Ui::AddDivider(content);
		} else {
			AddAboutVerification(content, _peer);
			_aboutVerificationAdded = true;
		}
	} else {
		const auto wrap = content->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				content,
				object_ptr<Ui::VerticalLayout>(content)));
		Ui::AddDivider(wrap->entity());
		wrap->setDuration(
			st::infoSlideDuration
		)->toggleOn(rpl::duplicate(showDivider));
	}
}

object_ptr<Ui::RpWidget> InnerWidget::setupSharedMedia(
		not_null<RpWidget*> parent,
		rpl::producer<bool> showDivider,
		Ui::MultiSlideTracker &sharedTracker) {
	using namespace rpl::mappers;
	using MediaType = Media::Type;

	const auto peer = _sublist ? _sublist->sublistPeer() : _peer;
	auto content = object_ptr<Ui::VerticalLayout>(parent);
	auto &tracker = sharedTracker;
	auto addMediaButton = [&](
			MediaType type,
			const style::icon &icon) {
		auto result = Media::AddButton(
			content,
			_controller,
			peer,
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
		addStoriesButton(peer, st::infoIconMediaStories);
		addPeerGiftsButton(peer, st::infoIconMediaGifts);
		addSavedSublistButton(peer, st::infoIconMediaSaved);
	}
	addMediaButton(MediaType::Photo, st::infoIconMediaPhoto);
	addMediaButton(MediaType::Video, st::infoIconMediaVideo);
	addMediaButton(MediaType::File, st::infoIconMediaFile);
	addMediaButton(MediaType::MusicFile, st::infoIconMediaAudio);
	addMediaButton(MediaType::Link, st::infoIconMediaLink);
	addMediaButton(MediaType::RoundVoiceFile, st::infoIconMediaVoice);
	addMediaButton(MediaType::GIF, st::infoIconMediaGif);
	if (const auto bot = peer->asBot()) {
		addCommonGroupsButton(bot, st::infoIconMediaGroup);
		addSimilarPeersButton(bot, st::infoIconMediaBot);
	} else if (const auto channel = peer->asBroadcast()) {
		addSimilarPeersButton(channel, st::infoIconMediaChannel);
	} else if (const auto user = peer->asUser()) {
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

	addAboutVerificationOrDivider(layout, std::move(showDivider));
	Ui::AddSkip(layout, st::infoSharedMediaBottomSkip);
	layout->add(std::move(content));
	Ui::AddSkip(layout, st::infoSharedMediaBottomSkip);

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

void InnerWidget::enableBackButton() {
	_backToggles.force_assign(true);
}

void InnerWidget::showFinished() {
	_showFinished.fire({});
}

bool InnerWidget::hasFlexibleTopBar() const {
	return true;
}

base::weak_qptr<Ui::RpWidget> InnerWidget::createPinnedToTop(
		not_null<Ui::RpWidget*> parent) {
	const auto content = Ui::CreateChild<TopBar>(
		parent,
		TopBar::Descriptor{
			.controller = _controller->parentController(),
			.key = _controller->key(),
			.wrap = _controller->wrapValue(),
			.peer = _sublist ? _sublist->sublistPeer().get() : nullptr,
			.backToggles = _backToggles.value(),
			.showFinished = _showFinished.events(),
		});
	content->backRequest(
	) | rpl::start_to_stream(_backClicks, content->lifetime());
	content->setOnlineCount(_onlineCount.events());
	_topBarColor = content->edgeColor();
	return base::make_weak(not_null<Ui::RpWidget*>{ content });
}

base::weak_qptr<Ui::RpWidget> InnerWidget::createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) {
	return nullptr;
}

} // namespace Profile
} // namespace Info
