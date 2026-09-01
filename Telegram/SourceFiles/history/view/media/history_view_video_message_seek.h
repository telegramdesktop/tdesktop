/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"

class Painter;

namespace Ui {
struct ChatPaintContext;
} // namespace Ui

namespace HistoryView {

class VideoMessageSeek final {
public:
	explicit VideoMessageSeek(Fn<void()> repaint);

	[[nodiscard]] float64 progress() const {
		return _progress;
	}
	void setProgress(float64 progress);
	void setDraggedProgress(float64 progress);
	void setGrabbed(bool grabbed);

	void paint(
		Painter &p,
		const Ui::ChatPaintContext &context,
		QRect rthumb,
		bool shown,
		bool seeking,
		float64 playback,
		bool inTTLViewer);

	[[nodiscard]] bool grabPoint(QRect rthumb, QPoint point) const;

	void unloadHeavyPart();

private:
	void validateShadow(QSize size) const;
	void paintShadow(Painter &p, QRect rthumb, float64 shown) const;

	const Fn<void()> _repaint;

	Ui::Animations::Simple _shownAnimation;
	Ui::Animations::Simple _grabAnimation;
	Ui::Animations::Simple _ghostAnimation;
	float64 _progress = 0.;
	float64 _ghostProgress = 0.;
	mutable QImage _shadow;
	bool _shown = false;

};

} // namespace HistoryView
