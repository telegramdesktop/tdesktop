/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/style/style_core_types.h"
#include "ui/passcode_strength.h"
#include "ui/rp_widget.h"

namespace style {
struct PasscodeStrengthMeter;
} // namespace style

namespace Ui {

class FlatLabel;

class PasscodeStrengthMeter final : public RpWidget {
public:
	PasscodeStrengthMeter(
		QWidget *parent,
		const style::PasscodeStrengthMeter &st);

	void showCandidate(const QString &candidate);

protected:
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	const style::PasscodeStrengthMeter &_st;
	not_null<FlatLabel*> _label;
	PasscodeStrength _strength;
	rpl::lifetime _adviceLifetime;
	bool _empty = true;
	bool _inResize = false;

};

} // namespace Ui
