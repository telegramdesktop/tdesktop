/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/warning_tooltip.h"

#include "base/timer_rpl.h"
#include "ui/effects/animation_value.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/tooltip.h"
#include "ui/ui_utility.h"

#include "styles/style_layers.h"
#include "styles/style_widgets.h"

namespace Ui {
namespace {

constexpr auto kDefaultWarningTooltipDuration = crl::time(2000);

} // namespace

WarningTooltip::WarningTooltip() = default;

WarningTooltip::~WarningTooltip() {
	hide(anim::type::instant);
}

void WarningTooltip::show(Args &&args) {
	const auto duration = args.duration
		? args.duration
		: kDefaultWarningTooltipDuration;
	const auto maxWidth = args.maxWidth
		? args.maxWidth
		: st::boxWideWidth;
	const auto &tooltipSt = args.st
		? *args.st
		: st::defaultImportantTooltip;
	const auto &labelSt = args.labelSt
		? *args.labelSt
		: st::defaultImportantTooltipLabel;

	const auto parent = args.parent.get();
	const auto target = args.target;
	const auto side = args.side;
	auto countPosition = std::move(args.countPosition);
	const auto tooltip = CreateChild<ImportantTooltip>(
		parent,
		MakeNiceTooltipLabel(
			parent,
			std::move(args.text),
			maxWidth,
			labelSt),
		tooltipSt);
	tooltip->toggleFast(false);

	const auto update = [=] {
		tooltip->pointAt(
			MapFrom(parent, target, target->rect()),
			side,
			countPosition);
	};
	parent->widthValue(
	) | rpl::on_next(update, tooltip->lifetime());
	update();
	tooltip->toggleAnimated(true);

	tooltip->shownValue(
	) | rpl::filter(
		!rpl::mappers::_1
	) | rpl::on_next([=] {
		crl::on_main(tooltip, [=] {
			if (tooltip->isHidden()) {
				delete tooltip;
			}
		});
	}, tooltip->lifetime());

	base::timer_once(
		duration
	) | rpl::on_next([=] {
		tooltip->toggleAnimated(false);
	}, tooltip->lifetime());

	if (_current && _current->tooltip) {
		_current->tooltip->toggleAnimated(false);
	}
	auto entry = std::make_unique<Entry>();
	entry->tooltip = tooltip;
	_current = std::move(entry);
}

void WarningTooltip::hide(anim::type animated) {
	if (!_current) {
		return;
	}
	const auto raw = _current->tooltip.data();
	_current.reset();
	if (!raw) {
		return;
	}
	if (animated == anim::type::instant) {
		delete raw;
	} else {
		raw->toggleAnimated(false);
	}
}

} // namespace Ui
