/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "apiwrap.h"

#include "data/data_drafts.h"
#include "data/data_photo.h"
#include "data/data_web_page.h"
#include "data/data_feed.h"
#include "data/data_media_types.h"
#include "data/data_sparse_ids.h"
#include "data/data_search_controller.h"
#include "data/data_channel_admins.h"
#include "data/data_session.h"
#include "dialogs/dialogs_key.h"
#include "core/tl_help.h"
#include "base/overload.h"
#include "observer_peer.h"
#include "lang/lang_keys.h"
#include "application.h"
#include "mainwindow.h"
#include "messenger.h"
#include "mainwidget.h"
#include "boxes/add_contact_box.h"
#include "history/history.h"
#include "history/history_message.h"
#include "history/history_media_types.h"
#include "history/history_item_components.h"
#include "history/feed/history_feed_section.h"
#include "storage/localstorage.h"
#include "auth_session.h"
#include "boxes/confirm_box.h"
#include "window/notifications_manager.h"
#include "window/window_lock_widgets.h"
#include "window/window_controller.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/stickers.h"
#include "storage/localimageloader.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"
#include "storage/storage_user_photos.h"
#include "storage/storage_media_prepare.h"
#include "storage/storage_feed_messages.h"

namespace {

constexpr auto kReloadChannelMembersTimeout = 1000; // 1 second wait before reload members in channel after adding
constexpr auto kSaveCloudDraftTimeout = 1000; // save draft to the cloud with 1 sec extra delay
constexpr auto kSaveDraftBeforeQuitTimeout = 1500; // give the app 1.5 secs to save drafts to cloud when quitting
constexpr auto kProxyPromotionInterval = TimeId(60 * 60);
constexpr auto kProxyPromotionMinDelay = TimeId(10);
constexpr auto kSmallDelayMs = 5;
constexpr auto kUnreadMentionsPreloadIfLess = 5;
constexpr auto kUnreadMentionsFirstRequestLimit = 10;
constexpr auto kUnreadMentionsNextRequestLimit = 100;
constexpr auto kSharedMediaLimit = 100;
constexpr auto kFeedMessagesLimit = 50;
constexpr auto kReadFeaturedSetsTimeout = TimeMs(1000);
constexpr auto kFileLoaderQueueStopTimeout = TimeMs(5000);
constexpr auto kFeedReadTimeout = TimeMs(1000);
constexpr auto kStickersByEmojiInvalidateTimeout = TimeMs(60 * 60 * 1000);
constexpr auto kNotifySettingSaveTimeout = TimeMs(1000);

bool IsSilentPost(not_null<HistoryItem*> item, bool silent) {
	const auto history = item->history();
	return silent
		&& history->peer->isChannel()
		&& !history->peer->isMegagroup();
}

MTPVector<MTPDocumentAttribute> ComposeSendingDocumentAttributes(
		not_null<DocumentData*> document) {
	const auto filenameAttribute = MTP_documentAttributeFilename(
		MTP_string(document->filename()));
	const auto dimensions = document->dimensions;
	auto attributes = QVector<MTPDocumentAttribute>(1, filenameAttribute);
	if (dimensions.width() > 0 && dimensions.height() > 0) {
		const auto duration = document->duration();
		if (duration >= 0) {
			auto flags = MTPDdocumentAttributeVideo::Flags(0);
			if (document->isVideoMessage()) {
				flags |= MTPDdocumentAttributeVideo::Flag::f_round_message;
			}
			attributes.push_back(MTP_documentAttributeVideo(
				MTP_flags(flags),
				MTP_int(duration),
				MTP_int(dimensions.width()),
				MTP_int(dimensions.height())));
		} else {
			attributes.push_back(MTP_documentAttributeImageSize(
				MTP_int(dimensions.width()),
				MTP_int(dimensions.height())));
		}
	}
	if (document->type == AnimatedDocument) {
		attributes.push_back(MTP_documentAttributeAnimated());
	} else if (document->type == StickerDocument && document->sticker()) {
		attributes.push_back(MTP_documentAttributeSticker(
			MTP_flags(0),
			MTP_string(document->sticker()->alt),
			document->sticker()->set,
			MTPMaskCoords()));
	} else if (const auto song = document->song()) {
		const auto flags = MTPDdocumentAttributeAudio::Flag::f_title
			| MTPDdocumentAttributeAudio::Flag::f_performer;
		attributes.push_back(MTP_documentAttributeAudio(
			MTP_flags(flags),
			MTP_int(song->duration),
			MTP_string(song->title),
			MTP_string(song->performer),
			MTPstring()));
	} else if (const auto voice = document->voice()) {
		const auto flags = MTPDdocumentAttributeAudio::Flag::f_voice
			| MTPDdocumentAttributeAudio::Flag::f_waveform;
		attributes.push_back(MTP_documentAttributeAudio(
			MTP_flags(flags),
			MTP_int(voice->duration),
			MTPstring(),
			MTPstring(),
			MTP_bytes(documentWaveformEncode5bit(voice->waveform))));
	}
	return MTP_vector<MTPDocumentAttribute>(attributes);
}

FileLoadTo FileLoadTaskOptions(const ApiWrap::SendOptions &options) {
	const auto peer = options.history->peer;
	return FileLoadTo(
		peer->id,
		Auth().data().notifySilentPosts(peer),
		options.replyTo);
}

} // namespace

ApiWrap::ApiWrap(not_null<AuthSession*> session)
: _session(session)
, _messageDataResolveDelayed([=] { resolveMessageDatas(); })
, _webPagesTimer([=] { resolveWebPages(); })
, _draftsSaveTimer([=] { saveDraftsToCloud(); })
, _featuredSetsReadTimer([=] { readFeaturedSets(); })
, _fileLoader(std::make_unique<TaskQueue>(kFileLoaderQueueStopTimeout))
, _feedReadTimer([=] { readFeeds(); })
, _proxyPromotionTimer([=] { refreshProxyPromotion(); })
, _updateNotifySettingsTimer([=] { sendNotifySettingsUpdates(); }) {
}

void ApiWrap::requestChangelog(
		const QString &sinceVersion,
		Fn<void(const MTPUpdates &result)> callback) {
	request(MTPhelp_GetAppChangelog(
		MTP_string(sinceVersion)
	)).done(
		callback
	).send();
}

void ApiWrap::refreshProxyPromotion() {
	const auto now = unixtime();
	const auto next = (_proxyPromotionNextRequestTime != 0)
		? _proxyPromotionNextRequestTime
		: now;
	if (_proxyPromotionRequestId) {
		getProxyPromotionDelayed(now, next);
		return;
	}
	const auto proxy = Global::UseProxy()
		? Global::SelectedProxy()
		: ProxyData();
	const auto key = [&]() -> std::pair<QString, uint32> {
		if (!Global::UseProxy()) {
			return {};
		}
		const auto &proxy = Global::SelectedProxy();
		if (proxy.type != ProxyData::Type::Mtproto) {
			return {};
		}
		return { proxy.host, proxy.port };
	}();
	if (_proxyPromotionKey == key && now < next) {
		getProxyPromotionDelayed(now, next);
		return;
	}
	_proxyPromotionKey = key;
	if (key.first.isEmpty() || !key.second) {
		proxyPromotionDone(MTP_help_proxyDataEmpty(
			MTP_int(unixtime() + kProxyPromotionInterval)));
		return;
	}
	_proxyPromotionRequestId = request(MTPhelp_GetProxyData(
	)).done([=](const MTPhelp_ProxyData &result) {
		_proxyPromotionRequestId = 0;
		proxyPromotionDone(result);
	}).fail([=](const RPCError &error) {
		_proxyPromotionRequestId = 0;
		const auto now = unixtime();
		const auto next = _proxyPromotionNextRequestTime = now
			+ kProxyPromotionInterval;
		if (!_proxyPromotionTimer.isActive()) {
			getProxyPromotionDelayed(now, next);
		}
	}).send();
}

void ApiWrap::getProxyPromotionDelayed(TimeId now, TimeId next) {
	_proxyPromotionTimer.callOnce(std::min(
		std::max(next - now, kProxyPromotionMinDelay),
		kProxyPromotionInterval) * TimeMs(1000));
};

void ApiWrap::proxyPromotionDone(const MTPhelp_ProxyData &proxy) {
	if (proxy.type() == mtpc_help_proxyDataEmpty) {
		const auto &data = proxy.c_help_proxyDataEmpty();
		const auto next = _proxyPromotionNextRequestTime = data.vexpires.v;
		getProxyPromotionDelayed(unixtime(), next);
		_session->data().setProxyPromoted(nullptr);
		return;
	}
	Assert(proxy.type() == mtpc_help_proxyDataPromo);
	const auto &data = proxy.c_help_proxyDataPromo();
	const auto next = _proxyPromotionNextRequestTime = data.vexpires.v;
	getProxyPromotionDelayed(unixtime(), next);

	App::feedChats(data.vchats);
	App::feedUsers(data.vusers);
	const auto peerId = peerFromMTP(data.vpeer);
	const auto peer = App::peer(peerId);
	_session->data().setProxyPromoted(peer);
	if (const auto history = App::historyLoaded(peer)) {
		_session->api().requestDialogEntry(history);
	}
}

void ApiWrap::requestDeepLinkInfo(
		const QString &path,
		Fn<void(const MTPDhelp_deepLinkInfo &result)> callback) {
	request(_deepLinkInfoRequestId).cancel();
	_deepLinkInfoRequestId = request(MTPhelp_GetDeepLinkInfo(
		MTP_string(path)
	)).done([=](const MTPhelp_DeepLinkInfo &result) {
		_deepLinkInfoRequestId = 0;
		if (result.type() == mtpc_help_deepLinkInfo) {
			callback(result.c_help_deepLinkInfo());
		}
	}).fail([=](const RPCError &error) {
		_deepLinkInfoRequestId = 0;
	}).send();
}

void ApiWrap::requestTermsUpdate() {
	if (_termsUpdateRequestId) {
		return;
	}
	const auto now = getms(true);
	if (_termsUpdateSendAt && now < _termsUpdateSendAt) {
		App::CallDelayed(_termsUpdateSendAt - now, _session, [=] {
			requestTermsUpdate();
		});
		return;
	}

	constexpr auto kTermsUpdateTimeoutMin = 10 * TimeMs(1000);
	constexpr auto kTermsUpdateTimeoutMax = 86400 * TimeMs(1000);

	_termsUpdateRequestId = request(MTPhelp_GetTermsOfServiceUpdate(
	)).done([=](const MTPhelp_TermsOfServiceUpdate &result) {
		_termsUpdateRequestId = 0;

		const auto requestNext = [&](auto &&data) {
			const auto timeout = (data.vexpires.v - unixtime());
			_termsUpdateSendAt = getms(true) + snap(
				timeout * TimeMs(1000),
				kTermsUpdateTimeoutMin,
				kTermsUpdateTimeoutMax);
			requestTermsUpdate();
		};
		switch (result.type()) {
		case mtpc_help_termsOfServiceUpdateEmpty: {
			const auto &data = result.c_help_termsOfServiceUpdateEmpty();
			requestNext(data);
		} break;
		case mtpc_help_termsOfServiceUpdate: {
			const auto &data = result.c_help_termsOfServiceUpdate();
			const auto &terms = data.vterms_of_service;
			const auto &fields = terms.c_help_termsOfService();
			Messenger::Instance().lockByTerms(
				Window::TermsLock::FromMTP(fields));
			requestNext(data);
		} break;
		default: Unexpected("Type in requestTermsUpdate().");
		}
	}).fail([=](const RPCError &error) {
		_termsUpdateRequestId = 0;
		_termsUpdateSendAt = getms(true) + kTermsUpdateTimeoutMin;
		requestTermsUpdate();
	}).send();
}

void ApiWrap::acceptTerms(bytes::const_span id) {
	request(MTPhelp_AcceptTermsOfService(
		MTP_dataJSON(MTP_bytes(id))
	)).done([=](const MTPBool &result) {
		requestTermsUpdate();
	}).send();
}

void ApiWrap::applyUpdates(
		const MTPUpdates &updates,
		uint64 sentMessageRandomId) {
	App::main()->feedUpdates(updates, sentMessageRandomId);
}

void ApiWrap::savePinnedOrder() {
	const auto &order = _session->data().pinnedDialogsOrder();
	auto peers = QVector<MTPInputDialogPeer>();
	peers.reserve(order.size());
	for (const auto pinned : base::reversed(order)) {
		if (const auto history = pinned.history()) {
			peers.push_back(MTP_inputDialogPeer(history->peer->input));
		} else if (const auto feed = pinned.feed()) {
//			peers.push_back(MTP_inputDialogPeerFeed(MTP_int(feed->id()))); // #feed
		}
	}
	auto flags = MTPmessages_ReorderPinnedDialogs::Flag::f_force;
	request(MTPmessages_ReorderPinnedDialogs(
		MTP_flags(flags),
		MTP_vector(peers)
	)).send();
}
// #feed
//void ApiWrap::toggleChannelGrouping(
//		not_null<ChannelData*> channel,
//		bool group,
//		Fn<void()> callback) {
//	if (const auto already = _channelGroupingRequests.take(channel)) {
//		request(already->first).cancel();
//	}
//	const auto feedId = Data::Feed::kId;
//	const auto flags = group
//		? MTPchannels_ChangeFeedBroadcast::Flag::f_feed_id
//		: MTPchannels_ChangeFeedBroadcast::Flag(0);
//	const auto requestId = request(MTPchannels_ChangeFeedBroadcast(
//		MTP_flags(flags),
//		channel->inputChannel,
//		MTP_int(feedId)
//	)).done([=](const MTPUpdates &result) {
//		applyUpdates(result);
//		if (group) {
//			channel->setFeed(_session->data().feed(feedId));
//		} else {
//			channel->clearFeed();
//		}
//		if (const auto data = _channelGroupingRequests.take(channel)) {
//			data->second();
//		}
//	}).fail([=](const RPCError &error) {
//		_channelGroupingRequests.remove(channel);
//	}).send();
//	_channelGroupingRequests.emplace(channel, requestId, callback);
//}
//
//void ApiWrap::ungroupAllFromFeed(not_null<Data::Feed*> feed) {
//	const auto flags = MTPchannels_SetFeedBroadcasts::Flag::f_channels
//		| MTPchannels_SetFeedBroadcasts::Flag::f_also_newly_joined;
//	request(MTPchannels_SetFeedBroadcasts(
//		MTP_flags(flags),
//		MTP_int(feed->id()),
//		MTP_vector<MTPInputChannel>(0),
//		MTP_bool(false)
//	)).done([=](const MTPUpdates &result) {
//		applyUpdates(result);
//	}).send();
//}

void ApiWrap::sendMessageFail(const RPCError &error) {
	if (error.type() == qstr("PEER_FLOOD")) {
		Ui::show(Box<InformBox>(
			PeerFloodErrorText(PeerFloodType::Send)));
	} else if (error.type() == qstr("USER_BANNED_IN_CHANNEL")) {
		const auto link = textcmdLink(
			Messenger::Instance().createInternalLinkFull(qsl("spambot")),
			lang(lng_cant_more_info));
		Ui::show(Box<InformBox>(lng_error_public_groups_denied(
			lt_more_info,
			link)));
	}
}

void ApiWrap::requestMessageData(ChannelData *channel, MsgId msgId, RequestMessageDataCallback callback) {
	auto &req = (channel ? _channelMessageDataRequests[channel][msgId] : _messageDataRequests[msgId]);
	if (callback) {
		req.callbacks.append(callback);
	}
	if (!req.requestId) _messageDataResolveDelayed.call();
}

QVector<MTPInputMessage> ApiWrap::collectMessageIds(const MessageDataRequests &requests) {
	auto result = QVector<MTPInputMessage>();
	result.reserve(requests.size());
	for (auto i = requests.cbegin(), e = requests.cend(); i != e; ++i) {
		if (i.value().requestId > 0) continue;
		result.push_back(MTP_inputMessageID(MTP_int(i.key())));
	}
	return result;
}

ApiWrap::MessageDataRequests *ApiWrap::messageDataRequests(ChannelData *channel, bool onlyExisting) {
	if (channel) {
		auto i = _channelMessageDataRequests.find(channel);
		if (i == _channelMessageDataRequests.cend()) {
			if (onlyExisting) return 0;
			i = _channelMessageDataRequests.insert(channel, MessageDataRequests());
		}
		return &i.value();
	}
	return &_messageDataRequests;
}

void ApiWrap::resolveMessageDatas() {
	if (_messageDataRequests.isEmpty() && _channelMessageDataRequests.isEmpty()) return;

	auto ids = collectMessageIds(_messageDataRequests);
	if (!ids.isEmpty()) {
		auto requestId = request(MTPmessages_GetMessages(
			MTP_vector<MTPInputMessage>(ids)
		)).done([this](const MTPmessages_Messages &result, mtpRequestId requestId) {
			gotMessageDatas(nullptr, result, requestId);
		}).fail([this](const RPCError &error, mtpRequestId requestId) {
			finalizeMessageDataRequest(nullptr, requestId);
		}).afterDelay(kSmallDelayMs).send();
		for (auto &request : _messageDataRequests) {
			if (request.requestId > 0) continue;
			request.requestId = requestId;
		}
	}
	for (auto j = _channelMessageDataRequests.begin(); j != _channelMessageDataRequests.cend();) {
		if (j->isEmpty()) {
			j = _channelMessageDataRequests.erase(j);
			continue;
		}
		auto ids = collectMessageIds(j.value());
		if (!ids.isEmpty()) {
			auto channel = j.key();
			auto requestId = request(MTPchannels_GetMessages(
				j.key()->inputChannel,
				MTP_vector<MTPInputMessage>(ids)
			)).done([=](const MTPmessages_Messages &result, mtpRequestId requestId) {
				gotMessageDatas(channel, result, requestId);
			}).fail([=](const RPCError &error, mtpRequestId requestId) {
				finalizeMessageDataRequest(channel, requestId);
			}).afterDelay(kSmallDelayMs).send();

			for (auto &request : *j) {
				if (request.requestId > 0) continue;
				request.requestId = requestId;
			}
		}
		++j;
	}
}

void ApiWrap::gotMessageDatas(ChannelData *channel, const MTPmessages_Messages &msgs, mtpRequestId requestId) {
	auto handleResult = [&](auto &&result) {
		App::feedUsers(result.vusers);
		App::feedChats(result.vchats);
		App::feedMsgs(result.vmessages, NewMessageExisting);
	};
	switch (msgs.type()) {
	case mtpc_messages_messages:
		handleResult(msgs.c_messages_messages());
		break;
	case mtpc_messages_messagesSlice:
		handleResult(msgs.c_messages_messagesSlice());
		break;
	case mtpc_messages_channelMessages: {
		auto &d = msgs.c_messages_channelMessages();
		if (channel) {
			channel->ptsReceived(d.vpts.v);
		} else {
			LOG(("App Error: received messages.channelMessages when no channel was passed! (ApiWrap::gotDependencyItem)"));
		}
		handleResult(d);
	} break;
	case mtpc_messages_messagesNotModified:
		LOG(("API Error: received messages.messagesNotModified! (ApiWrap::gotDependencyItem)"));
		break;
	}
	finalizeMessageDataRequest(channel, requestId);
}

void ApiWrap::finalizeMessageDataRequest(
		ChannelData *channel,
		mtpRequestId requestId) {
	auto requests = messageDataRequests(channel, true);
	if (requests) {
		for (auto i = requests->begin(); i != requests->cend();) {
			if (i.value().requestId == requestId) {
				for_const (auto &callback, i.value().callbacks) {
					callback(channel, i.key());
				}
				i = requests->erase(i);
			} else {
				++i;
			}
		}
		if (channel && requests->isEmpty()) {
			_channelMessageDataRequests.remove(channel);
		}
	}
}

void ApiWrap::requestContacts() {
	if (_session->data().contactsLoaded().value() || _contactsRequestId) {
		return;
	}
	_contactsRequestId = request(MTPcontacts_GetContacts(
		MTP_int(0)
	)).done([=](const MTPcontacts_Contacts &result) {
		_contactsRequestId = 0;
		if (result.type() == mtpc_contacts_contactsNotModified) {
			return;
		}
		Assert(result.type() == mtpc_contacts_contacts);
		const auto &d = result.c_contacts_contacts();
		App::feedUsers(d.vusers);
		for (const auto &contact : d.vcontacts.v) {
			if (contact.type() != mtpc_contact) continue;

			const auto userId = contact.c_contact().vuser_id.v;
			if (userId == _session->userId() && App::self()) {
				App::self()->setContactStatus(
					UserData::ContactStatus::Contact);
			}
		}
		_session->data().contactsLoaded().set(true);
	}).fail([=](const RPCError &error) {
		_contactsRequestId = 0;
	}).send();
}

void ApiWrap::requestDialogEntry(not_null<Data::Feed*> feed) {
	if (_dialogFeedRequests.contains(feed)) {
		return;
	}
	_dialogFeedRequests.emplace(feed);

	//auto peers = QVector<MTPInputDialogPeer>( // #feed
	//	1,
	//	MTP_inputDialogPeerFeed(MTP_int(feed->id())));
	//request(MTPmessages_GetPeerDialogs(
	//	MTP_vector(std::move(peers))
	//)).done([=](const MTPmessages_PeerDialogs &result) {
	//	applyPeerDialogs(result);
	//	_dialogFeedRequests.remove(feed);
	//}).fail([=](const RPCError &error) {
	//	_dialogFeedRequests.remove(feed);
	//}).send();
}

//void ApiWrap::requestFeedDialogsEntries(not_null<Data::Feed*> feed) {
//	if (_dialogFeedRequests.contains(feed)) {
//		return;
//	}
//	_dialogFeedRequests.emplace(feed);
//
//	request(MTPmessages_GetDialogs(
//		MTP_flags(MTPmessages_GetDialogs::Flag::f_feed_id),
//		MTP_int(feed->id()),
//		MTP_int(0), // offset_date
//		MTP_int(0), // offset_id
//		MTP_inputPeerEmpty(), // offset_peer
//		MTP_int(Data::Feed::kChannelsLimit)
//	)).done([=](const MTPmessages_Dialogs &result) {
//		applyFeedDialogs(feed, result);
//		_dialogFeedRequests.remove(feed);
//	}).fail([=](const RPCError &error) {
//		_dialogFeedRequests.remove(feed);
//	}).send();
//}

void ApiWrap::requestDialogEntry(not_null<History*> history) {
	if (_dialogRequests.contains(history)) {
		return;
	}
	_dialogRequests.emplace(history);
	auto peers = QVector<MTPInputDialogPeer>(
		1,
		MTP_inputDialogPeer(history->peer->input));
	request(MTPmessages_GetPeerDialogs(
		MTP_vector(std::move(peers))
	)).done([=](const MTPmessages_PeerDialogs &result) {
		applyPeerDialogs(result);
		historyDialogEntryApplied(history);
		_dialogRequests.remove(history);
	}).fail([=](const RPCError &error) {
		_dialogRequests.remove(history);
	}).send();
}

void ApiWrap::applyPeerDialogs(const MTPmessages_PeerDialogs &dialogs) {
	Expects(dialogs.type() == mtpc_messages_peerDialogs);

	const auto &data = dialogs.c_messages_peerDialogs();
	App::feedUsers(data.vusers);
	App::feedChats(data.vchats);
	App::feedMsgs(data.vmessages, NewMessageLast);
	for (const auto &dialog : data.vdialogs.v) {
		switch (dialog.type()) {
		case mtpc_dialog: {
			const auto &fields = dialog.c_dialog();
			if (const auto peerId = peerFromMTP(fields.vpeer)) {
				App::history(peerId)->applyDialog(fields);
			}
		} break;

		//case mtpc_dialogFeed: { // #feed
		//	const auto &fields = dialog.c_dialogFeed();
		//	const auto feed = _session->data().feed(fields.vfeed_id.v);
		//	feed->applyDialog(fields);
		//} break;
		}
	}
	_session->data().sendHistoryChangeNotifications();
}

void ApiWrap::historyDialogEntryApplied(not_null<History*> history) {
	if (!history->lastMessage()) {
		if (const auto chat = history->peer->asChat()) {
			if (!chat->haveLeft()) {
				Local::addSavedPeer(
					history->peer,
					history->chatsListDate());
			}
		} else if (const auto channel = history->peer->asChannel()) {
			const auto inviter = channel->inviter;
			if (inviter != 0 && channel->amIn()) {
				if (const auto from = App::userLoaded(inviter)) {
					history->unloadBlocks();
					history->addNewerSlice(QVector<MTPMessage>());
					history->insertJoinedMessage(true);
				}
			}
		} else {
			App::main()->deleteConversation(history->peer, false);
		}
		return;
	}

	if (!history->chatsListDate().isNull()
		&& history->loadedAtBottom()) {
		if (const auto channel = history->peer->asChannel()) {
			const auto inviter = channel->inviter;
			if (inviter != 0
				&& history->chatsListDate() <= ParseDateTime(channel->inviteDate)
				&& channel->amIn()) {
				if (const auto from = App::userLoaded(inviter)) {
					history->insertJoinedMessage(true);
				}
			}
		}
	}
	history->updateChatListExistence();
}

void ApiWrap::applyFeedDialogs(
		not_null<Data::Feed*> feed,
		const MTPmessages_Dialogs &dialogs) {
	const auto [dialogsList, messagesList] = [&] {
		const auto process = [&](const auto &data) {
			App::feedUsers(data.vusers);
			App::feedChats(data.vchats);
			return std::make_tuple(&data.vdialogs.v, &data.vmessages.v);
		};
		switch (dialogs.type()) {
		case mtpc_messages_dialogs:
			return process(dialogs.c_messages_dialogs());

		case mtpc_messages_dialogsSlice:
			LOG(("API Error: "
				"Unexpected dialogsSlice in feed dialogs list."));
			return process(dialogs.c_messages_dialogsSlice());
		}
		Unexpected("Type in DialogsWidget::dialogsReceived");
	}();

	App::feedMsgs(*messagesList, NewMessageLast);

	auto channels = std::vector<not_null<ChannelData*>>();
	channels.reserve(dialogsList->size());
	for (const auto &dialog : *dialogsList) {
		switch (dialog.type()) {
		case mtpc_dialog: {
			if (const auto peerId = peerFromMTP(dialog.c_dialog().vpeer)) {
				if (peerIsChannel(peerId)) {
					const auto history = App::history(peerId);
					history->applyDialog(dialog.c_dialog());
					channels.push_back(history->peer->asChannel());
				} else {
					LOG(("API Error: "
						"Unexpected non-channel in feed dialogs list."));
				}
			}
		} break;
		//case mtpc_dialogFeed: { // #feed
		//	LOG(("API Error: Unexpected dialogFeed in feed dialogs list."));
		//} break;
		default: Unexpected("Type in DialogsInner::dialogsReceived");
		}
	}

	feed->setChannels(channels);
	_session->data().sendHistoryChangeNotifications();
}

void ApiWrap::requestFullPeer(PeerData *peer) {
	if (!peer || _fullPeerRequests.contains(peer)) return;

	auto sendRequest = [this, peer] {
		auto failHandler = [this, peer](const RPCError &error) {
			_fullPeerRequests.remove(peer);
		};
		if (auto user = peer->asUser()) {
			return request(MTPusers_GetFullUser(user->inputUser)).done([this, user](const MTPUserFull &result, mtpRequestId requestId) {
				gotUserFull(user, result, requestId);
			}).fail(failHandler).send();
		} else if (auto chat = peer->asChat()) {
			return request(MTPmessages_GetFullChat(chat->inputChat)).done([this, peer](const MTPmessages_ChatFull &result, mtpRequestId requestId) {
				gotChatFull(peer, result, requestId);
			}).fail(failHandler).send();
		} else if (auto channel = peer->asChannel()) {
			return request(MTPchannels_GetFullChannel(channel->inputChannel)).done([this, peer](const MTPmessages_ChatFull &result, mtpRequestId requestId) {
				gotChatFull(peer, result, requestId);
			}).fail(failHandler).send();
		}
		return 0;
	};
	if (auto requestId = sendRequest()) {
		_fullPeerRequests.insert(peer, requestId);
	}
}

void ApiWrap::processFullPeer(PeerData *peer, const MTPmessages_ChatFull &result) {
	gotChatFull(peer, result, mtpRequestId(0));
}

void ApiWrap::processFullPeer(UserData *user, const MTPUserFull &result) {
	gotUserFull(user, result, mtpRequestId(0));
}

void ApiWrap::gotChatFull(PeerData *peer, const MTPmessages_ChatFull &result, mtpRequestId req) {
	auto &d = result.c_messages_chatFull();
	auto &vc = d.vchats.v;
	auto badVersion = false;
	if (const auto chat = peer->asChat()) {
		badVersion = !vc.isEmpty()
			&& (vc[0].type() == mtpc_chat)
			&& (vc[0].c_chat().vversion.v < chat->version);
	} else if (const auto channel = peer->asChannel()) {
		badVersion = !vc.isEmpty()
			&& (vc[0].type() == mtpc_channel)
			&& (vc[0].c_channel().vversion.v < channel->version);
	}

	App::feedUsers(d.vusers);
	App::feedChats(d.vchats);

	using UpdateFlag = Notify::PeerUpdate::Flag;
	if (auto chat = peer->asChat()) {
		if (d.vfull_chat.type() != mtpc_chatFull) {
			LOG(("MTP Error: bad type in gotChatFull for chat: %1").arg(d.vfull_chat.type()));
			return;
		}
		auto &f = d.vfull_chat.c_chatFull();
		App::feedParticipants(f.vparticipants, false);
		auto &v = f.vbot_info.v;
		for_const (auto &item, v) {
			switch (item.type()) {
			case mtpc_botInfo: {
				auto &b = item.c_botInfo();
				if (auto user = App::userLoaded(b.vuser_id.v)) {
					user->setBotInfo(item);
					fullPeerUpdated().notify(user);
				}
			} break;
			}
		}
		chat->setUserpicPhoto(f.vchat_photo);
		chat->setInviteLink((f.vexported_invite.type() == mtpc_chatInviteExported) ? qs(f.vexported_invite.c_chatInviteExported().vlink) : QString());
		chat->fullUpdated();

		notifySettingReceived(MTP_inputNotifyPeer(peer->input), f.vnotify_settings);
	} else if (auto channel = peer->asChannel()) {
		if (d.vfull_chat.type() != mtpc_channelFull) {
			LOG(("MTP Error: bad type in gotChatFull for channel: %1").arg(d.vfull_chat.type()));
			return;
		}
		auto &f = d.vfull_chat.c_channelFull();
		channel->setAvailableMinId(f.vavailable_min_id.v);
		auto canViewAdmins = channel->canViewAdmins();
		auto canViewMembers = channel->canViewMembers();
		auto canEditStickers = channel->canEditStickers();

		channel->setFullFlags(f.vflags.v);
		channel->setUserpicPhoto(f.vchat_photo);
		if (f.has_migrated_from_chat_id()) {
			channel->addFlags(MTPDchannel::Flag::f_megagroup);
			auto cfrom = App::chat(peerFromChat(f.vmigrated_from_chat_id));
			bool updatedTo = (cfrom->migrateToPtr != channel), updatedFrom = (channel->mgInfo->migrateFromPtr != cfrom);
			if (updatedTo) {
				cfrom->migrateToPtr = channel;
			}
			if (updatedFrom) {
				channel->mgInfo->migrateFromPtr = cfrom;
				if (auto h = App::historyLoaded(cfrom->id)) {
					if (auto hto = App::historyLoaded(channel->id)) {
						if (!h->isEmpty()) {
							h->unloadBlocks();
						}
						if (hto->inChatList(Dialogs::Mode::All) && h->inChatList(Dialogs::Mode::All)) {
							App::main()->removeDialog(h);
						}
					}
				}
				Notify::migrateUpdated(channel);
			}
			if (updatedTo) {
				Notify::migrateUpdated(cfrom);
			}
		}
		auto &v = f.vbot_info.v;
		for_const (auto &item, v) {
			switch (item.type()) {
			case mtpc_botInfo: {
				auto &b = item.c_botInfo();
				if (auto user = App::userLoaded(b.vuser_id.v)) {
					user->setBotInfo(item);
					fullPeerUpdated().notify(user);
				}
			} break;
			}
		}
		channel->setAbout(qs(f.vabout));
		channel->setMembersCount(f.has_participants_count() ? f.vparticipants_count.v : 0);
		channel->setAdminsCount(f.has_admins_count() ? f.vadmins_count.v : 0);
		channel->setRestrictedCount(f.has_banned_count() ? f.vbanned_count.v : 0);
		channel->setKickedCount(f.has_kicked_count() ? f.vkicked_count.v : 0);
		channel->setInviteLink((f.vexported_invite.type() == mtpc_chatInviteExported) ? qs(f.vexported_invite.c_chatInviteExported().vlink) : QString());
		if (const auto history = App::historyLoaded(channel->id)) {
			history->applyDialogFields(
				f.vunread_count.v,
				f.vread_inbox_max_id.v,
				f.vread_outbox_max_id.v);
		}
		if (f.has_pinned_msg_id()) {
			channel->setPinnedMessageId(f.vpinned_msg_id.v);
		} else {
			channel->clearPinnedMessage();
		}
		if (channel->isMegagroup()) {
			auto stickersChanged = (canEditStickers != channel->canEditStickers());
			auto stickerSet = (f.has_stickerset() ? &f.vstickerset.c_stickerSet() : nullptr);
			auto newSetId = (stickerSet ? stickerSet->vid.v : 0);
			auto oldSetId = (channel->mgInfo->stickerSet.type() == mtpc_inputStickerSetID)
				? channel->mgInfo->stickerSet.c_inputStickerSetID().vid.v
				: 0;
			if (oldSetId != newSetId) {
				channel->mgInfo->stickerSet = stickerSet
					? MTP_inputStickerSetID(stickerSet->vid, stickerSet->vaccess_hash)
					: MTP_inputStickerSetEmpty();
				stickersChanged = true;
			}
			if (stickersChanged) {
				Notify::peerUpdatedDelayed(channel, UpdateFlag::ChannelStickersChanged);
			}
		}
		channel->fullUpdated();

		if (canViewAdmins != channel->canViewAdmins()
			|| canViewMembers != channel->canViewMembers()) {
			Notify::peerUpdatedDelayed(channel, UpdateFlag::ChannelRightsChanged);
		}

		notifySettingReceived(MTP_inputNotifyPeer(peer->input), f.vnotify_settings);
	}

	if (req) {
		auto i = _fullPeerRequests.find(peer);
		if (i != _fullPeerRequests.cend() && i.value() == req) {
			_fullPeerRequests.erase(i);
		}
	}
	if (badVersion) {
		if (const auto chat = peer->asChat()) {
			chat->version = vc[0].c_chat().vversion.v;
		} else if (const auto channel = peer->asChannel()) {
			channel->version = vc[0].c_channel().vversion.v;
		}
		requestPeer(peer);
	}
	fullPeerUpdated().notify(peer);
}

void ApiWrap::gotUserFull(UserData *user, const MTPUserFull &result, mtpRequestId req) {
	auto &d = result.c_userFull();

	App::feedUsers(MTP_vector<MTPUser>(1, d.vuser));
	if (d.has_profile_photo()) {
		_session->data().photo(d.vprofile_photo);
	}
	App::feedUserLink(MTP_int(peerToUser(user->id)), d.vlink.c_contacts_link().vmy_link, d.vlink.c_contacts_link().vforeign_link);
	if (App::main()) {
		notifySettingReceived(MTP_inputNotifyPeer(user->input), d.vnotify_settings);
	}

	if (d.has_bot_info()) {
		user->setBotInfo(d.vbot_info);
	} else {
		user->setBotInfoVersion(-1);
	}
	user->setBlockStatus(d.is_blocked() ? UserData::BlockStatus::Blocked : UserData::BlockStatus::NotBlocked);
	user->setCallsStatus(d.is_phone_calls_private() ? UserData::CallsStatus::Private : d.is_phone_calls_available() ? UserData::CallsStatus::Enabled : UserData::CallsStatus::Disabled);
	user->setAbout(d.has_about() ? qs(d.vabout) : QString());
	user->setCommonChatsCount(d.vcommon_chats_count.v);
	user->fullUpdated();

	if (req) {
		auto i = _fullPeerRequests.find(user);
		if (i != _fullPeerRequests.cend() && i.value() == req) {
			_fullPeerRequests.erase(i);
		}
	}
	fullPeerUpdated().notify(user);
}

void ApiWrap::requestPeer(PeerData *peer) {
	if (!peer || _fullPeerRequests.contains(peer) || _peerRequests.contains(peer)) return;

	auto sendRequest = [this, peer] {
		auto failHandler = [this, peer](const RPCError &error) {
			_peerRequests.remove(peer);
		};
		auto chatHandler = [this, peer](const MTPmessages_Chats &result) {
			_peerRequests.remove(peer);

			if (auto chats = Api::getChatsFromMessagesChats(result)) {
				auto &v = chats->v;
				bool badVersion = false;
				if (const auto chat = peer->asChat()) {
					badVersion = !v.isEmpty()
						&& (v[0].type() == mtpc_chat)
						&& (v[0].c_chat().vversion.v < chat->version);
				} else if (const auto channel = peer->asChannel()) {
					badVersion = !v.isEmpty()
						&& (v[0].type() == mtpc_channel)
						&& (v[0].c_channel().vversion.v < channel->version);
				}
				auto chat = App::feedChats(*chats);
				if (chat == peer) {
					if (badVersion) {
						if (auto chat = peer->asChat()) {
							chat->version = v[0].c_chat().vversion.v;
						} else if (auto channel = peer->asChannel()) {
							channel->version = v[0].c_channel().vversion.v;
						}
						requestPeer(peer);
					}
				}
			}
		};
		if (auto user = peer->asUser()) {
			return request(MTPusers_GetUsers(MTP_vector<MTPInputUser>(1, user->inputUser))).done([this, user](const MTPVector<MTPUser> &result) {
				_peerRequests.remove(user);
				App::feedUsers(result);
			}).fail(failHandler).send();
		} else if (auto chat = peer->asChat()) {
			return request(MTPmessages_GetChats(MTP_vector<MTPint>(1, chat->inputChat))).done(chatHandler).fail(failHandler).send();
		} else if (auto channel = peer->asChannel()) {
			return request(MTPchannels_GetChannels(MTP_vector<MTPInputChannel>(1, channel->inputChannel))).done(chatHandler).fail(failHandler).send();
		}
		return 0;
	};
	if (auto requestId = sendRequest()) {
		_peerRequests.insert(peer, requestId);
	}
}

void ApiWrap::markMediaRead(
		const base::flat_set<not_null<HistoryItem*>> &items) {
	auto markedIds = QVector<MTPint>();
	auto channelMarkedIds = base::flat_map<
		not_null<ChannelData*>,
		QVector<MTPint>>();
	markedIds.reserve(items.size());
	for (const auto item : items) {
		if (!item->isMediaUnread() || (item->out() && !item->mentionsMe())) {
			continue;
		}
		item->markMediaRead();
		if (!IsServerMsgId(item->id)) {
			continue;
		}
		if (const auto channel = item->history()->peer->asChannel()) {
			channelMarkedIds[channel].push_back(MTP_int(item->id));
		} else {
			markedIds.push_back(MTP_int(item->id));
		}
	}
	if (!markedIds.isEmpty()) {
		request(MTPmessages_ReadMessageContents(
			MTP_vector<MTPint>(markedIds)
		)).done([=](const MTPmessages_AffectedMessages &result) {
			applyAffectedMessages(result);
		}).send();
	}
	for (const auto &channelIds : channelMarkedIds) {
		request(MTPchannels_ReadMessageContents(
			channelIds.first->inputChannel,
			MTP_vector<MTPint>(channelIds.second)
		)).send();
	}
}

void ApiWrap::markMediaRead(not_null<HistoryItem*> item) {
	if (!item->isMediaUnread() || (item->out() && !item->mentionsMe())) {
		return;
	}
	item->markMediaRead();
	if (!IsServerMsgId(item->id)) {
		return;
	}
	const auto ids = MTP_vector<MTPint>(1, MTP_int(item->id));
	if (const auto channel = item->history()->peer->asChannel()) {
		request(MTPchannels_ReadMessageContents(
			channel->inputChannel,
			ids
		)).send();
	} else {
		request(MTPmessages_ReadMessageContents(
			ids
		)).done([=](const MTPmessages_AffectedMessages &result) {
			applyAffectedMessages(result);
		}).send();
	}
}

void ApiWrap::requestPeers(const QList<PeerData*> &peers) {
	QVector<MTPint> chats;
	QVector<MTPInputChannel> channels;
	QVector<MTPInputUser> users;
	chats.reserve(peers.size());
	channels.reserve(peers.size());
	users.reserve(peers.size());
	for (QList<PeerData*>::const_iterator i = peers.cbegin(), e = peers.cend(); i != e; ++i) {
		if (!*i || _fullPeerRequests.contains(*i) || _peerRequests.contains(*i)) continue;
		if ((*i)->isUser()) {
			users.push_back((*i)->asUser()->inputUser);
		} else if ((*i)->isChat()) {
			chats.push_back((*i)->asChat()->inputChat);
		} else if ((*i)->isChannel()) {
			channels.push_back((*i)->asChannel()->inputChannel);
		}
	}
	auto handleChats = [=](const MTPmessages_Chats &result) {
		if (auto chats = Api::getChatsFromMessagesChats(result)) {
			App::feedChats(*chats);
		}
	};
	if (!chats.isEmpty()) {
		request(MTPmessages_GetChats(MTP_vector<MTPint>(chats))).done(handleChats).send();
	}
	if (!channels.isEmpty()) {
		request(MTPchannels_GetChannels(MTP_vector<MTPInputChannel>(channels))).done(handleChats).send();
	}
	if (!users.isEmpty()) {
		request(MTPusers_GetUsers(MTP_vector<MTPInputUser>(users))).done([=](const MTPVector<MTPUser> &result) {
			App::feedUsers(result);
		}).send();
	}
}

void ApiWrap::requestLastParticipants(not_null<ChannelData*> channel) {
	if (!channel->isMegagroup() || _participantsRequests.contains(channel)) {
		return;
	}

	const auto offset = 0;
	const auto participantsHash = 0;
	const auto requestId = request(MTPchannels_GetParticipants(
		channel->inputChannel,
		MTP_channelParticipantsRecent(),
		MTP_int(offset),
		MTP_int(Global::ChatSizeMax()),
		MTP_int(participantsHash)
	)).done([this, channel](const MTPchannels_ChannelParticipants &result) {
		_participantsRequests.remove(channel);
		parseChannelParticipants(channel, result, [&](
				int availableCount,
				const QVector<MTPChannelParticipant> &list) {
			applyLastParticipantsList(
				channel,
				availableCount,
				list);
		});
	}).fail([this, channel](const RPCError &error) {
		_participantsRequests.remove(channel);
	}).send();

	_participantsRequests.insert(channel, requestId);
}

void ApiWrap::requestBots(not_null<ChannelData*> channel) {
	if (!channel->isMegagroup() || _botsRequests.contains(channel)) {
		return;
	}

	auto offset = 0;
	auto participantsHash = 0;
	auto requestId = request(MTPchannels_GetParticipants(
		channel->inputChannel,
		MTP_channelParticipantsBots(),
		MTP_int(offset),
		MTP_int(Global::ChatSizeMax()),
		MTP_int(participantsHash)
	)).done([this, channel](const MTPchannels_ChannelParticipants &result) {
		_botsRequests.remove(channel);
		parseChannelParticipants(channel, result, [&](
				int availableCount,
				const QVector<MTPChannelParticipant> &list) {
			applyBotsList(
				channel,
				availableCount,
				list);
		});
	}).fail([this, channel](const RPCError &error) {
		_botsRequests.remove(channel);
	}).send();

	_botsRequests.insert(channel, requestId);
}

void ApiWrap::requestAdmins(not_null<ChannelData*> channel) {
	if (!channel->isMegagroup() || _adminsRequests.contains(channel)) {
		return;
	}

	auto offset = 0;
	auto participantsHash = 0;
	auto requestId = request(MTPchannels_GetParticipants(
		channel->inputChannel,
		MTP_channelParticipantsAdmins(),
		MTP_int(offset),
		MTP_int(Global::ChatSizeMax()),
		MTP_int(participantsHash)
	)).done([this, channel](const MTPchannels_ChannelParticipants &result) {
		_adminsRequests.remove(channel);
		TLHelp::VisitChannelParticipants(result, base::overload([&](
				const MTPDchannels_channelParticipants &data) {
			App::feedUsers(data.vusers);
			applyAdminsList(
				channel,
				data.vcount.v,
				data.vparticipants.v);
		}, [&](mtpTypeId) {
			LOG(("API Error: channels.channelParticipantsNotModified received!"));
		}));
	}).fail([this, channel](const RPCError &error) {
		_adminsRequests.remove(channel);
	}).send();

	_adminsRequests.insert(channel, requestId);
}

void ApiWrap::applyLastParticipantsList(
		not_null<ChannelData*> channel,
		int availableCount,
		const QVector<MTPChannelParticipant> &list) {
	channel->mgInfo->lastAdmins.clear();
	channel->mgInfo->lastRestricted.clear();
	channel->mgInfo->lastParticipants.clear();
	channel->mgInfo->lastParticipantsStatus = MegagroupInfo::LastParticipantsUpToDate;

	auto botStatus = channel->mgInfo->botStatus;
	const auto emptyAdminRights = MTP_channelAdminRights(MTP_flags(0));
	const auto emptyRestrictedRights = MTP_channelBannedRights(
		MTP_flags(0),
		MTP_int(0));
	for (const auto &p : list) {
		const auto userId = TLHelp::ReadChannelParticipantUserId(p);
		const auto adminCanEdit = (p.type() == mtpc_channelParticipantAdmin)
			? p.c_channelParticipantAdmin().is_can_edit()
			: false;
		const auto adminRights = (p.type() == mtpc_channelParticipantAdmin)
			? p.c_channelParticipantAdmin().vadmin_rights
			: emptyAdminRights;
		const auto restrictedRights = (p.type() == mtpc_channelParticipantBanned)
			? p.c_channelParticipantBanned().vbanned_rights
			: emptyRestrictedRights;
		if (!userId) {
			continue;
		}

		auto user = App::user(userId);
		if (p.type() == mtpc_channelParticipantCreator) {
			channel->mgInfo->creator = user;
			if (!channel->mgInfo->admins.empty()
				&& !channel->mgInfo->admins.contains(userId)) {
				Data::ChannelAdminChanges changes(channel);
				changes.feed(userId, true);
			}
		}
		if (!base::contains(channel->mgInfo->lastParticipants, user)) {
			channel->mgInfo->lastParticipants.push_back(user);
			if (adminRights.c_channelAdminRights().vflags.v) {
				channel->mgInfo->lastAdmins.emplace(
					user,
					MegagroupInfo::Admin{ adminRights, adminCanEdit });
			} else if (restrictedRights.c_channelBannedRights().vflags.v != 0) {
				channel->mgInfo->lastRestricted.emplace(
					user,
					MegagroupInfo::Restricted{ restrictedRights });
			}
			if (user->botInfo) {
				channel->mgInfo->bots.insert(user);
				if (channel->mgInfo->botStatus != 0 && channel->mgInfo->botStatus < 2) {
					channel->mgInfo->botStatus = 2;
				}
			}
		}
	}
	//
	// getParticipants(Recent) sometimes can't return all members,
	// only some last subset, size of this subset is availableCount.
	//
	// So both list size and availableCount have nothing to do with
	// the full supergroup members count.
	//
	//if (list.isEmpty()) {
	//	channel->setMembersCount(channel->mgInfo->lastParticipants.size());
	//} else {
	//	channel->setMembersCount(availableCount);
	//}
	Notify::PeerUpdate update(channel);
	update.flags |= Notify::PeerUpdate::Flag::MembersChanged | Notify::PeerUpdate::Flag::AdminsChanged;
	Notify::peerUpdatedDelayed(update);

	channel->mgInfo->botStatus = botStatus;
	if (App::main()) fullPeerUpdated().notify(channel);
}

void ApiWrap::applyBotsList(
		not_null<ChannelData*> channel,
		int availableCount,
		const QVector<MTPChannelParticipant> &list) {
	const auto history = App::historyLoaded(channel->id);
	channel->mgInfo->bots.clear();
	channel->mgInfo->botStatus = -1;

	auto needBotsInfos = false;
	auto botStatus = channel->mgInfo->botStatus;
	auto keyboardBotFound = !history || !history->lastKeyboardFrom;
	for (const auto &p : list) {
		const auto userId = TLHelp::ReadChannelParticipantUserId(p);
		if (!userId) {
			continue;
		}

		auto user = App::user(userId);
		if (user->botInfo) {
			channel->mgInfo->bots.insert(user);
			botStatus = 2;// (botStatus > 0/* || !i.key()->botInfo->readsAllHistory*/) ? 2 : 1;
			if (!user->botInfo->inited) {
				needBotsInfos = true;
			}
		}
		if (!keyboardBotFound && user->id == history->lastKeyboardFrom) {
			keyboardBotFound = true;
		}
	}
	if (needBotsInfos) {
		requestFullPeer(channel);
	}
	if (!keyboardBotFound) {
		history->clearLastKeyboard();
	}

	channel->mgInfo->botStatus = botStatus;
	if (App::main()) fullPeerUpdated().notify(channel);
}

void ApiWrap::applyAdminsList(
		not_null<ChannelData*> channel,
		int availableCount,
		const QVector<MTPChannelParticipant> &list) {
	auto admins = ranges::make_iterator_range(
		list.begin(), list.end()
	) | ranges::view::transform([](const MTPChannelParticipant &p) {
		return TLHelp::ReadChannelParticipantUserId(p);
	});
	auto adding = base::flat_set<UserId>{ admins.begin(), admins.end() };
	if (channel->mgInfo->creator) {
		adding.insert(peerToUser(channel->mgInfo->creator->id));
	}
	auto removing = channel->mgInfo->admins;

	if (removing.empty() && adding.empty()) {
		// Add some admin-placeholder so we don't DDOS
		// server with admins list requests.
		LOG(("API Error: Got empty admins list from server."));
		adding.insert(0);
	}

	Data::ChannelAdminChanges changes(channel);
	for (const auto addingId : adding) {
		if (!removing.remove(addingId)) {
			changes.feed(addingId, true);
		}
	}
	for (const auto removingId : removing) {
		changes.feed(removingId, false);
	}
}

void ApiWrap::requestSelfParticipant(ChannelData *channel) {
	if (_selfParticipantRequests.contains(channel)) {
		return;
	}

	auto requestId = request(MTPchannels_GetParticipant(
		channel->inputChannel,
		MTP_inputUserSelf()
	)).done([this, channel](const MTPchannels_ChannelParticipant &result) {
		_selfParticipantRequests.remove(channel);
		if (result.type() != mtpc_channels_channelParticipant) {
			LOG(("API Error: unknown type in gotSelfParticipant (%1)").arg(result.type()));
			channel->inviter = -1;
			if (App::main()) App::main()->onSelfParticipantUpdated(channel);
			return;
		}

		auto &p = result.c_channels_channelParticipant();
		App::feedUsers(p.vusers);

		switch (p.vparticipant.type()) {
		case mtpc_channelParticipantSelf: {
			auto &d = p.vparticipant.c_channelParticipantSelf();
			channel->inviter = d.vinviter_id.v;
			channel->inviteDate = d.vdate.v;
		} break;
		case mtpc_channelParticipantCreator: {
			auto &d = p.vparticipant.c_channelParticipantCreator();
			channel->inviter = _session->userId();
			channel->inviteDate = channel->date;
			if (channel->mgInfo) {
				channel->mgInfo->creator = App::self();
			}
		} break;
		case mtpc_channelParticipantAdmin: {
			auto &d = p.vparticipant.c_channelParticipantAdmin();
			channel->inviter = d.vinviter_id.v;
			channel->inviteDate = d.vdate.v;
		} break;
		}

		if (App::main()) App::main()->onSelfParticipantUpdated(channel);
	}).fail([this, channel](const RPCError &error) {
		_selfParticipantRequests.remove(channel);
		if (error.type() == qstr("USER_NOT_PARTICIPANT")) {
			channel->inviter = -1;
		}
	}).afterDelay(kSmallDelayMs).send();

	_selfParticipantRequests.insert(channel, requestId);
}

void ApiWrap::kickParticipant(
		not_null<ChatData*> chat,
		not_null<UserData*> user) {
	request(MTPmessages_DeleteChatUser(
		chat->inputChat,
		user->inputUser
	)).done([=](const MTPUpdates &result) {
		applyUpdates(result);
	}).send();
}

void ApiWrap::kickParticipant(
		not_null<ChannelData*> channel,
		not_null<UserData*> user,
		const MTPChannelBannedRights &currentRights) {
	const auto kick = KickRequest(channel, user);
	if (_kickRequests.contains(kick)) return;

	const auto rights = ChannelData::KickedRestrictedRights();
	const auto requestId = request(MTPchannels_EditBanned(
		channel->inputChannel,
		user->inputUser,
		rights
	)).done([=](const MTPUpdates &result) {
		applyUpdates(result);

		_kickRequests.remove(KickRequest(channel, user));
		channel->applyEditBanned(user, currentRights, rights);
	}).fail([this, kick](const RPCError &error) {
		_kickRequests.remove(kick);
	}).send();

	_kickRequests.emplace(kick, requestId);
}

void ApiWrap::unblockParticipant(
		not_null<ChannelData*> channel,
		not_null<UserData*> user) {
	const auto kick = KickRequest(channel, user);
	if (_kickRequests.contains(kick)) {
		return;
	}

	const auto requestId = request(MTPchannels_EditBanned(
		channel->inputChannel,
		user->inputUser,
		MTP_channelBannedRights(MTP_flags(0), MTP_int(0))
	)).done([=](const MTPUpdates &result) {
		applyUpdates(result);

		_kickRequests.remove(KickRequest(channel, user));
		if (channel->kickedCount() > 0) {
			channel->setKickedCount(channel->kickedCount() - 1);
		} else {
			channel->updateFullForced();
		}
	}).fail([this, kick](const RPCError &error) {
		_kickRequests.remove(kick);
	}).send();

	_kickRequests.emplace(kick, requestId);
}

void ApiWrap::deleteAllFromUser(
		not_null<ChannelData*> channel,
		not_null<UserData*> from) {
	const auto history = App::historyLoaded(channel->id);
	const auto ids = history
		? history->collectMessagesFromUserToDelete(from)
		: QVector<MsgId>();
	const auto channelId = peerToChannel(channel->id);
	for (const auto msgId : ids) {
		if (const auto item = App::histItemById(channelId, msgId)) {
			item->destroy();
		}
	}

	_session->data().sendHistoryChangeNotifications();

	deleteAllFromUserSend(channel, from);
}

void ApiWrap::deleteAllFromUserSend(
		not_null<ChannelData*> channel,
		not_null<UserData*> from) {
	request(MTPchannels_DeleteUserHistory(
		channel->inputChannel,
		from->inputUser
	)).done([=](const MTPmessages_AffectedHistory &result) {
		const auto offset = applyAffectedHistory(channel, result);
		if (offset > 0) {
			deleteAllFromUserSend(channel, from);
		} else if (const auto history = App::historyLoaded(channel)) {
			if (!history->lastMessageKnown()) {
				requestDialogEntry(history);
			}
		}
	}).send();
}

void ApiWrap::requestChannelMembersForAdd(
		not_null<ChannelData*> channel,
		Fn<void(const MTPchannels_ChannelParticipants&)> callback) {
	_channelMembersForAddCallback = std::move(callback);
	if (_channelMembersForAdd == channel) {
		return;
	}
	request(base::take(_channelMembersForAddRequestId)).cancel();

	auto offset = 0;
	auto participantsHash = 0;

	_channelMembersForAdd = channel;
	_channelMembersForAddRequestId = request(MTPchannels_GetParticipants(
		channel->inputChannel,
		MTP_channelParticipantsRecent(),
		MTP_int(offset),
		MTP_int(Global::ChatSizeMax()),
		MTP_int(participantsHash)
	)).done([this](const MTPchannels_ChannelParticipants &result) {
		base::take(_channelMembersForAddRequestId);
		base::take(_channelMembersForAdd);
		base::take(_channelMembersForAddCallback)(result);
	}).fail([this](const RPCError &error) {
		base::take(_channelMembersForAddRequestId);
		base::take(_channelMembersForAdd);
		base::take(_channelMembersForAddCallback);
	}).send();
}

void ApiWrap::scheduleStickerSetRequest(uint64 setId, uint64 access) {
	if (!_stickerSetRequests.contains(setId)) {
		_stickerSetRequests.insert(setId, qMakePair(access, 0));
	}
}

void ApiWrap::requestStickerSets() {
	for (auto i = _stickerSetRequests.begin(), j = i, e = _stickerSetRequests.end(); i != e; i = j) {
		++j;
		if (i.value().second) continue;

		auto waitMs = (j == e) ? 0 : kSmallDelayMs;
		i.value().second = request(MTPmessages_GetStickerSet(MTP_inputStickerSetID(MTP_long(i.key()), MTP_long(i.value().first)))).done([this, setId = i.key()](const MTPmessages_StickerSet &result) {
			gotStickerSet(setId, result);
		}).fail([this, setId = i.key()](const RPCError &error) {
			_stickerSetRequests.remove(setId);
		}).afterDelay(waitMs).send();
	}
}

void ApiWrap::saveStickerSets(
		const Stickers::Order &localOrder,
		const Stickers::Order &localRemoved) {
	for (auto requestId : base::take(_stickerSetDisenableRequests)) {
		request(requestId).cancel();
	}
	request(base::take(_stickersReorderRequestId)).cancel();
	request(base::take(_stickersClearRecentRequestId)).cancel();

	auto writeInstalled = true, writeRecent = false, writeCloudRecent = false, writeFaved = false, writeArchived = false;
	auto &recent = Stickers::GetRecentPack();
	auto &sets = _session->data().stickerSetsRef();

	_stickersOrder = localOrder;
	for_const (auto removedSetId, localRemoved) {
		if (removedSetId == Stickers::CloudRecentSetId) {
			if (sets.remove(Stickers::CloudRecentSetId) != 0) {
				writeCloudRecent = true;
			}
			if (sets.remove(Stickers::CustomSetId)) {
				writeInstalled = true;
			}
			if (!recent.isEmpty()) {
				recent.clear();
				writeRecent = true;
			}

			_stickersClearRecentRequestId = request(MTPmessages_ClearRecentStickers(
				MTP_flags(0)
			)).done([this](const MTPBool &result) {
				_stickersClearRecentRequestId = 0;
			}).fail([this](const RPCError &error) {
				_stickersClearRecentRequestId = 0;
			}).send();
			continue;
		}

		auto it = sets.find(removedSetId);
		if (it != sets.cend()) {
			for (auto i = recent.begin(); i != recent.cend();) {
				if (it->stickers.indexOf(i->first) >= 0) {
					i = recent.erase(i);
					writeRecent = true;
				} else {
					++i;
				}
			}
			if (!(it->flags & MTPDstickerSet::Flag::f_archived)) {
				MTPInputStickerSet setId = (it->id && it->access) ? MTP_inputStickerSetID(MTP_long(it->id), MTP_long(it->access)) : MTP_inputStickerSetShortName(MTP_string(it->shortName));

				auto requestId = request(MTPmessages_UninstallStickerSet(setId)).done([this](const MTPBool &result, mtpRequestId requestId) {
					stickerSetDisenabled(requestId);
				}).fail([this](const RPCError &error, mtpRequestId requestId) {
					stickerSetDisenabled(requestId);
				}).afterDelay(kSmallDelayMs).send();

				_stickerSetDisenableRequests.insert(requestId);

				int removeIndex = _session->data().stickerSetsOrder().indexOf(it->id);
				if (removeIndex >= 0) _session->data().stickerSetsOrderRef().removeAt(removeIndex);
				if (!(it->flags & MTPDstickerSet_ClientFlag::f_featured)
					&& !(it->flags & MTPDstickerSet_ClientFlag::f_special)) {
					sets.erase(it);
				} else {
					if (it->flags & MTPDstickerSet::Flag::f_archived) {
						writeArchived = true;
					}
					it->flags &= ~(MTPDstickerSet::Flag::f_installed_date | MTPDstickerSet::Flag::f_archived);
					it->installDate = TimeId(0);
				}
			}
		}
	}

	// Clear all installed flags, set only for sets from order.
	for (auto &set : sets) {
		if (!(set.flags & MTPDstickerSet::Flag::f_archived)) {
			set.flags &= ~MTPDstickerSet::Flag::f_installed_date;
		}
	}

	auto &order = _session->data().stickerSetsOrderRef();
	order.clear();
	for_const (auto setId, _stickersOrder) {
		auto it = sets.find(setId);
		if (it != sets.cend()) {
			if ((it->flags & MTPDstickerSet::Flag::f_archived) && !localRemoved.contains(it->id)) {
				MTPInputStickerSet mtpSetId = (it->id && it->access) ? MTP_inputStickerSetID(MTP_long(it->id), MTP_long(it->access)) : MTP_inputStickerSetShortName(MTP_string(it->shortName));

				auto requestId = request(MTPmessages_InstallStickerSet(mtpSetId, MTP_boolFalse())).done([this](const MTPmessages_StickerSetInstallResult &result, mtpRequestId requestId) {
					stickerSetDisenabled(requestId);
				}).fail([this](const RPCError &error, mtpRequestId requestId) {
					stickerSetDisenabled(requestId);
				}).afterDelay(kSmallDelayMs).send();

				_stickerSetDisenableRequests.insert(requestId);

				it->flags &= ~MTPDstickerSet::Flag::f_archived;
				writeArchived = true;
			}
			order.push_back(setId);
			it->flags |= MTPDstickerSet::Flag::f_installed_date;
			if (!it->installDate) {
				it->installDate = unixtime();
			}
		}
	}
	for (auto it = sets.begin(); it != sets.cend();) {
		if ((it->flags & MTPDstickerSet_ClientFlag::f_featured)
			|| (it->flags & MTPDstickerSet::Flag::f_installed_date)
			|| (it->flags & MTPDstickerSet::Flag::f_archived)
			|| (it->flags & MTPDstickerSet_ClientFlag::f_special)) {
			++it;
		} else {
			it = sets.erase(it);
		}
	}

	if (writeInstalled) Local::writeInstalledStickers();
	if (writeRecent) Local::writeUserSettings();
	if (writeArchived) Local::writeArchivedStickers();
	if (writeCloudRecent) Local::writeRecentStickers();
	if (writeFaved) Local::writeFavedStickers();
	_session->data().notifyStickersUpdated();

	if (_stickerSetDisenableRequests.empty()) {
		stickersSaveOrder();
	} else {
		requestSendDelayed();
	}
}

void ApiWrap::stickerSetDisenabled(mtpRequestId requestId) {
	_stickerSetDisenableRequests.remove(requestId);
	if (_stickerSetDisenableRequests.empty()) {
		stickersSaveOrder();
	}
};

void ApiWrap::joinChannel(not_null<ChannelData*> channel) {
	if (channel->amIn()) {
		Notify::peerUpdatedDelayed(
			channel,
			Notify::PeerUpdate::Flag::ChannelAmIn);
	} else if (!_channelAmInRequests.contains(channel)) {
		auto requestId = request(MTPchannels_JoinChannel(
			channel->inputChannel
		)).done([=](const MTPUpdates &result) {
			_channelAmInRequests.remove(channel);
			applyUpdates(result);
		}).fail([=](const RPCError &error) {
			if (error.type() == qstr("CHANNEL_PRIVATE")
				|| error.type() == qstr("CHANNEL_PUBLIC_GROUP_NA")
				|| error.type() == qstr("USER_BANNED_IN_CHANNEL")) {
				Ui::show(Box<InformBox>(lang(channel->isMegagroup()
					? lng_group_not_accessible
					: lng_channel_not_accessible)));
			} else if (error.type() == qstr("CHANNELS_TOO_MUCH")) {
				Ui::show(Box<InformBox>(lang(lng_join_channel_error)));
			} else if (error.type() == qstr("USERS_TOO_MUCH")) {
				Ui::show(Box<InformBox>(lang(lng_group_full)));
			}
			_channelAmInRequests.remove(channel);
		}).send();

		_channelAmInRequests.insert(channel, requestId);
	}
}

void ApiWrap::leaveChannel(not_null<ChannelData*> channel) {
	if (!channel->amIn()) {
		Notify::peerUpdatedDelayed(
			channel,
			Notify::PeerUpdate::Flag::ChannelAmIn);
	} else if (!_channelAmInRequests.contains(channel)) {
		auto requestId = request(MTPchannels_LeaveChannel(
			channel->inputChannel
		)).done([=](const MTPUpdates &result) {
			_channelAmInRequests.remove(channel);
			applyUpdates(result);
		}).fail([=](const RPCError &error) {
			_channelAmInRequests.remove(channel);
		}).send();

		_channelAmInRequests.insert(channel, requestId);
	}
}

void ApiWrap::blockUser(not_null<UserData*> user) {
	if (user->isBlocked()) {
		Notify::peerUpdatedDelayed(user, Notify::PeerUpdate::Flag::UserIsBlocked);
	} else if (_blockRequests.find(user) == end(_blockRequests)) {
		auto requestId = request(MTPcontacts_Block(user->inputUser)).done([this, user](const MTPBool &result) {
			_blockRequests.erase(user);
			user->setBlockStatus(UserData::BlockStatus::Blocked);
		}).fail([this, user](const RPCError &error) {
			_blockRequests.erase(user);
		}).send();

		_blockRequests.emplace(user, requestId);
	}
}

void ApiWrap::unblockUser(not_null<UserData*> user) {
	if (!user->isBlocked()) {
		Notify::peerUpdatedDelayed(user, Notify::PeerUpdate::Flag::UserIsBlocked);
	} else if (_blockRequests.find(user) == end(_blockRequests)) {
		auto requestId = request(MTPcontacts_Unblock(user->inputUser)).done([this, user](const MTPBool &result) {
			_blockRequests.erase(user);
			user->setBlockStatus(UserData::BlockStatus::NotBlocked);
		}).fail([this, user](const RPCError &error) {
			_blockRequests.erase(user);
		}).send();

		_blockRequests.emplace(user, requestId);
	}
}

void ApiWrap::exportInviteLink(not_null<PeerData*> peer) {
	if (_exportInviteRequests.find(peer) != end(_exportInviteRequests)) {
		return;
	}

	const auto sendRequest = [this, peer] {
		const auto exportFail = [this, peer](const RPCError &error) {
			_exportInviteRequests.erase(peer);
		};
		if (const auto chat = peer->asChat()) {
			return request(MTPmessages_ExportChatInvite(
				chat->inputChat
			)).done([=](const MTPExportedChatInvite &result) {
				_exportInviteRequests.erase(chat);
				chat->setInviteLink(
					(result.type() == mtpc_chatInviteExported
						? qs(result.c_chatInviteExported().vlink)
						: QString()));
			}).fail(exportFail).send();
		} else if (const auto channel = peer->asChannel()) {
			return request(MTPchannels_ExportInvite(
				channel->inputChannel
			)).done([=](const MTPExportedChatInvite &result) {
				_exportInviteRequests.erase(channel);
				channel->setInviteLink(
					(result.type() == mtpc_chatInviteExported
						? qs(result.c_chatInviteExported().vlink)
						: QString()));
			}).fail(exportFail).send();
		}
		return 0;
	};
	if (const auto requestId = sendRequest()) {
		_exportInviteRequests.emplace(peer, requestId);
	}
}

void ApiWrap::requestNotifySettings(const MTPInputNotifyPeer &peer) {
	const auto key = [&] {
		switch (peer.type()) {
		case mtpc_inputNotifyUsers: return peerFromUser(0);
		case mtpc_inputNotifyChats: return peerFromChat(0);
		case mtpc_inputNotifyPeer: {
			const auto &inner = peer.c_inputNotifyPeer().vpeer;
			switch (inner.type()) {
			case mtpc_inputPeerSelf:
				return _session->userPeerId();
			case mtpc_inputPeerEmpty:
				return PeerId(0);
			case mtpc_inputPeerChannel:
				return peerFromChannel(
					inner.c_inputPeerChannel().vchannel_id);
			case mtpc_inputPeerChat:
				return peerFromChat(inner.c_inputPeerChat().vchat_id);
			case mtpc_inputPeerUser:
				return peerFromUser(inner.c_inputPeerUser().vuser_id);
			}
			Unexpected("Type in ApiRequest::requestNotifySettings peer.");
		} break;
		}
		Unexpected("Type in ApiRequest::requestNotifySettings.");
	}();
	if (_notifySettingRequests.find(key) != end(_notifySettingRequests)) {
		return;
	}
	auto requestId = request(MTPaccount_GetNotifySettings(
		peer
	)).done([=](const MTPPeerNotifySettings &result) {
		notifySettingReceived(peer, result);
		_notifySettingRequests.erase(key);
	}).fail([=](const RPCError &error) {
		notifySettingReceived(peer, MTP_peerNotifySettings(
			MTP_flags(0),
			MTPBool(),
			MTPBool(),
			MTPint(),
			MTPstring()));
		_notifySettingRequests.erase(key);
	}).send();

	_notifySettingRequests.emplace(key, requestId);
}

void ApiWrap::updateNotifySettingsDelayed(not_null<const PeerData*> peer) {
	_updateNotifySettingsPeers.emplace(peer);
	_updateNotifySettingsTimer.callOnce(kNotifySettingSaveTimeout);
}

void ApiWrap::sendNotifySettingsUpdates() {
	while (!_updateNotifySettingsPeers.empty()) {
		const auto peer = *_updateNotifySettingsPeers.begin();
		_updateNotifySettingsPeers.erase(_updateNotifySettingsPeers.begin());
		request(MTPaccount_UpdateNotifySettings(
			MTP_inputNotifyPeer(peer->input),
			peer->notifySerialize()
		)).afterDelay(_updateNotifySettingsPeers.empty() ? 0 : 10).send();
	}
}

void ApiWrap::saveDraftToCloudDelayed(not_null<History*> history) {
	_draftsSaveRequestIds.emplace(history, 0);
	if (!_draftsSaveTimer.isActive()) {
		_draftsSaveTimer.callOnce(kSaveCloudDraftTimeout);
	}
}

void ApiWrap::savePrivacy(const MTPInputPrivacyKey &key, QVector<MTPInputPrivacyRule> &&rules) {
	auto keyTypeId = key.type();
	auto it = _privacySaveRequests.find(keyTypeId);
	if (it != _privacySaveRequests.cend()) {
		request(it->second).cancel();
		_privacySaveRequests.erase(it);
	}

	auto requestId = request(MTPaccount_SetPrivacy(key, MTP_vector<MTPInputPrivacyRule>(std::move(rules)))).done([this, keyTypeId](const MTPaccount_PrivacyRules &result) {
		Expects(result.type() == mtpc_account_privacyRules);

		auto &rules = result.c_account_privacyRules();
		App::feedUsers(rules.vusers);
		_privacySaveRequests.remove(keyTypeId);
		handlePrivacyChange(keyTypeId, rules.vrules);
	}).fail([this, keyTypeId](const RPCError &error) {
		_privacySaveRequests.remove(keyTypeId);
	}).send();

	_privacySaveRequests.emplace(keyTypeId, requestId);
}

void ApiWrap::handlePrivacyChange(mtpTypeId keyTypeId, const MTPVector<MTPPrivacyRule> &rules) {
	if (keyTypeId == mtpc_privacyKeyStatusTimestamp) {
		enum class Rule {
			Unknown,
			Allow,
			Disallow,
		};
		auto userRules = QMap<UserId, Rule>();
		auto contactsRule = Rule::Unknown;
		auto everyoneRule = Rule::Unknown;
		for (auto &rule : rules.v) {
			auto type = rule.type();
			if (type != mtpc_privacyValueAllowAll && type != mtpc_privacyValueDisallowAll && contactsRule != Rule::Unknown) {
				// This is simplified: we ignore per-user rules that come after a contacts rule.
				// But none of the official apps provide such complicated rule sets, so its fine.
				continue;
			}

			switch (type) {
			case mtpc_privacyValueAllowAll: everyoneRule = Rule::Allow; break;
			case mtpc_privacyValueDisallowAll: everyoneRule = Rule::Disallow; break;
			case mtpc_privacyValueAllowContacts: contactsRule = Rule::Allow; break;
			case mtpc_privacyValueDisallowContacts: contactsRule = Rule::Disallow; break;
			case mtpc_privacyValueAllowUsers: {
				for_const (auto &userId, rule.c_privacyValueAllowUsers().vusers.v) {
					if (!userRules.contains(userId.v)) {
						userRules.insert(userId.v, Rule::Allow);
					}
				}
			} break;
			case mtpc_privacyValueDisallowUsers: {
				for_const (auto &userId, rule.c_privacyValueDisallowUsers().vusers.v) {
					if (!userRules.contains(userId.v)) {
						userRules.insert(userId.v, Rule::Disallow);
					}
				}
			} break;
			}
			if (everyoneRule != Rule::Unknown) {
				break;
			}
		}

		auto now = unixtime();
		App::enumerateUsers([&](UserData *user) {
			if (user->isSelf() || user->loadedStatus != PeerData::FullLoaded) {
				return;
			}
			if (user->onlineTill <= 0) {
				return;
			}

			if (user->onlineTill + 3 * 86400 >= now) {
				user->onlineTill = -2; // recently
			} else if (user->onlineTill + 7 * 86400 >= now) {
				user->onlineTill = -3; // last week
			} else if (user->onlineTill + 30 * 86400 >= now) {
				user->onlineTill = -4; // last month
			} else {
				user->onlineTill = 0;
			}
			Notify::peerUpdatedDelayed(user, Notify::PeerUpdate::Flag::UserOnlineChanged);
		});

		if (_contactsStatusesRequestId) {
			request(_contactsStatusesRequestId).cancel();
		}
		_contactsStatusesRequestId = request(MTPcontacts_GetStatuses()).done([this](const MTPVector<MTPContactStatus> &result) {
			_contactsStatusesRequestId = 0;
			for_const (auto &item, result.v) {
				Assert(item.type() == mtpc_contactStatus);
				auto &data = item.c_contactStatus();
				if (auto user = App::userLoaded(data.vuser_id.v)) {
					auto oldOnlineTill = user->onlineTill;
					auto newOnlineTill = onlineTillFromStatus(data.vstatus, oldOnlineTill);
					if (oldOnlineTill != newOnlineTill) {
						user->onlineTill = newOnlineTill;
						Notify::peerUpdatedDelayed(user, Notify::PeerUpdate::Flag::UserOnlineChanged);
					}
				}
			}
		}).fail([this](const RPCError &error) {
			_contactsStatusesRequestId = 0;
		}).send();
	}
}

int ApiWrap::onlineTillFromStatus(const MTPUserStatus &status, int currentOnlineTill) {
	switch (status.type()) {
	case mtpc_userStatusEmpty: return 0;
	case mtpc_userStatusRecently: return (currentOnlineTill > -10) ? -2 : currentOnlineTill; // don't modify pseudo-online
	case mtpc_userStatusLastWeek: return -3;
	case mtpc_userStatusLastMonth: return -4;
	case mtpc_userStatusOffline: return status.c_userStatusOffline().vwas_online.v;
	case mtpc_userStatusOnline: return status.c_userStatusOnline().vexpires.v;
	}
	Unexpected("Bad UserStatus type.");
}

void ApiWrap::clearHistory(not_null<PeerData*> peer) {
	auto deleteTillId = MsgId(0);
	if (auto history = App::historyLoaded(peer->id)) {
		if (const auto last = history->lastMessage()) {
			deleteTillId = last->id;
			Local::addSavedPeer(history->peer, ItemDateTime(last));
		}
		history->clear();
		history->markFullyLoaded();
	}
	if (const auto channel = peer->asChannel()) {
		if (const auto migrated = peer->migrateFrom()) {
			clearHistory(migrated);
		}
		if (IsServerMsgId(deleteTillId)) {
			request(MTPchannels_DeleteHistory(
				channel->inputChannel,
				MTP_int(deleteTillId)
			)).send();
		}
	} else {
		request(MTPmessages_DeleteHistory(
			MTP_flags(MTPmessages_DeleteHistory::Flag::f_just_clear),
			peer->input,
			MTP_int(0)
		)).done([=](const MTPmessages_AffectedHistory &result) {
			const auto offset = applyAffectedHistory(peer, result);
			if (offset > 0) {
				clearHistory(peer);
			}
		}).send();
	}
}

int ApiWrap::applyAffectedHistory(
		not_null<PeerData*> peer,
		const MTPmessages_AffectedHistory &result) {
	const auto &data = result.c_messages_affectedHistory();
	if (const auto channel = peer->asChannel()) {
		channel->ptsUpdateAndApply(data.vpts.v, data.vpts_count.v);
	} else {
		App::main()->ptsUpdateAndApply(data.vpts.v, data.vpts_count.v);
	}
	return data.voffset.v;
}

void ApiWrap::applyAffectedMessages(
		not_null<PeerData*> peer,
		const MTPmessages_AffectedMessages &result) {
	const auto &data = result.c_messages_affectedMessages();
	if (const auto channel = peer->asChannel()) {
		channel->ptsUpdateAndApply(data.vpts.v, data.vpts_count.v);
	} else {
		applyAffectedMessages(result);
	}
}

void ApiWrap::applyAffectedMessages(
		const MTPmessages_AffectedMessages &result) {
	const auto &data = result.c_messages_affectedMessages();
	App::main()->ptsUpdateAndApply(data.vpts.v, data.vpts_count.v);
}

void ApiWrap::saveDraftsToCloud() {
	for (auto i = _draftsSaveRequestIds.begin(), e = _draftsSaveRequestIds.end(); i != e; ++i) {
		if (i->second) continue; // sent already

		auto history = i->first;
		auto cloudDraft = history->cloudDraft();
		auto localDraft = history->localDraft();
		if (cloudDraft && cloudDraft->saveRequestId) {
			request(base::take(cloudDraft->saveRequestId)).cancel();
		}
		cloudDraft = history->createCloudDraft(localDraft);

		auto flags = MTPmessages_SaveDraft::Flags(0);
		auto &textWithTags = cloudDraft->textWithTags;
		if (cloudDraft->previewCancelled) {
			flags |= MTPmessages_SaveDraft::Flag::f_no_webpage;
		}
		if (cloudDraft->msgId) {
			flags |= MTPmessages_SaveDraft::Flag::f_reply_to_msg_id;
		}
		if (!textWithTags.tags.isEmpty()) {
			flags |= MTPmessages_SaveDraft::Flag::f_entities;
		}
		auto entities = TextUtilities::EntitiesToMTP(
			ConvertTextTagsToEntities(textWithTags.tags),
			TextUtilities::ConvertOption::SkipLocal);

		history->setSentDraftText(textWithTags.text);
		cloudDraft->saveRequestId = request(MTPmessages_SaveDraft(
			MTP_flags(flags),
			MTP_int(cloudDraft->msgId),
			history->peer->input,
			MTP_string(textWithTags.text),
			entities
		)).done([=](const MTPBool &result, mtpRequestId requestId) {
			history->clearSentDraftText();

			if (const auto cloudDraft = history->cloudDraft()) {
				if (cloudDraft->saveRequestId == requestId) {
					cloudDraft->saveRequestId = 0;
					history->draftSavedToCloud();
				}
			}
			auto i = _draftsSaveRequestIds.find(history);
			if (i != _draftsSaveRequestIds.cend() && i->second == requestId) {
				_draftsSaveRequestIds.erase(history);
				checkQuitPreventFinished();
			}
		}).fail([=](const RPCError &error, mtpRequestId requestId) {
			history->clearSentDraftText();

			if (const auto cloudDraft = history->cloudDraft()) {
				if (cloudDraft->saveRequestId == requestId) {
					history->clearCloudDraft();
				}
			}
			auto i = _draftsSaveRequestIds.find(history);
			if (i != _draftsSaveRequestIds.cend() && i->second == requestId) {
				_draftsSaveRequestIds.erase(history);
				checkQuitPreventFinished();
			}
		}).send();

		i->second = cloudDraft->saveRequestId;
	}
}

bool ApiWrap::isQuitPrevent() {
	if (_draftsSaveRequestIds.empty()) {
		return false;
	}
	LOG(("ApiWrap prevents quit, saving drafts..."));
	saveDraftsToCloud();
	return true;
}

void ApiWrap::checkQuitPreventFinished() {
	if (_draftsSaveRequestIds.empty()) {
		if (App::quitting()) {
			LOG(("ApiWrap doesn't prevent quit any more."));
		}
		Messenger::Instance().quitPreventFinished();
	}
}

void ApiWrap::notifySettingReceived(
		MTPInputNotifyPeer notifyPeer,
		const MTPPeerNotifySettings &settings) {
	switch (notifyPeer.type()) {
	case mtpc_inputNotifyUsers:
		_session->data().applyNotifySetting(MTP_notifyUsers(), settings);
	break;
	case mtpc_inputNotifyChats:
		_session->data().applyNotifySetting(MTP_notifyChats(), settings);
	break;
	case mtpc_inputNotifyPeer: {
		auto &peer = notifyPeer.c_inputNotifyPeer().vpeer;
		const auto apply = [&](PeerId peerId) {
			_session->data().applyNotifySetting(
				MTP_notifyPeer(peerToMTP(peerId)),
				settings);
		};
		switch (peer.type()) {
		case mtpc_inputPeerEmpty:
			apply(0);
			break;
		case mtpc_inputPeerSelf:
			apply(_session->userPeerId());
			break;
		case mtpc_inputPeerUser:
			apply(peerFromUser(peer.c_inputPeerUser().vuser_id));
			break;
		case mtpc_inputPeerChat:
			apply(peerFromChat(peer.c_inputPeerChat().vchat_id));
			break;
		case mtpc_inputPeerChannel:
			apply(peerFromChannel(peer.c_inputPeerChannel().vchannel_id));
			break;
		}
	} break;
	}
	_session->notifications().checkDelayed();
}

void ApiWrap::gotStickerSet(uint64 setId, const MTPmessages_StickerSet &result) {
	_stickerSetRequests.remove(setId);
	Stickers::FeedSetFull(result);
}

void ApiWrap::requestWebPageDelayed(WebPageData *page) {
	if (page->pendingTill <= 0) return;
	_webPagesPending.insert(page, 0);
	auto left = (page->pendingTill - unixtime()) * 1000;
	if (!_webPagesTimer.isActive() || left <= _webPagesTimer.remainingTime()) {
		_webPagesTimer.callOnce((left < 0 ? 0 : left) + 1);
	}
}

void ApiWrap::clearWebPageRequest(WebPageData *page) {
	_webPagesPending.remove(page);
	if (_webPagesPending.isEmpty() && _webPagesTimer.isActive()) {
		_webPagesTimer.cancel();
	}
}

void ApiWrap::clearWebPageRequests() {
	_webPagesPending.clear();
	_webPagesTimer.cancel();
}

void ApiWrap::resolveWebPages() {
	auto ids = QVector<MTPInputMessage>(); // temp_req_id = -1
	using IndexAndMessageIds = QPair<int32, QVector<MTPInputMessage>>;
	using MessageIdsByChannel = QMap<ChannelData*, IndexAndMessageIds>;
	MessageIdsByChannel idsByChannel; // temp_req_id = -index - 2

	ids.reserve(_webPagesPending.size());
	int32 t = unixtime(), m = INT_MAX;
	for (auto i = _webPagesPending.begin(); i != _webPagesPending.cend(); ++i) {
		if (i.value() > 0) continue;
		if (i.key()->pendingTill <= t) {
			const auto item = _session->data().findWebPageItem(i.key());
			if (item) {
				if (item->channelId() == NoChannel) {
					ids.push_back(MTP_inputMessageID(MTP_int(item->id)));
					i.value() = -1;
				} else {
					auto channel = item->history()->peer->asChannel();
					auto channelMap = idsByChannel.find(channel);
					if (channelMap == idsByChannel.cend()) {
						channelMap = idsByChannel.insert(
							channel,
							IndexAndMessageIds(
								idsByChannel.size(),
								QVector<MTPInputMessage>(
									1,
									MTP_inputMessageID(MTP_int(item->id)))));
					} else {
						channelMap.value().second.push_back(
							MTP_inputMessageID(MTP_int(item->id)));
					}
					i.value() = -channelMap.value().first - 2;
				}
			}
		} else {
			m = qMin(m, i.key()->pendingTill - t);
		}
	}

	auto requestId = mtpRequestId(0);
	if (!ids.isEmpty()) {
		requestId = request(MTPmessages_GetMessages(
			MTP_vector<MTPInputMessage>(ids)
		)).done([=](const MTPmessages_Messages &result, mtpRequestId requestId) {
			gotWebPages(nullptr, result, requestId);
		}).afterDelay(kSmallDelayMs).send();
	}
	QVector<mtpRequestId> reqsByIndex(idsByChannel.size(), 0);
	for (auto i = idsByChannel.cbegin(), e = idsByChannel.cend(); i != e; ++i) {
		reqsByIndex[i.value().first] = request(MTPchannels_GetMessages(
			i.key()->inputChannel,
			MTP_vector<MTPInputMessage>(i.value().second)
		)).done([=, channel = i.key()](const MTPmessages_Messages &result, mtpRequestId requestId) {
			gotWebPages(channel, result, requestId);
		}).afterDelay(kSmallDelayMs).send();
	}
	if (requestId || !reqsByIndex.isEmpty()) {
		for (auto &pendingRequestId : _webPagesPending) {
			if (pendingRequestId > 0) continue;
			if (pendingRequestId < 0) {
				if (pendingRequestId == -1) {
					pendingRequestId = requestId;
				} else {
					pendingRequestId = reqsByIndex[-pendingRequestId - 2];
				}
			}
		}
	}

	if (m < INT_MAX) {
		_webPagesTimer.callOnce(m * 1000);
	}
}

void ApiWrap::requestParticipantsCountDelayed(
		not_null<ChannelData*> channel) {
	_participantsCountRequestTimer.call(
		kReloadChannelMembersTimeout,
		[=] { channel->updateFullForced(); });
}

void ApiWrap::requestChannelRangeDifference(not_null<History*> history) {
	Expects(history->isChannel());

	const auto channel = history->peer->asChannel();
	if (const auto requestId = _rangeDifferenceRequests.take(channel)) {
		request(*requestId).cancel();
	}
	const auto range = history->rangeForDifferenceRequest();
	if (!(range.from < range.till) || !channel->pts()) {
		return;
	}

	MTP_LOG(0, ("getChannelDifference { good - "
		"after channelDifferenceTooLong was received, "
		"validating history part }%1").arg(cTestMode() ? " TESTMODE" : ""));
	channelRangeDifferenceSend(channel, range, channel->pts());
}

void ApiWrap::channelRangeDifferenceSend(
		not_null<ChannelData*> channel,
		MsgRange range,
		int32 pts) {
	Expects(range.from < range.till);

	const auto limit = range.till - range.from;
	const auto filter = MTP_channelMessagesFilter(
		MTP_flags(0),
		MTP_vector<MTPMessageRange>(1, MTP_messageRange(
			MTP_int(range.from),
			MTP_int(range.till - 1))));
	const auto requestId = request(MTPupdates_GetChannelDifference(
		MTP_flags(MTPupdates_GetChannelDifference::Flag::f_force),
		channel->inputChannel,
		filter,
		MTP_int(pts),
		MTP_int(limit)
	)).done([=](const MTPupdates_ChannelDifference &result) {
		_rangeDifferenceRequests.remove(channel);
		channelRangeDifferenceDone(channel, range, result);
	}).fail([=](const RPCError &error) {
		_rangeDifferenceRequests.remove(channel);
	}).send();
	_rangeDifferenceRequests.emplace(channel, requestId);
}

void ApiWrap::channelRangeDifferenceDone(
		not_null<ChannelData*> channel,
		MsgRange range,
		const MTPupdates_ChannelDifference &result) {
	auto nextRequestPts = int32(0);
	auto isFinal = true;

	switch (result.type()) {
	case mtpc_updates_channelDifferenceEmpty: {
		const auto &d = result.c_updates_channelDifferenceEmpty();
		nextRequestPts = d.vpts.v;
		isFinal = d.is_final();
	} break;

	case mtpc_updates_channelDifferenceTooLong: {
		const auto &d = result.c_updates_channelDifferenceTooLong();

		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);

		nextRequestPts = d.vpts.v;
		isFinal = d.is_final();
	} break;

	case mtpc_updates_channelDifference: {
		const auto &d = result.c_updates_channelDifference();

		App::main()->feedChannelDifference(d);

		nextRequestPts = d.vpts.v;
		isFinal = d.is_final();
	} break;
	}

	if (!isFinal) {
		MTP_LOG(0, ("getChannelDifference { "
			"good - after not final channelDifference was received, "
			"validating history part }%1"
			).arg(cTestMode() ? " TESTMODE" : ""));
		channelRangeDifferenceSend(channel, range, nextRequestPts);
	}
}

void ApiWrap::gotWebPages(ChannelData *channel, const MTPmessages_Messages &msgs, mtpRequestId req) {
	const QVector<MTPMessage> *v = 0;
	switch (msgs.type()) {
	case mtpc_messages_messages: {
		auto &d = msgs.c_messages_messages();
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		v = &d.vmessages.v;
	} break;

	case mtpc_messages_messagesSlice: {
		auto &d = msgs.c_messages_messagesSlice();
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		v = &d.vmessages.v;
	} break;

	case mtpc_messages_channelMessages: {
		auto &d = msgs.c_messages_channelMessages();
		if (channel) {
			channel->ptsReceived(d.vpts.v);
		} else {
			LOG(("API Error: received messages.channelMessages when no channel was passed! (ApiWrap::gotWebPages)"));
		}
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		v = &d.vmessages.v;
	} break;

	case mtpc_messages_messagesNotModified: {
		LOG(("API Error: received messages.messagesNotModified! (ApiWrap::gotWebPages)"));
	} break;
	}

	if (!v) return;

	auto indices = base::flat_map<uint64, int>(); // copied from feedMsgs
	for (auto i = 0, l = v->size(); i != l; ++i) {
		const auto msgId = idFromMessage(v->at(i));
		indices.emplace((uint64(uint32(msgId)) << 32) | uint64(i), i);
	}

	for (const auto [position, index] : indices) {
		const auto item = App::histories().addNewMessage(
			v->at(index),
			NewMessageExisting);
		if (item) {
			_session->data().requestItemResize(item);
		}
	}

	for (auto i = _webPagesPending.begin(); i != _webPagesPending.cend();) {
		if (i.value() == req) {
			if (i.key()->pendingTill > 0) {
				i.key()->pendingTill = -1;
				_session->data().notifyWebPageUpdateDelayed(i.key());
			}
			i = _webPagesPending.erase(i);
		} else {
			++i;
		}
	}
	_session->data().sendWebPageGameNotifications();
}

void ApiWrap::stickersSaveOrder() {
	if (_stickersOrder.size() > 1) {
		QVector<MTPlong> mtpOrder;
		mtpOrder.reserve(_stickersOrder.size());
		for_const (auto setId, _stickersOrder) {
			mtpOrder.push_back(MTP_long(setId));
		}

		_stickersReorderRequestId = request(MTPmessages_ReorderStickerSets(MTP_flags(0), MTP_vector<MTPlong>(mtpOrder))).done([this](const MTPBool &result) {
			_stickersReorderRequestId = 0;
		}).fail([this](const RPCError &error) {
			_stickersReorderRequestId = 0;
			_session->data().setLastStickersUpdate(0);
			updateStickers();
		}).send();
	}
}

void ApiWrap::updateStickers() {
	auto now = getms(true);
	requestStickers(now);
	requestRecentStickers(now);
	requestFavedStickers(now);
	requestFeaturedStickers(now);
	requestSavedGifs(now);
}

void ApiWrap::requestRecentStickersForce() {
	requestRecentStickersWithHash(0);
}

void ApiWrap::setGroupStickerSet(not_null<ChannelData*> megagroup, const MTPInputStickerSet &set) {
	Expects(megagroup->mgInfo != nullptr);

	megagroup->mgInfo->stickerSet = set;
	request(MTPchannels_SetStickers(megagroup->inputChannel, set)).send();
	_session->data().notifyStickersUpdated();
}

std::vector<not_null<DocumentData*>> *ApiWrap::stickersByEmoji(
		not_null<EmojiPtr> emoji) {
	const auto it = _stickersByEmoji.find(emoji);
	const auto sendRequest = [&] {
		if (it == _stickersByEmoji.end()) {
			return true;
		}
		const auto received = it->second.received;
		const auto now = getms(true);
		return (received > 0)
			&& (received + kStickersByEmojiInvalidateTimeout) <= now;
	}();
	if (sendRequest) {
		const auto hash = (it != _stickersByEmoji.end())
			? it->second.hash
			: int32(0);
		request(MTPmessages_GetStickers(
			MTP_string(emoji->text()),
			MTP_int(hash)
		)).done([=](const MTPmessages_Stickers &result) {
			if (result.type() == mtpc_messages_stickersNotModified) {
				return;
			}
			Assert(result.type() == mtpc_messages_stickers);
			const auto &data = result.c_messages_stickers();
			auto &entry = _stickersByEmoji[emoji];
			entry.list.clear();
			entry.list.reserve(data.vstickers.v.size());
			entry.hash = data.vhash.v;
			entry.received = getms(true);
			_session->data().notifyStickersUpdated();
		}).send();
	}
	if (it == _stickersByEmoji.end()) {
		_stickersByEmoji.emplace(emoji, StickersByEmoji());
	} else if (it->second.received > 0) {
		return &it->second.list;
	}
	return nullptr;
}

void ApiWrap::requestStickers(TimeId now) {
	if (!_session->data().stickersUpdateNeeded(now)
		|| _stickersUpdateRequest) {
		return;
	}
	auto onDone = [this](const MTPmessages_AllStickers &result) {
		_session->data().setLastStickersUpdate(getms(true));
		_stickersUpdateRequest = 0;

		switch (result.type()) {
		case mtpc_messages_allStickersNotModified: return;
		case mtpc_messages_allStickers: {
			auto &d = result.c_messages_allStickers();
			Stickers::SetsReceived(d.vsets.v, d.vhash.v);
		} return;
		default: Unexpected("Type in ApiWrap::stickersDone()");
		}
	};
	_stickersUpdateRequest = request(MTPmessages_GetAllStickers(
		MTP_int(Local::countStickersHash(true))
	)).done(onDone).fail([=](const RPCError &error) {
		LOG(("App Fail: Failed to get stickers!"));
		onDone(MTP_messages_allStickersNotModified());
	}).send();
}

void ApiWrap::requestRecentStickers(TimeId now) {
	if (!_session->data().recentStickersUpdateNeeded(now)) {
		return;
	}
	requestRecentStickersWithHash(Local::countRecentStickersHash());
}

void ApiWrap::requestRecentStickersWithHash(int32 hash) {
	if (_recentStickersUpdateRequest) {
		return;
	}
	_recentStickersUpdateRequest = request(MTPmessages_GetRecentStickers(
		MTP_flags(0),
		MTP_int(hash)
	)).done([=](const MTPmessages_RecentStickers &result) {
		_session->data().setLastRecentStickersUpdate(getms(true));
		_recentStickersUpdateRequest = 0;

		switch (result.type()) {
		case mtpc_messages_recentStickersNotModified: return;
		case mtpc_messages_recentStickers: {
			auto &d = result.c_messages_recentStickers();
			Stickers::SpecialSetReceived(
				Stickers::CloudRecentSetId,
				lang(lng_recent_stickers),
				d.vstickers.v,
				d.vhash.v,
				d.vpacks.v,
				d.vdates.v);
		} return;
		default: Unexpected("Type in ApiWrap::recentStickersDone()");
		}
	}).fail([=](const RPCError &error) {
		_session->data().setLastRecentStickersUpdate(getms(true));
		_recentStickersUpdateRequest = 0;

		LOG(("App Fail: Failed to get recent stickers!"));
	}).send();
}

void ApiWrap::requestFavedStickers(TimeId now) {
	if (!_session->data().favedStickersUpdateNeeded(now)
		|| _favedStickersUpdateRequest) {
		return;
	}
	_favedStickersUpdateRequest = request(MTPmessages_GetFavedStickers(
		MTP_int(Local::countFavedStickersHash())
	)).done([=](const MTPmessages_FavedStickers &result) {
		_session->data().setLastFavedStickersUpdate(getms(true));
		_favedStickersUpdateRequest = 0;

		switch (result.type()) {
		case mtpc_messages_favedStickersNotModified: return;
		case mtpc_messages_favedStickers: {
			auto &d = result.c_messages_favedStickers();
			Stickers::SpecialSetReceived(
				Stickers::FavedSetId,
				Lang::Hard::FavedSetTitle(),
				d.vstickers.v,
				d.vhash.v,
				d.vpacks.v);
		} return;
		default: Unexpected("Type in ApiWrap::favedStickersDone()");
		}
	}).fail([=](const RPCError &error) {
		_session->data().setLastFavedStickersUpdate(getms(true));
		_favedStickersUpdateRequest = 0;

		LOG(("App Fail: Failed to get faved stickers!"));
	}).send();
}

void ApiWrap::requestFeaturedStickers(TimeId now) {
	if (!_session->data().featuredStickersUpdateNeeded(now)
		|| _featuredStickersUpdateRequest) {
		return;
	}
	_featuredStickersUpdateRequest = request(MTPmessages_GetFeaturedStickers(
		MTP_int(Local::countFeaturedStickersHash())
	)).done([=](const MTPmessages_FeaturedStickers &result) {
		_session->data().setLastFeaturedStickersUpdate(getms(true));
		_featuredStickersUpdateRequest = 0;

		switch (result.type()) {
		case mtpc_messages_featuredStickersNotModified: return;
		case mtpc_messages_featuredStickers: {
			auto &d = result.c_messages_featuredStickers();
			Stickers::FeaturedSetsReceived(d.vsets.v, d.vunread.v, d.vhash.v);
		} return;
		default: Unexpected("Type in ApiWrap::featuredStickersDone()");
		}
	}).fail([=](const RPCError &error) {
		_session->data().setLastFeaturedStickersUpdate(getms(true));
		_featuredStickersUpdateRequest = 0;

		LOG(("App Fail: Failed to get featured stickers!"));
	}).send();
}

void ApiWrap::requestSavedGifs(TimeId now) {
	if (!_session->data().savedGifsUpdateNeeded(now)
		|| _savedGifsUpdateRequest) {
		return;
	}
	_savedGifsUpdateRequest = request(MTPmessages_GetSavedGifs(
		MTP_int(Local::countSavedGifsHash())
	)).done([=](const MTPmessages_SavedGifs &result) {
		_session->data().setLastSavedGifsUpdate(getms(true));
		_savedGifsUpdateRequest = 0;

		switch (result.type()) {
		case mtpc_messages_savedGifsNotModified: return;
		case mtpc_messages_savedGifs: {
			auto &d = result.c_messages_savedGifs();
			Stickers::GifsReceived(d.vgifs.v, d.vhash.v);
		} return;
		default: Unexpected("Type in ApiWrap::savedGifsDone()");
		}
	}).fail([=](const RPCError &error) {
		_session->data().setLastSavedGifsUpdate(getms(true));
		_savedGifsUpdateRequest = 0;

		LOG(("App Fail: Failed to get saved gifs!"));
	}).send();
}

void ApiWrap::readFeaturedSetDelayed(uint64 setId) {
	if (!_featuredSetsRead.contains(setId)) {
		_featuredSetsRead.insert(setId);
		_featuredSetsReadTimer.callOnce(kReadFeaturedSetsTimeout);
	}
}

void ApiWrap::readFeaturedSets() {
	auto &sets = _session->data().stickerSetsRef();
	auto count = _session->data().featuredStickerSetsUnreadCount();
	QVector<MTPlong> wrappedIds;
	wrappedIds.reserve(_featuredSetsRead.size());
	for (auto setId : _featuredSetsRead) {
		auto it = sets.find(setId);
		if (it != sets.cend()) {
			it->flags &= ~MTPDstickerSet_ClientFlag::f_unread;
			wrappedIds.append(MTP_long(setId));
			if (count) {
				--count;
			}
		}
	}
	_featuredSetsRead.clear();

	if (!wrappedIds.empty()) {
		auto requestData = MTPmessages_ReadFeaturedStickers(
			MTP_vector<MTPlong>(wrappedIds));
		request(std::move(requestData)).done([=](const MTPBool &result) {
			Local::writeFeaturedStickers();
			_session->data().notifyStickersUpdated();
		}).send();

		_session->data().setFeaturedStickerSetsUnreadCount(count);
	}
}

void ApiWrap::parseChannelParticipants(
		not_null<ChannelData*> channel,
		const MTPchannels_ChannelParticipants &result,
		Fn<void(
			int availableCount,
			const QVector<MTPChannelParticipant> &list)> callbackList,
		Fn<void()> callbackNotModified) {
	TLHelp::VisitChannelParticipants(result, base::overload([&](
			const MTPDchannels_channelParticipants &data) {
		App::feedUsers(data.vusers);
		if (channel->mgInfo) {
			refreshChannelAdmins(channel, data.vparticipants.v);
		}
		if (callbackList) {
			callbackList(data.vcount.v, data.vparticipants.v);
		}
	}, [&](mtpTypeId) {
		if (callbackNotModified) {
			callbackNotModified();
		} else {
			LOG(("API Error: channels.channelParticipantsNotModified received!"));
		}
	}));
}

void ApiWrap::refreshChannelAdmins(
		not_null<ChannelData*> channel,
		const QVector<MTPChannelParticipant> &participants) {
	Data::ChannelAdminChanges changes(channel);
	for (auto &p : participants) {
		const auto userId = TLHelp::ReadChannelParticipantUserId(p);
		const auto isAdmin = (p.type() == mtpc_channelParticipantAdmin)
			|| (p.type() == mtpc_channelParticipantCreator);
		changes.feed(userId, isAdmin);
	}
}

void ApiWrap::parseRecentChannelParticipants(
		not_null<ChannelData*> channel,
		const MTPchannels_ChannelParticipants &result,
		Fn<void(
			int availableCount,
			const QVector<MTPChannelParticipant> &list)> callbackList,
		Fn<void()> callbackNotModified) {
	parseChannelParticipants(channel, result, [&](
			int availableCount,
			const QVector<MTPChannelParticipant> &list) {
		auto applyLast = channel->isMegagroup()
			&& (channel->mgInfo->lastParticipants.size() <= list.size());
		if (applyLast) {
			applyLastParticipantsList(
				channel,
				availableCount,
				list);
		}
		callbackList(availableCount, list);
	}, std::move(callbackNotModified));
}

void ApiWrap::applyUpdatesNoPtsCheck(const MTPUpdates &updates) {
	switch (updates.type()) {
	case mtpc_updateShortMessage: {
		auto &d = updates.c_updateShortMessage();
		auto flags = mtpCastFlags(d.vflags.v) | MTPDmessage::Flag::f_from_id;
		const auto peerUserId = d.is_out()
			? d.vuser_id
			: MTP_int(_session->userId());
		App::histories().addNewMessage(
			MTP_message(
				MTP_flags(flags),
				d.vid,
				d.is_out() ? MTP_int(_session->userId()) : d.vuser_id,
				MTP_peerUser(peerUserId),
				d.vfwd_from,
				d.vvia_bot_id,
				d.vreply_to_msg_id,
				d.vdate,
				d.vmessage,
				MTP_messageMediaEmpty(),
				MTPnullMarkup,
				d.has_entities() ? d.ventities : MTPnullEntities,
				MTPint(),
				MTPint(),
				MTPstring(),
				MTPlong()),
			NewMessageUnread);
	} break;

	case mtpc_updateShortChatMessage: {
		auto &d = updates.c_updateShortChatMessage();
		auto flags = mtpCastFlags(d.vflags.v) | MTPDmessage::Flag::f_from_id;
		App::histories().addNewMessage(
			MTP_message(
				MTP_flags(flags),
				d.vid,
				d.vfrom_id,
				MTP_peerChat(d.vchat_id),
				d.vfwd_from,
				d.vvia_bot_id,
				d.vreply_to_msg_id,
				d.vdate,
				d.vmessage,
				MTP_messageMediaEmpty(),
				MTPnullMarkup,
				d.has_entities() ? d.ventities : MTPnullEntities,
				MTPint(),
				MTPint(),
				MTPstring(),
				MTPlong()),
			NewMessageUnread);
	} break;

	case mtpc_updateShortSentMessage: {
		auto &d = updates.c_updateShortSentMessage();
		Q_UNUSED(d); // Sent message data was applied anyway.
	} break;

	default: Unexpected("Type in applyUpdatesNoPtsCheck()");
	}
}

void ApiWrap::applyUpdateNoPtsCheck(const MTPUpdate &update) {
	switch (update.type()) {
	case mtpc_updateNewMessage: {
		auto &d = update.c_updateNewMessage();
		auto needToAdd = true;
		if (d.vmessage.type() == mtpc_message) { // index forwarded messages to links _overview
			if (App::checkEntitiesAndViewsUpdate(d.vmessage.c_message())) { // already in blocks
				LOG(("Skipping message, because it is already in blocks!"));
				needToAdd = false;
			}
		}
		if (needToAdd) {
			App::histories().addNewMessage(d.vmessage, NewMessageUnread);
		}
	} break;

	case mtpc_updateReadMessagesContents: {
		auto &d = update.c_updateReadMessagesContents();
		auto possiblyReadMentions = base::flat_set<MsgId>();
		for (const auto &msgId : d.vmessages.v) {
			if (auto item = App::histItemById(NoChannel, msgId.v)) {
				if (item->isMediaUnread()) {
					item->markMediaRead();
					_session->data().requestItemRepaint(item);

					if (item->out() && item->history()->peer->isUser()) {
						auto when = App::main()->requestingDifference() ? 0 : unixtime();
						item->history()->peer->asUser()->madeAction(when);
					}
				}
			} else {
				// Perhaps it was an unread mention!
				possiblyReadMentions.insert(msgId.v);
			}
		}
		checkForUnreadMentions(possiblyReadMentions);
	} break;

	case mtpc_updateReadHistoryInbox: {
		auto &d = update.c_updateReadHistoryInbox();
		App::feedInboxRead(peerFromMTP(d.vpeer), d.vmax_id.v);
	} break;

	case mtpc_updateReadHistoryOutbox: {
		auto &d = update.c_updateReadHistoryOutbox();
		auto peerId = peerFromMTP(d.vpeer);
		auto when = App::main()->requestingDifference() ? 0 : unixtime();
		App::feedOutboxRead(peerId, d.vmax_id.v, when);
	} break;

	case mtpc_updateWebPage: {
		auto &d = update.c_updateWebPage();
		Q_UNUSED(d); // Web page was updated anyway.
	} break;

	case mtpc_updateDeleteMessages: {
		auto &d = update.c_updateDeleteMessages();
		App::feedWereDeleted(NoChannel, d.vmessages.v);
	} break;

	case mtpc_updateNewChannelMessage: {
		auto &d = update.c_updateNewChannelMessage();
		auto needToAdd = true;
		if (d.vmessage.type() == mtpc_message) { // index forwarded messages to links _overview
			if (App::checkEntitiesAndViewsUpdate(d.vmessage.c_message())) { // already in blocks
				LOG(("Skipping message, because it is already in blocks!"));
				needToAdd = false;
			}
		}
		if (needToAdd) {
			App::histories().addNewMessage(d.vmessage, NewMessageUnread);
		}
	} break;

	case mtpc_updateEditChannelMessage: {
		auto &d = update.c_updateEditChannelMessage();
		App::updateEditedMessage(d.vmessage);
	} break;

	case mtpc_updateEditMessage: {
		auto &d = update.c_updateEditMessage();
		App::updateEditedMessage(d.vmessage);
	} break;

	case mtpc_updateChannelWebPage: {
		auto &d = update.c_updateChannelWebPage();
		Q_UNUSED(d); // Web page was updated anyway.
	} break;

	case mtpc_updateDeleteChannelMessages: {
		auto &d = update.c_updateDeleteChannelMessages();
		App::feedWereDeleted(d.vchannel_id.v, d.vmessages.v);
	} break;

	default: Unexpected("Type in applyUpdateNoPtsCheck()");
	}
}

void ApiWrap::jumpToDate(Dialogs::Key chat, const QDate &date) {
	if (const auto peer = chat.peer()) {
		jumpToHistoryDate(peer, date);
	} else if (const auto feed = chat.feed()) {
		jumpToFeedDate(feed, date);
	}
}

template <typename Callback>
void ApiWrap::requestMessageAfterDate(
		not_null<PeerData*> peer,
		const QDate &date,
		Callback &&callback) {
	// API returns a message with date <= offset_date.
	// So we request a message with offset_date = desired_date - 1 and add_offset = -1.
	// This should give us the first message with date >= desired_date.
	auto offsetId = 0;
	auto offsetDate = static_cast<int>(QDateTime(date).toTime_t()) - 1;
	auto addOffset = -1;
	auto limit = 1;
	auto maxId = 0;
	auto minId = 0;
	auto historyHash = 0;
	request(MTPmessages_GetHistory(
		peer->input,
		MTP_int(offsetId),
		MTP_int(offsetDate),
		MTP_int(addOffset),
		MTP_int(limit),
		MTP_int(maxId),
		MTP_int(minId),
		MTP_int(historyHash)
	)).done([
		peer,
		offsetDate,
		callback = std::forward<Callback>(callback)
	](const MTPmessages_Messages &result) {
		auto getMessagesList = [&result, peer]() -> const QVector<MTPMessage>* {
			auto handleMessages = [](auto &messages) {
				App::feedUsers(messages.vusers);
				App::feedChats(messages.vchats);
				return &messages.vmessages.v;
			};
			switch (result.type()) {
			case mtpc_messages_messages:
				return handleMessages(result.c_messages_messages());
			case mtpc_messages_messagesSlice:
				return handleMessages(result.c_messages_messagesSlice());
			case mtpc_messages_channelMessages: {
				auto &messages = result.c_messages_channelMessages();
				if (peer && peer->isChannel()) {
					peer->asChannel()->ptsReceived(messages.vpts.v);
				} else {
					LOG(("API Error: received messages.channelMessages when no channel was passed! (ApiWrap::jumpToDate)"));
				}
				return handleMessages(messages);
			} break;
			case mtpc_messages_messagesNotModified: {
				LOG(("API Error: received messages.messagesNotModified! (ApiWrap::jumpToDate)"));
			} break;
			}
			return nullptr;
		};

		if (auto list = getMessagesList()) {
			App::feedMsgs(*list, NewMessageExisting);
			for (auto &message : *list) {
				if (dateFromMessage(message) >= offsetDate) {
					callback(idFromMessage(message));
					return;
				}
			}
		}
		callback(ShowAtUnreadMsgId);
	}).send();
}

void ApiWrap::jumpToHistoryDate(not_null<PeerData*> peer, const QDate &date) {
	if (const auto channel = peer->migrateTo()) {
		jumpToHistoryDate(channel, date);
		return;
	}
	const auto jumpToDateInPeer = [=] {
		requestMessageAfterDate(peer, date, [=](MsgId resultId) {
			Ui::showPeerHistory(peer, resultId);
		});
	};
	if (const auto chat = peer->migrateFrom()) {
		requestMessageAfterDate(chat, date, [=](MsgId resultId) {
			if (resultId) {
				Ui::showPeerHistory(chat, resultId);
			} else {
				jumpToDateInPeer();
			}
		});
	} else {
		jumpToDateInPeer();
	}
}

template <typename Callback>
void ApiWrap::requestMessageAfterDate(
		not_null<Data::Feed*> feed,
		const QDate &date,
		Callback &&callback) {
	const auto offsetId = 0;
	const auto offsetDate = static_cast<TimeId>(QDateTime(date).toTime_t());
	const auto addOffset = -2;
	const auto limit = 1;
	const auto hash = 0;
	//request(MTPchannels_GetFeed( // #feed
	//	MTP_flags(MTPchannels_GetFeed::Flag::f_offset_position),
	//	MTP_int(feed->id()),
	//	MTP_feedPosition(
	//		MTP_int(offsetDate),
	//		MTP_peerUser(MTP_int(_session->userId())),
	//		MTP_int(0)),
	//	MTP_int(addOffset),
	//	MTP_int(limit),
	//	MTPfeedPosition(), // max_id
	//	MTPfeedPosition(), // min_id
	//	MTP_int(hash)
	//)).done([
	//	=,
	//	callback = std::forward<Callback>(callback)
	//](const MTPmessages_FeedMessages &result) {
	//	if (result.type() == mtpc_messages_feedMessagesNotModified) {
	//		LOG(("API Error: "
	//			"Unexpected messages.feedMessagesNotModified."));
	//		callback(Data::UnreadMessagePosition);
	//		return;
	//	}
	//	Assert(result.type() == mtpc_messages_feedMessages);
	//	const auto &data = result.c_messages_feedMessages();
	//	const auto &messages = data.vmessages.v;
	//	const auto type = NewMessageExisting;
	//	App::feedUsers(data.vusers);
	//	App::feedChats(data.vchats);
	//	for (const auto &msg : messages) {
	//		if (const auto item = App::histories().addNewMessage(msg, type)) {
	//			if (item->date() >= offsetDate || true) {
	//				callback(item->position());
	//				return;
	//			}
	//		}
	//	}
	//	callback(Data::UnreadMessagePosition);
	//}).send();
}

void ApiWrap::jumpToFeedDate(not_null<Data::Feed*> feed, const QDate &date) {
	requestMessageAfterDate(feed, date, [=](Data::MessagePosition result) {
		Ui::hideLayer();
		App::wnd()->controller()->showSection(
			HistoryFeed::Memento(feed, result));
	});
}

void ApiWrap::preloadEnoughUnreadMentions(not_null<History*> history) {
	auto fullCount = history->getUnreadMentionsCount();
	auto loadedCount = history->getUnreadMentionsLoadedCount();
	auto allLoaded = (fullCount >= 0) ? (loadedCount >= fullCount) : false;
	if (fullCount < 0 || loadedCount >= kUnreadMentionsPreloadIfLess || allLoaded) {
		return;
	}
	if (_unreadMentionsRequests.contains(history)) {
		return;
	}
	auto offsetId = loadedCount ? history->getMaxLoadedUnreadMention() : 1;
	auto limit = loadedCount ? kUnreadMentionsNextRequestLimit : kUnreadMentionsFirstRequestLimit;
	auto addOffset = loadedCount ? -(limit + 1) : -limit;
	auto maxId = 0;
	auto minId = 0;
	auto requestId = request(MTPmessages_GetUnreadMentions(history->peer->input, MTP_int(offsetId), MTP_int(addOffset), MTP_int(limit), MTP_int(maxId), MTP_int(minId))).done([this, history](const MTPmessages_Messages &result) {
		_unreadMentionsRequests.remove(history);
		history->addUnreadMentionsSlice(result);
	}).fail([this, history](const RPCError &error) {
		_unreadMentionsRequests.remove(history);
	}).send();
	_unreadMentionsRequests.emplace(history, requestId);
}

void ApiWrap::checkForUnreadMentions(
		const base::flat_set<MsgId> &possiblyReadMentions,
		ChannelData *channel) {
	for (auto msgId : possiblyReadMentions) {
		requestMessageData(channel, msgId, [](ChannelData *channel, MsgId msgId) {
			if (auto item = App::histItemById(channel, msgId)) {
				if (item->mentionsMe()) {
					item->markMediaRead();
				}
			}
		});
	}
}

void ApiWrap::cancelEditChatAdmins(not_null<ChatData*> chat) {
	_chatAdminsEnabledRequests.take(
		chat
	) | requestCanceller();

	_chatAdminsSaveRequests.take(
		chat
	) | [&](auto &&requests) {
		ranges::for_each(std::move(requests), requestCanceller());
	};

	_chatAdminsToSave.remove(chat);
}

void ApiWrap::editChatAdmins(
		not_null<ChatData*> chat,
		bool adminsEnabled,
		base::flat_set<not_null<UserData*>> &&admins) {
	cancelEditChatAdmins(chat);
	if (adminsEnabled) {
		_chatAdminsToSave.emplace(chat, std::move(admins));
	}

	auto requestId = request(MTPmessages_ToggleChatAdmins(chat->inputChat, MTP_bool(adminsEnabled))).done([this, chat](const MTPUpdates &updates) {
		_chatAdminsEnabledRequests.remove(chat);
		applyUpdates(updates);
		saveChatAdmins(chat);
	}).fail([this, chat](const RPCError &error) {
		_chatAdminsEnabledRequests.remove(chat);
		if (error.type() == qstr("CHAT_NOT_MODIFIED")) {
			saveChatAdmins(chat);
		}
	}).send();
	_chatAdminsEnabledRequests.emplace(chat, requestId);
}

void ApiWrap::saveChatAdmins(not_null<ChatData*> chat) {
	if (!_chatAdminsToSave.contains(chat)) {
		return;
	}
	auto requestId = request(MTPmessages_GetFullChat(chat->inputChat)).done([this, chat](const MTPmessages_ChatFull &result) {
		_chatAdminsEnabledRequests.remove(chat);
		processFullPeer(chat, result);
		sendSaveChatAdminsRequests(chat);
	}).fail([this, chat](const RPCError &error) {
		_chatAdminsEnabledRequests.remove(chat);
		_chatAdminsToSave.remove(chat);
	}).send();
	_chatAdminsEnabledRequests.emplace(chat, requestId);
}

void ApiWrap::sendSaveChatAdminsRequests(not_null<ChatData*> chat) {
	auto editOne = [this, chat](not_null<UserData*> user, bool admin) {
		auto requestId = request(MTPmessages_EditChatAdmin(
				chat->inputChat,
				user->inputUser,
				MTP_bool(admin)))
			.done([this, chat, user, admin](
					const MTPBool &result,
					mtpRequestId requestId) {
			_chatAdminsSaveRequests[chat].remove(requestId);
			if (_chatAdminsSaveRequests[chat].empty()) {
				_chatAdminsSaveRequests.remove(chat);
				Notify::peerUpdatedDelayed(chat, Notify::PeerUpdate::Flag::AdminsChanged);
			}
			if (mtpIsTrue(result)) {
				if (admin) {
					if (chat->noParticipantInfo()) {
						requestFullPeer(chat);
					} else {
						chat->admins.insert(user);
					}
				} else {
					chat->admins.remove(user);
				}
			}
		}).fail([this, chat](
				const RPCError &error,
				mtpRequestId requestId) {
			_chatAdminsSaveRequests[chat].remove(requestId);
			if (_chatAdminsSaveRequests[chat].empty()) {
				_chatAdminsSaveRequests.remove(chat);
			}
			chat->invalidateParticipants();
			if (error.type() == qstr("USER_RESTRICTED")) {
				Ui::show(Box<InformBox>(lang(lng_cant_do_this)));
			}
		}).afterDelay(kSmallDelayMs).send();

		_chatAdminsSaveRequests[chat].insert(requestId);
	};
	auto appointOne = [&](auto user) { editOne(user, true); };
	auto removeOne = [&](auto user) { editOne(user, false); };

	auto admins = _chatAdminsToSave.take(chat);
	Assert(!!admins);

	auto toRemove = chat->admins;
	auto toAppoint = std::vector<not_null<UserData*>>();
	if (!admins->empty()) {
		toAppoint.reserve(admins->size());
		for (auto user : *admins) {
			if (!toRemove.remove(user) && user->id != peerFromUser(chat->creator)) {
				toAppoint.push_back(user);
			}
		}
	}
	ranges::for_each(toRemove, removeOne);
	ranges::for_each(toAppoint, appointOne);
	requestSendDelayed();
}

void ApiWrap::requestSharedMediaCount(
		not_null<PeerData*> peer,
		Storage::SharedMediaType type) {
	requestSharedMedia(peer, type, 0, SliceType::Before);
}

void ApiWrap::requestSharedMedia(
		not_null<PeerData*> peer,
		SharedMediaType type,
		MsgId messageId,
		SliceType slice) {
	auto key = std::make_tuple(peer, type, messageId, slice);
	if (_sharedMediaRequests.contains(key)) {
		return;
	}

	auto prepared = Api::PrepareSearchRequest(
		peer,
		type,
		QString(),
		messageId,
		slice);
	if (prepared.vfilter.type() == mtpc_inputMessagesFilterEmpty) {
		return;
	}

	auto requestId = request(
		std::move(prepared)
	).done([this, peer, type, messageId, slice](
			const MTPmessages_Messages &result) {
		auto key = std::make_tuple(peer, type, messageId, slice);
		_sharedMediaRequests.remove(key);
		sharedMediaDone(peer, type, messageId, slice, result);
	}).fail([this, key](const RPCError &error) {
		_sharedMediaRequests.remove(key);
	}).send();
	_sharedMediaRequests.emplace(key, requestId);
}

void ApiWrap::sharedMediaDone(
		not_null<PeerData*> peer,
		SharedMediaType type,
		MsgId messageId,
		SliceType slice,
		const MTPmessages_Messages &result) {
	auto parsed = Api::ParseSearchResult(
		peer,
		type,
		messageId,
		slice,
		result);
	_session->storage().add(Storage::SharedMediaAddSlice(
		peer->id,
		type,
		std::move(parsed.messageIds),
		parsed.noSkipRange,
		parsed.fullCount
	));
}

void ApiWrap::requestUserPhotos(
		not_null<UserData*> user,
		PhotoId afterId) {
	if (_userPhotosRequests.contains(user)) {
		return;
	}

	auto limit = kSharedMediaLimit;

	auto requestId = request(MTPphotos_GetUserPhotos(
		user->inputUser,
		MTP_int(0),
		MTP_long(afterId),
		MTP_int(limit)
	)).done([this, user, afterId](const MTPphotos_Photos &result) {
		_userPhotosRequests.remove(user);
		userPhotosDone(user, afterId, result);
	}).fail([this, user](const RPCError &error) {
		_userPhotosRequests.remove(user);
	}).send();
	_userPhotosRequests.emplace(user, requestId);
}

void ApiWrap::userPhotosDone(
		not_null<UserData*> user,
		PhotoId photoId,
		const MTPphotos_Photos &result) {
	auto fullCount = 0;
	auto &photos = *[&] {
		switch (result.type()) {
		case mtpc_photos_photos: {
			auto &d = result.c_photos_photos();
			App::feedUsers(d.vusers);
			fullCount = d.vphotos.v.size();
			return &d.vphotos.v;
		} break;

		case mtpc_photos_photosSlice: {
			auto &d = result.c_photos_photosSlice();
			App::feedUsers(d.vusers);
			fullCount = d.vcount.v;
			return &d.vphotos.v;
		} break;
		}
		Unexpected("photos.Photos type in userPhotosDone()");
	}();

	auto photoIds = std::vector<PhotoId>();
	photoIds.reserve(photos.size());
	for (auto &photo : photos) {
		if (auto photoData = _session->data().photo(photo)) {
			photoIds.push_back(photoData->id);
		}
	}
	_session->storage().add(Storage::UserPhotosAddSlice(
		user->id,
		std::move(photoIds),
		fullCount
	));
}
// #feed
//void ApiWrap::requestFeedChannels(not_null<Data::Feed*> feed) {
//	if (_feedChannelsGetRequests.contains(feed)) {
//		return;
//	}
//	const auto hash = feed->channelsHash();
//	request(MTPchannels_GetFeedSources(
//		MTP_flags(MTPchannels_GetFeedSources::Flag::f_feed_id),
//		MTP_int(feed->id()),
//		MTP_int(hash)
//	)).done([=](const MTPchannels_FeedSources &result) {
//		_feedChannelsGetRequests.remove(feed);
//
//		switch (result.type()) {
//		case mtpc_channels_feedSourcesNotModified:
//			if (feed->channelsHash() == hash) {
//				feedChannelsDone(feed);
//			} else {
//				requestFeedChannels(feed);
//			}
//			break;
//
//		case mtpc_channels_feedSources: {
//			const auto &data = result.c_channels_feedSources();
//			applyFeedSources(data);
//			if (feed->channelsLoaded()) {
//				feedChannelsDone(feed);
//			} else {
//				LOG(("API Error: feed channels not received for "
//					).arg(feed->id()));
//			}
//		} break;
//
//		default: Unexpected("Type in channels.getFeedSources response.");
//		}
//	}).fail([=](const RPCError &error) {
//		_feedChannelsGetRequests.remove(feed);
//	}).send();
//	_feedChannelsGetRequests.emplace(feed);
//}
//
//void ApiWrap::applyFeedSources(const MTPDchannels_feedSources &data) {
//	// First we set channels without reading them from data.
//	// This allows us to apply them all at once without registering
//	// them one by one.
//	for (const auto &broadcasts : data.vfeeds.v) {
//		if (broadcasts.type() == mtpc_feedBroadcasts) {
//			const auto &list = broadcasts.c_feedBroadcasts();
//			const auto feedId = list.vfeed_id.v;
//			const auto feed = _session->data().feed(feedId);
//			auto channels = std::vector<not_null<ChannelData*>>();
//			for (const auto &channelId : list.vchannels.v) {
//				channels.push_back(App::channel(channelId.v));
//			}
//			feed->setChannels(std::move(channels));
//		}
//	}
//
//	App::feedUsers(data.vusers);
//	App::feedChats(data.vchats);
//
//	if (data.has_newly_joined_feed()) {
//		_session->data().setDefaultFeedId(
//			data.vnewly_joined_feed.v);
//	}
//}
//
//void ApiWrap::setFeedChannels(
//		not_null<Data::Feed*> feed,
//		const std::vector<not_null<ChannelData*>> &channels) {
//	if (const auto already = _feedChannelsSetRequests.take(feed)) {
//		request(*already).cancel();
//	}
//	auto inputs = QVector<MTPInputChannel>();
//	inputs.reserve(channels.size());
//	for (const auto channel : channels) {
//		inputs.push_back(channel->inputChannel);
//	}
//	const auto requestId = request(MTPchannels_SetFeedBroadcasts(
//		MTP_flags(MTPchannels_SetFeedBroadcasts::Flag::f_channels),
//		MTP_int(feed->id()),
//		MTP_vector<MTPInputChannel>(inputs),
//		MTPbool()
//	)).done([=](const MTPUpdates &result) {
//		applyUpdates(result);
//
//		_feedChannelsSetRequests.remove(feed);
//	}).fail([=](const RPCError &error) {
//		_feedChannelsSetRequests.remove(feed);
//	}).send();
//
//}
//
//void ApiWrap::feedChannelsDone(not_null<Data::Feed*> feed) {
//	feed->setChannelsLoaded(true);
//	for (const auto key : base::take(_feedMessagesRequestsPending)) {
//		std::apply(
//			[=](auto&&...args) { requestFeedMessages(args...); },
//			key);
//	}
//}
//
//void ApiWrap::requestFeedMessages(
//		not_null<Data::Feed*> feed,
//		Data::MessagePosition messageId,
//		SliceType slice) {
//	const auto key = std::make_tuple(feed, messageId, slice);
//	if (_feedMessagesRequests.contains(key)
//		|| _feedMessagesRequestsPending.contains(key)) {
//		return;
//	}
//
//	if (!feed->channelsLoaded()) {
//		_feedMessagesRequestsPending.emplace(key);
//		requestFeedChannels(feed);
//		return;
//	}
//
//	// We request messages with overlapping and skip overlapped in response.
//	const auto limit = kFeedMessagesLimit;
//	const auto addOffset = [&] {
//		switch (slice) {
//		case SliceType::Before: return -2;
//		case SliceType::Around: return -limit / 2;
//		case SliceType::After: return 1 - limit;
//		}
//		Unexpected("Direction in PrepareSearchRequest");
//	}();
//	const auto hash = int32(0);
//	const auto flags = (messageId && messageId.fullId.channel)
//		? MTPchannels_GetFeed::Flag::f_offset_position
//		: MTPchannels_GetFeed::Flag::f_offset_to_max_read;
//	const auto requestId = request(MTPchannels_GetFeed(
//		MTP_flags(flags),
//		MTP_int(feed->id()),
//		MTP_feedPosition(
//			MTP_int(messageId.date),
//			MTP_peerChannel(MTP_int(messageId.fullId.channel)),
//			MTP_int(messageId.fullId.msg)),
//		MTP_int(addOffset),
//		MTP_int(limit),
//		MTPFeedPosition(),
//		MTPFeedPosition(),
//		MTP_int(hash)
//	)).done([=](const MTPmessages_FeedMessages &result) {
//		const auto key = std::make_tuple(feed, messageId, slice);
//		_feedMessagesRequests.remove(key);
//		feedMessagesDone(feed, messageId, slice, result);
//	}).fail([=](const RPCError &error) {
//		_feedMessagesRequests.remove(key);
//		if (error.type() == qstr("SOURCES_HASH_INVALID")) {
//			_feedMessagesRequestsPending.emplace(key);
//			requestFeedChannels(feed);
//		}
//	}).send();
//	_feedMessagesRequests.emplace(key);
//}
//
//void ApiWrap::feedMessagesDone(
//		not_null<Data::Feed*> feed,
//		Data::MessagePosition messageId,
//		SliceType slice,
//		const MTPmessages_FeedMessages &result) {
//	if (result.type() == mtpc_messages_feedMessagesNotModified) {
//		LOG(("API Error: Unexpected messages.feedMessagesNotModified."));
//		_session->storage().add(Storage::FeedMessagesAddSlice(
//			feed->id(),
//			std::vector<Data::MessagePosition>(),
//			Data::FullMessagesRange));
//		return;
//	}
//	Assert(result.type() == mtpc_messages_feedMessages);
//	const auto &data = result.c_messages_feedMessages();
//	const auto &messages = data.vmessages.v;
//	const auto type = NewMessageExisting;
//
//	auto ids = std::vector<Data::MessagePosition>();
//	auto noSkipRange = Data::MessagesRange(messageId, messageId);
//	const auto accumulateFrom = [](auto &from, const auto &candidate) {
//		if (!from || from > candidate) {
//			from = candidate;
//		}
//	};
//	const auto accumulateTill = [](auto &till, const auto &candidate) {
//		if (!till || till < candidate) {
//			till = candidate;
//		}
//	};
//	const auto tooLargePosition = [&](const auto &position) {
//		return (slice == SliceType::Before) && !(position < messageId);
//	};
//	const auto tooSmallPosition = [&](const auto &position) {
//		return (slice == SliceType::After) && !(messageId < position);
//	};
//	App::feedUsers(data.vusers);
//	App::feedChats(data.vchats);
//	if (!messages.empty()) {
//		ids.reserve(messages.size());
//		for (const auto &msg : messages) {
//			if (const auto item = App::histories().addNewMessage(msg, type)) {
//				const auto position = item->position();
//				if (tooLargePosition(position)) {
//					accumulateTill(noSkipRange.till, position);
//					continue;
//				} else if (tooSmallPosition(position)) {
//					accumulateFrom(noSkipRange.from, position);
//					continue;
//				}
//				ids.push_back(position);
//				accumulateFrom(noSkipRange.from, position);
//				accumulateTill(noSkipRange.till, position);
//			}
//		}
//		ranges::reverse(ids);
//	}
//	if (data.has_min_position() && !ids.empty()) {
//		accumulateFrom(
//			noSkipRange.from,
//			Data::FeedPositionFromMTP(data.vmin_position));
//	} else if (slice == SliceType::Before) {
//		noSkipRange.from = Data::MinMessagePosition;
//	}
//	if (data.has_max_position() && !ids.empty()) {
//		accumulateTill(
//			noSkipRange.till,
//			Data::FeedPositionFromMTP(data.vmax_position));
//	} else if (slice == SliceType::After) {
//		noSkipRange.till = Data::MaxMessagePosition;
//	}
//
//	const auto unreadPosition = [&] {
//		if (data.has_read_max_position()) {
//			return Data::FeedPositionFromMTP(data.vread_max_position);
//		} else if (!messageId) {
//			const auto result = ids.empty()
//				? noSkipRange.till
//				: ids.back();
//			return Data::MessagePosition(
//				result.date,
//				FullMsgId(result.fullId.channel, result.fullId.msg - 1));
//		}
//		return Data::MessagePosition();
//	}();
//
//	_session->storage().add(Storage::FeedMessagesAddSlice(
//		feed->id(),
//		std::move(ids),
//		noSkipRange));
//
//	if (unreadPosition) {
//		feed->setUnreadPosition(unreadPosition);
//	}
//}
//
//void ApiWrap::saveDefaultFeedId(FeedId id, bool isDefaultFeedId) {
//	if (const auto already = base::take(_saveDefaultFeedIdRequest)) {
//		request(already).cancel();
//	}
//	_saveDefaultFeedIdRequest = request(MTPchannels_SetFeedBroadcasts(
//		MTP_flags(MTPchannels_SetFeedBroadcasts::Flag::f_also_newly_joined),
//		MTP_int(id),
//		MTPVector<MTPInputChannel>(),
//		MTP_bool(isDefaultFeedId)
//	)).send();
//}

void ApiWrap::sendAction(const SendOptions &options) {
	readServerHistory(options.history);
	options.history->getReadyFor(ShowAtTheEndMsgId);
	_sendActions.fire_copy(options);
}

void ApiWrap::forwardMessages(
		HistoryItemsList &&items,
		const SendOptions &options,
		FnMut<void()> &&successCallback) {
	Expects(!items.empty());

	struct SharedCallback {
		int requestsLeft = 0;
		FnMut<void()> callback;
	};
	const auto shared = successCallback
		? std::make_shared<SharedCallback>()
		: std::shared_ptr<SharedCallback>();
	if (successCallback) {
		shared->callback = std::move(successCallback);
	}

	const auto count = int(items.size());
	const auto genClientSideMessage = options.generateLocal && (count < 2);
	const auto history = options.history;
	const auto peer = history->peer;

	readServerHistory(history);

	const auto channelPost = peer->isChannel() && !peer->isMegagroup();
	const auto silentPost = channelPost
		&& _session->data().notifySilentPosts(peer);

	auto flags = MTPDmessage::Flags(0);
	auto sendFlags = MTPmessages_ForwardMessages::Flags(0);
	if (channelPost) {
		flags |= MTPDmessage::Flag::f_views;
		flags |= MTPDmessage::Flag::f_post;
	}
	if (!channelPost) {
		flags |= MTPDmessage::Flag::f_from_id;
	} else if (peer->asChannel()->addsSignature()) {
		flags |= MTPDmessage::Flag::f_post_author;
	}
	if (silentPost) {
		sendFlags |= MTPmessages_ForwardMessages::Flag::f_silent;
	}

	auto forwardFrom = items.front()->history()->peer;
	auto currentGroupId = items.front()->groupId();
	auto ids = QVector<MTPint>();
	auto randomIds = QVector<MTPlong>();

	const auto sendAccumulated = [&] {
		if (shared) {
			++shared->requestsLeft;
		}
		const auto finalFlags = sendFlags
			| (currentGroupId == MessageGroupId()
				? MTPmessages_ForwardMessages::Flag(0)
				: MTPmessages_ForwardMessages::Flag::f_grouped);
		history->sendRequestId = request(MTPmessages_ForwardMessages(
			MTP_flags(finalFlags),
			forwardFrom->input,
			MTP_vector<MTPint>(ids),
			MTP_vector<MTPlong>(randomIds),
			peer->input
		)).done([=, callback = std::move(successCallback)](
				const MTPUpdates &updates) {
			applyUpdates(updates);
			if (shared && !--shared->requestsLeft) {
				shared->callback();
			}
		}).afterRequest(
			history->sendRequestId
		).send();

		ids.resize(0);
		randomIds.resize(0);
	};

	ids.reserve(count);
	randomIds.reserve(count);
	for (const auto item : items) {
		auto randomId = rand_value<uint64>();
		if (genClientSideMessage) {
			if (auto message = item->toHistoryMessage()) {
				const auto newId = FullMsgId(
					peerToChannel(peer->id),
					clientMsgId());
				const auto self = _session->user();
				const auto messageFromId = channelPost
					? UserId(0)
					: peerToUser(self->id);
				const auto messagePostAuthor = channelPost
					? (self->firstName + ' ' + self->lastName)
					: QString();
				history->addNewForwarded(
					newId.msg,
					flags,
					unixtime(),
					messageFromId,
					messagePostAuthor,
					message);
				App::historyRegRandom(randomId, newId);
			}
		}
		const auto newFrom = item->history()->peer;
		const auto newGroupId = item->groupId();
		if (forwardFrom != newFrom
			|| currentGroupId != newGroupId) {
			sendAccumulated();
			forwardFrom = newFrom;
			currentGroupId = newGroupId;
		}
		ids.push_back(MTP_int(item->id));
		randomIds.push_back(MTP_long(randomId));
	}
	sendAccumulated();
	_session->data().sendHistoryChangeNotifications();
}

void ApiWrap::shareContact(
		const QString &phone,
		const QString &firstName,
		const QString &lastName,
		const SendOptions &options) {
	const auto userId = UserId(0);
	sendSharedContact(phone, firstName, lastName, userId, options);
}

void ApiWrap::shareContact(
		not_null<UserData*> user,
		const SendOptions &options) {
	const auto userId = peerToUser(user->id);
	const auto phone = _session->data().findContactPhone(user);
	if (phone.isEmpty()) {
		return;
	}
	sendSharedContact(
		phone,
		user->firstName,
		user->lastName,
		userId,
		options);
}

void ApiWrap::sendSharedContact(
		const QString &phone,
		const QString &firstName,
		const QString &lastName,
		UserId userId,
		const SendOptions &options) {
	sendAction(options);

	const auto history = options.history;
	const auto peer = history->peer;

	const auto newId = FullMsgId(history->channelId(), clientMsgId());
	const auto channelPost = peer->isChannel() && !peer->isMegagroup();

	auto flags = NewMessageFlags(peer) | MTPDmessage::Flag::f_media;
	if (options.replyTo) {
		flags |= MTPDmessage::Flag::f_reply_to_msg_id;
	}
	if (channelPost) {
		flags |= MTPDmessage::Flag::f_views;
		flags |= MTPDmessage::Flag::f_post;
		if (peer->asChannel()->addsSignature()) {
			flags |= MTPDmessage::Flag::f_post_author;
		}
	} else {
		flags |= MTPDmessage::Flag::f_from_id;
	}
	const auto messageFromId = channelPost ? 0 : _session->userId();
	const auto messagePostAuthor = channelPost
		? (_session->user()->firstName + ' ' + _session->user()->lastName)
		: QString();
	const auto item = history->addNewMessage(
		MTP_message(
			MTP_flags(flags),
			MTP_int(newId.msg),
			MTP_int(messageFromId),
			peerToMTP(peer->id),
			MTPnullFwdHeader,
			MTPint(),
			MTP_int(options.replyTo),
			MTP_int(unixtime()),
			MTP_string(""),
			MTP_messageMediaContact(
				MTP_string(phone),
				MTP_string(firstName),
				MTP_string(lastName),
				MTP_int(userId)),
			MTPnullMarkup,
			MTPnullEntities,
			MTP_int(1),
			MTPint(),
			MTP_string(messagePostAuthor),
			MTPlong()),
		NewMessageUnread);

	const auto media = MTP_inputMediaContact(
		MTP_string(phone),
		MTP_string(firstName),
		MTP_string(lastName));
	sendMedia(item, media, _session->data().notifySilentPosts(peer));

	if (const auto main = App::main()) {
		_session->data().sendHistoryChangeNotifications();
		main->historyToDown(history);
		main->dialogsToUp();
	}
}

void ApiWrap::sendVoiceMessage(
		QByteArray result,
		VoiceWaveform waveform,
		int duration,
		const SendOptions &options) {
	const auto caption = TextWithTags();
	const auto to = FileLoadTaskOptions(options);
	_fileLoader->addTask(std::make_unique<FileLoadTask>(
		result,
		duration,
		waveform,
		to,
		caption));
}

void ApiWrap::sendFiles(
		Storage::PreparedList &&list,
		SendMediaType type,
		TextWithTags &&caption,
		std::shared_ptr<SendingAlbum> album,
		const SendOptions &options) {
	if (list.files.size() > 1 && !caption.text.isEmpty()) {
		auto message = MainWidget::MessageToSend(options.history);
		message.textWithTags = std::move(caption);
		message.replyTo = options.replyTo;
		message.clearDraft = false;
		App::main()->sendMessage(message);
		caption = TextWithTags();
	}

	const auto to = FileLoadTaskOptions(options);
	if (album) {
		album->silent = to.silent;
	}
	auto tasks = std::vector<std::unique_ptr<Task>>();
	tasks.reserve(list.files.size());
	for (auto &file : list.files) {
		if (album) {
			switch (file.type) {
			case Storage::PreparedFile::AlbumType::Photo:
				type = SendMediaType::Photo;
				break;
			case Storage::PreparedFile::AlbumType::Video:
				type = SendMediaType::File;
				break;
			default: Unexpected("AlbumType in uploadFilesAfterConfirmation");
			}
		}
		tasks.push_back(std::make_unique<FileLoadTask>(
			file.path,
			file.content,
			std::move(file.information),
			type,
			to,
			caption,
			album));
	}
	if (album) {
		_sendingAlbums.emplace(album->groupId, album);
		album->items.reserve(tasks.size());
		for (const auto &task : tasks) {
			album->items.push_back(SendingAlbum::Item(task->id()));
		}
	}
	_fileLoader->addTasks(std::move(tasks));
}

void ApiWrap::sendFile(
		const QByteArray &fileContent,
		SendMediaType type,
		const SendOptions &options) {
	auto to = FileLoadTaskOptions(options);
	auto caption = TextWithTags();
	_fileLoader->addTask(std::make_unique<FileLoadTask>(
		QString(),
		fileContent,
		nullptr,
		type,
		to,
		caption));
}

void ApiWrap::sendUploadedPhoto(
		FullMsgId localId,
		const MTPInputFile &file,
		bool silent) {
	if (const auto item = App::histItemById(localId)) {
		const auto media = MTP_inputMediaUploadedPhoto(
			MTP_flags(0),
			file,
			MTPVector<MTPInputDocument>(),
			MTP_int(0));
		if (const auto groupId = item->groupId()) {
			uploadAlbumMedia(item, groupId, media);
		} else {
			sendMedia(item, media, silent);
		}
	}
}

void ApiWrap::sendUploadedDocument(
		FullMsgId localId,
		const MTPInputFile &file,
		const base::optional<MTPInputFile> &thumb,
		bool silent) {
	if (const auto item = App::histItemById(localId)) {
		auto media = item->media();
		if (auto document = media ? media->document() : nullptr) {
			const auto groupId = item->groupId();
			const auto flags = MTPDinputMediaUploadedDocument::Flags(0)
				| (thumb
					? MTPDinputMediaUploadedDocument::Flag::f_thumb
					: MTPDinputMediaUploadedDocument::Flag(0))
				| (groupId
					? MTPDinputMediaUploadedDocument::Flag::f_nosound_video
					: MTPDinputMediaUploadedDocument::Flag(0));
			const auto media = MTP_inputMediaUploadedDocument(
				MTP_flags(flags),
				file,
				thumb ? *thumb : MTPInputFile(),
				MTP_string(document->mimeString()),
				ComposeSendingDocumentAttributes(document),
				MTPVector<MTPInputDocument>(),
				MTP_int(0));
			if (groupId) {
				uploadAlbumMedia(item, groupId, media);
			} else {
				sendMedia(item, media, silent);
			}
		}
	}
}

void ApiWrap::cancelLocalItem(not_null<HistoryItem*> item) {
	Expects(!IsServerMsgId(item->id));

	if (const auto groupId = item->groupId()) {
		sendAlbumWithCancelled(item, groupId);
	}
}

void ApiWrap::uploadAlbumMedia(
		not_null<HistoryItem*> item,
		const MessageGroupId &groupId,
		const MTPInputMedia &media) {
	const auto localId = item->fullId();
	const auto failed = [=] {

	};
	request(MTPmessages_UploadMedia(
		item->history()->peer->input,
		media
	)).done([=](const MTPMessageMedia &result) {
		const auto item = App::histItemById(localId);
		if (!item) {
			failed();
			return;
		}
		if (const auto media = item->media()) {
			if (const auto photo = media->photo()) {
				photo->setWaitingForAlbum();
			} else if (const auto document = media->document()) {
				document->setWaitingForAlbum();
			}
		}

		switch (result.type()) {
		case mtpc_messageMediaPhoto: {
			const auto &data = result.c_messageMediaPhoto();
			if (data.vphoto.type() != mtpc_photo) {
				failed();
				return;
			}
			const auto &photo = data.vphoto.c_photo();
			const auto flags = MTPDinputMediaPhoto::Flags(0)
				| (data.has_ttl_seconds()
					? MTPDinputMediaPhoto::Flag::f_ttl_seconds
					: MTPDinputMediaPhoto::Flag(0));
			const auto media = MTP_inputMediaPhoto(
				MTP_flags(flags),
				MTP_inputPhoto(photo.vid, photo.vaccess_hash),
				data.has_ttl_seconds() ? data.vttl_seconds : MTPint());
			sendAlbumWithUploaded(item, groupId, media);
		} break;

		case mtpc_messageMediaDocument: {
			const auto &data = result.c_messageMediaDocument();
			if (data.vdocument.type() != mtpc_document) {
				failed();
				return;
			}
			const auto &document = data.vdocument.c_document();
			const auto flags = MTPDinputMediaDocument::Flags(0)
				| (data.has_ttl_seconds()
					? MTPDinputMediaDocument::Flag::f_ttl_seconds
					: MTPDinputMediaDocument::Flag(0));
			const auto media = MTP_inputMediaDocument(
				MTP_flags(flags),
				MTP_inputDocument(document.vid, document.vaccess_hash),
				data.has_ttl_seconds() ? data.vttl_seconds : MTPint());
			sendAlbumWithUploaded(item, groupId, media);
		} break;
		}
	}).fail([=](const RPCError &error) {
		failed();
	}).send();
}

void ApiWrap::sendMedia(
		not_null<HistoryItem*> item,
		const MTPInputMedia &media,
		bool silent) {
	const auto randomId = rand_value<uint64>();
	App::historyRegRandom(randomId, item->fullId());

	sendMediaWithRandomId(item, media, silent, randomId);
}

void ApiWrap::sendMediaWithRandomId(
		not_null<HistoryItem*> item,
		const MTPInputMedia &media,
		bool silent,
		uint64 randomId) {
	const auto history = item->history();
	const auto replyTo = item->replyToId();

	auto caption = item->originalText();
	TextUtilities::Trim(caption);
	auto sentEntities = TextUtilities::EntitiesToMTP(
		caption.entities,
		TextUtilities::ConvertOption::SkipLocal);

	const auto flags = MTPmessages_SendMedia::Flags(0)
		| (replyTo
			? MTPmessages_SendMedia::Flag::f_reply_to_msg_id
			: MTPmessages_SendMedia::Flag(0))
		| (IsSilentPost(item, silent)
			? MTPmessages_SendMedia::Flag::f_silent
			: MTPmessages_SendMedia::Flag(0))
		| (!sentEntities.v.isEmpty()
			? MTPmessages_SendMedia::Flag::f_entities
			: MTPmessages_SendMedia::Flag(0));

	history->sendRequestId = request(MTPmessages_SendMedia(
		MTP_flags(flags),
		history->peer->input,
		MTP_int(replyTo),
		media,
		MTP_string(caption.text),
		MTP_long(randomId),
		MTPnullMarkup,
		sentEntities
	)).done([=](const MTPUpdates &result) { applyUpdates(result);
	}).fail([=](const RPCError &error) { sendMessageFail(error);
	}).afterRequest(history->sendRequestId
	).send();
}

void ApiWrap::sendAlbumWithUploaded(
		not_null<HistoryItem*> item,
		const MessageGroupId &groupId,
		const MTPInputMedia &media) {
	const auto localId = item->fullId();
	const auto randomId = rand_value<uint64>();
	App::historyRegRandom(randomId, localId);

	const auto albumIt = _sendingAlbums.find(groupId.raw());
	Assert(albumIt != _sendingAlbums.end());
	const auto &album = albumIt->second;

	const auto proj = [](const SendingAlbum::Item &item) {
		return item.msgId;
	};
	const auto itemIt = ranges::find(album->items, localId, proj);
	Assert(itemIt != album->items.end());
	Assert(!itemIt->media);

	auto caption = item->originalText();
	TextUtilities::Trim(caption);
	auto sentEntities = TextUtilities::EntitiesToMTP(
		caption.entities,
		TextUtilities::ConvertOption::SkipLocal);
	const auto flags = !sentEntities.v.isEmpty()
		? MTPDinputSingleMedia::Flag::f_entities
		: MTPDinputSingleMedia::Flag(0);

	itemIt->media = MTP_inputSingleMedia(
		MTP_flags(flags),
		media,
		MTP_long(randomId),
		MTP_string(caption.text),
		sentEntities);

	sendAlbumIfReady(album.get());
}

void ApiWrap::sendAlbumWithCancelled(
		not_null<HistoryItem*> item,
		const MessageGroupId &groupId) {
	const auto localId = item->fullId();
	const auto albumIt = _sendingAlbums.find(groupId.raw());
	if (albumIt == _sendingAlbums.end()) {
		// Sometimes we destroy item being sent already after the album
		// was sent successfully. For example the message could be loaded
		// from server (by messages.getHistory or updateNewMessage) and
		// added to history and after that updateMessageID was received with
		// the same message id, in this case we destroy a detached local
		// item and sendAlbumWithCancelled is called for already sent album.
		return;
	}
	const auto &album = albumIt->second;

	const auto proj = [](const SendingAlbum::Item &item) {
		return item.msgId;
	};
	const auto itemIt = ranges::find(album->items, localId, proj);
	Assert(itemIt != album->items.end());
	album->items.erase(itemIt);

	sendAlbumIfReady(album.get());
}

void ApiWrap::sendAlbumIfReady(not_null<SendingAlbum*> album) {
	const auto groupId = album->groupId;
	if (album->items.empty()) {
		_sendingAlbums.remove(groupId);
		return;
	}
	auto sample = (HistoryItem*)nullptr;
	auto medias = QVector<MTPInputSingleMedia>();
	medias.reserve(album->items.size());
	for (const auto &item : album->items) {
		if (!item.media) {
			return;
		} else if (!sample) {
			sample = App::histItemById(item.msgId);
		}
		medias.push_back(*item.media);
	}
	if (!sample) {
		_sendingAlbums.remove(groupId);
		return;
	} else if (medias.size() < 2) {
		const auto &single = medias.front().c_inputSingleMedia();
		sendMediaWithRandomId(
			sample,
			single.vmedia,
			album->silent,
			single.vrandom_id.v);
		_sendingAlbums.remove(groupId);
		return;
	}
	const auto history = sample->history();
	const auto replyTo = sample->replyToId();
	const auto flags = MTPmessages_SendMultiMedia::Flags(0)
		| (replyTo
			? MTPmessages_SendMultiMedia::Flag::f_reply_to_msg_id
			: MTPmessages_SendMultiMedia::Flag(0))
		| (IsSilentPost(sample, album->silent)
			? MTPmessages_SendMultiMedia::Flag::f_silent
			: MTPmessages_SendMultiMedia::Flag(0));
	history->sendRequestId = request(MTPmessages_SendMultiMedia(
		MTP_flags(flags),
		history->peer->input,
		MTP_int(replyTo),
		MTP_vector<MTPInputSingleMedia>(medias)
	)).done([=](const MTPUpdates &result) {
		_sendingAlbums.remove(groupId);
		applyUpdates(result);
	}).fail([=](const RPCError &error) {
		_sendingAlbums.remove(groupId);
		sendMessageFail(error);
	}).afterRequest(history->sendRequestId
	).send();
}

void ApiWrap::readServerHistory(not_null<History*> history) {
	if (history->unreadCount()) {
		readServerHistoryForce(history);
	}
}

void ApiWrap::readServerHistoryForce(not_null<History*> history) {
	const auto peer = history->peer;
	const auto upTo = history->readInbox();
	if (!upTo) {
		return;
	}

	if (const auto channel = peer->asChannel()) {
		if (!channel->amIn()) {
			return; // no read request for channels that I didn't join
		} else if (const auto migrateFrom = channel->migrateFrom()) {
			if (const auto migrated = App::historyLoaded(migrateFrom)) {
				readServerHistory(migrated);
			}
		}
	}

	if (_readRequests.contains(peer)) {
		const auto i = _readRequestsPending.find(peer);
		if (i == _readRequestsPending.cend()) {
			_readRequestsPending.emplace(peer, upTo);
		} else if (i->second < upTo) {
			i->second = upTo;
		}
	} else {
		sendReadRequest(peer, upTo);
	}
}

void ApiWrap::readFeed(
		not_null<Data::Feed*> feed,
		Data::MessagePosition position) {
	const auto already = feed->unreadPosition();
	if (already && already >= position) {
		return;
	}
	feed->setUnreadPosition(position);
	if (!_feedReadsDelayed.contains(feed)) {
		if (_feedReadsDelayed.empty()) {
			_feedReadTimer.callOnce(kFeedReadTimeout);
		}
		_feedReadsDelayed.emplace(feed, getms(true) + kFeedReadTimeout);
	}
}

void ApiWrap::readFeeds() {
	auto delay = kFeedReadTimeout;
	const auto now = getms(true);
	//for (auto i = begin(_feedReadsDelayed); i != end(_feedReadsDelayed);) { // #feed
	//	const auto feed = i->first;
	//	const auto time = i->second;
	//	// Clang fails to capture structure-binded feed to lambda :(
	//	//const auto [feed, time] = *i;
	//	if (time > now) {
	//		accumulate_min(delay, time - now);
	//		++i;
	//	} else if (_feedReadRequests.contains(feed)) {
	//		++i;
	//	} else {
	//		const auto position = feed->unreadPosition();
	//		const auto requestId = request(MTPchannels_ReadFeed(
	//			MTP_int(feed->id()),
	//			MTP_feedPosition(
	//				MTP_int(position.date),
	//				MTP_peerChannel(MTP_int(position.fullId.channel)),
	//				MTP_int(position.fullId.msg))
	//		)).done([=](const MTPUpdates &result) {
	//			applyUpdates(result);
	//			_feedReadRequests.remove(feed);
	//		}).fail([=](const RPCError &error) {
	//			_feedReadRequests.remove(feed);
	//		}).send();
	//		_feedReadRequests.emplace(feed, requestId);

	//		i = _feedReadsDelayed.erase(i);
	//	}
	//}
	//if (!_feedReadsDelayed.empty()) {
	//	_feedReadTimer.callOnce(delay);
	//}
}

void ApiWrap::sendReadRequest(not_null<PeerData*> peer, MsgId upTo) {
	const auto requestId = [&] {
		const auto finished = [=] {
			_readRequests.remove(peer);
			if (const auto next = _readRequestsPending.take(peer)) {
				sendReadRequest(peer, *next);
			}
		};
		if (const auto channel = peer->asChannel()) {
			return request(MTPchannels_ReadHistory(
				channel->inputChannel,
				MTP_int(upTo)
			)).done([=](const MTPBool &result) {
				finished();
			}).fail([=](const RPCError &error) {
				finished();
			}).send();
		}
		return request(MTPmessages_ReadHistory(
			peer->input,
			MTP_int(upTo)
		)).done([=](const MTPmessages_AffectedMessages &result) {
			applyAffectedMessages(peer, result);
			finished();
		}).fail([=](const RPCError &error) {
			finished();
		}).send();
	}();
	_readRequests.emplace(peer, requestId, upTo);
}

ApiWrap::~ApiWrap() = default;
