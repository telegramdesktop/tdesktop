/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"

namespace Dialogs {

class ArchiveSlide final {
public:
	explicit ArchiveSlide(Fn<void()> update);

	void start(
		int from,
		int to,
		QImage band,
		rpl::producer<bool> covered);
	void stop();

	[[nodiscard]] int shift() const;
	void paintBand(QPainter &p) const;

private:
	Fn<void()> _update;
	Ui::Animations::Simple _animation;
	QImage _band;
	int _from = 0;
	int _to = 0;
	bool _waiting = false;
	rpl::lifetime _lifetime;

};

} // namespace Dialogs
