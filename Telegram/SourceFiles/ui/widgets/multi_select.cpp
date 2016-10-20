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
#include "stdafx.h"
#include "ui/widgets/multi_select.h"

#include "styles/style_widgets.h"
#include "ui/buttons/icon_button.h"
#include "lang.h"

namespace Ui {

MultiSelect::MultiSelect(QWidget *parent, const style::MultiSelect &st, const QString &placeholder) : TWidget(parent)
, _st(st)
, _scroll(this, st::boxScroll)
, _inner(this, st, placeholder) {
	_scroll->setOwnedWidget(_inner);
	setAttribute(Qt::WA_OpaquePaintEvent);
}

void MultiSelect::setQueryChangedCallback(base::lambda_unique<void(const QString &query)> callback) {
	_inner->setQueryChangedCallback(std_::move(callback));
}

void MultiSelect::setSubmittedCallback(base::lambda_unique<void(bool ctrlShiftEnter)> callback) {
	_inner->setSubmittedCallback(std_::move(callback));
}

void MultiSelect::setInnerFocus() {
	if (_inner->setInnerFocus()) {
		_scroll->scrollToY(_scroll->scrollTopMax());
	}
}

QString MultiSelect::getQuery() const {
	return _inner->getQuery();
}

void MultiSelect::addItem(std_::unique_ptr<Item> item) {
	_inner->addItem(std_::move(item));
}

int MultiSelect::resizeGetHeight(int newWidth) {
	_inner->resizeToWidth(newWidth);
	auto newHeight = qMin(_inner->height(), _st.maxHeight);
	_scroll->resize(newWidth, newHeight);
	return newHeight;
}

void MultiSelect::resizeEvent(QResizeEvent *e) {
	_scroll->moveToLeft(0, 0);
}

MultiSelect::Item::Item(uint64 id, const QString &text, const style::color &color)
: _id(id) {
}

void MultiSelect::Item::setText(const QString &text) {

}

void MultiSelect::Item::paint(Painter &p, int x, int y) {

}

MultiSelect::Inner::Inner(QWidget *parent, const style::MultiSelect &st, const QString &placeholder) : ScrolledWidget(parent)
, _st(st)
, _filter(this, _st.field, placeholder)
, _cancel(this, _st.cancel) {
	connect(_filter, SIGNAL(changed()), this, SLOT(onQueryChanged()));
	connect(_filter, SIGNAL(submitted(bool)), this, SLOT(onSubmitted(bool)));
	_cancel->hide();
	_cancel->setClickedCallback([this] {
		_filter->setText(QString());
		_filter->setFocus();
	});
}

void MultiSelect::Inner::onQueryChanged() {
	auto query = getQuery();
	_cancel->setVisible(!query.isEmpty());
	if (_queryChangedCallback) {
		_queryChangedCallback(query);
	}
}

bool MultiSelect::Inner::setInnerFocus() {
	if (!_filter->hasFocus()) {
		_filter->setFocus();
		return true;
	}
	return false;
}

QString MultiSelect::Inner::getQuery() const {
	return _filter->getLastText().trimmed();
}

void MultiSelect::Inner::setQueryChangedCallback(base::lambda_unique<void(const QString &query)> callback) {
	_queryChangedCallback = std_::move(callback);
}

void MultiSelect::Inner::setSubmittedCallback(base::lambda_unique<void(bool ctrlShiftEnter)> callback) {
	_submittedCallback = std_::move(callback);
}

int MultiSelect::Inner::resizeGetHeight(int newWidth) {
	_filter->resizeToWidth(newWidth);
	return _filter->height();
}

void MultiSelect::Inner::resizeEvent(QResizeEvent *e) {
	_filter->moveToLeft(0, 0);
	_cancel->moveToRight(0, 0);
}

void MultiSelect::Inner::addItem(std_::unique_ptr<Item> item) {
	_items.push_back(item.release());
	refreshItemsGeometry(nullptr);
}

void MultiSelect::Inner::refreshItemsGeometry(Item *startingFromRowWithItem) {
	int startingFromRow = 0;
	int startingFromIndex = 0;
	for (int row = 1, rowsCount = qMin(_rows.size(), 1); row != rowsCount; ++row) {
		if (startingFromRowWithItem) {
			if (_rows[row - 1].contains(startingFromRowWithItem)) {
				break;
			}
		}
		startingFromIndex += _rows[row - 1].size();
		++startingFromRow;
	}
	while (_rows.size() > startingFromRow) {
		_rows.pop_back();
	}
	for (int i = startingFromIndex, count = _items.size(); i != count; ++i) {
		Row row;
		row.append(_items[i]);
		_rows.append(row);
	}
}

void MultiSelect::Inner::setItemText(uint64 itemId, const QString &text) {
	for (int i = 0, count = _items.size(); i != count; ++i) {
		auto item = _items[i];
		if (item->id() == itemId) {
			item->setText(text);
			refreshItemsGeometry(item);
			return;
		}
	}
}

void MultiSelect::Inner::setItemRemovedCallback(base::lambda_unique<void(uint64 itemId)> callback) {
	_itemRemovedCallback = std_::move(callback);
}

void MultiSelect::Inner::removeItem(uint64 itemId) {
	for (int i = 0, count = _items.size(); i != count; ++i) {
		auto item = _items[i];
		if (item->id() == itemId) {
			_items.removeAt(i);
			refreshItemsGeometry(item);
			delete item;
			break;
		}
	}
	if (_itemRemovedCallback) {
		_itemRemovedCallback(itemId);
	}
}

MultiSelect::Inner::~Inner() {
	base::take(_rows);
	for (auto item : base::take(_items)) {
		delete item;
	}
}

} // namespace Ui
