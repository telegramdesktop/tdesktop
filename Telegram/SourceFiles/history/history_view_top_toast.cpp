/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_view_top_toast.h"

#include "ui/rect.h"
#include "ui/toast/toast.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/tooltip.h"
#include "core/ui_integration.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

namespace HistoryView {

namespace {

constexpr auto kAnchoredTooltipDuration = 4 * crl::time(1000);

[[nodiscard]] crl::time CountToastDuration(const TextWithEntities &text) {
	return std::clamp(
		crl::time(1000) * int(text.text.size()) / 14,
		crl::time(1000) * 5,
		crl::time(1000) * 8);
}

} // namespace

InfoTooltip::InfoTooltip() = default;

void InfoTooltip::show(
		not_null<QWidget*> parent,
		not_null<Main::Session*> session,
		const TextWithEntities &text,
		Fn<void()> hiddenCallback) {
	hide(anim::type::normal);
	_topToast = Ui::Toast::Show(parent, Ui::Toast::Config{
		.text = text,
		.textContext = Core::TextContext({ .session = session }),
		.icon = &st::historyInfoToastIcon,
		.st = &st::historyInfoToast,
		.attach = RectPart::Top,
		.duration = CountToastDuration(text),
	});
	if (const auto strong = _topToast.get()) {
		if (hiddenCallback) {
			QObject::connect(
				strong->widget(),
				&QObject::destroyed,
				hiddenCallback);
		}
	} else if (hiddenCallback) {
		hiddenCallback();
	}
}

void InfoTooltip::hide(anim::type animated) {
	if (const auto strong = _topToast.get()) {
		if (animated == anim::type::normal) {
			strong->hideAnimated();
		} else {
			strong->hide();
		}
	}
}

void AnchoredTooltip::show(
		not_null<QWidget*> scroll,
		rpl::producer<> scrolls,
		QRect globalArea,
		TextWithEntities text) {
	if (globalArea.isEmpty()) {
		globalArea = QRect(QCursor::pos(), Size(1));
	}
	_tooltip = base::make_unique_q<Ui::ImportantTooltip>(
		scroll,
		Ui::MakeNiceTooltipLabel(
			scroll,
			rpl::single(std::move(text)),
			st::boxWideWidth,
			st::defaultImportantTooltipLabel),
		st::defaultImportantTooltip);
	const auto raw = _tooltip.get();
	raw->toggleFast(false);

	const auto local = QRect(
		scroll->mapFromGlobal(globalArea.topLeft()),
		globalArea.size());
	raw->pointAt(local, RectPart::Top | RectPart::Center);
	raw->toggleAnimated(true);
	raw->hideAfter(kAnchoredTooltipDuration);

	std::move(scrolls) | rpl::on_next([=] {
		raw->toggleAnimated(false);
	}, raw->lifetime());
}

void AnchoredTooltip::hide() {
	if (const auto raw = _tooltip.get()) {
		raw->toggleAnimated(false);
	}
}

} // namespace HistoryView
