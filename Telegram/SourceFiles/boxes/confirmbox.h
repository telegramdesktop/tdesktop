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

#include "abstractbox.h"

class InformBox;
class ConfirmBox : public AbstractBox {
	Q_OBJECT

public:

	ConfirmBox(const QString &text, const QString &doneText = QString(), const style::BoxButton &doneStyle = st::defaultBoxButton, const QString &cancelText = QString(), const style::BoxButton &cancelStyle = st::cancelBoxButton);
	void keyPressEvent(QKeyEvent *e);
	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);
	void leaveEvent(QEvent *e);
	void updateLink();

public slots:

	void onCancel();

signals:

	void confirmed();
	void cancelled();
	void cancelPressed();

protected:

	void closePressed();
	void hideAll();
	void showAll();

private:

	ConfirmBox(const QString &text, const QString &doneText, const style::BoxButton &doneStyle, bool informative);
	friend class InformBox;

	void init(const QString &text);

	bool _informative;

	Text _text;
	int32 _textWidth, _textHeight;

	void updateHover();

	QPoint _lastMousePos;
	TextLinkPtr _myLink;

	BoxButton _confirm, _cancel;
};

class InformBox : public ConfirmBox {
public:
	InformBox(const QString &text, const QString &doneText = QString(), const style::BoxButton &doneStyle = st::defaultBoxButton) : ConfirmBox(text, doneText, doneStyle, true) {
	}
};

class ConfirmLinkBox : public ConfirmBox {
	Q_OBJECT

public:

	ConfirmLinkBox(const QString &url);

public slots:

	void onOpenLink();

private:

	QString _url;

};

class MaxInviteBox : public AbstractBox {
	Q_OBJECT

public:

	MaxInviteBox(const QString &link);
	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void leaveEvent(QEvent *e);
	
protected:

	void hideAll();
	void showAll();

private:

	void updateSelected(const QPoint &cursorGlobalPosition);
	void step_good(float64 ms, bool timer);

	BoxButton _close;
	Text _text;
	int32 _textWidth, _textHeight;

	QString _link;
	QRect _invitationLink;
	bool _linkOver;

	QPoint _lastMousePos;

	QString _goodTextLink;
	anim::fvalue a_goodOpacity;
	Animation _a_good;
};
