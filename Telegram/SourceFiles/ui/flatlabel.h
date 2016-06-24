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

class FlatLabel : public TWidget, public ClickHandlerHost {
	Q_OBJECT

public:
	FlatLabel(QWidget *parent, const style::flatLabel &st = st::labelDefFlat, const style::textStyle &tst = st::defaultTextStyle);

	enum class InitType {
		Simple,
		Rich,
	};
	FlatLabel(QWidget *parent, const QString &text, InitType initType, const style::flatLabel &st = st::labelDefFlat, const style::textStyle &tst = st::defaultTextStyle);

	void setOpacity(float64 o);

	void setText(const QString &text);
	void setRichText(const QString &text);
	void setMarkedText(const TextWithEntities &textWithEntities);
	void setSelectable(bool selectable);
	void setDoubleClickSelectsParagraph(bool doubleClickSelectsParagraph);
	void setContextCopyText(const QString &copyText);
	void setExpandLinksMode(ExpandLinksMode mode);
	void setBreakEverywhere(bool breakEverywhere);

	void resizeToWidth(int32 width);
	int naturalWidth() const;

	void setLink(uint16 lnkIndex, const ClickHandlerPtr &lnk);

	using ClickHandlerHook = Function<bool, const ClickHandlerPtr &, Qt::MouseButton>;
	void setClickHandlerHook(ClickHandlerHook &&hook);

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &action, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &action, bool pressed) override;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
	void enterEvent(QEvent *e) override;
	void leaveEvent(QEvent *e) override;
	void focusOutEvent(QFocusEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	bool event(QEvent *e) override; // calls touchEvent when necessary
	void touchEvent(QTouchEvent *e);

private slots:
	void onCopySelectedText();
	void onCopyContextText();
	void onCopyContextUrl();

	void onTouchSelect();
	void onContextMenuDestroy(QObject *obj);

	void onExecuteDrag();

private:
	void init();

	Text::StateResult dragActionUpdate();
	Text::StateResult dragActionStart(const QPoint &p, Qt::MouseButton button);
	Text::StateResult dragActionFinish(const QPoint &p, Qt::MouseButton button);
	void updateHover(const Text::StateResult &state);
	Text::StateResult getTextState(const QPoint &m) const;
	void refreshCursor(bool uponSymbol);

	int countTextWidth() const;
	int countTextHeight(int textWidth);
	void refreshSize();

	enum class ContextMenuReason {
		FromEvent,
		FromTouch,
	};
	void showContextMenu(QContextMenuEvent *e, ContextMenuReason reason);

	Text _text;
	style::flatLabel _st;
	style::textStyle _tst;
	float64 _opacity = 1.;

	int _allowedWidth = 0;
	int _fullTextHeight = 0;
	bool _breakEverywhere = false;

	style::cursor _cursor = style::cur_default;
	bool _selectable = false;
	TextSelection _selection, _savedSelection;
	TextSelectType _selectionType = TextSelectType::Letters;
	bool _doubleClickSelectsParagraph = false;

	enum DragAction {
		NoDrag = 0x00,
		PrepareDrag = 0x01,
		Dragging = 0x02,
		Selecting = 0x04,
	};
	DragAction _dragAction = NoDrag;
	QPoint _dragStartPosition;
	uint16 _dragSymbol = 0;
	bool _dragWasInactive = false;

	QPoint _lastMousePos;

	QPoint _trippleClickPoint;
	QTimer _trippleClickTimer;

	PopupMenu *_contextMenu = nullptr;
	ClickHandlerPtr _contextMenuClickHandler;
	QString _contextCopyText;
	ExpandLinksMode _contextExpandLinksMode = ExpandLinksAll;

	ClickHandlerHook _clickHandlerHook;

	// text selection and context menu by touch support (at least Windows Surface tablets)
	bool _touchSelect = false;
	bool _touchInProgress = false;
	QPoint _touchStart, _touchPrevPos, _touchPos;
	QTimer _touchSelectTimer;

};
