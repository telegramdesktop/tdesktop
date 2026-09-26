/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_compose_ai_tooltip.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_keys.h"
#include "ui/widgets/tooltip.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_widgets.h"

namespace HistoryView::Controls {

ComposeTooltipManager::ComposeTooltipManager(
	not_null<QWidget*> parent,
	not_null<Ui::RpWidget*> button,
	rpl::producer<TextWithEntities> text,
	base::const_string prefKey,
	Fn<int()> widthProvider)
: _button(button)
, _prefKey(prefKey)
, _widthProvider(std::move(widthProvider)) {
	_tooltip.reset(Ui::CreateChild<Ui::ImportantTooltip>(
		parent,
		Ui::MakeTooltipWithClose(
			parent,
			std::move(text),
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

void ComposeTooltipManager::hideAndRemember() {
	if (!Core::App().settings().readPref<bool>(_prefKey)) {
		Core::App().settings().writePref<bool>(_prefKey, true);
	}
	_shown = false;
	_tooltip->toggleAnimated(false);
}

void ComposeTooltipManager::updateVisibility(bool buttonShown) {
	const auto showTooltip = buttonShown
		&& !Core::App().settings().readPref<bool>(_prefKey);
	if (showTooltip) {
		updateGeometry();
	}
	if ((_shown != showTooltip)
		|| (showTooltip && _tooltip->isHidden())) {
		_shown = showTooltip;
		_tooltip->toggleAnimated(showTooltip);
	}
}

void ComposeTooltipManager::updateGeometry() {
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

void ComposeTooltipManager::raise() {
	_tooltip->raise();
}

} // namespace HistoryView::Controls
