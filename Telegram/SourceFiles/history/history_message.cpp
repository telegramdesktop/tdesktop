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
#include "history/history_media_types.h"
#include "history/history_service.h"
#include "history/view/history_view_service_message.h"
#include "auth_session.h"
#include "boxes/share_box.h"
#include "boxes/confirm_box.h"
#include "ui/toast/toast.h"
#include "ui/text_options.h"
#include "messenger.h"
#include "layout.h"
#include "styles/style_dialogs.h"
#include "styles/style_widgets.h"
#include "styles/style_history.h"
#include "styles/style_window.h"
#include "window/notifications_manager.h"
#include "window/window_controller.h"
#include "observer_peer.h"
#include "storage/storage_shared_media.h"
#include "data/data_session.h"

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
	if (auto channel = peer->asChannel()) {
		if (auto media = fwd->getMedia()) {
			if (media->type() == MediaTypeWebPage) {
				// Drop web page if we're not allowed to send it.
				if (channel->restricted(
						ChannelRestriction::f_embed_links)) {
					result &= MTPDmessage::Flag::f_media;
				}
			}
		}
	} else {
		if (auto media = fwd->getMedia()) {
			if (media->type() == MediaTypeVoiceFile) {
				result |= MTPDmessage::Flag::f_media_unread;
//			} else if (media->type() == MediaTypeVideo) {
//				result |= MTPDmessage::flag_media_unread;
			}
		}
	}
	if (fwd->hasViews()) {
		result |= MTPDmessage::Flag::f_views;
	}
	return result;
}

bool HasMediaItems(const HistoryItemsList &items) {
	for (const auto item : items) {
		if (const auto media = item->getMedia()) {
			switch (media->type()) {
			case MediaTypePhoto:
			case MediaTypeVideo:
			case MediaTypeGrouped:
			case MediaTypeFile:
			case MediaTypeMusicFile:
			case MediaTypeVoiceFile: return true;
			case MediaTypeGif: return media->getDocument()->isVideoMessage();
			}
		}
	}
	return false;
}

bool HasStickerItems(const HistoryItemsList &items) {
	for (const auto item : items) {
		if (const auto media = item->getMedia()) {
			switch (media->type()) {
			case MediaTypeSticker: return true;
			}
		}
	}
	return false;
}

bool HasGifItems(const HistoryItemsList &items) {
	for (const auto item : items) {
		if (const auto media = item->getMedia()) {
			switch (media->type()) {
			case MediaTypeGif: return !media->getDocument()->isVideoMessage();
			}
		}
	}
	return false;
}

bool HasGameItems(const HistoryItemsList &items) {
	for (const auto item : items) {
		if (const auto media = item->getMedia()) {
			switch (media->type()) {
			case MediaTypeGame: return true;
			}
		}
	}
	return false;
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
	const auto data = std::make_shared<ShareData>(
		item->history()->peer,
		Auth().data().itemOrItsGroup(item));
	const auto isGroup = (item->getFullGroup() != nullptr);
	const auto isGame = item->getMessageBot()
		&& item->getMedia()
		&& (item->getMedia()->type() == MediaTypeGame);
	const auto canCopyLink = item->hasDirectLink() || isGame;

	auto copyCallback = [data]() {
		if (auto main = App::main()) {
			if (auto item = App::histItemById(data->msgIds[0])) {
				if (item->hasDirectLink()) {
					QApplication::clipboard()->setText(item->directLink());

					Ui::Toast::Show(lang(lng_channel_public_link_copied));
				} else if (auto bot = item->getMessageBot()) {
					if (auto media = item->getMedia()) {
						if (media->type() == MediaTypeGame) {
							auto shortName = static_cast<HistoryGame*>(media)->game()->shortName;

							QApplication::clipboard()->setText(Messenger::Instance().createInternalLinkFull(bot->username + qsl("?game=") + shortName));

							Ui::Toast::Show(lang(lng_share_game_link_copied));
						}
					}
				}
			}
		}
	};
	auto submitCallback = [data, isGroup](const QVector<PeerData*> &result) {
		if (!data->requests.empty()) {
			return; // Share clicked already.
		}
		auto items = Auth().data().idsToItems(data->msgIds);
		if (items.empty() || result.empty()) {
			return;
		}

		auto restrictedSomewhere = false;
		auto restrictedEverywhere = true;
		auto firstError = QString();
		for (const auto peer : result) {
			const auto error = GetErrorTextForForward(peer, items);
			if (!error.isEmpty()) {
				if (firstError.isEmpty()) {
					firstError = error;
				}
				restrictedSomewhere = true;
				continue;
			}
			restrictedEverywhere = false;
		}
		if (restrictedEverywhere) {
			Ui::show(
				Box<InformBox>(firstError),
				LayerOption::KeepOther);
			return;
		}

		auto doneCallback = [data](const MTPUpdates &updates, mtpRequestId requestId) {
			if (auto main = App::main()) {
				main->sentUpdatesReceived(updates);
			}
			data->requests.remove(requestId);
			if (data->requests.empty()) {
				Ui::Toast::Show(lang(lng_share_done));
				Ui::hideLayer();
			}
		};

		const auto sendFlags = MTPmessages_ForwardMessages::Flag(0)
			| MTPmessages_ForwardMessages::Flag::f_with_my_score
			| (isGroup
				? MTPmessages_ForwardMessages::Flag::f_grouped
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
		if (auto main = App::main()) {
			for (const auto peer : result) {
				if (!GetErrorTextForForward(peer, items).isEmpty()) {
					continue;
				}

				auto request = MTPmessages_ForwardMessages(
					MTP_flags(sendFlags),
					data->peer->input,
					MTP_vector<MTPint>(msgIds),
					MTP_vector<MTPlong>(generateRandom()),
					peer->input);
				auto callback = doneCallback;
				auto requestId = MTP::send(request, rpcDone(std::move(callback)));
				data->requests.insert(requestId);
			}
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
		? base::lambda<void()>(std::move(copyCallback))
		: base::lambda<void()>();
	Ui::show(Box<ShareBox>(
		std::move(copyLinkCallback),
		std::move(submitCallback),
		std::move(filterCallback)));
}

base::lambda<void(ChannelData*, MsgId)> HistoryDependentItemCallback(
		const FullMsgId &msgId) {
	return [dependent = msgId](ChannelData *channel, MsgId msgId) {
		if (auto item = App::histItemById(dependent)) {
			item->updateDependencyItem();
		}
	};
}

MTPDmessage::Flags NewMessageFlags(not_null<PeerData*> peer) {
	MTPDmessage::Flags result = 0;
	if (!peer->isSelf()) {
		result |= MTPDmessage::Flag::f_out;
		//if (p->isChat() || (p->isUser() && !p->asUser()->botInfo)) {
		//	result |= MTPDmessage::Flag::f_unread;
		//}
	}
	return result;
}

QString GetErrorTextForForward(
		not_null<PeerData*> peer,
		const HistoryItemsList &items) {
	if (!peer->canWrite()) {
		return lang(lng_forward_cant);
	}

	if (auto megagroup = peer->asMegagroup()) {
		if (megagroup->restricted(ChannelRestriction::f_send_media) && HasMediaItems(items)) {
			return lang(lng_restricted_send_media);
		} else if (megagroup->restricted(ChannelRestriction::f_send_stickers) && HasStickerItems(items)) {
			return lang(lng_restricted_send_stickers);
		} else if (megagroup->restricted(ChannelRestriction::f_send_gifs) && HasGifItems(items)) {
			return lang(lng_restricted_send_gifs);
		} else if (megagroup->restricted(ChannelRestriction::f_send_games) && HasGameItems(items)) {
			return lang(lng_restricted_send_inline);
		} else if (megagroup->restricted(ChannelRestriction::f_send_inline) && HasInlineItems(items)) {
			return lang(lng_restricted_send_inline);
		}
	}
	return QString();
}

struct HistoryMessage::CreateConfig {
	MsgId replyTo = 0;
	UserId viaBotId = 0;
	int viewsCount = -1;
	QString author;
	PeerId senderOriginal = 0;
	MsgId originalId = 0;
	PeerId savedFromPeer = 0;
	MsgId savedFromMsgId = 0;
	QString authorOriginal;
	QDateTime originalDate;
	QDateTime editDate;
	MessageGroupId groupId = MessageGroupId::None;

	// For messages created from MTP structs.
	const MTPReplyMarkup *mtpMarkup = nullptr;

	// For messages created from existing messages (forwarded).
	const HistoryMessageReplyMarkup *inlineMarkup = nullptr;
};

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	const MTPDmessage &msg)
: HistoryItem(history, msg.vid.v, msg.vflags.v, ::date(msg.vdate), msg.has_from_id() ? msg.vfrom_id.v : 0) {
	CreateConfig config;

	if (msg.has_fwd_from() && msg.vfwd_from.type() == mtpc_messageFwdHeader) {
		auto &f = msg.vfwd_from.c_messageFwdHeader();
		config.originalDate = ::date(f.vdate);
		if (f.has_from_id() || f.has_channel_id()) {
			config.senderOriginal = f.has_channel_id()
				? peerFromChannel(f.vchannel_id)
				: peerFromUser(f.vfrom_id);
			if (f.has_channel_post()) config.originalId = f.vchannel_post.v;
			if (f.has_post_author()) config.authorOriginal = qs(f.vpost_author);
			if (f.has_saved_from_peer() && f.has_saved_from_msg_id()) {
				config.savedFromPeer = peerFromMTP(f.vsaved_from_peer);
				config.savedFromMsgId = f.vsaved_from_msg_id.v;
			}
		}
	}
	if (msg.has_reply_to_msg_id()) config.replyTo = msg.vreply_to_msg_id.v;
	if (msg.has_via_bot_id()) config.viaBotId = msg.vvia_bot_id.v;
	if (msg.has_views()) config.viewsCount = msg.vviews.v;
	if (msg.has_reply_markup()) config.mtpMarkup = &msg.vreply_markup;
	if (msg.has_edit_date()) config.editDate = ::date(msg.vedit_date);
	if (msg.has_post_author()) config.author = qs(msg.vpost_author);
	if (msg.has_grouped_id()) {
		config.groupId = MessageGroupId::FromRaw(msg.vgrouped_id.v);
	}

	createComponents(config);

	initMedia(msg.has_media() ? (&msg.vmedia) : nullptr);

	auto text = TextUtilities::Clean(qs(msg.vmessage));
	auto entities = msg.has_entities()
		? TextUtilities::EntitiesFromMTP(msg.ventities.v)
		: EntitiesInText();
	setText({ text, entities });
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	const MTPDmessageService &msg)
: HistoryItem(history, msg.vid.v, mtpCastFlags(msg.vflags.v), ::date(msg.vdate), msg.has_from_id() ? msg.vfrom_id.v : 0) {
	CreateConfig config;

	if (msg.has_reply_to_msg_id()) config.replyTo = msg.vreply_to_msg_id.v;

	createComponents(config);

	switch (msg.vaction.type()) {
	case mtpc_messageActionPhoneCall: {
		_media = std::make_unique<HistoryCall>(this, msg.vaction.c_messageActionPhoneCall());
	} break;

	default: Unexpected("Service message action type in HistoryMessage.");
	}

	setText(TextWithEntities {});
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	MsgId id,
	MTPDmessage::Flags flags,
	QDateTime date,
	UserId from,
	const QString &postAuthor,
	not_null<HistoryMessage*> fwd)
: HistoryItem(history, id, NewForwardedFlags(history->peer, from, fwd) | flags, date, from) {
	CreateConfig config;

	if (fwd->Has<HistoryMessageForwarded>() || !fwd->history()->peer->isSelf()) {
		// Server doesn't add "fwd_from" to non-forwarded messages from chat with yourself.
		config.originalDate = fwd->dateOriginal();
		auto senderOriginal = fwd->senderOriginal();
		config.senderOriginal = senderOriginal->id;
		config.authorOriginal = fwd->authorOriginal();
		if (senderOriginal->isChannel()) {
			config.originalId = fwd->idOriginal();
		}
	}
	if (history->peer->isSelf()) {
		//
		// iOS app sends you to the original post if we forward a forward from channel.
		// But server returns not the original post but the forward in saved_from_...
		//
		//if (config.originalId) {
		//	config.savedFromPeer = config.senderOriginal;
		//	config.savedFromMsgId = config.originalId;
		//} else {
			config.savedFromPeer = fwd->history()->peer->id;
			config.savedFromMsgId = fwd->id;
		//}
	}
	if (flags & MTPDmessage::Flag::f_post_author) {
		config.author = postAuthor;
	}
	auto fwdViaBot = fwd->viaBot();
	if (fwdViaBot) config.viaBotId = peerToUser(fwdViaBot->id);
	int fwdViewsCount = fwd->viewsCount();
	if (fwdViewsCount > 0) {
		config.viewsCount = fwdViewsCount;
	} else if (isPost()) {
		config.viewsCount = 1;
	}

	// Copy inline keyboard when forwarding messages with a game.
	auto mediaOriginal = fwd->getMedia();
	auto mediaType = mediaOriginal ? mediaOriginal->type() : MediaTypeCount;
	if (mediaOriginal && mediaType == MediaTypeGame) {
		config.inlineMarkup = fwd->inlineReplyMarkup();
	}

	createComponents(config);

	auto cloneMedia = [this, history, mediaType] {
		if (mediaType == MediaTypeWebPage) {
			if (auto channel = history->peer->asChannel()) {
				if (channel->restricted(ChannelRestriction::f_embed_links)) {
					return false;
				}
			}
		}
		return (mediaType != MediaTypeCount);
	};
	if (cloneMedia()) {
		_media = mediaOriginal->clone(this, this);
	}
	setText(fwd->originalText());
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	MsgId id,
	MTPDmessage::Flags flags,
	MsgId replyTo,
	UserId viaBotId,
	QDateTime date,
	UserId from,
	const QString &postAuthor,
	const TextWithEntities &textWithEntities)
: HistoryItem(history, id, flags, date, (flags & MTPDmessage::Flag::f_from_id) ? from : 0) {
	createComponentsHelper(flags, replyTo, viaBotId, postAuthor, MTPnullMarkup);

	setText(textWithEntities);
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	MsgId msgId,
	MTPDmessage::Flags flags,
	MsgId replyTo,
	UserId viaBotId,
	QDateTime date,
	UserId from,
	const QString &postAuthor,
	not_null<DocumentData*> document,
	const QString &caption,
	const MTPReplyMarkup &markup)
: HistoryItem(history, msgId, flags, date, (flags & MTPDmessage::Flag::f_from_id) ? from : 0) {
	createComponentsHelper(flags, replyTo, viaBotId, postAuthor, markup);

	initMediaFromDocument(document, caption);
	setText(TextWithEntities());
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	MsgId msgId,
	MTPDmessage::Flags flags,
	MsgId replyTo,
	UserId viaBotId,
	QDateTime date,
	UserId from,
	const QString &postAuthor,
	not_null<PhotoData*> photo,
	const QString &caption,
	const MTPReplyMarkup &markup)
: HistoryItem(history, msgId, flags, date, (flags & MTPDmessage::Flag::f_from_id) ? from : 0) {
	createComponentsHelper(flags, replyTo, viaBotId, postAuthor, markup);

	_media = std::make_unique<HistoryPhoto>(this, photo, caption);
	setText(TextWithEntities());
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	MsgId msgId,
	MTPDmessage::Flags flags,
	MsgId replyTo,
	UserId viaBotId,
	QDateTime date,
	UserId from,
	const QString &postAuthor,
	not_null<GameData*> game,
	const MTPReplyMarkup &markup)
: HistoryItem(history, msgId, flags, date, (flags & MTPDmessage::Flag::f_from_id) ? from : 0) {
	createComponentsHelper(flags, replyTo, viaBotId, postAuthor, markup);

	_media = std::make_unique<HistoryGame>(this, game);
	setText(TextWithEntities());
}

void HistoryMessage::createComponentsHelper(
		MTPDmessage::Flags flags,
		MsgId replyTo,
		UserId viaBotId,
		const QString &postAuthor,
		const MTPReplyMarkup &markup) {
	CreateConfig config;

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

not_null<PeerData*> HistoryMessage::displayFrom() const {
	return history()->peer->isSelf()
		? senderOriginal()
		: author();
}

bool HistoryMessage::updateDependencyItem() {
	if (const auto reply = Get<HistoryMessageReply>()) {
		return reply->updateData(this, true);
	}
	return true;
}

void HistoryMessage::updateAdminBadgeState() {
	auto hasAdminBadge = [&] {
		if (auto channel = history()->peer->asChannel()) {
			if (auto user = author()->asUser()) {
				return channel->isGroupAdmin(user);
			}
		}
		return false;
	}();
	if (hasAdminBadge) {
		_flags |= MTPDmessage_ClientFlag::f_has_admin_badge;
	} else {
		_flags &= ~MTPDmessage_ClientFlag::f_has_admin_badge;
	}
}

void HistoryMessage::applyGroupAdminChanges(
		const base::flat_map<UserId, bool> &changes) {
	auto i = changes.find(peerToUser(author()->id));
	if (i != changes.end()) {
		if (i->second) {
			_flags |= MTPDmessage_ClientFlag::f_has_admin_badge;
		} else {
			_flags &= ~MTPDmessage_ClientFlag::f_has_admin_badge;
		}
		Auth().data().requestItemViewResize(this);
	}
}

bool HistoryMessage::displayEditedBadge() const {
	return !displayedEditDate().isNull();
}

QDateTime HistoryMessage::displayedEditDate() const {
	auto hasViaBotId = Has<HistoryMessageVia>();
	auto hasInlineMarkup = (inlineReplyMarkup() != nullptr);
	return displayedEditDate(hasViaBotId || hasInlineMarkup);
}

QDateTime HistoryMessage::displayedEditDate(
		bool hasViaBotOrInlineMarkup) const {
	if (hasViaBotOrInlineMarkup) {
		return QDateTime();
	} else if (const auto fromUser = from()->asUser()) {
		if (fromUser->botInfo) {
			return QDateTime();
		}
	}
	if (const auto edited = displayedEditBadge()) {
		return edited->date;
	}
	return QDateTime();
}

HistoryMessageEdited *HistoryMessage::displayedEditBadge() {
	if (_media && _media->overrideEditedDate()) {
		return _media->displayedEditBadge();
	}
	return Get<HistoryMessageEdited>();
}

const HistoryMessageEdited *HistoryMessage::displayedEditBadge() const {
	if (_media && _media->overrideEditedDate()) {
		return _media->displayedEditBadge();
	}
	return Get<HistoryMessageEdited>();
}

bool HistoryMessage::uploading() const {
	return _media && _media->uploading();
}

bool HistoryMessage::displayRightAction() const {
	return displayFastShare() || displayGoToOriginal();
}

bool HistoryMessage::displayFastShare() const {
	if (_history->peer->isChannel()) {
		return !_history->peer->isMegagroup();
	} else if (auto user = _history->peer->asUser()) {
		if (user->botInfo && !out()) {
			return _media && _media->allowsFastShare();
		}
	}
	return false;
}

bool HistoryMessage::displayGoToOriginal() const {
	if (_history->peer->isSelf()) {
		if (auto forwarded = Get<HistoryMessageForwarded>()) {
			return forwarded->savedFromPeer && forwarded->savedFromMsgId;
		}
	}
	return false;
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
	auto hasViaBot = (config.viaBotId != 0);
	auto hasInlineMarkup = [&config] {
		if (config.mtpMarkup) {
			return (config.mtpMarkup->type() == mtpc_replyInlineMarkup);
		}
		return (config.inlineMarkup != nullptr);
	};
	if (!config.editDate.isNull()) {
		mask |= HistoryMessageEdited::Bit();
	}
	if (config.senderOriginal) {
		mask |= HistoryMessageForwarded::Bit();
	}
	if (config.mtpMarkup) {
		// optimization: don't create markup component for the case
		// MTPDreplyKeyboardHide with flags = 0, assume it has f_zero flag
		if (config.mtpMarkup->type() != mtpc_replyKeyboardHide || config.mtpMarkup->c_replyKeyboardHide().vflags.v != 0) {
			mask |= HistoryMessageReplyMarkup::Bit();
		}
	} else if (config.inlineMarkup) {
		mask |= HistoryMessageReplyMarkup::Bit();
	}
	if (config.groupId) {
		mask |= HistoryMessageGroup::Bit();
	}

	UpdateComponents(mask);

	if (const auto reply = Get<HistoryMessageReply>()) {
		reply->replyToMsgId = config.replyTo;
		if (!reply->updateData(this)) {
			Auth().api().requestMessageData(
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
	if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		forwarded->originalDate = config.originalDate;
		forwarded->originalSender = App::peer(config.senderOriginal);
		forwarded->originalId = config.originalId;
		forwarded->originalAuthor = config.authorOriginal;
		forwarded->savedFromPeer = App::peerLoaded(config.savedFromPeer);
		forwarded->savedFromMsgId = config.savedFromMsgId;
	}
	if (const auto markup = Get<HistoryMessageReplyMarkup>()) {
		if (config.mtpMarkup) {
			markup->create(*config.mtpMarkup);
		} else if (config.inlineMarkup) {
			markup->create(*config.inlineMarkup);
		}
		if (markup->flags & MTPDreplyKeyboardMarkup_ClientFlag::f_has_switch_inline_button) {
			_flags |= MTPDmessage_ClientFlag::f_has_switch_inline_button;
		}
	}
	if (const auto group = Get<HistoryMessageGroup>()) {
		group->groupId = config.groupId;
		group->leader = this;
	}
	_fromNameVersion = displayFrom()->nameVersion;
}

QString formatViewsCount(int32 views) {
	if (views > 999999) {
		views /= 100000;
		if (views % 10) {
			return QString::number(views / 10) + '.' + QString::number(views % 10) + 'M';
		}
		return QString::number(views / 10) + 'M';
	} else if (views > 9999) {
		views /= 100;
		if (views % 10) {
			return QString::number(views / 10) + '.' + QString::number(views % 10) + 'K';
		}
		return QString::number(views / 10) + 'K';
	} else if (views > 0) {
		return QString::number(views);
	}
	return qsl("1");
}

void HistoryMessage::initTime() {
	if (const auto msgsigned = Get<HistoryMessageSigned>()) {
		_timeWidth = msgsigned->maxWidth();
	} else if (const auto edited = displayedEditBadge()) {
		_timeWidth = edited->maxWidth();
	} else {
		_timeText = date.toString(cTimeFormat());
		_timeWidth = st::msgDateFont->width(_timeText);
	}
	if (const auto views = Get<HistoryMessageViews>()) {
		views->_viewsText = (views->_views >= 0) ? formatViewsCount(views->_views) : QString();
		views->_viewsWidth = views->_viewsText.isEmpty() ? 0 : st::msgDateFont->width(views->_viewsText);
	}
	if (_text.hasSkipBlock()) {
		_text.setSkipBlock(skipBlockWidth(), skipBlockHeight());
		_textWidth = -1;
		_textHeight = 0;
	}
}

void HistoryMessage::initMedia(const MTPMessageMedia *media) {
	switch (media ? media->type() : mtpc_messageMediaEmpty) {
	case mtpc_messageMediaContact: {
		auto &d = media->c_messageMediaContact();
		_media = std::make_unique<HistoryContact>(this, d.vuser_id.v, qs(d.vfirst_name), qs(d.vlast_name), qs(d.vphone_number));
	} break;
	case mtpc_messageMediaGeo: {
		auto &point = media->c_messageMediaGeo().vgeo;
		if (point.type() == mtpc_geoPoint) {
			_media = std::make_unique<HistoryLocation>(this, LocationCoords(point.c_geoPoint()));
		}
	} break;
	case mtpc_messageMediaGeoLive: {
		auto &point = media->c_messageMediaGeoLive().vgeo;
		if (point.type() == mtpc_geoPoint) {
			_media = std::make_unique<HistoryLocation>(this, LocationCoords(point.c_geoPoint()));
		}
	} break;
	case mtpc_messageMediaVenue: {
		auto &d = media->c_messageMediaVenue();
		if (d.vgeo.type() == mtpc_geoPoint) {
			_media = std::make_unique<HistoryLocation>(this, LocationCoords(d.vgeo.c_geoPoint()), qs(d.vtitle), qs(d.vaddress));
		}
	} break;
	case mtpc_messageMediaPhoto: {
		auto &photo = media->c_messageMediaPhoto();
		if (photo.has_ttl_seconds()) {
			LOG(("App Error: Unexpected MTPMessageMediaPhoto with ttl_seconds in HistoryMessage."));
		} else if (photo.has_photo() && photo.vphoto.type() == mtpc_photo) {
			_media = std::make_unique<HistoryPhoto>(this, App::feedPhoto(photo.vphoto.c_photo()), photo.has_caption() ? qs(photo.vcaption) : QString());
		} else {
			LOG(("API Error: Got MTPMessageMediaPhoto without photo and without ttl_seconds."));
		}
	} break;
	case mtpc_messageMediaDocument: {
		auto &document = media->c_messageMediaDocument();
		if (document.has_ttl_seconds()) {
			LOG(("App Error: Unexpected MTPMessageMediaDocument with ttl_seconds in HistoryMessage."));
		} else if (document.has_document() && document.vdocument.type() == mtpc_document) {
			return initMediaFromDocument(App::feedDocument(document.vdocument.c_document()), document.has_caption() ? qs(document.vcaption) : QString());
		} else {
			LOG(("API Error: Got MTPMessageMediaDocument without document and without ttl_seconds."));
		}
	} break;
	case mtpc_messageMediaWebPage: {
		auto &d = media->c_messageMediaWebPage().vwebpage;
		switch (d.type()) {
		case mtpc_webPageEmpty: break;
		case mtpc_webPagePending: {
			_media = std::make_unique<HistoryWebPage>(this, App::feedWebPage(d.c_webPagePending()));
		} break;
		case mtpc_webPage: {
			_media = std::make_unique<HistoryWebPage>(this, App::feedWebPage(d.c_webPage()));
		} break;
		case mtpc_webPageNotModified: LOG(("API Error: webPageNotModified is unexpected in message media.")); break;
		}
	} break;
	case mtpc_messageMediaGame: {
		auto &d = media->c_messageMediaGame().vgame;
		if (d.type() == mtpc_game) {
			_media = std::make_unique<HistoryGame>(this, App::feedGame(d.c_game()));
		}
	} break;
	case mtpc_messageMediaInvoice: {
		_media = std::make_unique<HistoryInvoice>(this, media->c_messageMediaInvoice());
		if (static_cast<HistoryInvoice*>(getMedia())->getReceiptMsgId()) {
			replaceBuyWithReceiptInMarkup();
		}
	} break;
	};
}

void HistoryMessage::replaceBuyWithReceiptInMarkup() {
	if (auto markup = inlineReplyMarkup()) {
		for (auto &row : markup->rows) {
			for (auto &button : row) {
				if (button.type == HistoryMessageMarkupButton::Type::Buy) {
					button.text = lang(lng_payments_receipt_button);
				}
			}
		}
	}
}

void HistoryMessage::initMediaFromDocument(DocumentData *doc, const QString &caption) {
	if (doc->sticker()) {
		_media = std::make_unique<HistorySticker>(this, doc);
	} else if (doc->isAnimation()) {
		_media = std::make_unique<HistoryGif>(this, doc, caption);
	} else if (doc->isVideoFile()) {
		_media = std::make_unique<HistoryVideo>(this, doc, caption);
	} else {
		_media = std::make_unique<HistoryDocument>(this, doc, caption);
	}
}

int32 HistoryMessage::plainMaxWidth() const {
	return st::msgPadding.left() + _text.maxWidth() + st::msgPadding.right();
}

bool HistoryMessage::drawBubble() const {
	if (isHiddenByGroup()) {
		return false;
	} else if (Has<HistoryMessageLogEntryOriginal>()) {
		return true;
	}
	return _media ? (!emptyText() || _media->needsBubble()) : !isEmpty();
}

bool HistoryMessage::hasFastReply() const {
	return !hasOutLayout()
		&& (history()->peer->isChat() || history()->peer->isMegagroup());
}

bool HistoryMessage::displayFastReply() const {
	return hasFastReply() && history()->peer->canWrite();
}

void HistoryMessage::applyEdition(const MTPDmessage &message) {
	int keyboardTop = -1;
	//if (!pendingResize()) {// #TODO edit bot message
	//	if (auto keyboard = inlineReplyKeyboard()) {
	//		int h = st::msgBotKbButton.margin + keyboard->naturalHeight();
	//		keyboardTop = _height - h + st::msgBotKbButton.margin - marginBottom();
	//	}
	//}

	if (message.has_edit_date()) {
		_flags |= MTPDmessage::Flag::f_edit_date;
		if (!Has<HistoryMessageEdited>()) {
			AddComponents(HistoryMessageEdited::Bit());
		}
		auto edited = Get<HistoryMessageEdited>();
		edited->date = ::date(message.vedit_date);
	}

	TextWithEntities textWithEntities = { qs(message.vmessage), EntitiesInText() };
	if (message.has_entities()) {
		textWithEntities.entities = TextUtilities::EntitiesFromMTP(message.ventities.v);
	}
	setText(textWithEntities);
	setReplyMarkup(message.has_reply_markup() ? (&message.vreply_markup) : nullptr);
	setMedia(message.has_media() ? (&message.vmedia) : nullptr);
	setViewsCount(message.has_views() ? message.vviews.v : -1);

	finishEdition(keyboardTop);
}

void HistoryMessage::applyEdition(const MTPDmessageService &message) {
	if (message.vaction.type() == mtpc_messageActionHistoryClear) {
		applyEditionToEmpty();
	}
}

void HistoryMessage::applyEditionToEmpty() {
	setEmptyText();
	setMedia(nullptr);
	setReplyMarkup(nullptr);
	setViewsCount(-1);

	finishEditionToEmpty();
}

void HistoryMessage::refreshEditedBadge() {
	const auto edited = displayedEditBadge();
	const auto editDate = displayedEditDate();
	const auto dateText = date.toString(cTimeFormat());
	if (edited) {
		edited->refresh(dateText, !editDate.isNull());
	}
	if (const auto msgsigned = Get<HistoryMessageSigned>()) {
		const auto text = (!edited || editDate.isNull())
			? dateText
			: edited->text.originalText();
		msgsigned->refresh(text);
	}
	initTime();
}

bool HistoryMessage::displayForwardedFrom() const {
	if (auto forwarded = Get<HistoryMessageForwarded>()) {
		if (history()->peer->isSelf()) {
			return false;
		}
		return Has<HistoryMessageVia>()
			|| !_media
			|| !_media->isDisplayed()
			|| !_media->hideForwardedFrom()
			|| forwarded->originalSender->isChannel();
	}
	return false;
}

void HistoryMessage::updateMedia(const MTPMessageMedia *media) {
	auto setMediaAllowed = [](HistoryMediaType type) {
		return (type == MediaTypeWebPage)
			|| (type == MediaTypeGame)
			|| (type == MediaTypeLocation);
	};
	if (_flags & MTPDmessage_ClientFlag::f_from_inline_bot) {
		bool needReSet = true;
		if (media && _media) {
			needReSet = _media->needReSetInlineResultMedia(*media);
		}
		if (needReSet) {
			setMedia(media);
		}
		_flags &= ~MTPDmessage_ClientFlag::f_from_inline_bot;
	} else if (media && _media && !setMediaAllowed(_media->type())) {
		_media->updateSentMedia(*media);
	} else {
		setMedia(media);
	}
	Auth().data().requestItemViewResize(this);
}

void HistoryMessage::addToUnreadMentions(UnreadMentionType type) {
	if (IsServerMsgId(id) && mentionsMe() && isMediaUnread()) {
		if (history()->addToUnreadMentions(id, type)) {
			Notify::peerUpdatedDelayed(
				history()->peer,
				Notify::PeerUpdate::Flag::UnreadMentionsChanged);
		}
	}
}

void HistoryMessage::eraseFromUnreadMentions() {
	if (mentionsMe() && isMediaUnread()) {
		history()->eraseFromUnreadMentions(id);
	}
}

Storage::SharedMediaTypesMask HistoryMessage::sharedMediaTypes() const {
	auto result = Storage::SharedMediaTypesMask {};
	if (auto media = getMedia()) {
		result.set(media->sharedMediaTypes());
	}
	if (hasTextLinks()) {
		result.set(Storage::SharedMediaType::Link);
	}
	return result;
}

TextWithEntities HistoryMessage::selectedText(TextSelection selection) const {
	TextWithEntities logEntryOriginalResult;
	const auto textSelection = (selection == FullSelection)
		? AllTextSelection
		: IsSubGroupSelection(selection)
		? TextSelection(0, 0)
		: selection;
	auto textResult = _text.originalTextWithEntities(
		textSelection,
		ExpandLinksAll);
	auto skipped = skipTextSelection(selection);
	auto mediaDisplayed = (_media && _media->isDisplayed());
	auto mediaResult = (mediaDisplayed || isHiddenByGroup())
		? _media->selectedText(skipped)
		: TextWithEntities();
	if (auto entry = Get<HistoryMessageLogEntryOriginal>()) {
		const auto originalSelection = mediaDisplayed
			? _media->skipSelection(skipped)
			: skipped;
		logEntryOriginalResult = entry->_page->selectedText(originalSelection);
	}
	auto result = textResult;
	if (result.text.isEmpty()) {
		result = std::move(mediaResult);
	} else if (!mediaResult.text.isEmpty()) {
		result.text += qstr("\n\n");
		TextUtilities::Append(result, std::move(mediaResult));
	}
	if (result.text.isEmpty()) {
		result = std::move(logEntryOriginalResult);
	} else if (!logEntryOriginalResult.text.isEmpty()) {
		result.text += qstr("\n\n");
		TextUtilities::Append(result, std::move(logEntryOriginalResult));
	}
	if (auto reply = Get<HistoryMessageReply>()) {
		if (selection == FullSelection && reply->replyToMsg) {
			TextWithEntities wrapped;
			wrapped.text.reserve(lang(lng_in_reply_to).size() + reply->replyToMsg->author()->name.size() + 4 + result.text.size());
			wrapped.text.append('[').append(lang(lng_in_reply_to)).append(' ').append(reply->replyToMsg->author()->name).append(qsl("]\n"));
			TextUtilities::Append(wrapped, std::move(result));
			result = wrapped;
		}
	}
	if (auto forwarded = Get<HistoryMessageForwarded>()) {
		if (selection == FullSelection) {
			auto fwdinfo = forwarded->text.originalTextWithEntities(AllTextSelection, ExpandLinksAll);
			auto wrapped = TextWithEntities();
			wrapped.text.reserve(fwdinfo.text.size() + 4 + result.text.size());
			wrapped.entities.reserve(fwdinfo.entities.size() + result.entities.size());
			wrapped.text.append('[');
			TextUtilities::Append(wrapped, std::move(fwdinfo));
			wrapped.text.append(qsl("]\n"));
			TextUtilities::Append(wrapped, std::move(result));
			result = wrapped;
		}
	}
	return result;
}

void HistoryMessage::setMedia(const MTPMessageMedia *media) {
	if (!_media && (!media || media->type() == mtpc_messageMediaEmpty)) return;

	bool mediaRemovedSkipBlock = false;
	if (_media) {
		// Don't update Game media because we loose the consumed text of the message.
		if (_media->type() == MediaTypeGame) return;

		mediaRemovedSkipBlock = _media->isDisplayed() && _media->isBubbleBottom();
		_media.reset();
	}
	initMedia(media);
	auto mediaDisplayed = _media && _media->isDisplayed();
	if (mediaDisplayed && _media->isBubbleBottom() && !mediaRemovedSkipBlock) {
		_text.removeSkipBlock();
		_textWidth = -1;
		_textHeight = 0;
	} else if (mediaRemovedSkipBlock && (!mediaDisplayed || !_media->isBubbleBottom())) {
		_text.setSkipBlock(skipBlockWidth(), skipBlockHeight());
		_textWidth = -1;
		_textHeight = 0;
	}
	_history->recountGroupingAround(this);
}

void HistoryMessage::setText(const TextWithEntities &textWithEntities) {
	for_const (auto &entity, textWithEntities.entities) {
		auto type = entity.type();
		if (type == EntityInTextUrl || type == EntityInTextCustomUrl || type == EntityInTextEmail) {
			_flags |= MTPDmessage_ClientFlag::f_has_text_links;
			break;
		}
	}

	auto mediaDisplayed = _media && _media->isDisplayed();
	if (mediaDisplayed && _media->consumeMessageText(textWithEntities)) {
		setEmptyText();
	} else {
		auto mediaOnBottom = (_media && _media->isDisplayed() && _media->isBubbleBottom()) || Has<HistoryMessageLogEntryOriginal>();
		if (mediaOnBottom) {
			_text.setMarkedText(
				st::messageTextStyle,
				textWithEntities,
				Ui::ItemTextOptions(this));
		} else {
			_text.setMarkedText(
				st::messageTextStyle,
				{
					textWithEntities.text + skipBlock(),
					textWithEntities.entities
				},
				Ui::ItemTextOptions(this));
		}
		_textWidth = -1;
		_textHeight = 0;
	}
}

void HistoryMessage::setEmptyText() {
	_text.setMarkedText(
		st::messageTextStyle,
		{ QString(), EntitiesInText() },
		Ui::ItemTextOptions(this));

	_textWidth = -1;
	_textHeight = 0;
}

void HistoryMessage::setReplyMarkup(const MTPReplyMarkup *markup) {
	if (!markup) {
		if (_flags & MTPDmessage::Flag::f_reply_markup) {
			_flags &= ~MTPDmessage::Flag::f_reply_markup;
			if (Has<HistoryMessageReplyMarkup>()) {
				RemoveComponents(HistoryMessageReplyMarkup::Bit());
			}
			Auth().data().requestItemViewResize(this);
			Notify::replyMarkupUpdated(this);
		}
		return;
	}

	// optimization: don't create markup component for the case
	// MTPDreplyKeyboardHide with flags = 0, assume it has f_zero flag
	if (markup->type() == mtpc_replyKeyboardHide && markup->c_replyKeyboardHide().vflags.v == 0) {
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
			Auth().data().requestItemViewResize(this);
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
		Auth().data().requestItemViewResize(this);
		Notify::replyMarkupUpdated(this);
	}
}

TextWithEntities HistoryMessage::originalText() const {
	if (emptyText()) {
		return { QString(), EntitiesInText() };
	}
	return _text.originalTextWithEntities();
}

bool HistoryMessage::textHasLinks() const {
	return emptyText() ? false : _text.hasLinks();
}

int HistoryMessage::infoWidth() const {
	int result = _timeWidth;
	if (auto views = Get<HistoryMessageViews>()) {
		result += st::historyViewsSpace + views->_viewsWidth + st::historyViewsWidth;
	} else if (id < 0 && history()->peer->isSelf()) {
		if (!hasOutLayout()) {
			result += st::historySendStateSpace;
		}
	}
	if (hasOutLayout()) {
		result += st::historySendStateSpace;
	}
	return result;
}

int HistoryMessage::timeLeft() const {
	int result = 0;
	if (auto views = Get<HistoryMessageViews>()) {
		result += st::historyViewsSpace + views->_viewsWidth + st::historyViewsWidth;
	} else if (id < 0 && history()->peer->isSelf()) {
		if (!hasOutLayout()) {
			result += st::historySendStateSpace;
		}
	}
	return result;
}

void HistoryMessage::drawInfo(Painter &p, int32 right, int32 bottom, int32 width, bool selected, InfoDisplayType type) const {
	p.setFont(st::msgDateFont);

	bool outbg = hasOutLayout();
	bool invertedsprites = (type == InfoDisplayOverImage || type == InfoDisplayOverBackground);
	int32 infoRight = right, infoBottom = bottom;
	switch (type) {
	case InfoDisplayDefault:
		infoRight -= st::msgPadding.right() - st::msgDateDelta.x();
		infoBottom -= st::msgPadding.bottom() - st::msgDateDelta.y();
		p.setPen(selected ? (outbg ? st::msgOutDateFgSelected : st::msgInDateFgSelected) : (outbg ? st::msgOutDateFg : st::msgInDateFg));
	break;
	case InfoDisplayOverImage:
		infoRight -= st::msgDateImgDelta + st::msgDateImgPadding.x();
		infoBottom -= st::msgDateImgDelta + st::msgDateImgPadding.y();
		p.setPen(st::msgDateImgFg);
	break;
	case InfoDisplayOverBackground:
		infoRight -= st::msgDateImgDelta + st::msgDateImgPadding.x();
		infoBottom -= st::msgDateImgDelta + st::msgDateImgPadding.y();
		p.setPen(st::msgServiceFg);
	break;
	}

	int32 infoW = HistoryMessage::infoWidth();
	if (rtl()) infoRight = width - infoRight + infoW;

	int32 dateX = infoRight - infoW;
	int32 dateY = infoBottom - st::msgDateFont->height;
	if (type == InfoDisplayOverImage) {
		int32 dateW = infoW + 2 * st::msgDateImgPadding.x(), dateH = st::msgDateFont->height + 2 * st::msgDateImgPadding.y();
		App::roundRect(p, dateX - st::msgDateImgPadding.x(), dateY - st::msgDateImgPadding.y(), dateW, dateH, selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);
	} else if (type == InfoDisplayOverBackground) {
		int32 dateW = infoW + 2 * st::msgDateImgPadding.x(), dateH = st::msgDateFont->height + 2 * st::msgDateImgPadding.y();
		App::roundRect(p, dateX - st::msgDateImgPadding.x(), dateY - st::msgDateImgPadding.y(), dateW, dateH, selected ? st::msgServiceBgSelected : st::msgServiceBg, selected ? StickerSelectedCorners : StickerCorners);
	}
	dateX += HistoryMessage::timeLeft();

	if (const auto msgsigned = Get<HistoryMessageSigned>()) {
		msgsigned->signature.drawElided(p, dateX, dateY, _timeWidth);
	} else if (const auto edited = displayedEditBadge()) {
		edited->text.drawElided(p, dateX, dateY, _timeWidth);
	} else {
		p.drawText(dateX, dateY + st::msgDateFont->ascent, _timeText);
	}

	if (auto views = Get<HistoryMessageViews>()) {
		auto icon = ([this, outbg, invertedsprites, selected] {
			if (id > 0) {
				if (outbg) {
					return &(invertedsprites ? st::historyViewsInvertedIcon : (selected ? st::historyViewsOutSelectedIcon : st::historyViewsOutIcon));
				}
				return &(invertedsprites ? st::historyViewsInvertedIcon : (selected ? st::historyViewsInSelectedIcon : st::historyViewsInIcon));
			}
			return &(invertedsprites ? st::historyViewsSendingInvertedIcon : st::historyViewsSendingIcon);
		})();
		if (id > 0) {
			icon->paint(p, infoRight - infoW, infoBottom + st::historyViewsTop, width);
			p.drawText(infoRight - infoW + st::historyViewsWidth, infoBottom - st::msgDateFont->descent, views->_viewsText);
		} else if (!outbg) { // sending outbg icon will be painted below
			auto iconSkip = st::historyViewsSpace + views->_viewsWidth;
			icon->paint(p, infoRight - infoW + iconSkip, infoBottom + st::historyViewsTop, width);
		}
	} else if (id < 0 && history()->peer->isSelf() && !outbg) {
		auto icon = &(invertedsprites ? st::historyViewsSendingInvertedIcon : st::historyViewsSendingIcon);
		icon->paint(p, infoRight - infoW, infoBottom + st::historyViewsTop, width);
	}
	if (outbg) {
		auto icon = ([this, invertedsprites, selected] {
			if (id > 0) {
				if (unread()) {
					return &(invertedsprites ? st::historySentInvertedIcon : (selected ? st::historySentSelectedIcon : st::historySentIcon));
				}
				return &(invertedsprites ? st::historyReceivedInvertedIcon : (selected ? st::historyReceivedSelectedIcon : st::historyReceivedIcon));
			}
			return &(invertedsprites ? st::historySendingInvertedIcon : st::historySendingIcon);
		})();
		icon->paint(p, QPoint(infoRight, infoBottom) + st::historySendStatePosition, width);
	}
}

void HistoryMessage::setViewsCount(int32 count) {
	auto views = Get<HistoryMessageViews>();
	if (!views || views->_views == count || (count >= 0 && views->_views > count)) return;

	int32 was = views->_viewsWidth;
	views->_views = count;
	views->_viewsText = (views->_views >= 0) ? formatViewsCount(views->_views) : QString();
	views->_viewsWidth = views->_viewsText.isEmpty() ? 0 : st::msgDateFont->width(views->_viewsText);
	if (was == views->_viewsWidth) {
		Auth().data().requestItemRepaint(this);
	} else {
		if (_text.hasSkipBlock()) {
			_text.setSkipBlock(HistoryMessage::skipBlockWidth(), HistoryMessage::skipBlockHeight());
			_textWidth = -1;
			_textHeight = 0;
		}
		Auth().data().requestItemViewResize(this);
	}
}

void HistoryMessage::setRealId(MsgId newId) {
	HistoryItem::setRealId(newId);

	if (_text.hasSkipBlock()) {
		_text.setSkipBlock(HistoryMessage::skipBlockWidth(), HistoryMessage::skipBlockHeight());
		_textWidth = -1;
		_textHeight = 0;
	}
	Auth().data().requestItemViewResize(this);
}

void HistoryMessage::drawRightAction(Painter &p, int left, int top, int outerWidth) const {
	{
		p.setPen(Qt::NoPen);
		p.setBrush(st::msgServiceBg);

		PainterHighQualityEnabler hq(p);
		p.drawEllipse(rtlrect(left, top, st::historyFastShareSize, st::historyFastShareSize, outerWidth));
	}
	if (displayFastShare()) {
		st::historyFastShareIcon.paint(p, left, top, outerWidth);
	} else {
		st::historyGoToOriginalIcon.paint(p, left, top, outerWidth);
	}
}

void HistoryMessage::dependencyItemRemoved(HistoryItem *dependency) {
	if (auto reply = Get<HistoryMessageReply>()) {
		reply->itemRemoved(this, dependency);
	}
}

bool HistoryMessage::pointInTime(int right, int bottom, QPoint point, InfoDisplayType type) const {
	auto infoRight = right;
	auto infoBottom = bottom;
	switch (type) {
	case InfoDisplayDefault:
		infoRight -= st::msgPadding.right() - st::msgDateDelta.x();
		infoBottom -= st::msgPadding.bottom() - st::msgDateDelta.y();
	break;
	case InfoDisplayOverImage:
		infoRight -= st::msgDateImgDelta + st::msgDateImgPadding.x();
		infoBottom -= st::msgDateImgDelta + st::msgDateImgPadding.y();
	break;
	}
	auto dateX = infoRight - HistoryMessage::infoWidth() + HistoryMessage::timeLeft();
	auto dateY = infoBottom - st::msgDateFont->height;
	return QRect(dateX, dateY, HistoryMessage::timeWidth(), st::msgDateFont->height).contains(point);
}

ClickHandlerPtr HistoryMessage::rightActionLink() const {
	if (!_rightActionLink) {
		const auto itemId = fullId();
		const auto forwarded = Get<HistoryMessageForwarded>();
		const auto savedFromPeer = forwarded ? forwarded->savedFromPeer : nullptr;
		const auto savedFromMsgId = forwarded ? forwarded->savedFromMsgId : 0;
		_rightActionLink = std::make_shared<LambdaClickHandler>([=] {
			if (auto item = App::histItemById(itemId)) {
				if (savedFromPeer && savedFromMsgId) {
					App::wnd()->controller()->showPeerHistory(
						savedFromPeer,
						Window::SectionShow::Way::Forward,
						savedFromMsgId);
				} else {
					FastShareMessage(item);
				}
			}
		});
	}
	return _rightActionLink;
}

ClickHandlerPtr HistoryMessage::fastReplyLink() const {
	if (!_fastReplyLink) {
		const auto itemId = fullId();
		_fastReplyLink = std::make_shared<LambdaClickHandler>([=] {
			if (const auto item = App::histItemById(itemId)) {
				if (const auto main = App::main()) {
					main->replyToItem(item);
				}
			}
		});
	}
	return _fastReplyLink;
}

TextSelection HistoryMessage::adjustSelection(TextSelection selection, TextSelectType type) const {
	auto result = _text.adjustSelection(selection, type);
	auto beforeMediaLength = _text.length();
	if (selection.to <= beforeMediaLength) {
		return result;
	}
	auto mediaDisplayed = _media && _media->isDisplayed();
	if (mediaDisplayed) {
		auto mediaSelection = unskipTextSelection(_media->adjustSelection(skipTextSelection(selection), type));
		if (selection.from >= beforeMediaLength) {
			result = mediaSelection;
		} else {
			result.to = mediaSelection.to;
		}
	}
	auto beforeEntryLength = beforeMediaLength + (mediaDisplayed ? _media->fullSelectionLength() : 0);
	if (selection.to <= beforeEntryLength) {
		return result;
	}
	if (auto entry = Get<HistoryMessageLogEntryOriginal>()) {
		auto entrySelection = mediaDisplayed ? _media->skipSelection(skipTextSelection(selection)) : skipTextSelection(selection);
		auto logEntryOriginalSelection = entry->_page->adjustSelection(entrySelection, type);
		if (mediaDisplayed) {
			logEntryOriginalSelection = _media->unskipSelection(logEntryOriginalSelection);
		}
		logEntryOriginalSelection = unskipTextSelection(logEntryOriginalSelection);
		if (selection.from >= beforeEntryLength) {
			result = logEntryOriginalSelection;
		} else {
			result.to = logEntryOriginalSelection.to;
		}
	}
	return result;
}

QString HistoryMessage::notificationHeader() const {
	return (!_history->peer->isUser() && !isPost()) ? from()->name : QString();
}

std::unique_ptr<HistoryView::Element> HistoryMessage::createView(
		not_null<Window::Controller*> controller,
		HistoryView::Context context) {
	return controller->createMessageView(this, context);
}

HistoryMessage::~HistoryMessage() {
	_media.reset();
	if (auto reply = Get<HistoryMessageReply>()) {
		reply->clearData(this);
	}
}
