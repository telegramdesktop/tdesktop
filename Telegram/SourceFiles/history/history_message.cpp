/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_message.h"

#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/history_location_manager.h"
#include "history/history_service.h"
#include "history/view/history_view_service_message.h"
#include "history/view/history_view_context_menu.h" // For CopyPostLink().
#include "chat_helpers/stickers_emoji_pack.h"
#include "main/main_session.h"
#include "boxes/share_box.h"
#include "boxes/confirm_box.h"
#include "ui/toast/toast.h"
#include "ui/text/text_utilities.h"
#include "ui/text/text_isolated_emoji.h"
#include "ui/text_options.h"
#include "core/application.h"
#include "layout.h"
#include "window/notifications_manager.h"
#include "window/window_session_controller.h"
#include "observer_peer.h"
#include "storage/storage_shared_media.h"
#include "data/data_session.h"
#include "data/data_game.h"
#include "data/data_media_types.h"
#include "data/data_channel.h"
#include "data/data_user.h"
#include "styles/style_dialogs.h"
#include "styles/style_widgets.h"
#include "styles/style_history.h"
#include "styles/style_window.h"

namespace {

constexpr auto kPinnedMessageTextLimit = 16;

MTPDmessage::Flags NewForwardedFlags(
		not_null<PeerData*> peer,
		UserId from,
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

MTPDmessage_ClientFlags NewForwardedClientFlags() {
	return NewMessageClientFlags();
}

bool CopyMarkupToForward(not_null<const HistoryItem*> item) {
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

bool HasInlineItems(const HistoryItemsList &items) {
	for (const auto item : items) {
		if (item->viaBot()) {
			return true;
		}
	}
	return false;
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
				formatDurationWords(left));
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
	const auto data = std::make_shared<ShareData>(
		history->peer,
		owner->itemOrItsGroup(item));
	const auto isGroup = (owner->groups().find(item) != nullptr);
	const auto isGame = item->getMessageBot()
		&& item->media()
		&& (item->media()->game() != nullptr);
	const auto canCopyLink = item->hasDirectLink() || isGame;

	auto copyCallback = [=]() {
		if (const auto item = owner->message(data->msgIds[0])) {
			if (item->hasDirectLink()) {
				HistoryView::CopyPostLink(item->fullId());
			} else if (const auto bot = item->getMessageBot()) {
				if (const auto media = item->media()) {
					if (const auto game = media->game()) {
						const auto link = Core::App().createInternalLinkFull(
							bot->username
							+ qsl("?game=")
							+ game->shortName);

						QApplication::clipboard()->setText(link);

						Ui::Toast::Show(tr::lng_share_game_link_copied(tr::now));
					}
				}
			}
		}
	};
	auto submitCallback = [=](
			QVector<PeerData*> &&result,
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
					Ui::Text::Bold(App::peerName(error.second))
				).append("\n\n");
			}
			text.append(error.first);
			Ui::show(
				Box<InformBox>(text),
				LayerOption::KeepOther);
			return;
		}

		auto doneCallback = [=](const MTPUpdates &updates, mtpRequestId requestId) {
			history->session().api().applyUpdates(updates);
			data->requests.remove(requestId);
			if (data->requests.empty()) {
				Ui::Toast::Show(tr::lng_share_done(tr::now));
				Ui::hideLayer();
			}
		};

		const auto sendFlags = MTPmessages_ForwardMessages::Flag(0)
			| MTPmessages_ForwardMessages::Flag::f_with_my_score
			| (isGroup
				? MTPmessages_ForwardMessages::Flag::f_grouped
				: MTPmessages_ForwardMessages::Flag(0))
			| (options.silent
				? MTPmessages_ForwardMessages::Flag::f_silent
				: MTPmessages_ForwardMessages::Flag(0));
		auto msgIds = QVector<MTPint>();
		msgIds.reserve(data->msgIds.size());
		for (const auto fullId : data->msgIds) {
			msgIds.push_back(MTP_int(fullId.msg));
		}
		auto generateRandom = [&] {
			auto result = QVector<MTPlong>(data->msgIds.size());
			for (auto &value : result) {
				value = rand_value<MTPlong>();
			}
			return result;
		};
		for (const auto peer : result) {
			const auto history = peer->owner().history(peer);
			if (!comment.text.isEmpty()) {
				auto message = ApiWrap::MessageToSend(history);
				message.textWithTags = comment;
				message.action.clearDraft = false;
				history->session().api().sendMessage(std::move(message));
			}
			history->sendRequestId = MTP::send(
				MTPmessages_ForwardMessages(
					MTP_flags(sendFlags),
					data->peer->input,
					MTP_vector<MTPint>(msgIds),
					MTP_vector<MTPlong>(generateRandom()),
					peer->input,
					MTP_int(0)),
				rpcDone(base::duplicate(doneCallback)),
				nullptr,
				0,
				0,
				history->sendRequestId);
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
	Ui::show(Box<ShareBox>(
		App::wnd()->sessionController(),
		std::move(copyLinkCallback),
		std::move(submitCallback),
		std::move(filterCallback)));
}

Fn<void(ChannelData*, MsgId)> HistoryDependentItemCallback(
		const FullMsgId &msgId) {
	return [dependent = msgId](ChannelData *channel, MsgId msgId) {
		if (const auto item = Auth().data().message(dependent)) {
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
	MsgId replyTo = 0;
	UserId viaBotId = 0;
	int viewsCount = -1;
	QString author;
	PeerId senderOriginal = 0;
	QString senderNameOriginal;
	MsgId originalId = 0;
	PeerId savedFromPeer = 0;
	MsgId savedFromMsgId = 0;
	QString authorOriginal;
	TimeId originalDate = 0;
	TimeId editDate = 0;

	// For messages created from MTP structs.
	const MTPReplyMarkup *mtpMarkup = nullptr;

	// For messages created from existing messages (forwarded).
	const HistoryMessageReplyMarkup *inlineMarkup = nullptr;
};

void HistoryMessage::FillForwardedInfo(
		CreateConfig &config,
		const MTPDmessageFwdHeader &data) {
	if (const auto channelId = data.vchannel_id()) {
		config.senderOriginal = peerFromChannel(*channelId);
	} else if (const auto fromId = data.vfrom_id()) {
		config.senderOriginal = peerFromUser(*fromId);
	}
	config.originalDate = data.vdate().v;
	config.senderNameOriginal = qs(data.vfrom_name().value_or_empty());
	config.originalId = data.vchannel_post().value_or_empty();
	config.authorOriginal = qs(data.vpost_author().value_or_empty());
	const auto savedFromPeer = data.vsaved_from_peer();
	const auto savedFromMsgId = data.vsaved_from_msg_id();
	if (savedFromPeer && savedFromMsgId) {
		config.savedFromPeer = peerFromMTP(*savedFromPeer);
		config.savedFromMsgId = savedFromMsgId->v;
	}
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
		data.vfrom_id().value_or_empty()) {
	auto config = CreateConfig();
	if (const auto forwarded = data.vfwd_from()) {
		forwarded->match([&](const MTPDmessageFwdHeader &data) {
			FillForwardedInfo(config, data);
		});
	}
	config.replyTo = data.vreply_to_msg_id().value_or_empty();
	config.viaBotId = data.vvia_bot_id().value_or_empty();
	config.viewsCount = data.vviews().value_or(-1);
	config.mtpMarkup = data.vreply_markup();
	config.editDate = data.vedit_date().value_or_empty();
	config.author = qs(data.vpost_author().value_or_empty());

	createComponents(config);

	if (const auto media = data.vmedia()) {
		setMedia(*media);
	}
	setText({
		TextUtilities::Clean(qs(data.vmessage())),
		TextUtilities::EntitiesFromMTP(data.ventities().value_or_empty())
	});
	if (const auto groupedId = data.vgrouped_id()) {
		setGroupId(
			MessageGroupId::FromRaw(history->peer->id, groupedId->v));
	}

	refreshMessageBadge();
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
		data.vfrom_id().value_or_empty()) {
	auto config = CreateConfig();

	config.replyTo = data.vreply_to_msg_id().value_or_empty();

	createComponents(config);

	switch (data.vaction().type()) {
	case mtpc_messageActionPhoneCall: {
		_media = std::make_unique<Data::MediaCall>(
			this,
			data.vaction().c_messageActionPhoneCall());
	} break;

	default: Unexpected("Service message action type in HistoryMessage.");
	}

	setText(TextWithEntities {});
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	MsgId id,
	MTPDmessage::Flags flags,
	MTPDmessage_ClientFlags clientFlags,
	TimeId date,
	UserId from,
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
	} else if (isPost()
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
	UserId from,
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
	UserId from,
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
	UserId from,
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
	UserId from,
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
	setText(TextWithEntities());
}

void HistoryMessage::createComponentsHelper(
		MTPDmessage::Flags flags,
		MsgId replyTo,
		UserId viaBotId,
		const QString &postAuthor,
		const MTPReplyMarkup &markup) {
	auto config = CreateConfig();

	if (flags & MTPDmessage::Flag::f_via_bot_id) config.viaBotId = viaBotId;
	if (flags & MTPDmessage::Flag::f_reply_to_msg_id) config.replyTo = replyTo;
	if (flags & MTPDmessage::Flag::f_reply_markup) config.mtpMarkup = &markup;
	if (flags & MTPDmessage::Flag::f_post_author) config.author = postAuthor;
	if (isPost()) config.viewsCount = 1;

	createComponents(config);
}

int HistoryMessage::viewsCount() const {
	if (const auto views = Get<HistoryMessageViews>()) {
		return views->_views;
	}
	return HistoryItem::viewsCount();
}

bool HistoryMessage::updateDependencyItem() {
	if (const auto reply = Get<HistoryMessageReply>()) {
		return reply->updateData(this, true);
	}
	return true;
}

void HistoryMessage::refreshMessageBadge() {
	const auto text = [&] {
		if (isDiscussionPost()) {
			return tr::lng_channel_badge(tr::now);
		}
		const auto channel = history()->peer->asMegagroup();
		const auto user = author()->asUser();
		if (!channel || !user) {
			return QString();
		}
		const auto info = channel->mgInfo.get();
		const auto i = channel->mgInfo->admins.find(peerToUser(user->id));
		const auto custom = (i != channel->mgInfo->admins.end())
			? i->second
			: (info->creator == user)
			? info->creatorRank
			: QString();
		return !custom.isEmpty()
			? custom
			: (info->creator == user)
			? tr::lng_owner_badge(tr::now)
			: (i != channel->mgInfo->admins.end())
			? tr::lng_admin_badge(tr::now)
			: QString();
	}();
	if (text.isEmpty()) {
		_messageBadge.clear();
	} else {
		_messageBadge.setText(
			st::defaultTextStyle,
			TextUtilities::RemoveEmoji(TextUtilities::SingleLine(text)));
	}
}

void HistoryMessage::applyGroupAdminChanges(
		const base::flat_set<UserId> &changes) {
	if (!out() && changes.contains(peerToUser(author()->id))) {
		refreshMessageBadge();
		history()->owner().requestItemResize(this);
	}
}

bool HistoryMessage::allowsForward() const {
	if (id < 0 || !isHistoryEntry()) {
		return false;
	}
	return !_media || _media->allowsForward();
}

bool HistoryMessage::allowsSendNow() const {
	return isScheduled();
}

bool HistoryMessage::isTooOldForEdit(TimeId now) const {
	const auto peer = _history->peer;
	if (peer->isSelf()) {
		return false;
	} else if (const auto megagroup = peer->asMegagroup()) {
		if (megagroup->canPinMessages()) {
			return false;
		}
	}
	return (now - date() >= Global::EditTimeLimit());
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
	if (config.viewsCount >= 0) {
		mask |= HistoryMessageViews::Bit();
	}
	if (!config.author.isEmpty()) {
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
		reply->replyToMsgId = config.replyTo;
		if (!reply->updateData(this)) {
			history()->session().api().requestMessageData(
				history()->peer->asChannel(),
				reply->replyToMsgId,
				HistoryDependentItemCallback(fullId()));
		}
	}
	if (const auto via = Get<HistoryMessageVia>()) {
		via->create(config.viaBotId);
	}
	if (const auto views = Get<HistoryMessageViews>()) {
		views->_views = config.viewsCount;
	}
	if (const auto edited = Get<HistoryMessageEdited>()) {
		edited->date = config.editDate;
	}
	if (const auto msgsigned = Get<HistoryMessageSigned>()) {
		msgsigned->author = config.author;
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
			config.senderNameOriginal);
	}
	forwarded->originalId = config.originalId;
	forwarded->originalAuthor = config.authorOriginal;
	forwarded->savedFromPeer = history()->owner().peerLoaded(
		config.savedFromPeer);
	forwarded->savedFromMsgId = config.savedFromMsgId;
}

void HistoryMessage::refreshMedia(const MTPMessageMedia *media) {
	_media = nullptr;
	if (media) {
		setMedia(*media);
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
	if (!_savedMedia) {
		return;
	}
	const auto wasGrouped = history()->owner().groups().isGrouped(this);
	_media = std::move(_savedMedia);
	if (wasGrouped) {
		history()->owner().groups().refreshMessage(this, true);
	} else {
		history()->owner().requestItemViewRefresh(this);
	}
}

void HistoryMessage::setMedia(const MTPMessageMedia &media) {
	_media = CreateMedia(this, media);
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
	}, [](const MTPDmessageMediaEmpty &) -> Result {
		return nullptr;
	}, [](const MTPDmessageMediaUnsupported &) -> Result {
		return nullptr;
	});
	return nullptr;
}

void HistoryMessage::replaceBuyWithReceiptInMarkup() {
	if (auto markup = inlineReplyMarkup()) {
		for (auto &row : markup->rows) {
			for (auto &button : row) {
				if (button.type == HistoryMessageMarkupButton::Type::Buy) {
					button.text = tr::lng_payments_receipt_button(tr::now);
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
		TextUtilities::EntitiesFromMTP(message.ventities().value_or_empty())
	};
	setReplyMarkup(message.vreply_markup());
	if (!isLocalUpdateMedia()) {
		refreshMedia(message.vmedia());
	}
	setViewsCount(message.vviews().value_or(-1));
	setText(textWithEntities);

	finishEdition(keyboardTop);
}

void HistoryMessage::applyEdition(const MTPDmessageService &message) {
	if (message.vaction().type() == mtpc_messageActionHistoryClear) {
		setReplyMarkup(nullptr);
		refreshMedia(nullptr);
		setEmptyText();
		setViewsCount(-1);

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
			Notify::peerUpdatedDelayed(
				history()->peer,
				Notify::PeerUpdate::Flag::UnreadMentionsChanged);
		}
	}
}

void HistoryMessage::eraseFromUnreadMentions() {
	if (isUnreadMention()) {
		history()->eraseFromUnreadMentions(id);
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
	return result;
}

void HistoryMessage::setText(const TextWithEntities &textWithEntities) {
	for_const (auto &entity, textWithEntities.entities) {
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
	_text.setMarkedText(
		st::messageTextStyle,
		textWithEntities,
		Ui::ItemTextOptions(this));
	if (!textWithEntities.text.isEmpty() && _text.isEmpty()) {
		// If server has allowed some text that we've trim-ed entirely,
		// just replace it with something so that UI won't look buggy.
		_text.setMarkedText(
			st::messageTextStyle,
			{ QString::fromUtf8(":-("), EntitiesInText() },
			Ui::ItemTextOptions(this));
	} else if (!_media) {
		checkIsolatedEmoji();
	}
	_textWidth = -1;
	_textHeight = 0;
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
	if (!markup) {
		if (_flags & MTPDmessage::Flag::f_reply_markup) {
			_flags &= ~MTPDmessage::Flag::f_reply_markup;
			if (Has<HistoryMessageReplyMarkup>()) {
				RemoveComponents(HistoryMessageReplyMarkup::Bit());
			}
			history()->owner().requestItemResize(this);
			Notify::replyMarkupUpdated(this);
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
			history()->owner().requestItemResize(this);
			Notify::replyMarkupUpdated(this);
		}
	} else {
		if (!(_flags & MTPDmessage::Flag::f_reply_markup)) {
			_flags |= MTPDmessage::Flag::f_reply_markup;
		}
		if (!Has<HistoryMessageReplyMarkup>()) {
			AddComponents(HistoryMessageReplyMarkup::Bit());
		}
		Get<HistoryMessageReplyMarkup>()->create(*markup);
		history()->owner().requestItemResize(this);
		Notify::replyMarkupUpdated(this);
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

void HistoryMessage::setViewsCount(int32 count) {
	const auto views = Get<HistoryMessageViews>();
	if (!views
		|| views->_views == count
		|| (count >= 0 && views->_views > count)) {
		return;
	}

	const auto was = views->_viewsWidth;
	views->_views = count;
	views->_viewsText = (views->_views > 0)
		? Lang::FormatCountToShort(views->_views).string
		: QString("1");
	views->_viewsWidth = views->_viewsText.isEmpty()
		? 0
		: st::msgDateFont->width(views->_viewsText);
	if (was == views->_viewsWidth) {
		history()->owner().requestItemRepaint(this);
	} else {
		history()->owner().requestItemResize(this);
	}
}

void HistoryMessage::setRealId(MsgId newId) {
	HistoryItem::setRealId(newId);

	history()->owner().groups().refreshMessage(this);
	history()->owner().requestItemResize(this);
	if (const auto reply = Get<HistoryMessageReply>()) {
		if (reply->replyToLink()) {
			reply->setReplyToLinkFrom(this);
		}
	}
}

void HistoryMessage::dependencyItemRemoved(HistoryItem *dependency) {
	if (auto reply = Get<HistoryMessageReply>()) {
		reply->itemRemoved(this, dependency);
	}
}

QString HistoryMessage::notificationHeader() const {
	if (out() && isFromScheduled() && !_history->peer->isSelf()) {
		return tr::lng_from_you(tr::now);
	} else if (!_history->peer->isUser() && !isPost()) {
		return App::peerName(from());
	}
	return QString();
}

std::unique_ptr<HistoryView::Element> HistoryMessage::createView(
		not_null<HistoryView::ElementDelegate*> delegate) {
	return delegate->elementCreate(this);
}

HistoryMessage::~HistoryMessage() {
	_media.reset();
	_savedMedia.reset();
	if (auto reply = Get<HistoryMessageReply>()) {
		reply->clearData(this);
	}
}
