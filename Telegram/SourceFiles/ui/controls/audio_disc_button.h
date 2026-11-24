/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/widgets/buttons.h"

namespace Ui {

class AudioDiscButton : public RippleButton {
public:
	enum class FallbackRing {
		Full,
		SpinningArc,
	};

	AudioDiscButton(QWidget *parent, FallbackRing fallbackRing);
	~AudioDiscButton();

	void setCover(const QImage &cover);
	void setActive(bool active);

protected:
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	[[nodiscard]] float64 angle() const;

	const FallbackRing _fallbackRing;
	QImage _cover;
	Animations::Basic _spin;
	Animations::Simple _ringAnimation;
	crl::time _spinStarted = 0;
	float64 _spinBase = 0.;
	bool _active = false;

};

} // namespace Ui
