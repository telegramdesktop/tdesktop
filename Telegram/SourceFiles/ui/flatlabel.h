/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

class FlatLabel : public TWidget, public ClickHandlerHost {
	Q_OBJECT

public:

	FlatLabel(QWidget *parent, const QString &text, const style::flatLabel &st = st::labelDefFlat, const style::textStyle &tst = st::defaultTextStyle);

	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void enterEvent(QEvent *e) override;
	void leaveEvent(QEvent *e) override;
	void updateLink();
	void setOpacity(float64 o);

	void setText(const QString &text);
	void setRichText(const QString &text);

	void resizeToWidth(int32 width);

	void setLink(uint16 lnkIndex, const ClickHandlerPtr &lnk);

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &action, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &action, bool pressed) override;

private:

	void updateHover();

	Text _text;
	style::flatLabel _st;
	style::textStyle _tst;
	float64 _opacity;

	QPoint _lastMousePos;

};
