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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "settings/settings_block_widget.h"

#include "styles/style_settings.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"

namespace Settings {

BlockWidget::BlockWidget(QWidget *parent, UserData *self, const QString &title) : TWidget(parent)
, _self(self)
, _title(title) {
}

void BlockWidget::setContentLeft(int contentLeft) {
	_contentLeft = contentLeft;
}

int BlockWidget::contentTop() const {
	return emptyTitle() ? 0 : (st::settingsBlockMarginTop + st::settingsBlockTitleHeight);
}

int BlockWidget::resizeGetHeight(int newWidth) {
	int x = contentLeft(), result = contentTop();
	int availw = newWidth - x;
	for_const (auto &row, _rows) {
		auto childMargins = row.child->getMargins();
		row.child->moveToLeft(x + row.margin.left(), result + row.margin.top(), newWidth);
		auto availRowWidth = availw - row.margin.left() - row.margin.right() - x;
		auto natural = row.child->naturalWidth();
		auto rowWidth = (natural < 0) ? availRowWidth : qMin(natural, availRowWidth);
		if (row.child->widthNoMargins() != rowWidth) {
			row.child->resizeToWidth(rowWidth);
		}
		result += row.margin.top() + row.child->heightNoMargins() + row.margin.bottom();
	}
	result += st::settingsBlockMarginBottom;
	return result;
}

void BlockWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	paintTitle(p);
	paintContents(p);
}

void BlockWidget::paintTitle(Painter &p) {
	if (emptyTitle()) return;

	p.setFont(st::settingsBlockTitleFont);
	p.setPen(st::settingsBlockTitleFg);
	int titleTop = st::settingsBlockMarginTop + st::settingsBlockTitleTop;
	p.drawTextLeft(contentLeft(), titleTop, width(), _title);
}

void BlockWidget::addCreatedRow(TWidget *child, const style::margins &margin) {
	_rows.push_back({ child, margin });
}

void BlockWidget::rowHeightUpdated() {
	auto newHeight = resizeGetHeight(width());
	if (newHeight != height()) {
		resize(width(), newHeight);
		emit heightUpdated();
	}
}

void BlockWidget::createChildRow(object_ptr<Ui::Checkbox> &child, style::margins &margin, const QString &text, base::lambda<void(bool checked)> callback, bool checked) {
	child.create(this, text, checked, st::defaultBoxCheckbox);
	subscribe(child->checkedChanged, std::move(callback));
}

void BlockWidget::createChildRow(object_ptr<Ui::LinkButton> &child, style::margins &margin, const QString &text, const char *slot, const style::LinkButton &st) {
	child.create(this, text, st);
	connect(child, SIGNAL(clicked()), this, slot);
}

} // namespace Settings
