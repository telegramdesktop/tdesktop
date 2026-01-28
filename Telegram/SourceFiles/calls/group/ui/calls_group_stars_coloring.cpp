/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/ui/calls_group_stars_coloring.h"

#include "base/object_ptr.h"
#include "lang/lang_keys.h"
#include "payments/ui/payments_reaction_box.h"
#include "ui/widgets/labels.h"
#include "ui/emoji_config.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "styles/style_credits.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"

namespace Calls::Group::Ui {

StarsColoring StarsColoringForCount(
		const std::vector<StarsColoring> &colorings,
		int stars) {
	for (auto i = begin(colorings), e = end(colorings); i != e; ++i) {
		if (i->fromStars > stars) {
			Assert(i != begin(colorings));
			return *(std::prev(i));
		}
	}
	return colorings.back();
}

int StarsRequiredForMessage(
		const std::vector<StarsColoring> &colorings,
		const TextWithTags &text) {
	Expects(!colorings.empty());

	auto emojis = 0;
	auto outLength = 0;
	auto view = QStringView(text.text);
	const auto length = int(view.size());
	while (!view.isEmpty()) {
		if (Emoji::Find(view, &outLength)) {
			view = view.mid(outLength);
			++emojis;
		} else {
			view = view.mid(1);
		}
	}
	for (const auto &entry : colorings) {
		if (emojis <= entry.emojiLimit && length <= entry.charactersMax) {
			return entry.fromStars;
		}
	}
	return colorings.back().fromStars + 1;
}

object_ptr<RpWidget> VideoStreamStarsLevel(
		not_null<RpWidget*> box,
		const std::vector<StarsColoring> &colorings,
		rpl::producer<int> starsValue) {
	struct State {
		rpl::variable<int> stars;
		rpl::variable<StarsColoring> coloring;
	};
	const auto state = box->lifetime().make_state<State>();
	state->stars = std::move(starsValue);
	state->coloring = state->stars.value(
	) | rpl::map([=](int stars) {
		return StarsColoringForCount(colorings, stars);
	});

	auto pinTitle = state->coloring.value(
	) | rpl::map([=](const StarsColoring &value) {
		const auto seconds = value.secondsPin;
		return (seconds >= 3600)
			? tr::lng_hours_tiny(tr::now, lt_count, seconds / 3600)
			: (seconds >= 60)
			? tr::lng_minutes_tiny(tr::now, lt_count, seconds / 60)
			: tr::lng_seconds_tiny(tr::now, lt_count, seconds);
	});
	auto limitTitle = state->coloring.value(
	) | rpl::map([=](const StarsColoring &value) {
		return QString::number(value.charactersMax);
	});
	auto limitSubtext = state->coloring.value(
	) | rpl::map([=](const StarsColoring &value) {
		return tr::lng_paid_comment_limit_about(
			tr::now,
			lt_count,
			value.charactersMax);
	});
	auto emojiTitle = state->coloring.value(
	) | rpl::map([=](const StarsColoring &value) {
		return QString::number(value.emojiLimit);
	});
	auto emojiSubtext = state->coloring.value(
	) | rpl::map([=](const StarsColoring &value) {
		return tr::lng_paid_comment_emoji_about(
			tr::now,
			lt_count,
			value.emojiLimit);
	});
	return MakeStarSelectInfoBlocks(box, {
		{
			.title = std::move(pinTitle) | rpl::map(tr::marked),
			.subtext = tr::lng_paid_comment_pin_about(),
		},
		{
			.title = std::move(limitTitle) | rpl::map(tr::marked),
			.subtext = std::move(limitSubtext),
		},
		{
			.title = std::move(emojiTitle) | rpl::map(tr::marked),
			.subtext = std::move(emojiSubtext),
		},
	}, {}, true);
}

} // namespace Calls::Group::Ui
