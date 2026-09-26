/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "ui/effects/animations.h"
#include "ui/effects/voice_once_particles.h"

namespace Ui {

class RpWidget;

struct TtlCountdown {
	explicit TtlCountdown(Fn<void()> repaint);

	TimeId destroyAt = 0;
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

[[nodiscard]] std::unique_ptr<RpWidget> MakeTtlCountdownBadge(
	QWidget *parent,
	TimeId destroyAt,
	crl::time ttl);

[[nodiscard]] std::unique_ptr<RpWidget> MakeTtlOnceBadge(QWidget *parent);

[[nodiscard]] object_ptr<RpWidget> MakeTtlTooltipContent(
	QWidget *parent,
	rpl::producer<TextWithEntities> text);

} // namespace Ui
