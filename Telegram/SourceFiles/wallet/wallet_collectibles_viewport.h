/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/wrap/slide_wrap.h"
#include "ui/rp_widget.h"

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Wallet {

// WHY: sibling heights change the clip during a switch, but the squares
// must stay at the grid's full-skip origin until that clip removes them.
class CollectiblesViewport final : public Ui::RpWidget {
public:
	CollectiblesViewport(
		QWidget *parent,
		not_null<Ui::RpWidget*> above,
		not_null<Ui::SlideWrap<>*> skip);

	[[nodiscard]] not_null<Ui::VerticalLayout*> page() const;

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

private:
	void placePage();

	const not_null<Ui::RpWidget*> _above;
	const not_null<Ui::SlideWrap<>*> _skip;
	const not_null<Ui::VerticalLayout*> _page;

};

} // namespace Wallet
