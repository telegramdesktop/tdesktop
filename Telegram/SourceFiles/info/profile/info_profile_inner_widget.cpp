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
#include "info/profile/info_profile_inner_widget.h"

#include <rpl/combine.h>
#include <rpl/flatten_latest.h>
#include "info/info_memento.h"
#include "info/profile/info_profile_button.h"
#include "info/profile/info_profile_widget.h"
#include "info/profile/info_profile_text.h"
#include "info/profile/info_profile_values.h"
#include "info/profile/info_profile_cover.h"
#include "info/profile/info_profile_icon.h"
#include "info/profile/info_profile_members.h"
#include "info/media/info_media_buttons.h"
#include "info/info_top_bar_override.h"
#include "boxes/abstract_box.h"
#include "boxes/add_contact_box.h"
#include "boxes/confirm_box.h"
#include "mainwidget.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "window/main_window.h"
#include "window/window_controller.h"
#include "storage/storage_shared_media.h"
#include "lang/lang_keys.h"
#include "styles/style_info.h"
#include "styles/style_boxes.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "history/history_shared_media.h"
#include "profile/profile_common_groups_section.h"

namespace Info {
namespace Profile {

InnerWidget::InnerWidget(
	QWidget *parent,
	rpl::producer<Wrap> &&wrapValue,
	not_null<Window::Controller*> controller,
	not_null<PeerData*> peer)
: RpWidget(parent)
, _controller(controller)
, _peer(peer)
, _content(setupContent(this, std::move(wrapValue))) {
	_content->heightValue()
		| rpl::start_with_next([this](int height) {
			resizeToWidth(width());
			_desiredHeight.fire(countDesiredHeight());
		}, lifetime());
}

bool InnerWidget::canHideDetailsEver() const {
	return (_peer->isChat() || _peer->isMegagroup());
}

rpl::producer<bool> InnerWidget::canHideDetails() const {
	using namespace rpl::mappers;
	return MembersCountValue(_peer)
		| rpl::map($1 > 0);
}

object_ptr<Ui::RpWidget> InnerWidget::setupContent(
		RpWidget *parent,
		rpl::producer<Wrap> &&wrapValue) {
	auto result = object_ptr<Ui::VerticalLayout>(parent);
	_cover = result->add(object_ptr<Cover>(
		result,
		_peer));
	_cover->setOnlineCount(rpl::single(0));
	auto details = setupDetails(parent);
	if (canHideDetailsEver()) {
		_cover->setToggleShown(canHideDetails());
		_infoWrap = result->add(object_ptr<Ui::SlideWrap<>>(
			result,
			std::move(details))
		)->toggleOn(_cover->toggledValue());
	} else {
		result->add(std::move(details));
	}
	result->add(setupSharedMedia(result));
	result->add(object_ptr<BoxContentDivider>(result));
	if (auto user = _peer->asUser()) {
		result->add(setupUserActions(result, user));
	//} else if (auto channel = _peer->asChannel()) {
	//	if (!channel->isMegagroup()) {
	//		setupChannelActions(result, channel);
	//	}
	}
	if (_peer->isChat() || _peer->isMegagroup()) {
		_members = result->add(object_ptr<Members>(
			result,
			_controller,
			std::move(wrapValue),
			_peer)
		);
		_members->scrollToRequests()
			| rpl::start_with_next([this](Ui::ScrollToRequest request) {
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
	}
	return std::move(result);
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
	auto tracker = Ui::MultiSlideTracker();
	auto addInfoLine = [&](
			LangKey label,
			rpl::producer<TextWithEntities> &&text,
			bool selectByDoubleClick = false,
			const style::FlatLabel &textSt = st::infoLabeled) {
		auto line = result->add(CreateTextWithLabel(
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
		addInfoOneLine(lng_info_mobile_label, PhoneValue(user));
		addInfoLine(lng_info_bio_label, BioValue(user));
		addInfoOneLine(lng_info_username_label, UsernameValue(user));
	} else {
		addInfoOneLine(lng_info_link_label, LinkValue(_peer));
		addInfoLine(lng_info_about_label, AboutValue(_peer));
	}
	result->add(object_ptr<Ui::SlideWrap<>>(
		result,
		object_ptr<Ui::PlainShadow>(result),
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
		NotificationsEnabledValue(_peer)
	)->addClickHandler([this] {
		App::main()->updateNotifySetting(
			_peer,
			_peer->isMuted()
				? NotifySettingSetNotify
				: NotifySettingSetMuted);
	});
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
	auto tracker = Ui::MultiSlideTracker();
	auto topSkip = wrap->add(createSlideSkipWidget(wrap));
	auto addButton = [&](auto &&text) {
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
	)->entity()->addClickHandler([this, user] {
		_controller->showPeerHistory(
			user,
			Window::SectionShow::Way::Forward);
	});

	addButton(
		Lang::Viewer(lng_info_add_as_contact) | ToUpperValue()
	)->toggleOn(
		CanAddContactValue(user)
	)->entity()->addClickHandler([user] {
		auto firstName = user->firstName;
		auto lastName = user->lastName;
		auto phone = user->phone().isEmpty()
			? App::phoneFromSharedContact(user->bareId())
			: user->phone();
		Ui::show(Box<AddContactBox>(firstName, lastName, phone));
	});

	topSkip->toggleOn(std::move(tracker).atLeastOneShownValue());
}

object_ptr<Ui::RpWidget> InnerWidget::setupSharedMedia(
		RpWidget *parent) {
	using namespace rpl::mappers;
	using MediaType = Media::Type;

	auto content = object_ptr<Ui::VerticalLayout>(parent);
	auto tracker = Ui::MultiSlideTracker();
	auto addMediaButton = [&](MediaType type) {
		return Media::AddButton(
			content,
			_controller,
			peer(),
			type,
			tracker);
	};
	auto addCommonGroupsButton = [&](not_null<UserData*> user) {
		return Media::AddCommonGroupsButton(
			content,
			_controller,
			user,
			tracker);
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
//	addMediaButton(MediaType::RoundFile);

	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent)
	);
	result->toggleOn(tracker.atLeastOneShownValue());
	auto layout = result->entity();

	layout->add(object_ptr<BoxContentDivider>(layout));
	_sharedMediaCover = layout->add(
		object_ptr<SharedMediaCover>(layout));
	if (canHideDetailsEver()) {
		_sharedMediaCover->setToggleShown(canHideDetails());
		_sharedMediaWrap = layout->add(object_ptr<Ui::SlideWrap<>>(
			layout,
			std::move(content))
		)->toggleOn(_sharedMediaCover->toggledValue());
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
	result->add(createSkipWidget(result));
	auto addButton = [&](
			auto &&text,
			auto &&toggleOn,
			auto &&callback,
			const style::InfoProfileButton &st
				= st::infoSharedMediaButton) {
		return result->add(object_ptr<Ui::SlideWrap<Button>>(
			result,
			object_ptr<Button>(
				result,
				std::move(text),
				st))
		)->toggleOn(
			std::move(toggleOn)
		)->entity()->addClickHandler(std::move(callback));
	};

	addButton(
		Lang::Viewer(lng_profile_share_contact),
		CanShareContactValue(user),
		[this, user] { shareContact(user); });
	addButton(
		Lang::Viewer(lng_info_edit_contact),
		IsContactValue(user),
		[user] { Ui::show(Box<AddContactBox>(user)); });
	addButton(
		Lang::Viewer(lng_profile_clear_history),
		rpl::single(true),
		[user] {
			auto confirmation = lng_sure_delete_history(lt_contact, App::peerName(user));
			Ui::show(Box<ConfirmBox>(confirmation, lang(lng_box_delete), st::attentionBoxButton, [user] {
				Ui::hideLayer();
				App::main()->clearHistory(user);
				Ui::showPeerHistory(user, ShowAtUnreadMsgId);
			}));
		});
	addButton(
		Lang::Viewer(lng_profile_delete_conversation),
		rpl::single(true),
		[user] {
			auto confirmation = lng_sure_delete_history(lt_contact, App::peerName(user));
			auto confirmButton = lang(lng_box_delete);
			Ui::show(Box<ConfirmBox>(confirmation, confirmButton, st::attentionBoxButton, [user] {
				Ui::hideLayer();
				Ui::showChatsList();
				App::main()->deleteConversation(user);
			}));
		});

	if (!user->isSelf()) {
		result->add(CreateSkipWidget(
			result,
			st::infoBlockButtonSkip));

		auto text = PeerUpdateValue(user, Notify::PeerUpdate::Flag::UserIsBlocked)
			| rpl::map([user]() -> rpl::producer<QString> {
				switch (user->blockStatus()) {
				case UserData::BlockStatus::Blocked:
					return Lang::Viewer(lng_profile_unblock_user);
				case UserData::BlockStatus::NotBlocked:
					return Lang::Viewer(lng_profile_block_user);
				default:
					return rpl::single(QString());
				}
			})
			| rpl::flatten_latest()
			| rpl::start_spawning(result->lifetime());
		addButton(
			rpl::duplicate(text),
			rpl::duplicate(text)
				| rpl::map([](const QString &text) {
					return !text.isEmpty();
				}),
			[user] {
				if (user->isBlocked()) {
					Auth().api().unblockUser(user);
				} else {
					Auth().api().blockUser(user);
				}
			},
			st::infoBlockButton);
	}
	result->add(createSkipWidget(result));

	object_ptr<FloatingIcon>(
		result,
		st::infoIconActions,
		st::infoIconPosition);
	return std::move(result);
}

void InnerWidget::shareContact(not_null<UserData*> user) const {
	auto callback = [user](not_null<PeerData*> peer) {
		if (!peer->canWrite()) {
			Ui::show(Box<InformBox>(
				lang(lng_forward_share_cant)),
				LayerOption::KeepOther);
			return;
		}
		auto recipient = peer->isUser()
			? peer->name
			: '\xAB' + peer->name + '\xBB';
		Ui::show(Box<ConfirmBox>(
			lng_forward_share_contact(lt_recipient, recipient),
			lang(lng_forward_send),
			[peer, user] {
				App::main()->onShareContact(
					peer->id,
					user);
				Ui::hideLayer();
			}), LayerOption::KeepOther);
	};
	Ui::show(Box<PeerListBox>(
		std::make_unique<ChooseRecipientBoxController>(std::move(callback)),
		[](not_null<PeerListBox*> box) {
			box->addButton(langFactory(lng_cancel), [box] {
				box->closeBox();
			});
		}));
}

object_ptr<Ui::RpWidget> InnerWidget::createSkipWidget(
		RpWidget *parent) const {
	return Ui::CreateSkipWidget(parent, st::infoProfileSkip);
}

object_ptr<Ui::SlideWrap<>> InnerWidget::createSlideSkipWidget(
		RpWidget *parent) const {
	return Ui::CreateSlideSkipWidget(parent, st::infoProfileSkip);
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
	memento->setMediaExpanded(_sharedMediaCover->toggled());
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	_cover->toggle(memento->infoExpanded());
	if (_infoWrap) {
		_infoWrap->finishAnimating();
	}
	_sharedMediaCover->toggle(memento->mediaExpanded());
	if (_sharedMediaWrap) {
		_sharedMediaWrap->finishAnimating();
	}
}

int InnerWidget::resizeGetHeight(int newWidth) {
	_content->resizeToWidth(newWidth);
	_content->moveToLeft(0, 0);
	return _content->heightNoMargins();
}

} // namespace Profile
} // namespace Info
