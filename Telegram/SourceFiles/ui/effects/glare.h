/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/xmdnx/exteraGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace Ui {

struct GlareEffect final {
	void validate(
		const QColor &color,
		Fn<void()> updateCallback,
		crl::time timeout,
		crl::time duration);
	[[nodiscard]] float64 progress(crl::time now) const;

	Ui::Animations::Basic animation;
	struct {
		crl::time birthTime = 0;
		crl::time deathTime = 0;
	} glare;
	QPixmap pixmap;
	int width = 0;
	bool paused = false;
};

} // namespace Ui
