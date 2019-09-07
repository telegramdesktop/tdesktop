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
#include "data/data_poll.h"
#include "data/data_folder.h"
#include "data/data_media_types.h"
#include "data/data_sparse_ids.h"
#include "data/data_search_controller.h"
#include "data/data_scheduled_messages.h"
#include "data/data_channel_admins.h"
#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "dialogs/dialogs_key.h"
#include "core/core_cloud_password.h"
#include "core/application.h"
#include "base/openssl_help.h"
#include "base/unixtime.h"
#include "observer_peer.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "boxes/add_contact_box.h"
#include "history/history.h"
#include "history/history_message.h"
#include "history/history_item_components.h"
//#include "history/feed/history_feed_section.h" // #feed
#include "storage/localstorage.h"
#include "main/main_session.h"
#include "boxes/confirm_box.h"
#include "boxes/stickers_box.h"
#include "boxes/sticker_set_box.h"
#include "window/notifications_manager.h"
#include "window/window_lock_widgets.h"
#include "window/window_session_controller.h"
#include "window/themes/window_theme.h"
#include "inline_bots/inline_bot_result.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/stickers.h"
#include "ui/text_options.h"
#include "ui/emoji_config.h"
#include "support/support_helper.h"
#include "storage/localimageloader.h"
#include "storage/file_download.h"
#include "storage/file_upload.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"
#include "storage/storage_user_photos.h"
#include "storage/storage_media_prepare.h"
//#include "storage/storage_feed_messages.h" // #feed

namespace {

// 1 second wait before reload members in channel after adding.
constexpr auto kReloadChannelMembersTimeout = 1000;

// Save draft to the cloud with 1 sec extra delay.
constexpr auto kSaveCloudDraftTimeout = 1000;

// Give the app 1.5 secs to save drafts to cloud when quitting.
constexpr auto kSaveDraftBeforeQuitTimeout = 1500;

// Max users in one super group invite request.
constexpr auto kMaxUsersPerInvite = 100;

// How many messages from chat history server should forward to user,
// that was added to this chat.
constexpr auto kForwardMessagesOnAdd = 100;

constexpr auto kProxyPromotionInterval = TimeId(60 * 60);
constexpr auto kProxyPromotionMinDelay = TimeId(10);
constexpr auto kSmallDelayMs = 5;
constexpr auto kUnreadMentionsPreloadIfLess = 5;
constexpr auto kUnreadMentionsFirstRequestLimit = 10;
constexpr auto kUnreadMentionsNextRequestLimit = 100;
constexpr auto kSharedMediaLimit = 100;
//constexpr auto kFeedMessagesLimit = 50; // #feed
constexpr auto kReadFeaturedSetsTimeout = crl::time(1000);
constexpr auto kFileLoaderQueueStopTimeout = crl::time(5000);
//constexpr auto kFeedReadTimeout = crl::time(1000); // #feed
constexpr auto kStickersByEmojiInvalidateTimeout = crl::time(60 * 60 * 1000);
constexpr auto kNotifySettingSaveTimeout = crl::time(1000);
constexpr auto kDialogsFirstLoad = 20;
constexpr auto kDialogsPerPage = 500;
constexpr auto kBlockedFirstSlice = 16;

using PhotoFileLocationId = Data::PhotoFileLocationId;
using DocumentFileLocationId = Data::DocumentFileLocationId;
using UpdatedFileReferences = Data::UpdatedFileReferences;

MTPVector<MTPDocumentAttribute> ComposeSendingDocumentAttributes(
		not_null<DocumentData*> document) {
	const auto filenameAttribute = MTP_documentAttributeFilename(
		MTP_string(document->filename()));
	const auto dimensions = document->dimensions;
	auto attributes = QVector<MTPDocumentAttribute>(1, filenameAttribute);
	if (dimensions.width() > 0 && dimensions.height() > 0) {
		const auto duration = document->getDuration();
		if (duration >= 0 && !document->hasMimeType(qstr("image/gif"))) {
			auto flags = MTPDdocumentAttributeVideo::Flags(0);
			if (document->isVideoMessage()) {
				flags |= MTPDdocumentAttributeVideo::Flag::f_round_message;
			}
			if (document->supportsStreaming()) {
				flags |= MTPDdocumentAttributeVideo::Flag::f_supports_streaming;
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

} // namespace

MTPInputPrivacyKey ApiWrap::Privacy::Input(Key key) {
	switch (key) {
	case Privacy::Key::Calls: return MTP_inputPrivacyKeyPhoneCall();
	case Privacy::Key::Invites: return MTP_inputPrivacyKeyChatInvite();
	case Privacy::Key::PhoneNumber: return MTP_inputPrivacyKeyPhoneNumber();
	case Privacy::Key::AddedByPhone:
		return MTP_inputPrivacyKeyAddedByPhone();
	case Privacy::Key::LastSeen:
		return MTP_inputPrivacyKeyStatusTimestamp();
	case Privacy::Key::CallsPeer2Peer:
		return MTP_inputPrivacyKeyPhoneP2P();
	case Privacy::Key::Forwards:
		return MTP_inputPrivacyKeyForwards();
	case Privacy::Key::ProfilePhoto:
		return MTP_inputPrivacyKeyProfilePhoto();
	}
	Unexpected("Key in ApiWrap::Privacy::Input.");
}

std::optional<ApiWrap::Privacy::Key> ApiWrap::Privacy::KeyFromMTP(
		mtpTypeId type) {
	using Key = Privacy::Key;
	switch (type) {
	case mtpc_privacyKeyPhoneNumber:
	case mtpc_inputPrivacyKeyPhoneNumber: return Key::PhoneNumber;
	case mtpc_privacyKeyAddedByPhone:
	case mtpc_inputPrivacyKeyAddedByPhone: return Key::AddedByPhone;
	case mtpc_privacyKeyStatusTimestamp:
	case mtpc_inputPrivacyKeyStatusTimestamp: return Key::LastSeen;
	case mtpc_privacyKeyChatInvite:
	case mtpc_inputPrivacyKeyChatInvite: return Key::Invites;
	case mtpc_privacyKeyPhoneCall:
	case mtpc_inputPrivacyKeyPhoneCall: return Key::Calls;
	case mtpc_privacyKeyPhoneP2P:
	case mtpc_inputPrivacyKeyPhoneP2P: return Key::CallsPeer2Peer;
	case mtpc_privacyKeyForwards:
	case mtpc_inputPrivacyKeyForwards: return Key::Forwards;
	case mtpc_privacyKeyProfilePhoto:
	case mtpc_inputPrivacyKeyProfilePhoto: return Key::ProfilePhoto;
	}
	return std::nullopt;
}

bool ApiWrap::BlockedUsersSlice::Item::operator==(const Item &other) const {
	return (user == other.user) && (date == other.date);
}

bool ApiWrap::BlockedUsersSlice::Item::operator!=(const Item &other) const {
	return !(*this == other);
}

bool ApiWrap::BlockedUsersSlice::operator==(const BlockedUsersSlice &other) const {
	return (total == other.total) && (list == other.list);
}

bool ApiWrap::BlockedUsersSlice::operator!=(const BlockedUsersSlice &other) const {
	return !(*this == other);
}

ApiWrap::ApiWrap(not_null<Main::Session*> session)
: _session(session)
, _messageDataResolveDelayed([=] { resolveMessageDatas(); })
, _webPagesTimer([=] { resolveWebPages(); })
, _draftsSaveTimer([=] { saveDraftsToCloud(); })
, _featuredSetsReadTimer([=] { readFeaturedSets(); })
, _dialogsLoadState(std::make_unique<DialogsLoadState>())
, _fileLoader(std::make_unique<TaskQueue>(kFileLoaderQueueStopTimeout))
//, _feedReadTimer([=] { readFeeds(); }) // #feed
, _proxyPromotionTimer([=] { refreshProxyPromotion(); })
, _updateNotifySettingsTimer([=] { sendNotifySettingsUpdates(); }) {
	crl::on_main([=] {
		// You can't use _session->lifetime() in the constructor,
		// only queued, because it is not constructed yet.
		_session->uploader().photoReady(
		) | rpl::start_with_next([=](const Storage::UploadedPhoto &data) {
			photoUploadReady(data.fullId, data.file);
		}, _session->lifetime());

		setupSupportMode();
	});
}

Main::Session &ApiWrap::session() const {
	return *_session;
}

void ApiWrap::setupSupportMode() {
	if (!_session->supportMode()) {
		return;
	}

	_session->settings().supportChatsTimeSliceValue(
	) | rpl::start_with_next([=](int seconds) {
		_dialogsLoadTill = seconds ? std::max(base::unixtime::now() - seconds, 0) : 0;
		refreshDialogsLoadBlocked();
	}, _session->lifetime());
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
	const auto now = base::unixtime::now();
	const auto next = (_proxyPromotionNextRequestTime != 0)
		? _proxyPromotionNextRequestTime
		: now;
	if (_proxyPromotionRequestId) {
		getProxyPromotionDelayed(now, next);
		return;
	}
	const auto key = [&]() -> std::pair<QString, uint32> {
		if (Global::ProxySettings() != ProxyData::Settings::Enabled) {
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
			MTP_int(base::unixtime::now() + kProxyPromotionInterval)));
		return;
	}
	_proxyPromotionRequestId = request(MTPhelp_GetProxyData(
	)).done([=](const MTPhelp_ProxyData &result) {
		_proxyPromotionRequestId = 0;
		proxyPromotionDone(result);
	}).fail([=](const RPCError &error) {
		_proxyPromotionRequestId = 0;
		const auto now = base::unixtime::now();
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
		kProxyPromotionInterval) * crl::time(1000));
};

void ApiWrap::proxyPromotionDone(const MTPhelp_ProxyData &proxy) {
	_proxyPromotionNextRequestTime = proxy.match([&](const auto &data) {
		return data.vexpires().v;
	});
	getProxyPromotionDelayed(base::unixtime::now(), _proxyPromotionNextRequestTime);

	proxy.match([&](const MTPDhelp_proxyDataEmpty &data) {
		_session->data().setProxyPromoted(nullptr);
	}, [&](const MTPDhelp_proxyDataPromo &data) {
		_session->data().processChats(data.vchats());
		_session->data().processUsers(data.vusers());
		const auto peerId = peerFromMTP(data.vpeer());
		const auto peer = _session->data().peer(peerId);
		_session->data().setProxyPromoted(peer);
		if (const auto history = _session->data().historyLoaded(peer)) {
			requestDialogEntry(history);
		}
	});
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
	const auto now = crl::now();
	if (_termsUpdateSendAt && now < _termsUpdateSendAt) {
		App::CallDelayed(_termsUpdateSendAt - now, _session, [=] {
			requestTermsUpdate();
		});
		return;
	}

	constexpr auto kTermsUpdateTimeoutMin = 10 * crl::time(1000);
	constexpr auto kTermsUpdateTimeoutMax = 86400 * crl::time(1000);

	_termsUpdateRequestId = request(MTPhelp_GetTermsOfServiceUpdate(
	)).done([=](const MTPhelp_TermsOfServiceUpdate &result) {
		_termsUpdateRequestId = 0;

		const auto requestNext = [&](auto &&data) {
			const auto timeout = (data.vexpires().v - base::unixtime::now());
			_termsUpdateSendAt = crl::now() + snap(
				timeout * crl::time(1000),
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
			const auto &terms = data.vterms_of_service();
			const auto &fields = terms.c_help_termsOfService();
			Core::App().lockByTerms(
				Window::TermsLock::FromMTP(fields));
			requestNext(data);
		} break;
		default: Unexpected("Type in requestTermsUpdate().");
		}
	}).fail([=](const RPCError &error) {
		_termsUpdateRequestId = 0;
		_termsUpdateSendAt = crl::now() + kTermsUpdateTimeoutMin;
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

void ApiWrap::checkChatInvite(
		const QString &hash,
		FnMut<void(const MTPChatInvite &)> done,
		FnMut<void(const RPCError &)> fail) {
	request(base::take(_checkInviteRequestId)).cancel();
	_checkInviteRequestId = request(MTPmessages_CheckChatInvite(
		MTP_string(hash)
	)).done(std::move(done)).fail(std::move(fail)).send();
}

void ApiWrap::importChatInvite(const QString &hash) {
	request(MTPmessages_ImportChatInvite(
		MTP_string(hash)
	)).done([=](const MTPUpdates &result) {
		applyUpdates(result);

		Ui::hideLayer();
		const auto handleChats = [&](const MTPVector<MTPChat> &chats) {
			if (chats.v.isEmpty()) {
				return;
			}
			const auto peerId = chats.v[0].match([](const MTPDchat &data) {
				return peerFromChat(data.vid().v);
			}, [](const MTPDchannel &data) {
				return peerFromChannel(data.vid().v);
			}, [](auto&&) {
				return PeerId(0);
			});
			if (const auto peer = _session->data().peerLoaded(peerId)) {
				App::wnd()->sessionController()->showPeerHistory(
					peer,
					Window::SectionShow::Way::Forward);
			}
		};
		result.match([&](const MTPDupdates &data) {
			handleChats(data.vchats());
		}, [&](const MTPDupdatesCombined &data) {
			handleChats(data.vchats());
		}, [&](auto &&) {
			LOG(("API Error: unexpected update cons %1 "
				"(MainWidget::inviteImportDone)").arg(result.type()));
		});
	}).fail([=](const RPCError &error) {
		const auto &type = error.type();
		if (type == qstr("CHANNELS_TOO_MUCH")) {
			Ui::show(Box<InformBox>(tr::lng_join_channel_error(tr::now)));
		} else if (error.code() == 400) {
			Ui::show(Box<InformBox>((type == qstr("USERS_TOO_MUCH"))
				? tr::lng_group_invite_no_room(tr::now)
				: tr::lng_group_invite_bad_link(tr::now)));
		}
	}).send();
}

void ApiWrap::applyUpdates(
		const MTPUpdates &updates,
		uint64 sentMessageRandomId) {
	App::main()->feedUpdates(updates, sentMessageRandomId);
}

void ApiWrap::savePinnedOrder(Data::Folder *folder) {
	const auto &order = _session->data().pinnedChatsOrder(folder);
	const auto input = [](const Dialogs::Key &key) {
		if (const auto history = key.history()) {
			return MTP_inputDialogPeer(history->peer->input);
		} else if (const auto folder = key.folder()) {
			return MTP_inputDialogPeerFolder(MTP_int(folder->id()));
		}
		Unexpected("Key type in pinnedDialogsOrder().");
	};
	auto peers = QVector<MTPInputDialogPeer>();
	peers.reserve(order.size());
	ranges::transform(
		order,
		ranges::back_inserter(peers),
		input);
	request(MTPmessages_ReorderPinnedDialogs(
		MTP_flags(MTPmessages_ReorderPinnedDialogs::Flag::f_force),
		MTP_int(folder ? folder->id() : 0),
		MTP_vector(peers)
	)).send();
}

void ApiWrap::toggleHistoryArchived(
		not_null<History*> history,
		bool archived,
		Fn<void()> callback) {
	if (const auto already = _historyArchivedRequests.take(history)) {
		request(already->first).cancel();
	}
	const auto isPinned = history->isPinnedDialog();
	const auto archiveId = Data::Folder::kId;
	const auto requestId = request(MTPfolders_EditPeerFolders(
		MTP_vector<MTPInputFolderPeer>(
			1,
			MTP_inputFolderPeer(
				history->peer->input,
				MTP_int(archived ? archiveId : 0)))
	)).done([=](const MTPUpdates &result) {
		applyUpdates(result);
		if (archived) {
			history->setFolder(_session->data().folder(archiveId));
		} else {
			history->clearFolder();
		}
		if (const auto data = _historyArchivedRequests.take(history)) {
			data->second();
		}
		if (isPinned) {
			_session->data().notifyPinnedDialogsOrderUpdated();
		}
	}).fail([=](const RPCError &error) {
		_historyArchivedRequests.remove(history);
	}).send();
	_historyArchivedRequests.emplace(history, requestId, callback);
}
// #feed
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

void ApiWrap::sendMessageFail(
		const RPCError &error,
		not_null<PeerData*> peer,
		FullMsgId itemId) {
	if (error.type() == qstr("PEER_FLOOD")) {
		Ui::show(Box<InformBox>(
			PeerFloodErrorText(PeerFloodType::Send)));
	} else if (error.type() == qstr("USER_BANNED_IN_CHANNEL")) {
		const auto link = textcmdLink(
			Core::App().createInternalLinkFull(qsl("spambot")),
			tr::lng_cant_more_info(tr::now));
		Ui::show(Box<InformBox>(tr::lng_error_public_groups_denied(
			tr::now,
			lt_more_info,
			link)));
	} else if (error.type().startsWith(qstr("SLOWMODE_WAIT_"))) {
		const auto chop = qstr("SLOWMODE_WAIT_").size();
		const auto left = error.type().mid(chop).toInt();
		if (const auto channel = peer->asChannel()) {
			const auto seconds = channel->slowmodeSeconds();
			if (seconds >= left) {
				channel->growSlowmodeLastMessage(
					base::unixtime::now() - (left - seconds));
			} else {
				requestFullPeer(peer);
			}
		}
	}
	if (const auto item = _session->data().message(itemId)) {
		item->sendFailed();
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
			if (onlyExisting) {
				return nullptr;
			}
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
	const auto handleResult = [&](auto &&result) {
		_session->data().processUsers(result.vusers());
		_session->data().processChats(result.vchats());
		_session->data().processMessages(
			result.vmessages(),
			NewMessageType::Existing);
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
			channel->ptsReceived(d.vpts().v);
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

QString ApiWrap::exportDirectMessageLink(not_null<HistoryItem*> item) {
	Expects(item->history()->peer->isChannel());

	const auto itemId = item->fullId();
	const auto channel = item->history()->peer->asChannel();
	const auto fallback = [&] {
		const auto base = channel->hasUsername()
			? channel->username
			: "c/" + QString::number(channel->bareId());
		const auto query = base + '/' + QString::number(item->id);
		if (channel->hasUsername() && !channel->isMegagroup()) {
			if (const auto media = item->media()) {
				if (const auto document = media->document()) {
					if (document->isVideoMessage()) {
						return qsl("https://telesco.pe/") + query;
					}
				}
			}
		}
		return Core::App().createInternalLinkFull(query);
	};
	const auto i = _unlikelyMessageLinks.find(itemId);
	const auto current = (i != end(_unlikelyMessageLinks))
		? i->second
		: fallback();
	request(MTPchannels_ExportMessageLink(
		channel->inputChannel,
		MTP_int(item->id),
		MTP_bool(false)
	)).done([=](const MTPExportedMessageLink &result) {
		const auto link = result.match([&](const auto &data) {
			return qs(data.vlink());
		});
		if (current != link) {
			_unlikelyMessageLinks.emplace_or_assign(itemId, link);
		}
	}).send();
	return current;
}

void ApiWrap::requestContacts() {
	if (_session->data().contactsLoaded().current() || _contactsRequestId) {
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
		_session->data().processUsers(d.vusers());
		for (const auto &contact : d.vcontacts().v) {
			if (contact.type() != mtpc_contact) continue;

			const auto userId = contact.c_contact().vuser_id().v;
			if (userId == _session->userId()) {
				_session->user()->setIsContact(true);
			}
		}
		_session->data().contactsLoaded() = true;
	}).fail([=](const RPCError &error) {
		_contactsRequestId = 0;
	}).send();
}

void ApiWrap::requestDialogs(Data::Folder *folder) {
	if (folder && !_foldersLoadState.contains(folder)) {
		_foldersLoadState.emplace(folder, DialogsLoadState());
	}
	requestMoreDialogs(folder);
}

void ApiWrap::requestMoreDialogs(Data::Folder *folder) {
	const auto state = dialogsLoadState(folder);
	if (!state) {
		return;
	} else if (state->requestId) {
		return;
	} else if (_dialogsLoadBlockedByDate.current()) {
		return;
	}

	const auto firstLoad = !state->offsetDate;
	const auto loadCount = firstLoad ? kDialogsFirstLoad : kDialogsPerPage;
	const auto flags = MTPmessages_GetDialogs::Flag::f_exclude_pinned
		| MTPmessages_GetDialogs::Flag::f_folder_id;
	const auto hash = 0;
	state->requestId = request(MTPmessages_GetDialogs(
		MTP_flags(flags),
		MTP_int(folder ? folder->id() : 0),
		MTP_int(state->offsetDate),
		MTP_int(state->offsetId),
		(state->offsetPeer
			? state->offsetPeer->input
			: MTP_inputPeerEmpty()),
		MTP_int(loadCount),
		MTP_int(hash)
	)).done([=](const MTPmessages_Dialogs &result) {
		const auto state = dialogsLoadState(folder);
		const auto count = result.match([](
				const MTPDmessages_dialogsNotModified &) {
			LOG(("API Error: not-modified received for requested dialogs."));
			return 0;
		}, [&](const MTPDmessages_dialogs &data) {
			if (state) {
				state->listReceived = true;
				dialogsLoadFinish(folder); // may kill 'state'.
			}
			return int(data.vdialogs().v.size());
		}, [&](const MTPDmessages_dialogsSlice &data) {
			updateDialogsOffset(
				folder,
				data.vdialogs().v,
				data.vmessages().v);
			return data.vcount().v;
		});
		result.match([](const MTPDmessages_dialogsNotModified & data) {
			LOG(("API Error: not-modified received for requested dialogs."));
		}, [&](const auto &data) {
			_session->data().processUsers(data.vusers());
			_session->data().processChats(data.vchats());
			_session->data().applyDialogs(
				folder,
				data.vmessages().v,
				data.vdialogs().v,
				count);
		});

		if (!folder) {
			if (!_dialogsLoadState || !_dialogsLoadState->listReceived) {
				refreshDialogsLoadBlocked();
			}
			requestDialogs(folder);
			requestContacts();
		}
		_session->data().chatsListChanged(folder);
	}).fail([=](const RPCError &error) {
		dialogsLoadState(folder)->requestId = 0;
	}).send();

	if (!state->pinnedReceived) {
		requestPinnedDialogs(folder);
	}
	if (!folder) {
		refreshDialogsLoadBlocked();
	}
}

void ApiWrap::refreshDialogsLoadBlocked() {
	_dialogsLoadMayBlockByDate = _dialogsLoadState
		&& !_dialogsLoadState->listReceived
		&& (_dialogsLoadTill > 0);
	_dialogsLoadBlockedByDate = _dialogsLoadState
		&& !_dialogsLoadState->listReceived
		&& !_dialogsLoadState->requestId
		&& (_dialogsLoadTill > 0)
		&& (_dialogsLoadState->offsetDate > 0)
		&& (_dialogsLoadState->offsetDate <= _dialogsLoadTill);
}

void ApiWrap::updateDialogsOffset(
		Data::Folder *folder,
		const QVector<MTPDialog> &dialogs,
		const QVector<MTPMessage> &messages) {
	auto lastDate = TimeId(0);
	auto lastPeer = PeerId(0);
	auto lastMsgId = MsgId(0);
	for (const auto &dialog : ranges::view::reverse(dialogs)) {
		dialog.match([&](const auto &dialog) {
			const auto peer = peerFromMTP(dialog.vpeer());
			const auto messageId = dialog.vtop_message().v;
			if (!peer || !messageId) {
				return;
			}
			if (!lastPeer) {
				lastPeer = peer;
			}
			if (!lastMsgId) {
				lastMsgId = messageId;
			}
			for (const auto &message : ranges::view::reverse(messages)) {
				if (IdFromMessage(message) == messageId
					&& PeerFromMessage(message) == peer) {
					if (const auto date = DateFromMessage(message)) {
						lastDate = date;
					}
					return;
				}
			}
		});
		if (lastDate) {
			break;
		}
	}
	if (const auto state = dialogsLoadState(folder)) {
		if (lastDate) {
			state->offsetDate = lastDate;
			state->offsetId = lastMsgId;
			state->offsetPeer = _session->data().peer(lastPeer);
			state->requestId = 0;
		} else {
			state->listReceived = true;
			dialogsLoadFinish(folder);
		}
	}
}

auto ApiWrap::dialogsLoadState(Data::Folder *folder) -> DialogsLoadState* {
	if (!folder) {
		return _dialogsLoadState.get();
	}
	const auto i = _foldersLoadState.find(folder);
	return (i != end(_foldersLoadState)) ? &i->second : nullptr;
}

void ApiWrap::dialogsLoadFinish(Data::Folder *folder) {
	const auto notify = [&] {
		Core::App().postponeCall(crl::guard(_session, [=] {
			_session->data().chatsListDone(folder);
		}));
	};
	const auto state = dialogsLoadState(folder);
	if (!state || !state->listReceived || !state->pinnedReceived) {
		return;
	}
	if (folder) {
		_foldersLoadState.remove(folder);
		notify();
	} else {
		_dialogsLoadState = nullptr;
		notify();
	}
}

void ApiWrap::requestPinnedDialogs(Data::Folder *folder) {
	const auto state = dialogsLoadState(folder);
	if (!state || state->pinnedReceived || state->pinnedRequestId) {
		return;
	}

	const auto finalize = [=] {
		if (const auto state = dialogsLoadState(folder)) {
			state->pinnedRequestId = 0;
			state->pinnedReceived = true;
			dialogsLoadFinish(folder);
		}
	};
	state->pinnedRequestId = request(MTPmessages_GetPinnedDialogs(
		MTP_int(folder ? folder->id() : 0)
	)).done([=](const MTPmessages_PeerDialogs &result) {
		finalize();
		result.match([&](const MTPDmessages_peerDialogs &data) {
			_session->data().processUsers(data.vusers());
			_session->data().processChats(data.vchats());
			_session->data().clearPinnedChats(folder);
			_session->data().applyDialogs(
				folder,
				data.vmessages().v,
				data.vdialogs().v);
			_session->data().chatsListChanged(folder);
			_session->data().notifyPinnedDialogsOrderUpdated();
		});
	}).fail([=](const RPCError &error) {
		finalize();
	}).send();
}

void ApiWrap::requestMoreBlockedByDateDialogs() {
	if (!_dialogsLoadState) {
		return;
	}
	const auto max = _session->settings().supportChatsTimeSlice();
	_dialogsLoadTill = _dialogsLoadState->offsetDate
		? (_dialogsLoadState->offsetDate - max)
		: (base::unixtime::now() - max);
	refreshDialogsLoadBlocked();
	requestDialogs();
}

rpl::producer<bool> ApiWrap::dialogsLoadMayBlockByDate() const {
	return _dialogsLoadMayBlockByDate.value();
}

rpl::producer<bool> ApiWrap::dialogsLoadBlockedByDate() const {
	return _dialogsLoadBlockedByDate.value();
}

void ApiWrap::requestDialogEntry(not_null<Data::Folder*> folder) {
	if (_dialogFolderRequests.contains(folder)) {
		return;
	}
	_dialogFolderRequests.emplace(folder);

	auto peers = QVector<MTPInputDialogPeer>(
		1,
		MTP_inputDialogPeerFolder(MTP_int(folder->id())));
	request(MTPmessages_GetPeerDialogs(
		MTP_vector(std::move(peers))
	)).done([=](const MTPmessages_PeerDialogs &result) {
		applyPeerDialogs(result);
		_dialogFolderRequests.remove(folder);
	}).fail([=](const RPCError &error) {
		_dialogFolderRequests.remove(folder);
	}).send();
}

void ApiWrap::requestDialogEntry(
		not_null<History*> history,
		Fn<void()> callback) {
	const auto i = _dialogRequests.find(history);
	if (i != end(_dialogRequests)) {
		if (callback) {
			i->second.push_back(std::move(callback));
		}
		return;
	}

	const auto [j, ok] = _dialogRequestsPending.try_emplace(history);
	if (callback) {
		j->second.push_back(std::move(callback));
	}
	if (!ok) {
		return;
	}
	if (_dialogRequestsPending.size() > 1) {
		return;
	}
	Core::App().postponeCall(crl::guard(_session, [=] {
		sendDialogRequests();
	}));
}

void ApiWrap::sendDialogRequests() {
	if (_dialogRequestsPending.empty()) {
		return;
	}
	auto histories = std::vector<not_null<History*>>();
	ranges::transform(
		_dialogRequestsPending,
		ranges::back_inserter(histories),
		[](const auto &pair) { return pair.first; });
	auto peers = QVector<MTPInputDialogPeer>();
	const auto dialogPeer = [](not_null<History*> history) {
		return MTP_inputDialogPeer(history->peer->input);
	};
	ranges::transform(
		histories,
		ranges::back_inserter(peers),
		dialogPeer);
	for (auto &[history, callbacks] : base::take(_dialogRequestsPending)) {
		_dialogRequests.emplace(history, std::move(callbacks));
	}

	const auto finalize = [=] {
		for (const auto history : histories) {
			dialogEntryApplied(history);
			history->updateChatListExistence();
		}
	};
	request(MTPmessages_GetPeerDialogs(
		MTP_vector(std::move(peers))
	)).done([=](const MTPmessages_PeerDialogs &result) {
		applyPeerDialogs(result);
		finalize();
	}).fail([=](const RPCError &error) {
		finalize();
	}).send();
}

void ApiWrap::dialogEntryApplied(not_null<History*> history) {
	history->dialogEntryApplied();
	if (const auto callbacks = _dialogRequestsPending.take(history)) {
		for (const auto &callback : *callbacks) {
			callback();
		}
	}
	if (const auto callbacks = _dialogRequests.take(history)) {
		for (const auto &callback : *callbacks) {
			callback();
		}
	}
}

void ApiWrap::applyPeerDialogs(const MTPmessages_PeerDialogs &dialogs) {
	Expects(dialogs.type() == mtpc_messages_peerDialogs);

	const auto &data = dialogs.c_messages_peerDialogs();
	_session->data().processUsers(data.vusers());
	_session->data().processChats(data.vchats());
	_session->data().processMessages(data.vmessages(), NewMessageType::Last);
	for (const auto &dialog : data.vdialogs().v) {
		dialog.match([&](const MTPDdialog &data) {
			if (const auto peerId = peerFromMTP(data.vpeer())) {
				_session->data().history(peerId)->applyDialog(nullptr, data);
			}
		}, [&](const MTPDdialogFolder &data) {
			const auto folder = _session->data().processFolder(data.vfolder());
			folder->applyDialog(data);
		});
	}
	_session->data().sendHistoryChangeNotifications();
}

void ApiWrap::changeDialogUnreadMark(
		not_null<History*> history,
		bool unread) {
	history->setUnreadMark(unread);

	using Flag = MTPmessages_MarkDialogUnread::Flag;
	request(MTPmessages_MarkDialogUnread(
		MTP_flags(unread ? Flag::f_unread : Flag(0)),
		MTP_inputDialogPeer(history->peer->input)
	)).send();
}

void ApiWrap::requestFakeChatListMessage(
		not_null<History*> history) {
	if (_fakeChatListRequests.contains(history)) {
		return;
	}

	_fakeChatListRequests.emplace(history);
	request(MTPmessages_GetHistory(
		history->peer->input,
		MTP_int(0),  // offset_id
		MTP_int(0),  // offset_date
		MTP_int(0),  // add_offset
		MTP_int(2),  // limit
		MTP_int(0),  // max_id
		MTP_int(0),  // min_id
		MTP_int(0)
	)).done([=](const MTPmessages_Messages &result) {
		_fakeChatListRequests.erase(history);
		history->setFakeChatListMessageFrom(result);
	}).fail([=](const RPCError &error) {
		_fakeChatListRequests.erase(history);
		history->setFakeChatListMessageFrom(MTP_messages_messages(
			MTP_vector<MTPMessage>(0),
			MTP_vector<MTPChat>(0),
			MTP_vector<MTPUser>(0)));
	}).send();
}

void ApiWrap::requestWallPaper(
		const QString &slug,
		Fn<void(const Data::WallPaper &)> done,
		Fn<void(const RPCError &)> fail) {
	if (_wallPaperSlug != slug) {
		_wallPaperSlug = slug;
		if (_wallPaperRequestId) {
			request(base::take(_wallPaperRequestId)).cancel();
		}
	}
	_wallPaperDone = std::move(done);
	_wallPaperFail = std::move(fail);
	if (_wallPaperRequestId) {
		return;
	}
	_wallPaperRequestId = request(MTPaccount_GetWallPaper(
		MTP_inputWallPaperSlug(MTP_string(slug))
	)).done([=](const MTPWallPaper &result) {
		_wallPaperRequestId = 0;
		_wallPaperSlug = QString();
		if (const auto paper = Data::WallPaper::Create(result)) {
			if (const auto done = base::take(_wallPaperDone)) {
				done(*paper);
			}
		} else if (const auto fail = base::take(_wallPaperFail)) {
			fail(RPCError::Local("BAD_DOCUMENT", "In a wallpaper."));
		}
	}).fail([=](const RPCError &error) {
		_wallPaperRequestId = 0;
		_wallPaperSlug = QString();
		if (const auto fail = base::take(_wallPaperFail)) {
			fail(error);
		}
	}).send();
}

void ApiWrap::requestFullPeer(not_null<PeerData*> peer) {
	if (_fullPeerRequests.contains(peer)) {
		return;
	}

	const auto requestId = [&] {
		const auto failHandler = [=](const RPCError &error) {
			_fullPeerRequests.remove(peer);
			migrateFail(peer, error);
		};
		if (const auto user = peer->asUser()) {
			if (_session->supportMode()) {
				_session->supportHelper().refreshInfo(user);
			}
			return request(MTPusers_GetFullUser(
				user->inputUser
			)).done([=](const MTPUserFull &result, mtpRequestId requestId) {
				gotUserFull(user, result, requestId);
			}).fail(failHandler).send();
		} else if (const auto chat = peer->asChat()) {
			return request(MTPmessages_GetFullChat(
				chat->inputChat
			)).done([=](
					const MTPmessages_ChatFull &result,
					mtpRequestId requestId) {
				gotChatFull(peer, result, requestId);
			}).fail(failHandler).send();
		} else if (const auto channel = peer->asChannel()) {
			return request(MTPchannels_GetFullChannel(
				channel->inputChannel
			)).done([=](
					const MTPmessages_ChatFull &result,
					mtpRequestId requestId) {
				gotChatFull(peer, result, requestId);
				migrateDone(channel, channel);
			}).fail(failHandler).send();
		}
		Unexpected("Peer type in requestFullPeer.");
	}();
	_fullPeerRequests.insert(peer, requestId);
}

void ApiWrap::processFullPeer(
		not_null<PeerData*> peer,
		const MTPmessages_ChatFull &result) {
	gotChatFull(peer, result, mtpRequestId(0));
}

void ApiWrap::processFullPeer(
		not_null<UserData*> user,
		const MTPUserFull &result) {
	gotUserFull(user, result, mtpRequestId(0));
}

void ApiWrap::gotChatFull(
		not_null<PeerData*> peer,
		const MTPmessages_ChatFull &result,
		mtpRequestId req) {
	const auto &d = result.c_messages_chatFull();
	_session->data().applyMaximumChatVersions(d.vchats());

	_session->data().processUsers(d.vusers());
	_session->data().processChats(d.vchats());

	d.vfull_chat().match([&](const MTPDchatFull &data) {
		if (const auto chat = peer->asChat()) {
			Data::ApplyChatUpdate(chat, data);
		} else {
			LOG(("MTP Error: bad type in gotChatFull for channel: %1"
				).arg(d.vfull_chat().type()));
		}
	}, [&](const MTPDchannelFull &data) {
		if (const auto channel = peer->asChannel()) {
			Data::ApplyChannelUpdate(channel, data);
		} else {
			LOG(("MTP Error: bad type in gotChatFull for chat: %1"
				).arg(d.vfull_chat().type()));
		}
	});

	if (req) {
		const auto i = _fullPeerRequests.find(peer);
		if (i != _fullPeerRequests.cend() && i.value() == req) {
			_fullPeerRequests.erase(i);
		}
	}
	fullPeerUpdated().notify(peer);
}

void ApiWrap::gotUserFull(
		not_null<UserData*> user,
		const MTPUserFull &result,
		mtpRequestId req) {
	const auto &d = result.c_userFull();
	if (user == _session->user() && !_session->validateSelf(d.vuser())) {
		constexpr auto kRequestUserAgainTimeout = crl::time(10000);
		App::CallDelayed(kRequestUserAgainTimeout, _session, [=] {
			requestFullPeer(user);
		});
		return;
	}
	Data::ApplyUserUpdate(user, d);

	if (req) {
		const auto i = _fullPeerRequests.find(user);
		if (i != _fullPeerRequests.cend() && i.value() == req) {
			_fullPeerRequests.erase(i);
		}
	}
	fullPeerUpdated().notify(user);
}

void ApiWrap::requestPeer(not_null<PeerData*> peer) {
	if (_fullPeerRequests.contains(peer) || _peerRequests.contains(peer)) {
		return;
	}

	const auto requestId = [&] {
		const auto failHandler = [=](const RPCError &error) {
			_peerRequests.remove(peer);
		};
		const auto chatHandler = [=](const MTPmessages_Chats &result) {
			_peerRequests.remove(peer);
			const auto &chats = result.match([](const auto &data) {
				return data.vchats();
			});
			_session->data().applyMaximumChatVersions(chats);
			_session->data().processChats(chats);
		};
		if (const auto user = peer->asUser()) {
			return request(MTPusers_GetUsers(
				MTP_vector<MTPInputUser>(1, user->inputUser)
			)).done([=](const MTPVector<MTPUser> &result) {
				_peerRequests.remove(user);
				_session->data().processUsers(result);
			}).fail(failHandler).send();
		} else if (const auto chat = peer->asChat()) {
			return request(MTPmessages_GetChats(
				MTP_vector<MTPint>(1, chat->inputChat)
			)).done(chatHandler).fail(failHandler).send();
		} else if (const auto channel = peer->asChannel()) {
			return request(MTPchannels_GetChannels(
				MTP_vector<MTPInputChannel>(1, channel->inputChannel)
			)).done(chatHandler).fail(failHandler).send();
		}
		Unexpected("Peer type in requestPeer.");
	}();
	_peerRequests.insert(peer, requestId);
}

void ApiWrap::requestPeerSettings(not_null<PeerData*> peer) {
	if (!_requestedPeerSettings.emplace(peer).second) {
		return;
	}
	request(MTPmessages_GetPeerSettings(
		peer->input
	)).done([=](const MTPPeerSettings &result) {
		peer->setSettings(result.match([&](const MTPDpeerSettings &data) {
			return data.vflags().v;
		}));
		_requestedPeerSettings.erase(peer);
	}).fail([=](const RPCError &error) {
		_requestedPeerSettings.erase(peer);
	}).send();
}

void ApiWrap::migrateChat(
		not_null<ChatData*> chat,
		FnMut<void(not_null<ChannelData*>)> done,
		FnMut<void(const RPCError &)> fail) {
	const auto callback = [&] {
		return MigrateCallbacks{ std::move(done), std::move(fail) };
	};
	const auto i = _migrateCallbacks.find(chat);
	if (i != _migrateCallbacks.end()) {
		i->second.push_back(callback());
		return;
	}
	_migrateCallbacks.emplace(chat).first->second.push_back(callback());
	if (const auto channel = chat->migrateTo()) {
		Notify::peerUpdatedDelayed(
			chat,
			Notify::PeerUpdate::Flag::MigrationChanged);
		crl::on_main([=] {
			migrateDone(chat, channel);
		});
	} else if (chat->isDeactivated()) {
		crl::on_main([=] {
			migrateFail(
				chat,
				RPCError::Local(
					"BAD_MIGRATION",
					"Chat is already deactivated"));
		});
		return;
	} else if (!chat->amCreator()) {
		crl::on_main([=] {
			migrateFail(
				chat,
				RPCError::Local(
					"BAD_MIGRATION",
					"Current user is not the creator of that chat"));
		});
		return;
	}

	request(MTPmessages_MigrateChat(
		chat->inputChat
	)).done([=](const MTPUpdates &result) {
		applyUpdates(result);
		Notify::peerUpdatedSendDelayed();

		if (const auto channel = chat->migrateTo()) {
			if (auto handlers = _migrateCallbacks.take(chat)) {
				_migrateCallbacks.emplace(channel, std::move(*handlers));
			}
			requestFullPeer(channel);
		} else {
			migrateFail(
				chat,
				RPCError::Local("MIGRATION_FAIL", "No channel"));
		}
	}).fail([=](const RPCError &error) {
		migrateFail(chat, error);
	}).send();
}

void ApiWrap::migrateDone(
		not_null<PeerData*> peer,
		not_null<ChannelData*> channel) {
	Notify::peerUpdatedSendDelayed();
	if (auto handlers = _migrateCallbacks.take(peer)) {
		for (auto &handler : *handlers) {
			if (handler.done) {
				handler.done(channel);
			}
		}
	}
}

void ApiWrap::migrateFail(not_null<PeerData*> peer, const RPCError &error) {
	const auto &type = error.type();
	if (type == qstr("CHANNELS_TOO_MUCH")) {
		Ui::show(Box<InformBox>(tr::lng_migrate_error(tr::now)));
	}
	if (auto handlers = _migrateCallbacks.take(peer)) {
		for (auto &handler : *handlers) {
			if (handler.fail) {
				handler.fail(error);
			}
		}
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
		if ((!item->isUnreadMedia() || item->out())
			&& !item->isUnreadMention()) {
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
	if ((!item->isUnreadMedia() || item->out())
		&& !item->isUnreadMention()) {
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
	for (const auto peer : peers) {
		if (!peer
			|| _fullPeerRequests.contains(peer)
			|| _peerRequests.contains(peer)) {
			continue;
		}
		if (const auto user = peer->asUser()) {
			users.push_back(user->inputUser);
		} else if (const auto chat = peer->asChat()) {
			chats.push_back(chat->inputChat);
		} else if (const auto channel = peer->asChannel()) {
			channels.push_back(channel->inputChannel);
		}
	}
	const auto handleChats = [=](const MTPmessages_Chats &result) {
		_session->data().processChats(result.match([](const auto &data) {
			return data.vchats();
		}));
	};
	if (!chats.isEmpty()) {
		request(MTPmessages_GetChats(
			MTP_vector<MTPint>(chats)
		)).done(handleChats).send();
	}
	if (!channels.isEmpty()) {
		request(MTPchannels_GetChannels(
			MTP_vector<MTPInputChannel>(channels)
		)).done(handleChats).send();
	}
	if (!users.isEmpty()) {
		request(MTPusers_GetUsers(
			MTP_vector<MTPInputUser>(users)
		)).done([=](const MTPVector<MTPUser> &result) {
			_session->data().processUsers(result);
		}).send();
	}
}

void ApiWrap::requestLastParticipants(not_null<ChannelData*> channel) {
	if (!channel->isMegagroup()
		|| _participantsRequests.contains(channel)) {
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
	)).done([=](const MTPchannels_ChannelParticipants &result) {
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
		result.match([&](const MTPDchannels_channelParticipants &data) {
			Data::ApplyMegagroupAdmins(channel, data);
		}, [&](const MTPDchannels_channelParticipantsNotModified &) {
			LOG(("API Error: channels.channelParticipantsNotModified received!"));
		});
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
	const auto emptyAdminRights = MTP_chatAdminRights(MTP_flags(0));
	const auto emptyRestrictedRights = MTP_chatBannedRights(
		MTP_flags(0),
		MTP_int(0));
	for (const auto &p : list) {
		const auto userId = p.match([](const auto &data) {
			return data.vuser_id().v;
		});
		const auto adminCanEdit = (p.type() == mtpc_channelParticipantAdmin)
			? p.c_channelParticipantAdmin().is_can_edit()
			: false;
		const auto adminRights = (p.type() == mtpc_channelParticipantAdmin)
			? p.c_channelParticipantAdmin().vadmin_rights()
			: emptyAdminRights;
		const auto restrictedRights = (p.type() == mtpc_channelParticipantBanned)
			? p.c_channelParticipantBanned().vbanned_rights()
			: emptyRestrictedRights;
		if (!userId) {
			continue;
		}

		auto user = _session->data().user(userId);
		if (p.type() == mtpc_channelParticipantCreator) {
			const auto &creator = p.c_channelParticipantCreator();
			const auto rank = qs(creator.vrank().value_or_empty());
			channel->mgInfo->creator = user;
			channel->mgInfo->creatorRank = rank;
			if (!channel->mgInfo->admins.empty()) {
				Data::ChannelAdminChanges(channel).add(userId, rank);
			}
		}
		if (!base::contains(channel->mgInfo->lastParticipants, user)) {
			channel->mgInfo->lastParticipants.push_back(user);
			if (adminRights.c_chatAdminRights().vflags().v) {
				channel->mgInfo->lastAdmins.emplace(
					user,
					MegagroupInfo::Admin{ adminRights, adminCanEdit });
			} else if (restrictedRights.c_chatBannedRights().vflags().v != 0) {
				channel->mgInfo->lastRestricted.emplace(
					user,
					MegagroupInfo::Restricted{ restrictedRights });
			}
			if (user->isBot()) {
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
	fullPeerUpdated().notify(channel);
}

void ApiWrap::applyBotsList(
		not_null<ChannelData*> channel,
		int availableCount,
		const QVector<MTPChannelParticipant> &list) {
	const auto history = _session->data().historyLoaded(channel);
	channel->mgInfo->bots.clear();
	channel->mgInfo->botStatus = -1;

	auto needBotsInfos = false;
	auto botStatus = channel->mgInfo->botStatus;
	auto keyboardBotFound = !history || !history->lastKeyboardFrom;
	for (const auto &p : list) {
		const auto userId = p.match([](const auto &data) {
			return data.vuser_id().v;
		});
		if (!userId) {
			continue;
		}

		auto user = _session->data().user(userId);
		if (user->isBot()) {
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
	fullPeerUpdated().notify(channel);
}

void ApiWrap::requestSelfParticipant(not_null<ChannelData*> channel) {
	if (_selfParticipantRequests.contains(channel)) {
		return;
	}

	const auto finalize = [=](UserId inviter, TimeId inviteDate) {
		channel->inviter = inviter;
		channel->inviteDate = inviteDate;
		if (const auto history = _session->data().historyLoaded(channel)) {
			if (history->lastMessageKnown()) {
				history->checkLocalMessages();
				history->owner().sendHistoryChangeNotifications();
			} else {
				requestDialogEntry(history);
			}
		}
	};
	_selfParticipantRequests.emplace(channel);
	request(MTPchannels_GetParticipant(
		channel->inputChannel,
		MTP_inputUserSelf()
	)).done([=](const MTPchannels_ChannelParticipant &result) {
		_selfParticipantRequests.erase(channel);
		result.match([&](const MTPDchannels_channelParticipant &data) {
			_session->data().processUsers(data.vusers());

			const auto &participant = data.vparticipant();
			participant.match([&](const MTPDchannelParticipantSelf &data) {
				finalize(data.vinviter_id().v, data.vdate().v);
			}, [&](const MTPDchannelParticipantCreator &) {
				if (channel->mgInfo) {
					channel->mgInfo->creator = _session->user();
				}
				finalize(_session->userId(), channel->date);
			}, [&](const MTPDchannelParticipantAdmin &data) {
				const auto inviter = data.is_self()
					? data.vinviter_id().value_or(-1)
					: -1;
				finalize(inviter, data.vdate().v);
			}, [&](const MTPDchannelParticipantBanned &data) {
				LOG(("API Error: Got self banned participant."));
				finalize(-1, 0);
			}, [&](const MTPDchannelParticipant &data) {
				LOG(("API Error: Got self regular participant."));
				finalize(-1, 0);
			});
		});
	}).fail([=](const RPCError &error) {
		_selfParticipantRequests.erase(channel);
		if (error.type() == qstr("CHANNEL_PRIVATE")) {
			channel->markForbidden();
		}
		finalize(-1, 0);
	}).afterDelay(kSmallDelayMs).send();
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
		const MTPChatBannedRights &currentRights) {
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
		MTP_chatBannedRights(MTP_flags(0), MTP_int(0))
	)).done([=](const MTPUpdates &result) {
		applyUpdates(result);

		_kickRequests.remove(KickRequest(channel, user));
		if (channel->kickedCount() > 0) {
			channel->setKickedCount(channel->kickedCount() - 1);
		} else {
			channel->updateFullForced();
		}
	}).fail([=](const RPCError &error) {
		_kickRequests.remove(kick);
	}).send();

	_kickRequests.emplace(kick, requestId);
}

void ApiWrap::deleteAllFromUser(
		not_null<ChannelData*> channel,
		not_null<UserData*> from) {
	const auto history = _session->data().historyLoaded(channel);
	const auto ids = history
		? history->collectMessagesFromUserToDelete(from)
		: QVector<MsgId>();
	const auto channelId = peerToChannel(channel->id);
	for (const auto msgId : ids) {
		if (const auto item = _session->data().message(channelId, msgId)) {
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
		} else if (const auto history = _session->data().historyLoaded(channel)) {
			history->requestChatListMessage();
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
				it->installDate = base::unixtime::now();
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
				Ui::show(Box<InformBox>(channel->isMegagroup()
					? tr::lng_group_not_accessible(tr::now)
					: tr::lng_channel_not_accessible(tr::now)));
			} else if (error.type() == qstr("CHANNELS_TOO_MUCH")) {
				Ui::show(Box<InformBox>(tr::lng_join_channel_error(tr::now)));
			} else if (error.type() == qstr("USERS_TOO_MUCH")) {
				Ui::show(Box<InformBox>(tr::lng_group_full(tr::now)));
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
		const auto requestId = request(MTPcontacts_Block(user->inputUser)).done([this, user](const MTPBool &result) {
			_blockRequests.erase(user);
			user->setIsBlocked(true);
			if (_blockedUsersSlice) {
				_blockedUsersSlice->list.insert(
					_blockedUsersSlice->list.begin(),
					{ user, base::unixtime::now() });
				++_blockedUsersSlice->total;
				_blockedUsersChanges.fire_copy(*_blockedUsersSlice);
			}
		}).fail([this, user](const RPCError &error) {
			_blockRequests.erase(user);
		}).send();

		_blockRequests.emplace(user, requestId);
	}
}

void ApiWrap::unblockUser(not_null<UserData*> user, Fn<void()> onDone) {
	if (!user->isBlocked()) {
		Notify::peerUpdatedDelayed(
			user,
			Notify::PeerUpdate::Flag::UserIsBlocked);
		return;
	} else if (_blockRequests.find(user) != end(_blockRequests)) {
		return;
	}
	const auto requestId = request(MTPcontacts_Unblock(
		user->inputUser
	)).done([=](const MTPBool &result) {
		_blockRequests.erase(user);
		user->setIsBlocked(false);
		if (_blockedUsersSlice) {
			auto &list = _blockedUsersSlice->list;
			for (auto i = list.begin(); i != list.end(); ++i) {
				if (i->user == user) {
					list.erase(i);
					break;
				}
			}
			if (_blockedUsersSlice->total > list.size()) {
				--_blockedUsersSlice->total;
			}
			_blockedUsersChanges.fire_copy(*_blockedUsersSlice);
		}
		if (onDone) {
			onDone();
		}
	}).fail([=](const RPCError &error) {
		_blockRequests.erase(user);
	}).send();
	_blockRequests.emplace(user, requestId);
}

void ApiWrap::exportInviteLink(not_null<PeerData*> peer) {
	if (_exportInviteRequests.find(peer) != end(_exportInviteRequests)) {
		return;
	}

	const auto requestId = [&] {
		return request(MTPmessages_ExportChatInvite(
			peer->input
		)).done([=](const MTPExportedChatInvite &result) {
			_exportInviteRequests.erase(peer);
			const auto link = (result.type() == mtpc_chatInviteExported)
				? qs(result.c_chatInviteExported().vlink())
				: QString();
			if (const auto chat = peer->asChat()) {
				chat->setInviteLink(link);
			} else if (const auto channel = peer->asChannel()) {
				channel->setInviteLink(link);
			} else {
				Unexpected("Peer in ApiWrap::exportInviteLink.");
			}
		}).fail([=](const RPCError &error) {
			_exportInviteRequests.erase(peer);
		}).send();
	}();
	_exportInviteRequests.emplace(peer, requestId);
}

void ApiWrap::requestNotifySettings(const MTPInputNotifyPeer &peer) {
	const auto key = [&] {
		switch (peer.type()) {
		case mtpc_inputNotifyUsers: return peerFromUser(0);
		case mtpc_inputNotifyChats: return peerFromChat(0);
		case mtpc_inputNotifyBroadcasts: return peerFromChannel(0);
		case mtpc_inputNotifyPeer: {
			const auto &inner = peer.c_inputNotifyPeer().vpeer();
			switch (inner.type()) {
			case mtpc_inputPeerSelf:
				return _session->userPeerId();
			case mtpc_inputPeerEmpty:
				return PeerId(0);
			case mtpc_inputPeerChannel:
				return peerFromChannel(
					inner.c_inputPeerChannel().vchannel_id());
			case mtpc_inputPeerChat:
				return peerFromChat(inner.c_inputPeerChat().vchat_id());
			case mtpc_inputPeerUser:
				return peerFromUser(inner.c_inputPeerUser().vuser_id());
			}
			Unexpected("Type in ApiRequest::requestNotifySettings peer.");
		} break;
		}
		Unexpected("Type in ApiRequest::requestNotifySettings.");
	}();
	if (_notifySettingRequests.find(key) != end(_notifySettingRequests)) {
		return;
	}
	const auto requestId = request(MTPaccount_GetNotifySettings(
		peer
	)).done([=](const MTPPeerNotifySettings &result) {
		applyNotifySettings(peer, result);
		_notifySettingRequests.erase(key);
	}).fail([=](const RPCError &error) {
		applyNotifySettings(
			peer,
			MTP_peerNotifySettings(
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

void ApiWrap::savePrivacy(
		const MTPInputPrivacyKey &key,
		QVector<MTPInputPrivacyRule> &&rules) {
	const auto keyTypeId = key.type();
	const auto it = _privacySaveRequests.find(keyTypeId);
	if (it != _privacySaveRequests.cend()) {
		request(it->second).cancel();
		_privacySaveRequests.erase(it);
	}

	const auto requestId = request(MTPaccount_SetPrivacy(
		key,
		MTP_vector<MTPInputPrivacyRule>(std::move(rules))
	)).done([=](const MTPaccount_PrivacyRules &result) {
		result.match([&](const MTPDaccount_privacyRules &data) {
			_session->data().processUsers(data.vusers());
			_session->data().processChats(data.vchats());
			_privacySaveRequests.remove(keyTypeId);
			if (const auto key = Privacy::KeyFromMTP(keyTypeId)) {
				handlePrivacyChange(*key, data.vrules());
			}
		});
	}).fail([=](const RPCError &error) {
		_privacySaveRequests.remove(keyTypeId);
	}).send();

	_privacySaveRequests.emplace(keyTypeId, requestId);
}

void ApiWrap::handlePrivacyChange(
		Privacy::Key key,
		const MTPVector<MTPPrivacyRule> &rules) {
	pushPrivacy(key, rules.v);
	if (key == Privacy::Key::LastSeen) {
		updatePrivacyLastSeens(rules.v);
	}
}

void ApiWrap::updatePrivacyLastSeens(const QVector<MTPPrivacyRule> &rules) {
	const auto now = base::unixtime::now();
	_session->data().enumerateUsers([&](UserData *user) {
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
	_contactsStatusesRequestId = request(MTPcontacts_GetStatuses(
	)).done([=](const MTPVector<MTPContactStatus> &result) {
		_contactsStatusesRequestId = 0;
		for_const (auto &item, result.v) {
			Assert(item.type() == mtpc_contactStatus);
			auto &data = item.c_contactStatus();
			if (auto user = _session->data().userLoaded(data.vuser_id().v)) {
				auto oldOnlineTill = user->onlineTill;
				auto newOnlineTill = OnlineTillFromStatus(data.vstatus(), oldOnlineTill);
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

int ApiWrap::OnlineTillFromStatus(
		const MTPUserStatus &status,
		int currentOnlineTill) {
	switch (status.type()) {
	case mtpc_userStatusEmpty: return 0;
	case mtpc_userStatusRecently:
		// Don't modify pseudo-online.
		return (currentOnlineTill > -10) ? -2 : currentOnlineTill;
	case mtpc_userStatusLastWeek: return -3;
	case mtpc_userStatusLastMonth: return -4;
	case mtpc_userStatusOffline: return status.c_userStatusOffline().vwas_online().v;
	case mtpc_userStatusOnline: return status.c_userStatusOnline().vexpires().v;
	}
	Unexpected("Bad UserStatus type.");
}

void ApiWrap::clearHistory(not_null<PeerData*> peer, bool revoke) {
	auto deleteTillId = MsgId(0);
	if (const auto history = _session->data().historyLoaded(peer)) {
		while (history->lastMessageKnown()) {
			const auto last = history->lastMessage();
			if (!last) {
				// History is empty.
				return;
			} else if (!IsServerMsgId(last->id)) {
				// Destroy client-side message locally.
				last->destroy();
			} else {
				break;
			}
		}
		if (!history->lastMessageKnown()) {
			requestDialogEntry(history, [=] {
				Expects(history->lastMessageKnown());

				clearHistory(peer, revoke);
			});
			return;
		}
		deleteTillId = history->lastMessage()->id;
		history->clear(History::ClearType::ClearHistory);
	}
	if (const auto channel = peer->asChannel()) {
		if (const auto migrated = peer->migrateFrom()) {
			clearHistory(migrated, revoke);
		}
		if (IsServerMsgId(deleteTillId)) {
			request(MTPchannels_DeleteHistory(
				channel->inputChannel,
				MTP_int(deleteTillId)
			)).send();
		}
	} else {
		deleteHistory(peer, true, revoke);
	}
}

void ApiWrap::deleteConversation(not_null<PeerData*> peer, bool revoke) {
	if (const auto chat = peer->asChat()) {
		request(MTPmessages_DeleteChatUser(
			chat->inputChat,
			_session->user()->inputUser
		)).done([=](const MTPUpdates &updates) {
			applyUpdates(updates);
			deleteHistory(peer, false, revoke);
		}).fail([=](const RPCError &error) {
			deleteHistory(peer, false, revoke);
		}).send();
	} else if (const auto channel = peer->asChannel()) {
		channel->ptsWaitingForShortPoll(-1);
		leaveChannel(channel);
	} else {
		deleteHistory(peer, false, revoke);
	}
	_session->data().deleteConversationLocally(peer);
}

void ApiWrap::deleteHistory(not_null<PeerData*> peer, bool justClear, bool revoke) {
	using Flag = MTPmessages_DeleteHistory::Flag;
	const auto flags = Flag(0)
		| (justClear ? Flag::f_just_clear : Flag(0))
		| ((peer->isUser() && revoke) ? Flag::f_revoke : Flag(0));
	request(MTPmessages_DeleteHistory(
		MTP_flags(flags),
		peer->input,
		MTP_int(0)
	)).done([=](const MTPmessages_AffectedHistory &result) {
		const auto offset = applyAffectedHistory(peer, result);
		if (offset > 0) {
			deleteHistory(peer, justClear, revoke);
		}
	}).send();
}

int ApiWrap::applyAffectedHistory(
		not_null<PeerData*> peer,
		const MTPmessages_AffectedHistory &result) {
	const auto &data = result.c_messages_affectedHistory();
	if (const auto channel = peer->asChannel()) {
		channel->ptsUpdateAndApply(data.vpts().v, data.vpts_count().v);
	} else {
		App::main()->ptsUpdateAndApply(data.vpts().v, data.vpts_count().v);
	}
	return data.voffset().v;
}

void ApiWrap::applyAffectedMessages(
		not_null<PeerData*> peer,
		const MTPmessages_AffectedMessages &result) {
	const auto &data = result.c_messages_affectedMessages();
	if (const auto channel = peer->asChannel()) {
		channel->ptsUpdateAndApply(data.vpts().v, data.vpts_count().v);
	} else {
		applyAffectedMessages(result);
	}
}

void ApiWrap::applyAffectedMessages(
		const MTPmessages_AffectedMessages &result) {
	const auto &data = result.c_messages_affectedMessages();
	App::main()->ptsUpdateAndApply(data.vpts().v, data.vpts_count().v);
}

void ApiWrap::deleteMessages(
		not_null<PeerData*> peer,
		const QVector<MTPint> &ids,
		bool revoke) {
	const auto done = [=](const MTPmessages_AffectedMessages & result) {
		applyAffectedMessages(peer, result);
		if (const auto history = peer->owner().historyLoaded(peer)) {
			history->requestChatListMessage();
		}
	};
	if (const auto channel = peer->asChannel()) {
		request(MTPchannels_DeleteMessages(
			channel->inputChannel,
			MTP_vector<MTPint>(ids)
		)).done(done).send();
	} else {
		using Flag = MTPmessages_DeleteMessages::Flag;
		request(MTPmessages_DeleteMessages(
			MTP_flags(revoke ? Flag::f_revoke : Flag(0)),
			MTP_vector<MTPint>(ids)
		)).done(done).send();
	}
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
		if (!_session->supportMode()) {
			cloudDraft = history->createCloudDraft(localDraft);
		} else if (!cloudDraft) {
			cloudDraft = history->createCloudDraft(nullptr);
		}

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

		const auto draftText = textWithTags.text;
		history->setSentDraftText(draftText);
		cloudDraft->saveRequestId = request(MTPmessages_SaveDraft(
			MTP_flags(flags),
			MTP_int(cloudDraft->msgId),
			history->peer->input,
			MTP_string(textWithTags.text),
			entities
		)).done([=](const MTPBool &result, mtpRequestId requestId) {
			history->clearSentDraftText(draftText);

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
			history->clearSentDraftText(draftText);

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
		Core::App().quitPreventFinished();
	}
}

void ApiWrap::registerModifyRequest(
		const QString &key,
		mtpRequestId requestId) {
	const auto i = _modifyRequests.find(key);
	if (i != end(_modifyRequests)) {
		request(i->second).cancel();
		i->second = requestId;
	} else {
		_modifyRequests.emplace(key, requestId);
	}
}

void ApiWrap::clearModifyRequest(const QString &key) {
	_modifyRequests.remove(key);
}

void ApiWrap::applyNotifySettings(
		MTPInputNotifyPeer notifyPeer,
		const MTPPeerNotifySettings &settings) {
	switch (notifyPeer.type()) {
	case mtpc_inputNotifyUsers:
		_session->data().applyNotifySetting(MTP_notifyUsers(), settings);
	break;
	case mtpc_inputNotifyChats:
		_session->data().applyNotifySetting(MTP_notifyChats(), settings);
	break;
	case mtpc_inputNotifyBroadcasts:
		_session->data().applyNotifySetting(
			MTP_notifyBroadcasts(),
			settings);
	break;
	case mtpc_inputNotifyPeer: {
		auto &peer = notifyPeer.c_inputNotifyPeer().vpeer();
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
			apply(peerFromUser(peer.c_inputPeerUser().vuser_id()));
			break;
		case mtpc_inputPeerChat:
			apply(peerFromChat(peer.c_inputPeerChat().vchat_id()));
			break;
		case mtpc_inputPeerChannel:
			apply(peerFromChannel(peer.c_inputPeerChannel().vchannel_id()));
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
	auto left = (page->pendingTill - base::unixtime::now()) * 1000;
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
	int32 t = base::unixtime::now(), m = INT_MAX;
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

void ApiWrap::requestAttachedStickerSets(not_null<PhotoData*> photo) {
	request(_attachedStickerSetsRequestId).cancel();
	_attachedStickerSetsRequestId = request(MTPmessages_GetAttachedStickers(
		MTP_inputStickeredMediaPhoto(photo->mtpInput())
	)).done([=](const MTPVector<MTPStickerSetCovered> &result) {
		if (result.v.isEmpty()) {
			Ui::show(Box<InformBox>(tr::lng_stickers_not_found(tr::now)));
			return;
		} else if (result.v.size() > 1) {
			Ui::show(Box<StickersBox>(&session(), result));
			return;
		}
		// Single attached sticker pack.
		const auto setData = result.v.front().match([&](const auto &data) {
			return data.vset().match([&](const MTPDstickerSet &data) {
				return &data;
			});
		});

		const auto setId = (setData->vid().v && setData->vaccess_hash().v)
			? MTP_inputStickerSetID(setData->vid(), setData->vaccess_hash())
			: MTP_inputStickerSetShortName(setData->vshort_name());
		Ui::show(
			Box<StickerSetBox>(App::wnd()->sessionController(), setId),
			LayerOption::KeepOther);
	}).fail([=](const RPCError &error) {
		Ui::show(Box<InformBox>(tr::lng_stickers_not_found(tr::now)));
	}).send();
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
		nextRequestPts = d.vpts().v;
		isFinal = d.is_final();
	} break;

	case mtpc_updates_channelDifferenceTooLong: {
		const auto &d = result.c_updates_channelDifferenceTooLong();

		_session->data().processUsers(d.vusers());
		_session->data().processChats(d.vchats());

		nextRequestPts = d.vdialog().match([&](const MTPDdialog &data) {
			return data.vpts().value_or_empty();
		}, [&](const MTPDdialogFolder &data) {
			return 0;
		});
		isFinal = d.is_final();
	} break;

	case mtpc_updates_channelDifference: {
		const auto &d = result.c_updates_channelDifference();

		App::main()->feedChannelDifference(d);

		nextRequestPts = d.vpts().v;
		isFinal = d.is_final();
	} break;
	}

	if (!isFinal && nextRequestPts) {
		MTP_LOG(0, ("getChannelDifference { "
			"good - after not final channelDifference was received, "
			"validating history part }%1"
			).arg(cTestMode() ? " TESTMODE" : ""));
		channelRangeDifferenceSend(channel, range, nextRequestPts);
	}
}

template <typename Request>
void ApiWrap::requestFileReference(
		Data::FileOrigin origin,
		FileReferencesHandler &&handler,
		Request &&data) {
	const auto i = _fileReferenceHandlers.find(origin);
	if (i != end(_fileReferenceHandlers)) {
		i->second.push_back(std::move(handler));
		return;
	}
	auto handlers = std::vector<FileReferencesHandler>();
	handlers.push_back(std::move(handler));
	_fileReferenceHandlers.emplace(origin, std::move(handlers));

	request(std::move(data)).done([=](const auto &result) {
		const auto parsed = Data::GetFileReferences(result);
		for (const auto &p : parsed.data) {
			// Unpack here the parsed pair by hand to workaround a GCC bug.
			// See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=87122
			const auto &origin = p.first;
			const auto &reference = p.second;
			const auto documentId = base::get_if<DocumentFileLocationId>(
				&origin);
			if (documentId) {
				_session->data().document(
					documentId->id
				)->refreshFileReference(reference);
			}
			const auto photoId = base::get_if<PhotoFileLocationId>(&origin);
			if (photoId) {
				_session->data().photo(
					photoId->id
				)->refreshFileReference(reference);
			}
		}
		const auto i = _fileReferenceHandlers.find(origin);
		Assert(i != end(_fileReferenceHandlers));
		auto handlers = std::move(i->second);
		_fileReferenceHandlers.erase(i);
		for (auto &handler : handlers) {
			handler(parsed);
		}
	}).fail([=](const RPCError &error) {
		const auto i = _fileReferenceHandlers.find(origin);
		Assert(i != end(_fileReferenceHandlers));
		auto handlers = std::move(i->second);
		_fileReferenceHandlers.erase(i);
		for (auto &handler : handlers) {
			handler(UpdatedFileReferences());
		}
	}).send();
}

void ApiWrap::refreshFileReference(
		Data::FileOrigin origin,
		not_null<mtpFileLoader*> loader,
		int requestId,
		const QByteArray &current) {
	return refreshFileReference(origin, crl::guard(loader, [=](
			const UpdatedFileReferences &data) {
		loader->refreshFileReferenceFrom(data, requestId, current);
	}));
}

void ApiWrap::refreshFileReference(
		Data::FileOrigin origin,
		FileReferencesHandler &&handler) {
	const auto request = [&](
			auto &&data,
			Fn<void()> &&additional = nullptr) {
		requestFileReference(
			origin,
			std::move(handler),
			std::move(data));
		if (additional) {
			const auto i = _fileReferenceHandlers.find(origin);
			Assert(i != end(_fileReferenceHandlers));
			if (i->second.size() == 1) {
				i->second.push_back([=](auto&&) {
					additional();
				});
			}
		}
	};
	const auto fail = [&] {
		handler(UpdatedFileReferences());
	};
	origin.data.match([&](Data::FileOriginMessage data) {
		if (const auto item = _session->data().message(data)) {
			if (item->isScheduled()) {
				const auto &scheduled = session().data().scheduledMessages();
				const auto realId = scheduled.lookupId(item);
				request(MTPmessages_GetScheduledMessages(
					item->history()->peer->input,
					MTP_vector<MTPint>(1, MTP_int(realId))));
			} else if (const auto channel = item->history()->peer->asChannel()) {
				request(MTPchannels_GetMessages(
					channel->inputChannel,
					MTP_vector<MTPInputMessage>(
						1,
						MTP_inputMessageID(MTP_int(item->id)))));
			} else {
				request(MTPmessages_GetMessages(
					MTP_vector<MTPInputMessage>(
						1,
						MTP_inputMessageID(MTP_int(item->id)))));
			}
		} else {
			fail();
		}
	}, [&](Data::FileOriginUserPhoto data) {
		if (const auto user = _session->data().user(data.userId)) {
			request(MTPphotos_GetUserPhotos(
				user->inputUser,
				MTP_int(-1),
				MTP_long(data.photoId),
				MTP_int(1)));
		} else {
			fail();
		}
	}, [&](Data::FileOriginPeerPhoto data) {
		fail();
	}, [&](Data::FileOriginStickerSet data) {
		if (data.setId == Stickers::CloudRecentSetId
			|| data.setId == Stickers::RecentSetId) {
			request(MTPmessages_GetRecentStickers(
				MTP_flags(0),
				MTP_int(0)),
				[] { crl::on_main([] { Local::writeRecentStickers(); }); });
		} else if (data.setId == Stickers::FavedSetId) {
			request(MTPmessages_GetFavedStickers(MTP_int(0)),
				[] { crl::on_main([] { Local::writeFavedStickers(); }); });
		} else {
			request(MTPmessages_GetStickerSet(
				MTP_inputStickerSetID(
					MTP_long(data.setId),
					MTP_long(data.accessHash))),
				[] { crl::on_main([] {
					Local::writeInstalledStickers();
					Local::writeRecentStickers();
					Local::writeFavedStickers();
				}); });
		}
	}, [&](Data::FileOriginSavedGifs data) {
		request(
			MTPmessages_GetSavedGifs(MTP_int(0)),
			[] { crl::on_main([] { Local::writeSavedGifs(); }); });
	}, [&](Data::FileOriginWallpaper data) {
		request(MTPaccount_GetWallPaper(
			MTP_inputWallPaper(
				MTP_long(data.paperId),
				MTP_long(data.accessHash))));
	}, [&](std::nullopt_t) {
		fail();
	});
}

void ApiWrap::gotWebPages(ChannelData *channel, const MTPmessages_Messages &msgs, mtpRequestId req) {
	const QVector<MTPMessage> *v = 0;
	switch (msgs.type()) {
	case mtpc_messages_messages: {
		auto &d = msgs.c_messages_messages();
		_session->data().processUsers(d.vusers());
		_session->data().processChats(d.vchats());
		v = &d.vmessages().v;
	} break;

	case mtpc_messages_messagesSlice: {
		auto &d = msgs.c_messages_messagesSlice();
		_session->data().processUsers(d.vusers());
		_session->data().processChats(d.vchats());
		v = &d.vmessages().v;
	} break;

	case mtpc_messages_channelMessages: {
		auto &d = msgs.c_messages_channelMessages();
		if (channel) {
			channel->ptsReceived(d.vpts().v);
		} else {
			LOG(("API Error: received messages.channelMessages when no channel was passed! (ApiWrap::gotWebPages)"));
		}
		_session->data().processUsers(d.vusers());
		_session->data().processChats(d.vchats());
		v = &d.vmessages().v;
	} break;

	case mtpc_messages_messagesNotModified: {
		LOG(("API Error: received messages.messagesNotModified! (ApiWrap::gotWebPages)"));
	} break;
	}

	if (!v) return;

	auto indices = base::flat_map<uint64, int>(); // copied from feedMsgs
	for (auto i = 0, l = v->size(); i != l; ++i) {
		const auto msgId = IdFromMessage(v->at(i));
		indices.emplace((uint64(uint32(msgId)) << 32) | uint64(i), i);
	}

	for (const auto &[position, index] : indices) {
		const auto item = _session->data().addNewMessage(
			v->at(index),
			MTPDmessage_ClientFlags(),
			NewMessageType::Existing);
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
	_session->data().sendWebPageGamePollNotifications();
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
	auto now = crl::now();
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
		const auto now = crl::now();
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
			entry.list.reserve(data.vstickers().v.size());
			for (const auto &sticker : data.vstickers().v) {
				const auto document = _session->data().processDocument(
					sticker);
				if (document->sticker()) {
					entry.list.push_back(document);
				}
			}
			entry.hash = data.vhash().v;
			entry.received = crl::now();
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

void ApiWrap::toggleFavedSticker(
		not_null<DocumentData*> document,
		Data::FileOrigin origin,
		bool faved) {
	if (faved && !document->sticker()) {
		return;
	}

	auto failHandler = std::make_shared<Fn<void(const RPCError&, QByteArray)>>();
	auto performRequest = [=] {
		const auto usedFileReference = document->fileReference();
		request(MTPmessages_FaveSticker(
			document->mtpInput(),
			MTP_bool(!faved)
		)).done([=](const MTPBool &result) {
			if (mtpIsTrue(result)) {
				Stickers::SetFaved(document, faved);
			}
		}).fail([=](const RPCError &error) {
			(*failHandler)(error, usedFileReference);
		}).send();
	};
	*failHandler = [=](const RPCError &error, QByteArray usedFileReference) {
		if (error.code() == 400
			&& error.type().startsWith(qstr("FILE_REFERENCE_"))) {
			auto refreshed = [=](const UpdatedFileReferences &data) {
				if (document->fileReference() != usedFileReference) {
					performRequest();
				}
			};
			refreshFileReference(origin, std::move(refreshed));
		}
	};
	performRequest();
}

void ApiWrap::toggleSavedGif(
		not_null<DocumentData*> document,
		Data::FileOrigin origin,
		bool saved) {
	if (saved && !document->isGifv()) {
		return;
	}

	auto failHandler = std::make_shared<Fn<void(const RPCError&, QByteArray)>>();
	auto performRequest = [=] {
		const auto usedFileReference = document->fileReference();
		request(MTPmessages_SaveGif(
			document->mtpInput(),
			MTP_bool(!saved)
		)).done([=](const MTPBool &result) {
			if (mtpIsTrue(result)) {
				if (saved) {
					session().data().addSavedGif(document);
				}
			}
		}).fail([=](const RPCError &error) {
			(*failHandler)(error, usedFileReference);
		}).send();
	};
	*failHandler = [=](const RPCError & error, QByteArray usedFileReference) {
		if (error.code() == 400
			&& error.type().startsWith(qstr("FILE_REFERENCE_"))) {
			auto refreshed = [=](const UpdatedFileReferences &data) {
				if (document->fileReference() != usedFileReference) {
					performRequest();
				}
			};
			refreshFileReference(origin, std::move(refreshed));
		}
	};
	performRequest();
}

void ApiWrap::requestStickers(TimeId now) {
	if (!_session->data().stickersUpdateNeeded(now)
		|| _stickersUpdateRequest) {
		return;
	}
	auto onDone = [this](const MTPmessages_AllStickers &result) {
		_session->data().setLastStickersUpdate(crl::now());
		_stickersUpdateRequest = 0;

		switch (result.type()) {
		case mtpc_messages_allStickersNotModified: return;
		case mtpc_messages_allStickers: {
			auto &d = result.c_messages_allStickers();
			Stickers::SetsReceived(d.vsets().v, d.vhash().v);
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
		_session->data().setLastRecentStickersUpdate(crl::now());
		_recentStickersUpdateRequest = 0;

		switch (result.type()) {
		case mtpc_messages_recentStickersNotModified: return;
		case mtpc_messages_recentStickers: {
			auto &d = result.c_messages_recentStickers();
			Stickers::SpecialSetReceived(
				Stickers::CloudRecentSetId,
				tr::lng_recent_stickers(tr::now),
				d.vstickers().v,
				d.vhash().v,
				d.vpacks().v,
				d.vdates().v);
		} return;
		default: Unexpected("Type in ApiWrap::recentStickersDone()");
		}
	}).fail([=](const RPCError &error) {
		_session->data().setLastRecentStickersUpdate(crl::now());
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
		_session->data().setLastFavedStickersUpdate(crl::now());
		_favedStickersUpdateRequest = 0;

		switch (result.type()) {
		case mtpc_messages_favedStickersNotModified: return;
		case mtpc_messages_favedStickers: {
			auto &d = result.c_messages_favedStickers();
			Stickers::SpecialSetReceived(
				Stickers::FavedSetId,
				Lang::Hard::FavedSetTitle(),
				d.vstickers().v,
				d.vhash().v,
				d.vpacks().v);
		} return;
		default: Unexpected("Type in ApiWrap::favedStickersDone()");
		}
	}).fail([=](const RPCError &error) {
		_session->data().setLastFavedStickersUpdate(crl::now());
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
		_session->data().setLastFeaturedStickersUpdate(crl::now());
		_featuredStickersUpdateRequest = 0;

		switch (result.type()) {
		case mtpc_messages_featuredStickersNotModified: return;
		case mtpc_messages_featuredStickers: {
			auto &d = result.c_messages_featuredStickers();
			Stickers::FeaturedSetsReceived(d.vsets().v, d.vunread().v, d.vhash().v);
		} return;
		default: Unexpected("Type in ApiWrap::featuredStickersDone()");
		}
	}).fail([=](const RPCError &error) {
		_session->data().setLastFeaturedStickersUpdate(crl::now());
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
		_session->data().setLastSavedGifsUpdate(crl::now());
		_savedGifsUpdateRequest = 0;

		switch (result.type()) {
		case mtpc_messages_savedGifsNotModified: return;
		case mtpc_messages_savedGifs: {
			auto &d = result.c_messages_savedGifs();
			Stickers::GifsReceived(d.vgifs().v, d.vhash().v);
		} return;
		default: Unexpected("Type in ApiWrap::savedGifsDone()");
		}
	}).fail([=](const RPCError &error) {
		_session->data().setLastSavedGifsUpdate(crl::now());
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
	result.match([&](const MTPDchannels_channelParticipants &data) {
		_session->data().processUsers(data.vusers());
		if (channel->mgInfo) {
			refreshChannelAdmins(channel, data.vparticipants().v);
		}
		if (callbackList) {
			callbackList(data.vcount().v, data.vparticipants().v);
		}
	}, [&](const MTPDchannels_channelParticipantsNotModified &) {
		if (callbackNotModified) {
			callbackNotModified();
		} else {
			LOG(("API Error: "
				"channels.channelParticipantsNotModified received!"));
		}
	});
}

void ApiWrap::refreshChannelAdmins(
		not_null<ChannelData*> channel,
		const QVector<MTPChannelParticipant> &participants) {
	Data::ChannelAdminChanges changes(channel);
	for (const auto &p : participants) {
		const auto userId = p.match([](const auto &data) {
			return data.vuser_id().v;
		});
		p.match([&](const MTPDchannelParticipantAdmin &data) {
			changes.add(userId, qs(data.vrank().value_or_empty()));
		}, [&](const MTPDchannelParticipantCreator &data) {
			const auto rank = qs(data.vrank().value_or_empty());
			if (const auto info = channel->mgInfo.get()) {
				info->creator = channel->owner().userLoaded(userId);
				info->creatorRank = rank;
			}
			changes.add(userId, rank);
		}, [&](const auto &data) {
			changes.remove(userId);
		});
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
		if (callbackList) {
			callbackList(availableCount, list);
		}
	}, std::move(callbackNotModified));
}

void ApiWrap::applyUpdatesNoPtsCheck(const MTPUpdates &updates) {
	switch (updates.type()) {
	case mtpc_updateShortMessage: {
		const auto &d = updates.c_updateShortMessage();
		const auto flags = mtpCastFlags(d.vflags().v)
			| MTPDmessage::Flag::f_from_id;
		const auto peerUserId = d.is_out()
			? d.vuser_id()
			: MTP_int(_session->userId());
		const auto fwd = d.vfwd_from();
		_session->data().addNewMessage(
			MTP_message(
				MTP_flags(flags),
				d.vid(),
				d.is_out() ? MTP_int(_session->userId()) : d.vuser_id(),
				MTP_peerUser(peerUserId),
				fwd ? (*fwd) : MTPMessageFwdHeader(),
				MTP_int(d.vvia_bot_id().value_or_empty()),
				MTP_int(d.vreply_to_msg_id().value_or_empty()),
				d.vdate(),
				d.vmessage(),
				MTP_messageMediaEmpty(),
				MTPReplyMarkup(),
				MTP_vector<MTPMessageEntity>(d.ventities().value_or_empty()),
				MTPint(),
				MTPint(),
				MTPstring(),
				MTPlong(),
				//MTPMessageReactions(),
				MTPVector<MTPRestrictionReason>()),
			MTPDmessage_ClientFlags(),
			NewMessageType::Unread);
	} break;

	case mtpc_updateShortChatMessage: {
		const auto &d = updates.c_updateShortChatMessage();
		const auto flags = mtpCastFlags(d.vflags().v) | MTPDmessage::Flag::f_from_id;
		const auto fwd = d.vfwd_from();
		_session->data().addNewMessage(
			MTP_message(
				MTP_flags(flags),
				d.vid(),
				d.vfrom_id(),
				MTP_peerChat(d.vchat_id()),
				fwd ? (*fwd) : MTPMessageFwdHeader(),
				MTP_int(d.vvia_bot_id().value_or_empty()),
				MTP_int(d.vreply_to_msg_id().value_or_empty()),
				d.vdate(),
				d.vmessage(),
				MTP_messageMediaEmpty(),
				MTPReplyMarkup(),
				MTP_vector<MTPMessageEntity>(d.ventities().value_or_empty()),
				MTPint(),
				MTPint(),
				MTPstring(),
				MTPlong(),
				//MTPMessageReactions(),
				MTPVector<MTPRestrictionReason>()),
			MTPDmessage_ClientFlags(),
			NewMessageType::Unread);
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
		if (d.vmessage().type() == mtpc_message) { // index forwarded messages to links _overview
			if (_session->data().checkEntitiesAndViewsUpdate(d.vmessage().c_message())) { // already in blocks
				LOG(("Skipping message, because it is already in blocks!"));
				needToAdd = false;
			}
		}
		if (needToAdd) {
			_session->data().addNewMessage(
				d.vmessage(),
				MTPDmessage_ClientFlags(),
				NewMessageType::Unread);
		}
	} break;

	case mtpc_updateReadMessagesContents: {
		const auto &d = update.c_updateReadMessagesContents();
		auto possiblyReadMentions = base::flat_set<MsgId>();
		for (const auto &msgId : d.vmessages().v) {
			if (const auto item = _session->data().message(NoChannel, msgId.v)) {
				if (item->isUnreadMedia() || item->isUnreadMention()) {
					item->markMediaRead();
					_session->data().requestItemRepaint(item);

					if (item->out()
						&& item->history()->peer->isUser()
						&& !App::main()->requestingDifference()) {
						item->history()->peer->asUser()->madeAction(base::unixtime::now());
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
		const auto &d = update.c_updateReadHistoryInbox();
		const auto peer = peerFromMTP(d.vpeer());
		if (const auto history = _session->data().historyLoaded(peer)) {
			const auto folderId = d.vfolder_id().value_or_empty();
			history->applyInboxReadUpdate(
				folderId,
				d.vmax_id().v,
				d.vstill_unread_count().v);
		}
	} break;

	case mtpc_updateReadHistoryOutbox: {
		const auto &d = update.c_updateReadHistoryOutbox();
		const auto peer = peerFromMTP(d.vpeer());
		if (const auto history = _session->data().historyLoaded(peer)) {
			history->outboxRead(d.vmax_id().v);
			if (!App::main()->requestingDifference()) {
				if (const auto user = history->peer->asUser()) {
					user->madeAction(base::unixtime::now());
				}
			}
		}
	} break;

	case mtpc_updateWebPage: {
		auto &d = update.c_updateWebPage();
		Q_UNUSED(d); // Web page was updated anyway.
	} break;

	case mtpc_updateFolderPeers: {
		const auto &data = update.c_updateFolderPeers();
		auto &owner = _session->data();
		for (const auto &peer : data.vfolder_peers().v) {
			peer.match([&](const MTPDfolderPeer &data) {
				const auto peerId = peerFromMTP(data.vpeer());
				if (const auto history = owner.historyLoaded(peerId)) {
					if (const auto folderId = data.vfolder_id().v) {
						history->setFolder(owner.folder(folderId));
					} else {
						history->clearFolder();
					}
				}
			});
		}
	} break;

	case mtpc_updateDeleteMessages: {
		auto &d = update.c_updateDeleteMessages();
		_session->data().processMessagesDeleted(NoChannel, d.vmessages().v);
	} break;

	case mtpc_updateNewChannelMessage: {
		auto &d = update.c_updateNewChannelMessage();
		auto needToAdd = true;
		if (d.vmessage().type() == mtpc_message) { // index forwarded messages to links _overview
			if (_session->data().checkEntitiesAndViewsUpdate(d.vmessage().c_message())) { // already in blocks
				LOG(("Skipping message, because it is already in blocks!"));
				needToAdd = false;
			}
		}
		if (needToAdd) {
			_session->data().addNewMessage(
				d.vmessage(),
				MTPDmessage_ClientFlags(),
				NewMessageType::Unread);
		}
	} break;

	case mtpc_updateEditChannelMessage: {
		auto &d = update.c_updateEditChannelMessage();
		_session->data().updateEditedMessage(d.vmessage());
	} break;

	case mtpc_updateEditMessage: {
		auto &d = update.c_updateEditMessage();
		_session->data().updateEditedMessage(d.vmessage());
	} break;

	case mtpc_updateChannelWebPage: {
		auto &d = update.c_updateChannelWebPage();
		Q_UNUSED(d); // Web page was updated anyway.
	} break;

	case mtpc_updateDeleteChannelMessages: {
		auto &d = update.c_updateDeleteChannelMessages();
		_session->data().processMessagesDeleted(d.vchannel_id().v, d.vmessages().v);
	} break;

	default: Unexpected("Type in applyUpdateNoPtsCheck()");
	}
}

void ApiWrap::jumpToDate(Dialogs::Key chat, const QDate &date) {
	if (const auto peer = chat.peer()) {
		jumpToHistoryDate(peer, date);
	//} else if (const auto feed = chat.feed()) { // #feed
	//	jumpToFeedDate(feed, date);
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
		=,
		callback = std::forward<Callback>(callback)
	](const MTPmessages_Messages &result) {
		auto getMessagesList = [&]() -> const QVector<MTPMessage>* {
			auto handleMessages = [&](auto &messages) {
				_session->data().processUsers(messages.vusers());
				_session->data().processChats(messages.vchats());
				return &messages.vmessages().v;
			};
			switch (result.type()) {
			case mtpc_messages_messages:
				return handleMessages(result.c_messages_messages());
			case mtpc_messages_messagesSlice:
				return handleMessages(result.c_messages_messagesSlice());
			case mtpc_messages_channelMessages: {
				auto &messages = result.c_messages_channelMessages();
				if (peer && peer->isChannel()) {
					peer->asChannel()->ptsReceived(messages.vpts().v);
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

		if (const auto list = getMessagesList()) {
			_session->data().processMessages(*list, NewMessageType::Existing);
			for (const auto &message : *list) {
				if (DateFromMessage(message) >= offsetDate) {
					callback(IdFromMessage(message));
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
// // #feed
//template <typename Callback>
//void ApiWrap::requestMessageAfterDate(
//		not_null<Data::Feed*> feed,
//		const QDate &date,
//		Callback &&callback) {
//	const auto offsetId = 0;
//	const auto offsetDate = static_cast<TimeId>(QDateTime(date).toTime_t());
//	const auto addOffset = -2;
//	const auto limit = 1;
//	const auto hash = 0;
//	request(MTPchannels_GetFeed(
//		MTP_flags(MTPchannels_GetFeed::Flag::f_offset_position),
//		MTP_int(feed->id()),
//		MTP_feedPosition(
//			MTP_int(offsetDate),
//			MTP_peerUser(MTP_int(_session->userId())),
//			MTP_int(0)),
//		MTP_int(addOffset),
//		MTP_int(limit),
//		MTPfeedPosition(), // max_id
//		MTPfeedPosition(), // min_id
//		MTP_int(hash)
//	)).done([
//		=,
//		callback = std::forward<Callback>(callback)
//	](const MTPmessages_FeedMessages &result) {
//		if (result.type() == mtpc_messages_feedMessagesNotModified) {
//			LOG(("API Error: "
//				"Unexpected messages.feedMessagesNotModified."));
//			callback(Data::UnreadMessagePosition);
//			return;
//		}
//		Assert(result.type() == mtpc_messages_feedMessages);
//		const auto &data = result.c_messages_feedMessages();
//		const auto &messages = data.vmessages().v;
//		const auto type = NewMessageExisting;
//		_session->data().processUsers(data.vusers());
//		_session->data().processChats(data.vchats());
//		for (const auto &msg : messages) {
//			if (const auto item = _session->data().addNewMessage(msg, type)) {
//				if (item->date() >= offsetDate || true) {
//					callback(item->position());
//					return;
//				}
//			}
//		}
//		callback(Data::UnreadMessagePosition);
//	}).send();
//}
//
//void ApiWrap::jumpToFeedDate(not_null<Data::Feed*> feed, const QDate &date) {
//	requestMessageAfterDate(feed, date, [=](Data::MessagePosition result) {
//		Ui::hideLayer();
//		App::wnd()->sessionController()->showSection(
//			HistoryFeed::Memento(feed, result));
//	});
//}

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
		requestMessageData(channel, msgId, [=](
				ChannelData *channel,
				MsgId msgId) {
			if (const auto item = _session->data().message(channel, msgId)) {
				if (item->mentionsMe()) {
					item->markMediaRead();
				}
			}
		});
	}
}

void ApiWrap::addChatParticipants(
		not_null<PeerData*> peer,
		const std::vector<not_null<UserData*>> &users) {
	if (const auto chat = peer->asChat()) {
		for (const auto user : users) {
			request(MTPmessages_AddChatUser(
				chat->inputChat,
				user->inputUser,
				MTP_int(kForwardMessagesOnAdd)
			)).done([=](const MTPUpdates &result) {
				applyUpdates(result);
			}).fail([=](const RPCError &error) {
				ShowAddParticipantsError(error.type(), peer, { 1, user });
			}).afterDelay(crl::time(5)).send();
		}
	} else if (const auto channel = peer->asChannel()) {
		const auto bot = ranges::find_if(users, &UserData::isBot);
		if (!peer->isMegagroup() && bot != end(users)) {
			ShowAddParticipantsError("USER_BOT", peer, users);
			return;
		}
		auto list = QVector<MTPInputUser>();
		list.reserve(qMin(int(users.size()), int(kMaxUsersPerInvite)));
		const auto send = [&] {
			request(MTPchannels_InviteToChannel(
				channel->inputChannel,
				MTP_vector<MTPInputUser>(list)
			)).done([=](const MTPUpdates &result) {
				applyUpdates(result);
				requestParticipantsCountDelayed(channel);
			}).fail([=](const RPCError &error) {
				ShowAddParticipantsError(error.type(), peer, users);
			}).afterDelay(crl::time(5)).send();
		};
		for (const auto user : users) {
			list.push_back(user->inputUser);
			if (list.size() == kMaxUsersPerInvite) {
				send();
				list.clear();
			}
		}
		if (!list.empty()) {
			send();
		}
	} else {
		Unexpected("User in ApiWrap::addChatParticipants.");
	}
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
	if (!prepared) {
		return;
	}

	auto requestId = request(
		std::move(*prepared)
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
			_session->data().processUsers(d.vusers());
			fullCount = d.vphotos().v.size();
			return &d.vphotos().v;
		} break;

		case mtpc_photos_photosSlice: {
			auto &d = result.c_photos_photosSlice();
			_session->data().processUsers(d.vusers());
			fullCount = d.vcount().v;
			return &d.vphotos().v;
		} break;
		}
		Unexpected("photos.Photos type in userPhotosDone()");
	}();

	auto photoIds = std::vector<PhotoId>();
	photoIds.reserve(photos.size());
	for (auto &photo : photos) {
		if (auto photoData = _session->data().processPhoto(photo)) {
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
//	for (const auto &broadcasts : data.vfeeds().v) {
//		if (broadcasts.type() == mtpc_feedBroadcasts) {
//			const auto &list = broadcasts.c_feedBroadcasts();
//			const auto feedId = list.vfeed_id().v;
//			const auto feed = _session->data().feed(feedId);
//			auto channels = std::vector<not_null<ChannelData*>>();
//			for (const auto &channelId : list.vchannels().v) {
//				channels.push_back(_session->data().channel(channelId.v));
//			}
//			feed->setChannels(std::move(channels));
//		}
//	}
//
//	_session->data().processUsers(data.vusers());
//	_session->data().processChats(data.vchats());
//
//	if (const auto id = data.vnewly_joined_feed()) {
//		_session->data().setDefaultFeedId(id->v);
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
//	const auto &messages = data.vmessages().v;
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
//	_session->data().processUsers(data.vusers());
//	_session->data().processChats(data.vchats());
//	if (!messages.empty()) {
//		ids.reserve(messages.size());
//		for (const auto &msg : messages) {
//			if (const auto item = _session->data().addNewMessage(msg, type)) {
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
//	if (data.vmin_position() && !ids.empty()) {
//		accumulateFrom(
//			noSkipRange.from,
//			Data::FeedPositionFromMTP(*data.vmin_position()));
//	} else if (slice == SliceType::Before) {
//		noSkipRange.from = Data::MinMessagePosition;
//	}
//	if (data.vmax_position() && !ids.empty()) {
//		accumulateTill(
//			noSkipRange.till,
//			Data::FeedPositionFromMTP(*data.vmax_position()));
//	} else if (slice == SliceType::After) {
//		noSkipRange.till = Data::MaxMessagePosition;
//	}
//
//	const auto unreadPosition = [&] {
//		if (data.vread_max_position()) {
//			return Data::FeedPositionFromMTP(*data.vread_max_position());
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

void ApiWrap::sendAction(const SendAction &action) {
	readServerHistory(action.history);
	action.history->getReadyFor(ShowAtTheEndMsgId);
	_sendActions.fire_copy(action);
}

void ApiWrap::forwardMessages(
		HistoryItemsList &&items,
		const SendAction &action,
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
	const auto genClientSideMessage = action.generateLocal && (count < 2);
	const auto history = action.history;
	const auto peer = history->peer;

	readServerHistory(history);

	const auto channelPost = peer->isChannel() && !peer->isMegagroup();
	const auto silentPost = action.options.silent
		|| (channelPost && _session->data().notifySilentPosts(peer));

	auto flags = MTPDmessage::Flags(0);
	auto clientFlags = MTPDmessage_ClientFlags();
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
	if (action.options.scheduled) {
		flags |= MTPDmessage::Flag::f_from_scheduled;
		sendFlags |= MTPmessages_ForwardMessages::Flag::f_schedule_date;
	} else {
		clientFlags |= MTPDmessage_ClientFlag::f_local_history_entry;
	}

	auto forwardFrom = items.front()->history()->peer;
	auto currentGroupId = items.front()->groupId();
	auto ids = QVector<MTPint>();
	auto randomIds = QVector<MTPlong>();
	auto localIds = std::unique_ptr<std::vector<FullMsgId>>();

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
			peer->input,
			MTP_int(action.options.scheduled)
		)).done([=, callback = std::move(successCallback)](
				const MTPUpdates &updates) {
			applyUpdates(updates);
			if (shared && !--shared->requestsLeft) {
				shared->callback();
			}
		}).fail([=, ids = std::move(localIds)](const RPCError &error) {
			if (ids) {
				for (const auto &itemId : *ids) {
					sendMessageFail(error, peer, itemId);
				}
			} else {
				sendMessageFail(error, peer);
			}
		}).afterRequest(
			history->sendRequestId
		).send();

		ids.resize(0);
		randomIds.resize(0);
		localIds = nullptr;
	};

	ids.reserve(count);
	randomIds.reserve(count);
	for (const auto item : items) {
		auto randomId = rand_value<uint64>();
		if (genClientSideMessage) {
			if (const auto message = item->toHistoryMessage()) {
				const auto newId = FullMsgId(
					peerToChannel(peer->id),
					session().data().nextLocalMessageId());
				const auto self = _session->user();
				const auto messageFromId = channelPost
					? UserId(0)
					: peerToUser(self->id);
				const auto messagePostAuthor = channelPost
					? App::peerName(self)
					: QString();
				history->addNewLocalMessage(
					newId.msg,
					flags,
					clientFlags,
					HistoryItem::NewMessageDate(action.options.scheduled),
					messageFromId,
					messagePostAuthor,
					message);
				_session->data().registerMessageRandomId(randomId, newId);
				if (!localIds) {
					localIds = std::make_unique<std::vector<FullMsgId>>();
				}
				localIds->push_back(newId);
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
		const SendAction &action) {
	const auto userId = UserId(0);
	sendSharedContact(phone, firstName, lastName, userId, action);
}

void ApiWrap::shareContact(
		not_null<UserData*> user,
		const SendAction &action) {
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
		action);
}

void ApiWrap::sendSharedContact(
		const QString &phone,
		const QString &firstName,
		const QString &lastName,
		UserId userId,
		const SendAction &action) {
	sendAction(action);

	const auto history = action.history;
	const auto peer = history->peer;

	const auto newId = FullMsgId(
		history->channelId(),
		session().data().nextLocalMessageId());
	const auto channelPost = peer->isChannel() && !peer->isMegagroup();

	auto flags = NewMessageFlags(peer) | MTPDmessage::Flag::f_media;
	auto clientFlags = NewMessageClientFlags();
	if (action.replyTo) {
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
	if (action.options.scheduled) {
		flags |= MTPDmessage::Flag::f_from_scheduled;
	} else {
		clientFlags |= MTPDmessage_ClientFlag::f_local_history_entry;
	}
	const auto messageFromId = channelPost ? 0 : _session->userId();
	const auto messagePostAuthor = channelPost
		? App::peerName(_session->user())
		: QString();
	const auto vcard = QString();
	const auto views = 1;
	const auto item = history->addNewMessage(
		MTP_message(
			MTP_flags(flags),
			MTP_int(newId.msg),
			MTP_int(messageFromId),
			peerToMTP(peer->id),
			MTPMessageFwdHeader(),
			MTPint(),
			MTP_int(action.replyTo),
			MTP_int(HistoryItem::NewMessageDate(action.options.scheduled)),
			MTP_string(),
			MTP_messageMediaContact(
				MTP_string(phone),
				MTP_string(firstName),
				MTP_string(lastName),
				MTP_string(vcard),
				MTP_int(userId)),
			MTPReplyMarkup(),
			MTPVector<MTPMessageEntity>(),
			MTP_int(views),
			MTPint(),
			MTP_string(messagePostAuthor),
			MTPlong(),
			//MTPMessageReactions(),
			MTPVector<MTPRestrictionReason>()),
		clientFlags,
		NewMessageType::Unread);

	const auto media = MTP_inputMediaContact(
		MTP_string(phone),
		MTP_string(firstName),
		MTP_string(lastName),
		MTP_string(vcard));
	auto options = action.options;
	options.silent = _session->data().notifySilentPosts(peer);
	sendMedia(item, media, options);

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
		const SendAction &action) {
	const auto caption = TextWithTags();
	const auto to = fileLoadTaskOptions(action);
	_fileLoader->addTask(std::make_unique<FileLoadTask>(
		result,
		duration,
		waveform,
		to,
		caption));
}

void ApiWrap::editMedia(
		Storage::PreparedList &&list,
		SendMediaType type,
		TextWithTags &&caption,
		const SendAction &action,
		MsgId msgIdToEdit) {
	if (list.files.empty()) return;

	auto &file = list.files.front();
	const auto to = fileLoadTaskOptions(action);
	_fileLoader->addTask(std::make_unique<FileLoadTask>(
		file.path,
		file.content,
		std::move(file.information),
		type,
		to,
		caption,
		nullptr,
		msgIdToEdit));
}

void ApiWrap::sendFiles(
		Storage::PreparedList &&list,
		SendMediaType type,
		TextWithTags &&caption,
		std::shared_ptr<SendingAlbum> album,
		const SendAction &action) {
	const auto haveCaption = !caption.text.isEmpty();
	const auto isAlbum = (album != nullptr);
	const auto compressImages = (type == SendMediaType::Photo);
	if (haveCaption && !list.canAddCaption(isAlbum, compressImages)) {
		auto message = MessageToSend(action.history);
		message.textWithTags = std::move(caption);
		message.action = action;
		message.action.clearDraft = false;
		sendMessage(std::move(message));
		caption = TextWithTags();
	}

	const auto to = fileLoadTaskOptions(action);
	if (album) {
		album->options = to.options;
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
		caption = TextWithTags();
	}
	if (album) {
		_sendingAlbums.emplace(album->groupId, album);
		album->items.reserve(tasks.size());
		for (const auto &task : tasks) {
			album->items.emplace_back(task->id());
		}
	}
	_fileLoader->addTasks(std::move(tasks));
}

void ApiWrap::sendFile(
		const QByteArray &fileContent,
		SendMediaType type,
		const SendAction &action) {
	const auto to = fileLoadTaskOptions(action);
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
		Api::SendOptions options) {
	if (const auto item = _session->data().message(localId)) {
		const auto media = MTP_inputMediaUploadedPhoto(
			MTP_flags(0),
			file,
			MTPVector<MTPInputDocument>(),
			MTP_int(0));
		if (const auto groupId = item->groupId()) {
			uploadAlbumMedia(item, groupId, media);
		} else {
			sendMedia(item, media, options);
		}
	}
}

void ApiWrap::sendUploadedDocument(
		FullMsgId localId,
		const MTPInputFile &file,
		const std::optional<MTPInputFile> &thumb,
		Api::SendOptions options) {
	if (const auto item = _session->data().message(localId)) {
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
				sendMedia(item, media, options);
			}
		}
	}
}

void ApiWrap::editUploadedFile(
		FullMsgId localId,
		const MTPInputFile &file,
		const std::optional<MTPInputFile> &thumb,
		Api::SendOptions options,
		bool isDocument) {
	const auto item = _session->data().message(localId);
	if (!item) {
		return;
	}
	if (!item->media()) {
		return;
	}

	auto sentEntities = TextUtilities::EntitiesToMTP(
		item->originalText().entities,
		TextUtilities::ConvertOption::SkipLocal);

	auto flagsEditMsg = MTPmessages_EditMessage::Flag::f_message | 0;
	flagsEditMsg |= MTPmessages_EditMessage::Flag::f_no_webpage;
	flagsEditMsg |= MTPmessages_EditMessage::Flag::f_entities;
	flagsEditMsg |= MTPmessages_EditMessage::Flag::f_media;

	const auto media = [&]() -> std::optional<MTPInputMedia> {
		if (!isDocument) {
			if (!item->media()->photo()) {
				return std::nullopt;
			}
			return MTP_inputMediaUploadedPhoto(
				MTP_flags(0),
				file,
				MTPVector<MTPInputDocument>(),
				MTP_int(0));
		}

		const auto document = item->media()->document();
		if (!document) {
			return std::nullopt;
		}

		const auto flags = MTPDinputMediaUploadedDocument::Flags(0)
			| (thumb
				? MTPDinputMediaUploadedDocument::Flag::f_thumb
				: MTPDinputMediaUploadedDocument::Flag(0))
			| (item->groupId()
				? MTPDinputMediaUploadedDocument::Flag::f_nosound_video
				: MTPDinputMediaUploadedDocument::Flag(0));
		return MTP_inputMediaUploadedDocument(
			MTP_flags(flags),
			file,
			thumb ? *thumb : MTPInputFile(),
			MTP_string(document->mimeString()),
			ComposeSendingDocumentAttributes(document),
			MTPVector<MTPInputDocument>(),
			MTP_int(0));
	}();

	if (!media) {
		return;
	}

	const auto peer = item->history()->peer;
	request(MTPmessages_EditMessage(
		MTP_flags(flagsEditMsg),
		peer->input,
		MTP_int(item->id),
		MTP_string(item->originalText().text),
		*media,
		MTPReplyMarkup(),
		sentEntities,
		MTP_int(0) // schedule_date
	)).done([=](const MTPUpdates &result) {
		item->clearSavedMedia();
		item->setIsLocalUpdateMedia(true);
		applyUpdates(result);
		item->setIsLocalUpdateMedia(false);
	}).fail([=](const RPCError &error) {
		QString err = error.type();
		if (err == qstr("MESSAGE_NOT_MODIFIED")) {
			item->returnSavedMedia();
			_session->data().sendHistoryChangeNotifications();
		} else if (err == qstr("MEDIA_NEW_INVALID")) {
			item->returnSavedMedia();
			_session->data().sendHistoryChangeNotifications();
			Ui::show(
				Box<InformBox>(tr::lng_edit_media_invalid_file(tr::now)),
				LayerOption::KeepOther);
		} else {
			sendMessageFail(error, peer);
		}
	}).send();
}

void ApiWrap::cancelLocalItem(not_null<HistoryItem*> item) {
	Expects(!IsServerMsgId(item->id));

	if (const auto groupId = item->groupId()) {
		sendAlbumWithCancelled(item, groupId);
	}
}

void ApiWrap::sendMessage(MessageToSend &&message) {
	const auto history = message.action.history;
	const auto peer = history->peer;
	auto &textWithTags = message.textWithTags;

	auto action = message.action;
	action.generateLocal = true;
	sendAction(action);

	if (!peer->canWrite()) {
		return;
	}
	Local::saveRecentSentHashtags(textWithTags.text);

	auto sending = TextWithEntities();
	auto left = TextWithEntities {
		textWithTags.text,
		ConvertTextTagsToEntities(textWithTags.tags)
	};
	auto prepareFlags = Ui::ItemTextOptions(
		history,
		_session->user()).flags;
	TextUtilities::PrepareForSending(left, prepareFlags);

	HistoryItem *lastMessage = nullptr;

	while (TextUtilities::CutPart(sending, left, MaxMessageSize)) {
		auto newId = FullMsgId(
			peerToChannel(peer->id),
			session().data().nextLocalMessageId());
		auto randomId = rand_value<uint64>();

		TextUtilities::Trim(sending);

		_session->data().registerMessageRandomId(randomId, newId);
		_session->data().registerMessageSentData(randomId, peer->id, sending.text);

		MTPstring msgText(MTP_string(sending.text));
		auto flags = NewMessageFlags(peer) | MTPDmessage::Flag::f_entities;
		auto clientFlags = NewMessageClientFlags();
		auto sendFlags = MTPmessages_SendMessage::Flags(0);
		if (action.replyTo) {
			flags |= MTPDmessage::Flag::f_reply_to_msg_id;
			sendFlags |= MTPmessages_SendMessage::Flag::f_reply_to_msg_id;
		}
		MTPMessageMedia media = MTP_messageMediaEmpty();
		if (message.webPageId == CancelledWebPageId) {
			sendFlags |= MTPmessages_SendMessage::Flag::f_no_webpage;
		} else if (message.webPageId) {
			auto page = _session->data().webpage(message.webPageId);
			media = MTP_messageMediaWebPage(
				MTP_webPagePending(
					MTP_long(page->id),
					MTP_int(page->pendingTill)));
			flags |= MTPDmessage::Flag::f_media;
		}
		const auto channelPost = peer->isChannel() && !peer->isMegagroup();
		const auto silentPost = action.options.silent
			|| (channelPost && _session->data().notifySilentPosts(peer));
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
			sendFlags |= MTPmessages_SendMessage::Flag::f_silent;
		}
		auto localEntities = TextUtilities::EntitiesToMTP(sending.entities);
		auto sentEntities = TextUtilities::EntitiesToMTP(sending.entities, TextUtilities::ConvertOption::SkipLocal);
		if (!sentEntities.v.isEmpty()) {
			sendFlags |= MTPmessages_SendMessage::Flag::f_entities;
		}
		if (action.clearDraft) {
			sendFlags |= MTPmessages_SendMessage::Flag::f_clear_draft;
			history->clearCloudDraft();
			history->setSentDraftText(QString());
		}
		auto messageFromId = channelPost ? 0 : _session->userId();
		auto messagePostAuthor = channelPost
			? App::peerName(_session->user())
			: QString();
		if (action.options.scheduled) {
			flags |= MTPDmessage::Flag::f_from_scheduled;
			sendFlags |= MTPmessages_SendMessage::Flag::f_schedule_date;
		} else {
			clientFlags |= MTPDmessage_ClientFlag::f_local_history_entry;
		}
		lastMessage = history->addNewMessage(
			MTP_message(
				MTP_flags(flags),
				MTP_int(newId.msg),
				MTP_int(messageFromId),
				peerToMTP(peer->id),
				MTPMessageFwdHeader(),
				MTPint(),
				MTP_int(action.replyTo),
				MTP_int(
					HistoryItem::NewMessageDate(action.options.scheduled)),
				msgText,
				media,
				MTPReplyMarkup(),
				localEntities,
				MTP_int(1),
				MTPint(),
				MTP_string(messagePostAuthor),
				MTPlong(),
				//MTPMessageReactions(),
				MTPVector<MTPRestrictionReason>()),
			clientFlags,
			NewMessageType::Unread);
		history->sendRequestId = request(MTPmessages_SendMessage(
			MTP_flags(sendFlags),
			peer->input,
			MTP_int(action.replyTo),
			msgText,
			MTP_long(randomId),
			MTPReplyMarkup(),
			sentEntities,
			MTP_int(action.options.scheduled)
		)).done([=](const MTPUpdates &result) {
			applyUpdates(result, randomId);
			history->clearSentDraftText(QString());
		}).fail([=](const RPCError &error) {
			if (error.type() == qstr("MESSAGE_EMPTY")) {
				lastMessage->destroy();
			} else {
				sendMessageFail(error, peer, newId);
			}
			history->clearSentDraftText(QString());
		}).afterRequest(history->sendRequestId
		).send();
	}

	if (const auto main = App::main()) {
		main->finishForwarding(action);
	}
}

void ApiWrap::sendBotStart(not_null<UserData*> bot, PeerData *chat) {
	Expects(bot->isBot());
	Expects(chat == nullptr || !bot->botInfo->startGroupToken.isEmpty());

	if (chat && chat->isChannel() && !chat->isMegagroup()) {
		ShowAddParticipantsError("USER_BOT", chat, { 1, bot });
		return;
	}

	auto &info = bot->botInfo;
	auto &token = chat ? info->startGroupToken : info->startToken;
	if (token.isEmpty()) {
		auto message = ApiWrap::MessageToSend(_session->data().history(bot));
		message.textWithTags = { qsl("/start"), TextWithTags::Tags() };
		sendMessage(std::move(message));
		return;
	}
	const auto randomId = rand_value<uint64>();
	request(MTPmessages_StartBot(
		bot->inputUser,
		chat ? chat->input : MTP_inputPeerEmpty(),
		MTP_long(randomId),
		MTP_string(base::take(token))
	)).done([=](const MTPUpdates &result) {
		applyUpdates(result);
	}).fail([=](const RPCError &error) {
		if (chat) {
			ShowAddParticipantsError(error.type(), chat, { 1, bot });
		}
	}).send();
}

void ApiWrap::sendInlineResult(
		not_null<UserData*> bot,
		not_null<InlineBots::Result*> data,
		const SendAction &action) {
	sendAction(action);

	const auto history = action.history;
	const auto peer = history->peer;
	const auto newId = FullMsgId(
		peerToChannel(peer->id),
		session().data().nextLocalMessageId());
	const auto randomId = rand_value<uint64>();

	auto flags = NewMessageFlags(peer) | MTPDmessage::Flag::f_media;
	auto clientFlags = NewMessageClientFlags();
	auto sendFlags = MTPmessages_SendInlineBotResult::Flag::f_clear_draft | 0;
	if (action.replyTo) {
		flags |= MTPDmessage::Flag::f_reply_to_msg_id;
		sendFlags |= MTPmessages_SendInlineBotResult::Flag::f_reply_to_msg_id;
	}
	bool channelPost = peer->isChannel() && !peer->isMegagroup();
	bool silentPost = action.options.silent
		|| (channelPost && _session->data().notifySilentPosts(peer));
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
		sendFlags |= MTPmessages_SendInlineBotResult::Flag::f_silent;
	}
	if (bot) {
		flags |= MTPDmessage::Flag::f_via_bot_id;
	}
	if (action.options.scheduled) {
		flags |= MTPDmessage::Flag::f_from_scheduled;
		sendFlags |= MTPmessages_SendInlineBotResult::Flag::f_schedule_date;
	} else {
		clientFlags |= MTPDmessage_ClientFlag::f_local_history_entry;
	}

	const auto messageFromId = channelPost ? 0 : _session->userId();
	const auto messagePostAuthor = channelPost
		? App::peerName(_session->user())
		: QString();

	_session->data().registerMessageRandomId(randomId, newId);

	data->addToHistory(
		history,
		flags,
		clientFlags,
		newId.msg,
		messageFromId,
		MTP_int(HistoryItem::NewMessageDate(action.options.scheduled)),
		bot ? peerToUser(bot->id) : 0,
		action.replyTo,
		messagePostAuthor);

	history->clearCloudDraft();
	history->setSentDraftText(QString());

	history->sendRequestId = request(MTPmessages_SendInlineBotResult(
		MTP_flags(sendFlags),
		peer->input,
		MTP_int(action.replyTo),
		MTP_long(randomId),
		MTP_long(data->getQueryId()),
		MTP_string(data->getId()),
		MTP_int(action.options.scheduled)
	)).done([=](const MTPUpdates &result) {
		applyUpdates(result, randomId);
		history->clearSentDraftText(QString());
	}).fail([=](const RPCError &error) {
		sendMessageFail(error, peer, newId);
		history->clearSentDraftText(QString());
	}).afterRequest(history->sendRequestId
	).send();

	if (const auto main = App::main()) {
		main->finishForwarding(action);
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
		const auto item = _session->data().message(localId);
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
			const auto photo = data.vphoto();
			if (!photo || photo->type() != mtpc_photo) {
				failed();
				return;
			}
			const auto &fields = photo->c_photo();
			const auto flags = MTPDinputMediaPhoto::Flags(0)
				| (data.vttl_seconds()
					? MTPDinputMediaPhoto::Flag::f_ttl_seconds
					: MTPDinputMediaPhoto::Flag(0));
			const auto media = MTP_inputMediaPhoto(
				MTP_flags(flags),
				MTP_inputPhoto(
					fields.vid(),
					fields.vaccess_hash(),
					fields.vfile_reference()),
				MTP_int(data.vttl_seconds().value_or_empty()));
			sendAlbumWithUploaded(item, groupId, media);
		} break;

		case mtpc_messageMediaDocument: {
			const auto &data = result.c_messageMediaDocument();
			const auto document = data.vdocument();
			if (!document || document->type() != mtpc_document) {
				failed();
				return;
			}
			const auto &fields = document->c_document();
			const auto flags = MTPDinputMediaDocument::Flags(0)
				| (data.vttl_seconds()
					? MTPDinputMediaDocument::Flag::f_ttl_seconds
					: MTPDinputMediaDocument::Flag(0));
			const auto media = MTP_inputMediaDocument(
				MTP_flags(flags),
				MTP_inputDocument(
					fields.vid(),
					fields.vaccess_hash(),
					fields.vfile_reference()),
				MTP_int(data.vttl_seconds().value_or_empty()));
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
		Api::SendOptions options) {
	const auto randomId = rand_value<uint64>();
	_session->data().registerMessageRandomId(randomId, item->fullId());

	sendMediaWithRandomId(item, media, options, randomId);
}

void ApiWrap::sendMediaWithRandomId(
		not_null<HistoryItem*> item,
		const MTPInputMedia &media,
		Api::SendOptions options,
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
		| (options.silent
			? MTPmessages_SendMedia::Flag::f_silent
			: MTPmessages_SendMedia::Flag(0))
		| (!sentEntities.v.isEmpty()
			? MTPmessages_SendMedia::Flag::f_entities
			: MTPmessages_SendMedia::Flag(0))
		| (options.scheduled
			? MTPmessages_SendMedia::Flag::f_schedule_date
			: MTPmessages_SendMedia::Flag(0));

	const auto peer = history->peer;
	const auto itemId = item->fullId();
	history->sendRequestId = request(MTPmessages_SendMedia(
		MTP_flags(flags),
		peer->input,
		MTP_int(replyTo),
		media,
		MTP_string(caption.text),
		MTP_long(randomId),
		MTPReplyMarkup(),
		sentEntities,
		MTP_int(options.scheduled)
	)).done([=](const MTPUpdates &result) {
		applyUpdates(result);
	}).fail([=](const RPCError &error) {
		sendMessageFail(error, peer, itemId);
	}).afterRequest(
		history->sendRequestId
	).send();
}

void ApiWrap::sendAlbumWithUploaded(
		not_null<HistoryItem*> item,
		const MessageGroupId &groupId,
		const MTPInputMedia &media) {
	const auto localId = item->fullId();
	const auto randomId = rand_value<uint64>();
	_session->data().registerMessageRandomId(randomId, localId);

	const auto albumIt = _sendingAlbums.find(groupId.raw());
	Assert(albumIt != _sendingAlbums.end());
	const auto &album = albumIt->second;
	album->fillMedia(item, media, randomId);
	sendAlbumIfReady(album.get());
}

void ApiWrap::sendAlbumWithCancelled(
		not_null<HistoryItem*> item,
		const MessageGroupId &groupId) {
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
	album->removeItem(item);
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
			sample = _session->data().message(item.msgId);
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
			single.vmedia(),
			album->options,
			single.vrandom_id().v);
		_sendingAlbums.remove(groupId);
		return;
	}
	const auto history = sample->history();
	const auto replyTo = sample->replyToId();
	const auto flags = MTPmessages_SendMultiMedia::Flags(0)
		| (replyTo
			? MTPmessages_SendMultiMedia::Flag::f_reply_to_msg_id
			: MTPmessages_SendMultiMedia::Flag(0))
		| (album->options.silent
			? MTPmessages_SendMultiMedia::Flag::f_silent
			: MTPmessages_SendMultiMedia::Flag(0))
		| (album->options.scheduled
			? MTPmessages_SendMultiMedia::Flag::f_schedule_date
			: MTPmessages_SendMultiMedia::Flag(0));
	const auto peer = history->peer;
	history->sendRequestId = request(MTPmessages_SendMultiMedia(
		MTP_flags(flags),
		peer->input,
		MTP_int(replyTo),
		MTP_vector<MTPInputSingleMedia>(medias),
		MTP_int(album->options.scheduled)
	)).done([=](const MTPUpdates &result) {
		_sendingAlbums.remove(groupId);
		applyUpdates(result);
	}).fail([=](const RPCError &error) {
		if (const auto album = _sendingAlbums.take(groupId)) {
			for (const auto &item : (*album)->items) {
				sendMessageFail(error, peer, item.msgId);
			}
		} else {
			sendMessageFail(error, peer);
		}
	}).afterRequest(
		history->sendRequestId
	).send();
}

FileLoadTo ApiWrap::fileLoadTaskOptions(const SendAction &action) const {
	const auto peer = action.history->peer;
	auto options = action.options;
	if (_session->data().notifySilentPosts(peer)) {
		options.silent = true;
	}
	return FileLoadTo(peer->id, action.options, action.replyTo);
}

void ApiWrap::requestSupportContact(FnMut<void(const MTPUser &)> callback) {
	_supportContactCallbacks.push_back(std::move(callback));
	if (_supportContactCallbacks.size() > 1) {
		return;
	}
	request(MTPhelp_GetSupport(
	)).done([=](const MTPhelp_Support &result) {
		result.match([&](const MTPDhelp_support &data) {
			for (auto &handler : base::take(_supportContactCallbacks)) {
				handler(data.vuser());
			}
		});
	}).fail([=](const RPCError &error) {
		_supportContactCallbacks.clear();
	}).send();
}

void ApiWrap::uploadPeerPhoto(not_null<PeerData*> peer, QImage &&image) {
	peer = peer->migrateToOrMe();
	const auto ready = PreparePeerPhoto(peer->id, std::move(image));

	const auto fakeId = FullMsgId(
		peerToChannel(peer->id),
		session().data().nextLocalMessageId());
	const auto already = ranges::find(
		_peerPhotoUploads,
		peer,
		[](const auto &pair) { return pair.second; });
	if (already != end(_peerPhotoUploads)) {
		_session->uploader().cancel(already->first);
		_peerPhotoUploads.erase(already);
	}
	_peerPhotoUploads.emplace(fakeId, peer);
	_session->uploader().uploadMedia(fakeId, ready);
}

void ApiWrap::photoUploadReady(
		const FullMsgId &msgId,
		const MTPInputFile &file) {
	if (const auto maybePeer = _peerPhotoUploads.take(msgId)) {
		const auto peer = *maybePeer;
		const auto applier = [=](const MTPUpdates &result) {
			applyUpdates(result);
		};
		if (peer->isSelf()) {
			request(MTPphotos_UploadProfilePhoto(
				file
			)).done([=](const MTPphotos_Photo &result) {
				result.match([&](const MTPDphotos_photo &data) {
					_session->data().processPhoto(data.vphoto());
					_session->data().processUsers(data.vusers());
				});
			}).send();
		} else if (const auto chat = peer->asChat()) {
			const auto history = _session->data().history(chat);
			history->sendRequestId = request(MTPmessages_EditChatPhoto(
				chat->inputChat,
				MTP_inputChatUploadedPhoto(file)
			)).done(applier).afterRequest(history->sendRequestId).send();
		} else if (const auto channel = peer->asChannel()) {
			const auto history = _session->data().history(channel);
			history->sendRequestId = request(MTPchannels_EditPhoto(
				channel->inputChannel,
				MTP_inputChatUploadedPhoto(file)
			)).done(applier).afterRequest(history->sendRequestId).send();
		}
	}
}

void ApiWrap::clearPeerPhoto(not_null<PhotoData*> photo) {
	const auto self = _session->user();
	if (self->userpicPhotoId() == photo->id) {
		request(MTPphotos_UpdateProfilePhoto(
			MTP_inputPhotoEmpty()
		)).done([=](const MTPUserProfilePhoto &result) {
			self->setPhoto(result);
		}).send();
	} else if (photo->peer && photo->peer->userpicPhotoId() == photo->id) {
		const auto applier = [=](const MTPUpdates &result) {
			applyUpdates(result);
		};
		if (const auto chat = photo->peer->asChat()) {
			request(MTPmessages_EditChatPhoto(
				chat->inputChat,
				MTP_inputChatPhotoEmpty()
			)).done(applier).send();
		} else if (const auto channel = photo->peer->asChannel()) {
			request(MTPchannels_EditPhoto(
				channel->inputChannel,
				MTP_inputChatPhotoEmpty()
			)).done(applier).send();
		}
	} else {
		request(MTPphotos_DeletePhotos(
			MTP_vector<MTPInputPhoto>(1, photo->mtpInput())
		)).send();
		_session->storage().remove(Storage::UserPhotosRemoveOne(
			self->bareId(),
			photo->id));
	}
}

void ApiWrap::reloadPasswordState() {
	if (_passwordRequestId) {
		return;
	}
	_passwordRequestId = request(MTPaccount_GetPassword(
	)).done([=](const MTPaccount_Password &result) {
		_passwordRequestId = 0;
		result.match([&](const MTPDaccount_password &data) {
			openssl::AddRandomSeed(bytes::make_span(data.vsecure_random().v));
			if (_passwordState) {
				*_passwordState = Core::ParseCloudPasswordState(data);
			} else {
				_passwordState = std::make_unique<Core::CloudPasswordState>(
					Core::ParseCloudPasswordState(data));
			}
			_passwordStateChanges.fire_copy(*_passwordState);
		});
	}).fail([=](const RPCError &error) {
		_passwordRequestId = 0;
	}).send();
}

void ApiWrap::clearUnconfirmedPassword() {
	_passwordRequestId = request(MTPaccount_CancelPasswordEmail(
	)).done([=](const MTPBool &result) {
		_passwordRequestId = 0;
		reloadPasswordState();
	}).fail([=](const RPCError &error) {
		_passwordRequestId = 0;
		reloadPasswordState();
	}).send();
}

rpl::producer<Core::CloudPasswordState> ApiWrap::passwordState() const {
	return _passwordState
		? _passwordStateChanges.events_starting_with_copy(*_passwordState)
		: (_passwordStateChanges.events() | rpl::type_erased());
}

auto ApiWrap::passwordStateCurrent() const
-> std::optional<Core::CloudPasswordState> {
	return _passwordState
		? base::make_optional(*_passwordState)
		: std::nullopt;
}

void ApiWrap::reloadContactSignupSilent() {
	if (_contactSignupSilentRequestId) {
		return;
	}
	const auto requestId = request(MTPaccount_GetContactSignUpNotification(
	)).done([=](const MTPBool &result) {
		_contactSignupSilentRequestId = 0;
		const auto silent = mtpIsTrue(result);
		_contactSignupSilent = silent;
		_contactSignupSilentChanges.fire_copy(silent);
	}).fail([=](const RPCError &error) {
		_contactSignupSilentRequestId = 0;
	}).send();
	_contactSignupSilentRequestId = requestId;
}

rpl::producer<bool> ApiWrap::contactSignupSilent() const {
	return _contactSignupSilent
		? _contactSignupSilentChanges.events_starting_with_copy(
			*_contactSignupSilent)
		: (_contactSignupSilentChanges.events() | rpl::type_erased());
}

std::optional<bool> ApiWrap::contactSignupSilentCurrent() const {
	return _contactSignupSilent;
}

void ApiWrap::saveContactSignupSilent(bool silent) {
	request(base::take(_contactSignupSilentRequestId)).cancel();

	const auto requestId = request(MTPaccount_SetContactSignUpNotification(
		MTP_bool(silent)
	)).done([=](const MTPBool &) {
		_contactSignupSilentRequestId = 0;
		_contactSignupSilent = silent;
		_contactSignupSilentChanges.fire_copy(silent);
	}).fail([=](const RPCError &error) {
		_contactSignupSilentRequestId = 0;
	}).send();
	_contactSignupSilentRequestId = requestId;
}

void ApiWrap::saveSelfBio(const QString &text, FnMut<void()> done) {
	if (_saveBioRequestId) {
		if (text != _saveBioText) {
			request(_saveBioRequestId).cancel();
		} else {
			if (done) {
				_saveBioDone = std::move(done);
			}
			return;
		}
	}
	_saveBioText = text;
	_saveBioDone = std::move(done);
	_saveBioRequestId = request(MTPaccount_UpdateProfile(
		MTP_flags(MTPaccount_UpdateProfile::Flag::f_about),
		MTPstring(),
		MTPstring(),
		MTP_string(text)
	)).done([=](const MTPUser &result) {
		_saveBioRequestId = 0;

		_session->data().processUsers(MTP_vector<MTPUser>(1, result));
		_session->user()->setAbout(_saveBioText);
		if (_saveBioDone) {
			_saveBioDone();
		}
	}).fail([=](const RPCError &error) {
		_saveBioRequestId = 0;
	}).send();
}

void ApiWrap::reloadPrivacy(Privacy::Key key) {
	if (_privacyRequestIds.contains(key)) {
		return;
	}
	const auto requestId = request(MTPaccount_GetPrivacy(
		Privacy::Input(key)
	)).done([=](const MTPaccount_PrivacyRules &result) {
		_privacyRequestIds.erase(key);
		result.match([&](const MTPDaccount_privacyRules &data) {
			_session->data().processUsers(data.vusers());
			_session->data().processChats(data.vchats());
			pushPrivacy(key, data.vrules().v);
		});
	}).fail([=](const RPCError &error) {
		_privacyRequestIds.erase(key);
	}).send();
	_privacyRequestIds.emplace(key, requestId);
}

auto ApiWrap::parsePrivacy(const QVector<MTPPrivacyRule> &rules)
-> Privacy {
	using Option = Privacy::Option;

	// This is simplified version of privacy rules interpretation.
	// But it should be fine for all the apps
	// that use the same subset of features.
	auto result = Privacy();
	auto optionSet = false;
	const auto SetOption = [&](Option option) {
		if (optionSet) return;
		optionSet = true;
		result.option = option;
	};
	auto &always = result.always;
	auto &never = result.never;
	const auto Feed = [&](const MTPPrivacyRule &rule) {
		rule.match([&](const MTPDprivacyValueAllowAll &) {
			SetOption(Option::Everyone);
		}, [&](const MTPDprivacyValueAllowContacts &) {
			SetOption(Option::Contacts);
		}, [&](const MTPDprivacyValueAllowUsers &data) {
			const auto &users = data.vusers().v;
			always.reserve(always.size() + users.size());
			for (const auto userId : users) {
				const auto user = _session->data().user(UserId(userId.v));
				if (!base::contains(never, user)
					&& !base::contains(always, user)) {
					always.emplace_back(user);
				}
			}
		}, [&](const MTPDprivacyValueAllowChatParticipants &data) {
			const auto &chats = data.vchats().v;
			always.reserve(always.size() + chats.size());
			for (const auto chatId : chats) {
				const auto chat = _session->data().chatLoaded(chatId.v);
				const auto peer = chat
					? static_cast<PeerData*>(chat)
					: _session->data().channelLoaded(chatId.v);
				if (peer
					&& !base::contains(never, peer)
					&& !base::contains(always, peer)) {
					always.emplace_back(peer);
				}
			}
		}, [&](const MTPDprivacyValueDisallowContacts &) {
			// not supported
		}, [&](const MTPDprivacyValueDisallowAll &) {
			SetOption(Option::Nobody);
		}, [&](const MTPDprivacyValueDisallowUsers &data) {
			const auto &users = data.vusers().v;
			never.reserve(never.size() + users.size());
			for (const auto userId : users) {
				const auto user = _session->data().user(UserId(userId.v));
				if (!base::contains(always, user)
					&& !base::contains(never, user)) {
					never.emplace_back(user);
				}
			}
		}, [&](const MTPDprivacyValueDisallowChatParticipants &data) {
			const auto &chats = data.vchats().v;
			never.reserve(never.size() + chats.size());
			for (const auto chatId : chats) {
				const auto chat = _session->data().chatLoaded(chatId.v);
				const auto peer = chat
					? static_cast<PeerData*>(chat)
					: _session->data().channelLoaded(chatId.v);
				if (peer
					&& !base::contains(always, peer)
					&& !base::contains(never, peer)) {
					never.emplace_back(peer);
				}
			}
		});
	};
	for (const auto &rule : rules) {
		Feed(rule);
	}
	Feed(MTP_privacyValueDisallowAll()); // disallow by default.
	return result;
}

void ApiWrap::pushPrivacy(
		Privacy::Key key,
		const QVector<MTPPrivacyRule> &rules) {
	const auto &saved = (_privacyValues[key] = parsePrivacy(rules));
	const auto i = _privacyChanges.find(key);
	if (i != end(_privacyChanges)) {
		i->second.fire_copy(saved);
	}
}

auto ApiWrap::privacyValue(Privacy::Key key) -> rpl::producer<Privacy> {
	if (const auto i = _privacyValues.find(key); i != end(_privacyValues)) {
		return _privacyChanges[key].events_starting_with_copy(i->second);
	} else {
		return _privacyChanges[key].events();
	}
}

void ApiWrap::reloadBlockedUsers() {
	if (_blockedUsersRequestId) {
		return;
	}
	_blockedUsersRequestId = request(MTPcontacts_GetBlocked(
		MTP_int(0),
		MTP_int(kBlockedFirstSlice)
	)).done([=](const MTPcontacts_Blocked &result) {
		_blockedUsersRequestId = 0;
		const auto push = [&](
				int count,
				const QVector<MTPContactBlocked> &list) {
			auto slice = BlockedUsersSlice();
			slice.total = std::max(count, list.size());
			slice.list.reserve(list.size());
			for (const auto &contact : list) {
				contact.match([&](const MTPDcontactBlocked &data) {
					const auto user = _session->data().userLoaded(
						data.vuser_id().v);
					if (user) {
						user->setIsBlocked(true);
						slice.list.push_back({ user, data.vdate().v });
					}
				});
			}
			if (!_blockedUsersSlice || *_blockedUsersSlice != slice) {
				_blockedUsersSlice = slice;
				_blockedUsersChanges.fire(std::move(slice));
			}
		};
		result.match([&](const MTPDcontacts_blockedSlice &data) {
			_session->data().processUsers(data.vusers());
			push(data.vcount().v, data.vblocked().v);
		}, [&](const MTPDcontacts_blocked &data) {
			_session->data().processUsers(data.vusers());
			push(0, data.vblocked().v);
		});
	}).fail([=](const RPCError &error) {
		_blockedUsersRequestId = 0;
	}).send();
}

auto ApiWrap::blockedUsersSlice() -> rpl::producer<BlockedUsersSlice> {
	if (!_blockedUsersSlice) {
		reloadBlockedUsers();
	}
	return _blockedUsersSlice
		? _blockedUsersChanges.events_starting_with_copy(*_blockedUsersSlice)
		: (_blockedUsersChanges.events() | rpl::type_erased());
}

void ApiWrap::reloadSelfDestruct() {
	if (_selfDestructRequestId) {
		return;
	}
	_selfDestructRequestId = request(MTPaccount_GetAccountTTL(
	)).done([=](const MTPAccountDaysTTL &result) {
		_selfDestructRequestId = 0;
		result.match([&](const MTPDaccountDaysTTL &data) {
			setSelfDestructDays(data.vdays().v);
		});
	}).fail([=](const RPCError &error) {
		_selfDestructRequestId = 0;
	}).send();
}

rpl::producer<int> ApiWrap::selfDestructValue() const {
	return _selfDestructDays
		? _selfDestructChanges.events_starting_with_copy(*_selfDestructDays)
		: (_selfDestructChanges.events() | rpl::type_erased());
}

void ApiWrap::saveSelfDestruct(int days) {
	request(_selfDestructRequestId).cancel();
	_selfDestructRequestId = request(MTPaccount_SetAccountTTL(
		MTP_accountDaysTTL(MTP_int(days))
	)).done([=](const MTPBool &result) {
		_selfDestructRequestId = 0;
	}).fail([=](const RPCError &result) {
		_selfDestructRequestId = 0;
	}).send();
	setSelfDestructDays(days);
}

void ApiWrap::setSelfDestructDays(int days) {
	_selfDestructDays = days;
	_selfDestructChanges.fire_copy(days);
}

void ApiWrap::createPoll(
		const PollData &data,
		const SendAction &action,
		FnMut<void()> done,
		FnMut<void(const RPCError &error)> fail) {
	sendAction(action);

	const auto history = action.history;
	const auto peer = history->peer;
	auto sendFlags = MTPmessages_SendMedia::Flags(0);
	if (action.replyTo) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to_msg_id;
	}
	if (action.clearDraft) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_clear_draft;
		history->clearLocalDraft();
		history->clearCloudDraft();
	}
	const auto channelPost = peer->isChannel() && !peer->isMegagroup();
	const auto silentPost = action.options.silent
		|| (channelPost && _session->data().notifySilentPosts(peer));
	if (silentPost) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
	}
	if (action.options.scheduled) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_schedule_date;
	}

	const auto replyTo = action.replyTo;
	history->sendRequestId = request(MTPmessages_SendMedia(
		MTP_flags(sendFlags),
		peer->input,
		MTP_int(replyTo),
		MTP_inputMediaPoll(PollDataToMTP(&data)),
		MTP_string(),
		MTP_long(rand_value<uint64>()),
		MTPReplyMarkup(),
		MTPVector<MTPMessageEntity>(),
		MTP_int(action.options.scheduled)
	)).done([=, done = std::move(done)](const MTPUpdates &result) mutable {
		applyUpdates(result);
		done();
	}).fail([=, fail = std::move(fail)](const RPCError &error) mutable {
		fail(error);
	}).afterRequest(history->sendRequestId
	).send();
}

void ApiWrap::sendPollVotes(
		FullMsgId itemId,
		const std::vector<QByteArray> &options) {
	if (_pollVotesRequestIds.contains(itemId)) {
		return;
	}
	const auto item = _session->data().message(itemId);
	const auto media = item ? item->media() : nullptr;
	const auto poll = media ? media->poll() : nullptr;
	if (!item) {
		return;
	}

	const auto showSending = poll && !options.empty();
	const auto hideSending = [=] {
		if (showSending) {
			if (const auto item = _session->data().message(itemId)) {
				poll->sendingVote = QByteArray();
				_session->data().requestItemRepaint(item);
			}
		}
	};
	if (showSending) {
		poll->sendingVote = options.front();
		_session->data().requestItemRepaint(item);
	}

	auto prepared = QVector<MTPbytes>();
	prepared.reserve(options.size());
	ranges::transform(
		options,
		ranges::back_inserter(prepared),
		[](const QByteArray &option) { return MTP_bytes(option); });
	const auto requestId = request(MTPmessages_SendVote(
		item->history()->peer->input,
		MTP_int(item->id),
		MTP_vector<MTPbytes>(prepared)
	)).done([=](const MTPUpdates &result) {
		_pollVotesRequestIds.erase(itemId);
		hideSending();
		applyUpdates(result);
	}).fail([=](const RPCError &error) {
		_pollVotesRequestIds.erase(itemId);
		hideSending();
	}).send();
	_pollVotesRequestIds.emplace(itemId, requestId);
}

void ApiWrap::closePoll(not_null<HistoryItem*> item) {
	const auto itemId = item->fullId();
	if (_pollCloseRequestIds.contains(itemId)) {
		return;
	}
	const auto media = item ? item->media() : nullptr;
	const auto poll = media ? media->poll() : nullptr;
	if (!poll) {
		return;
	}

	const auto requestId = request(MTPmessages_EditMessage(
		MTP_flags(MTPmessages_EditMessage::Flag::f_media),
		item->history()->peer->input,
		MTP_int(item->id),
		MTPstring(),
		MTP_inputMediaPoll(PollDataToMTP(poll)),
		MTPReplyMarkup(),
		MTPVector<MTPMessageEntity>(),
		MTP_int(0) // schedule_date
	)).done([=](const MTPUpdates &result) {
		_pollCloseRequestIds.erase(itemId);
		applyUpdates(result);
	}).fail([=](const RPCError &error) {
		_pollCloseRequestIds.erase(itemId);
	}).send();
	_pollCloseRequestIds.emplace(itemId, requestId);
}

void ApiWrap::reloadPollResults(not_null<HistoryItem*> item) {
	const auto itemId = item->fullId();
	if (!IsServerMsgId(item->id)
		|| _pollReloadRequestIds.contains(itemId)) {
		return;
	}
	const auto requestId = request(MTPmessages_GetPollResults(
		item->history()->peer->input,
		MTP_int(item->id)
	)).done([=](const MTPUpdates &result) {
		_pollReloadRequestIds.erase(itemId);
		applyUpdates(result);
	}).fail([=](const RPCError &error) {
		_pollReloadRequestIds.erase(itemId);
	}).send();
	_pollReloadRequestIds.emplace(itemId, requestId);
}

void ApiWrap::readServerHistory(not_null<History*> history) {
	if (history->unreadCount()) {
		readServerHistoryForce(history);
	}
	if (history->unreadMark()) {
		changeDialogUnreadMark(history, false);
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
			if (const auto migrated = _session->data().historyLoaded(migrateFrom)) {
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
// // #feed
//void ApiWrap::readFeed(
//		not_null<Data::Feed*> feed,
//		Data::MessagePosition position) {
//	const auto already = feed->unreadPosition();
//	if (already && already >= position) {
//		return;
//	}
//	feed->setUnreadPosition(position);
//	if (!_feedReadsDelayed.contains(feed)) {
//		if (_feedReadsDelayed.empty()) {
//			_feedReadTimer.callOnce(kFeedReadTimeout);
//		}
//		_feedReadsDelayed.emplace(feed, crl::now() + kFeedReadTimeout);
//	}
//}
//
//void ApiWrap::readFeeds() {
//	auto delay = kFeedReadTimeout;
//	const auto now = crl::now();
//	for (auto i = begin(_feedReadsDelayed); i != end(_feedReadsDelayed);) {
//		const auto feed = i->first;
//		const auto time = i->second;
//		// Clang fails to capture structure-binded feed to lambda :(
//		//const auto [feed, time] = *i;
//		if (time > now) {
//			accumulate_min(delay, time - now);
//			++i;
//		} else if (_feedReadRequests.contains(feed)) {
//			++i;
//		} else {
//			const auto position = feed->unreadPosition();
//			const auto requestId = request(MTPchannels_ReadFeed(
//				MTP_int(feed->id()),
//				MTP_feedPosition(
//					MTP_int(position.date),
//					MTP_peerChannel(MTP_int(position.fullId.channel)),
//					MTP_int(position.fullId.msg))
//			)).done([=](const MTPUpdates &result) {
//				applyUpdates(result);
//				_feedReadRequests.remove(feed);
//			}).fail([=](const RPCError &error) {
//				_feedReadRequests.remove(feed);
//			}).send();
//			_feedReadRequests.emplace(feed, requestId);
//
//			i = _feedReadsDelayed.erase(i);
//		}
//	}
//	if (!_feedReadsDelayed.empty()) {
//		_feedReadTimer.callOnce(delay);
//	}
//}

void ApiWrap::sendReadRequest(not_null<PeerData*> peer, MsgId upTo) {
	const auto requestId = [&] {
		const auto finished = [=] {
			_readRequests.remove(peer);
			if (const auto next = _readRequestsPending.take(peer)) {
				sendReadRequest(peer, *next);
			} else if (const auto history
				= _session->data().historyLoaded(peer)) {
				if (!history->unreadCountKnown()) {
					requestDialogEntry(history);
				}
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
