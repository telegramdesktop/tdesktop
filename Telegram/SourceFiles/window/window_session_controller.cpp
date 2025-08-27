/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_session_controller.h"

#include "api/api_text_entities.h"
#include "boxes/add_contact_box.h"
#include "boxes/peers/add_bot_to_chat_box.h"
#include "boxes/peers/edit_peer_info_box.h"
#include "boxes/peers/replace_boost_box.h"
#include "boxes/delete_messages_box.h"
#include "window/window_chat_preview.h"
#include "window/window_chat_switch_process.h"
#include "window/window_controller.h"
#include "window/window_filters_menu.h"
#include "window/window_separate_id.h"
#include "info/channel_statistics/earn/info_channel_earn_list.h"
#include "info/peer_gifts/info_peer_gifts_widget.h"
#include "info/stories/info_stories_widget.h"
#include "info/info_memento.h"
#include "info/info_controller.h"
#include "inline_bots/bot_attach_web_view.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/reactions/history_view_reactions.h"
//#include "history/view/reactions/history_view_reactions_button.h"
#include "history/view/history_view_chat_section.h"
#include "history/view/history_view_scheduled_section.h"
#include "history/view/history_view_subsection_tabs.h"
#include "media/player/media_player_instance.h"
#include "media/view/media_view_open_common.h"
#include "data/components/recent_peers.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_document_resolver.h"
#include "data/data_download_manager.h"
#include "data/data_saved_messages.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "data/data_flags.h"
#include "data/data_folder.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_changes.h"
#include "data/data_group_call.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_chat_filters.h"
#include "data/data_replies_list.h"
#include "data/data_peer_values.h"
#include "data/data_premium_limits.h"
#include "data/data_web_page.h"
#include "dialogs/ui/chat_search_in.h"
#include "passport/passport_form_controller.h"
#include "chat_helpers/tabbed_selector.h"
#include "chat_helpers/emoji_interactions.h"
#include "core/shortcuts.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/ui_integration.h"
#include "base/unixtime.h"
#include "info/channel_statistics/earn/earn_icons.h"
#include "ui/controls/userpic_button.h"
#include "ui/text/text_utilities.h"
#include "ui/text/format_values.h" // Ui::FormatPhone.
#include "ui/delayed_activation.h"
#include "ui/boxes/boost_box.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/message_sending_animation_controller.h"
#include "ui/style/style_palette_colorizer.h"
#include "ui/toast/toast.h"
#include "calls/calls_instance.h" // Core::App().calls().inCall().
#include "calls/group/calls_group_call.h"
#include "calls/group/calls_group_common.h"
#include "calls/group/calls_group_invite_controller.h"
#include "ui/boxes/calendar_box.h"
#include "ui/boxes/collectible_info_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/ui_utility.h"
#include "mainwidget.h"
#include "main/main_app_config.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "api/api_chat_invite.h"
#include "api/api_global_privacy.h"
#include "api/api_blocked_peers.h"
#include "support/support_helper.h"
#include "storage/file_upload.h"
#include "storage/download_manager_mtproto.h"
#include "storage/storage_account.h"
#include "window/themes/window_theme.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller_link_info.h"
#include "settings/settings_main.h"
#include "settings/settings_premium.h"
#include "settings/settings_privacy_security.h"
#include "styles/style_window.h"
#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h" // st::boxLabel

namespace Window {
namespace {

constexpr auto kCustomThemesInMemory = 5;
constexpr auto kMaxChatEntryHistorySize = 50;

class MainWindowShow final : public ChatHelpers::Show {
public:
	explicit MainWindowShow(not_null<SessionController*> controller);

	void activate() override;

	void showOrHideBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<Ui::BoxContent>,
			std::unique_ptr<Ui::LayerWidget>> &&layer,
		Ui::LayerOptions options,
		anim::type animated) const override;

	not_null<QWidget*> toastParent() const override;
	bool valid() const override;
	operator bool() const override;

	Main::Session &session() const override;
	bool paused(ChatHelpers::PauseReason reason) const override;
	rpl::producer<> pauseChanged() const override;

	rpl::producer<bool> adjustShadowLeft() const override;
	SendMenu::Details sendMenuDetails() const override;

	bool showMediaPreview(
		Data::FileOrigin origin,
		not_null<DocumentData*> document) const override;
	bool showMediaPreview(
		Data::FileOrigin origin,
		not_null<PhotoData*> photo) const override;

	void processChosenSticker(
		ChatHelpers::FileChosen &&chosen) const override;

private:
	const base::weak_ptr<SessionController> _window;

};

[[nodiscard]] Ui::ChatThemeBubblesData PrepareBubblesData(
		const Data::CloudTheme &theme,
		Data::CloudThemeType type) {
	const auto i = theme.settings.find(type);
	return {
		.colors = (i != end(theme.settings)
			? i->second.outgoingMessagesColors
			: std::vector<QColor>()),
		.accent = (i != end(theme.settings)
			? i->second.outgoingAccentColor
			: std::optional<QColor>()),
	};
}

[[nodiscard]] bool DownloadingDocument(not_null<DocumentData*> document) {
	for (const auto id : Core::App().downloadManager().loadingList()) {
		if (id->object.document == document.get()) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] Ui::CollectibleInfo Parse(
		const QString &entity,
		not_null<PeerData*> owner,
		const MTPfragment_CollectibleInfo &info) {
	const auto &data = info.data();
	return {
		.entity = entity,
		.copyText = (entity.startsWith('+')
			? QString()
			: owner->session().createInternalLinkFull(entity)),
		.ownerUserpic = Ui::MakeUserpicThumbnail(owner, true),
		.ownerName = owner->name(),
		.cryptoAmount = data.vcrypto_amount().v,
		.amount = data.vamount().v,
		.cryptoCurrency = qs(data.vcrypto_currency()),
		.currency = qs(data.vcurrency()),
		.url = qs(data.vurl()),
		.date = data.vpurchase_date().v,
	};
}

MainWindowShow::MainWindowShow(not_null<SessionController*> controller)
: _window(base::make_weak(controller)) {
}

void MainWindowShow::activate() {
	if (const auto window = _window.get()) {
		Window::ActivateWindow(window);
	}
}

void MainWindowShow::showOrHideBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<Ui::BoxContent>,
			std::unique_ptr<Ui::LayerWidget>> &&layer,
		Ui::LayerOptions options,
		anim::type animated) const {
	if (const auto window = _window.get()) {
		window->window().widget()->showOrHideBoxOrLayer(
			std::move(layer),
			options,
			animated);
	}
}

not_null<QWidget*> MainWindowShow::toastParent() const {
	const auto window = _window.get();
	Assert(window != nullptr);
	return window->widget()->bodyWidget();
}

bool MainWindowShow::valid() const {
	return !_window.empty();
}

MainWindowShow::operator bool() const {
	return valid();
}

Main::Session &MainWindowShow::session() const {
	const auto window = _window.get();
	Assert(window != nullptr);
	return window->session();
}

bool MainWindowShow::paused(ChatHelpers::PauseReason reason) const {
	const auto window = _window.get();
	return window && window->isGifPausedAtLeastFor(reason);
}

rpl::producer<> MainWindowShow::pauseChanged() const {
	const auto window = _window.get();
	if (!window) {
		return rpl::never<>();
	}
	return window->gifPauseLevelChanged();
}

rpl::producer<bool> MainWindowShow::adjustShadowLeft() const {
	const auto window = _window.get();
	if (!window) {
		return rpl::single(false);
	}
	return window->adaptive().value(
	) | rpl::map([=] {
		return !window->adaptive().isOneColumn();
	});
}

SendMenu::Details MainWindowShow::sendMenuDetails() const {
	const auto window = _window.get();
	if (!window) {
		return SendMenu::Details();
	}
	return window->content()->sendMenuDetails();
}

bool MainWindowShow::showMediaPreview(
		Data::FileOrigin origin,
		not_null<DocumentData*> document) const {
	const auto window = _window.get();
	return window && window->widget()->showMediaPreview(origin, document);
}

bool MainWindowShow::showMediaPreview(
		Data::FileOrigin origin,
		not_null<PhotoData*> photo) const {
	const auto window = _window.get();
	return window && window->widget()->showMediaPreview(origin, photo);
}

void MainWindowShow::processChosenSticker(
		ChatHelpers::FileChosen &&chosen) const {
	if (const auto window = _window.get()) {
		Ui::PostponeCall(window, [=, chosen = std::move(chosen)]() mutable {
			window->stickerOrEmojiChosen(std::move(chosen));
		});
	}
}

} // namespace

void ActivateWindow(not_null<SessionController*> controller) {
	Ui::ActivateWindow(controller->widget());
}

bool IsPaused(
		not_null<SessionController*> controller,
		GifPauseReason level) {
	return controller->isGifPausedAtLeastFor(level);
}

Fn<bool()> PausedIn(
		not_null<SessionController*> controller,
		GifPauseReason level) {
	return [=] { return IsPaused(controller, level); };
}

bool operator==(const PeerThemeOverride &a, const PeerThemeOverride &b) {
	return (a.peer == b.peer) && (a.theme == b.theme);
}

bool operator!=(const PeerThemeOverride &a, const PeerThemeOverride &b) {
	return !(a == b);
}

DateClickHandler::DateClickHandler(Dialogs::Key chat, QDate date)
: _chat(chat)
, _weak(chat.topic())
, _date(date) {
}

void DateClickHandler::setDate(QDate date) {
	_date = date;
}

void DateClickHandler::onClick(ClickContext context) const {
	const auto my = context.other.value<ClickHandlerContext>();
	if (const auto window = my.sessionWindow.get()) {
		if (!_chat.topic()) {
			window->showCalendar(_chat, _date);
		} else if (const auto strong = _weak.get()) {
			window->showCalendar(strong, _date);
		}
	}
}

ForumThreadClickHandler::ForumThreadClickHandler(not_null<HistoryItem*> item)
: _thread(resolveThread(item)) {
}

void ForumThreadClickHandler::update(not_null<HistoryItem*> item) {
	_thread = resolveThread(item);
}

void ForumThreadClickHandler::onClick(ClickContext context) const {
	const auto my = context.other.value<ClickHandlerContext>();
	if (const auto window = my.sessionWindow.get()) {
		if (const auto strong = _thread.get()) {
			window->showThread(strong, 0, SectionShow::Way::ClearStack);
		}
	}
}

base::weak_ptr<Data::Thread> ForumThreadClickHandler::resolveThread(
		not_null<HistoryItem*> item) const {
	if (const auto sublist = item->savedSublist()) {
		return sublist;
	} else if (const auto topic = item->topic()) {
		return topic;
	}
	return nullptr;
}

MessageHighlightId SearchHighlightId(const QString &query) {
	auto result = MessageHighlightId{ .quote = { query } };
	if (!result.quote.empty()) {
		result.quoteOffset = kSearchQueryOffsetHint;
	}
	return result;
}

SessionNavigation::SessionNavigation(not_null<Main::Session*> session)
: _session(session)
, _api(&_session->mtp()) {
}

SessionNavigation::~SessionNavigation() = default;

Main::Session &SessionNavigation::session() const {
	return *_session;
}

bool SessionNavigation::showFrozenError() {
	return uiShow()->showFrozenError();
}

void SessionNavigation::showPeerByLink(const PeerByLinkInfo &info) {
	Core::App().hideMediaView();
	if (!info.phone.isEmpty()) {
		resolvePhone(info.phone, [=](not_null<PeerData*> peer) {
			showPeerByLinkResolved(peer, info);
		});
	} else if (!info.chatLinkSlug.isEmpty()) {
		resolveChatLink(info.chatLinkSlug, [=](
				not_null<PeerData*> peer,
				TextWithEntities draft) {
			Data::SetChatLinkDraft(peer, draft);
			showPeerByLinkResolved(peer, info);
		});
	} else if (const auto name = std::get_if<QString>(&info.usernameOrId)) {
		resolveUsername(*name, [=](not_null<PeerData*> peer) {
			if (info.startAutoSubmit) {
				peer->session().api().blockedPeers().unblock(
					peer,
					[=](bool) { showPeerByLinkResolved(peer, info); },
					true);
			} else if (info.joinChannel && peer->isChannel()) {
				peer->session().api().joinChannel(peer->asChannel());
			} else {
				showPeerByLinkResolved(peer, info);
			}
		}, info.referral);
	} else if (const auto id = std::get_if<ChannelId>(&info.usernameOrId)) {
		resolveChannelById(*id, [=](not_null<ChannelData*> channel) {
			showPeerByLinkResolved(channel, info);
		});
	}
}

void SessionNavigation::resolvePhone(
		const QString &phone,
		Fn<void(not_null<PeerData*>)> done) {
	if (const auto peer = _session->data().userByPhone(phone)) {
		done(peer);
		return;
	}
	_api.request(base::take(_resolveRequestId)).cancel();
	_resolveRequestId = _api.request(MTPcontacts_ResolvePhone(
		MTP_string(phone)
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		resolveDone(result, done);
	}).fail([=](const MTP::Error &error) {
		_resolveRequestId = 0;
		if (error.code() == 400) {
			parentController()->show(
				Ui::MakeInformBox(tr::lng_username_by_phone_not_found(
					tr::now,
					lt_phone,
					Ui::FormatPhone(phone))),
				Ui::LayerOption::CloseOther);
		}
	}).send();
}

void SessionNavigation::resolveChatLink(
		const QString &slug,
		Fn<void(not_null<PeerData*> peer, TextWithEntities draft)> done) {
	_api.request(base::take(_resolveRequestId)).cancel();
	_resolveRequestId = _api.request(MTPaccount_ResolveBusinessChatLink(
		MTP_string(slug)
	)).done([=](const MTPaccount_ResolvedBusinessChatLinks &result) {
		_resolveRequestId = 0;
		parentController()->hideLayer();
		const auto &data = result.data();
		_session->data().processUsers(data.vusers());
		_session->data().processChats(data.vchats());

		using namespace Api;
		const auto peerId = peerFromMTP(data.vpeer());
		done(_session->data().peer(peerId), {
			qs(data.vmessage()),
			EntitiesFromMTP(_session, data.ventities().value_or_empty())
		});
	}).fail([=](const MTP::Error &error) {
		_resolveRequestId = 0;
		if (error.code() == 400) {
			showToast(tr::lng_confirm_phone_link_invalid(tr::now));
		}
	}).send();
}

void SessionNavigation::resolveUsername(
		const QString &username,
		Fn<void(not_null<PeerData*>)> done,
		const QString &referral) {
	if (referral.isEmpty()) {
		if (const auto peer = _session->data().peerByUsername(username)) {
			done(peer);
			return;
		}
	}
	_api.request(base::take(_resolveRequestId)).cancel();
	using Flag = MTPcontacts_ResolveUsername::Flag;
	_resolveRequestId = _api.request(MTPcontacts_ResolveUsername(
		MTP_flags(referral.isEmpty() ? Flag() : Flag::f_referer),
		MTP_string(username),
		MTP_string(referral)
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		resolveDone(result, done);
	}).fail([=](const MTP::Error &error) {
		_resolveRequestId = 0;
		if (error.type() == u"STARREF_EXPIRED"_q) {
			parentController()->showToast(tr::lng_star_ref_stopped(tr::now));
		} else if (error.code() == 400) {
			parentController()->show(
				Ui::MakeInformBox(
					tr::lng_username_not_found(tr::now, lt_user, username)),
				Ui::LayerOption::CloseOther);
		}
	}).send();
}

void SessionNavigation::resolveDone(
		const MTPcontacts_ResolvedPeer &result,
		Fn<void(not_null<PeerData*>)> done) {
	_resolveRequestId = 0;
	parentController()->hideLayer();
	result.match([&](const MTPDcontacts_resolvedPeer &data) {
		_session->data().processUsers(data.vusers());
		_session->data().processChats(data.vchats());
		if (const auto peerId = peerFromMTP(data.vpeer())) {
			done(_session->data().peer(peerId));
		}
	});
}

void SessionNavigation::resolveChannelById(
		ChannelId channelId,
		Fn<void(not_null<ChannelData*>)> done) {
	if (const auto channel = _session->data().channelLoaded(channelId)) {
		done(channel);
		return;
	}
	const auto fail = crl::guard(this, [=] {
		uiShow()->showToast(tr::lng_error_post_link_invalid(tr::now));
	});
	_api.request(base::take(_resolveRequestId)).cancel();
	_resolveRequestId = _api.request(MTPchannels_GetChannels(
		MTP_vector<MTPInputChannel>(
			1,
			MTP_inputChannel(MTP_long(channelId.bare), MTP_long(0)))
	)).done([=](const MTPmessages_Chats &result) {
		result.match([&](const auto &data) {
			const auto peer = _session->data().processChats(data.vchats());
			if (peer && peer->id == peerFromChannel(channelId)) {
				done(peer->asChannel());
			} else {
				fail();
			}
		});
	}).fail(fail).send();
}

void SessionNavigation::showMessageByLinkResolved(
		not_null<HistoryItem*> item,
		const PeerByLinkInfo &info) {
	auto params = SectionShow{
		SectionShow::Way::Forward
	};
	params.origin = SectionShow::OriginMessage{
		info.clickFromMessageId
	};
	const auto peer = item->history()->peer;
	const auto topicId = peer->isForum() ? item->topicRootId() : 0;
	if (topicId) {
		const auto messageId = (item->id == topicId) ? MsgId() : item->id;
		showRepliesForMessage(item->history(), topicId, messageId, params);
	} else {
		showPeerHistory(peer, params, item->id);
	}
}

void SessionNavigation::showPeerByLinkResolved(
		not_null<PeerData*> peer,
		const PeerByLinkInfo &info) {
	auto params = SectionShow{
		SectionShow::Way::Forward
	};
	params.origin = SectionShow::OriginMessage{
		info.clickFromMessageId
	};
	if (info.voicechatHash && peer->isChannel()) {
		// First show the channel itself.
		crl::on_main(this, [=] {
			showPeerHistory(peer, params, ShowAtUnreadMsgId);
		});

		// Then try to join the voice chat.
		joinVoiceChatFromLink(peer, info);
		return;
	}
	using Scope = AddBotToGroupBoxController::Scope;
	const auto user = peer->asUser();
	const auto bot = (user && user->isBot()) ? user : nullptr;

	// t.me/username/012345 - we thought it was a channel post link, but
	// after resolving the username we found out it is a bot.
	const auto resolveType = (bot
		&& !info.botAppName.isEmpty()
		&& info.resolveType == ResolveType::Default)
		? ResolveType::BotApp
		: info.resolveType;

	const auto &replies = info.repliesInfo;
	if (const auto threadId = std::get_if<ThreadId>(&replies)) {
		const auto history = peer->owner().history(peer);
		const auto controller = parentController();
		if (const auto forum = peer->forum()) {
			if (controller->windowId().hasChatsList()
				&& !controller->adaptive().isOneColumn()
				&& controller->shownForum().current() != forum
				&& !forum->channel()->useSubsectionTabs()) {
				controller->showForum(forum);
			}
		}
		showRepliesForMessage(
			history,
			threadId->id,
			info.messageId,
			params);
	} else if (const auto commentId = std::get_if<CommentId>(&replies)) {
		showRepliesForMessage(
			session().data().history(peer),
			info.messageId,
			commentId->id,
			params);
	} else if (resolveType == ResolveType::Profile) {
		showPeerInfo(peer, params);
	} else if (resolveType == ResolveType::HashtagSearch) {
		searchMessages(info.text, peer->owner().history(peer));
	} else if (peer->isForum() && resolveType != ResolveType::Boost) {
		const auto itemId = info.messageId;
		if (!itemId) {
			parentController()->showForum(peer->forum(), params);
		} else if (const auto item = peer->owner().message(peer, itemId)) {
			showMessageByLinkResolved(item, info);
		} else {
			const auto callback = crl::guard(this, [=] {
				if (const auto item = peer->owner().message(peer, itemId)) {
					showMessageByLinkResolved(item, info);
				} else {
					showPeerHistory(peer, params, itemId);
				}
			});
			peer->session().api().requestMessageData(
				peer,
				info.messageId,
				callback);
		}
	} else if (info.storyId) {
		const auto storyId = FullStoryId{ peer->id, info.storyId };
		const auto context = (info.storyAlbumId > 0)
			? Data::StoriesContext{ Data::StoriesContextAlbum{
				info.storyAlbumId,
			} }
			: Data::StoriesContext{ Data::StoriesContextSingle() };
		peer->owner().stories().resolve(storyId, crl::guard(this, [=] {
			if (peer->owner().stories().lookup(storyId)) {
				parentController()->openPeerStory(
					peer,
					storyId.story,
					context);
			} else {
				showToast(tr::lng_stories_link_invalid(tr::now));
			}
		}));
	} else if (info.storyAlbumId > 0) {
		showSection(Info::Stories::Make(peer, info.storyAlbumId));
	} else if (info.giftCollectionId > 0) {
		showSection(Info::PeerGifts::Make(peer, info.giftCollectionId));
	} else if (bot && resolveType == ResolveType::BotApp) {
		const auto itemId = info.clickFromMessageId;
		const auto item = _session->data().message(itemId);
		const auto contextPeer = item
			? item->history()->peer
			: bot;
		const auto action = info.clickFromBotWebviewContext
			? info.clickFromBotWebviewContext->action
			: Api::SendAction(bot->owner().history(contextPeer));
		crl::on_main(this, [=] {
			bot->session().attachWebView().open({
				.bot = bot,
				.context = {
					.controller = parentController(),
					.action = action,
					.fullscreen = info.botAppFullScreen,
					.maySkipConfirmation = !info.botAppForceConfirmation,
				},
				.button = { .startCommand = info.startToken },
				.source = InlineBots::WebViewSourceLinkApp{
					.appname = info.botAppName,
					.token = info.startToken,
				},
			});
		});
	} else if (bot && resolveType == ResolveType::ShareGame) {
		Window::ShowShareGameBox(parentController(), bot, info.startToken);
	} else if (bot
		&& (resolveType == ResolveType::AddToGroup
			|| resolveType == ResolveType::AddToChannel)) {
		const auto scope = (resolveType == ResolveType::AddToGroup)
			? (info.startAdminRights ? Scope::GroupAdmin : Scope::All)
			: (resolveType == ResolveType::AddToChannel)
			? Scope::ChannelAdmin
			: Scope::None;
		Assert(scope != Scope::None);

		AddBotToGroupBoxController::Start(
			parentController(),
			bot,
			scope,
			info.startToken,
			info.startAdminRights);
	} else if (resolveType == ResolveType::Mention) {
		if (bot || peer->isChannel()) {
			crl::on_main(this, [=] {
				showPeerHistory(peer, params);
			});
		} else {
			showPeerInfo(peer, params);
		}
	} else if (resolveType == ResolveType::Boost && peer->isChannel()) {
		resolveBoostState(peer->asChannel());
	} else {
		// Show specific posts only in channels / supergroups.
		const auto msgId = peer->isChannel()
			? info.messageId
			: info.startAutoSubmit
			? ShowAndStartBotMsgId
			: (bot && !info.startToken.isEmpty())
			? ShowAndMaybeStartBotMsgId
			: ShowAtUnreadMsgId;
		const auto attachBotUsername = info.attachBotUsername;
		if (bot && bot->botInfo->startToken != info.startToken) {
			bot->botInfo->startToken = info.startToken;
			bot->session().changes().peerUpdated(
				bot,
				Data::PeerUpdate::Flag::BotStartToken);
		}
		if (!attachBotUsername.isEmpty()) {
			crl::on_main(this, [=] {
				const auto history = peer->owner().history(peer);
				showPeerHistory(history, params, msgId);

				peer->session().attachWebView().openByUsername(
					parentController(),
					Api::SendAction(history),
					attachBotUsername,
					info.attachBotToggleCommand.value_or(QString()),
					info.botAppFullScreen);
			});
		} else if (bot && info.attachBotMainOpen) {
			const auto startCommand = info.attachBotToggleCommand.value_or(
				QString());
			bot->session().attachWebView().open({
				.bot = bot,
				.context = {
					.controller = parentController(),
					.fullscreen = info.botAppFullScreen,
				},
				.button = { .startCommand = startCommand },
				.source = InlineBots::WebViewSourceLinkBotProfile{
					.token = startCommand,
					.compact = info.attachBotMainCompact,
				},
			});
		} else if (bot && info.attachBotToggleCommand) {
			const auto itemId = info.clickFromMessageId;
			const auto item = _session->data().message(itemId);
			const auto contextPeer = item
				? item->history()->peer.get()
				: nullptr;
			const auto contextUser = contextPeer
				? contextPeer->asUser()
				: nullptr;
			bot->session().attachWebView().open({
				.bot = bot,
				.context = {
					.controller = parentController(),
					.action = (contextUser
						? Api::SendAction(
							contextUser->owner().history(contextUser))
						: std::optional<Api::SendAction>()),
					.fullscreen = info.botAppFullScreen,
				},
				.button = { .startCommand = *info.attachBotToggleCommand },
				.source = InlineBots::WebViewSourceLinkAttachMenu{
					.choose = info.attachBotChooseTypes,
					.token = *info.attachBotToggleCommand,
				},
			});
		} else {
			const auto draft = info.text;
			const auto historyInNewWindow = info.historyInNewWindow;
			params.videoTimestamp = info.videoTimestamp;
			crl::on_main(this, [=] {
				if (peer->isUser() && !draft.isEmpty()) {
					Data::SetChatLinkDraft(peer, { draft });
				}
				if (historyInNewWindow) {
					const auto window
						= Core::App().ensureSeparateWindowFor(peer);
					const auto controller = window
						? window->sessionController()
						: nullptr;
					if (controller) {
						controller->showPeerHistory(peer, params, msgId);
					} else {
						showPeerHistory(peer, params, msgId);
					}
				} else {
					showPeerHistory(peer, params, msgId);
				}
			});
		}
	}
}

void SessionNavigation::resolveBoostState(
		not_null<ChannelData*> channel,
		int boostsToLift) {
	_boostsToLift = boostsToLift;
	if (_boostStateResolving == channel) {
		return;
	}
	_boostStateResolving = channel;
	_api.request(MTPpremium_GetBoostsStatus(
		channel->input
	)).done([=](const MTPpremium_BoostsStatus &result) {
		if (base::take(_boostStateResolving) != channel) {
			return;
		}
		const auto boosted = std::make_shared<bool>();
		channel->updateLevelHint(result.data().vlevel().v);
		const auto submit = [=](Fn<void(Ui::BoostCounters)> done) {
			applyBoost(channel, [=](Ui::BoostCounters counters) {
				*boosted = true;
				done(counters);
			});
		};
		const auto lifting = base::take(_boostsToLift);
		const auto box = uiShow()->show(Box(Ui::BoostBox, Ui::BoostBoxData{
			.name = channel->name(),
			.boost = ParseBoostCounters(result),
			.features = LookupBoostFeatures(channel),
			.lifting = lifting,
			.allowMulti = (BoostsForGift(_session) > 0),
			.group = channel->isMegagroup(),
		}, submit));
		if (lifting) {
			box->boxClosing() | rpl::start_with_next([=] {
				if (*boosted) {
					channel->updateFullForced();
				}
			}, box->lifetime());
		}
	}).fail([=](const MTP::Error &error) {
		_boostStateResolving = nullptr;
		showToast(u"Error: "_q + error.type());
	}).send();
}

void SessionNavigation::resolveCollectible(
		PeerId ownerId,
		const QString &entity,
		Fn<void(QString)> fail) {
	if (_collectibleEntity == entity) {
		return;
	}
	_collectibleEntity = entity;
	_api.request(base::take(_collectibleRequestId)).cancel();
	_collectibleRequestId = _api.request(MTPfragment_GetCollectibleInfo(
		((Ui::DetectCollectibleType(entity) == Ui::CollectibleType::Phone)
			? MTP_inputCollectiblePhone(MTP_string(entity))
			: MTP_inputCollectibleUsername(MTP_string(entity)))
	)).done([=](const MTPfragment_CollectibleInfo &result) {
		const auto entity = base::take(_collectibleEntity);
		_collectibleRequestId = 0;
		uiShow()->show(Box(
			Ui::CollectibleInfoBox,
			Parse(entity, _session->data().peer(ownerId), result)));
	}).fail([=](const MTP::Error &error) {
		_collectibleEntity = QString();
		_collectibleRequestId = 0;
		if (fail) {
			fail(error.type());
		}
	}).send();
}

void SessionNavigation::resolveConferenceCall(
		QString slug,
		FullMsgId contextId) {
	resolveConferenceCall(std::move(slug), 0, contextId);
}

void SessionNavigation::resolveConferenceCall(
		MsgId inviteMsgId,
		FullMsgId contextId) {
	resolveConferenceCall({}, inviteMsgId, contextId);
}

[[nodiscard]] std::vector<not_null<UserData*>> ExtractParticipantsForInvite(
		HistoryItem *item) {
	if (!item) {
		return {};
	}
	auto result = std::vector<not_null<UserData*>>();
	const auto add = [&](not_null<PeerData*> peer) {
		if (const auto user = peer->asUser()) {
			if (!user->isSelf()
				&& !ranges::contains(result, not_null(user))) {
				result.push_back(user);
			}
		}
	};
	add(item->from());
	const auto media = item->media();
	if (const auto call = media ? media->call() : nullptr) {
		for (const auto &peer : call->otherParticipants) {
			add(peer);
		}
	}
	return result;
}

void SessionNavigation::resolveConferenceCall(
		QString slug,
		MsgId inviteMsgId,
		FullMsgId contextId) {
	_conferenceCallResolveContextId = contextId;
	if (_conferenceCallSlug == slug
		&& _conferenceCallInviteMsgId == inviteMsgId) {
		return;
	}
	_api.request(base::take(_conferenceCallRequestId)).cancel();
	_conferenceCallSlug = slug;
	_conferenceCallInviteMsgId = inviteMsgId;

	const auto limit = 5;
	_conferenceCallRequestId = _api.request(MTPphone_GetGroupCall(
		(inviteMsgId
			? MTP_inputGroupCallInviteMessage(MTP_int(inviteMsgId.bare))
			: MTP_inputGroupCallSlug(MTP_string(slug))),
		MTP_int(limit)
	)).done([=](const MTPphone_GroupCall &result) {
		_conferenceCallRequestId = 0;
		const auto slug = base::take(_conferenceCallSlug);
		const auto inviteMsgId = base::take(_conferenceCallInviteMsgId);
		const auto contextId = base::take(_conferenceCallResolveContextId);
		const auto context = session().data().message(contextId);
		result.data().vcall().match([&](const MTPDgroupCall &data) {
			const auto call = session().data().sharedConferenceCall(
				data.vid().v,
				data.vaccess_hash().v);
			call->processFullCall(result);
			const auto join = [=](Fn<void()> close) {
				const auto &appConfig = call->session().appConfig();
				const auto conferenceLimit = appConfig.confcallSizeLimit();
				if (call->fullCount() >= conferenceLimit) {
					showToast(tr::lng_confcall_participants_limit(tr::now));
				} else {
					Core::App().calls().startOrJoinConferenceCall({
						.call = call,
						.linkSlug = slug,
						.joinMessageId = inviteMsgId,
					});
					close();
				}
			};
			const auto inviter = context
				? context->from()->asUser()
				: nullptr;
			if (inviteMsgId && call->participants().empty()) {
				uiShow()->show(Calls::Group::PrepareInviteToEmptyBox(
					call,
					inviteMsgId,
					ExtractParticipantsForInvite(context)));
			} else {
				uiShow()->show(Box(
					Calls::Group::ConferenceCallJoinConfirm,
					call,
					(inviter && !inviter->isSelf()) ? inviter : nullptr,
					join));
			}
		}, [&](const MTPDgroupCallDiscarded &data) {
			if (inviteMsgId) {
				uiShow()->show(
					Calls::Group::PrepareCreateCallBox(
						parentController(),
						nullptr,
						inviteMsgId,
						ExtractParticipantsForInvite(context)));
			} else {
				showToast(tr::lng_confcall_link_inactive(tr::now));
			}
		});
	}).fail([=] {
		_conferenceCallRequestId = 0;
		_conferenceCallSlug = QString();
		const auto contextId = base::take(_conferenceCallResolveContextId);
		const auto context = session().data().message(contextId);
		const auto inviteMsgId = base::take(_conferenceCallInviteMsgId);
		if (inviteMsgId) {
			uiShow()->show(
				Calls::Group::PrepareCreateCallBox(
					parentController(),
					nullptr,
					inviteMsgId,
					ExtractParticipantsForInvite(context)));
		} else {
			showToast(tr::lng_confcall_link_inactive(tr::now));
		}
	}).send();
}

void SessionNavigation::applyBoost(
		not_null<ChannelData*> channel,
		Fn<void(Ui::BoostCounters)> done) {
	_api.request(MTPpremium_GetMyBoosts(
	)).done([=](const MTPpremium_MyBoosts &result) {
		const auto &data = result.data();
		_session->data().processUsers(data.vusers());
		_session->data().processChats(data.vchats());
		const auto slots = ParseForChannelBoostSlots(
			channel,
			data.vmy_boosts().v);
		if (!slots.free.empty()) {
			applyBoostsChecked(channel, { slots.free.front() }, done);
		} else if (slots.other.empty()) {
			if (!slots.already.empty()) {
				if (const auto receive = BoostsForGift(_session)) {
					const auto again = true;
					const auto name = channel->name();
					uiShow()->show(
						Box(Ui::GiftForBoostsBox, name, receive, again));
				} else {
					uiShow()->show(
						Box(Ui::BoostBoxAlready, channel->isMegagroup()));
				}
			} else if (!_session->premium()) {
				const auto group = channel->isMegagroup();
				uiShow()->show(Box(Ui::PremiumForBoostsBox, group, [=] {
					const auto id = peerToChannel(channel->id).bare;
					Settings::ShowPremium(
						parentController(),
						"channel_boost__" + QString::number(id));
				}));
			} else if (const auto receive = BoostsForGift(_session)) {
				const auto again = false;
				const auto name = channel->name();
				uiShow()->show(
					Box(Ui::GiftForBoostsBox, name, receive, again));
			} else {
				uiShow()->show(
					Box(Ui::GiftedNoBoostsBox, channel->isMegagroup()));
			}
			done({});
		} else {
			const auto weak = std::make_shared<base::weak_qptr<Ui::BoxContent>>();
			const auto reassign = [=](
					std::vector<int> slots,
					int groups,
					int channels) {
				const auto count = int(slots.size());
				const auto callback = [=](Ui::BoostCounters counters) {
					if (const auto strong = weak->get()) {
						strong->closeBox();
					}
					done(counters);
					uiShow()->showToast(tr::lng_boost_reassign_done(
						tr::now,
						lt_count,
						count,
						lt_channels,
						(!groups
							? tr::lng_boost_reassign_channels
							: !channels
							? tr::lng_boost_reassign_groups
							: tr::lng_boost_reassign_mixed)(
									tr::now,
									lt_count,
									groups + channels)));
				};
				applyBoostsChecked(
					channel,
					slots,
					crl::guard(this, callback));
			};
			*weak = uiShow()->show(ReassignBoostsBox(
				channel,
				slots.other,
				reassign,
				[=] { done({}); }));
		}
	}).fail([=](const MTP::Error &error) {
		const auto type = error.type();
		showToast(u"Error: "_q + type);
		done({});
	}).handleFloodErrors().send();
}

void SessionNavigation::applyBoostsChecked(
		not_null<ChannelData*> channel,
		std::vector<int> slots,
		Fn<void(Ui::BoostCounters)> done) {
	auto mtp = MTP_vector_from_range(ranges::views::all(
		slots
	) | ranges::views::transform([](int slot) {
		return MTP_int(slot);
	}));
	_api.request(MTPpremium_ApplyBoost(
		MTP_flags(MTPpremium_ApplyBoost::Flag::f_slots),
		std::move(mtp),
		channel->input
	)).done([=](const MTPpremium_MyBoosts &result) {
		_api.request(MTPpremium_GetBoostsStatus(
			channel->input
		)).done([=](const MTPpremium_BoostsStatus &result) {
			channel->updateLevelHint(result.data().vlevel().v);
			done(ParseBoostCounters(result));
		}).fail([=](const MTP::Error &error) {
			showToast(u"Error: "_q + error.type());
			done({});
		}).send();
	}).fail([=](const MTP::Error &error) {
		showToast(u"Error: "_q + error.type());
		done({});
	}).send();
}

void SessionNavigation::joinVoiceChatFromLink(
		not_null<PeerData*> peer,
		const PeerByLinkInfo &info) {
	Expects(info.voicechatHash.has_value());

	const auto bad = crl::guard(this, [=] {
		uiShow()->showToast(tr::lng_group_invite_bad_link(tr::now));
	});
	const auto hash = *info.voicechatHash;
	_api.request(base::take(_resolveRequestId)).cancel();
	_resolveRequestId = _api.request(
		MTPchannels_GetFullChannel(peer->asChannel()->inputChannel)
	).done([=](const MTPmessages_ChatFull &result) {
		_session->api().processFullPeer(peer, result);
		const auto call = peer->groupCall();
		if (!call) {
			bad();
			return;
		}
		const auto join = [=] {
			parentController()->startOrJoinGroupCall(
				peer,
				{ hash, Calls::StartGroupCallArgs::JoinConfirm::Always });
		};
		if (call->loaded()) {
			join();
			return;
		}
		const auto id = call->id();
		const auto limit = 5;
		_resolveRequestId = _api.request(
			MTPphone_GetGroupCall(call->input(), MTP_int(limit))
		).done([=](const MTPphone_GroupCall &result) {
			if (const auto now = peer->groupCall(); now && now->id() == id) {
				if (!now->loaded()) {
					now->processFullCall(result);
				}
				join();
			} else {
				bad();
			}
		}).fail(bad).send();
	}).send();
}

void SessionNavigation::showRepliesForMessage(
		not_null<History*> history,
		MsgId rootId,
		MsgId commentId,
		const SectionShow &params) {
	if (const auto topic = history->peer->forumTopicFor(rootId)) {
		auto replies = topic->replies();
		if (replies->unreadCountKnown()) {
			using namespace HistoryView;
			auto memento = std::make_shared<ChatMemento>(
				ChatViewId{
					.history = history,
					.repliesRootId = rootId,
				},
				commentId,
				params.highlight);
			memento->setFromTopic(topic);
			showSection(std::move(memento), params);
			return;
		}
	}
	if (_showingRepliesRequestId
		&& _showingRepliesHistory == history.get()
		&& _showingRepliesRootId == rootId) {
		return;
	} else if (!history->peer->asChannel()) {
		// HistoryView::ChatWidget replies right now handles only channels.
		return;
	}
	_api.request(base::take(_showingRepliesRequestId)).cancel();

	const auto postPeer = history->peer;
	_showingRepliesHistory = history;
	_showingRepliesRootId = rootId;
	_showingRepliesRequestId = _api.request(
		MTPmessages_GetDiscussionMessage(
			history->peer->input,
			MTP_int(rootId))
	).done([=](const MTPmessages_DiscussionMessage &result) {
		_showingRepliesRequestId = 0;
		result.match([&](const MTPDmessages_discussionMessage &data) {
			_session->data().processUsers(data.vusers());
			_session->data().processChats(data.vchats());
			_session->data().processMessages(
				data.vmessages(),
				NewMessageType::Existing);
			const auto list = data.vmessages().v;
			const auto deleted = list.isEmpty();
			const auto comments = history->peer->isBroadcast();
			if (comments && deleted) {
				return;
			}
			const auto id = deleted ? rootId : IdFromMessage(list.front());
			const auto peer = deleted
				? history->peer->id
				: PeerFromMessage(list.front());
			if (!peer || !id) {
				return;
			}
			auto item = deleted
				? nullptr
				: _session->data().message(peer, id);
			if (comments && !item) {
				return;
			}
			auto &groups = _session->data().groups();
			if (const auto group = item ? groups.find(item) : nullptr) {
				item = group->items.front();
			}
			if (comments) {
				const auto post = _session->data().message(postPeer, rootId);
				if (post) {
					post->setCommentsItemId(item->fullId());
					if (const auto maxId = data.vmax_id()) {
						post->setCommentsMaxId(maxId->v);
					}
					post->setCommentsInboxReadTill(
						data.vread_inbox_max_id().value_or_empty());
				}
			}
			if (deleted || item) {
				using namespace HistoryView;
				auto memento = item
					? std::make_shared<ChatMemento>(
						ChatMemento::Comments(),
						item,
						commentId)
					: std::make_shared<ChatMemento>(ChatViewId{
						.history = history,
						.repliesRootId = rootId,
					}, commentId);
				memento->setReadInformation(
					data.vread_inbox_max_id().value_or_empty(),
					data.vunread_count().v,
					data.vread_outbox_max_id().value_or_empty());
				showSection(std::move(memento), params);
			}
		});
	}).fail([=](const MTP::Error &error) {
		_showingRepliesRequestId = 0;
		if (error.type() == u"CHANNEL_PRIVATE"_q
			|| error.type() == u"USER_BANNED_IN_CHANNEL"_q) {
			showToast(tr::lng_group_not_accessible(tr::now));
		} else if (error.type() == u"MSG_ID_INVALID"_q) {
			showToast(tr::lng_message_not_found(tr::now));
		}
	}).send();
}

void SessionNavigation::showPeerInfo(
		PeerId peerId,
		const SectionShow &params) {
	showPeerInfo(_session->data().peer(peerId), params);
}

void SessionNavigation::showTopic(
		not_null<Data::ForumTopic*> topic,
		MsgId itemId,
		const SectionShow &params) {
	return showRepliesForMessage(
		topic->history(),
		topic->rootId(),
		itemId,
		params);
}

void SessionNavigation::showSublist(
		not_null<Data::SavedSublist*> sublist,
		MsgId itemId,
		const SectionShow &params) {
	using namespace HistoryView;
	auto memento = std::make_shared<ChatMemento>(
		ChatViewId{
			.history = sublist->owningHistory(),
			.sublist = sublist,
		},
		itemId,
		params.highlight);
	showSection(std::move(memento), params);
}

void SessionNavigation::showThread(
		not_null<Data::Thread*> thread,
		MsgId itemId,
		const SectionShow &params) {
	if (const auto topic = thread->asTopic()) {
		showTopic(topic, itemId, params);
	} else if (const auto sublist = thread->asSublist()) {
		showSublist(sublist, itemId, params);
	} else {
		showPeerHistory(thread->asHistory(), params, itemId);
	}
	if (parentController()->activeChatCurrent().thread() == thread) {
		parentController()->content()->hideDragForwardInfo();
	}
}

void SessionNavigation::showPeerInfo(
		not_null<PeerData*> peer,
		const SectionShow &params) {
	//if (Adaptive::ThreeColumn()
	//	&& !Core::App().settings().thirdSectionInfoEnabled()) {
	//	Core::App().settings().setThirdSectionInfoEnabled(true);
	//	Core::App().saveSettingsDelayed();
	//}
	showSection(std::make_shared<Info::Memento>(peer), params);
}

void SessionNavigation::showPeerInfo(
		not_null<Data::Thread*> thread,
		const SectionShow &params) {
	if (const auto topic = thread->asTopic()) {
		showSection(std::make_shared<Info::Memento>(topic), params);
	} else if (const auto sublist = thread->asSublist()
		; sublist && sublist->parentChat()) {
		showPeerInfo(sublist->sublistPeer()->id, params);
	} else {
		showPeerInfo(thread->peer()->id, params);
	}
}

void SessionNavigation::showPeerHistory(
		not_null<PeerData*> peer,
		const SectionShow &params,
		MsgId msgId) {
	showPeerHistory(peer->id, params, msgId);
}

void SessionNavigation::showPeerHistory(
		not_null<History*> history,
		const SectionShow &params,
		MsgId msgId) {
	showPeerHistory(history->peer->id, params, msgId);
}

void SessionNavigation::showByInitialId(
		const SectionShow &params,
		MsgId msgId) {
	const auto parent = parentController();
	const auto id = parent->window().id();
	auto instant = params;
	instant.animated = anim::type::instant;
	switch (id.type) {
	case SeparateType::Archive:
		clearSectionStack(instant);
		parent->openFolder(id.folder());
		break;
	case SeparateType::Forum:
		clearSectionStack(instant);
		parent->showForum(id.forum(), instant);
		break;
	case SeparateType::Primary:
		clearSectionStack(instant);
		break;
	case SeparateType::Chat:
		showThread(id.thread, msgId, instant);
		break;
	case SeparateType::SharedMedia: {
		clearSectionStack(instant);
		const auto type = id.sharedMediaType;
		const auto topic = id.thread->asTopic();
		showSection(
			(topic
				? std::make_shared<Info::Memento>(topic, type)
				: std::make_shared<Info::Memento>(id.thread->peer(), type)),
			instant);
		parent->widget()->setMaximumWidth(st::maxWidthSharedMediaWindow);
		break;
	}
	case SeparateType::SavedSublist:
		using namespace HistoryView;
		showSection(
			std::make_shared<ChatMemento>(ChatViewId{
				.history = id.sublist()->owningHistory(),
				.sublist = id.sublist(),
			}),
			instant);
		break;
	}
}

void SessionNavigation::showSettings(
		Settings::Type type,
		const SectionShow &params) {
	showSection(
		std::make_shared<Info::Memento>(
			Info::Settings::Tag{ _session->user() },
			Info::Section(type)),
		params);
}

void SessionNavigation::showSettings(const SectionShow &params) {
	showSettings(Settings::Main::Id(), params);
}

void SessionNavigation::showPollResults(
		not_null<PollData*> poll,
		FullMsgId contextId,
		const SectionShow &params) {
	showSection(std::make_shared<Info::Memento>(poll, contextId), params);
}

void SessionNavigation::searchInChat(
		Dialogs::Key inChat,
		PeerData *searchFrom) {
	searchMessages(QString(), inChat, searchFrom);
}

void SessionNavigation::searchMessages(
		const QString &query,
		Dialogs::Key inChat,
		PeerData *searchFrom) {
	parentController()->content()->searchMessages(query, inChat, searchFrom);
}

auto SessionNavigation::showToast(Ui::Toast::Config &&config)
-> base::weak_ptr<Ui::Toast::Instance> {
	return uiShow()->showToast(std::move(config));
}

auto SessionNavigation::showToast(const QString &text, crl::time duration)
-> base::weak_ptr<Ui::Toast::Instance> {
	return uiShow()->showToast(text);
}

auto SessionNavigation::showToast(
	TextWithEntities &&text,
	crl::time duration)
-> base::weak_ptr<Ui::Toast::Instance> {
	return uiShow()->showToast(std::move(text));
}

std::shared_ptr<ChatHelpers::Show> SessionNavigation::uiShow() {
	return parentController()->uiShow();
}

struct SessionController::CachedThemeKey {
	Ui::ChatThemeKey theme;
	QString paper;

	friend inline auto operator<=>(
		const CachedThemeKey&,
		const CachedThemeKey&) = default;
	[[nodiscard]] explicit operator bool() const {
		return theme || !paper.isEmpty();
	}
};

struct SessionController::CachedTheme {
	std::weak_ptr<Ui::ChatTheme> theme;
	std::shared_ptr<Data::DocumentMedia> media;
	Data::WallPaper paper;
	bool basedOnDark = false;
	bool caching = false;
	rpl::lifetime lifetime;
};

SessionController::SessionController(
	not_null<Main::Session*> session,
	not_null<Controller*> window)
: SessionNavigation(session)
, _window(window)
, _emojiInteractions(
	std::make_unique<ChatHelpers::EmojiInteractions>(session))
, _chatPreviewManager(std::make_unique<ChatPreviewManager>(this))
, _isPrimary(window->isPrimary())
, _hasDialogs(window->id().hasChatsList())
, _sendingAnimation(
	std::make_unique<Ui::MessageSendingAnimationController>(this))
, _tabbedSelector(
	std::make_unique<ChatHelpers::TabbedSelector>(
		_window->widget(),
		uiShow(),
		GifPauseReason::TabbedPanel))
, _invitePeekTimer([=] { checkInvitePeek(); })
, _activeChatsFilter(session->data().chatsFilters().defaultId())
, _openedFolder(window->id().folder())
, _defaultChatTheme(std::make_shared<Ui::ChatTheme>())
, _chatStyle(std::make_unique<Ui::ChatStyle>(session->colorIndicesValue())) {
	init();

	_chatStyleTheme = _defaultChatTheme;
	_chatStyle->apply(_defaultChatTheme.get());

	pushDefaultChatBackground();
	Theme::Background()->updates(
	) | rpl::start_with_next([=](const Theme::BackgroundUpdate &update) {
		if (update.type == Theme::BackgroundUpdate::Type::New
			|| update.type == Theme::BackgroundUpdate::Type::Changed) {
			pushDefaultChatBackground();
		}
	}, _lifetime);
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		for (auto &[key, value] : _customChatThemes) {
			if (!key.theme.id) {
				value.theme.reset();
			}
		}
	}, _lifetime);

	_authedName = session->user()->name();
	session->changes().peerUpdates(
		Data::PeerUpdate::Flag::FullInfo
		| Data::PeerUpdate::Flag::Name
	) | rpl::filter([=](const Data::PeerUpdate &update) {
		if (update.flags & Data::PeerUpdate::Flag::Name) {
			const auto user = session->user();
			if (update.peer == user) {
				_authedName = user->name();
				const auto &settings = Core::App().settings();
				if (!settings.windowTitleContent().hideAccountName) {
					widget()->updateTitle();
				}
			}
		}
		return (update.flags & Data::PeerUpdate::Flag::FullInfo)
			&& (update.peer == _showEditPeer);
	}) | rpl::start_with_next([=] {
		show(Box<EditPeerInfoBox>(this, base::take(_showEditPeer)));
	}, lifetime());

	session->data().chatsListChanges(
	) | rpl::filter([=](Data::Folder *folder) {
		return (folder != nullptr)
			&& (folder == _openedFolder.current())
			&& folder->chatsList()->indexed()->empty()
			&& !folder->storiesCount();
	}) | rpl::start_with_next([=](Data::Folder *folder) {
		folder->updateChatListSortPosition();
		closeFolder();
	}, lifetime());

	const auto processFiltersMenu = [this] {
		if (SessionNavigation::session().data().chatsFilters().has()) {
			const auto isHorizontal
				= Core::App().settings().chatFiltersHorizontal()
					|| !enoughSpaceForFilters();
			content()->toggleFiltersMenu(isHorizontal);
			toggleFiltersMenu(!isHorizontal);
		} else {
			content()->toggleFiltersMenu(false);
			toggleFiltersMenu(false);
		}
	};
	rpl::merge(
		enoughSpaceForFiltersValue() | rpl::skip(1) | rpl::to_empty,
		Core::App().settings().chatFiltersHorizontalChanges() | rpl::to_empty,
		session->data().chatsFilters().changed()
	) | rpl::start_with_next([=] {
		if (!_filtersActivated) {
			processFiltersMenu();
		}
		checkOpenedFilter();
		crl::on_main(this, processFiltersMenu);
	}, lifetime());

	session->data().itemIdChanged(
	) | rpl::start_with_next([=](Data::Session::IdChange change) {
		const auto current = _activeChatEntry.current();
		if (const auto topic = current.key.topic()) {
			if (topic->rootId() == change.oldId) {
				setActiveChatEntry({
					Dialogs::Key(topic->forum()->topicFor(change.newId.msg)),
					current.fullId,
				});
			}
		}
		for (auto &entry : _chatEntryHistory) {
			if (const auto topic = entry.key.topic()) {
				if (topic->rootId() == change.oldId) {
					entry.key = Dialogs::Key(
						topic->forum()->topicFor(change.newId.msg));
				}
			}
		}
	}, lifetime());

	session->api().globalPrivacy().suggestArchiveAndMute(
	) | rpl::take(1) | rpl::start_with_next([=] {
		session->api().globalPrivacy().reload(crl::guard(this, [=] {
			if (!session->api().globalPrivacy().archiveAndMuteCurrent()) {
				suggestArchiveAndMute();
			}
		}));
	}, _lifetime);

	session->downloader().nonPremiumDelays(
	) | rpl::start_with_next([=](DocumentId id) {
		checkNonPremiumLimitToastDownload(id);
	}, _lifetime);

	session->uploader().nonPremiumDelays(
	) | rpl::start_with_next([=](FullMsgId id) {
		checkNonPremiumLimitToastUpload(id);
	}, _lifetime);

	session->addWindow(this);

	crl::on_main(this, [=] {
		activateFirstChatsFilter();
		setupPremiumToast();
	});
}

bool SessionController::skipNonPremiumLimitToast(bool download) const {
	if (session().premium()) {
		return true;
	}
	const auto now = base::unixtime::now();
	const auto last = download
		? session().settings().lastNonPremiumLimitDownload()
		: session().settings().lastNonPremiumLimitUpload();
	const auto delay = session().appConfig().get<int>(
		u"upload_premium_speedup_notify_period"_q,
		3600);
	return (last && now < last + delay && now > last - delay);
}

void SessionController::checkNonPremiumLimitToastDownload(DocumentId id) {
	if (skipNonPremiumLimitToast(true)) {
		return;
	}
	const auto document = session().data().document(id);
	const auto visible = session().data().queryDocumentVisibility(document)
		|| DownloadingDocument(document);
	if (!visible) {
		return;
	}
	content()->showNonPremiumLimitToast(true);
	const auto now = base::unixtime::now();
	session().settings().setLastNonPremiumLimitDownload(now);
	session().saveSettingsDelayed();
}

void SessionController::checkNonPremiumLimitToastUpload(FullMsgId id) {
	if (skipNonPremiumLimitToast(false)) {
		return;
	} else if (const auto item = session().data().message(id)) {
		if (!session().data().queryItemVisibility(item)) {
			return;
		}
		content()->showNonPremiumLimitToast(false);
		const auto now = base::unixtime::now();
		session().settings().setLastNonPremiumLimitUpload(now);
		session().saveSettingsDelayed();
	}
}

void SessionController::suggestArchiveAndMute() {
	const auto weak = base::make_weak(this);
	_window->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_suggest_hide_new_title());
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_suggest_hide_new_about(Ui::Text::RichLangValue),
			st::boxLabel));
		box->addButton(tr::lng_suggest_hide_new_to_settings(), [=] {
			showSettings(Settings::PrivacySecurity::Id());
		});
		box->setCloseByOutsideClick(false);
		box->boxClosing(
		) | rpl::start_with_next([=] {
			crl::on_main(weak, [=] {
				auto &privacy = session().api().globalPrivacy();
				privacy.dismissArchiveAndMuteSuggestion();
			});
		}, box->lifetime());
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

SeparateId SessionController::windowId() const {
	return _window->id();
}

bool SessionController::isPrimary() const {
	return _isPrimary;
}

not_null<::MainWindow*> SessionController::widget() const {
	return _window->widget();
}

auto SessionController::sendingAnimation() const
-> Ui::MessageSendingAnimationController & {
	return *_sendingAnimation;
}

auto SessionController::tabbedSelector() const
-> not_null<ChatHelpers::TabbedSelector*> {
	return _tabbedSelector.get();
}

void SessionController::takeTabbedSelectorOwnershipFrom(
		not_null<QWidget*> parent) {
	if (_tabbedSelector->parent() == parent) {
		if (const auto chats = widget()->sessionContent()) {
			chats->returnTabbedSelector();
		}
		if (_tabbedSelector->parent() == parent) {
			_tabbedSelector->hide();
			_tabbedSelector->setParent(widget());
		}
	}
}

bool SessionController::hasTabbedSelectorOwnership() const {
	return (_tabbedSelector->parent() == widget());
}

void SessionController::showEditPeerBox(PeerData *peer) {
	_showEditPeer = peer;
	session().api().requestFullPeer(peer);
}

void SessionController::init() {
	if (session().supportMode()) {
		session().supportHelper().registerWindow(this);
	}
	setupShortcuts();
}

void SessionController::setupShortcuts() {
	using namespace Shortcuts;

	ChatSwitchRequests(
	) | rpl::filter([=](const ChatSwitchRequest &request) {
		return !window().locked()
			&& (_chatSwitchProcess
				|| (request.started
					&& (Core::App().activeWindow() == &window())));
	}) | rpl::start_with_next([=](const ChatSwitchRequest &request) {
		if (!_chatSwitchProcess) {
			_chatSwitchProcess = std::make_unique<ChatSwitchProcess>(
				widget()->bodyWidget(),
				&session(),
				activeChatCurrent().thread());
			const auto close = [this, raw = _chatSwitchProcess.get()] {
				if (_chatSwitchProcess.get() == raw) {
					base::take(_chatSwitchProcess);
				}
			};

			_chatSwitchProcess->chosen(
			) | rpl::start_with_next([=](not_null<Data::Thread*> thread) {
				close();

				const auto id = SeparateId(thread);
				if (const auto window = Core::App().separateWindowFor(id)) {
					window->activate();
					return;
				}
				jumpToChatListEntry({ Dialogs::Key(thread), FullMsgId() });
			}, _chatSwitchProcess->lifetime());

			_chatSwitchProcess->closeRequests(
			) | rpl::start_with_next(close, _chatSwitchProcess->lifetime());
		}
		_chatSwitchProcess->process(request);
	}, _lifetime);

	Requests(
	) | rpl::filter([=] {
		return (Core::App().activeWindow() == &window())
			&& !isLayerShown()
			&& !window().locked();
	}) | rpl::start_with_next([=](not_null<Request*> request) {
		using C = Command;

		const auto app = &Core::App();
		const auto accountsCount = int(app->domain().accounts().size());
		auto &&accounts = ranges::views::zip(
			kShowAccount,
			ranges::views::ints(0, accountsCount));
		for (const auto &[command, index] : accounts) {
			request->check(command) && request->handle([=] {
				const auto list = app->domain().orderedAccounts();
				if (index >= list.size()) {
					return false;
				}
				const auto account = list[index];
				if (account == &session().account()) {
					return false;
				}
				const auto window = app->separateWindowFor(account);
				if (window) {
					window->activate();
				} else {
					app->domain().maybeActivate(account);
				}
				return true;
			});
		}

		if (!session().supportMode()) {
			return;
		}
		request->check(C::SupportHistoryBack) && request->handle([=] {
			return chatEntryHistoryMove(-1);
		});
		request->check(C::SupportHistoryForward) && request->handle([=] {
			return chatEntryHistoryMove(1);
		});
	}, lifetime());
}

void SessionController::toggleFiltersMenu(bool enabled) {
	if (!_isPrimary || (!enabled == !_filters)) {
		return;
	} else if (enabled) {
		_filters = std::make_unique<FiltersMenu>(
			widget()->bodyWidget(),
			this);
	} else {
		_filters = nullptr;
	}
	_filtersMenuChanged.fire({});
}

rpl::producer<> SessionController::filtersMenuChanged() const {
	return _filtersMenuChanged.events();
}

void SessionController::checkOpenedFilter() {
	activateFirstChatsFilter();
	if (const auto filterId = activeChatsFilterCurrent()) {
		const auto &list = session().data().chatsFilters().list();
		const auto i = ranges::find(list, filterId, &Data::ChatFilter::id);
		if (i == end(list)) {
			setActiveChatsFilter(
				0,
				{ anim::type::normal, anim::activation::background });
		}
	}
}

void SessionController::activateFirstChatsFilter() {
	if (_filtersActivated
		|| !isPrimary()
		|| !session().data().chatsFilters().loaded()) {
		return;
	}
	_filtersActivated = true;
	setActiveChatsFilter(session().data().chatsFilters().defaultId());
}

bool SessionController::uniqueChatsInSearchResults(
		const Dialogs::SearchState &state) const {
	const auto global = (state.tab == Dialogs::ChatSearchTab::MyMessages)
		|| (state.tab == Dialogs::ChatSearchTab::PublicPosts);
	return session().supportMode()
		&& !session().settings().supportAllSearchResults()
		&& (global || !state.inChat);
}

bool SessionController::openFolderInDifferentWindow(
		not_null<Data::Folder*> folder) {
	const auto id = SeparateId(SeparateType::Archive, &session());
	if (const auto separate = Core::App().separateWindowFor(id)) {
		if (separate == _window) {
			return false;
		}
		separate->sessionController()->showByInitialId();
		separate->activate();
		return true;
	}
	return false;
}

void SessionController::openFolder(not_null<Data::Folder*> folder) {
	if (openFolderInDifferentWindow(folder)) {
		return;
	} else if (_openedFolder.current() != folder) {
		resetFakeUnreadWhileOpened();
	}
	if (activeChatsFilterCurrent() != 0) {
		setActiveChatsFilter(0);
	} else if (adaptive().isOneColumn()) {
		clearSectionStack(SectionShow::Way::ClearStack);
	}
	closeForum();
	_openedFolder = folder.get();
}

void SessionController::closeFolder() {
	if (_openedFolder.current()
		&& windowId().type == SeparateType::Archive) {
		Core::App().closeWindow(_window);
		return;
	}
	_openedFolder = nullptr;
}

bool SessionController::showForumInDifferentWindow(
		not_null<Data::Forum*> forum,
		const SectionShow &params) {
	const auto window = Core::App().windowForShowingForum(forum);
	if (window == _window) {
		return false;
	} else if (window) {
		window->sessionController()->showForum(forum, params);
		window->activate();
		return true;
	} else if (windowId().hasChatsList()) {
		return false;
	}
	const auto account = not_null(&session().account());
	auto primary = Core::App().separateWindowFor(account);
	if (!primary) {
		Core::App().domain().activate(account);
		primary = Core::App().separateWindowFor(account);
	}
	if (primary && &primary->account() == account) {
		primary->sessionController()->showForum(forum, params);
		primary->activate();
	}
	return true;
}

void SessionController::showForum(
		not_null<Data::Forum*> forum,
		const SectionShow &params) {
	const auto forced = params.forceTopicsList;
	if (showForumInDifferentWindow(forum, params)) {
		return;
	} else if (!forced && forum->channel()->useSubsectionTabs()) {
		if (const auto active = forum->activeSubsectionThread()) {
			showThread(active, ShowAtUnreadMsgId, params);
		} else {
			showPeerHistory(forum->channel(), params);
		}
		return;
	}
	_shownForumLifetime.destroy();
	if (_shownForum.current() != forum) {
		resetFakeUnreadWhileOpened();
	}
	if (forum
		&& _activeChatEntry.current().key.peer()
		&& adaptive().isOneColumn()) {
		clearSectionStack(params);
	}
	_shownForum = forum.get();
	if (_shownForum.current() != forum) {
		return;
	}
	const auto history = forum->history();
	const auto closeAndShowHistory = [=](bool showOnlyIfEmpty) {
		const auto now = activeChatCurrent().owningHistory();
		const auto showHistory = !now
			|| (!showOnlyIfEmpty && (now == history));
		const auto weak = base::make_weak(this);
		closeForum();
		if (weak && showHistory) {
			showPeerHistory(history, {
				SectionShow::Way::Backward,
				anim::type::normal,
				anim::activation::background,
			});
		}
	};
	content()->showForum(forum, params);
	if (_shownForum.current() != forum) {
		return;
	}

	forum->destroyed(
	) | rpl::start_with_next([=] {
		closeAndShowHistory(false);
	}, _shownForumLifetime);
	if (!forced) {
		using FlagChange = Data::Flags<ChannelDataFlags>::Change;
		forum->channel()->flagsValue(
		) | rpl::start_with_next([=](FlagChange change) {
			if (change.diff & ChannelDataFlag::ForumTabs) {
				if (HistoryView::SubsectionTabs::UsedFor(history)) {
					closeAndShowHistory(true);
				}
			}
		}, _shownForumLifetime);
	}
}

void SessionController::closeForum() {
	if (const auto forum = _shownForum.current()) {
		const auto id = windowId();
		if (id.type == SeparateType::Forum) {
			const auto initial = id.forum();
			if (!initial || initial == forum) {
				Core::App().closeWindow(_window);
			} else {
				showForum(initial);
			}
			return;
		}
	}
	_shownForumLifetime.destroy();
	_shownForum = nullptr;
}

void SessionController::setupPremiumToast() {
	rpl::combine(
		Data::AmPremiumValue(&session()),
		session().changes().peerUpdates(
			Data::PeerUpdate::Flag::FullInfo
		)
	) | rpl::filter([=] {
		return session().user()->isFullLoaded();
	}) | rpl::map([=](bool premium, const auto&) {
		return premium;
	}) | rpl::distinct_until_changed() | rpl::skip(
		1
	) | rpl::filter([=](bool premium) {
		session().mtp().requestConfig();
		return premium;
	}) | rpl::start_with_next([=] {
		MainWindowShow(this).showToast({
			.text = { tr::lng_premium_success(tr::now) },
			.adaptive = true,
		});
	}, _lifetime);
}

const rpl::variable<Data::Folder*> &SessionController::openedFolder() const {
	return _openedFolder;
}

const rpl::variable<Data::Forum*> &SessionController::shownForum() const {
	return _shownForum;
}

void SessionController::setActiveChatEntry(Dialogs::RowDescriptor row) {
	if (windowId().type == SeparateType::SharedMedia) {
		return;
	}
	const auto was = _activeChatEntry.current().key.history();
	const auto now = row.key.history();
	if (was && was != now) {
		_activeHistoryLifetime.destroy();
		was->setFakeUnreadWhileOpened(false);
		_invitePeekTimer.cancel();
	}
	_activeChatEntry = row;
	if (now) {
		now->setFakeUnreadWhileOpened(true);
		if (const auto channel = now->peer->asChannel()
			; channel && !channel->isForum()) {
			Data::PeerFlagValue(
				channel,
				ChannelData::Flag::Forum
			) | rpl::filter([=](bool forum) {
				return forum && !channel->useSubsectionTabs();
			}) | rpl::start_with_next([=] {
				clearSectionStack(
					{ anim::type::normal, anim::activation::background });
				showForum(channel->forum(),
					{ anim::type::normal, anim::activation::background });
			}, _activeHistoryLifetime);
		}
	}
	if (const auto thread = row.key.thread()) {
		session().recentPeers().chatOpenPush(thread);
	}
	if (session().supportMode()) {
		pushToChatEntryHistory(row);
	}
	checkInvitePeek();
}

void SessionController::checkInvitePeek() {
	const auto history = activeChatCurrent().history();
	if (!history) {
		return;
	}
	const auto channel = history->peer->asChannel();
	if (!channel) {
		return;
	}
	const auto expires = channel->invitePeekExpires();
	if (!expires) {
		return;
	}
	const auto now = base::unixtime::now();
	if (expires > now) {
		_invitePeekTimer.callOnce((expires - now) * crl::time(1000));
		return;
	}
	const auto hash = channel->invitePeekHash();
	channel->clearInvitePeek();
	Api::CheckChatInvite(this, hash, channel);
}

void SessionController::resetFakeUnreadWhileOpened() {
	if (const auto history = _activeChatEntry.current().key.history()) {
		history->setFakeUnreadWhileOpened(false);
	}
}

bool SessionController::chatEntryHistoryMove(int steps) {
	if (_chatEntryHistory.empty()) {
		return false;
	}
	const auto position = _chatEntryHistoryPosition + steps;
	if (!base::in_range(position, 0, int(_chatEntryHistory.size()))) {
		return false;
	}
	_chatEntryHistoryPosition = position;
	return jumpToChatListEntry(_chatEntryHistory[position]);
}

bool SessionController::jumpToChatListEntry(Dialogs::RowDescriptor row) {
	if (const auto thread = row.key.thread()) {
		showThread(
			thread,
			row.fullId.msg,
			SectionShow::Way::ClearStack);
		return true;
	}
	return false;
}

void SessionController::setDialogsEntryState(
		Dialogs::EntryState state) {
	_dialogsEntryState = state;
}

Dialogs::EntryState SessionController::dialogsEntryStateCurrent() const {
	return _dialogsEntryState.current();
}

auto SessionController::dialogsEntryStateValue() const
-> rpl::producer<Dialogs::EntryState> {
	return _dialogsEntryState.value();
}

bool SessionController::switchInlineQuery(
		Dialogs::EntryState to,
		not_null<UserData*> bot,
		const QString &query) {
	Expects(to.key.owningHistory() != nullptr);

	using Section = Dialogs::EntryState::Section;

	const auto thread = to.key.thread();
	if (!thread || !Data::CanSend(thread, ChatRestriction::SendInline)) {
		show(Ui::MakeInformBox(tr::lng_inline_switch_cant()));
		return false;
	}

	const auto history = to.key.owningHistory();
	const auto textWithTags = TextWithTags{
		'@' + bot->username() + ' ' + query,
		TextWithTags::Tags(),
	};
	MessageCursor cursor = {
		int(textWithTags.text.size()),
		int(textWithTags.text.size()),
		Ui::kQFixedMax
	};
	if (to.currentReplyTo.messageId.msg == to.currentReplyTo.topicRootId
		&& to.currentReplyTo.quote.empty()) {
		to.currentReplyTo.messageId.msg = MsgId();
	}
	if (!history->suggestDraftAllowed()) {
		to.currentSuggest = SuggestPostOptions();
	}
	auto draft = std::make_unique<Data::Draft>(
		textWithTags,
		to.currentReplyTo,
		to.currentSuggest,
		cursor,
		Data::WebPageDraft());

	auto params = Window::SectionShow();
	params.reapplyLocalDraft = true;
	if (to.section == Section::Scheduled) {
		history->setDraft(Data::DraftKey::Scheduled(), std::move(draft));
		showSection(
			std::make_shared<HistoryView::ScheduledMemento>(history),
			params);
	} else {
		const auto topicRootId = to.currentReplyTo.topicRootId;
		const auto monoforumPeerId = to.currentReplyTo.monoforumPeerId;
		history->setLocalDraft(std::move(draft));
		history->clearLocalEditDraft(topicRootId, monoforumPeerId);
		if (to.section == Section::Replies) {
			const auto commentId = MsgId();
			showRepliesForMessage(history, topicRootId, commentId, params);
		} else {
			showPeerHistory(history->peer, params);
		}
	}
	return true;
}

bool SessionController::switchInlineQuery(
		not_null<Data::Thread*> thread,
		not_null<UserData*> bot,
		const QString &query) {
	const auto entryState = Dialogs::EntryState{
		.key = thread,
		.section = (thread->asTopic()
			? Dialogs::EntryState::Section::Replies
			: Dialogs::EntryState::Section::History),
		.currentReplyTo = { .topicRootId = thread->topicRootId() },
	};
	return switchInlineQuery(entryState, bot, query);
}

Dialogs::RowDescriptor SessionController::resolveChatNext(
		Dialogs::RowDescriptor from) const {
	return content()->resolveChatNext(from);
}

Dialogs::RowDescriptor SessionController::resolveChatPrevious(
		Dialogs::RowDescriptor from) const {
	return content()->resolveChatPrevious(from);
}

void SessionController::pushToChatEntryHistory(Dialogs::RowDescriptor row) {
	if (!_chatEntryHistory.empty()
		&& _chatEntryHistory[_chatEntryHistoryPosition] == row) {
		return;
	}
	_chatEntryHistory.resize(++_chatEntryHistoryPosition);
	_chatEntryHistory.push_back(row);
	if (_chatEntryHistory.size() > kMaxChatEntryHistorySize) {
		_chatEntryHistory.pop_front();
		--_chatEntryHistoryPosition;
	}
}

void SessionController::setActiveChatEntry(Dialogs::Key key) {
	setActiveChatEntry({ key, FullMsgId() });
}

Dialogs::RowDescriptor SessionController::activeChatEntryCurrent() const {
	return _activeChatEntry.current();
}

Dialogs::Key SessionController::activeChatCurrent() const {
	return activeChatEntryCurrent().key;
}

auto SessionController::activeChatEntryChanges() const
-> rpl::producer<Dialogs::RowDescriptor> {
	return _activeChatEntry.changes();
}

rpl::producer<Dialogs::Key> SessionController::activeChatChanges() const {
	return activeChatEntryChanges(
	) | rpl::map([](const Dialogs::RowDescriptor &value) {
		return value.key;
	}) | rpl::distinct_until_changed();
}

auto SessionController::activeChatEntryValue() const
-> rpl::producer<Dialogs::RowDescriptor> {
	return _activeChatEntry.value();
}

rpl::producer<Dialogs::Key> SessionController::activeChatValue() const {
	return activeChatEntryValue(
	) | rpl::map([](const Dialogs::RowDescriptor &value) {
		return value.key;
	}) | rpl::distinct_until_changed();
}

void SessionController::enableGifPauseReason(GifPauseReason reason) {
	if (!(_gifPauseReasons & reason)) {
		auto notify = (static_cast<int>(_gifPauseReasons) < static_cast<int>(reason));
		_gifPauseReasons |= reason;
		if (notify) {
			_gifPauseLevelChanged.fire({});
		}
	}
}

void SessionController::disableGifPauseReason(GifPauseReason reason) {
	if (_gifPauseReasons & reason) {
		_gifPauseReasons &= ~reason;
		if (_gifPauseReasons < reason) {
			_gifPauseLevelChanged.fire({});
		}
	}
}

bool SessionController::isGifPausedAtLeastFor(GifPauseReason reason) const {
	if (reason == GifPauseReason::Any) {
		return (_gifPauseReasons != 0) || !widget()->isActive();
	}
	return (static_cast<int>(_gifPauseReasons) >= 2 * static_cast<int>(reason)) || !widget()->isActive();
}

void SessionController::floatPlayerAreaUpdated() {
	if (const auto main = widget()->sessionContent()) {
		main->floatPlayerAreaUpdated();
	}
}

int SessionController::dialogsSmallColumnWidth() const {
	return st::defaultDialogRow.padding.left()
		+ st::defaultDialogRow.photoSize
		+ st::defaultDialogRow.padding.left();
}

int SessionController::minimalThreeColumnWidth() const {
	return (_hasDialogs ? st::columnMinimalWidthLeft : 0)
		+ st::columnMinimalWidthMain
		+ st::columnMinimalWidthThird;
}

auto SessionController::computeColumnLayout() const -> ColumnLayout {
	auto layout = Adaptive::WindowLayout::OneColumn;

	auto bodyWidth = widget()->bodyWidget()->width() - filtersWidth();
	auto dialogsWidth = 0, chatWidth = 0, thirdWidth = 0;

	auto useOneColumnLayout = [&] {
		auto minimalNormal = st::columnMinimalWidthLeft
			+ st::columnMinimalWidthMain;
		if (_hasDialogs && bodyWidth < minimalNormal) {
			return true;
		}
		return false;
	};

	auto useNormalLayout = [&] {
		// Used if useSmallColumnLayout() == false.
		if (bodyWidth < minimalThreeColumnWidth()) {
			return true;
		}
		if (!Core::App().settings().tabbedSelectorSectionEnabled()
			&& !Core::App().settings().thirdSectionInfoEnabled()) {
			return true;
		}
		return false;
	};

	if (useOneColumnLayout()) {
		dialogsWidth = chatWidth = bodyWidth;
	} else if (useNormalLayout()) {
		layout = Adaptive::WindowLayout::Normal;
		dialogsWidth = countDialogsWidthFromRatio(bodyWidth);
		accumulate_min(dialogsWidth, bodyWidth - st::columnMinimalWidthMain);
		chatWidth = bodyWidth - dialogsWidth;
	} else {
		layout = Adaptive::WindowLayout::ThreeColumn;
		dialogsWidth = countDialogsWidthFromRatio(bodyWidth);
		thirdWidth = countThirdColumnWidthFromRatio(bodyWidth);
		auto shrink = shrinkDialogsAndThirdColumns(
			dialogsWidth,
			thirdWidth,
			bodyWidth);
		dialogsWidth = shrink.dialogsWidth;
		thirdWidth = shrink.thirdWidth;

		chatWidth = bodyWidth - dialogsWidth - thirdWidth;
	}
	return { bodyWidth, dialogsWidth, chatWidth, thirdWidth, layout };
}

int SessionController::countDialogsWidthFromRatio(int bodyWidth) const {
	if (!_hasDialogs) {
		return 0;
	}
	const auto nochat = !mainSectionShown();
	const auto width = bodyWidth
		* Core::App().settings().dialogsWidthRatio(nochat);
	auto result = qRound(width);
	accumulate_max(result, st::columnMinimalWidthLeft);
//	accumulate_min(result, st::columnMaximalWidthLeft);
	return result;
}

int SessionController::countThirdColumnWidthFromRatio(int bodyWidth) const {
	auto result = Core::App().settings().thirdColumnWidth();
	accumulate_max(result, st::columnMinimalWidthThird);
	accumulate_min(result, st::columnMaximalWidthThird);
	return result;
}

SessionController::ShrinkResult SessionController::shrinkDialogsAndThirdColumns(
		int dialogsWidth,
		int thirdWidth,
		int bodyWidth) const {
	auto chatWidth = st::columnMinimalWidthMain;
	if (dialogsWidth + thirdWidth + chatWidth <= bodyWidth) {
		return { dialogsWidth, thirdWidth };
	}
	auto thirdWidthNew = ((bodyWidth - chatWidth) * thirdWidth)
		/ (dialogsWidth + thirdWidth);
	auto dialogsWidthNew = ((bodyWidth - chatWidth) * dialogsWidth)
		/ (dialogsWidth + thirdWidth);
	if (thirdWidthNew < st::columnMinimalWidthThird) {
		thirdWidthNew = st::columnMinimalWidthThird;
		dialogsWidthNew = bodyWidth - thirdWidthNew - chatWidth;
		Assert(!_hasDialogs || dialogsWidthNew >= st::columnMinimalWidthLeft);
	} else if (_hasDialogs && dialogsWidthNew < st::columnMinimalWidthLeft) {
		dialogsWidthNew = st::columnMinimalWidthLeft;
		thirdWidthNew = bodyWidth - dialogsWidthNew - chatWidth;
		Assert(thirdWidthNew >= st::columnMinimalWidthThird);
	}
	return { dialogsWidthNew, thirdWidthNew };
}

bool SessionController::canShowThirdSection() const {
	auto currentLayout = computeColumnLayout();
	auto minimalExtendBy = minimalThreeColumnWidth()
		- currentLayout.bodyWidth;
	return (minimalExtendBy <= widget()->maximalExtendBy());
}

bool SessionController::canShowThirdSectionWithoutResize() const {
	auto currentWidth = computeColumnLayout().bodyWidth;
	return currentWidth >= minimalThreeColumnWidth();
}

bool SessionController::takeThirdSectionFromLayer() {
	return widget()->takeThirdSectionFromLayer();
}

void SessionController::resizeForThirdSection() {
	if (adaptive().isThreeColumn()) {
		return;
	}

	auto &settings = Core::App().settings();
	auto layout = computeColumnLayout();
	auto tabbedSelectorSectionEnabled
		= settings.tabbedSelectorSectionEnabled();
	auto thirdSectionInfoEnabled
		= settings.thirdSectionInfoEnabled();
	settings.setTabbedSelectorSectionEnabled(false);
	settings.setThirdSectionInfoEnabled(false);

	auto wanted = countThirdColumnWidthFromRatio(layout.bodyWidth);
	auto minimal = st::columnMinimalWidthThird;
	auto extendBy = wanted;
	auto extendedBy = [&] {
		// Best - extend by third column without moving the window.
		// Next - extend by minimal third column without moving.
		// Next - show third column inside the window without moving.
		// Last - extend with moving.
		if (widget()->canExtendNoMove(wanted)) {
			return widget()->tryToExtendWidthBy(wanted);
		} else if (widget()->canExtendNoMove(minimal)) {
			extendBy = minimal;
			return widget()->tryToExtendWidthBy(minimal);
		} else if (layout.bodyWidth >= minimalThreeColumnWidth()) {
			return 0;
		}
		return widget()->tryToExtendWidthBy(minimal);
	}();
	if (extendedBy) {
		if (extendBy != settings.thirdColumnWidth()) {
			settings.setThirdColumnWidth(extendBy);
		}
		const auto nochat = !mainSectionShown();
		auto newBodyWidth = layout.bodyWidth + extendedBy;
		auto currentRatio = settings.dialogsWidthRatio(nochat);
		settings.updateDialogsWidthRatio(
			(currentRatio * layout.bodyWidth) / newBodyWidth,
			nochat);
	}
	auto savedValue = (extendedBy == extendBy) ? -1 : extendedBy;
	settings.setThirdSectionExtendedBy(savedValue);

	settings.setTabbedSelectorSectionEnabled(
		tabbedSelectorSectionEnabled);
	settings.setThirdSectionInfoEnabled(
		thirdSectionInfoEnabled);
}

void SessionController::closeThirdSection() {
	auto &settings = Core::App().settings();
	auto newWindowSize = widget()->size();
	auto layout = computeColumnLayout();
	if (layout.windowLayout == Adaptive::WindowLayout::ThreeColumn) {
		const auto nochat = !mainSectionShown();
		auto noResize = widget()->isFullScreen()
			|| widget()->isMaximized();
		auto savedValue = settings.thirdSectionExtendedBy();
		auto extendedBy = (savedValue == -1)
			? layout.thirdWidth
			: savedValue;
		auto newBodyWidth = noResize
			? layout.bodyWidth
			: (layout.bodyWidth - extendedBy);
		auto currentRatio = settings.dialogsWidthRatio(nochat);
		settings.updateDialogsWidthRatio(
			(currentRatio * layout.bodyWidth) / newBodyWidth,
			nochat);
		newWindowSize = QSize(
			widget()->width() + (newBodyWidth - layout.bodyWidth),
			widget()->height());
	}
	settings.setTabbedSelectorSectionEnabled(false);
	settings.setThirdSectionInfoEnabled(false);
	Core::App().saveSettingsDelayed();
	if (widget()->size() != newWindowSize) {
		widget()->resize(newWindowSize);
	} else {
		updateColumnLayout();
	}
}

bool SessionController::canShowSeparateWindow(SeparateId id) const {
	if (const auto thread = id.thread) {
		return thread->peer()->computeUnavailableReason().isEmpty();
	}
	return true;
}

void SessionController::showPeer(not_null<PeerData*> peer, MsgId msgId) {
	const auto currentPeer = activeChatCurrent().peer();
	if (peer && peer->isChannel() && currentPeer != peer) {
		const auto clickedChannel = peer->asChannel();
		if (!clickedChannel->isPublic()
			&& !clickedChannel->amIn()
			&& (!currentPeer->isChannel()
				|| currentPeer->asChannel()->discussionLink()
					!= clickedChannel)) {
			MainWindowShow(this).showToast(peer->isMegagroup()
				? tr::lng_group_not_accessible(tr::now)
				: tr::lng_channel_not_accessible(tr::now));
		} else {
			showPeerHistory(peer->id, SectionShow(), msgId);
		}
	} else {
		showPeerInfo(peer, SectionShow());
	}
}

void SessionController::startOrJoinGroupCall(not_null<PeerData*> peer) {
	startOrJoinGroupCall(peer, {});
}

void SessionController::startOrJoinGroupCall(
		not_null<PeerData*> peer,
		Calls::StartGroupCallArgs args) {
	Core::App().calls().startOrJoinGroupCall(uiShow(), peer, args);
}

void SessionController::showCalendar(Dialogs::Key chat, QDate requestedDate) {
	const auto topic = chat.topic();
	const auto history = chat.owningHistory();
	if (!history) {
		return;
	}
	const auto currentPeerDate = [&] {
		if (topic) {
			if (const auto item = topic->lastMessage()) {
				return base::unixtime::parse(item->date()).date();
			}
			return QDate();
		} else if (history->scrollTopItem) {
			return history->scrollTopItem->dateTime().date();
		} else if (history->loadedAtTop()
			&& !history->isEmpty()
			&& history->peer->migrateFrom()) {
			if (const auto migrated = history->owner().historyLoaded(history->peer->migrateFrom())) {
				if (migrated->scrollTopItem) {
					// We're up in the migrated history.
					// So current date is the date of first message here.
					return history->blocks.front()->messages.front()->dateTime().date();
				}
			}
		} else if (const auto item = history->lastMessage()) {
			return base::unixtime::parse(item->date()).date();
		}
		return QDate();
	}();
	const auto maxPeerDate = [&] {
		if (topic) {
			if (const auto item = topic->lastMessage()) {
				return base::unixtime::parse(item->date()).date();
			}
			return QDate();
		}
		const auto check = history->peer->migrateTo()
			? history->owner().historyLoaded(history->peer->migrateTo())
			: history;
		if (const auto item = check ? check->lastMessage() : nullptr) {
			return base::unixtime::parse(item->date()).date();
		}
		return QDate();
	}();
	const auto minPeerDate = [&] {
		const auto startDate = [&] {
			// Telegram was launched in August 2013 :)
			return QDate(2013, 8, 1);
		};
		if (topic) {
			return base::unixtime::parse(topic->creationDate()).date();
		} else if (const auto chat = history->peer->migrateFrom()) {
			if (const auto history = chat->owner().historyLoaded(chat)) {
				if (history->loadedAtTop()) {
					if (!history->isEmpty()) {
						return history->blocks.front()->messages.front()->dateTime().date();
					}
				} else {
					return startDate();
				}
			}
		}
		if (history->loadedAtTop()) {
			if (!history->isEmpty()) {
				return history->blocks.front()->messages.front()->dateTime().date();
			}
			return QDate::currentDate();
		}
		return startDate();
	}();
	const auto highlighted = !requestedDate.isNull()
		? requestedDate
		: !currentPeerDate.isNull()
		? currentPeerDate
		: QDate::currentDate();
	struct ButtonState {
		enum class Type {
			None,
			Disabled,
			Active,
		};
		Type type = Type::None;
		style::complex_color disabledFg = style::complex_color([] {
			auto result = st::attentionBoxButton.textFg->c;
			result.setAlpha(result.alpha() / 2);
			return result;
		});
		style::RoundButton disabled = st::attentionBoxButton;
	};
	const auto buttonState = std::make_shared<ButtonState>();
	buttonState->disabled.textFg
		= buttonState->disabled.textFgOver
		= buttonState->disabledFg.color();
	buttonState->disabled.ripple.color
		= buttonState->disabled.textBgOver
		= buttonState->disabled.textBg;
	const auto selectionChanged = [=](
			not_null<Ui::CalendarBox*> box,
			std::optional<int> selected) {
		if (!selected.has_value()) {
			buttonState->type = ButtonState::Type::None;
			return;
		}
		const auto type = (*selected > 0)
			? ButtonState::Type::Active
			: ButtonState::Type::Disabled;
		if (buttonState->type == type) {
			return;
		}
		buttonState->type = type;
		box->clearButtons();
		box->addButton(tr::lng_cancel(), [=] {
			box->toggleSelectionMode(false);
		});
		auto text = tr::lng_profile_clear_history();
		const auto button = box->addLeftButton(std::move(text), [=] {
			const auto firstDate = box->selectedFirstDate();
			const auto lastDate = box->selectedLastDate();
			if (!firstDate.isNull()) {
				auto confirm = Box<DeleteMessagesBox>(
					history->peer,
					firstDate,
					lastDate);
				confirm->setDeleteConfirmedCallback(crl::guard(box, [=] {
					box->closeBox();
				}));
				box->getDelegate()->show(std::move(confirm));
			}
		}, (*selected > 0) ? st::attentionBoxButton : buttonState->disabled);
		if (!*selected) {
			button->setPointerCursor(false);
		}
	};
	const auto weak = base::make_weak(this);
	const auto weakTopic = base::make_weak(topic);
	const auto jump = [=](const QDate &date) {
		const auto open = [=](not_null<PeerData*> peer, MsgId id) {
			if (const auto strong = weak.get()) {
				if (!topic) {
					strong->showPeerHistory(
						peer,
						SectionShow::Way::Forward,
						id);
				} else if (const auto strongTopic = weakTopic.get()) {
					strong->showTopic(
						strongTopic,
						id,
						SectionShow::Way::Forward);
					strong->hideLayer(anim::type::normal);
				}
			}
		};
		if (!topic || weakTopic) {
			session().api().resolveJumpToDate(chat, date, open);
		}
	};
	show(Box<Ui::CalendarBox>(Ui::CalendarBoxArgs{
		.month = highlighted,
		.highlighted = highlighted,
		.callback = [=](const QDate &date) { jump(date); },
		.minDate = minPeerDate,
		.maxDate = maxPeerDate,
		.allowsSelection = history->peer->isUser(),
		.selectionChanged = selectionChanged,
	}));
}

void SessionController::showPassportForm(const Passport::FormRequest &request) {
	_passportForm = std::make_unique<Passport::FormController>(
		this,
		request);
	_passportForm->show();
}

void SessionController::clearPassportForm() {
	_passportForm = nullptr;
}

void SessionController::showChooseReportMessages(
		not_null<PeerData*> peer,
		Data::ReportInput reportInput,
		Fn<void(std::vector<MsgId>)> done) const {
	content()->showChooseReportMessages(peer, reportInput, std::move(done));
}

void SessionController::clearChooseReportMessages() const {
	content()->clearChooseReportMessages();
}

void SessionController::showInNewWindow(
		SeparateId id,
		MsgId msgId) {
	if (!canShowSeparateWindow(id)) {
		Assert(id.thread != nullptr);
		showThread(id.thread, msgId, SectionShow::Way::ClearStack);
		return;
	}
	const auto active = activeChatCurrent();
	// windows check active forum / active archive
	const auto fromActive = active.thread()
		? (active.thread() == id.thread && id.type == SeparateType::Chat)
		: false;
	const auto toSeparate = [=] {
		Core::App().ensureSeparateWindowFor(id, msgId);
	};
	if (fromActive) {
		window().preventOrInvoke([=] {
			clearSectionStack();
			toSeparate();
		});
	} else {
		toSeparate();
	}
}

void SessionController::toggleChooseChatTheme(
		not_null<PeerData*> peer,
		std::optional<bool> show) {
	if (showFrozenError()) {
		return;
	}
	content()->toggleChooseChatTheme(peer, show);
}

void SessionController::finishChatThemeEdit(not_null<PeerData*> peer) {
	toggleChooseChatTheme(peer, false);
	const auto weak = base::make_weak(this);
	const auto history = activeChatCurrent().history();
	if (!history || history->peer != peer) {
		showPeerHistory(peer);
	}
	if (weak) {
		hideLayer();
	}
}

void SessionController::updateColumnLayout() const {
	content()->updateColumnLayout();
}

void SessionController::showPeerHistory(
		PeerId peerId,
		const SectionShow &params,
		MsgId msgId) {
	content()->showHistory(peerId, params, msgId);
}

void SessionController::showMessage(
		not_null<const HistoryItem*> item,
		const SectionShow &params) {
	_window->invokeForSessionController(
		&item->history()->session().account(),
		item->history()->peer,
		[&](not_null<SessionController*> controller) {
			if (item->isScheduled()) {
				controller->showSection(
					std::make_shared<HistoryView::ScheduledMemento>(
						item->history()),
					params);
				if (params.activation != anim::activation::background) {
					controller->window().activate();
				}
			} else {
				controller->content()->showMessage(item, params);
			}
		});
}

void SessionController::cancelUploadLayer(not_null<HistoryItem*> item) {
	const auto itemId = item->fullId();
	session().uploader().pause(itemId);
	const auto stopUpload = [=](Fn<void()> close) {
		auto &data = session().data();
		if (const auto item = data.message(itemId)) {
			if (!item->isEditingMedia()) {
				const auto history = item->history();
				item->destroy();
				history->requestChatListMessage();
			} else {
				item->returnSavedMedia();
				session().uploader().cancel(item->fullId());
			}
			data.sendHistoryChangeNotifications();
		}
		session().uploader().unpause();
		close();
	};
	const auto continueUpload = [=](Fn<void()> close) {
		session().uploader().unpause();
		close();
	};

	show(Ui::MakeConfirmBox({
		.text = tr::lng_selected_cancel_sure_this(),
		.confirmed = stopUpload,
		.cancelled = continueUpload,
		.confirmText = tr::lng_box_yes(),
		.cancelText = tr::lng_box_no(),
	}));
}

void SessionController::showSection(
		std::shared_ptr<SectionMemento> memento,
		const SectionShow &params) {
	if (!params.thirdColumn
		&& widget()->showSectionInExistingLayer(memento.get(), params)) {
		return;
	}
	content()->showSection(std::move(memento), params);
}

void SessionController::showBackFromStack(const SectionShow &params) {
	const auto bad = [&] {
		// If we show a currently-being-destroyed topic, then
		// skip it and show back one more.
		const auto topic = _activeChatEntry.current().key.topic();
		return topic && topic->forum()->topicDeleted(topic->rootId());
	};
	do {
		const auto empty = content()->stackIsEmpty();
		const auto shown = content()->showBackFromStack(params);
		if (empty && !shown && content()->stackIsEmpty() && bad()) {
			clearSectionStack(anim::type::instant);
			window().close();
			break;
		}
	} while (bad());
}

void SessionController::showSpecialLayer(
		object_ptr<Ui::LayerWidget> &&layer,
		anim::type animated) {
	widget()->showSpecialLayer(std::move(layer), animated);
}

void SessionController::showLayer(
		std::unique_ptr<Ui::LayerWidget> &&layer,
		Ui::LayerOptions options,
		anim::type animated) {
	_window->showLayer(std::move(layer), options, animated);
}

void SessionController::removeLayerBlackout() {
	widget()->ui_removeLayerBlackout();
}

bool SessionController::isLayerShown() const {
	return _window->isLayerShown();
}

not_null<MainWidget*> SessionController::content() const {
	return widget()->sessionContent();
}

int SessionController::filtersWidth() const {
	return _filters ? st::windowFiltersWidth : 0;
}

bool SessionController::enoughSpaceForFilters() const {
	return widget()->width() >= widget()->minimumWidth() + st::windowFiltersWidth;
}

rpl::producer<bool> SessionController::enoughSpaceForFiltersValue() const {
	return widget()->widthValue() | rpl::map([=] {
		return enoughSpaceForFilters();
	}) | rpl::distinct_until_changed();
}

rpl::producer<FilterId> SessionController::activeChatsFilter() const {
	return _activeChatsFilter.value();
}

FilterId SessionController::activeChatsFilterCurrent() const {
	return _activeChatsFilter.current();
}

void SessionController::setActiveChatsFilter(
		FilterId id,
		const SectionShow &params) {
	if (!isPrimary()) {
		return;
	}
	const auto changed = (activeChatsFilterCurrent() != id);
	if (changed) {
		resetFakeUnreadWhileOpened();
	}
	_activeChatsFilter.force_assign(id);
	if (id || !changed) {
		closeForum();
		closeFolder();
	}
	if (adaptive().isOneColumn()) {
		clearSectionStack(params);
	}
}

void SessionController::showAddContact() {
	_window->show(Box<AddContactBox>(&session()));
}

void SessionController::showNewGroup() {
	_window->show(Box<GroupInfoBox>(this, GroupInfoBox::Type::Group));
}

void SessionController::showNewChannel() {
	_window->show(Box<GroupInfoBox>(this, GroupInfoBox::Type::Channel));
}

Window::Adaptive &SessionController::adaptive() const {
	return _window->adaptive();
}

void SessionController::setConnectingBottomSkip(int skip) {
	_connectingBottomSkip = skip;
}

rpl::producer<int> SessionController::connectingBottomSkipValue() const {
	return _connectingBottomSkip.value();
}

void SessionController::stickerOrEmojiChosen(FileChosen chosen) {
	_stickerOrEmojiChosen.fire(std::move(chosen));
}

auto SessionController::stickerOrEmojiChosen() const
-> rpl::producer<FileChosen> {
	return _stickerOrEmojiChosen.events();
}

base::weak_qptr<Ui::BoxContent> SessionController::show(
		object_ptr<Ui::BoxContent> content,
		Ui::LayerOptions options,
		anim::type animated) {
	return _window->show(std::move(content), options, animated);
}

void SessionController::hideLayer(anim::type animated) {
	_window->hideLayer(animated);
}

void SessionController::openPhoto(
		not_null<PhotoData*> photo,
		MessageContext message,
		const Data::StoriesContext *stories) {
	const auto item = session().data().message(message.id);
	if (openSharedStory(item) || openFakeItemStory(message.id, stories)) {
		return;
	}
	_window->openInMediaView(Media::View::OpenRequest(
		this,
		photo,
		item,
		message.topicRootId,
		message.monoforumPeerId));
}

void SessionController::openPhoto(
		not_null<PhotoData*> photo,
		not_null<PeerData*> peer) {
	_window->openInMediaView(Media::View::OpenRequest(this, photo, peer));
}

void SessionController::openDocument(
		not_null<DocumentData*> document,
		bool showInMediaView,
		MessageContext message,
		const Data::StoriesContext *stories,
		std::optional<TimeId> videoTimestampOverride) {
	const auto item = session().data().message(message.id);
	if (openSharedStory(item) || openFakeItemStory(message.id, stories)) {
		return;
	} else if (showInMediaView) {
		using namespace Media::View;
		const auto saved = session().local().mediaLastPlaybackPosition(
			document->id);
		const auto timestamp = item ? ExtractVideoTimestamp(item) : 0;
		const auto usedTimestamp = videoTimestampOverride
			? ((*videoTimestampOverride) * crl::time(1000))
			: saved
			? saved
			: timestamp
			? (timestamp * crl::time(1000))
			: crl::time();
		_window->openInMediaView(OpenRequest(
			this,
			document,
			item,
			message.topicRootId,
			message.monoforumPeerId,
			false,
			usedTimestamp));
		return;
	}
	Data::ResolveDocument(
		this,
		document,
		item,
		message.topicRootId,
		message.monoforumPeerId);
}

bool SessionController::openSharedStory(HistoryItem *item) {
	if (const auto media = item ? item->media() : nullptr) {
		if (const auto storyId = media->storyId()) {
			const auto story = session().data().stories().lookup(storyId);
			if (story) {
				_window->openInMediaView(::Media::View::OpenRequest(
					this,
					*story,
					Data::StoriesContext{ Data::StoriesContextSingle() }));
			}
			return true;
		}
	}
	return false;
}

bool SessionController::openFakeItemStory(
		FullMsgId fakeItemId,
		const Data::StoriesContext *stories) {
	if (peerIsChat(fakeItemId.peer)
		|| !IsStoryMsgId(fakeItemId.msg)) {
		return false;
	}
	const auto maybeStory = session().data().stories().lookup({
		fakeItemId.peer,
		StoryIdFromMsgId(fakeItemId.msg),
	});
	if (maybeStory) {
		using namespace Data;
		const auto story = *maybeStory;
		const auto context = stories
			? *stories
			: StoriesContext{ StoriesContextSingle() };
		_window->openInMediaView(
			::Media::View::OpenRequest(this, story, context));
	}
	return true;
}

auto SessionController::cachedChatThemeValue(
	const Data::CloudTheme &data,
	const Data::WallPaper &paper,
	Data::CloudThemeType type)
-> rpl::producer<std::shared_ptr<Ui::ChatTheme>> {
	const auto themeKey = Ui::ChatThemeKey{
		data.id,
		(type == Data::CloudThemeType::Dark),
	};
	if (!themeKey && paper.isNull()) {
		return rpl::single(_defaultChatTheme);
	}
	const auto settings = data.settings.find(type);
	if (data.id && settings == end(data.settings)) {
		return rpl::single(_defaultChatTheme);
	}
	if (paper.isNull()
		&& (!settings->second.paper
			|| settings->second.paper->backgroundColors().empty())) {
		return rpl::single(_defaultChatTheme);
	}
	const auto key = CachedThemeKey{
		themeKey,
		!paper.isNull() ? paper.key() : settings->second.paper->key(),
	};
	const auto i = _customChatThemes.find(key);
	if (i != end(_customChatThemes)) {
		if (auto strong = i->second.theme.lock()) {
			pushLastUsedChatTheme(strong);
			return rpl::single(std::move(strong));
		}
	}
	if (i == end(_customChatThemes) || !i->second.caching) {
		cacheChatTheme(key, data, paper, type);
	}
	const auto limit = Data::CloudThemes::TestingColors() ? (1 << 20) : 1;
	using namespace rpl::mappers;
	return rpl::single(
		_defaultChatTheme
	) | rpl::then(_cachedThemesStream.events(
	) | rpl::filter([=](const std::shared_ptr<Ui::ChatTheme> &theme) {
		if (theme->key() != key.theme
			|| theme->background().key != key.paper) {
			return false;
		}
		pushLastUsedChatTheme(theme);
		return true;
	}) | rpl::take(limit));
}

bool SessionController::chatThemeAlreadyCached(
		const Data::CloudTheme &data,
		const Data::WallPaper &paper,
		Data::CloudThemeType type) {
	Expects(paper.document() != nullptr);

	const auto key = CachedThemeKey{
		Ui::ChatThemeKey{
			data.id,
			(type == Data::CloudThemeType::Dark),
		},
		paper.key(),
	};
	const auto i = _customChatThemes.find(key);
	return (i != end(_customChatThemes))
		&& (i->second.theme.lock() != nullptr);
}

void SessionController::pushLastUsedChatTheme(
		const std::shared_ptr<Ui::ChatTheme> &theme) {
	const auto i = ranges::find(_lastUsedCustomChatThemes, theme);
	if (i == end(_lastUsedCustomChatThemes)) {
		if (_lastUsedCustomChatThemes.size() >= kCustomThemesInMemory) {
			_lastUsedCustomChatThemes.pop_back();
		}
		_lastUsedCustomChatThemes.push_front(theme);
	} else if (i != begin(_lastUsedCustomChatThemes)) {
		std::rotate(begin(_lastUsedCustomChatThemes), i, i + 1);
	}
}

not_null<Ui::ChatTheme*> SessionController::currentChatTheme() const {
	if (const auto custom = content()->customChatTheme()) {
		return custom;
	}
	return defaultChatTheme().get();
}

void SessionController::setChatStyleTheme(
		const std::shared_ptr<Ui::ChatTheme> &theme) {
	if (_chatStyleTheme.lock() == theme) {
		return;
	}
	_chatStyleTheme = theme;
	_chatStyle->apply(theme.get());
}

void SessionController::clearCachedChatThemes() {
	_customChatThemes.clear();
}

void SessionController::overridePeerTheme(
		not_null<PeerData*> peer,
		std::shared_ptr<Ui::ChatTheme> theme,
		EmojiPtr emoji) {
	_peerThemeOverride = PeerThemeOverride{
		peer,
		theme ? theme : _defaultChatTheme,
		emoji,
	};
}

void SessionController::clearPeerThemeOverride(not_null<PeerData*> peer) {
	if (_peerThemeOverride.current().peer == peer.get()) {
		_peerThemeOverride = PeerThemeOverride();
	}
}

void SessionController::pushDefaultChatBackground() {
	const auto background = Theme::Background();
	const auto &paper = background->paper();
	_defaultChatTheme->setBackground({
		.prepared = background->prepared(),
		.preparedForTiled = background->preparedForTiled(),
		.gradientForFill = background->gradientForFill(),
		.colorForFill = background->colorForFill(),
		.colors = paper.backgroundColors(),
		.patternOpacity = paper.patternOpacity(),
		.gradientRotation = paper.gradientRotation(),
		.isPattern = paper.isPattern(),
		.tile = background->tile(),
	});
}

void SessionController::cacheChatTheme(
		CachedThemeKey key,
		const Data::CloudTheme &data,
		const Data::WallPaper &paper,
		Data::CloudThemeType type) {
	Expects(data.id != 0 || !paper.isNull());

	const auto dark = (type == Data::CloudThemeType::Dark);
	const auto i = data.settings.find(type);
	Assert((!data.id || (i != end(data.settings)))
		&& (!paper.isNull()
			|| (i->second.paper.has_value()
				&& !i->second.paper->backgroundColors().empty())));
	const auto &use = !paper.isNull() ? paper : *i->second.paper;
	const auto document = use.document();
	const auto media = document ? document->createMediaView() : nullptr;
	use.loadDocument();
	auto &theme = [&]() -> CachedTheme& {
		const auto i = _customChatThemes.find(key);
		if (i != end(_customChatThemes)) {
			i->second.media = media;
			i->second.paper = use;
			i->second.basedOnDark = dark;
			i->second.caching = true;
			return i->second;
		}
		return _customChatThemes.emplace(
			key,
			CachedTheme{
				.media = media,
				.paper = use,
				.basedOnDark = dark,
				.caching = true,
			}).first->second;
	}();
	auto descriptor = Ui::ChatThemeDescriptor{
		.key = key.theme,
		.preparePalette = (data.id
			? Theme::PreparePaletteCallback(dark, i->second.accentColor)
			: Theme::PrepareCurrentPaletteCallback()),
		.backgroundData = backgroundData(theme),
		.bubblesData = PrepareBubblesData(data, type),
		.basedOnDark = dark,
	};
	crl::async([
		this,
		descriptor = std::move(descriptor),
		weak = base::make_weak(this)
	]() mutable {
		crl::on_main(weak,[
			this,
			result = std::make_shared<Ui::ChatTheme>(std::move(descriptor))
		]() mutable {
			result->finishCreateOnMain();
			cacheChatThemeDone(std::move(result));
		});
	});
	if (media && media->loaded(true)) {
		theme.media = nullptr;
	}
}

void SessionController::cacheChatThemeDone(
		std::shared_ptr<Ui::ChatTheme> result) {
	Expects(result != nullptr);

	const auto key = CachedThemeKey{
		result->key(),
		result->background().key,
	};
	const auto i = _customChatThemes.find(key);
	if (i == end(_customChatThemes)) {
		return;
	}
	i->second.caching = false;
	i->second.theme = result;
	if (i->second.media) {
		if (i->second.media->loaded(true)) {
			updateCustomThemeBackground(i->second);
		} else {
			session().downloaderTaskFinished(
			) | rpl::filter([=] {
				const auto i = _customChatThemes.find(key);
				Assert(i != end(_customChatThemes));
				return !i->second.media || i->second.media->loaded(true);
			}) | rpl::start_with_next([=] {
				const auto i = _customChatThemes.find(key);
				Assert(i != end(_customChatThemes));
				updateCustomThemeBackground(i->second);
			}, i->second.lifetime);
		}
	}
	_cachedThemesStream.fire(std::move(result));
}

void SessionController::updateCustomThemeBackground(CachedTheme &theme) {
	const auto guard = gsl::finally([&] {
		theme.lifetime.destroy();
		theme.media = nullptr;
	});
	const auto strong = theme.theme.lock();
	if (!theme.media || !strong || !theme.media->loaded(true)) {
		return;
	}
	const auto key = strong->key();
	const auto weak = base::make_weak(this);
	crl::async([=, data = backgroundData(theme, false)] {
		crl::on_main(weak, [
			=,
			result = Ui::PrepareBackgroundImage(data)
		]() mutable {
			const auto cacheKey = CachedThemeKey{ key, result.key };
			const auto i = _customChatThemes.find(cacheKey);
			if (i != end(_customChatThemes)) {
				if (const auto strong = i->second.theme.lock()) {
					strong->updateBackgroundImageFrom(std::move(result));
				}
			}
		});
	});
}

Ui::ChatThemeBackgroundData SessionController::backgroundData(
		CachedTheme &theme,
		bool generateGradient) const {
	const auto &paper = theme.paper;
	const auto &media = theme.media;
	const auto paperPath = media ? media->owner()->filepath() : QString();
	const auto paperBytes = media ? media->bytes() : QByteArray();
	const auto gzipSvg = media && media->owner()->isPatternWallPaperSVG();
	const auto &colors = paper.backgroundColors();
	const auto isPattern = paper.isPattern();
	const auto patternOpacity = paper.patternOpacity();
	const auto isBlurred = paper.isBlurred();
	const auto gradientRotation = paper.gradientRotation();
	const auto darkModeDimming = isPattern
		? 100
		: std::clamp(paper.patternIntensity(), 0, 100);
	return {
		.key = paper.key(),
		.path = paperPath,
		.bytes = paperBytes,
		.gzipSvg = gzipSvg,
		.colors = colors,
		.isPattern = isPattern,
		.patternOpacity = patternOpacity,
		.darkModeDimming = darkModeDimming,
		.isBlurred = isBlurred,
		.forDarkMode = theme.basedOnDark,
		.generateGradient = generateGradient,
		.gradientRotation = gradientRotation,
	};
}

void SessionController::openPeerStory(
		not_null<PeerData*> peer,
		StoryId storyId,
		Data::StoriesContext context) {
	using namespace Media::View;
	using namespace Data;

	invalidate_weak_ptrs(&_storyOpenGuard);
	auto &stories = session().data().stories();
	const auto from = stories.lookup({ peer->id, storyId });
	if (from) {
		window().openInMediaView(OpenRequest(this, *from, context));
	} else if (from.error() == Data::NoStory::Unknown) {
		const auto done = crl::guard(&_storyOpenGuard, [=] {
			openPeerStory(peer, storyId, context);
		});
		stories.resolve({ peer->id, storyId }, done);
	}
}

void SessionController::openPeerStories(
		PeerId peerId,
		std::optional<Data::StorySourcesList> list) {
	using namespace Media::View;
	using namespace Data;

	invalidate_weak_ptrs(&_storyOpenGuard);
	auto &stories = session().data().stories();
	if (const auto source = stories.source(peerId)) {
		if (const auto idDates = source->toOpen()) {
			openPeerStory(
				source->peer,
				idDates.id,
				(list
					? StoriesContext{ *list }
					: StoriesContext{ StoriesContextPeer() }));
		}
	} else if (const auto peer = session().data().peerLoaded(peerId)) {
		const auto done = crl::guard(&_storyOpenGuard, [=] {
			openPeerStories(peerId, list);
		});
		stories.requestPeerStories(peer, done);
	}
}

HistoryView::PaintContext SessionController::preparePaintContext(
		Ui::ChatPaintContextArgs &&args) {
	const auto visibleAreaTopLocal = content()->mapFromGlobal(
		args.visibleAreaPositionGlobal).y();
	const auto viewport = QRect(
		0,
		args.visibleAreaTop - visibleAreaTopLocal,
		args.visibleAreaWidth,
		content()->height());
	return args.theme->preparePaintContext(
		_chatStyle.get(),
		viewport,
		args.clip,
		isGifPausedAtLeastFor(GifPauseReason::Any));
}

void SessionController::setPremiumRef(const QString &ref) {
	_premiumRef = ref;
}

QString SessionController::premiumRef() const {
	return _premiumRef;
}

bool SessionController::showChatPreview(
		Dialogs::RowDescriptor row,
		Fn<void(bool shown)> callback,
		QPointer<QWidget> parentOverride,
		std::optional<QPoint> positionOverride) {
	return _chatPreviewManager->show(
		std::move(row),
		std::move(callback),
		std::move(parentOverride),
		positionOverride);
}

bool SessionController::scheduleChatPreview(
		Dialogs::RowDescriptor row,
		Fn<void(bool shown)> callback,
	QPointer<QWidget> parentOverride,
	std::optional<QPoint> positionOverride) {
	return _chatPreviewManager->schedule(
		std::move(row),
		std::move(callback),
		std::move(parentOverride),
		positionOverride);
}

void SessionController::cancelScheduledPreview() {
	_chatPreviewManager->cancelScheduled();
}

bool SessionController::contentOverlapped(QWidget *w, QPaintEvent *e) const {
	return widget()->contentOverlapped(w, e);
}

std::shared_ptr<ChatHelpers::Show> SessionController::uiShow() {
	if (!_cachedShow) {
		_cachedShow = std::make_shared<MainWindowShow>(this);
	}
	return _cachedShow;
}

void SessionController::saveSubsectionTabs(
		std::unique_ptr<HistoryView::SubsectionTabs> tabs) {
	_savedSubsectionTabsLifetime.destroy();
	_savedSubsectionTabs = std::move(tabs);
	_savedSubsectionTabs->extractToParent(widget());
	_savedSubsectionTabs->removeRequests() | rpl::start_with_next([=] {
		_savedSubsectionTabs = nullptr;
	}, _savedSubsectionTabsLifetime);
}

auto SessionController::restoreSubsectionTabsFor(
	not_null<Ui::RpWidget*> parent,
	not_null<Data::Thread*> thread)
-> std::unique_ptr<HistoryView::SubsectionTabs> {
	if (!_savedSubsectionTabs) {
		return nullptr;
	} else if (_savedSubsectionTabs->switchTo(thread, parent)) {
		_savedSubsectionTabsLifetime.destroy();
		return base::take(_savedSubsectionTabs);
	}
	return nullptr;
}

void SessionController::dropSubsectionTabs() {
	_savedSubsectionTabsLifetime.destroy();
	base::take(_savedSubsectionTabs);
}

SessionController::~SessionController() {
	resetFakeUnreadWhileOpened();
	dropSubsectionTabs();
}

bool CheckAndJumpToNearChatsFilter(
		not_null<SessionController*> controller,
		bool isNext,
		bool jump) {
	const auto id = controller->activeChatsFilterCurrent();
	const auto session = &controller->session();
	const auto list = &session->data().chatsFilters().list();
	const auto index = int(ranges::find(
		*list,
		id,
		&Data::ChatFilter::id
	) - begin(*list));
	if (index == list->size() && id != 0) {
		return false;
	}
	const auto changed = index + (isNext ? 1 : -1);
	if (changed >= int(list->size()) || changed < 0) {
		return false;
	}
	if (changed > Data::PremiumLimits(session).dialogFiltersCurrent()) {
		return false;
	}
	if (jump) {
		controller->setActiveChatsFilter((changed >= 0)
			? (*list)[changed].id()
			: 0);
	}
	return true;
}

} // namespace Window
