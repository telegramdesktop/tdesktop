/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "styles/style_widgets.h"

namespace Ui {

class RoundCheckbox {
public:
	RoundCheckbox(const style::RoundCheckbox &st, Fn<void()> updateCallback);

	void paint(Painter &p, int x, int y, int outerWidth, float64 masterScale = 1.) const;

	void setDisplayInactive(bool displayInactive);
	bool checked() const {
		return _checked;
	}
	void setChecked(
		bool newChecked,
		anim::type animated = anim::type::normal);

	void invalidateCache();

private:
	void prepareInactiveCache();

	const style::RoundCheckbox &_st;
	Fn<void()> _updateCallback;

	bool _checked = false;
	Ui::Animations::Simple _checkedProgress;

	bool _displayInactive = false;
	QPixmap _inactiveCacheBg, _inactiveCacheFg;

};

class RoundImageCheckbox {
public:
	using PaintRoundImage = Fn<void(Painter &p, int x, int y, int outerWidth, int size)>;
	RoundImageCheckbox(const style::RoundImageCheckbox &st, Fn<void()> updateCallback, PaintRoundImage &&paintRoundImage);

	void paint(Painter &p, int x, int y, int outerWidth) const;
	float64 checkedAnimationRatio() const;

	void setColorOverride(std::optional<QBrush> fg);

	bool checked() const {
		return _check.checked();
	}
	void setChecked(
		bool newChecked,
		anim::type animated = anim::type::normal);

	void invalidateCache() {
		_check.invalidateCache();
	}

private:
	void prepareWideCache();

	const style::RoundImageCheckbox &_st;
	Fn<void()> _updateCallback;
	PaintRoundImage _paintRoundImage;

	QPixmap _wideCache;
	Ui::Animations::Simple _selection;

	RoundCheckbox _check;

	std::optional<QBrush> _fgOverride;

};

} // namespace Ui
