/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class IconButton;
} // namespace Ui

namespace Settings {

class FixedBar : public TWidget {
public:
	FixedBar(QWidget *parent);

	void setText(const QString &text);

protected:
	void paintEvent(QPaintEvent *e) override;

	int resizeGetHeight(int newWidth) override;

private:
	QString _text;

};

} // namespace Settings
