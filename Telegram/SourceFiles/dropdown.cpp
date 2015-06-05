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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"

#include "dropdown.h"
#include "historywidget.h"

#include "localstorage.h"
#include "lang.h"

#include "window.h"
#include "apiwrap.h"

#include "boxes/confirmbox.h"

Dropdown::Dropdown(QWidget *parent, const style::dropdown &st) : TWidget(parent),
_ignore(false), _selected(-1), _st(st), _width(_st.width), _hiding(false), a_opacity(0), _shadow(_st.shadow) {
	resetButtons();

	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(hideStart()));

	if (cPlatform() == dbipMac) {
		connect(App::wnd()->windowHandle(), SIGNAL(activeChanged()), this, SLOT(onWndActiveChanged()));
	}
}

void Dropdown::ignoreShow(bool ignore) {
	_ignore = ignore;
}

void Dropdown::onWndActiveChanged() {
	if (!App::wnd()->windowHandle()->isActive() && !isHidden()) {
		leaveEvent(0);
	}
}

IconedButton *Dropdown::addButton(IconedButton *button) {
	button->setParent(this);

	int32 nw = _st.padding.left() + _st.padding.right() + button->width();
	if (nw > _width) {
		_width = nw;
		for (int32 i = 0, l = _buttons.size(); i < l; ++i) _buttons[i]->resize(_width - _st.padding.left() - _st.padding.right(), _buttons[i]->height());
	} else {
		button->resize(_width - _st.padding.left() - _st.padding.right(), button->height());
	}
	if (!button->isHidden()) {
		if (_height > _st.padding.top() + _st.padding.bottom()) {
			_height += _st.border;
		}
		_height += button->height();
	}
	_buttons.push_back(button);
	connect(button, SIGNAL(stateChanged(int, ButtonStateChangeSource)), this, SLOT(buttonStateChanged(int, ButtonStateChangeSource)));

	resize(_width, _height);

	return button;
}

void Dropdown::resetButtons() {
	_width = qMax(_st.padding.left() + _st.padding.right(), int(_st.width));
	_height = _st.padding.top() + _st.padding.bottom();
	for (int32 i = 0, l = _buttons.size(); i < l; ++i) {
		delete _buttons[i];
	}
	_buttons.clear();
	resize(_width, _height);

	_selected = -1;
}

void Dropdown::updateButtons() {
	int32 top = _st.padding.top(), starttop = top;
	for (Buttons::const_iterator i = _buttons.cbegin(), e = _buttons.cend(); i != e; ++i) {
		if (!(*i)->isHidden()) {
			(*i)->move(_st.padding.left(), top);
			if ((*i)->width() != _width - _st.padding.left() - _st.padding.right()) {
				(*i)->resize(_width - _st.padding.left() - _st.padding.right(), (*i)->height());
			}
			top += (*i)->height() + _st.border;
		}
	}
	_height = top + _st.padding.bottom() - (top > starttop ? _st.border : 0);
	resize(_width, _height);
}

void Dropdown::resizeEvent(QResizeEvent *e) {
	int32 top = _st.padding.top();
	for (Buttons::const_iterator i = _buttons.cbegin(), e = _buttons.cend(); i != e; ++i) {
		if (!(*i)->isHidden()) {
			(*i)->move(_st.padding.left(), top);
			top += (*i)->height() + _st.border;
		}
	}
}

void Dropdown::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	if (animating()) {
		p.setOpacity(a_opacity.current());
	}

	// draw shadow
	QRect r(_st.padding.left(), _st.padding.top(), _width - _st.padding.left() - _st.padding.right(), _height - _st.padding.top() - _st.padding.bottom());
	_shadow.paint(p, r, _st.shadowShift);

	if (!_buttons.isEmpty() && _st.border > 0) { // paint separators
		p.setPen(_st.borderColor->p);
		int32 top = _st.padding.top(), i = 0, l = _buttons.size();
		for (; i < l; ++i) {
			if (!_buttons.at(i)->isHidden()) break;
		}
		if (i < l) {
			top += _buttons.at(i)->height();
			for (++i; i < l; ++i) {
				if (!_buttons.at(i)->isHidden()) {
					p.fillRect(_st.padding.left(), top, _width - _st.padding.left() - _st.padding.right(), _st.border, _st.borderColor->b);
					top += _st.border + _buttons.at(i)->height();
				}
			}
		}
	}
}

void Dropdown::enterEvent(QEvent *e) {
	_hideTimer.stop();
	if (_hiding) showStart();
	return TWidget::enterEvent(e);
}

void Dropdown::leaveEvent(QEvent *e) {
	if (animating()) {
		hideStart();
	} else {
		_hideTimer.start(300);
	}
	return TWidget::leaveEvent(e);
}

void Dropdown::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		if (_selected >= 0 && _selected < _buttons.size()) {
			emit _buttons[_selected]->clicked();
			return;
		}
	} else if (e->key() == Qt::Key_Escape) {
		hideStart();
		return;
	}
	if ((e->key() != Qt::Key_Up && e->key() != Qt::Key_Down) || _buttons.size() < 1) return;

	bool none = (_selected < 0 || _selected >= _buttons.size());
	int32 delta = (e->key() == Qt::Key_Down ? 1 : -1);
	int32 newSelected = none ? (e->key() == Qt::Key_Down ? 0 : _buttons.size() - 1) : (_selected + delta);
	if (newSelected < 0) {
		newSelected = _buttons.size() - 1;
	} else if (newSelected >= _buttons.size()) {
		newSelected = 0;
	}
	int32 startFrom = newSelected;
	while (_buttons.at(newSelected)->isHidden()) {
		newSelected += delta;
		if (newSelected < 0) {
			newSelected = _buttons.size() - 1;
		} else if (newSelected >= _buttons.size()) {
			newSelected = 0;
		}
		if (newSelected == startFrom) return;
	}
	if (!none) {
		_buttons[_selected]->setOver(false);
	}
	_selected = newSelected;
	_buttons[_selected]->setOver(true);
}

void Dropdown::buttonStateChanged(int oldState, ButtonStateChangeSource source) {
	if (source == ButtonByUser) {
		for (int32 i = 0, l = _buttons.size(); i < l; ++i) {
			if (_buttons[i]->getState() & Button::StateOver) {
				if (i != _selected) {
					_buttons[i]->setOver(false);
				}
			}
		}
	} else if (source == ButtonByHover) {
		bool found = false;
		for (int32 i = 0, l = _buttons.size(); i < l; ++i) {
			if (_buttons[i]->getState() & Button::StateOver) {
				found = true;
				if (i != _selected) {
					int32 sel = _selected;
					_selected = i;
					if (sel >= 0 && sel < _buttons.size()) {
						_buttons[sel]->setOver(false);
					}
				}
			}
		}
		if (!found) {
			_selected = -1;
		}
	}
}

void Dropdown::otherEnter() {
	_hideTimer.stop();
	showStart();
}

void Dropdown::otherLeave() {
	if (animating()) {
		hideStart();
	} else {
		_hideTimer.start(0);
	}
}

void Dropdown::fastHide() {
	if (animating()) {
		anim::stop(this);
	}
	a_opacity = anim::fvalue(0, 0);
	_hideTimer.stop();
	hide();
}

void Dropdown::adjustButtons() {
	for (Buttons::const_iterator i = _buttons.cbegin(), e = _buttons.cend(); i != e; ++i) {
		(*i)->setOpacity(a_opacity.current());
	}
}

void Dropdown::hideStart() {
	_hiding = true;
	a_opacity.start(0);
	anim::start(this);
}

void Dropdown::hideFinish() {
	emit hiding();
	hide();
	for (Buttons::const_iterator i = _buttons.cbegin(), e = _buttons.cend(); i != e; ++i) {
		(*i)->clearState();
	}
	_selected = -1;
}

void Dropdown::showStart() {
	if (!isHidden() && a_opacity.current() == 1) {
		return;
	}
	_selected = -1;
	_hiding = false;
	show();
	a_opacity.start(1);
	anim::start(this);
}

bool Dropdown::animStep(float64 ms) {
	float64 dt = ms / _st.duration;
	bool res = true;
	if (dt >= 1) {
		a_opacity.finish();
		if (_hiding) {
			hideFinish();
		}
		res = false;
	} else {
		a_opacity.update(dt, anim::linear);
	}
	adjustButtons();
	update();
	return res;
}

bool Dropdown::eventFilter(QObject *obj, QEvent *e) {
	if (e->type() == QEvent::Enter) {
		otherEnter();
	} else if (e->type() == QEvent::Leave) {
		otherLeave();
	} else if (e->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton) {
		if (isHidden() || _hiding) {
			otherEnter();
		} else {
			otherLeave();
		}
	}
	return false;
}

DragArea::DragArea(QWidget *parent) : TWidget(parent),
_hiding(false), _in(false), a_opacity(0), a_color(st::dragColor->c), _shadow(st::boxShadow) {
	setMouseTracking(true);
	setAcceptDrops(true);
}

void DragArea::mouseMoveEvent(QMouseEvent *e) {
	if (_hiding) return;

	bool newIn = QRect(st::dragPadding.left(), st::dragPadding.top(), width() - st::dragPadding.left() - st::dragPadding.right(), height() - st::dragPadding.top() - st::dragPadding.bottom()).contains(e->pos());
	if (newIn != _in) {
		_in = newIn;
		a_opacity.start(1);
		a_color.start((_in ? st::dragDropColor : st::dragColor)->c);
		anim::start(this);
	}
}

void DragArea::dragMoveEvent(QDragMoveEvent *e) {
	QRect r(st::dragPadding.left(), st::dragPadding.top(), width() - st::dragPadding.left() - st::dragPadding.right(), height() - st::dragPadding.top() - st::dragPadding.bottom());
	bool newIn = r.contains(e->pos());
	if (newIn != _in) {
		_in = newIn;
		a_opacity.start(1);
		a_color.start((_in ? st::dragDropColor : st::dragColor)->c);
		anim::start(this);
	}
	e->setDropAction(_in ? Qt::CopyAction : Qt::IgnoreAction);
	e->accept();
}

void DragArea::setText(const QString &text, const QString &subtext) {
	_text = text;
	_subtext = subtext;
	update();
}

void DragArea::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	if (animating()) {
		p.setOpacity(a_opacity.current());
	}

	QRect r(st::dragPadding.left(), st::dragPadding.top(), width() - st::dragPadding.left() - st::dragPadding.right(), height() - st::dragPadding.top() - st::dragPadding.bottom());

	// draw shadow
	_shadow.paint(p, r, st::boxShadowShift);

	p.fillRect(r, st::white->b);

	p.setPen(a_color.current());

	p.setFont(st::dragFont->f);
	p.drawText(QRect(0, (height() - st::dragHeight) / 2, width(), st::dragFont->height), _text, QTextOption(style::al_top));

	p.setFont(st::dragSubfont->f);
	p.drawText(QRect(0, (height() + st::dragHeight) / 2 - st::dragSubfont->height, width(), st::dragSubfont->height * 2), _subtext, QTextOption(style::al_top));
}

void DragArea::dragEnterEvent(QDragEnterEvent *e) {
	static_cast<HistoryWidget*>(parentWidget())->dragEnterEvent(e);
	e->setDropAction(Qt::IgnoreAction);
	e->accept();
}

void DragArea::dragLeaveEvent(QDragLeaveEvent *e) {
	static_cast<HistoryWidget*>(parentWidget())->dragLeaveEvent(e);
	_in = false;
	a_opacity.start(_hiding ? 0 : 1);
	a_color.start((_in ? st::dragDropColor : st::dragColor)->c);
	anim::start(this);
}

void DragArea::dropEvent(QDropEvent *e) {
	static_cast<HistoryWidget*>(parentWidget())->dropEvent(e);
	if (e->isAccepted()) {
		emit dropped(e);
	}
}

void DragArea::otherEnter() {
	showStart();
}

void DragArea::otherLeave() {
	hideStart();
}

void DragArea::fastHide() {
	if (animating()) {
		anim::stop(this);
	}
	a_opacity = anim::fvalue(0, 0);
	hide();
}

void DragArea::hideStart() {
	_hiding = true;
	_in = false;
	a_opacity.start(0);
	a_color.start((_in ? st::dragDropColor : st::dragColor)->c);
	anim::start(this);
}

void DragArea::hideFinish() {
	hide();
	_in = false;
	a_color = anim::cvalue(st::dragColor->c);
}

void DragArea::showStart() {
	_hiding = false;
	show();
	a_opacity.start(1);
	a_color.start((_in ? st::dragDropColor : st::dragColor)->c);
	anim::start(this);
}

bool DragArea::animStep(float64 ms) {
	float64 dt = ms / st::dropdownDef.duration;
	bool res = true;
	if (dt >= 1) {
		a_opacity.finish();
		a_color.finish();
		if (_hiding) {
			hideFinish();
		}
		res = false;
	} else {
		a_opacity.update(dt, anim::linear);
		a_color.update(dt, anim::linear);
	}
	update();
	return res;
}

EmojiColorPicker::EmojiColorPicker(QWidget *parent) : TWidget(parent),
_ignoreShow(false), _selected(-1), _pressedSel(-1), _hiding(false), a_opacity(0), _shadow(st::dropdownDef.shadow) {
	memset(_variants, 0, sizeof(_variants));
	memset(_hovers, 0, sizeof(_hovers));

	setMouseTracking(true);
	setFocusPolicy(Qt::NoFocus);

	int32 w = st::emojiPanSize.width() * (EmojiColorsCount + 1) + 4 * st::emojiColorsPadding + st::emojiColorsSep + st::dropdownDef.shadow.pxWidth() * 2;
	int32 h = 2 * st::emojiColorsPadding + st::emojiPanSize.height() + st::dropdownDef.shadow.pxHeight() * 2;
	resize(w, h);

	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(hideStart()));
}

void EmojiColorPicker::showEmoji(uint32 code) {
	EmojiPtr e = emojiGet(code);
	if (!e || e == TwoSymbolEmoji || !e->color) {
		return;
	}
	_ignoreShow = false;

	_variants[0] = e;
	_variants[1] = emojiGet(e, 0xD83CDFFB);
	_variants[2] = emojiGet(e, 0xD83CDFFC);
	_variants[3] = emojiGet(e, 0xD83CDFFD);
	_variants[4] = emojiGet(e, 0xD83CDFFE);
	_variants[5] = emojiGet(e, 0xD83CDFFF);

	if (!_cache.isNull()) _cache = QPixmap();
	showStart();
}

void EmojiColorPicker::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (!_cache.isNull()) {
		p.setOpacity(a_opacity.current());
	}

	int32 w = st::dropdownDef.shadow.pxWidth(), h = st::dropdownDef.shadow.pxHeight();
	QRect r = QRect(w, h, width() - 2 * w, height() - 2 * h);
	_shadow.paint(p, r, st::dropdownDef.shadowShift);

	if (_cache.isNull()) {
		p.fillRect(r, st::white->b);

		int32 x = w + 2 * st::emojiColorsPadding + st::emojiPanSize.width();
		if (rtl()) x = width() - x - st::emojiColorsSep;
		p.fillRect(x, h + st::emojiColorsPadding, st::emojiColorsSep, r.height() - st::emojiColorsPadding * 2, st::emojiColorsSepColor->b);

		if (!_variants[0]) return;
		for (int i = 0; i < EmojiColorsCount + 1; ++i) {
			drawVariant(p, i);
		}
	} else {
		p.drawPixmap(r.left(), r.top(), _cache);
	}

}

void EmojiColorPicker::enterEvent(QEvent *e) {
	_hideTimer.stop();
	if (_hiding) showStart();
	TWidget::enterEvent(e);
}

void EmojiColorPicker::leaveEvent(QEvent *e) {
	TWidget::leaveEvent(e);
}

void EmojiColorPicker::mousePressEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
	_pressedSel = _selected;
}

void EmojiColorPicker::mouseReleaseEvent(QMouseEvent *e) {
	_lastMousePos = e ? e->globalPos() : QCursor::pos();
	int32 pressed = _pressedSel;
	_pressedSel = -1;

	updateSelected();
	if (_selected >= 0 && (pressed < 0 || _selected == pressed)) {
		emit emojiSelected(_variants[_selected]);
	}
	_ignoreShow = true;
	hideStart();
}

void EmojiColorPicker::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e ? e->globalPos() : QCursor::pos();
	updateSelected();
}

bool EmojiColorPicker::animStep(float64 ms) {
	bool res1 = true, res2 = true;
	if (!_cache.isNull()) {
		float64 dt = ms / st::dropdownDef.duration;
		if (dt >= 1) {
			a_opacity.finish();
			_cache = QPixmap();
			if (_hiding) {
				hide();
				emit hidden();
			} else {
				_lastMousePos = QCursor::pos();
				updateSelected();
			}
			res1 = false;
		} else {
			a_opacity.update(dt, anim::linear);
		}
	}
	if (!_emojiAnimations.isEmpty()) {
		uint64 now = getms();
		for (EmojiAnimations::iterator i = _emojiAnimations.begin(); i != _emojiAnimations.end();) {
			int index = qAbs(i.key()) - 1;
			float64 dt = float64(now - i.value()) / st::emojiPanDuration;
			if (dt >= 1) {
				_hovers[index] = (i.key() > 0) ? 1 : 0;
				i = _emojiAnimations.erase(i);
			} else {
				_hovers[index] = (i.key() > 0) ? dt : (1 - dt);
				++i;
			}
		}
		res2 = !_emojiAnimations.isEmpty();
	}
	update();
	return res1 || res2;
}

void EmojiColorPicker::hideStart(bool fast) {
	if (fast) {
		clearSelection(true);
		if (animating()) anim::stop(this);
		a_opacity = anim::fvalue(0);
		_cache = QPixmap();
		hide();
		emit hidden();
	} else {
		if (_cache.isNull()) {
			int32 w = st::dropdownDef.shadow.pxWidth(), h = st::dropdownDef.shadow.pxHeight();
			_cache = myGrab(this, QRect(w, h, width() - 2 * w, height() - 2 * h));
			clearSelection(true);
		}
		_hiding = true;
		a_opacity.start(0);
		anim::start(this);
	}
}

void EmojiColorPicker::showStart() {
	if (_ignoreShow) return;

	_hiding = false;
	if (!isHidden() && a_opacity.current() == 1) {
		if (animating()) {
			anim::stop(this);
			_cache = QPixmap();
		}
		return;
	}
	if (_cache.isNull()) {
		int32 w = st::dropdownDef.shadow.pxWidth(), h = st::dropdownDef.shadow.pxHeight();
		_cache = myGrab(this, QRect(w, h, width() - 2 * w, height() - 2 * h));
		clearSelection(true);
	}
	show();
	a_opacity.start(1);
	anim::start(this);
}

void EmojiColorPicker::clearSelection(bool fast) {
	_pressedSel = -1;
	_lastMousePos = mapToGlobal(QPoint(-10, -10));
	if (fast) {
		_selected = -1;
		memset(_hovers, 0, sizeof(_hovers));
		_emojiAnimations.clear();
	} else {
		updateSelected();
	}
}

void EmojiColorPicker::updateSelected() {
	int32 selIndex = -1;
	QPoint p(mapFromGlobal(_lastMousePos));
	int32 sx = rtl() ? (width() - p.x()) : p.x(), y = p.y() - st::dropdownDef.shadow.pxHeight() - st::emojiColorsPadding;
	if (y >= 0 && y < st::emojiPanSize.height()) {
		int32 x = sx - st::dropdownDef.shadow.pxWidth() - st::emojiColorsPadding;
		if (x >= 0 && x < st::emojiPanSize.width()) {
			selIndex = 0;
		} else {
			x -= st::emojiPanSize.width() + 2 * st::emojiColorsPadding + st::emojiColorsSep;
			if (x >= 0 && x < st::emojiPanSize.width() * EmojiColorsCount) {
				selIndex = (x / st::emojiPanSize.width()) + 1;
			}
		}
	}

	bool startanim = false;
	if (selIndex != _selected) {
		if (_selected >= 0) {
			_emojiAnimations.remove(_selected + 1);
			if (_emojiAnimations.find(-_selected - 1) == _emojiAnimations.end()) {
				if (_emojiAnimations.isEmpty()) startanim = true;
				_emojiAnimations.insert(-_selected - 1, getms());
			}
		}
		_selected = selIndex;
		if (_selected >= 0) {
			_emojiAnimations.remove(-_selected - 1);
			if (_emojiAnimations.find(_selected + 1) == _emojiAnimations.end()) {
				if (_emojiAnimations.isEmpty()) startanim = true;
				_emojiAnimations.insert(_selected + 1, getms());
			}
		}
		setCursor((_selected >= 0) ? style::cur_pointer : style::cur_default);
	}
	if (startanim && !animating()) anim::start(this);
}

void EmojiColorPicker::drawVariant(Painter &p, int variant) {
	float64 hover = _hovers[variant];

	QPoint w(st::dropdownDef.shadow.pxWidth() + st::emojiColorsPadding + variant * st::emojiPanSize.width() + (variant ? 2 * st::emojiColorsPadding + st::emojiColorsSep : 0), st::dropdownDef.shadow.pxHeight() + st::emojiColorsPadding);
	if (hover > 0) {
		p.setOpacity(hover);
		QPoint tl(w);
		if (rtl()) tl.setX(width() - tl.x() - st::emojiPanSize.width());
		App::roundRect(p, QRect(tl, st::emojiPanSize), st::emojiPanHover, StickerHoverCorners);
		p.setOpacity(1);
	}
	int esize = EmojiSizes[EIndex + 1];
	p.drawPixmapLeft(w.x() + (st::emojiPanSize.width() - (esize / cIntRetinaFactor())) / 2, w.y() + (st::emojiPanSize.height() - (esize / cIntRetinaFactor())) / 2, width(), App::emojisLarge(), QRect(_variants[variant]->x * esize, _variants[variant]->y * esize, esize, esize));
}

EmojiPanInner::EmojiPanInner(QWidget *parent) : TWidget(parent),
_top(0), _selected(-1), _pressedSel(-1), _pickerSel(-1), _picker(this),
_switcherHover(0), _stickersWidth(st::emojiPanHeaderFont->m.width(lang(lng_switch_stickers))) {
	resize(st::emojiPanFullSize.width(), countHeight());
	setMouseTracking(true);
	setFocusPolicy(Qt::NoFocus);

	_picker.hide();

	_esize = EmojiSizes[EIndex + 1];

	for (int32 i = 0; i < emojiTabCount; ++i) {
		_counts[i] = emojiPackCount(emojiTabAtIndex(i));
		_hovers[i] = QVector<float64>(_counts[i], 0);
	}

	_saveConfigTimer.setSingleShot(true);
	connect(&_saveConfigTimer, SIGNAL(timeout()), this, SLOT(onSaveConfig()));

	_showPickerTimer.setSingleShot(true);
	connect(&_showPickerTimer, SIGNAL(timeout()), this, SLOT(onShowPicker()));
	connect(&_picker, SIGNAL(emojiSelected(EmojiPtr)), this, SLOT(onColorSelected(EmojiPtr)));
	connect(&_picker, SIGNAL(hidden()), this, SLOT(onPickerHidden()));
}

void EmojiPanInner::setScrollTop(int top) {
	if (top == _top) return;

	QRegion upd = QRect(0, _top, width(), st::emojiPanHeader);
	_top = top;
	upd += QRect(0, _top, width(), st::emojiPanHeader);
	repaint(upd);
	updateSelected();
}

int EmojiPanInner::countHeight() {
	int result = 0;
	for (int i = 0; i < emojiTabCount; ++i) {
		int cnt = emojiPackCount(emojiTabAtIndex(i)), rows = (cnt / EmojiPanPerRow) + ((cnt % EmojiPanPerRow) ? 1 : 0);
		result += st::emojiPanHeader + rows * st::emojiPanSize.height();
	}

	return result + st::emojiPanPadding;
}

void EmojiPanInner::paintEvent(QPaintEvent *e) {
	Painter p(this);
	QRect r = e ? e->rect() : rect();
	if (r != rect()) {
		p.setClipRect(r);
	}
	p.fillRect(r, st::white->b);

	int32 y, tilly = 0;
	for (int c = 0; c < emojiTabCount; ++c) {
		y = tilly;
		int32 size = _counts[c];
		int32 rows = (size / EmojiPanPerRow) + ((size % EmojiPanPerRow) ? 1 : 0);
		tilly = y + st::emojiPanHeader + (rows * st::emojiPanSize.height());
		if (r.top() >= tilly) continue;

		y += st::emojiPanHeader;

		if (r.bottom() <= y) {
			p.setFont(st::emojiPanHeaderFont->f);
			p.setPen(st::emojiPanHeaderColor->p);
			p.drawTextLeft(st::emojiPanHeaderLeft, qMax(y - int(st::emojiPanHeader), _top) + st::emojiPanHeaderTop, width(), lang(LangKey(lng_emoji_category0 + c)));
			break;
		}

		if (_emojis[c].isEmpty()) {
			_emojis[c] = emojiPack(emojiTabAtIndex(c));
			if (emojiTabAtIndex(c) != dbietRecent) {
				for (EmojiPack::iterator i = _emojis[c].begin(), e = _emojis[c].end(); i != e; ++i) {
					if ((*i)->color) {
						EmojiColorVariants::const_iterator j = cEmojiVariants().constFind((*i)->code);
						if (j != cEmojiVariants().cend()) {
							EmojiPtr replace = emojiFromKey(j.value());
							if (replace) {
								if (replace != TwoSymbolEmoji && replace->code == (*i)->code && replace->code2 == (*i)->code2) {
									*i = replace;
								}
							}
						}
					}
				}
			}
		}

		int32 fromrow = (r.top() <= y) ? 0 : qMax(qFloor((r.top() - y) / st::emojiPanSize.height()), 0), torow = qMin(qCeil((r.bottom() - y) / st::emojiPanSize.height()) + 1, rows);
		for (int32 i = fromrow; i < torow; ++i) {
			for (int32 j = 0; j < EmojiPanPerRow; ++j) {
				int32 index = i * EmojiPanPerRow + j;
				if (index >= size) break;

				float64 hover = (!_picker.isHidden() && c * emojiTabShift + index == _pickerSel) ? 1 : _hovers[c][index];

				QPoint w(st::emojiPanPadding + j * st::emojiPanSize.width(), y + i * st::emojiPanSize.height());
				if (hover > 0) {
					p.setOpacity(hover);
					QPoint tl(w);
					if (rtl()) tl.setX(width() - tl.x() - st::emojiPanSize.width());
					App::roundRect(p, QRect(tl, st::emojiPanSize), st::emojiPanHover, StickerHoverCorners);
					p.setOpacity(1);
				}
				p.drawPixmapLeft(w.x() + (st::emojiPanSize.width() - (_esize / cIntRetinaFactor())) / 2, w.y() + (st::emojiPanSize.height() - (_esize / cIntRetinaFactor())) / 2, width(), App::emojisLarge(), QRect(_emojis[c][index]->x * _esize, _emojis[c][index]->y * _esize, _esize, _esize));
			}
		}

		if (y - int(st::emojiPanHeader) < _top) {
			p.fillRect(QRect(0, qMin(_top, tilly - int(st::emojiPanHeader)), width(), st::emojiPanHeader), st::emojiPanHeaderBg->b);
		}
		p.setFont(st::emojiPanHeaderFont->f);
		p.setPen(st::emojiPanHeaderColor->p);
		p.drawTextLeft(st::emojiPanHeaderLeft, qMin(qMax(y - int(st::emojiPanHeader), _top), tilly - int(st::emojiPanHeader)) + st::emojiPanHeaderTop, width(), lang(LangKey(lng_emoji_category0 + c)));
	}

	p.setFont(st::emojiPanHeaderFont->f);
	p.setPen(st::emojiSwitchColor->p);
	p.drawTextRight(st::emojiSwitchSkip, _top + st::emojiPanHeaderTop, width(), lang(lng_switch_stickers), _stickersWidth);
	p.drawSpriteRight(QPoint(st::emojiSwitchImgSkip - st::emojiSwitchStickers.pxWidth(), _top + (st::emojiPanHeader - st::emojiSwitchStickers.pxHeight()) / 2), width(), st::emojiSwitchStickers);
}

void EmojiPanInner::mousePressEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
	if (!_picker.isHidden() && _selected == _pickerSel) {
		_picker.hideStart();
		return;
	}
	_pressedSel = _selected;

	if (_selected >= 0 && _selected != SwitcherSelected) {
		int tab = (_selected / emojiTabShift), sel = _selected % emojiTabShift;
		if (tab < emojiTabCount && sel < _emojis[tab].size() && _emojis[tab][sel]->color) {
			_pickerSel = _selected;
			if (cEmojiVariants().constFind(_emojis[tab][sel]->code) == cEmojiVariants().cend()) {
				onShowPicker();
			} else {
				_showPickerTimer.start(500);
			}
		}
	}
}

void EmojiPanInner::mouseReleaseEvent(QMouseEvent *e) {
	int32 pressed = _pressedSel;
	_pressedSel = -1;

	_lastMousePos = e->globalPos();
	if (!_picker.isHidden()) {
		if (_picker.rect().contains(_picker.mapFromGlobal(_lastMousePos))) {
			return _picker.mouseReleaseEvent(0);
		} else if (_pickerSel >= 0) {
			int tab = (_pickerSel / emojiTabShift), sel = _pickerSel % emojiTabShift;
			if (tab < emojiTabCount && sel < _emojis[tab].size() && _emojis[tab][sel]->color) {
				if (cEmojiVariants().constFind(_emojis[tab][sel]->code) != cEmojiVariants().cend()) {
					_picker.hideStart();
				}
			}
		}
	}
	updateSelected();

	if (_showPickerTimer.isActive()) {
		_showPickerTimer.stop();
		_pickerSel = -1;
		_picker.hide();
	}

	if (_selected < 0 || _selected != pressed) return;
	if (_selected == SwitcherSelected) {
		emit switchToStickers();
		return;
	}

	if (_selected >= emojiTabCount * emojiTabShift) {
		return;
	}

	int tab = (_selected / emojiTabShift), sel = _selected % emojiTabShift;
	if (sel < _emojis[tab].size()) {
		EmojiPtr emoji(_emojis[tab][sel]);
		if (emoji->color && !_picker.isHidden()) return;

		selectEmoji(emoji);
	}
}

void EmojiPanInner::selectEmoji(EmojiPtr emoji) {
	RecentEmojiPack &recent(cGetRecentEmojis());
	RecentEmojiPack::iterator i = recent.begin(), e = recent.end();
	for (; i != e; ++i) {
		if (i->first == emoji) {
			++i->second;
			if (i->second > 0x8000) {
				for (RecentEmojiPack::iterator j = recent.begin(); j != e; ++j) {
					if (j->second > 1) {
						j->second /= 2;
					} else {
						j->second = 1;
					}
				}
			}
			for (; i != recent.begin(); --i) {
				if ((i - 1)->second > i->second) {
					break;
				}
				qSwap(*i, *(i - 1));
			}
			break;
		}
	}
	if (i == e) {
		while (recent.size() >= EmojiPanPerRow * EmojiPanRowsPerPage) recent.pop_back();
		recent.push_back(qMakePair(emoji, 1));
		for (i = recent.end() - 1; i != recent.begin(); --i) {
			if ((i - 1)->second > i->second) {
				break;
			}
			qSwap(*i, *(i - 1));
		}
	}
	_saveConfigTimer.start(SaveRecentEmojisTimeout);

	emit selected(emoji);
}

void EmojiPanInner::onSaveConfig() {
	Local::writeUserSettings();
}

void EmojiPanInner::onShowPicker() {
	int tab = (_pickerSel / emojiTabShift), sel = _pickerSel % emojiTabShift;
	if (tab < emojiTabCount && sel < _emojis[tab].size() && _emojis[tab][sel]->color) {
		int32 y = 0;
		for (int c = 0; c <= tab; ++c) {
			int32 size = (c == tab) ? (sel - (sel % EmojiPanPerRow)) : _counts[c], rows = (size / EmojiPanPerRow) + ((size % EmojiPanPerRow) ? 1 : 0);
			y += st::emojiPanHeader + (rows * st::emojiPanSize.height());
		}
		y -= _picker.height() - st::msgRadius;
		if (y < _top) {
			y += _picker.height() - st::msgRadius + st::emojiPanSize.height() - st::msgRadius;
		}
		int xmax = width() - _picker.width();
		float64 coef = float64(sel % EmojiPanPerRow) / float64(EmojiPanPerRow - 1);
		if (rtl()) coef = 1. - coef;
		_picker.move(qRound(xmax * coef), y);

		_picker.showEmoji(_emojis[tab][sel]->code);
		emit disableScroll(true);
	}
}

void EmojiPanInner::onPickerHidden() {
	_pickerSel = -1;
	update();
	emit disableScroll(false);

	_lastMousePos = QCursor::pos();
	updateSelected();
}

void EmojiPanInner::onColorSelected(EmojiPtr emoji) {
	if (emoji->color) {
		cRefEmojiVariants().insert(emoji->code, emojiKey(emoji));
	}
	if (_pickerSel >= 0) {
		int tab = (_pickerSel / emojiTabShift), sel = _pickerSel % emojiTabShift;
		if (tab >= 0 && tab < emojiTabCount) {
			_emojis[tab][sel] = emoji;
			update();
		}
	}
	selectEmoji(emoji);
	_picker.hideStart();
}

void EmojiPanInner::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	if (!_picker.isHidden()) {
		if (_picker.rect().contains(_picker.mapFromGlobal(_lastMousePos))) {
			return _picker.mouseMoveEvent(0);
		} else {
			_picker.clearSelection();
		}
	}
	updateSelected();
}

void EmojiPanInner::leaveEvent(QEvent *e) {
	clearSelection();
}

void EmojiPanInner::leaveToChildEvent(QEvent *e) {
	clearSelection();
}

void EmojiPanInner::enterFromChildEvent(QEvent *e) {
	_lastMousePos = QCursor::pos();
	updateSelected();
}

void EmojiPanInner::clearSelection(bool fast) {
	_lastMousePos = mapToGlobal(QPoint(-10, -10));
	if (fast) {
		for (Animations::const_iterator i = _animations.cbegin(); i != _animations.cend(); ++i) {
			int index = qAbs(i.key()) - 1, tab = (index / emojiTabShift), sel = index % emojiTabShift;
			(index == SwitcherSelected ? _switcherHover : _hovers[tab][sel]) = 0;
		}
		_animations.clear();
		_selected = _pressedSel = -1;
		anim::stop(this);
	} else {
		updateSelected();
	}
}

DBIEmojiTab EmojiPanInner::currentTab(int yOffset) const {
	int y, ytill = 0;
	for (int c = 0; c < emojiTabCount; ++c) {
		int cnt = _counts[c];
		y = ytill;
		ytill = y + st::emojiPanHeader + ((cnt / EmojiPanPerRow) + ((cnt % EmojiPanPerRow) ? 1 : 0)) * st::emojiPanSize.height();
		if (yOffset < ytill) {
			return emojiTabAtIndex(c);
		}
	}
	return emojiTabAtIndex(emojiTabCount - 1);
}

void EmojiPanInner::hideFinish() {
	if (!_picker.isHidden()) {
		_picker.hideStart(true);
		_pickerSel = -1;
	}
}

void EmojiPanInner::refreshRecent() {
	clearSelection(true);
	_counts[0] = emojiPackCount(dbietRecent);
	if (_hovers[0].size() != _counts[0]) _hovers[0] = QVector<float64>(_counts[0], 0);
	_emojis[0] = emojiPack(dbietRecent);
	int32 h = countHeight();
	if (h != height()) resize(width(), h);
}

void EmojiPanInner::updateSelected() {
	if (_pressedSel >= 0 || _pickerSel >= 0) return;

	int32 selIndex = -1;
	QPoint p(mapFromGlobal(_lastMousePos));
	if (p.y() < _top + st::emojiPanHeader) {
		bool upon1 = rtl() && p.x() >= 0 && p.x() < st::emojiSwitchSkip + _stickersWidth + (st::emojiSwitchSkip - st::emojiSwitchImgSkip);
		bool upon2 = !rtl() && p.x() < width() && p.x() >= width() - st::emojiSwitchSkip - _stickersWidth - (st::emojiSwitchSkip - st::emojiSwitchImgSkip);
		if (upon1 || upon2) {
			selIndex = SwitcherSelected;
		}
	} else {
		int y, ytill = 0, sx = (rtl() ? width() - p.x() : p.x()) - st::emojiPanPadding;
		for (int c = 0; c < emojiTabCount; ++c) {
			int cnt = _counts[c];
			y = ytill;
			ytill = y + st::emojiPanHeader + ((cnt / EmojiPanPerRow) + ((cnt % EmojiPanPerRow) ? 1 : 0)) * st::emojiPanSize.height();
			if (p.y() >= y && p.y() < ytill) {
				y += st::emojiPanHeader;
				if (p.y() >= y && sx >= 0 && sx < EmojiPanPerRow * st::emojiPanSize.width()) {
					selIndex = qFloor((p.y() - y) / st::emojiPanSize.height()) * EmojiPanPerRow + qFloor(sx / st::emojiPanSize.width());
					if (selIndex >= _emojis[c].size()) {
						selIndex = -1;
					} else {
						selIndex += c * emojiTabShift;
					}
				}
				break;
			}
		}
	}
	
	bool startanim = false;
	int oldSel = _selected, newSel = selIndex;
	
	if (newSel != oldSel) {
		if (oldSel >= 0) {
			_animations.remove(oldSel + 1);
			if (_animations.find(-oldSel - 1) == _animations.end()) {
				if (_animations.isEmpty()) startanim = true;
				_animations.insert(-oldSel - 1, getms());
			}
		}
		if (newSel >= 0) {
			_animations.remove(-newSel - 1);
			if (_animations.find(newSel + 1) == _animations.end()) {
				if (_animations.isEmpty()) startanim = true;
				_animations.insert(newSel + 1, getms());
			}
		}
		setCursor((newSel >= 0) ? style::cur_pointer : style::cur_default);
		if (newSel >= 0 && !_picker.isHidden()) {
			if (newSel != _pickerSel) {
				_picker.hideStart();
			} else {
				_picker.showStart();
			}
		}
	}

	_selected = selIndex;
	if (startanim) anim::start(this);
}

bool EmojiPanInner::animStep(float64 ms) {
	uint64 now = getms();
	for (Animations::iterator i = _animations.begin(); i != _animations.end();) {
		int index = qAbs(i.key()) - 1, tab = (index / emojiTabShift), sel = index % emojiTabShift;
		float64 dt = float64(now - i.value()) / st::emojiPanDuration;
		if (dt >= 1) {
			(index == SwitcherSelected ? _switcherHover : _hovers[tab][sel]) = (i.key() > 0) ? 1 : 0;
			i = _animations.erase(i);
		} else {
			(index == SwitcherSelected ? _switcherHover : _hovers[tab][sel]) = (i.key() > 0) ? dt : (1 - dt);
			++i;
		}
	}
	update();
	return !_animations.isEmpty();
}

void EmojiPanInner::showEmojiPack(DBIEmojiTab packIndex) {
	clearSelection(true);

	refreshRecent();

	int32 y = 0;
	for (int c = 0; c < emojiTabCount; ++c) {
		if (emojiTabAtIndex(c) == packIndex) break;
		int rows = (_counts[c] / EmojiPanPerRow) + ((_counts[c] % EmojiPanPerRow) ? 1 : 0);
		y += st::emojiPanHeader + rows * st::emojiPanSize.height();
	}

	emit scrollToY(y);

	_lastMousePos = QCursor::pos();

	update();
}

StickerPanInner::StickerPanInner(QWidget *parent) : TWidget(parent),
_top(0), _selected(-1), _pressedSel(-1),
_switcherHover(0), _emojiWidth(st::emojiPanHeaderFont->m.width(lang(lng_switch_emoji))) {
	resize(st::emojiPanFullSize.width(), countHeight());
	setMouseTracking(true);
	setFocusPolicy(Qt::NoFocus);

	connect(App::wnd(), SIGNAL(imageLoaded()), this, SLOT(update()));
	
	refreshStickers();
}

void StickerPanInner::setScrollTop(int top) {
	if (top == _top) return;

	QRegion upd = QRect(0, _top, width(), st::emojiPanHeader);
	_top = top;
	upd += QRect(0, _top, width(), st::emojiPanHeader);
	repaint(upd);
	updateSelected();
}

int StickerPanInner::countHeight() {
	int result = 0, minLastH = st::emojiPanFullSize.height() - st::rbEmoji.height - st::stickerPanPadding;
	for (int i = 0; i < _sets.size(); ++i) {
		int cnt = _sets.at(i).size(), rows = (cnt / StickerPanPerRow) + ((cnt % StickerPanPerRow) ? 1 : 0);
		int h = st::emojiPanHeader + rows * st::stickerPanSize.height();
		if (i == _sets.size() - 1 && _setIds.at(i) != CustomStickerSetId && h < minLastH) h = minLastH;
		result += h;
	}
	return result + st::stickerPanPadding;
}

void StickerPanInner::paintEvent(QPaintEvent *e) {
	Painter p(this);
	QRect r = e ? e->rect() : rect();
	if (r != rect()) {
		p.setClipRect(r);
	}
	p.fillRect(r, st::white->b);
	int32 y, tilly = 0;
	for (int c = 0, l = _sets.size(); c < l; ++c) {
		y = tilly;
		int32 size = _sets.at(c).size();
		int32 rows = (size / StickerPanPerRow) + ((size % StickerPanPerRow) ? 1 : 0);
		tilly = y + st::emojiPanHeader + (rows * st::stickerPanSize.height());
		if (r.top() >= tilly) continue;

		bool special = (_setIds[c] == DefaultStickerSetId || _setIds[c] == CustomStickerSetId || _setIds[c] == RecentStickerSetId);
		y += st::emojiPanHeader;

		QString title = _titles[c];
		if (r.bottom() <= y) {
			p.setFont(st::emojiPanHeaderFont->f);
			p.setPen(st::emojiPanHeaderColor->p);
			p.drawTextLeft(st::emojiPanHeaderLeft, qMax(y - int(st::emojiPanHeader), _top) + st::emojiPanHeaderTop, width(), title);
			if (!special && y >= _top + 2 * st::emojiPanHeader) {
				p.setOpacity(st::stickerPanDeleteOpacity + (1 - st::stickerPanDeleteOpacity) * _hovers[c][size]);
				p.drawSpriteRight(QPoint(st::emojiPanHeaderLeft, y - (st::emojiPanHeader + st::notifyClose.icon.pxHeight()) / 2), width(), st::notifyClose.icon);
				p.setOpacity(1);
			}
			break;
		}

		int32 fromrow = (r.top() <= y) ? 0 : qMax(qFloor((r.top() - y) / st::stickerPanSize.height()), 0), torow = qMin(qCeil((r.bottom() - y) / st::stickerPanSize.height()) + 1, rows);
		for (int32 i = fromrow; i < torow; ++i) {
			for (int32 j = 0; j < StickerPanPerRow; ++j) {
				int32 index = i * StickerPanPerRow + j;
				if (index >= size) break;

				float64 hover = _hovers[c][index];

				DocumentData *sticker = _sets[c][index];
				if (!sticker->sticker) continue;

				QPoint pos(st::stickerPanPadding + j * st::stickerPanSize.width(), y + i * st::stickerPanSize.height());
				if (hover > 0) {
					p.setOpacity(hover);
					QPoint tl(pos);
					if (rtl()) tl.setX(width() - tl.x() - st::stickerPanSize.width());
					App::roundRect(p, QRect(tl, st::stickerPanSize), st::emojiPanHover, StickerHoverCorners);
					p.setOpacity(1);
				}

				bool goodThumb = !sticker->thumb->isNull() && ((sticker->thumb->width() >= 128) || (sticker->thumb->height() >= 128));
				if (goodThumb) {
					sticker->thumb->load();
				} else {
					bool already = !sticker->already().isEmpty(), hasdata = !sticker->data.isEmpty();
					if (!sticker->loader && sticker->status != FileFailed && !already && !hasdata) {
						sticker->save(QString());
					}
					if (sticker->sticker->img->isNull() && (already || hasdata)) {
						if (already) {
							sticker->sticker->img = ImagePtr(sticker->already());
						} else {
							sticker->sticker->img = ImagePtr(sticker->data);
						}
					}
				}

				float64 coef = qMin((st::stickerPanSize.width() - st::msgRadius * 2) / float64(sticker->dimensions.width()), (st::stickerPanSize.height() - st::msgRadius * 2) / float64(sticker->dimensions.height()));
				if (coef > 1) coef = 1;
				int32 w = qRound(coef * sticker->dimensions.width()), h = qRound(coef * sticker->dimensions.height());
				if (w < 1) w = 1;
				if (h < 1) h = 1;
				QPoint ppos = pos + QPoint((st::stickerPanSize.width() - w) / 2, (st::stickerPanSize.height() - h) / 2);
				if (goodThumb) {
					p.drawPixmapLeft(ppos, width(), sticker->thumb->pix(w, h));
				} else if (!sticker->sticker->img->isNull()) {
					p.drawPixmapLeft(ppos, width(), sticker->sticker->img->pix(w, h));
				}

				if (hover > 0 && _setIds[c] == CustomStickerSetId) {
					float64 xHover = _hovers[c][_sets[c].size() + index];

					QPoint xPos = pos + QPoint(st::stickerPanSize.width() - st::stickerPanDelete.pxWidth(), 0);
					p.setOpacity(hover * (xHover + (1 - xHover) * st::stickerPanDeleteOpacity));
					p.drawPixmapLeft(xPos, width(), App::sprite(), st::stickerPanDelete);
					p.setOpacity(1);
				}
			}
		}

		if (y - st::emojiPanHeader < _top) {
			p.fillRect(QRect(0, qMin(_top, tilly - int(st::emojiPanHeader)), width(), st::emojiPanHeader), st::emojiPanHeaderBg->b);
		} else if (!special && y >= _top + 2 * st::emojiPanHeader) {
			p.setOpacity(st::stickerPanDeleteOpacity + (1 - st::stickerPanDeleteOpacity) * _hovers[c][size]);
			p.drawSpriteRight(QPoint(st::emojiPanHeaderLeft, y - (st::emojiPanHeader + st::notifyClose.icon.pxHeight()) / 2), width(), st::notifyClose.icon);
			p.setOpacity(1);
		}

		p.setFont(st::emojiPanHeaderFont->f);
		p.setPen(st::emojiPanHeaderColor->p);
		p.drawTextLeft(st::emojiPanHeaderLeft, qMin(qMax(y - int(st::emojiPanHeader), _top), tilly - int(st::emojiPanHeader)) + st::emojiPanHeaderTop, width(), title);
	}

	p.setFont(st::emojiPanHeaderFont->f);
	p.setPen(st::emojiSwitchColor->p);
	p.drawTextRight(st::emojiSwitchImgSkip - st::emojiSwitchEmoji.pxWidth(), _top + st::emojiPanHeaderTop, width(), lang(lng_switch_emoji), _emojiWidth);
	p.drawSpriteRight(QPoint(st::emojiSwitchSkip + _emojiWidth - st::emojiSwitchEmoji.pxWidth(), _top + (st::emojiPanHeader - st::emojiSwitchEmoji.pxHeight()) / 2), width(), st::emojiSwitchEmoji);
}

void StickerPanInner::mousePressEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();

	_pressedSel = _selected;
}

void StickerPanInner::mouseReleaseEvent(QMouseEvent *e) {
	int32 pressed = _pressedSel;
	_pressedSel = -1;

	_lastMousePos = e->globalPos();
	updateSelected();

	if (_selected < 0 || _selected != pressed) return;
	if (_selected == SwitcherSelected) {
		emit switchToEmoji();
		return;
	}
	if (_selected >= emojiTabShift * _setIds.size()) {
		return;
	}

	int tab = (_selected / emojiTabShift), sel = _selected % emojiTabShift;
	if (_setIds[tab] == CustomStickerSetId && sel >= _sets[tab].size() && sel < _sets[tab].size() * 2) {
		clearSelection(true);
		int32 refresh = 0;
		DocumentData *sticker = _sets[tab].at(sel - _sets[tab].size());
		RecentStickerPack &recent(cGetRecentStickers());
		for (int32 i = 0, l = recent.size(); i < l; ++i) {
			if (recent.at(i).first == sticker) {
				recent.removeAt(i);
				Local::writeUserSettings();
				refresh = 1;
				break;
			}
		}
		StickerSets &sets(cRefStickerSets());
		StickerSets::iterator it = sets.find(CustomStickerSetId);
		if (it != sets.cend()) {
			for (int32 i = 0, l = it->stickers.size(); i < l; ++i) {
				if (it->stickers.at(i) == sticker) {
					it->stickers.removeAt(i);
					if (it->stickers.isEmpty()) {
						sets.erase(it);
					}
					Local::writeStickers();
					refresh = 2;
					break;
				}
			}
		}
		if (refresh) {
			if (refresh > 1) {
				refreshStickers();
			} else {
				refreshRecent();
			}
			updateSelected();
			update();
		}
		return;
	}
	if (sel < _sets[tab].size()) {
		emit selected(_sets[tab][sel]);
	} else if (sel == _sets[tab].size()) {
		emit removing(_setIds[tab]);
	}
}

void StickerPanInner::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
}

void StickerPanInner::leaveEvent(QEvent *e) {
	clearSelection();
}

void StickerPanInner::leaveToChildEvent(QEvent *e) {
	clearSelection();
}

void StickerPanInner::enterFromChildEvent(QEvent *e) {
	_lastMousePos = QCursor::pos();
	updateSelected();
}

void StickerPanInner::clearSelection(bool fast) {
	_lastMousePos = mapToGlobal(QPoint(-10, -10));
	if (fast) {
		for (Animations::const_iterator i = _animations.cbegin(); i != _animations.cend(); ++i) {
			int index = qAbs(i.key()) - 1, tab = (index / emojiTabShift), sel = index % emojiTabShift;
			(index == SwitcherSelected ? _switcherHover : _hovers[tab][sel]) = 0;
		}
		_animations.clear();
		_selected = _pressedSel = -1;
		anim::stop(this);
	} else {
		updateSelected();
	}
}

void StickerPanInner::refreshStickers() {
	clearSelection(true);

	const StickerSets &sets(cStickerSets());
	_setIds.clear(); _setIds.reserve(sets.size() + 1);
	_sets.clear(); _sets.reserve(sets.size() + 1);
	_hovers.clear(); _hovers.reserve(sets.size() + 1);
	_titles.clear(); _titles.reserve(sets.size() + 1);

	refreshRecent(false);

	appendSet(CustomStickerSetId);
	appendSet(DefaultStickerSetId);
	for (StickerSetsOrder::const_iterator i = cStickerSetsOrder().cbegin(), e = cStickerSetsOrder().cend(); i != e; ++i) {
		appendSet(*i);
	}

	int32 h = countHeight();
	if (h != height()) resize(width(), h);

	emit refreshIcons();

	updateSelected();
}

void StickerPanInner::preloadImages() {
	uint64 ms = getms();
	for (int32 i = 0, l = _sets.size(), k = 0; i < l; ++i) {
		for (int32 j = 0, n = _sets.at(i).size(); j < n; ++j) {
			if (++k > StickerPanPerRow * (StickerPanPerRow + 1)) break;

			DocumentData *sticker = _sets.at(i).at(j);
			if (!sticker || !sticker->sticker) continue;

			bool goodThumb = !sticker->thumb->isNull() && ((sticker->thumb->width() >= 128) || (sticker->thumb->height() >= 128));
			if (goodThumb) {
				sticker->thumb->load();
			} else {
				bool already = !sticker->already().isEmpty(), hasdata = !sticker->data.isEmpty();
				if (!sticker->loader && sticker->status != FileFailed && !already && !hasdata) {
					sticker->save(QString());
				}
				//if (sticker->sticker->img->isNull() && (already || hasdata)) {
				//	if (already) {
				//		sticker->sticker->img = ImagePtr(sticker->already());
				//	} else {
				//		sticker->sticker->img = ImagePtr(sticker->data);
				//	}
				//}
			}
		}
		if (k > StickerPanPerRow * (StickerPanPerRow + 1)) break;
	}
}

uint64 StickerPanInner::currentSet(int yOffset) const {
	int y, ytill = 0;
	for (int i = 0, l = _sets.size(); i < l; ++i) {
		int cnt = _sets.at(i).size();
		y = ytill;
		ytill = y + st::emojiPanHeader + ((cnt / StickerPanPerRow) + ((cnt % StickerPanPerRow) ? 1 : 0)) * st::stickerPanSize.height();
		if (yOffset < ytill) {
			return _setIds.at(i);
		}
	}
	return _setIds.isEmpty() ? RecentStickerSetId : _setIds.back();
}

void StickerPanInner::appendSet(uint64 setId) {
	const StickerSets &sets(cStickerSets());
	StickerSets::const_iterator it = sets.constFind(setId);
	if (it == sets.cend() || it->stickers.isEmpty()) return;

	StickerPack pack;
	pack.reserve(it->stickers.size());
	for (int32 i = 0, l = it->stickers.size(); i < l; ++i) {
		pack.push_back(it->stickers.at(i));
	}
	_setIds.push_back(it->id);
	_sets.push_back(pack);
	_hovers.push_back(QVector<float64>(it->stickers.size() + (it->id == CustomStickerSetId ? it->stickers.size() : 1), 0));
	int32 availw = width() - st::emojiPanHeaderLeft - st::emojiSwitchSkip - _emojiWidth - (st::emojiSwitchSkip - st::emojiSwitchImgSkip);
	_titles.push_back(st::emojiPanHeaderFont->m.elidedText(it->title, Qt::ElideRight, availw));
}

void StickerPanInner::refreshRecent(bool performResize) {
	clearSelection(true);
	if (cGetRecentStickers().isEmpty()) {
		if (!_setIds.isEmpty() && _setIds.at(0) == RecentStickerSetId) {
			_setIds.pop_front();
			_sets.pop_front();
			_hovers.pop_front();
			_titles.pop_front();
		}
	} else {
		StickerPack recent;
		recent.reserve(cGetRecentStickers().size());
		for (int32 i = 0, l = cGetRecentStickers().size(); i < l; ++i) {
			recent.push_back(cGetRecentStickers().at(i).first);
		}
		if (_setIds.isEmpty() || _setIds.at(0) != RecentStickerSetId) {
			_setIds.push_front(RecentStickerSetId);
			_hovers.push_back(QVector<float64>(recent.size() + 1, 0));
			_sets.push_back(recent);
			_titles.push_back(lang(lng_emoji_category0));
		} else {
			_sets[0] = recent;
			_hovers[0].resize(recent.size() + 1);
		}
	}

	if (performResize) {
		int32 h = countHeight();
		if (h != height()) resize(width(), h);

		updateSelected();
	}
}

void StickerPanInner::fillIcons(QVector<StickerIcon> &icons) {
	icons.clear();
	icons.reserve(_sets.size());
	int32 i = 0;
	if (_setIds.at(0) == RecentStickerSetId) ++i;
	if (_setIds.at(0) == CustomStickerSetId || _setIds.at(1) == CustomStickerSetId) ++i;
	if (i > 0) icons.push_back(StickerIcon());
	for (int32 l = _sets.size(); i < l; ++i) {
		DocumentData *s = _sets.at(i).at(0);
		int32 availw = st::rbEmoji.width - 2 * st::stickerIconPadding, availh = st::rbEmoji.height - 2 * st::stickerIconPadding;
		int32 thumbw = s->thumb->width(), thumbh = s->thumb->height(), pixw = 1, pixh = 1;
		if (availw * thumbh > availh * thumbw) {
			pixh = availh;
			pixw = (pixh * thumbw) / thumbh;
		} else {
			pixw = availw;
			pixh = thumbw ? ((pixw * thumbh) / thumbw) : 1;
		}
		if (pixw < 1) pixw = 1;
		if (pixh < 1) pixh = 1;
		icons.push_back(StickerIcon(_setIds.at(i), s, pixw, pixh));
	}
}

void StickerPanInner::updateSelected() {
	if (_pressedSel >= 0) return;

	int32 selIndex = -1;
	QPoint p(mapFromGlobal(_lastMousePos));

	if (p.y() < _top + st::emojiPanHeader) {
		bool upon1 = rtl() && p.x() >= 0 && p.x() < st::emojiSwitchSkip + _emojiWidth + (st::emojiSwitchSkip - st::emojiSwitchImgSkip);
		bool upon2 = !rtl() && p.x() < width() && p.x() >= width() - st::emojiSwitchSkip - _emojiWidth - (st::emojiSwitchSkip - st::emojiSwitchImgSkip);
		if (upon1 || upon2) {
			selIndex = SwitcherSelected;
		}
	} else {
		int y, ytill = 0, sx = (rtl() ? width() - p.x() : p.x()) - st::stickerPanPadding;
		for (int c = 0, l = _setIds.size(); c < l; ++c) {
			int cnt = _sets[c].size();
			bool special = _setIds[c] == DefaultStickerSetId || _setIds[c] == CustomStickerSetId || _setIds[c] == RecentStickerSetId;
			y = ytill;
			ytill = y + st::emojiPanHeader + ((cnt / StickerPanPerRow) + ((cnt % StickerPanPerRow) ? 1 : 0)) * st::stickerPanSize.height();
			if (p.y() >= y && p.y() < ytill) {
				if (!special && p.y() >= y && p.y() < y + st::emojiPanHeader && sx + st::stickerPanPadding >= width() - st::emojiPanHeaderLeft - st::notifyClose.icon.pxWidth() && sx + st::stickerPanPadding < width() - st::emojiPanHeaderLeft) {
					selIndex = c * emojiTabShift + _sets[c].size();
				} else {
					y += st::emojiPanHeader;
					if (p.y() >= y && sx >= 0 && sx < StickerPanPerRow * st::stickerPanSize.width()) {
						selIndex = qFloor((p.y() - y) / st::stickerPanSize.height()) * StickerPanPerRow + qFloor(sx / st::stickerPanSize.width());
						if (selIndex >= _sets[c].size()) {
							selIndex = -1;
						} else {
							if (_setIds[c] == CustomStickerSetId) {
								int32 inx = sx - (selIndex % StickerPanPerRow) * st::stickerPanSize.width(), iny = p.y() - y - ((selIndex / StickerPanPerRow) * st::stickerPanSize.height());
								if (inx >= st::stickerPanSize.width() - st::stickerPanDelete.pxWidth() && iny < st::stickerPanDelete.pxHeight()) {
									selIndex += _sets[c].size();
								}
							}
							selIndex += c * emojiTabShift;
						}
					}
				}
				break;
			}
		}
	}

	bool startanim = false;
	int oldSel = _selected, oldSelTab = oldSel / emojiTabShift, xOldSel = -1, newSel = selIndex, newSelTab = newSel / emojiTabShift, xNewSel = -1;
	if (oldSel >= 0 && oldSelTab < _setIds.size() && _setIds[oldSelTab] == CustomStickerSetId && oldSel >= oldSelTab * emojiTabShift + _sets[oldSelTab].size()) {
		xOldSel = oldSel;
		oldSel -= _sets[oldSelTab].size();
	}
	if (newSel >= 0 && newSelTab < _setIds.size() && _setIds[newSelTab] == CustomStickerSetId && newSel >= newSelTab * emojiTabShift + _sets[newSelTab].size()) {
		xNewSel = newSel;
		newSel -= _sets[newSelTab].size();
	}
	if (newSel != oldSel) {
		if (oldSel >= 0) {
			_animations.remove(oldSel + 1);
			if (_animations.find(-oldSel - 1) == _animations.end()) {
				if (_animations.isEmpty()) startanim = true;
				_animations.insert(-oldSel - 1, getms());
			}
		}
		if (newSel >= 0) {
			_animations.remove(-newSel - 1);
			if (_animations.find(newSel + 1) == _animations.end()) {
				if (_animations.isEmpty()) startanim = true;
				_animations.insert(newSel + 1, getms());
			}
		}
		setCursor((newSel >= 0) ? style::cur_pointer : style::cur_default);
	}
	if (xNewSel != xOldSel) {
		if (xOldSel >= 0) {
			_animations.remove(xOldSel + 1);
			if (_animations.find(-xOldSel - 1) == _animations.end()) {
				if (_animations.isEmpty()) startanim = true;
				_animations.insert(-xOldSel - 1, getms());
			}
		}
		if (xNewSel >= 0) {
			_animations.remove(-xNewSel - 1);
			if (_animations.find(xNewSel + 1) == _animations.end()) {
				if (_animations.isEmpty()) startanim = true;
				_animations.insert(xNewSel + 1, getms());
			}
		}
	}
	_selected = selIndex;
	if (startanim) anim::start(this);
}

bool StickerPanInner::animStep(float64 ms) {
	uint64 now = getms();
	for (Animations::iterator i = _animations.begin(); i != _animations.end();) {
		int index = qAbs(i.key()) - 1, tab = (index / emojiTabShift), sel = index % emojiTabShift;
		float64 dt = float64(now - i.value()) / st::emojiPanDuration;
		if (dt >= 1) {
			(index == SwitcherSelected ? _switcherHover : _hovers[tab][sel]) = (i.key() > 0) ? 1 : 0;
			i = _animations.erase(i);
		} else {
			(index == SwitcherSelected ? _switcherHover : _hovers[tab][sel]) = (i.key() > 0) ? dt : (1 - dt);
			++i;
		}
	}
	update();
	return !_animations.isEmpty();
}

void StickerPanInner::showStickerSet(uint64 setId) {
	clearSelection(true);

	int32 y = 0;
	for (int c = 0; c < _setIds.size(); ++c) {
		if (_setIds.at(c) == setId) break;
		int rows = (_sets[c].size() / StickerPanPerRow) + ((_sets[c].size() % StickerPanPerRow) ? 1 : 0);
		y += st::emojiPanHeader + rows * st::stickerPanSize.height();
	}

	emit scrollToY(y);

	_lastMousePos = QCursor::pos();

	update();
}

EmojiPan::EmojiPan(QWidget *parent) : TWidget(parent),
_horizontal(false), _noTabUpdate(false), _hiding(false), a_opacity(0), _shadow(st::dropdownDef.shadow),
_recent(this     , qsl("emoji_group"), dbietRecent     , QString(), true , st::rbEmojiRecent),
_people(this     , qsl("emoji_group"), dbietPeople     , QString(), false, st::rbEmojiPeople),
_nature(this     , qsl("emoji_group"), dbietNature     , QString(), false, st::rbEmojiNature),
_food(this       , qsl("emoji_group"), dbietFood       , QString(), false, st::rbEmojiFood),
_celebration(this, qsl("emoji_group"), dbietCelebration, QString(), false, st::rbEmojiCelebration),
_activity(this   , qsl("emoji_group"), dbietActivity   , QString(), false, st::rbEmojiActivity),
_travel(this     , qsl("emoji_group"), dbietTravel     , QString(), false, st::rbEmojiTravel),
_objects(this    , qsl("emoji_group"), dbietObjects    , QString(), false, st::rbEmojiObjects),
_iconOver(-1), _iconSel(0), _iconDown(-1), _iconsDragging(false),
_iconAnim(animFunc(this, &EmojiPan::iconAnim)),
_iconsLeft(0), _iconsTop(0), _iconsStartX(0), _iconsMax(0), _iconsX(0, 0), _iconsStartAnim(0),
_stickersShown(false), _moveStart(0),
e_scroll(this, st::emojiScroll), e_inner(), s_scroll(this, st::emojiScroll), s_inner(), _removingSetId(0) {
	setFocusPolicy(Qt::NoFocus);
	e_scroll.setFocusPolicy(Qt::NoFocus);
	e_scroll.viewport()->setFocusPolicy(Qt::NoFocus);
	s_scroll.setFocusPolicy(Qt::NoFocus);
	s_scroll.viewport()->setFocusPolicy(Qt::NoFocus);

	_width = st::dropdownDef.padding.left() + st::emojiPanFullSize.width() + st::dropdownDef.padding.right();
	_height = st::dropdownDef.padding.top() + st::emojiPanFullSize.height() + st::dropdownDef.padding.bottom();
	resize(_width, _height);

	e_scroll.resize(st::emojiPanFullSize.width(), st::emojiPanFullSize.height() - st::rbEmoji.height);
	s_scroll.resize(st::emojiPanFullSize.width(), st::emojiPanFullSize.height() - st::rbEmoji.height);

	e_scroll.move(st::dropdownDef.padding.left(), st::dropdownDef.padding.top());
	e_scroll.setWidget(&e_inner);
	s_scroll.move(st::dropdownDef.padding.left(), st::dropdownDef.padding.top());
	s_scroll.setWidget(&s_inner);

	e_inner.setAttribute(Qt::WA_OpaquePaintEvent);
	e_scroll.setAutoFillBackground(true);
	s_inner.setAttribute(Qt::WA_OpaquePaintEvent);
	s_scroll.setAutoFillBackground(true);

	int32 left = _iconsLeft = st::dropdownDef.padding.left() + (st::emojiPanFullSize.width() - 8 * st::rbEmoji.width) / 2;
	int32 top = _iconsTop = st::dropdownDef.padding.top() + st::emojiPanFullSize.height() - st::rbEmoji.height;
	prepareTab(left, top, _width, _recent);
	prepareTab(left, top, _width, _people);
	prepareTab(left, top, _width, _nature);
	prepareTab(left, top, _width, _food);
	prepareTab(left, top, _width, _celebration);
	prepareTab(left, top, _width, _activity);
	prepareTab(left, top, _width, _travel);
	prepareTab(left, top, _width, _objects);

	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(hideStart()));

	connect(&e_inner, SIGNAL(scrollToY(int)), &e_scroll, SLOT(scrollToY(int)));
	connect(&e_inner, SIGNAL(disableScroll(bool)), &e_scroll, SLOT(disableScroll(bool)));

	connect(&s_inner, SIGNAL(scrollToY(int)), &s_scroll, SLOT(scrollToY(int)));

	connect(&e_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));
	connect(&s_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));

	connect(&e_inner, SIGNAL(selected(EmojiPtr)), this, SIGNAL(emojiSelected(EmojiPtr)));
	connect(&s_inner, SIGNAL(selected(DocumentData*)), this, SIGNAL(stickerSelected(DocumentData*)));

	connect(&e_inner, SIGNAL(switchToStickers()), this, SLOT(onSwitch()));
	connect(&s_inner, SIGNAL(switchToEmoji()), this, SLOT(onSwitch()));

	connect(&s_inner, SIGNAL(removing(uint64)), this, SLOT(onRemoveSet(uint64)));
	connect(&s_inner, SIGNAL(refreshIcons()), this, SLOT(onRefreshIcons()));

	if (cPlatform() == dbipMac) {
		connect(App::wnd()->windowHandle(), SIGNAL(activeChanged()), this, SLOT(onWndActiveChanged()));
	}

	setMouseTracking(true);
//	setAttribute(Qt::WA_AcceptTouchEvents);
}

void EmojiPan::prepareTab(int32 &left, int32 top, int32 _width, FlatRadiobutton &tab) {
	tab.moveToLeft(left, top, _width);
	left += tab.width();
	tab.setAttribute(Qt::WA_OpaquePaintEvent);
	connect(&tab, SIGNAL(changed()), this, SLOT(onTabChange()));
}

void EmojiPan::onWndActiveChanged() {
	if (!App::wnd()->windowHandle()->isActive() && !isHidden()) {
		leaveEvent(0);
	}
}

void EmojiPan::paintEvent(QPaintEvent *e) {
	Painter p(this);

	float64 o = 1;
	if (!_cache.isNull()) {
		p.setOpacity(o = a_opacity.current());
	}

	QRect r(st::dropdownDef.padding.left(), st::dropdownDef.padding.top(), _width - st::dropdownDef.padding.left() - st::dropdownDef.padding.right(), _height - st::dropdownDef.padding.top() - st::dropdownDef.padding.bottom());

	_shadow.paint(p, r, st::dropdownDef.shadowShift);

	if (_toCache.isNull()) {
		if (_cache.isNull()) {
			if (_stickersShown) {
				p.fillRect(r.left(), _iconsTop, r.width(), st::rbEmoji.height, st::emojiPanCategories->b);
				if (!_icons.isEmpty()) {
					int32 x = _iconsLeft, i = 0;
					if (!_icons.at(i).sticker) {
						if (_iconSel == i) {
							p.fillRect(rtl() ? (width() - x - st::rbEmoji.width) : x, _iconsTop, st::rbEmoji.width, st::rbEmoji.height, st::stickerIconSel->b);
						} else if (_iconHovers.at(i) > 0) {
							p.setOpacity(_iconHovers.at(i));
							p.fillRect(rtl() ? (width() - x - st::rbEmoji.width) : x, _iconsTop, st::rbEmoji.width, st::rbEmoji.height, st::stickerIconHover->b);
							p.setOpacity(1);
						}
						p.drawSpriteLeft(x + st::rbEmojiRecent.imagePos.x(), _iconsTop + st::rbEmojiRecent.imagePos.y(), width(), st::stickerIconRecent);
						x += st::rbEmoji.width;
						++i;
					}

					QRect clip(x, _iconsTop, _iconsLeft + 8 * st::rbEmoji.width - x, st::rbEmoji.height);
					if (rtl()) clip.moveLeft(width() - x - clip.width());
					p.setClipRect(clip);

					i += _iconsX.current() / int(st::rbEmoji.width);
					x -= _iconsX.current() % int(st::rbEmoji.width);
					for (int32 l = qMin(_icons.size(), i + 8 + (_icons.at(0).sticker ? 1 : 0)); i < l; ++i) {
						const StickerIcon &s(_icons.at(i));
						QPixmap pix(s.sticker->thumb->pix(s.pixw, s.pixh));
						if (_iconSel == i) {
							p.fillRect(rtl() ? (width() - x - st::rbEmoji.width) : x, _iconsTop, st::rbEmoji.width, st::rbEmoji.height, st::stickerIconSel->b);
						} else if (_iconHovers.at(i) > 0) {
							p.setOpacity(_iconHovers.at(i));
							p.fillRect(rtl() ? (width() - x - st::rbEmoji.width) : x, _iconsTop, st::rbEmoji.width, st::rbEmoji.height, st::stickerIconHover->b);
							p.setOpacity(1);
						}
						p.drawPixmapLeft(x + (st::rbEmoji.width - s.pixw) / 2, _iconsTop + (st::rbEmoji.height - s.pixh) / 2, width(), pix);
						x += st::rbEmoji.width;
					}
					float64 o_left = snap(float64(_iconsX.current()) / st::stickerIconLeft.pxWidth(), 0., 1.);
					if (o_left > 0) {
						p.setOpacity(o_left);
						p.drawSpriteLeft(QRect(_iconsLeft + (_icons.at(0).sticker ? 0 : st::rbEmoji.width), _iconsTop, st::stickerIconLeft.pxWidth(), st::rbEmoji.height), width(), st::stickerIconLeft);
					}
					float64 o_right = snap(float64(_iconsMax - _iconsX.current()) / st::stickerIconRight.pxWidth(), 0., 1.);
					if (o_right > 0) {
						p.setOpacity(o_right);
						p.drawSpriteRight(QRect(width() - _iconsLeft - 8 * st::rbEmoji.width, _iconsTop, st::stickerIconRight.pxWidth(), st::rbEmoji.height), width(), st::stickerIconRight);
					}
				}
			} else {
				p.fillRect(r.left(), _recent.y(), (rtl() ? _objects.x() : _recent.x() - r.left()), st::rbEmoji.height, st::emojiPanCategories->b);
				int32 x = rtl() ? (_recent.x() + _recent.width()) : (_objects.x() + _objects.width());
				p.fillRect(x, _recent.y(), r.left() + r.width() - x, st::rbEmoji.height, st::emojiPanCategories->b);
			}
		} else {
			p.fillRect(r, st::white->b);
			p.drawPixmap(r.left(), r.top(), _cache);
		}
	} else {
		p.fillRect(QRect(r.left(), r.top(), r.width(), r.height() - st::rbEmoji.height), st::white->b);
		p.fillRect(QRect(r.left(), _iconsTop, r.width(), st::rbEmoji.height), st::emojiPanCategories->b);
		p.setOpacity(o * a_fromAlpha.current());
		QRect fromDst = QRect(r.left() + a_fromCoord.current(), r.top(), _fromCache.width() / cIntRetinaFactor(), _fromCache.height() / cIntRetinaFactor());
		QRect fromSrc = QRect(0, 0, _fromCache.width(), _fromCache.height());
		if (fromDst.x() < r.left() + r.width() && fromDst.x() + fromDst.width() > r.left()) {
			if (fromDst.x() < r.left()) {
				fromSrc.setX((r.left() - fromDst.x()) * cIntRetinaFactor());
				fromDst.setX(r.left());
			} else if (fromDst.x() + fromDst.width() > r.left() + r.width()) {
				fromSrc.setWidth((r.left() + r.width() - fromDst.x()) * cIntRetinaFactor());
				fromDst.setWidth(r.left() + r.width() - fromDst.x());
			}
			p.drawPixmap(fromDst, _fromCache, fromSrc);
		}
		p.setOpacity(o * a_toAlpha.current());
		QRect toDst = QRect(r.left() + a_toCoord.current(), r.top(), _toCache.width() / cIntRetinaFactor(), _toCache.height() / cIntRetinaFactor());
		QRect toSrc = QRect(0, 0, _toCache.width(), _toCache.height());
		if (toDst.x() < r.left() + r.width() && toDst.x() + toDst.width() > r.left()) {
			if (toDst.x() < r.left()) {
				toSrc.setX((r.left() - toDst.x()) * cIntRetinaFactor());
				toDst.setX(r.left());
			} else if (toDst.x() + toDst.width() > r.left() + r.width()) {
				toSrc.setWidth((r.left() + r.width() - toDst.x()) * cIntRetinaFactor());
				toDst.setWidth(r.left() + r.width() - toDst.x());
			}
			p.drawPixmap(toDst, _toCache, toSrc);
		}
	}
}

void EmojiPan::enterEvent(QEvent *e) {
	_hideTimer.stop();
	if (_hiding) showStart();
}

void EmojiPan::leaveEvent(QEvent *e) {
	if (_removingSetId) return;
	if (animating()) {
		hideStart();
	} else {
		_hideTimer.start(300);
	}
}

void EmojiPan::otherEnter() {
	_hideTimer.stop();
	showStart();
}

void EmojiPan::otherLeave() {
	if (animating()) {
		hideStart();
	} else {
		_hideTimer.start(0);
	}
}

void EmojiPan::mousePressEvent(QMouseEvent *e) {
	if (!_stickersShown || _icons.isEmpty()) return;
	_iconsMousePos = e ? e->globalPos() : QCursor::pos();
	updateSelected();

	_iconDown = _iconOver;
	_iconsMouseDown = _iconsMousePos;
	_iconsStartX = _iconsX.current();
}

void EmojiPan::mouseMoveEvent(QMouseEvent *e) {
	if (!_stickersShown || _icons.isEmpty()) return;
	_iconsMousePos = e ? e->globalPos() : QCursor::pos();
	updateSelected();

	if (!_iconsDragging && _iconDown >= (_icons.at(0).sticker ? 0 : 1)) {
		if ((_iconsMousePos - _iconsMouseDown).manhattanLength() >= QApplication::startDragDistance()) {
			_iconsDragging = true;
		}
	}
	if (_iconsDragging) {
		int32 newX = snap(_iconsStartX + _iconsMouseDown.x() - _iconsMousePos.x(), 0, _iconsMax);
		if (newX != _iconsX.current()) {
			_iconsX = anim::ivalue(newX, newX);
			_iconsStartAnim = 0;
			if (_iconAnimations.isEmpty()) _iconAnim.stop();
			updateIcons();
		}
	}
}

void EmojiPan::mouseReleaseEvent(QMouseEvent *e) {
	if (!_stickersShown || _icons.isEmpty()) return;

	int32 wasDown = _iconDown;
	_iconDown = -1;

	_iconsMousePos = e ? e->globalPos() : QCursor::pos();
	if (_iconsDragging) {
		int32 newX = snap(_iconsStartX + _iconsMouseDown.x() - _iconsMousePos.x(), 0, _iconsMax);
		if (newX != _iconsX.current()) {
			_iconsX = anim::ivalue(newX, newX);
			_iconsStartAnim = 0;
			if (_iconAnimations.isEmpty()) _iconAnim.stop();
			updateIcons();
		}
		_iconsDragging = false;
		updateSelected();
	} else {
		updateSelected();

		if (wasDown == _iconOver && _iconOver >= 0) {
			s_inner.showStickerSet(_icons.at(_iconOver).setId);
		}
	}
}

bool EmojiPan::event(QEvent *e) {
	if (e->type() == QEvent::TouchBegin) {
		int a = 0;
	} else if (e->type() == QEvent::Wheel && _iconOver >= ((_icons.isEmpty() || _icons.at(0).sticker) ? 0 : 1) && _iconDown < 0) {
		QWheelEvent *ev = static_cast<QWheelEvent*>(e);
		bool hor = (ev->angleDelta().x() != 0 || ev->orientation() == Qt::Horizontal);
		bool ver = (ev->angleDelta().y() != 0 || ev->orientation() == Qt::Vertical);
		if (hor) _horizontal = true;
		int32 newX = _iconsX.current();
		if (/*_horizontal && */hor) {
			newX = snap(newX - (rtl() ? -1 : 1) * (ev->pixelDelta().x() ? ev->pixelDelta().x() : ev->angleDelta().x()), 0, _iconsMax);
		} else if (/*!_horizontal && */ver) {
			newX = snap(newX - (ev->pixelDelta().y() ? ev->pixelDelta().y() : ev->angleDelta().y()), 0, _iconsMax);
		}
		if (newX != _iconsX.current()) {
			_iconsX = anim::ivalue(newX, newX);
			_iconsStartAnim = 0;
			if (_iconAnimations.isEmpty()) _iconAnim.stop();
			updateSelected();
			updateIcons();
		}
	}
	return TWidget::event(e);
}

void EmojiPan::fastHide() {
	if (animating()) {
		anim::stop(this);
	}
	a_opacity = anim::fvalue(0, 0);
	_hideTimer.stop();
	hide();
	_cache = QPixmap();
}

void EmojiPan::refreshStickers() {
	s_inner.refreshStickers();
	if (!_stickersShown) {
		s_inner.preloadImages();
	}
}

void EmojiPan::onRefreshIcons() {
	_iconOver = -1;
	_iconHovers.clear();
	_iconAnimations.clear();
	s_inner.fillIcons(_icons);
	_iconsX = anim::ivalue(0, 0);
	_iconsStartAnim = 0;
	_iconAnim.stop();
	if (_icons.isEmpty()) {
		_iconsMax = 0;
	} else {
		_iconHovers = QVector<float64>(_icons.size(), 0);
		_iconsMax = qMax(int((_icons.size() - 8) * st::rbEmoji.width), 0);
	}
	updateSelected();

	updateIcons();
}

void EmojiPan::leaveToChildEvent(QEvent *e) {
	if (!_stickersShown) return;
	_iconsMousePos = QCursor::pos();
	updateSelected();
}

void EmojiPan::updateSelected() {
	if (_icons.isEmpty() || _iconDown >= 0) return;

	QPoint p(mapFromGlobal(_iconsMousePos));
	int32 x = p.x(), y = p.y(), newOver = -1;
	if (rtl()) x = width() - x;
	x -= _iconsLeft;
	if (y >= _iconsTop && y < _iconsTop + st::rbEmoji.height && x >= 0 && x < 8 * st::rbEmoji.width && x < _icons.size() * st::rbEmoji.width) {
		if (!_icons.at(0).sticker) {
			if (x < st::rbEmoji.width) {
				newOver = 0;
			} else {
				x -= st::rbEmoji.width;
			}
		}
		if (newOver < 0) {
			x += _iconsX.current();
			newOver = qFloor(x / st::rbEmoji.width) + (_icons.at(0).sticker ? 0 : 1);
		}
	}
	if (newOver != _iconOver) {
		if (newOver < 0) {
			setCursor(style::cur_default);
		} else if (_iconOver < 0) {
			setCursor(style::cur_pointer);
		}
		bool startanim = false;
		if (_iconOver >= 0) {
			_iconAnimations.remove(_iconOver + 1);
			if (_iconAnimations.find(-_iconOver - 1) == _iconAnimations.end()) {
				if (_iconAnimations.isEmpty() && !_iconsStartAnim) startanim = true;
				_iconAnimations.insert(-_iconOver - 1, getms());
			}
		}
		_iconOver = newOver;
		if (_iconOver >= 0) {
			_iconAnimations.remove(-_iconOver - 1);
			if (_iconAnimations.find(_iconOver + 1) == _iconAnimations.end()) {
				if (_iconAnimations.isEmpty() && !_iconsStartAnim) startanim = true;
				_iconAnimations.insert(_iconOver + 1, getms());
			}
		}
		if (startanim) _iconAnim.start();
	}
}

void EmojiPan::updateIcons() {
	QRect r(st::dropdownDef.padding.left(), st::dropdownDef.padding.top(), _width - st::dropdownDef.padding.left() - st::dropdownDef.padding.right(), _height - st::dropdownDef.padding.top() - st::dropdownDef.padding.bottom());
	update(r.left(), _iconsTop, r.width(), st::rbEmoji.height);
}

bool EmojiPan::iconAnim(float64 ms) {
	if (!_stickersShown) return false;

	uint64 now = getms();
	for (Animations::iterator i = _iconAnimations.begin(); i != _iconAnimations.end();) {
		int index = qAbs(i.key()) - 1;
		float64 dt = float64(now - i.value()) / st::emojiPanDuration;
		if (index >= _iconHovers.size()) {
			i = _iconAnimations.erase(i);
		} else if (dt >= 1) {
			_iconHovers[index] = (i.key() > 0) ? 1 : 0;
			i = _iconAnimations.erase(i);
		} else {
			_iconHovers[index] = (i.key() > 0) ? dt : (1 - dt);
			++i;
		}
	}

	if (_iconsStartAnim) {
		float64 dt = (now - _iconsStartAnim) / st::stickerIconMove;
		if (dt >= 1) {
			_iconsStartAnim = 0;
			_iconsX.finish();
		} else {
			_iconsX.update(dt, anim::sineInOut);
		}
		updateSelected();
	}

	updateIcons();

	return !_iconAnimations.isEmpty() || _iconsStartAnim;
}

bool EmojiPan::animStep(float64 ms) {
	bool res1 = false;
	if (_moveStart) {
		float64 movems = getms() - _moveStart;
		float64 fullDuration = st::introSlideDelta + st::introSlideDuration, dt = ms / fullDuration;
		float64 dt1 = (movems > st::introSlideDuration) ? 1 : (movems / st::introSlideDuration), dt2 = (movems > st::introSlideDelta) ? (movems - st::introSlideDelta) / (st::introSlideDuration) : 0;
		if (dt2 >= 1) {
			a_fromCoord.finish();
			a_fromAlpha.finish();
			a_toCoord.finish();
			a_toAlpha.finish();
			_fromCache = _toCache = QPixmap();
			_moveStart = 0;
			if (_cache.isNull()) showAll();
		} else {
			a_fromCoord.update(dt1, st::introHideFunc);
			a_fromAlpha.update(dt1, st::introAlphaHideFunc);
			a_toCoord.update(dt2, st::introShowFunc);
			a_toAlpha.update(dt2, st::introAlphaShowFunc);
			res1 = true;
		}
	}
	bool res2 = false;
	if (!_cache.isNull()) {
		float64 dt = ms / st::dropdownDef.duration;
		if (dt >= 1) {
			a_opacity.finish();
			if (_hiding) {
				res1 = false;
				hideFinish();
			} else {
				_cache = QPixmap();
				if (_toCache.isNull()) showAll();
			}
		} else {
			a_opacity.update(dt, anim::linear);
			res2 = true;
		}
	}
	update();
	return res1 || res2;
}

void EmojiPan::hideStart() {
	if (_cache.isNull()) {
		QPixmap from = _fromCache, to = _toCache;
		_fromCache = _toCache = QPixmap();
		showAll();
		_cache = myGrab(this, rect().marginsRemoved(st::dropdownDef.padding));
		_fromCache = from; _toCache = to;
	}
	hideAll();
	_hiding = true;
	a_opacity.start(0);
	anim::start(this);
}

void EmojiPan::hideFinish() {
	hide();
	e_inner.hideFinish();
	_cache = _toCache = _fromCache = QPixmap();
	_moveStart = 0;
	_horizontal = false;

	e_scroll.scrollToY(0);
	if (!_recent.checked()) {
		_noTabUpdate = true;
		_recent.setChecked(true);
		_noTabUpdate = false;
	}
	s_scroll.scrollToY(0);
	_iconOver = _iconDown = -1;
	_iconSel = 0;
	_iconsX = anim::ivalue(0, 0);
	_iconsStartAnim = 0;
	_iconAnim.stop();
	_iconHovers = _icons.isEmpty() ? QVector<float64>() : QVector<float64>(_icons.size(), 0);
	_iconAnimations.clear();
}

void EmojiPan::showStart() {
	if (!isHidden() && a_opacity.current() == 1) {
		return;
	}
	if (isHidden()) {
		e_inner.refreshRecent();
		s_inner.refreshRecent();
		s_inner.preloadImages();
		_stickersShown = false;
		_fromCache = _toCache = QPixmap();
		_moveStart = 0;
	}
	if (_cache.isNull()) {
		QPixmap from = _fromCache, to = _toCache;
		_fromCache = _toCache = QPixmap();
		showAll();
		_cache = myGrab(this, rect().marginsRemoved(st::dropdownDef.padding));
		_fromCache = from; _toCache = to;
	}
	hideAll();
	_hiding = false;
	show();
	a_opacity.start(1);
	anim::start(this);
	emit updateStickers();
}

bool EmojiPan::eventFilter(QObject *obj, QEvent *e) {
	if (e->type() == QEvent::Enter) {
		//if (dynamic_cast<StickerPan*>(obj)) {
		//	enterEvent(e);
		//} else {
			otherEnter();
		//}
	} else if (e->type() == QEvent::Leave) {
		//if (dynamic_cast<StickerPan*>(obj)) {
		//	leaveEvent(e);
		//} else {
			otherLeave();
		//}
	} else if (e->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton/* && !dynamic_cast<StickerPan*>(obj)*/) {
		if (isHidden() || _hiding) {
			otherEnter();
		} else {
			otherLeave();
		}
	}
	return false;
}

void EmojiPan::stickersInstalled(uint64 setId) {
	_stickersShown = true;
	if (isHidden()) {
		show();
		a_opacity = anim::fvalue(0, 1);
		a_opacity.update(0, anim::linear);
		_cache = _fromCache = _toCache = QPixmap();
	}
	showAll();
	s_inner.showStickerSet(setId);
	showStart();
}

void EmojiPan::showAll() {
	if (_stickersShown) {
		s_scroll.show();
		_recent.hide();
		_people.hide();
		_nature.hide();
		_food.hide();
		_celebration.hide();
		_activity.hide();
		_travel.hide();
		_objects.hide();
		e_scroll.hide();
	} else {
		s_scroll.hide();
		_recent.show();
		_people.show();
		_nature.show();
		_food.show();
		_celebration.show();
		_activity.show();
		_travel.show();
		_objects.show();
		e_scroll.show();
	}
}

void EmojiPan::hideAll() {
	_recent.hide();
	_people.hide();
	_nature.hide();
	_food.hide();
	_celebration.hide();
	_activity.hide();
	_travel.hide();
	_objects.hide();
	e_scroll.hide();
	s_scroll.hide();
	e_inner.clearSelection(true);
	s_inner.clearSelection(true);
}

void EmojiPan::onTabChange() {
	if (_noTabUpdate) return;
	DBIEmojiTab newTab = dbietRecent;
	if (_people.checked()) newTab = dbietPeople;
	else if (_nature.checked()) newTab = dbietNature;
	else if (_food.checked()) newTab = dbietFood;
	else if (_celebration.checked()) newTab = dbietCelebration;
	else if (_activity.checked()) newTab = dbietActivity;
	else if (_travel.checked()) newTab = dbietTravel;
	else if (_objects.checked()) newTab = dbietObjects;
	e_inner.showEmojiPack(newTab);
}

void EmojiPan::onScroll() {
	int top = e_scroll.scrollTop();
	if (!_stickersShown) {
		DBIEmojiTab tab = e_inner.currentTab(top);
		FlatRadiobutton *check = 0;
		switch (tab) {
			case dbietRecent     : check = &_recent     ; break;
			case dbietPeople     : check = &_people     ; break;
			case dbietNature     : check = &_nature     ; break;
			case dbietFood       : check = &_food       ; break;
			case dbietCelebration: check = &_celebration; break;
			case dbietActivity   : check = &_activity   ; break;
			case dbietTravel     : check = &_travel     ; break;
			case dbietObjects    : check = &_objects    ; break;
		}
		if (check && !check->checked()) {
			_noTabUpdate = true;
			check->setChecked(true);
			_noTabUpdate = false;
		}
	}
	e_inner.setScrollTop(top);

	top = s_scroll.scrollTop();
	if (_stickersShown) {
		uint64 setId = s_inner.currentSet(top);
		int32 newSel = 0;
		for (int32 i = 0, l = _icons.size(); i < l; ++i) {
			if (_icons.at(i).setId == setId) {
				newSel = i;
				break;
			}
		}
		if (newSel != _iconSel) {
			_iconSel = newSel;
			_iconsX.start(snap((2 * newSel - 7 - ((_icons.isEmpty() || _icons.at(0).sticker) ? 0 : 1)) * int(st::rbEmoji.width) / 2, 0, _iconsMax));
			_iconsStartAnim = getms();
			_iconAnim.start();
			updateSelected();
			updateIcons();
		}
	}
	s_inner.setScrollTop(top);
}

void EmojiPan::onSwitch() {
	QPixmap cache = _cache;
	_fromCache = myGrab(this, rect().marginsRemoved(st::dropdownDef.padding));
	_stickersShown = !_stickersShown;

	_iconOver = -1;
	_iconHovers = _icons.isEmpty() ? QVector<float64>() : QVector<float64>(_icons.size(), 0);
	_iconAnimations.clear();
	_iconAnim.stop();

	_cache = QPixmap();
	showAll();
	_toCache = myGrab(this, rect().marginsRemoved(st::dropdownDef.padding));
	_cache = cache;

	hideAll();
	_moveStart = getms();

	a_toCoord = (_stickersShown != rtl()) ? anim::ivalue(st::emojiPanFullSize.width(), 0) : anim::ivalue(-st::emojiPanFullSize.width(), 0);
	a_toAlpha = anim::fvalue(0, 1);
	a_fromCoord = (_stickersShown != rtl()) ? anim::ivalue(0, -st::emojiPanFullSize.width()) : anim::ivalue(0, st::emojiPanFullSize.width());
	a_fromAlpha = anim::fvalue(1, 0);

	if (!animating()) anim::start(this);
	update();
}

void EmojiPan::onRemoveSet(uint64 setId) {
	StickerSets::const_iterator it = cStickerSets().constFind(setId);
	if (it != cStickerSets().cend() && setId != CustomStickerSetId && setId != DefaultStickerSetId && setId != RecentStickerSetId) {
		_removingSetId = it->id;
		ConfirmBox *box = new ConfirmBox(lng_stickers_remove_pack(lt_sticker_pack, it->title));
		connect(box, SIGNAL(confirmed()), this, SLOT(onRemoveSetSure()));
		connect(box, SIGNAL(destroyed(QObject*)), this, SLOT(onDelayedHide()));
		App::wnd()->showLayer(box);
	}
}

void EmojiPan::onRemoveSetSure() {
	App::wnd()->hideLayer();
	StickerSets::iterator it = cRefStickerSets().find(_removingSetId);
	if (it != cRefStickerSets().cend() && _removingSetId != CustomStickerSetId && _removingSetId != DefaultStickerSetId && _removingSetId != RecentStickerSetId) {
		if (it->id && it->access) {
			MTP::send(MTPmessages_UninstallStickerSet(MTP_inputStickerSetID(MTP_long(it->id), MTP_long(it->access))));
		} else if (!it->shortName.isEmpty()) {
			MTP::send(MTPmessages_UninstallStickerSet(MTP_inputStickerSetShortName(MTP_string(it->shortName))));
		}
		bool writeRecent = false;
		RecentStickerPack &recent(cGetRecentStickers());
		for (RecentStickerPack::iterator i = recent.begin(); i != recent.cend();) {
			if (it->stickers.indexOf(i->first) >= 0) {
				i = recent.erase(i);
				writeRecent = true;
			} else {
				++i;
			}
		}
		cRefStickerSets().erase(it);
		cRefStickerSetsOrder().removeOne(_removingSetId);
		cSetStickersHash(QByteArray());
		refreshStickers();
		Local::writeStickers();
		if (writeRecent) Local::writeUserSettings();
	}
	_removingSetId = 0;
}

void EmojiPan::onDelayedHide() {
	if (!rect().contains(mapFromGlobal(QCursor::pos()))) {
		_hideTimer.start(3000);
	}
	_removingSetId = 0;
}

MentionsInner::MentionsInner(MentionsDropdown *parent, MentionRows *rows, HashtagRows *hrows) : _parent(parent), _rows(rows), _hrows(hrows), _sel(-1), _mouseSel(false), _overDelete(false) {
}

void MentionsInner::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	int32 atwidth = st::mentionFont->m.width('@'), hashwidth = st::mentionFont->m.width('#');
	int32 availwidth = width() - 2 * st::mentionPadding.left() - st::mentionPhotoSize - 2 * st::mentionPadding.right();
	int32 htagleft = st::btnAttachPhoto.width + st::taMsgField.textMrg.left() - st::dlgShadow, htagwidth = width() - st::mentionPadding.right() - htagleft;

	int32 from = qFloor(e->rect().top() / st::mentionHeight), to = qFloor(e->rect().bottom() / st::mentionHeight) + 1, last = _rows->isEmpty() ? _hrows->size() : _rows->size();
	for (int32 i = from; i < to; ++i) {
		if (i >= last) break;

		if (i == _sel) {
			p.fillRect(0, i * st::mentionHeight, width(), st::mentionHeight, st::dlgHoverBG->b);
			int skip = (st::mentionHeight - st::notifyClose.icon.pxHeight()) / 2;
			if (_rows->isEmpty()) p.drawPixmap(QPoint(width() - st::notifyClose.icon.pxWidth() - skip, i * st::mentionHeight + skip), App::sprite(), st::notifyClose.icon);
		}
		p.setPen(st::black->p);
		if (_rows->isEmpty()) {
			QString tag = st::mentionFont->m.elidedText('#' + _hrows->at(last - i - 1), Qt::ElideRight, htagwidth);
			p.setFont(st::mentionFont->f);
			p.drawText(htagleft, i * st::mentionHeight + st::mentionTop + st::mentionFont->ascent, tag);
		} else {
			UserData *user = _rows->at(last - i - 1);
			QString first = (_parent->filter().size() < 2) ? QString() : ('@' + user->username.mid(0, _parent->filter().size() - 1)), second = (_parent->filter().size() < 2) ? ('@' + user->username) : user->username.mid(_parent->filter().size() - 1);
			int32 firstwidth = st::mentionFont->m.width(first), secondwidth = st::mentionFont->m.width(second), unamewidth = firstwidth + secondwidth, namewidth = user->nameText.maxWidth();
			if (availwidth < unamewidth + namewidth) {
				namewidth = (availwidth * namewidth) / (namewidth + unamewidth);
				unamewidth = availwidth - namewidth;
				if (firstwidth <= unamewidth) {
					if (firstwidth < unamewidth) {
						first = st::mentionFont->m.elidedText(first, Qt::ElideRight, unamewidth);
					} else if (!second.isEmpty()) {
						first = st::mentionFont->m.elidedText(first + second, Qt::ElideRight, unamewidth);
						second = QString();
					}
				} else {
					second = st::mentionFont->m.elidedText(second, Qt::ElideRight, unamewidth - firstwidth);
				}
			}
			user->photo->load();
			p.drawPixmap(st::mentionPadding.left(), i * st::mentionHeight + st::mentionPadding.top(), user->photo->pixRounded(st::mentionPhotoSize));
			user->nameText.drawElided(p, 2 * st::mentionPadding.left() + st::mentionPhotoSize, i * st::mentionHeight + st::mentionTop, namewidth);
			p.setFont(st::mentionFont->f);

			p.setPen(st::profileOnlineColor->p);
			p.drawText(2 * st::mentionPadding.left() + st::mentionPhotoSize + namewidth + st::mentionPadding.right(), i * st::mentionHeight + st::mentionTop + st::mentionFont->ascent, first);
			if (!second.isEmpty()) {
				p.setPen(st::profileOfflineColor->p);
				p.drawText(2 * st::mentionPadding.left() + st::mentionPhotoSize + namewidth + st::mentionPadding.right() + firstwidth, i * st::mentionHeight + st::mentionTop + st::mentionFont->ascent, second);
			}
		}
	}

	p.fillRect(cWideMode() ? st::dlgShadow : 0, _parent->innerTop(), width() - (cWideMode() ? st::dlgShadow : 0), st::titleShadow, st::titleShadowColor->b);
	p.fillRect(cWideMode() ? st::dlgShadow : 0, _parent->innerBottom() - st::titleShadow, width() - (cWideMode() ? st::dlgShadow : 0), st::titleShadow, st::titleShadowColor->b);
}

void MentionsInner::mouseMoveEvent(QMouseEvent *e) {
	_mousePos = mapToGlobal(e->pos());
	_mouseSel = true;
	onUpdateSelected(true);
}

void MentionsInner::clearSel() {
	_mouseSel = _overDelete = false;
	setSel(-1);
}

bool MentionsInner::moveSel(int direction) {
	_mouseSel = false;
	int32 maxSel = (_rows->isEmpty() ? _hrows->size() : _rows->size());
	if (_sel >= maxSel || _sel < 0) {
		if (direction < 0) setSel(maxSel - 1, true);
		return (_sel >= 0 && _sel < maxSel);
	}
	if (_sel > 0 || direction > 0) {
		setSel((_sel + direction >= maxSel) ? -1 : (_sel + direction), true);
	}
	return true;
}

bool MentionsInner::select() {
	int32 maxSel = (_rows->isEmpty() ? _hrows->size() : _rows->size());
	if (_sel >= 0 && _sel < maxSel) {
		QString result = _rows->isEmpty() ? ('#' + _hrows->at(_hrows->size() - _sel - 1)) : ('@' + _rows->at(_rows->size() - _sel - 1)->username);
		emit chosen(result);
		return true;
	}
	return false;
}

void MentionsInner::mousePressEvent(QMouseEvent *e) {
	_mousePos = mapToGlobal(e->pos());
	_mouseSel = true;
	onUpdateSelected(true);
	if (e->button() == Qt::LeftButton) {
		if (_overDelete && _sel >= 0 && _sel < _hrows->size()) {
			_mousePos = mapToGlobal(e->pos());

			QString toRemove = _hrows->at(_hrows->size() - _sel - 1);
			RecentHashtagPack recent(cRecentWriteHashtags());
			for (RecentHashtagPack::iterator i = recent.begin(); i != recent.cend();) {
				if (i->first == toRemove) {
					i = recent.erase(i);
				} else {
					++i;
				}
			}
			cSetRecentWriteHashtags(recent);
			Local::writeRecentHashtags();
			_parent->updateFiltered();

			_mouseSel = true;
			onUpdateSelected(true);
		} else {
			select();
		}
	}
}

void MentionsInner::enterEvent(QEvent *e) {
	setMouseTracking(true);
	_mousePos = QCursor::pos();
	onUpdateSelected(true);
}

void MentionsInner::leaveEvent(QEvent *e) {
	setMouseTracking(false);
	if (_sel >= 0) {
		setSel(-1);
	}
}

void MentionsInner::setSel(int sel, bool scroll) {
	_sel = sel;
	parentWidget()->update();
	int32 maxSel = _rows->isEmpty() ? _hrows->size() : _rows->size();
	if (scroll && _sel >= 0 && _sel < maxSel) emit mustScrollTo(_sel * st::mentionHeight, (_sel + 1) * st::mentionHeight);
}

void MentionsInner::onUpdateSelected(bool force) {
	QPoint mouse(mapFromGlobal(_mousePos));
	if ((!force && !rect().contains(mouse)) || !_mouseSel) return;

	int w = width(), mouseY = mouse.y();
	_overDelete = _rows->isEmpty() && (mouse.x() >= w - st::mentionHeight);
	int32 sel = mouseY / int32(st::mentionHeight), maxSel = _rows->isEmpty() ? _hrows->size() : _rows->size();
	if (sel < 0 || sel >= maxSel) {
		sel = -1;
	}
	if (sel != _sel) {
		setSel(sel);
	}
}

void MentionsInner::onParentGeometryChanged() {
	_mousePos = QCursor::pos();
	if (rect().contains(mapFromGlobal(_mousePos))) {
		setMouseTracking(true);
		onUpdateSelected(true);
	}
}

MentionsDropdown::MentionsDropdown(QWidget *parent) : QWidget(parent),
_scroll(this, st::mentionScroll), _inner(this, &_rows, &_hrows), _chat(0), _hiding(false), a_opacity(0), _shadow(st::dropdownDef.shadow) {
	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(hideStart()));
	connect(&_inner, SIGNAL(chosen(QString)), this, SIGNAL(chosen(QString)));
	connect(&_inner, SIGNAL(mustScrollTo(int,int)), &_scroll, SLOT(scrollToY(int,int)));

	setFocusPolicy(Qt::NoFocus);
	_scroll.setFocusPolicy(Qt::NoFocus);
	_scroll.viewport()->setFocusPolicy(Qt::NoFocus);

	_inner.setGeometry(rect());
	_scroll.setGeometry(rect());

	_scroll.setWidget(&_inner);
	_scroll.show();
	_inner.show();

	connect(&_scroll, SIGNAL(geometryChanged()), &_inner, SLOT(onParentGeometryChanged()));
	connect(&_scroll, SIGNAL(scrolled()), &_inner, SLOT(onUpdateSelected()));

	if (cPlatform() == dbipMac) {
		connect(App::wnd()->windowHandle(), SIGNAL(activeChanged()), this, SLOT(onWndActiveChanged()));
	}
}

void MentionsDropdown::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	if (animating()) {
		p.setOpacity(a_opacity.current());
		p.drawPixmap(0, 0, _cache);
		return;
	}

	p.fillRect(rect(), st::white->b);

}

void MentionsDropdown::showFiltered(ChatData *chat, QString start) {
	_chat = chat;
	start = start.toLower();
	bool toDown = (_filter != start);
	if (toDown) {
		_filter = start;
	}

	updateFiltered(toDown);
}

void MentionsDropdown::updateFiltered(bool toDown) {
	int32 now = unixtime();
	QMultiMap<int32, UserData*> ordered;
	MentionRows rows;
	HashtagRows hrows;
	if (_filter.at(0) == '@') {
		rows.reserve(_chat->participants.isEmpty() ? _chat->lastAuthors.size() : _chat->participants.size());
		if (_chat->participants.isEmpty()) {
			if (_chat->count > 0) {
				App::api()->requestFullPeer(_chat);
			}
		} else {
			for (ChatData::Participants::const_iterator i = _chat->participants.cbegin(), e = _chat->participants.cend(); i != e; ++i) {
				UserData *user = i.key();
				if (user->username.isEmpty()) continue;
				if (_filter.size() > 1 && (!user->username.startsWith(_filter.midRef(1), Qt::CaseInsensitive) || user->username.size() + 1 == _filter.size())) continue;
				ordered.insertMulti(App::onlineForSort(user->onlineTill, now), user);
			}
		}
		for (MentionRows::const_iterator i = _chat->lastAuthors.cbegin(), e = _chat->lastAuthors.cend(); i != e; ++i) {
			UserData *user = *i;
			if (user->username.isEmpty()) continue;
			if (_filter.size() > 1 && (!user->username.startsWith(_filter.midRef(1), Qt::CaseInsensitive) || user->username.size() + 1 == _filter.size())) continue;
			rows.push_back(user);
			if (!ordered.isEmpty()) {
				ordered.remove(App::onlineForSort(user->onlineTill, now), user);
			}
		}
		if (!ordered.isEmpty()) {
			for (QMultiMap<int32, UserData*>::const_iterator i = ordered.cend(), b = ordered.cbegin(); i != b;) {
				--i;
				rows.push_back(i.value());
			}
		}
	} else {
		const RecentHashtagPack &recent(cRecentWriteHashtags());
		hrows.reserve(recent.size());
		for (RecentHashtagPack::const_iterator i = recent.cbegin(), e = recent.cend(); i != e; ++i) {
			if (_filter.size() > 1 && (!i->first.startsWith(_filter.midRef(1), Qt::CaseInsensitive) || i->first.size() + 1 == _filter.size())) continue;
			hrows.push_back(i->first);
		}
	}
	if (rows.isEmpty() && hrows.isEmpty()) {
		if (!isHidden()) {
			hideStart();
			_rows.clear();
			_hrows.clear();
		}
	} else {
		_rows = rows;
		_hrows = hrows;
		bool hidden = _hiding || isHidden();
		if (hidden) {
			show();
			_scroll.show();
		}
		recount(toDown);
		if (hidden) {
			hide();
			showStart();
		}
	}
}

void MentionsDropdown::setBoundings(QRect boundings) {
	_boundings = boundings;
	resize(_boundings.width(), height());
	_scroll.resize(size());
	_inner.resize(width(), _inner.height());
	recount();
}

void MentionsDropdown::recount(bool toDown) {
	int32 h = (_rows.isEmpty() ? _hrows.size() : _rows.size()) * st::mentionHeight, oldst = _scroll.scrollTop(), st = oldst;
	
	if (_inner.height() != h) {
		st += h - _inner.height();
		_inner.resize(width(), h);
	}
	if (h > _boundings.height()) h = _boundings.height();
	if (h > 5 * st::mentionHeight) h = 5 * st::mentionHeight;
	if (height() != h) {
		st += _scroll.height() - h;
		setGeometry(0, _boundings.height() - h, width(), h);
		_scroll.resize(width(), h);
	} else if (y() != _boundings.height() - h) {
		move(0, _boundings.height() - h);
	}
	if (toDown) st = _scroll.scrollTopMax();
	if (st != oldst) _scroll.scrollToY(st);
	if (toDown) _inner.clearSel();
}

void MentionsDropdown::fastHide() {
	if (animating()) {
		anim::stop(this);
	}
	a_opacity = anim::fvalue(0, 0);
	_hideTimer.stop();
	hideFinish();
}

void MentionsDropdown::hideStart() {
	if (!_hiding) {
		if (_cache.isNull()) {
			_scroll.show();
			_cache = myGrab(this, rect());
		}
		_scroll.hide();
		_hiding = true;
		a_opacity.start(0);
		anim::start(this);
	}
}

void MentionsDropdown::hideFinish() {
	hide();
	_hiding = false;
	_filter = qsl("-");
	_inner.clearSel();
}

void MentionsDropdown::showStart() {
	if (!isHidden() && a_opacity.current() == 1 && !_hiding) {
		return;
	}
	if (_cache.isNull()) {
		_scroll.show();
		_cache = myGrab(this, rect());
	}
	_scroll.hide();
	_hiding = false;
	show();
	a_opacity.start(1);
	anim::start(this);
}

bool MentionsDropdown::animStep(float64 ms) {
	float64 dt = ms / st::dropdownDef.duration;
	bool res = true;
	if (dt >= 1) {
		a_opacity.finish();
		_cache = QPixmap();
		if (_hiding) {
			hideFinish();
		} else {
			_scroll.show();
			_inner.clearSel();
		}
		res = false;
	} else {
		a_opacity.update(dt, anim::linear);
	}
	update();
	return res;
}

const QString &MentionsDropdown::filter() const {
	return _filter;
}

int32 MentionsDropdown::innerTop() {
	return _scroll.scrollTop();
}

int32 MentionsDropdown::innerBottom() {
	return _scroll.scrollTop() + _scroll.height();
}

bool MentionsDropdown::eventFilter(QObject *obj, QEvent *e) {
	if (isHidden()) return QWidget::eventFilter(obj, e);
	if (e->type() == QEvent::KeyPress) {
		QKeyEvent *ev = static_cast<QKeyEvent*>(e);
		if (ev->key() == Qt::Key_Up) {
			_inner.moveSel(-1);
			return true;
		} else if (ev->key() == Qt::Key_Down) {
			return _inner.moveSel(1);
		} else if (ev->key() == Qt::Key_Enter || ev->key() == Qt::Key_Return || ev->key() == Qt::Key_Space) {
			return _inner.select();
		}
	}
	return QWidget::eventFilter(obj, e);
}

MentionsDropdown::~MentionsDropdown() {
}
