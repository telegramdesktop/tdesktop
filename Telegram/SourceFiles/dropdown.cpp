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
	_shadow.paint(p, r);

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
	_shadow.paint(p, r);

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
	_shadow.paint(p, r);

	if (_cache.isNull()) {
		p.fillRect(r, st::white->b);
		p.translate(w, h);

		p.fillRect(2 * st::emojiColorsPadding + st::emojiPanSize.width(), st::emojiColorsPadding, st::emojiColorsSep, r.height() - st::emojiColorsPadding * 2, st::emojiColorsSepColor->b);

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
	int32 y = p.y() - st::dropdownDef.shadow.pxHeight() - st::emojiColorsPadding;
	if (y >= 0 && y < st::emojiPanSize.height()) {
		int32 x = p.x() - st::dropdownDef.shadow.pxWidth() - st::emojiColorsPadding;
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

	QPoint w(st::emojiColorsPadding + variant * st::emojiPanSize.width() + (variant ? 2 * st::emojiColorsPadding + st::emojiColorsSep : 0), st::emojiColorsPadding);
	if (hover > 0) {
		p.setOpacity(hover);
		p.setBrush(st::emojiPanHover->b);
		p.setPen(Qt::NoPen);
		p.drawRoundedRect(QRect(w, st::emojiPanSize), st::emojiPanRound, st::emojiPanRound);
		p.setOpacity(1);
	}
	int esize = EmojiSizes[EIndex + 1];
	p.drawPixmapLeft(w.x() + (st::emojiPanSize.width() - (esize / cIntRetinaFactor())) / 2, w.y() + (st::emojiPanSize.height() - (esize / cIntRetinaFactor())) / 2, width(), App::emojisLarge(), QRect(_variants[variant]->x * esize, _variants[variant]->y * esize, esize, esize));

}

EmojiPanInner::EmojiPanInner(QWidget *parent) : TWidget(parent), _top(0), _selected(-1), _pressedSel(-1), _pickerSel(-1), _picker(this) {
	resize(EmojiPadPerRow * st::emojiPanSize.width(), countHeight());
	setMouseTracking(true);
	setFocusPolicy(Qt::NoFocus);

	_picker.hide();

	_esize = EmojiSizes[EIndex + 1];

	int sum = 0;
	for (int i = 0; i < emojiTabCount; ++i) {
		sum += (_counts[i] = emojiPackCount(emojiTabAtIndex(i)));
		_hovers[i] = QVector<float64>(_counts[i], 0);
	}
	_count = sum;

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
		int cnt = emojiPackCount(emojiTabAtIndex(i)), rows = (cnt / EmojiPadPerRow) + ((cnt % EmojiPadPerRow) ? 1 : 0);
		result += st::emojiPanHeader + rows * st::emojiPanSize.height();
	}

	result += st::emojiPanHeader;
	int32 cnt = cRecentStickers().size(), rows = (cnt / StickerPadPerRow) + ((cnt % StickerPadPerRow) ? 1 : 0);
	_stickerWidth = (EmojiPadPerRow * st::emojiPanSize.width()) / float64(StickerPadPerRow);
	_stickerSize = int32(_stickerWidth);
	result += rows * _stickerSize;
	return result;
}

void EmojiPanInner::paintEvent(QPaintEvent *e) {
	Painter p(this);
	QRect r = e ? e->rect() : rect();
	if (r != rect()) {
		p.setClipRect(r);
	}
	p.fillRect(r, st::white->b);

	float64 stickerWidth = width() / float64(StickerPadPerRow);
	int32 stickerSize = int32(stickerWidth);

	int32 fullh = 0;
	for (int c = 0; c < emojiTabCount; ++c) {
		int32 size = _counts[c], rows = (size / EmojiPadPerRow) + ((size % EmojiPadPerRow) ? 1 : 0);
		fullh += st::emojiPanHeader + (rows * st::emojiPanSize.height());
	}
	int32 ssize = _stickers.size(), srows = (ssize / StickerPadPerRow) + ((ssize % StickerPadPerRow) ? 1 : 0);
	fullh += st::emojiPanHeader + ssize * stickerSize;
	int32 downfrom = 0, uptill = (_top * _stickers.size()) / fullh;
	if (uptill >= _stickers.size()) uptill = _stickers.size();
	if (uptill > downfrom + StickerPadPerRow * 4) {
		downfrom = uptill - StickerPadPerRow * 4;
	}
	for (int index = downfrom; index < uptill; ++index) { // preload stickers
		DocumentData *sticker = _stickers[index];
		bool already = !sticker->already().isEmpty(), hasdata = !sticker->data.isEmpty();
		if (!sticker->loader && sticker->status != FileFailed && !already && !hasdata) {
			sticker->save(QString());
		}
		if (sticker->sticker->isNull() && (already || hasdata)) {
			if (already) {
				sticker->sticker = ImagePtr(sticker->already());
			} else {
				sticker->sticker = ImagePtr(sticker->data);
			}
		}

		float64 coef = qMin((stickerWidth - st::stickerPanPadding * 2) / float64(sticker->dimensions.width()), (stickerSize - st::stickerPanPadding * 2) / float64(sticker->dimensions.height()));
		if (coef > 1) coef = 1;
		int32 w = qRound(coef * sticker->dimensions.width()), h = qRound(coef * sticker->dimensions.height());
		if (w < 1) w = 1;
		if (h < 1) h = 1;
		if (!sticker->sticker->isNull()) sticker->sticker->pix(w, h);
	}

	int32 y, tilly = 0;
	for (int c = 0; c < emojiTabCount; ++c) {
		y = tilly;
		int32 size = _counts[c];
		int32 rows = (size / EmojiPadPerRow) + ((size % EmojiPadPerRow) ? 1 : 0);
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
			for (int32 j = 0; j < EmojiPadPerRow; ++j) {
				int32 index = i * EmojiPadPerRow + j;
				if (index >= size) break;

				float64 hover = (!_picker.isHidden() && c * emojiTabShift + index == _pickerSel) ? 1 : _hovers[c][index];

				QPoint w(j * st::emojiPanSize.width(), y + i * st::emojiPanSize.height());
				if (hover > 0) {
					p.setOpacity(hover);
					p.setBrush(st::emojiPanHover->b);
					p.setPen(Qt::NoPen);
					p.drawRoundedRect(QRect(w, st::emojiPanSize), st::emojiPanRound, st::emojiPanRound);
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

	y = tilly;
	int32 size = _stickers.size(), rows = (size / StickerPadPerRow) + ((size % StickerPadPerRow) ? 1 : 0);
	tilly = y + st::emojiPanHeader + (rows * stickerSize);
	if (r.top() >= tilly) return;

	y += st::emojiPanHeader;

	if (r.bottom() <= y) {
		p.setFont(st::emojiPanHeaderFont->f);
		p.setPen(st::emojiPanHeaderColor->p);
		p.drawTextLeft(st::emojiPanHeaderLeft, qMax(y - int(st::emojiPanHeader), _top) + st::emojiPanHeaderTop, width(), lang(LangKey(lng_emoji_category0 + emojiTabCount)));
		return;
	}

	int32 fromrow = (r.top() <= y) ? 0 : qMax(qFloor((r.top() - y) / stickerSize), 0), torow = qMin(qCeil((r.bottom() - y) / stickerSize) + 1, rows);
	for (int32 i = fromrow; i < torow; ++i) {
		for (int32 j = 0; j < StickerPadPerRow; ++j) {
			int32 index = i * StickerPadPerRow + j;
			if (index >= size) break;

			float64 hover = _hovers[emojiTabCount][index];

			QPoint pos(qRound(j * stickerWidth), y + i * stickerSize);
			if (hover > 0) {
				p.setOpacity(hover);
				p.setBrush(st::emojiPanHover->b);
				p.setPen(Qt::NoPen);
				p.drawRoundedRect(QRect(pos, QSize(stickerSize, stickerSize)), st::stickerPanRound, st::stickerPanRound);
				p.setOpacity(1);
			}

			DocumentData *sticker = _stickers[index];
			bool already = !sticker->already().isEmpty(), hasdata = !sticker->data.isEmpty();
			if (!sticker->loader && sticker->status != FileFailed && !already && !hasdata) {
				sticker->save(QString());
			}
			if (sticker->sticker->isNull() && (already || hasdata)) {
				if (already) {
					sticker->sticker = ImagePtr(sticker->already());
				} else {
					sticker->sticker = ImagePtr(sticker->data);
				}
			}

			float64 coef = qMin((stickerWidth - st::stickerPanPadding * 2) / float64(sticker->dimensions.width()), (stickerSize - st::stickerPanPadding * 2) / float64(sticker->dimensions.height()));
			if (coef > 1) coef = 1;
			int32 w = qRound(coef * sticker->dimensions.width()), h = qRound(coef * sticker->dimensions.height());
			if (w < 1) w = 1;
			if (h < 1) h = 1;
			QPoint ppos = pos + QPoint((stickerSize - w) / 2, (stickerSize - h) / 2);
			if (sticker->sticker->isNull()) {
				p.drawPixmap(ppos, sticker->thumb->pix(w, h));
			} else {
				p.drawPixmap(ppos, sticker->sticker->pix(w, h));
			}

			if (hover > 0 && _isUserGen[index]) {
				float64 xHover = _hovers[emojiTabCount][_stickers.size() + index];

				QPoint xPos = pos + QPoint(stickerWidth - st::stickerPanDelete.pxWidth(), 0);
				p.setOpacity(hover * (xHover + (1 - xHover) * st::stickerPanDeleteOpacity));
				p.drawPixmap(xPos, App::sprite(), st::stickerPanDelete);
				p.setOpacity(1);
			}
		}
	}

	if (y - int(st::emojiPanHeader) < _top) {
		p.fillRect(QRect(0, qMin(_top, tilly - int(st::emojiPanHeader)), width(), st::emojiPanHeader), st::emojiPanHeaderBg->b);
	}
	p.setFont(st::emojiPanHeaderFont->f);
	p.setPen(st::emojiPanHeaderColor->p);
	p.drawTextLeft(st::emojiPanHeaderLeft, qMax(y - int(st::emojiPanHeader), _top) + st::emojiPanHeaderTop, width(), lang(LangKey(lng_emoji_category0 + emojiTabCount)));
}

void EmojiPanInner::mousePressEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
	if (!_picker.isHidden() && _selected == _pickerSel) {
		_picker.hideStart();
		return;
	}
	_pressedSel = _selected;

	if (_selected >= 0) {
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
	if (_selected >= emojiTabCount * emojiTabShift) {
		int sel = _selected - (emojiTabCount * emojiTabShift);
		if (sel >= _stickers.size()) {
			RecentStickerPack recent(cRecentStickers());
			DocumentData *sticker = _stickers.at(sel - _stickers.size());
			for (int32 i = 0, l = recent.size(); i < l; ++i) {
				if (recent.at(i).first == sticker) {
					recent.removeAt(i);
					cSetRecentStickers(recent);
					Local::writeRecentStickers();
					refreshStickers();
					updateSelected();
					break;
				}
			}
		} else {
			emit stickerSelected(_stickers[sel]);
		}
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
		while (recent.size() >= EmojiPadPerRow * EmojiPadRowsPerPage) recent.pop_back();
		recent.push_back(qMakePair(emoji, 1));
		for (i = recent.end() - 1; i != recent.begin(); --i) {
			if ((i - 1)->second > i->second) {
				break;
			}
			qSwap(*i, *(i - 1));
		}
	}
	_saveConfigTimer.start(SaveRecentEmojisTimeout);

	emit emojiSelected(emoji);
}

void EmojiPanInner::onSaveConfig() {
	Local::writeUserSettings();
}

void EmojiPanInner::onShowPicker() {
	int tab = (_pickerSel / emojiTabShift), sel = _pickerSel % emojiTabShift;
	if (tab < emojiTabCount && sel < _emojis[tab].size() && _emojis[tab][sel]->color) {
		int32 y = 0;
		for (int c = 0; c <= tab; ++c) {
			int32 size = (c == tab) ? (sel - (sel % EmojiPadPerRow)) : _counts[c], rows = (size / EmojiPadPerRow) + ((size % EmojiPadPerRow) ? 1 : 0);
			y += st::emojiPanHeader + (rows * st::emojiPanSize.height());
		}
		y -= _picker.height() - st::emojiPanRound;
		if (y < _top) {
			y += _picker.height() - st::emojiPanRound + st::emojiPanSize.height() - st::emojiPanRound;
		}
		int xmax = width() - _picker.width() - st::emojiPanPadding.right();
		_picker.move(qRound(xmax * float64(sel % EmojiPadPerRow) / float64(EmojiPadPerRow - 1)), y);

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
		for (EmojiAnimations::const_iterator i = _emojiAnimations.cbegin(); i != _emojiAnimations.cend(); ++i) {
			int index = qAbs(i.key()) - 1, tab = (index / emojiTabShift), sel = index % emojiTabShift;
			_hovers[tab][sel] = 0;
		}
		_emojiAnimations.clear();
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
		ytill = y + st::emojiPanHeader + ((cnt / EmojiPadPerRow) + ((cnt % EmojiPadPerRow) ? 1 : 0)) * st::emojiPanSize.height();
		if (yOffset < ytill) {
			return emojiTabAtIndex(c);
		}
	}
	return dbietStickers;
}

void EmojiPanInner::refreshStickers() {
	clearSelection(true);
	int32 cnt = cRecentStickers().size();
	_hovers[emojiTabCount] = QVector<float64>(cnt * 2, 0);
	_stickers.resize(cnt);
	_isUserGen.resize(cnt);
	for (int32 i = 0; i < cnt; ++i) {
		_stickers[i] = cRecentStickers().at(i).first;
		_isUserGen[i] = (cRecentStickers().at(i).second < 0);
	}
	int32 h = countHeight();
	if (h != height()) resize(width(), h);
}

void EmojiPanInner::hideFinish() {
	if (!_picker.isHidden()) {
		_picker.hideStart(true);
		_pickerSel = -1;
	}
}

void EmojiPanInner::refreshRecent() {
	clearSelection(true);
	_count -= _counts[0];
	_counts[0] = emojiPackCount(dbietRecent);
	_count += _counts[0];
	if (_hovers[0].size() != _counts[0]) _hovers[0] = QVector<float64>(_counts[0], 0);
	_emojis[0] = emojiPack(dbietRecent);
	int32 h = countHeight();
	if (h != height()) resize(width(), h);
}

void EmojiPanInner::updateSelected() {
	if (_pressedSel >= 0 || _pickerSel >= 0) return;

	int32 selIndex = -1;
	QPoint p(mapFromGlobal(_lastMousePos));

	int y, ytill = 0;
	for (int c = 0; c < emojiTabCount; ++c) {
		int cnt = _counts[c];
		y = ytill;
		ytill = y + st::emojiPanHeader + ((cnt / EmojiPadPerRow) + ((cnt % EmojiPadPerRow) ? 1 : 0)) * st::emojiPanSize.height();
		if (p.y() >= y && p.y() < ytill) {
			y += st::emojiPanHeader;
			if (p.y() >= y && p.x() >= 0 && p.x() < EmojiPadPerRow * st::emojiPanSize.width()) {
				selIndex = qFloor((p.y() - y) / st::emojiPanSize.height()) * EmojiPadPerRow + qFloor(p.x() / st::emojiPanSize.width());
				if (selIndex >= _emojis[c].size()) {
					selIndex = -1;
				} else {
					selIndex += c * emojiTabShift;
				}
			}
			break;
		}
	}
	ytill += st::emojiPanHeader;
	if (p.y() >= ytill) {
		float64 stickerWidth = width() / float64(StickerPadPerRow);
		int32 stickerSize = int32(stickerWidth);
		if (p.x() >= 0 && p.x() < StickerPadPerRow * stickerWidth) {
			selIndex = qFloor((p.y() - ytill) / stickerSize) * StickerPadPerRow + qFloor(p.x() / stickerWidth);
			if (selIndex >= _stickers.size()) {
				selIndex = -1;
			} else {
				int32 inx = p.x() - (selIndex % StickerPadPerRow) * stickerWidth, iny = p.y() - ytill - ((selIndex / StickerPadPerRow) * stickerSize);
				if (inx >= stickerWidth - st::stickerPanDelete.pxWidth() && iny < st::stickerPanDelete.pxHeight()) {
					selIndex = _stickers.size() + selIndex;
				}
				selIndex += emojiTabCount * emojiTabShift;
			}
		}

	}

	bool startanim = false;
	int oldSel = _selected, xOldSel = -1, newSel = selIndex, xNewSel = -1;
	if (oldSel >= emojiTabCount * emojiTabShift + _stickers.size()) {
		xOldSel = oldSel;
		oldSel -= _stickers.size();
	}
	if (newSel >= emojiTabCount * emojiTabShift + _stickers.size()) {
		xNewSel = newSel;
		newSel -= _stickers.size();
	}
	if (newSel != oldSel) {
		if (oldSel >= 0) {
			_emojiAnimations.remove(oldSel + 1);
			if (_emojiAnimations.find(-oldSel - 1) == _emojiAnimations.end()) {
				if (_emojiAnimations.isEmpty()) startanim = true;
				_emojiAnimations.insert(-oldSel - 1, getms());
			}
		}
		if (newSel >= 0) {
			_emojiAnimations.remove(-newSel - 1);
			if (_emojiAnimations.find(newSel + 1) == _emojiAnimations.end()) {
				if (_emojiAnimations.isEmpty()) startanim = true;
				_emojiAnimations.insert(newSel + 1, getms());
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
	if (xNewSel != xOldSel) {
		if (xOldSel >= 0) {
			_emojiAnimations.remove(xOldSel + 1);
			if (_emojiAnimations.find(-xOldSel - 1) == _emojiAnimations.end()) {
				if (_emojiAnimations.isEmpty()) startanim = true;
				_emojiAnimations.insert(-xOldSel - 1, getms());
			}
		}
		if (xNewSel >= 0) {
			_emojiAnimations.remove(-xNewSel - 1);
			if (_emojiAnimations.find(xNewSel + 1) == _emojiAnimations.end()) {
				if (_emojiAnimations.isEmpty()) startanim = true;
				_emojiAnimations.insert(xNewSel + 1, getms());
			}
		}
	}
	_selected = selIndex;
	if (startanim) anim::start(this);
}

bool EmojiPanInner::animStep(float64 ms) {
	uint64 now = getms();
	for (EmojiAnimations::iterator i = _emojiAnimations.begin(); i != _emojiAnimations.end();) {
		int index = qAbs(i.key()) - 1, tab = (index / emojiTabShift), sel = index % emojiTabShift;
		float64 dt = float64(now - i.value()) / st::emojiPanDuration;
		if (dt >= 1) {
			_hovers[tab][sel] = (i.key() > 0) ? 1 : 0;
			i = _emojiAnimations.erase(i);
		} else {
			_hovers[tab][sel] = (i.key() > 0) ? dt : (1 - dt);
			++i;
		}
	}
	update();
	return !_emojiAnimations.isEmpty();
}

void EmojiPanInner::showEmojiPack(DBIEmojiTab packIndex) {
	clearSelection(true);

	refreshRecent();

	int32 y = 0;
	for (int c = 0; c < emojiTabCount; ++c) {
		if (emojiTabAtIndex(c) == packIndex) break;
		int rows = (_counts[c] / EmojiPadPerRow) + ((_counts[c] % EmojiPadPerRow) ? 1 : 0);
		y += st::emojiPanHeader + rows * st::emojiPanSize.height();
	}

	emit scrollToY(y);

	_lastMousePos = QCursor::pos();

	update();
}

EmojiPan::EmojiPan(QWidget *parent) : TWidget(parent),
_noTabUpdate(false), _hiding(false), a_opacity(0), _shadow(st::dropdownDef.shadow),
_recent(this     , qsl("emoji_group"), dbietRecent     , QString(), true , st::rbEmojiRecent),
_people(this     , qsl("emoji_group"), dbietPeople     , QString(), false, st::rbEmojiPeople),
_nature(this     , qsl("emoji_group"), dbietNature     , QString(), false, st::rbEmojiNature),
_food(this       , qsl("emoji_group"), dbietFood       , QString(), false, st::rbEmojiFood),
_celebration(this, qsl("emoji_group"), dbietCelebration, QString(), false, st::rbEmojiCelebration),
_activity(this   , qsl("emoji_group"), dbietActivity   , QString(), false, st::rbEmojiActivity),
_travel(this     , qsl("emoji_group"), dbietTravel     , QString(), false, st::rbEmojiTravel),
_objects(this    , qsl("emoji_group"), dbietObjects    , QString(), false, st::rbEmojiObjects),
_stickers(this   , qsl("emoji_group"), dbietStickers   , QString(), false, st::rbEmojiStickers),
_scroll(this, st::emojiScroll), _inner() {
	setFocusPolicy(Qt::NoFocus);
	_scroll.setFocusPolicy(Qt::NoFocus);
	_scroll.viewport()->setFocusPolicy(Qt::NoFocus);

	_scroll.setGeometry(st::dropdownDef.padding.left() + st::emojiPanPadding.left(), st::dropdownDef.padding.top() + _recent.height() + st::emojiPanPadding.top(), st::emojiPanPadding.left() + _inner.width() + st::emojiPanPadding.right(), EmojiPadRowsPerPage * st::emojiPanSize.height() + st::emojiPanHeader);
	_scroll.setWidget(&_inner);

	_inner.setAttribute(Qt::WA_OpaquePaintEvent);
	_scroll.setAutoFillBackground(true);

	_width = st::dropdownDef.padding.left() + st::emojiPanPadding.left() + _scroll.width() + st::emojiPanPadding.right() + st::dropdownDef.padding.right();
	_height = st::dropdownDef.padding.top() + _recent.height() + st::emojiPanPadding.top() + _scroll.height() + st::emojiPanPadding.bottom() + st::dropdownDef.padding.bottom();
	resize(_width, _height);

	int32 left = st::dropdownDef.padding.left() + (_width - st::dropdownDef.padding.left() - st::dropdownDef.padding.right() - 9 * _recent.width()) / 2;
	int32 top = st::dropdownDef.padding.top();
	_recent.move(left     , top); left += _recent.width();
	_people.move(left     , top); left += _people.width();
	_nature.move(left     , top); left += _nature.width();
	_food.move(left       , top); left += _food.width();
	_celebration.move(left, top); left += _celebration.width();
	_activity.move(left   , top); left += _activity.width();
	_travel.move(left     , top); left += _travel.width();
	_objects.move(left    , top); left += _objects.width();
	_stickers.move(left   , top); left += _stickers.width();

	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(hideStart()));

	connect(&_inner, SIGNAL(scrollToY(int)), &_scroll, SLOT(scrollToY(int)));
	connect(&_inner, SIGNAL(disableScroll(bool)), &_scroll, SLOT(disableScroll(bool)));

	connect(&_recent     , SIGNAL(changed()), this, SLOT(onTabChange()));
	connect(&_people     , SIGNAL(changed()), this, SLOT(onTabChange()));
	connect(&_nature     , SIGNAL(changed()), this, SLOT(onTabChange()));
	connect(&_food       , SIGNAL(changed()), this, SLOT(onTabChange()));
	connect(&_celebration, SIGNAL(changed()), this, SLOT(onTabChange()));
	connect(&_activity   , SIGNAL(changed()), this, SLOT(onTabChange()));
	connect(&_travel     , SIGNAL(changed()), this, SLOT(onTabChange()));
	connect(&_objects    , SIGNAL(changed()), this, SLOT(onTabChange()));
	connect(&_stickers   , SIGNAL(changed()), this, SLOT(onTabChange()));

	connect(&_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));

	connect(&_inner, SIGNAL(emojiSelected(EmojiPtr)), this, SIGNAL(emojiSelected(EmojiPtr)));
	connect(&_inner, SIGNAL(stickerSelected(DocumentData*)), this, SIGNAL(stickerSelected(DocumentData*)));

	if (cPlatform() == dbipMac) {
		connect(App::wnd()->windowHandle(), SIGNAL(activeChanged()), this, SLOT(onWndActiveChanged()));
	}
}

void EmojiPan::onWndActiveChanged() {
	if (!App::wnd()->windowHandle()->isActive() && !isHidden()) {
		leaveEvent(0);
	}
}

void EmojiPan::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	if (!_cache.isNull()) {
		p.setOpacity(a_opacity.current());
	}

	QRect r(st::dropdownDef.padding.left(), st::dropdownDef.padding.top(), _width - st::dropdownDef.padding.left() - st::dropdownDef.padding.right(), _height - st::dropdownDef.padding.top() - st::dropdownDef.padding.bottom());

	_shadow.paint(p, r);

	if (_cache.isNull()) {
		p.fillRect(r.left(), r.top(), r.width(), _scroll.y() - r.top(), st::white->b);
		p.fillRect(r.left(), _scroll.y(), _scroll.x() - r.left(), _scroll.height(), st::white->b);
		p.fillRect(_scroll.x() + _scroll.width(), _scroll.y(), r.left() + r.width() - _scroll.x() - _scroll.width(), _scroll.height(), st::white->b);
		p.fillRect(r.left(), _scroll.y() + _scroll.height(), r.width(), r.top() + r.height() - _scroll.y() - _scroll.height(), st::white->b);
	} else {
		p.drawPixmap(r.left(), r.top(), _cache);
	}
}

void EmojiPan::enterEvent(QEvent *e) {
	_hideTimer.stop();
	if (_hiding) showStart();
}

void EmojiPan::leaveEvent(QEvent *e) {
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
	_inner.refreshStickers();
}

bool EmojiPan::animStep(float64 ms) {
	float64 dt = ms / st::dropdownDef.duration;
	bool res = true;
	if (dt >= 1) {
		a_opacity.finish();
		if (_hiding) {
			hideFinish();
		} else {
			showAll();
			_cache = QPixmap();
		}
		res = false;
	} else {
		a_opacity.update(dt, anim::linear);
	}
	update();
	return res;
}

void EmojiPan::hideStart() {
	if (_cache.isNull()) {
		showAll();
		_cache = myGrab(this, rect().marginsRemoved(st::dropdownDef.padding));
	}
	hideAll();
	_hiding = true;
	a_opacity.start(0);
	anim::start(this);
}

void EmojiPan::hideFinish() {
	hide();
	_inner.hideFinish();
	_cache = QPixmap();
	_recent.setChecked(true);
}

void EmojiPan::showStart() {
	if (!isHidden() && a_opacity.current() == 1) {
		return;
	}
	if (isHidden()) {
		_inner.refreshRecent();
	}
	if (_cache.isNull()) {
		showAll();
		_cache = myGrab(this, rect().marginsRemoved(st::dropdownDef.padding));
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

void EmojiPan::showAll() {
	_recent.show();
	_people.show();
	_nature.show();
	_food.show();
	_celebration.show();
	_activity.show();
	_travel.show();
	_objects.show();
	_stickers.show();
	_scroll.show();
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
	_stickers.hide();
	_scroll.hide();
	_inner.clearSelection(true);
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
	else if (_stickers.checked()) newTab = dbietStickers;
	_inner.showEmojiPack(newTab);
}

void EmojiPan::onScroll() {
	int top = _scroll.scrollTop();
	DBIEmojiTab tab = _inner.currentTab(top);
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
		case dbietStickers   : check = &_stickers   ; break;
	}
	if (check && !check->checked()) {
		_noTabUpdate = true;
		check->setChecked(true);
		_noTabUpdate = false;
	}
	_inner.setScrollTop(top);
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
			p.drawPixmap(st::mentionPadding.left(), i * st::mentionHeight + st::mentionPadding.top(), user->photo->pix(st::mentionPhotoSize));
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
