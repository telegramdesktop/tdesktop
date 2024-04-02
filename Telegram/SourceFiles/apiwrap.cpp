/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "apiwrap.h"

#include "api/api_authorizations.h"
#include "api/api_attached_stickers.h"
#include "api/api_blocked_peers.h"
#include "api/api_chat_links.h"
#include "api/api_chat_participants.h"
#include "api/api_cloud_password.h"
#include "api/api_hash.h"
#include "api/api_invite_links.h"
#include "api/api_media.h"
#include "api/api_peer_colors.h"
#include "api/api_peer_photo.h"
#include "api/api_polls.h"
#include "api/api_sending.h"
#include "api/api_text_entities.h"
#include "api/api_self_destruct.h"
#include "api/api_sensitive_content.h"
#include "api/api_global_privacy.h"
#include "api/api_updates.h"
#include "api/api_user_privacy.h"
#include "api/api_views.h"
#include "api/api_confirm_phone.h"
#include "api/api_unread_things.h"
#include "api/api_ringtones.h"
#include "api/api_transcribes.h"
#include "api/api_premium.h"
#include "api/api_user_names.h"
#include "api/api_websites.h"
#include "data/business/data_shortcut_messages.h"
#include "data/notify/data_notify_settings.h"
#include "data/data_changes.h"
#include "data/data_web_page.h"
#include "data/data_folder.h"
#include "data/data_forum_topic.h"
#include "data/data_forum.h"
#include "data/data_saved_sublist.h"
#include "data/data_search_controller.h"
#include "data/data_scheduled_messages.h"
#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_chat_filters.h"
#include "data/data_histories.h"
#include "core/core_cloud_password.h"
#include "core/application.h"
#include "base/unixtime.h"
#include "base/random.h"
#include "base/call_delayed.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "boxes/add_contact_box.h"
#include "mtproto/mtproto_config.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "main/main_account.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/sticker_set_box.h"
#include "boxes/premium_limits_box.h"
#include "window/notifications_manager.h"
#include "window/window_controller.h"
#include "window/window_lock_widgets.h"
#include "window/window_session_controller.h"
#include "inline_bots/inline_bot_result.h"
#include "chat_helpers/message_field.h"
#include "ui/item_text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/toast/toast.h"
#include "support/support_helper.h"
#include "settings/settings_premium.h"
#include "storage/localimageloader.h"
#include "storage/download_manager_mtproto.h"
#include "storage/file_upload.h"
#include "storage/storage_account.h"

namespace {

// Save draft to the cloud with 1 sec extra delay.
constexpr auto kSaveCloudDraftTimeout = 1000;

constexpr auto kTopPromotionInterval = TimeId(60 * 60);
constexpr auto kTopPromotionMinDelay = TimeId(10);
constexpr auto kSmallDelayMs = 5;
constexpr auto kReadFeaturedSetsTimeout = crl::time(1000);
constexpr auto kFileLoaderQueueStopTimeout = crl::time(5000);
constexpr auto kStickersByEmojiInvalidateTimeout = crl::time(6 * 1000);
constexpr auto kNotifySettingSaveTimeout = crl::time(1000);
constexpr auto kDialogsFirstLoad = 20;
constexpr auto kDialogsPerPage = 500;
constexpr auto kStatsSessionKillTimeout = 10 * crl::time(1000);

using PhotoFileLocationId = Data::PhotoFileLocationId;
using DocumentFileLocationId = Data::DocumentFileLocationId;
using UpdatedFileReferences = Data::UpdatedFileReferences;

[[nodiscard]] TimeId UnixtimeFromMsgId(mtpMsgId msgId) {
	return TimeId(msgId >> 32);
}

[[nodiscard]] std::shared_ptr<ChatHelpers::Show> ShowForPeer(
		not_null<PeerData*> peer) {
	if (const auto window = Core::App().windowFor(peer)) {
		if (const auto controller = window->sessionController()) {
			if (&controller->session() == &peer->session()) {
				return controller->uiShow();
			}
		}
	}
	return nullptr;
}

void ShowChannelsLimitBox(not_null<PeerData*> peer) {
	if (const auto window = Core::App().windowFor(peer)) {
		window->invokeForSessionController(
			&peer->session().account(),
			peer,
			[&](not_null<Window::SessionController*> controller) {
				controller->show(Box(ChannelsLimitBox, &peer->session()));
			});
	}
}

[[nodiscard]] FileLoadTo FileLoadTaskOptions(const Api::SendAction &action) {
	const auto peer = action.history->peer;
	return FileLoadTo(
		peer->id,
		action.options,
		action.replyTo,
		action.replaceMediaOf);
}

} // namespace

ApiWrap::ApiWrap(not_null<Main::Session*> session)
: MTP::Sender(&session->account().mtp())
, _session(session)
, _messageDataResolveDelayed([=] { resolveMessageDatas(); })
, _webPagesTimer([=] { resolveWebPages(); })
, _draftsSaveTimer([=] { saveDraftsToCloud(); })
, _featuredSetsReadTimer([=] { readFeaturedSets(); })
, _dialogsLoadState(std::make_unique<DialogsLoadState>())
, _fileLoader(std::make_unique<TaskQueue>(kFileLoaderQueueStopTimeout))
, _topPromotionTimer([=] { refreshTopPromotion(); })
, _updateNotifyTimer([=] { sendNotifySettingsUpdates(); })
, _statsSessionKillTimer([=] { checkStatsSessions(); })
, _authorizations(std::make_unique<Api::Authorizations>(this))
, _attachedStickers(std::make_unique<Api::AttachedStickers>(this))
, _blockedPeers(std::make_unique<Api::BlockedPeers>(this))
, _cloudPassword(std::make_unique<Api::CloudPassword>(this))
, _selfDestruct(std::make_unique<Api::SelfDestruct>(this))
, _sensitiveContent(std::make_unique<Api::SensitiveContent>(this))
, _globalPrivacy(std::make_unique<Api::GlobalPrivacy>(this))
, _userPrivacy(std::make_unique<Api::UserPrivacy>(this))
, _inviteLinks(std::make_unique<Api::InviteLinks>(this))
, _chatLinks(std::make_unique<Api::ChatLinks>(this))
, _views(std::make_unique<Api::ViewsManager>(this))
, _confirmPhone(std::make_unique<Api::ConfirmPhone>(this))
, _peerPhoto(std::make_unique<Api::PeerPhoto>(this))
, _polls(std::make_unique<Api::Polls>(this))
, _chatParticipants(std::make_unique<Api::ChatParticipants>(this))
, _unreadThings(std::make_unique<Api::UnreadThings>(this))
, _ringtones(std::make_unique<Api::Ringtones>(this))
, _transcribes(std::make_unique<Api::Transcribes>(this))
, _premium(std::make_unique<Api::Premium>(this))
, _usernames(std::make_unique<Api::Usernames>(this))
, _websites(std::make_unique<Api::Websites>(this))
, _peerColors(std::make_unique<Api::PeerColors>(this)) {
	crl::on_main(session, [=] {
		// You can't use _session->lifetime() in the constructor,
		// only queued, because it is not constructed yet.
		_session->data().chatsFilters().changed(
		) | rpl::filter([=] {
			return _session->data().chatsFilters().archiveNeeded();
		}) | rpl::start_with_next([=] {
			requestMoreDialogsIfNeeded();
		}, _session->lifetime());

		setupSupportMode();

		Core::App().settings().proxy().connectionTypeValue(
		) | rpl::start_with_next([=] {
			refreshTopPromotion();
		}, _session->lifetime());
	});
}

ApiWrap::~ApiWrap() = default;

Main::Session &ApiWrap::session() const {
	return *_session;
}

Storage::Account &ApiWrap::local() const {
	return _session->local();
}

Api::Updates &ApiWrap::updates() const {
	return _session->updates();
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
	//request(MTPhelp_GetAppChangelog(
	//	MTP_string(sinceVersion)
	//)).done(
	//	callback
	//).send();
}

void ApiWrap::refreshTopPromotion() {
	const auto now = base::unixtime::now();
	const auto next = (_topPromotionNextRequestTime != 0)
		? _topPromotionNextRequestTime
		: now;
	if (_topPromotionRequestId) {
		getTopPromotionDelayed(now, next);
		return;
	}
	const auto key = [&]() -> std::pair<QString, uint32> {
		if (!Core::App().settings().proxy().isEnabled()) {
			return {};
		}
		const auto &proxy = Core::App().settings().proxy().selected();
		if (proxy.type != MTP::ProxyData::Type::Mtproto) {
			return {};
		}
		return { proxy.host, proxy.port };
	}();
	if (_topPromotionKey == key && now < next) {
		getTopPromotionDelayed(now, next);
		return;
	}
	_topPromotionKey = key;
	_topPromotionRequestId = request(MTPhelp_GetPromoData(
	)).done([=](const MTPhelp_PromoData &result) {
		_topPromotionRequestId = 0;
		topPromotionDone(result);
	}).fail([=] {
		_topPromotionRequestId = 0;
		const auto now = base::unixtime::now();
		const auto next = _topPromotionNextRequestTime = now
			+ kTopPromotionInterval;
		if (!_topPromotionTimer.isActive()) {
			getTopPromotionDelayed(now, next);
		}
	}).send();
}

void ApiWrap::getTopPromotionDelayed(TimeId now, TimeId next) {
	_topPromotionTimer.callOnce(std::min(
		std::max(next - now, kTopPromotionMinDelay),
		kTopPromotionInterval) * crl::time(1000));
};

void ApiWrap::topPromotionDone(const MTPhelp_PromoData &proxy) {
	_topPromotionNextRequestTime = proxy.match([&](const auto &data) {
		return data.vexpires().v;
	});
	getTopPromotionDelayed(
		base::unixtime::now(),
		_topPromotionNextRequestTime);

	proxy.match([&](const MTPDhelp_promoDataEmpty &data) {
		_session->data().setTopPromoted(nullptr, QString(), QString());
	}, [&](const MTPDhelp_promoData &data) {
		_session->data().processChats(data.vchats());
		_session->data().processUsers(data.vusers());
		const auto peerId = peerFromMTP(data.vpeer());
		const auto history = _session->data().history(peerId);
		_session->data().setTopPromoted(
			history,
			data.vpsa_type().value_or_empty(),
			data.vpsa_message().value_or_empty());
	});
}

void ApiWrap::requestDeepLinkInfo(
		const QString &path,
		Fn<void(TextWithEntities message, bool updateRequired)> callback) {
	request(_deepLinkInfoRequestId).cancel();
	_deepLinkInfoRequestId = request(MTPhelp_GetDeepLinkInfo(
		MTP_string(path)
	)).done([=](const MTPhelp_DeepLinkInfo &result) {
		_deepLinkInfoRequestId = 0;
		if (result.type() == mtpc_help_deepLinkInfo) {
			const auto &data = result.c_help_deepLinkInfo();
			callback(TextWithEntities{
				qs(data.vmessage()),
				Api::EntitiesFromMTP(
					_session,
					data.ventities().value_or_empty())
			}, data.is_update_app());
		}
	}).fail([=] {
		_deepLinkInfoRequestId = 0;
	}).send();
}

void ApiWrap::requestTermsUpdate() {
	if (_termsUpdateRequestId) {
		return;
	}
	const auto now = crl::now();
	if (_termsUpdateSendAt && now < _termsUpdateSendAt) {
		base::call_delayed(_termsUpdateSendAt - now, _session, [=] {
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
			_termsUpdateSendAt = crl::now() + std::clamp(
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
			session().lockByTerms(
				Window::TermsLock::FromMTP(_session, fields));
			requestNext(data);
		} break;
		default: Unexpected("Type in requestTermsUpdate().");
		}
	}).fail([=] {
		_termsUpdateRequestId = 0;
		_termsUpdateSendAt = crl::now() + kTermsUpdateTimeoutMin;
		requestTermsUpdate();
	}).send();
}

void ApiWrap::acceptTerms(bytes::const_span id) {
	request(MTPhelp_AcceptTermsOfService(
		MTP_dataJSON(MTP_bytes(id))
	)).done([=] {
		requestTermsUpdate();
	}).send();
}

void ApiWrap::checkChatInvite(
		const QString &hash,
		FnMut<void(const MTPChatInvite &)> done,
		Fn<void(const MTP::Error &)> fail) {
	request(base::take(_checkInviteRequestId)).cancel();
	_checkInviteRequestId = request(MTPmessages_CheckChatInvite(
		MTP_string(hash)
	)).done(std::move(done)).fail(std::move(fail)).send();
}

void ApiWrap::checkFilterInvite(
		const QString &slug,
		FnMut<void(const MTPchatlists_ChatlistInvite &)> done,
		Fn<void(const MTP::Error &)> fail) {
	request(base::take(_checkFilterInviteRequestId)).cancel();
	_checkFilterInviteRequestId = request(
		MTPchatlists_CheckChatlistInvite(MTP_string(slug))
	).done(std::move(done)).fail(std::move(fail)).send();
}

void ApiWrap::savePinnedOrder(Data::Folder *folder) {
	const auto &order = _session->data().pinnedChatsOrder(folder);
	const auto input = [](Dialogs::Key key) {
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

void ApiWrap::savePinnedOrder(not_null<Data::Forum*> forum) {
	const auto &order = _session->data().pinnedChatsOrder(forum);
	const auto input = [](Dialogs::Key key) {
		if (const auto topic = key.topic()) {
			return MTP_int(topic->rootId().bare);
		}
		Unexpected("Key type in pinnedDialogsOrder().");
	};
	auto topics = QVector<MTPint>();
	topics.reserve(order.size());
	ranges::transform(
		order,
		ranges::back_inserter(topics),
		input);
	request(MTPchannels_ReorderPinnedForumTopics(
		MTP_flags(MTPchannels_ReorderPinnedForumTopics::Flag::f_force),
		forum->channel()->inputChannel,
		MTP_vector(topics)
	)).done([=](const MTPUpdates &result) {
		applyUpdates(result);
	}).send();
}

void ApiWrap::savePinnedOrder(not_null<Data::SavedMessages*> saved) {
	const auto &order = _session->data().pinnedChatsOrder(saved);
	const auto input = [](Dialogs::Key key) {
		if (const auto sublist = key.sublist()) {
			return MTP_inputDialogPeer(sublist->peer()->input);
		}
		Unexpected("Key type in pinnedDialogsOrder().");
	};
	auto peers = QVector<MTPInputDialogPeer>();
	peers.reserve(order.size());
	ranges::transform(
		order,
		ranges::back_inserter(peers),
		input);
	request(MTPmessages_ReorderPinnedSavedDialogs(
		MTP_flags(MTPmessages_ReorderPinnedSavedDialogs::Flag::f_force),
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
	const auto isPinned = history->isPinnedDialog(0);
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
	}).fail([=] {
		_historyArchivedRequests.remove(history);
	}).send();
	_historyArchivedRequests.emplace(history, requestId, callback);
}

void ApiWrap::sendMessageFail(
		const MTP::Error &error,
		not_null<PeerData*> peer,
		uint64 randomId,
		FullMsgId itemId) {
	sendMessageFail(error.type(), peer, randomId, itemId);
}

void ApiWrap::sendMessageFail(
		const QString &error,
		not_null<PeerData*> peer,
		uint64 randomId,
		FullMsgId itemId) {
	const auto show = ShowForPeer(peer);
	if (show && error == u"PEER_FLOOD"_q) {
		show->showBox(
			Ui::MakeInformBox(
				PeerFloodErrorText(&session(), PeerFloodType::Send)),
			Ui::LayerOption::CloseOther);
	} else if (show && error == u"USER_BANNED_IN_CHANNEL"_q) {
		const auto link = Ui::Text::Link(
			tr::lng_cant_more_info(tr::now),
			session().createInternalLinkFull(u"spambot"_q));
		show->showBox(
			Ui::MakeInformBox(
				tr::lng_error_public_groups_denied(
					tr::now,
					lt_more_info,
					link,
					Ui::Text::WithEntities)),
			Ui::LayerOption::CloseOther);
	} else if (error.startsWith(u"SLOWMODE_WAIT_"_q)) {
		const auto chop = u"SLOWMODE_WAIT_"_q.size();
		const auto left = base::StringViewMid(error, chop).toInt();
		if (const auto channel = peer->asChannel()) {
			const auto seconds = channel->slowmodeSeconds();
			if (seconds >= left) {
				channel->growSlowmodeLastMessage(
					base::unixtime::now() - (left - seconds));
			} else {
				requestFullPeer(peer);
			}
		}
	} else if (error == u"SCHEDULE_STATUS_PRIVATE"_q) {
		auto &scheduled = _session->data().scheduledMessages();
		Assert(peer->isUser());
		if (const auto item = scheduled.lookupItem(peer->id, itemId.msg)) {
			scheduled.removeSending(item);
			if (show) {
				show->showBox(
					Ui::MakeInformBox(tr::lng_cant_do_this()),
					Ui::LayerOption::CloseOther);
			}
		}
	} else if (show && error == u"CHAT_FORWARDS_RESTRICTED"_q) {
		show->showToast(peer->isBroadcast()
			? tr::lng_error_noforwards_channel(tr::now)
			: tr::lng_error_noforwards_group(tr::now), kJoinErrorDuration);
	} else if (error == u"PREMIUM_ACCOUNT_REQUIRED"_q) {
		Settings::ShowPremium(&session(), "premium_stickers");
	}
	if (const auto item = _session->data().message(itemId)) {
		Assert(randomId != 0);
		_session->data().unregisterMessageRandomId(randomId);
		item->sendFailed();

		if (error == u"TOPIC_CLOSED"_q) {
			if (const auto topic = item->topic()) {
				topic->setClosed(true);
			}
		}
	}
}

void ApiWrap::requestMessageData(
		PeerData *peer,
		MsgId msgId,
		Fn<void()> done) {
	auto &requests = (peer && peer->isChannel())
		? _channelMessageDataRequests[peer->asChannel()][msgId]
		: _messageDataRequests[msgId];
	if (done) {
		requests.callbacks.push_back(std::move(done));
	}
	if (!requests.requestId) {
		_messageDataResolveDelayed.call();
	}
}

QVector<MTPInputMessage> ApiWrap::collectMessageIds(
		const MessageDataRequests &requests) {
	auto result = QVector<MTPInputMessage>();
	result.reserve(requests.size());
	for (const auto &[msgId, request] : requests) {
		if (request.requestId > 0) {
			continue;
		}
		result.push_back(MTP_inputMessageID(MTP_int(msgId)));
	}
	return result;
}

auto ApiWrap::messageDataRequests(ChannelData *channel, bool onlyExisting)
-> MessageDataRequests* {
	if (!channel) {
		return &_messageDataRequests;
	}
	const auto i = _channelMessageDataRequests.find(channel);
	if (i != end(_channelMessageDataRequests)) {
		return &i->second;
	} else if (onlyExisting) {
		return nullptr;
	}
	return &_channelMessageDataRequests.emplace(
		channel,
		MessageDataRequests()
	).first->second;
}

void ApiWrap::resolveMessageDatas() {
	if (_messageDataRequests.empty() && _channelMessageDataRequests.empty()) {
		return;
	}

	const auto ids = collectMessageIds(_messageDataRequests);
	if (!ids.isEmpty()) {
		const auto requestId = request(MTPmessages_GetMessages(
			MTP_vector<MTPInputMessage>(ids)
		)).done([=](
				const MTPmessages_Messages &result,
				mtpRequestId requestId) {
			_session->data().processExistingMessages(nullptr, result);
			finalizeMessageDataRequest(nullptr, requestId);
		}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
			finalizeMessageDataRequest(nullptr, requestId);
		}).afterDelay(kSmallDelayMs).send();

		for (auto &[msgId, request] : _messageDataRequests) {
			if (request.requestId > 0) {
				continue;
			}
			request.requestId = requestId;
		}
	}
	for (auto j = _channelMessageDataRequests.begin(); j != _channelMessageDataRequests.cend();) {
		if (j->second.empty()) {
			j = _channelMessageDataRequests.erase(j);
			continue;
		}
		const auto ids = collectMessageIds(j->second);
		if (!ids.isEmpty()) {
			const auto channel = j->first;
			const auto requestId = request(MTPchannels_GetMessages(
				channel->inputChannel,
				MTP_vector<MTPInputMessage>(ids)
			)).done([=](
					const MTPmessages_Messages &result,
					mtpRequestId requestId) {
				_session->data().processExistingMessages(channel, result);
				finalizeMessageDataRequest(channel, requestId);
			}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
				finalizeMessageDataRequest(channel, requestId);
			}).afterDelay(kSmallDelayMs).send();

			for (auto &[msgId, request] : j->second) {
				if (request.requestId > 0) {
					continue;
				}
				request.requestId = requestId;
			}
		}
		++j;
	}
}

void ApiWrap::finalizeMessageDataRequest(
		ChannelData *channel,
		mtpRequestId requestId) {
	auto requests = messageDataRequests(channel, true);
	if (!requests) {
		return;
	}
	auto callbacks = std::vector<Fn<void()>>();
	for (auto i = requests->begin(); i != requests->cend();) {
		if (i->second.requestId == requestId) {
			auto &list = i->second.callbacks;
			if (callbacks.empty()) {
				callbacks = std::move(list);
			} else {
				callbacks.insert(
					end(callbacks),
					std::make_move_iterator(begin(list)),
					std::make_move_iterator(end(list)));
			}
			i = requests->erase(i);
		} else {
			++i;
		}
	}
	if (channel && requests->empty()) {
		_channelMessageDataRequests.remove(channel);
	}
	for (const auto &callback : callbacks) {
		callback();
	}
}

QString ApiWrap::exportDirectMessageLink(
		not_null<HistoryItem*> item,
		bool inRepliesContext) {
	Expects(item->history()->peer->isChannel());

	const auto itemId = item->fullId();
	const auto channel = item->history()->peer->asChannel();
	const auto fallback = [&] {
		auto linkChannel = channel;
		auto linkItemId = item->id;
		auto linkCommentId = MsgId();
		auto linkThreadId = MsgId();
		auto linkThreadIsTopic = false;
		if (inRepliesContext) {
			linkThreadIsTopic = item->history()->isForum();
			const auto rootId = linkThreadIsTopic
				? item->topicRootId()
				: item->replyToTop();
			if (rootId) {
				const auto root = item->history()->owner().message(
					channel->id,
					rootId);
				const auto sender = root
					? root->discussionPostOriginalSender()
					: nullptr;
				if (sender && sender->hasUsername()) {
					// Comment to a public channel.
					const auto forwarded = root->Get<HistoryMessageForwarded>();
					linkItemId = forwarded->savedFromMsgId;
					if (linkItemId) {
						linkChannel = sender;
						linkCommentId = item->id;
					} else {
						linkItemId = item->id;
					}
				} else {
					// Reply in a thread, maybe comment in a private channel.
					linkThreadId = rootId;
				}
			}
		}
		const auto base = linkChannel->hasUsername()
			? linkChannel->username()
			: "c/" + QString::number(peerToChannel(linkChannel->id).bare);
		const auto post = QString::number(linkItemId.bare);
		const auto query = base
			+ '/'
			+ (linkCommentId
				? (post + "?comment=" + QString::number(linkCommentId.bare))
				: (linkThreadId && !linkThreadIsTopic)
				? (post + "?thread=" + QString::number(linkThreadId.bare))
				: linkThreadId
				? (QString::number(linkThreadId.bare) + '/' + post)
				: post);
		if (linkChannel->hasUsername()
			&& !linkChannel->isMegagroup()
			&& !linkCommentId
			&& !linkThreadId) {
			if (const auto media = item->media()) {
				if (const auto document = media->document()) {
					if (document->isVideoMessage()) {
						return u"https://telesco.pe/"_q + query;
					}
				}
			}
		}
		return session().createInternalLinkFull(query);
	};
	const auto i = _unlikelyMessageLinks.find(itemId);
	const auto current = (i != end(_unlikelyMessageLinks))
		? i->second
		: fallback();
	request(MTPchannels_ExportMessageLink(
		MTP_flags(inRepliesContext
			? MTPchannels_ExportMessageLink::Flag::f_thread
			: MTPchannels_ExportMessageLink::Flag(0)),
		channel->inputChannel,
		MTP_int(item->id)
	)).done([=](const MTPExportedMessageLink &result) {
		const auto link = qs(result.data().vlink());
		if (current != link) {
			_unlikelyMessageLinks.emplace_or_assign(itemId, link);
		}
	}).send();
	return current;
}

QString ApiWrap::exportDirectStoryLink(not_null<Data::Story*> story) {
	const auto storyId = story->fullId();
	const auto peer = story->peer();
	const auto fallback = [&] {
		const auto base = peer->username();
		const auto story = QString::number(storyId.story);
		const auto query = base + "/s/" + story;
		return session().createInternalLinkFull(query);
	};
	const auto i = _unlikelyStoryLinks.find(storyId);
	const auto current = (i != end(_unlikelyStoryLinks))
		? i->second
		: fallback();
	request(MTPstories_ExportStoryLink(
		peer->input,
		MTP_int(story->id())
	)).done([=](const MTPExportedStoryLink &result) {
		const auto link = qs(result.data().vlink());
		if (current != link) {
			_unlikelyStoryLinks.emplace_or_assign(storyId, link);
		}
	}).send();
	return current;
}

void ApiWrap::requestContacts() {
	if (_session->data().contactsLoaded().current() || _contactsRequestId) {
		return;
	}
	_contactsRequestId = request(MTPcontacts_GetContacts(
		MTP_long(0) // hash
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

			const auto userId = UserId(contact.c_contact().vuser_id());
			if (userId == _session->userId()) {
				_session->user()->setIsContact(true);
			}
		}
		_session->data().contactsLoaded() = true;
	}).fail([=] {
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
	const auto hash = uint64(0);
	state->requestId = request(MTPmessages_GetDialogs(
		MTP_flags(flags),
		MTP_int(folder ? folder->id() : 0),
		MTP_int(state->offsetDate),
		MTP_int(state->offsetId),
		(state->offsetPeer
			? state->offsetPeer->input
			: MTP_inputPeerEmpty()),
		MTP_int(loadCount),
		MTP_long(hash)
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

		if (!folder
			&& (!_dialogsLoadState || !_dialogsLoadState->listReceived)) {
			refreshDialogsLoadBlocked();
		}
		requestMoreDialogsIfNeeded();
		_session->data().chatsListChanged(folder);
	}).fail([=] {
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

void ApiWrap::requestMoreDialogsIfNeeded() {
	const auto dialogsReady = !_dialogsLoadState
		|| _dialogsLoadState->listReceived;
	if (_session->data().chatsFilters().loadNextExceptions(dialogsReady)) {
		return;
	} else if (_dialogsLoadState && !_dialogsLoadState->listReceived) {
		if (_dialogsLoadState->requestId) {
			return;
		}
		requestDialogs(nullptr);
	} else if (const auto folder = _session->data().folderLoaded(
			Data::Folder::kId)) {
		if (_session->data().chatsFilters().archiveNeeded()) {
			requestMoreDialogs(folder);
		}
	}
	requestContacts();
	_session->data().shortcutMessages().preloadShortcuts();
}

void ApiWrap::updateDialogsOffset(
		Data::Folder *folder,
		const QVector<MTPDialog> &dialogs,
		const QVector<MTPMessage> &messages) {
	auto lastDate = TimeId(0);
	auto lastPeer = PeerId(0);
	auto lastMsgId = MsgId(0);
	for (const auto &dialog : ranges::views::reverse(dialogs)) {
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
			for (const auto &message : ranges::views::reverse(messages)) {
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
	}).fail([=] {
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

void ApiWrap::requestWallPaper(
		const QString &slug,
		Fn<void(const Data::WallPaper &)> done,
		Fn<void()> fail) {
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
		if (const auto paper = Data::WallPaper::Create(_session, result)) {
			if (const auto done = base::take(_wallPaperDone)) {
				done(*paper);
			}
		} else if (const auto fail = base::take(_wallPaperFail)) {
			fail();
		}
	}).fail([=](const MTP::Error &error) {
		_wallPaperRequestId = 0;
		_wallPaperSlug = QString();
		if (const auto fail = base::take(_wallPaperFail)) {
			fail();
		}
	}).send();
}

void ApiWrap::requestFullPeer(not_null<PeerData*> peer) {
	if (_fullPeerRequests.contains(peer)) {
		return;
	}

	const auto requestId = [&] {
		const auto failHandler = [=](const MTP::Error &error) {
			_fullPeerRequests.remove(peer);
			migrateFail(peer, error.type());
		};
		if (const auto user = peer->asUser()) {
			if (_session->supportMode()) {
				_session->supportHelper().refreshInfo(user);
			}
			return request(MTPusers_GetFullUser(
				user->inputUser
			)).done([=](const MTPusers_UserFull &result) {
				result.match([&](const MTPDusers_userFull &data) {
					_session->data().processUsers(data.vusers());
					_session->data().processChats(data.vchats());
				});
				gotUserFull(user, result);
			}).fail(failHandler).send();
		} else if (const auto chat = peer->asChat()) {
			return request(MTPmessages_GetFullChat(
				chat->inputChat
			)).done([=](const MTPmessages_ChatFull &result) {
				gotChatFull(peer, result);
			}).fail(failHandler).send();
		} else if (const auto channel = peer->asChannel()) {
			return request(MTPchannels_GetFullChannel(
				channel->inputChannel
			)).done([=](const MTPmessages_ChatFull &result) {
				gotChatFull(peer, result);
				migrateDone(channel, channel);
			}).fail(failHandler).send();
		}
		Unexpected("Peer type in requestFullPeer.");
	}();
	_fullPeerRequests.emplace(peer, requestId);
}

void ApiWrap::processFullPeer(
		not_null<PeerData*> peer,
		const MTPmessages_ChatFull &result) {
	gotChatFull(peer, result);
}

void ApiWrap::gotChatFull(
		not_null<PeerData*> peer,
		const MTPmessages_ChatFull &result) {
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

	_fullPeerRequests.remove(peer);
	_session->changes().peerUpdated(
		peer,
		Data::PeerUpdate::Flag::FullInfo);
}

void ApiWrap::gotUserFull(
		not_null<UserData*> user,
		const MTPusers_UserFull &result) {
	result.match([&](const MTPDusers_userFull &data) {
		data.vfull_user().match([&](const MTPDuserFull &fields) {
			if (user == _session->user() && !_session->validateSelf(fields.vid().v)) {
				constexpr auto kRequestUserAgainTimeout = crl::time(10000);
				base::call_delayed(kRequestUserAgainTimeout, _session, [=] {
					requestFullPeer(user);
				});
				return;
			}
			Data::ApplyUserUpdate(user, fields);
		});
	});
	_fullPeerRequests.remove(user);
	_session->changes().peerUpdated(
		user,
		Data::PeerUpdate::Flag::FullInfo);
}

void ApiWrap::requestPeerSettings(not_null<PeerData*> peer) {
	if (!_requestedPeerSettings.emplace(peer).second) {
		return;
	}
	request(MTPmessages_GetPeerSettings(
		peer->input
	)).done([=](const MTPmessages_PeerSettings &result) {
		result.match([&](const MTPDmessages_peerSettings &data) {
			_session->data().processUsers(data.vusers());
			_session->data().processChats(data.vchats());
			peer->setBarSettings(data.vsettings());
			_requestedPeerSettings.erase(peer);
		});
	}).fail([=] {
		_requestedPeerSettings.erase(peer);
	}).send();
}

void ApiWrap::migrateChat(
		not_null<ChatData*> chat,
		FnMut<void(not_null<ChannelData*>)> done,
		Fn<void(const QString &)> fail) {
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
		session().changes().peerUpdated(
			chat,
			Data::PeerUpdate::Flag::Migration);
		crl::on_main([=] {
			migrateDone(chat, channel);
		});
	} else if (chat->isDeactivated()) {
		crl::on_main([=] {
			migrateFail(
				chat,
				MTP::Error::Local(
					"BAD_MIGRATION",
					"Chat is already deactivated").type());
		});
		return;
	} else if (!chat->amCreator()) {
		crl::on_main([=] {
			migrateFail(
				chat,
				MTP::Error::Local(
					"BAD_MIGRATION",
					"Current user is not the creator of that chat").type());
		});
		return;
	}

	request(MTPmessages_MigrateChat(
		chat->inputChat
	)).done([=](const MTPUpdates &result) {
		applyUpdates(result);
		session().changes().sendNotifications();

		if (const auto channel = chat->migrateTo()) {
			if (auto handlers = _migrateCallbacks.take(chat)) {
				_migrateCallbacks.emplace(channel, std::move(*handlers));
			}
			requestFullPeer(channel);
		} else {
			migrateFail(
				chat,
				MTP::Error::Local("MIGRATION_FAIL", "No channel").type());
		}
	}).fail([=](const MTP::Error &error) {
		migrateFail(chat, error.type());
	}).send();
}

void ApiWrap::migrateDone(
		not_null<PeerData*> peer,
		not_null<ChannelData*> channel) {
	session().changes().sendNotifications();
	if (auto handlers = _migrateCallbacks.take(peer)) {
		for (auto &handler : *handlers) {
			if (handler.done) {
				handler.done(channel);
			}
		}
	}
}

void ApiWrap::migrateFail(not_null<PeerData*> peer, const QString &error) {
	if (error == u"CHANNELS_TOO_MUCH"_q) {
		ShowChannelsLimitBox(peer);
	}
	if (auto handlers = _migrateCallbacks.take(peer)) {
		for (auto &handler : *handlers) {
			if (handler.fail) {
				handler.fail(error);
			}
		}
	}
}

void ApiWrap::markContentsRead(
		const base::flat_set<not_null<HistoryItem*>> &items) {
	auto markedIds = QVector<MTPint>();
	auto channelMarkedIds = base::flat_map<
		not_null<ChannelData*>,
		QVector<MTPint>>();
	markedIds.reserve(items.size());
	for (const auto &item : items) {
		if (!item->markContentsRead(true) || !item->isRegular()) {
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

void ApiWrap::markContentsRead(not_null<HistoryItem*> item) {
	if (!item->markContentsRead(true) || !item->isRegular()) {
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

void ApiWrap::deleteAllFromParticipant(
		not_null<ChannelData*> channel,
		not_null<PeerData*> from) {
	const auto history = _session->data().historyLoaded(channel);
	const auto ids = history
		? history->collectMessagesFromParticipantToDelete(from)
		: std::vector<MsgId>();
	for (const auto &msgId : ids) {
		if (const auto item = _session->data().message(channel->id, msgId)) {
			item->destroy();
		}
	}

	_session->data().sendHistoryChangeNotifications();

	deleteAllFromParticipantSend(channel, from);
}

void ApiWrap::deleteAllFromParticipantSend(
		not_null<ChannelData*> channel,
		not_null<PeerData*> from) {
	request(MTPchannels_DeleteParticipantHistory(
		channel->inputChannel,
		from->input
	)).done([=](const MTPmessages_AffectedHistory &result) {
		const auto offset = applyAffectedHistory(channel, result);
		if (offset > 0) {
			deleteAllFromParticipantSend(channel, from);
		} else if (const auto history = _session->data().historyLoaded(channel)) {
			history->requestChatListMessage();
		}
	}).send();
}

void ApiWrap::scheduleStickerSetRequest(uint64 setId, uint64 access) {
	if (!_stickerSetRequests.contains(setId)) {
		_stickerSetRequests.emplace(setId, StickerSetRequest{ access });
	}
}

void ApiWrap::requestStickerSets() {
	for (auto &[id, info] : _stickerSetRequests) {
		if (info.id) {
			continue;
		}
		info.id = request(MTPmessages_GetStickerSet(
			MTP_inputStickerSetID(
				MTP_long(id),
				MTP_long(info.accessHash)),
			MTP_int(0) // hash
		)).done([=, setId = id](const MTPmessages_StickerSet &result) {
			gotStickerSet(setId, result);
		}).fail([=, setId = id] {
			_stickerSetRequests.remove(setId);
		}).afterDelay(kSmallDelayMs).send();
	}
}

void ApiWrap::saveStickerSets(
		const Data::StickersSetsOrder &localOrder,
		const Data::StickersSetsOrder &localRemoved,
		Data::StickersType type) {
	auto &setDisenableRequests = (type == Data::StickersType::Emoji)
		? _customEmojiSetDisenableRequests
		: (type == Data::StickersType::Masks)
		? _maskSetDisenableRequests
		: _stickerSetDisenableRequests;
	const auto reorderRequestId = [=]() -> mtpRequestId & {
		return (type == Data::StickersType::Emoji)
			? _customEmojiReorderRequestId
			: (type == Data::StickersType::Masks)
			? _masksReorderRequestId
			: _stickersReorderRequestId;
	};
	for (auto requestId : base::take(setDisenableRequests)) {
		request(requestId).cancel();
	}
	request(base::take(reorderRequestId())).cancel();
	request(base::take(_stickersClearRecentRequestId)).cancel();
	request(base::take(_stickersClearRecentAttachedRequestId)).cancel();

	const auto stickersSaveOrder = [=] {
		if (localOrder.size() < 2) {
			return;
		}
		QVector<MTPlong> mtpOrder;
		mtpOrder.reserve(localOrder.size());
		for (const auto setId : std::as_const(localOrder)) {
			mtpOrder.push_back(MTP_long(setId));
		}

		using Flag = MTPmessages_ReorderStickerSets::Flag;
		const auto flags = (type == Data::StickersType::Emoji)
			? Flag::f_emojis
			: (type == Data::StickersType::Masks)
			? Flag::f_masks
			: Flag(0);
		reorderRequestId() = request(MTPmessages_ReorderStickerSets(
			MTP_flags(flags),
			MTP_vector<MTPlong>(mtpOrder)
		)).done([=] {
			reorderRequestId() = 0;
		}).fail([=] {
			reorderRequestId() = 0;
			if (type == Data::StickersType::Emoji) {
				_session->data().stickers().setLastEmojiUpdate(0);
				updateCustomEmoji();
			} else if (type == Data::StickersType::Masks) {
				_session->data().stickers().setLastMasksUpdate(0);
				updateMasks();
			} else {
				_session->data().stickers().setLastUpdate(0);
				updateStickers();
			}
		}).send();
	};

	const auto stickerSetDisenabled = [=](mtpRequestId requestId) {
		auto &setDisenableRequests = (type == Data::StickersType::Emoji)
			? _customEmojiSetDisenableRequests
			: (type == Data::StickersType::Masks)
			? _maskSetDisenableRequests
			: _stickerSetDisenableRequests;
		setDisenableRequests.remove(requestId);
		if (setDisenableRequests.empty()) {
			stickersSaveOrder();
		}
	};

	auto writeInstalled = true,
		writeRecent = false,
		writeCloudRecent = false,
		writeCloudRecentAttached = false,
		writeFaved = false,
		writeArchived = false;
	auto &recent = _session->data().stickers().getRecentPack();
	auto &sets = _session->data().stickers().setsRef();

	auto &order = (type == Data::StickersType::Emoji)
		? _session->data().stickers().emojiSetsOrder()
		: (type == Data::StickersType::Masks)
		? _session->data().stickers().maskSetsOrder()
		: _session->data().stickers().setsOrder();
	auto &orderRef = (type == Data::StickersType::Emoji)
		? _session->data().stickers().emojiSetsOrderRef()
		: (type == Data::StickersType::Masks)
		? _session->data().stickers().maskSetsOrderRef()
		: _session->data().stickers().setsOrderRef();

	using Flag = Data::StickersSetFlag;
	for (const auto removedSetId : localRemoved) {
		if ((removedSetId == Data::Stickers::CloudRecentSetId)
			|| (removedSetId == Data::Stickers::CloudRecentAttachedSetId)) {
			if (sets.remove(Data::Stickers::CloudRecentSetId) != 0) {
				writeCloudRecent = true;
			}
			if (sets.remove(Data::Stickers::CloudRecentAttachedSetId) != 0) {
				writeCloudRecentAttached = true;
			}
			if (sets.remove(Data::Stickers::CustomSetId)) {
				writeInstalled = true;
			}
			if (!recent.isEmpty()) {
				recent.clear();
				writeRecent = true;
			}

			const auto isAttached =
				(removedSetId == Data::Stickers::CloudRecentAttachedSetId);
			const auto flags = isAttached
				? MTPmessages_ClearRecentStickers::Flag::f_attached
				: MTPmessages_ClearRecentStickers::Flags(0);
			auto &requestId = isAttached
				? _stickersClearRecentAttachedRequestId
				: _stickersClearRecentRequestId;
			const auto finish = [=] {
				(isAttached
					? _stickersClearRecentAttachedRequestId
					: _stickersClearRecentRequestId) = 0;
			};
			requestId = request(MTPmessages_ClearRecentStickers(
				MTP_flags(flags)
			)).done(finish).fail(finish).send();
			continue;
		}

		auto it = sets.find(removedSetId);
		if (it != sets.cend()) {
			const auto set = it->second.get();
			for (auto i = recent.begin(); i != recent.cend();) {
				if (set->stickers.indexOf(i->first) >= 0) {
					i = recent.erase(i);
					writeRecent = true;
				} else {
					++i;
				}
			}
			const auto archived = !!(set->flags & Flag::Archived);
			if (!archived) {
				const auto featured = !!(set->flags & Flag::Featured);
				const auto special = !!(set->flags & Flag::Special);
				const auto emoji = !!(set->flags & Flag::Emoji);
				const auto locked = (set->locked > 0);
				const auto setId = set->mtpInput();

				auto requestId = request(MTPmessages_UninstallStickerSet(
					setId
				)).done([=](const MTPBool &result, mtpRequestId requestId) {
					stickerSetDisenabled(requestId);
				}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
					stickerSetDisenabled(requestId);
				}).afterDelay(kSmallDelayMs).send();

				setDisenableRequests.insert(requestId);

				const auto removeIndex = order.indexOf(set->id);
				if (removeIndex >= 0) {
					orderRef.removeAt(removeIndex);
				}
				if (!featured && !special && !emoji && !locked) {
					sets.erase(it);
				} else {
					if (archived) {
						writeArchived = true;
					}
					set->flags &= ~(Flag::Installed | Flag::Archived);
					set->installDate = TimeId(0);
				}
			}
		}
	}

	// Clear all installed flags, set only for sets from order.
	for (auto &[id, set] : sets) {
		const auto archived = !!(set->flags & Flag::Archived);
		const auto thatType = !!(set->flags & Flag::Emoji)
			? Data::StickersType::Emoji
			: !!(set->flags & Flag::Masks)
			? Data::StickersType::Masks
			: Data::StickersType::Stickers;
		if (!archived && (type == thatType)) {
			set->flags &= ~Flag::Installed;
		}
	}

	orderRef.clear();
	for (const auto setId : std::as_const(localOrder)) {
		auto it = sets.find(setId);
		if (it == sets.cend()) {
			continue;
		}
		const auto set = it->second.get();
		const auto archived = !!(set->flags & Flag::Archived);
		if (archived && !localRemoved.contains(set->id)) {
			const auto mtpSetId = set->mtpInput();

			const auto requestId = request(MTPmessages_InstallStickerSet(
				mtpSetId,
				MTP_boolFalse()
			)).done([=](
					const MTPmessages_StickerSetInstallResult &result,
					mtpRequestId requestId) {
				stickerSetDisenabled(requestId);
			}).fail([=](
					const MTP::Error &error,
					mtpRequestId requestId) {
				stickerSetDisenabled(requestId);
			}).afterDelay(kSmallDelayMs).send();

			setDisenableRequests.insert(requestId);

			set->flags &= ~Flag::Archived;
			writeArchived = true;
		}
		orderRef.push_back(setId);
		set->flags |= Flag::Installed;
		if (!set->installDate) {
			set->installDate = base::unixtime::now();
		}
	}

	for (auto it = sets.begin(); it != sets.cend();) {
		const auto set = it->second.get();
		if ((set->flags & Flag::Featured)
			|| (set->flags & Flag::Installed)
			|| (set->flags & Flag::Archived)
			|| (set->flags & Flag::Special)
			|| (set->flags & Flag::Emoji)
			|| (set->locked > 0)) {
			++it;
		} else {
			it = sets.erase(it);
		}
	}

	auto &storage = local();
	if (writeInstalled) {
		if (type == Data::StickersType::Emoji) {
			storage.writeInstalledCustomEmoji();
		} else if (type == Data::StickersType::Masks) {
			storage.writeInstalledMasks();
		} else {
			storage.writeInstalledStickers();
		}
	}
	if (writeRecent) {
		session().saveSettings();
	}
	if (writeArchived) {
		if (type == Data::StickersType::Emoji) {
		} else if (type == Data::StickersType::Masks) {
			storage.writeArchivedMasks();
		} else {
			storage.writeArchivedStickers();
		}
	}
	if (writeCloudRecent) {
		storage.writeRecentStickers();
	}
	if (writeCloudRecentAttached) {
		storage.writeRecentMasks();
	}
	if (writeFaved) {
		storage.writeFavedStickers();
	}
	_session->data().stickers().notifyUpdated(type);

	if (setDisenableRequests.empty()) {
		stickersSaveOrder();
	} else {
		requestSendDelayed();
	}
}

void ApiWrap::joinChannel(not_null<ChannelData*> channel) {
	if (channel->amIn()) {
		session().changes().peerUpdated(
			channel,
			Data::PeerUpdate::Flag::ChannelAmIn);
	} else if (!_channelAmInRequests.contains(channel)) {
		const auto requestId = request(MTPchannels_JoinChannel(
			channel->inputChannel
		)).done([=](const MTPUpdates &result) {
			_channelAmInRequests.remove(channel);
			applyUpdates(result);
		}).fail([=](const MTP::Error &error) {
			const auto &type = error.type();

			const auto show = ShowForPeer(channel);
			if (type == u"CHANNEL_PRIVATE"_q
				&& channel->invitePeekExpires()) {
				channel->privateErrorReceived();
			} else if (type == u"CHANNELS_TOO_MUCH"_q) {
				ShowChannelsLimitBox(channel);
			} else {
				const auto text = [&] {
					if (type == u"INVITE_REQUEST_SENT"_q) {
						return channel->isMegagroup()
							? tr::lng_group_request_sent(tr::now)
							: tr::lng_group_request_sent_channel(tr::now);
					} else if (type == u"CHANNEL_PRIVATE"_q
						|| type == u"CHANNEL_PUBLIC_GROUP_NA"_q
						|| type == u"USER_BANNED_IN_CHANNEL"_q) {
						return channel->isMegagroup()
							? tr::lng_group_not_accessible(tr::now)
							: tr::lng_channel_not_accessible(tr::now);
					} else if (type == u"USERS_TOO_MUCH"_q) {
						return tr::lng_group_full(tr::now);
					}
					return QString();
				}();
				if (show && !text.isEmpty()) {
					show->showToast(text, kJoinErrorDuration);
				}
			}
			_channelAmInRequests.remove(channel);
		}).send();

		_channelAmInRequests.emplace(channel, requestId);

		using Flag = ChannelDataFlag;
		chatParticipants().loadSimilarChannels(channel);
		channel->setFlags(channel->flags() | Flag::SimilarExpanded);
	}
}

void ApiWrap::leaveChannel(not_null<ChannelData*> channel) {
	if (!channel->amIn()) {
		session().changes().peerUpdated(
			channel,
			Data::PeerUpdate::Flag::ChannelAmIn);
	} else if (!_channelAmInRequests.contains(channel)) {
		auto requestId = request(MTPchannels_LeaveChannel(
			channel->inputChannel
		)).done([=](const MTPUpdates &result) {
			_channelAmInRequests.remove(channel);
			applyUpdates(result);
		}).fail([=] {
			_channelAmInRequests.remove(channel);
		}).send();

		_channelAmInRequests.emplace(channel, requestId);
	}
}

void ApiWrap::requestNotifySettings(const MTPInputNotifyPeer &peer) {
	const auto bad = peer.match([](const MTPDinputNotifyUsers &) {
		return false;
	}, [](const MTPDinputNotifyChats &) {
		return false;
	}, [](const MTPDinputNotifyBroadcasts &) {
		return false;
	}, [&](const MTPDinputNotifyPeer &data) {
		if (data.vpeer().type() == mtpc_inputPeerEmpty) {
			LOG(("Api Error: Requesting settings for empty peer."));
			return true;
		}
		return false;
	}, [&](const MTPDinputNotifyForumTopic &data) {
		if (data.vpeer().type() == mtpc_inputPeerEmpty) {
			LOG(("Api Error: Requesting settings for empty peer topic."));
			return true;
		}
		return false;
	});
	if (bad) {
		return;
	}

	const auto peerFromInput = [&](const MTPInputPeer &inputPeer) {
		return inputPeer.match([&](const MTPDinputPeerSelf &) {
			return _session->userPeerId();
		}, [](const MTPDinputPeerEmpty &) {
			return PeerId(0);
		}, [](const MTPDinputPeerChannel &data) {
			return peerFromChannel(data.vchannel_id());
		}, [](const MTPDinputPeerChat &data) {
			return peerFromChat(data.vchat_id());
		}, [](const MTPDinputPeerUser &data) {
			return peerFromUser(data.vuser_id());
		}, [](const auto &) -> PeerId {
			Unexpected("Type in ApiRequest::requestNotifySettings peer.");
		});
	};
	const auto key = peer.match([](const MTPDinputNotifyUsers &) {
		return NotifySettingsKey{ peerFromUser(1) };
	}, [](const MTPDinputNotifyChats &) {
		return NotifySettingsKey{ peerFromChat(1) };
	}, [](const MTPDinputNotifyBroadcasts &) {
		return NotifySettingsKey{ peerFromChannel(1) };
	}, [&](const MTPDinputNotifyPeer &data) {
		return NotifySettingsKey{ peerFromInput(data.vpeer()) };
	}, [&](const MTPDinputNotifyForumTopic &data) {
		return NotifySettingsKey{
			peerFromInput(data.vpeer()),
			data.vtop_msg_id().v,
		};
	});
	if (_notifySettingRequests.contains(key)) {
		return;
	}
	const auto requestId = request(MTPaccount_GetNotifySettings(
		peer
	)).done([=](const MTPPeerNotifySettings &result) {
		_session->data().notifySettings().apply(peer, result);
		_notifySettingRequests.remove(key);
	}).fail([=] {
		_session->data().notifySettings().apply(
			peer,
			MTP_peerNotifySettings(
				MTP_flags(0),
				MTPBool(),
				MTPBool(),
				MTPint(),
				MTPNotificationSound(),
				MTPNotificationSound(),
				MTPNotificationSound(),
				MTPBool(),
				MTPBool(),
				MTPNotificationSound(),
				MTPNotificationSound(),
				MTPNotificationSound()));
		_notifySettingRequests.erase(key);
	}).send();
	_notifySettingRequests.emplace(key, requestId);
}

void ApiWrap::updateNotifySettingsDelayed(
		not_null<const Data::Thread*> thread) {
	const auto topic = thread->asTopic();
	if (!topic) {
		return updateNotifySettingsDelayed(thread->peer());
	}
	if (_updateNotifyTopics.emplace(topic).second) {
		topic->destroyed(
		) | rpl::start_with_next([=] {
			_updateNotifyTopics.remove(topic);
		}, _updateNotifyQueueLifetime);
		_updateNotifyTimer.callOnce(kNotifySettingSaveTimeout);
	}
}

void ApiWrap::updateNotifySettingsDelayed(not_null<const PeerData*> peer) {
	if (_updateNotifyPeers.emplace(peer).second) {
		_updateNotifyTimer.callOnce(kNotifySettingSaveTimeout);
	}
}

void ApiWrap::updateNotifySettingsDelayed(Data::DefaultNotify type) {
	if (_updateNotifyDefaults.emplace(type).second) {
		_updateNotifyTimer.callOnce(kNotifySettingSaveTimeout);
	}
}

void ApiWrap::sendNotifySettingsUpdates() {
	_updateNotifyQueueLifetime.destroy();
	for (const auto topic : base::take(_updateNotifyTopics)) {
		request(MTPaccount_UpdateNotifySettings(
			MTP_inputNotifyForumTopic(
				topic->channel()->input,
				MTP_int(topic->rootId())),
			topic->notify().serialize()
		)).afterDelay(kSmallDelayMs).send();
	}
	for (const auto peer : base::take(_updateNotifyPeers)) {
		request(MTPaccount_UpdateNotifySettings(
			MTP_inputNotifyPeer(peer->input),
			peer->notify().serialize()
		)).afterDelay(kSmallDelayMs).send();
	}
	const auto &settings = session().data().notifySettings();
	for (const auto type : base::take(_updateNotifyDefaults)) {
		request(MTPaccount_UpdateNotifySettings(
			Data::DefaultNotifyToMTP(type),
			settings.defaultSettings(type).serialize()
		)).afterDelay(kSmallDelayMs).send();
	}
	session().mtp().sendAnything();
}

void ApiWrap::saveDraftToCloudDelayed(not_null<Data::Thread*> thread) {
	_draftsSaveRequestIds.emplace(base::make_weak(thread), 0);
	if (!_draftsSaveTimer.isActive()) {
		_draftsSaveTimer.callOnce(kSaveCloudDraftTimeout);
	}
}

void ApiWrap::updatePrivacyLastSeens() {
	const auto now = base::unixtime::now();
	if (!_session->premium()) {
		_session->data().enumerateUsers([&](not_null<UserData*> user) {
			if (user->isSelf()
				|| !user->isLoaded()
				|| user->lastseen().isHidden()) {
				return;
			}

			const auto till = user->lastseen().onlineTill();
			user->updateLastseen((till + 3 * 86400 >= now)
				? Data::LastseenStatus::Recently(true)
				: (till + 7 * 86400 >= now)
				? Data::LastseenStatus::WithinWeek(true)
				: (till + 30 * 86400 >= now)
				? Data::LastseenStatus::WithinMonth(true)
				: Data::LastseenStatus::LongAgo(true));
			session().changes().peerUpdated(
				user,
				Data::PeerUpdate::Flag::OnlineStatus);
			session().data().maybeStopWatchForOffline(user);
		});
	}

	if (_contactsStatusesRequestId) {
		request(_contactsStatusesRequestId).cancel();
	}
	_contactsStatusesRequestId = request(MTPcontacts_GetStatuses(
	)).done([=](const MTPVector<MTPContactStatus> &result) {
		_contactsStatusesRequestId = 0;
		for (const auto &status : result.v) {
			const auto &data = status.data();
			const auto userId = UserId(data.vuser_id());
			if (const auto user = _session->data().userLoaded(userId)) {
				const auto status = LastseenFromMTP(
					data.vstatus(),
					user->lastseen());
				if (user->updateLastseen(status)) {
					session().changes().peerUpdated(
						user,
						Data::PeerUpdate::Flag::OnlineStatus);
				}
			}
		}
	}).fail([this] {
		_contactsStatusesRequestId = 0;
	}).send();
}

void ApiWrap::clearHistory(not_null<PeerData*> peer, bool revoke) {
	deleteHistory(peer, true, revoke);
}

void ApiWrap::deleteConversation(not_null<PeerData*> peer, bool revoke) {
	if (const auto chat = peer->asChat()) {
		request(MTPmessages_DeleteChatUser(
			MTP_flags(0),
			chat->inputChat,
			_session->user()->inputUser
		)).done([=](const MTPUpdates &result) {
			applyUpdates(result);
			deleteHistory(peer, false, revoke);
		}).fail([=] {
			deleteHistory(peer, false, revoke);
		}).send();
	} else {
		deleteHistory(peer, false, revoke);
	}
}

void ApiWrap::deleteHistory(
		not_null<PeerData*> peer,
		bool justClear,
		bool revoke) {
	auto deleteTillId = MsgId(0);
	const auto history = _session->data().history(peer);
	if (justClear) {
		// In case of clear history we need to know the last server message.
		while (history->lastMessageKnown()) {
			const auto last = history->lastMessage();
			if (!last) {
				// History is empty.
				return;
			} else if (!last->isRegular()) {
				// Destroy client-side message locally.
				last->destroy();
			} else {
				break;
			}
		}
		if (!history->lastMessageKnown()) {
			history->owner().histories().requestDialogEntry(history, [=] {
				Expects(history->lastMessageKnown());

				deleteHistory(peer, justClear, revoke);
			});
			return;
		}
		deleteTillId = history->lastMessage()->id;
	}
	if (const auto channel = peer->asChannel()) {
		if (!justClear && !revoke) {
			channel->ptsWaitingForShortPoll(-1);
			leaveChannel(channel);
		} else {
			if (const auto migrated = peer->migrateFrom()) {
				deleteHistory(migrated, justClear, revoke);
			}
			if (deleteTillId || (!justClear && revoke)) {
				history->owner().histories().deleteAllMessages(
					history,
					deleteTillId,
					justClear,
					revoke);
			}
		}
	} else {
		history->owner().histories().deleteAllMessages(
			history,
			deleteTillId,
			justClear,
			revoke);
	}
	if (!justClear) {
		_session->data().deleteConversationLocally(peer);
	} else if (history) {
		history->clear(History::ClearType::ClearHistory);
	}
}

void ApiWrap::applyUpdates(
		const MTPUpdates &updates,
		uint64 sentMessageRandomId) const {
	this->updates().applyUpdates(updates, sentMessageRandomId);
}

int ApiWrap::applyAffectedHistory(
		PeerData *peer,
		const MTPmessages_AffectedHistory &result) const {
	const auto &data = result.c_messages_affectedHistory();
	if (const auto channel = peer ? peer->asChannel() : nullptr) {
		channel->ptsUpdateAndApply(data.vpts().v, data.vpts_count().v);
	} else {
		updates().updateAndApply(data.vpts().v, data.vpts_count().v);
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
		const MTPmessages_AffectedMessages &result) const {
	const auto &data = result.c_messages_affectedMessages();
	updates().updateAndApply(data.vpts().v, data.vpts_count().v);
}

void ApiWrap::saveCurrentDraftToCloud() {
	Core::App().materializeLocalDrafts();
	for (const auto &controller : _session->windows()) {
		if (const auto thread = controller->activeChatCurrent().thread()) {
			const auto topic = thread->asTopic();
			if (topic && topic->creating()) {
				continue;
			}
			const auto history = thread->owningHistory();
			_session->local().writeDrafts(history);

			const auto topicRootId = thread->topicRootId();
			const auto localDraft = history->localDraft(topicRootId);
			const auto cloudDraft = history->cloudDraft(topicRootId);
			if (!Data::DraftsAreEqual(localDraft, cloudDraft)
				&& !_session->supportMode()) {
				saveDraftToCloudDelayed(thread);
			}
		}
	}
}

void ApiWrap::saveDraftsToCloud() {
	for (auto i = begin(_draftsSaveRequestIds); i != end(_draftsSaveRequestIds);) {
		const auto weak = i->first;
		const auto thread = weak.get();
		if (!thread) {
			i = _draftsSaveRequestIds.erase(i);
			continue;
		} else if (i->second) {
			++i;
			continue; // sent already
		}

		const auto history = thread->owningHistory();
		const auto topicRootId = thread->topicRootId();
		auto cloudDraft = history->cloudDraft(topicRootId);
		auto localDraft = history->localDraft(topicRootId);
		if (cloudDraft && cloudDraft->saveRequestId) {
			request(base::take(cloudDraft->saveRequestId)).cancel();
		}
		if (!_session->supportMode()) {
			cloudDraft = history->createCloudDraft(topicRootId, localDraft);
		} else if (!cloudDraft) {
			cloudDraft = history->createCloudDraft(topicRootId, nullptr);
		}

		auto flags = MTPmessages_SaveDraft::Flags(0);
		auto &textWithTags = cloudDraft->textWithTags;
		if (cloudDraft->webpage.removed) {
			flags |= MTPmessages_SaveDraft::Flag::f_no_webpage;
		} else if (!cloudDraft->webpage.url.isEmpty()) {
			flags |= MTPmessages_SaveDraft::Flag::f_media;
		}
		if (cloudDraft->reply.messageId || cloudDraft->reply.topicRootId) {
			flags |= MTPmessages_SaveDraft::Flag::f_reply_to;
		}
		if (!textWithTags.tags.isEmpty()) {
			flags |= MTPmessages_SaveDraft::Flag::f_entities;
		}
		auto entities = Api::EntitiesToMTP(
			_session,
			TextUtilities::ConvertTextTagsToEntities(textWithTags.tags),
			Api::ConvertOption::SkipLocal);

		history->startSavingCloudDraft(topicRootId);
		cloudDraft->saveRequestId = request(MTPmessages_SaveDraft(
			MTP_flags(flags),
			ReplyToForMTP(history, cloudDraft->reply),
			history->peer->input,
			MTP_string(textWithTags.text),
			entities,
			Data::WebPageForMTP(
				cloudDraft->webpage,
				textWithTags.text.isEmpty())
		)).done([=](const MTPBool &result, const MTP::Response &response) {
			const auto requestId = response.requestId;
			history->finishSavingCloudDraft(
				topicRootId,
				UnixtimeFromMsgId(response.outerMsgId));
			if (const auto cloudDraft = history->cloudDraft(topicRootId)) {
				if (cloudDraft->saveRequestId == requestId) {
					cloudDraft->saveRequestId = 0;
					history->draftSavedToCloud(topicRootId);
				}
			}
			const auto i = _draftsSaveRequestIds.find(weak);
			if (i != _draftsSaveRequestIds.cend()
				&& i->second == requestId) {
				_draftsSaveRequestIds.erase(i);
				checkQuitPreventFinished();
			}
		}).fail([=](const MTP::Error &error, const MTP::Response &response) {
			const auto requestId = response.requestId;
			history->finishSavingCloudDraft(
				topicRootId,
				UnixtimeFromMsgId(response.outerMsgId));
			if (const auto cloudDraft = history->cloudDraft(topicRootId)) {
				if (cloudDraft->saveRequestId == requestId) {
					history->clearCloudDraft(topicRootId);
				}
			}
			const auto i = _draftsSaveRequestIds.find(weak);
			if (i != _draftsSaveRequestIds.cend()
				&& i->second == requestId) {
				_draftsSaveRequestIds.erase(i);
				checkQuitPreventFinished();
			}
		}).send();

		i->second = cloudDraft->saveRequestId;
		++i;
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
		if (Core::Quitting()) {
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

void ApiWrap::gotStickerSet(
		uint64 setId,
		const MTPmessages_StickerSet &result) {
	_stickerSetRequests.remove(setId);
	result.match([&](const MTPDmessages_stickerSet &data) {
		_session->data().stickers().feedSetFull(data);
	}, [](const MTPDmessages_stickerSetNotModified &) {
		LOG(("API Error: Unexpected messages.stickerSetNotModified."));
	});
}

void ApiWrap::requestWebPageDelayed(not_null<WebPageData*> page) {
	if (page->failed || !page->pendingTill) {
		return;
	}
	_webPagesPending.emplace(page, 0);
	auto left = (page->pendingTill - base::unixtime::now()) * 1000;
	if (!_webPagesTimer.isActive() || left <= _webPagesTimer.remainingTime()) {
		_webPagesTimer.callOnce((left < 0 ? 0 : left) + 1);
	}
}

void ApiWrap::clearWebPageRequest(not_null<WebPageData*> page) {
	_webPagesPending.remove(page);
	if (_webPagesPending.empty() && _webPagesTimer.isActive()) {
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
	using MessageIdsByChannel = base::flat_map<ChannelData*, IndexAndMessageIds>;
	MessageIdsByChannel idsByChannel; // temp_req_id = -index - 2

	ids.reserve(_webPagesPending.size());
	int32 t = base::unixtime::now(), m = INT_MAX;
	for (auto &[page, requestId] : _webPagesPending) {
		if (requestId > 0) {
			continue;
		}
		if (page->pendingTill <= t) {
			if (const auto item = _session->data().findWebPageItem(page)) {
				if (const auto channel = item->history()->peer->asChannel()) {
					auto channelMap = idsByChannel.find(channel);
					if (channelMap == idsByChannel.cend()) {
						channelMap = idsByChannel.emplace(
							channel,
							IndexAndMessageIds(
								idsByChannel.size(),
								QVector<MTPInputMessage>(
									1,
									MTP_inputMessageID(MTP_int(item->id))))).first;
					} else {
						channelMap->second.second.push_back(
							MTP_inputMessageID(MTP_int(item->id)));
					}
					requestId = -channelMap->second.first - 2;
				} else {
					ids.push_back(MTP_inputMessageID(MTP_int(item->id)));
					requestId = -1;
				}
			}
		} else {
			m = std::min(m, page->pendingTill - t);
		}
	}

	auto requestId = mtpRequestId(0);
	if (!ids.isEmpty()) {
		requestId = request(MTPmessages_GetMessages(
			MTP_vector<MTPInputMessage>(ids)
		)).done([=](
				const MTPmessages_Messages &result,
				mtpRequestId requestId) {
			gotWebPages(nullptr, result, requestId);
		}).afterDelay(kSmallDelayMs).send();
	}
	QVector<mtpRequestId> reqsByIndex(idsByChannel.size(), 0);
	for (auto i = idsByChannel.cbegin(), e = idsByChannel.cend(); i != e; ++i) {
		reqsByIndex[i->second.first] = request(MTPchannels_GetMessages(
			i->first->inputChannel,
			MTP_vector<MTPInputMessage>(i->second.second)
		)).done([=, channel = i->first](
				const MTPmessages_Messages &result,
				mtpRequestId requestId) {
			gotWebPages(channel, result, requestId);
		}).afterDelay(kSmallDelayMs).send();
	}
	if (requestId || !reqsByIndex.isEmpty()) {
		for (auto &[page, pendingRequestId] : _webPagesPending) {
			if (pendingRequestId > 0) {
				continue;
			} else if (pendingRequestId < 0) {
				if (pendingRequestId == -1) {
					pendingRequestId = requestId;
				} else {
					pendingRequestId = reqsByIndex[-pendingRequestId - 2];
				}
			}
		}
	}
	if (m < INT_MAX) {
		_webPagesTimer.callOnce(std::min(m, 86400) * crl::time(1000));
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
			const auto documentId = std::get_if<DocumentFileLocationId>(
				&origin);
			if (documentId) {
				_session->data().document(
					documentId->id
				)->refreshFileReference(reference);
			}
			const auto photoId = std::get_if<PhotoFileLocationId>(&origin);
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
	}).fail([=] {
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
		not_null<Storage::DownloadMtprotoTask*> task,
		int requestId,
		const QByteArray &current) {
	return refreshFileReference(origin, crl::guard(task, [=](
			const UpdatedFileReferences &data) {
		task->refreshFileReferenceFrom(data, requestId, current);
	}));
}

void ApiWrap::refreshFileReference(
		Data::FileOrigin origin,
		FileReferencesHandler &&handler) {
	const auto fail = [&] {
		handler(UpdatedFileReferences());
	};
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
	v::match(origin.data, [&](Data::FileOriginMessage data) {
		if (const auto item = _session->data().message(data)) {
			const auto media = item->media();
			const auto storyId = media ? media->storyId() : FullStoryId();
			if (storyId) {
				request(MTPstories_GetStoriesByID(
					_session->data().peer(storyId.peer)->input,
					MTP_vector<MTPint>(1, MTP_int(storyId.story))));
			} else if (item->isScheduled()) {
				const auto &scheduled = _session->data().scheduledMessages();
				const auto realId = scheduled.lookupId(item);
				request(MTPmessages_GetScheduledMessages(
					item->history()->peer->input,
					MTP_vector<MTPint>(1, MTP_int(realId))));
			} else if (item->isBusinessShortcut()) {
				const auto &shortcuts = _session->data().shortcutMessages();
				const auto realId = shortcuts.lookupId(item);
				request(MTPmessages_GetQuickReplyMessages(
					MTP_flags(MTPmessages_GetQuickReplyMessages::Flag::f_id),
					MTP_int(item->shortcutId()),
					MTP_vector<MTPint>(1, MTP_int(realId)),
					MTP_long(0)));
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
	}, [&](Data::FileOriginFullUser data) {
		if (const auto user = _session->data().user(data.userId)) {
			request(MTPusers_GetFullUser(user->inputUser));
		} else {
			fail();
		}
	}, [&](Data::FileOriginPeerPhoto data) {
		fail();
	}, [&](Data::FileOriginStickerSet data) {
		const auto isRecentAttached =
			(data.setId == Data::Stickers::CloudRecentAttachedSetId);
		if (data.setId == Data::Stickers::CloudRecentSetId
			|| data.setId == Data::Stickers::RecentSetId
			|| isRecentAttached) {
			auto done = [=] { crl::on_main(_session, [=] {
				if (isRecentAttached) {
					local().writeRecentMasks();
				} else {
					local().writeRecentStickers();
				}
			}); };
			request(MTPmessages_GetRecentStickers(
				MTP_flags(isRecentAttached
					? MTPmessages_GetRecentStickers::Flag::f_attached
					: MTPmessages_GetRecentStickers::Flags(0)),
				MTP_long(0)),
				std::move(done));
		} else if (data.setId == Data::Stickers::FavedSetId) {
			request(MTPmessages_GetFavedStickers(MTP_long(0)),
				[=] { crl::on_main(_session, [=] { local().writeFavedStickers(); }); });
		} else {
			request(MTPmessages_GetStickerSet(
				MTP_inputStickerSetID(
					MTP_long(data.setId),
					MTP_long(data.accessHash)),
				MTP_int(0)), // hash
				[=] { crl::on_main(_session, [=] {
					local().writeInstalledStickers();
					local().writeRecentStickers();
					local().writeFavedStickers();
				}); });
		}
	}, [&](Data::FileOriginSavedGifs data) {
		request(
			MTPmessages_GetSavedGifs(MTP_long(0)),
			[=] { crl::on_main(_session, [=] { local().writeSavedGifs(); }); });
	}, [&](Data::FileOriginWallpaper data) {
		const auto useSlug = data.ownerId
			&& (data.ownerId != session().userId())
			&& !data.slug.isEmpty();
		request(MTPaccount_GetWallPaper(useSlug
			? MTP_inputWallPaperSlug(MTP_string(data.slug))
			: MTP_inputWallPaper(
				MTP_long(data.paperId),
				MTP_long(data.accessHash))));
	}, [&](Data::FileOriginTheme data) {
		request(MTPaccount_GetTheme(
			MTP_string(Data::CloudThemes::Format()),
			MTP_inputTheme(
				MTP_long(data.themeId),
				MTP_long(data.accessHash))));
	}, [&](Data::FileOriginRingtones data) {
		request(MTPaccount_GetSavedRingtones(MTP_long(0)));
	}, [&](Data::FileOriginPremiumPreviews data) {
		request(MTPhelp_GetPremiumPromo());
	}, [&](Data::FileOriginWebPage data) {
		request(MTPmessages_GetWebPage(
			MTP_string(data.url),
			MTP_int(0)));
	}, [&](Data::FileOriginStory data) {
		request(MTPstories_GetStoriesByID(
			_session->data().peer(data.peer)->input,
			MTP_vector<MTPint>(1, MTP_int(data.story))));
	}, [&](v::null_t) {
		fail();
	});
}

void ApiWrap::gotWebPages(ChannelData *channel, const MTPmessages_Messages &result, mtpRequestId req) {
	WebPageData::ApplyChanges(_session, channel, result);
	for (auto i = _webPagesPending.begin(); i != _webPagesPending.cend();) {
		if (i->second == req) {
			if (i->first->pendingTill > 0) {
				i->first->pendingTill = 0;
				i->first->failed = 1;
				_session->data().notifyWebPageUpdateDelayed(i->first);
			}
			i = _webPagesPending.erase(i);
		} else {
			++i;
		}
	}
	_session->data().sendWebPageGamePollNotifications();
}

void ApiWrap::updateStickers() {
	const auto now = crl::now();
	requestStickers(now);
	requestRecentStickers(now);
	requestFavedStickers(now);
	requestFeaturedStickers(now);
}

void ApiWrap::updateSavedGifs() {
	const auto now = crl::now();
	requestSavedGifs(now);
}

void ApiWrap::updateMasks() {
	const auto now = crl::now();
	requestMasks(now);
	requestRecentStickers(now, true);
}

void ApiWrap::updateCustomEmoji() {
	const auto now = crl::now();
	requestCustomEmoji(now);
	requestFeaturedEmoji(now);
}

void ApiWrap::requestRecentStickersForce(bool attached) {
	requestRecentStickersWithHash(0, attached);
}

void ApiWrap::setGroupStickerSet(
		not_null<ChannelData*> megagroup,
		const StickerSetIdentifier &set) {
	Expects(megagroup->mgInfo != nullptr);

	megagroup->mgInfo->stickerSet = set;
	request(MTPchannels_SetStickers(
		megagroup->inputChannel,
		Data::InputStickerSet(set)
	)).send();
	_session->data().stickers().notifyUpdated(Data::StickersType::Stickers);
}

void ApiWrap::setGroupEmojiSet(
		not_null<ChannelData*> megagroup,
		const StickerSetIdentifier &set) {
	Expects(megagroup->mgInfo != nullptr);

	megagroup->mgInfo->emojiSet = set;
	request(MTPchannels_SetEmojiStickers(
		megagroup->inputChannel,
		Data::InputStickerSet(set)
	)).send();
	_session->changes().peerUpdated(
		megagroup,
		Data::PeerUpdate::Flag::EmojiSet);
	_session->data().stickers().notifyUpdated(Data::StickersType::Emoji);
}

std::vector<not_null<DocumentData*>> *ApiWrap::stickersByEmoji(
		const QString &key) {
	const auto it = _stickersByEmoji.find(key);
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
			: uint64(0);
		request(MTPmessages_GetStickers(
			MTP_string(key),
			MTP_long(hash)
		)).done([=](const MTPmessages_Stickers &result) {
			if (result.type() == mtpc_messages_stickersNotModified) {
				return;
			}
			Assert(result.type() == mtpc_messages_stickers);
			const auto &data = result.c_messages_stickers();
			auto &entry = _stickersByEmoji[key];
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
			_session->data().stickers().notifyUpdated(
				Data::StickersType::Stickers);
		}).send();
	}
	if (it == _stickersByEmoji.end()) {
		_stickersByEmoji.emplace(key, StickersByEmoji());
	} else if (it->second.received > 0) {
		return &it->second.list;
	}
	return nullptr;
}

void ApiWrap::requestStickers(TimeId now) {
	if (!_session->data().stickers().updateNeeded(now)
		|| _stickersUpdateRequest) {
		return;
	}
	const auto done = [=](const MTPmessages_AllStickers &result) {
		_session->data().stickers().setLastUpdate(crl::now());
		_stickersUpdateRequest = 0;

		result.match([&](const MTPDmessages_allStickersNotModified&) {
		}, [&](const MTPDmessages_allStickers &data) {
			_session->data().stickers().setsReceived(
				data.vsets().v,
				data.vhash().v);
		});
	};
	_stickersUpdateRequest = request(MTPmessages_GetAllStickers(
		MTP_long(Api::CountStickersHash(_session, true))
	)).done(done).fail([=] {
		LOG(("App Fail: Failed to get stickers!"));
		done(MTP_messages_allStickersNotModified());
	}).send();
}

void ApiWrap::requestMasks(TimeId now) {
	if (!_session->data().stickers().masksUpdateNeeded(now)
		|| _masksUpdateRequest) {
		return;
	}
	const auto done = [=](const MTPmessages_AllStickers &result) {
		_session->data().stickers().setLastMasksUpdate(crl::now());
		_masksUpdateRequest = 0;

		result.match([&](const MTPDmessages_allStickersNotModified&) {
		}, [&](const MTPDmessages_allStickers &data) {
			_session->data().stickers().masksReceived(
				data.vsets().v,
				data.vhash().v);
		});
	};
	_masksUpdateRequest = request(MTPmessages_GetMaskStickers(
		MTP_long(Api::CountMasksHash(_session, true))
	)).done(done).fail([=] {
		LOG(("App Fail: Failed to get masks!"));
		done(MTP_messages_allStickersNotModified());
	}).send();
}

void ApiWrap::requestCustomEmoji(TimeId now) {
	if (!_session->data().stickers().emojiUpdateNeeded(now)
		|| _customEmojiUpdateRequest) {
		return;
	}
	const auto done = [=](const MTPmessages_AllStickers &result) {
		_session->data().stickers().setLastEmojiUpdate(crl::now());
		_customEmojiUpdateRequest = 0;

		result.match([&](const MTPDmessages_allStickersNotModified&) {
		}, [&](const MTPDmessages_allStickers &data) {
			_session->data().stickers().emojiReceived(
				data.vsets().v,
				data.vhash().v);
		});
	};
	_customEmojiUpdateRequest = request(MTPmessages_GetEmojiStickers(
		MTP_long(Api::CountCustomEmojiHash(_session, true))
	)).done(done).fail([=] {
		LOG(("App Fail: Failed to get custom emoji!"));
		done(MTP_messages_allStickersNotModified());
	}).send();
}

void ApiWrap::requestRecentStickers(TimeId now, bool attached) {
	const auto needed = attached
		? _session->data().stickers().recentAttachedUpdateNeeded(now)
		: _session->data().stickers().recentUpdateNeeded(now);
	if (!needed) {
		return;
	}
	requestRecentStickersWithHash(
		Api::CountRecentStickersHash(_session, attached), attached);
}

void ApiWrap::requestRecentStickersWithHash(uint64 hash, bool attached) {
	const auto requestId = [=]() -> mtpRequestId & {
		return attached
			? _recentAttachedStickersUpdateRequest
			: _recentStickersUpdateRequest;
	};
	if (requestId()) {
		return;
	}
	const auto finish = [=] {
		auto &stickers = _session->data().stickers();
		if (attached) {
			stickers.setLastRecentAttachedUpdate(crl::now());
		} else {
			stickers.setLastRecentUpdate(crl::now());
		}
		requestId() = 0;
	};
	const auto flags = attached
		? MTPmessages_getRecentStickers::Flag::f_attached
		: MTPmessages_getRecentStickers::Flags(0);
	requestId() = request(MTPmessages_GetRecentStickers(
		MTP_flags(flags),
		MTP_long(hash)
	)).done([=](const MTPmessages_RecentStickers &result) {
		finish();

		switch (result.type()) {
		case mtpc_messages_recentStickersNotModified: return;
		case mtpc_messages_recentStickers: {
			auto &d = result.c_messages_recentStickers();
			_session->data().stickers().specialSetReceived(
				attached
					? Data::Stickers::CloudRecentAttachedSetId
					: Data::Stickers::CloudRecentSetId,
				tr::lng_recent_stickers(tr::now),
				d.vstickers().v,
				d.vhash().v,
				d.vpacks().v,
				d.vdates().v);
		} return;
		default: Unexpected("Type in ApiWrap::recentStickersDone()");
		}
	}).fail([=] {
		finish();

		LOG(("App Fail: Failed to get recent stickers!"));
	}).send();
}

void ApiWrap::requestFavedStickers(TimeId now) {
	if (!_session->data().stickers().favedUpdateNeeded(now)
		|| _favedStickersUpdateRequest) {
		return;
	}
	_favedStickersUpdateRequest = request(MTPmessages_GetFavedStickers(
		MTP_long(Api::CountFavedStickersHash(_session))
	)).done([=](const MTPmessages_FavedStickers &result) {
		_session->data().stickers().setLastFavedUpdate(crl::now());
		_favedStickersUpdateRequest = 0;

		switch (result.type()) {
		case mtpc_messages_favedStickersNotModified: return;
		case mtpc_messages_favedStickers: {
			auto &d = result.c_messages_favedStickers();
			_session->data().stickers().specialSetReceived(
				Data::Stickers::FavedSetId,
				Lang::Hard::FavedSetTitle(),
				d.vstickers().v,
				d.vhash().v,
				d.vpacks().v);
		} return;
		default: Unexpected("Type in ApiWrap::favedStickersDone()");
		}
	}).fail([=] {
		_session->data().stickers().setLastFavedUpdate(crl::now());
		_favedStickersUpdateRequest = 0;

		LOG(("App Fail: Failed to get faved stickers!"));
	}).send();
}

void ApiWrap::requestFeaturedStickers(TimeId now) {
	if (!_session->data().stickers().featuredUpdateNeeded(now)
		|| _featuredStickersUpdateRequest) {
		return;
	}
	_featuredStickersUpdateRequest = request(MTPmessages_GetFeaturedStickers(
		MTP_long(Api::CountFeaturedStickersHash(_session))
	)).done([=](const MTPmessages_FeaturedStickers &result) {
		_featuredStickersUpdateRequest = 0;
		_session->data().stickers().featuredSetsReceived(result);
	}).fail([=] {
		_featuredStickersUpdateRequest = 0;
		_session->data().stickers().setLastFeaturedUpdate(crl::now());
		LOG(("App Fail: Failed to get featured stickers!"));
	}).send();
}

void ApiWrap::requestFeaturedEmoji(TimeId now) {
	if (!_session->data().stickers().featuredEmojiUpdateNeeded(now)
		|| _featuredEmojiUpdateRequest) {
		return;
	}
	_featuredEmojiUpdateRequest = request(
		MTPmessages_GetFeaturedEmojiStickers(
			MTP_long(Api::CountFeaturedStickersHash(_session)))
	).done([=](const MTPmessages_FeaturedStickers &result) {
		_featuredEmojiUpdateRequest = 0;
		_session->data().stickers().featuredEmojiSetsReceived(result);
	}).fail([=] {
		_featuredEmojiUpdateRequest = 0;
		_session->data().stickers().setLastFeaturedEmojiUpdate(crl::now());
		LOG(("App Fail: Failed to get featured emoji!"));
	}).send();
}

void ApiWrap::requestSavedGifs(TimeId now) {
	if (!_session->data().stickers().savedGifsUpdateNeeded(now)
		|| _savedGifsUpdateRequest) {
		return;
	}
	_savedGifsUpdateRequest = request(MTPmessages_GetSavedGifs(
		MTP_long(Api::CountSavedGifsHash(_session))
	)).done([=](const MTPmessages_SavedGifs &result) {
		_session->data().stickers().setLastSavedGifsUpdate(crl::now());
		_savedGifsUpdateRequest = 0;

		switch (result.type()) {
		case mtpc_messages_savedGifsNotModified: return;
		case mtpc_messages_savedGifs: {
			auto &d = result.c_messages_savedGifs();
			_session->data().stickers().gifsReceived(
				d.vgifs().v,
				d.vhash().v);
		} return;
		default: Unexpected("Type in ApiWrap::savedGifsDone()");
		}
	}).fail([=] {
		_session->data().stickers().setLastSavedGifsUpdate(crl::now());
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
	const auto &sets = _session->data().stickers().sets();
	auto count = _session->data().stickers().featuredSetsUnreadCount();
	QVector<MTPlong> wrappedIds;
	wrappedIds.reserve(_featuredSetsRead.size());
	for (const auto setId : _featuredSetsRead) {
		const auto it = sets.find(setId);
		if (it != sets.cend()) {
			it->second->flags &= ~Data::StickersSetFlag::Unread;
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
		request(std::move(requestData)).done([=] {
			local().writeFeaturedStickers();
			_session->data().stickers().notifyUpdated(
				Data::StickersType::Stickers);
		}).send();

		_session->data().stickers().setFeaturedSetsUnreadCount(count);
	}
}

void ApiWrap::resolveJumpToDate(
		Dialogs::Key chat,
		const QDate &date,
		Fn<void(not_null<PeerData*>, MsgId)> callback) {
	if (const auto peer = chat.peer()) {
		const auto topic = chat.topic();
		const auto rootId = topic ? topic->rootId() : 0;
		resolveJumpToHistoryDate(peer, rootId, date, std::move(callback));
	}
}

template <typename Callback>
void ApiWrap::requestMessageAfterDate(
		not_null<PeerData*> peer,
		MsgId topicRootId,
		const QDate &date,
		Callback &&callback) {
	// API returns a message with date <= offset_date.
	// So we request a message with offset_date = desired_date - 1 and add_offset = -1.
	// This should give us the first message with date >= desired_date.
	const auto offsetId = 0;
	const auto offsetDate = static_cast<int>(date.startOfDay().toSecsSinceEpoch()) - 1;
	const auto addOffset = -1;
	const auto limit = 1;
	const auto maxId = 0;
	const auto minId = 0;
	const auto historyHash = uint64(0);

	auto send = [&](auto &&serialized) {
		request(std::move(serialized)).done([
			=,
			callback = std::forward<Callback>(callback)
		](const MTPmessages_Messages &result) {
			const auto handleMessages = [&](auto &messages) {
				_session->data().processUsers(messages.vusers());
				_session->data().processChats(messages.vchats());
				return &messages.vmessages().v;
			};
			const auto list = result.match([&](
					const MTPDmessages_messages &data) {
				return handleMessages(data);
			}, [&](const MTPDmessages_messagesSlice &data) {
				return handleMessages(data);
			}, [&](const MTPDmessages_channelMessages &data) {
				if (const auto channel = peer->asChannel()) {
					channel->ptsReceived(data.vpts().v);
					channel->processTopics(data.vtopics());
				} else {
					LOG(("API Error: received messages.channelMessages when "
						"no channel was passed! (ApiWrap::jumpToDate)"));
				}
				return handleMessages(data);
			}, [&](const MTPDmessages_messagesNotModified &) {
				LOG(("API Error: received messages.messagesNotModified! "
					"(ApiWrap::jumpToDate)"));
				return (const QVector<MTPMessage>*)nullptr;
			});
			if (list) {
				_session->data().processMessages(
					*list,
					NewMessageType::Existing);
				for (const auto &message : *list) {
					if (DateFromMessage(message) >= offsetDate) {
						callback(IdFromMessage(message));
						return;
					}
				}
			}
			callback(ShowAtUnreadMsgId);
		}).send();
	};
	if (topicRootId) {
		send(MTPmessages_GetReplies(
			peer->input,
			MTP_int(topicRootId),
			MTP_int(offsetId),
			MTP_int(offsetDate),
			MTP_int(addOffset),
			MTP_int(limit),
			MTP_int(maxId),
			MTP_int(minId),
			MTP_long(historyHash)));
	} else {
		send(MTPmessages_GetHistory(
			peer->input,
			MTP_int(offsetId),
			MTP_int(offsetDate),
			MTP_int(addOffset),
			MTP_int(limit),
			MTP_int(maxId),
			MTP_int(minId),
			MTP_long(historyHash)));
	}
}

void ApiWrap::resolveJumpToHistoryDate(
		not_null<PeerData*> peer,
		MsgId topicRootId,
		const QDate &date,
		Fn<void(not_null<PeerData*>, MsgId)> callback) {
	if (const auto channel = peer->migrateTo()) {
		return resolveJumpToHistoryDate(
			channel,
			topicRootId,
			date,
			std::move(callback));
	}
	const auto jumpToDateInPeer = [=] {
		requestMessageAfterDate(peer, topicRootId, date, [=](MsgId itemId) {
			callback(peer, itemId);
		});
	};
	if (const auto chat = topicRootId ? nullptr : peer->migrateFrom()) {
		requestMessageAfterDate(chat, 0, date, [=](MsgId itemId) {
			if (itemId) {
				callback(chat, itemId);
			} else {
				jumpToDateInPeer();
			}
		});
	} else {
		jumpToDateInPeer();
	}
}

void ApiWrap::requestSharedMedia(
		not_null<PeerData*> peer,
		MsgId topicRootId,
		SharedMediaType type,
		MsgId messageId,
		SliceType slice) {
	const auto key = SharedMediaRequest{
		peer,
		topicRootId,
		type,
		messageId,
		slice,
	};
	if (_sharedMediaRequests.contains(key)) {
		return;
	}

	const auto prepared = Api::PrepareSearchRequest(
		peer,
		topicRootId,
		type,
		QString(),
		messageId,
		slice);
	if (!prepared) {
		return;
	}

	const auto history = _session->data().history(peer);
	auto &histories = history->owner().histories();
	const auto requestType = Data::Histories::RequestType::History;
	histories.sendRequest(history, requestType, [=](Fn<void()> finish) {
		return request(
			std::move(*prepared)
		).done([=](const Api::SearchRequestResult &result) {
			_sharedMediaRequests.remove(key);
			auto parsed = Api::ParseSearchResult(
				peer,
				type,
				messageId,
				slice,
				result);
			sharedMediaDone(peer, topicRootId, type, std::move(parsed));
			finish();
		}).fail([=] {
			_sharedMediaRequests.remove(key);
			finish();
		}).send();
	});
	_sharedMediaRequests.emplace(key);
}

void ApiWrap::sharedMediaDone(
		not_null<PeerData*> peer,
		MsgId topicRootId,
		SharedMediaType type,
		Api::SearchResult &&parsed) {
	const auto topic = peer->forumTopicFor(topicRootId);
	if (topicRootId && !topic) {
		return;
	}
	const auto hasMessages = !parsed.messageIds.empty();
	_session->storage().add(Storage::SharedMediaAddSlice(
		peer->id,
		topicRootId,
		type,
		std::move(parsed.messageIds),
		parsed.noSkipRange,
		parsed.fullCount
	));
	if (type == SharedMediaType::Pinned && hasMessages) {
		peer->owner().history(peer)->setHasPinnedMessages(true);
		if (topic) {
			topic->setHasPinnedMessages(true);
		}
	}
}

void ApiWrap::sendAction(const SendAction &action) {
	if (!action.options.scheduled
		&& !action.options.shortcutId
		&& !action.replaceMediaOf) {
		const auto topicRootId = action.replyTo.topicRootId;
		const auto topic = topicRootId
			? action.history->peer->forumTopicFor(topicRootId)
			: nullptr;
		if (topic) {
			topic->readTillEnd();
		} else {
			_session->data().histories().readInbox(action.history);
		}
		action.history->getReadyFor(ShowAtTheEndMsgId);
	}
	_sendActions.fire_copy(action);
}

void ApiWrap::finishForwarding(const SendAction &action) {
	const auto history = action.history;
	const auto topicRootId = action.replyTo.topicRootId;
	auto toForward = history->resolveForwardDraft(topicRootId);
	if (!toForward.items.empty()) {
		const auto error = GetErrorTextForSending(
			history->peer,
			{
				.topicRootId = topicRootId,
				.forward = &toForward.items,
			});
		if (!error.isEmpty()) {
			return;
		}

		forwardMessages(std::move(toForward), action);
		history->setForwardDraft(topicRootId, {});
	}

	_session->data().sendHistoryChangeNotifications();
	if (!action.options.shortcutId) {
		_session->changes().historyUpdated(
			history,
			(action.options.scheduled
				? Data::HistoryUpdate::Flag::ScheduledSent
				: Data::HistoryUpdate::Flag::MessageSent));
	}
}

void ApiWrap::forwardMessages(
		Data::ResolvedForwardDraft &&draft,
		const SendAction &action,
		FnMut<void()> &&successCallback) {
	Expects(!draft.items.empty());

	auto &histories = _session->data().histories();

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

	const auto count = int(draft.items.size());
	const auto genClientSideMessage = action.generateLocal
		&& (count < 2)
		&& (draft.options == Data::ForwardOptions::PreserveInfo);
	const auto history = action.history;
	const auto peer = history->peer;

	if (!action.options.scheduled && !action.options.shortcutId) {
		histories.readInbox(history);
	}
	const auto anonymousPost = peer->amAnonymous();
	const auto silentPost = ShouldSendSilent(peer, action.options);
	const auto sendAs = action.options.sendAs;

	using SendFlag = MTPmessages_ForwardMessages::Flag;
	auto flags = MessageFlags();
	auto sendFlags = SendFlag() | SendFlag();
	FillMessagePostFlags(action, peer, flags);
	if (silentPost) {
		sendFlags |= SendFlag::f_silent;
	}
	if (action.options.scheduled) {
		flags |= MessageFlag::IsOrWasScheduled;
		sendFlags |= SendFlag::f_schedule_date;
	}
	if (action.options.shortcutId) {
		flags |= MessageFlag::ShortcutMessage;
		sendFlags |= SendFlag::f_quick_reply_shortcut;
	}
	if (draft.options != Data::ForwardOptions::PreserveInfo) {
		sendFlags |= SendFlag::f_drop_author;
	}
	if (draft.options == Data::ForwardOptions::NoNamesAndCaptions) {
		sendFlags |= SendFlag::f_drop_media_captions;
	}
	if (sendAs) {
		sendFlags |= SendFlag::f_send_as;
	}
	const auto kGeneralId = Data::ForumTopic::kGeneralId;
	const auto topicRootId = action.replyTo.topicRootId;
	const auto topMsgId = (topicRootId == kGeneralId)
		? MsgId(0)
		: topicRootId;
	if (topMsgId) {
		sendFlags |= SendFlag::f_top_msg_id;
	}

	auto forwardFrom = draft.items.front()->history()->peer;
	auto ids = QVector<MTPint>();
	auto randomIds = QVector<MTPlong>();
	auto localIds = std::shared_ptr<base::flat_map<uint64, FullMsgId>>();

	const auto sendAccumulated = [&] {
		if (shared) {
			++shared->requestsLeft;
		}
		const auto requestType = Data::Histories::RequestType::Send;
		const auto idsCopy = localIds;
		histories.sendRequest(history, requestType, [=](Fn<void()> finish) {
			history->sendRequestId = request(MTPmessages_ForwardMessages(
				MTP_flags(sendFlags),
				forwardFrom->input,
				MTP_vector<MTPint>(ids),
				MTP_vector<MTPlong>(randomIds),
				peer->input,
				MTP_int(topMsgId),
				MTP_int(action.options.scheduled),
				(sendAs ? sendAs->input : MTP_inputPeerEmpty()),
				Data::ShortcutIdToMTP(_session, action.options.shortcutId)
			)).done([=](const MTPUpdates &result) {
				applyUpdates(result);
				if (shared && !--shared->requestsLeft) {
					shared->callback();
				}
				finish();
			}).fail([=](const MTP::Error &error) {
				if (idsCopy) {
					for (const auto &[randomId, itemId] : *idsCopy) {
						sendMessageFail(error, peer, randomId, itemId);
					}
				} else {
					sendMessageFail(error, peer);
				}
				finish();
			}).afterRequest(
				history->sendRequestId
			).send();
			return history->sendRequestId;
		});

		ids.resize(0);
		randomIds.resize(0);
		localIds = nullptr;
	};

	ids.reserve(count);
	randomIds.reserve(count);
	for (const auto item : draft.items) {
		const auto randomId = base::RandomValue<uint64>();
		if (genClientSideMessage) {
			const auto newId = FullMsgId(
				peer->id,
				_session->data().nextLocalMessageId());
			const auto self = _session->user();
			const auto messageFromId = sendAs
				? sendAs->id
				: anonymousPost
				? PeerId(0)
				: self->id;
			const auto messagePostAuthor = peer->isBroadcast()
				? self->name()
				: QString();
			history->addNewLocalMessage({
				.id = newId.msg,
				.flags = flags,
				.from = messageFromId,
				.replyTo = { .topicRootId = topMsgId },
				.date = HistoryItem::NewMessageDate(action.options),
				.shortcutId = action.options.shortcutId,
				.postAuthor = messagePostAuthor,
			}, item);
			_session->data().registerMessageRandomId(randomId, newId);
			if (!localIds) {
				localIds = std::make_shared<base::flat_map<uint64, FullMsgId>>();
			}
			localIds->emplace(randomId, newId);
		}
		const auto newFrom = item->history()->peer;
		if (forwardFrom != newFrom) {
			sendAccumulated();
			forwardFrom = newFrom;
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
		const SendAction &action,
		Fn<void(bool)> done) {
	const auto userId = UserId(0);
	sendSharedContact(
		phone,
		firstName,
		lastName,
		userId,
		action,
		std::move(done));
}

void ApiWrap::shareContact(
		not_null<UserData*> user,
		const SendAction &action,
		Fn<void(bool)> done) {
	const auto userId = peerToUser(user->id);
	const auto phone = _session->data().findContactPhone(user);
	if (phone.isEmpty()) {
		if (done) {
			done(false);
		}
		return;
	}
	return sendSharedContact(
		phone,
		user->firstName,
		user->lastName,
		userId,
		action,
		std::move(done));
}

void ApiWrap::sendSharedContact(
		const QString &phone,
		const QString &firstName,
		const QString &lastName,
		UserId userId,
		const SendAction &action,
		Fn<void(bool)> done) {
	sendAction(action);

	const auto history = action.history;
	const auto peer = history->peer;

	const auto newId = FullMsgId(
		peer->id,
		_session->data().nextLocalMessageId());
	const auto anonymousPost = peer->amAnonymous();

	auto flags = NewMessageFlags(peer);
	if (action.replyTo) {
		flags |= MessageFlag::HasReplyInfo;
	}
	FillMessagePostFlags(action, peer, flags);
	if (action.options.scheduled) {
		flags |= MessageFlag::IsOrWasScheduled;
	}
	if (action.options.shortcutId) {
		flags |= MessageFlag::ShortcutMessage;
	}
	const auto messageFromId = action.options.sendAs
		? action.options.sendAs->id
		: anonymousPost
		? PeerId()
		: _session->userPeerId();
	const auto messagePostAuthor = peer->isBroadcast()
		? _session->user()->name()
		: QString();
	const auto item = history->addNewLocalMessage({
		.id = newId.msg,
		.flags = flags,
		.from = messageFromId,
		.replyTo = action.replyTo,
		.date = HistoryItem::NewMessageDate(action.options),
		.shortcutId = action.options.shortcutId,
		.postAuthor = messagePostAuthor,
	}, TextWithEntities(), MTP_messageMediaContact(
		MTP_string(phone),
		MTP_string(firstName),
		MTP_string(lastName),
		MTP_string(), // vcard
		MTP_long(userId.bare)));

	const auto media = MTP_inputMediaContact(
		MTP_string(phone),
		MTP_string(firstName),
		MTP_string(lastName),
		MTP_string()); // vcard
	sendMedia(item, media, action.options, std::move(done));

	_session->data().sendHistoryChangeNotifications();
	_session->changes().historyUpdated(
		history,
		(action.options.scheduled
			? Data::HistoryUpdate::Flag::ScheduledSent
			: Data::HistoryUpdate::Flag::MessageSent));
}

void ApiWrap::sendVoiceMessage(
		QByteArray result,
		VoiceWaveform waveform,
		crl::time duration,
		const SendAction &action) {
	const auto caption = TextWithTags();
	const auto to = FileLoadTaskOptions(action);
	_fileLoader->addTask(std::make_unique<FileLoadTask>(
		&session(),
		result,
		duration,
		waveform,
		to,
		caption));
}

void ApiWrap::editMedia(
		Ui::PreparedList &&list,
		SendMediaType type,
		TextWithTags &&caption,
		const SendAction &action) {
	if (list.files.empty()) return;

	auto &file = list.files.front();
	const auto to = FileLoadTaskOptions(action);
	_fileLoader->addTask(std::make_unique<FileLoadTask>(
		&session(),
		file.path,
		file.content,
		std::move(file.information),
		type,
		to,
		caption,
		file.spoiler));
}

void ApiWrap::sendFiles(
		Ui::PreparedList &&list,
		SendMediaType type,
		TextWithTags &&caption,
		std::shared_ptr<SendingAlbum> album,
		const SendAction &action) {
	const auto haveCaption = !caption.text.isEmpty();
	if (haveCaption
		&& !list.canAddCaption(
			album != nullptr,
			type == SendMediaType::Photo)) {
		auto message = MessageToSend(action);
		message.textWithTags = base::take(caption);
		message.action.clearDraft = false;
		sendMessage(std::move(message));
	}

	const auto to = FileLoadTaskOptions(action);
	if (album) {
		album->options = to.options;
	}
	auto tasks = std::vector<std::unique_ptr<Task>>();
	tasks.reserve(list.files.size());
	for (auto &file : list.files) {
		const auto uploadWithType = !album
			? type
			: (file.type == Ui::PreparedFile::Type::Photo
				&& type != SendMediaType::File)
			? SendMediaType::Photo
			: SendMediaType::File;
		tasks.push_back(std::make_unique<FileLoadTask>(
			&session(),
			file.path,
			file.content,
			std::move(file.information),
			uploadWithType,
			to,
			caption,
			file.spoiler,
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
	const auto to = FileLoadTaskOptions(action);
	auto caption = TextWithTags();
	const auto spoiler = false;
	const auto information = nullptr;
	_fileLoader->addTask(std::make_unique<FileLoadTask>(
		&session(),
		QString(),
		fileContent,
		information,
		type,
		to,
		caption,
		spoiler));
}

void ApiWrap::sendUploadedPhoto(
		FullMsgId localId,
		Api::RemoteFileInfo info,
		Api::SendOptions options) {
	if (const auto item = _session->data().message(localId)) {
		const auto media = Api::PrepareUploadedPhoto(item, std::move(info));
		if (const auto groupId = item->groupId()) {
			uploadAlbumMedia(item, groupId, media);
		} else {
			sendMedia(item, media, options);
		}
	}
}

void ApiWrap::sendUploadedDocument(
		FullMsgId localId,
		Api::RemoteFileInfo info,
		Api::SendOptions options) {
	if (const auto item = _session->data().message(localId)) {
		if (!item->media() || !item->media()->document()) {
			return;
		}
		const auto media = Api::PrepareUploadedDocument(
			item,
			std::move(info));
		const auto groupId = item->groupId();
		if (groupId) {
			uploadAlbumMedia(item, groupId, media);
		} else {
			sendMedia(item, media, options);
		}
	}
}

void ApiWrap::cancelLocalItem(not_null<HistoryItem*> item) {
	Expects(item->isSending());

	if (const auto groupId = item->groupId()) {
		sendAlbumWithCancelled(item, groupId);
	}
}

void ApiWrap::sendShortcutMessages(
		not_null<PeerData*> peer,
		BusinessShortcutId id) {
	auto ids = QVector<MTPint>();
	auto randomIds = QVector<MTPlong>();
	request(MTPmessages_SendQuickReplyMessages(
		peer->input,
		MTP_int(id),
		MTP_vector<MTPint>(ids),
		MTP_vector<MTPlong>(randomIds)
	)).done([=](const MTPUpdates &result) {
		applyUpdates(result);
	}).fail([=](const MTP::Error &error) {
	}).send();
}

void ApiWrap::sendMessage(MessageToSend &&message) {
	const auto history = message.action.history;
	const auto peer = history->peer;
	auto &textWithTags = message.textWithTags;

	auto action = message.action;
	action.generateLocal = true;
	sendAction(action);

	const auto clearCloudDraft = action.clearDraft;
	const auto draftTopicRootId = action.replyTo.topicRootId;
	const auto replyTo = action.replyTo.messageId
		? peer->owner().message(action.replyTo.messageId)
		: nullptr;
	const auto topicRootId = draftTopicRootId
		? draftTopicRootId
		: replyTo
		? replyTo->topicRootId()
		: Data::ForumTopic::kGeneralId;
	const auto topic = peer->forumTopicFor(topicRootId);
	if (!(topic ? Data::CanSendTexts(topic) : Data::CanSendTexts(peer))
		|| Api::SendDice(message)) {
		return;
	}
	local().saveRecentSentHashtags(textWithTags.text);

	auto sending = TextWithEntities();
	auto left = TextWithEntities {
		textWithTags.text,
		TextUtilities::ConvertTextTagsToEntities(textWithTags.tags)
	};
	auto prepareFlags = Ui::ItemTextOptions(
		history,
		_session->user()).flags;
	TextUtilities::PrepareForSending(left, prepareFlags);

	HistoryItem *lastMessage = nullptr;

	auto &histories = history->owner().histories();

	const auto exactWebPage = !message.webPage.url.isEmpty();
	auto isFirst = true;
	while (TextUtilities::CutPart(sending, left, MaxMessageSize)
		|| (isFirst && exactWebPage)) {
		TextUtilities::Trim(left);
		const auto isLast = left.empty();

		auto newId = FullMsgId(
			peer->id,
			_session->data().nextLocalMessageId());
		auto randomId = base::RandomValue<uint64>();

		TextUtilities::Trim(sending);

		_session->data().registerMessageRandomId(randomId, newId);
		_session->data().registerMessageSentData(
			randomId,
			peer->id,
			sending.text);

		MTPstring msgText(MTP_string(sending.text));
		auto flags = NewMessageFlags(peer);
		auto sendFlags = MTPmessages_SendMessage::Flags(0);
		auto mediaFlags = MTPmessages_SendMedia::Flags(0);
		if (action.replyTo) {
			flags |= MessageFlag::HasReplyInfo;
			sendFlags |= MTPmessages_SendMessage::Flag::f_reply_to;
			mediaFlags |= MTPmessages_SendMedia::Flag::f_reply_to;
		}
		const auto ignoreWebPage = message.webPage.removed
			|| (exactWebPage && !isLast);
		const auto manualWebPage = exactWebPage
			&& !ignoreWebPage
			&& (message.webPage.manual || (isLast && !isFirst));
		MTPMessageMedia media = MTP_messageMediaEmpty();
		if (ignoreWebPage) {
			sendFlags |= MTPmessages_SendMessage::Flag::f_no_webpage;
		} else if (exactWebPage) {
			using PageFlag = MTPDmessageMediaWebPage::Flag;
			using PendingFlag = MTPDwebPagePending::Flag;
			const auto &fields = message.webPage;
			const auto page = _session->data().webpage(fields.id);
			media = MTP_messageMediaWebPage(
				MTP_flags(PageFlag()
					| (manualWebPage ? PageFlag::f_manual : PageFlag())
					| (fields.forceLargeMedia
						? PageFlag::f_force_large_media
						: PageFlag())
					| (fields.forceSmallMedia
						? PageFlag::f_force_small_media
						: PageFlag())),
				MTP_webPagePending(
					MTP_flags(PendingFlag::f_url),
					MTP_long(fields.id),
					MTP_string(fields.url),
					MTP_int(page->pendingTill)));
		}
		const auto anonymousPost = peer->amAnonymous();
		const auto silentPost = ShouldSendSilent(peer, action.options);
		FillMessagePostFlags(action, peer, flags);
		if (exactWebPage && !ignoreWebPage && message.webPage.invert) {
			flags |= MessageFlag::InvertMedia;
			sendFlags |= MTPmessages_SendMessage::Flag::f_invert_media;
			mediaFlags |= MTPmessages_SendMedia::Flag::f_invert_media;
		}
		if (silentPost) {
			sendFlags |= MTPmessages_SendMessage::Flag::f_silent;
			mediaFlags |= MTPmessages_SendMedia::Flag::f_silent;
		}
		const auto sentEntities = Api::EntitiesToMTP(
			_session,
			sending.entities,
			Api::ConvertOption::SkipLocal);
		if (!sentEntities.v.isEmpty()) {
			sendFlags |= MTPmessages_SendMessage::Flag::f_entities;
			mediaFlags |= MTPmessages_SendMedia::Flag::f_entities;
		}
		if (clearCloudDraft) {
			sendFlags |= MTPmessages_SendMessage::Flag::f_clear_draft;
			mediaFlags |= MTPmessages_SendMedia::Flag::f_clear_draft;
			history->clearCloudDraft(draftTopicRootId);
			history->startSavingCloudDraft(draftTopicRootId);
		}
		const auto sendAs = action.options.sendAs;
		const auto messageFromId = sendAs
			? sendAs->id
			: anonymousPost
			? PeerId()
			: _session->userPeerId();
		if (sendAs) {
			sendFlags |= MTPmessages_SendMessage::Flag::f_send_as;
			mediaFlags |= MTPmessages_SendMedia::Flag::f_send_as;
		}
		const auto messagePostAuthor = peer->isBroadcast()
			? _session->user()->name()
			: QString();
		if (action.options.scheduled) {
			flags |= MessageFlag::IsOrWasScheduled;
			sendFlags |= MTPmessages_SendMessage::Flag::f_schedule_date;
			mediaFlags |= MTPmessages_SendMedia::Flag::f_schedule_date;
		}
		if (action.options.shortcutId) {
			flags |= MessageFlag::ShortcutMessage;
			sendFlags |= MTPmessages_SendMessage::Flag::f_quick_reply_shortcut;
			mediaFlags |= MTPmessages_SendMedia::Flag::f_quick_reply_shortcut;
		}
		lastMessage = history->addNewLocalMessage({
			.id = newId.msg,
			.flags = flags,
			.from = messageFromId,
			.replyTo = action.replyTo,
			.date = HistoryItem::NewMessageDate(action.options),
			.shortcutId = action.options.shortcutId,
			.postAuthor = messagePostAuthor,
		}, sending, media);
		const auto done = [=](
				const MTPUpdates &result,
				const MTP::Response &response) {
			if (clearCloudDraft) {
				history->finishSavingCloudDraft(
					draftTopicRootId,
					UnixtimeFromMsgId(response.outerMsgId));
			}
		};
		const auto fail = [=](
				const MTP::Error &error,
				const MTP::Response &response) {
			if (error.type() == u"MESSAGE_EMPTY"_q) {
				lastMessage->destroy();
			} else {
				sendMessageFail(error, peer, randomId, newId);
			}
			if (clearCloudDraft) {
				history->finishSavingCloudDraft(
					draftTopicRootId,
					UnixtimeFromMsgId(response.outerMsgId));
			}
		};
		const auto mtpShortcut = Data::ShortcutIdToMTP(
			_session,
			action.options.shortcutId);
		if (exactWebPage
			&& !ignoreWebPage
			&& (manualWebPage || sending.empty())) {
			histories.sendPreparedMessage(
				history,
				action.replyTo,
				randomId,
				Data::Histories::PrepareMessage<MTPmessages_SendMedia>(
					MTP_flags(mediaFlags),
					peer->input,
					Data::Histories::ReplyToPlaceholder(),
					Data::WebPageForMTP(message.webPage, true),
					msgText,
					MTP_long(randomId),
					MTPReplyMarkup(),
					sentEntities,
					MTP_int(action.options.scheduled),
					(sendAs ? sendAs->input : MTP_inputPeerEmpty()),
					mtpShortcut
				), done, fail);
		} else {
			histories.sendPreparedMessage(
				history,
				action.replyTo,
				randomId,
				Data::Histories::PrepareMessage<MTPmessages_SendMessage>(
					MTP_flags(sendFlags),
					peer->input,
					Data::Histories::ReplyToPlaceholder(),
					msgText,
					MTP_long(randomId),
					MTPReplyMarkup(),
					sentEntities,
					MTP_int(action.options.scheduled),
					(sendAs ? sendAs->input : MTP_inputPeerEmpty()),
					mtpShortcut
				), done, fail);
		}
		isFirst = false;
	}

	finishForwarding(action);
}

void ApiWrap::sendBotStart(
		std::shared_ptr<Ui::Show> show,
		not_null<UserData*> bot,
		PeerData *chat,
		const QString &startTokenForChat) {
	Expects(bot->isBot());

	if (chat && chat->isChannel() && !chat->isMegagroup()) {
		ShowAddParticipantsError(show, "USER_BOT", chat, bot);
		return;
	}

	auto &info = bot->botInfo;
	const auto token = chat ? startTokenForChat : info->startToken;
	if (token.isEmpty()) {
		auto message = MessageToSend(
			Api::SendAction(_session->data().history(chat
				? chat
				: bot.get())));
		message.textWithTags = { u"/start"_q, TextWithTags::Tags() };
		if (chat) {
			message.textWithTags.text += '@' + bot->username();
		}
		sendMessage(std::move(message));
		return;
	}
	const auto randomId = base::RandomValue<uint64>();
	if (!chat) {
		info->startToken = QString();
	}
	request(MTPmessages_StartBot(
		bot->inputUser,
		chat ? chat->input : MTP_inputPeerEmpty(),
		MTP_long(randomId),
		MTP_string(token)
	)).done([=](const MTPUpdates &result) {
		applyUpdates(result);
	}).fail([=](const MTP::Error &error) {
		if (chat) {
			const auto type = error.type();
			ShowAddParticipantsError(show, type, chat, bot);
		}
	}).send();
}

void ApiWrap::sendInlineResult(
		not_null<UserData*> bot,
		not_null<InlineBots::Result*> data,
		const SendAction &action,
		std::optional<MsgId> localMessageId) {
	sendAction(action);

	const auto history = action.history;
	const auto peer = history->peer;
	const auto newId = FullMsgId(
		peer->id,
		localMessageId
			? (*localMessageId)
			: _session->data().nextLocalMessageId());
	const auto randomId = base::RandomValue<uint64>();
	const auto topicRootId = action.replyTo.messageId
		? action.replyTo.topicRootId
		: 0;

	using SendFlag = MTPmessages_SendInlineBotResult::Flag;
	auto flags = NewMessageFlags(peer);
	auto sendFlags = SendFlag::f_clear_draft | SendFlag();
	if (action.replyTo) {
		flags |= MessageFlag::HasReplyInfo;
		sendFlags |= SendFlag::f_reply_to;
	}
	const auto anonymousPost = peer->amAnonymous();
	const auto silentPost = ShouldSendSilent(peer, action.options);
	FillMessagePostFlags(action, peer, flags);
	if (silentPost) {
		sendFlags |= SendFlag::f_silent;
	}
	if (action.options.scheduled) {
		flags |= MessageFlag::IsOrWasScheduled;
		sendFlags |= SendFlag::f_schedule_date;
	}
	if (action.options.shortcutId) {
		flags |= MessageFlag::ShortcutMessage;
		sendFlags |= SendFlag::f_quick_reply_shortcut;
	}
	if (action.options.hideViaBot) {
		sendFlags |= SendFlag::f_hide_via;
	}

	const auto sendAs = action.options.sendAs;
	const auto messageFromId = sendAs
		? sendAs->id
		: anonymousPost ? PeerId()
		: _session->userPeerId();
	if (sendAs) {
		sendFlags |= MTPmessages_SendInlineBotResult::Flag::f_send_as;
	}
	const auto messagePostAuthor = peer->isBroadcast()
		? _session->user()->name()
		: QString();

	_session->data().registerMessageRandomId(randomId, newId);

	data->addToHistory(history, {
		.id = newId.msg,
		.flags = flags,
		.from = messageFromId,
		.replyTo = action.replyTo,
		.date = HistoryItem::NewMessageDate(action.options),
		.shortcutId = action.options.shortcutId,
		.viaBotId = ((bot && !action.options.hideViaBot)
			? peerToUser(bot->id)
			: UserId()),
		.postAuthor = messagePostAuthor,
	});

	history->clearCloudDraft(topicRootId);
	history->startSavingCloudDraft(topicRootId);

	auto &histories = history->owner().histories();
	histories.sendPreparedMessage(
		history,
		action.replyTo,
		randomId,
		Data::Histories::PrepareMessage<MTPmessages_SendInlineBotResult>(
			MTP_flags(sendFlags),
			peer->input,
			Data::Histories::ReplyToPlaceholder(),
			MTP_long(randomId),
			MTP_long(data->getQueryId()),
			MTP_string(data->getId()),
			MTP_int(action.options.scheduled),
			(sendAs ? sendAs->input : MTP_inputPeerEmpty()),
			Data::ShortcutIdToMTP(_session, action.options.shortcutId)
		), [=](const MTPUpdates &result, const MTP::Response &response) {
		history->finishSavingCloudDraft(
			topicRootId,
			UnixtimeFromMsgId(response.outerMsgId));
	}, [=](const MTP::Error &error, const MTP::Response &response) {
		sendMessageFail(error, peer, randomId, newId);
		history->finishSavingCloudDraft(
			topicRootId,
			UnixtimeFromMsgId(response.outerMsgId));
	});
	finishForwarding(action);
}

void ApiWrap::uploadAlbumMedia(
		not_null<HistoryItem*> item,
		const MessageGroupId &groupId,
		const MTPInputMedia &media) {
	const auto localId = item->fullId();
	const auto failed = [=] {

	};
	request(MTPmessages_UploadMedia(
		MTP_flags(0),
		MTPstring(), // business_connection_id
		item->history()->peer->input,
		media
	)).done([=](const MTPMessageMedia &result) {
		const auto item = _session->data().message(localId);
		if (!item) {
			failed();
			return;
		}
		auto spoiler = false;
		if (const auto media = item->media()) {
			spoiler = media->hasSpoiler();
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
			using Flag = MTPDinputMediaPhoto::Flag;
			const auto flags = Flag()
				| (data.vttl_seconds() ? Flag::f_ttl_seconds : Flag())
				| (spoiler ? Flag::f_spoiler : Flag());
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
			using Flag = MTPDinputMediaDocument::Flag;
			const auto flags = Flag()
				| (data.vttl_seconds() ? Flag::f_ttl_seconds : Flag())
				| (spoiler ? Flag::f_spoiler : Flag());
			const auto media = MTP_inputMediaDocument(
				MTP_flags(flags),
				MTP_inputDocument(
					fields.vid(),
					fields.vaccess_hash(),
					fields.vfile_reference()),
				MTP_int(data.vttl_seconds().value_or_empty()),
				MTPstring()); // query
			sendAlbumWithUploaded(item, groupId, media);
		} break;
		}
	}).fail([=] {
		failed();
	}).send();
}

void ApiWrap::sendMedia(
		not_null<HistoryItem*> item,
		const MTPInputMedia &media,
		Api::SendOptions options,
		Fn<void(bool)> done) {
	const auto randomId = base::RandomValue<uint64>();
	_session->data().registerMessageRandomId(randomId, item->fullId());

	sendMediaWithRandomId(item, media, options, randomId, std::move(done));
}

void ApiWrap::sendMediaWithRandomId(
		not_null<HistoryItem*> item,
		const MTPInputMedia &media,
		Api::SendOptions options,
		uint64 randomId,
		Fn<void(bool)> done) {
	const auto history = item->history();
	const auto replyTo = item->replyTo();

	auto caption = item->originalText();
	TextUtilities::Trim(caption);
	auto sentEntities = Api::EntitiesToMTP(
		_session,
		caption.entities,
		Api::ConvertOption::SkipLocal);

	const auto updateRecentStickers = Api::HasAttachedStickers(media);

	using Flag = MTPmessages_SendMedia::Flag;
	const auto flags = Flag(0)
		| (replyTo ? Flag::f_reply_to : Flag(0))
		| (ShouldSendSilent(history->peer, options)
			? Flag::f_silent
			: Flag(0))
		| (!sentEntities.v.isEmpty() ? Flag::f_entities : Flag(0))
		| (options.scheduled ? Flag::f_schedule_date : Flag(0))
		| (options.sendAs ? Flag::f_send_as : Flag(0))
		| (options.shortcutId ? Flag::f_quick_reply_shortcut : Flag(0));

	auto &histories = history->owner().histories();
	const auto peer = history->peer;
	const auto itemId = item->fullId();
	histories.sendPreparedMessage(
		history,
		replyTo,
		randomId,
		Data::Histories::PrepareMessage<MTPmessages_SendMedia>(
			MTP_flags(flags),
			peer->input,
			Data::Histories::ReplyToPlaceholder(),
			media,
			MTP_string(caption.text),
			MTP_long(randomId),
			MTPReplyMarkup(),
			sentEntities,
			MTP_int(options.scheduled),
			(options.sendAs ? options.sendAs->input : MTP_inputPeerEmpty()),
			Data::ShortcutIdToMTP(_session, options.shortcutId)
		), [=](const MTPUpdates &result, const MTP::Response &response) {
		if (done) done(true);
		if (updateRecentStickers) {
			requestRecentStickersForce(true);
		}
	}, [=](const MTP::Error &error, const MTP::Response &response) {
		if (done) done(false);
		sendMessageFail(error, peer, randomId, itemId);
	});
}

void ApiWrap::sendAlbumWithUploaded(
		not_null<HistoryItem*> item,
		const MessageGroupId &groupId,
		const MTPInputMedia &media) {
	const auto localId = item->fullId();
	const auto randomId = base::RandomValue<uint64>();
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
	const auto replyTo = sample->replyTo();
	const auto sendAs = album->options.sendAs;
	using Flag = MTPmessages_SendMultiMedia::Flag;
	const auto flags = Flag(0)
		| (replyTo ? Flag::f_reply_to : Flag(0))
		| (ShouldSendSilent(history->peer, album->options)
			? Flag::f_silent
			: Flag(0))
		| (album->options.scheduled ? Flag::f_schedule_date : Flag(0))
		| (sendAs ? Flag::f_send_as : Flag(0))
		| (album->options.shortcutId
			? Flag::f_quick_reply_shortcut
			: Flag(0));
	auto &histories = history->owner().histories();
	const auto peer = history->peer;
	histories.sendPreparedMessage(
		history,
		replyTo,
		uint64(0), // randomId
		Data::Histories::PrepareMessage<MTPmessages_SendMultiMedia>(
			MTP_flags(flags),
			peer->input,
			Data::Histories::ReplyToPlaceholder(),
			MTP_vector<MTPInputSingleMedia>(medias),
			MTP_int(album->options.scheduled),
			(sendAs ? sendAs->input : MTP_inputPeerEmpty()),
			Data::ShortcutIdToMTP(_session, album->options.shortcutId)
		), [=](const MTPUpdates &result, const MTP::Response &response) {
		_sendingAlbums.remove(groupId);
	}, [=](const MTP::Error &error, const MTP::Response &response) {
		if (const auto album = _sendingAlbums.take(groupId)) {
			for (const auto &item : (*album)->items) {
				sendMessageFail(error, peer, item.randomId, item.msgId);
			}
		} else {
			sendMessageFail(error, peer);
		}
	});
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
	}).fail([=] {
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
	)).done([=] {
		_contactSignupSilentRequestId = 0;
		_contactSignupSilent = silent;
		_contactSignupSilentChanges.fire_copy(silent);
	}).fail([=] {
		_contactSignupSilentRequestId = 0;
	}).send();
	_contactSignupSilentRequestId = requestId;
}

auto ApiWrap::botCommonGroups(not_null<UserData*> bot) const
-> std::optional<std::vector<not_null<PeerData*>>> {
	const auto i = _botCommonGroups.find(bot);
	return (i != end(_botCommonGroups))
		? i->second
		: std::optional<std::vector<not_null<PeerData*>>>();
}

void ApiWrap::requestBotCommonGroups(
		not_null<UserData*> bot,
		Fn<void()> done) {
	if (_botCommonGroupsRequests.contains(bot)) {
		return;
	}
	_botCommonGroupsRequests.emplace(bot, done);
	const auto finish = [=](std::vector<not_null<PeerData*>> list) {
		_botCommonGroups.emplace(bot, std::move(list));
		if (const auto callback = _botCommonGroupsRequests.take(bot)) {
			(*callback)();
		}
	};
	const auto limit = 100;
	request(MTPmessages_GetCommonChats(
		bot->inputUser,
		MTP_long(0), // max_id
		MTP_int(limit)
	)).done([=](const MTPmessages_Chats &result) {
		const auto chats = result.match([](const auto &data) {
			return &data.vchats().v;
		});
		auto &owner = session().data();
		auto list = std::vector<not_null<PeerData*>>();
		list.reserve(chats->size());
		for (const auto &chat : *chats) {
			if (const auto peer = owner.processChat(chat)) {
				list.push_back(peer);
			}
		}
		finish(std::move(list));
	}).fail([=] {
		finish({});
	}).send();
}

void ApiWrap::saveSelfBio(const QString &text) {
	if (_bio.requestId) {
		if (text != _bio.requestedText) {
			request(_bio.requestId).cancel();
		} else {
			return;
		}
	}
	_bio.requestedText = text;
	_bio.requestId = request(MTPaccount_UpdateProfile(
		MTP_flags(MTPaccount_UpdateProfile::Flag::f_about),
		MTPstring(),
		MTPstring(),
		MTP_string(text)
	)).done([=](const MTPUser &result) {
		_bio.requestId = 0;

		_session->data().processUser(result);
		_session->user()->setAbout(_bio.requestedText);
	}).fail([=] {
		_bio.requestId = 0;
	}).send();
}

void ApiWrap::registerStatsRequest(MTP::DcId dcId, mtpRequestId id) {
	_statsRequests[dcId].emplace(id);
}

void ApiWrap::unregisterStatsRequest(MTP::DcId dcId, mtpRequestId id) {
	const auto i = _statsRequests.find(dcId);
	Assert(i != end(_statsRequests));
	const auto removed = i->second.remove(id);
	Assert(removed);
	if (i->second.empty()) {
		_statsSessionKillTimer.callOnce(kStatsSessionKillTimeout);
	}
}

void ApiWrap::checkStatsSessions() {
	for (auto i = begin(_statsRequests); i != end(_statsRequests);) {
		if (i->second.empty()) {
			instance().killSession(
				MTP::ShiftDcId(i->first, MTP::kStatsDcShift));
			i = _statsRequests.erase(i);
		} else {
			++i;
		}
	}
}

Api::Authorizations &ApiWrap::authorizations() {
	return *_authorizations;
}

Api::AttachedStickers &ApiWrap::attachedStickers() {
	return *_attachedStickers;
}

Api::BlockedPeers &ApiWrap::blockedPeers() {
	return *_blockedPeers;
}

Api::CloudPassword &ApiWrap::cloudPassword() {
	return *_cloudPassword;
}

Api::SelfDestruct &ApiWrap::selfDestruct() {
	return *_selfDestruct;
}

Api::SensitiveContent &ApiWrap::sensitiveContent() {
	return *_sensitiveContent;
}

Api::GlobalPrivacy &ApiWrap::globalPrivacy() {
	return *_globalPrivacy;
}

Api::UserPrivacy &ApiWrap::userPrivacy() {
	return *_userPrivacy;
}

Api::InviteLinks &ApiWrap::inviteLinks() {
	return *_inviteLinks;
}

Api::ChatLinks &ApiWrap::chatLinks() {
	return *_chatLinks;
}

Api::ViewsManager &ApiWrap::views() {
	return *_views;
}

Api::ConfirmPhone &ApiWrap::confirmPhone() {
	return *_confirmPhone;
}

Api::PeerPhoto &ApiWrap::peerPhoto() {
	return *_peerPhoto;
}

Api::Polls &ApiWrap::polls() {
	return *_polls;
}

Api::ChatParticipants &ApiWrap::chatParticipants() {
	return *_chatParticipants;
}

Api::UnreadThings &ApiWrap::unreadThings() {
	return *_unreadThings;
}

Api::Ringtones &ApiWrap::ringtones() {
	return *_ringtones;
}

Api::Transcribes &ApiWrap::transcribes() {
	return *_transcribes;
}

Api::Premium &ApiWrap::premium() {
	return *_premium;
}

Api::Usernames &ApiWrap::usernames() {
	return *_usernames;
}

Api::Websites &ApiWrap::websites() {
	return *_websites;
}

Api::PeerColors &ApiWrap::peerColors() {
	return *_peerColors;
}
