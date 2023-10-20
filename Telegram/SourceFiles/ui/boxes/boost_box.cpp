/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/boost_box.h"

#include "lang/lang_keys.h"
#include "ui/effects/fireworks_animation.h"
#include "ui/effects/premium_graphics.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"

namespace Ui {
namespace {

void StartFireworks(not_null<QWidget*> parent) {
	const auto result = Ui::CreateChild<RpWidget>(parent.get());
	result->setAttribute(Qt::WA_TransparentForMouseEvents);
	result->setGeometry(parent->rect());
	result->show();

	auto &lifetime = result->lifetime();
	const auto animation = lifetime.make_state<FireworksAnimation>([=] {
		result->update();
	});
	result->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(result);
		if (!animation->paint(p, result->rect())) {
			crl::on_main(result, [=] { delete result; });
		}
	}, lifetime);
}

} // namespace

void BoostBox(
		not_null<GenericBox*> box,
		BoostBoxData data,
		Fn<void(Fn<void(bool)>)> boost) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::boostBox);

	const auto full = !data.boost.nextLevelBoosts;

	struct State {
		rpl::variable<bool> you = false;
		bool submitted = false;
	};
	const auto state = box->lifetime().make_state<State>(State{
		.you = data.boost.mine,
	});

	FillBoostLimit(
		BoxShowFinishes(box),
		state->you.value(),
		box->verticalLayout(),
		data,
		st::boxRowPadding);

	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

	const auto name = data.name;
	auto title = state->you.value() | rpl::map([=](bool your) {
		return your
			? tr::lng_boost_channel_you_title(
				lt_channel,
				rpl::single(data.name))
			: full
			? tr::lng_boost_channel_title_max()
			: !data.boost.level
			? tr::lng_boost_channel_title_first()
			: tr::lng_boost_channel_title_more();
	}) | rpl::flatten_latest();
	auto text = state->you.value() | rpl::map([=](bool your) {
		const auto bold = Ui::Text::Bold(data.name);
		const auto now = data.boost.boosts + (your ? 1 : 0);
		const auto left = (data.boost.nextLevelBoosts > now)
			? (data.boost.nextLevelBoosts - now)
			: 0;
		auto post = tr::lng_boost_channel_post_stories(
			lt_count,
			rpl::single(float64(data.boost.level + 1)),
			Ui::Text::RichLangValue);
		return (your || full)
			? ((!full && left > 0)
				? (!data.boost.level
					? tr::lng_boost_channel_you_first(
						lt_count,
						rpl::single(float64(left)),
						Ui::Text::RichLangValue)
					: tr::lng_boost_channel_you_more(
						lt_count,
						rpl::single(float64(left)),
						lt_post,
						std::move(post),
						Ui::Text::RichLangValue))
				: (!data.boost.level
					? tr::lng_boost_channel_reached_first(
						Ui::Text::RichLangValue)
					: tr::lng_boost_channel_reached_more(
						lt_count,
						rpl::single(float64(data.boost.level + 1)),
						lt_post,
						std::move(post),
						Ui::Text::RichLangValue)))
			: !data.boost.level
			? tr::lng_boost_channel_needs_first(
				lt_count,
				rpl::single(float64(left)),
				lt_channel,
				rpl::single(bold),
				Ui::Text::RichLangValue)
			: tr::lng_boost_channel_needs_more(
				lt_count,
				rpl::single(float64(left)),
				lt_channel,
				rpl::single(bold),
				lt_post,
				std::move(post),
				Ui::Text::RichLangValue);
	}) | rpl::flatten_latest();
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			std::move(title),
			st::boostTitle),
		st::boxRowPadding + QMargins(0, st::boostTitleSkip, 0, 0));
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			std::move(text),
			st::boostText),
		(st::boxRowPadding
			+ QMargins(0, st::boostTextSkip, 0, st::boostBottomSkip)));

	auto submit = full
		? (tr::lng_box_ok() | rpl::type_erased())
		: state->you.value(
		) | rpl::map([](bool mine) {
			return mine ? tr::lng_box_ok() : tr::lng_boost_channel_button();
		}) | rpl::flatten_latest();
	const auto button = box->addButton(rpl::duplicate(submit), [=] {
		if (state->submitted) {
			return;
		} else if (!full && !state->you.current()) {
			state->submitted = true;
			boost(crl::guard(box, [=](bool success) {
				state->submitted = false;
				if (success) {
					StartFireworks(box->parentWidget());
					state->you = true;
				}
			}));
		} else {
			box->closeBox();
		}
	});
	rpl::combine(
		std::move(submit),
		box->widthValue()
	) | rpl::start_with_next([=](const QString &, int width) {
		const auto &padding = st::boostBox.buttonPadding;
		button->resizeToWidth(width
			- padding.left()
			- padding.right());
		button->moveToLeft(padding.left(), button->y());
	}, button->lifetime());
}

void FillBoostLimit(
		rpl::producer<> showFinished,
		rpl::producer<bool> you,
		not_null<VerticalLayout*> container,
		BoostBoxData data,
		style::margins limitLinePadding) {
	const auto full = !data.boost.nextLevelBoosts;

	if (data.boost.mine && data.boost.boosts > 0) {
		--data.boost.boosts;
	}

	if (full) {
		data.boost.nextLevelBoosts = data.boost.boosts
			+ (data.boost.mine ? 1 : 0);
		data.boost.thisLevelBoosts = 0;
		if (data.boost.level > 0) {
			--data.boost.level;
		}
	} else if (data.boost.mine
			&& data.boost.level > 0
			&& data.boost.boosts < data.boost.thisLevelBoosts) {
		--data.boost.level;
		data.boost.nextLevelBoosts = data.boost.thisLevelBoosts;
		data.boost.thisLevelBoosts = 0;
	}

	const auto addSkip = [&](int skip) {
		container->add(object_ptr<Ui::FixedHeightWidget>(container, skip));
	};

	addSkip(st::boostSkipTop);

	const auto levelWidth = [&](int add) {
		return st::normalFont->width(
			tr::lng_boost_level(tr::now, lt_count, data.boost.level + add));
	};
	const auto paddings = 2 * st::premiumLineTextSkip;
	const auto labelLeftWidth = paddings + levelWidth(0);
	const auto labelRightWidth = paddings + levelWidth(1);
	const auto ratio = [=](int boosts) {
		const auto min = std::min(
			data.boost.boosts,
			data.boost.thisLevelBoosts);
		const auto max = std::max({
			data.boost.boosts,
			data.boost.nextLevelBoosts,
			1,
		});
		Assert(boosts >= min && boosts <= max);
		const auto count = (max - min);
		const auto index = (boosts - min);
		if (!index) {
			return 0.;
		} else if (index == count) {
			return 1.;
		} else if (count == 2) {
			return 0.5;
		}
		const auto available = st::boxWideWidth
			- st::boxPadding.left()
			- st::boxPadding.right();
		const auto average = available / float64(count);
		const auto first = std::max(average, labelLeftWidth * 1.);
		const auto last = std::max(average, labelRightWidth * 1.);
		const auto other = (available - first - last) / (count - 2);
		return (first + (index - 1) * other) / available;
	};

	const auto min = std::min(data.boost.boosts, data.boost.thisLevelBoosts);
	const auto now = data.boost.boosts;
	const auto max = (data.boost.nextLevelBoosts > min)
		? (data.boost.nextLevelBoosts)
		: (data.boost.boosts > 0)
		? data.boost.boosts
		: 1;
	auto bubbleRowState = (
		std::move(you)
	) | rpl::map([=](bool mine) {
		const auto index = mine ? (now + 1) : now;
		return Premium::BubbleRowState{
			.counter = index,
			.ratio = ratio(index),
			.dynamic = true,
		};
	});
	Premium::AddBubbleRow(
		container,
		st::boostBubble,
		std::move(showFinished),
		rpl::duplicate(bubbleRowState),
		max,
		true,
		nullptr,
		&st::premiumIconBoost,
		limitLinePadding);
	addSkip(st::premiumLineTextSkip);

	const auto level = [](int level) {
		return tr::lng_boost_level(tr::now, lt_count, level);
	};
	auto ratioValue = std::move(
		bubbleRowState
	) | rpl::map([](const Premium::BubbleRowState &state) {
		return state.ratio;
	});
	Premium::AddLimitRow(
		container,
		st::boostLimits,
		Premium::LimitRowLabels{
			.leftLabel = level(data.boost.level),
			.rightLabel = level(data.boost.level + 1),
			.dynamic = true,
		},
		std::move(ratioValue),
		limitLinePadding);
}

} // namespace Ui
