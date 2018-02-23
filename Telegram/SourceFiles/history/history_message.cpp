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
#include "window/notifications_manager.h"
#include "window/window_controller.h"
#include "observer_peer.h"
#include "storage/storage_shared_media.h"
#include "data/data_session.h"
#include "data/data_media_types.h"
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
	if (const auto channel = peer->asChannel()) {
		if (dynamic_cast<Data::MediaWebPage*>(fwd->media())) {
			// Drop web page if we're not allowed to send it.
			if (channel->restricted(
					ChannelRestriction::f_embed_links)) {
				result &= ~MTPDmessage::Flag::f_media;
			}
		}
	} else if (const auto media = fwd->media()) {
		if (media->forwardedBecomesUnread()) {
			result |= MTPDmessage::Flag::f_media_unread;
		}
	}
	if (fwd->hasViews()) {
		result |= MTPDmessage::Flag::f_views;
	}
	return result;
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
	const auto isGroup = (Auth().data().groups().find(item) != nullptr);
	const auto isGame = item->getMessageBot()
		&& item->media()
		&& (item->media()->game() != nullptr);
	const auto canCopyLink = item->hasDirectLink() || isGame;

	auto copyCallback = [data]() {
		if (auto main = App::main()) {
			if (auto item = App::histItemById(data->msgIds[0])) {
				if (item->hasDirectLink()) {
					QApplication::clipboard()->setText(item->directLink());

					Ui::Toast::Show(lang(lng_channel_public_link_copied));
				} else if (const auto bot = item->getMessageBot()) {
					if (const auto media = item->media()) {
						if (const auto game = media->game()) {
							const auto link = Messenger::Instance().createInternalLinkFull(
								bot->username
								+ qsl("?game=")
								+ game->shortName);

							QApplication::clipboard()->setText(link);

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
		for (const auto item : items) {
			if (const auto media = item->media()) {
				const auto error = media->errorTextForForward(megagroup);
				if (!error.isEmpty() && error != qstr("skip")) {
					return error;
				}
			}
		}
		if (megagroup->restricted(ChannelRestriction::f_send_inline)
			&& HasInlineItems(items)) {
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
	TimeId originalDate = 0;
	TimeId editDate = 0;

	// For messages created from MTP structs.
	const MTPReplyMarkup *mtpMarkup = nullptr;

	// For messages created from existing messages (forwarded).
	const HistoryMessageReplyMarkup *inlineMarkup = nullptr;
};

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	const MTPDmessage &data)
: HistoryItem(
		history,
		data.vid.v,
		data.vflags.v,
		data.vdate.v,
		data.has_from_id() ? data.vfrom_id.v : UserId(0)) {
	CreateConfig config;

	if (data.has_fwd_from() && data.vfwd_from.type() == mtpc_messageFwdHeader) {
		auto &f = data.vfwd_from.c_messageFwdHeader();
		config.originalDate = f.vdate.v;
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
	if (data.has_reply_to_msg_id()) config.replyTo = data.vreply_to_msg_id.v;
	if (data.has_via_bot_id()) config.viaBotId = data.vvia_bot_id.v;
	if (data.has_views()) config.viewsCount = data.vviews.v;
	if (data.has_reply_markup()) config.mtpMarkup = &data.vreply_markup;
	if (data.has_edit_date()) config.editDate = data.vedit_date.v;
	if (data.has_post_author()) config.author = qs(data.vpost_author);

	createComponents(config);

	if (data.has_media()) {
		setMedia(data.vmedia);
	}

	auto text = TextUtilities::Clean(qs(data.vmessage));
	auto entities = data.has_entities()
		? TextUtilities::EntitiesFromMTP(data.ventities.v)
		: EntitiesInText();
	setText({ text, entities });

	if (data.has_grouped_id()) {
		setGroupId(MessageGroupId::FromRaw(data.vgrouped_id.v));
	}
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	const MTPDmessageService &data)
: HistoryItem(
		history,
		data.vid.v,
		mtpCastFlags(data.vflags.v),
		data.vdate.v,
		data.has_from_id() ? data.vfrom_id.v : UserId(0)) {
	CreateConfig config;

	if (data.has_reply_to_msg_id()) config.replyTo = data.vreply_to_msg_id.v;

	createComponents(config);

	switch (data.vaction.type()) {
	case mtpc_messageActionPhoneCall: {
		_media = std::make_unique<Data::MediaCall>(
			this,
			data.vaction.c_messageActionPhoneCall());
	} break;

	default: Unexpected("Service message action type in HistoryMessage.");
	}

	setText(TextWithEntities {});
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	MsgId id,
	MTPDmessage::Flags flags,
	TimeId date,
	UserId from,
	const QString &postAuthor,
	not_null<HistoryMessage*> original)
: HistoryItem(
		history,
		id,
		NewForwardedFlags(history->peer, from, original) | flags,
		date,
		from) {
	CreateConfig config;

	if (original->Has<HistoryMessageForwarded>() || !original->history()->peer->isSelf()) {
		// Server doesn't add "fwd_from" to non-forwarded messages from chat with yourself.
		config.originalDate = original->dateOriginal();
		auto senderOriginal = original->senderOriginal();
		config.senderOriginal = senderOriginal->id;
		config.authorOriginal = original->authorOriginal();
		if (senderOriginal->isChannel()) {
			config.originalId = original->idOriginal();
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
			config.savedFromPeer = original->history()->peer->id;
			config.savedFromMsgId = original->id;
		//}
	}
	if (flags & MTPDmessage::Flag::f_post_author) {
		config.author = postAuthor;
	}
	auto fwdViaBot = original->viaBot();
	if (fwdViaBot) config.viaBotId = peerToUser(fwdViaBot->id);
	int fwdViewsCount = original->viewsCount();
	if (fwdViewsCount > 0) {
		config.viewsCount = fwdViewsCount;
	} else if (isPost()) {
		config.viewsCount = 1;
	}

	// Copy inline keyboard when forwarding messages with a game.
	auto mediaOriginal = original->media();
	if (mediaOriginal && mediaOriginal->game()) {
		config.inlineMarkup = original->inlineReplyMarkup();
	}

	createComponents(config);

	auto ignoreMedia = [&] {
		if (mediaOriginal && mediaOriginal->webpage()) {
			if (const auto channel = history->peer->asChannel()) {
				if (channel->restricted(ChannelRestriction::f_embed_links)) {
					return true;
				}
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
	MsgId replyTo,
	UserId viaBotId,
	TimeId date,
	UserId from,
	const QString &postAuthor,
	const TextWithEntities &textWithEntities)
: HistoryItem(history, id, flags, date, (flags & MTPDmessage::Flag::f_from_id) ? from : 0) {
	createComponentsHelper(flags, replyTo, viaBotId, postAuthor, MTPnullMarkup);

	setText(textWithEntities);
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	MsgId id,
	MTPDmessage::Flags flags,
	MsgId replyTo,
	UserId viaBotId,
	TimeId date,
	UserId from,
	const QString &postAuthor,
	not_null<DocumentData*> document,
	const TextWithEntities &caption,
	const MTPReplyMarkup &markup)
: HistoryItem(history, id, flags, date, (flags & MTPDmessage::Flag::f_from_id) ? from : 0) {
	createComponentsHelper(flags, replyTo, viaBotId, postAuthor, markup);

	_media = std::make_unique<Data::MediaFile>(this, document);
	setText(caption);
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	MsgId id,
	MTPDmessage::Flags flags,
	MsgId replyTo,
	UserId viaBotId,
	TimeId date,
	UserId from,
	const QString &postAuthor,
	not_null<PhotoData*> photo,
	const TextWithEntities &caption,
	const MTPReplyMarkup &markup)
: HistoryItem(history, id, flags, date, (flags & MTPDmessage::Flag::f_from_id) ? from : 0) {
	createComponentsHelper(flags, replyTo, viaBotId, postAuthor, markup);

	_media = std::make_unique<Data::MediaPhoto>(this, photo);
	setText(caption);
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	MsgId id,
	MTPDmessage::Flags flags,
	MsgId replyTo,
	UserId viaBotId,
	TimeId date,
	UserId from,
	const QString &postAuthor,
	not_null<GameData*> game,
	const MTPReplyMarkup &markup)
: HistoryItem(history, id, flags, date, (flags & MTPDmessage::Flag::f_from_id) ? from : 0) {
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
		Auth().data().requestItemResize(this);
	}
}

bool HistoryMessage::allowsForward() const {
	if (id < 0 || isLogEntry()) {
		return false;
	}
	return !_media || _media->allowsForward();
}

bool HistoryMessage::allowsEdit(TimeId now) const {
	const auto peer = _history->peer;
	const auto messageToMyself = peer->isSelf();
	const auto canPinInMegagroup = [&] {
		if (const auto megagroup = peer->asMegagroup()) {
			return megagroup->canPinMessages();
		}
		return false;
	}();
	const auto messageTooOld = (messageToMyself || canPinInMegagroup)
		? false
		: (now >= date() + Global::EditTimeLimit());
	if (id < 0 || messageTooOld) {
		return false;
	}

	if (Has<HistoryMessageVia>() || Has<HistoryMessageForwarded>()) {
		return false;
	}

	if (_media && !_media->allowsEdit()) {
		return false;
	}
	if (messageToMyself) {
		return true;
	}
	if (const auto channel = _history->peer->asChannel()) {
		if (isPost() && channel->canEditMessages()) {
			return true;
		}
		if (out()) {
			return !isPost() || channel->canPublish();
		}
	}
	return out();
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
	auto hasViaBot = (config.viaBotId != 0);
	auto hasInlineMarkup = [&config] {
		if (config.mtpMarkup) {
			return (config.mtpMarkup->type() == mtpc_replyInlineMarkup);
		}
		return (config.inlineMarkup != nullptr);
	};
	if (config.editDate != TimeId(0)) {
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
	_fromNameVersion = displayFrom()->nameVersion;
}

QString FormatViewsCount(int views) {
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

void HistoryMessage::refreshMedia(const MTPMessageMedia *media) {
	_media = nullptr;
	if (media) {
		setMedia(*media);
	}
}

void HistoryMessage::refreshSentMedia(const MTPMessageMedia *media) {
	const auto wasGrouped = Auth().data().groups().isGrouped(this);
	refreshMedia(media);
	if (wasGrouped) {
		Auth().data().groups().refreshMessage(this);
	} else {
		Auth().data().requestItemViewRefresh(this);
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
	switch (media.type()) {
	case mtpc_messageMediaContact: {
		const auto &data = media.c_messageMediaContact();
		return std::make_unique<Data::MediaContact>(
			item,
			data.vuser_id.v,
			qs(data.vfirst_name),
			qs(data.vlast_name),
			qs(data.vphone_number));
	} break;
	case mtpc_messageMediaGeo: {
		const auto &data = media.c_messageMediaGeo().vgeo;
		if (data.type() == mtpc_geoPoint) {
			return std::make_unique<Data::MediaLocation>(
				item,
				LocationCoords(data.c_geoPoint()));
		}
	} break;
	case mtpc_messageMediaGeoLive: {
		const auto &data = media.c_messageMediaGeoLive().vgeo;
		if (data.type() == mtpc_geoPoint) {
			return std::make_unique<Data::MediaLocation>(
				item,
				LocationCoords(data.c_geoPoint()));
		}
	} break;
	case mtpc_messageMediaVenue: {
		const auto &data = media.c_messageMediaVenue();
		if (data.vgeo.type() == mtpc_geoPoint) {
			return std::make_unique<Data::MediaLocation>(
				item,
				LocationCoords(data.vgeo.c_geoPoint()),
				qs(data.vtitle),
				qs(data.vaddress));
		}
	} break;
	case mtpc_messageMediaPhoto: {
		const auto &data = media.c_messageMediaPhoto();
		if (data.has_ttl_seconds()) {
			LOG(("App Error: "
				"Unexpected MTPMessageMediaPhoto "
				"with ttl_seconds in HistoryMessage."));
		} else if (data.has_photo() && data.vphoto.type() == mtpc_photo) {
			return std::make_unique<Data::MediaPhoto>(
				item,
				Auth().data().photo(data.vphoto.c_photo()));
		} else {
			LOG(("API Error: "
				"Got MTPMessageMediaPhoto "
				"without photo and without ttl_seconds."));
		}
	} break;
	case mtpc_messageMediaDocument: {
		const auto &data = media.c_messageMediaDocument();
		if (data.has_ttl_seconds()) {
			LOG(("App Error: "
				"Unexpected MTPMessageMediaDocument "
				"with ttl_seconds in HistoryMessage."));
		} else if (data.has_document()
			&& data.vdocument.type() == mtpc_document) {
			return std::make_unique<Data::MediaFile>(
				item,
				Auth().data().document(data.vdocument.c_document()));
		} else {
			LOG(("API Error: "
				"Got MTPMessageMediaDocument "
				"without document and without ttl_seconds."));
		}
	} break;
	case mtpc_messageMediaWebPage: {
		const auto &data = media.c_messageMediaWebPage().vwebpage;
		switch (data.type()) {
		case mtpc_webPageEmpty: break;
		case mtpc_webPagePending:
			return std::make_unique<Data::MediaWebPage>(
				item,
				Auth().data().webpage(data.c_webPagePending()));
			break;
		case mtpc_webPage:
			return std::make_unique<Data::MediaWebPage>(
				item,
				Auth().data().webpage(data.c_webPage()));
			break;
		case mtpc_webPageNotModified:
			LOG(("API Error: "
				"webPageNotModified is unexpected in message media."));
			break;
		}
	} break;
	case mtpc_messageMediaGame: {
		const auto &data = media.c_messageMediaGame().vgame;
		if (data.type() == mtpc_game) {
			return std::make_unique<Data::MediaGame>(
				item,
				Auth().data().game(data.c_game()));
		}
	} break;
	case mtpc_messageMediaInvoice: {
		return std::make_unique<Data::MediaInvoice>(
			item,
			media.c_messageMediaInvoice());
	} break;
	};

	return nullptr;
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
		edited->date = message.vedit_date.v;
	}

	TextWithEntities textWithEntities = { qs(message.vmessage), EntitiesInText() };
	if (message.has_entities()) {
		textWithEntities.entities = TextUtilities::EntitiesFromMTP(message.ventities.v);
	}
	setText(textWithEntities);
	setReplyMarkup(message.has_reply_markup() ? (&message.vreply_markup) : nullptr);
	refreshMedia(message.has_media() ? (&message.vmedia) : nullptr);
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
	refreshMedia(nullptr);
	setReplyMarkup(nullptr);
	setViewsCount(-1);

	finishEditionToEmpty();
}

void HistoryMessage::updateSentMedia(const MTPMessageMedia *media) {
	if (_flags & MTPDmessage_ClientFlag::f_from_inline_bot) {
		if (!media || !_media || !_media->updateInlineResultMedia(*media)) {
			refreshSentMedia(media);
		}
		_flags &= ~MTPDmessage_ClientFlag::f_from_inline_bot;
	} else {
		if (!media || !_media || !_media->updateSentMedia(*media)) {
			refreshSentMedia(media);
		}
	}
	Auth().data().requestItemResize(this);
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
		if (type == EntityInTextUrl || type == EntityInTextCustomUrl || type == EntityInTextEmail) {
			_flags |= MTPDmessage_ClientFlag::f_has_text_links;
			break;
		}
	}

	if (_media && _media->consumeMessageText(textWithEntities)) {
		setEmptyText();
	} else {
		_text.setMarkedText(
			st::messageTextStyle,
			textWithEntities,
			Ui::ItemTextOptions(this));
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
			Auth().data().requestItemResize(this);
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
			Auth().data().requestItemResize(this);
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
		Auth().data().requestItemResize(this);
		Notify::replyMarkupUpdated(this);
	}
}

TextWithEntities HistoryMessage::originalText() const {
	if (emptyText()) {
		return { QString(), EntitiesInText() };
	}
	return _text.originalTextWithEntities();
}

TextWithEntities HistoryMessage::clipboardText() const {
	if (emptyText()) {
		return { QString(), EntitiesInText() };
	}
	return _text.originalTextWithEntities(AllTextSelection, ExpandLinksAll);
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
	views->_viewsText = (views->_views >= 0)
		? FormatViewsCount(views->_views)
		: QString();
	views->_viewsWidth = views->_viewsText.isEmpty()
		? 0
		: st::msgDateFont->width(views->_viewsText);
	if (was == views->_viewsWidth) {
		Auth().data().requestItemRepaint(this);
	} else {
		Auth().data().requestItemResize(this);
	}
}

void HistoryMessage::setRealId(MsgId newId) {
	HistoryItem::setRealId(newId);
	Auth().data().groups().refreshMessage(this);
	Auth().data().requestItemResize(this);
}

void HistoryMessage::dependencyItemRemoved(HistoryItem *dependency) {
	if (auto reply = Get<HistoryMessageReply>()) {
		reply->itemRemoved(this, dependency);
	}
}

QString HistoryMessage::notificationHeader() const {
	return (!_history->peer->isUser() && !isPost()) ? from()->name : QString();
}

std::unique_ptr<HistoryView::Element> HistoryMessage::createView(
		not_null<HistoryView::ElementDelegate*> delegate) {
	return delegate->elementCreate(this);
}

HistoryMessage::~HistoryMessage() {
	_media.reset();
	if (auto reply = Get<HistoryMessageReply>()) {
		reply->clearData(this);
	}
}
