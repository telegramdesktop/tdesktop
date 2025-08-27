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
#include "history/view/media/history_view_unique_gift.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "lang/lang_keys.h"
#include "ui/chat/chat_style.h"
#include "ui/text/text_utilities.h"
#include "ui/text/format_values.h"
#include "styles/style_chat.h"
#include "styles/style_credits.h"

namespace HistoryView {
namespace {

constexpr auto kFadedOpacity = 0.85;

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

struct Changes {
	bool date = false;
	bool price = false;
	bool message = true;
};
[[nodiscard]] std::optional<Changes> ResolveChanges(
		not_null<HistoryItem*> changed,
		HistoryItem *original) {
	const auto wasSuggest = original
		? original->Get<HistoryMessageSuggestedPost>()
		: nullptr;
	const auto nowSuggest = changed->Get<HistoryMessageSuggestedPost>();
	if (!wasSuggest || !nowSuggest) {
		return {};
	}
	auto result = Changes();
	if (wasSuggest->date != nowSuggest->date) {
		result.date = true;
	}
	if (wasSuggest->price != nowSuggest->price) {
		result.price = true;
	}
	const auto wasText = original->originalText();
	const auto nowText = changed->originalText();
	const auto mediaSame = [&] {
		const auto wasMedia = original->media();
		const auto nowMedia = changed->media();
		if (!wasMedia && !nowMedia) {
			return true;
		} else if (!wasMedia
			|| !nowMedia
			|| !wasMedia->allowsEditCaption()
			|| !nowMedia->allowsEditCaption()) {
			return false;
		}
		// We treat as "same" only same photo or same file.
		return (wasMedia->photo() == nowMedia->photo())
			&& (wasMedia->document() == nowMedia->document());
	};
	if (!result.price && !result.date) {
		result.message = true;
	} else if (wasText == nowText && mediaSame()) {
		result.message = false;
	}
	return result;
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
						? (decision->price.ton()
							? tr::lng_suggest_action_your_not_enough_ton
							: tr::lng_suggest_action_your_not_enough_stars)
						: (decision->price.ton()
							? tr::lng_suggest_action_his_not_enough_ton
							: tr::lng_suggest_action_his_not_enough_stars))(
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
				const auto fadedFg = [](const PaintContext &context) {
					auto result = context.st->msgServiceFg()->c;
					result.setAlphaF(result.alphaF() * kFadedOpacity);
					return result;
				};
				push(std::make_unique<TextPartColored>(
					TextWithEntities().append('"').append(
						decision->rejectComment
					).append('"'),
					st::chatSuggestInfoLastMargin,
					fadedFg));
			}
		} else {
			const auto price = decision->price;
			pushText(
				TextWithEntities(
				).append(Emoji(kAgreement)).append(' ').append(
					Ui::Text::Bold(tr::lng_suggest_action_agreement(tr::now))
				),
				st::chatSuggestInfoTitleMargin,
				style::al_top);
			const auto date = base::unixtime::parse(decision->date);
			pushText(
				TextWithEntities(
				).append(Emoji(kCalendar)).append(' ').append(
					tr::lng_suggest_action_agree_date(
						tr::now,
						lt_channel,
						Ui::Text::Bold(broadcast->name()),
						lt_date,
						Ui::Text::Bold(tr::lng_mediaview_date_time(
							tr::now,
							lt_date,
							QLocale().toString(
								date.date(),
								QLocale::ShortFormat),
							lt_time,
							QLocale().toString(
								date.time(),
								QLocale::ShortFormat))),
						Ui::Text::WithEntities)),
				(price
					? st::chatSuggestInfoMiddleMargin
					: st::chatSuggestInfoLastMargin));
			if (price) {
				pushText(
					TextWithEntities(
					).append(Emoji(kMoney)).append(' ').append(
						(sublistPeer->isSelf()
							? (price.stars()
								? tr::lng_suggest_action_your_charged_stars
								: tr::lng_suggest_action_your_charged_ton)(
									tr::now,
									lt_count_decimal,
									price.value(),
									Ui::Text::RichLangValue)
							: (price.stars()
								? tr::lng_suggest_action_his_charged_stars
								: tr::lng_suggest_action_his_charged_ton)(
									tr::now,
									lt_count_decimal,
									price.value(),
									lt_from,
									Ui::Text::Bold(sublistPeer->shortName()),
									Ui::Text::RichLangValue))),
					st::chatSuggestInfoMiddleMargin);

				pushText(
					TextWithEntities(
					).append(Emoji(kHourglass)).append(' ').append(
						(price.ton()
							? tr::lng_suggest_action_agree_receive_ton
							: tr::lng_suggest_action_agree_receive_stars)(
							tr::now,
							lt_channel,
							Ui::Text::Bold(broadcast->name()),
							Ui::Text::WithEntities)),
					st::chatSuggestInfoMiddleMargin);

				pushText(
					TextWithEntities(
					).append(Emoji(kReload)).append(' ').append(
						(price.ton()
							? tr::lng_suggest_action_agree_removed_ton
							: tr::lng_suggest_action_agree_removed_stars)(
							tr::now,
							lt_channel,
							Ui::Text::Bold(broadcast->name()),
							Ui::Text::WithEntities)),
					st::chatSuggestInfoLastMargin);
			}
		}
	};
}

auto GenerateSuggestRequestMedia(
	not_null<Element*> parent,
	not_null<const HistoryMessageSuggestedPost*> suggest)
	-> Fn<void(
		not_null<MediaGeneric*>,
		Fn<void(std::unique_ptr<MediaGenericPart>)>)> {
	return [=](
			not_null<MediaGeneric*> media,
			Fn<void(std::unique_ptr<MediaGenericPart>)> push) {
		const auto normalFg = [](const PaintContext &context) {
			return context.st->msgServiceFg()->c;
		};
		const auto fadedFg = [](const PaintContext &context) {
			auto result = context.st->msgServiceFg()->c;
			result.setAlphaF(result.alphaF() * kFadedOpacity);
			return result;
		};
		const auto item = parent->data();
		const auto replyData = item->Get<HistoryMessageReply>();
		const auto original = replyData
			? replyData->resolvedMessage.get()
			: nullptr;
		const auto changes = ResolveChanges(item, original);
		const auto from = item->from();

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

		pushText(
			((!changes && from->isSelf())
				? tr::lng_suggest_action_your(
					tr::now,
					Ui::Text::WithEntities)
				: (!changes
					? tr::lng_suggest_action_his
					: changes->message
					? tr::lng_suggest_change_content
					: (changes->date && changes->price)
					? tr::lng_suggest_change_price_time
					: changes->price
					? tr::lng_suggest_change_price
					: tr::lng_suggest_change_time)(
						tr::now,
						lt_from,
						Ui::Text::Bold(from->shortName()),
						Ui::Text::WithEntities)),
			st::chatSuggestInfoTitleMargin,
			style::al_top);

		auto entries = std::vector<AttributeTable::Entry>();
		entries.push_back({
			((changes && changes->price)
				? tr::lng_suggest_change_price_label
				: tr::lng_suggest_action_price_label)(tr::now),
			Ui::Text::Bold(!suggest->price
				? tr::lng_suggest_action_price_free(tr::now)
				: suggest->price.stars()
				? tr::lng_suggest_stars_amount(
					tr::now,
					lt_count_decimal,
					suggest->price.value())
				: tr::lng_suggest_ton_amount(
					tr::now,
					lt_count_decimal,
					suggest->price.value())),
		});
		entries.push_back({
			((changes && changes->date)
				? tr::lng_suggest_change_time_label
				: tr::lng_suggest_action_time_label)(tr::now),
			Ui::Text::Bold(suggest->date
				? Ui::FormatDateTime(base::unixtime::parse(suggest->date))
				: tr::lng_suggest_action_time_any(tr::now)),
		});
		push(std::make_unique<AttributeTable>(
			std::move(entries),
			((changes && changes->message)
				? st::chatSuggestTableMiddleMargin
				: st::chatSuggestTableLastMargin),
			fadedFg,
			normalFg));
		if (changes && changes->message) {
			push(std::make_unique<TextPartColored>(
				tr::lng_suggest_change_text_label(
					tr::now,
					Ui::Text::WithEntities),
				st::chatSuggestInfoLastMargin,
				fadedFg));
		}
	};
}

} // namespace HistoryView
