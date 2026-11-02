/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

namespace Editor {

class EnhanceIndicator final : public Ui::RpWidget {
public:
	explicit EnhanceIndicator(not_null<Ui::RpWidget*> parent);

	void setValue(int value);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void hideValue();

	base::Timer _hideTimer;
	Ui::Animations::Simple _fade;
	int _value = 0;
	bool _shown = false;

};

} // namespace Editor
