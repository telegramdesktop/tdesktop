/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

#include "ui/effects/animations.h"

namespace UserpicBuilder {

class ColorsPalette final : public Ui::RpWidget {
public:
	ColorsPalette(not_null<Ui::RpWidget*> parent);

	[[nodiscard]] rpl::producer<QGradientStops> stopsValue();

protected:
	void resizeEvent(QResizeEvent *event) override;
	int resizeGetHeight(int newWidth) final override;

private:
	class CircleButton;
	void rebuildButtons();

	std::vector<base::unique_qptr<CircleButton>> _buttons;
	Ui::Animations::Simple _animation;
	rpl::variable<int> _currentIndex = 0;

};

} // namespace UserpicBuilder
