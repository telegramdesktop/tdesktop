/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "base/timer.h"

namespace Media::Player {

class Dropdown final : public Ui::RpWidget {
public:
	explicit Dropdown(QWidget *parent);

	bool overlaps(const QRect &globalRect);

	QMargins getMargin() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	bool eventFilter(QObject *obj, QEvent *e) override;

private:
	void startHide();
	void startShow();

	void otherEnter();
	void otherLeave();

	void appearanceCallback();
	void hidingFinished();
	void startAnimation();

	bool _hiding = false;

	QPixmap _cache;
	Ui::Animations::Simple _a_appearance;

	base::Timer _hideTimer;
	base::Timer _showTimer;

};

} // namespace Media::Player
