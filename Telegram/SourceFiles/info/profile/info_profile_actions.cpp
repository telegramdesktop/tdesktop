/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_actions.h"

#include <rpl/flatten_latest.h>
#include <rpl/combine.h>
#include "data/data_peer_values.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/labels.h"
#include "ui/toast/toast.h"
#include "boxes/abstract_box.h"
#include "boxes/confirm_box.h"
#include "boxes/peer_list_box.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/add_contact_box.h"
#include "boxes/report_box.h"
#include "lang/lang_keys.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/profile/info_profile_icon.h"
#include "info/profile/info_profile_values.h"
#include "info/profile/info_profile_button.h"
#include "info/profile/info_profile_text.h"
#include "window/window_controller.h"
#include "window/window_peer_menu.h"
#include "mainwidget.h"
#include "auth_session.h"
#include "messenger.h"
#include "apiwrap.h"
#include "application.h"
#include "styles/style_info.h"
#include "styles/style_boxes.h"

namespace Info {
namespace Profile {
namespace {

object_ptr<Ui::RpWidget> CreateSkipWidget(
		not_null<Ui::RpWidget*> parent) {
	return Ui::CreateSkipWidget(parent, st::infoProfileSkip);
}

object_ptr<Ui::SlideWrap<>> CreateSlideSkipWidget(
		not_null<Ui::RpWidget*> parent) {
	auto result = Ui::CreateSlideSkipWidget(
		parent,
		st::infoProfileSkip);
	result->setDuration(st::infoSlideDuration);
	return result;
}

template <typename Text, typename ToggleOn, typename Callback>
auto AddActionButton(
		not_null<Ui::VerticalLayout*> parent,
		Text &&text,
		ToggleOn &&toggleOn,
		Callback &&callback,
		const style::InfoProfileButton &st
			= st::infoSharedMediaButton) {
	auto result = parent->add(object_ptr<Ui::SlideWrap<Button>>(
		parent,
		object_ptr<Button>(
			parent,
			std::move(text),
			st))
	);
	result->setDuration(
		st::infoSlideDuration
	)->toggleOn(
		std::move(toggleOn)
	)->entity()->addClickHandler(std::move(callback));
	result->finishAnimating();
	return result;
};

template <typename Text, typename ToggleOn, typename Callback>
auto AddMainButton(
		not_null<Ui::VerticalLayout*> parent,
		Text &&text,
		ToggleOn &&toggleOn,
		Callback &&callback,
		Ui::MultiSlideTracker &tracker) {
	tracker.track(AddActionButton(
		parent,
		std::move(text) | ToUpperValue(),
		std::move(toggleOn),
		std::move(callback),
		st::infoMainButton));
}

class DetailsFiller {
public:
	DetailsFiller(
		not_null<Controller*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer);

	object_ptr<Ui::RpWidget> fill();

private:
	object_ptr<Ui::RpWidget> setupInfo();
	object_ptr<Ui::RpWidget> setupMuteToggle();
	void setupMainButtons();
	Ui::MultiSlideTracker fillUserButtons(
		not_null<UserData*> user);
	Ui::MultiSlideTracker fillChannelButtons(
		not_null<ChannelData*> channel);

	template <
		typename Widget,
		typename = std::enable_if_t<
		std::is_base_of_v<Ui::RpWidget, Widget>>>
	Widget *add(
			object_ptr<Widget> &&child,
			const style::margins &margin = style::margins()) {
		return _wrap->add(
			std::move(child),
			margin);
	}

	not_null<Controller*> _controller;
	not_null<Ui::RpWidget*> _parent;
	not_null<PeerData*> _peer;
	object_ptr<Ui::VerticalLayout> _wrap;

};

class ActionsFiller {
public:
	ActionsFiller(
		not_null<Controller*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer);

	object_ptr<Ui::RpWidget> fill();

private:
	void addInviteToGroupAction(not_null<UserData*> user);
	void addShareContactAction(not_null<UserData*> user);
	void addEditContactAction(not_null<UserData*> user);
	void addDeleteContactAction(not_null<UserData*> user);
	void addClearHistoryAction(not_null<UserData*> user);
	void addDeleteConversationAction(not_null<UserData*> user);
	void addBotCommandActions(not_null<UserData*> user);
	void addReportAction();
	void addBlockAction(not_null<UserData*> user);
	void addLeaveChannelAction(not_null<ChannelData*> channel);
	void addJoinChannelAction(not_null<ChannelData*> channel);
	void fillUserActions(not_null<UserData*> user);
	void fillChannelActions(not_null<ChannelData*> channel);

	not_null<Controller*> _controller;
	not_null<Ui::RpWidget*> _parent;
	not_null<PeerData*> _peer;
	object_ptr<Ui::VerticalLayout> _wrap = { nullptr };

};

DetailsFiller::DetailsFiller(
	not_null<Controller*> controller,
	not_null<Ui::RpWidget*> parent,
	not_null<PeerData*> peer)
: _controller(controller)
, _parent(parent)
, _peer(peer)
, _wrap(_parent) {
}

object_ptr<Ui::RpWidget> DetailsFiller::setupInfo() {
	auto result = object_ptr<Ui::VerticalLayout>(_wrap);
	auto tracker = Ui::MultiSlideTracker();
	auto addInfoLine = [&](
			LangKey label,
			rpl::producer<TextWithEntities> &&text,
			const style::FlatLabel &textSt = st::infoLabeled) {
		auto line = CreateTextWithLabel(
			result,
			Lang::Viewer(label) | WithEmptyEntities(),
			std::move(text),
			textSt,
			st::infoProfileLabeledPadding);
		tracker.track(result->add(std::move(line.wrap)));
		return line.text;
	};
	auto addInfoOneLine = [&](
			LangKey label,
			rpl::producer<TextWithEntities> &&text,
			const QString &contextCopyText) {
		auto result = addInfoLine(
			label,
			std::move(text),
			st::infoLabeledOneLine);
		result->setDoubleClickSelectsParagraph(true);
		result->setContextCopyText(contextCopyText);
		return result;
	};
	if (auto user = _peer->asUser()) {
		addInfoOneLine(
			lng_info_mobile_label,
			PhoneValue(user),
			lang(lng_profile_copy_phone));
		if (user->botInfo) {
			addInfoLine(lng_info_about_label, AboutValue(user));
		} else {
			addInfoLine(lng_info_bio_label, BioValue(user));
		}
		addInfoOneLine(
			lng_info_username_label,
			UsernameValue(user),
			lang(lng_context_copy_mention));
	} else {
		auto linkText = LinkValue(
			_peer
		) | rpl::map([](const QString &link) {
			auto result = TextWithEntities{ link, {} };
			if (!link.isEmpty()) {
				auto remove = qstr("https://");
				if (result.text.startsWith(remove)) {
					result.text.remove(0, remove.size());
				}
				result.entities.push_back(EntityInText(
					EntityInTextCustomUrl,
					0,
					result.text.size(),
					link));
			}
			return result;
		});
		auto link = addInfoOneLine(
			lng_info_link_label,
			std::move(linkText),
			QString());
		link->setClickHandlerHook([peer = _peer](auto&&...) {
			auto link = Messenger::Instance().createInternalLinkFull(
				peer->userName());
			if (!link.isEmpty()) {
				Application::clipboard()->setText(link);
				Ui::Toast::Show(lang(lng_username_copied));
			}
			return false;
		});
		addInfoLine(lng_info_about_label, AboutValue(_peer));
	}
	if (!_peer->isSelf()) {
		// No notifications toggle for Self => no separator.
		result->add(object_ptr<Ui::SlideWrap<>>(
			result,
			object_ptr<Ui::PlainShadow>(result),
			st::infoProfileSeparatorPadding)
		)->setDuration(
			st::infoSlideDuration
		)->toggleOn(
			std::move(tracker).atLeastOneShownValue()
		);
	}
	object_ptr<FloatingIcon>(
		result,
		st::infoIconInformation,
		st::infoInformationIconPosition);
	return std::move(result);
}

object_ptr<Ui::RpWidget> DetailsFiller::setupMuteToggle() {
	const auto peer = _peer;
	auto result = object_ptr<Button>(
		_wrap,
		Lang::Viewer(lng_profile_enable_notifications),
		st::infoNotificationsButton);
	result->toggleOn(
		NotificationsEnabledValue(peer)
	)->addClickHandler([=] {
		const auto muteState = peer->isMuted()
			? Data::NotifySettings::MuteChange::Unmute
			: Data::NotifySettings::MuteChange::Mute;
		App::main()->updateNotifySettings(peer, muteState);
	});
	object_ptr<FloatingIcon>(
		result,
		st::infoIconNotifications,
		st::infoNotificationsIconPosition);
	return std::move(result);
}

void DetailsFiller::setupMainButtons() {
	auto wrapButtons = [=](auto &&callback) {
		auto topSkip = _wrap->add(CreateSlideSkipWidget(_wrap));
		auto tracker = callback();
		topSkip->toggleOn(std::move(tracker).atLeastOneShownValue());
	};
	if (auto user = _peer->asUser()) {
		wrapButtons([=] {
			return fillUserButtons(user);
		});
	} else if (auto channel = _peer->asChannel()) {
		if (!channel->isMegagroup()) {
			wrapButtons([=] {
				return fillChannelButtons(channel);
			});
		}
	}
}

Ui::MultiSlideTracker DetailsFiller::fillUserButtons(
		not_null<UserData*> user) {
	using namespace rpl::mappers;

	Ui::MultiSlideTracker tracker;
	auto window = _controller->parentController();

	auto addSendMessageButton = [&] {
		auto sendMessageVisible = rpl::combine(
			_controller->wrapValue(),
			window->historyPeer.value(),
			(_1 != Wrap::Side) || (_2 != user));
		auto sendMessage = [window, user] {
			window->showPeerHistory(
				user,
				Window::SectionShow::Way::Forward);
		};
		AddMainButton(
			_wrap,
			Lang::Viewer(lng_profile_send_message),
			std::move(sendMessageVisible),
			std::move(sendMessage),
			tracker);
	};

	if (user->isSelf()) {
		auto separator = _wrap->add(object_ptr<Ui::SlideWrap<>>(
			_wrap,
			object_ptr<Ui::PlainShadow>(_wrap),
			st::infoProfileSeparatorPadding)
		)->setDuration(
			st::infoSlideDuration
		);

		addSendMessageButton();

		separator->toggleOn(
			std::move(tracker).atLeastOneShownValue()
		);
	} else {
		addSendMessageButton();

		AddMainButton(
			_wrap,
			Lang::Viewer(lng_info_add_as_contact),
			CanAddContactValue(user),
			[user] { Window::PeerMenuAddContact(user); },
			tracker);
	}
	return tracker;
}

Ui::MultiSlideTracker DetailsFiller::fillChannelButtons(
		not_null<ChannelData*> channel) {
	using namespace rpl::mappers;

	Ui::MultiSlideTracker tracker;
	auto window = _controller->parentController();
	auto viewChannelVisible = rpl::combine(
		_controller->wrapValue(),
		window->historyPeer.value(),
		(_1 != Wrap::Side) || (_2 != channel));
	auto viewChannel = [=] {
		window->showPeerHistory(
			channel,
			Window::SectionShow::Way::Forward);
	};
	AddMainButton(
		_wrap,
		Lang::Viewer(lng_profile_view_channel),
		std::move(viewChannelVisible),
		std::move(viewChannel),
		tracker);

	return tracker;
}

object_ptr<Ui::RpWidget> DetailsFiller::fill() {
	add(object_ptr<BoxContentDivider>(_wrap));
	add(CreateSkipWidget(_wrap));
	add(setupInfo());
	if (!_peer->isSelf()) {
		add(setupMuteToggle());
	}
	setupMainButtons();
	add(CreateSkipWidget(_wrap));
	return std::move(_wrap);
}

ActionsFiller::ActionsFiller(
	not_null<Controller*> controller,
	not_null<Ui::RpWidget*> parent,
	not_null<PeerData*> peer)
: _controller(controller)
, _parent(parent)
, _peer(peer) {
}

void ActionsFiller::addInviteToGroupAction(
		not_null<UserData*> user) {
	AddActionButton(
		_wrap,
		Lang::Viewer(lng_profile_invite_to_group),
		CanInviteBotToGroupValue(user),
		[user] { AddBotToGroupBoxController::Start(user); });
}

void ActionsFiller::addShareContactAction(not_null<UserData*> user) {
	AddActionButton(
		_wrap,
		Lang::Viewer(lng_info_share_contact),
		CanShareContactValue(user),
		[user] { Window::PeerMenuShareContactBox(user); });
}

void ActionsFiller::addEditContactAction(not_null<UserData*> user) {
	AddActionButton(
		_wrap,
		Lang::Viewer(lng_info_edit_contact),
		IsContactValue(user),
		[user] { Ui::show(Box<AddContactBox>(user)); });
}

void ActionsFiller::addDeleteContactAction(
		not_null<UserData*> user) {
	AddActionButton(
		_wrap,
		Lang::Viewer(lng_info_delete_contact),
		IsContactValue(user),
		[user] { Window::PeerMenuDeleteContact(user); });
}

void ActionsFiller::addClearHistoryAction(not_null<UserData*> user) {
	AddActionButton(
		_wrap,
		Lang::Viewer(lng_profile_clear_history),
		rpl::single(true),
		 Window::ClearHistoryHandler(user));
}

void ActionsFiller::addDeleteConversationAction(
		not_null<UserData*> user) {
	AddActionButton(
		_wrap,
		Lang::Viewer(lng_profile_delete_conversation),
		rpl::single(true),
		Window::DeleteAndLeaveHandler(user));
}

void ActionsFiller::addBotCommandActions(not_null<UserData*> user) {
	auto findBotCommand = [user](const QString &command) {
		if (!user->botInfo) {
			return QString();
		}
		for_const (auto &data, user->botInfo->commands) {
			auto isSame = data.command.compare(
				command,
				Qt::CaseInsensitive) == 0;
			if (isSame) {
				return data.command;
			}
		}
		return QString();
	};
	auto hasBotCommandValue = [=](const QString &command) {
		return Notify::PeerUpdateValue(
			user,
			Notify::PeerUpdate::Flag::BotCommandsChanged
		) | rpl::map([=] {
			return !findBotCommand(command).isEmpty();
		});
	};
	auto sendBotCommand = [=](const QString &command) {
		auto original = findBotCommand(command);
		if (!original.isEmpty()) {
			Ui::showPeerHistory(user, ShowAtTheEndMsgId);
			App::sendBotCommand(user, user, '/' + original);
		}
	};
	auto addBotCommand = [=](LangKey key, const QString &command) {
		AddActionButton(
			_wrap,
			Lang::Viewer(key),
			hasBotCommandValue(command),
			[=] { sendBotCommand(command); });
	};
	addBotCommand(lng_profile_bot_help, qsl("help"));
	addBotCommand(lng_profile_bot_settings, qsl("settings"));
}

void ActionsFiller::addReportAction() {
	auto peer = _peer;
	AddActionButton(
		_wrap,
		Lang::Viewer(lng_profile_report),
		rpl::single(true),
		[peer] { Ui::show(Box<ReportBox>(peer)); },
		st::infoBlockButton);
}

void ActionsFiller::addBlockAction(not_null<UserData*> user) {
	auto text = Notify::PeerUpdateValue(
		user,
		Notify::PeerUpdate::Flag::UserIsBlocked
	) | rpl::map([user] {
		switch (user->blockStatus()) {
		case UserData::BlockStatus::Blocked:
			return Lang::Viewer(user->botInfo
				? lng_profile_unblock_bot
				: lng_profile_unblock_user);
		case UserData::BlockStatus::NotBlocked:
		default:
			return Lang::Viewer(user->botInfo
				? lng_profile_block_bot
				: lng_profile_block_user);
		}
	}) | rpl::flatten_latest(
	) | rpl::start_spawning(_wrap->lifetime());

	auto toggleOn = rpl::duplicate(
		text
	) | rpl::map([](const QString &text) {
		return !text.isEmpty();
	});
	auto callback = [user] {
		if (user->isBlocked()) {
			Auth().api().unblockUser(user);
		} else {
			Auth().api().blockUser(user);
		}
	};
	AddActionButton(
		_wrap,
		rpl::duplicate(text),
		std::move(toggleOn),
		std::move(callback),
		st::infoBlockButton);
}

void ActionsFiller::addLeaveChannelAction(
		not_null<ChannelData*> channel) {
	auto callback = [=] {
		auto text = lang(channel->isMegagroup()
			? lng_sure_leave_group
			: lng_sure_leave_channel);
		Ui::show(Box<ConfirmBox>(text, lang(lng_box_leave), [=] {
			Auth().api().leaveChannel(channel);
		}), LayerOption::KeepOther);
	};
	AddActionButton(
		_wrap,
		Lang::Viewer(lng_profile_leave_channel),
		AmInChannelValue(channel),
		std::move(callback));
}

void ActionsFiller::addJoinChannelAction(
		not_null<ChannelData*> channel) {
	using namespace rpl::mappers;
	auto joinVisible = AmInChannelValue(channel)
		| rpl::map(!_1)
		| rpl::start_spawning(_wrap->lifetime());
	AddActionButton(
		_wrap,
		Lang::Viewer(lng_profile_join_channel),
		rpl::duplicate(joinVisible),
		[channel] { Auth().api().joinChannel(channel); });
	_wrap->add(object_ptr<Ui::SlideWrap<Ui::FixedHeightWidget>>(
		_wrap,
		CreateSkipWidget(
			_wrap,
			st::infoBlockButtonSkip))
	)->setDuration(
		st::infoSlideDuration
	)->toggleOn(
		rpl::duplicate(joinVisible)
	);
}

void ActionsFiller::fillUserActions(not_null<UserData*> user) {
	if (user->botInfo) {
		addInviteToGroupAction(user);
	}
	addShareContactAction(user);
	if (!user->isSelf()) {
		addEditContactAction(user);
		addDeleteContactAction(user);
	}
	addClearHistoryAction(user);
	addDeleteConversationAction(user);
	if (!user->isSelf()) {
		if (user->botInfo) {
			addBotCommandActions(user);
		}
		_wrap->add(CreateSkipWidget(
			_wrap,
			st::infoBlockButtonSkip));
		if (user->botInfo) {
			addReportAction();
		}
		addBlockAction(user);
	}
}

void ActionsFiller::fillChannelActions(
		not_null<ChannelData*> channel) {
	using namespace rpl::mappers;

	addJoinChannelAction(channel);
	addLeaveChannelAction(channel);
	if (!channel->amCreator()) {
		addReportAction();
	}
}

object_ptr<Ui::RpWidget> ActionsFiller::fill() {
	auto wrapResult = [=](auto &&callback) {
		_wrap = object_ptr<Ui::VerticalLayout>(_parent);
		_wrap->add(CreateSkipWidget(_wrap));
		callback();
		_wrap->add(CreateSkipWidget(_wrap));
		object_ptr<FloatingIcon>(
			_wrap,
			st::infoIconActions,
			st::infoIconPosition);
		return std::move(_wrap);
	};
	if (auto user = _peer->asUser()) {
		return wrapResult([=] {
			fillUserActions(user);
		});
	} else if (auto channel = _peer->asChannel()) {
		if (channel->isMegagroup()) {
			return { nullptr };
		}
		return wrapResult([=] {
			fillChannelActions(channel);
		});
	}
	return { nullptr };
}

} // namespace

object_ptr<Ui::RpWidget> SetupDetails(
		not_null<Controller*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer) {
	DetailsFiller filler(controller, parent, peer);
	return filler.fill();
}

object_ptr<Ui::RpWidget> SetupActions(
		not_null<Controller*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer) {
	ActionsFiller filler(controller, parent, peer);
	return filler.fill();
}

void SetupAddChannelMember(
		not_null<Ui::RpWidget*> parent,
		not_null<ChannelData*> channel) {
	auto add = Ui::CreateChild<Ui::IconButton>(
		parent.get(),
		st::infoMembersAddMember);
	add->showOn(CanAddMemberValue(channel));
	add->addClickHandler([channel] {
		Window::PeerMenuAddChannelMembers(channel);
	});
	parent->widthValue(
	) | rpl::start_with_next([add](int newWidth) {
		auto availableWidth = newWidth
			- st::infoMembersButtonPosition.x();
		add->moveToLeft(
			availableWidth - add->width(),
			st::infoMembersButtonPosition.y(),
			newWidth);
	}, add->lifetime());
}

object_ptr<Ui::RpWidget> SetupChannelMembers(
		not_null<Controller*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer) {
	using namespace rpl::mappers;

	auto channel = peer->asChannel();
	if (!channel || channel->isMegagroup()) {
		return { nullptr };
	}

	auto membersShown = rpl::combine(
		MembersCountValue(channel),
		Data::PeerFullFlagValue(
			channel,
			MTPDchannelFull::Flag::f_can_view_participants),
			(_1 > 0) && _2);
	auto membersText = MembersCountValue(
		channel
	) | rpl::map([](int count) {
		return lng_chat_status_members(lt_count, count);
	});
	auto membersCallback = [controller, channel] {
		controller->showSection(Info::Memento(
			channel->id,
			Section::Type::Members));
	};

	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent));
	result->setDuration(
		st::infoSlideDuration
	)->toggleOn(
		std::move(membersShown)
	);

	auto members = result->entity();
	members->add(object_ptr<BoxContentDivider>(members));
	members->add(CreateSkipWidget(members));
	auto button = AddActionButton(
		members,
		std::move(membersText),
		rpl::single(true),
		std::move(membersCallback))->entity();

	SetupAddChannelMember(button, channel);

	object_ptr<FloatingIcon>(
		members,
		st::infoIconMembers,
		st::infoChannelMembersIconPosition);
	members->add(CreateSkipWidget(members));

	return std::move(result);
}

} // namespace Profile
} // namespace Info
