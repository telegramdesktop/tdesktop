/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_message.h"

#include "base/openssl_help.h"
#include "base/unixtime.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "api/api_text_entities.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/history_location_manager.h"
#include "history/history_service.h"
#include "history/view/history_view_service_message.h"
#include "history/view/history_view_context_menu.h" // CopyPostLink.
#include "history/view/media/history_view_media.h" // AddTimestampLinks.
#include "chat_helpers/stickers_emoji_pack.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "api/api_updates.h"
#include "boxes/share_box.h"
#include "boxes/confirm_box.h"
#include "ui/toast/toast.h"
#include "ui/text/text_utilities.h"
#include "ui/text/text_isolated_emoji.h"
#include "ui/text/format_values.h"
#include "ui/item_text_options.h"
#include "core/application.h"
#include "core/ui_integration.h"
#include "window/notifications_manager.h"
#include "window/window_session_controller.h"
#include "storage/storage_shared_media.h"
#include "mtproto/mtproto_config.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_game.h"
#include "data/data_media_types.h"
#include "data/data_channel.h"
#include "data/data_user.h"
#include "data/data_histories.h"
#include "app.h"
#include "styles/style_dialogs.h"
#include "styles/style_widgets.h"
#include "styles/style_chat.h"
#include "styles/style_window.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace {

[[nodiscard]] MTPDmessage::Flags NewForwardedFlags(
		not_null<PeerData*> peer,
		PeerId from,
		not_null<HistoryMessage*> fwd) {
	auto result = NewMessageFlags(peer) | MTPDmessage::Flag::f_fwd_from;
	if (from) {
		result |= MTPDmessage::Flag::f_from_id;
	}
	if (fwd->Has<HistoryMessageVia>()) {
		result |= MTPDmessage::Flag::f_via_bot_id;
	}
	if (const auto media = fwd->media()) {
		if (dynamic_cast<Data::MediaWebPage*>(media)) {
			// Drop web page if we're not allowed to send it.
			if (peer->amRestricted(ChatRestriction::f_embed_links)) {
				result &= ~MTPDmessage::Flag::f_media;
			}
		}
		if ((!peer->isChannel() || peer->isMegagroup())
			&& media->forwardedBecomesUnread()) {
			result |= MTPDmessage::Flag::f_media_unread;
		}
	}
	if (fwd->hasViews()) {
		result |= MTPDmessage::Flag::f_views;
	}
	return result;
}

[[nodiscard]] MTPDmessage_ClientFlags NewForwardedClientFlags() {
	return NewMessageClientFlags();
}

[[nodiscard]] bool CopyMarkupToForward(not_null<const HistoryItem*> item) {
	auto mediaOriginal = item->media();
	if (mediaOriginal && mediaOriginal->game()) {
		// Copy inline keyboard when forwarding messages with a game.
		return true;
	}
	const auto markup = item->inlineReplyMarkup();
	if (!markup) {
		return false;
	}
	using Type = HistoryMessageMarkupButton::Type;
	for (const auto &row : markup->rows) {
		for (const auto &button : row) {
			const auto switchInline = (button.type == Type::SwitchInline)
				|| (button.type == Type::SwitchInlineSame);
			const auto url = (button.type == Type::Url)
				|| (button.type == Type::Auth);
			if ((!switchInline || !item->viaBot()) && !url) {
				return false;
			}
		}
	}
	return true;
}

[[nodiscard]] bool HasInlineItems(const HistoryItemsList &items) {
	for (const auto item : items) {
		if (item->viaBot()) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] TextWithEntities EnsureNonEmpty(
		const TextWithEntities &text = TextWithEntities()) {
	if (!text.text.isEmpty()) {
		return text;
	}
	return { QString::fromUtf8(":-("), EntitiesInText() };
}

} // namespace

QString GetErrorTextForSending(
		not_null<PeerData*> peer,
		const HistoryItemsList &items,
		const TextWithTags &comment,
		bool ignoreSlowmodeCountdown) {
	if (!peer->canWrite()) {
		return tr::lng_forward_cant(tr::now);
	}

	for (const auto item : items) {
		if (const auto media = item->media()) {
			const auto error = media->errorTextForForward(peer);
			if (!error.isEmpty() && error != qstr("skip")) {
				return error;
			}
		}
	}
	const auto error = Data::RestrictionError(
		peer,
		ChatRestriction::f_send_inline);
	if (error && HasInlineItems(items)) {
		return *error;
	}

	if (peer->slowmodeApplied()) {
		if (const auto history = peer->owner().historyLoaded(peer)) {
			if (!ignoreSlowmodeCountdown
				&& (history->latestSendingMessage() != nullptr)
				&& (!items.empty() || !comment.text.isEmpty())) {
				return tr::lng_slowmode_no_many(tr::now);
			}
		}
		if (comment.text.size() > MaxMessageSize) {
			return tr::lng_slowmode_too_long(tr::now);
		} else if (!items.empty() && !comment.text.isEmpty()) {
			return tr::lng_slowmode_no_many(tr::now);
		} else if (items.size() > 1) {
			const auto albumForward = [&] {
				if (const auto groupId = items.front()->groupId()) {
					for (const auto item : items) {
						if (item->groupId() != groupId) {
							return false;
						}
					}
					return true;
				}
				return false;
			}();
			if (!albumForward) {
				return tr::lng_slowmode_no_many(tr::now);
			}
		}
	}
	if (const auto left = peer->slowmodeSecondsLeft()) {
		if (!ignoreSlowmodeCountdown) {
			return tr::lng_slowmode_enabled(
				tr::now,
				lt_left,
				Ui::FormatDurationWords(left));
		}
	}

	return QString();
}

void FastShareMessage(not_null<HistoryItem*> item) {
	struct ShareData {
		ShareData(not_null<PeerData*> peer, MessageIdsList &&ids)
		: peer(peer)
		, msgIds(std::move(ids)) {
		}
		not_null<PeerData*> peer;
		MessageIdsList msgIds;
		base::flat_set<mtpRequestId> requests;
	};
	const auto history = item->history();
	const auto owner = &history->owner();
	const auto session = &history->session();
	const auto data = std::make_shared<ShareData>(
		history->peer,
		owner->itemOrItsGroup(item));
	const auto isGame = item->getMessageBot()
		&& item->media()
		&& (item->media()->game() != nullptr);
	const auto canCopyLink = item->hasDirectLink() || isGame;

	auto copyCallback = [=]() {
		if (const auto item = owner->message(data->msgIds[0])) {
			if (item->hasDirectLink()) {
				HistoryView::CopyPostLink(
					session,
					item->fullId(),
					HistoryView::Context::History);
			} else if (const auto bot = item->getMessageBot()) {
				if (const auto media = item->media()) {
					if (const auto game = media->game()) {
						const auto link = session->createInternalLinkFull(
							bot->username
							+ qsl("?game=")
							+ game->shortName);

						QGuiApplication::clipboard()->setText(link);

						Ui::Toast::Show(tr::lng_share_game_link_copied(tr::now));
					}
				}
			}
		}
	};
	auto submitCallback = [=](
			std::vector<not_null<PeerData*>> &&result,
			TextWithTags &&comment,
			Api::SendOptions options) {
		if (!data->requests.empty()) {
			return; // Share clicked already.
		}
		auto items = history->owner().idsToItems(data->msgIds);
		if (items.empty() || result.empty()) {
			return;
		}

		const auto error = [&] {
			for (const auto peer : result) {
				const auto error = GetErrorTextForSending(
					peer,
					items,
					comment);
				if (!error.isEmpty()) {
					return std::make_pair(error, peer);
				}
			}
			return std::make_pair(QString(), result.front());
		}();
		if (!error.first.isEmpty()) {
			auto text = TextWithEntities();
			if (result.size() > 1) {
				text.append(
					Ui::Text::Bold(error.second->name)
				).append("\n\n");
			}
			text.append(error.first);
			Ui::show(
				Box<InformBox>(text),
				Ui::LayerOption::KeepOther);
			return;
		}

		const auto commonSendFlags = MTPmessages_ForwardMessages::Flag(0)
			| MTPmessages_ForwardMessages::Flag::f_with_my_score
			| (options.scheduled
				? MTPmessages_ForwardMessages::Flag::f_schedule_date
				: MTPmessages_ForwardMessages::Flag(0));
		auto msgIds = QVector<MTPint>();
		msgIds.reserve(data->msgIds.size());
		for (const auto fullId : data->msgIds) {
			msgIds.push_back(MTP_int(fullId.msg));
		}
		auto generateRandom = [&] {
			auto result = QVector<MTPlong>(data->msgIds.size());
			for (auto &value : result) {
				value = openssl::RandomValue<MTPlong>();
			}
			return result;
		};
		auto &api = owner->session().api();
		auto &histories = owner->histories();
		const auto requestType = Data::Histories::RequestType::Send;
		for (const auto peer : result) {
			const auto history = owner->history(peer);
			if (!comment.text.isEmpty()) {
				auto message = ApiWrap::MessageToSend(history);
				message.textWithTags = comment;
				message.action.options = options;
				message.action.clearDraft = false;
				api.sendMessage(std::move(message));
			}
			histories.sendRequest(history, requestType, [=](Fn<void()> finish) {
				auto &api = history->session().api();
				const auto sendFlags = commonSendFlags
					| (ShouldSendSilent(peer, options)
						? MTPmessages_ForwardMessages::Flag::f_silent
						: MTPmessages_ForwardMessages::Flag(0));
				history->sendRequestId = api.request(MTPmessages_ForwardMessages(
					MTP_flags(sendFlags),
					data->peer->input,
					MTP_vector<MTPint>(msgIds),
					MTP_vector<MTPlong>(generateRandom()),
					peer->input,
					MTP_int(options.scheduled)
				)).done([=](const MTPUpdates &updates, mtpRequestId requestId) {
					history->session().api().applyUpdates(updates);
					data->requests.remove(requestId);
					if (data->requests.empty()) {
						Ui::Toast::Show(tr::lng_share_done(tr::now));
						Ui::hideLayer();
					}
					finish();
				}).fail([=](const MTP::Error &error) {
					finish();
				}).afterRequest(history->sendRequestId).send();
				return history->sendRequestId;
			});
			data->requests.insert(history->sendRequestId);
		}
	};
	auto filterCallback = [isGame](PeerData *peer) {
		if (peer->canWrite()) {
			if (auto channel = peer->asChannel()) {
				return isGame ? (!channel->isBroadcast()) : true;
			}
			return true;
		}
		return false;
	};
	auto copyLinkCallback = canCopyLink
		? Fn<void()>(std::move(copyCallback))
		: Fn<void()>();
	Ui::show(Box<ShareBox>(ShareBox::Descriptor{
		.session = session,
		.copyCallback = std::move(copyLinkCallback),
		.submitCallback = std::move(submitCallback),
		.filterCallback = std::move(filterCallback),
		.navigation = App::wnd()->sessionController() }));
}

Fn<void(ChannelData*, MsgId)> HistoryDependentItemCallback(
		not_null<HistoryItem*> item) {
	const auto session = &item->history()->session();
	const auto dependent = item->fullId();
	return [=](ChannelData *channel, MsgId msgId) {
		if (const auto item = session->data().message(dependent)) {
			item->updateDependencyItem();
		}
	};
}

MTPDmessage::Flags NewMessageFlags(not_null<PeerData*> peer) {
	MTPDmessage::Flags result = 0;
	if (!peer->isSelf()) {
		result |= MTPDmessage::Flag::f_out;
		//if (p->isChat() || (p->isUser() && !p->asUser()->isBot())) {
		//	result |= MTPDmessage::Flag::f_unread;
		//}
	}
	return result;
}

bool ShouldSendSilent(
		not_null<PeerData*> peer,
		const Api::SendOptions &options) {
	return options.silent
		|| (peer->isBroadcast() && peer->owner().notifySilentPosts(peer))
		|| (peer->session().supportMode()
			&& peer->session().settings().supportAllSilent());
}

MsgId LookupReplyToTop(not_null<History*> history, MsgId replyToId) {
	const auto &owner = history->owner();
	if (const auto item = owner.message(history->channelId(), replyToId)) {
		return item->replyToTop();
	}
	return 0;
}

MTPMessageReplyHeader NewMessageReplyHeader(const Api::SendAction &action) {
	if (const auto id = action.replyTo) {
		if (const auto replyToTop = LookupReplyToTop(action.history, id)) {
			return MTP_messageReplyHeader(
				MTP_flags(MTPDmessageReplyHeader::Flag::f_reply_to_top_id),
				MTP_int(id),
				MTPPeer(),
				MTP_int(replyToTop));
		}
		return MTP_messageReplyHeader(
			MTP_flags(0),
			MTP_int(id),
			MTPPeer(),
			MTPint());
	}
	return MTPMessageReplyHeader();
}

MTPDmessage_ClientFlags NewMessageClientFlags() {
	return MTPDmessage_ClientFlag::f_sending;
}

QString GetErrorTextForSending(
		not_null<PeerData*> peer,
		const HistoryItemsList &items,
		bool ignoreSlowmodeCountdown) {
	return GetErrorTextForSending(peer, items, {}, ignoreSlowmodeCountdown);
}

struct HistoryMessage::CreateConfig {
	PeerId replyToPeer = 0;
	MsgId replyTo = 0;
	MsgId replyToTop = 0;
	UserId viaBotId = 0;
	int viewsCount = -1;
	QString author;
	PeerId senderOriginal = 0;
	QString senderNameOriginal;
	QString forwardPsaType;
	MsgId originalId = 0;
	PeerId savedFromPeer = 0;
	MsgId savedFromMsgId = 0;
	QString authorOriginal;
	TimeId originalDate = 0;
	TimeId editDate = 0;
	bool imported = false;

	// For messages created from MTP structs.
	const MTPMessageReplies *mtpReplies = nullptr;
	const MTPReplyMarkup *mtpMarkup = nullptr;

	// For messages created from existing messages (forwarded).
	const HistoryMessageReplyMarkup *inlineMarkup = nullptr;
};

void HistoryMessage::FillForwardedInfo(
		CreateConfig &config,
		const MTPDmessageFwdHeader &data) {
	if (const auto fromId = data.vfrom_id()) {
		config.senderOriginal = peerFromMTP(*fromId);
	}
	config.originalDate = data.vdate().v;
	config.senderNameOriginal = qs(data.vfrom_name().value_or_empty());
	config.forwardPsaType = qs(data.vpsa_type().value_or_empty());
	config.originalId = data.vchannel_post().value_or_empty();
	config.authorOriginal = qs(data.vpost_author().value_or_empty());
	const auto savedFromPeer = data.vsaved_from_peer();
	const auto savedFromMsgId = data.vsaved_from_msg_id();
	if (savedFromPeer && savedFromMsgId) {
		config.savedFromPeer = peerFromMTP(*savedFromPeer);
		config.savedFromMsgId = savedFromMsgId->v;
	}
	config.imported = data.is_imported();
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	const MTPDmessage &data,
	MTPDmessage_ClientFlags clientFlags)
: HistoryItem(
		history,
		data.vid().v,
		data.vflags().v,
		clientFlags,
		data.vdate().v,
		data.vfrom_id() ? peerFromMTP(*data.vfrom_id()) : PeerId(0)) {
	auto config = CreateConfig();
	if (const auto forwarded = data.vfwd_from()) {
		forwarded->match([&](const MTPDmessageFwdHeader &data) {
			FillForwardedInfo(config, data);
		});
	}
	if (const auto reply = data.vreply_to()) {
		reply->match([&](const MTPDmessageReplyHeader &data) {
			if (const auto peer = data.vreply_to_peer_id()) {
				config.replyToPeer = peerFromMTP(*peer);
				if (config.replyToPeer == history->peer->id) {
					config.replyToPeer = 0;
				}
			}
			config.replyTo = data.vreply_to_msg_id().v;
			config.replyToTop = data.vreply_to_top_id().value_or(
				config.replyTo);
		});
	}
	config.viaBotId = data.vvia_bot_id().value_or_empty();
	config.viewsCount = data.vviews().value_or(-1);
	config.mtpReplies = isScheduled() ? nullptr : data.vreplies();
	config.mtpMarkup = data.vreply_markup();
	config.editDate = data.vedit_date().value_or_empty();
	config.author = qs(data.vpost_author().value_or_empty());

	createComponents(config);

	if (const auto media = data.vmedia()) {
		setMedia(*media);
	}
	const auto textWithEntities = TextWithEntities{
		TextUtilities::Clean(qs(data.vmessage())),
		Api::EntitiesFromMTP(
			&history->session(),
			data.ventities().value_or_empty())
	};
	setText(_media ? textWithEntities : EnsureNonEmpty(textWithEntities));
	if (const auto groupedId = data.vgrouped_id()) {
		setGroupId(
			MessageGroupId::FromRaw(history->peer->id, groupedId->v));
	}

	applyTTL(data);
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	const MTPDmessageService &data,
	MTPDmessage_ClientFlags clientFlags)
: HistoryItem(
		history,
		data.vid().v,
		mtpCastFlags(data.vflags().v),
		clientFlags,
		data.vdate().v,
		data.vfrom_id() ? peerFromMTP(*data.vfrom_id()) : PeerId(0)) {
	auto config = CreateConfig();

	if (const auto reply = data.vreply_to()) {
		reply->match([&](const MTPDmessageReplyHeader &data) {
			const auto peer = data.vreply_to_peer_id()
				? peerFromMTP(*data.vreply_to_peer_id())
				: history->peer->id;
			if (!peer || peer == history->peer->id) {
				config.replyTo = data.vreply_to_msg_id().v;
				config.replyToTop = data.vreply_to_top_id().value_or(
					config.replyTo);
			}
		});
	}

	createComponents(config);

	data.vaction().match([&](const MTPDmessageActionPhoneCall &data) {
		_media = std::make_unique<Data::MediaCall>(this, data);
		setEmptyText();
	}, [](const auto &) {
		Unexpected("Service message action type in HistoryMessage.");
	});

	applyTTL(data);
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	MsgId id,
	MTPDmessage::Flags flags,
	MTPDmessage_ClientFlags clientFlags,
	TimeId date,
	PeerId from,
	const QString &postAuthor,
	not_null<HistoryMessage*> original)
: HistoryItem(
		history,
		id,
		NewForwardedFlags(history->peer, from, original) | flags,
		NewForwardedClientFlags() | clientFlags,
		date,
		from) {
	const auto peer = history->peer;

	auto config = CreateConfig();

	if (original->Has<HistoryMessageForwarded>() || !original->history()->peer->isSelf()) {
		// Server doesn't add "fwd_from" to non-forwarded messages from chat with yourself.
		config.originalDate = original->dateOriginal();
		if (const auto info = original->hiddenForwardedInfo()) {
			config.senderNameOriginal = info->name;
		} else if (const auto senderOriginal = original->senderOriginal()) {
			config.senderOriginal = senderOriginal->id;
			if (senderOriginal->isChannel()) {
				config.originalId = original->idOriginal();
			}
		} else {
			Unexpected("Corrupt forwarded information in message.");
		}
		config.authorOriginal = original->authorOriginal();
	}
	if (peer->isSelf()) {
		//
		// iOS app sends you to the original post if we forward a forward from channel.
		// But server returns not the original post but the forward in saved_from_...
		//
		//if (config.originalId) {
		//	config.savedFromPeer = config.senderOriginal;
		//	config.savedFromMsgId = config.originalId;
		//} else {
			config.savedFromPeer = original->history()->peer->id;
			config.savedFromMsgId = original->id;
		//}
	}
	if (flags & MTPDmessage::Flag::f_post_author) {
		config.author = postAuthor;
	}
	if (const auto fwdViaBot = original->viaBot()) {
		config.viaBotId = peerToUser(fwdViaBot->id);
	}
	const auto fwdViewsCount = original->viewsCount();
	if (fwdViewsCount > 0) {
		config.viewsCount = fwdViewsCount;
	} else if ((isPost() && !isScheduled())
		|| (original->senderOriginal()
			&& original->senderOriginal()->isChannel())) {
		config.viewsCount = 1;
	}

	const auto mediaOriginal = original->media();
	if (CopyMarkupToForward(original)) {
		config.inlineMarkup = original->inlineReplyMarkup();
	}

	createComponents(config);

	const auto ignoreMedia = [&] {
		if (mediaOriginal && mediaOriginal->webpage()) {
			if (peer->amRestricted(ChatRestriction::f_embed_links)) {
				return true;
			}
		}
		return false;
	};
	if (mediaOriginal && !ignoreMedia()) {
		_media = mediaOriginal->clone(this);
	}
	setText(original->originalText());
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	MsgId id,
	MTPDmessage::Flags flags,
	MTPDmessage_ClientFlags clientFlags,
	MsgId replyTo,
	UserId viaBotId,
	TimeId date,
	PeerId from,
	const QString &postAuthor,
	const TextWithEntities &textWithEntities)
: HistoryItem(
		history,
		id,
		flags & ~MTPDmessage::Flag::f_reply_markup,
		clientFlags,
		date,
		(flags & MTPDmessage::Flag::f_from_id) ? from : 0) {
	createComponentsHelper(
		flags & ~MTPDmessage::Flag::f_reply_markup,
		replyTo,
		viaBotId,
		postAuthor,
		MTPReplyMarkup());

	setText(textWithEntities);
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	MsgId id,
	MTPDmessage::Flags flags,
	MTPDmessage_ClientFlags clientFlags,
	MsgId replyTo,
	UserId viaBotId,
	TimeId date,
	PeerId from,
	const QString &postAuthor,
	not_null<DocumentData*> document,
	const TextWithEntities &caption,
	const MTPReplyMarkup &markup)
: HistoryItem(
		history,
		id,
		flags,
		clientFlags,
		date,
		(flags & MTPDmessage::Flag::f_from_id) ? from : 0) {
	createComponentsHelper(flags, replyTo, viaBotId, postAuthor, markup);

	_media = std::make_unique<Data::MediaFile>(this, document);
	setText(caption);
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	MsgId id,
	MTPDmessage::Flags flags,
	MTPDmessage_ClientFlags clientFlags,
	MsgId replyTo,
	UserId viaBotId,
	TimeId date,
	PeerId from,
	const QString &postAuthor,
	not_null<PhotoData*> photo,
	const TextWithEntities &caption,
	const MTPReplyMarkup &markup)
: HistoryItem(
		history,
		id,
		flags,
		clientFlags,
		date,
		(flags & MTPDmessage::Flag::f_from_id) ? from : 0) {
	createComponentsHelper(flags, replyTo, viaBotId, postAuthor, markup);

	_media = std::make_unique<Data::MediaPhoto>(this, photo);
	setText(caption);
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	MsgId id,
	MTPDmessage::Flags flags,
	MTPDmessage_ClientFlags clientFlags,
	MsgId replyTo,
	UserId viaBotId,
	TimeId date,
	PeerId from,
	const QString &postAuthor,
	not_null<GameData*> game,
	const MTPReplyMarkup &markup)
: HistoryItem(
		history,
		id,
		flags,
		clientFlags,
		date,
		(flags & MTPDmessage::Flag::f_from_id) ? from : 0) {
	createComponentsHelper(flags, replyTo, viaBotId, postAuthor, markup);

	_media = std::make_unique<Data::MediaGame>(this, game);
	setEmptyText();
}

void HistoryMessage::createComponentsHelper(
		MTPDmessage::Flags flags,
		MsgId replyTo,
		UserId viaBotId,
		const QString &postAuthor,
		const MTPReplyMarkup &markup) {
	auto config = CreateConfig();

	if (flags & MTPDmessage::Flag::f_via_bot_id) config.viaBotId = viaBotId;
	if (flags & MTPDmessage::Flag::f_reply_to) {
		config.replyTo = replyTo;
		const auto replyToTop = LookupReplyToTop(history(), replyTo);
		config.replyToTop = replyToTop ? replyToTop : replyTo;
	}
	if (flags & MTPDmessage::Flag::f_reply_markup) config.mtpMarkup = &markup;
	if (flags & MTPDmessage::Flag::f_post_author) config.author = postAuthor;
	if (flags & MTPDmessage::Flag::f_views) config.viewsCount = 1;

	createComponents(config);
}

int HistoryMessage::viewsCount() const {
	if (const auto views = Get<HistoryMessageViews>()) {
		return std::max(views->views.count, 0);
	}
	return HistoryItem::viewsCount();
}

bool HistoryMessage::checkCommentsLinkedChat(ChannelId id) const {
	if (!id) {
		return true;
	} else if (const auto channel = history()->peer->asChannel()) {
		if (channel->linkedChatKnown()
			|| !(channel->flags() & MTPDchannel::Flag::f_has_link)) {
			const auto linked = channel->linkedChat();
			if (!linked || peerToChannel(linked->id) != id) {
				return false;
			}
		}
		return true;
	}
	return false;
}

int HistoryMessage::repliesCount() const {
	if (const auto views = Get<HistoryMessageViews>()) {
		if (!checkCommentsLinkedChat(views->commentsMegagroupId)) {
			return 0;
		}
		return std::max(views->replies.count, 0);
	}
	return HistoryItem::repliesCount();
}

bool HistoryMessage::repliesAreComments() const {
	if (const auto views = Get<HistoryMessageViews>()) {
		return (views->commentsMegagroupId != 0)
			&& checkCommentsLinkedChat(views->commentsMegagroupId);
	}
	return HistoryItem::repliesAreComments();
}

bool HistoryMessage::externalReply() const {
	if (!history()->peer->isRepliesChat()) {
		return false;
	} else if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		return forwarded->savedFromPeer && forwarded->savedFromMsgId;
	}
	return false;
}

MsgId HistoryMessage::repliesInboxReadTill() const {
	if (const auto views = Get<HistoryMessageViews>()) {
		return views->repliesInboxReadTillId;
	}
	return 0;
}

void HistoryMessage::setRepliesInboxReadTill(MsgId readTillId) {
	if (const auto views = Get<HistoryMessageViews>()) {
		const auto newReadTillId = std::max(readTillId, 1);
		if (newReadTillId > views->repliesInboxReadTillId) {
			const auto wasUnread = repliesAreComments() && areRepliesUnread();
			views->repliesInboxReadTillId = newReadTillId;
			if (wasUnread && !areRepliesUnread()) {
				history()->owner().requestItemRepaint(this);
			}
		}
	}
}

MsgId HistoryMessage::computeRepliesInboxReadTillFull() const {
	const auto views = Get<HistoryMessageViews>();
	if (!views) {
		return 0;
	}
	const auto local = views->repliesInboxReadTillId;
	const auto group = views->commentsMegagroupId
		? history()->owner().historyLoaded(
			peerFromChannel(views->commentsMegagroupId))
		: history().get();
	if (const auto megagroup = group->peer->asChannel()) {
		if (megagroup->amIn()) {
			return std::max(local, group->inboxReadTillId());
		}
	}
	return local;
}

MsgId HistoryMessage::repliesOutboxReadTill() const {
	if (const auto views = Get<HistoryMessageViews>()) {
		return views->repliesOutboxReadTillId;
	}
	return 0;
}

void HistoryMessage::setRepliesOutboxReadTill(MsgId readTillId) {
	if (const auto views = Get<HistoryMessageViews>()) {
		const auto newReadTillId = std::max(readTillId, 1);
		if (newReadTillId > views->repliesOutboxReadTillId) {
			views->repliesOutboxReadTillId = newReadTillId;
			if (!repliesAreComments()) {
				history()->session().changes().historyUpdated(
					history(),
					Data::HistoryUpdate::Flag::OutboxRead);
			}
		}
	}
}

MsgId HistoryMessage::computeRepliesOutboxReadTillFull() const {
	const auto views = Get<HistoryMessageViews>();
	if (!views) {
		return 0;
	}
	const auto local = views->repliesOutboxReadTillId;
	const auto group = views->commentsMegagroupId
		? history()->owner().historyLoaded(
			peerFromChannel(views->commentsMegagroupId))
		: history().get();
	if (const auto megagroup = group->peer->asChannel()) {
		if (megagroup->amIn()) {
			return std::max(local, group->outboxReadTillId());
		}
	}
	return local;
}

void HistoryMessage::setRepliesMaxId(MsgId maxId) {
	if (const auto views = Get<HistoryMessageViews>()) {
		if (views->repliesMaxId != maxId) {
			const auto comments = repliesAreComments();
			const auto wasUnread = comments && areRepliesUnread();
			views->repliesMaxId = maxId;
			if (comments && wasUnread != areRepliesUnread()) {
				history()->owner().requestItemRepaint(this);
			}
		}
	}
}

void HistoryMessage::setRepliesPossibleMaxId(MsgId possibleMaxId) {
	if (const auto views = Get<HistoryMessageViews>()) {
		if (views->repliesMaxId < possibleMaxId) {
			const auto comments = repliesAreComments();
			const auto wasUnread = comments && areRepliesUnread();
			views->repliesMaxId = possibleMaxId;
			if (comments && !wasUnread && areRepliesUnread()) {
				history()->owner().requestItemRepaint(this);
			}
		}
	}
}

bool HistoryMessage::areRepliesUnread() const {
	const auto views = Get<HistoryMessageViews>();
	if (!views) {
		return false;
	}
	const auto local = views->repliesInboxReadTillId;
	if (views->repliesInboxReadTillId < 2 || views->repliesMaxId <= local) {
		return false;
	}
	const auto group = views->commentsMegagroupId
		? history()->owner().historyLoaded(
			peerFromChannel(views->commentsMegagroupId))
		: history().get();
	return !group || (views->repliesMaxId > group->inboxReadTillId());
}

FullMsgId HistoryMessage::commentsItemId() const {
	if (const auto views = Get<HistoryMessageViews>()) {
		return FullMsgId(views->commentsMegagroupId, views->commentsRootId);
	}
	return FullMsgId();
}

void HistoryMessage::setCommentsItemId(FullMsgId id) {
	if (id.channel == _history->channelId()) {
		if (id.msg != this->id) {
			if (const auto reply = Get<HistoryMessageReply>()) {
				reply->replyToMsgTop = id.msg;
			}
		}
		return;
	} else if (const auto views = Get<HistoryMessageViews>()) {
		if (views->commentsMegagroupId != id.channel) {
			views->commentsMegagroupId = id.channel;
			history()->owner().requestItemResize(this);
		}
		views->commentsRootId = id.msg;
	}
}

bool HistoryMessage::updateDependencyItem() {
	if (const auto reply = Get<HistoryMessageReply>()) {
		const auto documentId = reply->replyToDocumentId;
		const auto result = reply->updateData(this, true);
		if (documentId != reply->replyToDocumentId
			&& generateLocalEntitiesByReply()) {
			reapplyText();
		}
		return result;
	}
	return true;
}

void HistoryMessage::applySentMessage(const MTPDmessage &data) {
	HistoryItem::applySentMessage(data);

	if (const auto period = data.vttl_period(); period && period->v > 0) {
		applyTTL(data.vdate().v + period->v);
	} else {
		applyTTL(0);
	}
}

void HistoryMessage::applySentMessage(
		const QString &text,
		const MTPDupdateShortSentMessage &data,
		bool wasAlready) {
	HistoryItem::applySentMessage(text, data, wasAlready);

	if (const auto period = data.vttl_period(); period && period->v > 0) {
		applyTTL(data.vdate().v + period->v);
	} else {
		applyTTL(0);
	}
}

bool HistoryMessage::allowsForward() const {
	if (id < 0 || !isHistoryEntry()) {
		return false;
	}
	return !_media || _media->allowsForward();
}

bool HistoryMessage::allowsSendNow() const {
	return isScheduled() && !isSending() && !hasFailed() && !isEditingMedia();
}

bool HistoryMessage::isTooOldForEdit(TimeId now) const {
	return !_history->peer->canEditMessagesIndefinitely()
		&& !isScheduled()
		&& (now - date() >= _history->session().serverConfig().editTimeLimit);
}

bool HistoryMessage::allowsEdit(TimeId now) const {
	return canStopPoll()
		&& !isTooOldForEdit(now)
		&& (!_media || _media->allowsEdit())
		&& !isLegacyMessage()
		&& !isEditingMedia();
}

bool HistoryMessage::uploading() const {
	return _media && _media->uploading();
}

void HistoryMessage::createComponents(const CreateConfig &config) {
	uint64 mask = 0;
	if (config.replyTo) {
		mask |= HistoryMessageReply::Bit();
	}
	if (config.viaBotId) {
		mask |= HistoryMessageVia::Bit();
	}
	if (config.viewsCount >= 0 || config.mtpReplies) {
		mask |= HistoryMessageViews::Bit();
	}
	if (!config.author.isEmpty()) {
		mask |= HistoryMessageSigned::Bit();
	} else if (_history->peer->isMegagroup() // Discussion posts signatures.
		&& config.savedFromPeer
		&& !config.authorOriginal.isEmpty()) {
		const auto savedFrom = _history->owner().peerLoaded(
			config.savedFromPeer);
		if (savedFrom && savedFrom->isChannel()) {
			mask |= HistoryMessageSigned::Bit();
		}
	} else if ((_history->peer->isSelf() || _history->peer->isRepliesChat())
		&& !config.authorOriginal.isEmpty()) {
		mask |= HistoryMessageSigned::Bit();
	}
	if (config.editDate != TimeId(0)) {
		mask |= HistoryMessageEdited::Bit();
	}
	if (config.originalDate != 0) {
		mask |= HistoryMessageForwarded::Bit();
	}
	if (config.mtpMarkup) {
		// optimization: don't create markup component for the case
		// MTPDreplyKeyboardHide with flags = 0, assume it has f_zero flag
		if (config.mtpMarkup->type() != mtpc_replyKeyboardHide || config.mtpMarkup->c_replyKeyboardHide().vflags().v != 0) {
			mask |= HistoryMessageReplyMarkup::Bit();
		}
	} else if (config.inlineMarkup) {
		mask |= HistoryMessageReplyMarkup::Bit();
	}

	UpdateComponents(mask);

	if (const auto reply = Get<HistoryMessageReply>()) {
		reply->replyToPeerId = config.replyToPeer;
		reply->replyToMsgId = config.replyTo;
		reply->replyToMsgTop = isScheduled() ? 0 : config.replyToTop;
		if (!reply->updateData(this)) {
			history()->session().api().requestMessageData(
				(peerIsChannel(reply->replyToPeerId)
					? history()->owner().channel(
						peerToChannel(reply->replyToPeerId)).get()
					: history()->peer->asChannel()),
				reply->replyToMsgId,
				HistoryDependentItemCallback(this));
		}
	}
	if (const auto via = Get<HistoryMessageVia>()) {
		via->create(&history()->owner(), config.viaBotId);
	}
	if (const auto views = Get<HistoryMessageViews>()) {
		setViewsCount(config.viewsCount);
		if (config.mtpReplies) {
			setReplies(*config.mtpReplies);
		} else if (isSending() && !config.mtpMarkup) {
			if (const auto broadcast = history()->peer->asBroadcast()) {
				if (const auto linked = broadcast->linkedChat()) {
					setReplies(MTP_messageReplies(
						MTP_flags(MTPDmessageReplies::Flag::f_comments
							| MTPDmessageReplies::Flag::f_comments),
						MTP_int(0),
						MTP_int(0),
						MTPVector<MTPPeer>(), // recent_repliers
						MTP_int(peerToChannel(linked->id).bare),
						MTP_int(0), // max_id
						MTP_int(0))); // read_max_id
				}
			}
		}
	}
	if (const auto edited = Get<HistoryMessageEdited>()) {
		edited->date = config.editDate;
	}
	if (const auto msgsigned = Get<HistoryMessageSigned>()) {
		msgsigned->author = config.author.isEmpty()
			? config.authorOriginal
			: config.author;
		msgsigned->isAnonymousRank = !isDiscussionPost()
			&& author()->isMegagroup();
	}
	setupForwardedComponent(config);
	if (const auto markup = Get<HistoryMessageReplyMarkup>()) {
		if (config.mtpMarkup) {
			markup->create(*config.mtpMarkup);
		} else if (config.inlineMarkup) {
			markup->create(*config.inlineMarkup);
		}
		if (markup->flags & MTPDreplyKeyboardMarkup_ClientFlag::f_has_switch_inline_button) {
			_clientFlags |= MTPDmessage_ClientFlag::f_has_switch_inline_button;
		}
	}
	const auto from = displayFrom();
	_fromNameVersion = from ? from->nameVersion : 1;
}

bool HistoryMessage::checkRepliesPts(const MTPMessageReplies &data) const {
	const auto channel = history()->peer->asChannel();
	const auto pts = channel
		? channel->pts()
		: history()->session().updates().pts();
	const auto repliesPts = data.match([&](const MTPDmessageReplies &data) {
		return data.vreplies_pts().v;
	});
	return (repliesPts >= pts);
}

void HistoryMessage::setupForwardedComponent(const CreateConfig &config) {
	const auto forwarded = Get<HistoryMessageForwarded>();
	if (!forwarded) {
		return;
	}
	forwarded->originalDate = config.originalDate;
	forwarded->originalSender = config.senderOriginal
		? history()->owner().peer(config.senderOriginal).get()
		: nullptr;
	if (!forwarded->originalSender) {
		forwarded->hiddenSenderInfo = std::make_unique<HiddenSenderInfo>(
			config.senderNameOriginal,
			config.imported);
	}
	forwarded->originalId = config.originalId;
	forwarded->originalAuthor = config.authorOriginal;
	forwarded->psaType = config.forwardPsaType;
	forwarded->savedFromPeer = history()->owner().peerLoaded(
		config.savedFromPeer);
	forwarded->savedFromMsgId = config.savedFromMsgId;
	forwarded->imported = config.imported;
}

void HistoryMessage::refreshMedia(const MTPMessageMedia *media) {
	const auto was = (_media != nullptr);
	_media = nullptr;
	if (media) {
		setMedia(*media);
	}
	if (was || _media) {
		if (const auto views = Get<HistoryMessageViews>()) {
			refreshRepliesText(views);
		}
	}
}

void HistoryMessage::refreshSentMedia(const MTPMessageMedia *media) {
	const auto wasGrouped = history()->owner().groups().isGrouped(this);
	refreshMedia(media);
	if (wasGrouped) {
		history()->owner().groups().refreshMessage(this);
	} else {
		history()->owner().requestItemViewRefresh(this);
	}
}

void HistoryMessage::returnSavedMedia() {
	if (!isEditingMedia()) {
		return;
	}
	const auto wasGrouped = history()->owner().groups().isGrouped(this);
	_media = std::move(_savedLocalEditMediaData.media);
	setText(_savedLocalEditMediaData.text);
	clearSavedMedia();
	if (wasGrouped) {
		history()->owner().groups().refreshMessage(this, true);
	} else {
		history()->owner().requestItemViewRefresh(this);
		history()->owner().updateDependentMessages(this);
	}
}

void HistoryMessage::setMedia(const MTPMessageMedia &media) {
	_media = CreateMedia(this, media);
	checkBuyButton();
}

void HistoryMessage::checkBuyButton() {
	if (const auto invoice = _media ? _media->invoice() : nullptr) {
		if (invoice->receiptMsgId) {
			replaceBuyWithReceiptInMarkup();
		}
	}
}

std::unique_ptr<Data::Media> HistoryMessage::CreateMedia(
		not_null<HistoryMessage*> item,
		const MTPMessageMedia &media) {
	using Result = std::unique_ptr<Data::Media>;
	return media.match([&](const MTPDmessageMediaContact &media) -> Result {
		return std::make_unique<Data::MediaContact>(
			item,
			media.vuser_id().v,
			qs(media.vfirst_name()),
			qs(media.vlast_name()),
			qs(media.vphone_number()));
	}, [&](const MTPDmessageMediaGeo &media) -> Result {
		return media.vgeo().match([&](const MTPDgeoPoint &point) -> Result {
			return std::make_unique<Data::MediaLocation>(
				item,
				Data::LocationPoint(point));
		}, [](const MTPDgeoPointEmpty &) -> Result {
			return nullptr;
		});
	}, [&](const MTPDmessageMediaGeoLive &media) -> Result {
		return media.vgeo().match([&](const MTPDgeoPoint &point) -> Result {
			return std::make_unique<Data::MediaLocation>(
				item,
				Data::LocationPoint(point));
		}, [](const MTPDgeoPointEmpty &) -> Result {
			return nullptr;
		});
	}, [&](const MTPDmessageMediaVenue &media) -> Result {
		return media.vgeo().match([&](const MTPDgeoPoint &point) -> Result {
			return std::make_unique<Data::MediaLocation>(
				item,
				Data::LocationPoint(point),
				qs(media.vtitle()),
				qs(media.vaddress()));
		}, [](const MTPDgeoPointEmpty &data) -> Result {
			return nullptr;
		});
	}, [&](const MTPDmessageMediaPhoto &media) -> Result {
		const auto photo = media.vphoto();
		if (media.vttl_seconds()) {
			LOG(("App Error: "
				"Unexpected MTPMessageMediaPhoto "
				"with ttl_seconds in HistoryMessage."));
			return nullptr;
		} else if (!photo) {
			LOG(("API Error: "
				"Got MTPMessageMediaPhoto "
				"without photo and without ttl_seconds."));
			return nullptr;
		}
		return photo->match([&](const MTPDphoto &photo) -> Result {
			return std::make_unique<Data::MediaPhoto>(
				item,
				item->history()->owner().processPhoto(photo));
		}, [](const MTPDphotoEmpty &) -> Result {
			return nullptr;
		});
	}, [&](const MTPDmessageMediaDocument &media) -> Result {
		const auto document = media.vdocument();
		if (media.vttl_seconds()) {
			LOG(("App Error: "
				"Unexpected MTPMessageMediaDocument "
				"with ttl_seconds in HistoryMessage."));
			return nullptr;
		} else if (!document) {
			LOG(("API Error: "
				"Got MTPMessageMediaDocument "
				"without document and without ttl_seconds."));
			return nullptr;
		}
		return document->match([&](const MTPDdocument &document) -> Result {
			return std::make_unique<Data::MediaFile>(
				item,
				item->history()->owner().processDocument(document));
		}, [](const MTPDdocumentEmpty &) -> Result {
			return nullptr;
		});
	}, [&](const MTPDmessageMediaWebPage &media) {
		return media.vwebpage().match([](const MTPDwebPageEmpty &) -> Result {
			return nullptr;
		}, [&](const MTPDwebPagePending &webpage) -> Result {
			return std::make_unique<Data::MediaWebPage>(
				item,
				item->history()->owner().processWebpage(webpage));
		}, [&](const MTPDwebPage &webpage) -> Result {
			return std::make_unique<Data::MediaWebPage>(
				item,
				item->history()->owner().processWebpage(webpage));
		}, [](const MTPDwebPageNotModified &) -> Result {
			LOG(("API Error: "
				"webPageNotModified is unexpected in message media."));
			return nullptr;
		});
	}, [&](const MTPDmessageMediaGame &media) -> Result {
		return media.vgame().match([&](const MTPDgame &game) {
			return std::make_unique<Data::MediaGame>(
				item,
				item->history()->owner().processGame(game));
		});
	}, [&](const MTPDmessageMediaInvoice &media) -> Result {
		return std::make_unique<Data::MediaInvoice>(item, media);
	}, [&](const MTPDmessageMediaPoll &media) -> Result {
		return std::make_unique<Data::MediaPoll>(
			item,
			item->history()->owner().processPoll(media));
	}, [&](const MTPDmessageMediaDice &media) -> Result {
		return std::make_unique<Data::MediaDice>(
			item,
			qs(media.vemoticon()),
			media.vvalue().v);
	}, [](const MTPDmessageMediaEmpty &) -> Result {
		return nullptr;
	}, [](const MTPDmessageMediaUnsupported &) -> Result {
		return nullptr;
	});
	return nullptr;
}

void HistoryMessage::replaceBuyWithReceiptInMarkup() {
	if (const auto markup = inlineReplyMarkup()) {
		for (auto &row : markup->rows) {
			for (auto &button : row) {
				if (button.type == HistoryMessageMarkupButton::Type::Buy) {
					const auto receipt = tr::lng_payments_receipt_button(tr::now);
					if (button.text != receipt) {
						button.text = receipt;
						if (markup->inlineKeyboard) {
							markup->inlineKeyboard = nullptr;
							history()->owner().requestItemResize(this);
						}
					}
				}
			}
		}
	}
}

void HistoryMessage::applyEdition(const MTPDmessage &message) {
	int keyboardTop = -1;
	//if (!pendingResize()) {// #TODO edit bot message
	//	if (auto keyboard = inlineReplyKeyboard()) {
	//		int h = st::msgBotKbButton.margin + keyboard->naturalHeight();
	//		keyboardTop = _height - h + st::msgBotKbButton.margin - marginBottom();
	//	}
	//}

	const auto copyFlags = MTPDmessage::Flag::f_edit_hide;
	_flags = (_flags & ~copyFlags) | (message.vflags().v & copyFlags);

	if (const auto editDate = message.vedit_date()) {
		_flags |= MTPDmessage::Flag::f_edit_date;
		if (!Has<HistoryMessageEdited>()) {
			AddComponents(HistoryMessageEdited::Bit());
		}
		auto edited = Get<HistoryMessageEdited>();
		edited->date = editDate->v;
	}

	const auto textWithEntities = TextWithEntities{
		qs(message.vmessage()),
		Api::EntitiesFromMTP(
			&history()->session(),
			message.ventities().value_or_empty())
	};
	setReplyMarkup(message.vreply_markup());
	if (!isLocalUpdateMedia()) {
		refreshMedia(message.vmedia());
	}
	setViewsCount(message.vviews().value_or(-1));
	setForwardsCount(message.vforwards().value_or(-1));
	setText(_media ? textWithEntities : EnsureNonEmpty(textWithEntities));
	if (const auto replies = message.vreplies()) {
		if (checkRepliesPts(*replies)) {
			setReplies(*replies);
		}
	} else {
		clearReplies();
	}

	if (const auto period = message.vttl_period(); period && period->v > 0) {
		applyTTL(message.vdate().v + period->v);
	} else {
		applyTTL(0);
	}

	finishEdition(keyboardTop);
}

void HistoryMessage::applyEdition(const MTPDmessageService &message) {
	if (message.vaction().type() == mtpc_messageActionHistoryClear) {
		const auto wasGrouped = history()->owner().groups().isGrouped(this);
		setReplyMarkup(nullptr);
		refreshMedia(nullptr);
		setEmptyText();
		setViewsCount(-1);
		setForwardsCount(-1);
		if (wasGrouped) {
			history()->owner().groups().unregisterMessage(this);
		}
		finishEditionToEmpty();
	}
}

void HistoryMessage::updateSentContent(
		const TextWithEntities &textWithEntities,
		const MTPMessageMedia *media) {
	const auto isolated = isolatedEmoji();
	setText(textWithEntities);
	if (_clientFlags & MTPDmessage_ClientFlag::f_from_inline_bot) {
		if (!media || !_media || !_media->updateInlineResultMedia(*media)) {
			refreshSentMedia(media);
		}
		_clientFlags &= ~MTPDmessage_ClientFlag::f_from_inline_bot;
	} else if (media || _media || !isolated || isolated != isolatedEmoji()) {
		if (!media || !_media || !_media->updateSentMedia(*media)) {
			refreshSentMedia(media);
		}
	}
	history()->owner().requestItemResize(this);
}

void HistoryMessage::updateForwardedInfo(const MTPMessageFwdHeader *fwd) {
	const auto forwarded = Get<HistoryMessageForwarded>();
	if (!fwd) {
		if (forwarded) {
			LOG(("API Error: Server removed forwarded information."));
		}
		return;
	} else if (!forwarded) {
		LOG(("API Error: Server added forwarded information."));
		return;
	}
	fwd->match([&](const MTPDmessageFwdHeader &data) {
		auto config = CreateConfig();
		FillForwardedInfo(config, data);
		setupForwardedComponent(config);
		history()->owner().requestItemResize(this);
	});
}

void HistoryMessage::contributeToSlowmode(TimeId realDate) {
	if (const auto channel = history()->peer->asChannel()) {
		if (out() && IsServerMsgId(id)) {
			channel->growSlowmodeLastMessage(realDate ? realDate : date());
		}
	}
}

void HistoryMessage::addToUnreadMentions(UnreadMentionType type) {
	if (IsServerMsgId(id) && isUnreadMention()) {
		if (history()->addToUnreadMentions(id, type)) {
			history()->session().changes().historyUpdated(
				history(),
				Data::HistoryUpdate::Flag::UnreadMentions);
		}
	}
}

void HistoryMessage::destroyHistoryEntry() {
	if (isUnreadMention()) {
		history()->eraseFromUnreadMentions(id);
	}
	if (const auto reply = Get<HistoryMessageReply>()) {
		changeReplyToTopCounter(reply, -1);
	}
}

Storage::SharedMediaTypesMask HistoryMessage::sharedMediaTypes() const {
	auto result = Storage::SharedMediaTypesMask {};
	if (const auto media = this->media()) {
		result.set(media->sharedMediaTypes());
	}
	if (hasTextLinks()) {
		result.set(Storage::SharedMediaType::Link);
	}
	if (isPinned()) {
		result.set(Storage::SharedMediaType::Pinned);
	}
	return result;
}

bool HistoryMessage::generateLocalEntitiesByReply() const {
	return !_media || _media->webpage();
}

TextWithEntities HistoryMessage::withLocalEntities(
		const TextWithEntities &textWithEntities) const {
	if (!generateLocalEntitiesByReply()) {
		return textWithEntities;
	}
	if (const auto reply = Get<HistoryMessageReply>()) {
		const auto document = reply->replyToDocumentId
			? history()->owner().document(reply->replyToDocumentId).get()
			: nullptr;
		if (document
			&& (document->isVideoFile()
				|| document->isSong()
				|| document->isVoiceMessage())) {
			using namespace HistoryView;
			const auto duration = document->getDuration();
			const auto base = (duration > 0)
				? DocumentTimestampLinkBase(
					document,
					reply->replyToMsg->fullId())
				: QString();
			if (!base.isEmpty()) {
				return AddTimestampLinks(
					textWithEntities,
					duration,
					base);
			}
		}
	}
	return textWithEntities;
}

void HistoryMessage::setText(const TextWithEntities &textWithEntities) {
	for (const auto &entity : textWithEntities.entities) {
		auto type = entity.type();
		if (type == EntityType::Url
			|| type == EntityType::CustomUrl
			|| type == EntityType::Email) {
			_clientFlags |= MTPDmessage_ClientFlag::f_has_text_links;
			break;
		}
	}

	if (_media && _media->consumeMessageText(textWithEntities)) {
		setEmptyText();
		return;
	}

	clearIsolatedEmoji();
	const auto context = Core::MarkedTextContext{
		.session = &history()->session()
	};
	_text.setMarkedText(
		st::messageTextStyle,
		withLocalEntities(textWithEntities),
		Ui::ItemTextOptions(this),
		context);
	if (!textWithEntities.text.isEmpty() && _text.isEmpty()) {
		// If server has allowed some text that we've trim-ed entirely,
		// just replace it with something so that UI won't look buggy.
		_text.setMarkedText(
			st::messageTextStyle,
			EnsureNonEmpty(),
			Ui::ItemTextOptions(this));
	} else if (!_media) {
		checkIsolatedEmoji();
	}

	_textWidth = -1;
	_textHeight = 0;
}

void HistoryMessage::reapplyText() {
	setText(originalText());
	history()->owner().requestItemResize(this);
}

void HistoryMessage::setEmptyText() {
	clearIsolatedEmoji();
	_text.setMarkedText(
		st::messageTextStyle,
		{ QString(), EntitiesInText() },
		Ui::ItemTextOptions(this));

	_textWidth = -1;
	_textHeight = 0;
}

void HistoryMessage::clearIsolatedEmoji() {
	if (!(_clientFlags & MTPDmessage_ClientFlag::f_isolated_emoji)) {
		return;
	}
	history()->session().emojiStickersPack().remove(this);
	_clientFlags &= ~MTPDmessage_ClientFlag::f_isolated_emoji;
}

void HistoryMessage::checkIsolatedEmoji() {
	if (history()->session().emojiStickersPack().add(this)) {
		_clientFlags |= MTPDmessage_ClientFlag::f_isolated_emoji;
	}
}

void HistoryMessage::setReplyMarkup(const MTPReplyMarkup *markup) {
	const auto requestUpdate = [&] {
		history()->owner().requestItemResize(this);
		history()->session().changes().messageUpdated(
			this,
			Data::MessageUpdate::Flag::ReplyMarkup);
	};
	if (!markup) {
		if (_flags & MTPDmessage::Flag::f_reply_markup) {
			_flags &= ~MTPDmessage::Flag::f_reply_markup;
			if (Has<HistoryMessageReplyMarkup>()) {
				RemoveComponents(HistoryMessageReplyMarkup::Bit());
			}
			requestUpdate();
		}
		return;
	}

	// optimization: don't create markup component for the case
	// MTPDreplyKeyboardHide with flags = 0, assume it has f_zero flag
	if (markup->type() == mtpc_replyKeyboardHide && markup->c_replyKeyboardHide().vflags().v == 0) {
		bool changed = false;
		if (Has<HistoryMessageReplyMarkup>()) {
			RemoveComponents(HistoryMessageReplyMarkup::Bit());
			changed = true;
		}
		if (!(_flags & MTPDmessage::Flag::f_reply_markup)) {
			_flags |= MTPDmessage::Flag::f_reply_markup;
			changed = true;
		}
		if (changed) {
			requestUpdate();
		}
	} else {
		if (!(_flags & MTPDmessage::Flag::f_reply_markup)) {
			_flags |= MTPDmessage::Flag::f_reply_markup;
		}
		if (!Has<HistoryMessageReplyMarkup>()) {
			AddComponents(HistoryMessageReplyMarkup::Bit());
		}
		Get<HistoryMessageReplyMarkup>()->create(*markup);
		requestUpdate();
	}
}

Ui::Text::IsolatedEmoji HistoryMessage::isolatedEmoji() const {
	return _text.toIsolatedEmoji();
}

TextWithEntities HistoryMessage::originalText() const {
	if (emptyText()) {
		return { QString(), EntitiesInText() };
	}
	return _text.toTextWithEntities();
}

TextForMimeData HistoryMessage::clipboardText() const {
	if (emptyText()) {
		return TextForMimeData();
	}
	return _text.toTextForMimeData();
}

bool HistoryMessage::textHasLinks() const {
	return emptyText() ? false : _text.hasLinks();
}

void HistoryMessage::setViewsCount(int count) {
	const auto views = Get<HistoryMessageViews>();
	if (!views
		|| views->views.count == count
		|| (count >= 0 && views->views.count > count)) {
		return;
	}

	views->views.count = count;
	views->views.text = Lang::FormatCountToShort(
		std::max(views->views.count, 1)
	).string;
	const auto was = views->views.textWidth;
	views->views.textWidth = views->views.text.isEmpty()
		? 0
		: st::msgDateFont->width(views->views.text);
	if (was == views->views.textWidth) {
		history()->owner().requestItemRepaint(this);
	} else {
		history()->owner().requestItemResize(this);
	}
}

void HistoryMessage::setForwardsCount(int count) {
}

void HistoryMessage::setPostAuthor(const QString &author) {
	auto msgsigned = Get<HistoryMessageSigned>();
	if (author.isEmpty()) {
		if (!msgsigned) {
			return;
		}
		RemoveComponents(HistoryMessageSigned::Bit());
		history()->owner().requestItemResize(this);
		return;
	}
	if (!msgsigned) {
		AddComponents(HistoryMessageSigned::Bit());
		msgsigned = Get<HistoryMessageSigned>();
	} else if (msgsigned->author == author) {
		return;
	}
	msgsigned->author = author;
	msgsigned->isAnonymousRank = !isDiscussionPost()
		&& this->author()->isMegagroup();
	history()->owner().requestItemResize(this);
}

void HistoryMessage::setReplies(const MTPMessageReplies &data) {
	data.match([&](const MTPDmessageReplies &data) {
		auto views = Get<HistoryMessageViews>();
		if (!views) {
			AddComponents(HistoryMessageViews::Bit());
			views = Get<HistoryMessageViews>();
		}
		const auto repliers = [&] {
			auto result = std::vector<PeerId>();
			if (const auto list = data.vrecent_repliers()) {
				result.reserve(list->v.size());
				for (const auto &id : list->v) {
					result.push_back(peerFromMTP(id));
				}
			}
			return result;
		}();
		const auto count = data.vreplies().v;
		const auto channelId = ChannelId(
			data.vchannel_id().value_or_empty());
		const auto readTillId = data.vread_max_id()
			? std::max(
				{ views->repliesInboxReadTillId, data.vread_max_id()->v, 1 })
			: views->repliesInboxReadTillId;
		const auto maxId = data.vmax_id().value_or(views->repliesMaxId);
		const auto countsChanged = (views->replies.count != count)
			|| (views->repliesInboxReadTillId != readTillId)
			|| (views->repliesMaxId != maxId);
		const auto megagroupChanged = (views->commentsMegagroupId != channelId);
		const auto recentChanged = (views->recentRepliers != repliers);
		if (!countsChanged && !megagroupChanged && !recentChanged) {
			return;
		}
		views->replies.count = count;
		if (recentChanged) {
			views->recentRepliers = repliers;
		}
		views->commentsMegagroupId = channelId;
		const auto wasUnread = channelId && areRepliesUnread();
		views->repliesInboxReadTillId = readTillId;
		views->repliesMaxId = maxId;
		if (channelId && wasUnread != areRepliesUnread()) {
			history()->owner().requestItemRepaint(this);
		}
		refreshRepliesText(views, megagroupChanged);
	});
}

void HistoryMessage::clearReplies() {
	auto views = Get<HistoryMessageViews>();
	if (!views) {
		return;
	}
	const auto viewsPart = views->views;
	if (viewsPart.count < 0) {
		RemoveComponents(HistoryMessageViews::Bit());
	} else {
		*views = HistoryMessageViews();
		views->views = viewsPart;
	}
	history()->owner().requestItemResize(this);
}

void HistoryMessage::refreshRepliesText(
		not_null<HistoryMessageViews*> views,
		bool forceResize) {
	const auto was = views->replies.textWidth;
	if (views->commentsMegagroupId) {
		views->replies.text = (views->replies.count > 0)
			? tr::lng_comments_open_count(
				tr::now,
				lt_count_short,
				views->replies.count)
			: tr::lng_comments_open_none(tr::now);
		views->replies.textWidth = st::semiboldFont->width(
			views->replies.text);
		views->repliesSmall.text = (views->replies.count > 0)
			? Lang::FormatCountToShort(views->replies.count).string
			: QString();
		views->repliesSmall.textWidth = st::semiboldFont->width(
			views->repliesSmall.text);
	} else {
		views->replies.text = (views->replies.count > 0)
			? Lang::FormatCountToShort(views->replies.count).string
			: QString();
		views->replies.textWidth = views->replies.text.isEmpty()
			? 0
			: st::msgDateFont->width(views->replies.text);
	}
	if (forceResize || views->replies.textWidth != was) {
		history()->owner().requestItemResize(this);
	} else {
		history()->owner().requestItemRepaint(this);
	}
}

void HistoryMessage::changeRepliesCount(int delta, PeerId replier) {
	const auto views = Get<HistoryMessageViews>();
	const auto limit = HistoryMessageViews::kMaxRecentRepliers;
	if (!views || views->replies.count < 0) {
		return;
	}
	views->replies.count = std::max(views->replies.count + delta, 0);
	if (replier && views->commentsMegagroupId) {
		if (delta < 0) {
			views->recentRepliers.erase(
				ranges::remove(views->recentRepliers, replier),
				end(views->recentRepliers));
		} else if (!ranges::contains(views->recentRepliers, replier)) {
			views->recentRepliers.insert(views->recentRepliers.begin(), replier);
			while (views->recentRepliers.size() > limit) {
				views->recentRepliers.pop_back();
			}
		}
	}
	refreshRepliesText(views);
}

void HistoryMessage::setReplyToTop(MsgId replyToTop) {
	const auto reply = Get<HistoryMessageReply>();
	if (!reply
		|| (reply->replyToMsgTop == replyToTop)
		|| (reply->replyToMsgTop != 0)
		|| isScheduled()) {
		return;
	}
	reply->replyToMsgTop = replyToTop;
	changeReplyToTopCounter(reply, 1);
}

void HistoryMessage::setRealId(MsgId newId) {
	HistoryItem::setRealId(newId);

	history()->owner().groups().refreshMessage(this);
	history()->owner().requestItemResize(this);
	if (const auto reply = Get<HistoryMessageReply>()) {
		if (reply->replyToLink()) {
			reply->setReplyToLinkFrom(this);
		}
		changeReplyToTopCounter(reply, 1);
	}
}

void HistoryMessage::incrementReplyToTopCounter() {
	if (const auto reply = Get<HistoryMessageReply>()) {
		changeReplyToTopCounter(reply, 1);
	}
}

void HistoryMessage::changeReplyToTopCounter(
		not_null<HistoryMessageReply*> reply,
		int delta) {
	if (!IsServerMsgId(id) || !reply->replyToTop()) {
		return;
	}
	const auto channelId = history()->channelId();
	if (!channelId) {
		return;
	}
	const auto top = history()->owner().message(
		channelId,
		reply->replyToTop());
	if (!top) {
		return;
	}
	const auto changeFor = [&](not_null<HistoryItem*> item) {
		if (const auto from = displayFrom()) {
			item->changeRepliesCount(delta, from->id);
			return;
		}
		item->changeRepliesCount(delta, PeerId());
	};
	if (const auto views = top->Get<HistoryMessageViews>()) {
		if (views->commentsMegagroupId) {
			// This is a post in channel, we don't track its replies.
			return;
		}
	}
	changeFor(top);
	if (const auto original = top->lookupDiscussionPostOriginal()) {
		changeFor(original);
	}
}

void HistoryMessage::dependencyItemRemoved(HistoryItem *dependency) {
	if (const auto reply = Get<HistoryMessageReply>()) {
		const auto documentId = reply->replyToDocumentId;
		reply->itemRemoved(this, dependency);
		if (documentId != reply->replyToDocumentId
			&& generateLocalEntitiesByReply()) {
			reapplyText();
		}
	}
}

QString HistoryMessage::notificationHeader() const {
	if (out() && isFromScheduled() && !_history->peer->isSelf()) {
		return tr::lng_from_you(tr::now);
	} else if (!_history->peer->isUser() && !isPost()) {
		return from()->name;
	}
	return QString();
}

std::unique_ptr<HistoryView::Element> HistoryMessage::createView(
		not_null<HistoryView::ElementDelegate*> delegate,
		HistoryView::Element *replacing) {
	return delegate->elementCreate(this, replacing);
}

HistoryMessage::~HistoryMessage() {
	_media.reset();
	clearSavedMedia();
	if (auto reply = Get<HistoryMessageReply>()) {
		reply->clearData(this);
	}
}
