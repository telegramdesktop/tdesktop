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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#pragma once

class Widget : public QWidget {
public:

	Widget(QWidget *parent = 0) : QWidget(parent) {
	}
	void moveToLeft(int x, int y, int outerw) {
		move(rtl() ? (outerw - x - width()) : x, y);
	}
	void moveToRight(int x, int y, int outerw) {
		move(rtl() ? x : (outerw - x - width()), y);
	}

};

namespace App {
	const QPixmap &sprite();
}

class Painter : public QPainter {
public:
	explicit Painter(QPaintDevice *device) : QPainter(device) {
	}

	void drawTextLeft(int x, int y, int outerw, const QString &text, int textWidth = -1) {
		QFontMetrics m(fontMetrics());
		if (rtl() && textWidth < 0) textWidth = m.width(text);
		drawText(rtl() ? (outerw - x - textWidth) : x, y + m.ascent(), text);
	}
	void drawTextRight(int x, int y, int outerw, const QString &text, int textWidth = -1) {
		QFontMetrics m(fontMetrics());
		if (!rtl() && textWidth < 0) textWidth = m.width(text);
		drawText(rtl() ? x : (outerw - x - textWidth), y + m.ascent(), text);
	}
	void drawPixmapLeft(int x, int y, int outerw, const QPixmap &pix, const QRect &from) {
		drawPixmap(QPoint(rtl() ? (outerw - x - (from.width() / pix.devicePixelRatio())) : x, y), pix, from);
	}
	void drawPixmapLeft(const QPoint &p, int outerw, const QPixmap &pix, const QRect &from) {
		return drawPixmapLeft(p.x(), p.y(), outerw, pix, from);
	}
	void drawPixmapRight(int x, int y, int outerw, const QPixmap &pix, const QRect &from) {
		drawPixmap(QPoint(rtl() ? x : (outerw - x - (from.width() / pix.devicePixelRatio())), y), pix, from);
	}
	void drawPixmapRight(const QPoint &p, int outerw, const QPixmap &pix, const QRect &from) {
		return drawPixmapRight(p.x(), p.y(), outerw, pix, from);
	}
	void drawSprite(int x, int y, const style::sprite &sprite) {
		return drawPixmap(QPoint(x, y), App::sprite(), sprite);
	}
	void drawSprite(const QPoint &p, const style::sprite &sprite) {
		return drawPixmap(p, App::sprite(), sprite);
	}
	void drawSpriteLeft(int x, int y, int outerw, const style::sprite &sprite) {
		return drawPixmapLeft(x, y, outerw, App::sprite(), sprite);
	}
	void drawSpriteLeft(const QPoint &p, int outerw, const style::sprite &sprite) {
		return drawPixmapLeft(p, outerw, App::sprite(), sprite);
	}
	void drawSpriteRight(int x, int y, int outerw, const style::sprite &sprite) {
		return drawPixmapRight(x, y, outerw, App::sprite(), sprite);
	}
	void drawSpriteRight(const QPoint &p, int outerw, const style::sprite &sprite) {
		return drawPixmapRight(p, outerw, App::sprite(), sprite);
	}
	void drawSpriteCenter(const QRect &in, const style::sprite &sprite) {
		return drawPixmap(QPoint(in.x() + (in.width() - sprite.pxWidth()) / 2, in.y() + (in.height() - sprite.pxHeight()) / 2), App::sprite(), sprite);
	}
};

class TWidget : public Widget {
	Q_OBJECT

public:

	TWidget(QWidget *parent = 0) : Widget(parent) {
	}
	TWidget *tparent() {
		return qobject_cast<TWidget*>(parentWidget());
	}
	const TWidget *tparent() const {
		return qobject_cast<const TWidget*>(parentWidget());
	}

	virtual void leaveToChildEvent(QEvent *e) { // e -- from enterEvent() of child TWidget
	}
	virtual void enterFromChildEvent(QEvent *e) { // e -- from leaveEvent() of child TWidget
	}

	bool event(QEvent *e) {
		return QWidget::event(e);
	}

protected:

	void enterEvent(QEvent *e) {
		TWidget *p(tparent());
		if (p) p->leaveToChildEvent(e);
		return Widget::enterEvent(e);
	}
	void leaveEvent(QEvent *e) {
		TWidget *p(tparent());
		if (p) p->enterFromChildEvent(e);
		return Widget::leaveEvent(e);
	}

private:

};

QPixmap myGrab(QWidget *target, const QRect &rect);
