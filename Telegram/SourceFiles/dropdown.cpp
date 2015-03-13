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

Dropdown::Dropdown(QWidget *parent) : TWidget(parent),
	_hiding(false), a_opacity(0), _shadow(st::dropdownShadow) {
	resetButtons();

	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(hideStart()));

	if (cPlatform() == dbipMac) {
		connect(App::wnd()->windowHandle(), SIGNAL(activeChanged()), this, SLOT(onWndActiveChanged()));
	}
}

void Dropdown::onWndActiveChanged() {
	if (!App::wnd()->windowHandle()->isActive() && !isHidden()) {
		leaveEvent(0);
	}
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

void Dropdown::resetButtons() {
	_width = st::dropdownPadding.left() + st::dropdownPadding.right();
	_height = st::dropdownPadding.top() + st::dropdownPadding.bottom();
	resize(_width, _height);
	for (int32 i = 0, l = _buttons.size(); i < l; ++i) {
		delete _buttons[i];
	}
	_buttons.clear();
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

EmojiPanInner::EmojiPanInner(QWidget *parent) : QWidget(parent), _tab(cEmojiTab()), _selected(-1), _xSelected(-1), _pressedSel(-1), _xPressedSel(-1) {
	resize(EmojiPadPerRow * st::emojiPanSize.width(), EmojiPadRowsPerPage * st::emojiPanSize.height() - st::emojiPanSub);
	setMouseTracking(true);
	setFocusPolicy(Qt::NoFocus);

	_saveConfigTimer.setSingleShot(true);
	connect(&_saveConfigTimer, SIGNAL(timeout()), this, SLOT(onSaveConfig()));
}

void EmojiPanInner::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	QRect r = e ? e->rect() : rect();

	if (_tab == dbietStickers) {
		int32 size = _stickers.size();
		float64 stickerWidth = width() / float64(StickerPadPerRow);
		int32 rows = (size / StickerPadPerRow) + ((size % StickerPadPerRow) ? 1 : 0), stickerSize = int32(stickerWidth);
		int32 fromrow = qMax(qFloor(r.top() / stickerSize), 0), torow = qMin(qCeil(r.bottom() / stickerSize) + 1, rows);
		for (int32 i = fromrow; i < torow; ++i) {
			for (int32 j = 0; j < StickerPadPerRow; ++j) {
				int32 index = i * StickerPadPerRow + j;
				if (index >= size) break;

				float64 hover = _hovers[index];

				QPoint pos(qRound(j * stickerWidth), i * stickerSize);
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
					float64 xHover = _hovers[_stickers.size() + index];

					QPoint xPos = pos + QPoint(stickerWidth - st::stickerPanDelete.pxWidth(), 0);
					p.setOpacity(hover * (xHover + (1 - xHover) * st::stickerPanDeleteOpacity));
					p.drawPixmap(xPos, App::sprite(), st::stickerPanDelete);
					p.setOpacity(1);
				}
			}
		}
	} else {
		int32 size = _emojis.size();
		int32 rows = (size / EmojiPadPerRow) + ((size % EmojiPadPerRow) ? 1 : 0);
		int32 fromrow = qMax(qFloor(r.top() / st::emojiPanSize.height()), 0), torow = qMin(qCeil(r.bottom() / st::emojiPanSize.height()) + 1, rows);
		for (int32 i = fromrow; i < torow; ++i) {
			for (int32 j = 0; j < EmojiPadPerRow; ++j) {
				int32 index = i * EmojiPadPerRow + j;
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
}

void EmojiPanInner::mousePressEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
	_pressedSel = _selected;
	_xPressedSel = _xSelected;
}

void EmojiPanInner::mouseReleaseEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
	if (_xSelected == _xPressedSel && _xSelected >= 0 && _tab == dbietStickers) {
		RecentStickerPack recent(cRecentStickers());
		DocumentData *sticker = _stickers.at(_xSelected - _stickers.size());
		for (int32 i = 0, l = recent.size(); i < l; ++i) {
			if (recent.at(i).first == sticker) {
				recent.removeAt(i);
				cSetRecentStickers(recent);
				Local::writeRecentStickers();
				showEmojiPack(dbietStickers);
				updateSelected();
				break;
			}
		}
	} else if (_selected == _pressedSel && _selected >= 0) {
		if (_tab == dbietStickers) {
			if (_selected < _stickers.size()) {
				emit stickerSelected(_stickers[_selected]);
			}
		} else if (_selected < _emojis.size()) {
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
				while (recent.size() >= EmojiPadPerRow * EmojiPadRowsPerPage) recent.pop_back();
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
}

void EmojiPanInner::onSaveConfig() {
	Local::writeUserSettings();
}

void EmojiPanInner::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
}

void EmojiPanInner::leaveEvent(QEvent *e) {
	clearSelection();
}

void EmojiPanInner::clearSelection(bool fast) {
	_lastMousePos = mapToGlobal(QPoint(-10, -10));
	if (fast) {
		if (_tab == dbietStickers) {
			_hovers = QVector<float64>(_stickers.size() * 2, 0);
		} else {
			_hovers = QVector<float64>(_emojis.size(), 0);
		}
		_emojiAnimations.clear();
		_selected = _pressedSel = _xSelected = _xPressedSel = -1;
		anim::stop(this);
	} else {
		updateSelected();
	}
}

void EmojiPanInner::updateSelected() {
	int32 selIndex = -1, xSelIndex = -1;
	QPoint p(mapFromGlobal(_lastMousePos));
	if (_tab == dbietStickers) {
		float64 stickerWidth = width() / float64(StickerPadPerRow);
		int32 stickerSize = int32(stickerWidth);
		if (p.x() >= 0 && p.y() >= 0 && p.x() < StickerPadPerRow * stickerWidth) {
			selIndex = qFloor(p.y() / stickerSize) * StickerPadPerRow + qFloor(p.x() / stickerWidth);
			if (selIndex >= _stickers.size()) {
				selIndex = -1;
			} else {
				int32 inx = p.x() - (selIndex % StickerPadPerRow) * stickerWidth, iny = p.y() - ((selIndex / StickerPadPerRow) * stickerSize);
				if (inx >= stickerWidth - st::stickerPanDelete.pxWidth() && iny < st::stickerPanDelete.pxHeight()) {
					xSelIndex = _stickers.size() + selIndex;
				}
			}
		}
	} else if (p.x() >= 0 && p.y() >= 0 && p.x() < EmojiPadPerRow * st::emojiPanSize.width()) {
		selIndex = qFloor(p.y() / st::emojiPanSize.height()) * EmojiPadPerRow + qFloor(p.x() / st::emojiPanSize.width());
		if (selIndex >= _emojis.size()) {
			selIndex = -1;
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
	if (xSelIndex != _xSelected) {
		if (_xSelected >= 0) {
			_emojiAnimations.remove(_xSelected + 1);
			if (_emojiAnimations.find(-_xSelected - 1) == _emojiAnimations.end()) {
				if (_emojiAnimations.isEmpty()) startanim = true;
				_emojiAnimations.insert(-_xSelected - 1, getms());
			}
		}
		_xSelected = xSelIndex;
		if (_xSelected >= 0) {
			_emojiAnimations.remove(-_xSelected - 1);
			if (_emojiAnimations.find(_xSelected + 1) == _emojiAnimations.end()) {
				if (_emojiAnimations.isEmpty()) startanim = true;
				_emojiAnimations.insert(_xSelected + 1, getms());
			}
		}
	}
	if (startanim) anim::start(this);
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
	int32 h, size;
	if (packIndex == dbietStickers) {
		_emojis.clear();

		float64 stickerWidth = width() / float64(StickerPadPerRow);
		int32 stickerSize = int32(stickerWidth);

		int32 l = cRecentStickers().size();
		_stickers.resize(l);
		_isUserGen.resize(l);
		for (int32 i = 0; i < l; ++i) {
			DocumentData *sticker = _stickers[i] = cRecentStickers().at(i).first;
			_isUserGen[i] = (cRecentStickers().at(i).second < 0);
			if (i < StickerPadPerRow * ((EmojiPadRowsPerPage * st::emojiPanSize.height() - int(st::emojiPanSub)) / stickerSize + 1)) {
				bool already = !sticker->already().isEmpty(), hasdata = !sticker->data.isEmpty();
				if (!sticker->loader && sticker->status != FileFailed && !already && !hasdata) {
					sticker->save(QString());
				}
			}
		}

		size = _stickers.size();
		h = ((size / StickerPadPerRow) + ((size % StickerPadPerRow) ? 1 : 0)) * stickerSize;
		_hovers = QVector<float64>(size * 2, 0);
	} else {
		_emojis = emojiPack(packIndex);
		_stickers.clear();
		_isUserGen.clear();

		size = _emojis.size();
		h = ((size / EmojiPadPerRow) + ((size % EmojiPadPerRow) ? 1 : 0)) * st::emojiPanSize.height();
		_hovers = QVector<float64>(size, 0);
	}
	h = qMax(h, EmojiPadRowsPerPage * st::emojiPanSize.height() - int(st::emojiPanSub));
	_emojiAnimations.clear();
	_selected = _pressedSel = -1;
	resize(width(), h);
	_lastMousePos = QCursor::pos();
	updateSelected();
	update();
}

EmojiPan::EmojiPan(QWidget *parent) : TWidget(parent),
_hiding(false), a_opacity(0), _shadow(st::dropdownShadow),
_recent  (this, qsl("emoji_group"), dbietRecent  , QString(), cEmojiTab() == dbietRecent  , st::rbEmojiRecent),
_people  (this, qsl("emoji_group"), dbietPeople  , QString(), cEmojiTab() == dbietPeople  , st::rbEmojiPeople),
_nature  (this, qsl("emoji_group"), dbietNature  , QString(), cEmojiTab() == dbietNature  , st::rbEmojiNature),
_objects (this, qsl("emoji_group"), dbietObjects , QString(), cEmojiTab() == dbietObjects , st::rbEmojiObjects),
_places  (this, qsl("emoji_group"), dbietPlaces  , QString(), cEmojiTab() == dbietPlaces  , st::rbEmojiPlaces),
_symbols (this, qsl("emoji_group"), dbietSymbols , QString(), cEmojiTab() == dbietSymbols , st::rbEmojiSymbols),
_stickers(this, qsl("emoji_group"), dbietStickers, QString(), cEmojiTab() == dbietStickers, st::rbEmojiStickers),
_scroll(this, st::emojiScroll), _inner() {
	setFocusPolicy(Qt::NoFocus);
	_scroll.setFocusPolicy(Qt::NoFocus);
	_scroll.viewport()->setFocusPolicy(Qt::NoFocus);

	if (cEmojiTab() != dbietStickers) {
		_inner.showEmojiPack(cEmojiTab());
	}

	_scroll.setGeometry(st::dropdownPadding.left() + st::emojiPanPadding.left(), st::dropdownPadding.top() + _recent.height() + st::emojiPanPadding.top(), st::emojiPanPadding.left() + _inner.width() + st::emojiPanPadding.right(), EmojiPadRowsPerPage * st::emojiPanSize.height() - st::emojiPanSub);
	_scroll.setWidget(&_inner);

	_width = st::dropdownPadding.left() + st::emojiPanPadding.left() + _scroll.width() + st::emojiPanPadding.right() + st::dropdownPadding.right();
	_height = st::dropdownPadding.top() + _recent.height() + st::emojiPanPadding.top() + _scroll.height() + st::emojiPanPadding.bottom() + st::dropdownPadding.bottom();
	resize(_width, _height);

	int32 left = st::dropdownPadding.left() + (_width - st::dropdownPadding.left() - st::dropdownPadding.right() - 7 * _recent.width()) / 2;
	int32 top = st::dropdownPadding.top();
	_recent.move(left, top);  left += _recent.width();
	_people.move(left, top);  left += _people.width();
	_nature.move(left, top);  left += _nature.width();
	_objects.move(left, top); left += _objects.width();
	_places.move(left, top);  left += _places.width();
	_symbols.move(left, top); left += _symbols.width();
	_stickers.move(left, top); left += _stickers.width();

	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(hideStart()));

	connect(&_recent  , SIGNAL(changed()), this, SLOT(onTabChange()));
	connect(&_people  , SIGNAL(changed()), this, SLOT(onTabChange()));
	connect(&_nature  , SIGNAL(changed()), this, SLOT(onTabChange()));
	connect(&_objects , SIGNAL(changed()), this, SLOT(onTabChange()));
	connect(&_places  , SIGNAL(changed()), this, SLOT(onTabChange()));
	connect(&_symbols , SIGNAL(changed()), this, SLOT(onTabChange()));
	connect(&_stickers, SIGNAL(changed()), this, SLOT(onTabChange()));

	connect(&_scroll, SIGNAL(scrolled()), &_inner, SLOT(updateSelected()));

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
	_recent.setChecked(true);
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
	if (_stickers.checked()) emit updateStickers();
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
	_objects.show();
	_places.show();
	_symbols.show();
	_stickers.show();
	_scroll.show();
}

void EmojiPan::hideAll() {
	_recent.hide();
	_people.hide();
	_nature.hide();
	_objects.hide();
	_places.hide();
	_symbols.hide();
	_stickers.hide();
	_scroll.hide();
	_inner.clearSelection(true);
}

void EmojiPan::onTabChange() {
	DBIEmojiTab newTab = dbietRecent;
	if (_people.checked()) newTab = dbietPeople;
	else if (_nature.checked()) newTab = dbietNature;
	else if (_objects.checked()) newTab = dbietObjects;
	else if (_places.checked()) newTab = dbietPlaces;
	else if (_symbols.checked()) newTab = dbietSymbols;
	else if (_stickers.checked()) newTab = dbietStickers;
	if (newTab != cEmojiTab()) {
		cSetEmojiTab(newTab);
		Local::writeUserSettings();
		_scroll.scrollToY(0);
	}
	_inner.showEmojiPack(newTab);
	if (newTab == dbietStickers) {
		emit updateStickers();
	}
}

//StickerPanInner::StickerPanInner(QWidget *parent) : QWidget(parent), _emoji(0), _selected(-1), _pressedSel(-1) {
//	resize(StickerPadPerRow * st::stickerPanSize.width(), EmojiPadRowsPerPage * st::emojiPanSize.height() - st::emojiPanSub);
//	setMouseTracking(true);
//	setFocusPolicy(Qt::NoFocus);
//}
//
//void StickerPanInner::paintEvent(QPaintEvent *e) {
//	QPainter p(this);
//	int32 size = _stickers.size();
//
//	QRect r = e ? e->rect() : rect();
//
//	int32 rows = (size / StickerPadPerRow) + ((size % StickerPadPerRow) ? 1 : 0);
//	int32 fromrow = qMax(qFloor(r.top() / st::stickerPanSize.height()), 0), torow = qMin(qCeil(r.bottom() / st::stickerPanSize.height()) + 1, rows);
//	for (int32 i = fromrow; i < torow; ++i) {
//		for (int32 j = 0; j < StickerPadPerRow; ++j) {
//			int32 index = i * StickerPadPerRow + j;
//			if (index >= size) break;
//
//			float64 hover = _hovers[index];
//			QPoint pos(j * st::stickerPanSize.width(), i * st::stickerPanSize.height());
//			if (hover > 0) {
//				p.setOpacity(hover);
//				p.setBrush(st::stickerPanHover->b);
//				p.setPen(Qt::NoPen);
//				p.drawRoundedRect(QRect(pos, st::stickerPanSize), st::stickerPanRound, st::stickerPanRound);
//				p.setOpacity(1);
//			}
//
//			DocumentData *data = _stickers[index];
//			bool already = !data->already().isEmpty(), hasdata = !data->data.isEmpty();
//			if (!data->loader && data->status != FileFailed && !already && !hasdata) {
//				data->save(QString());
//			}
//			if (data->sticker->isNull() && (already || hasdata)) {
//				if (already) {
//					data->sticker = ImagePtr(data->already());
//				} else {
//					data->sticker = ImagePtr(data->data);
//				}
//			}
//
//			float64 coef = qMin(st::stickerPanSize.width() / float64(data->dimensions.width()), st::stickerPanSize.height() / float64(data->dimensions.height()));
//			int32 w = qRound(coef * data->dimensions.width()), h = qRound(coef * data->dimensions.height());
//			pos += QPoint((st::stickerPanSize.width() - w) / 2, (st::stickerPanSize.height() - h) / 2);
//
//			if (data->sticker->isNull()) {
//				p.drawPixmap(pos, data->thumb->pix(w));
//			} else {
//				p.drawPixmap(pos, data->sticker->pix(w));
//			}
//		}
//	}
//}
//
//void StickerPanInner::mousePressEvent(QMouseEvent *e) {
//	_lastMousePos = e->globalPos();
//	updateSelected();
//	_pressedSel = _selected;
//}
//
//void StickerPanInner::mouseReleaseEvent(QMouseEvent *e) {
//	_lastMousePos = e->globalPos();
//	updateSelected();
//	if (_selected == _pressedSel && _selected >= 0 && _selected < _stickers.size()) {
//		emit stickerSelected(_stickers[_selected]);
//	}
//}
//
//void StickerPanInner::mouseMoveEvent(QMouseEvent *e) {
//	_lastMousePos = e->globalPos();
//	updateSelected();
//}
//
//void StickerPanInner::leaveEvent(QEvent *e) {
//	_lastMousePos = QCursor::pos();
//	updateSelected();
//}
//
//void StickerPanInner::updateSelected() {
//	int32 selIndex = -1;
//	QPoint p(mapFromGlobal(_lastMousePos));
//	if (p.x() >= 0 && p.y() >= 0 && p.x() < StickerPadPerRow * st::stickerPanSize.width()) {
//		selIndex = qFloor(p.y() / st::stickerPanSize.height()) * StickerPadPerRow + qFloor(p.x() / st::stickerPanSize.width());
//		if (selIndex >= _stickers.size()) {
//			selIndex = -1;
//		}
//	}
//	if (selIndex != _selected) {
//		bool startanim = false;
//		if (_selected >= 0) {
//			_stickerAnimations.remove(_selected + 1);
//			if (_stickerAnimations.find(-_selected - 1) == _stickerAnimations.end()) {
//				if (_stickerAnimations.isEmpty()) startanim = true;
//				_stickerAnimations.insert(-_selected - 1, getms());
//			}
//		}
//		_selected = selIndex;
//		if (_selected >= 0) {
//			_stickerAnimations.remove(-_selected - 1);
//			if (_stickerAnimations.find(_selected + 1) == _stickerAnimations.end()) {
//				if (_stickerAnimations.isEmpty()) startanim = true;
//				_stickerAnimations.insert(_selected + 1, getms());
//			}
//		}
//		if (startanim) anim::start(this);
//		setCursor((_selected >= 0) ? style::cur_pointer : style::cur_default);
//	}
//}
//
//bool StickerPanInner::animStep(float64 ms) {
//	uint64 now = getms();
//	for (StickerAnimations::iterator i = _stickerAnimations.begin(); i != _stickerAnimations.end();) {
//		float64 dt = float64(now - i.value()) / st::emojiPanDuration;
//		if (dt >= 1) {
//			_hovers[qAbs(i.key()) - 1] = (i.key() > 0) ? 1 : 0;
//			i = _stickerAnimations.erase(i);
//		} else {
//			_hovers[qAbs(i.key()) - 1] = (i.key() > 0) ? dt : (1 - dt);
//			++i;
//		}
//	}
//	update();
//	return !_stickerAnimations.isEmpty();
//}
//
//void StickerPanInner::showStickerPack(EmojiPtr emoji) {
//	StickerPack stickers = cStickers().value(emoji);
//	if (stickers.isEmpty()) {
//		_emoji = 0;
//	} else {
//		_emoji = emoji;
//		_stickers = stickers;
//		_hovers = QVector<float64>(_stickers.size(), 0);
//		_stickerAnimations.clear();
//		_selected = _pressedSel = -1;
//		int32 size = _stickers.size();
//		int32 h = qMax(((size / StickerPadPerRow) + ((size % StickerPadPerRow) ? 1 : 0)) * st::stickerPanSize.height(), EmojiPadRowsPerPage * st::emojiPanSize.height() - int(st::emojiPanSub));
//		resize(width(), h);
//		_lastMousePos = QCursor::pos();
//		updateSelected();
//		update();
//	}
//}
//
//bool StickerPanInner::hasContent() const {
//	return !!_emoji;
//}
//
//StickerPan::StickerPan(QWidget *parent) : TWidget(parent),
//_hiding(false), a_opacity(0), _shadow(st::dropdownShadow),
//_scroll(this, st::emojiScroll), _emoji(0), _inner() {
//	setFocusPolicy(Qt::NoFocus);
//	_scroll.setFocusPolicy(Qt::NoFocus);
//	_scroll.viewport()->setFocusPolicy(Qt::NoFocus);
//
//	_inner.showStickerPack(0);
//	_scroll.setGeometry(st::dropdownPadding.left() + st::stickerPanPadding.left(), st::dropdownPadding.top() + st::rbEmoji.height + st::stickerPanPadding.top(), st::stickerPanPadding.left() + _inner.width() + st::stickerPanPadding.right(), EmojiPadRowsPerPage * st::emojiPanSize.height() - st::emojiPanSub);
//	_scroll.setWidget(&_inner);
//
//	_width = st::dropdownPadding.left() + st::stickerPanPadding.left() + _scroll.width() + st::stickerPanPadding.right() + st::dropdownPadding.right();
//	_height = st::dropdownPadding.top() + st::rbEmoji.height + st::stickerPanPadding.top() + _scroll.height() + st::stickerPanPadding.bottom() + st::dropdownPadding.bottom();
//	resize(_width, _height);
//
//	_hideTimer.setSingleShot(true);
//	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(hideStart()));
//
//	connect(&_scroll, SIGNAL(scrolled()), &_inner, SLOT(updateSelected()));
//
//	connect(&_inner, SIGNAL(stickerSelected(DocumentData*)), this, SIGNAL(stickerSelected(DocumentData*)));
//}
//
//void StickerPan::setStickerPack(EmojiPtr emoji, bool show) {
//	_emoji = emoji;
//	_inner.showStickerPack(_emoji);
//	if (!_hiding && !isHidden() && !_inner.hasContent()) {
//		_hideTimer.stop();
//		hideStart();
//	} else if ((_hiding || isHidden()) && _inner.hasContent() && show) {
//		_hideTimer.stop();
//		showStart();
//	}
//}
//
//void StickerPan::paintEvent(QPaintEvent *e) {
//	QPainter p(this);
//
//	if (!_cache.isNull()) {
//		p.setOpacity(a_opacity.current());
//	}
//
//	QRect r(st::dropdownPadding.left(), st::dropdownPadding.top(), _width - st::dropdownPadding.left() - st::dropdownPadding.right(), _height - st::dropdownPadding.top() - st::dropdownPadding.bottom());
//
//	// draw shadow
//	_shadow.paint(p, r);
//
//	if (_cache.isNull()) {
//		p.fillRect(r, st::white->b);
//
//		p.setFont(st::stickerPanFont->f);
//		p.setPen(st::stickerPanColor->p);
//		p.drawText(QRect(st::dropdownPadding.left(), st::dropdownPadding.top(), width() - st::dropdownPadding.left() - st::dropdownPadding.right(), st::rbEmoji.height), lang(lng_attach_stickers_header), style::al_center);
//	} else {
//		p.drawPixmap(r.left(), r.top(), _cache);
//	}
//}
//
//void StickerPan::enterEvent(QEvent *e) {
//	_hideTimer.stop();
//	if (_hiding) showStart();
//}
//
//void StickerPan::leaveEvent(QEvent *e) {
//	if (animating()) {
//		hideStart();
//	} else {
//		_hideTimer.start(300);
//	}
//}
//
//void StickerPan::otherEnter() {
//	_hideTimer.stop();
//	showStart();
//}
//
//void StickerPan::otherLeave() {
//	if (animating()) {
//		hideStart();
//	} else {
//		_hideTimer.start(0);
//	}
//}
//
//void StickerPan::fastHide() {
//	if (animating()) {
//		anim::stop(this);
//	}
//	a_opacity = anim::fvalue(0, 0);
//	_hideTimer.stop();
//	hide();
//	_cache = QPixmap();
//}
//
//bool StickerPan::animStep(float64 ms) {
//	float64 dt = ms / 150;
//	bool res = true;
//	if (dt >= 1) {
//		a_opacity.finish();
//		if (_hiding) {
//			hideFinish();
//		} else {
//			showAll();
//			_cache = QPixmap();
//		}
//		res = false;
//	} else {
//		a_opacity.update(dt, anim::linear);
//	}
//	update();
//	return res;
//}
//
//void StickerPan::hideStart() {
//	if (_cache.isNull()) {
//		showAll();
//		_cache = myGrab(this, rect().marginsRemoved(st::dropdownPadding));
//	}
//	hideAll();
//	_hiding = true;
//	a_opacity.start(0);
//	anim::start(this);
//}
//
//void StickerPan::hideFinish() {
//	hide();
//	_cache = QPixmap();
//}
//
//void StickerPan::showStart() {
//	if (!isHidden() && a_opacity.current() == 1) {
//		return;
//	}
//	if (!_inner.hasContent()) {
//		return;
//	}
//	if (_cache.isNull()) {
//		showAll();
//		_cache = myGrab(this, rect().marginsRemoved(st::dropdownPadding));
//	}
//	hideAll();
//	_hiding = false;
//	show();
//	a_opacity.start(1);
//	anim::start(this);
//}
//
//bool StickerPan::eventFilter(QObject *obj, QEvent *e) {
//	if (e->type() == QEvent::Enter) {
//		if (dynamic_cast<EmojiPan*>(obj)) {
//			enterEvent(e);
//		} else {
//			otherEnter();
//		}
//	} else if (e->type() == QEvent::Leave) {
//		if (dynamic_cast<EmojiPan*>(obj)) {
//			leaveEvent(e);
//		} else {
//			otherLeave();
//		}
//	} else if (e->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton && !dynamic_cast<EmojiPan*>(obj)) {
//		if (isHidden() || _hiding) {
//			otherEnter();
//		} else {
//			otherLeave();
//		}
//	}
//	return false;
//}
//
//void StickerPan::showAll() {
//	_scroll.show();
//}
//
//void StickerPan::hideAll() {
//	_scroll.hide();
//}
