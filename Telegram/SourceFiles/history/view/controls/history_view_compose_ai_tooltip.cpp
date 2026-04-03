/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_compose_ai_tooltip.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "history/view/controls/history_view_compose_ai_button.h"
#include "lang/lang_keys.h"
#include "ui/widgets/tooltip.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_widgets.h"

namespace HistoryView::Controls {
namespace {

constexpr auto kAiComposeTooltipHiddenPref = "ai_compose_tooltip_hidden"_cs;

} // namespace

AiTooltipManager::AiTooltipManager(
	not_null<QWidget*> parent,
	not_null<ComposeAiButton*> button,
	Fn<int()> widthProvider)
: _button(button)
, _widthProvider(std::move(widthProvider)) {
	_tooltip.reset(Ui::CreateChild<Ui::ImportantTooltip>(
		parent,
		Ui::MakeTooltipWithClose(
			parent,
			tr::lng_ai_compose_tooltip(tr::rich),
			st::historyMessagesTTLLabel.minWidth,
			st::ttlMediaImportantTooltipLabel,
			st::importantTooltipHide,
			st::defaultImportantTooltip.padding,
			[=] { hideAndRemember(); }),
		st::historyRecordTooltip));
	_tooltip->toggleFast(false);
	_button->geometryValue(
	) | rpl::on_next([=](const QRect &geometry) {
		if (!geometry.isEmpty()) {
			updateGeometry();
		}
	}, _tooltip->lifetime());
}

void AiTooltipManager::hideAndRemember() {
	if (!Core::App().settings().readPref<bool>(
			kAiComposeTooltipHiddenPref)) {
		Core::App().settings().writePref<bool>(
			kAiComposeTooltipHiddenPref,
			true);
	}
	_shown = false;
	_tooltip->toggleAnimated(false);
}

void AiTooltipManager::updateVisibility(bool buttonShown) {
	const auto showTooltip = buttonShown
		&& !Core::App().settings().readPref<bool>(
			kAiComposeTooltipHiddenPref);
	if (showTooltip) {
		updateGeometry();
	}
	if ((_shown != showTooltip)
		|| (showTooltip && _tooltip->isHidden())) {
		_shown = showTooltip;
		_tooltip->toggleAnimated(showTooltip);
	}
}

void AiTooltipManager::updateGeometry() {
	if (_button->isHidden()) {
		return;
	}
	const auto geometry = _button->geometry();
	const auto maxWidth = _widthProvider();
	const auto countPosition = [=](QSize size) {
		const auto left = geometry.x()
			+ geometry.width()
			- size.width();
		return QPoint(
			std::max(std::min(left, maxWidth - size.width()), 0),
			geometry.y() - size.height() - st::historyAiComposeTooltipSkip);
	};
	_tooltip->pointAt(geometry, RectPart::Top, countPosition);
}

void AiTooltipManager::raise() {
	_tooltip->raise();
}

} // namespace HistoryView::Controls
