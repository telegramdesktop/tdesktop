/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/radial_animation.h"
#include "ui/effects/animations.h"

namespace Ui {

class UploadProgressOverlay final {
public:
	struct PaintArgs {
		int lineWidth = 3;
		int margin = 8;
		style::color progressFg;
		style::color overlayFg;
		const style::icon *cancelIcon = nullptr;
		float64 roundRadius = 0.;
	};

	UploadProgressOverlay(
		not_null<QWidget*> parent,
		Fn<void()> update);

	void start();
	void stop(Fn<void()> done = nullptr);
	void fail(Fn<void()> done = nullptr);
	void setProgress(float64 progress);
	void setOver(bool over);

	[[nodiscard]] bool shown() const;
	[[nodiscard]] bool uploading() const;

	void paint(QPainter &p, QRect rect, const PaintArgs &args);

private:
	void radialAnimationCallback(crl::time now);
	void finish(Fn<void()> done);
	void proceedWithStop();

	const not_null<QWidget*> _parent;
	const Fn<void()> _update;
	RadialAnimation _radial;
	Animations::Simple _cancelShown;
	Animations::Simple _hiding;
	Fn<void()> _hidingDone;
	bool _uploading = false;
	bool _stopping = false;
	bool _over = false;
	float64 _progress = 0.;
	crl::time _startedAt = 0;

};

} // namespace Ui
