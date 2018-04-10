/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class RoundButton;
} // namespace Ui

namespace Window {
namespace Theme {

class WarningWidget : public TWidget {
public:
	WarningWidget(QWidget *parent);

	void setHiddenCallback(base::lambda<void()> callback) {
		_hiddenCallback = std::move(callback);
	}

	void showAnimated();
	void hideAnimated();

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void refreshLang();
	void updateControlsGeometry();
	void setSecondsLeft(int secondsLeft);
	void startAnimation(bool hiding);
	void updateText();
	void handleTimer();

	bool _hiding = false;
	Animation _animation;
	QPixmap _cache;
	QRect _inner, _outer;

	SingleTimer _timer;
	TimeMs _started = 0;
	int _secondsLeft = 0;
	QString _text;

	object_ptr<Ui::RoundButton> _keepChanges;
	object_ptr<Ui::RoundButton> _revert;

	base::lambda<void()> _hiddenCallback;

};

} // namespace Theme
} // namespace Window
