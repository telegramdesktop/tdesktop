/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "info/info_profile_inner_widget.h"

#include <rpl/combine.h>
#include <rpl/range.h>
#include <rpl/then.h>
#include "boxes/abstract_box.h"
#include "boxes/add_contact_box.h"
#include "mainwidget.h"
#include "info/info_profile_widget.h"
#include "info/info_profile_lines.h"
#include "window/window_controller.h"
#include "storage/storage_shared_media.h"
#include "lang/lang_keys.h"
#include "styles/style_info.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"

namespace Info {
namespace Profile {

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Window::Controller*> controller,
	not_null<PeerData*> peer)
: RpWidget(parent)
, _controller(controller)
, _peer(peer)
, _content(this) {
	setupContent();
}

bool InnerWidget::canHideDetailsEver() const {
	return (_peer->isChat() || _peer->isMegagroup());
}

rpl::producer<bool> InnerWidget::canHideDetails() const {
	using namespace rpl::mappers;
	return MembersCountViewer(_peer)
		| rpl::map($1 > 0);
}

void InnerWidget::setupContent() {
	auto cover = _content->add(object_ptr<Cover>(
		this,
		_peer)
	);
	cover->setOnlineCount(rpl::single(0));
	auto details = setupDetails(_content);
	if (canHideDetailsEver()) {
		cover->setToggleShown(canHideDetails());
		_content->add(object_ptr<Ui::SlideWrap<>>(
			this,
			std::move(details))
		)->toggleOn(cover->toggledValue());
	} else {
		_content->add(std::move(details));
	}
	_content->add(setupSharedMedia(_content));
	_content->add(object_ptr<BoxContentDivider>(this));
	if (auto user = _peer->asUser()) {
		_content->add(setupUserActions(_content, user));
	}

	_content->heightValue()
		| rpl::start([this](int height) {
			TWidget::resizeToWidth(width());
		}, _lifetime);
}

object_ptr<Ui::RpWidget> InnerWidget::setupDetails(
		RpWidget *parent) const {
	auto result = object_ptr<Ui::VerticalLayout>(parent);
	result->add(object_ptr<BoxContentDivider>(result));
	result->add(createSkipWidget(result));
	result->add(setupInfo(result));
	result->add(setupMuteToggle(result));
	if (auto user = _peer->asUser()) {
		setupUserButtons(result, user);
	//} else if (auto channel = _peer->asChannel()) {
	//	if (!channel->isMegagroup()) {
	//		setupChannelButtons(result, channel);
	//	}
	}
	result->add(createSkipWidget(result));
	return std::move(result);
}

object_ptr<Ui::RpWidget> InnerWidget::setupInfo(
		RpWidget *parent) const {
	auto result = object_ptr<Ui::VerticalLayout>(parent);
	auto tracker = MultiLineTracker();
	auto addInfoLine = [&](
			LangKey label,
			rpl::producer<TextWithEntities> &&text,
			bool selectByDoubleClick = false,
			const style::FlatLabel &textSt = st::infoLabeled) {
		auto line = result->add(object_ptr<LabeledLine>(
			result,
			Lang::Viewer(label) | WithEmptyEntities(),
			std::move(text),
			textSt,
			st::infoProfileLabeledPadding,
			selectByDoubleClick));
		tracker.track(line);
		return line;
	};
	auto addInfoOneLine = [&](
			LangKey label,
			rpl::producer<TextWithEntities> &&text) {
		addInfoLine(
			label,
			std::move(text),
			true,
			st::infoLabeledOneLine);
	};
	if (auto user = _peer->asUser()) {
		addInfoOneLine(lng_info_mobile_label, PhoneViewer(user));
		addInfoLine(lng_info_bio_label, BioViewer(user));
		addInfoOneLine(lng_info_username_label, UsernameViewer(user));
	} else {
		addInfoOneLine(lng_info_link_label, LinkViewer(_peer));
		addInfoLine(lng_info_about_label, AboutViewer(_peer));
	}
	result->add(object_ptr<Ui::SlideWrap<>>(
		result,
		object_ptr<Ui::PlainShadow>(result, st::shadowFg),
		st::infoProfileSeparatorPadding)
	)->toggleOn(std::move(tracker).atLeastOneShownValue());
	object_ptr<FloatingIcon>(
		result,
		st::infoIconInformation,
		st::infoInformationIconPosition);
	return std::move(result);
}

object_ptr<Ui::RpWidget> InnerWidget::setupMuteToggle(
		RpWidget *parent) const {
	auto result = object_ptr<Button>(
		parent,
		Lang::Viewer(lng_profile_enable_notifications),
		st::infoNotificationsButton);
	result->toggleOn(
		NotificationsEnabledViewer(_peer)
	)->clicks()
		| rpl::start([this](auto) {
			App::main()->updateNotifySetting(
				_peer,
				_peer->isMuted()
					? NotifySettingSetNotify
					: NotifySettingSetMuted);
		}, result->lifetime());
	object_ptr<FloatingIcon>(
		result,
		st::infoIconNotifications,
		st::infoNotificationsIconPosition);
	return std::move(result);
}

void InnerWidget::setupUserButtons(
		Ui::VerticalLayout *wrap,
		not_null<UserData*> user) const {
	using namespace rpl::mappers;
	auto tracker = MultiLineTracker();
	auto topSkip = wrap->add(createSlideSkipWidget(wrap));
	auto addButton = [&](rpl::producer<QString> &&text) {
		auto result = wrap->add(object_ptr<Ui::SlideWrap<Button>>(
			wrap,
			object_ptr<Button>(
				wrap,
				std::move(text),
				st::infoMainButton)));
		tracker.track(result);
		return result;
	};
	addButton(
		Lang::Viewer(lng_profile_send_message) | ToUpperValue()
	)->toggleOn(
		_controller->historyPeer.value()
		| rpl::map($1 != user)
	)->entity()->clicks()
		| rpl::start([this, user](auto&&) {
			_controller->showPeerHistory(
				user,
				Ui::ShowWay::Forward);
		}, wrap->lifetime());

	addButton(
		Lang::Viewer(lng_info_add_as_contact) | ToUpperValue()
	)->toggleOn(
		CanAddContactViewer(user)
	)->entity()->clicks()
		| rpl::start([user](auto&&) {
			auto firstName = user->firstName;
			auto lastName = user->lastName;
			auto phone = user->phone().isEmpty()
				? App::phoneFromSharedContact(user->bareId())
				: user->phone();
			Ui::show(Box<AddContactBox>(firstName, lastName, phone));
		}, wrap->lifetime());

	topSkip->toggleOn(std::move(tracker).atLeastOneShownValue());
}

object_ptr<Ui::RpWidget> InnerWidget::setupSharedMedia(
		RpWidget *parent) const {
	using namespace rpl::mappers;

	auto content = object_ptr<Ui::VerticalLayout>(parent);
	auto tracker = MultiLineTracker();
	auto addButton = [&](
			rpl::producer<int> &&count,
			auto textFromCount) {
		auto forked = rpl::single(0)
			| rpl::then(std::move(count))
			| start_spawning(content->lifetime());
		auto button = content->add(object_ptr<Ui::SlideWrap<Button>>(
			content,
			object_ptr<Button>(
				content,
				rpl::duplicate(forked)
					| rpl::map([textFromCount](int count) {
						return (count > 0)
							? textFromCount(count)
							: QString();
					}),
				st::infoSharedMediaButton))
		)->toggleOn(
			rpl::duplicate(forked)
				| rpl::map($1 > 0));
		tracker.track(button);
		return button;
	};
	using MediaType = Storage::SharedMediaType;
	auto mediaText = [](MediaType type) {
		switch (type) {
		case MediaType::Photo: return lng_profile_photos;
		case MediaType::Video: return lng_profile_videos;
		case MediaType::File: return lng_profile_files;
		case MediaType::MusicFile: return lng_profile_songs;
		case MediaType::Link: return lng_profile_shared_links;
		case MediaType::VoiceFile: return lng_profile_audios;
		case MediaType::RoundFile: return lng_profile_rounds;
		}
		Unexpected("Type in setupSharedMedia()");
	};
	auto addMediaButton = [&](MediaType type) {
		return addButton(
			SharedMediaCountViewer(_peer, type),
			[phrase = mediaText(type)](int count) {
				return phrase(lt_count, count);
			});
	};
	auto addCommonGroupsButton = [&](not_null<UserData*> user) {
		return addButton(
			CommonGroupsCountViewer(user),
			[](int count) {
				return lng_profile_common_groups(lt_count, count);
			});
	};
	addMediaButton(MediaType::Photo);
	addMediaButton(MediaType::Video);
	addMediaButton(MediaType::File);
	addMediaButton(MediaType::MusicFile);
	addMediaButton(MediaType::Link);
	if (auto user = _peer->asUser()) {
		addCommonGroupsButton(user);
	}
	addMediaButton(MediaType::VoiceFile);
	addMediaButton(MediaType::RoundFile);

	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent)
	);
	result->toggleOn(tracker.atLeastOneShownValue());
	auto layout = result->entity();

	layout->add(object_ptr<BoxContentDivider>(result));
	auto cover = layout->add(object_ptr<SharedMediaCover>(layout));
	if (canHideDetailsEver()) {
		cover->setToggleShown(canHideDetails());
		layout->add(object_ptr<Ui::SlideWrap<>>(
			layout,
			std::move(content))
		)->toggleOn(cover->toggledValue());
	} else {
		layout->add(std::move(content));
	}
	layout->add(object_ptr<Ui::FixedHeightWidget>(
		layout,
		st::infoSharedMediaBottomSkip)
	)->setAttribute(Qt::WA_TransparentForMouseEvents);
	object_ptr<FloatingIcon>(
		result,
		st::infoIconMediaPhoto,
		st::infoSharedMediaIconPosition);
	return std::move(result);
}

object_ptr<Ui::RpWidget> InnerWidget::setupUserActions(
		RpWidget *parent,
		not_null<UserData*> user) const {
	auto result = object_ptr<Ui::VerticalLayout>(parent);
	auto tracker = MultiLineTracker();
	auto addButton = [&](rpl::producer<QString> &&text) {
		auto button = result->add(object_ptr<Ui::SlideWrap<Button>>(
			result,
			object_ptr<Button>(
				result,
				std::move(text),
				st::infoSharedMediaButton)));
		tracker.track(button);
		return button;
	};
	addButton(rpl::single(QString("test action")));
	object_ptr<FloatingIcon>(
		result,
		st::infoIconActions,
		st::infoIconPosition);
	return std::move(result);
}

object_ptr<Ui::RpWidget> InnerWidget::createSkipWidget(
		RpWidget *parent) const {
	return Ui::CreateSkipWidget(parent, st::infoProfileSkip);
}

object_ptr<Ui::SlideWrap<>> InnerWidget::createSlideSkipWidget(
		RpWidget *parent) const {
	return Ui::CreateSlideSkipWidget(parent, st::infoProfileSkip);
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
}

void InnerWidget::saveState(not_null<Memento*> memento) {
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
}

int InnerWidget::resizeGetHeight(int newWidth) {
	_content->resizeToWidth(newWidth);
	_content->moveToLeft(0, 0);
	return qMax(_content->heightNoMargins(), _minHeight);
}

} // namespace Profile
} // namespace Info
