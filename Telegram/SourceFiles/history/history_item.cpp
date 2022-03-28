/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_item.h"

#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_item_preview.h"
#include "history/view/history_view_service_message.h"
#include "history/view/media/history_view_media_grouped.h"
#include "history/history_item_components.h"
#include "history/history_service.h"
#include "history/history_message.h"
#include "history/history_unread_things.h"
#include "history/history.h"
#include "mtproto/mtproto_config.h"
#include "media/clip/media_clip_reader.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/text_isolated_emoji.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "storage/file_upload.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "media/audio/media_audio.h"
#include "core/application.h"
#include "mainwindow.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "core/crash_reports.h"
#include "base/unixtime.h"
#include "api/api_text_entities.h"
#include "dialogs/ui/dialogs_message_view.h"
#include "data/data_scheduled_messages.h" // kScheduledUntilOnlineTimestamp
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_message_reactions.h"
#include "data/data_messages.h"
#include "data/data_media_types.h"
#include "data/data_folder.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_sponsored_messages.h"
#include "styles/style_dialogs.h"
#include "styles/style_chat.h"

namespace {

constexpr auto kNotificationTextLimit = 255;
constexpr auto kMaxUnreadReactions = 5; // Now 3, but just in case.

using ItemPreview = HistoryView::ItemPreview;

enum class MediaCheckResult {
	Good,
	Unsupported,
	Empty,
	HasTimeToLive,
};

not_null<HistoryItem*> CreateUnsupportedMessage(
		not_null<History*> history,
		MsgId msgId,
		MessageFlags flags,
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
	flags &= ~MessageFlag::HasPostAuthor;
	flags |= MessageFlag::Legacy;
	const auto groupedId = uint64();
	return history->makeMessage(
		msgId,
		flags,
		replyTo,
		viaBotId,
		date,
		from,
		QString(),
		text,
		MTP_messageMediaEmpty(),
		HistoryMessageMarkupData(),
		groupedId);
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

[[nodiscard]] MessageFlags FinalizeMessageFlags(MessageFlags flags) {
	if (!(flags & MessageFlag::FakeHistoryItem)
		&& !(flags & MessageFlag::IsOrWasScheduled)
		&& !(flags & MessageFlag::AdminLogEntry)) {
		flags |= MessageFlag::HistoryEntry;
	}
	return flags;
}

using OnStackUsers = std::array<UserData*, kMaxUnreadReactions>;
[[nodiscard]] OnStackUsers LookupRecentUnreadReactedUsers(
		not_null<HistoryItem*> item) {
	auto result = OnStackUsers();
	auto index = 0;
	for (const auto &[emoji, reactions] : item->recentReactions()) {
		for (const auto &reaction : reactions) {
			if (!reaction.unread) {
				continue;
			}
			if (const auto user = reaction.peer->asUser()) {
				result[index++] = user;
				if (index == result.size()) {
					return result;
				}
			}
		}
	}
	return result;
}

void CheckReactionNotificationSchedule(
		not_null<HistoryItem*> item,
		const OnStackUsers &wasUsers) {
	// Call to addToUnreadThings may have read the reaction already.
	if (!item->hasUnreadReaction()) {
		return;
	}
	for (const auto &[emoji, reactions] : item->recentReactions()) {
		for (const auto &reaction : reactions) {
			if (!reaction.unread) {
				continue;
			}
			const auto user = reaction.peer->asUser();
			if (!user
				|| !user->isContact()
				|| ranges::contains(wasUsers, user)) {
				continue;
			}
			using Status = PeerData::BlockStatus;
			if (user->blockStatus() == Status::Unknown) {
				user->updateFull();
			}
			const auto notification = ItemNotification{
				.item = item,
				.reactionSender = user,
				.type = ItemNotificationType::Reaction,
			};
			item->history()->pushNotification(notification);
			Core::App().notifications().schedule(notification);
			return;
		}
	}
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
	MessageFlags flags,
	TimeId date,
	PeerId from)
: id(id)
, _history(history)
, _from(from ? history->owner().peer(from) : history->peer)
, _flags(FinalizeMessageFlags(flags))
, _date(date) {
	if (isHistoryEntry() && IsClientMsgId(id)) {
		_history->registerClientSideMessage(this);
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
	if (const auto group = _history->owner().groups().find(this)) {
		for (const auto &item : group->items) {
			_history->owner().requestItemViewRefresh(item);
			item->invalidateChatListEntry();
		}
	} else {
		_history->owner().requestItemViewRefresh(this);
		invalidateChatListEntry();
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
		if (markup->data.flags & ReplyMarkupFlag::Inline) {
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
		forwarded->savedFromPeer->id,
		forwarded->savedFromMsgId);
}

PeerData *HistoryItem::displayFrom() const {
	if (const auto sender = discussionPostOriginalSender()) {
		return sender;
	} else if (const auto sponsored = Get<HistoryMessageSponsored>()) {
		if (sponsored->sender) {
			return nullptr;
		}
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
	history()->lastItemDialogsView.itemInvalidated(this);
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
	return _flags & MessageFlag::MediaIsUnread;
}

bool HistoryItem::isUnreadMention() const {
	return !out() && mentionsMe() && (_flags & MessageFlag::MediaIsUnread);
}

bool HistoryItem::hasUnreadReaction() const {
	return (_flags & MessageFlag::HasUnreadReaction);
}

bool HistoryItem::mentionsMe() const {
	if (Has<HistoryServicePinned>()
		&& !Core::App().settings().notifyAboutPinned()) {
		return false;
	}
	return _flags & MessageFlag::MentionsMe;
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

bool HistoryItem::isIncomingUnreadMedia() const {
	return !out() && isUnreadMedia();
}

void HistoryItem::markMediaAndMentionRead() {
	_flags &= ~MessageFlag::MediaIsUnread;

	if (mentionsMe()) {
		history()->updateChatListEntry();
		history()->unreadMentions().erase(id);
	}
}

void HistoryItem::markReactionsRead() {
	if (_reactions) {
		_reactions->markRead();
	}
	_flags &= ~MessageFlag::HasUnreadReaction;
	history()->updateChatListEntry();
	history()->unreadReactions().erase(id);
}

bool HistoryItem::markContentsRead(bool fromThisClient) {
	if (hasUnreadReaction()) {
		if (fromThisClient) {
			history()->owner().requestUnreadReactionsAnimation(this);
		}
		markReactionsRead();
		return true;
	} else if (isUnreadMention() || isIncomingUnreadMedia()) {
		markMediaAndMentionRead();
		return true;
	}
	return false;
}

void HistoryItem::setIsPinned(bool pinned) {
	const auto changed = (isPinned() != pinned);
	if (pinned) {
		_flags |= MessageFlag::Pinned;
		history()->session().storage().add(Storage::SharedMediaAddExisting(
			history()->peer->id,
			Storage::SharedMediaType::Pinned,
			id,
			{ id, id }));
		history()->setHasPinnedMessages(true);
	} else {
		_flags &= ~MessageFlag::Pinned;
		history()->session().storage().remove(Storage::SharedMediaRemoveOne(
			history()->peer->id,
			Storage::SharedMediaType::Pinned,
			id));
	}
	if (changed) {
		history()->owner().notifyItemDataChange(this);
	}
}

void HistoryItem::returnSavedMedia() {
}

void HistoryItem::savePreviousMedia() {
	Expects(_media != nullptr);

	using Data = SavedMediaData;
	_savedLocalEditMediaData = std::make_unique<Data>(Data{
		.text = originalText(),
		.media = _media->clone(this),
	});
}

bool HistoryItem::isEditingMedia() const {
	return _savedLocalEditMediaData != nullptr;
}

void HistoryItem::clearSavedMedia() {
	_savedLocalEditMediaData = nullptr;
}

bool HistoryItem::definesReplyKeyboard() const {
	if (const auto markup = Get<HistoryMessageReplyMarkup>()) {
		if (markup->data.flags & ReplyMarkupFlag::Inline) {
			return false;
		}
		return true;
	}

	// optimization: don't create markup component for the case
	// MTPDreplyKeyboardHide with flags = 0, assume it has f_zero flag
	return (_flags & MessageFlag::HasReplyMarkup);
}

ReplyMarkupFlags HistoryItem::replyKeyboardFlags() const {
	Expects(definesReplyKeyboard());

	if (const auto markup = Get<HistoryMessageReplyMarkup>()) {
		return markup->data.flags;
	}

	// optimization: don't create markup component for the case
	// MTPDreplyKeyboardHide with flags = 0, assume it has f_zero flag
	return ReplyMarkupFlag::None;
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
	return (_flags & MessageFlag::HistoryEntry);
}

bool HistoryItem::isAdminLogEntry() const {
	return (_flags & MessageFlag::AdminLogEntry);
}

bool HistoryItem::isFromScheduled() const {
	return isHistoryEntry()
		&& (_flags & MessageFlag::IsOrWasScheduled);
}

bool HistoryItem::isScheduled() const {
	return !isHistoryEntry()
		&& !isAdminLogEntry()
		&& (_flags & MessageFlag::IsOrWasScheduled);
}

bool HistoryItem::isSponsored() const {
	return Has<HistoryMessageSponsored>();
}

bool HistoryItem::skipNotification() const {
	if (isSilent() && (_flags & MessageFlag::IsContactSignUp)) {
		return true;
	} else if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		if (forwarded->imported) {
			return true;
		}
	}
	return false;
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

void HistoryItem::addToUnreadThings(HistoryUnreadThings::AddType type) {
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
	updateReplyMarkup(HistoryMessageMarkupData(data.vreply_markup()));
	updateForwardedInfo(data.vfwd_from());
	changeViewsCount(data.vviews().value_or(-1));
	if (const auto replies = data.vreplies()) {
		setReplies(HistoryMessageRepliesData(replies));
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
	history()->owner().notifyItemDataChange(this);
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
	if (isRegular()) {
		addToUnreadThings(HistoryUnreadThings::AddType::New);
		if (const auto types = sharedMediaTypes()) {
			_history->session().storage().add(Storage::SharedMediaAddNew(
				_history->peer->id,
				types,
				id));
			if (types.test(Storage::SharedMediaType::Pinned)) {
				_history->setHasPinnedMessages(true);
			}
		}
	}
}

void HistoryItem::setRealId(MsgId newId) {
	Expects(_flags & MessageFlag::BeingSent);
	Expects(IsClientMsgId(id));

	const auto oldId = std::exchange(id, newId);
	_flags &= ~(MessageFlag::BeingSent | MessageFlag::Local);
	if (isRegular()) {
		_history->unregisterClientSideMessage(this);
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

	_history->owner().notifyItemDataChange(this);
	_history->owner().requestItemRepaint(this);
}

bool HistoryItem::canPin() const {
	if (!isRegular() || isService()) {
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

bool HistoryItem::canBeEdited() const {
	if ((!isRegular() && !isScheduled())
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

bool HistoryItem::canStopPoll() const {
	return canBeEdited() && isRegular();
}

bool HistoryItem::forbidsForward() const {
	return (_flags & MessageFlag::NoForwards);
}

bool HistoryItem::canDelete() const {
	if (isSponsored()) {
		return false;
	} else if (isService() && !isRegular()) {
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
	if (out() && !isService()) {
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
	if (!isRegular() || messageToMyself || messageTooOld || isPost()) {
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
	if (out() || isService() || !isRegular()) {
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
	if (!channel || !channel->canRestrictParticipant(from())) {
		return false;
	}
	return !isPost() && !out();
}

bool HistoryItem::suggestDeleteAllReport() const {
	auto channel = history()->peer->asChannel();
	if (!channel || !channel->canDeleteMessages()) {
		return false;
	}
	return !isPost() && !out();
}

bool HistoryItem::canReact() const {
	if (!isRegular() || isService()) {
		return false;
	} else if (const auto media = this->media()) {
		if (media->call()) {
			return false;
		}
	}
	return true;
}

void HistoryItem::addReaction(const QString &reaction) {
	if (!_reactions) {
		_reactions = std::make_unique<Data::MessageReactions>(this);
	}
	_reactions->add(reaction);
	history()->owner().notifyItemDataChange(this);
}

void HistoryItem::toggleReaction(const QString &reaction) {
	if (!_reactions) {
		_reactions = std::make_unique<Data::MessageReactions>(this);
		const auto canViewReactions = !isDiscussionPost()
			&& (history()->peer->isChat() || history()->peer->isMegagroup());
		if (canViewReactions) {
			_flags |= MessageFlag::CanViewReactions;
		}
		_reactions->add(reaction);
	} else if (_reactions->chosen() == reaction) {
		_reactions->remove();
		if (_reactions->empty()) {
			_reactions = nullptr;
			_flags &= ~MessageFlag::CanViewReactions;
			history()->owner().notifyItemDataChange(this);
		}
	} else {
		_reactions->add(reaction);
	}
	history()->owner().notifyItemDataChange(this);
}

void HistoryItem::setReactions(const MTPMessageReactions *reactions) {
	Expects(!_reactions);

	if (changeReactions(reactions) && _reactions->hasUnread()) {
		_flags |= MessageFlag::HasUnreadReaction;
	}
}

void HistoryItem::updateReactions(const MTPMessageReactions *reactions) {
	const auto wasRecentUsers = LookupRecentUnreadReactedUsers(this);
	const auto hadUnread = hasUnreadReaction();
	const auto changed = changeReactions(reactions);
	if (!changed) {
		return;
	}
	const auto hasUnread = _reactions && _reactions->hasUnread();
	if (hasUnread && !hadUnread) {
		_flags |= MessageFlag::HasUnreadReaction;

		addToUnreadThings(HistoryUnreadThings::AddType::New);
	} else if (!hasUnread && hadUnread) {
		markReactionsRead();
	}
	CheckReactionNotificationSchedule(this, wasRecentUsers);
	history()->owner().notifyItemDataChange(this);
}

bool HistoryItem::changeReactions(const MTPMessageReactions *reactions) {
	if (reactions || _reactionsLastRefreshed) {
		_reactionsLastRefreshed = crl::now();
	}
	if (!reactions) {
		_flags &= ~MessageFlag::CanViewReactions;
		return (base::take(_reactions) != nullptr);
	}
	return reactions->match([&](const MTPDmessageReactions &data) {
		if (data.is_can_see_list()) {
			_flags |= MessageFlag::CanViewReactions;
		} else {
			_flags &= ~MessageFlag::CanViewReactions;
		}
		if (data.vresults().v.isEmpty()) {
			return (base::take(_reactions) != nullptr);
		} else if (!_reactions) {
			_reactions = std::make_unique<Data::MessageReactions>(this);
		}
		const auto min = data.is_min();
		const auto &list = data.vresults().v;
		const auto &recent = data.vrecent_reactions().value_or_empty();
		if (min && hasUnreadReaction()) {
			// We can't update reactions from min if we have unread.
			if (_reactions->checkIfChanged(list, recent)) {
				updateReactionsUnknown();
			}
			return false;
		}
		return _reactions->change(list, recent, min);
	});
}

void HistoryItem::updateReactionsUnknown() {
	_reactionsLastRefreshed = 1;
}

const base::flat_map<QString, int> &HistoryItem::reactions() const {
	static const auto kEmpty = base::flat_map<QString, int>();
	return _reactions ? _reactions->list() : kEmpty;
}

auto HistoryItem::recentReactions() const
-> const base::flat_map<QString, std::vector<Data::RecentReaction>> & {
	static const auto kEmpty = base::flat_map<
		QString,
		std::vector<Data::RecentReaction>>();
	return _reactions ? _reactions->recent() : kEmpty;
}

bool HistoryItem::canViewReactions() const {
	return (_flags & MessageFlag::CanViewReactions)
		&& _reactions
		&& !_reactions->list().empty();
}

QString HistoryItem::chosenReaction() const {
	return _reactions ? _reactions->chosen() : QString();
}

QString HistoryItem::lookupUnreadReaction(not_null<UserData*> from) const {
	if (!_reactions) {
		return QString();
	}
	const auto recent = _reactions->recent();
	for (const auto &[emoji, list] : _reactions->recent()) {
		const auto i = ranges::find(
			list,
			from,
			&Data::RecentReaction::peer);
		if (i != end(list) && i->unread) {
			return emoji;
		}
	}
	return QString();
}

crl::time HistoryItem::lastReactionsRefreshTime() const {
	return _reactionsLastRefreshed;
}

bool HistoryItem::hasDirectLink() const {
	return isRegular() && _history->peer->isChannel();
}

FullMsgId HistoryItem::fullId() const {
	return FullMsgId(_history->peer->id, id);
}

GlobalMsgId HistoryItem::globalId() const {
	return { fullId(), _history->session().uniqueId() };
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
	return (isPost() && !isSponsored()) ? history()->peer : from();
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

const HiddenSenderInfo *HistoryItem::hiddenSenderInfo() const {
	if (const auto sponsored = Get<HistoryMessageSponsored>()) {
		return sponsored->sender.get();
	} else if (const auto forwarded = Get<HistoryMessageForwarded>()) {
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

bool HistoryItem::isUploading() const {
	return _media && _media->uploading();
}

bool HistoryItem::isRegular() const {
	return isHistoryEntry() && !isLocal();
}

void HistoryItem::sendFailed() {
	Expects(_flags & MessageFlag::BeingSent);
	Expects(!(_flags & MessageFlag::SendingFailed));

	_flags = (_flags | MessageFlag::SendingFailed) & ~MessageFlag::BeingSent;
	_history->owner().notifyItemDataChange(this);
	history()->session().changes().historyUpdated(
		history(),
		Data::HistoryUpdate::Flag::ClientSideMessages);
}

bool HistoryItem::needCheck() const {
	return (out() && !isEmpty()) || (!isRegular() && history()->peer->isSelf());
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

		if (isRegular()) {
			if (!history()->isServerSideUnread(this)) {
				return false;
			}
			if (const auto user = history()->peer->asUser()) {
				if (user->isBot() && !user->isSupport()) {
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

	if (isRegular()) {
		if (!history()->isServerSideUnread(this)) {
			return false;
		}
		return true;
	}
	return (_flags & MessageFlag::ClientSideUnread);
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
	_flags &= ~MessageFlag::ClientSideUnread;
}

MessageGroupId HistoryItem::groupId() const {
	return _groupId;
}

bool HistoryItem::isEmpty() const {
	return _text.isEmpty()
		&& !_media
		&& !Has<HistoryMessageLogEntryOriginal>();
}

TextWithEntities HistoryItem::notificationText() const {
	const auto result = [&] {
		if (_media && !isService()) {
			return _media->notificationText();
		} else if (!emptyText()) {
			return _text.toTextWithEntities();
		}
		return TextWithEntities();
	}();
	if (result.text.size() <= kNotificationTextLimit) {
		return result;
	}
	return Ui::Text::Mid(result, 0, kNotificationTextLimit).append(
		Ui::kQEllipsis);
}

ItemPreview HistoryItem::toPreview(ToPreviewOptions options) const {
	auto result = [&]() -> ItemPreview {
		if (_media) {
			return _media->toPreview(options);
		} else if (!emptyText()) {
			return {
				.text = _text.toTextWithEntities()
			};
		}
		return {};
	}();
	const auto fromSender = [](not_null<PeerData*> sender) {
		return sender->isSelf()
			? tr::lng_from_you(tr::now)
			: sender->shortName();
	};
	const auto fromForwarded = [&]() -> std::optional<QString> {
		if (const auto forwarded = Get<HistoryMessageForwarded>()) {
			return forwarded->originalSender
				? fromSender(forwarded->originalSender)
				: forwarded->hiddenSenderInfo->name;
		}
		return {};
	};
	const auto sender = [&]() -> std::optional<QString> {
		if (options.hideSender || isPost() || isEmpty()) {
			return {};
		} else if (const auto sponsored = Get<HistoryMessageSponsored>()) {
			return sponsored->sender->name;
		} else if (!_history->peer->isUser()) {
			if (const auto from = displayFrom()) {
				return fromSender(from);
			}
			return fromForwarded();
		} else if (_history->peer->isSelf()) {
			return fromForwarded();
		}
		return {};
	}();
	if (!sender) {
		return result;
	}
	const auto fromWrapped = Ui::Text::PlainLink(
		tr::lng_dialogs_text_from_wrapped(tr::now, lt_from, *sender));
	return Dialogs::Ui::PreviewWithSender(std::move(result), fromWrapped);
}

TextWithEntities HistoryItem::inReplyText() const {
	return toPreview({
		.hideSender = true,
		.generateImages = false,
	}).text;
}

Ui::Text::IsolatedEmoji HistoryItem::isolatedEmoji() const {
	return Ui::Text::IsolatedEmoji();
}

HistoryItem::~HistoryItem() {
	applyTTL(0);
}

Main::Session *SessionByUniqueId(uint64 sessionUniqueId) {
	if (!sessionUniqueId) {
		return nullptr;
	}
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		if (const auto session = account->maybeSession()) {
			if (session->uniqueId() == sessionUniqueId) {
				return session;
			}
		}
	}
	return nullptr;
}

HistoryItem *MessageByGlobalId(GlobalMsgId globalId) {
	const auto sessionId = globalId.itemId ? globalId.sessionUniqueId : 0;
	if (const auto session = SessionByUniqueId(sessionId)) {
		return session->data().message(globalId.itemId);
	}
	return nullptr;
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
		const auto separate = Core::App().separateWindowForPeer(peer);
		const auto controller = separate
			? separate->sessionController()
			: peer->session().tryResolveWindow();
		if (controller) {
			auto params = Window::SectionShow{
				Window::SectionShow::Way::Forward
			};
			params.origin = Window::SectionShow::OriginMessage{
				returnToId
			};
			controller->showPeerHistory(peer, params, msgId);
		}
	});
}

MessageFlags FlagsFromMTP(
		MsgId id,
		MTPDmessage::Flags flags,
		MessageFlags localFlags) {
	using Flag = MessageFlag;
	using MTP = MTPDmessage::Flag;
	return localFlags
		| (IsServerMsgId(id) ? Flag::HistoryEntry : Flag())
		| ((flags & MTP::f_out) ? Flag::Outgoing : Flag())
		| ((flags & MTP::f_mentioned) ? Flag::MentionsMe : Flag())
		| ((flags & MTP::f_media_unread) ? Flag::MediaIsUnread : Flag())
		| ((flags & MTP::f_silent) ? Flag::Silent : Flag())
		| ((flags & MTP::f_post) ? Flag::Post : Flag())
		| ((flags & MTP::f_legacy) ? Flag::Legacy : Flag())
		| ((flags & MTP::f_edit_hide) ? Flag::HideEdited : Flag())
		| ((flags & MTP::f_pinned) ? Flag::Pinned : Flag())
		| ((flags & MTP::f_from_id) ? Flag::HasFromId : Flag())
		| ((flags & MTP::f_reply_to) ? Flag::HasReplyInfo : Flag())
		| ((flags & MTP::f_reply_markup) ? Flag::HasReplyMarkup : Flag())
		| ((flags & MTP::f_from_scheduled) ? Flag::IsOrWasScheduled : Flag())
		| ((flags & MTP::f_views) ? Flag::HasViews : Flag())
		| ((flags & MTP::f_noforwards) ? Flag::NoForwards : Flag());
}

MessageFlags FlagsFromMTP(
		MsgId id,
		MTPDmessageService::Flags flags,
		MessageFlags localFlags) {
	using Flag = MessageFlag;
	using MTP = MTPDmessageService::Flag;
	return localFlags
		| (IsServerMsgId(id) ? Flag::HistoryEntry : Flag())
		| ((flags & MTP::f_out) ? Flag::Outgoing : Flag())
		| ((flags & MTP::f_mentioned) ? Flag::MentionsMe : Flag())
		| ((flags & MTP::f_media_unread) ? Flag::MediaIsUnread : Flag())
		| ((flags & MTP::f_silent) ? Flag::Silent : Flag())
		| ((flags & MTP::f_post) ? Flag::Post : Flag())
		| ((flags & MTP::f_legacy) ? Flag::Legacy : Flag())
		| ((flags & MTP::f_from_id) ? Flag::HasFromId : Flag())
		| ((flags & MTP::f_reply_to) ? Flag::HasReplyInfo : Flag());
}

not_null<HistoryItem*> HistoryItem::Create(
		not_null<History*> history,
		MsgId id,
		const MTPMessage &message,
		MessageFlags localFlags) {
	return message.match([&](const MTPDmessage &data) -> HistoryItem* {
		const auto media = data.vmedia();
		const auto checked = media
			? CheckMessageMedia(*media)
			: MediaCheckResult::Good;
		if (checked == MediaCheckResult::Unsupported) {
			return CreateUnsupportedMessage(
				history,
				id,
				FlagsFromMTP(id, data.vflags().v, localFlags),
				MsgId(0), // No need to pass reply_to data here.
				data.vvia_bot_id().value_or_empty(),
				data.vdate().v,
				data.vfrom_id() ? peerFromMTP(*data.vfrom_id()) : PeerId(0));
		} else if (checked == MediaCheckResult::Empty) {
			const auto text = HistoryService::PreparedText{
				tr::lng_message_empty(tr::now, Ui::Text::WithEntities)
			};
			return history->makeServiceMessage(
				id,
				FlagsFromMTP(id, data.vflags().v, localFlags),
				data.vdate().v,
				text,
				data.vfrom_id() ? peerFromMTP(*data.vfrom_id()) : PeerId(0));
		} else if (checked == MediaCheckResult::HasTimeToLive) {
			return history->makeServiceMessage(id, data, localFlags);
		}
		return history->makeMessage(id, data, localFlags);
	}, [&](const MTPDmessageService &data) -> HistoryItem* {
		if (data.vaction().type() == mtpc_messageActionPhoneCall) {
			return history->makeMessage(id, data, localFlags);
		}
		return history->makeServiceMessage(id, data, localFlags);
	}, [&](const MTPDmessageEmpty &data) -> HistoryItem* {
		const auto text = HistoryService::PreparedText{
			tr::lng_message_empty(tr::now, Ui::Text::WithEntities)
		};
		return history->makeServiceMessage(id, localFlags, TimeId(0), text);
	});
}
