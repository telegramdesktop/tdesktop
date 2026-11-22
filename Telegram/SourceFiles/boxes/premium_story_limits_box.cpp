/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/premium_story_limits_box.h"

#include "boxes/premium_preview_box.h"
#include "data/data_premium_limits.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/effects/premium_bubble.h"
#include "ui/effects/premium_graphics.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/gradient_round_button.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"

#include "styles/style_layers.h"
#include "styles/style_premium.h"

namespace {

using Type = Api::CanSendStory::Type;

struct Limit {
	int free = 0;
	int premium = 0;
	tr::phrase<lngtag_count> text;
};

[[nodiscard]] std::optional<Limit> PostingLimit(
		not_null<Main::Session*> session,
		Type type) {
	const auto limits = Data::PremiumLimits(session);
	switch (type) {
	case Type::CountLimit:
		return Limit{
			limits.storiesExpiringDefault(),
			limits.storiesExpiringPremium(),
			tr::lng_story_limit_count_premium,
		};
	case Type::WeeklyLimit:
		return Limit{
			limits.storiesSentWeeklyDefault(),
			limits.storiesSentWeeklyPremium(),
			tr::lng_story_limit_weekly_premium,
		};
	case Type::MonthlyLimit:
		return Limit{
			limits.storiesSentMonthlyDefault(),
			limits.storiesSentMonthlyPremium(),
			tr::lng_story_limit_monthly_premium,
		};
	default: return std::nullopt;
	}
}

} // namespace

void StoryPostingLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		Type type) {
	const auto session = &controller->session();
	const auto limit = PostingLimit(session, type);
	Expects(limit.has_value());

	const auto premium = session->premium();
	const auto increase = !premium && (limit->premium > limit->free);
	const auto current = premium ? limit->premium : limit->free;
	const auto ratio = increase
		? std::clamp(float64(current) / limit->premium, 0.2, 0.8)
		: Ui::Premium::kLimitRowRatio;
	using BubbleType = Ui::Premium::BubbleType;

	box->setWidth(st::boxWideWidth);
	box->setStyle(st::storyPostingLimitBox);
	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });
	box->addSkip(st::premiumInfographicPadding.top());

	const auto layout = box->verticalLayout();
	Ui::Premium::AddBubbleRow(
		layout,
		st::defaultPremiumBubble,
		Ui::BoxShowFinishes(box),
		rpl::single(
			Ui::Premium::BubbleRowState{
				.counter = current,
				.ratio = ratio,
			}),
		increase ? BubbleType::Premium : BubbleType::NoPremium,
		{},
		&st::premiumIconStories,
		st::boxRowPadding);
	if (increase) {
		box->addSkip(st::premiumLineTextSkip);
		Ui::Premium::AddLimitRow(
			layout,
			st::defaultPremiumLimits,
			limit->premium,
			std::nullopt,
			0,
			ratio);
	}
	box->addSkip(st::premiumInfographicPadding.bottom());
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_story_limit_title(),
			st::boostCenteredTitle),
		st::boxRowPadding,
		style::al_top);
	box->addSkip(st::boostTextSkip);

	auto text = limit->text(
		lt_count,
		rpl::single(float64(current)),
		tr::rich);
	if (increase) {
		text = rpl::combine(
			std::move(text),
			tr::lng_story_limit_increase(
				lt_limit,
				rpl::single(
					tr::marked(
						QString::number(
							limit->premium))),
				tr::rich),
			[](
				TextWithEntities first,
				const TextWithEntities &second) {
				return first.append(QChar('\n'))
					.append(second);
			});
	}
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			std::move(text),
			st::boostText),
		st::boxRowPadding,
		style::al_top);

	if (increase) {
		auto button
			= CreateUnlockButton(box, tr::lng_limits_increase());
		button->setFullRadius(true);
		button->setClickedCallback([=] {
			box->closeBox();
			ShowPremiumPreviewBox(
				controller,
				PremiumFeature::Stories);
		});
		box->addButton(std::move(button));
	} else {
		box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
	}
}
