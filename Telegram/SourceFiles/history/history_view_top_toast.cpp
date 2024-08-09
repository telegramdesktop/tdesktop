/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_view_top_toast.h"

#include "ui/toast/toast.h"
#include "core/ui_integration.h"
#include "styles/style_chat.h"

namespace HistoryView {

namespace {

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
	const auto context = [=](not_null<QWidget*> toast) {
		return Core::MarkedTextContext{
			.session = session,
			.customEmojiRepaint = [=] { toast->update(); },
		};
	};
	hide(anim::type::normal);
	_topToast = Ui::Toast::Show(parent, Ui::Toast::Config{
		.text = text,
		.textContext = context,
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

} // namespace HistoryView
