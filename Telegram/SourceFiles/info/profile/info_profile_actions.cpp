/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_actions.h"

#include "base/options.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_folder.h"
#include "data/data_forum_topic.h"
#include "data/data_channel.h"
#include "data/data_changes.h"
#include "data/data_user.h"
#include "data/notify/data_notify_settings.h"
#include "ui/vertical_list.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/boxes/report_box.h"
#include "ui/layers/generic_box.h"
#include "ui/toast/toast.h"
#include "ui/text/text_utilities.h" // Ui::Text::ToUpper
#include "ui/text/text_variant.h"
#include "history/history_location_manager.h" // LocationClickHandler.
#include "history/view/history_view_context_menu.h" // HistoryView::ShowReportPeerBox
#include "boxes/peers/add_bot_to_chat_box.h"
#include "boxes/peers/edit_contact_box.h"
#include "boxes/report_messages_box.h"
#include "boxes/translate_box.h"
#include "lang/lang_keys.h"
#include "menu/menu_mute.h"
#include "history/history.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/profile/info_profile_icon.h"
#include "info/profile/info_profile_phone_menu.h"
#include "info/profile/info_profile_values.h"
#include "info/profile/info_profile_text.h"
#include "support/support_helper.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h" // Window::Controller::show.
#include "window/window_peer_menu.h"
#include "mainwidget.h"
#include "mainwindow.h" // MainWindow::controller.
#include "main/main_session.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "apiwrap.h"
#include "api/api_blocked_peers.h"
#include "styles/style_info.h"
#include "styles/style_boxes.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace Info {
namespace Profile {
namespace {

base::options::toggle ShowPeerIdBelowAbout({
	.id = kOptionShowPeerIdBelowAbout,
	.name = "Show Peer IDs in Profile",
	.description = "Show peer IDs from API below their Bio / Description.",
});

[[nodiscard]] rpl::producer<TextWithEntities> UsernamesSubtext(
		not_null<PeerData*> peer,
		rpl::producer<QString> fallback) {
	return rpl::combine(
		UsernamesValue(peer),
		std::move(fallback)
	) | rpl::map([](std::vector<TextWithEntities> usernames, QString text) {
		if (usernames.size() < 2) {
			return TextWithEntities{ .text = text };
		} else {
			auto result = TextWithEntities();
			result.append(tr::lng_info_usernames_label(tr::now));
			result.append(' ');
			auto &&subrange = ranges::make_subrange(
				begin(usernames) + 1,
				end(usernames));
			for (auto &username : std::move(subrange)) {
				const auto isLast = (usernames.back() == username);
				result.append(Ui::Text::Link(
					'@' + base::take(username.text),
					username.entities.front().data()));
				if (!isLast) {
					result.append(u", "_q);
				}
			}
			return result;
		}
	});
}

[[nodiscard]] Fn<void(QString)> UsernamesLinkCallback(
		not_null<PeerData*> peer,
		std::shared_ptr<Ui::Show> show,
		const QString &addToLink) {
	return [=](QString link) {
		if (!link.startsWith(u"https://"_q)) {
			link = peer->session().createInternalLinkFull(peer->userName())
				+ addToLink;
		}
		if (!link.isEmpty()) {
			QGuiApplication::clipboard()->setText(link);
			show->showToast(tr::lng_username_copied(tr::now));
		}
	};
}

[[nodiscard]] object_ptr<Ui::RpWidget> CreateSkipWidget(
		not_null<Ui::RpWidget*> parent) {
	return Ui::CreateSkipWidget(parent, st::infoProfileSkip);
}

[[nodiscard]] object_ptr<Ui::SlideWrap<>> CreateSlideSkipWidget(
		not_null<Ui::RpWidget*> parent) {
	auto result = Ui::CreateSlideSkipWidget(
		parent,
		st::infoProfileSkip);
	result->setDuration(st::infoSlideDuration);
	return result;
}

[[nodiscard]] rpl::producer<TextWithEntities> AboutWithIdValue(
		not_null<PeerData*> peer) {

	return AboutValue(
		peer
	) | rpl::map([=](TextWithEntities &&value) {
		if (!ShowPeerIdBelowAbout.value()) {
			return std::move(value);
		}
		using namespace Ui::Text;
		if (!value.empty()) {
			value.append("\n");
		}
		value.append(Italic(u"id: "_q));
		const auto raw = peer->id.value & PeerId::kChatTypeMask;
		const auto id = QString::number(raw);
		value.append(Link(Italic(id), "internal:copy:" + id));
		return std::move(value);
	});
}

template <typename Text, typename ToggleOn, typename Callback>
auto AddActionButton(
		not_null<Ui::VerticalLayout*> parent,
		Text &&text,
		ToggleOn &&toggleOn,
		Callback &&callback,
		const style::icon *icon,
		const style::SettingsButton &st
			= st::infoSharedMediaButton) {
	auto result = parent->add(object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
		parent,
		object_ptr<Ui::SettingsButton>(
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
	if (icon) {
		object_ptr<Profile::FloatingIcon>(
			result,
			*icon,
			st::infoSharedMediaButtonIconPosition);
	}
	return result;
};

template <typename Text, typename ToggleOn, typename Callback>
[[nodiscard]] auto AddMainButton(
		not_null<Ui::VerticalLayout*> parent,
		Text &&text,
		ToggleOn &&toggleOn,
		Callback &&callback,
		Ui::MultiSlideTracker &tracker) {
	tracker.track(AddActionButton(
		parent,
		std::move(text) | Ui::Text::ToUpper(),
		std::move(toggleOn),
		std::move(callback),
		nullptr,
		st::infoMainButton));
}

class DetailsFiller {
public:
	DetailsFiller(
		not_null<Controller*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer);
	DetailsFiller(
		not_null<Controller*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<Data::ForumTopic*> topic);

	object_ptr<Ui::RpWidget> fill();

private:
	object_ptr<Ui::RpWidget> setupInfo();
	object_ptr<Ui::RpWidget> setupMuteToggle();
	void setupMainButtons();
	Ui::MultiSlideTracker fillTopicButtons();
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
	Data::ForumTopic *_topic = nullptr;
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

DetailsFiller::DetailsFiller(
	not_null<Controller*> controller,
	not_null<Ui::RpWidget*> parent,
	not_null<Data::ForumTopic*> topic)
: _controller(controller)
, _parent(parent)
, _peer(topic->peer())
, _topic(topic)
, _wrap(_parent) {
}

template <typename T>
bool SetClickContext(
		const ClickHandlerPtr &handler,
		const ClickContext &context) {
	if (const auto casted = std::dynamic_pointer_cast<T>(handler)) {
		casted->T::onClick(context);
		return true;
	}
	return false;
}

object_ptr<Ui::RpWidget> DetailsFiller::setupInfo() {
	auto result = object_ptr<Ui::VerticalLayout>(_wrap);
	auto tracker = Ui::MultiSlideTracker();

	// Fill context for a mention / hashtag / bot command link.
	const auto infoClickFilter = [=,
		peer = _peer.get(),
		window = _controller->parentController()](
			const ClickHandlerPtr &handler,
			Qt::MouseButton button) {
		const auto context = ClickContext{
			button,
			QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = base::make_weak(window),
				.peer = peer,
			})
		};
		if (SetClickContext<BotCommandClickHandler>(handler, context)) {
			return false;
		} else if (SetClickContext<MentionClickHandler>(handler, context)) {
			return false;
		} else if (SetClickContext<HashtagClickHandler>(handler, context)) {
			return false;
		} else if (SetClickContext<CashtagClickHandler>(handler, context)) {
			return false;
		} else if (SetClickContext<UrlClickHandler>(handler, context)) {
			return false;
		}
		return true;
	};

	const auto addTranslateToMenu = [&,
			peer = _peer.get(),
			controller = _controller->parentController()](
			not_null<Ui::FlatLabel*> label,
			rpl::producer<TextWithEntities> &&text) {
		struct State {
			rpl::variable<TextWithEntities> labelText;
		};
		const auto state = label->lifetime().make_state<State>();
		state->labelText = std::move(text);
		label->setContextMenuHook([=](
				Ui::FlatLabel::ContextMenuRequest request) {
			label->fillContextMenu(request);
			if (Ui::SkipTranslate(state->labelText.current())) {
				return;
			}
			auto item = tr::lng_context_translate(tr::now);
			request.menu->addAction(std::move(item), [=] {
				controller->window().show(Box(
					Ui::TranslateBox,
					peer,
					MsgId(),
					state->labelText.current(),
					false));
			});
		});
	};

	const auto addInfoLineGeneric = [&](
			v::text::data &&label,
			rpl::producer<TextWithEntities> &&text,
			const style::FlatLabel &textSt = st::infoLabeled,
			const style::margins &padding = st::infoProfileLabeledPadding) {
		auto line = CreateTextWithLabel(
			result,
			v::text::take_marked(std::move(label)),
			std::move(text),
			st::infoLabel,
			textSt,
			padding);
		tracker.track(result->add(std::move(line.wrap)));

		line.text->setClickHandlerFilter(infoClickFilter);
		return line;
	};
	const auto addInfoLine = [&](
			v::text::data &&label,
			rpl::producer<TextWithEntities> &&text,
			const style::FlatLabel &textSt = st::infoLabeled,
			const style::margins &padding = st::infoProfileLabeledPadding) {
		return addInfoLineGeneric(
			std::move(label),
			std::move(text),
			textSt,
			padding);
	};
	const auto addInfoOneLine = [&](
			v::text::data &&label,
			rpl::producer<TextWithEntities> &&text,
			const QString &contextCopyText,
			const style::margins &padding = st::infoProfileLabeledPadding) {
		auto result = addInfoLine(
			std::move(label),
			std::move(text),
			st::infoLabeledOneLine,
			padding);
		result.text->setDoubleClickSelectsParagraph(true);
		result.text->setContextCopyText(contextCopyText);
		return result;
	};
	if (const auto user = _peer->asUser()) {
		const auto controller = _controller->parentController();
		if (user->session().supportMode()) {
			addInfoLineGeneric(
				user->session().supportHelper().infoLabelValue(user),
				user->session().supportHelper().infoTextValue(user));
		}

		{
			const auto phoneLabel = addInfoOneLine(
				tr::lng_info_mobile_label(),
				PhoneOrHiddenValue(user),
				tr::lng_profile_copy_phone(tr::now)).text;
			const auto hook = [=](Ui::FlatLabel::ContextMenuRequest request) {
				phoneLabel->fillContextMenu(request);
				AddPhoneMenu(request.menu, user);
			};
			phoneLabel->setContextMenuHook(hook);
		}
		auto label = user->isBot()
			? tr::lng_info_about_label()
			: tr::lng_info_bio_label();
		addTranslateToMenu(
			addInfoLine(std::move(label), AboutWithIdValue(user)).text,
			AboutWithIdValue(user));

		const auto usernameLine = addInfoOneLine(
			UsernamesSubtext(_peer, tr::lng_info_username_label()),
			UsernameValue(user, true) | rpl::map([=](TextWithEntities u) {
				return u.text.isEmpty()
					? TextWithEntities()
					: Ui::Text::Link(
						u,
						user->session().createInternalLinkFull(
							u.text.mid(1)));
			}),
			QString(),
			st::infoProfileLabeledUsernamePadding);
		const auto callback = UsernamesLinkCallback(
			_peer,
			controller->uiShow(),
			QString());
		const auto hook = [=](Ui::FlatLabel::ContextMenuRequest request) {
			if (!request.link) {
				return;
			}
			const auto text = request.link->copyToClipboardContextItemText();
			if (text.isEmpty()) {
				return;
			}
			const auto link = request.link->copyToClipboardText();
			request.menu->addAction(
				text,
				[=] { QGuiApplication::clipboard()->setText(link); });
			const auto last = link.lastIndexOf('/');
			if (last < 0) {
				return;
			}
			const auto mention = '@' + link.mid(last + 1);
			if (mention.size() < 2) {
				return;
			}
			request.menu->addAction(
				tr::lng_context_copy_mention(tr::now),
				[=] { QGuiApplication::clipboard()->setText(mention); });
		};
		usernameLine.text->overrideLinkClickHandler(callback);
		usernameLine.subtext->overrideLinkClickHandler(callback);
		usernameLine.text->setContextMenuHook(hook);
		usernameLine.subtext->setContextMenuHook(hook);
		const auto usernameLabel = usernameLine.text;
		if (user->isBot()) {
			const auto copyUsername = Ui::CreateChild<Ui::IconButton>(
				usernameLabel->parentWidget(),
				st::infoProfileLabeledButtonCopy);
			result->sizeValue(
			) | rpl::start_with_next([=] {
				const auto s = usernameLabel->parentWidget()->size();
				copyUsername->moveToRight(
					0,
					(s.height() - copyUsername->height()) / 2);
			}, copyUsername->lifetime());
			copyUsername->setClickedCallback([=] {
				const auto link = user->session().createInternalLinkFull(
					user->userName());
				if (!link.isEmpty()) {
					QGuiApplication::clipboard()->setText(link);
					controller->showToast(tr::lng_username_copied(tr::now));
				}
				return false;
			});
		}

		AddMainButton(
			result,
			tr::lng_info_add_as_contact(),
			CanAddContactValue(user),
			[=] { controller->window().show(Box(EditContactBox, controller, user)); },
			tracker);
	} else {
		const auto topicRootId = _topic ? _topic->rootId() : 0;
		const auto addToLink = topicRootId
			? ('/' + QString::number(topicRootId.bare))
			: QString();
		auto linkText = LinkValue(
			_peer,
			true
		) | rpl::map([=](const QString &link) {
			return link.isEmpty()
				? TextWithEntities()
				: Ui::Text::Link(
					(link.startsWith(u"https://"_q)
						? link.mid(u"https://"_q.size())
						: link) + addToLink,
					link + addToLink);
		});
		auto linkLine = addInfoOneLine(
			(topicRootId
				? tr::lng_info_link_label(Ui::Text::WithEntities)
				: UsernamesSubtext(_peer, tr::lng_info_link_label())),
			std::move(linkText),
			QString());
		const auto controller = _controller->parentController();
		const auto linkCallback = UsernamesLinkCallback(
			_peer,
			controller->uiShow(),
			addToLink);
		linkLine.text->overrideLinkClickHandler(linkCallback);
		linkLine.subtext->overrideLinkClickHandler(linkCallback);

		if (const auto channel = _topic ? nullptr : _peer->asChannel()) {
			auto locationText = LocationValue(
				channel
			) | rpl::map([](const ChannelLocation *location) {
				return location
					? Ui::Text::Link(
						TextUtilities::SingleLine(location->address),
						LocationClickHandler::Url(location->point))
					: TextWithEntities();
			});
			addInfoOneLine(
				tr::lng_info_location_label(),
				std::move(locationText),
				QString()
			).text->setLinksTrusted();
		}

		const auto about = addInfoLine(tr::lng_info_about_label(), _topic
			? rpl::single(TextWithEntities())
			: AboutWithIdValue(_peer));
		if (!_topic) {
			addTranslateToMenu(about.text, AboutWithIdValue(_peer));
		}
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

	return result;
}

object_ptr<Ui::RpWidget> DetailsFiller::setupMuteToggle() {
	const auto peer = _peer;
	const auto topicRootId = _topic ? _topic->rootId() : MsgId();
	const auto makeThread = [=] {
		return topicRootId
			? static_cast<Data::Thread*>(peer->forumTopicFor(topicRootId))
			: peer->owner().history(peer).get();
	};
	auto result = object_ptr<Ui::SettingsButton>(
		_wrap,
		tr::lng_profile_enable_notifications(),
		st::infoNotificationsButton);
	result->toggleOn(_topic
		? NotificationsEnabledValue(_topic)
		: NotificationsEnabledValue(peer), true);
	result->setAcceptBoth();
	const auto notifySettings = &peer->owner().notifySettings();
	MuteMenu::SetupMuteMenu(
		result.data(),
		result->clicks(
		) | rpl::filter([=](Qt::MouseButton button) {
			if (button == Qt::RightButton) {
				return true;
			}
			const auto topic = topicRootId
				? peer->forumTopicFor(topicRootId)
				: nullptr;
			Assert(!topicRootId || topic != nullptr);
			const auto is = topic
				? notifySettings->isMuted(topic)
				: notifySettings->isMuted(peer);
			if (is) {
				if (topic) {
					notifySettings->update(topic, { .unmute = true });
				} else {
					notifySettings->update(peer, { .unmute = true });
				}
				return false;
			} else {
				return true;
			}
		}) | rpl::to_empty,
		makeThread,
		_controller->uiShow());
	object_ptr<FloatingIcon>(
		result,
		st::infoIconNotifications,
		st::infoNotificationsIconPosition);
	return result;
}

void DetailsFiller::setupMainButtons() {
	auto wrapButtons = [=](auto &&callback) {
		auto topSkip = _wrap->add(CreateSlideSkipWidget(_wrap));
		auto tracker = callback();
		topSkip->toggleOn(std::move(tracker).atLeastOneShownValue());
	};
	if (_topic) {
		wrapButtons([=] {
			return fillTopicButtons();
		});
	} else if (const auto user = _peer->asUser()) {
		wrapButtons([=] {
			return fillUserButtons(user);
		});
	} else if (const auto channel = _peer->asChannel()) {
		if (!channel->isMegagroup()) {
			wrapButtons([=] {
				return fillChannelButtons(channel);
			});
		}
	}
}

Ui::MultiSlideTracker DetailsFiller::fillTopicButtons() {
	using namespace rpl::mappers;

	Ui::MultiSlideTracker tracker;
	const auto window = _controller->parentController();

	const auto forum = _topic->forum();
	auto showTopicsVisible = rpl::combine(
		window->adaptive().oneColumnValue(),
		window->shownForum().value(),
		_1 || (_2 != forum));
	AddMainButton(
		_wrap,
		tr::lng_forum_show_topics_list(),
		std::move(showTopicsVisible),
		[=] { window->showForum(forum); },
		tracker);
	return tracker;
}

Ui::MultiSlideTracker DetailsFiller::fillUserButtons(
		not_null<UserData*> user) {
	using namespace rpl::mappers;

	Ui::MultiSlideTracker tracker;
	auto window = _controller->parentController();

	auto addSendMessageButton = [&] {
		auto activePeerValue = window->activeChatValue(
		) | rpl::map([](Dialogs::Key key) {
			return key.peer();
		});
		auto sendMessageVisible = rpl::combine(
			_controller->wrapValue(),
			std::move(activePeerValue),
			(_1 != Wrap::Side) || (_2 != user));
		auto sendMessage = [window, user] {
			window->showPeerHistory(
				user,
				Window::SectionShow::Way::Forward);
		};
		AddMainButton(
			_wrap,
			tr::lng_profile_send_message(),
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
	}
	return tracker;
}

Ui::MultiSlideTracker DetailsFiller::fillChannelButtons(
		not_null<ChannelData*> channel) {
	using namespace rpl::mappers;

	Ui::MultiSlideTracker tracker;
	auto window = _controller->parentController();
	auto activePeerValue = window->activeChatValue(
	) | rpl::map([](Dialogs::Key key) {
		return key.peer();
	});
	auto viewChannelVisible = rpl::combine(
		_controller->wrapValue(),
		std::move(activePeerValue),
		(_1 != Wrap::Side) || (_2 != channel));
	auto viewChannel = [=] {
		window->showPeerHistory(
			channel,
			Window::SectionShow::Way::Forward);
	};
	AddMainButton(
		_wrap,
		tr::lng_profile_view_channel(),
		std::move(viewChannelVisible),
		std::move(viewChannel),
		tracker);

	return tracker;
}

object_ptr<Ui::RpWidget> DetailsFiller::fill() {
	Expects(!_topic || !_topic->creating());

	add(object_ptr<Ui::BoxContentDivider>(_wrap));
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
	const auto notEmpty = [](const QString &value) {
		return !value.isEmpty();
	};
	const auto controller = _controller->parentController();
	AddActionButton(
		_wrap,
		InviteToChatButton(user) | rpl::filter(notEmpty),
		InviteToChatButton(user) | rpl::map(notEmpty),
		[=] { AddBotToGroupBoxController::Start(controller, user); },
		&st::infoIconAddMember);
	const auto about = _wrap->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_wrap.data(),
			object_ptr<Ui::VerticalLayout>(_wrap.data())));
	about->toggleOn(InviteToChatAbout(user) | rpl::map(notEmpty));
	Ui::AddSkip(about->entity());
	Ui::AddDividerText(
		about->entity(),
		InviteToChatAbout(user) | rpl::filter(notEmpty));
	Ui::AddSkip(about->entity());
	about->finishAnimating();
}

void ActionsFiller::addShareContactAction(not_null<UserData*> user) {
	const auto controller = _controller->parentController();
	AddActionButton(
		_wrap,
		tr::lng_info_share_contact(),
		CanShareContactValue(user),
		[=] { Window::PeerMenuShareContactBox(controller, user); },
		&st::infoIconShare);
}

void ActionsFiller::addEditContactAction(not_null<UserData*> user) {
	const auto controller = _controller->parentController();
	AddActionButton(
		_wrap,
		tr::lng_info_edit_contact(),
		IsContactValue(user),
		[=] { controller->window().show(Box(EditContactBox, controller, user)); },
		&st::infoIconEdit);
}

void ActionsFiller::addDeleteContactAction(not_null<UserData*> user) {
	const auto controller = _controller->parentController();
	AddActionButton(
		_wrap,
		tr::lng_info_delete_contact(),
		IsContactValue(user),
		[=] { Window::PeerMenuDeleteContact(controller, user); },
		&st::infoIconDelete);
}

void ActionsFiller::addBotCommandActions(not_null<UserData*> user) {
	auto findBotCommand = [user](const QString &command) {
		if (!user->isBot()) {
			return QString();
		}
		for (const auto &data : user->botInfo->commands) {
			const auto isSame = !data.command.compare(
				command,
				Qt::CaseInsensitive);
			if (isSame) {
				return data.command;
			}
		}
		return QString();
	};
	auto hasBotCommandValue = [=](const QString &command) {
		return user->session().changes().peerFlagsValue(
			user,
			Data::PeerUpdate::Flag::BotCommands
		) | rpl::map([=] {
			return !findBotCommand(command).isEmpty();
		});
	};
	auto sendBotCommand = [=, window = _controller->parentController()](
			const QString &command) {
		const auto original = findBotCommand(command);
		if (original.isEmpty()) {
			return;
		}
		BotCommandClickHandler('/' + original).onClick(ClickContext{
			Qt::LeftButton,
			QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = base::make_weak(window),
				.peer = user,
			})
		});
	};
	auto addBotCommand = [=](
			rpl::producer<QString> text,
			const QString &command,
			const style::icon *icon = nullptr) {
		AddActionButton(
			_wrap,
			std::move(text),
			hasBotCommandValue(command),
			[=] { sendBotCommand(command); },
			icon);
	};
	addBotCommand(
		tr::lng_profile_bot_help(),
		u"help"_q,
		&st::infoIconInformation);
	addBotCommand(tr::lng_profile_bot_settings(), u"settings"_q);
	addBotCommand(tr::lng_profile_bot_privacy(), u"privacy"_q);
}

void ActionsFiller::addReportAction() {
	const auto peer = _peer;
	const auto controller = _controller->parentController();
	const auto report = [=] {
		ShowReportPeerBox(controller, peer);
	};
	AddActionButton(
		_wrap,
		tr::lng_profile_report(),
		rpl::single(true),
		report,
		&st::infoIconReport,
		st::infoBlockButton);
}

void ActionsFiller::addBlockAction(not_null<UserData*> user) {
	const auto controller = _controller->parentController();
	const auto window = &controller->window();

	auto text = user->session().changes().peerFlagsValue(
		user,
		Data::PeerUpdate::Flag::IsBlocked
	) | rpl::map([=] {
		switch (user->blockStatus()) {
		case UserData::BlockStatus::Blocked:
			return ((user->isBot() && !user->isSupport())
				? tr::lng_profile_restart_bot
				: tr::lng_profile_unblock_user)();
		case UserData::BlockStatus::NotBlocked:
		default:
			return ((user->isBot() && !user->isSupport())
				? tr::lng_profile_block_bot
				: tr::lng_profile_block_user)();
		}
	}) | rpl::flatten_latest(
	) | rpl::start_spawning(_wrap->lifetime());

	auto toggleOn = rpl::duplicate(
		text
	) | rpl::map([](const QString &text) {
		return !text.isEmpty();
	});
	auto callback = [=] {
		if (user->isBlocked()) {
			Window::PeerMenuUnblockUserWithBotRestart(user);
			if (user->isBot()) {
				controller->showPeerHistory(user);
			}
		} else if (user->isBot()) {
			user->session().api().blockedPeers().block(user);
		} else {
			window->show(Box(
				Window::PeerMenuBlockUserBox,
				window,
				user,
				v::null,
				v::null));
		}
	};
	AddActionButton(
		_wrap,
		rpl::duplicate(text),
		std::move(toggleOn),
		std::move(callback),
		&st::infoIconBlock,
		st::infoBlockButton);
}

void ActionsFiller::addLeaveChannelAction(not_null<ChannelData*> channel) {
	Expects(_controller->parentController());

	AddActionButton(
		_wrap,
		tr::lng_profile_leave_channel(),
		AmInChannelValue(channel),
		Window::DeleteAndLeaveHandler(
			_controller->parentController(),
			channel),
		&st::infoIconLeave);
}

void ActionsFiller::addJoinChannelAction(
		not_null<ChannelData*> channel) {
	using namespace rpl::mappers;
	auto joinVisible = AmInChannelValue(channel)
		| rpl::map(!_1)
		| rpl::start_spawning(_wrap->lifetime());
	AddActionButton(
		_wrap,
		tr::lng_profile_join_channel(),
		rpl::duplicate(joinVisible),
		[=] { channel->session().api().joinChannel(channel); },
		&st::infoIconAddMember);
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
	if (user->isBot()) {
		addInviteToGroupAction(user);
	}
	addShareContactAction(user);
	if (!user->isSelf()) {
		addEditContactAction(user);
		addDeleteContactAction(user);
	}
	if (!user->isSelf() && !user->isSupport()) {
		if (user->isBot()) {
			addBotCommandActions(user);
		}
		_wrap->add(CreateSkipWidget(
			_wrap,
			st::infoBlockButtonSkip));
		if (user->isBot()) {
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

const char kOptionShowPeerIdBelowAbout[] = "show-peer-id-below-about";

object_ptr<Ui::RpWidget> SetupDetails(
		not_null<Controller*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer) {
	DetailsFiller filler(controller, parent, peer);
	return filler.fill();
}

object_ptr<Ui::RpWidget> SetupDetails(
		not_null<Controller*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<Data::ForumTopic*> topic) {
	DetailsFiller filler(controller, parent, topic);
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
		not_null<Window::SessionNavigation*> navigation,
		not_null<Ui::RpWidget*> parent,
		not_null<ChannelData*> channel) {
	auto add = Ui::CreateChild<Ui::IconButton>(
		parent.get(),
		st::infoMembersAddMember);
	add->showOn(CanAddMemberValue(channel));
	add->addClickHandler([=] {
		Window::PeerMenuAddChannelMembers(navigation, channel);
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
		Data::PeerFlagValue(
			channel,
			ChannelDataFlag::CanViewParticipants),
			(_1 > 0) && _2);
	auto membersText = tr::lng_chat_status_subscribers(
		lt_count_decimal,
		MembersCountValue(channel) | tr::to_count());
	auto membersCallback = [=] {
		controller->showSection(std::make_shared<Info::Memento>(
			channel,
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
	members->add(object_ptr<Ui::BoxContentDivider>(members));
	members->add(CreateSkipWidget(members));
	auto button = AddActionButton(
		members,
		std::move(membersText),
		rpl::single(true),
		std::move(membersCallback),
		nullptr)->entity();

	SetupAddChannelMember(controller, button, channel);

	object_ptr<FloatingIcon>(
		members,
		st::infoIconMembers,
		st::infoChannelMembersIconPosition);
	members->add(CreateSkipWidget(members));

	return result;
}

} // namespace Profile
} // namespace Info
