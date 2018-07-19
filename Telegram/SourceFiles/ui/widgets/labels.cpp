/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/labels.h"

#include "ui/widgets/popup_menu.h"
#include "mainwindow.h"
#include "lang/lang_keys.h"

namespace Ui {
namespace {

TextParseOptions _labelOptions = {
	TextParseMultiline, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

TextParseOptions _labelMarkedOptions = {
	TextParseMultiline | TextParseRichText | TextParseLinks | TextParseHashtags | TextParseMentions | TextParseBotCommands | TextParseMarkdown, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

} // namespace

CrossFadeAnimation::CrossFadeAnimation(style::color bg) : _bg(bg) {
}

void CrossFadeAnimation::addLine(Part was, Part now) {
	_lines.push_back(Line(std::move(was), std::move(now)));
}

void CrossFadeAnimation::paintFrame(Painter &p, float64 positionReady, float64 alphaWas, float64 alphaNow) {
	if (_lines.isEmpty()) return;

	for_const (auto &line, _lines) {
		paintLine(p, line, positionReady, alphaWas, alphaNow);
	}
}

void CrossFadeAnimation::paintLine(Painter &p, const Line &line, float64 positionReady, float64 alphaWas, float64 alphaNow) {
	auto &snapshotWas = line.was.snapshot;
	auto &snapshotNow = line.now.snapshot;
	if (snapshotWas.isNull() && snapshotNow.isNull()) {
		// This can happen if both labels have an empty line or if one
		// label has an empty line where the second one already ended.
		// In this case lineWidth is zero and snapshot is null.
		return;
	}

	auto positionWas = line.was.position;
	auto positionNow = line.now.position;
	auto left = anim::interpolate(positionWas.x(), positionNow.x(), positionReady);
	auto topDelta = (snapshotNow.height() / cIntRetinaFactor()) - (snapshotWas.height() / cIntRetinaFactor());
	auto widthDelta = (snapshotNow.width() / cIntRetinaFactor()) - (snapshotWas.width() / cIntRetinaFactor());
	auto topWas = anim::interpolate(positionWas.y(), positionNow.y() + topDelta, positionReady);
	auto topNow = topWas - topDelta;

	p.setOpacity(alphaWas);
	if (!snapshotWas.isNull()) {
		p.drawPixmap(left, topWas, snapshotWas);
		if (topDelta > 0) {
			p.fillRect(left, topWas - topDelta, snapshotWas.width() / cIntRetinaFactor(), topDelta, _bg);
		}
	}
	if (widthDelta > 0) {
		p.fillRect(left + (snapshotWas.width() / cIntRetinaFactor()), topNow, widthDelta, snapshotNow.height() / cIntRetinaFactor(), _bg);
	}

	p.setOpacity(alphaNow);
	if (!snapshotNow.isNull()) {
		p.drawPixmap(left, topNow, snapshotNow);
		if (topDelta < 0) {
			p.fillRect(left, topNow + topDelta, snapshotNow.width() / cIntRetinaFactor(), -topDelta, _bg);
		}
	}
	if (widthDelta < 0) {
		p.fillRect(left + (snapshotNow.width() / cIntRetinaFactor()), topWas, -widthDelta, snapshotWas.height() / cIntRetinaFactor(), _bg);
	}
}

LabelSimple::LabelSimple(
	QWidget *parent,
	const style::LabelSimple &st,
	const QString &value)
: RpWidget(parent)
, _st(st) {
	setText(value);
}

void LabelSimple::setText(const QString &value, bool *outTextChanged) {
	if (_fullText == value) {
		if (outTextChanged) *outTextChanged = false;
		return;
	}

	_fullText = value;
	_fullTextWidth = _st.font->width(_fullText);
	if (!_st.maxWidth || _fullTextWidth <= _st.maxWidth) {
		_text = _fullText;
		_textWidth = _fullTextWidth;
	} else {
		auto newText = _st.font->elided(_fullText, _st.maxWidth);
		if (newText == _text) {
			if (outTextChanged) *outTextChanged = false;
			return;
		}
		_text = newText;
		_textWidth = _st.font->width(_text);
	}
	resize(_textWidth, _st.font->height);
	update();
	if (outTextChanged) *outTextChanged = true;
}

void LabelSimple::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.setFont(_st.font);
	p.setPen(_st.textFg);
	p.drawTextLeft(0, 0, width(), _text, _textWidth);
}

FlatLabel::FlatLabel(QWidget *parent, const style::FlatLabel &st)
: RpWidget(parent)
, _text(st.minWidth ? st.minWidth : QFIXED_MAX)
, _st(st)
, _contextCopyText(lang(lng_context_copy_text)) {
	init();
}

FlatLabel::FlatLabel(
	QWidget *parent,
	const QString &text,
	InitType initType,
	const style::FlatLabel &st)
: RpWidget(parent)
, _text(st.minWidth ? st.minWidth : QFIXED_MAX)
, _st(st)
, _contextCopyText(lang(lng_context_copy_text)) {
	if (initType == InitType::Rich) {
		setRichText(text);
	} else {
		setText(text);
	}
	init();
}

FlatLabel::FlatLabel(
	QWidget *parent,
	rpl::producer<QString> &&text,
	const style::FlatLabel &st)
: RpWidget(parent)
, _text(st.minWidth ? st.minWidth : QFIXED_MAX)
, _st(st)
, _contextCopyText(lang(lng_context_copy_text)) {
	textUpdated();
	std::move(
		text
	) | rpl::start_with_next([this](const QString &value) {
		setText(value);
	}, lifetime());
}

FlatLabel::FlatLabel(
	QWidget *parent,
	rpl::producer<TextWithEntities> &&text,
	const style::FlatLabel &st)
: RpWidget(parent)
, _text(st.minWidth ? st.minWidth : QFIXED_MAX)
, _st(st)
, _contextCopyText(lang(lng_context_copy_text)) {
	textUpdated();
	std::move(
		text
	) | rpl::start_with_next([this](const TextWithEntities &value) {
		setMarkedText(value);
	}, lifetime());
}

void FlatLabel::init() {
	_trippleClickTimer.setSingleShot(true);

	_touchSelectTimer.setSingleShot(true);
	connect(&_touchSelectTimer, SIGNAL(timeout()), this, SLOT(onTouchSelect()));
}

void FlatLabel::textUpdated() {
	refreshSize();
	setMouseTracking(_selectable || _text.hasLinks());
	update();
}

void FlatLabel::setText(const QString &text) {
	_text.setText(_st.style, text, _labelOptions);
	textUpdated();
}

void FlatLabel::setRichText(const QString &text) {
	_text.setRichText(_st.style, text, _labelOptions);
	textUpdated();
}

void FlatLabel::setMarkedText(const TextWithEntities &textWithEntities) {
	_text.setMarkedText(_st.style, textWithEntities, _labelMarkedOptions);
	textUpdated();
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

int FlatLabel::resizeGetHeight(int newWidth) {
	_allowedWidth = newWidth;
	int textWidth = countTextWidth();
	int textHeight = countTextHeight(textWidth);
	return textHeight;
}

int FlatLabel::naturalWidth() const {
	return _text.maxWidth();
}

QMargins FlatLabel::getMargins() const {
	return _st.margin;
}

int FlatLabel::countTextWidth() const {
	return _allowedWidth
		? _allowedWidth
		: (_st.minWidth ? _st.minWidth : _text.maxWidth());
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

void FlatLabel::setClickHandlerFilter(ClickHandlerFilter &&filter) {
	_clickHandlerFilter = std::move(filter);
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
	_dragWasInactive = App::wnd()->wasInactivePress();
	if (_dragWasInactive) App::wnd()->setInactivePress(false);

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

	auto activated = ClickHandler::unpressed();
	if (_dragAction == Dragging) {
		activated = nullptr;
	} else if (_dragAction == PrepareDrag) {
		_selection = { 0, 0 };
		_savedSelection = { 0, 0 };
		update();
	}
	_dragAction = NoDrag;
	_selectionType = TextSelectType::Letters;

	if (activated) {
		if (!_clickHandlerFilter
			|| _clickHandlerFilter(activated, button)) {
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

void FlatLabel::enterEventHook(QEvent *e) {
	_lastMousePos = QCursor::pos();
	dragActionUpdate();
}

void FlatLabel::leaveEventHook(QEvent *e) {
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

bool FlatLabel::eventHook(QEvent *e) {
	if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchUpdate || e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel) {
		QTouchEvent *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == QTouchDevice::TouchScreen) {
			touchEvent(ev);
			return true;
		}
	}
	return RpWidget::eventHook(e);
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
	case QEvent::TouchBegin: {
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
	} break;

	case QEvent::TouchUpdate: {
		if (!_touchInProgress) return;
		if (_touchSelect) {
			_lastMousePos = _touchPos;
			dragActionUpdate();
		}
	} break;

	case QEvent::TouchEnd: {
		if (!_touchInProgress) return;
		_touchInProgress = false;
		auto weak = make_weak(this);
		if (_touchSelect) {
			dragActionFinish(_touchPos, Qt::RightButton);
			QContextMenuEvent contextMenu(QContextMenuEvent::Mouse, mapFromGlobal(_touchPos), _touchPos);
			showContextMenu(&contextMenu, ContextMenuReason::FromTouch);
		} else { // one short tap -- like mouse click
			dragActionStart(_touchPos, Qt::LeftButton);
			dragActionFinish(_touchPos, Qt::LeftButton);
		}
		if (weak) {
			_touchSelectTimer.stop();
			_touchSelect = false;
		}
	} break;
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

	_contextMenu = new Ui::PopupMenu(this);

	if (fullSelection && !_contextCopyText.isEmpty()) {
		_contextMenu->addAction(_contextCopyText, this, SLOT(onCopyContextText()));
	} else if (uponSelection && !fullSelection) {
		_contextMenu->addAction(lang(lng_context_copy_selected), this, SLOT(onCopySelectedText()));
	} else if (!hasSelection && !_contextCopyText.isEmpty()) {
		_contextMenu->addAction(_contextCopyText, this, SLOT(onCopyContextText()));
	}

	if (const auto link = ClickHandler::getActive()) {
		const auto actionText = link->copyToClipboardContextItemText();
		if (!actionText.isEmpty()) {
			_contextMenu->addAction(
				actionText,
				[text = link->copyToClipboardText()] {
					QApplication::clipboard()->setText(text);
				});
		}
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

std::unique_ptr<CrossFadeAnimation> FlatLabel::CrossFade(
		not_null<FlatLabel*> from,
		not_null<FlatLabel*> to,
		style::color bg,
		QPoint fromPosition,
		QPoint toPosition) {
	auto result = std::make_unique<CrossFadeAnimation>(bg);

	struct Data {
		QImage full;
		QVector<int> lineWidths;
		int lineHeight = 0;
		int lineAddTop = 0;
	};
	auto prepareData = [&bg](not_null<FlatLabel*> label) {
		auto result = Data();
		result.full = GrabWidgetToImage(label, QRect(), bg->c);
		auto textWidth = label->width() - label->_st.margin.left() - label->_st.margin.right();
		label->_text.countLineWidths(textWidth, &result.lineWidths);
		result.lineHeight = label->_st.style.font->height;
		auto addedHeight = (label->_st.style.lineHeight - result.lineHeight);
		if (addedHeight > 0) {
			result.lineAddTop = addedHeight / 2;
			result.lineHeight += addedHeight;
		}
		return result;
	};
	auto was = prepareData(from);
	auto now = prepareData(to);

	auto maxLines = qMax(was.lineWidths.size(), now.lineWidths.size());
	auto fillDataTill = [maxLines](Data &data) {
		for (auto i = data.lineWidths.size(); i != maxLines; ++i) {
			data.lineWidths.push_back(-1);
		}
	};
	fillDataTill(was);
	fillDataTill(now);
	auto preparePart = [](FlatLabel *label, QPoint position, Data &data, int index, Data &other) {
		auto result = CrossFadeAnimation::Part();
		auto lineWidth = data.lineWidths[index];
		if (lineWidth < 0) {
			lineWidth = other.lineWidths[index];
		}
		auto fullWidth = data.full.width() / cIntRetinaFactor();
		auto top = index * data.lineHeight + data.lineAddTop;
		auto left = 0;
		if (label->_st.align & Qt::AlignHCenter) {
			left += (fullWidth - lineWidth) / 2;
		} else if (label->_st.align & Qt::AlignRight) {
			left += (fullWidth - lineWidth);
		}
		auto snapshotRect = data.full.rect().intersected(QRect(left * cIntRetinaFactor(), top * cIntRetinaFactor(), lineWidth * cIntRetinaFactor(), label->_st.style.font->height * cIntRetinaFactor()));
		if (!snapshotRect.isEmpty()) {
			result.snapshot = App::pixmapFromImageInPlace(data.full.copy(snapshotRect));
			result.snapshot.setDevicePixelRatio(cRetinaFactor());
		}
		auto positionBase = position + label->pos();
		result.position = positionBase + QPoint(label->_st.margin.left() + left, label->_st.margin.top() + top);
		return result;
	};
	for (int i = 0; i != maxLines; ++i) {
		result->addLine(preparePart(from, fromPosition, was, i, now), preparePart(to, toPosition, now, i, was));
	}

	return result;
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

	Text::StateResult state;
	bool heightExceeded = _st.maxHeight && (_st.maxHeight < _fullTextHeight || textWidth < _text.maxWidth());
	bool renderElided = _breakEverywhere || heightExceeded;
	if (renderElided) {
		auto lineHeight = qMax(_st.style.lineHeight, _st.style.font->height);
		auto lines = _st.maxHeight ? qMax(_st.maxHeight / lineHeight, 1) : ((height() / lineHeight) + 2);
		request.lines = lines;
		if (_breakEverywhere) {
			request.flags |= Text::StateRequest::Flag::BreakEverywhere;
		}
		state = _text.getStateElided(m - QPoint(_st.margin.left(), _st.margin.top()), textWidth, request);
	} else {
		state = _text.getState(m - QPoint(_st.margin.left(), _st.margin.top()), textWidth, request);
	}

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
	p.setTextPalette(_st.palette);
	int textWidth = width() - _st.margin.left() - _st.margin.right();
	auto selection = _selection.empty() ? (_contextMenu ? _savedSelection : _selection) : _selection;
	bool heightExceeded = _st.maxHeight && (_st.maxHeight < _fullTextHeight || textWidth < _text.maxWidth());
	bool renderElided = _breakEverywhere || heightExceeded;
	if (renderElided) {
		auto lineHeight = qMax(_st.style.lineHeight, _st.style.font->height);
		auto lines = _st.maxHeight ? qMax(_st.maxHeight / lineHeight, 1) : ((height() / lineHeight) + 2);
		_text.drawElided(p, _st.margin.left(), _st.margin.top(), textWidth, lines, _st.align, e->rect().y(), e->rect().bottom(), 0, _breakEverywhere, selection);
	} else {
		_text.draw(p, _st.margin.left(), _st.margin.top(), textWidth, _st.align, e->rect().y(), e->rect().bottom(), selection);
	}
}

int DividerLabel::naturalWidth() const {
	return -1;
}

void DividerLabel::resizeEvent(QResizeEvent *e) {
	_background->lower();
	_background->setGeometry(rect());
	return PaddingWrap::resizeEvent(e);
}

} // namespace Ui
