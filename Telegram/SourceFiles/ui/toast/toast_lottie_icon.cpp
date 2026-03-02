/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/toast/toast_lottie_icon.h"

#include "lottie/lottie_icon.h"
#include "ui/rp_widget.h"
#include "styles/style_widgets.h"

namespace Ui {

void AddLottieToToast(
		not_null<QWidget*> widget,
		const style::Toast &st,
		QSize iconSize,
		const QString &name) {
	const auto lottieWidget = CreateChild<RpWidget>(widget);
	struct State {
		std::unique_ptr<Lottie::Icon> lottieIcon;
	};
	const auto state = lottieWidget->lifetime().make_state<State>();
	state->lottieIcon = Lottie::MakeIcon({
		.name = name,
		.sizeOverride = iconSize,
	});
	const auto icon = state->lottieIcon.get();
	lottieWidget->resize(iconSize);
	lottieWidget->move(st.iconPosition);
	lottieWidget->show();
	lottieWidget->raise();
	icon->animate(
		[=] { lottieWidget->update(); },
		0,
		icon->framesCount() - 1);
	lottieWidget->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(lottieWidget);
		icon->paint(p, 0, 0);
	}, lottieWidget->lifetime());
}

} // namespace Ui
