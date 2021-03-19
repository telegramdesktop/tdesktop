/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_item.h"

#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "layout.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_service_message.h"
#include "history/history_item_components.h"
#include "history/view/media/history_view_media_grouped.h"
#include "history/history_service.h"
#include "history/history_message.h"
#include "history/history.h"
#include "mtproto/mtproto_config.h"
#include "media/clip/media_clip_reader.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/text_isolated_emoji.h"
#include "ui/text/text_options.h"
#include "storage/file_upload.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "media/audio/media_audio.h"
#include "core/application.h"
#include "mainwindow.h"
#include "window/window_session_controller.h"
#include "core/crash_reports.h"
#include "base/unixtime.h"
#include "api/api_text_entities.h"
#include "data/data_scheduled_messages.h" // kScheduledUntilOnlineTimestamp
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_messages.h"
#include "data/data_media_types.h"
#include "data/data_folder.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "styles/style_dialogs.h"
#include "styles/style_chat.h"

namespace {

constexpr auto kNotificationTextLimit = 255;

enum class MediaCheckResult {
	Good,
	Unsupported,
	Empty,
	HasTimeToLive,
};

not_null<HistoryItem*> CreateUnsupportedMessage(
		not_null<History*> history,
		MsgId msgId,
		MTPDmessage::Flags flags,
		MTPDmessage_ClientFlags clientFlags,
		MsgId replyTo,
		UserId viaBotId,
		TimeId date,
		PeerId from) {
	const auto siteLink = qsl("https://desktop.telegram.org");
	auto text = TextWithEntities{
		tr::lng_message_unsupported(tr::now, lt_link, siteLink)
	};
	TextUtilities::ParseEntities(text, Ui::ItemTextNoMonoOptions().flags);
	text.entities.push_front(
		EntityInText(EntityType::Italic, 0, text.text.size()));
	flags &= ~MTPDmessage::Flag::f_post_author;
	flags |= MTPDmessage::Flag::f_legacy;
	return history->makeMessage(
		msgId,
		flags,
		clientFlags,
		replyTo,
		viaBotId,
		date,
		from,
		QString(),
		text);
}

MediaCheckResult CheckMessageMedia(const MTPMessageMedia &media) {
	using Result = MediaCheckResult;
	return media.match([](const MTPDmessageMediaEmpty &) {
		return Result::Good;
	}, [](const MTPDmessageMediaContact &) {
		return Result::Good;
	}, [](const MTPDmessageMediaGeo &data) {
		return data.vgeo().match([](const MTPDgeoPoint &) {
			return Result::Good;
		}, [](const MTPDgeoPointEmpty &) {
			return Result::Empty;
		});
	}, [](const MTPDmessageMediaVenue &data) {
		return data.vgeo().match([](const MTPDgeoPoint &) {
			return Result::Good;
		}, [](const MTPDgeoPointEmpty &) {
			return Result::Empty;
		});
	}, [](const MTPDmessageMediaGeoLive &data) {
		return data.vgeo().match([](const MTPDgeoPoint &) {
			return Result::Good;
		}, [](const MTPDgeoPointEmpty &) {
			return Result::Empty;
		});
	}, [](const MTPDmessageMediaPhoto &data) {
		const auto photo = data.vphoto();
		if (data.vttl_seconds()) {
			return Result::HasTimeToLive;
		} else if (!photo) {
			return Result::Empty;
		}
		return photo->match([](const MTPDphoto &) {
			return Result::Good;
		}, [](const MTPDphotoEmpty &) {
			return Result::Empty;
		});
	}, [](const MTPDmessageMediaDocument &data) {
		const auto document = data.vdocument();
		if (data.vttl_seconds()) {
			return Result::HasTimeToLive;
		} else if (!document) {
			return Result::Empty;
		}
		return document->match([](const MTPDdocument &) {
			return Result::Good;
		}, [](const MTPDdocumentEmpty &) {
			return Result::Empty;
		});
	}, [](const MTPDmessageMediaWebPage &data) {
		return data.vwebpage().match([](const MTPDwebPage &) {
			return Result::Good;
		}, [](const MTPDwebPageEmpty &) {
			return Result::Good;
		}, [](const MTPDwebPagePending &) {
			return Result::Good;
		}, [](const MTPDwebPageNotModified &) {
			return Result::Unsupported;
		});
	}, [](const MTPDmessageMediaGame &data) {
		return data.vgame().match([](const MTPDgame &) {
			return Result::Good;
		});
	}, [](const MTPDmessageMediaInvoice &) {
		return Result::Good;
	}, [](const MTPDmessageMediaPoll &) {
		return Result::Good;
	}, [](const MTPDmessageMediaDice &) {
		return Result::Good;
	}, [](const MTPDmessageMediaUnsupported &) {
		return Result::Unsupported;
	});
}

} // namespace

void HistoryItem::HistoryItem::Destroyer::operator()(HistoryItem *value) {
	if (value) {
		value->destroy();
	}
}

HistoryItem::HistoryItem(
	not_null<History*> history,
	MsgId id,
	MTPDmessage::Flags flags,
	MTPDmessage_ClientFlags clientFlags,
	TimeId date,
	PeerId from)
: id(id)
, _history(history)
, _from(from ? history->owner().peer(from) : history->peer)
, _flags(flags)
, _clientFlags(clientFlags)
, _date(date) {
	if (isHistoryEntry() && IsClientMsgId(id)) {
		_history->registerLocalMessage(this);
	}
}

TimeId HistoryItem::date() const {
	return _date;
}

TimeId HistoryItem::NewMessageDate(TimeId scheduled) {
	return scheduled ? scheduled : base::unixtime::now();
}

void HistoryItem::applyServiceDateEdition(const MTPDmessageService &data) {
	const auto date = data.vdate().v;
	if (_date == date) {
		return;
	}
	_date = date;
}

void HistoryItem::finishEdition(int oldKeyboardTop) {
	_history->owner().requestItemViewRefresh(this);
	invalidateChatListEntry();
	if (const auto group = _history->owner().groups().find(this)) {
		const auto leader = group->items.front();
		if (leader != this) {
			_history->owner().requestItemViewRefresh(leader);
			leader->invalidateChatListEntry();
		}
	}

	// Should be completely redesigned as the oldTop no longer exists.
	//if (oldKeyboardTop >= 0) { // #TODO edit bot message
	//	if (auto keyboard = Get<HistoryMessageReplyMarkup>()) {
	//		keyboard->oldTop = oldKeyboardTop;
	//	}
	//}

	_history->owner().updateDependentMessages(this);
}

void HistoryItem::setGroupId(MessageGroupId groupId) {
	Expects(!_groupId);

	_groupId = groupId;
	_history->owner().groups().registerMessage(this);
}

HistoryMessageReplyMarkup *HistoryItem::inlineReplyMarkup() {
	if (const auto markup = Get<HistoryMessageReplyMarkup>()) {
		if (markup->flags & MTPDreplyKeyboardMarkup_ClientFlag::f_inline) {
			return markup;
		}
	}
	return nullptr;
}

ReplyKeyboard *HistoryItem::inlineReplyKeyboard() {
	if (const auto markup = inlineReplyMarkup()) {
		return markup->inlineKeyboard.get();
	}
	return nullptr;
}

ChannelData *HistoryItem::discussionPostOriginalSender() const {
	if (!history()->peer->isMegagroup()) {
		return nullptr;
	}
	if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		const auto from = forwarded->savedFromPeer;
		if (const auto result = from ? from->asChannel() : nullptr) {
			return result;
		}
	}
	return nullptr;
}

bool HistoryItem::isDiscussionPost() const {
	return (discussionPostOriginalSender() != nullptr);
}

HistoryItem *HistoryItem::lookupDiscussionPostOriginal() const {
	if (!history()->peer->isMegagroup()) {
		return nullptr;
	}
	const auto forwarded = Get<HistoryMessageForwarded>();
	if (!forwarded
		|| !forwarded->savedFromPeer
		|| !forwarded->savedFromMsgId) {
		return nullptr;
	}
	return _history->owner().message(
		forwarded->savedFromPeer->asChannel(),
		forwarded->savedFromMsgId);
}

PeerData *HistoryItem::displayFrom() const {
	if (const auto sender = discussionPostOriginalSender()) {
		return sender;
	} else if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		if (history()->peer->isSelf() || history()->peer->isRepliesChat() || forwarded->imported) {
			return forwarded->originalSender;
		}
	}
	return author().get();
}

void HistoryItem::invalidateChatListEntry() {
	history()->session().changes().messageUpdated(
		this,
		Data::MessageUpdate::Flag::DialogRowRefresh);

	// invalidate cache for drawInDialog
	if (history()->textCachedFor == this) {
		history()->textCachedFor = nullptr;
	}
	//if (const auto feed = history()->peer->feed()) { // #TODO archive
	//	if (feed->textCachedFor == this) {
	//		feed->textCachedFor = nullptr;
	//		feed->updateChatListEntry();
	//	}
	//}
}

void HistoryItem::finishEditionToEmpty() {
	finishEdition(-1);
	_history->itemVanished(this);
}

bool HistoryItem::hasUnreadMediaFlag() const {
	if (_history->peer->isChannel()) {
		const auto passed = base::unixtime::now() - date();
		const auto &config = _history->session().serverConfig();
		if (passed >= config.channelsReadMediaPeriod) {
			return false;
		}
	}
	return _flags & MTPDmessage::Flag::f_media_unread;
}

bool HistoryItem::isUnreadMention() const {
	return mentionsMe() && (_flags & MTPDmessage::Flag::f_media_unread);
}

bool HistoryItem::mentionsMe() const {
	if (Has<HistoryServicePinned>()
		&& !Core::App().settings().notifyAboutPinned()) {
		return false;
	}
	return _flags & MTPDmessage::Flag::f_mentioned;
}

bool HistoryItem::isUnreadMedia() const {
	if (!hasUnreadMediaFlag()) {
		return false;
	} else if (const auto media = this->media()) {
		if (const auto document = media->document()) {
			if (document->isVoiceMessage() || document->isVideoMessage()) {
				return (media->webpage() == nullptr);
			}
		}
	}
	return false;
}

void HistoryItem::markMediaRead() {
	_flags &= ~MTPDmessage::Flag::f_media_unread;

	if (mentionsMe()) {
		history()->updateChatListEntry();
		history()->eraseFromUnreadMentions(id);
	}
}

void HistoryItem::setIsPinned(bool pinned) {
	const auto changed = (isPinned() != pinned);
	if (pinned) {
		_flags |= MTPDmessage::Flag::f_pinned;
		history()->session().storage().add(Storage::SharedMediaAddExisting(
			history()->peer->id,
			Storage::SharedMediaType::Pinned,
			id,
			{ id, id }));
		history()->peer->setHasPinnedMessages(true);
	} else {
		_flags &= ~MTPDmessage::Flag::f_pinned;
		history()->session().storage().remove(Storage::SharedMediaRemoveOne(
			history()->peer->id,
			Storage::SharedMediaType::Pinned,
			id));
	}
	if (changed) {
		history()->owner().requestItemResize(this);
	}
}

bool HistoryItem::definesReplyKeyboard() const {
	if (const auto markup = Get<HistoryMessageReplyMarkup>()) {
		if (markup->flags & MTPDreplyKeyboardMarkup_ClientFlag::f_inline) {
			return false;
		}
		return true;
	}

	// optimization: don't create markup component for the case
	// MTPDreplyKeyboardHide with flags = 0, assume it has f_zero flag
	return (_flags & MTPDmessage::Flag::f_reply_markup);
}

MTPDreplyKeyboardMarkup::Flags HistoryItem::replyKeyboardFlags() const {
	Expects(definesReplyKeyboard());

	if (const auto markup = Get<HistoryMessageReplyMarkup>()) {
		return markup->flags;
	}

	// optimization: don't create markup component for the case
	// MTPDreplyKeyboardHide with flags = 0, assume it has f_zero flag
	return MTPDreplyKeyboardMarkup_ClientFlag::f_zero | 0;
}

void HistoryItem::addLogEntryOriginal(
		WebPageId localId,
		const QString &label,
		const TextWithEntities &content) {
	Expects(isAdminLogEntry());

	AddComponents(HistoryMessageLogEntryOriginal::Bit());
	Get<HistoryMessageLogEntryOriginal>()->page = _history->owner().webpage(
		localId,
		label,
		content);
}

PeerData *HistoryItem::specialNotificationPeer() const {
	return (mentionsMe() && !_history->peer->isUser())
		? from().get()
		: nullptr;
}

UserData *HistoryItem::viaBot() const {
	if (const auto via = Get<HistoryMessageVia>()) {
		return via->bot;
	}
	return nullptr;
}

UserData *HistoryItem::getMessageBot() const {
	if (const auto bot = viaBot()) {
		return bot;
	}
	auto bot = from()->asUser();
	if (!bot) {
		bot = history()->peer->asUser();
	}
	return (bot && bot->isBot()) ? bot : nullptr;
}

bool HistoryItem::isHistoryEntry() const {
	return IsServerMsgId(id)
		|| (_clientFlags & MTPDmessage_ClientFlag::f_local_history_entry);
}

bool HistoryItem::isAdminLogEntry() const {
	return (_clientFlags & MTPDmessage_ClientFlag::f_admin_log_entry);
}

bool HistoryItem::isFromScheduled() const {
	return isHistoryEntry()
		&& (_flags & MTPDmessage::Flag::f_from_scheduled);
}

bool HistoryItem::isScheduled() const {
	return !isHistoryEntry()
		&& !isAdminLogEntry()
		&& (_flags & MTPDmessage::Flag::f_from_scheduled);
}

void HistoryItem::destroy() {
	_history->destroyMessage(this);
}

void HistoryItem::refreshMainView() {
	if (const auto view = mainView()) {
		_history->owner().notifyHistoryChangeDelayed(_history);
		view->refreshInBlock();
	}
}

void HistoryItem::removeMainView() {
	if (const auto view = mainView()) {
		_history->owner().notifyHistoryChangeDelayed(_history);
		view->removeFromBlock();
	}
}

void HistoryItem::clearMainView() {
	_mainView = nullptr;
}

void HistoryItem::addToUnreadMentions(UnreadMentionType type) {
}

void HistoryItem::applyEditionToHistoryCleared() {
	applyEdition(
		MTP_messageService(
			MTP_flags(0),
			MTP_int(id),
			peerToMTP(PeerId(0)), // from_id
			peerToMTP(history()->peer->id),
			MTPMessageReplyHeader(),
			MTP_int(date()),
			MTP_messageActionHistoryClear(),
			MTPint() // ttl_period
		).c_messageService());
}

void HistoryItem::applySentMessage(const MTPDmessage &data) {
	updateSentContent({
		qs(data.vmessage()),
		Api::EntitiesFromMTP(
			&history()->session(),
			data.ventities().value_or_empty())
	}, data.vmedia());
	updateReplyMarkup(data.vreply_markup());
	updateForwardedInfo(data.vfwd_from());
	setViewsCount(data.vviews().value_or(-1));
	if (const auto replies = data.vreplies()) {
		setReplies(*replies);
	} else {
		clearReplies();
	}
	setForwardsCount(data.vforwards().value_or(-1));
	if (const auto reply = data.vreply_to()) {
		reply->match([&](const MTPDmessageReplyHeader &data) {
			setReplyToTop(
				data.vreply_to_top_id().value_or(
					data.vreply_to_msg_id().v));
		});
	}
	setPostAuthor(data.vpost_author().value_or_empty());
	contributeToSlowmode(data.vdate().v);
	indexAsNewItem();
	history()->owner().requestItemTextRefresh(this);
	history()->owner().updateDependentMessages(this);
}

void HistoryItem::applySentMessage(
		const QString &text,
		const MTPDupdateShortSentMessage &data,
		bool wasAlready) {
	updateSentContent({
		text,
		Api::EntitiesFromMTP(
			&history()->session(),
			data.ventities().value_or_empty())
		}, data.vmedia());
	contributeToSlowmode(data.vdate().v);
	if (!wasAlready) {
		indexAsNewItem();
	}
}

void HistoryItem::indexAsNewItem() {
	if (IsServerMsgId(id)) {
		addToUnreadMentions(UnreadMentionType::New);
		if (const auto types = sharedMediaTypes()) {
			_history->session().storage().add(Storage::SharedMediaAddNew(
				_history->peer->id,
				types,
				id));
			if (types.test(Storage::SharedMediaType::Pinned)) {
				_history->peer->setHasPinnedMessages(true);
			}
		}
	}
}

void HistoryItem::setRealId(MsgId newId) {
	Expects(_clientFlags & MTPDmessage_ClientFlag::f_sending);
	Expects(IsClientMsgId(id));

	const auto oldId = std::exchange(id, newId);
	_clientFlags &= ~MTPDmessage_ClientFlag::f_sending;
	if (IsServerMsgId(id)) {
		_history->unregisterLocalMessage(this);
	}
	_history->owner().notifyItemIdChange({ this, oldId });

	// We don't fire MessageUpdate::Flag::ReplyMarkup and update keyboard
	// in history widget, because it can't exist for an outgoing message.
	// Only inline keyboards can be in outgoing messages.
	if (const auto markup = inlineReplyMarkup()) {
		if (markup->inlineKeyboard) {
			markup->inlineKeyboard->updateMessageId();
		}
	}

	_history->owner().requestItemRepaint(this);
}

bool HistoryItem::canPin() const {
	if (id < 0 || !toHistoryMessage()) {
		return false;
	} else if (const auto m = media(); m && m->call()) {
		return false;
	}
	return _history->peer->canPinMessages();
}

bool HistoryItem::allowsSendNow() const {
	return false;
}

bool HistoryItem::allowsForward() const {
	return false;
}

bool HistoryItem::allowsEdit(TimeId now) const {
	return false;
}

bool HistoryItem::canStopPoll() const {
	if (id < 0
		|| Has<HistoryMessageVia>()
		|| Has<HistoryMessageForwarded>()) {
		return false;
	}

	const auto peer = _history->peer;
	if (peer->isSelf()) {
		return true;
	} else if (const auto channel = peer->asChannel()) {
		if (isPost() && channel->canEditMessages()) {
			return true;
		} else if (out()) {
			return isPost() ? channel->canPublish() : channel->canWrite();
		} else {
			return false;
		}
	}
	return out();
}

bool HistoryItem::canDelete() const {
	if (!IsServerMsgId(id) && serviceMsg()) {
		return false;
	} else if (!isHistoryEntry() && !isScheduled()) {
		return false;
	}
	auto channel = _history->peer->asChannel();
	if (!channel) {
		return !isGroupMigrate();
	}

	if (id == 1) {
		return false;
	}
	if (channel->canDeleteMessages()) {
		return true;
	}
	if (out() && toHistoryMessage()) {
		return isPost() ? channel->canPublish() : true;
	}
	return false;
}

bool HistoryItem::canDeleteForEveryone(TimeId now) const {
	const auto peer = history()->peer;
	const auto &config = history()->session().serverConfig();
	const auto messageToMyself = peer->isSelf();
	const auto messageTooOld = messageToMyself
		? false
		: peer->isUser()
		? (now - date() >= config.revokePrivateTimeLimit)
		: (now - date() >= config.revokeTimeLimit);
	if (id < 0 || messageToMyself || messageTooOld || isPost()) {
		return false;
	}
	if (peer->isChannel()) {
		return false;
	} else if (const auto user = peer->asUser()) {
		// Bots receive all messages and there is no sense in revoking them.
		// See https://github.com/telegramdesktop/tdesktop/issues/3818
		if (user->isBot() && !user->isSupport()) {
			return false;
		}
	}
	if (const auto media = this->media()) {
		if (!media->allowsRevoke(now)) {
			return false;
		}
	}
	if (!out()) {
		if (const auto chat = peer->asChat()) {
			if (!chat->canDeleteMessages()) {
				return false;
			}
		} else if (peer->isUser()) {
			return config.revokePrivateInbox;
		} else {
			return false;
		}
	}
	return true;
}

bool HistoryItem::suggestReport() const {
	if (out() || serviceMsg() || !IsServerMsgId(id)) {
		return false;
	} else if (const auto channel = history()->peer->asChannel()) {
		return true;
	} else if (const auto user = history()->peer->asUser()) {
		return user->isBot();
	}
	return false;
}

bool HistoryItem::suggestBanReport() const {
	const auto channel = history()->peer->asChannel();
	const auto fromUser = from()->asUser();
	if (!channel
		|| !fromUser
		|| !channel->canRestrictParticipant(fromUser)) {
		return false;
	}
	return !isPost() && !out();
}

bool HistoryItem::suggestDeleteAllReport() const {
	auto channel = history()->peer->asChannel();
	if (!channel || !channel->canDeleteMessages()) {
		return false;
	}
	return !isPost() && !out() && from()->isUser();
}

bool HistoryItem::hasDirectLink() const {
	return IsServerMsgId(id) && _history->peer->isChannel();
}

ChannelId HistoryItem::channelId() const {
	return _history->channelId();
}

Data::MessagePosition HistoryItem::position() const {
	return { .fullId = fullId(), .date = date() };
}

MsgId HistoryItem::replyToId() const {
	if (const auto reply = Get<HistoryMessageReply>()) {
		return reply->replyToId();
	}
	return 0;
}

MsgId HistoryItem::replyToTop() const {
	if (const auto reply = Get<HistoryMessageReply>()) {
		return reply->replyToTop();
	}
	return 0;
}

not_null<PeerData*> HistoryItem::author() const {
	return isPost() ? history()->peer : from();
}

TimeId HistoryItem::dateOriginal() const {
	if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		return forwarded->originalDate;
	}
	return date();
}

PeerData *HistoryItem::senderOriginal() const {
	if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		return forwarded->originalSender;
	}
	const auto peer = history()->peer;
	return (peer->isChannel() && !peer->isMegagroup()) ? peer : from();
}

const HiddenSenderInfo *HistoryItem::hiddenForwardedInfo() const {
	if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		return forwarded->hiddenSenderInfo.get();
	}
	return nullptr;
}

not_null<PeerData*> HistoryItem::fromOriginal() const {
	if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		if (forwarded->originalSender) {
			if (const auto user = forwarded->originalSender->asUser()) {
				return user;
			}
		}
	}
	return from();
}

QString HistoryItem::authorOriginal() const {
	if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		return forwarded->originalAuthor;
	} else if (const auto msgsigned = Get<HistoryMessageSigned>()) {
		if (!msgsigned->isAnonymousRank) {
			return msgsigned->author;
		}
	}
	return QString();
}

MsgId HistoryItem::idOriginal() const {
	if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		return forwarded->originalId;
	}
	return id;
}

void HistoryItem::updateDate(TimeId newDate) {
	if (canUpdateDate() && _date != newDate) {
		_date = newDate;
		_history->owner().requestItemViewRefresh(this);
	}
}

bool HistoryItem::canUpdateDate() const {
	return isScheduled();
}

void HistoryItem::applyTTL(const MTPDmessage &data) {
	if (const auto period = data.vttl_period()) {
		if (period->v > 0) {
			applyTTL(data.vdate().v + period->v);
		}
	}
}

void HistoryItem::applyTTL(const MTPDmessageService &data) {
	if (const auto period = data.vttl_period()) {
		if (period->v > 0) {
			applyTTL(data.vdate().v + period->v);
		}
	}
}

void HistoryItem::applyTTL(TimeId destroyAt) {
	const auto previousDestroyAt = std::exchange(_ttlDestroyAt, destroyAt);
	if (previousDestroyAt) {
		history()->owner().unregisterMessageTTL(previousDestroyAt, this);
	}
	if (!_ttlDestroyAt) {
		return;
	} else if (base::unixtime::now() >= _ttlDestroyAt) {
		const auto session = &history()->session();
		crl::on_main(session, [session, id = fullId()]{
			if (const auto item = session->data().message(id)) {
				item->destroy();
			}
		});
	} else {
		history()->owner().registerMessageTTL(_ttlDestroyAt, this);
	}
}

void HistoryItem::sendFailed() {
	Expects(_clientFlags & MTPDmessage_ClientFlag::f_sending);
	Expects(!(_clientFlags & MTPDmessage_ClientFlag::f_failed));

	_clientFlags = (_clientFlags | MTPDmessage_ClientFlag::f_failed)
		& ~MTPDmessage_ClientFlag::f_sending;
	history()->session().changes().historyUpdated(
		history(),
		Data::HistoryUpdate::Flag::LocalMessages);
}

bool HistoryItem::needCheck() const {
	return (out() && !isEmpty()) || (id < 0 && history()->peer->isSelf());
}

bool HistoryItem::unread() const {
	// Messages from myself are always read, unless scheduled.
	if (history()->peer->isSelf() && !isFromScheduled()) {
		return false;
	}

	if (out()) {
		// Outgoing messages in converted chats are always read.
		if (history()->peer->migrateTo()) {
			return false;
		}

		if (IsServerMsgId(id)) {
			if (!history()->isServerSideUnread(this)) {
				return false;
			}
			if (const auto user = history()->peer->asUser()) {
				if (user->isBot()) {
					return false;
				}
			} else if (const auto channel = history()->peer->asChannel()) {
				if (!channel->isMegagroup()) {
					return false;
				}
			}
		}
		return true;
	}

	if (IsServerMsgId(id)) {
		if (!history()->isServerSideUnread(this)) {
			return false;
		}
		return true;
	}
	return (_clientFlags & MTPDmessage_ClientFlag::f_clientside_unread);
}

bool HistoryItem::showNotification() const {
	const auto channel = _history->peer->asChannel();
	if (channel && !channel->amIn()) {
		return false;
	}
	return (out() || _history->peer->isSelf())
		? isFromScheduled()
		: unread();
}

void HistoryItem::markClientSideAsRead() {
	_clientFlags &= ~MTPDmessage_ClientFlag::f_clientside_unread;
}

MessageGroupId HistoryItem::groupId() const {
	return _groupId;
}

bool HistoryItem::isEmpty() const {
	return _text.isEmpty()
		&& !_media
		&& !Has<HistoryMessageLogEntryOriginal>();
}

QString HistoryItem::notificationText() const {
	const auto result = [&] {
		if (_media) {
			return _media->notificationText();
		} else if (!emptyText()) {
			return _text.toString();
		}
		return QString();
	}();
	return (result.size() <= kNotificationTextLimit)
		? result
		: result.mid(0, kNotificationTextLimit) + qsl("...");
}

QString HistoryItem::inDialogsText(DrawInDialog way) const {
	auto getText = [this]() {
		if (_media) {
			if (_groupId) {
				return textcmdLink(1, TextUtilities::Clean(tr::lng_in_dlg_album(tr::now)));
			}
			return _media->chatListText();
		} else if (!emptyText()) {
			return TextUtilities::Clean(_text.toString());
		}
		return QString();
	};
	const auto plainText = getText();
	const auto sender = [&]() -> PeerData* {
		if (isPost() || isEmpty() || (way == DrawInDialog::WithoutSender)) {
			return nullptr;
		} else if (!_history->peer->isUser() || out()) {
			return displayFrom();
		} else if (_history->peer->isSelf() && !Has<HistoryMessageForwarded>()) {
			return senderOriginal();
		}
		return nullptr;
	}();
	if (sender) {
		auto fromText = sender->isSelf() ? tr::lng_from_you(tr::now) : sender->shortName();
		auto fromWrapped = textcmdLink(1, tr::lng_dialogs_text_from_wrapped(tr::now, lt_from, TextUtilities::Clean(fromText)));
		return tr::lng_dialogs_text_with_from(tr::now, lt_from_part, fromWrapped, lt_message, plainText);
	}
	return plainText;
}

Ui::Text::IsolatedEmoji HistoryItem::isolatedEmoji() const {
	return Ui::Text::IsolatedEmoji();
}

void HistoryItem::drawInDialog(
		Painter &p,
		const QRect &r,
		bool active,
		bool selected,
		DrawInDialog way,
		const HistoryItem *&cacheFor,
		Ui::Text::String &cache) const {
	if (r.isEmpty()) {
		return;
	}
	if (cacheFor != this) {
		cacheFor = this;
		cache.setText(st::dialogsTextStyle, inDialogsText(way), Ui::DialogTextOptions());
	}
	p.setTextPalette(active ? st::dialogsTextPaletteActive : (selected ? st::dialogsTextPaletteOver : st::dialogsTextPalette));
	p.setFont(st::dialogsTextFont);
	p.setPen(active ? st::dialogsTextFgActive : (selected ? st::dialogsTextFgOver : st::dialogsTextFg));
	cache.drawElided(p, r.left(), r.top(), r.width(), r.height() / st::dialogsTextFont->height);
	p.restoreTextPalette();
}

HistoryItem::~HistoryItem() {
	applyTTL(0);
}

QDateTime ItemDateTime(not_null<const HistoryItem*> item) {
	return base::unixtime::parse(item->date());
}

QString ItemDateText(not_null<const HistoryItem*> item, bool isUntilOnline) {
	const auto dateText = langDayOfMonthFull(ItemDateTime(item).date());
	return !item->isScheduled()
		? dateText
		: isUntilOnline
			? tr::lng_scheduled_date_until_online(tr::now)
			: tr::lng_scheduled_date(tr::now, lt_date, dateText);
}

bool IsItemScheduledUntilOnline(not_null<const HistoryItem*> item) {
	return item->isScheduled()
		&& (item->date() ==
			Data::ScheduledMessages::kScheduledUntilOnlineTimestamp);
}

ClickHandlerPtr goToMessageClickHandler(
		not_null<HistoryItem*> item,
		FullMsgId returnToId) {
	return goToMessageClickHandler(
		item->history()->peer,
		item->id,
		returnToId);
}

ClickHandlerPtr goToMessageClickHandler(
		not_null<PeerData*> peer,
		MsgId msgId,
		FullMsgId returnToId) {
	return std::make_shared<LambdaClickHandler>([=] {
		if (const auto main = App::main()) { // multi good
			if (&main->session() == &peer->session()) {
				auto params = Window::SectionShow{
					Window::SectionShow::Way::Forward
				};
				params.origin = Window::SectionShow::OriginMessage{
					returnToId
				};
				main->controller()->showPeerHistory(peer, params, msgId);
			}
		}
	});
}

not_null<HistoryItem*> HistoryItem::Create(
		not_null<History*> history,
		const MTPMessage &message,
		MTPDmessage_ClientFlags clientFlags) {
	return message.match([&](const MTPDmessage &data) -> HistoryItem* {
		const auto media = data.vmedia();
		const auto checked = media
			? CheckMessageMedia(*media)
			: MediaCheckResult::Good;
		if (checked == MediaCheckResult::Unsupported) {
			return CreateUnsupportedMessage(
				history,
				data.vid().v,
				data.vflags().v,
				clientFlags,
				MsgId(0), // No need to pass reply_to data here.
				data.vvia_bot_id().value_or_empty(),
				data.vdate().v,
				data.vfrom_id() ? peerFromMTP(*data.vfrom_id()) : PeerId(0));
		} else if (checked == MediaCheckResult::Empty) {
			const auto text = HistoryService::PreparedText{
				tr::lng_message_empty(tr::now)
			};
			return history->makeServiceMessage(
				data.vid().v,
				clientFlags,
				data.vdate().v,
				text,
				data.vflags().v,
				data.vfrom_id() ? peerFromMTP(*data.vfrom_id()) : PeerId(0));
		} else if (checked == MediaCheckResult::HasTimeToLive) {
			return history->makeServiceMessage(data, clientFlags);
		}
		return history->makeMessage(data, clientFlags);
	}, [&](const MTPDmessageService &data) -> HistoryItem* {
		if (data.vaction().type() == mtpc_messageActionPhoneCall) {
			return history->makeMessage(data, clientFlags);
		}
		return history->makeServiceMessage(data, clientFlags);
	}, [&](const MTPDmessageEmpty &data) -> HistoryItem* {
		const auto text = HistoryService::PreparedText{
			tr::lng_message_empty(tr::now)
		};
		return history->makeServiceMessage(
			data.vid().v,
			clientFlags,
			TimeId(0),
			text);
	});
}
