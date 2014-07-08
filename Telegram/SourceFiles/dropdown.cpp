/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#include "stdafx.h"

#include "dropdown.h"
#include "historywidget.h"

#include "lang.h"

Dropdown::Dropdown(QWidget *parent) : TWidget(parent),
	_hiding(false), a_opacity(0), _shadow(st::dropdownShadow) {
	_width = st::dropdownPadding.left() + st::dropdownPadding.right();
	_height = st::dropdownPadding.top() + st::dropdownPadding.bottom();
	resize(_width, _height);

	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(hideStart()));
}

IconedButton *Dropdown::addButton(IconedButton *button) {
	button->setParent(this);

	_width = qMax(_width, st::dropdownPadding.left() + st::dropdownPadding.right() + button->width());
	if (!_buttons.isEmpty()) {
		_height += st::dropdownBorder;
	}
	_height += button->height();

	_buttons.push_back(button);

	resize(_width, _height);

	return button;
}

void Dropdown::resizeEvent(QResizeEvent *e) {
	int32 top = st::dropdownPadding.top();
	for (Buttons::const_iterator i = _buttons.cbegin(), e = _buttons.cend(); i != e; ++i) {
		(*i)->move(st::dropdownPadding.left(), top);
		top += st::dropdownBorder + (*i)->height();
	}
}

void Dropdown::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	if (animating()) {
		p.setOpacity(a_opacity.current());
	}

	QRect r(st::dropdownPadding.left(), st::dropdownPadding.top(), _width - st::dropdownPadding.left() - st::dropdownPadding.right(), _height - st::dropdownPadding.top() - st::dropdownPadding.bottom());
	// draw shadow
	_shadow.paint(p, r);

	if (!_buttons.isEmpty()) { // paint separators
		int32 top = st::dropdownPadding.top() + _buttons.front()->height();
		p.setPen(st::dropdownBorderColor->p);
		for (int32 i = 1, s = _buttons.size(); i < s; ++i) {
			p.fillRect(st::dropdownPadding.left(), top, _width - st::dropdownPadding.left() - st::dropdownPadding.right(), st::dropdownBorder, st::dropdownBorderColor->b);
			top += st::dropdownBorder + _buttons[i]->height();
		}
	}
}

void Dropdown::enterEvent(QEvent *e) {
	_hideTimer.stop();
	if (_hiding) showStart();
}

void Dropdown::leaveEvent(QEvent *e) {
	if (animating()) {
		hideStart();
	} else {
		_hideTimer.start(300);
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
	hide();
}

void Dropdown::showStart() {
	if (!isHidden() && a_opacity.current() == 1) {
		return;
	}
	_hiding = false;
	show();
	a_opacity.start(1);
	anim::start(this);
}

bool Dropdown::animStep(float64 ms) {
	float64 dt = ms / 150;
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
	float64 dt = ms / 150;
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

static const int emojiPerRow = 7, emojiRowsPerPage = 6;

EmojiPanInner::EmojiPanInner(QWidget *parent) : QWidget(parent), _tab(cEmojiTab()), _selected(-1), _pressedSel(-1) {
	resize(emojiPerRow * st::emojiPanSize.width(), emojiRowsPerPage * st::emojiPanSize.height() - st::emojiPanSub);
	setMouseTracking(true);
	setFocusPolicy(Qt::NoFocus);
	_saveConfigTimer.setSingleShot(true);
	connect(&_saveConfigTimer, SIGNAL(timeout()), this, SLOT(onSaveConfig()));
}

void EmojiPanInner::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	int32 size = _emojis.size();
	if (!size) {
		p.setFont(st::emojiPanFont->f);
		p.setPen(st::emojiPanText->p);
		p.drawText(QRect(0, 0, width(), height() * 0.75), lang(lng_emoji_no_recent), QTextOption(style::al_center));
		return;
	}

	QRect r = e ? e->rect() : rect();

	int32 rows = (size / emojiPerRow) + ((size % emojiPerRow) ? 1 : 0);
	int32 fromrow = qMax(qFloor(r.top() / st::emojiPanSize.height()), 0), torow = qMin(qCeil(r.bottom() / st::emojiPanSize.height()) + 1, rows);
	for (int32 i = fromrow; i < torow; ++i) {
		for (int32 j = 0; j < emojiPerRow; ++j) {
			int32 index = i * emojiPerRow + j;
			if (index >= size) break;

			float64 hover = _hovers[index];

			QPoint w(j * st::emojiPanSize.width(), i * st::emojiPanSize.height());
			if (hover > 0) {
				p.setOpacity(hover);
				p.setBrush(st::emojiPanHover->b);
				p.setPen(Qt::NoPen);
				p.drawRoundedRect(QRect(w, st::emojiPanSize), st::emojiPanRound, st::emojiPanRound);
				p.setOpacity(1);
			}
			QRect r(_emojis[index]->x, _emojis[index]->y, st::emojiImgSize, st::emojiImgSize);
			p.drawPixmap(w + QPoint((st::emojiPanSize.width() - st::emojiSize) / 2, (st::emojiPanSize.height() - st::emojiSize) / 2), App::emojis(), r);
		}
	}
}

void EmojiPanInner::mousePressEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
	_pressedSel = _selected;
}

void EmojiPanInner::mouseReleaseEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
	if (_selected == _pressedSel && _selected >= 0 && _selected < _emojis.size()) {
		EmojiPtr emoji(_emojis[_selected]);
		RecentEmojiPack recent(cGetRecentEmojis());
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
			while (recent.size() >= emojiPerRow * emojiRowsPerPage) recent.pop_back();
			recent.push_back(qMakePair(emoji, 1));
			for (i = recent.end() - 1; i != recent.begin(); --i) {
				if ((i - 1)->second > i->second) {
					break;
				}
				qSwap(*i, *(i - 1));
			}
		}
		cSetRecentEmojis(recent);
		_saveConfigTimer.start(SaveRecentEmojisTimeout);

		emit emojiSelected(emoji);
	}
}

void EmojiPanInner::onSaveConfig() {
	App::writeUserConfig();
}

void EmojiPanInner::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
}

void EmojiPanInner::leaveEvent(QEvent *e) {
	_lastMousePos = QCursor::pos();
	updateSelected();
}

void EmojiPanInner::updateSelected() {
	int32 selIndex = -1;
	QPoint p(mapFromGlobal(_lastMousePos));
	if (p.x() >= 0 && p.y() >= 0 && p.x() < emojiPerRow * st::emojiPanSize.width()) {
		selIndex = qFloor(p.y() / st::emojiPanSize.height()) * emojiPerRow + qFloor(p.x() / st::emojiPanSize.width());
		if (selIndex >= _emojis.size()) {
			selIndex = -1;
		}
	}
	if (selIndex != _selected) {
		bool startanim = false;
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
		if (startanim) anim::start(this);
		setCursor((_selected >= 0) ? style::cur_pointer : style::cur_default);
	}
}

bool EmojiPanInner::animStep(float64 ms) {
	uint64 now = getms();
	for (EmojiAnimations::iterator i = _emojiAnimations.begin(); i != _emojiAnimations.end();) {
		float64 dt = float64(now - i.value()) / st::emojiPanDuration;
		if (dt >= 1) {
			_hovers[qAbs(i.key()) - 1] = (i.key() > 0) ? 1 : 0;
			i = _emojiAnimations.erase(i);
		} else {
			_hovers[qAbs(i.key()) - 1] = (i.key() > 0) ? dt : (1 - dt);
			++i;
		}
	}
	update();
	return !_emojiAnimations.isEmpty();
}

void EmojiPanInner::showEmojiPack(DBIEmojiTab packIndex) {
	_tab = packIndex;
	_emojis = emojiPack(packIndex);
	_hovers = QVector<float64>(_emojis.size(), 0);
	_emojiAnimations.clear();
	_selected = _pressedSel = -1;
	int32 size = _emojis.size();
	int32 h = qMax(((size / emojiPerRow) + ((size % emojiPerRow) ? 1 : 0)) * st::emojiPanSize.height(), emojiRowsPerPage * st::emojiPanSize.height() - int(st::emojiPanSub));
	resize(width(), h);
	_lastMousePos = QCursor::pos();
	updateSelected();
	update();
}

EmojiPan::EmojiPan(QWidget *parent) : TWidget(parent),
_hiding(false), a_opacity(0), _shadow(st::dropdownShadow),
_recent (this, qsl("emoji_group"), dbietRecent , QString(), cEmojiTab() == dbietRecent , st::rbEmojiRecent),
_people (this, qsl("emoji_group"), dbietPeople , QString(), cEmojiTab() == dbietPeople , st::rbEmojiPeople),
_nature (this, qsl("emoji_group"), dbietNature , QString(), cEmojiTab() == dbietNature , st::rbEmojiNature),
_objects(this, qsl("emoji_group"), dbietObjects, QString(), cEmojiTab() == dbietObjects, st::rbEmojiObjects),
_places (this, qsl("emoji_group"), dbietPlaces , QString(), cEmojiTab() == dbietPlaces , st::rbEmojiPlaces),
_symbols(this, qsl("emoji_group"), dbietSymbols, QString(), cEmojiTab() == dbietSymbols, st::rbEmojiSymbols),
_scroll(this, st::emojiScroll), _inner() {
	setFocusPolicy(Qt::NoFocus);
	_scroll.setFocusPolicy(Qt::NoFocus);
	_scroll.viewport()->setFocusPolicy(Qt::NoFocus);

	_inner.showEmojiPack(cEmojiTab());

	_scroll.setGeometry(st::dropdownPadding.left() + st::emojiPanPadding.left(), st::dropdownPadding.top() + _recent.height() + st::emojiPanPadding.top(), st::emojiPanPadding.left() + _inner.width() + st::emojiPanPadding.right(), emojiRowsPerPage * st::emojiPanSize.height() - st::emojiPanSub);
	_scroll.setWidget(&_inner);

	_width = st::dropdownPadding.left() + st::emojiPanPadding.left() + _scroll.width() + st::emojiPanPadding.right() + st::dropdownPadding.right();
	_height = st::dropdownPadding.top() + _recent.height() + st::emojiPanPadding.top() + _scroll.height() + st::emojiPanPadding.bottom() + st::dropdownPadding.bottom();
	resize(_width, _height);

	int32 left = st::dropdownPadding.left() + (_width - st::dropdownPadding.left() - st::dropdownPadding.right() - 6 * _recent.width()) / 2;
	int32 top = st::dropdownPadding.top();
	_recent.move(left, top);  left += _recent.width();
	_people.move(left, top);  left += _people.width();
	_nature.move(left, top);  left += _nature.width();
	_objects.move(left, top); left += _objects.width();
	_places.move(left, top);  left += _places.width();
	_symbols.move(left, top); left += _symbols.width();

	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(hideStart()));

	connect(&_recent , SIGNAL(changed()), this, SLOT(onTabChange()));
	connect(&_people , SIGNAL(changed()), this, SLOT(onTabChange()));
	connect(&_nature , SIGNAL(changed()), this, SLOT(onTabChange()));
	connect(&_objects, SIGNAL(changed()), this, SLOT(onTabChange()));
	connect(&_places , SIGNAL(changed()), this, SLOT(onTabChange()));
	connect(&_symbols, SIGNAL(changed()), this, SLOT(onTabChange()));

	connect(&_scroll, SIGNAL(scrolled()), &_inner, SLOT(updateSelected()));

	connect(&_inner, SIGNAL(emojiSelected(EmojiPtr)), this, SIGNAL(emojiSelected(EmojiPtr)));
}

void EmojiPan::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	if (!_cache.isNull()) {
		p.setOpacity(a_opacity.current());
	}

	QRect r(st::dropdownPadding.left(), st::dropdownPadding.top(), _width - st::dropdownPadding.left() - st::dropdownPadding.right(), _height - st::dropdownPadding.top() - st::dropdownPadding.bottom());

	// draw shadow
	_shadow.paint(p, r);

	if (_cache.isNull()) {
		p.fillRect(r, st::white->b);
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

bool EmojiPan::animStep(float64 ms) {
	float64 dt = ms / 150;
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
		_cache = myGrab(this, rect().marginsRemoved(st::dropdownPadding));
	}
	hideAll();
	_hiding = true;
	a_opacity.start(0);
	anim::start(this);
}

void EmojiPan::hideFinish() {
	hide();
	_cache = QPixmap();
}

void EmojiPan::showStart() {
	if (!isHidden() && a_opacity.current() == 1) {
		return;
	}
	if (_cache.isNull()) {
		showAll();
		_cache = myGrab(this, rect().marginsRemoved(st::dropdownPadding));
	}
	hideAll();
	_hiding = false;
	show();
	a_opacity.start(1);
	anim::start(this);
}

bool EmojiPan::eventFilter(QObject *obj, QEvent *e) {
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

void EmojiPan::showAll() {
	_recent.show();
	_people.show();
	_nature.show();
	_objects.show();
	_places.show();
	_symbols.show();
	_scroll.show();
}

void EmojiPan::hideAll() {
	_recent.hide();
	_people.hide();
	_nature.hide();
	_objects.hide();
	_places.hide();
	_symbols.hide();
	_scroll.hide();
}

void EmojiPan::onTabChange() {
	DBIEmojiTab newTab = dbietRecent;
	if (_people.checked()) newTab = dbietPeople;
	else if (_nature.checked()) newTab = dbietNature;
	else if (_objects.checked()) newTab = dbietObjects;
	else if (_places.checked()) newTab = dbietPlaces;
	else if (_symbols.checked()) newTab = dbietSymbols;
	if (newTab != cEmojiTab()) {
		cSetEmojiTab(newTab);
		App::writeUserConfig();
		_scroll.scrollToY(0);
		_inner.showEmojiPack(newTab);
	}
}
