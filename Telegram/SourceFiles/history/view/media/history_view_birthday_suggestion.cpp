/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_birthday_suggestion.h"

#include "boxes/star_gift_box.h"
#include "chat_helpers/stickers_lottie.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_birthday.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "data/data_star_gift.h"
#include "history/view/media/history_view_media_generic.h"
#include "history/view/media/history_view_premium_gift.h"
#include "history/view/media/history_view_unique_gift.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_credits_graphics.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/premium_stars_colored.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/rect.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_credits.h"

namespace HistoryView {

[[nodiscard]] auto GenerateSuggetsBirthdayMedia(
	not_null<Element*> parent,
	Element *replacing,
	Data::Birthday birthday)
-> Fn<void(
		not_null<MediaGeneric*>,
		Fn<void(std::unique_ptr<MediaGenericPart>)>)> {
	return [=](
			not_null<MediaGeneric*> media,
			Fn<void(std::unique_ptr<MediaGenericPart>)> push) {
		const auto session = &media->parent()->history()->session();
		const auto document = ChatHelpers::GenerateLocalTgsSticker(
			session,
			u"cake"_q);
		const auto sticker = [=] {
			using Tag = ChatHelpers::StickerLottieSize;
			return StickerInBubblePart::Data{
				.sticker = document,
				.size = st::birthdaySuggestStickerSize,
				.cacheTag = Tag::ChatIntroHelloSticker,
				.stopOnLastFrame = true,
			};
		};
		push(std::make_unique<StickerInBubblePart>(
			parent,
			replacing,
			sticker,
			st::birthdaySuggestStickerPadding));

		const auto from = media->parent()->data()->from();
		const auto isSelf = (from->id == from->session().userPeerId());
		const auto peer = isSelf ? media->parent()->history()->peer : from;
		push(std::make_unique<MediaGenericTextPart>(
			(isSelf
				? tr::lng_action_suggested_birthday_me
				: tr::lng_action_suggested_birthday)(
					tr::now,
					lt_user,
					TextWithEntities{ peer->shortName() },
					Ui::Text::WithEntities),
			st::birthdaySuggestTextPadding));

		push(std::make_unique<BirthdayTable>(
			birthday,
			(isSelf
				? st::birthdaySuggestTableLastPadding
				: st::birthdaySuggestTablePadding)));
		if (!isSelf) {
			auto link = std::make_shared<LambdaClickHandler>([=](
					ClickContext context) {
				Core::App().openInternalUrl(
					(u"internal:edit_birthday:suggestion_"_q
						+ QString::number(birthday.serialize())),
					context.other);
			});
			push(MakeGenericButtonPart(
				tr::lng_sticker_premium_view(tr::now),
				st::chatUniqueButtonPadding,
				[=] { parent->repaint(); },
				std::move(link)));
		}
	};
}

BirthdayTable::BirthdayTable(Data::Birthday birthday, QMargins margins)
: _margins(margins) {
	const auto push = [&](QString label, QString value) {
		_parts.push_back({
			.label = Ui::Text::String(st::defaultTextStyle, label),
			.value = Ui::Text::String(
				st::defaultTextStyle,
				Ui::Text::Bold(value)),
		});
	};
	push(tr::lng_date_input_day(tr::now), QString::number(birthday.day()));
	push(
		tr::lng_date_input_month(tr::now),
		Lang::Month(birthday.month())(tr::now));
	if (const auto year = birthday.year()) {
		push(tr::lng_date_input_year(tr::now), QString::number(year));
	}
}

void BirthdayTable::draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const {
	const auto top = _margins.top();
	const auto palette = &context.st->serviceTextPalette();
	const auto paint = [&](
			const Ui::Text::String &text,
			int left,
			int yskip = 0) {
		text.draw(p, {
			.position = { left, top + yskip},
			.outerWidth = outerWidth,
			.availableWidth = text.maxWidth(),
			.palette = palette,
			.spoiler = Ui::Text::DefaultSpoilerCache(),
			.now = context.now,
			.pausedEmoji = context.paused || On(PowerSaving::kEmojiChat),
			.pausedSpoiler = context.paused || On(PowerSaving::kChatSpoiler),
			.elisionLines = 1,
		});
	};

	p.setPen(context.st->msgServiceFg()->c);
	for (const auto &part : _parts) {
		p.setOpacity(0.7);
		paint(part.label, part.labelLeft);

		p.setOpacity(1.);
		paint(
			part.value,
			part.valueLeft,
			st::normalFont->height + st::birthdaySuggestTableSkip);
	}
}

TextState BirthdayTable::textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const {
	return {};
}

QSize BirthdayTable::countOptimalSize() {
	auto width = 0;
	for (const auto &part : _parts) {
		width += std::max(part.label.maxWidth(), part.value.maxWidth());
	}
	width += st::normalFont->spacew * (_parts.size() - 1);

	const auto height = st::normalFont->height * 2
		+ st::birthdaySuggestTableSkip;
	return {
		_margins.left() + width + _margins.right(),
		_margins.top() + height + _margins.bottom(),
	};
}

QSize BirthdayTable::countCurrentSize(int newWidth) {
	auto available = newWidth - _margins.left() - _margins.right();
	for (const auto &part : _parts) {
		available -= std::max(part.label.maxWidth(), part.value.maxWidth());
	}
	const auto skip = available / int(_parts.size() + 1);
	auto left = _margins.left() + skip;
	for (auto &part : _parts) {
		auto full = std::max(part.label.maxWidth(), part.value.maxWidth());
		part.labelLeft = left + (full - part.label.maxWidth()) / 2;
		part.valueLeft = left + (full - part.value.maxWidth()) / 2;
		left += full + skip;
	}
	return { newWidth, minHeight() };
}

} // namespace HistoryView
