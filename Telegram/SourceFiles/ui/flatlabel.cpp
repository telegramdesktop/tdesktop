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

#include "ui/flatlabel.h"
#include "mainwindow.h"
#include "lang.h"

namespace {
	TextParseOptions _labelOptions = {
		TextParseMultiline, // flags
		0, // maxw
		0, // maxh
		Qt::LayoutDirectionAuto, // dir
	};
	TextParseOptions _labelMarkedOptions = {
		TextParseMultiline | TextParseLinks | TextParseHashtags | TextParseMentions | TextParseBotCommands, // flags
		0, // maxw
		0, // maxh
		Qt::LayoutDirectionAuto, // dir
	};
}

FlatLabel::FlatLabel(QWidget *parent, const style::flatLabel &st, const style::textStyle &tst) : TWidget(parent)
, _text(st.width ? st.width : QFIXED_MAX)
, _st(st)
, _tst(tst)
, _contextCopyText(lang(lng_context_copy_text)) {
	init();
}

FlatLabel::FlatLabel(QWidget *parent, const QString &text, InitType initType, const style::flatLabel &st, const style::textStyle &tst) : TWidget(parent)
, _text(st.width ? st.width : QFIXED_MAX)
, _st(st)
, _tst(tst)
, _contextCopyText(lang(lng_context_copy_text)) {
	if (initType == InitType::Rich) {
		setRichText(text);
	} else {
		setText(text);
	}
	init();
}

void FlatLabel::init() {
	_trippleClickTimer.setSingleShot(true);

	_touchSelectTimer.setSingleShot(true);
	connect(&_touchSelectTimer, SIGNAL(timeout()), this, SLOT(onTouchSelect()));
}

void FlatLabel::setText(const QString &text) {
	textstyleSet(&_tst);
	_text.setText(_st.font, text, _labelOptions);
	refreshSize();
	textstyleRestore();
	setMouseTracking(_selectable || _text.hasLinks());
}

void FlatLabel::setRichText(const QString &text) {
	textstyleSet(&_tst);
	_text.setRichText(_st.font, text, _labelOptions);
	refreshSize();
	textstyleRestore();
	setMouseTracking(_selectable || _text.hasLinks());
}

void FlatLabel::setMarkedText(const TextWithEntities &textWithEntities) {
	textstyleSet(&_tst);
	_text.setMarkedText(_st.font, textWithEntities, _labelMarkedOptions);
	refreshSize();
	textstyleRestore();
	setMouseTracking(_selectable || _text.hasLinks());
}

void FlatLabel::setSelectable(bool selectable) {
	_selectable = selectable;
	setMouseTracking(_selectable || _text.hasLinks());
}

void FlatLabel::setDoubleClickSelectsParagraph(bool doubleClickSelectsParagraph) {
	_doubleClickSelectsParagraph = doubleClickSelectsParagraph;
}

void FlatLabel::setContextCopyText(const QString &copyText) {
	_contextCopyText = copyText;
}

void FlatLabel::setExpandLinksMode(ExpandLinksMode mode) {
	_contextExpandLinksMode = mode;
}

void FlatLabel::setBreakEverywhere(bool breakEverywhere) {
	_breakEverywhere = breakEverywhere;
}

void FlatLabel::resizeToWidth(int32 width) {
	textstyleSet(&_tst);
	_allowedWidth = width;
	refreshSize();
	textstyleRestore();
}

int FlatLabel::naturalWidth() const {
	return _text.maxWidth();
}

int FlatLabel::countTextWidth() const {
	return _allowedWidth ? (_allowedWidth - _st.margin.left() - _st.margin.right()) : (_st.width ? _st.width : _text.maxWidth());
}

int FlatLabel::countTextHeight(int textWidth) {
	_fullTextHeight = _text.countHeight(textWidth);
	return _st.maxHeight ? qMin(_fullTextHeight, _st.maxHeight) : _fullTextHeight;
}

void FlatLabel::refreshSize() {
	int textWidth = countTextWidth();
	int textHeight = countTextHeight(textWidth);
	int fullWidth = _st.margin.left() + textWidth + _st.margin.right();
	int fullHeight = _st.margin.top() + textHeight + _st.margin.bottom();
	resize(fullWidth, fullHeight);
}

void FlatLabel::setLink(uint16 lnkIndex, const ClickHandlerPtr &lnk) {
	_text.setLink(lnkIndex, lnk);
}

void FlatLabel::setClickHandlerHook(ClickHandlerHook &&hook) {
	_clickHandlerHook = std_::move(hook);
}

void FlatLabel::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	dragActionUpdate();
}

void FlatLabel::mousePressEvent(QMouseEvent *e) {
	if (_contextMenu) {
		e->accept();
		return; // ignore mouse press, that was hiding context menu
	}
	dragActionStart(e->globalPos(), e->button());
}

Text::StateResult FlatLabel::dragActionStart(const QPoint &p, Qt::MouseButton button) {
	_lastMousePos = p;
	auto state = dragActionUpdate();

	if (button != Qt::LeftButton) return state;

	ClickHandler::pressed();
	_dragAction = NoDrag;
	_dragWasInactive = App::wnd()->inactivePress();
	if (_dragWasInactive) App::wnd()->inactivePress(false);

	if (ClickHandler::getPressed()) {
		_dragStartPosition = mapFromGlobal(_lastMousePos);
		_dragAction = PrepareDrag;
	}
	if (!_selectable || _dragAction != NoDrag) {
		return state;
	}

	if (_trippleClickTimer.isActive() && (_lastMousePos - _trippleClickPoint).manhattanLength() < QApplication::startDragDistance()) {
		if (state.uponSymbol) {
			_selection = { state.symbol, state.symbol };
			_savedSelection = { 0, 0 };
			_dragSymbol = state.symbol;
			_dragAction = Selecting;
			_selectionType = TextSelectType::Paragraphs;
			updateHover(state);
			_trippleClickTimer.start(QApplication::doubleClickInterval());
			update();
		}
	}
	if (_selectionType != TextSelectType::Paragraphs) {
		_dragSymbol = state.symbol;
		bool uponSelected = state.uponSymbol;
		if (uponSelected) {
			if (_dragSymbol < _selection.from || _dragSymbol >= _selection.to) {
				uponSelected = false;
			}
		}
		if (uponSelected) {
			_dragStartPosition = mapFromGlobal(_lastMousePos);
			_dragAction = PrepareDrag; // start text drag
		} else if (!_dragWasInactive) {
			if (state.afterSymbol) ++_dragSymbol;
			_selection = { _dragSymbol, _dragSymbol };
			_savedSelection = { 0, 0 };
			_dragAction = Selecting;
			update();
		}
	}
	return state;
}

Text::StateResult FlatLabel::dragActionFinish(const QPoint &p, Qt::MouseButton button) {
	_lastMousePos = p;
	auto state = dragActionUpdate();

	ClickHandlerPtr activated = ClickHandler::unpressed();
	if (_dragAction == Dragging) {
		activated.clear();
	} else if (_dragAction == PrepareDrag) {
		_selection = { 0, 0 };
		_savedSelection = { 0, 0 };
		update();
	}
	_dragAction = NoDrag;
	_selectionType = TextSelectType::Letters;

	if (activated) {
		if (_clickHandlerHook.isNull() || _clickHandlerHook.call(activated, button)) {
			App::activateClickHandler(activated, button);
		}
	}

#if defined Q_OS_LINUX32 || defined Q_OS_LINUX64
	if (!_selection.empty()) {
		QApplication::clipboard()->setText(_text.originalText(_selection, _contextExpandLinksMode), QClipboard::Selection);
	}
#endif // Q_OS_LINUX32 || Q_OS_LINUX64

	return state;
}

void FlatLabel::mouseReleaseEvent(QMouseEvent *e) {
	dragActionFinish(e->globalPos(), e->button());
	if (!rect().contains(e->pos())) {
		leaveEvent(e);
	}
}

void FlatLabel::mouseDoubleClickEvent(QMouseEvent *e) {
	auto state = dragActionStart(e->globalPos(), e->button());
	if (((_dragAction == Selecting) || (_dragAction == NoDrag)) && _selectionType == TextSelectType::Letters) {
		if (state.uponSymbol) {
			_dragSymbol = state.symbol;
			_selectionType = _doubleClickSelectsParagraph ? TextSelectType::Paragraphs : TextSelectType::Words;
			if (_dragAction == NoDrag) {
				_dragAction = Selecting;
				_selection = { state.symbol, state.symbol };
				_savedSelection = { 0, 0 };
			}
			mouseMoveEvent(e);

			_trippleClickPoint = e->globalPos();
			_trippleClickTimer.start(QApplication::doubleClickInterval());
		}
	}
}

void FlatLabel::enterEvent(QEvent *e) {
	_lastMousePos = QCursor::pos();
	dragActionUpdate();
}

void FlatLabel::leaveEvent(QEvent *e) {
	ClickHandler::clearActive(this);
}

void FlatLabel::focusOutEvent(QFocusEvent *e) {
	if (!_selection.empty()) {
		if (_contextMenu) {
			_savedSelection = _selection;
		}
		_selection = { 0, 0 };
		update();
	}
}

void FlatLabel::focusInEvent(QFocusEvent *e) {
	if (!_savedSelection.empty()) {
		_selection = _savedSelection;
		_savedSelection = { 0, 0 };
		update();
	}
}

void FlatLabel::keyPressEvent(QKeyEvent *e) {
	e->ignore();
	if (e->key() == Qt::Key_Copy || (e->key() == Qt::Key_C && e->modifiers().testFlag(Qt::ControlModifier))) {
		if (!_selection.empty()) {
			onCopySelectedText();
			e->accept();
		}
#ifdef Q_OS_MAC
	} else if (e->key() == Qt::Key_E && e->modifiers().testFlag(Qt::ControlModifier)) {
		auto selection = _selection.empty() ? (_contextMenu ? _savedSelection : _selection) : _selection;
		if (!selection.empty()) {
			QApplication::clipboard()->setText(_text.originalText(selection, _contextExpandLinksMode), QClipboard::FindBuffer);
		}
#endif // Q_OS_MAC
	}
}

void FlatLabel::contextMenuEvent(QContextMenuEvent *e) {
	if (!_selectable) return;

	showContextMenu(e, ContextMenuReason::FromEvent);
}

bool FlatLabel::event(QEvent *e) {
	if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchUpdate || e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel) {
		QTouchEvent *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == QTouchDevice::TouchScreen) {
			touchEvent(ev);
			return true;
		}
	}
	return QWidget::event(e);
}

void FlatLabel::touchEvent(QTouchEvent *e) {
	const Qt::TouchPointStates &states(e->touchPointStates());
	if (e->type() == QEvent::TouchCancel) { // cancel
		if (!_touchInProgress) return;
		_touchInProgress = false;
		_touchSelectTimer.stop();
		_touchSelect = false;
		_dragAction = NoDrag;
		return;
	}

	if (!e->touchPoints().isEmpty()) {
		_touchPrevPos = _touchPos;
		_touchPos = e->touchPoints().cbegin()->screenPos().toPoint();
	}

	switch (e->type()) {
	case QEvent::TouchBegin:
		if (_contextMenu) {
			e->accept();
			return; // ignore mouse press, that was hiding context menu
		}
		if (_touchInProgress) return;
		if (e->touchPoints().isEmpty()) return;

		_touchInProgress = true;
		_touchSelectTimer.start(QApplication::startDragTime());
		_touchSelect = false;
		_touchStart = _touchPrevPos = _touchPos;
	break;

	case QEvent::TouchUpdate:
		if (!_touchInProgress) return;
		if (_touchSelect) {
			_lastMousePos = _touchPos;
			dragActionUpdate();
		}
	break;

	case QEvent::TouchEnd:
		if (!_touchInProgress) return;
		_touchInProgress = false;
		if (_touchSelect) {
			dragActionFinish(_touchPos, Qt::RightButton);
			QContextMenuEvent contextMenu(QContextMenuEvent::Mouse, mapFromGlobal(_touchPos), _touchPos);
			showContextMenu(&contextMenu, ContextMenuReason::FromTouch);
		} else { // one short tap -- like mouse click
			dragActionStart(_touchPos, Qt::LeftButton);
			dragActionFinish(_touchPos, Qt::LeftButton);
		}
		_touchSelectTimer.stop();
		_touchSelect = false;
	break;
	}
}

void FlatLabel::showContextMenu(QContextMenuEvent *e, ContextMenuReason reason) {
	if (_contextMenu) {
		_contextMenu->deleteLater();
		_contextMenu = nullptr;
	}

	if (e->reason() == QContextMenuEvent::Mouse) {
		_lastMousePos = e->globalPos();
	} else {
		_lastMousePos = QCursor::pos();
	}
	auto state = dragActionUpdate();

	bool hasSelection = !_selection.empty();
	bool uponSelection = state.uponSymbol && (state.symbol >= _selection.from) && (state.symbol < _selection.to);
	bool fullSelection = _text.isFullSelection(_selection);
	if (reason == ContextMenuReason::FromTouch && hasSelection && !uponSelection) {
		uponSelection = hasSelection;
	}

	_contextMenu = new PopupMenu();

	_contextMenuClickHandler = ClickHandler::getActive();

	if (fullSelection && !_contextCopyText.isEmpty()) {
		_contextMenu->addAction(_contextCopyText, this, SLOT(onCopyContextText()))->setEnabled(true);
	} else if (uponSelection && !fullSelection) {
		_contextMenu->addAction(lang(lng_context_copy_selected), this, SLOT(onCopySelectedText()))->setEnabled(true);
	} else if (!hasSelection && !_contextCopyText.isEmpty()) {
		_contextMenu->addAction(_contextCopyText, this, SLOT(onCopyContextText()))->setEnabled(true);
	}

	QString linkCopyToClipboardText = _contextMenuClickHandler ? _contextMenuClickHandler->copyToClipboardContextItemText() : QString();
	if (!linkCopyToClipboardText.isEmpty()) {
		_contextMenu->addAction(linkCopyToClipboardText, this, SLOT(onCopyContextUrl()))->setEnabled(true);
	}

	if (_contextMenu->actions().isEmpty()) {
		delete _contextMenu;
		_contextMenu = nullptr;
	} else {
		connect(_contextMenu, SIGNAL(destroyed(QObject*)), this, SLOT(onContextMenuDestroy(QObject*)));
		_contextMenu->popup(e->globalPos());
		e->accept();
	}
}

void FlatLabel::onCopySelectedText() {
	auto selection = _selection.empty() ? (_contextMenu ? _savedSelection : _selection) : _selection;
	if (!selection.empty()) {
		QApplication::clipboard()->setText(_text.originalText(selection, _contextExpandLinksMode));
	}
}

void FlatLabel::onCopyContextText() {
	QApplication::clipboard()->setText(_text.originalText({ 0, 0xFFFF }, _contextExpandLinksMode));
}

void FlatLabel::onCopyContextUrl() {
	if (_contextMenuClickHandler) {
		_contextMenuClickHandler->copyToClipboard();
	}
}

void FlatLabel::onTouchSelect() {
	_touchSelect = true;
	dragActionStart(_touchPos, Qt::LeftButton);
}

void FlatLabel::onContextMenuDestroy(QObject *obj) {
	if (obj == _contextMenu) {
		_contextMenu = nullptr;
	}
}

void FlatLabel::onExecuteDrag() {
	if (_dragAction != Dragging) return;

	auto state = getTextState(_dragStartPosition);
	bool uponSelected = state.uponSymbol && _selection.from <= state.symbol;
	if (uponSelected) {
		if (_dragSymbol < _selection.from || _dragSymbol >= _selection.to) {
			uponSelected = false;
		}
	}

	ClickHandlerPtr pressedHandler = ClickHandler::getPressed();
	QString selectedText;
	if (uponSelected) {
		selectedText = _text.originalText(_selection, ExpandLinksAll);
	} else if (pressedHandler) {
		selectedText = pressedHandler->dragText();
	}
	if (!selectedText.isEmpty()) {
		auto mimeData = new QMimeData();
		mimeData->setText(selectedText);
		auto drag = new QDrag(App::wnd());
		drag->setMimeData(mimeData);
		drag->exec(Qt::CopyAction);

		// We don't receive mouseReleaseEvent when drag is finished.
		ClickHandler::unpressed();
	}
}

void FlatLabel::clickHandlerActiveChanged(const ClickHandlerPtr &action, bool active) {
	update();
}

void FlatLabel::clickHandlerPressedChanged(const ClickHandlerPtr &action, bool active) {
	update();
}

Text::StateResult FlatLabel::dragActionUpdate() {
	auto m = mapFromGlobal(_lastMousePos);
	auto state = getTextState(m);
	updateHover(state);

	if (_dragAction == PrepareDrag && (m - _dragStartPosition).manhattanLength() >= QApplication::startDragDistance()) {
		_dragAction = Dragging;
		QTimer::singleShot(1, this, SLOT(onExecuteDrag()));
	}

	return state;
}

void FlatLabel::updateHover(const Text::StateResult &state) {
	bool lnkChanged = ClickHandler::setActive(state.link, this);

	if (!_selectable) {
		refreshCursor(state.uponSymbol);
		return;
	}

	Qt::CursorShape cur = style::cur_default;
	if (_dragAction == NoDrag) {
		if (state.link) {
			cur = style::cur_pointer;
		} else if (state.uponSymbol) {
			cur = style::cur_text;
		}
	} else {
		if (_dragAction == Selecting) {
			uint16 second = state.symbol;
			if (state.afterSymbol && _selectionType == TextSelectType::Letters) {
				++second;
			}
			auto selection = _text.adjustSelection({ qMin(second, _dragSymbol), qMax(second, _dragSymbol) }, _selectionType);
			if (_selection != selection) {
				_selection = selection;
				_savedSelection = { 0, 0 };
				setFocus();
				update();
			}
		} else if (_dragAction == Dragging) {
		}

		if (ClickHandler::getPressed()) {
			cur = style::cur_pointer;
		} else if (_dragAction == Selecting) {
			cur = style::cur_text;
		}
	}
	if (_dragAction == Selecting) {
//		checkSelectingScroll();
	} else {
//		noSelectingScroll();
	}

	if (_dragAction == NoDrag && (lnkChanged || cur != _cursor)) {
		setCursor(_cursor = cur);
	}
}

void FlatLabel::refreshCursor(bool uponSymbol) {
	if (_dragAction != NoDrag) {
		return;
	}
	bool needTextCursor = _selectable && uponSymbol;
	style::cursor newCursor = needTextCursor ? style::cur_text : style::cur_default;
	if (ClickHandler::getActive()) {
		newCursor = style::cur_pointer;
	}
	if (newCursor != _cursor) {
		_cursor = newCursor;
		setCursor(_cursor);
	}
}

Text::StateResult FlatLabel::getTextState(const QPoint &m) const {
	Text::StateRequestElided request;
	request.align = _st.align;
	if (_selectable) {
		request.flags |= Text::StateRequest::Flag::LookupSymbol;
	}
	int textWidth = width() - _st.margin.left() - _st.margin.right();

	textstyleSet(&_tst);
	Text::StateResult state;
	bool heightExceeded = _st.maxHeight && (_st.maxHeight < _fullTextHeight || textWidth < _text.maxWidth());
	bool renderElided = _breakEverywhere || heightExceeded;
	if (renderElided) {
		auto lineHeight = qMax(_tst.lineHeight, _st.font->height);
		auto lines = _st.maxHeight ? qMax(_st.maxHeight / lineHeight, 1) : ((height() / lineHeight) + 2);
		request.lines = lines;
		if (_breakEverywhere) {
			request.flags |= Text::StateRequest::Flag::BreakEverywhere;
		}
		state = _text.getStateElided(m.x() - _st.margin.left(), m.y() - _st.margin.top(), textWidth, request);
	} else {
		state = _text.getState(m.x() - _st.margin.left(), m.y() - _st.margin.top(), textWidth, request);
	}
	textstyleRestore();

	return state;
}

void FlatLabel::setOpacity(float64 o) {
	_opacity = o;
	update();
}

void FlatLabel::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.setOpacity(_opacity);
	p.setPen(_st.textFg);
	textstyleSet(&_tst);
	int textWidth = width() - _st.margin.left() - _st.margin.right();
	auto selection = _selection.empty() ? (_contextMenu ? _savedSelection : _selection) : _selection;
	bool heightExceeded = _st.maxHeight && (_st.maxHeight < _fullTextHeight || textWidth < _text.maxWidth());
	bool renderElided = _breakEverywhere || heightExceeded;
	if (renderElided) {
		auto lineHeight = qMax(_tst.lineHeight, _st.font->height);
		auto lines = _st.maxHeight ? qMax(_st.maxHeight / lineHeight, 1) : ((height() / lineHeight) + 2);
		_text.drawElided(p, _st.margin.left(), _st.margin.top(), textWidth, lines, _st.align, e->rect().y(), e->rect().bottom(), 0, _breakEverywhere, selection);
	} else {
		_text.draw(p, _st.margin.left(), _st.margin.top(), textWidth, _st.align, e->rect().y(), e->rect().bottom(), selection);
	}
	textstyleRestore();
}
