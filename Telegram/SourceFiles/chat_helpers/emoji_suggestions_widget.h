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
#pragma once

#include "ui/effects/panel_animation.h"

namespace Ui {

class InnerDropdown;
class FlatTextarea;

namespace Emoji {

class SuggestionsWidget : public TWidget {
public:
	SuggestionsWidget(QWidget *parent, const style::Menu &st);

	void showWithQuery(const QString &query);
	void handleKeyEvent(int key);

	base::Observable<bool> toggleAnimated;
	base::Observable<QString> triggered;

protected:
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	class Row;

	std::vector<Row> getRowsByQuery() const;
	void resizeToRows();
	int countWidth(const Row &row);
	void setSelected(int selected);
	void setPressed(int pressed);
	void clearMouseSelection();
	void clearSelection();
	void updateSelectedItem();
	int itemTop(int index);
	void updateItem(int index);
	void updateSelection(QPoint globalPosition);
	void triggerSelectedRow();
	void triggerRow(const Row &row);

	not_null<const style::Menu*> _st;

	QString _query;
	std::vector<Row> _rows;

	int _rowHeight = 0;
	bool _mouseSelection = false;
	int _selected = -1;
	int _pressed = -1;

};

class SuggestionsController : public QObject, private base::Subscriber {
public:
	SuggestionsController(QWidget *parent, not_null<QTextEdit*> field);

	void raise();

protected:
	bool eventFilter(QObject *object, QEvent *event) override;

private:
	void handleCursorPositionChange();
	void handleTextChange();
	QString getEmojiQuery();
	void suggestionsUpdated(bool visible);
	void updateGeometry();
	void updateForceHidden();
	void replaceCurrent(const QString &replacement);

	bool _shown = false;
	bool _forceHidden = false;
	int _queryStartPosition = 0;
	bool _ignoreCursorPositionChange = false;
	bool _textChangeAfterKeyPress = false;
	QPointer<QTextEdit> _field;
	object_ptr<InnerDropdown> _container;
	QPointer<SuggestionsWidget> _suggestions;

};

} // namespace Emoji
} // namespace Ui
