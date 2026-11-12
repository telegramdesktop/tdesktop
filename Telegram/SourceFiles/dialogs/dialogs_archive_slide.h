/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"

namespace style {
struct DialogRow;
} // namespace style

namespace Dialogs {

struct ArchiveBand {
	QImage image;
	QString title;
	QPoint titleBaseline;
};

[[nodiscard]] ArchiveBand GrabArchiveBand(
	not_null<QWidget*> widget,
	int height,
	bool collapsed,
	const style::DialogRow &st,
	const QString &title);

class ArchiveSlide final {
public:
	explicit ArchiveSlide(Fn<void()> update);

	void start(
		int from,
		int to,
		ArchiveBand was,
		ArchiveBand now,
		rpl::producer<bool> covered);
	void stop();

	[[nodiscard]] int shift() const;
	void paintBand(QPainter &p) const;

private:
	[[nodiscard]] int current() const;
	void paintMorph(QPainter &p, int now) const;

	Fn<void()> _update;
	Ui::Animations::Simple _animation;
	ArchiveBand _was;
	ArchiveBand _now;
	int _from = 0;
	int _to = 0;
	bool _waiting = false;
	rpl::lifetime _lifetime;

};

} // namespace Dialogs
