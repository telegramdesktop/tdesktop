/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

namespace Data {
class Thread;
} // namespace Data

namespace Ui {
class RoundButton;
} // namespace Ui

namespace Window {

class HistoryHider final : public Ui::RpWidget {
public:
	HistoryHider(QWidget *parent, const QString &text);
	~HistoryHider();

	void startHide();
	[[nodiscard]] rpl::producer<> hidden() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void refreshLang();
	void updateControlsGeometry();
	void animationCallback();

	QString _text;
	Ui::Animations::Simple _a_opacity;

	QRect _box;
	bool _hiding = false;

	int _chooseWidth = 0;

	rpl::event_stream<> _hidden;

};

} // namespace Window
