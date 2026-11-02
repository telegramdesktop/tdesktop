/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/tooltip.h"

namespace Editor {

class VideoLayerButton final
	: public Ui::RippleButton
	, public Ui::AbstractTooltipShower {
public:
	explicit VideoLayerButton(QWidget *parent);
	~VideoLayerButton();

	void setLocked(bool locked, anim::type animated = anim::type::normal);
	[[nodiscard]] bool locked() const;

	QString tooltipText() const override;
	QPoint tooltipPos() const override;
	bool tooltipWindowActive() const override;

protected:
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	bool _locked = true;
	Ui::Animations::Simple _activeAnimation;

};

} // namespace Editor
