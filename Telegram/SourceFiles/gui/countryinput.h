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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "style.h"

#include "gui/flatinput.h"
#include "gui/scrollarea.h"
#include "gui/flatbutton.h"
#include "gui/boxshadow.h"
#include "boxes/abstractbox.h"

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

signals:

	void codeChanged(const QString &code);

private:

	void setText(const QString &newText);

	QPixmap _arrow;
	QRect _inner, _arrowRect;
	style::countryInput _st;
	bool _active;
	QString _text;

	CountrySelect *_select;

};

class CountrySelectInner : public TWidget {
	Q_OBJECT

public:

	CountrySelectInner();
	void paintEvent(QPaintEvent *e);
	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mousePressEvent(QMouseEvent *e);

	void updateFilter(QString filter = QString());

	void selectSkip(int32 dir);
	void selectSkipPage(int32 h, int32 dir);

	void chooseCountry();
	
	void refresh();

signals:

	void countryChosen(const QString &iso);
	void mustScrollTo(int ymin, int ymax);

public slots:

	void updateSel();

private:

	void updateSelectedRow();

	int32 _rowHeight;

	int32 _sel;
	QString _filter;
	bool _mouseSel;

	QPoint _lastMousePos;
};

class CountrySelectBox : public ItemListBox {
	Q_OBJECT

public:

	CountrySelectBox();
	void keyPressEvent(QKeyEvent *e);
	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);

	void setInnerFocus() {
		_filter.setFocus();
	}

signals:

	void countryChosen(const QString &iso);

public slots:

	void onFilterUpdate();
	void onFilterCancel();
	void onSubmit();

protected:

	void showDone();
	void hideAll();
	void showAll();

private:

	CountrySelectInner _inner;
	InputField _filter;
	IconedButton _filterCancel;

	ScrollableBoxShadow _topShadow;
};

/** /
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
	BoxButton _doneButton, _cancelButton;
	int32 _innerLeft, _innerTop, _innerWidth, _innerHeight;

	anim::fvalue a_alpha, a_bgAlpha;
	anim::ivalue a_coord;
	anim::transition af_alpha, af_bgAlpha, af_coord;
	QPixmap _cache;

	BoxShadow _shadow;

};
*/