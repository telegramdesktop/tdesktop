/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/marquee_label.h"

#include "base/event_filter.h"
#include "base/invoke_queued.h"
#include "ui/inactive_press.h"
#include "ui/integration.h"
#include "ui/painter.h"
#include "ui/text/text_entity.h"
#include "ui/text/text_options.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_marquee_label.h"
#include "styles/style_widgets.h"

#include <QtWidgets/QApplication>
#include <QtGui/QClipboard>
#include <QtGui/QCursor>
#include <QtGui/QGuiApplication>

namespace Ui {
namespace {

constexpr auto kMarqueeDelay = crl::time(500);
constexpr auto kFrameDelta = crl::time(16);
constexpr auto kEdgeScrollSpeed = 3.; // Speed per pixel of edge distance.

} // namespace

MarqueeLabel::MarqueeLabel(
	QWidget *parent,
	rpl::producer<QString> text,
	const style::FlatLabel &st)
: RpWidget(parent)
, _st(st)
, _marquee([=](crl::time now) { return marqueeStep(now); })
, _edgeScroll([=](crl::time now) { return edgeScrollStep(now); }) {
	std::move(
		text
	) | rpl::on_next([=](const QString &value) {
		setText(value);
	}, lifetime());

	anim::Disables(
	) | rpl::on_next([=](bool) {
		refreshMarqueeState();
	}, lifetime());

	style::PaletteChanged(
	) | rpl::on_next([=] {
		invalidateCache();
		update();
	}, lifetime());
}

void MarqueeLabel::setTextColorOverride(std::optional<QColor> color) {
	if (_textColorOverride == color) {
		return;
	}
	_textColorOverride = color;
	invalidateCache();
	update();
}

void MarqueeLabel::setContextCopyText(const QString &copyText) {
	_contextCopyText = copyText;
}

void MarqueeLabel::setSelectable(bool selectable) {
	if (_selectable == selectable) {
		return;
	}
	_selectable = selectable;
	setMouseTracking(_selectable);
	if (!_selectable) {
		clearSelection();
		_inside = false;
		updateCursor({});
		refreshMarqueeState();
		update();
	}
}

QAccessible::Role MarqueeLabel::accessibilityRole() {
	return QAccessible::Role::StaticText;
}

QString MarqueeLabel::accessibilityName() {
	return _text.toString();
}

void MarqueeLabel::setText(const QString &text) {
	_text.setText(_st.style, text, NameTextOptions());
	accessibilityNameChanged();
	invalidateCache();
	clearSelection();
	_offset = 0.;
	_delay = kMarqueeDelay;
	_lastUpdate = crl::now();
	setNaturalWidth(_text.maxWidth());
	refreshMarqueeState();
	update();
}

int MarqueeLabel::resizeGetHeight(int newWidth) {
	_availableTextWidth = newWidth;
	refreshMarqueeState();
	const auto full = _text.isEmpty()
		? _st.style.font->height
		: _text.countHeight(_text.maxWidth());
	return _st.maxHeight ? std::min(full, _st.maxHeight) : full;
}

void MarqueeLabel::showEvent(QShowEvent *e) {
	refreshMarqueeState();
}

void MarqueeLabel::hideEvent(QHideEvent *e) {
	clearSelection();
	_inside = false;
	refreshMarqueeState();
}

bool MarqueeLabel::eventHook(QEvent *e) {
	const auto type = e->type();
	if ((type == QEvent::WindowActivate)
		|| (type == QEvent::WindowDeactivate)) {
		_windowActive = (type == QEvent::WindowActivate);
		refreshMarqueeState();
	}
	return RpWidget::eventHook(e);
}

bool MarqueeLabel::paused() const {
	return _selectable
		&& (_inside
			|| _selecting
			|| !_selection.empty()
			|| (_menu != nullptr));
}

float64 MarqueeLabel::linearMax() const {
	return float64(std::max(_text.maxWidth() - width(), 0));
}

void MarqueeLabel::refreshMarqueeState() {
	const auto was = _overflown;
	_overflown = (_availableTextWidth > 0)
		&& (_text.maxWidth() > _availableTextWidth);
	if (was != _overflown) {
		_offset = 0.;
		_edgeScroll.stop();
	}
	const auto scroll = _overflown
		&& isVisible()
		&& _windowActive
		&& !anim::Disabled()
		&& !paused();
	if (scroll && !_marquee.animating()) {
		if (_offset < 0.) {
			_offset += _text.maxWidth() + st::marqueeLabelGap;
		}
		_delay = kMarqueeDelay;
		_lastUpdate = crl::now();
		_lastFrame = 0;
		_marquee.start();
	} else if (!scroll && _marquee.animating()) {
		_marquee.stop();
		if (!paused() && _windowActive) {
			_offset = 0.;
		}
		update();
	}
	if (was != _overflown) {
		update();
	}
	refreshOutsideClickFilter();
}

void MarqueeLabel::refreshOutsideClickFilter() {
	const auto needed = _selectable
		&& (!_selection.empty() || (_menu != nullptr));
	if (needed && !_outsideClickFilter) {
		_outsideClickFilter.reset(base::install_event_filter(
			this,
			QCoreApplication::instance(),
			[=](not_null<QEvent*> e) {
				handleOutsidePress(e.get());
				return base::EventFilterResult::Continue;
			}).get());
	} else if (!needed && _outsideClickFilter) {
		_outsideClickFilter = nullptr;
	}
}

void MarqueeLabel::handleOutsidePress(QEvent *e) {
	if ((e->type() != QEvent::MouseButtonPress) || _menu) {
		return;
	}
	const auto mouse = static_cast<QMouseEvent*>(e);
	const auto global = mouse->globalPos();
	if (QRect(mapToGlobal(QPoint(0, 0)), size()).contains(global)) {
		return;
	}
	_selection = { 0, 0 };
	_savedSelection = { 0, 0 };
	update();
	InvokeQueued(this, [=] {
		refreshMarqueeState();
	});
}

bool MarqueeLabel::marqueeStep(crl::time now) {
	const auto dt = std::min(now - _lastUpdate, kFrameDelta);
	_lastUpdate = now;
	if (_delay > 0) {
		_delay -= dt;
		return true;
	}
	const auto total = float64(_text.maxWidth() + st::marqueeLabelGap);
	const auto slowdown = float64(st::marqueeLabelSlowdown);
	const auto fast = float64(st::marqueeLabelSpeed);
	const auto slow = float64(st::marqueeLabelSpeedSlow);
	auto speed = fast;
	if (_offset < slowdown) {
		speed = slow + (fast - slow) * (_offset / slowdown);
	} else if (_offset >= total - slowdown) {
		const auto dist = _offset - (total - slowdown);
		speed = fast - (fast - slow) * (dist / slowdown);
	}
	_offset += (dt / 1000.) * speed;
	if (_offset > total) {
		_offset = 0.;
		_delay = kMarqueeDelay;
	}
	if (now - _lastFrame >= kFrameDelta) {
		_lastFrame = now;
		update();
	}
	return true;
}

void MarqueeLabel::paintEvent(QPaintEvent *e) {
	if (_text.isEmpty()) {
		return;
	}
	Painter p(this);
	const auto selection = !_selection.empty()
		? _selection
		: _menu
		? _savedSelection
		: TextSelection();
	if (_overflown) {
		paintMarquee(p, selection);
		return;
	}
	if (_textColorOverride) {
		p.setPen(*_textColorOverride);
	} else {
		p.setPen(_st.textFg);
	}
	p.setTextPalette(_st.palette);
	_text.draw(p, {
		.position = { 0, 0 },
		.availableWidth = width(),
		.align = _st.align,
		.clip = e->rect(),
		.palette = &_st.palette,
		.now = crl::now(),
		.selection = selection,
	});
}

void MarqueeLabel::invalidateCache() {
	_tape = QImage();
	_frameOffset = std::nullopt;
}

void MarqueeLabel::validateTape() {
	const auto ratio = style::DevicePixelRatio();
	const auto natural = _text.maxWidth();
	const auto size = QSize(natural + st::marqueeLabelGap, height());
	if (_tape.size() == size * ratio) {
		return;
	}
	_frameOffset = std::nullopt;
	_tape = QImage(size * ratio, QImage::Format_ARGB32_Premultiplied);
	_tape.setDevicePixelRatio(ratio);
	_tape.fill(Qt::transparent);
	auto q = Painter(&_tape);
	if (_textColorOverride) {
		q.setPen(*_textColorOverride);
	} else {
		q.setPen(_st.textFg);
	}
	q.setTextPalette(_st.palette);
	_text.draw(q, {
		.position = { 0, 0 },
		.availableWidth = natural,
		.palette = &_st.palette,
		.now = crl::now(),
	});
}

void MarqueeLabel::validateFades() {
	const auto ratio = style::DevicePixelRatio();
	const auto fade = st::marqueeLabelFade;
	const auto size = QSize(fade, height());
	if (_fadeRight.size() == size * ratio) {
		return;
	}
	_frameOffset = std::nullopt;
	const auto prepare = [&](int fromAlpha, int tillAlpha) {
		auto result = QImage(
			size * ratio,
			QImage::Format_ARGB32_Premultiplied);
		result.setDevicePixelRatio(ratio);
		result.fill(Qt::transparent);
		auto q = QPainter(&result);
		auto gradient = QLinearGradient(0, 0, fade, 0);
		gradient.setColorAt(0., QColor(255, 255, 255, fromAlpha));
		gradient.setColorAt(1., QColor(255, 255, 255, tillAlpha));
		q.fillRect(QRect(QPoint(), size), gradient);
		return result;
	};
	_fadeLeft = prepare(255, 0);
	_fadeRight = prepare(0, 255);
}

void MarqueeLabel::validateFrame(float64 offset, TextSelection selection) {
	const auto ratio = style::DevicePixelRatio();
	const auto full = size() * ratio;
	if (_frame.size() != full) {
		_frame = QImage(full, QImage::Format_ARGB32_Premultiplied);
		_frame.setDevicePixelRatio(ratio);
		_frameOffset = std::nullopt;
	}
	if (selection.empty()) {
		validateTape();
	}
	validateFades();
	const auto honestFades = paused();
	if (_frameOffset == offset
		&& _frameSelection == selection
		&& _frameHonestFades == honestFades) {
		return;
	}
	_frameOffset = offset;
	_frameSelection = selection;
	_frameHonestFades = honestFades;

	const auto fade = st::marqueeLabelFade;
	const auto gap = float64(st::marqueeLabelGap);
	const auto natural = float64(_text.maxWidth());
	const auto total = natural + gap;
	const auto hasNext = (offset + width() > total);
	const auto hasPrevious = (offset < -gap);
	_frame.fill(Qt::transparent);
	auto q = Painter(&_frame);
	if (selection.empty()) {
		q.drawImage(QPointF(-offset, 0), _tape);
		if (hasNext) {
			q.drawImage(QPointF(total - offset, 0), _tape);
		}
		if (hasPrevious) {
			q.drawImage(QPointF(-offset - total, 0), _tape);
		}
	} else {
		if (_textColorOverride) {
			q.setPen(*_textColorOverride);
		} else {
			q.setPen(_st.textFg);
		}
		q.setTextPalette(_st.palette);
		const auto drawText = [&](float64 left) {
			q.translate(left, 0);
			_text.draw(q, {
				.position = { 0, 0 },
				.availableWidth = _text.maxWidth(),
				.palette = &_st.palette,
				.now = crl::now(),
				.selection = selection,
			});
			q.translate(-left, 0);
		};
		drawText(-offset);
		if (hasNext) {
			drawText(total - offset);
		}
		if (hasPrevious) {
			drawText(-offset - total);
		}
	}
	q.setCompositionMode(QPainter::CompositionMode_DestinationOut);
	const auto ramp = float64(st::marqueeLabelFadeRamp);
	const auto leftOpacity = !honestFades
		? ((offset < ramp)
			? (offset / ramp)
			: (offset > total - ramp)
			? (1. - (offset - (total - ramp)) / ramp)
			: 1.)
		: (offset < 0.)
		? std::clamp((-offset - gap) / ramp, 0., 1.)
		: (offset > linearMax())
		? std::clamp((natural - offset) / ramp, 0., 1.)
		: std::clamp(offset / ramp, 0., 1.);
	const auto rightOpacity = !honestFades
		? 1.
		: (offset < 0.)
		? 1.
		: (offset > linearMax())
		? (hasNext ? 1. : 0.)
		: std::clamp((linearMax() - offset) / ramp, 0., 1.);
	if (leftOpacity > 0.) {
		q.setOpacity(leftOpacity);
		q.drawImage(QPointF(0, 0), _fadeLeft);
	}
	if (rightOpacity > 0.) {
		q.setOpacity(rightOpacity);
		q.drawImage(QPointF(width() - fade, 0), _fadeRight);
	}
}

void MarqueeLabel::paintMarquee(Painter &p, TextSelection selection) {
	const auto ratio = style::DevicePixelRatio();
	validateFrame(std::round(_offset * ratio) / ratio, selection);
	p.drawImage(0, 0, _frame);
}

Text::StateResult MarqueeLabel::getTextState(QPoint m) const {
	auto request = Text::StateRequest();
	request.flags |= Text::StateRequest::Flag::LookupSymbol;
	m.setY(std::clamp(m.y(), 0, std::max(height() - 1, 0)));
	if (_overflown) {
		const auto gap = st::marqueeLabelGap;
		const auto total = _text.maxWidth() + gap;
		auto x = m.x() + int(std::round(_offset));
		if (!_selecting) {
			if ((_offset > linearMax()) && (x >= total)) {
				x -= total;
			} else if ((_offset < 0.) && (x < -gap)) {
				x += total;
			}
		}
		return _text.getState({ x, m.y() }, _text.maxWidth(), request);
	}
	request.align = _st.align;
	return _text.getState(m, width(), request);
}

Text::StateResult MarqueeLabel::dragActionUpdate() {
	const auto state = getTextState(mapFromGlobal(_lastMousePos));
	if (_selecting) {
		updateSelection(state);
	} else {
		updateCursor(state);
	}
	return state;
}

void MarqueeLabel::updateSelection(const Text::StateResult &state) {
	auto second = state.symbol;
	if (state.afterSymbol && _selectionType == TextSelectType::Letters) {
		++second;
	}
	const auto selection = _text.adjustSelection(
		{ std::min(second, _dragSymbol), std::max(second, _dragSymbol) },
		_selectionType);
	if (_selection != selection) {
		_selection = selection;
		_savedSelection = { 0, 0 };
		setFocus();
		update();
		refreshOutsideClickFilter();
	}
}

void MarqueeLabel::updateCursor(const Text::StateResult &state) {
	const auto cursor = (_selectable && state.uponSymbol)
		? style::cur_text
		: style::cur_default;
	if (_cursor != cursor) {
		_cursor = cursor;
		setCursor(_cursor);
	}
}

void MarqueeLabel::clearSelection() {
	_selecting = false;
	_selection = { 0, 0 };
	_savedSelection = { 0, 0 };
	_edgeScroll.stop();
}

void MarqueeLabel::copySelectedText() {
	const auto selection = _selection.empty()
		? (_menu ? _savedSelection : _selection)
		: _selection;
	if (!selection.empty()) {
		TextUtilities::SetClipboardText(_text.toTextForMimeData(selection));
	}
}

void MarqueeLabel::reanchorWrapped() {
	if (!_overflown) {
		return;
	}
	const auto gap = float64(st::marqueeLabelGap);
	const auto natural = float64(_text.maxWidth());
	const auto total = natural + gap;
	const auto tape = mapFromGlobal(_lastMousePos).x() + _offset;
	if (_offset > linearMax()) {
		const auto head = (tape >= total)
			|| ((tape >= natural) && ((tape - natural) >= (total - tape)));
		if (head) {
			_offset -= total;
			update();
		}
	} else if (_offset < 0.) {
		const auto tail = (tape < -gap)
			|| ((tape < 0.) && ((-tape) >= (tape + gap)));
		if (tail) {
			_offset += total;
			update();
		}
	}
}

void MarqueeLabel::checkEdgeScroll() {
	if (!_selecting || !_overflown) {
		_edgeScroll.stop();
		return;
	}
	const auto x = mapFromGlobal(_lastMousePos).x();
	const auto delta = (x < 0) ? x : (x > width()) ? (x - width()) : 0;
	const auto can = ((delta < 0) && (_offset > 0.))
		|| ((delta > 0) && (_offset < linearMax()));
	if (can && !_edgeScroll.animating()) {
		_lastUpdate = crl::now();
		_edgeScroll.start();
	} else if (!can) {
		_edgeScroll.stop();
	}
}

bool MarqueeLabel::edgeScrollStep(crl::time now) {
	if (!_selecting || !_overflown) {
		return false;
	}
	const auto dt = std::min(now - _lastUpdate, kFrameDelta);
	_lastUpdate = now;
	if (dt <= 0) {
		return true;
	}
	const auto x = mapFromGlobal(_lastMousePos).x();
	const auto delta = (x < 0) ? x : (x > width()) ? (x - width()) : 0;
	if (!delta) {
		return false;
	}
	const auto was = _offset;
	const auto next = was + delta * kEdgeScrollSpeed * (dt / 1000.);
	_offset = (delta > 0)
		? std::min(next, std::max(was, linearMax()))
		: std::max(next, std::min(was, 0.));
	if (_offset == was) {
		return false;
	}
	updateSelection(getTextState(mapFromGlobal(_lastMousePos)));
	update();
	return true;
}

void MarqueeLabel::dragActionStart(QPoint position, Qt::MouseButton button) {
	_lastMousePos = position;
	if (button == Qt::LeftButton) {
		reanchorWrapped();
	}
	const auto state = dragActionUpdate();
	if (button != Qt::LeftButton) {
		return;
	}
	_dragWasInactive = WasInactivePress(window());
	if (_dragWasInactive) {
		MarkInactivePress(window(), false);
	}
	if (_trippleClickTimer.isActive()
		&& ((position - _trippleClickPoint).manhattanLength()
			< QApplication::startDragDistance())) {
		if (state.uponSymbol) {
			_selection = { state.symbol, state.symbol };
			_savedSelection = { 0, 0 };
			_dragSymbol = state.symbol;
			_selecting = true;
			_selectionType = TextSelectType::Paragraphs;
			updateSelection(state);
			_trippleClickTimer.callOnce(QApplication::doubleClickInterval());
			update();
		}
	}
	if (_selectionType != TextSelectType::Paragraphs) {
		_dragSymbol = state.symbol;
		if (!_dragWasInactive) {
			if (state.afterSymbol) {
				++_dragSymbol;
			}
			_selection = { _dragSymbol, _dragSymbol };
			_savedSelection = { 0, 0 };
			_selecting = true;
			update();
		}
	}
	refreshMarqueeState();
}

void MarqueeLabel::dragActionFinish(QPoint position, Qt::MouseButton button) {
	_lastMousePos = position;
	dragActionUpdate();
	_selecting = false;
	_selectionType = TextSelectType::Letters;
	_edgeScroll.stop();
	if (QGuiApplication::clipboard()->supportsSelection()
		&& !_selection.empty()) {
		TextUtilities::SetClipboardText(
			_text.toTextForMimeData(_selection),
			QClipboard::Selection);
	}
	refreshMarqueeState();
}

void MarqueeLabel::mouseMoveEvent(QMouseEvent *e) {
	if (!_selectable) {
		RpWidget::mouseMoveEvent(e);
		return;
	}
	_lastMousePos = e->globalPos();
	dragActionUpdate();
	checkEdgeScroll();
}

void MarqueeLabel::mousePressEvent(QMouseEvent *e) {
	if (_menu) {
		e->accept();
		return;
	} else if (!_selectable) {
		RpWidget::mousePressEvent(e);
		return;
	}
	dragActionStart(e->globalPos(), e->button());
}

void MarqueeLabel::mouseReleaseEvent(QMouseEvent *e) {
	if (!_selectable) {
		RpWidget::mouseReleaseEvent(e);
		return;
	}
	dragActionFinish(e->globalPos(), e->button());
	if (!rect().contains(e->pos())) {
		_inside = false;
		refreshMarqueeState();
	}
}

void MarqueeLabel::mouseDoubleClickEvent(QMouseEvent *e) {
	if (!_selectable) {
		RpWidget::mouseDoubleClickEvent(e);
		return;
	}
	dragActionStart(e->globalPos(), e->button());
	if (_selecting && _selectionType == TextSelectType::Letters) {
		const auto state = getTextState(mapFromGlobal(_lastMousePos));
		if (state.uponSymbol) {
			_dragSymbol = state.symbol;
			_selectionType = TextSelectType::Words;
			updateSelection(state);
			_trippleClickPoint = e->globalPos();
			_trippleClickTimer.callOnce(QApplication::doubleClickInterval());
		}
	}
}

void MarqueeLabel::enterEventHook(QEnterEvent *e) {
	if (!_selectable) {
		return;
	}
	_inside = true;
	_lastMousePos = e->globalPos();
	dragActionUpdate();
	refreshMarqueeState();
}

void MarqueeLabel::leaveEventHook(QEvent *e) {
	if (!_selectable) {
		return;
	}
	_inside = false;
	if (!_selecting) {
		refreshMarqueeState();
	}
}

void MarqueeLabel::focusInEvent(QFocusEvent *e) {
	if (!_savedSelection.empty()) {
		_selection = _savedSelection;
		_savedSelection = { 0, 0 };
		update();
		refreshMarqueeState();
	}
}

void MarqueeLabel::focusOutEvent(QFocusEvent *e) {
	if (!_selection.empty()) {
		if (_menu) {
			_savedSelection = _selection;
		}
		_selection = { 0, 0 };
		update();
		refreshMarqueeState();
	}
}

void MarqueeLabel::keyPressEvent(QKeyEvent *e) {
	e->ignore();
	if (e->key() == Qt::Key_Copy
		|| (e->key() == Qt::Key_C
			&& e->modifiers().testFlag(Qt::ControlModifier))) {
		if (!_selection.empty()) {
			copySelectedText();
			e->accept();
		}
#ifdef Q_OS_MAC
	} else if (e->key() == Qt::Key_E
		&& e->modifiers().testFlag(Qt::ControlModifier)) {
		if (!_selection.empty()) {
			TextUtilities::SetClipboardText(
				_text.toTextForMimeData(_selection),
				QClipboard::FindBuffer);
		}
#endif // Q_OS_MAC
	}
}

void MarqueeLabel::contextMenuEvent(QContextMenuEvent *e) {
	if (_text.isEmpty() || (!_selectable && _contextCopyText.isEmpty())) {
		return;
	}
	if (e->reason() == QContextMenuEvent::Mouse) {
		_lastMousePos = e->globalPos();
	} else {
		_lastMousePos = QCursor::pos();
	}
	const auto state = _selectable
		? dragActionUpdate()
		: Text::StateResult();
	const auto uponSelection = _selectable
		&& state.uponSymbol
		&& (state.symbol >= _selection.from)
		&& (state.symbol < _selection.to);
	const auto fullSelection = _selectable
		&& _text.isFullSelection(_selection);
	_menu = base::make_unique_q<PopupMenu>(this);
	if (uponSelection && !fullSelection) {
		_menu->addAction(
			Integration::Instance().phraseContextCopySelected(),
			[=] { copySelectedText(); });
	} else if (!_contextCopyText.isEmpty()) {
		_menu->addAction(_contextCopyText, [text = _text.toString()] {
			TextUtilities::SetClipboardText(TextForMimeData::Simple(text));
		});
	}
	if (_menu->empty()) {
		_menu = nullptr;
		return;
	}
	_menu->setDestroyedCallback([=] {
		InvokeQueued(this, [=] {
			update();
			refreshMarqueeState();
		});
	});
	_menu->popup(e->globalPos());
	e->accept();
	refreshMarqueeState();
}

} // namespace Ui
