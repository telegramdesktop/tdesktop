/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/effects/voice_once_particles.h"

namespace Ui {

struct TtlCountdown {
	explicit TtlCountdown(Fn<void()> repaint);

	crl::time deadline = 0;
	crl::time total = 0;
	TimerParticles particles;
	Animations::Basic animation;
};

[[nodiscard]] std::unique_ptr<TtlCountdown> MakeTtlCountdown(
	TimeId destroyAt,
	crl::time ttl,
	Fn<void()> repaint);

void PaintTtlCountdown(
	QPainter &p,
	QRect inner,
	int line,
	not_null<TtlCountdown*> countdown,
	const style::color &color,
	bool paused);

void PaintTtlFireIcon(QPainter &p, QRect inner, QImage &cache);

} // namespace Ui
