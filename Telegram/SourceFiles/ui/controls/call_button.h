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
	void setLabelShown(bool shown);
	[[nodiscard]] bool textFits() const;
	[[nodiscard]] rpl::producer<bool> textFitsValue() const;
	[[nodiscard]] rpl::producer<QString> textValue() const;
	void setColorOverrides(rpl::producer<CallButtonColors> &&colors);

	void setStyle(
		const style::CallButton &stFrom,
		const style::CallButton *stTo = nullptr);

	[[nodiscard]] not_null<CallButton*> addCornerButton(
		const style::CallButton &stFrom,
		const style::CallButton *stTo = nullptr);

private:
	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

	void init();
	void refreshLabel();
	void refreshLabelShown();
	[[nodiscard]] bool inBackground(QPoint point) const;
	void refreshOverState(QPoint point);
	QPoint iconPosition(not_null<const style::CallButton*> st) const;
	void mixIconMasks();

	not_null<const style::CallButton*> _stFrom;
	const style::CallButton *_stTo = nullptr;
	CallButton *_corner = nullptr;
	float64 _progress = 0.;

	rpl::variable<QString> _text;
	rpl::variable<bool> _textFits = true;
	object_ptr<FlatLabel> _label = { nullptr };
	bool _labelShown = true;

	std::optional<QColor> _bgOverride;
	std::optional<QColor> _rippleOverride;

	QImage _bgMask, _bg;
	QPixmap _bgFrom, _bgTo;
	QImage _iconMixedMask, _iconFrom, _iconTo, _iconMixed;

	float64 _outerValue = 0.;
	Animations::Simple _outerAnimation;

};

} // namespace Ui
