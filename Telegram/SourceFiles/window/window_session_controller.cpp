/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_session_controller.h"

#include "boxes/add_contact_box.h"
#include "boxes/peers/edit_peer_info_box.h"
#include "boxes/peer_list_controllers.h"
#include "window/window_controller.h"
#include "window/main_window.h"
#include "window/window_filters_menu.h"
#include "info/info_memento.h"
#include "info/info_controller.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_replies_section.h"
#include "media/player/media_player_instance.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "data/data_folder.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_changes.h"
#include "data/data_group_call.h"
#include "data/data_chat_filters.h"
#include "passport/passport_form_controller.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/shortcuts.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/click_handler_types.h"
#include "base/unixtime.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/delayed_activation.h"
#include "ui/toast/toast.h"
#include "ui/toasts/common_toasts.h"
#include "calls/calls_instance.h" // Core::App().calls().inCall().
#include "ui/boxes/calendar_box.h"
#include "boxes/confirm_box.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "apiwrap.h"
#include "api/api_chat_invite.h"
#include "api/api_global_privacy.h"
#include "support/support_helper.h"
#include "facades.h"
#include "styles/style_window.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h" // st::boxLabel

namespace Window {
namespace {

constexpr auto kMaxChatEntryHistorySize = 50;

} // namespace

void ActivateWindow(not_null<SessionController*> controller) {
	const auto window = controller->widget();
	window->activateWindow();
	Ui::ActivateWindowDelayed(window);
}

DateClickHandler::DateClickHandler(Dialogs::Key chat, QDate date)
: _chat(chat)
, _date(date) {
}

void DateClickHandler::setDate(QDate date) {
	_date = date;
}

void DateClickHandler::onClick(ClickContext context) const {
	const auto my = context.other.value<ClickHandlerContext>();
	if (const auto window = my.sessionWindow.get()) {
		window->showJumpToDate(_chat, _date);
	}
}

SessionNavigation::SessionNavigation(not_null<Main::Session*> session)
: _session(session) {
}

SessionNavigation::~SessionNavigation() {
	_session->api().request(base::take(_showingRepliesRequestId)).cancel();
	_session->api().request(base::take(_resolveRequestId)).cancel();
}

Main::Session &SessionNavigation::session() const {
	return *_session;
}

void SessionNavigation::showPeerByLink(const PeerByLinkInfo &info) {
	Core::App().hideMediaView();
	if (const auto username = std::get_if<QString>(&info.usernameOrId)) {
		resolveUsername(*username, [=](not_null<PeerData*> peer) {
			showPeerByLinkResolved(peer, info);
		});
	} else if (const auto id = std::get_if<ChannelId>(&info.usernameOrId)) {
		resolveChannelById(*id, [=](not_null<ChannelData*> channel) {
			showPeerByLinkResolved(channel, info);
		});
	}
}

void SessionNavigation::resolveUsername(
		const QString &username,
		Fn<void(not_null<PeerData*>)> done) {
	if (const auto peer = _session->data().peerByUsername(username)) {
		done(peer);
		return;
	}
	_session->api().request(base::take(_resolveRequestId)).cancel();
	_resolveRequestId = _session->api().request(MTPcontacts_ResolveUsername(
		MTP_string(username)
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		_resolveRequestId = 0;
		Ui::hideLayer();
		if (result.type() != mtpc_contacts_resolvedPeer) {
			return;
		}

		const auto &d(result.c_contacts_resolvedPeer());
		_session->data().processUsers(d.vusers());
		_session->data().processChats(d.vchats());
		if (const auto peerId = peerFromMTP(d.vpeer())) {
			done(_session->data().peer(peerId));
		}
	}).fail([=](const MTP::Error &error) {
		_resolveRequestId = 0;
		if (error.code() == 400) {
			Ui::show(Box<InformBox>(
				tr::lng_username_not_found(tr::now, lt_user, username)));
		}
	}).send();
}

void SessionNavigation::resolveChannelById(
		ChannelId channelId,
		Fn<void(not_null<ChannelData*>)> done) {
	if (const auto channel = _session->data().channelLoaded(channelId)) {
		done(channel);
		return;
	}
	const auto fail = [=] {
		Ui::ShowMultilineToast({
			.text = { tr::lng_error_post_link_invalid(tr::now) }
		});
	};
	_session->api().request(base::take(_resolveRequestId)).cancel();
	_resolveRequestId = _session->api().request(MTPchannels_GetChannels(
		MTP_vector<MTPInputChannel>(
			1,
			MTP_inputChannel(MTP_int(channelId.bare), MTP_long(0))) // #TODO ids
	)).done([=](const MTPmessages_Chats &result) {
		result.match([&](const auto &data) {
			const auto peer = _session->data().processChats(data.vchats());
			if (peer && peer->id == peerFromChannel(channelId)) {
				done(peer->asChannel());
			} else {
				fail();
			}
		});
	}).fail([=](const MTP::Error &error) {
		fail();
	}).send();
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
			showPeerHistory(peer->id, params, ShowAtUnreadMsgId);
		});

		// Then try to join the voice chat.
		const auto bad = [=] {
			Ui::ShowMultilineToast({
				.text = { tr::lng_group_invite_bad_link(tr::now) }
			});
		};
		const auto hash = *info.voicechatHash;
		_session->api().request(base::take(_resolveRequestId)).cancel();
		_resolveRequestId = _session->api().request(
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
					hash,
					SessionController::GroupCallJoinConfirm::Always);
			};
			if (call->loaded()) {
				join();
				return;
			}
			const auto id = call->id();
			_resolveRequestId = _session->api().request(
				MTPphone_GetGroupCall(call->input())
			).done([=](const MTPphone_GroupCall &result) {
				if (const auto now = peer->groupCall()
					; now && now->id() == id) {
					if (!now->loaded()) {
						now->processFullCall(result);
					}
					join();
				} else {
					bad();
				}
			}).fail([=](const MTP::Error &error) {
				bad();
			}).send();
		}).send();
		return;
	}
	const auto &replies = info.repliesInfo;
	if (const auto threadId = std::get_if<ThreadId>(&replies)) {
		showRepliesForMessage(
			session().data().history(peer),
			threadId->id,
			info.messageId,
			params);
	} else if (const auto commentId = std::get_if<CommentId>(&replies)) {
		showRepliesForMessage(
			session().data().history(peer),
			info.messageId,
			commentId->id,
			params);
	} else if (info.messageId == ShowAtGameShareMsgId) {
		const auto user = peer->asUser();
		if (user && user->isBot() && !info.startToken.isEmpty()) {
			user->botInfo->shareGameShortName = info.startToken;
			AddBotToGroupBoxController::Start(user);
		} else {
			crl::on_main(this, [=] {
				showPeerHistory(peer->id, params);
			});
		}
	} else if (info.messageId == ShowAtProfileMsgId && !peer->isChannel()) {
		const auto user = peer->asUser();
		if (user
			&& user->isBot()
			&& !user->botInfo->cantJoinGroups
			&& !info.startToken.isEmpty()) {
			user->botInfo->startGroupToken = info.startToken;
			AddBotToGroupBoxController::Start(user);
		} else if (user && user->isBot()) {
			// Always open bot chats, even from mention links.
			crl::on_main(this, [=] {
				showPeerHistory(peer->id, params);
			});
		} else {
			showPeerInfo(peer, params);
		}
	} else {
		const auto user = peer->asUser();
		auto msgId = info.messageId;
		if (msgId == ShowAtProfileMsgId || !peer->isChannel()) {
			// Show specific posts only in channels / supergroups.
			msgId = ShowAtUnreadMsgId;
		}
		if (user && user->isBot()) {
			user->botInfo->startToken = info.startToken;
			user->session().changes().peerUpdated(
				user,
				Data::PeerUpdate::Flag::BotStartToken);
		}
		crl::on_main(this, [=] {
			showPeerHistory(peer->id, params, msgId);
		});
	}
}

void SessionNavigation::showRepliesForMessage(
		not_null<History*> history,
		MsgId rootId,
		MsgId commentId,
		const SectionShow &params) {
	if (_showingRepliesRequestId
		&& _showingRepliesHistory == history.get()
		&& _showingRepliesRootId == rootId) {
		return;
	} else if (!history->peer->asChannel()) {
		// HistoryView::RepliesWidget right now handles only channels.
		return;
	}
	_session->api().request(base::take(_showingRepliesRequestId)).cancel();

	const auto channelId = history->channelId();
	//const auto item = _session->data().message(channelId, rootId);
	//if (!commentId && (!item || !item->repliesAreComments())) {
	//	showSection(std::make_shared<HistoryView::RepliesMemento>(history, rootId));
	//	return;
	//} else if (const auto id = item ? item->commentsItemId() : FullMsgId()) {
	//	if (const auto commentsItem = _session->data().message(id)) {
	//		showSection(
	//			std::make_shared<HistoryView::RepliesMemento>(commentsItem));
	//		return;
	//	}
	//}
	_showingRepliesHistory = history;
	_showingRepliesRootId = rootId;
	_showingRepliesRequestId = _session->api().request(
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
			if (list.isEmpty()) {
				return;
			}
			const auto id = IdFromMessage(list.front());
			const auto peer = PeerFromMessage(list.front());
			if (!peer || !id) {
				return;
			}
			auto item = _session->data().message(
				peerToChannel(peer),
				id);
			if (const auto group = _session->data().groups().find(item)) {
				item = group->items.front();
			}
			if (item) {
				if (const auto maxId = data.vmax_id()) {
					item->setRepliesMaxId(maxId->v);
				}
				item->setRepliesInboxReadTill(
					data.vread_inbox_max_id().value_or_empty());
				item->setRepliesOutboxReadTill(
					data.vread_outbox_max_id().value_or_empty());
				const auto post = _session->data().message(channelId, rootId);
				if (post && item->history()->channelId() != channelId) {
					post->setCommentsItemId(item->fullId());
					if (const auto maxId = data.vmax_id()) {
						post->setRepliesMaxId(maxId->v);
					}
					post->setRepliesInboxReadTill(
						data.vread_inbox_max_id().value_or_empty());
					post->setRepliesOutboxReadTill(
						data.vread_outbox_max_id().value_or_empty());
				}
				showSection(std::make_shared<HistoryView::RepliesMemento>(
					item,
					commentId));
			}
		});
	}).fail([=](const MTP::Error &error) {
		_showingRepliesRequestId = 0;
		if (error.type() == u"CHANNEL_PRIVATE"_q
			|| error.type() == u"USER_BANNED_IN_CHANNEL"_q) {
			Ui::Toast::Show(tr::lng_group_not_accessible(tr::now));
		}
	}).send();
}

void SessionNavigation::showPeerInfo(
		PeerId peerId,
		const SectionShow &params) {
	showPeerInfo(_session->data().peer(peerId), params);
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
		not_null<History*> history,
		const SectionShow &params) {
	showPeerInfo(history->peer->id, params);
}

void SessionNavigation::showPeerHistory(
		not_null<PeerData*> peer,
		const SectionShow &params,
		MsgId msgId) {
	showPeerHistory(
		peer->id,
		params,
		msgId);
}

void SessionNavigation::showPeerHistory(
		not_null<History*> history,
		const SectionShow &params,
		MsgId msgId) {
	showPeerHistory(
		history->peer->id,
		params,
		msgId);
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
	showSettings(Settings::Type::Main, params);
}

void SessionNavigation::showPollResults(
		not_null<PollData*> poll,
		FullMsgId contextId,
		const SectionShow &params) {
	showSection(std::make_shared<Info::Memento>(poll, contextId), params);
}

SessionController::SessionController(
	not_null<Main::Session*> session,
	not_null<Controller*> window)
: SessionNavigation(session)
, _window(window)
, _tabbedSelector(
	std::make_unique<ChatHelpers::TabbedSelector>(
		_window->widget(),
		this))
, _invitePeekTimer([=] { checkInvitePeek(); }) {
	init();

	if (Media::Player::instance()->pauseGifByRoundVideo()) {
		enableGifPauseReason(GifPauseReason::RoundPlaying);
	}

	subscribe(session->api().fullPeerUpdated(), [=](PeerData *peer) {
		if (peer == _showEditPeer) {
			_showEditPeer = nullptr;
			Ui::show(Box<EditPeerInfoBox>(this, peer));
		}
	});

	session->data().chatsListChanges(
	) | rpl::filter([=](Data::Folder *folder) {
		return (folder != nullptr)
			&& (folder == _openedFolder.current())
			&& folder->chatsList()->indexed()->empty();
	}) | rpl::start_with_next([=](Data::Folder *folder) {
		folder->updateChatListSortPosition();
		closeFolder();
	}, lifetime());

	session->data().chatsFilters().changed(
	) | rpl::start_with_next([=] {
		checkOpenedFilter();
		crl::on_main(this, [=] {
			refreshFiltersMenu();
		});
	}, lifetime());

	session->api().globalPrivacy().suggestArchiveAndMute(
	) | rpl::take(1) | rpl::start_with_next([=] {
		session->api().globalPrivacy().reload(crl::guard(this, [=] {
			if (!session->api().globalPrivacy().archiveAndMuteCurrent()) {
				suggestArchiveAndMute();
			}
		}));
	}, _lifetime);

	session->addWindow(this);
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
			showSettings(Settings::Type::PrivacySecurity);
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

not_null<::MainWindow*> SessionController::widget() const {
	return _window->widget();
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
		initSupportMode();
	}
}

void SessionController::initSupportMode() {
	session().supportHelper().registerWindow(this);

	Shortcuts::Requests(
	) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using C = Shortcuts::Command;

		request->check(C::SupportHistoryBack) && request->handle([=] {
			return chatEntryHistoryMove(-1);
		});
		request->check(C::SupportHistoryForward) && request->handle([=] {
			return chatEntryHistoryMove(1);
		});
	}, lifetime());
}

void SessionController::toggleFiltersMenu(bool enabled) {
	if (!enabled == !_filters) {
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

void SessionController::refreshFiltersMenu() {
	toggleFiltersMenu(!session().data().chatsFilters().list().empty());
}

rpl::producer<> SessionController::filtersMenuChanged() const {
	return _filtersMenuChanged.events();
}

void SessionController::checkOpenedFilter() {
	if (const auto filterId = activeChatsFilterCurrent()) {
		const auto &list = session().data().chatsFilters().list();
		const auto i = ranges::find(list, filterId, &Data::ChatFilter::id);
		if (i == end(list)) {
			setActiveChatsFilter(0);
		}
	}
}

bool SessionController::uniqueChatsInSearchResults() const {
	return session().supportMode()
		&& !session().settings().supportAllSearchResults()
		&& !searchInChat.current();
}

void SessionController::openFolder(not_null<Data::Folder*> folder) {
	if (_openedFolder.current() != folder) {
		resetFakeUnreadWhileOpened();
	}
	setActiveChatsFilter(0);
	_openedFolder = folder.get();
}

void SessionController::closeFolder() {
	_openedFolder = nullptr;
}

const rpl::variable<Data::Folder*> &SessionController::openedFolder() const {
	return _openedFolder;
}

void SessionController::setActiveChatEntry(Dialogs::RowDescriptor row) {
	const auto was = _activeChatEntry.current().key.history();
	const auto now = row.key.history();
	if (was && was != now) {
		was->setFakeUnreadWhileOpened(false);
		_invitePeekTimer.cancel();
	}
	_activeChatEntry = row;
	if (now) {
		now->setFakeUnreadWhileOpened(true);
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
	if (const auto history = row.key.history()) {
		Ui::showPeerHistory(history, row.fullId.msg);
		return true;
	}
	return false;
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
			_gifPauseLevelChanged.notify();
		}
	}
}

void SessionController::disableGifPauseReason(GifPauseReason reason) {
	if (_gifPauseReasons & reason) {
		_gifPauseReasons &= ~reason;
		if (_gifPauseReasons < reason) {
			_gifPauseLevelChanged.notify();
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
	return st::dialogsPadding.x() + st::dialogsPhotoSize + st::dialogsPadding.x();
}

int SessionController::minimalThreeColumnWidth() const {
	return st::columnMinimalWidthLeft
		+ st::columnMinimalWidthMain
		+ st::columnMinimalWidthThird;
}

bool SessionController::forceWideDialogs() const {
	if (dialogsListDisplayForced().value()) {
		return true;
	} else if (dialogsListFocused().value()) {
		return true;
	}
	return !content()->isMainSectionShown();
}

auto SessionController::computeColumnLayout() const -> ColumnLayout {
	auto layout = Adaptive::WindowLayout::OneColumn;

	auto bodyWidth = widget()->bodyWidget()->width() - filtersWidth();
	auto dialogsWidth = 0, chatWidth = 0, thirdWidth = 0;

	auto useOneColumnLayout = [&] {
		auto minimalNormal = st::columnMinimalWidthLeft
			+ st::columnMinimalWidthMain;
		if (bodyWidth < minimalNormal) {
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
	auto result = qRound(bodyWidth * Core::App().settings().dialogsWidthRatio());
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
		Assert(dialogsWidthNew >= st::columnMinimalWidthLeft);
	} else if (dialogsWidthNew < st::columnMinimalWidthLeft) {
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
	if (Adaptive::ThreeColumn()) {
		return;
	}

	auto &settings = Core::App().settings();
	auto layout = computeColumnLayout();
	auto tabbedSelectorSectionEnabled =
		settings.tabbedSelectorSectionEnabled();
	auto thirdSectionInfoEnabled =
		settings.thirdSectionInfoEnabled();
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
		auto newBodyWidth = layout.bodyWidth + extendedBy;
		auto currentRatio = settings.dialogsWidthRatio();
		settings.setDialogsWidthRatio(
			(currentRatio * layout.bodyWidth) / newBodyWidth);
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
		auto noResize = widget()->isFullScreen()
			|| widget()->isMaximized();
		auto savedValue = settings.thirdSectionExtendedBy();
		auto extendedBy = (savedValue == -1)
			? layout.thirdWidth
			: savedValue;
		auto newBodyWidth = noResize
			? layout.bodyWidth
			: (layout.bodyWidth - extendedBy);
		auto currentRatio = settings.dialogsWidthRatio();
		settings.setDialogsWidthRatio(
			(currentRatio * layout.bodyWidth) / newBodyWidth);
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

void SessionController::startOrJoinGroupCall(
		not_null<PeerData*> peer,
		QString joinHash,
		GroupCallJoinConfirm confirm) {
	auto &calls = Core::App().calls();
	const auto askConfirmation = [&](QString text, QString button) {
		Ui::show(Box<ConfirmBox>(text, button, crl::guard(this, [=] {
			Ui::hideLayer();
			startOrJoinGroupCall(peer, joinHash, GroupCallJoinConfirm::None);
		})));
	};
	if (confirm != GroupCallJoinConfirm::None && calls.inCall()) {
		// Do you want to leave your active voice chat
		// to join a voice chat in this group?
		askConfirmation(
			tr::lng_call_leave_to_other_sure(tr::now),
			tr::lng_call_bar_hangup(tr::now));
	} else if (confirm != GroupCallJoinConfirm::None
		&& calls.inGroupCall()) {
		if (calls.currentGroupCall()->peer() == peer) {
			calls.activateCurrentCall(joinHash);
		} else if (calls.currentGroupCall()->scheduleDate()) {
			calls.startOrJoinGroupCall(peer, joinHash);
		} else {
			askConfirmation(
				tr::lng_group_call_leave_to_other_sure(tr::now),
				tr::lng_group_call_leave(tr::now));
		}
	} else {
		const auto confirmNeeded = (confirm == GroupCallJoinConfirm::Always);
		calls.startOrJoinGroupCall(peer, joinHash, confirmNeeded);
	}
}

void SessionController::showJumpToDate(Dialogs::Key chat, QDate requestedDate) {
	const auto currentPeerDate = [&] {
		if (const auto history = chat.history()) {
			if (history->scrollTopItem) {
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
			} else if (history->chatListTimeId() != 0) {
				return base::unixtime::parse(history->chatListTimeId()).date();
			}
		}
		return QDate();
	}();
	const auto maxPeerDate = [](Dialogs::Key chat) {
		if (auto history = chat.history()) {
			if (const auto channel = history->peer->migrateTo()) {
				history = channel->owner().historyLoaded(channel);
			}
			if (history && history->chatListTimeId() != 0) {
				return base::unixtime::parse(history->chatListTimeId()).date();
			}
		}
		return QDate::currentDate();
	};
	const auto minPeerDate = [](Dialogs::Key chat) {
		const auto startDate = [] {
			// Telegram was launched in August 2013 :)
			return QDate(2013, 8, 1);
		};
		if (const auto history = chat.history()) {
			if (const auto chat = history->peer->migrateFrom()) {
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
		}
		return startDate();
	};
	const auto highlighted = !requestedDate.isNull()
		? requestedDate
		: !currentPeerDate.isNull()
		? currentPeerDate
		: QDate::currentDate();
	const auto month = highlighted;
	auto callback = [=](const QDate &date) {
		session().api().jumpToDate(chat, date);
	};
	auto box = Box<Ui::CalendarBox>(
		month,
		highlighted,
		std::move(callback));
	box->setMinDate(minPeerDate(chat));
	box->setMaxDate(maxPeerDate(chat));
	box->setBeginningButton(true);
	Ui::show(std::move(box));
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
		Ui::ReportReason reason,
		Fn<void(MessageIdsList)> done) {
	content()->showChooseReportMessages(peer, reason, std::move(done));
}

void SessionController::clearChooseReportMessages() {
	content()->clearChooseReportMessages();
}

void SessionController::updateColumnLayout() {
	content()->updateColumnLayout();
}

void SessionController::showPeerHistory(
		PeerId peerId,
		const SectionShow &params,
		MsgId msgId) {
	content()->ui_showPeerHistory(
		peerId,
		params,
		msgId);
}

void SessionController::showSection(
		std::shared_ptr<SectionMemento> memento,
		const SectionShow &params) {
	if (!params.thirdColumn && widget()->showSectionInExistingLayer(
			memento.get(),
			params)) {
		return;
	}
	content()->showSection(std::move(memento), params);
}

void SessionController::showBackFromStack(const SectionShow &params) {
	content()->showBackFromStack(params);
}

void SessionController::showSpecialLayer(
		object_ptr<Ui::LayerWidget> &&layer,
		anim::type animated) {
	widget()->showSpecialLayer(std::move(layer), animated);
}

void SessionController::removeLayerBlackout() {
	widget()->ui_removeLayerBlackout();
}

not_null<MainWidget*> SessionController::content() const {
	return widget()->sessionContent();
}

int SessionController::filtersWidth() const {
	return _filters ? st::windowFiltersWidth : 0;
}

rpl::producer<FilterId> SessionController::activeChatsFilter() const {
	return _activeChatsFilter.value();
}

FilterId SessionController::activeChatsFilterCurrent() const {
	return _activeChatsFilter.current();
}

void SessionController::setActiveChatsFilter(FilterId id) {
	if (activeChatsFilterCurrent() != id) {
		resetFakeUnreadWhileOpened();
	}
	_activeChatsFilter.force_assign(id);
	if (id) {
		closeFolder();
	}
	if (Adaptive::OneColumn()) {
		Ui::showChatsList(&session());
	}
}

void SessionController::showAddContact() {
	_window->show(
		Box<AddContactBox>(&session()),
		Ui::LayerOption::KeepOther);
}

void SessionController::showNewGroup() {
	_window->show(
		Box<GroupInfoBox>(this, GroupInfoBox::Type::Group),
		Ui::LayerOption::KeepOther);
}

void SessionController::showNewChannel() {
	_window->show(
		Box<GroupInfoBox>(this, GroupInfoBox::Type::Channel),
		Ui::LayerOption::KeepOther);
}

SessionController::~SessionController() = default;

} // namespace Window
