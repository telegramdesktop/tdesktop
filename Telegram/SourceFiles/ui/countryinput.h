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

#include "ui/scrollarea.h"
#include "ui/effects/rect_shadow.h"
#include "boxes/abstractbox.h"
#include "styles/style_intro.h"

QString findValidCode(QString fullCode);

namespace Ui {
class MultiSelect;
} // namespace Ui

class CountryInput : public QWidget {
	Q_OBJECT

public:
	CountryInput(QWidget *parent, const style::countryInput &st);

public slots:
	void onChooseCode(const QString &code);
	bool onChooseCountry(const QString &country);

signals:
	void codeChanged(const QString &code);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void enterEvent(QEvent *e) override;
	void leaveEvent(QEvent *e) override;

private:
	void setText(const QString &newText);

	QPixmap _arrow;
	QRect _inner, _arrowRect;
	const style::countryInput &_st;
	bool _active;
	QString _text;

};

namespace internal {
class CountrySelectInner;
} // namespace internal

class CountrySelectBox : public ItemListBox {
	Q_OBJECT

public:
	CountrySelectBox();

signals:
	void countryChosen(const QString &iso);

public slots:
	void onSubmit();

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void doSetInnerFocus() override;
	void showAll() override;

private:
	void onFilterUpdate(const QString &query);

	class Inner;
	ChildWidget<Inner> _inner;
	ChildWidget<Ui::MultiSelect> _select;

	ScrollableBoxShadow _topShadow;

};

// This class is hold in header because it requires Qt preprocessing.
class CountrySelectBox::Inner : public TWidget {
	Q_OBJECT

public:
	Inner(QWidget *parent);

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

protected:
	void paintEvent(QPaintEvent *e) override;
	void enterEvent(QEvent *e) override;
	void leaveEvent(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

private:
	void updateSelectedRow();

	int _rowHeight;

	int _sel = 0;
	QString _filter;
	bool _mouseSel = false;

	QPoint _lastMousePos;

};
