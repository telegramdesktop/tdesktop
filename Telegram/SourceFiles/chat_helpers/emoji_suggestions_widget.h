/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
	void setReplaceCallback(base::lambda<void(
		int from,
		int till,
		const QString &replacement)> callback);

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
	base::lambda<void(
		int from,
		int till,
		const QString &replacement)> _replaceCallback;
	object_ptr<InnerDropdown> _container;
	QPointer<SuggestionsWidget> _suggestions;

};

} // namespace Emoji
} // namespace Ui
