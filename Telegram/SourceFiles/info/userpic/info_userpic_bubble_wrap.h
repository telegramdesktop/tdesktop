/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui {

class ChatStyle;
class VerticalLayout;

class BubbleWrap final : public Ui::RpWidget {
public:
	using Ui::RpWidget::RpWidget;

	[[nodiscard]] QRect innerRect() const;
	[[nodiscard]] rpl::producer<QRect> innerRectValue() const;

};

not_null<BubbleWrap*> AddBubbleWrap(
	not_null<Ui::VerticalLayout*> container,
	const QSize &size,
	Fn<not_null<const Ui::ChatStyle*>()> chatStyle);

} // namespace Ui
