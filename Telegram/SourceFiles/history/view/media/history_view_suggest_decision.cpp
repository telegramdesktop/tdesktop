/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_suggest_decision.h"

#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "history/view/media/history_view_media_generic.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "lang/lang_keys.h"
#include "ui/text/text_utilities.h"
#include "ui/text/format_values.h"
#include "styles/style_chat.h"
#include "styles/style_credits.h"

namespace HistoryView {
namespace {

enum EmojiType {
	kAgreement,
	kCalendar,
	kMoney,
	kHourglass,
	kReload,
	kDecline,
	kDiscard,
	kWarning,
};

[[nodiscard]] const char *Raw(EmojiType type) {
	switch (type) {
	case EmojiType::kAgreement: return "\xf0\x9f\xa4\x9d";
	case EmojiType::kCalendar: return "\xf0\x9f\x93\x86";
	case EmojiType::kMoney: return "\xf0\x9f\x92\xb0";
	case EmojiType::kHourglass: return "\xe2\x8c\x9b\xef\xb8\x8f";
	case EmojiType::kReload: return "\xf0\x9f\x94\x84";
	case EmojiType::kDecline: return "\xe2\x9d\x8c";
	case EmojiType::kDiscard: return "\xf0\x9f\x9a\xab";
	case EmojiType::kWarning: return "\xe2\x9a\xa0\xef\xb8\x8f";
	}
	Unexpected("EmojiType in Raw.");
}

[[nodiscard]] QString Emoji(EmojiType type) {
	return QString::fromUtf8(Raw(type));
}

} // namespace

auto GenerateSuggestDecisionMedia(
	not_null<Element*> parent,
	not_null<const HistoryServiceSuggestDecision*> decision)
	-> Fn<void(
		not_null<MediaGeneric*>,
		Fn<void(std::unique_ptr<MediaGenericPart>)>)> {
	return [=](
			not_null<MediaGeneric*> media,
			Fn<void(std::unique_ptr<MediaGenericPart>)> push) {
		const auto peer = parent->history()->peer;
		const auto broadcast = peer->monoforumBroadcast();
		if (!broadcast) {
			return;
		}

		const auto sublistPeerId = parent->data()->sublistPeerId();
		const auto sublistPeer = peer->owner().peer(sublistPeerId);

		auto pushText = [&](
				TextWithEntities text,
				QMargins margins = {},
				style::align align = style::al_left,
				const base::flat_map<uint16, ClickHandlerPtr> &links = {}) {
			push(std::make_unique<MediaGenericTextPart>(
				std::move(text),
				margins,
				st::defaultTextStyle,
				links,
				Ui::Text::MarkedContext(),
				align));
		};

		if (decision->balanceTooLow) {
			pushText(
				TextWithEntities(
				).append(Emoji(kWarning)).append(' ').append(
					(sublistPeer->isSelf()
						? tr::lng_suggest_action_your_not_enough
						: tr::lng_suggest_action_his_not_enough)(
							tr::now,
							Ui::Text::RichLangValue)),
				st::chatSuggestInfoFullMargin,
				style::al_top);
		} else if (decision->rejected) {
			const auto withComment = !decision->rejectComment.isEmpty();
			pushText(
				TextWithEntities(
				).append(Emoji(kDecline)).append(' ').append(
					(withComment
						? tr::lng_suggest_action_declined_reason
						: tr::lng_suggest_action_declined)(
							tr::now,
							lt_from,
							Ui::Text::Bold(broadcast->name()),
							Ui::Text::WithEntities)),
				(withComment
					? st::chatSuggestInfoTitleMargin
					: st::chatSuggestInfoFullMargin));
			if (withComment) {
				pushText(
					TextWithEntities().append('"').append(
						decision->rejectComment
					).append('"'),
					st::chatSuggestInfoLastMargin,
					style::al_top);
			}
		} else {
			const auto stars = decision->stars;
			pushText(
				TextWithEntities(
				).append(Emoji(kAgreement)).append(' ').append(
					Ui::Text::Bold(tr::lng_suggest_action_agreement(tr::now))
				),
				st::chatSuggestInfoTitleMargin,
				style::al_top);
			pushText(
				TextWithEntities(
				).append(Emoji(kCalendar)).append(' ').append(
					tr::lng_suggest_action_agree_date(
						tr::now,
						lt_channel,
						Ui::Text::Bold(broadcast->name()),
						lt_date,
						Ui::Text::Bold(Ui::FormatDateTime(
							base::unixtime::parse(decision->date))),
						Ui::Text::WithEntities)),
				(stars
					? st::chatSuggestInfoMiddleMargin
					: st::chatSuggestInfoLastMargin));
			if (stars) {
				const auto amount = Ui::Text::Bold(
					tr::lng_prize_credits_amount(tr::now, lt_count, stars));
				pushText(
					TextWithEntities(
					).append(Emoji(kMoney)).append(' ').append(
						(sublistPeer->isSelf()
							? tr::lng_suggest_action_your_charged(
								tr::now,
								lt_amount,
								amount,
								Ui::Text::WithEntities)
							: tr::lng_suggest_action_his_charged(
								tr::now,
								lt_from,
								Ui::Text::Bold(sublistPeer->shortName()),
								lt_amount,
								amount,
								Ui::Text::WithEntities))),
					st::chatSuggestInfoMiddleMargin);

				pushText(
					TextWithEntities(
					).append(Emoji(kHourglass)).append(' ').append(
						tr::lng_suggest_action_agree_receive(
							tr::now,
							lt_channel,
							Ui::Text::Bold(broadcast->name()),
							Ui::Text::WithEntities)),
					st::chatSuggestInfoMiddleMargin);

				pushText(
					TextWithEntities(
					).append(Emoji(kReload)).append(' ').append(
						tr::lng_suggest_action_agree_removed(
							tr::now,
							lt_channel,
							Ui::Text::Bold(broadcast->name()),
							Ui::Text::WithEntities)),
					st::chatSuggestInfoLastMargin);
			}
		}
	};
}

} // namespace HistoryView
