/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_giveaway.h"

#include "base/unixtime.h"
#include "boxes/gift_premium_box.h"
#include "chat_helpers/stickers_gift_box_pack.h"
#include "chat_helpers/stickers_dice_pack.h"
#include "countries/countries_instance.h"
#include "data/data_channel.h"
#include "data/data_media_types.h"
#include "history/view/media/history_view_media_generic.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/effects/credits_graphics.h"
#include "ui/text/text_utilities.h"
#include "styles/style_chat.h"

namespace HistoryView {

constexpr auto kOutlineRatio = 0.85;

auto GenerateGiveawayStart(
	not_null<Element*> parent,
	not_null<Data::GiveawayStart*> data)
-> Fn<void(
		not_null<MediaGeneric*>,
		Fn<void(std::unique_ptr<MediaGenericPart>)>)> {
	return [=](
			not_null<MediaGeneric*> media,
			Fn<void(std::unique_ptr<MediaGenericPart>)> push) {
		const auto months = data->months;
		const auto quantity = data->quantity;

		using Data = StickerWithBadgePart::Data;
		const auto sticker = [=] {
			const auto &session = parent->history()->session();
			auto &packs = session.giftBoxStickersPacks();
			return Data{
				.sticker = packs.lookup(months),
				.size = st::msgServiceGiftBoxStickerSize,
				.singleTimePlayback = true,
			};
		};
		push(std::make_unique<StickerWithBadgePart>(
			parent,
			nullptr,
			sticker,
			st::chatGiveawayStickerPadding,
			data->credits
				? QString::number(data->credits)
				: tr::lng_prizes_badge(
					tr::now,
					lt_amount,
					QString::number(quantity)),
			data->credits
				? Ui::CreditsWhiteDoubledIcon(
					st::chatGiveawayCreditsIconHeight,
					kOutlineRatio)
				: QImage(),
			data->credits
				? std::make_optional(st::creditsBg3->c)
				: std::nullopt));

		auto pushText = [&](
				TextWithEntities text,
				QMargins margins = {},
				const base::flat_map<uint16, ClickHandlerPtr> &links = {}) {
			push(std::make_unique<MediaGenericTextPart>(
				std::move(text),
				margins,
				st::defaultTextStyle,
				links));
		};
		pushText(
			Ui::Text::Bold(
				tr::lng_prizes_title(tr::now, lt_count, quantity)),
			st::chatGiveawayPrizesTitleMargin);

		if (!data->additionalPrize.isEmpty()) {
			pushText(
				tr::lng_prizes_additional(
					tr::now,
					lt_count,
					quantity,
					lt_prize,
					TextWithEntities{ data->additionalPrize },
					Ui::Text::RichLangValue),
				st::chatGiveawayPrizesMargin);
			push(std::make_unique<TextDelimeterPart>(
				tr::lng_prizes_additional_with(tr::now),
				st::chatGiveawayPrizesWithPadding));
		}

		pushText((data->credits && (quantity == 1))
			? tr::lng_prizes_credits_about_single(
				tr::now,
				lt_amount,
				tr::lng_prizes_credits_about_amount(
					tr::now,
					lt_count,
					data->credits,
					Ui::Text::RichLangValue),
				Ui::Text::RichLangValue)
			: (data->credits && (quantity > 1))
			? tr::lng_prizes_credits_about(
				tr::now,
				lt_count,
				quantity,
				lt_amount,
				tr::lng_prizes_credits_about_amount(
					tr::now,
					lt_count,
					data->credits,
					Ui::Text::RichLangValue),
				Ui::Text::RichLangValue)
			: tr::lng_prizes_about(
				tr::now,
				lt_count,
				quantity,
				lt_duration,
				Ui::Text::Bold(GiftDuration(months)),
				Ui::Text::RichLangValue),
			st::chatGiveawayPrizesMargin);
		pushText(
			Ui::Text::Bold(tr::lng_prizes_participants(tr::now)),
			st::chatGiveawayPrizesTitleMargin);

		const auto hasChannel = ranges::any_of(
			data->channels,
			&ChannelData::isBroadcast);
		const auto hasGroup = ranges::any_of(
			data->channels,
			&ChannelData::isMegagroup);
		const auto mixed = (hasChannel && hasGroup);
		pushText({ (data->all
			? (mixed
				? tr::lng_prizes_participants_all_mixed
				: hasGroup
				? tr::lng_prizes_participants_all_group
				: tr::lng_prizes_participants_all)
			: (mixed
				? tr::lng_prizes_participants_new_mixed
				: hasGroup
				? tr::lng_prizes_participants_new_group
				: tr::lng_prizes_participants_new))(
					tr::now,
					lt_count,
					data->channels.size()),
		}, st::chatGiveawayParticipantsMargin);

		auto list = ranges::views::all(
			data->channels
		) | ranges::views::transform([](not_null<ChannelData*> channel) {
			return not_null<PeerData*>(channel);
		}) | ranges::to_vector;
		push(std::make_unique<PeerBubbleListPart>(
			parent,
			std::move(list)));

		const auto &instance = Countries::Instance();
		auto countries = QStringList();
		for (const auto &country : data->countries) {
			const auto name = instance.countryNameByISO2(country);
			const auto flag = instance.flagEmojiByISO2(country);
			countries.push_back(flag + QChar(0xA0) + name);
		}
		if (const auto count = countries.size()) {
			auto united = countries.front();
			for (auto i = 1; i != count; ++i) {
				united = ((i + 1 == count)
					? tr::lng_prizes_countries_and_last
					: tr::lng_prizes_countries_and_one)(
						tr::now,
						lt_countries,
						united,
						lt_country,
						countries[i]);
			}
			pushText({
				tr::lng_prizes_countries(tr::now, lt_countries, united),
			}, st::chatGiveawayPrizesMargin);
		}

		pushText(
			Ui::Text::Bold(tr::lng_prizes_date(tr::now)),
			(countries.empty()
				? st::chatGiveawayNoCountriesTitleMargin
				: st::chatGiveawayPrizesMargin));
		pushText({
			langDateTime(base::unixtime::parse(data->untilDate)),
		}, st::chatGiveawayEndDateMargin);
	};
}

auto GenerateGiveawayResults(
	not_null<Element*> parent,
	not_null<Data::GiveawayResults*> data)
-> Fn<void(
		not_null<MediaGeneric*>,
		Fn<void(std::unique_ptr<MediaGenericPart>)>)> {
	return [=](
			not_null<MediaGeneric*> media,
			Fn<void(std::unique_ptr<MediaGenericPart>)> push) {
		const auto quantity = data->winnersCount;

		using Data = StickerWithBadgePart::Data;
		const auto sticker = [=] {
			const auto &session = parent->history()->session();
			auto &packs = session.diceStickersPacks();
			const auto &emoji = Stickers::DicePacks::kPartyPopper;
			return Data{
				.sticker = packs.lookup(emoji, 0),
				.skipTop = st::chatGiveawayWinnersTopSkip,
				.size = st::maxAnimatedEmojiSize,
				.singleTimePlayback = true,
			};
		};
		push(std::make_unique<StickerWithBadgePart>(
			parent,
			nullptr,
			sticker,
			st::chatGiveawayStickerPadding,
			data->credits
				? QString::number(data->credits)
				: tr::lng_prizes_badge(
					tr::now,
					lt_amount,
					QString::number(quantity)),
			data->credits
				? Ui::CreditsWhiteDoubledIcon(
					st::chatGiveawayCreditsIconHeight,
					kOutlineRatio)
				: QImage(),
			data->credits
				? std::make_optional(st::creditsBg3->c)
				: std::nullopt));

		auto pushText = [&](
				TextWithEntities text,
				QMargins margins = {},
				const base::flat_map<uint16, ClickHandlerPtr> &links = {}) {
			push(std::make_unique<MediaGenericTextPart>(
				std::move(text),
				margins,
				st::defaultTextStyle,
				links));
		};
		const auto isSingleWinner = (data->winnersCount == 1);
		pushText(
			(isSingleWinner
				? tr::lng_prizes_results_title_one
				: tr::lng_prizes_results_title)(tr::now, Ui::Text::Bold),
			st::chatGiveawayPrizesTitleMargin);
		const auto showGiveawayHandler = JumpToMessageClickHandler(
			data->channel,
			data->launchId,
			parent->data()->fullId());
		pushText(
			tr::lng_prizes_results_about(
				tr::now,
				lt_count,
				quantity,
				lt_link,
				Ui::Text::Link(tr::lng_prizes_results_link(tr::now)),
				Ui::Text::RichLangValue),
			st::chatGiveawayPrizesMargin,
			{ { 1, showGiveawayHandler } });
		pushText(
			(isSingleWinner
				? tr::lng_prizes_results_winner
				: tr::lng_prizes_results_winners)(tr::now, Ui::Text::Bold),
			st::chatGiveawayPrizesTitleMargin);

		push(std::make_unique<PeerBubbleListPart>(
			parent,
			data->winners));
		if (data->winnersCount > data->winners.size()) {
			pushText(
				Ui::Text::Bold(tr::lng_prizes_results_more(
					tr::now,
					lt_count,
					data->winnersCount - data->winners.size())),
				st::chatGiveawayNoCountriesTitleMargin);
		}
		pushText({ (data->credits && isSingleWinner)
			? tr::lng_prizes_credits_results_one(
				tr::now,
				lt_count,
				data->credits)
			: (data->credits && !isSingleWinner)
			? tr::lng_prizes_credits_results_all(
				tr::now,
				lt_count,
				data->credits)
			: data->unclaimedCount
			? tr::lng_prizes_results_some(tr::now)
			: isSingleWinner
			? tr::lng_prizes_results_one(tr::now)
			: tr::lng_prizes_results_all(tr::now)
		}, st::chatGiveawayEndDateMargin);
	};
}

} // namespace HistoryView
