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
	void toggleShown(bool shown);

	void paint(
		Painter &p,
		const Ui::ChatPaintContext &context,
		QRect rthumb,
		bool seeking,
		float64 playback,
		bool inTTLViewer);

private:
	const Fn<void()> _repaint;

	Ui::Animations::Simple _shownAnimation;
	float64 _progress = 0.;

};

} // namespace HistoryView
