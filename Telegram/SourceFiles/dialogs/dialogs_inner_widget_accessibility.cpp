/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_inner_widget_accessibility.h"

#include "data/notify/data_notify_settings.h"
#include "data/data_channel.h"
#include "data/data_chat_filters.h"
#include "data/data_drafts.h"
#include "data/data_folder.h"
#include "data/data_forum_topic.h"
#include "data/data_message_reactions.h"
#include "data/data_peer_values.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_user.h"
#include "dialogs/dialogs_entry.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/text/format_values.h"

namespace Dialogs {
namespace {

[[nodiscard]] QString ChatTypeString(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		if (user->isInaccessible()) {
			return tr::lng_deleted(tr::now);
		} else if (user->isSelf()) {
			return tr::lng_saved_messages(tr::now);
		} else if (user->isServiceUser()) {
			return tr::lng_sr_chat_service(tr::now);
		} else if (user->isBot()) {
			return tr::lng_sr_chat_bot(tr::now);
		}
	} else if (peer->isBroadcast()) {
		return tr::lng_sr_chat_channel(tr::now);
	} else if (peer->isMegagroup() || peer->isChat()) {
		return tr::lng_sr_chat_group(tr::now);
	}
	return QString();
}

[[nodiscard]] QStringList MessageTailParts(
		not_null<const HistoryItem*> item,
		not_null<History*> history,
		not_null<PeerData*> peer) {
	QStringList parts;
	if (item->isService()) {
		return parts;
	}
	const auto dateTime = ItemDateTime(item);

	QString status;
	auto statusBeforeMessage = false;
	if (item->out()) {
		if (item->isSending()) {
			status = tr::lng_sr_chat_sending(tr::now);
			statusBeforeMessage = true;
		} else if (item->hasFailed()) {
			status = tr::lng_sr_chat_failed(tr::now);
			statusBeforeMessage = true;
		} else if (item->unread(history)) {
			status = tr::lng_sr_message_not_seen(tr::now);
			statusBeforeMessage = true;
		} else {
			status = tr::lng_sr_message_seen(tr::now);
			statusBeforeMessage = true;
		}
	} else {
		status = tr::lng_sr_chat_received(tr::now);
		statusBeforeMessage = false;
	}

	auto messageText = item->notificationText().text;
	if (!messageText.isEmpty()) {
		constexpr auto kMaxMessageLength = 100;
		if (messageText.size() > kMaxMessageLength) {
			messageText = messageText.left(kMaxMessageLength) + u"..."_q;
		}
		if (peer->isChat() || peer->isMegagroup()) {
			if (const auto from = item->from()) {
				if (!item->out()) {
					messageText = from->shortName()
						+ u": "_q
						+ messageText;
				}
			}
		}
		if (statusBeforeMessage) {
			messageText = status + u", "_q + messageText;
		}
		parts << messageText;
	}

	const auto &reactions = item->reactions();
	if (!reactions.empty()) {
		QStringList reactionParts;
		for (const auto &reaction : reactions) {
			QString reactionText;
			if (reaction.id.paid()) {
				reactionText = tr::lng_sr_chat_reaction_star(tr::now);
			} else if (const auto emoji = reaction.id.emoji(); !emoji.isEmpty()) {
				reactionText = emoji;
			} else {
				reactionText = tr::lng_sr_chat_reaction_custom(tr::now);
			}
			if (reaction.count > 1) {
				reactionText += u" "_q + QString::number(reaction.count);
			}
			reactionParts << reactionText;
		}
		if (!reactionParts.isEmpty()) {
			parts << tr::lng_sr_chat_message_reactions(
				tr::now,
				lt_reactions,
				reactionParts.join(u", "_q));
		}
	}

	if (!statusBeforeMessage) {
		parts << status;
	}

	if (dateTime.isValid()) {
		const auto now = QDateTime::currentDateTime();
		QString timeText;
		if (dateTime.date() == now.date()) {
			timeText = tr::lng_schedule_at(tr::now)
				+ u" "_q
				+ QLocale().toString(
					dateTime.time(),
					QLocale::ShortFormat);
		} else {
			timeText = Ui::FormatDateTime(dateTime);
		}
		parts << timeText;
	}

	return parts;
}

} // namespace

QString CollapsedRowAccessibilityName(not_null<Data::Folder*> folder) {
	return tr::lng_sr_chat_folder(tr::now)
		+ u" "_q
		+ folder->chatListName();
}

QString RowAccessibilityName(
		not_null<const Row*> row,
		FilterId filterId) {
	if (const auto topic = row->topic()) {
		const auto title = topic->isGeneral()
			? Data::ForumGeneralIconTitle()
			: topic->title();
		return tr::lng_sr_chat_topic(tr::now) + u" "_q + title;
	}

	if (const auto folder = row->folder()) {
		return tr::lng_sr_chat_folder(tr::now)
			+ u" "_q
			+ folder->chatListName();
	}

	if (const auto sublist = row->sublist()) {
		return tr::lng_saved_messages(tr::now)
			+ u" "_q
			+ sublist->sublistPeer()->name();
	}

	const auto history = row->history();
	if (!history) {
		return {};
	}

	const auto peer = history->peer;
	if (!peer) {
		return {};
	}

	QStringList parts;

	const auto type = ChatTypeString(peer);
	if (!type.isEmpty()) {
		parts << type;
	}

	parts << peer->name();

	if (peer->isScam()) {
		parts << tr::lng_sr_chat_scam(tr::now);
	} else if (peer->isFake()) {
		parts << tr::lng_sr_chat_fake(tr::now);
	}

	if (const auto user = peer->asUser()) {
		if (user->isPremium()) {
			parts << tr::lng_premium(tr::now);
		}
	}

	if (peer->isVerified()) {
		parts << tr::lng_sr_chat_verified(tr::now);
	}

	if (const auto user = peer->asUser()) {
		if (Data::IsUserOnline(user)) {
			parts << tr::lng_sr_chat_online(tr::now);
		}
	}

	if (peer->owner().notifySettings().isMuted(peer)) {
		parts << tr::lng_notification_exceptions_muted(tr::now);
	}

	if (row->entry()->isPinnedDialog(filterId)) {
		parts << tr::lng_sr_chat_pinned(tr::now);
	}

	if (const auto draft = history->cloudDraft(MsgId(0), PeerId(0))) {
		if (!draft->textWithTags.text.isEmpty()) {
			parts << tr::lng_from_draft(tr::now);
		}
	} else if (const auto localDraft = history->localDraft(MsgId(0), PeerId(0))) {
		if (!localDraft->textWithTags.text.isEmpty()) {
			parts << tr::lng_from_draft(tr::now);
		}
	}

	const auto badges = row->entry()->chatListBadgesState();
	if (badges.unreadCounter > 0) {
		parts << tr::lng_sr_chat_unread(
			tr::now,
			lt_count,
			badges.unreadCounter);
	} else if (badges.unread) {
		parts << tr::lng_settings_quick_dialog_action_unread(tr::now);
	}

	if (badges.mention) {
		parts << tr::lng_sr_chat_mention(tr::now);
	}

	if (const auto item = history->chatListMessage()) {
		parts += MessageTailParts(item, history, peer);
	}

	return parts.join(u", "_q);
}

QString SubItemLabel(SubItem item) {
	switch (item) {
	case SubItem::Type: return tr::lng_sr_chat_column_type(tr::now);
	case SubItem::Name: return tr::lng_sr_chat_column_name(tr::now);
	case SubItem::Warning: return tr::lng_sr_chat_column_warning(tr::now);
	case SubItem::Premium: return tr::lng_sr_chat_column_premium(tr::now);
	case SubItem::Verified: return tr::lng_sr_chat_column_verified(tr::now);
	case SubItem::Activity: return tr::lng_sr_chat_column_activity(tr::now);
	case SubItem::Muted: return tr::lng_sr_chat_column_muted(tr::now);
	case SubItem::Pinned: return tr::lng_sr_chat_column_pinned(tr::now);
	case SubItem::Draft: return tr::lng_sr_chat_column_draft(tr::now);
	case SubItem::Unread: return tr::lng_sr_chat_column_unread(tr::now);
	case SubItem::Mention: return tr::lng_sr_chat_column_mention(tr::now);
	case SubItem::Sender: return tr::lng_sr_chat_column_sender(tr::now);
	case SubItem::Message: return tr::lng_sr_chat_column_message(tr::now);
	case SubItem::Delivery: return tr::lng_sr_chat_column_delivery(tr::now);
	case SubItem::Reactions: return tr::lng_sr_chat_column_reactions(tr::now);
	case SubItem::Time: return tr::lng_sr_chat_column_time(tr::now);
	case SubItem::Sponsored: return tr::lng_sr_chat_column_sponsored(tr::now);
	case SubItem::Stories: return tr::lng_sr_chat_column_stories(tr::now);
	case SubItem::Autodelete: return tr::lng_sr_chat_column_autodelete(tr::now);
	case SubItem::Subscription: return tr::lng_sr_chat_column_subscription(tr::now);
	case SubItem::Closed: return tr::lng_sr_chat_column_closed(tr::now);
	case SubItem::Forward: return tr::lng_sr_chat_column_forward(tr::now);
	case SubItem::Folders: return tr::lng_sr_chat_column_folders(tr::now);
	case SubItem::Count: break;
	}
	return {};
}

QString SubItemValue(
		not_null<const Row*> row,
		FilterId filterId,
		SubItem item) {
	if (const auto topic = row->topic()) {
		switch (item) {
		case SubItem::Type:
			return tr::lng_sr_chat_topic(tr::now);
		case SubItem::Name:
			return topic->isGeneral()
				? Data::ForumGeneralIconTitle()
				: topic->title();
		case SubItem::Closed:
			if (topic->closed()) {
				return tr::lng_hours_closed(tr::now);
			}
			return {};
		default:
			return {};
		}
	}

	if (const auto folder = row->folder()) {
		switch (item) {
		case SubItem::Type:
			return tr::lng_sr_chat_folder(tr::now);
		case SubItem::Name:
			return folder->chatListName();
		default:
			return {};
		}
	}

	if (const auto sublist = row->sublist()) {
		switch (item) {
		case SubItem::Type:
			return tr::lng_saved_messages(tr::now);
		case SubItem::Name:
			return sublist->sublistPeer()->name();
		default:
			return {};
		}
	}

	const auto history = row->history();
	if (!history) {
		return {};
	}

	const auto peer = history->peer;
	if (!peer) {
		return {};
	}

	switch (item) {
	case SubItem::Type:
		return ChatTypeString(peer);
	case SubItem::Name:
		return peer->name();
	case SubItem::Warning:
		if (peer->isScam()) {
			return tr::lng_sr_chat_scam(tr::now);
		} else if (peer->isFake()) {
			return tr::lng_sr_chat_fake(tr::now);
		}
		return {};
	case SubItem::Premium:
		if (const auto user = peer->asUser()) {
			if (user->isPremium()) {
				return tr::lng_premium(tr::now);
			}
		}
		return {};
	case SubItem::Verified:
		if (peer->isVerified()) {
			return tr::lng_sr_chat_verified(tr::now);
		}
		return {};
	case SubItem::Activity:
		if (const auto user = peer->asUser()) {
			if (Data::IsUserOnline(user)) {
				return tr::lng_sr_chat_online(tr::now);
			}
		}
		return {};
	case SubItem::Muted:
		if (peer->owner().notifySettings().isMuted(peer)) {
			return tr::lng_notification_exceptions_muted(tr::now);
		}
		return {};
	case SubItem::Pinned:
		if (row->entry()->isPinnedDialog(filterId)) {
			return tr::lng_sr_chat_pinned(tr::now);
		}
		return {};
	case SubItem::Draft:
		if (const auto draft = history->cloudDraft(MsgId(0), PeerId(0))) {
			if (!draft->textWithTags.text.isEmpty()) {
				return tr::lng_from_draft(tr::now);
			}
		} else if (const auto localDraft = history->localDraft(MsgId(0), PeerId(0))) {
			if (!localDraft->textWithTags.text.isEmpty()) {
				return tr::lng_from_draft(tr::now);
			}
		}
		return {};
	case SubItem::Unread: {
		const auto badges = row->entry()->chatListBadgesState();
		if (badges.unreadCounter > 0) {
			return tr::lng_sr_chat_unread(
				tr::now,
				lt_count,
				badges.unreadCounter);
		} else if (badges.unread) {
			return tr::lng_settings_quick_dialog_action_unread(tr::now);
		}
		return {};
	}
	case SubItem::Mention: {
		const auto badges = row->entry()->chatListBadgesState();
		if (badges.mention) {
			return tr::lng_sr_chat_mention(tr::now);
		}
		return {};
	}
	case SubItem::Sender: {
		const auto chatItem = history->chatListMessage();
		if (!chatItem || chatItem->isService() || chatItem->out()) {
			return {};
		}
		if (peer->isChat() || peer->isMegagroup()) {
			if (const auto from = chatItem->from()) {
				return from->shortName();
			}
		}
		return {};
	}
	case SubItem::Message: {
		const auto chatItem = history->chatListMessage();
		if (!chatItem || chatItem->isService()) {
			return {};
		}
		auto messageText = chatItem->notificationText().text;
		if (messageText.isEmpty()) {
			return {};
		}
		constexpr auto kMaxMessageLength = 100;
		if (messageText.size() > kMaxMessageLength) {
			messageText = messageText.left(kMaxMessageLength)
				+ u"..."_q;
		}
		return messageText;
	}
	case SubItem::Delivery: {
		const auto chatItem = history->chatListMessage();
		if (!chatItem || chatItem->isService()) {
			return {};
		}
		if (chatItem->out()) {
			if (chatItem->isSending()) {
				return tr::lng_sr_chat_sending(tr::now);
			} else if (chatItem->hasFailed()) {
				return tr::lng_sr_chat_failed(tr::now);
			} else if (chatItem->unread(history)) {
				return tr::lng_sr_message_not_seen(tr::now);
			} else {
				return tr::lng_sr_message_seen(tr::now);
			}
		}
		return tr::lng_sr_chat_received(tr::now);
	}
	case SubItem::Reactions: {
		const auto chatItem = history->chatListMessage();
		if (!chatItem || chatItem->isService()) {
			return {};
		}
		const auto &reactions = chatItem->reactions();
		if (reactions.empty()) {
			return {};
		}
		QStringList reactionParts;
		for (const auto &reaction : reactions) {
			QString reactionText;
			if (reaction.id.paid()) {
				reactionText = tr::lng_sr_chat_reaction_star(tr::now);
			} else if (const auto emoji = reaction.id.emoji(); !emoji.isEmpty()) {
				reactionText = emoji;
			} else {
				reactionText = tr::lng_sr_chat_reaction_custom(tr::now);
			}
			if (reaction.count > 1) {
				reactionText += u" "_q + QString::number(reaction.count);
			}
			reactionParts << reactionText;
		}
		return reactionParts.join(u", "_q);
	}
	case SubItem::Time: {
		const auto chatItem = history->chatListMessage();
		if (!chatItem || chatItem->isService()) {
			return {};
		}
		const auto dateTime = ItemDateTime(chatItem);
		if (!dateTime.isValid()) {
			return {};
		}
		const auto now = QDateTime::currentDateTime();
		if (dateTime.date() == now.date()) {
			return QLocale().toString(
				dateTime.time(),
				QLocale::ShortFormat);
		}
		return Ui::FormatDateTime(dateTime);
	}
	case SubItem::Sponsored: {
		if (history->useTopPromotion()) {
			const auto type = history->topPromotionType();
			if (type.isEmpty()) {
				return tr::lng_sr_chat_sponsored(tr::now);
			}
			const auto custom = Lang::GetNonDefaultValue(
				"cloud_lng_badge_psa_" + type.toUtf8());
			return custom.isEmpty()
				? tr::lng_badge_psa_default(tr::now)
				: custom;
		}
		return {};
	}
	case SubItem::Stories: {
		if (peer->hasActiveStories()) {
			if (peer->hasUnreadStories()) {
				const auto source = peer->owner().stories().source(
					peer->id);
				const auto count = source
					? int(source->unreadCount())
					: 1;
				return tr::lng_sr_chat_stories_unread(
					tr::now,
					lt_count,
					count);
			}
			return tr::lng_sr_chat_stories_read(tr::now);
		}
		return {};
	}
	case SubItem::Autodelete:
		if (peer->messagesTTL()) {
			return tr::lng_sr_chat_autodelete(tr::now);
		}
		return {};
	case SubItem::Subscription:
		if (Data::ChannelHasSubscriptionUntilDate(peer->asChannel())) {
			return tr::lng_sr_chat_subscribed(tr::now);
		}
		return {};
	case SubItem::Closed:
		return {};
	case SubItem::Forward: {
		const auto chatItem = history->chatListMessage();
		if (!chatItem || chatItem->isService()) {
			return {};
		}
		if (chatItem->Get<HistoryMessageForwarded>()) {
			return tr::lng_sr_chat_forwarded(tr::now);
		}
		if (chatItem->replyToStory().valid()) {
			return tr::lng_sr_chat_story_reply(tr::now);
		}
		return {};
	}
	case SubItem::Folders: {
		const auto entry = row->entry();
		if (!entry->hasChatsFilterTags(filterId)) {
			return {};
		}
		QStringList tags;
		const auto &list = row->history()->session().data().chatsFilters().list();
		for (const auto &filter : list) {
			if (!entry->inChatList(filter.id())
				|| (filter.id() == filterId)) {
				continue;
			}
			tags << filter.title().text.text;
		}
		return tags.join(u", "_q);
	}
	case SubItem::Count: break;
	}
	return {};
}

std::vector<SubItem> ActiveSubItems(
		not_null<const Row*> row,
		FilterId filterId) {
	auto result = std::vector<SubItem>();
	result.reserve(int(SubItem::Count));
	for (auto i = 0; i != int(SubItem::Count); ++i) {
		const auto item = SubItem(i);
		if (!SubItemValue(row, filterId, item).isEmpty()) {
			result.push_back(item);
		}
	}
	return result;
}

QString HashtagAccessibilityName(QStringView tag) {
	return tr::lng_sr_chat_hashtag(tr::now)
		+ u": #"_q
		+ tag.toString();
}

QString PeerSearchResultAccessibilityName(
		not_null<PeerData*> peer,
		bool sponsored) {
	QStringList parts;

	const auto type = ChatTypeString(peer);
	if (!type.isEmpty()) {
		parts << type;
	}

	parts << peer->name();

	if (peer->isScam()) {
		parts << tr::lng_sr_chat_scam(tr::now);
	} else if (peer->isFake()) {
		parts << tr::lng_sr_chat_fake(tr::now);
	}

	if (const auto user = peer->asUser()) {
		if (user->isPremium()) {
			parts << tr::lng_premium(tr::now);
		}
	}

	if (peer->isVerified()) {
		parts << tr::lng_sr_chat_verified(tr::now);
	}

	if (const auto user = peer->asUser()) {
		if (Data::IsUserOnline(user)) {
			parts << tr::lng_sr_chat_online(tr::now);
		}
	}

	if (sponsored) {
		parts << tr::lng_sr_chat_sponsored(tr::now);
	}

	return parts.join(u", "_q);
}

QString SearchedMessageAccessibilityName(
		not_null<const FakeRow*> row) {
	const auto item = row->item();
	const auto history = item->history();
	const auto peer = history->peer;

	QStringList parts;
	const auto type = ChatTypeString(peer);
	if (!type.isEmpty()) {
		parts << type;
	}
	parts << peer->name();
	if (const auto topic = row->topic()) {
		parts << (topic->isGeneral()
			? Data::ForumGeneralIconTitle()
			: topic->title());
	}
	parts += MessageTailParts(item, history, peer);
	return parts.join(u", "_q);
}

} // namespace Dialogs
