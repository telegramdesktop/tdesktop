/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_inner_widget_accessibility.h"

#include "base/unixtime.h"
#include "data/data_document.h"
#include "data/data_game.h"
#include "data/data_media_types.h"
#include "data/data_message_reaction_id.h"
#include "data/data_photo.h"
#include "data/data_poll.h"
#include "data/data_session.h"
#include "data/data_todo_list.h"
#include "data/data_user.h"
#include "data/data_web_page.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "lang/lang_keys.h"
#include "ui/text/format_values.h"

namespace HistoryView {

QString MessageAccessibilityName(
		not_null<const Element*> view,
		not_null<History*> history) {
	const auto item = view->data();

	if (item->isService()) {
		const auto text = item->notificationText().text;
		return text.isEmpty() ? QString() : text;
	}

	QStringList lines;

	if (item->out()) {
		lines.push_back(item->unread(history)
			? tr::lng_sr_message_not_seen(tr::now)
			: tr::lng_sr_message_seen(tr::now));
	}

	if (item->out()) {
		lines.push_back(tr::lng_sr_from_me(tr::now));
	} else if (const auto from = item->displayFrom()) {
		lines.push_back(from->name());
	}

	if (const auto bot = item->viaBot()) {
		lines.push_back(tr::lng_inline_bot_via(
			tr::now,
			lt_inline_bot,
			'@' + bot->username()));
	}

	if (const auto reply = item->Get<HistoryMessageReply>()) {
		if (const auto message = reply->resolvedMessage.get()) {
			const auto replyFrom = message->displayFrom();
			const auto replyName = replyFrom
				? replyFrom->name()
				: QString();
			const auto replyText = message->inReplyText().text;
			if (!replyName.isEmpty() || !replyText.isEmpty()) {
				lines.push_back(tr::lng_sr_message_reply_to(
					tr::now,
					lt_name,
					replyName.isEmpty()
						? tr::lng_sr_from_me(tr::now)
						: replyName,
					lt_text,
					replyText));
			}
		}
	}

	if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
		if (forwarded->originalSender) {
			lines.push_back(tr::lng_forwarded(
				tr::now,
				lt_user,
				forwarded->originalSender->name()));
		} else if (forwarded->originalHiddenSenderInfo) {
			lines.push_back(tr::lng_forwarded(
				tr::now,
				lt_user,
				forwarded->originalHiddenSenderInfo->name));
		}
	}

	if (const auto media = item->media()) {
		QStringList mediaParts;
		if (const auto document = media->document()) {
			if (document->isVoiceMessage()) {
				mediaParts.push_back(tr::lng_in_dlg_audio(tr::now));
			} else if (document->isVideoMessage()) {
				mediaParts.push_back(tr::lng_in_dlg_video_message(tr::now));
			} else if (document->isSong()) {
				mediaParts.push_back(tr::lng_in_dlg_audio_file(tr::now));
			} else if (document->isVideoFile()) {
				mediaParts.push_back(tr::lng_in_dlg_video(tr::now));
			} else if (document->isAnimation()) {
				mediaParts.push_back(u"GIF"_q);
			} else if (const auto sticker = document->sticker()) {
				mediaParts.push_back(tr::lng_in_dlg_sticker(tr::now));
				if (!sticker->alt.isEmpty()) {
					mediaParts.push_back(sticker->alt);
				}
			} else {
				mediaParts.push_back(tr::lng_in_dlg_file(tr::now));
			}
			if (document->loading()) {
				mediaParts.push_back(
					tr::lng_sr_message_downloading(tr::now));
			} else if (!document->filepath(true).isEmpty()
					|| document->loadedInMediaCache()) {
				mediaParts.push_back(
					tr::lng_emoji_set_ready(tr::now));
			} else {
				mediaParts.push_back(
					tr::lng_sr_message_not_downloaded(tr::now));
			}
			if (document->isVoiceMessage()
				|| document->isVideoMessage()) {
				mediaParts.push_back(item->isUnreadMedia()
					? tr::lng_sr_message_not_played(tr::now)
					: tr::lng_sr_message_played(tr::now));
			}
			if (const auto song = document->song()) {
				if (!song->performer.isEmpty()) {
					mediaParts.push_back(song->performer);
				}
				if (!song->title.isEmpty()) {
					mediaParts.push_back(song->title);
				}
			}
			if (!document->isSong()
				&& !document->isVoiceMessage()
				&& !document->isVideoMessage()
				&& !document->sticker()) {
				const auto name = document->filename();
				if (!name.isEmpty()) {
					mediaParts.push_back(name);
				}
			}
			const auto duration = document->duration();
			if (duration > 0) {
				mediaParts.push_back(
					Ui::FormatDurationText(duration / 1000));
			}
			if (!document->dimensions.isEmpty()
				&& (document->isVideoFile()
					|| document->isImage())) {
				mediaParts.push_back(
					Ui::FormatImageSizeText(document->dimensions));
			}
			if (document->size > 0) {
				mediaParts.push_back(
					Ui::FormatSizeText(document->size));
			}
		} else if (const auto photo = media->photo()) {
			mediaParts.push_back(tr::lng_in_dlg_photo(tr::now));
			if (media->hasSpoiler()) {
				mediaParts.push_back(
					tr::lng_sr_message_spoiler(tr::now));
			}
			const auto large = photo->size(
				Data::PhotoSize::Large);
			if (large && !large->isEmpty()) {
				mediaParts.push_back(
					Ui::FormatImageSizeText(*large));
			}
		} else if (const auto call = media->call()) {
			const auto notification = item->notificationText().text;
			if (!notification.isEmpty()) {
				mediaParts.push_back(notification);
			}
			if (call->duration > 0) {
				mediaParts.push_back(
					Ui::FormatDurationText(call->duration));
			}
		} else if (const auto poll = media->poll()) {
			mediaParts.push_back(poll->quiz()
				? tr::lng_polls_public_quiz(tr::now)
				: tr::lng_polls_public(tr::now));
			if (!poll->question.text.isEmpty()) {
				mediaParts.push_back(poll->question.text);
			}
			if (poll->closed()) {
				mediaParts.push_back(
					tr::lng_hours_closed(tr::now));
			}
			mediaParts.push_back(tr::lng_polls_votes_count(
				tr::now,
				lt_count,
				poll->totalVoters));
		} else if (const auto contact = media->sharedContact()) {
			auto contactName = contact->firstName;
			if (!contact->lastName.isEmpty()) {
				if (!contactName.isEmpty()) {
					contactName += u" "_q;
				}
				contactName += contact->lastName;
			}
			if (!contactName.isEmpty()) {
				mediaParts.push_back(contactName);
			}
			if (!contact->phoneNumber.isEmpty()) {
				mediaParts.push_back(contact->phoneNumber);
			}
		} else if (media->location()) {
			mediaParts.push_back(item->notificationText().text);
		} else if (const auto game = media->game()) {
			if (!game->title.isEmpty()) {
				mediaParts.push_back(game->title);
			}
		} else if (const auto invoice = media->invoice()) {
			if (!invoice->title.isEmpty()) {
				mediaParts.push_back(invoice->title);
			}
			mediaParts.push_back(invoice->currency
				+ u" "_q
				+ QString::number(invoice->amount / 100.0, 'f', 2));
		} else if (const auto gift = media->gift()) {
			switch (gift->type) {
			case Data::GiftType::Premium:
				mediaParts.push_back(
					tr::lng_sr_message_gift_premium(
						tr::now,
						lt_count,
						gift->count));
				break;
			case Data::GiftType::Credits:
			case Data::GiftType::StarGift:
				mediaParts.push_back(
					tr::lng_sr_message_gift_credits(
						tr::now,
						lt_count,
						gift->count));
				break;
			default:
				if (!gift->giftTitle.isEmpty()) {
					mediaParts.push_back(gift->giftTitle);
				}
				break;
			}
		} else if (const auto todolist = media->todolist()) {
			if (!todolist->title.text.isEmpty()) {
				mediaParts.push_back(todolist->title.text);
			}
		} else if (media->giveawayStart() || media->giveawayResults()) {
			mediaParts.push_back(item->notificationText().text);
		} else {
			const auto notification = item->notificationText().text;
			if (!notification.isEmpty()) {
				mediaParts.push_back(notification);
			}
		}
		if (!mediaParts.isEmpty()) {
			lines.push_back(mediaParts.join(u", "_q));
		}
		if (const auto webpage = media->webpage()) {
			QStringList webParts;
			if (!webpage->siteName.isEmpty()) {
				webParts.push_back(webpage->siteName);
			}
			if (!webpage->title.isEmpty()) {
				webParts.push_back(webpage->title);
			}
			if (!webpage->description.text.isEmpty()) {
				webParts.push_back(webpage->description.text);
			}
			if (!webParts.isEmpty()) {
				lines.push_back(webParts.join(u": "_q));
			}
		}
		const auto &caption = item->originalText().text;
		if (!caption.isEmpty()) {
			lines.push_back(caption);
		}
	} else {
		const auto &text = item->originalText().text;
		if (!text.isEmpty()) {
			lines.push_back(text);
		}
	}

	const auto factcheck = item->factcheckText();
	if (!factcheck.empty()) {
		lines.push_back(factcheck.text);
	}

	if (item->isPinned()) {
		lines.push_back(tr::lng_sr_chat_pinned(tr::now));
	}

	QStringList statusParts;
	if (item->out()) {
		if (item->isSending()) {
			statusParts.push_back(tr::lng_sr_chat_sending(tr::now));
		} else if (item->hasFailed()) {
			statusParts.push_back(tr::lng_sr_chat_failed(tr::now));
		} else {
			statusParts.push_back(tr::lng_sr_chat_sent(tr::now));
		}
	} else {
		statusParts.push_back(tr::lng_sr_chat_received(tr::now));
	}
	if (item->Has<HistoryMessageEdited>()) {
		statusParts.push_back(tr::lng_edited(tr::now));
	}
	const auto dateTime = view->dateTime();
	statusParts.push_back(
		tr::lng_schedule_at(tr::now)
		+ u" "_q
		+ QLocale().toString(dateTime.time(), QLocale::ShortFormat));
	if (const auto views = item->Get<HistoryMessageViews>()) {
		if (views->views.count >= 0) {
			statusParts.push_back(
				QString::number(views->views.count)
				+ u" "_q
				+ tr::lng_stats_overview_message_views(tr::now));
		}
	}
	if (const auto signed_ = item->Get<HistoryMessageSigned>()) {
		if (!signed_->author.isEmpty()) {
			statusParts.push_back(signed_->author);
		}
	}
	lines.push_back(statusParts.join(u" "_q));

	const auto &reactions = item->reactions();
	if (!reactions.empty()) {
		QStringList reactionParts;
		for (const auto &reaction : reactions) {
			auto text = reaction.id.emoji();
			if (text.isEmpty()) {
				if (const auto customId = reaction.id.custom()) {
					const auto doc = item->history()->owner().document(
						customId);
					if (const auto sticker = doc->sticker()) {
						text = tr::lng_sr_message_custom_emoji(
							tr::now,
							lt_emoji,
							sticker->alt);
					}
				}
			}
			if (!text.isEmpty()) {
				if (reaction.count > 1) {
					reactionParts.push_back(
						text + u" "_q + QString::number(reaction.count));
				} else {
					reactionParts.push_back(text);
				}
			}
		}
		if (!reactionParts.isEmpty()) {
			lines.push_back(tr::lng_notification_reactions(tr::now)
				+ u": "_q
				+ reactionParts.join(u", "_q));
		}
	}

	return lines.join(u"\n"_q);
}

QString UnreadBarAccessibilityName(not_null<const Element*> barElement) {
	if (const auto bar = barElement->Get<HistoryView::UnreadBar>()) {
		return bar->text;
	}
	return tr::lng_unread_bar_some(tr::now);
}

QString MessageSubItemLabel(MessageSubItem item) {
	switch (item) {
	case MessageSubItem::Seen:
		return tr::lng_sr_message_seen(tr::now);
	case MessageSubItem::Sender:
		return tr::lng_sr_chat_column_sender(tr::now);
	case MessageSubItem::ViaBot:
		return tr::lng_inline_bot_via(tr::now, lt_inline_bot, QString());
	case MessageSubItem::Reply:
		return tr::lng_notification_reply(tr::now);
	case MessageSubItem::Forward:
		return tr::lng_sr_chat_column_forward(tr::now);
	case MessageSubItem::MediaType:
		return tr::lng_sr_message_column_media_type(tr::now);
	case MessageSubItem::Download:
		return tr::lng_media_download(tr::now);
	case MessageSubItem::Played:
		return tr::lng_sr_message_played(tr::now);
	case MessageSubItem::Artist:
		return tr::lng_sr_message_column_artist(tr::now);
	case MessageSubItem::Title:
		return tr::lng_sr_message_column_title(tr::now);
	case MessageSubItem::Filename:
		return tr::lng_sr_message_column_filename(tr::now);
	case MessageSubItem::Duration:
		return tr::lng_sr_message_column_duration(tr::now);
	case MessageSubItem::Dimensions:
		return tr::lng_sr_message_column_dimensions(tr::now);
	case MessageSubItem::FileSize:
		return tr::lng_sr_message_column_file_size(tr::now);
	case MessageSubItem::Message:
		return tr::lng_sr_chat_column_message(tr::now);
	case MessageSubItem::Delivery:
		return tr::lng_sr_chat_column_delivery(tr::now);
	case MessageSubItem::Edited:
		return tr::lng_edited(tr::now);
	case MessageSubItem::Time:
		return tr::lng_sr_chat_column_time(tr::now);
	case MessageSubItem::Reactions:
		return tr::lng_notification_reactions(tr::now);
	case MessageSubItem::Views:
		return tr::lng_stats_overview_message_views(tr::now);
	case MessageSubItem::Signature:
		return tr::lng_sr_message_column_signature(tr::now);
	case MessageSubItem::Pinned:
		return tr::lng_sr_chat_pinned(tr::now);
	case MessageSubItem::WebSite:
		return tr::lng_sr_message_column_web_site(tr::now);
	case MessageSubItem::WebTitle:
		return tr::lng_sr_message_column_web_title(tr::now);
	case MessageSubItem::WebDescription:
		return tr::lng_sr_message_column_web_description(tr::now);
	case MessageSubItem::PollQuestion:
		return tr::lng_sr_message_column_poll_question(tr::now);
	case MessageSubItem::PollOptions:
		return tr::lng_polls_create_options(tr::now);
	case MessageSubItem::PollStatus:
		return tr::lng_sr_message_column_poll_status(tr::now);
	case MessageSubItem::ContactName:
		return tr::lng_sr_message_column_contact_name(tr::now);
	case MessageSubItem::ContactPhone:
		return tr::lng_sr_message_column_contact_phone(tr::now);
	case MessageSubItem::Location:
		return tr::lng_sr_message_column_location(tr::now);
	case MessageSubItem::StickerEmoji:
		return tr::lng_sr_message_column_sticker_emoji(tr::now);
	case MessageSubItem::GameTitle:
		return tr::lng_sr_message_column_game_title(tr::now);
	case MessageSubItem::GameDescription:
		return tr::lng_sr_message_column_game_description(tr::now);
	case MessageSubItem::InvoiceTitle:
		return tr::lng_sr_message_column_invoice_title(tr::now);
	case MessageSubItem::InvoiceAmount:
		return tr::lng_sr_message_column_invoice_amount(tr::now);
	case MessageSubItem::Spoiler:
		return tr::lng_sr_message_column_spoiler(tr::now);
	case MessageSubItem::Dice:
		return tr::lng_sr_message_column_dice(tr::now);
	case MessageSubItem::Giveaway:
		return tr::lng_sr_message_column_giveaway(tr::now);
	case MessageSubItem::Gift:
		return tr::lng_sr_message_column_gift(tr::now);
	case MessageSubItem::TodoTitle:
		return tr::lng_sr_message_column_todo_title(tr::now);
	case MessageSubItem::TodoItems:
		return tr::lng_sr_message_column_todo_items(tr::now);
	case MessageSubItem::Factcheck:
		return tr::lng_sr_message_column_factcheck(tr::now);
	case MessageSubItem::ForwardDate:
		return tr::lng_sr_message_column_forward_date(tr::now);
	case MessageSubItem::ForwardAuthor:
		return tr::lng_sr_message_column_forward_author(tr::now);
	case MessageSubItem::PaidReactions:
		return tr::lng_sr_message_column_paid_reactions(tr::now);
	case MessageSubItem::Count:
		break;
	}
	return {};
}

QString MessageSubItemValue(
		not_null<const Element*> view,
		not_null<History*> history,
		MessageSubItem item) {
	const auto data = view->data();
	if (data->isService()) {
		if (item == MessageSubItem::Message) {
			return data->notificationText().text;
		}
		return {};
	}

	switch (item) {
	case MessageSubItem::Seen:
		if (data->out()) {
			return data->unread(history)
				? tr::lng_sr_message_not_seen(tr::now)
				: tr::lng_sr_message_seen(tr::now);
		}
		return {};
	case MessageSubItem::Sender:
		if (data->out()) {
			return tr::lng_sr_from_me(tr::now);
		} else if (const auto from = data->displayFrom()) {
			return from->name();
		}
		return {};
	case MessageSubItem::ViaBot:
		if (const auto bot = data->viaBot()) {
			return tr::lng_inline_bot_via(
				tr::now,
				lt_inline_bot,
				'@' + bot->username());
		}
		return {};
	case MessageSubItem::Reply:
		if (const auto reply = data->Get<HistoryMessageReply>()) {
			if (const auto message = reply->resolvedMessage.get()) {
				const auto replyFrom = message->displayFrom();
				const auto replyName = replyFrom
					? replyFrom->name()
					: QString();
				const auto replyText = message->inReplyText().text;
				if (!replyName.isEmpty() || !replyText.isEmpty()) {
					return tr::lng_sr_message_reply_to(
						tr::now,
						lt_name,
						replyName.isEmpty()
							? tr::lng_sr_from_me(tr::now)
							: replyName,
						lt_text,
						replyText);
				}
			}
		}
		return {};
	case MessageSubItem::Forward:
		if (const auto fwd = data->Get<HistoryMessageForwarded>()) {
			if (fwd->originalSender) {
				return tr::lng_forwarded(
					tr::now,
					lt_user,
					fwd->originalSender->name());
			} else if (fwd->originalHiddenSenderInfo) {
				return tr::lng_forwarded(
					tr::now,
					lt_user,
					fwd->originalHiddenSenderInfo->name);
			}
		}
		return {};
	case MessageSubItem::MediaType: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		if (const auto document = media->document()) {
			if (document->isVoiceMessage()) {
				return tr::lng_in_dlg_audio(tr::now);
			} else if (document->isVideoMessage()) {
				return tr::lng_in_dlg_video_message(tr::now);
			} else if (document->isSong()) {
				return tr::lng_in_dlg_audio_file(tr::now);
			} else if (document->isVideoFile()) {
				return tr::lng_in_dlg_video(tr::now);
			} else if (document->isAnimation()) {
				return u"GIF"_q;
			} else if (document->sticker()) {
				return tr::lng_in_dlg_sticker(tr::now);
			}
			return tr::lng_in_dlg_file(tr::now);
		} else if (media->photo()) {
			return tr::lng_in_dlg_photo(tr::now);
		} else if (media->call()) {
			return data->notificationText().text;
		}
		return data->notificationText().text;
	}
	case MessageSubItem::Download: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		const auto document = media->document();
		if (!document) {
			return {};
		}
		if (document->loading()) {
			return tr::lng_sr_message_downloading(tr::now);
		} else if (!document->filepath(true).isEmpty()
			|| document->loadedInMediaCache()) {
			return tr::lng_emoji_set_ready(tr::now);
		}
		return tr::lng_sr_message_not_downloaded(tr::now);
	}
	case MessageSubItem::Played: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		const auto document = media->document();
		if (!document) {
			return {};
		}
		if (document->isVoiceMessage()
			|| document->isVideoMessage()) {
			return data->isUnreadMedia()
				? tr::lng_sr_message_not_played(tr::now)
				: tr::lng_sr_message_played(tr::now);
		}
		return {};
	}
	case MessageSubItem::Artist: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		const auto document = media->document();
		if (!document) {
			return {};
		}
		if (const auto song = document->song()) {
			return song->performer;
		}
		return {};
	}
	case MessageSubItem::Title: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		const auto document = media->document();
		if (!document) {
			return {};
		}
		if (const auto song = document->song()) {
			return song->title;
		}
		return {};
	}
	case MessageSubItem::Filename: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		const auto document = media->document();
		if (!document
			|| document->isSong()
			|| document->isVoiceMessage()
			|| document->isVideoMessage()
			|| document->sticker()) {
			return {};
		}
		return document->filename();
	}
	case MessageSubItem::Duration: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		if (const auto document = media->document()) {
			const auto duration = document->duration();
			if (duration > 0) {
				return Ui::FormatDurationText(duration / 1000);
			}
		} else if (const auto call = media->call()) {
			if (call->duration > 0) {
				return Ui::FormatDurationText(call->duration);
			}
		}
		return {};
	}
	case MessageSubItem::Dimensions: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		if (const auto document = media->document()) {
			if (!document->dimensions.isEmpty()
				&& (document->isVideoFile()
					|| document->isImage())) {
				return Ui::FormatImageSizeText(document->dimensions);
			}
		} else if (const auto photo = media->photo()) {
			const auto large = photo->size(
				Data::PhotoSize::Large);
			if (large && !large->isEmpty()) {
				return Ui::FormatImageSizeText(*large);
			}
		}
		return {};
	}
	case MessageSubItem::FileSize: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		const auto document = media->document();
		if (!document || document->size <= 0) {
			return {};
		}
		return Ui::FormatSizeText(document->size);
	}
	case MessageSubItem::Message: {
		if (data->media()) {
			return data->originalText().text;
		}
		return data->originalText().text;
	}
	case MessageSubItem::Delivery:
		if (data->out()) {
			if (data->isSending()) {
				return tr::lng_sr_chat_sending(tr::now);
			} else if (data->hasFailed()) {
				return tr::lng_sr_chat_failed(tr::now);
			}
			return tr::lng_sr_chat_sent(tr::now);
		}
		return tr::lng_sr_chat_received(tr::now);
	case MessageSubItem::Edited:
		if (data->Has<HistoryMessageEdited>()) {
			return tr::lng_edited(tr::now);
		}
		return {};
	case MessageSubItem::Time: {
		const auto dateTime = view->dateTime();
		return tr::lng_schedule_at(tr::now)
			+ u" "_q
			+ QLocale().toString(dateTime.time(), QLocale::ShortFormat);
	}
	case MessageSubItem::Reactions: {
		const auto &reactions = data->reactions();
		if (reactions.empty()) {
			return {};
		}
		QStringList reactionParts;
		for (const auto &reaction : reactions) {
			auto text = reaction.id.emoji();
			if (text.isEmpty()) {
				if (const auto customId = reaction.id.custom()) {
					const auto doc = data->history()->owner().document(
						customId);
					if (const auto sticker = doc->sticker()) {
						text = tr::lng_sr_message_custom_emoji(
							tr::now,
							lt_emoji,
							sticker->alt);
					}
				}
			}
			if (!text.isEmpty()) {
				if (reaction.count > 1) {
					reactionParts.push_back(
						text + u" "_q + QString::number(reaction.count));
				} else {
					reactionParts.push_back(text);
				}
			}
		}
		return reactionParts.join(u", "_q);
	}
	case MessageSubItem::Views: {
		const auto views = data->Get<HistoryMessageViews>();
		if (views && views->views.count >= 0) {
			return QString::number(views->views.count);
		}
		return {};
	}
	case MessageSubItem::Signature: {
		const auto signed_ = data->Get<HistoryMessageSigned>();
		if (signed_ && !signed_->author.isEmpty()) {
			return signed_->author;
		}
		return {};
	}
	case MessageSubItem::Pinned:
		if (data->isPinned()) {
			return tr::lng_sr_chat_pinned(tr::now);
		}
		return {};
	case MessageSubItem::WebSite: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		const auto webpage = media->webpage();
		if (webpage) {
			return webpage->siteName;
		}
		return {};
	}
	case MessageSubItem::WebTitle: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		const auto webpage = media->webpage();
		if (webpage) {
			return webpage->title;
		}
		return {};
	}
	case MessageSubItem::WebDescription: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		const auto webpage = media->webpage();
		if (webpage) {
			return webpage->description.text;
		}
		return {};
	}
	case MessageSubItem::PollQuestion: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		const auto poll = media->poll();
		if (poll) {
			return poll->question.text;
		}
		return {};
	}
	case MessageSubItem::PollOptions: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		const auto poll = media->poll();
		if (!poll) {
			return {};
		}
		QStringList options;
		for (const auto &answer : poll->answers) {
			auto line = answer.text.text;
			if (poll->totalVoters > 0 && answer.votes > 0) {
				const auto percent = (answer.votes * 100)
					/ poll->totalVoters;
				line += u" ("_q
					+ QString::number(percent)
					+ u"%, "_q
					+ QString::number(answer.votes)
					+ u")"_q;
			}
			if (answer.chosen) {
				line += u" ✓"_q;
			}
			if (answer.correct) {
				line += u" ✔"_q;
			}
			options.push_back(line);
		}
		return options.join(u"; "_q);
	}
	case MessageSubItem::PollStatus: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		const auto poll = media->poll();
		if (!poll) {
			return {};
		}
		QStringList parts;
		parts.push_back(poll->quiz()
			? tr::lng_polls_public_quiz(tr::now)
			: tr::lng_polls_public(tr::now));
		if (poll->closed()) {
			parts.push_back(
				tr::lng_hours_closed(tr::now));
		}
		parts.push_back(tr::lng_polls_votes_count(
			tr::now,
			lt_count,
			poll->totalVoters));
		return parts.join(u", "_q);
	}
	case MessageSubItem::ContactName: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		const auto contact = media->sharedContact();
		if (!contact) {
			return {};
		}
		auto name = contact->firstName;
		if (!contact->lastName.isEmpty()) {
			if (!name.isEmpty()) {
				name += u" "_q;
			}
			name += contact->lastName;
		}
		return name;
	}
	case MessageSubItem::ContactPhone: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		const auto contact = media->sharedContact();
		if (contact) {
			return contact->phoneNumber;
		}
		return {};
	}
	case MessageSubItem::Location: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		if (media->location()) {
			return data->notificationText().text;
		}
		return {};
	}
	case MessageSubItem::StickerEmoji: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		const auto document = media->document();
		if (!document) {
			return {};
		}
		if (const auto sticker = document->sticker()) {
			return sticker->alt;
		}
		return {};
	}
	case MessageSubItem::GameTitle: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		const auto game = media->game();
		if (game) {
			return game->title;
		}
		return {};
	}
	case MessageSubItem::GameDescription: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		const auto game = media->game();
		if (game) {
			return game->description;
		}
		return {};
	}
	case MessageSubItem::InvoiceTitle: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		const auto invoice = media->invoice();
		if (invoice) {
			return invoice->title;
		}
		return {};
	}
	case MessageSubItem::InvoiceAmount: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		const auto invoice = media->invoice();
		if (!invoice) {
			return {};
		}
		auto result = invoice->currency
			+ u" "_q
			+ QString::number(invoice->amount / 100.0, 'f', 2);
		if (invoice->receiptMsgId) {
			result += u" ("_q
				+ tr::lng_sr_message_invoice_paid(tr::now)
				+ u")"_q;
		} else {
			result += u" ("_q
				+ tr::lng_sr_message_invoice_unpaid(tr::now)
				+ u")"_q;
		}
		return result;
	}
	case MessageSubItem::Spoiler: {
		const auto media = data->media();
		if (media && media->hasSpoiler()) {
			return tr::lng_sr_message_spoiler(tr::now);
		}
		return {};
	}
	case MessageSubItem::Dice: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		const auto notification = data->notificationText().text;
		if (media->game() || media->poll()) {
			return {};
		}
		if (!media->document()
			&& !media->photo()
			&& !media->sharedContact()
			&& !media->location()
			&& !media->invoice()
			&& !media->giveawayStart()
			&& !media->giveawayResults()
			&& !media->gift()
			&& !media->todolist()
			&& !media->game()
			&& !media->poll()
			&& !media->webpage()
			&& !notification.isEmpty()) {
			return notification;
		}
		return {};
	}
	case MessageSubItem::Giveaway: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		if (const auto giveaway = media->giveawayStart()) {
			QStringList parts;
			if (giveaway->quantity > 0) {
				parts.push_back(QString::number(giveaway->quantity)
					+ u" winners"_q);
			}
			if (giveaway->months > 0) {
				parts.push_back(QString::number(giveaway->months)
					+ u" months Premium"_q);
			}
			if (giveaway->credits > 0) {
				parts.push_back(QString::number(giveaway->credits)
					+ u" stars"_q);
			}
			if (!giveaway->additionalPrize.isEmpty()) {
				parts.push_back(giveaway->additionalPrize);
			}
			return parts.join(u", "_q);
		}
		if (const auto results = media->giveawayResults()) {
			QStringList parts;
			parts.push_back(QString::number(results->winnersCount)
				+ u" winners"_q);
			if (results->unclaimedCount > 0) {
				parts.push_back(QString::number(results->unclaimedCount)
					+ u" unclaimed"_q);
			}
			if (results->refunded) {
				parts.push_back(u"refunded"_q);
			}
			return parts.join(u", "_q);
		}
		return {};
	}
	case MessageSubItem::Gift: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		const auto gift = media->gift();
		if (!gift) {
			return {};
		}
		switch (gift->type) {
		case Data::GiftType::Premium:
			return tr::lng_sr_message_gift_premium(
				tr::now,
				lt_count,
				gift->count);
		case Data::GiftType::Credits:
		case Data::GiftType::StarGift:
			return tr::lng_sr_message_gift_credits(
				tr::now,
				lt_count,
				gift->count);
		default:
			if (!gift->giftTitle.isEmpty()) {
				return gift->giftTitle;
			}
			return {};
		}
	}
	case MessageSubItem::TodoTitle: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		const auto todolist = media->todolist();
		if (todolist) {
			return todolist->title.text;
		}
		return {};
	}
	case MessageSubItem::TodoItems: {
		const auto media = data->media();
		if (!media) {
			return {};
		}
		const auto todolist = media->todolist();
		if (!todolist) {
			return {};
		}
		QStringList items;
		for (const auto &todoItem : todolist->items) {
			auto line = todoItem.text.text;
			line += u" ("_q;
			line += todoItem.completedBy
				? tr::lng_sr_message_todo_completed(tr::now)
				: tr::lng_sr_message_todo_not_completed(tr::now);
			line += u")"_q;
			items.push_back(line);
		}
		return items.join(u"; "_q);
	}
	case MessageSubItem::Factcheck: {
		const auto text = data->factcheckText();
		if (!text.empty()) {
			return text.text;
		}
		return {};
	}
	case MessageSubItem::ForwardDate: {
		const auto fwd = data->Get<HistoryMessageForwarded>();
		if (!fwd) {
			return {};
		}
		if (fwd->originalDate) {
			const auto dt = base::unixtime::parse(fwd->originalDate);
			return QLocale().toString(dt, QLocale::ShortFormat);
		}
		return {};
	}
	case MessageSubItem::ForwardAuthor: {
		const auto fwd = data->Get<HistoryMessageForwarded>();
		if (!fwd) {
			return {};
		}
		if (!fwd->originalPostAuthor.isEmpty()) {
			return fwd->originalPostAuthor;
		}
		if (fwd->imported) {
			return u"Imported"_q;
		}
		return {};
	}
	case MessageSubItem::PaidReactions: {
		const auto &reactions = data->reactions();
		for (const auto &reaction : reactions) {
			if (reaction.id.paid() && reaction.count > 0) {
				return QString::number(reaction.count);
			}
		}
		return {};
	}
	case MessageSubItem::Count:
		break;
	}
	return {};
}

std::vector<MessageSubItem> ActiveMessageSubItems(
		not_null<const Element*> view,
		not_null<History*> history) {
	auto result = std::vector<MessageSubItem>();
	result.reserve(int(MessageSubItem::Count));
	for (auto i = 0; i != int(MessageSubItem::Count); ++i) {
		const auto item = MessageSubItem(i);
		if (!MessageSubItemValue(view, history, item).isEmpty()) {
			result.push_back(item);
		}
	}
	return result;
}

} // namespace HistoryView
