/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_inner_widget.h"

#include <rpl/combine.h>
#include <rpl/combine_previous.h>
#include <rpl/flatten_latest.h>
#include "info/info_memento.h"
#include "info/info_controller.h"
#include "info/profile/info_profile_widget.h"
#include "info/profile/info_profile_text.h"
#include "info/profile/info_profile_values.h"
#include "info/profile/info_profile_cover.h"
#include "info/profile/info_profile_icon.h"
#include "info/profile/info_profile_members.h"
#include "info/profile/info_profile_actions.h"
#include "info/media/info_media_buttons.h"
#include "boxes/abstract_box.h"
#include "boxes/add_contact_box.h"
#include "boxes/confirm_box.h"
#include "boxes/report_box.h"
#include "mainwidget.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "window/main_window.h"
#include "window/window_session_controller.h"
#include "storage/storage_shared_media.h"
#include "lang/lang_keys.h"
#include "styles/style_info.h"
#include "styles/style_boxes.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "data/data_shared_media.h"

namespace Info {
namespace Profile {

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller)
: RpWidget(parent)
, _controller(controller)
, _peer(_controller->key().peer())
, _migrated(_controller->migrated())
, _content(setupContent(this)) {
	_content->heightValue(
	) | rpl::start_with_next([this](int height) {
		if (!_inResize) {
			resizeToWidth(width());
			updateDesiredHeight();
		}
	}, lifetime());
}

bool InnerWidget::canHideDetailsEver() const {
	return false;// (_peer->isChat() || _peer->isMegagroup());
}

rpl::producer<bool> InnerWidget::canHideDetails() const {
	using namespace rpl::mappers;
	return MembersCountValue(_peer)
		| rpl::map(_1 > 0);
}

object_ptr<Ui::RpWidget> InnerWidget::setupContent(
		not_null<RpWidget*> parent) {
	auto result = object_ptr<Ui::VerticalLayout>(parent);
	_cover = result->add(object_ptr<Cover>(
		result,
		_peer,
		_controller->parentController()));
	_cover->showSection(
	) | rpl::start_with_next([=](Section section) {
		_controller->showSection(
			std::make_shared<Info::Memento>(_peer, section));
	}, _cover->lifetime());
	_cover->setOnlineCount(rpl::single(0));
	auto details = SetupDetails(_controller, parent, _peer);
	if (canHideDetailsEver()) {
		_cover->setToggleShown(canHideDetails());
		_infoWrap = result->add(object_ptr<Ui::SlideWrap<>>(
			result,
			std::move(details))
		)->setDuration(
			st::infoSlideDuration
		)->toggleOn(
			_cover->toggledValue()
		);
	} else {
		result->add(std::move(details));
	}
	result->add(setupSharedMedia(result.data()));
	if (auto members = SetupChannelMembers(_controller, result.data(), _peer)) {
		result->add(std::move(members));
	}
	result->add(object_ptr<Ui::BoxContentDivider>(result));
	if (auto actions = SetupActions(_controller, result.data(), _peer)) {
		result->add(std::move(actions));
	}

	if (_peer->isChat() || _peer->isMegagroup()) {
		_members = result->add(object_ptr<Members>(
			result,
			_controller));
		_members->scrollToRequests(
		) | rpl::start_with_next([this](Ui::ScrollToRequest request) {
			auto min = (request.ymin < 0)
				? request.ymin
				: mapFromGlobal(_members->mapToGlobal({ 0, request.ymin })).y();
			auto max = (request.ymin < 0)
				? mapFromGlobal(_members->mapToGlobal({ 0, 0 })).y()
				: (request.ymax < 0)
				? request.ymax
				: mapFromGlobal(_members->mapToGlobal({ 0, request.ymax })).y();
			_scrollToRequests.fire({ min, max });
		}, _members->lifetime());
		_cover->setOnlineCount(_members->onlineCountValue());
	}
	return result;
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

	addMediaButton(MediaType::Photo, st::infoIconMediaPhoto);
	addMediaButton(MediaType::Video, st::infoIconMediaVideo);
	addMediaButton(MediaType::File, st::infoIconMediaFile);
	addMediaButton(MediaType::MusicFile, st::infoIconMediaAudio);
	addMediaButton(MediaType::Link, st::infoIconMediaLink);
	addMediaButton(MediaType::RoundVoiceFile, st::infoIconMediaVoice);
	if (auto user = _peer->asUser()) {
		addCommonGroupsButton(user, st::infoIconMediaGroup);
	}

	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent)
	);

	// Allows removing shared media links in third column.
	// Was done for tabs support.
	//
	//using ToggledData = std::tuple<bool, Wrap, bool>;
	//rpl::combine(
	//	tracker.atLeastOneShownValue(),
	//	_controller->wrapValue(),
	//	_isStackBottom.value()
	//) | rpl::combine_previous(
	//	ToggledData()
	//) | rpl::start_with_next([wrap = result.data()](
	//		const ToggledData &was,
	//		const ToggledData &now) {
	//	bool wasOneShown, wasStackBottom, nowOneShown, nowStackBottom;
	//	Wrap wasWrap, nowWrap;
	//	std::tie(wasOneShown, wasWrap, wasStackBottom) = was;
	//	std::tie(nowOneShown, nowWrap, nowStackBottom) = now;
	//	// MSVC Internal Compiler Error
	//	//auto [wasOneShown, wasWrap, wasStackBottom] = was;
	//	//auto [nowOneShown, nowWrap, nowStackBottom] = now;
	//	wrap->toggle(
	//		nowOneShown && (nowWrap != Wrap::Side || !nowStackBottom),
	//		(wasStackBottom == nowStackBottom && wasWrap == nowWrap)
	//			? anim::type::normal
	//			: anim::type::instant);
	//}, result->lifetime());
	//
	// Using that instead
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
	memento->setInfoExpanded(_cover->toggled());
	if (_members) {
		memento->setMembersState(_members->saveState());
	}
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	_cover->toggle(memento->infoExpanded(), anim::type::instant);
	if (_members) {
		_members->restoreState(memento->membersState());
	}
	if (_infoWrap) {
		_infoWrap->finishAnimating();
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
