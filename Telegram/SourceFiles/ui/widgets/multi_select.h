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

#include "styles/style_widgets.h"

class InputField;

namespace Ui {

class IconButton;

class MultiSelect : public TWidget {
public:
	MultiSelect(QWidget *parent, const style::MultiSelect &st, const QString &placeholder = QString());

	QString getQuery() const;
	void setInnerFocus();

	void setQueryChangedCallback(base::lambda_unique<void(const QString &query)> callback);
	void setSubmittedCallback(base::lambda_unique<void(bool ctrlShiftEnter)> callback);

	class Item;
	void addItem(std_::unique_ptr<Item> item);
	void setItemText(uint64 itemId, const QString &text);

	void setItemRemovedCallback(base::lambda_unique<void(uint64 itemId)> callback);
	void removeItem(uint64 itemId); // Always calls the itemRemovedCallback().

protected:
	int resizeGetHeight(int newWidth) override;

	void resizeEvent(QResizeEvent *e) override;

private:
	ChildWidget<ScrollArea> _scroll;

	class Inner;
	ChildWidget<Inner> _inner;

	const style::MultiSelect &_st;

};

class MultiSelect::Item {
public:
	Item(uint64 id, const QString &text, const style::color &color);

	uint64 id() const {
		return _id;
	}
	void setText(const QString &text);
	void paint(Painter &p, int x, int y);

	virtual ~Item() = default;

protected:
	virtual void paintImage(Painter &p, int x, int y, int outerWidth, int size) = 0;

private:
	uint64 _id;

};

// This class is hold in header because it requires Qt preprocessing.
class MultiSelect::Inner : public ScrolledWidget {
	Q_OBJECT

public:
	Inner(QWidget *parent, const style::MultiSelect &st, const QString &placeholder);

	QString getQuery() const;
	bool setInnerFocus();

	void setQueryChangedCallback(base::lambda_unique<void(const QString &query)> callback);
	void setSubmittedCallback(base::lambda_unique<void(bool ctrlShiftEnter)> callback);

	void addItem(std_::unique_ptr<Item> item);
	void setItemText(uint64 itemId, const QString &text);

	void setItemRemovedCallback(base::lambda_unique<void(uint64 itemId)> callback);
	void removeItem(uint64 itemId); // Always calls the itemRemovedCallback().

	~Inner();

protected:
	int resizeGetHeight(int newWidth) override;

	void resizeEvent(QResizeEvent *e) override;

private slots:
	void onQueryChanged();
	void onSubmitted(bool ctrlShiftEnter) {
		if (_submittedCallback) {
			_submittedCallback(ctrlShiftEnter);
		}
	}

private:
	void refreshItemsGeometry(Item *startingFromItem);

	const style::MultiSelect &_st;

	using Row = QList<Item*>;
	using Rows = QList<Row>;
	Rows _rows;

	using Items = QList<Item*>;
	Items _items;

	ChildWidget<InputField> _filter;
	ChildWidget<Ui::IconButton> _cancel;

	base::lambda_unique<void(const QString &query)> _queryChangedCallback;
	base::lambda_unique<void(bool ctrlShiftEnter)> _submittedCallback;
	base::lambda_unique<void(uint64 itemId)> _itemRemovedCallback;

};

} // namespace Ui
