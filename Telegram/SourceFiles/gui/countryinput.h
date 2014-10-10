/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#pragma once

#include <QtWidgets/QWidget>
#include "style.h"

#include "gui/flatinput.h"
#include "gui/scrollarea.h"
#include "gui/flatbutton.h"
#include "gui/boxshadow.h"

QString findValidCode(QString fullCode);

class CountrySelect;

class CountryInput : public QWidget {
	Q_OBJECT

public:

	CountryInput(QWidget *parent, const style::countryInput &st);

	void paintEvent(QPaintEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);

	~CountryInput();

public slots:

	void onChooseCode(const QString &code);
	bool onChooseCountry(const QString &country);
	void onFinishCountry();

signals:

	void codeChanged(const QString &code);
	void selectClosed();

private:

	void setText(const QString &newText);

	QPixmap _arrow;
	QRect _inner, _arrowRect;
	style::countryInput _st;
	bool _active;
	QString _text;

	CountrySelect *_select;

};

class CountryList : public QWidget {
	Q_OBJECT

public:

	CountryList(QWidget *parent, const style::countryList &st = st::countryList);

	void paintEvent(QPaintEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);

	void selectSkip(int delta);
	void selectSkipPage(int h, int delta);

	void updateFiltered();

	QString getSelectedCountry() const;

public slots:

	void onUpdateSelected(bool force = false);
	void onParentGeometryChanged();

signals:

	void countrySelected();
	void mustScrollTo(int scrollToTop, int scrollToBottom);

private:

	void resetList();
	void setSelected(int newSelected);

	int _sel;
	style::countryList _st;
	QPoint _mousePos;

	bool _mouseSel;

};

class CountrySelect : public QWidget, public Animated {
	Q_OBJECT

public:

	CountrySelect();

	void paintEvent(QPaintEvent *e);
	void keyPressEvent(QKeyEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void resizeEvent(QResizeEvent *e);

	bool animStep(float64 ms);

	~CountrySelect();

signals:

	void countryChosen(const QString &country = QString());
	void countryFinished();

public slots:

	void onParentResize(const QSize &newSize);
	void onCountryChoose();
	void onCountryCancel();
	void onScrollFinished();
	void onFilterUpdate();

private:

	void finish(const QString &res);
	void prepareAnimation(int to);

	QString _result;
	FlatInput _filter;
	ScrollArea _scroll;
	CountryList _list;
	FlatButton _doneButton, _cancelButton;
	int32 _innerLeft, _innerTop, _innerWidth, _innerHeight;

	anim::fvalue a_alpha, a_bgAlpha;
	anim::ivalue a_coord;
	anim::transition af_alpha, af_bgAlpha, af_coord;
	QPixmap _cache;

	BoxShadow _shadow;

};
