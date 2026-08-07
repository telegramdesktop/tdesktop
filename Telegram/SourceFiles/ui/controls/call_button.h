/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "ui/widgets/buttons.h"
#include "ui/effects/animations.h"

namespace style {
struct CallButton;
} // namespace style

namespace Ui {

class FlatLabel;

struct CallButtonColors {
	std::optional<QColor> bg;
	std::optional<QColor> ripple;
};

class CallButton final : public RippleButton {
public:
	CallButton(
		QWidget *parent,
		const style::CallButton &stFrom,
		const style::CallButton *stTo = nullptr);

	void setProgress(float64 progress);
	void setOuterValue(float64 value);
	void setText(rpl::producer<QString> text);
	void setColorOverrides(rpl::producer<CallButtonColors> &&colors);

	void setStyle(
		const style::CallButton &stFrom,
		const style::CallButton *stTo = nullptr);

	[[nodiscard]] not_null<CallButton*> addCornerButton(
		const style::CallButton &stFrom,
		const style::CallButton *stTo = nullptr);

private:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

	void init();
	QPoint iconPosition(not_null<const style::CallButton*> st) const;
	void mixIconMasks();

	not_null<const style::CallButton*> _stFrom;
	const style::CallButton *_stTo = nullptr;
	CallButton *_corner = nullptr;
	float64 _progress = 0.;

	object_ptr<FlatLabel> _label = { nullptr };

	std::optional<QColor> _bgOverride;
	std::optional<QColor> _rippleOverride;

	QImage _bgMask, _bg;
	QPixmap _bgFrom, _bgTo;
	QImage _iconMixedMask, _iconFrom, _iconTo, _iconMixed;

	float64 _outerValue = 0.;
	Animations::Simple _outerAnimation;

};

} // namespace Ui
