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
#include "stickers/emoji_pan.h"

#include "styles/style_stickers.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/effects/ripple_animation.h"
#include "boxes/confirmbox.h"
#include "boxes/stickersetbox.h"
#include "boxes/stickers_box.h"
#include "inline_bots/inline_bot_result.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "dialogs/dialogs_layout.h"
#include "stickers/stickers.h"
#include "historywidget.h"
#include "storage/localstorage.h"
#include "lang.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "auth_session.h"

namespace internal {
namespace {

constexpr auto kSaveRecentEmojiTimeout = 3000;

} // namespace

EmojiColorPicker::EmojiColorPicker(QWidget *parent) : TWidget(parent) {
	setMouseTracking(true);

	auto w = st::emojiPanMargins.left() + st::emojiPanSize.width() + st::emojiColorsSep + st::emojiPanMargins.right();
	auto h = st::emojiPanMargins.top() + 2 * st::emojiColorsPadding + st::emojiPanSize.height() + st::emojiPanMargins.bottom();
	resize(w, h);

	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(hideAnimated()));
}

void EmojiColorPicker::showEmoji(EmojiPtr emoji) {
	if (!emoji || !emoji->hasVariants()) {
		return;
	}
	_ignoreShow = false;

	_variants.resize(emoji->variantsCount() + 1);
	for (auto i = 0, size = _variants.size(); i != size; ++i) {
		_variants[i] = emoji->variant(i);
	}

	auto w = st::emojiPanMargins.left() + st::emojiPanSize.width() * _variants.size() + (_variants.size() - 2) * st::emojiColorsPadding + st::emojiColorsSep + st::emojiPanMargins.right();
	auto h = st::emojiPanMargins.top() + 2 * st::emojiColorsPadding + st::emojiPanSize.height() + st::emojiPanMargins.bottom();
	resize(w, h);

	if (!_cache.isNull()) _cache = QPixmap();
	showAnimated();
}

void EmojiColorPicker::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto opacity = _a_opacity.current(getms(), _hiding ? 0. : 1.);
	if (opacity < 1.) {
		if (opacity > 0.) {
			p.setOpacity(opacity);
		} else {
			return;
		}
	}
	if (e->rect() != rect()) {
		p.setClipRect(e->rect());
	}

	auto inner = rect().marginsRemoved(st::emojiPanMargins);
	if (!_cache.isNull()) {
		p.drawPixmap(0, 0, _cache);
		return;
	}
	Ui::Shadow::paint(p, inner, width(), st::defaultRoundShadow);
	App::roundRect(p, inner, st::boxBg, BoxCorners);

	auto x = st::emojiPanMargins.left() + 2 * st::emojiColorsPadding + st::emojiPanSize.width();
	if (rtl()) x = width() - x - st::emojiColorsSep;
	p.fillRect(x, st::emojiPanMargins.top() + st::emojiColorsPadding, st::emojiColorsSep, inner.height() - st::emojiColorsPadding * 2, st::emojiColorsSepColor);

	if (_variants.isEmpty()) return;
	for (auto i = 0, count = _variants.size(); i != count; ++i) {
		drawVariant(p, i);
	}
}

void EmojiColorPicker::enterEventHook(QEvent *e) {
	_hideTimer.stop();
	if (_hiding) showAnimated();
	TWidget::enterEventHook(e);
}

void EmojiColorPicker::leaveEventHook(QEvent *e) {
	TWidget::leaveEventHook(e);
}

void EmojiColorPicker::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_lastMousePos = e->globalPos();
	updateSelected();
	_pressedSel = _selected;
}

void EmojiColorPicker::mouseReleaseEvent(QMouseEvent *e) {
	handleMouseRelease(e->globalPos());
}

void EmojiColorPicker::handleMouseRelease(QPoint globalPos) {
	_lastMousePos = globalPos;
	int32 pressed = _pressedSel;
	_pressedSel = -1;

	updateSelected();
	if (_selected >= 0 && (pressed < 0 || _selected == pressed)) {
		emit emojiSelected(_variants[_selected]);
	}
	_ignoreShow = true;
	hideAnimated();
}

void EmojiColorPicker::handleMouseMove(QPoint globalPos) {
	_lastMousePos = globalPos;
	updateSelected();
}

void EmojiColorPicker::mouseMoveEvent(QMouseEvent *e) {
	handleMouseMove(e->globalPos());
}

void EmojiColorPicker::animationCallback() {
	update();
	if (!_a_opacity.animating()) {
		_cache = QPixmap();
		if (_hiding) {
			hide();
			emit hidden();
		} else {
			_lastMousePos = QCursor::pos();
			updateSelected();
		}
	}
}

void EmojiColorPicker::hideFast() {
	clearSelection();
	_a_opacity.finish();
	_cache = QPixmap();
	hide();
	emit hidden();
}

void EmojiColorPicker::hideAnimated() {
	if (_cache.isNull()) {
		_cache = myGrab(this);
		clearSelection();
	}
	_hiding = true;
	_a_opacity.start([this] { animationCallback(); }, 1., 0., st::emojiPanDuration);
}

void EmojiColorPicker::showAnimated() {
	if (_ignoreShow) return;

	if (!isHidden() && !_hiding) {
		return;
	}
	_hiding = false;
	if (_cache.isNull()) {
		_cache = myGrab(this);
		clearSelection();
	}
	show();
	_a_opacity.start([this] { animationCallback(); }, 0., 1., st::emojiPanDuration);
}

void EmojiColorPicker::clearSelection() {
	_pressedSel = -1;
	setSelected(-1);
	_lastMousePos = mapToGlobal(QPoint(-10, -10));
}

void EmojiColorPicker::updateSelected() {
	auto newSelected = -1;
	auto p = mapFromGlobal(_lastMousePos);
	auto sx = rtl() ? (width() - p.x()) : p.x(), y = p.y() - st::emojiPanMargins.top() - st::emojiColorsPadding;
	if (y >= 0 && y < st::emojiPanSize.height()) {
		auto x = sx - st::emojiPanMargins.left() - st::emojiColorsPadding;
		if (x >= 0 && x < st::emojiPanSize.width()) {
			newSelected = 0;
		} else {
			x -= st::emojiPanSize.width() + 2 * st::emojiColorsPadding + st::emojiColorsSep;
			if (x >= 0 && x < st::emojiPanSize.width() * (_variants.size() - 1)) {
				newSelected = (x / st::emojiPanSize.width()) + 1;
			}
		}
	}

	setSelected(newSelected);
}

void EmojiColorPicker::setSelected(int newSelected) {
	if (_selected == newSelected) {
		return;
	}
	auto updateSelectedRect = [this] {
		if (_selected < 0) return;
		rtlupdate(st::emojiPanMargins.left() + st::emojiColorsPadding + _selected * st::emojiPanSize.width() + (_selected ? 2 * st::emojiColorsPadding + st::emojiColorsSep : 0), st::emojiPanMargins.top() + st::emojiColorsPadding, st::emojiPanSize.width(), st::emojiPanSize.height());
	};
	updateSelectedRect();
	_selected = newSelected;
	updateSelectedRect();
	setCursor((_selected >= 0) ? style::cur_pointer : style::cur_default);
}

void EmojiColorPicker::drawVariant(Painter &p, int variant) {
	QPoint w(st::emojiPanMargins.left() + st::emojiColorsPadding + variant * st::emojiPanSize.width() + (variant ? 2 * st::emojiColorsPadding + st::emojiColorsSep : 0), st::emojiPanMargins.top() + st::emojiColorsPadding);
	if (variant == _selected) {
		QPoint tl(w);
		if (rtl()) tl.setX(width() - tl.x() - st::emojiPanSize.width());
		App::roundRect(p, QRect(tl, st::emojiPanSize), st::emojiPanHover, StickerHoverCorners);
	}
	auto esize = Ui::Emoji::Size(Ui::Emoji::Index() + 1);
	p.drawPixmapLeft(w.x() + (st::emojiPanSize.width() - (esize / cIntRetinaFactor())) / 2, w.y() + (st::emojiPanSize.height() - (esize / cIntRetinaFactor())) / 2, width(), App::emojiLarge(), QRect(_variants[variant]->x() * esize, _variants[variant]->y() * esize, esize, esize));
}

EmojiPanInner::EmojiPanInner(QWidget *parent) : TWidget(parent)
, _maxHeight(st::emojiPanMaxHeight - st::emojiCategory.height)
, _picker(this) {
	resize(st::emojiPanWidth - st::emojiScroll.width - st::buttonRadius, countHeight());

	setMouseTracking(true);
	setAttribute(Qt::WA_OpaquePaintEvent);

	_picker->hide();

	_esize = Ui::Emoji::Size(Ui::Emoji::Index() + 1);

	for (auto i = 0; i != emojiTabCount; ++i) {
		_counts[i] = Ui::Emoji::GetPackCount(emojiTabAtIndex(i));
	}

	_showPickerTimer.setSingleShot(true);
	connect(&_showPickerTimer, SIGNAL(timeout()), this, SLOT(onShowPicker()));
	connect(_picker, SIGNAL(emojiSelected(EmojiPtr)), this, SLOT(onColorSelected(EmojiPtr)));
	connect(_picker, SIGNAL(hidden()), this, SLOT(onPickerHidden()));
}

void EmojiPanInner::setMaxHeight(int maxHeight) {
	_maxHeight = maxHeight;
	resize(st::emojiPanWidth - st::emojiScroll.width, countHeight());
}

void EmojiPanInner::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
}

int EmojiPanInner::countHeight() {
	auto result = 0;
	for (auto i = 0; i != emojiTabCount; ++i) {
		auto cnt = Ui::Emoji::GetPackCount(emojiTabAtIndex(i));
		auto rows = (cnt / EmojiPanPerRow) + ((cnt % EmojiPanPerRow) ? 1 : 0);
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
	p.fillRect(r, st::emojiPanBg);

	int32 fromcol = floorclamp(r.x() - st::emojiPanPadding, st::emojiPanSize.width(), 0, EmojiPanPerRow);
	int32 tocol = ceilclamp(r.x() + r.width() - st::emojiPanPadding, st::emojiPanSize.width(), 0, EmojiPanPerRow);
	if (rtl()) {
		qSwap(fromcol, tocol);
		fromcol = EmojiPanPerRow - fromcol;
		tocol = EmojiPanPerRow - tocol;
	}

	int32 y, tilly = 0;
	for (int c = 0; c < emojiTabCount; ++c) {
		y = tilly;
		int32 size = _counts[c];
		int32 rows = (size / EmojiPanPerRow) + ((size % EmojiPanPerRow) ? 1 : 0);
		tilly = y + st::emojiPanHeader + (rows * st::emojiPanSize.height());
		if (r.top() >= tilly) continue;

		y += st::emojiPanHeader;
		if (_emojis[c].isEmpty()) {
			_emojis[c] = Ui::Emoji::GetPack(emojiTabAtIndex(c));
			if (emojiTabAtIndex(c) != dbietRecent) {
				for (auto &emoji : _emojis[c]) {
					if (emoji->hasVariants()) {
						auto j = cEmojiVariants().constFind(emoji->nonColoredId());
						if (j != cEmojiVariants().cend()) {
							emoji = emoji->variant(j.value());
						}
					}
				}
			}
		}

		int32 fromrow = floorclamp(r.y() - y, st::emojiPanSize.height(), 0, rows);
		int32 torow = ceilclamp(r.y() + r.height() - y, st::emojiPanSize.height(), 0, rows);
		for (int32 i = fromrow; i < torow; ++i) {
			for (int32 j = fromcol; j < tocol; ++j) {
				int32 index = i * EmojiPanPerRow + j;
				if (index >= size) break;

				auto selected = (!_picker->isHidden() && c * MatrixRowShift + index == _pickerSel) || (c * MatrixRowShift + index == _selected);

				QPoint w(st::emojiPanPadding + j * st::emojiPanSize.width(), y + i * st::emojiPanSize.height());
				if (selected) {
					QPoint tl(w);
					if (rtl()) tl.setX(width() - tl.x() - st::emojiPanSize.width());
					App::roundRect(p, QRect(tl, st::emojiPanSize), st::emojiPanHover, StickerHoverCorners);
				}
				p.drawPixmapLeft(w.x() + (st::emojiPanSize.width() - (_esize / cIntRetinaFactor())) / 2, w.y() + (st::emojiPanSize.height() - (_esize / cIntRetinaFactor())) / 2, width(), App::emojiLarge(), QRect(_emojis[c][index]->x() * _esize, _emojis[c][index]->y() * _esize, _esize, _esize));
			}
		}
	}
}

bool EmojiPanInner::checkPickerHide() {
	if (!_picker->isHidden() && _pickerSel >= 0) {
		_picker->hideAnimated();
		_pickerSel = -1;
		updateSelected();
		return true;
	}
	return false;
}

void EmojiPanInner::mousePressEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
	if (checkPickerHide() || e->button() != Qt::LeftButton) {
		return;
	}
	_pressedSel = _selected;

	if (_selected >= 0) {
		int tab = (_selected / MatrixRowShift), sel = _selected % MatrixRowShift;
		if (tab < emojiTabCount && sel < _emojis[tab].size() && _emojis[tab][sel]->hasVariants()) {
			_pickerSel = _selected;
			setCursor(style::cur_default);
			if (!cEmojiVariants().contains(_emojis[tab][sel]->nonColoredId())) {
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
	if (!_picker->isHidden()) {
		if (_picker->rect().contains(_picker->mapFromGlobal(_lastMousePos))) {
			return _picker->handleMouseRelease(QCursor::pos());
		} else if (_pickerSel >= 0) {
			int tab = (_pickerSel / MatrixRowShift), sel = _pickerSel % MatrixRowShift;
			if (tab < emojiTabCount && sel < _emojis[tab].size() && _emojis[tab][sel]->hasVariants()) {
				if (cEmojiVariants().contains(_emojis[tab][sel]->nonColoredId())) {
					_picker->hideAnimated();
					_pickerSel = -1;
				}
			}
		}
	}
	updateSelected();

	if (_showPickerTimer.isActive()) {
		_showPickerTimer.stop();
		_pickerSel = -1;
		_picker->hide();
	}

	if (_selected < 0 || _selected != pressed) return;

	if (_selected >= emojiTabCount * MatrixRowShift) {
		return;
	}

	int tab = (_selected / MatrixRowShift), sel = _selected % MatrixRowShift;
	if (sel < _emojis[tab].size()) {
		EmojiPtr emoji(_emojis[tab][sel]);
		if (emoji->hasVariants() && !_picker->isHidden()) return;

		selectEmoji(emoji);
	}
}

void EmojiPanInner::selectEmoji(EmojiPtr emoji) {
	auto &recent = cGetRecentEmoji();
	auto i = recent.begin(), e = recent.end();
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
	emit saveConfigDelayed(kSaveRecentEmojiTimeout);

	emit selected(emoji);
}

void EmojiPanInner::onShowPicker() {
	if (_pickerSel < 0) return;

	int tab = (_pickerSel / MatrixRowShift), sel = _pickerSel % MatrixRowShift;
	if (tab < emojiTabCount && sel < _emojis[tab].size() && _emojis[tab][sel]->hasVariants()) {
		_picker->showEmoji(_emojis[tab][sel]);

		int32 y = 0;
		for (int c = 0; c <= tab; ++c) {
			int32 size = (c == tab) ? (sel - (sel % EmojiPanPerRow)) : _counts[c], rows = (size / EmojiPanPerRow) + ((size % EmojiPanPerRow) ? 1 : 0);
			y += st::emojiPanHeader + (rows * st::emojiPanSize.height());
		}
		y -= _picker->height() - st::buttonRadius + _visibleTop;
		if (y < st::emojiPanHeader) {
			y += _picker->height() - st::buttonRadius + st::emojiPanSize.height() - st::buttonRadius;
		}
		int xmax = width() - _picker->width();
		float64 coef = float64(sel % EmojiPanPerRow) / float64(EmojiPanPerRow - 1);
		if (rtl()) coef = 1. - coef;
		_picker->move(qRound(xmax * coef), y);

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

QRect EmojiPanInner::emojiRect(int tab, int sel) {
	int x = 0, y = 0;
	for (int i = 0; i < emojiTabCount; ++i) {
		if (i == tab) {
			int rows = (sel / EmojiPanPerRow);
			y += st::emojiPanHeader + rows * st::emojiPanSize.height();
			x = st::emojiPanPadding + ((sel % EmojiPanPerRow) * st::emojiPanSize.width());
			break;
		} else {
			int cnt = _counts[i];
			int rows = (cnt / EmojiPanPerRow) + ((cnt % EmojiPanPerRow) ? 1 : 0);
			y += st::emojiPanHeader + rows * st::emojiPanSize.height();
		}
	}
	return QRect(x, y, st::emojiPanSize.width(), st::emojiPanSize.height());
}

void EmojiPanInner::onColorSelected(EmojiPtr emoji) {
	if (emoji->hasVariants()) {
		cRefEmojiVariants().insert(emoji->nonColoredId(), emoji->variantIndex(emoji));
	}
	if (_pickerSel >= 0) {
		int tab = (_pickerSel / MatrixRowShift), sel = _pickerSel % MatrixRowShift;
		if (tab >= 0 && tab < emojiTabCount) {
			_emojis[tab][sel] = emoji;
			rtlupdate(emojiRect(tab, sel));
		}
	}
	selectEmoji(emoji);
	_picker->hideAnimated();
}

void EmojiPanInner::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	if (!_picker->isHidden()) {
		if (_picker->rect().contains(_picker->mapFromGlobal(_lastMousePos))) {
			return _picker->handleMouseMove(QCursor::pos());
		} else {
			_picker->clearSelection();
		}
	}
	updateSelected();
}

void EmojiPanInner::leaveEventHook(QEvent *e) {
	clearSelection();
}

void EmojiPanInner::leaveToChildEvent(QEvent *e, QWidget *child) {
	clearSelection();
}

void EmojiPanInner::enterFromChildEvent(QEvent *e, QWidget *child) {
	_lastMousePos = QCursor::pos();
	updateSelected();
}

void EmojiPanInner::clearSelection() {
	_lastMousePos = mapToGlobal(QPoint(-10, -10));
	_pressedSel = -1;
	setSelected(-1);
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
	if (!_picker->isHidden()) {
		_picker->hideFast();
		_pickerSel = -1;
		clearSelection();
	}
}

void EmojiPanInner::refreshRecent() {
	clearSelection();
	_counts[0] = Ui::Emoji::GetPackCount(dbietRecent);
	_emojis[0] = Ui::Emoji::GetPack(dbietRecent);
	int32 h = countHeight();
	if (h != height()) {
		resize(width(), h);
		emit needRefreshPanels();
	}
}

void EmojiPanInner::fillPanels(QVector<EmojiPanel*> &panels) {
	if (_picker->parentWidget() != parentWidget()) {
		_picker->setParent(parentWidget());
	}
	for (int32 i = 0; i < panels.size(); ++i) {
		panels.at(i)->hide();
		panels.at(i)->deleteLater();
	}
	panels.clear();

	int y = 0;
	panels.reserve(emojiTabCount);
	for (int c = 0; c < emojiTabCount; ++c) {
		panels.push_back(new EmojiPanel(parentWidget(), lang(LangKey(lng_emoji_category0 + c)), Stickers::NoneSetId, true, y));
		connect(panels.back(), SIGNAL(mousePressed()), this, SLOT(checkPickerHide()));
		int cnt = _counts[c], rows = (cnt / EmojiPanPerRow) + ((cnt % EmojiPanPerRow) ? 1 : 0);
		panels.back()->show();
		y += st::emojiPanHeader + rows * st::emojiPanSize.height();
	}
	_picker->raise();
}

void EmojiPanInner::refreshPanels(QVector<EmojiPanel*> &panels) {
	if (panels.size() != emojiTabCount) return fillPanels(panels);

	int32 y = 0;
	for (int c = 0; c < emojiTabCount; ++c) {
		panels.at(c)->setWantedY(y);
		int cnt = _counts[c], rows = (cnt / EmojiPanPerRow) + ((cnt % EmojiPanPerRow) ? 1 : 0);
		y += st::emojiPanHeader + rows * st::emojiPanSize.height();
	}
}

void EmojiPanInner::updateSelected() {
	if (_pressedSel >= 0 || _pickerSel >= 0) return;

	auto newSelected = -1;
	auto p = mapFromGlobal(_lastMousePos);
	int y, ytill = 0, sx = (rtl() ? width() - p.x() : p.x()) - st::emojiPanPadding;
	for (int c = 0; c < emojiTabCount; ++c) {
		int cnt = _counts[c];
		y = ytill;
		ytill = y + st::emojiPanHeader + ((cnt / EmojiPanPerRow) + ((cnt % EmojiPanPerRow) ? 1 : 0)) * st::emojiPanSize.height();
		if (p.y() >= y && p.y() < ytill) {
			y += st::emojiPanHeader;
			if (p.y() >= y && sx >= 0 && sx < EmojiPanPerRow * st::emojiPanSize.width()) {
				newSelected = qFloor((p.y() - y) / st::emojiPanSize.height()) * EmojiPanPerRow + qFloor(sx / st::emojiPanSize.width());
				if (newSelected >= _emojis[c].size()) {
					newSelected = -1;
				} else {
					newSelected += c * MatrixRowShift;
				}
			}
			break;
		}
	}

	setSelected(newSelected);
}

void EmojiPanInner::setSelected(int newSelected) {
	if (_selected == newSelected) {
		return;
	}
	auto updateSelected = [this]() {
		if (_selected < 0) return;
		rtlupdate(emojiRect(_selected / MatrixRowShift, _selected % MatrixRowShift));
	};
	updateSelected();
	_selected = newSelected;
	updateSelected();

	setCursor((_selected >= 0) ? style::cur_pointer : style::cur_default);
	if (_selected >= 0 && !_picker->isHidden()) {
		if (_selected != _pickerSel) {
			_picker->hideAnimated();
		} else {
			_picker->showAnimated();
		}
	}
}

void EmojiPanInner::showEmojiPack(DBIEmojiTab packIndex) {
	clearSelection();

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

StickerPanInner::StickerPanInner(QWidget *parent) : TWidget(parent)
, _section(cShowingSavedGifs() ? Section::Gifs : Section::Stickers)
, _addText(lang(lng_stickers_featured_add).toUpper())
, _addWidth(st::stickersTrendingAdd.font->width(_addText))
, _settings(this, lang(lng_stickers_you_have)) {
	setMaxHeight(st::emojiPanMaxHeight - st::emojiCategory.height);

	setMouseTracking(true);
	setAttribute(Qt::WA_OpaquePaintEvent);

	connect(_settings, SIGNAL(clicked()), this, SLOT(onSettings()));

	_previewTimer.setSingleShot(true);
	connect(&_previewTimer, SIGNAL(timeout()), this, SLOT(onPreview()));

	_updateInlineItems.setSingleShot(true);
	connect(&_updateInlineItems, SIGNAL(timeout()), this, SLOT(onUpdateInlineItems()));

	subscribe(AuthSession::CurrentDownloaderTaskFinished(), [this] {
		update();
		readVisibleSets();
	});
}

void StickerPanInner::setMaxHeight(int maxHeight) {
	_maxHeight = maxHeight;
	resize(st::emojiPanWidth - st::emojiScroll.width - st::buttonRadius, countHeight());
	_settings->moveToLeft((st::emojiPanWidth - _settings->width()) / 2, height() / 3);
}

void StickerPanInner::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	_visibleBottom = visibleBottom;
	if (_visibleTop != visibleTop) {
		_visibleTop = visibleTop;
		_lastScrolled = getms();
	}
	if (_section == Section::Featured) {
		readVisibleSets();
	}
}

void StickerPanInner::readVisibleSets() {
	auto itemsVisibleTop = _visibleTop - st::emojiPanHeader;
	auto itemsVisibleBottom = _visibleBottom - st::emojiPanHeader;
	auto rowHeight = featuredRowHeight();
	int rowFrom = floorclamp(itemsVisibleTop, rowHeight, 0, _featuredSets.size());
	int rowTo = ceilclamp(itemsVisibleBottom, rowHeight, 0, _featuredSets.size());
	for (int i = rowFrom; i < rowTo; ++i) {
		auto &set = _featuredSets[i];
		if (!(set.flags & MTPDstickerSet_ClientFlag::f_unread)) {
			continue;
		}
		if (i * rowHeight < itemsVisibleTop || (i + 1) * rowHeight > itemsVisibleBottom) {
			continue;
		}
		int count = qMin(set.pack.size(), static_cast<int>(StickerPanPerRow));
		int loaded = 0;
		for (int j = 0; j < count; ++j) {
			if (set.pack[j]->thumb->loaded() || set.pack[j]->loaded()) {
				++loaded;
			}
		}
		if (loaded == count) {
			Stickers::markFeaturedAsRead(set.id);
		}
	}
}

int StickerPanInner::featuredRowHeight() const {
	return st::stickersTrendingHeader + st::stickerPanSize.height() + st::stickersTrendingSkip;
}

int StickerPanInner::countHeight(bool plain) {
	auto result = 0;
	auto minLastH = plain ? 0 : (_maxHeight - st::stickerPanPadding);
	if (showingInlineItems()) {
		result = st::emojiPanHeader;
		if (_switchPmButton) {
			result += _switchPmButton->height() + st::inlineResultsSkip;
		}
		for (int i = 0, l = _inlineRows.count(); i < l; ++i) {
			result += _inlineRows[i].height;
		}
	} else if (_section == Section::Featured) {
		result = st::emojiPanHeader + shownSets().size() * featuredRowHeight();
	} else {
		auto &sets = shownSets();
		for (auto i = 0; i != sets.size(); ++i) {
			auto cnt = sets[i].pack.size();
			auto rows = (cnt / StickerPanPerRow) + ((cnt % StickerPanPerRow) ? 1 : 0);
			auto h = st::emojiPanHeader + rows * st::stickerPanSize.height();
			if (i == sets.size() - 1 && h < minLastH) {
				h = minLastH;
			}
			result += h;
		}
	}
	return qMax(minLastH, result) + st::stickerPanPadding;
}

void StickerPanInner::installedLocally(uint64 setId) {
	_installedLocallySets.insert(setId);
}

void StickerPanInner::notInstalledLocally(uint64 setId) {
	_installedLocallySets.remove(setId);
}

void StickerPanInner::clearInstalledLocally() {
	if (!_installedLocallySets.empty()) {
		_installedLocallySets.clear();
		refreshStickers();
	}
}

StickerPanInner::~StickerPanInner() {
	clearInlineRows(true);
	deleteUnusedGifLayouts();
	deleteUnusedInlineLayouts();
}

int StickerPanInner::stickersLeft() const {
	return (st::stickerPanPadding - st::buttonRadius);
}

QRect StickerPanInner::stickerRect(int tab, int sel) {
	int x = 0, y = 0;
	if (_section == Section::Featured) {
		y += st::emojiPanHeader + (tab * featuredRowHeight()) + st::stickersTrendingHeader;
		x = stickersLeft() + (sel * st::stickerPanSize.width());
	} else {
		auto &sets = shownSets();
		for (int i = 0; i < sets.size(); ++i) {
			if (i == tab) {
				int rows = (((sel >= sets[i].pack.size()) ? (sel - sets[i].pack.size()) : sel) / StickerPanPerRow);
				y += st::emojiPanHeader + rows * st::stickerPanSize.height();
				x = stickersLeft() + ((sel % StickerPanPerRow) * st::stickerPanSize.width());
				break;
			} else {
				int cnt = sets[i].pack.size();
				int rows = (cnt / StickerPanPerRow) + ((cnt % StickerPanPerRow) ? 1 : 0);
				y += st::emojiPanHeader + rows * st::stickerPanSize.height();
			}
		}
	}
	return QRect(x, y, st::stickerPanSize.width(), st::stickerPanSize.height());
}

void StickerPanInner::paintEvent(QPaintEvent *e) {
	Painter p(this);
	QRect r = e ? e->rect() : rect();
	if (r != rect()) {
		p.setClipRect(r);
	}
	p.fillRect(r, st::emojiPanBg);

	if (showingInlineItems()) {
		paintInlineItems(p, r);
	} else {
		paintStickers(p, r);
	}
}

void StickerPanInner::paintInlineItems(Painter &p, const QRect &r) {
	if (_inlineRows.isEmpty() && !_switchPmButton) {
		p.setFont(st::normalFont);
		p.setPen(st::noContactsColor);
		p.drawText(QRect(0, 0, width(), (height() / 3) * 2 + st::normalFont->height), lang(lng_inline_bot_no_results), style::al_center);
		return;
	}
	auto gifPaused = Ui::isLayerShown() || Ui::isMediaViewShown() || _previewShown || !App::wnd()->isActive();
	InlineBots::Layout::PaintContext context(getms(), false, gifPaused, false);

	auto top = st::emojiPanHeader;
	if (_switchPmButton) {
		top += _switchPmButton->height() + st::inlineResultsSkip;
	}

	auto fromx = rtl() ? (width() - r.x() - r.width()) : r.x();
	auto tox = rtl() ? (width() - r.x()) : (r.x() + r.width());
	for (auto row = 0, rows = _inlineRows.size(); row != rows; ++row) {
		auto &inlineRow = _inlineRows[row];
		if (top >= r.top() + r.height()) break;
		if (top + inlineRow.height > r.top()) {
			auto left = st::inlineResultsLeft - st::buttonRadius;
			if (row == rows - 1) context.lastRow = true;
			for (int col = 0, cols = inlineRow.items.size(); col < cols; ++col) {
				if (left >= tox) break;

				auto item = inlineRow.items.at(col);
				auto w = item->width();
				if (left + w > fromx) {
					p.translate(left, top);
					item->paint(p, r.translated(-left, -top), &context);
					p.translate(-left, -top);
				}
				left += w;
				if (item->hasRightSkip()) {
					left += st::inlineResultsSkip;
				}
			}
		}
		top += inlineRow.height;
	}
}

void StickerPanInner::paintStickers(Painter &p, const QRect &r) {
	int32 fromcol = floorclamp(r.x() - stickersLeft(), st::stickerPanSize.width(), 0, StickerPanPerRow);
	int32 tocol = ceilclamp(r.x() + r.width() - stickersLeft(), st::stickerPanSize.width(), 0, StickerPanPerRow);
	if (rtl()) {
		qSwap(fromcol, tocol);
		fromcol = StickerPanPerRow - fromcol;
		tocol = StickerPanPerRow - tocol;
	}

	auto &sets = shownSets();
	auto seltab = (_selected >= 0) ? (_selected / MatrixRowShift) : -1;
	auto selindex = (seltab >= 0) ? (_selected % MatrixRowShift) : -1;
	auto seldelete = false;
	if (seltab >= sets.size()) {
		seltab = -1;
	} else if (seltab >= 0 && selindex >= sets[seltab].pack.size()) {
		selindex -= sets[seltab].pack.size();
		seldelete = true;
	}

	auto tilly = 0;
	auto ms = getms();
	if (_section == Section::Featured) {
		tilly += st::emojiPanHeader;
		for (int c = 0, l = sets.size(); c < l; ++c) {
			auto y = tilly;
			auto &set = sets[c];
			tilly = y + featuredRowHeight();
			if (r.top() >= tilly) continue;
			if (y >= r.y() + r.height()) break;

			int size = set.pack.size();

			int widthForTitle = featuredContentWidth() - (st::emojiPanHeaderLeft - st::buttonRadius);
			if (featuredHasAddButton(c)) {
				auto add = featuredAddRect(c);
				auto selected = (_selectedFeaturedSetAdd == c) || (_pressedFeaturedSetAdd == c);
				auto &textBg = selected ? st::stickersTrendingAdd.textBgOver : st::stickersTrendingAdd.textBg;

				App::roundRect(p, myrtlrect(add), textBg, ImageRoundRadius::Small);
				if (set.ripple) {
					set.ripple->paint(p, add.x(), add.y(), width(), ms);
					if (set.ripple->empty()) {
						set.ripple.reset();
					}
				}
				p.setFont(st::stickersTrendingAdd.font);
				p.setPen(selected ? st::stickersTrendingAdd.textFgOver : st::stickersTrendingAdd.textFg);
				p.drawTextLeft(add.x() - (st::stickersTrendingAdd.width / 2), add.y() + st::stickersTrendingAdd.textTop, width(), _addText, _addWidth);

				widthForTitle -= add.width() - (st::stickersTrendingAdd.width / 2);
			} else {
				auto add = featuredAddRect(c);
				int checkx = add.left() + (add.width() - st::stickersFeaturedInstalled.width()) / 2;
				int checky = add.top() + (add.height() - st::stickersFeaturedInstalled.height()) / 2;
				st::stickersFeaturedInstalled.paint(p, QPoint(checkx, checky), width());
			}
			if (set.flags & MTPDstickerSet_ClientFlag::f_unread) {
				widthForTitle -= st::stickersFeaturedUnreadSize + st::stickersFeaturedUnreadSkip;
			}

			auto titleText = set.title;
			auto titleWidth = st::stickersTrendingHeaderFont->width(titleText);
			if (titleWidth > widthForTitle) {
				titleText = st::stickersTrendingHeaderFont->elided(titleText, widthForTitle);
				titleWidth = st::stickersTrendingHeaderFont->width(titleText);
			}
			p.setFont(st::stickersTrendingHeaderFont);
			p.setPen(st::stickersTrendingHeaderFg);
			p.drawTextLeft(st::emojiPanHeaderLeft - st::buttonRadius, y + st::stickersTrendingHeaderTop, width(), titleText, titleWidth);

			if (set.flags & MTPDstickerSet_ClientFlag::f_unread) {
				p.setPen(Qt::NoPen);
				p.setBrush(st::stickersFeaturedUnreadBg);

				{
					PainterHighQualityEnabler hq(p);
					p.drawEllipse(rtlrect(st::emojiPanHeaderLeft - st::buttonRadius + titleWidth + st::stickersFeaturedUnreadSkip, y + st::stickersTrendingHeaderTop + st::stickersFeaturedUnreadTop, st::stickersFeaturedUnreadSize, st::stickersFeaturedUnreadSize, width()));
				}
			}

			p.setFont(st::stickersTrendingSubheaderFont);
			p.setPen(st::stickersTrendingSubheaderFg);
			p.drawTextLeft(st::emojiPanHeaderLeft - st::buttonRadius, y + st::stickersTrendingSubheaderTop, width(), lng_stickers_count(lt_count, size));

			y += st::stickersTrendingHeader;
			if (y >= r.y() + r.height()) break;

			for (int j = fromcol; j < tocol; ++j) {
				int index = j;
				if (index >= size) break;

				auto selected = (seltab == c && selindex == index);
				auto deleteSelected = selected && seldelete;
				paintSticker(p, set, y, index, selected, deleteSelected);
			}
		}
	} else {
		for (int c = 0, l = sets.size(); c < l; ++c) {
			auto y = tilly;
			auto &set = sets[c];
			auto size = set.pack.size();
			auto rows = (size / StickerPanPerRow) + ((size % StickerPanPerRow) ? 1 : 0);
			tilly = y + st::emojiPanHeader + (rows * st::stickerPanSize.height());
			if (r.y() >= tilly) continue;

			bool special = (set.flags & MTPDstickerSet::Flag::f_official);
			y += st::emojiPanHeader;
			if (y >= r.y() + r.height()) break;

			int fromrow = floorclamp(r.y() - y, st::stickerPanSize.height(), 0, rows);
			int torow = ceilclamp(r.y() + r.height() - y, st::stickerPanSize.height(), 0, rows);
			for (int i = fromrow; i < torow; ++i) {
				for (int j = fromcol; j < tocol; ++j) {
					int index = i * StickerPanPerRow + j;
					if (index >= size) break;

					auto selected = (seltab == c && selindex == index);
					auto deleteSelected = selected && seldelete;
					paintSticker(p, set, y, index, selected, deleteSelected);
				}
			}
		}
	}
}

void StickerPanInner::paintSticker(Painter &p, Set &set, int y, int index, bool selected, bool deleteSelected) {
	auto sticker = set.pack[index];
	if (!sticker->sticker()) return;

	int row = (index / StickerPanPerRow), col = (index % StickerPanPerRow);

	QPoint pos(stickersLeft() + col * st::stickerPanSize.width(), y + row * st::stickerPanSize.height());
	if (selected) {
		QPoint tl(pos);
		if (rtl()) tl.setX(width() - tl.x() - st::stickerPanSize.width());
		App::roundRect(p, QRect(tl, st::stickerPanSize), st::emojiPanHover, StickerHoverCorners);
	}

	bool goodThumb = !sticker->thumb->isNull() && ((sticker->thumb->width() >= 128) || (sticker->thumb->height() >= 128));
	if (goodThumb) {
		sticker->thumb->load();
	} else {
		sticker->checkSticker();
	}

	float64 coef = qMin((st::stickerPanSize.width() - st::buttonRadius * 2) / float64(sticker->dimensions.width()), (st::stickerPanSize.height() - st::buttonRadius * 2) / float64(sticker->dimensions.height()));
	if (coef > 1) coef = 1;
	int32 w = qRound(coef * sticker->dimensions.width()), h = qRound(coef * sticker->dimensions.height());
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	QPoint ppos = pos + QPoint((st::stickerPanSize.width() - w) / 2, (st::stickerPanSize.height() - h) / 2);
	if (goodThumb) {
		p.drawPixmapLeft(ppos, width(), sticker->thumb->pix(w, h));
	} else if (!sticker->sticker()->img->isNull()) {
		p.drawPixmapLeft(ppos, width(), sticker->sticker()->img->pix(w, h));
	}

	if (selected && set.id == Stickers::RecentSetId && _custom.at(index)) {
		QPoint xPos = pos + QPoint(st::stickerPanSize.width() - st::stickerPanDelete.width(), 0);
		if (!deleteSelected) p.setOpacity(st::stickerPanDeleteOpacity);
		st::stickerPanDelete.paint(p, xPos, width());
		if (!deleteSelected) p.setOpacity(1.);
	}
}

bool StickerPanInner::featuredHasAddButton(int index) const {
	if (index < 0 || index >= _featuredSets.size()) {
		return false;
	}
	auto flags = _featuredSets[index].flags;
	return !(flags & MTPDstickerSet::Flag::f_installed) || (flags & MTPDstickerSet::Flag::f_archived);
}

int StickerPanInner::featuredContentWidth() const {
	return stickersLeft() + (StickerPanPerRow * st::stickerPanSize.width());
}

QRect StickerPanInner::featuredAddRect(int index) const {
	int addw = _addWidth - st::stickersTrendingAdd.width;
	int addh = st::stickersTrendingAdd.height;
	int addx = featuredContentWidth() - addw;
	int addy = st::emojiPanHeader + index * featuredRowHeight() + st::stickersTrendingAddTop;
	return QRect(addx, addy, addw, addh);
}

void StickerPanInner::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_lastMousePos = e->globalPos();
	updateSelected();

	_pressed = _selected;
	_pressedFeaturedSet = _selectedFeaturedSet;
	setPressedFeaturedSetAdd(_selectedFeaturedSetAdd);
	ClickHandler::pressed();
	_previewTimer.start(QApplication::startDragTime());
}

void StickerPanInner::setPressedFeaturedSetAdd(int newPressedFeaturedSetAdd) {
	if (_pressedFeaturedSetAdd >= 0 && _pressedFeaturedSetAdd < _featuredSets.size()) {
		auto &set = _featuredSets[_pressedFeaturedSetAdd];
		if (set.ripple) {
			set.ripple->lastStop();
		}
	}
	_pressedFeaturedSetAdd = newPressedFeaturedSetAdd;
	if (_pressedFeaturedSetAdd >= 0 && _pressedFeaturedSetAdd < _featuredSets.size()) {
		auto &set = _featuredSets[_pressedFeaturedSetAdd];
		if (!set.ripple) {
			auto maskSize = QSize(_addWidth - st::stickersTrendingAdd.width, st::stickersTrendingAdd.height);
			auto mask = Ui::RippleAnimation::roundRectMask(maskSize, st::buttonRadius);
			set.ripple = MakeShared<Ui::RippleAnimation>(st::stickersTrendingAdd.ripple, std::move(mask), [this, index = _pressedFeaturedSetAdd] {
				update(myrtlrect(featuredAddRect(index)));
			});
		}
		auto rect = myrtlrect(featuredAddRect(_pressedFeaturedSetAdd));
		set.ripple->add(mapFromGlobal(QCursor::pos()) - rect.topLeft());
	}
}

void StickerPanInner::mouseReleaseEvent(QMouseEvent *e) {
	_previewTimer.stop();

	auto pressed = std::exchange(_pressed, -1);
	auto pressedFeaturedSet = std::exchange(_pressedFeaturedSet, -1);
	auto pressedFeaturedSetAdd = _pressedFeaturedSetAdd;
	setPressedFeaturedSetAdd(-1);
	if (pressedFeaturedSetAdd != _selectedFeaturedSetAdd) {
		update();
	}

	auto activated = ClickHandler::unpressed();

	if (_previewShown) {
		_previewShown = false;
		return;
	}

	_lastMousePos = e->globalPos();
	updateSelected();

	if (showingInlineItems()) {
		if (_selected < 0 || _selected != pressed || !activated) {
			return;
		}

		if (dynamic_cast<InlineBots::Layout::SendClickHandler*>(activated.data())) {
			int row = _selected / MatrixRowShift, column = _selected % MatrixRowShift;
			selectInlineResult(row, column);
		} else {
			App::activateClickHandler(activated, e->button());
		}
		return;
	}

	auto &sets = shownSets();
	if (_selected >= 0 && _selected < MatrixRowShift * sets.size() && _selected == pressed) {
		int tab = (_selected / MatrixRowShift), sel = _selected % MatrixRowShift;
		if (sets[tab].id == Stickers::RecentSetId && sel >= sets[tab].pack.size() && sel < sets[tab].pack.size() * 2 && _custom.at(sel - sets[tab].pack.size())) {
			removeRecentSticker(tab, sel - sets[tab].pack.size());
			return;
		}
		if (sel < sets[tab].pack.size()) {
			emit selected(sets[tab].pack[sel]);
		}
	} else if (_selectedFeaturedSet >= 0 && _selectedFeaturedSet < sets.size() && _selectedFeaturedSet == pressedFeaturedSet) {
		emit displaySet(sets[_selectedFeaturedSet].id);
	} else if (_selectedFeaturedSetAdd >= 0 && _selectedFeaturedSetAdd < sets.size() && _selectedFeaturedSetAdd == pressedFeaturedSetAdd) {
		emit installSet(sets[_selectedFeaturedSetAdd].id);
	}
}

void StickerPanInner::selectInlineResult(int row, int column) {
	if (row >= _inlineRows.size() || column >= _inlineRows.at(row).items.size()) {
		return;
	}

	auto item = _inlineRows[row].items[column];
	if (auto photo = item->getPhoto()) {
		if (photo->medium->loaded() || photo->thumb->loaded()) {
			emit selected(photo);
		} else if (!photo->medium->loading()) {
			photo->thumb->loadEvenCancelled();
			photo->medium->loadEvenCancelled();
		}
	} else if (auto document = item->getDocument()) {
		if (document->loaded()) {
			emit selected(document);
		} else if (document->loading()) {
			document->cancel();
		} else {
			DocumentOpenClickHandler::doOpen(document, nullptr, ActionOnLoadNone);
		}
	} else if (auto inlineResult = item->getResult()) {
		if (inlineResult->onChoose(item)) {
			emit selected(inlineResult, _inlineBot);
		}
	}
}

void StickerPanInner::removeRecentSticker(int tab, int index) {
	if (_section != Section::Stickers || tab >= _mySets.size() || _mySets[tab].id != Stickers::RecentSetId) {
		return;
	}

	clearSelection();
	bool refresh = false;
	auto sticker = _mySets[tab].pack[index];
	auto &recent = cGetRecentStickers();
	for (int32 i = 0, l = recent.size(); i < l; ++i) {
		if (recent.at(i).first == sticker) {
			recent.removeAt(i);
			Local::writeUserSettings();
			refresh = true;
			break;
		}
	}
	auto &sets = Global::RefStickerSets();
	auto it = sets.find(Stickers::CustomSetId);
	if (it != sets.cend()) {
		for (int i = 0, l = it->stickers.size(); i < l; ++i) {
			if (it->stickers.at(i) == sticker) {
				it->stickers.removeAt(i);
				if (it->stickers.isEmpty()) {
					sets.erase(it);
				}
				Local::writeInstalledStickers();
				refresh = true;
				break;
			}
		}
	}
	if (refresh) {
		refreshRecentStickers();
		updateSelected();
		update();
	}
}

void StickerPanInner::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
}

void StickerPanInner::leaveEventHook(QEvent *e) {
	clearSelection();
}

void StickerPanInner::leaveToChildEvent(QEvent *e, QWidget *child) {
	clearSelection();
}

void StickerPanInner::enterFromChildEvent(QEvent *e, QWidget *child) {
	_lastMousePos = QCursor::pos();
	updateSelected();
}

bool StickerPanInner::showSectionIcons() const {
	return !inlineResultsShown();
}

void StickerPanInner::clearSelection() {
	if (showingInlineItems()) {
		if (_selected >= 0) {
			int srow = _selected / MatrixRowShift, scol = _selected % MatrixRowShift;
			t_assert(srow >= 0 && srow < _inlineRows.size() && scol >= 0 && scol < _inlineRows.at(srow).items.size());
			ClickHandler::clearActive(_inlineRows.at(srow).items.at(scol));
			setCursor(style::cur_default);
		}
		_selected = _pressed = -1;
	} else {
		_pressed = -1;
		_pressedFeaturedSet = -1;
		setSelected(-1, -1, -1);
		setPressedFeaturedSetAdd(-1);
	}
	update();
}

void StickerPanInner::hideFinish(bool completely) {
	if (completely) {
		auto itemForget = [](auto &item) {
			if (auto document = item->getDocument()) {
				document->forget();
			}
			if (auto photo = item->getPhoto()) {
				photo->forget();
			}
			if (auto result = item->getResult()) {
				result->forget();
			}
		};
		clearInlineRows(false);
		for_const (auto &item, _gifLayouts) {
			itemForget(item.second);
		}
		for_const (auto &item, _inlineLayouts) {
			itemForget(item.second);
		}
		clearInstalledLocally();
	}
	if (_setGifCommand && _section == Section::Gifs) {
		App::insertBotCommand(qsl(""), true);
	}
	_setGifCommand = false;

	// Reset to the recent stickers section.
	if (_section == Section::Featured) {
		_section = Section::Stickers;
	}
}

void StickerPanInner::refreshStickers() {
	auto stickersShown = (_section == Section::Stickers || _section == Section::Featured);
	if (stickersShown) {
		clearSelection();
	}

	_mySets.clear();
	_mySets.reserve(Global::StickerSetsOrder().size() + 1);

	refreshRecentStickers(false);
	for_const (auto setId, Global::StickerSetsOrder()) {
		appendSet(_mySets, setId, AppendSkip::Archived);
	}

	_featuredSets.clear();
	_featuredSets.reserve(Global::FeaturedStickerSetsOrder().size());

	for_const (auto setId, Global::FeaturedStickerSetsOrder()) {
		appendSet(_featuredSets, setId, AppendSkip::Installed);
	}

	if (stickersShown) {
		int h = countHeight();
		if (h != height()) resize(width(), h);

		_settings->setVisible(_section == Section::Stickers && _mySets.isEmpty());
	} else {
		_settings->hide();
	}

	emit refreshIcons(kRefreshIconsNoAnimation);

	if (stickersShown) updateSelected();
}

bool StickerPanInner::inlineRowsAddItem(DocumentData *savedGif, InlineResult *result, InlineRow &row, int32 &sumWidth) {
	InlineItem *layout = nullptr;
	if (savedGif) {
		layout = layoutPrepareSavedGif(savedGif, (_inlineRows.size() * MatrixRowShift) + row.items.size());
	} else if (result) {
		layout = layoutPrepareInlineResult(result, (_inlineRows.size() * MatrixRowShift) + row.items.size());
	}
	if (!layout) return false;

	layout->preload();
	if (inlineRowFinalize(row, sumWidth, layout->isFullLine())) {
		layout->setPosition(_inlineRows.size() * MatrixRowShift);
	}

	sumWidth += layout->maxWidth();
	if (!row.items.isEmpty() && row.items.back()->hasRightSkip()) {
		sumWidth += st::inlineResultsSkip;
	}

	row.items.push_back(layout);
	return true;
}

bool StickerPanInner::inlineRowFinalize(InlineRow &row, int32 &sumWidth, bool force) {
	if (row.items.isEmpty()) return false;

	auto full = (row.items.size() >= kInlineItemsMaxPerRow);
	auto big = (sumWidth >= st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft);
	if (full || big || force) {
		_inlineRows.push_back(layoutInlineRow(row, (full || big) ? sumWidth : 0));
		row = InlineRow();
		row.items.reserve(kInlineItemsMaxPerRow);
		sumWidth = 0;
		return true;
	}
	return false;
}

void StickerPanInner::refreshSavedGifs() {
	if (_section == Section::Gifs) {
		_settings->hide();
		clearInlineRows(false);

		auto &saved = cSavedGifs();
		if (saved.isEmpty()) {
			showStickerSet(Stickers::RecentSetId);
			return;
		} else {
			_inlineRows.reserve(saved.size());
			auto row = InlineRow();
			row.items.reserve(kInlineItemsMaxPerRow);
			auto sumWidth = 0;
			for_const (auto &gif, saved) {
				inlineRowsAddItem(gif, 0, row, sumWidth);
			}
			inlineRowFinalize(row, sumWidth, true);
		}
		deleteUnusedGifLayouts();

		int32 h = countHeight();
		if (h != height()) resize(width(), h);

		update();
	}
	emit refreshIcons(kRefreshIconsNoAnimation);

	updateSelected();
}

void StickerPanInner::inlineBotChanged() {
	_setGifCommand = false;
	refreshInlineRows(nullptr, nullptr, true);
}

void StickerPanInner::clearInlineRows(bool resultsDeleted) {
	if (resultsDeleted) {
		if (showingInlineItems()) {
			_selected = _pressed = -1;
		}
	} else {
		if (showingInlineItems()) {
			clearSelection();
		}
		for_const (auto &row, _inlineRows) {
			for_const (auto &item, row.items) {
				item->setPosition(-1);
			}
		}
	}
	_inlineRows.clear();
}

InlineItem *StickerPanInner::layoutPrepareSavedGif(DocumentData *doc, int32 position) {
	auto it = _gifLayouts.find(doc);
	if (it == _gifLayouts.cend()) {
		if (auto layout = InlineItem::createLayoutGif(doc)) {
			it = _gifLayouts.emplace(doc, std::move(layout)).first;
			it->second->initDimensions();
		} else {
			return nullptr;
		}
	}
	if (!it->second->maxWidth()) return nullptr;

	it->second->setPosition(position);
	return it->second.get();
}

InlineItem *StickerPanInner::layoutPrepareInlineResult(InlineResult *result, int32 position) {
	auto it = _inlineLayouts.find(result);
	if (it == _inlineLayouts.cend()) {
		if (auto layout = InlineItem::createLayout(result, _inlineWithThumb)) {
			it = _inlineLayouts.emplace(result, std::move(layout)).first;
			it->second->initDimensions();
		} else {
			return nullptr;
		}
	}
	if (!it->second->maxWidth()) return nullptr;

	it->second->setPosition(position);
	return it->second.get();
}

void StickerPanInner::deleteUnusedGifLayouts() {
	if (_inlineRows.isEmpty() || _section != Section::Gifs) { // delete all
		_gifLayouts.clear();
	} else {
		for (auto i = _gifLayouts.begin(); i != _gifLayouts.cend();) {
			if (i->second->position() < 0) {
				i = _gifLayouts.erase(i);
			} else {
				++i;
			}
		}
	}
}

void StickerPanInner::deleteUnusedInlineLayouts() {
	if (_inlineRows.isEmpty() || _section == Section::Gifs) { // delete all
		_inlineLayouts.clear();
	} else {
		for (auto i = _inlineLayouts.begin(); i != _inlineLayouts.cend();) {
			if (i->second->position() < 0) {
				i = _inlineLayouts.erase(i);
			} else {
				++i;
			}
		}
	}
}

StickerPanInner::InlineRow &StickerPanInner::layoutInlineRow(InlineRow &row, int32 sumWidth) {
	auto count = int(row.items.size());
	t_assert(count <= kInlineItemsMaxPerRow);

	// enumerate items in the order of growing maxWidth()
	// for that sort item indices by maxWidth()
	int indices[kInlineItemsMaxPerRow];
	for (auto i = 0; i != count; ++i) {
		indices[i] = i;
	}
	std::sort(indices, indices + count, [&row](int a, int b) -> bool {
		return row.items.at(a)->maxWidth() < row.items.at(b)->maxWidth();
	});

	row.height = 0;
	int availw = width() - (st::inlineResultsLeft - st::buttonRadius);
	for (int i = 0; i < count; ++i) {
		int index = indices[i];
		int w = sumWidth ? (row.items.at(index)->maxWidth() * availw / sumWidth) : row.items.at(index)->maxWidth();
		int actualw = qMax(w, int(st::inlineResultsMinWidth));
		row.height = qMax(row.height, row.items.at(index)->resizeGetHeight(actualw));
		if (sumWidth) {
			availw -= actualw;
			sumWidth -= row.items.at(index)->maxWidth();
			if (index > 0 && row.items.at(index - 1)->hasRightSkip()) {
				availw -= st::inlineResultsSkip;
				sumWidth -= st::inlineResultsSkip;
			}
		}
	}
	return row;
}

void StickerPanInner::preloadImages() {
	if (showingInlineItems()) {
		for (int32 row = 0, rows = _inlineRows.size(); row < rows; ++row) {
			for (int32 col = 0, cols = _inlineRows.at(row).items.size(); col < cols; ++col) {
				_inlineRows.at(row).items.at(col)->preload();
			}
		}
		return;
	}

	auto &sets = shownSets();
	for (int i = 0, l = sets.size(), k = 0; i < l; ++i) {
		int count = sets[i].pack.size();
		if (_section == Section::Featured) {
			accumulate_min(count, static_cast<int>(StickerPanPerRow));
		}
		for (int j = 0; j != count; ++j) {
			if (++k > StickerPanPerRow * (StickerPanPerRow + 1)) break;

			auto sticker = sets.at(i).pack.at(j);
			if (!sticker || !sticker->sticker()) continue;

			bool goodThumb = !sticker->thumb->isNull() && ((sticker->thumb->width() >= 128) || (sticker->thumb->height() >= 128));
			if (goodThumb) {
				sticker->thumb->load();
			} else {
				sticker->automaticLoad(0);
			}
		}
		if (k > StickerPanPerRow * (StickerPanPerRow + 1)) break;
	}
}

uint64 StickerPanInner::currentSet(int yOffset) const {
	if (showingInlineItems()) {
		return Stickers::NoneSetId;
	} else if (_section == Section::Featured) {
		return Stickers::FeaturedSetId;
	}

	int y, ytill = 0;
	for (int i = 0, l = _mySets.size(); i < l; ++i) {
		int cnt = _mySets[i].pack.size();
		y = ytill;
		ytill = y + st::emojiPanHeader + ((cnt / StickerPanPerRow) + ((cnt % StickerPanPerRow) ? 1 : 0)) * st::stickerPanSize.height();
		if (yOffset < ytill) {
			return _mySets[i].id;
		}
	}
	return _mySets.isEmpty() ? Stickers::RecentSetId : _mySets.back().id;
}

void StickerPanInner::hideInlineRowsPanel() {
	clearInlineRows(false);
	if (showingInlineItems()) {
		_section = cShowingSavedGifs() ? Section::Gifs : Section::Inlines;
		if (_section == Section::Gifs) {
			refreshSavedGifs();
			emit scrollToY(0);
			emit scrollUpdated();
		} else {
			showStickerSet(Stickers::RecentSetId);
		}
	}
}

void StickerPanInner::clearInlineRowsPanel() {
	clearInlineRows(false);
}

void StickerPanInner::refreshSwitchPmButton(const InlineCacheEntry *entry) {
	if (!entry || entry->switchPmText.isEmpty()) {
		_switchPmButton.destroy();
		_switchPmStartToken.clear();
	} else {
		if (!_switchPmButton) {
			_switchPmButton.create(this, QString(), st::switchPmButton);
			_switchPmButton->show();
			_switchPmButton->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
			connect(_switchPmButton, SIGNAL(clicked()), this, SLOT(onSwitchPm()));
		}
		_switchPmButton->setText(entry->switchPmText); // doesn't perform text.toUpper()
		_switchPmStartToken = entry->switchPmStartToken;
		auto buttonTop = entry->results.empty() ? (2 * st::emojiPanHeader) : st::emojiPanHeader;
		_switchPmButton->move(st::inlineResultsLeft - st::buttonRadius, buttonTop);
	}
	update();
}

int StickerPanInner::refreshInlineRows(UserData *bot, const InlineCacheEntry *entry, bool resultsDeleted) {
	_inlineBot = bot;
	refreshSwitchPmButton(entry);
	auto clearResults = [this, entry]() {
		if (!entry) {
			return true;
		}
		if (entry->results.empty() && entry->switchPmText.isEmpty()) {
			if (!_inlineBot || _inlineBot->username != cInlineGifBotUsername()) {
				return true;
			}
		}
		return false;
	};
	auto clearResultsResult = clearResults(); // Clang segfault workaround.
	if (clearResultsResult) {
		if (resultsDeleted) {
			clearInlineRows(true);
			deleteUnusedInlineLayouts();
		}
		emit emptyInlineRows();
		return 0;
	}

	clearSelection();

	t_assert(_inlineBot != 0);
	_inlineBotTitle = lng_inline_bot_results(lt_inline_bot, _inlineBot->username.isEmpty() ? _inlineBot->name : ('@' + _inlineBot->username));

	_section = Section::Inlines;
	_settings->hide();

	auto count = int(entry->results.size());
	auto from = validateExistingInlineRows(entry->results);
	auto added = 0;

	if (count) {
		_inlineRows.reserve(count);
		auto row = InlineRow();
		row.items.reserve(kInlineItemsMaxPerRow);
		auto sumWidth = 0;
		for (auto i = from; i != count; ++i) {
			if (inlineRowsAddItem(0, entry->results[i].get(), row, sumWidth)) {
				++added;
			}
		}
		inlineRowFinalize(row, sumWidth, true);
	}

	int32 h = countHeight();
	if (h != height()) resize(width(), h);
	update();

	emit refreshIcons(kRefreshIconsNoAnimation);

	_lastMousePos = QCursor::pos();
	updateSelected();

	return added;
}

int StickerPanInner::validateExistingInlineRows(const InlineResults &results) {
	int count = results.size(), until = 0, untilrow = 0, untilcol = 0;
	for (; until < count;) {
		if (untilrow >= _inlineRows.size() || _inlineRows[untilrow].items[untilcol]->getResult() != results[until].get()) {
			break;
		}
		++until;
		if (++untilcol == _inlineRows[untilrow].items.size()) {
			++untilrow;
			untilcol = 0;
		}
	}
	if (until == count) { // all items are layed out
		if (untilrow == _inlineRows.size()) { // nothing changed
			return until;
		}

		for (int i = untilrow, l = _inlineRows.size(), skip = untilcol; i < l; ++i) {
			for (int j = 0, s = _inlineRows[i].items.size(); j < s; ++j) {
				if (skip) {
					--skip;
				} else {
					_inlineRows[i].items[j]->setPosition(-1);
				}
			}
		}
		if (!untilcol) { // all good rows are filled
			_inlineRows.resize(untilrow);
			return until;
		}
		_inlineRows.resize(untilrow + 1);
		_inlineRows[untilrow].items.resize(untilcol);
		_inlineRows[untilrow] = layoutInlineRow(_inlineRows[untilrow]);
		return until;
	}
	if (untilrow && !untilcol) { // remove last row, maybe it is not full
		--untilrow;
		untilcol = _inlineRows[untilrow].items.size();
	}
	until -= untilcol;

	for (int i = untilrow, l = _inlineRows.size(); i < l; ++i) {
		for (int j = 0, s = _inlineRows[i].items.size(); j < s; ++j) {
			_inlineRows[i].items[j]->setPosition(-1);
		}
	}
	_inlineRows.resize(untilrow);

	if (_inlineRows.isEmpty()) {
		_inlineWithThumb = false;
		for (int i = until; i < count; ++i) {
			if (results.at(i)->hasThumbDisplay()) {
				_inlineWithThumb = true;
				break;
			}
		}
	}
	return until;
}

void StickerPanInner::notify_inlineItemLayoutChanged(const InlineItem *layout) {
	if (_selected < 0 || !showingInlineItems()) {
		return;
	}

	int row = _selected / MatrixRowShift, col = _selected % MatrixRowShift;
	if (row < _inlineRows.size() && col < _inlineRows.at(row).items.size()) {
		if (layout == _inlineRows.at(row).items.at(col)) {
			updateSelected();
		}
	}
}

void StickerPanInner::ui_repaintInlineItem(const InlineItem *layout) {
	auto ms = getms();
	if (_lastScrolled + 100 <= ms) {
		update();
	} else {
		_updateInlineItems.start(_lastScrolled + 100 - ms);
	}
}

bool StickerPanInner::ui_isInlineItemVisible(const InlineItem *layout) {
	int32 position = layout->position();
	if (!showingInlineItems() || position < 0) {
		return false;
	}

	int row = position / MatrixRowShift, col = position % MatrixRowShift;
	t_assert((row < _inlineRows.size()) && (col < _inlineRows[row].items.size()));

	auto &inlineItems = _inlineRows[row].items;
	int top = st::emojiPanHeader;
	for (int32 i = 0; i < row; ++i) {
		top += _inlineRows.at(i).height;
	}

	return (top < _visibleTop + _maxHeight) && (top + _inlineRows[row].items[col]->height() > _visibleTop);
}

bool StickerPanInner::ui_isInlineItemBeingChosen() {
	return showingInlineItems();
}

void StickerPanInner::appendSet(Sets &to, uint64 setId, AppendSkip skip) {
	auto &sets = Global::StickerSets();
	auto it = sets.constFind(setId);
	if (it == sets.cend() || it->stickers.isEmpty()) return;
	if ((skip == AppendSkip::Archived) && (it->flags & MTPDstickerSet::Flag::f_archived)) return;
	if ((skip == AppendSkip::Installed) && (it->flags & MTPDstickerSet::Flag::f_installed) && !(it->flags & MTPDstickerSet::Flag::f_archived)) {
		if (!_installedLocallySets.contains(setId)) {
			return;
		}
	}

	to.push_back(Set(it->id, it->flags, it->title, it->stickers.size() + 1, it->stickers));
}

void StickerPanInner::refreshRecent() {
	if (_section == Section::Gifs) {
		refreshSavedGifs();
	} else if (_section == Section::Stickers) {
		refreshRecentStickers();
	}
}

void StickerPanInner::refreshRecentStickers(bool performResize) {
	_custom.clear();
	clearSelection();
	auto &sets = Global::StickerSets();
	auto &recent = cGetRecentStickers();
	auto customIt = sets.constFind(Stickers::CustomSetId);
	auto cloudIt = sets.constFind(Stickers::CloudRecentSetId);
	if (recent.isEmpty()
		&& (customIt == sets.cend() || customIt->stickers.isEmpty())
		&& (cloudIt == sets.cend() || cloudIt->stickers.isEmpty())) {
		if (!_mySets.isEmpty() && _mySets.at(0).id == Stickers::RecentSetId) {
			_mySets.pop_front();
		}
	} else {
		StickerPack recentPack;
		int customCnt = (customIt == sets.cend()) ? 0 : customIt->stickers.size();
		int cloudCnt = (cloudIt == sets.cend()) ? 0 : cloudIt->stickers.size();
		recentPack.reserve(cloudCnt + recent.size() + customCnt);
		_custom.reserve(cloudCnt + recent.size() + customCnt);
		if (cloudCnt > 0) {
			for_const (auto sticker, cloudIt->stickers) {
				recentPack.push_back(sticker);
				_custom.push_back(false);
			}
		}
		for_const (auto &recentSticker, recent) {
			auto sticker = recentSticker.first;
			recentPack.push_back(sticker);
			_custom.push_back(false);
		}
		if (customCnt > 0) {
			for_const (auto &sticker, customIt->stickers) {
				auto index = recentPack.indexOf(sticker);
				if (index >= cloudCnt) {
					_custom[index] = true; // mark stickers from recent as custom
				} else {
					recentPack.push_back(sticker);
					_custom.push_back(true);
				}
			}
		}
		if (_mySets.isEmpty() || _mySets.at(0).id != Stickers::RecentSetId) {
			_mySets.push_back(Set(Stickers::RecentSetId, MTPDstickerSet::Flag::f_official | MTPDstickerSet_ClientFlag::f_special, lang(lng_recent_stickers), recentPack.size() * 2, recentPack));
		} else {
			_mySets[0].pack = recentPack;
		}
	}

	if (performResize && (_section == Section::Stickers || _section == Section::Featured)) {
		int32 h = countHeight();
		if (h != height()) {
			resize(width(), h);
			emit needRefreshPanels();
		}

		updateSelected();
	}
}

void StickerPanInner::fillIcons(QList<StickerIcon> &icons) {
	icons.clear();
	icons.reserve(_mySets.size() + 1);
	if (!cSavedGifs().isEmpty()) {
		icons.push_back(StickerIcon(Stickers::NoneSetId));
	}
	if (Global::FeaturedStickerSetsUnreadCount() && !_featuredSets.isEmpty()) {
		icons.push_back(StickerIcon(Stickers::FeaturedSetId));
	}

	if (!_mySets.isEmpty()) {
		int i = 0;
		if (_mySets[0].id == Stickers::RecentSetId) {
			++i;
			icons.push_back(StickerIcon(Stickers::RecentSetId));
		}
		for (int l = _mySets.size(); i < l; ++i) {
			auto s = _mySets[i].pack[0];
			int32 availw = st::emojiCategory.width - 2 * st::stickerIconPadding, availh = st::emojiCategory.height - 2 * st::stickerIconPadding;
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
			icons.push_back(StickerIcon(_mySets[i].id, s, pixw, pixh));
		}
	}

	if (!Global::FeaturedStickerSetsUnreadCount() && !_featuredSets.isEmpty()) {
		icons.push_back(StickerIcon(Stickers::FeaturedSetId));
	}
}

void StickerPanInner::fillPanels(QVector<EmojiPanel*> &panels) {
	for (int32 i = 0; i < panels.size(); ++i) {
		panels.at(i)->hide();
		panels.at(i)->deleteLater();
	}
	panels.clear();

	if (_section != Section::Stickers) {
		auto title = [this]() -> QString {
			if (_section == Section::Gifs) {
				return lang(lng_saved_gifs);
			} else if (_section == Section::Inlines) {
				return _inlineBotTitle;
			}
			return lang(lng_stickers_featured);
		};
		panels.push_back(new EmojiPanel(parentWidget(), title(), Stickers::NoneSetId, true, 0));
		panels.back()->show();
		return;
	}

	if (_mySets.isEmpty()) return;

	int y = 0;
	panels.reserve(_mySets.size());
	for (int32 i = 0, l = _mySets.size(); i < l; ++i) {
		bool special = (_mySets[i].flags & MTPDstickerSet::Flag::f_official);
		panels.push_back(new EmojiPanel(parentWidget(), _mySets[i].title, _mySets[i].id, special, y));
		panels.back()->show();
		connect(panels.back(), SIGNAL(deleteClicked(quint64)), this, SIGNAL(removeSet(quint64)));
		int cnt = _mySets[i].pack.size(), rows = (cnt / StickerPanPerRow) + ((cnt % StickerPanPerRow) ? 1 : 0);
		int h = st::emojiPanHeader + rows * st::stickerPanSize.height();
		y += h;
	}
}

void StickerPanInner::refreshPanels(QVector<EmojiPanel*> &panels) {
	if (_section != Section::Stickers) return;

	if (panels.size() != _mySets.size()) {
		return fillPanels(panels);
	}

	int y = 0;
	for (int i = 0, l = _mySets.size(); i < l; ++i) {
		panels.at(i)->setWantedY(y);
		int cnt = _mySets[i].pack.size(), rows = (cnt / StickerPanPerRow) + ((cnt % StickerPanPerRow) ? 1 : 0);
		int h = st::emojiPanHeader + rows * st::stickerPanSize.height();
		y += h;
	}
}

void StickerPanInner::updateSelected() {
	if (_pressed >= 0 && !_previewShown) {
		return;
	}

	auto newSelected = -1;
	auto p = mapFromGlobal(_lastMousePos);

	if (showingInlineItems()) {
		int sx = (rtl() ? width() - p.x() : p.x()) - (st::inlineResultsLeft - st::buttonRadius);
		int sy = p.y() - st::emojiPanHeader;
		if (_switchPmButton) {
			sy -= _switchPmButton->height() + st::inlineResultsSkip;
		}
		int row = -1, col = -1, sel = -1;
		ClickHandlerPtr lnk;
		ClickHandlerHost *lnkhost = nullptr;
		HistoryCursorState cursor = HistoryDefaultCursorState;
		if (sy >= 0) {
			row = 0;
			for (int rows = _inlineRows.size(); row < rows; ++row) {
				if (sy < _inlineRows.at(row).height) {
					break;
				}
				sy -= _inlineRows.at(row).height;
			}
		}
		if (sx >= 0 && row >= 0 && row < _inlineRows.size()) {
			auto &inlineItems = _inlineRows[row].items;
			col = 0;
			for (int cols = inlineItems.size(); col < cols; ++col) {
				int width = inlineItems.at(col)->width();
				if (sx < width) {
					break;
				}
				sx -= width;
				if (inlineItems.at(col)->hasRightSkip()) {
					sx -= st::inlineResultsSkip;
				}
			}
			if (col < inlineItems.size()) {
				sel = row * MatrixRowShift + col;
				inlineItems.at(col)->getState(lnk, cursor, sx, sy);
				lnkhost = inlineItems.at(col);
			} else {
				row = col = -1;
			}
		} else {
			row = col = -1;
		}
		int srow = (_selected >= 0) ? (_selected / MatrixRowShift) : -1;
		int scol = (_selected >= 0) ? (_selected % MatrixRowShift) : -1;
		if (_selected != sel) {
			if (srow >= 0 && scol >= 0) {
				t_assert(srow >= 0 && srow < _inlineRows.size() && scol >= 0 && scol < _inlineRows.at(srow).items.size());
				Ui::repaintInlineItem(_inlineRows.at(srow).items.at(scol));
			}
			_selected = sel;
			if (row >= 0 && col >= 0) {
				t_assert(row >= 0 && row < _inlineRows.size() && col >= 0 && col < _inlineRows.at(row).items.size());
				Ui::repaintInlineItem(_inlineRows.at(row).items.at(col));
			}
			if (_previewShown && _selected >= 0 && _pressed != _selected) {
				_pressed = _selected;
				if (row >= 0 && col >= 0) {
					auto layout = _inlineRows.at(row).items.at(col);
					if (auto previewDocument = layout->getPreviewDocument()) {
						Ui::showMediaPreview(previewDocument);
					} else if (auto previewPhoto = layout->getPreviewPhoto()) {
						Ui::showMediaPreview(previewPhoto);
					}
				}
			}
		}
		if (ClickHandler::setActive(lnk, lnkhost)) {
			setCursor(lnk ? style::cur_pointer : style::cur_default);
		}
		return;
	}

	auto newSelectedFeaturedSet = -1;
	auto newSelectedFeaturedSetAdd = -1;
	auto featured = (_section == Section::Featured);
	auto &sets = shownSets();
	int y, ytill = 0, sx = (rtl() ? width() - p.x() : p.x()) - stickersLeft();
	if (featured) {
		ytill += st::emojiPanHeader;
	}
	for (int c = 0, l = sets.size(); c < l; ++c) {
		auto &set = sets[c];
		bool special = featured ? false : bool(set.flags & MTPDstickerSet::Flag::f_official);

		y = ytill;
		if (featured) {
			ytill = y + featuredRowHeight();
		} else {
			int cnt = set.pack.size();
			ytill = y + st::emojiPanHeader + ((cnt / StickerPanPerRow) + ((cnt % StickerPanPerRow) ? 1 : 0)) * st::stickerPanSize.height();
		}
		if (p.y() >= y && p.y() < ytill) {
			if (featured) {
				if (p.y() < y + st::stickersTrendingHeader) {
					if (featuredHasAddButton(c) && myrtlrect(featuredAddRect(c)).contains(p.x(), p.y())) {
						newSelectedFeaturedSetAdd = c;
					} else {
						newSelectedFeaturedSet = c;
					}
					break;
				}
				y += st::stickersTrendingHeader;
			} else {
				y += st::emojiPanHeader;
			}
			if (p.y() >= y && sx >= 0 && sx < StickerPanPerRow * st::stickerPanSize.width()) {
				auto rowIndex = qFloor((p.y() - y) / st::stickerPanSize.height());
				if (!featured || !rowIndex) {
					newSelected = rowIndex * StickerPanPerRow + qFloor(sx / st::stickerPanSize.width());
					if (newSelected >= set.pack.size()) {
						newSelected = -1;
					} else {
						if (set.id == Stickers::RecentSetId && _custom[newSelected]) {
							auto inx = sx - (newSelected % StickerPanPerRow) * st::stickerPanSize.width();
							auto iny = p.y() - y - ((newSelected / StickerPanPerRow) * st::stickerPanSize.height());
							if (inx >= st::stickerPanSize.width() - st::stickerPanDelete.width() && iny < st::stickerPanDelete.height()) {
								newSelected += set.pack.size();
							}
						}
						newSelected += c * MatrixRowShift;
					}
				}
			}
			break;
		}
	}

	setSelected(newSelected, newSelectedFeaturedSet, newSelectedFeaturedSetAdd);
}

void StickerPanInner::setSelected(int newSelected, int newSelectedFeaturedSet, int newSelectedFeaturedSetAdd) {
	if (_selected != newSelected || _selectedFeaturedSet != newSelectedFeaturedSet || _selectedFeaturedSetAdd != newSelectedFeaturedSetAdd) {
		setCursor((newSelected >= 0 || newSelectedFeaturedSet >= 0 || newSelectedFeaturedSetAdd >= 0) ? style::cur_pointer : style::cur_default);
	}
	if (_selected != newSelected) {
		auto &sets = shownSets();
		auto updateSelected = [this, &sets]() {
			if (_selected < 0) return;
			auto tab = _selected / MatrixRowShift, sel = _selected % MatrixRowShift;
			if (tab < sets.size() && sel >= sets[tab].pack.size()) {
				sel -= sets[tab].pack.size();
			}
			rtlupdate(stickerRect(tab, sel));
		};
		updateSelected();
		_selected = newSelected;
		updateSelected();

		if (_previewShown && _selected >= 0 && _pressed != _selected) {
			_pressed = _selected;
			auto tab = _selected / MatrixRowShift, sel = _selected % MatrixRowShift;
			if (tab < sets.size() && sel < sets[tab].pack.size()) {
				Ui::showMediaPreview(sets[tab].pack[sel]);
			}
		}
	}
	if (_selectedFeaturedSet != newSelectedFeaturedSet) {
		_selectedFeaturedSet = newSelectedFeaturedSet;
	}
	if (_selectedFeaturedSetAdd != newSelectedFeaturedSetAdd) {
		_selectedFeaturedSetAdd = newSelectedFeaturedSetAdd;
		update();
	}
}

void StickerPanInner::onSettings() {
	Ui::show(Box<StickersBox>(StickersBox::Section::Installed));
}

void StickerPanInner::onPreview() {
	if (_pressed < 0) return;
	if (showingInlineItems()) {
		int row = _pressed / MatrixRowShift, col = _pressed % MatrixRowShift;
		if (row < _inlineRows.size() && col < _inlineRows.at(row).items.size()) {
			auto layout = _inlineRows.at(row).items.at(col);
			if (auto previewDocument = layout->getPreviewDocument()) {
				Ui::showMediaPreview(previewDocument);
				_previewShown = true;
			} else if (auto previewPhoto = layout->getPreviewPhoto()) {
				Ui::showMediaPreview(previewPhoto);
				_previewShown = true;
			}
		}
	} else {
		auto &sets = shownSets();
		if (_pressed < MatrixRowShift * sets.size()) {
			int tab = (_pressed / MatrixRowShift), sel = _pressed % MatrixRowShift;
			if (sel < sets[tab].pack.size()) {
				Ui::showMediaPreview(sets[tab].pack[sel]);
				_previewShown = true;
			}
		}
	}
}

void StickerPanInner::onUpdateInlineItems() {
	if (!showingInlineItems()) return;

	auto ms = getms();
	if (_lastScrolled + 100 <= ms) {
		update();
	} else {
		_updateInlineItems.start(_lastScrolled + 100 - ms);
	}
}

void StickerPanInner::onSwitchPm() {
	if (_inlineBot && _inlineBot->botInfo) {
		_inlineBot->botInfo->startToken = _switchPmStartToken;
		Ui::showPeerHistory(_inlineBot, ShowAndStartBotMsgId);
	}
}

void StickerPanInner::showStickerSet(uint64 setId) {
	clearSelection();

	if (setId == Stickers::NoneSetId) {
		if (!showingInlineItems()) {
			_section = Section::Gifs;
			cSetShowingSavedGifs(true);
			emit saveConfigDelayed(kSaveRecentEmojiTimeout);
		}
		refreshSavedGifs();
		emit scrollToY(0);
		emit scrollUpdated();
		showFinish();
		return;
	}

	if (showingInlineItems()) {
		if (_setGifCommand && _section == Section::Gifs) {
			App::insertBotCommand(qsl(""), true);
		}
		_setGifCommand = false;

		cSetShowingSavedGifs(false);
		emit saveConfigDelayed(kSaveRecentEmojiTimeout);
		Notify::clipStopperHidden(ClipStopperSavedGifsPanel);
	}

	if (setId == Stickers::FeaturedSetId) {
		if (_section != Section::Featured) {
			_section = Section::Featured;

			refreshRecentStickers(true);
			emit refreshIcons(kRefreshIconsScrollAnimation);
			update();
		}

		emit scrollToY(0);
		emit scrollUpdated();
		return;
	}

	bool needRefresh = (_section != Section::Stickers);
	if (needRefresh) {
		_section = Section::Stickers;
		refreshRecentStickers(true);
	}

	int32 y = 0;
	for (int c = 0; c < _mySets.size(); ++c) {
		if (_mySets.at(c).id == setId) break;
		int rows = (_mySets[c].pack.size() / StickerPanPerRow) + ((_mySets[c].pack.size() % StickerPanPerRow) ? 1 : 0);
		y += st::emojiPanHeader + rows * st::stickerPanSize.height();
	}

	emit scrollToY(y);
	emit scrollUpdated();

	if (needRefresh) {
		emit refreshIcons(kRefreshIconsScrollAnimation);
	}

	_lastMousePos = QCursor::pos();

	update();
}

void StickerPanInner::updateShowingSavedGifs() {
	if (cShowingSavedGifs()) {
		if (!showingInlineItems()) {
			clearSelection();
			_section = Section::Gifs;
			if (_inlineRows.isEmpty()) refreshSavedGifs();
		}
	} else if (!showingInlineItems()) {
		clearSelection();
	}
}

void StickerPanInner::showFinish() {
	if (_section == Section::Gifs) {
		_setGifCommand = App::insertBotCommand('@' + cInlineGifBotUsername(), true);
	}
}

EmojiPanel::EmojiPanel(QWidget *parent, const QString &text, uint64 setId, bool special, int32 wantedY) : TWidget(parent)
, _wantedY(wantedY)
, _setId(setId)
, _special(special)
, _deleteVisible(false)
, _delete(special ? 0 : new Ui::IconButton(this, st::hashtagClose)) { // Stickers::NoneSetId if in emoji
	resize(st::emojiPanWidth - 2 * st::buttonRadius, st::emojiPanHeader);
	setMouseTracking(true);
	setText(text);
	if (_delete) {
		_delete->hide();
		_delete->moveToRight(st::emojiPanHeaderLeft - ((_delete->width() - st::hashtagClose.icon.width()) / 2) - st::buttonRadius, (st::emojiPanHeader - _delete->height()) / 2, width());
		connect(_delete, SIGNAL(clicked()), this, SLOT(onDelete()));
	}
}

void EmojiPanel::onDelete() {
	emit deleteClicked(_setId);
}

void EmojiPanel::setText(const QString &text) {
	_fullText = text;
	updateText();
}

void EmojiPanel::updateText() {
	int32 availw = st::emojiPanWidth - st::emojiPanHeaderLeft * 2;
	if (_deleteVisible) {
		if (!_special && _setId != Stickers::NoneSetId) {
			availw -= st::hashtagClose.icon.width() + st::emojiPanHeaderLeft;
		}
	} else {
		auto switchText = ([this]() {
			if (_setId != Stickers::NoneSetId) {
				return lang(lng_switch_emoji);
			}
			if (cSavedGifs().isEmpty()) {
				return lang(lng_switch_stickers);
			}
			return lang(lng_switch_stickers_gifs);
		})();
		availw -= st::emojiSwitchSkip + st::emojiPanHeaderFont->width(switchText);
	}
	_text = st::emojiPanHeaderFont->elided(_fullText, availw);
	update();
}

void EmojiPanel::setDeleteVisible(bool isVisible) {
	if (_deleteVisible != isVisible) {
		_deleteVisible = isVisible;
		updateText();
		if (_delete) {
			_delete->setVisible(_deleteVisible);
		}
	}
}

void EmojiPanel::mousePressEvent(QMouseEvent *e) {
	emit mousePressed();
}

void EmojiPanel::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (!_deleteVisible) {
		p.fillRect(0, 0, width(), st::emojiPanHeader, st::emojiPanHeaderBg);
	}
	p.setFont(st::emojiPanHeaderFont);
	p.setPen(st::emojiPanHeaderFg);
	p.drawTextLeft(st::emojiPanHeaderLeft - st::buttonRadius, st::emojiPanHeaderTop, width(), _text);
}

EmojiSwitchButton::EmojiSwitchButton(QWidget *parent, bool toStickers) : AbstractButton(parent)
, _toStickers(toStickers) {
	setCursor(style::cur_pointer);
	updateText();
}

void EmojiSwitchButton::updateText(const QString &inlineBotUsername) {
	if (_toStickers) {
		if (inlineBotUsername.isEmpty()) {
			_text = lang(cSavedGifs().isEmpty() ? lng_switch_stickers : lng_switch_stickers_gifs);
		} else {
			_text = '@' + inlineBotUsername;
		}
	} else {
		_text = lang(lng_switch_emoji);
	}
	_textWidth = st::emojiPanHeaderFont->width(_text);
	if (_toStickers && !inlineBotUsername.isEmpty()) {
		int32 maxw = 0;
		for (int c = 0; c < emojiTabCount; ++c) {
			accumulate_max(maxw, st::emojiPanHeaderFont->width(lang(LangKey(lng_emoji_category0 + c))));
		}
		maxw += st::emojiPanHeaderLeft + st::emojiSwitchSkip + (st::emojiSwitchSkip - st::emojiSwitchImgSkip);
		if (_textWidth > st::emojiPanWidth - maxw) {
			_text = st::emojiPanHeaderFont->elided(_text, st::emojiPanWidth - maxw);
			_textWidth = st::emojiPanHeaderFont->width(_text);
		}
	}

	int32 w = st::emojiSwitchSkip + _textWidth + (st::emojiSwitchSkip - st::emojiSwitchImgSkip) - st::buttonRadius;
	resize(w, st::emojiPanHeader);
}

void EmojiSwitchButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.setFont(st::emojiPanHeaderFont);
	p.setPen(st::emojiSwitchColor);
	if (_toStickers) {
		p.drawTextRight(st::emojiSwitchSkip, st::emojiPanHeaderTop, width(), _text, _textWidth);
		st::emojiSwitchStickers.paint(p, width() - st::emojiSwitchImgSkip, (st::emojiPanHeader - st::emojiSwitchStickers.height()) / 2, width());
	} else {
		p.drawTextRight(st::emojiSwitchImgSkip - st::emojiSwitchEmoji.width(), st::emojiPanHeaderTop, width(), lang(lng_switch_emoji), _textWidth);
		st::emojiSwitchEmoji.paint(p, width() - st::emojiSwitchSkip - _textWidth, (st::emojiPanHeader - st::emojiSwitchEmoji.height()) / 2, width());
	}
}

} // namespace internal

namespace {

FORCE_INLINE uint32 twoImagesOnBgWithAlpha(
	const anim::Shifted shiftedBg,
	const uint32 source1Alpha,
	const uint32 source2Alpha,
	const uint32 source1,
	const uint32 source2,
	const uint32 alpha) {
	auto source1Pattern = anim::reshifted(anim::shifted(source1) * source1Alpha);
	auto bg1Alpha = 256 - anim::getAlpha(source1Pattern);
	auto mixed1Pattern = anim::reshifted(shiftedBg * bg1Alpha) + source1Pattern;
	auto source2Pattern = anim::reshifted(anim::shifted(source2) * source2Alpha);
	auto bg2Alpha = 256 - anim::getAlpha(source2Pattern);
	auto mixed2Pattern = anim::reshifted(mixed1Pattern * bg2Alpha) + source2Pattern;
	return anim::unshifted(mixed2Pattern * alpha);
}

FORCE_INLINE uint32 oneImageOnBgWithAlpha(
	const anim::Shifted shiftedBg,
	const uint32 sourceAlpha,
	const uint32 source,
	const uint32 alpha) {
	auto sourcePattern = anim::reshifted(anim::shifted(source) * sourceAlpha);
	auto bgAlpha = 256 - anim::getAlpha(sourcePattern);
	auto mixedPattern = anim::reshifted(shiftedBg * bgAlpha) + sourcePattern;
	return anim::unshifted(mixedPattern * alpha);
};

} // namespace

class EmojiPan::SlideAnimation : public Ui::RoundShadowAnimation {
public:
	enum class Direction {
		LeftToRight,
		RightToLeft,
	};
	void setFinalImages(Direction direction, QImage &&left, QImage &&right, QRect inner);

	void start();
	void paintFrame(QPainter &p, float64 dt, float64 opacity);

private:
	Direction _direction = Direction::LeftToRight;
	QPixmap _leftImage, _rightImage;
	int _width = 0;
	int _height = 0;
	int _innerLeft = 0;
	int _innerTop = 0;
	int _innerRight = 0;
	int _innerBottom = 0;
	int _innerWidth = 0;
	int _innerHeight = 0;

	int _painterInnerLeft = 0;
	int _painterInnerTop = 0;
	int _painterInnerWidth = 0;
	int _painterInnerBottom = 0;
	int _painterCategoriesTop = 0;
	int _painterInnerHeight = 0;
	int _painterInnerRight = 0;

	int _frameIntsPerLineAdd = 0;

};

void EmojiPan::SlideAnimation::setFinalImages(Direction direction, QImage &&left, QImage &&right, QRect inner) {
	t_assert(!started());
	_direction = direction;
	_leftImage = QPixmap::fromImage(std::move(left).convertToFormat(QImage::Format_ARGB32_Premultiplied), Qt::ColorOnly);
	_rightImage = QPixmap::fromImage(std::move(right).convertToFormat(QImage::Format_ARGB32_Premultiplied), Qt::ColorOnly);

	t_assert(!_leftImage.isNull());
	t_assert(!_rightImage.isNull());
	_width = _leftImage.width();
	_height = _rightImage.height();
	t_assert(!(_width % cIntRetinaFactor()));
	t_assert(!(_height % cIntRetinaFactor()));
	t_assert(_leftImage.devicePixelRatio() == _rightImage.devicePixelRatio());
	t_assert(_rightImage.width() == _width);
	t_assert(_rightImage.height() == _height);
	t_assert(QRect(0, 0, _width, _height).contains(inner));
	_innerLeft = inner.x();
	_innerTop = inner.y();
	_innerWidth = inner.width();
	_innerHeight = inner.height();
	t_assert(!(_innerLeft % cIntRetinaFactor()));
	t_assert(!(_innerTop % cIntRetinaFactor()));
	t_assert(!(_innerWidth % cIntRetinaFactor()));
	t_assert(!(_innerHeight % cIntRetinaFactor()));
	_innerRight = _innerLeft + _innerWidth;
	_innerBottom = _innerTop + _innerHeight;

	_painterInnerLeft = _innerLeft / cIntRetinaFactor();
	_painterInnerTop = _innerTop / cIntRetinaFactor();
	_painterInnerRight = _innerRight / cIntRetinaFactor();
	_painterInnerBottom = _innerBottom / cIntRetinaFactor();
	_painterInnerWidth = _innerWidth / cIntRetinaFactor();
	_painterInnerHeight = _innerHeight / cIntRetinaFactor();
	_painterCategoriesTop = _painterInnerBottom - st::emojiCategory.height;

	setShadow(st::emojiPanAnimation.shadow);
}

void EmojiPan::SlideAnimation::start() {
	t_assert(!_leftImage.isNull());
	t_assert(!_rightImage.isNull());
	RoundShadowAnimation::start(_width, _height, _leftImage.devicePixelRatio());
	auto checkCorner = [this](const Corner &corner) {
		if (!corner.valid()) return;
		t_assert(corner.width <= _innerWidth);
		t_assert(corner.height <= _innerHeight);
	};
	checkCorner(_topLeft);
	checkCorner(_topRight);
	checkCorner(_bottomLeft);
	checkCorner(_bottomRight);
	_frameIntsPerLineAdd = (_width - _innerWidth) + _frameIntsPerLineAdded;
}

void EmojiPan::SlideAnimation::paintFrame(QPainter &p, float64 dt, float64 opacity) {
	t_assert(started());
	t_assert(dt >= 0.);

	_frameAlpha = anim::interpolate(1, 256, opacity);

	auto frameInts = _frameInts + _innerLeft + _innerTop * _frameIntsPerLine;

	auto leftToRight = (_direction == Direction::LeftToRight);

	auto easeOut = anim::easeOutCirc(1., dt);
	auto easeIn = anim::easeInCirc(1., dt);

	auto arrivingCoord = anim::interpolate(_innerWidth, 0, easeOut);
	auto departingCoord = anim::interpolate(0, _innerWidth, easeIn);
	if (auto decrease = (arrivingCoord % cIntRetinaFactor())) {
		arrivingCoord -= decrease;
	}
	if (auto decrease = (departingCoord % cIntRetinaFactor())) {
		departingCoord -= decrease;
	}
	auto arrivingAlpha = easeIn;
	auto departingAlpha = 1. - easeOut;
	auto leftCoord = (leftToRight ? arrivingCoord : departingCoord) * -1;
	auto leftAlpha = (leftToRight ? arrivingAlpha : departingAlpha);
	auto rightCoord = (leftToRight ? departingCoord : arrivingCoord);
	auto rightAlpha = (leftToRight ? departingAlpha : arrivingAlpha);

	// _innerLeft ..(left).. leftTo ..(both).. bothTo ..(none).. noneTo ..(right).. _innerRight
	auto leftTo = _innerLeft + snap(_innerWidth + leftCoord, 0, _innerWidth);
	auto rightFrom = _innerLeft + snap(rightCoord, 0, _innerWidth);
	auto painterRightFrom = rightFrom / cIntRetinaFactor();
	if (opacity < 1.) {
		_frame.fill(Qt::transparent);
	}
	{
		Painter p(&_frame);
		p.setOpacity(opacity);
		p.fillRect(_painterInnerLeft, _painterInnerTop, _painterInnerWidth, _painterCategoriesTop - _painterInnerTop, st::emojiPanBg);
		p.fillRect(_painterInnerLeft, _painterCategoriesTop, _painterInnerWidth, _painterInnerBottom - _painterCategoriesTop, st::emojiPanCategories);
		p.setCompositionMode(QPainter::CompositionMode_SourceOver);
		if (leftTo > _innerLeft) {
			p.setOpacity(opacity * leftAlpha);
			p.drawPixmap(_painterInnerLeft, _painterInnerTop, _leftImage, _innerLeft - leftCoord, _innerTop, leftTo - _innerLeft, _innerHeight);
		}
		if (rightFrom < _innerRight) {
			p.setOpacity(opacity * rightAlpha);
			p.drawPixmap(painterRightFrom, _painterInnerTop, _rightImage, _innerLeft, _innerTop, _innerRight - rightFrom, _innerHeight);
		}
	}

	// Draw corners
	paintCorner(_topLeft, _innerLeft, _innerTop);
	paintCorner(_topRight, _innerRight - _topRight.width, _innerTop);
	paintCorner(_bottomLeft, _innerLeft, _innerBottom - _bottomLeft.height);
	paintCorner(_bottomRight, _innerRight - _bottomRight.width, _innerBottom - _bottomRight.height);

	// Draw shadow upon the transparent
	auto outerLeft = _innerLeft;
	auto outerTop = _innerTop;
	auto outerRight = _innerRight;
	auto outerBottom = _innerBottom;
	if (_shadow.valid()) {
		outerLeft -= _shadow.extend.left();
		outerTop -= _shadow.extend.top();
		outerRight += _shadow.extend.right();
		outerBottom += _shadow.extend.bottom();
	}
	if (cIntRetinaFactor() > 1) {
		if (auto skipLeft = (outerLeft % cIntRetinaFactor())) {
			outerLeft -= skipLeft;
		}
		if (auto skipTop = (outerTop % cIntRetinaFactor())) {
			outerTop -= skipTop;
		}
		if (auto skipRight = (outerRight % cIntRetinaFactor())) {
			outerRight += (cIntRetinaFactor() - skipRight);
		}
		if (auto skipBottom = (outerBottom % cIntRetinaFactor())) {
			outerBottom += (cIntRetinaFactor() - skipBottom);
		}
	}

	if (opacity == 1.) {
		// Fill above the frame top with transparent.
		auto fillTopInts = (_frameInts + outerTop * _frameIntsPerLine + outerLeft);
		auto fillWidth = (outerRight - outerLeft) * sizeof(uint32);
		for (auto fillTop = _innerTop - outerTop; fillTop != 0; --fillTop) {
			memset(fillTopInts, 0, fillWidth);
			fillTopInts += _frameIntsPerLine;
		}

		// Fill to the left and to the right of the frame with transparent.
		auto fillLeft = (_innerLeft - outerLeft) * sizeof(uint32);
		auto fillRight = (outerRight - _innerRight) * sizeof(uint32);
		if (fillLeft || fillRight) {
			auto fillInts = _frameInts + _innerTop * _frameIntsPerLine;
			for (auto y = _innerTop; y != _innerBottom; ++y) {
				memset(fillInts + outerLeft, 0, fillLeft);
				memset(fillInts + _innerRight, 0, fillRight);
				fillInts += _frameIntsPerLine;
			}
		}

		// Fill below the frame bottom with transparent.
		auto fillBottomInts = (_frameInts + _innerBottom * _frameIntsPerLine + outerLeft);
		for (auto fillBottom = outerBottom - _innerBottom; fillBottom != 0; --fillBottom) {
			memset(fillBottomInts, 0, fillWidth);
			fillBottomInts += _frameIntsPerLine;
		}
	}
	if (_shadow.valid()) {
		paintShadow(outerLeft, outerTop, outerRight, outerBottom);
	}

	// Debug
	//frameInts = _frameInts;
	//auto pattern = anim::shifted((static_cast<uint32>(0xFF) << 24) | (static_cast<uint32>(0xFF) << 16) | (static_cast<uint32>(0xFF) << 8) | static_cast<uint32>(0xFF));
	//for (auto y = 0; y != _finalHeight; ++y) {
	//	for (auto x = 0; x != _finalWidth; ++x) {
	//		auto source = *frameInts;
	//		auto sourceAlpha = (source >> 24);
	//		*frameInts = anim::unshifted(anim::shifted(source) * 256 + pattern * (256 - sourceAlpha));
	//		++frameInts;
	//	}
	//	frameInts += _frameIntsPerLineAdded;
	//}

	p.drawImage(outerLeft / cIntRetinaFactor(), outerTop / cIntRetinaFactor(), _frame, outerLeft, outerTop, outerRight - outerLeft, outerBottom - outerTop);
}

EmojiPan::EmojiPan(QWidget *parent) : TWidget(parent)
, _contentMaxHeight(st::emojiPanMaxHeight)
, _contentHeight(_contentMaxHeight)
, _contentHeightEmoji(_contentHeight - st::emojiCategory.height)
, _contentHeightStickers(_contentHeight - st::emojiCategory.height)
, _recent(this, st::emojiCategoryRecent)
, _people(this, st::emojiCategoryPeople)
, _nature(this, st::emojiCategoryNature)
, _food(this, st::emojiCategoryFood)
, _activity(this, st::emojiCategoryActivity)
, _travel(this, st::emojiCategoryTravel)
, _objects(this, st::emojiCategoryObjects)
, _symbols(this, st::emojiCategorySymbols)
, _a_icons(animation(this, &EmojiPan::step_icons))
, e_scroll(this, st::emojiScroll)
, e_switch(e_scroll, true)
, s_scroll(this, st::emojiScroll)
, s_switch(s_scroll, false) {
	resize(QRect(0, 0, st::emojiPanWidth, _contentHeight).marginsAdded(innerPadding()).size());
	_width = width();
	_height = height();

	e_scroll->resize(st::emojiPanWidth - st::buttonRadius, _contentHeightEmoji);
	s_scroll->resize(st::emojiPanWidth - st::buttonRadius, _contentHeightStickers);

	e_scroll->move(verticalRect().topLeft());
	e_inner = e_scroll->setOwnedWidget(object_ptr<internal::EmojiPanInner>(this));
	s_scroll->move(verticalRect().topLeft());
	s_inner = s_scroll->setOwnedWidget(object_ptr<internal::StickerPanInner>(this));

	e_inner->moveToLeft(0, 0, e_scroll->width());
	s_inner->moveToLeft(0, 0, s_scroll->width());

	int32 left = _iconsLeft = innerRect().x() + (st::emojiPanWidth - 8 * st::emojiCategory.width) / 2;
	int32 top = _iconsTop = innerRect().y() + innerRect().height() - st::emojiCategory.height;
	prepareTab(left, top, _width, _recent, dbietRecent);
	prepareTab(left, top, _width, _people, dbietPeople);
	prepareTab(left, top, _width, _nature, dbietNature);
	prepareTab(left, top, _width, _food, dbietFood);
	prepareTab(left, top, _width, _activity, dbietActivity);
	prepareTab(left, top, _width, _travel, dbietTravel);
	prepareTab(left, top, _width, _objects, dbietObjects);
	prepareTab(left, top, _width, _symbols, dbietSymbols);
	e_inner->fillPanels(e_panels);
	updatePanelsPositions(e_panels, 0);

	setCurrentTabIcon(dbietRecent);

	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(hideByTimerOrLeave()));

	connect(e_inner, SIGNAL(scrollToY(int)), e_scroll, SLOT(scrollToY(int)));
	connect(e_inner, SIGNAL(disableScroll(bool)), e_scroll, SLOT(disableScroll(bool)));

	connect(s_inner, SIGNAL(scrollToY(int)), s_scroll, SLOT(scrollToY(int)));
	connect(s_inner, SIGNAL(scrollUpdated()), this, SLOT(onScrollStickers()));

	connect(e_scroll, SIGNAL(scrolled()), this, SLOT(onScrollEmoji()));
	connect(s_scroll, SIGNAL(scrolled()), this, SLOT(onScrollStickers()));

	connect(e_inner, SIGNAL(selected(EmojiPtr)), this, SIGNAL(emojiSelected(EmojiPtr)));
	connect(s_inner, SIGNAL(selected(DocumentData*)), this, SIGNAL(stickerSelected(DocumentData*)));
	connect(s_inner, SIGNAL(selected(PhotoData*)), this, SIGNAL(photoSelected(PhotoData*)));
	connect(s_inner, SIGNAL(selected(InlineBots::Result*,UserData*)), this, SIGNAL(inlineResultSelected(InlineBots::Result*,UserData*)));

	connect(s_inner, SIGNAL(emptyInlineRows()), this, SLOT(onEmptyInlineRows()));

	connect(s_switch, SIGNAL(clicked()), this, SLOT(onSwitch()));
	connect(e_switch, SIGNAL(clicked()), this, SLOT(onSwitch()));
	s_switch->moveToRight(st::buttonRadius, 0, st::emojiPanWidth);
	e_switch->moveToRight(st::buttonRadius, 0, st::emojiPanWidth);

	connect(s_inner, SIGNAL(displaySet(quint64)), this, SLOT(onDisplaySet(quint64)));
	connect(s_inner, SIGNAL(installSet(quint64)), this, SLOT(onInstallSet(quint64)));
	connect(s_inner, SIGNAL(removeSet(quint64)), this, SLOT(onRemoveSet(quint64)));
	connect(s_inner, SIGNAL(refreshIcons(bool)), this, SLOT(onRefreshIcons(bool)));
	connect(e_inner, SIGNAL(needRefreshPanels()), this, SLOT(onRefreshPanels()));
	connect(s_inner, SIGNAL(needRefreshPanels()), this, SLOT(onRefreshPanels()));

	_saveConfigTimer.setSingleShot(true);
	connect(&_saveConfigTimer, SIGNAL(timeout()), this, SLOT(onSaveConfig()));
	connect(e_inner, SIGNAL(saveConfigDelayed(int32)), this, SLOT(onSaveConfigDelayed(int32)));
	connect(s_inner, SIGNAL(saveConfigDelayed(int32)), this, SLOT(onSaveConfigDelayed(int32)));

	// inline bots
	_inlineRequestTimer.setSingleShot(true);
	connect(&_inlineRequestTimer, SIGNAL(timeout()), this, SLOT(onInlineRequest()));

	if (cPlatform() == dbipMac || cPlatform() == dbipMacOld) {
		connect(App::wnd()->windowHandle(), SIGNAL(activeChanged()), this, SLOT(onWndActiveChanged()));
	}

	setMouseTracking(true);
//	setAttribute(Qt::WA_AcceptTouchEvents);
	setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void EmojiPan::setMinTop(int minTop) {
	_minTop = minTop;
	updateContentHeight();
}

void EmojiPan::setMinBottom(int minBottom) {
	_minBottom = minBottom;
	updateContentHeight();
}

void EmojiPan::moveBottom(int bottom) {
	_bottom = bottom;
	updateContentHeight();
}

void EmojiPan::updateContentHeight() {
	auto wantedBottom = countBottom();
	auto maxContentHeight = wantedBottom - st::emojiPanMargins.top() - st::emojiPanMargins.bottom();
	auto contentHeight = qMin(_contentMaxHeight, maxContentHeight);
	accumulate_max(contentHeight, st::emojiPanMinHeight);
	auto resultTop = wantedBottom - st::emojiPanMargins.bottom() - contentHeight - st::emojiPanMargins.top();
	accumulate_max(resultTop, _minTop);
	auto he = contentHeight - st::emojiCategory.height;
	auto hs = contentHeight - (s_inner->showSectionIcons() ? st::emojiCategory.height : 0);
	if (contentHeight == _contentHeight && he == _contentHeightEmoji && hs == _contentHeightStickers) {
		move(x(), resultTop);
		return;
	}

	auto was = _contentHeight;
	auto wase = _contentHeightEmoji;
	auto wass = _contentHeightStickers;
	_contentHeight = contentHeight;
	_contentHeightEmoji = he;
	_contentHeightStickers = hs;

	resize(QRect(0, 0, innerRect().width(), _contentHeight).marginsAdded(innerPadding()).size());
	_height = height();
	move(x(), resultTop);

	if (was > _contentHeight || (was == _contentHeight && wass > _contentHeightStickers)) {
		e_scroll->resize(e_scroll->width(), _contentHeightEmoji);
		s_scroll->resize(s_scroll->width(), _contentHeightStickers);
		s_inner->setMaxHeight(_contentHeightStickers);
		e_inner->setMaxHeight(_contentHeightEmoji);
	} else {
		s_inner->setMaxHeight(_contentHeightStickers);
		e_inner->setMaxHeight(_contentHeightEmoji);
		e_scroll->resize(e_scroll->width(), _contentHeightEmoji);
		s_scroll->resize(s_scroll->width(), _contentHeightStickers);
	}

	_iconsTop = innerRect().y() + innerRect().height() - st::emojiCategory.height;
	_recent->move(_recent->x(), _iconsTop);
	_people->move(_people->x(), _iconsTop);
	_nature->move(_nature->x(), _iconsTop);
	_food->move(_food->x(), _iconsTop);
	_activity->move(_activity->x(), _iconsTop);
	_travel->move(_travel->x(), _iconsTop);
	_objects->move(_objects->x(), _iconsTop);
	_symbols->move(_symbols->x(), _iconsTop);

	update();
}

void EmojiPan::prepareTab(int &left, int top, int _width, Ui::IconButton *tab, DBIEmojiTab value) {
	tab->moveToLeft(left, top, _width);
	left += tab->width();
	tab->setClickedCallback([this, value] { setActiveTab(value); });
}

void EmojiPan::onWndActiveChanged() {
	if (!App::wnd()->windowHandle()->isActive() && !isHidden()) {
		leaveEvent(0);
	}
}

void EmojiPan::onSaveConfig() {
	Local::writeUserSettings();
}

void EmojiPan::onSaveConfigDelayed(int32 delay) {
	_saveConfigTimer.start(delay);
}

void EmojiPan::paintStickerSettingsIcon(Painter &p) const {
	int settingsLeft = _iconsLeft + 7 * st::emojiCategory.width;
	st::stickersSettings.paint(p, settingsLeft + st::emojiCategory.iconPosition.x(), _iconsTop + st::emojiCategory.iconPosition.y(), width());
}

void EmojiPan::paintFeaturedStickerSetsBadge(Painter &p, int iconLeft) const {
	if (auto unread = Global::FeaturedStickerSetsUnreadCount()) {
		Dialogs::Layout::UnreadBadgeStyle unreadSt;
		unreadSt.sizeId = Dialogs::Layout::UnreadBadgeInStickersPanel;
		unreadSt.size = st::stickersSettingsUnreadSize;
		int unreadRight = iconLeft + st::emojiCategory.width - st::stickersSettingsUnreadPosition.x();
		if (rtl()) unreadRight = width() - unreadRight;
		int unreadTop = _iconsTop + st::stickersSettingsUnreadPosition.y();
		Dialogs::Layout::paintUnreadCount(p, QString::number(unread), unreadRight, unreadTop, unreadSt);
	}
}

void EmojiPan::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();

	// This call can finish _a_show animation and destroy _showAnimation.
	auto opacityAnimating = _a_opacity.animating(ms);

	auto switching = (_slideAnimation != nullptr);
	auto showAnimating = _a_show.animating(ms);
	if (_showAnimation && !showAnimating) {
		_showAnimation.reset();
		if (!switching && !opacityAnimating) {
			showAll();
		}
	}

	if (showAnimating) {
		t_assert(_showAnimation != nullptr);
		if (auto opacity = _a_opacity.current(_hiding ? 0. : 1.)) {
			_showAnimation->paintFrame(p, 0, 0, width(), _a_show.current(1.), opacity);
		}
	} else if (!switching && opacityAnimating) {
		p.setOpacity(_a_opacity.current(_hiding ? 0. : 1.));
		p.drawPixmap(0, 0, _cache);
	} else if ((!switching && _hiding) || isHidden()) {
		hideFinished();
	} else if (switching) {
		auto slideDt = _a_slide.current(ms, 1.);
		_slideAnimation->paintFrame(p, slideDt, _a_opacity.current(_hiding ? 0. : 1.));
		if (!_a_slide.animating()) {
			_slideAnimation.reset();
			if (!opacityAnimating) showAll();
		}
	} else {
		if (!_cache.isNull()) _cache = QPixmap();
		if (!_inPanelGrab) Ui::Shadow::paint(p, innerRect(), width(), st::emojiPanAnimation.shadow);
		paintContent(p);
	}
}

void EmojiPan::paintContent(Painter &p) {
	auto inner = innerRect();
	App::roundRect(p, inner, st::emojiPanBg, ImageRoundRadius::Small, App::RectPart::TopFull);

	auto showSectionIcons = _emojiShown || s_inner->showSectionIcons();
	auto &bottomBg = showSectionIcons ? st::emojiPanCategories : st::emojiPanBg;
	auto bottomParts = showSectionIcons ? (App::RectPart::NoTopBottom | App::RectPart::BottomFull) : App::RectPart::BottomFull;
	App::roundRect(p, inner.x(), _iconsTop - st::buttonRadius, inner.width(), st::emojiCategory.height + st::buttonRadius, bottomBg, ImageRoundRadius::Small, bottomParts);

	auto horizontal = horizontalRect();
	auto sidesTop = horizontal.y();
	auto sidesHeight = e_scroll->y() + e_scroll->height() - sidesTop;
	p.fillRect(myrtlrect(inner.x() + inner.width() - st::emojiScroll.width, sidesTop, st::emojiScroll.width, sidesHeight), st::emojiPanBg);
	p.fillRect(myrtlrect(inner.x(), sidesTop, st::buttonRadius, sidesHeight), st::emojiPanBg);
	if (_emojiShown) {
		auto vertical = verticalRect();
		p.fillRect(vertical.x(), _iconsTop, vertical.width(), st::emojiCategory.height - st::buttonRadius, st::emojiPanCategories);
	} else if (showSectionIcons) {
		paintStickerSettingsIcon(p);

		if (!_icons.isEmpty()) {
			auto x = _iconsLeft;
			auto selxrel = _iconsLeft + qRound(_iconSelX.current());
			auto selx = selxrel - qRound(_iconsX.current());

			QRect clip(x, _iconsTop, _iconsLeft + 7 * st::emojiCategory.width - x, st::emojiCategory.height);
			if (rtl()) clip.moveLeft(width() - x - clip.width());
			p.setClipRect(clip);

			auto getSpecialSetIcon = [](uint64 setId, bool active) {
				if (setId == Stickers::NoneSetId) {
					return active ? &st::emojiSavedGifsActive : &st::emojiSavedGifs;
				} else if (setId == Stickers::FeaturedSetId) {
					return active ? &st::stickersTrendingActive : &st::stickersTrending;
				}
				return active ? &st::emojiRecentActive : &st::emojiRecent;
			};

			int i = 0;
			auto iconsX = qRound(_iconsX.current());
			i += iconsX / int(st::emojiCategory.width);
			x -= iconsX % int(st::emojiCategory.width);
			selxrel -= iconsX;
			for (int l = qMin(_icons.size(), i + 8); i < l; ++i) {
				auto &s = _icons.at(i);
				if (s.sticker) {
					s.sticker->thumb->load();
					QPixmap pix(s.sticker->thumb->pix(s.pixw, s.pixh));

					p.drawPixmapLeft(x + (st::emojiCategory.width - s.pixw) / 2, _iconsTop + (st::emojiCategory.height - s.pixh) / 2, width(), pix);
					x += st::emojiCategory.width;
				} else {
					getSpecialSetIcon(s.setId, false)->paint(p, x + st::emojiCategory.iconPosition.x(), _iconsTop + st::emojiCategory.iconPosition.y(), width());
					if (s.setId == Stickers::FeaturedSetId) {
						paintFeaturedStickerSetsBadge(p, x);
					}
					x += st::emojiCategory.width;
				}
			}

			if (rtl()) selx = width() - selx - st::emojiCategory.width;
			p.fillRect(selx, _iconsTop + st::emojiCategory.height - st::stickerIconPadding, st::emojiCategory.width, st::stickerIconSel, st::stickerIconSelColor);

			auto o_left = snap(_iconsX.current() / st::stickerIconLeft.width(), 0., 1.);
			if (o_left > 0) {
				p.setOpacity(o_left);
				st::stickerIconLeft.fill(p, rtlrect(_iconsLeft, _iconsTop, st::stickerIconLeft.width(), st::emojiCategory.height, width()));
				p.setOpacity(1.);
			}
			auto o_right = snap((_iconsMax - _iconsX.current()) / st::stickerIconRight.width(), 0., 1.);
			if (o_right > 0) {
				p.setOpacity(o_right);
				st::stickerIconRight.fill(p, rtlrect(_iconsLeft + 7 * st::emojiCategory.width - st::stickerIconRight.width(), _iconsTop, st::stickerIconRight.width(), st::emojiCategory.height, width()));
				p.setOpacity(1.);
			}

			p.setClipRect(QRect());
		}
	} else {
		p.fillRect(myrtlrect(inner.x() + inner.width() - st::emojiScroll.width, _iconsTop, st::emojiScroll.width, st::emojiCategory.height - st::buttonRadius), st::emojiPanBg);
		p.fillRect(myrtlrect(inner.x(), _iconsTop, st::buttonRadius, st::emojiCategory.height - st::buttonRadius), st::emojiPanBg);
	}
}

bool EmojiPan::inlineResultsShown() const {
	return !_emojiShown && s_inner->inlineResultsShown();
}

int EmojiPan::countBottom() const {
	return (_origin == Ui::PanelAnimation::Origin::BottomLeft) ? _bottom : (parentWidget()->height() - _minBottom);
}

void EmojiPan::moveByBottom() {
	if (inlineResultsShown()) {
		setOrigin(Ui::PanelAnimation::Origin::BottomLeft);
		moveToLeft(0, y());
	} else {
		setOrigin(Ui::PanelAnimation::Origin::BottomRight);
		moveToRight(0, y());
	}
	updateContentHeight();
}

void EmojiPan::enterEventHook(QEvent *e) {
	_hideTimer.stop();
	if (_hiding) showAnimated(_origin);
}

bool EmojiPan::preventAutoHide() const {
	return _removingSetId || _displayingSetId;
}

void EmojiPan::leaveEventHook(QEvent *e) {
	if (preventAutoHide() || s_inner->inlineResultsShown()) return;
	auto ms = getms();
	if (_a_show.animating(ms) || _a_opacity.animating(ms)) {
		hideAnimated();
	} else {
		_hideTimer.start(300);
	}
	return TWidget::leaveEventHook(e);
}

void EmojiPan::otherEnter() {
	_hideTimer.stop();
	showAnimated(_origin);
}

void EmojiPan::otherLeave() {
	if (preventAutoHide() || s_inner->inlineResultsShown()) return;
	auto ms = getms();
	if (_a_opacity.animating(ms)) {
		hideByTimerOrLeave();
	} else {
		_hideTimer.start(0);
	}
}

void EmojiPan::mousePressEvent(QMouseEvent *e) {
	if (_emojiShown || e->button() != Qt::LeftButton) return;
	_iconsMousePos = e ? e->globalPos() : QCursor::pos();
	updateSelected();

	if (_iconOver == _icons.size()) {
		Ui::show(Box<StickersBox>(StickersBox::Section::Installed));
	} else {
		_iconDown = _iconOver;
		_iconsMouseDown = _iconsMousePos;
		_iconsStartX = qRound(_iconsX.current());
	}
}

void EmojiPan::mouseMoveEvent(QMouseEvent *e) {
	if (_emojiShown) return;
	_iconsMousePos = e ? e->globalPos() : QCursor::pos();
	updateSelected();

	if (!_iconsDragging && !_icons.isEmpty() && _iconDown >= 0) {
		if ((_iconsMousePos - _iconsMouseDown).manhattanLength() >= QApplication::startDragDistance()) {
			_iconsDragging = true;
		}
	}
	if (_iconsDragging) {
		auto newX = snap(_iconsStartX + (rtl() ? -1 : 1) * (_iconsMouseDown.x() - _iconsMousePos.x()), 0, _iconsMax);
		if (newX != qRound(_iconsX.current())) {
			_iconsX = anim::value(newX, newX);
			_iconsStartAnim = 0;
			_a_icons.stop();
			updateIcons();
		}
	}
}

void EmojiPan::mouseReleaseEvent(QMouseEvent *e) {
	if (_emojiShown || _icons.isEmpty()) return;

	int32 wasDown = _iconDown;
	_iconDown = -1;

	_iconsMousePos = e ? e->globalPos() : QCursor::pos();
	if (_iconsDragging) {
		auto newX = snap(_iconsStartX + _iconsMouseDown.x() - _iconsMousePos.x(), 0, _iconsMax);
		if (newX != qRound(_iconsX.current())) {
			_iconsX = anim::value(newX, newX);
			_iconsStartAnim = 0;
			_a_icons.stop();
			updateIcons();
		}
		_iconsDragging = false;
		updateSelected();
	} else {
		updateSelected();

		if (wasDown == _iconOver && _iconOver >= 0 && _iconOver < _icons.size()) {
			_iconSelX = anim::value(_iconOver * st::emojiCategory.width, _iconOver * st::emojiCategory.width);
			s_inner->showStickerSet(_icons.at(_iconOver).setId);
		}
	}
}

bool EmojiPan::event(QEvent *e) {
	if (e->type() == QEvent::TouchBegin) {

	} else if (e->type() == QEvent::Wheel) {
		if (!_icons.isEmpty() && _iconOver >= 0 && _iconOver < _icons.size() && _iconDown < 0) {
			QWheelEvent *ev = static_cast<QWheelEvent*>(e);
			bool hor = (ev->angleDelta().x() != 0 || ev->orientation() == Qt::Horizontal);
			bool ver = (ev->angleDelta().y() != 0 || ev->orientation() == Qt::Vertical);
			if (hor) _horizontal = true;
			auto newX = qRound(_iconsX.current());
			if (/*_horizontal && */hor) {
				newX = snap(newX - (rtl() ? -1 : 1) * (ev->pixelDelta().x() ? ev->pixelDelta().x() : ev->angleDelta().x()), 0, _iconsMax);
			} else if (/*!_horizontal && */ver) {
				newX = snap(newX - (ev->pixelDelta().y() ? ev->pixelDelta().y() : ev->angleDelta().y()), 0, _iconsMax);
			}
			if (newX != qRound(_iconsX.current())) {
				_iconsX = anim::value(newX, newX);
				_iconsStartAnim = 0;
				_a_icons.stop();
				updateSelected();
				updateIcons();
			}
		}
	}
	return TWidget::event(e);
}

void EmojiPan::hideFast() {
	if (isHidden()) return;

	_hideTimer.stop();
	_hiding = false;
	_a_opacity.finish();
	hideFinished();
}

void EmojiPan::refreshStickers() {
	s_inner->refreshStickers();
	if (_emojiShown) {
		s_inner->preloadImages();
	}
	update();
}

void EmojiPan::refreshSavedGifs() {
	e_switch->updateText();
	e_switch->moveToRight(st::buttonRadius, 0, st::emojiPanWidth);
	s_inner->refreshSavedGifs();
	if (_emojiShown) {
		s_inner->preloadImages();
	}
}

void EmojiPan::onRefreshIcons(bool scrollAnimation) {
	_iconOver = -1;
	s_inner->fillIcons(_icons);
	s_inner->fillPanels(s_panels);
	_iconsX.finish();
	_iconSelX.finish();
	_iconsStartAnim = 0;
	_a_icons.stop();
	if (_icons.isEmpty()) {
		_iconsMax = 0;
	} else {
		_iconsMax = qMax(int((_icons.size() - 7) * st::emojiCategory.width), 0);
	}
	if (_iconsX.current() > _iconsMax) {
		_iconsX = anim::value(_iconsMax, _iconsMax);
	}
	updatePanelsPositions(s_panels, s_scroll->scrollTop());
	updateSelected();
	if (!_emojiShown) {
		validateSelectedIcon(scrollAnimation ? ValidateIconAnimations::Scroll : ValidateIconAnimations::None);
		updateContentHeight();
	}
	updateIcons();
}

void EmojiPan::onRefreshPanels() {
	e_inner->refreshPanels(e_panels);
	s_inner->refreshPanels(s_panels);
	if (_emojiShown) {
		updatePanelsPositions(e_panels, e_scroll->scrollTop());
	} else {
		updatePanelsPositions(s_panels, s_scroll->scrollTop());
	}
}

void EmojiPan::leaveToChildEvent(QEvent *e, QWidget *child) {
	if (_emojiShown) return;
	_iconsMousePos = QCursor::pos();
	updateSelected();
}

void EmojiPan::updateSelected() {
	if (_iconDown >= 0) {
		return;
	}

	QPoint p(mapFromGlobal(_iconsMousePos));
	int32 x = p.x(), y = p.y(), newOver = -1;
	if (rtl()) x = width() - x;
	x -= _iconsLeft;
	if (x >= st::emojiCategory.width * 7 && x < st::emojiCategory.width * 8 && y >= _iconsTop && y < _iconsTop + st::emojiCategory.height) {
		newOver = _icons.size();
	} else if (!_icons.isEmpty()) {
		if (y >= _iconsTop && y < _iconsTop + st::emojiCategory.height && x >= 0 && x < 7 * st::emojiCategory.width && x < _icons.size() * st::emojiCategory.width) {
			x += qRound(_iconsX.current());
			newOver = qFloor(x / st::emojiCategory.width);
		}
	}
	if (newOver != _iconOver) {
		if (newOver < 0) {
			setCursor(style::cur_default);
		} else if (_iconOver < 0) {
			setCursor(style::cur_pointer);
		}
		_iconOver = newOver;
	}
}

void EmojiPan::updateIcons() {
	if (_emojiShown || !s_inner->showSectionIcons()) return;

	auto verticalInner = rect().marginsRemoved(st::emojiPanMargins).marginsRemoved(QMargins(st::buttonRadius, 0, st::buttonRadius, 0));
	update(verticalInner.left(), _iconsTop, verticalInner.width(), st::emojiCategory.height);
}

void EmojiPan::step_icons(TimeMs ms, bool timer) {
	if (_emojiShown) {
		_a_icons.stop();
		return;
	}

	if (_iconsStartAnim) {
		float64 dt = (ms - _iconsStartAnim) / float64(st::stickerIconMove);
		if (dt >= 1) {
			_iconsStartAnim = 0;
			_iconsX.finish();
			_iconSelX.finish();
		} else {
			_iconsX.update(dt, anim::linear);
			_iconSelX.update(dt, anim::linear);
		}
		if (timer) updateSelected();
	}

	if (timer) updateIcons();

	if (!_iconsStartAnim) {
		_a_icons.stop();
	}
}

void EmojiPan::opacityAnimationCallback() {
	update();
	if (!_a_opacity.animating()) {
		if (_hiding) {
			_hiding = false;
			hideFinished();
		} else if (!_a_show.animating() && !_a_slide.animating()) {
			showAll();
		}
	}
}

void EmojiPan::hideByTimerOrLeave() {
	if (isHidden() || preventAutoHide() || s_inner->inlineResultsShown()) return;

	hideAnimated();
}

void EmojiPan::prepareCache() {
	if (_a_opacity.animating()) return;

	auto showAnimation = base::take(_a_show);
	auto showAnimationData = base::take(_showAnimation);
	auto slideAnimation = base::take(_slideAnimation);
	showAll();
	_cache = myGrab(this);
	_slideAnimation = base::take(slideAnimation);
	_showAnimation = base::take(showAnimationData);
	_a_show = base::take(showAnimation);
	if (_a_show.animating()) {
		hideAll();
	}
}

void EmojiPan::startOpacityAnimation(bool hiding) {
	_hiding = false;
	prepareCache();
	_hiding = hiding;
	hideAll();
	_a_opacity.start([this] { opacityAnimationCallback(); }, _hiding ? 1. : 0., _hiding ? 0. : 1., st::emojiPanDuration);
}

void EmojiPan::startShowAnimation() {
	if (!_a_show.animating()) {
		auto cache = base::take(_cache);
		auto opacityAnimation = base::take(_a_opacity);
		auto slideAnimationData = base::take(_slideAnimation);
		auto slideAnimation = base::take(_a_slide);
		showAll();
		auto image = grabForPanelAnimation();
		_a_slide = base::take(slideAnimation);
		_slideAnimation = base::take(slideAnimationData);
		_a_opacity = base::take(opacityAnimation);
		_cache = base::take(_cache);

		_showAnimation = std::make_unique<Ui::PanelAnimation>(st::emojiPanAnimation, _origin);
		auto inner = rect().marginsRemoved(st::emojiPanMargins);
		_showAnimation->setFinalImage(std::move(image), QRect(inner.topLeft() * cIntRetinaFactor(), inner.size() * cIntRetinaFactor()));
		auto corners = App::cornersMask(ImageRoundRadius::Small);
		_showAnimation->setCornerMasks(QImage(*corners[0]), QImage(*corners[1]), QImage(*corners[2]), QImage(*corners[3]));
		_showAnimation->start();
	}
	hideAll();
	_a_show.start([this] { update(); }, 0., 1., st::emojiPanShowDuration);
}

QImage EmojiPan::grabForPanelAnimation() {
	myEnsureResized(this);
	auto result = QImage(size() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	_inPanelGrab = true;
	render(&result);
	_inPanelGrab = false;
	return result;
}

void EmojiPan::hideAnimated() {
	if (isHidden()) return;
	if (_hiding) return;

	_hideTimer.stop();
	startOpacityAnimation(true);
}

EmojiPan::~EmojiPan() = default;

void EmojiPan::hideFinished() {
	hide();
	e_inner->hideFinish();
	s_inner->hideFinish(true);
	_a_show.finish();
	_showAnimation.reset();
	_a_slide.finish();
	_slideAnimation.reset();
	_cache = QPixmap();
	_horizontal = false;
	_hiding = false;

	e_scroll->scrollToY(0);
	setCurrentTabIcon(dbietRecent);
	s_scroll->scrollToY(0);
	_iconOver = _iconDown = -1;
	_iconSel = 0;
	_iconsX = anim::value();
	_iconSelX = anim::value();
	_iconsStartAnim = 0;
	_a_icons.stop();

	Notify::clipStopperHidden(ClipStopperSavedGifsPanel);
}

void EmojiPan::setOrigin(Ui::PanelAnimation::Origin origin) {
	_origin = origin;
}

void EmojiPan::showAnimated(Ui::PanelAnimation::Origin origin) {
	setOrigin(origin);
	_hideTimer.stop();
	showStarted();
}

void EmojiPan::showStarted() {
	if (isHidden()) {
		emit updateStickers();
		e_inner->refreshRecent();
		if (s_inner->inlineResultsShown() && refreshInlineRows()) {
			_emojiShown = false;
			_shownFromInlineQuery = true;
		} else {
			s_inner->refreshRecent();
			_emojiShown = true;
			_shownFromInlineQuery = false;
			_cache = QPixmap(); // clear after refreshInlineRows()
		}
		recountContentMaxHeight();
		s_inner->preloadImages();
		_a_slide.finish();
		_slideAnimation.reset();
		moveByBottom();
		show();
		startShowAnimation();
	} else if (_hiding) {
		if (s_inner->inlineResultsShown() && refreshInlineRows()) {
			onSwitch();
		}
		startOpacityAnimation(false);
	}
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
			_hideTimer.stop();
			showAnimated(_origin);
		} else {
			hideAnimated();
		}
	}
	return false;
}

void EmojiPan::stickersInstalled(uint64 setId) {
	_emojiShown = false;
	if (isHidden()) {
		moveByBottom();
		startShowAnimation();
		show();
	}
	showAll();
	s_inner->showStickerSet(setId);
	updateContentHeight();
	showAnimated(Ui::PanelAnimation::Origin::BottomRight);
}

void EmojiPan::notify_inlineItemLayoutChanged(const InlineBots::Layout::ItemBase *layout) {
	if (!_emojiShown && !isHidden()) {
		s_inner->notify_inlineItemLayoutChanged(layout);
	}
}

void EmojiPan::ui_repaintInlineItem(const InlineBots::Layout::ItemBase *layout) {
	if (!_emojiShown && !isHidden()) {
		s_inner->ui_repaintInlineItem(layout);
	}
}

bool EmojiPan::ui_isInlineItemVisible(const InlineBots::Layout::ItemBase *layout) {
	if (!_emojiShown && !isHidden()) {
		return s_inner->ui_isInlineItemVisible(layout);
	}
	return false;
}

bool EmojiPan::ui_isInlineItemBeingChosen() {
	if (!_emojiShown && !isHidden()) {
		return s_inner->ui_isInlineItemBeingChosen();
	}
	return false;
}

void EmojiPan::showAll() {
	if (_emojiShown) {
		s_scroll->hide();
		_recent->show();
		_people->show();
		_nature->show();
		_food->show();
		_activity->show();
		_travel->show();
		_objects->show();
		_symbols->show();
		e_scroll->show();
	} else {
		s_scroll->show();
		_recent->hide();
		_people->hide();
		_nature->hide();
		_food->hide();
		_activity->hide();
		_travel->hide();
		_objects->hide();
		_symbols->hide();
		e_scroll->hide();
	}
}

void EmojiPan::hideAll() {
	_recent->hide();
	_people->hide();
	_nature->hide();
	_food->hide();
	_activity->hide();
	_travel->hide();
	_objects->hide();
	_symbols->hide();
	e_scroll->hide();
	s_scroll->hide();
	e_inner->clearSelection();
	s_inner->clearSelection();
}

void EmojiPan::setActiveTab(DBIEmojiTab tab) {
	e_inner->showEmojiPack(tab);
}

void EmojiPan::updatePanelsPositions(const QVector<internal::EmojiPanel*> &panels, int32 st) {
	for (int32 i = 0, l = panels.size(); i < l; ++i) {
		int32 y = panels.at(i)->wantedY() - st;
		if (y < 0) {
			y = (i + 1 < l) ? qMin(panels.at(i + 1)->wantedY() - st - int(st::emojiPanHeader), 0) : 0;
		}
		panels.at(i)->move(0, y);
		panels.at(i)->setDeleteVisible(y >= st::emojiPanHeader);

		// Somehow the panels gets hidden (not displayed) when scrolling
		// by clicking on the scroll bar to the middle of the panel.
		// This bug occurs only in the Section::Featured stickers.
		if (s_inner->currentSet(0) == Stickers::FeaturedSetId) {
			panels.at(i)->repaint();
		}
	}
}

void EmojiPan::onScrollEmoji() {
	auto st = e_scroll->scrollTop();

	updatePanelsPositions(e_panels, st);

	setCurrentTabIcon(e_inner->currentTab(st));

	e_inner->setVisibleTopBottom(st, st + e_scroll->height());
}

void EmojiPan::setCurrentTabIcon(DBIEmojiTab tab) {
	_recent->setIconOverride((tab == dbietRecent) ? &st::emojiRecentActive : nullptr);
	_people->setIconOverride((tab == dbietPeople) ? &st::emojiPeopleActive : nullptr);
	_nature->setIconOverride((tab == dbietNature) ? &st::emojiNatureActive : nullptr);
	_food->setIconOverride((tab == dbietFood) ? &st::emojiFoodActive : nullptr);
	_activity->setIconOverride((tab == dbietActivity) ? &st::emojiActivityActive : nullptr);
	_travel->setIconOverride((tab == dbietTravel) ? &st::emojiTravelActive : nullptr);
	_objects->setIconOverride((tab == dbietObjects) ? &st::emojiObjectsActive : nullptr);
	_symbols->setIconOverride((tab == dbietSymbols) ? &st::emojiSymbolsActive : nullptr);
}

void EmojiPan::onScrollStickers() {
	auto st = s_scroll->scrollTop();

	updatePanelsPositions(s_panels, st);

	validateSelectedIcon(ValidateIconAnimations::Full);
	if (st + s_scroll->height() > s_scroll->scrollTopMax()) {
		onInlineRequest();
	}

	s_inner->setVisibleTopBottom(st, st + s_scroll->height());
}

void EmojiPan::validateSelectedIcon(ValidateIconAnimations animations) {
	uint64 setId = s_inner->currentSet(s_scroll->scrollTop());
	int32 newSel = 0;
	for (int i = 0, l = _icons.size(); i < l; ++i) {
		if (_icons.at(i).setId == setId) {
			newSel = i;
			break;
		}
	}
	if (newSel != _iconSel) {
		_iconSel = newSel;
		auto iconSelXFinal = newSel * st::emojiCategory.width;
		if (animations == ValidateIconAnimations::Full) {
			_iconSelX.start(iconSelXFinal);
		} else {
			_iconSelX = anim::value(iconSelXFinal, iconSelXFinal);
		}
		auto iconsXFinal = snap((2 * newSel - 7) * int(st::emojiCategory.width) / 2, 0, _iconsMax);
		if (animations == ValidateIconAnimations::None) {
			_iconsX = anim::value(iconsXFinal, iconsXFinal);
			_a_icons.stop();
		} else {
			_iconsX.start(iconsXFinal);
			_iconsStartAnim = getms();
			_a_icons.start();
		}
		updateSelected();
		updateIcons();
	}
}

style::margins EmojiPan::innerPadding() const {
	return st::emojiPanMargins;
}

QRect EmojiPan::innerRect() const {
	return rect().marginsRemoved(innerPadding());
}

QRect EmojiPan::horizontalRect() const {
	return innerRect().marginsRemoved(style::margins(0, st::buttonRadius, 0, st::buttonRadius));
}

QRect EmojiPan::verticalRect() const {
	return innerRect().marginsRemoved(style::margins(st::buttonRadius, 0, st::buttonRadius, 0));
}

void EmojiPan::onSwitch() {
	auto cache = base::take(_cache);
	auto opacityAnimation = base::take(_a_opacity);
	auto showAnimationData = base::take(_showAnimation);
	auto showAnimation = base::take(_a_show);

	showAll();
	auto leftImage = grabForPanelAnimation();
	performSwitch();
	showAll();
	auto rightImage = grabForPanelAnimation();
	if (_emojiShown) {
		std::swap(leftImage, rightImage);
	}

	_a_show = base::take(showAnimation);
	_showAnimation = base::take(showAnimationData);
	_a_opacity = base::take(opacityAnimation);
	_cache = base::take(_cache);

	auto direction = _emojiShown ? SlideAnimation::Direction::LeftToRight : SlideAnimation::Direction::RightToLeft;
	_slideAnimation = std::make_unique<SlideAnimation>();
	auto inner = rect().marginsRemoved(st::emojiPanMargins);
	_slideAnimation->setFinalImages(direction, std::move(leftImage), std::move(rightImage), QRect(inner.topLeft() * cIntRetinaFactor(), inner.size() * cIntRetinaFactor()));
	auto corners = App::cornersMask(ImageRoundRadius::Small);
	_slideAnimation->setCornerMasks(QImage(*corners[0]), QImage(*corners[1]), QImage(*corners[2]), QImage(*corners[3]));
	_slideAnimation->start();

	hideAll();

	if (_emojiShown) {
		s_inner->hideFinish(false);
	} else {
		e_inner->hideFinish();
	}

	_a_slide.start([this] { update(); }, 0., 1., st::emojiPanSlideDuration, anim::linear);
	update();
}

void EmojiPan::performSwitch() {
	_emojiShown = !_emojiShown;
	if (_emojiShown) {
		Notify::clipStopperHidden(ClipStopperSavedGifsPanel);
	} else {
		if (cShowingSavedGifs() && cSavedGifs().isEmpty()) {
			s_inner->showStickerSet(Stickers::DefaultSetId);
		} else if (!cShowingSavedGifs() && !cSavedGifs().isEmpty() && Global::StickerSetsOrder().isEmpty()) {
			s_inner->showStickerSet(Stickers::NoneSetId);
		} else {
			s_inner->updateShowingSavedGifs();
		}
		if (cShowingSavedGifs()) {
			s_inner->showFinish();
		}
		validateSelectedIcon(ValidateIconAnimations::None);
		updateContentHeight();
	}
	_iconOver = -1;
	_a_icons.stop();
}

void EmojiPan::onDisplaySet(quint64 setId) {
	auto &sets = Global::StickerSets();
	auto it = sets.constFind(setId);
	if (it != sets.cend()) {
		_displayingSetId = setId;
		auto box = Ui::show(Box<StickerSetBox>(Stickers::inputSetId(*it)), KeepOtherLayers);
		connect(box, SIGNAL(destroyed(QObject*)), this, SLOT(onDelayedHide()));
	}
}

void EmojiPan::onInstallSet(quint64 setId) {
	auto &sets = Global::StickerSets();
	auto it = sets.constFind(setId);
	if (it != sets.cend()) {
		request(MTPmessages_InstallStickerSet(Stickers::inputSetId(*it), MTP_bool(false))).done([this](const MTPmessages_StickerSetInstallResult &result) {
			if (result.type() == mtpc_messages_stickerSetInstallResultArchive) {
				Stickers::applyArchivedResult(result.c_messages_stickerSetInstallResultArchive());
			}
		}).fail([this, setId](const RPCError &error) {
			s_inner->notInstalledLocally(setId);
			Stickers::undoInstallLocally(setId);
		}).send();

		s_inner->installedLocally(setId);
		Stickers::installLocally(setId);
	}
}

void EmojiPan::onRemoveSet(quint64 setId) {
	auto &sets = Global::StickerSets();
	auto it = sets.constFind(setId);
	if (it != sets.cend() && !(it->flags & MTPDstickerSet::Flag::f_official)) {
		_removingSetId = it->id;
		auto text = lng_stickers_remove_pack(lt_sticker_pack, it->title);
		Ui::show(Box<ConfirmBox>(text, lang(lng_box_remove), base::lambda_guarded(this, [this] {
			Ui::hideLayer();
			auto &sets = Global::RefStickerSets();
			auto it = sets.find(_removingSetId);
			if (it != sets.cend() && !(it->flags & MTPDstickerSet::Flag::f_official)) {
				if (it->id && it->access) {
					request(MTPmessages_UninstallStickerSet(MTP_inputStickerSetID(MTP_long(it->id), MTP_long(it->access)))).send();
				} else if (!it->shortName.isEmpty()) {
					request(MTPmessages_UninstallStickerSet(MTP_inputStickerSetShortName(MTP_string(it->shortName)))).send();
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
				it->flags &= ~MTPDstickerSet::Flag::f_installed;
				if (!(it->flags & MTPDstickerSet_ClientFlag::f_featured) && !(it->flags & MTPDstickerSet_ClientFlag::f_special)) {
					sets.erase(it);
				}
				int removeIndex = Global::StickerSetsOrder().indexOf(_removingSetId);
				if (removeIndex >= 0) Global::RefStickerSetsOrder().removeAt(removeIndex);
				refreshStickers();
				Local::writeInstalledStickers();
				if (writeRecent) Local::writeUserSettings();
			}
			_removingSetId = 0;
			onDelayedHide();
		}), base::lambda_guarded(this, [this] {
			onDelayedHide();
		})));
	}
}

void EmojiPan::onDelayedHide() {
	if (!rect().contains(mapFromGlobal(QCursor::pos()))) {
		_hideTimer.start(3000);
	}
	_removingSetId = 0;
	_displayingSetId = 0;
}

void EmojiPan::clearInlineBot() {
	inlineBotChanged();
	e_switch->updateText();
	e_switch->moveToRight(st::buttonRadius, 0, st::emojiPanWidth);
}

bool EmojiPan::overlaps(const QRect &globalRect) const {
	if (isHidden() || !_cache.isNull()) return false;

	auto testRect = QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size());
	auto inner = rect().marginsRemoved(st::emojiPanMargins);
	return inner.marginsRemoved(QMargins(st::buttonRadius, 0, st::buttonRadius, 0)).contains(testRect)
		|| inner.marginsRemoved(QMargins(0, st::buttonRadius, 0, st::buttonRadius)).contains(testRect);
}

bool EmojiPan::hideOnNoInlineResults() {
	return _inlineBot && inlineResultsShown() && (_shownFromInlineQuery || _inlineBot->username != cInlineGifBotUsername());
}

void EmojiPan::inlineBotChanged() {
	if (!_inlineBot) return;

	if (!isHidden() && !_hiding) {
		if (hideOnNoInlineResults() || !rect().contains(mapFromGlobal(QCursor::pos()))) {
			hideAnimated();
		}
	}

	if (_inlineRequestId) MTP::cancel(_inlineRequestId);
	_inlineRequestId = 0;
	_inlineQuery = _inlineNextQuery = _inlineNextOffset = QString();
	_inlineBot = nullptr;
	_inlineCache.clear();
	s_inner->inlineBotChanged();
	s_inner->hideInlineRowsPanel();

	Notify::inlineBotRequesting(false);
}

void EmojiPan::inlineResultsDone(const MTPmessages_BotResults &result) {
	_inlineRequestId = 0;
	Notify::inlineBotRequesting(false);

	auto it = _inlineCache.find(_inlineQuery);
	auto adding = (it != _inlineCache.cend());
	if (result.type() == mtpc_messages_botResults) {
		auto &d = result.c_messages_botResults();
		auto &v = d.vresults.v;
		auto queryId = d.vquery_id.v;

		if (it == _inlineCache.cend()) {
			it = _inlineCache.emplace(_inlineQuery, std::make_unique<internal::InlineCacheEntry>()).first;
		}
		auto entry = it->second.get();
		entry->nextOffset = qs(d.vnext_offset);
		if (d.has_switch_pm() && d.vswitch_pm.type() == mtpc_inlineBotSwitchPM) {
			auto &switchPm = d.vswitch_pm.c_inlineBotSwitchPM();
			entry->switchPmText = qs(switchPm.vtext);
			entry->switchPmStartToken = qs(switchPm.vstart_param);
		}

		if (auto count = v.size()) {
			entry->results.reserve(entry->results.size() + count);
		}
		auto added = 0;
		for_const (const auto &res, v) {
			if (auto result = InlineBots::Result::create(queryId, res)) {
				++added;
				entry->results.push_back(std::move(result));
			}
		}

		if (!added) {
			entry->nextOffset = QString();
		}
	} else if (adding) {
		it->second->nextOffset = QString();
	}

	if (!showInlineRows(!adding)) {
		it->second->nextOffset = QString();
	}
	onScrollStickers();
}

void EmojiPan::queryInlineBot(UserData *bot, PeerData *peer, QString query) {
	bool force = false;
	_inlineQueryPeer = peer;
	if (bot != _inlineBot) {
		inlineBotChanged();
		_inlineBot = bot;
		force = true;
		//if (_inlineBot->isBotInlineGeo()) {
		//	Ui::show(Box<InformBox>(lang(lng_bot_inline_geo_unavailable)));
		//}
	}
	//if (_inlineBot && _inlineBot->isBotInlineGeo()) {
	//	return;
	//}

	if (_inlineQuery != query || force) {
		if (_inlineRequestId) {
			MTP::cancel(_inlineRequestId);
			_inlineRequestId = 0;
			Notify::inlineBotRequesting(false);
		}
		if (_inlineCache.find(query) != _inlineCache.cend()) {
			_inlineRequestTimer.stop();
			_inlineQuery = _inlineNextQuery = query;
			showInlineRows(true);
		} else {
			_inlineNextQuery = query;
			_inlineRequestTimer.start(InlineBotRequestDelay);
		}
	}
}

void EmojiPan::onInlineRequest() {
	if (_inlineRequestId || !_inlineBot || !_inlineQueryPeer) return;
	_inlineQuery = _inlineNextQuery;

	QString nextOffset;
	auto it = _inlineCache.find(_inlineQuery);
	if (it != _inlineCache.cend()) {
		nextOffset = it->second->nextOffset;
		if (nextOffset.isEmpty()) return;
	}
	Notify::inlineBotRequesting(true);
	_inlineRequestId = request(MTPmessages_GetInlineBotResults(MTP_flags(0), _inlineBot->inputUser, _inlineQueryPeer->input, MTPInputGeoPoint(), MTP_string(_inlineQuery), MTP_string(nextOffset))).done([this](const MTPmessages_BotResults &result, mtpRequestId requestId) {
		inlineResultsDone(result);
	}).fail([this](const RPCError &error) {
		// show error?
		Notify::inlineBotRequesting(false);
		_inlineRequestId = 0;
	}).handleAllErrors().send();
}

void EmojiPan::onEmptyInlineRows() {
	if (_shownFromInlineQuery || hideOnNoInlineResults()) {
		hideAnimated();
		s_inner->clearInlineRowsPanel();
	} else if (!_inlineBot) {
		s_inner->hideInlineRowsPanel();
	} else {
		s_inner->clearInlineRowsPanel();
	}
}

bool EmojiPan::refreshInlineRows(int32 *added) {
	auto it = _inlineCache.find(_inlineQuery);
	const internal::InlineCacheEntry *entry = nullptr;
	if (it != _inlineCache.cend()) {
		if (!it->second->results.empty() || !it->second->switchPmText.isEmpty()) {
			entry = it->second.get();
		}
		_inlineNextOffset = it->second->nextOffset;
	}
	if (!entry) prepareCache();
	auto result = s_inner->refreshInlineRows(_inlineBot, entry, false);
	if (added) *added = result;
	return (entry != nullptr);
}

int32 EmojiPan::showInlineRows(bool newResults) {
	int32 added = 0;
	bool clear = !refreshInlineRows(&added);
	if (newResults) s_scroll->scrollToY(0);

	e_switch->updateText(s_inner->inlineResultsShown() ? _inlineBot->username : QString());
	e_switch->moveToRight(0, 0, st::emojiPanWidth);

	bool hidden = isHidden();
	if (!hidden && !clear) {
		recountContentMaxHeight();
	}
	if (clear) {
		if (!hidden && hideOnNoInlineResults()) {
			hideAnimated();
		} else if (!_hiding) {
			_cache = QPixmap(); // clear after refreshInlineRows()
		}
	} else {
		_hideTimer.stop();
		if (hidden || _hiding) {
			showAnimated(_origin);
		} else if (_emojiShown) {
			onSwitch();
		}
	}

	return added;
}

void EmojiPan::recountContentMaxHeight() {
	if (_shownFromInlineQuery) {
		_contentMaxHeight = qMin(s_inner->countHeight(true), st::emojiPanMaxHeight);
	} else {
		_contentMaxHeight = st::emojiPanMaxHeight;
	}
	updateContentHeight();
}
