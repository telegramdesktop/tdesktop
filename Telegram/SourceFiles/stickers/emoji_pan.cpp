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
#include "ui/widgets/discrete_sliders.h"
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
constexpr auto kSaveChosenTabTimeout = 1000;
constexpr auto kEmojiPanPerRow = Ui::Emoji::kPanPerRow;
constexpr auto kEmojiPanRowsPerPage = Ui::Emoji::kPanRowsPerPage;
constexpr auto kStickerPanPerRow = Stickers::kPanPerRow;

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

BasicPanInner::BasicPanInner(QWidget *parent) : TWidget(parent) {
}

void BasicPanInner::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	auto oldVisibleHeight = getVisibleBottom() - getVisibleTop();
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
	auto visibleHeight = getVisibleBottom() - getVisibleTop();
	if (visibleHeight != oldVisibleHeight) {
		resize(st::emojiPanWidth - st::emojiScroll.width - st::buttonRadius, countHeight());
	}
}

class EmojiPanInner::Controller : public TWidget {
public:
	Controller(gsl::not_null<EmojiPanInner*> parent);

private:
	gsl::not_null<EmojiPanInner*> _pan;

};

EmojiPanInner::Controller::Controller(gsl::not_null<EmojiPanInner*> parent) : TWidget(parent)
, _pan(parent) {
}

EmojiPanInner::EmojiPanInner(QWidget *parent) : BasicPanInner(parent)
, _picker(this) {
	resize(st::emojiPanWidth - st::emojiScroll.width - st::buttonRadius, countHeight());

	setMouseTracking(true);
	setAttribute(Qt::WA_OpaquePaintEvent);

	_picker->hide();

	_esize = Ui::Emoji::Size(Ui::Emoji::Index() + 1);

	for (auto i = 0; i != kEmojiSectionCount; ++i) {
		_counts[i] = Ui::Emoji::GetPackCount(EmojiSectionAtIndex(i));
	}

	_showPickerTimer.setSingleShot(true);
	connect(&_showPickerTimer, SIGNAL(timeout()), this, SLOT(onShowPicker()));
	connect(_picker, SIGNAL(emojiSelected(EmojiPtr)), this, SLOT(onColorSelected(EmojiPtr)));
	connect(_picker, SIGNAL(hidden()), this, SLOT(onPickerHidden()));
}

object_ptr<TWidget> EmojiPanInner::createController() {
	return object_ptr<Controller>(this);
}

template <typename Callback>
bool EmojiPanInner::enumerateSections(Callback callback) const {
	auto info = SectionInfo();
	for (auto i = 0; i != kEmojiSectionCount; ++i) {
		info.section = i;
		info.count = Ui::Emoji::GetPackCount(EmojiSectionAtIndex(i));
		info.rowsCount = (info.count / kEmojiPanPerRow) + ((info.count % kEmojiPanPerRow) ? 1 : 0);
		info.rowsTop = info.top + (i == 0 ? st::emojiPanPadding : st::emojiPanHeader);
		info.rowsBottom = info.rowsTop + info.rowsCount * st::emojiPanSize.height();
		if (!callback(info)) {
			return false;
		}
		info.top = info.rowsBottom;
	}
	return true;
}

EmojiPanInner::SectionInfo EmojiPanInner::sectionInfo(int section) const {
	Expects(section >= 0 && section < kEmojiSectionCount);
	auto result = SectionInfo();
	enumerateSections([searchForSection = section, &result](const SectionInfo &info) {
		if (info.section == searchForSection) {
			result = info;
			return false;
		}
		return true;
	});
	return result;
}

EmojiPanInner::SectionInfo EmojiPanInner::sectionInfoByOffset(int yOffset) const {
	auto result = SectionInfo();
	enumerateSections([&result, yOffset](const SectionInfo &info) {
		if (yOffset < info.rowsBottom || info.section == kEmojiSectionCount - 1) {
			result = info;
			return false;
		}
		return true;
	});
	return result;
}

int EmojiPanInner::countHeight() {
	return sectionInfo(kEmojiSectionCount - 1).top + st::emojiPanPadding;
}

void EmojiPanInner::ensureLoaded(int section) {
	if (!_emoji[section].isEmpty()) {
		return;
	}
	_emoji[section] = Ui::Emoji::GetPack(EmojiSectionAtIndex(section));
	if (EmojiSectionAtIndex(section) == dbiesRecent) {
		return;
	}
	for (auto &emoji : _emoji[section]) {
		if (emoji->hasVariants()) {
			auto j = cEmojiVariants().constFind(emoji->nonColoredId());
			if (j != cEmojiVariants().cend()) {
				emoji = emoji->variant(j.value());
			}
		}
	}
}

void EmojiPanInner::paintEvent(QPaintEvent *e) {
	Painter p(this);
	QRect r = e ? e->rect() : rect();
	if (r != rect()) {
		p.setClipRect(r);
	}
	p.fillRect(r, st::emojiPanBg);

	auto fromColumn = floorclamp(r.x() - st::emojiPanPadding, st::emojiPanSize.width(), 0, kEmojiPanPerRow);
	auto toColumn = ceilclamp(r.x() + r.width() - st::emojiPanPadding, st::emojiPanSize.width(), 0, kEmojiPanPerRow);
	if (rtl()) {
		qSwap(fromColumn, toColumn);
		fromColumn = kEmojiPanPerRow - fromColumn;
		toColumn = kEmojiPanPerRow - toColumn;
	}

	enumerateSections([this, &p, r, fromColumn, toColumn](const SectionInfo &info) {
		if (r.top() >= info.rowsBottom) {
			return true;
		} else if (r.top() + r.height() <= info.top) {
			return false;
		}
		if (info.section > 0 && r.top() < info.rowsTop) {
			p.setFont(st::emojiPanHeaderFont);
			p.setPen(st::emojiPanHeaderFg);
			p.drawTextLeft(st::emojiPanHeaderLeft - st::buttonRadius, info.top + st::emojiPanHeaderTop, width(), lang(LangKey(lng_emoji_category0 + info.section)));
		}
		if (r.top() + r.height() > info.rowsTop) {
			ensureLoaded(info.section);
			auto fromRow = floorclamp(r.y() - info.rowsTop, st::emojiPanSize.height(), 0, info.rowsCount);
			auto toRow = ceilclamp(r.y() + r.height() - info.rowsTop, st::emojiPanSize.height(), 0, info.rowsCount);
			for (auto i = fromRow; i < toRow; ++i) {
				for (auto j = fromColumn; j < toColumn; ++j) {
					auto index = i * kEmojiPanPerRow + j;
					if (index >= info.count) break;

					auto selected = (!_picker->isHidden() && info.section * MatrixRowShift + index == _pickerSel) || (info.section * MatrixRowShift + index == _selected);

					auto w = QPoint(st::emojiPanPadding + j * st::emojiPanSize.width(), info.rowsTop + i * st::emojiPanSize.height());
					if (selected) {
						auto tl = w;
						if (rtl()) tl.setX(width() - tl.x() - st::emojiPanSize.width());
						App::roundRect(p, QRect(tl, st::emojiPanSize), st::emojiPanHover, StickerHoverCorners);
					}
					auto sourceRect = QRect(_emoji[info.section][index]->x() * _esize, _emoji[info.section][index]->y() * _esize, _esize, _esize);
					auto imageLeft = w.x() + (st::emojiPanSize.width() - (_esize / cIntRetinaFactor())) / 2;
					auto imageTop = w.y() + (st::emojiPanSize.height() - (_esize / cIntRetinaFactor())) / 2;
					p.drawPixmapLeft(imageLeft, imageTop, width(), App::emojiLarge(), sourceRect);
				}
			}
		}
		return true;
	});
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
		auto section = (_selected / MatrixRowShift);
		auto sel = _selected % MatrixRowShift;
		if (section < kEmojiSectionCount && sel < _emoji[section].size() && _emoji[section][sel]->hasVariants()) {
			_pickerSel = _selected;
			setCursor(style::cur_default);
			if (!cEmojiVariants().contains(_emoji[section][sel]->nonColoredId())) {
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
			auto section = (_pickerSel / MatrixRowShift);
			auto sel = _pickerSel % MatrixRowShift;
			if (section < kEmojiSectionCount && sel < _emoji[section].size() && _emoji[section][sel]->hasVariants()) {
				if (cEmojiVariants().contains(_emoji[section][sel]->nonColoredId())) {
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

	if (_selected >= kEmojiSectionCount * MatrixRowShift) {
		return;
	}

	auto section = (_selected / MatrixRowShift);
	auto sel = _selected % MatrixRowShift;
	if (sel < _emoji[section].size()) {
		auto emoji = _emoji[section][sel];
		if (emoji->hasVariants() && !_picker->isHidden()) return;

		selectEmoji(emoji);
	}
}

void EmojiPanInner::selectEmoji(EmojiPtr emoji) {
	auto &recent = Ui::Emoji::GetRecent();
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
		while (recent.size() >= kEmojiPanPerRow * kEmojiPanRowsPerPage) recent.pop_back();
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

	auto section = (_pickerSel / MatrixRowShift);
	auto sel = _pickerSel % MatrixRowShift;
	if (section < kEmojiSectionCount && sel < _emoji[section].size() && _emoji[section][sel]->hasVariants()) {
		_picker->showEmoji(_emoji[section][sel]);

		auto y = emojiRect(section, sel).y();
		y -= _picker->height() - st::buttonRadius + getVisibleTop();
		if (y < st::emojiPanHeader) {
			y += _picker->height() - st::buttonRadius + st::emojiPanSize.height() - st::buttonRadius;
		}
		auto xmax = width() - _picker->width();
		auto coef = float64(sel % kEmojiPanPerRow) / float64(kEmojiPanPerRow - 1);
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

QRect EmojiPanInner::emojiRect(int section, int sel) {
	auto info = sectionInfo(section);
	auto countTillItem = (sel - (sel % kEmojiPanPerRow));
	auto rowsToSkip = (countTillItem / kEmojiPanPerRow) + ((countTillItem % kEmojiPanPerRow) ? 1 : 0);
	auto x = st::emojiPanPadding + ((sel % kEmojiPanPerRow) * st::emojiPanSize.width());
	auto y = info.rowsTop + rowsToSkip * st::emojiPanSize.height();
	return QRect(x, y, st::emojiPanSize.width(), st::emojiPanSize.height());
}

void EmojiPanInner::onColorSelected(EmojiPtr emoji) {
	if (emoji->hasVariants()) {
		cRefEmojiVariants().insert(emoji->nonColoredId(), emoji->variantIndex(emoji));
	}
	if (_pickerSel >= 0) {
		auto section = (_pickerSel / MatrixRowShift);
		auto sel = _pickerSel % MatrixRowShift;
		if (section >= 0 && section < kEmojiSectionCount) {
			_emoji[section][sel] = emoji;
			rtlupdate(emojiRect(section, sel));
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

DBIEmojiSection EmojiPanInner::currentSection(int yOffset) const {
	return EmojiSectionAtIndex(sectionInfoByOffset(yOffset).section);
}

void EmojiPanInner::hideFinish(bool completely) {
	if (!_picker->isHidden()) {
		_picker->hideFast();
		_pickerSel = -1;
	}
	clearSelection();
}

void EmojiPanInner::refreshRecent() {
	clearSelection();
	_counts[0] = Ui::Emoji::GetPackCount(dbiesRecent);
	_emoji[0] = Ui::Emoji::GetPack(dbiesRecent);
	auto h = countHeight();
	if (h != height()) {
		resize(width(), h);
		update();
	}
}

bool EmojiPanInner::event(QEvent *e) {
	if (e->type() == QEvent::ParentChange) {
		if (_picker->parentWidget() != parentWidget()) {
			_picker->setParent(parentWidget());
		}
		_picker->raise();
	}
	return BasicPanInner::event(e);
}

void EmojiPanInner::updateSelected() {
	if (_pressedSel >= 0 || _pickerSel >= 0) return;

	auto newSelected = -1;
	auto p = mapFromGlobal(_lastMousePos);
	auto info = sectionInfoByOffset(p.y());
	if (p.y() >= info.rowsTop && p.y() < info.rowsBottom) {
		auto sx = (rtl() ? width() - p.x() : p.x()) - st::emojiPanPadding;
		if (sx >= 0 && sx < kEmojiPanPerRow * st::emojiPanSize.width()) {
			newSelected = qFloor((p.y() - info.rowsTop) / st::emojiPanSize.height()) * kEmojiPanPerRow + qFloor(sx / st::emojiPanSize.width());
			if (newSelected >= _emoji[info.section].size()) {
				newSelected = -1;
			} else {
				newSelected += info.section * MatrixRowShift;
			}
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

void EmojiPanInner::showEmojiSection(DBIEmojiSection section) {
	clearSelection();

	refreshRecent();

	auto y = 0;
	enumerateSections([&y, sectionForSearch = section](const SectionInfo &info) {
		if (EmojiSectionAtIndex(info.section) == sectionForSearch) {
			y = info.top;
			return false;
		}
		return true;
	});
	emit scrollToY(y);

	_lastMousePos = QCursor::pos();

	update();
}

class StickerPanInner::Controller : public TWidget {
public:
	Controller(gsl::not_null<StickerPanInner*> parent);

private:
	gsl::not_null<StickerPanInner*> _pan;

};

StickerPanInner::Controller::Controller(gsl::not_null<StickerPanInner*> parent) : TWidget(parent)
, _pan(parent) {
}

StickerPanInner::StickerPanInner(QWidget *parent, bool gifs) : BasicPanInner(parent)
, _section(gifs ? Section::Gifs : Section::Stickers)
, _addText(lang(lng_stickers_featured_add).toUpper())
, _addWidth(st::stickersTrendingAdd.font->width(_addText))
, _settings(this, lang(lng_stickers_you_have)) {
	resize(st::emojiPanWidth - st::emojiScroll.width - st::buttonRadius, countHeight());

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

object_ptr<TWidget> StickerPanInner::createController() {
	return object_ptr<Controller>(this);
}

void StickerPanInner::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	auto top = getVisibleTop();
	BasicPanInner::setVisibleTopBottom(visibleTop, visibleBottom);
	if (top != getVisibleTop()) {
		_lastScrolled = getms();
	}
	if (_section == Section::Featured) {
		readVisibleSets();
	}
}

void StickerPanInner::readVisibleSets() {
	auto itemsVisibleTop = getVisibleTop();
	auto itemsVisibleBottom = getVisibleBottom();
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
		int count = qMin(set.pack.size(), static_cast<int>(kStickerPanPerRow));
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

template <typename Callback>
bool StickerPanInner::enumerateSections(Callback callback) const {
	Expects(_section == Section::Stickers);
	auto info = SectionInfo();
	for (auto i = 0; i != _mySets.size(); ++i) {
		info.section = i;
		info.count = _mySets[i].pack.size();
		info.rowsCount = (info.count / kStickerPanPerRow) + ((info.count % kStickerPanPerRow) ? 1 : 0);
		info.rowsTop = info.top + (i == 0 ? st::stickerPanPadding : st::emojiPanHeader);
		info.rowsBottom = info.rowsTop + info.rowsCount * st::stickerPanSize.height();
		if (!callback(info)) {
			return false;
		}
		info.top = info.rowsBottom;
	}
	return true;
}

StickerPanInner::SectionInfo StickerPanInner::sectionInfo(int section) const {
	Expects(section >= 0 && section < _mySets.size());
	auto result = SectionInfo();
	enumerateSections([searchForSection = section, &result](const SectionInfo &info) {
		if (info.section == searchForSection) {
			result = info;
			return false;
		}
		return true;
	});
	return result;
}

StickerPanInner::SectionInfo StickerPanInner::sectionInfoByOffset(int yOffset) const {
	auto result = SectionInfo();
	enumerateSections([this, &result, yOffset](const SectionInfo &info) {
		if (yOffset < info.rowsBottom || info.section == _mySets.size() - 1) {
			result = info;
			return false;
		}
		return true;
	});
	return result;
}

int StickerPanInner::countHeight() {
	auto visibleHeight = getVisibleBottom() - getVisibleTop();
	if (visibleHeight <= 0) {
		visibleHeight = st::emojiPanMaxHeight - st::emojiCategory.height;
	}
	auto minimalLastHeight = (visibleHeight - st::stickerPanPadding);
	auto countResult = [this, minimalLastHeight] {
		if (showingInlineItems()) {
			auto result = st::stickerPanPadding;
			if (_switchPmButton) {
				result += _switchPmButton->height() + st::inlineResultsSkip;
			}
			for (int i = 0, l = _inlineRows.count(); i < l; ++i) {
				result += _inlineRows[i].height;
			}
			return result;
		} else if (_section == Section::Featured) {
			return st::stickerPanPadding + shownSets().size() * featuredRowHeight();
		} else if (!shownSets().empty()) {
			auto info = sectionInfo(shownSets().size() - 1);
			return info.top + qMax(info.rowsBottom - info.top, minimalLastHeight);
		}
		return 0;
	};
	return qMax(minimalLastHeight, countResult()) + st::stickerPanPadding;
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

QRect StickerPanInner::stickerRect(int section, int sel) {
	int x = 0, y = 0;
	if (_section == Section::Featured) {
		x = stickersLeft() + (sel * st::stickerPanSize.width());
		y = st::stickerPanPadding + (section * featuredRowHeight()) + st::stickersTrendingHeader;
	} else if (_section == Section::Stickers) {
		auto info = sectionInfo(section);
		if (sel >= _mySets[section].pack.size()) {
			sel -= _mySets[section].pack.size();
		}
		auto countTillItem = (sel - (sel % kStickerPanPerRow));
		auto rowsToSkip = (countTillItem / kStickerPanPerRow) + ((countTillItem % kStickerPanPerRow) ? 1 : 0);
		x = stickersLeft() + ((sel % kStickerPanPerRow) * st::stickerPanSize.width());
		y = info.rowsTop + rowsToSkip * st::stickerPanSize.height();
	}
	return QRect(x, y, st::stickerPanSize.width(), st::stickerPanSize.height());
}

void StickerPanInner::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto clip = e->rect();
	p.fillRect(clip, st::emojiPanBg);

	if (showingInlineItems()) {
		paintInlineItems(p, clip);
	} else if (_section == Section::Featured) {
		paintFeaturedStickers(p, clip);
	} else {
		paintStickers(p, clip);
	}
}

void StickerPanInner::paintInlineItems(Painter &p, QRect clip) {
	if (_inlineRows.isEmpty() && !_switchPmButton) {
		p.setFont(st::normalFont);
		p.setPen(st::noContactsColor);
		p.drawText(QRect(0, 0, width(), (height() / 3) * 2 + st::normalFont->height), lang(lng_inline_bot_no_results), style::al_center);
		return;
	}
	auto gifPaused = Ui::isLayerShown() || Ui::isMediaViewShown() || _previewShown || !App::wnd()->isActive();
	InlineBots::Layout::PaintContext context(getms(), false, gifPaused, false);

	auto top = st::stickerPanPadding;
	if (_switchPmButton) {
		top += _switchPmButton->height() + st::inlineResultsSkip;
	}

	auto fromx = rtl() ? (width() - clip.x() - clip.width()) : clip.x();
	auto tox = rtl() ? (width() - clip.x()) : (clip.x() + clip.width());
	for (auto row = 0, rows = _inlineRows.size(); row != rows; ++row) {
		auto &inlineRow = _inlineRows[row];
		if (top >= clip.top() + clip.height()) {
			break;
		}
		if (top + inlineRow.height > clip.top()) {
			auto left = st::inlineResultsLeft - st::buttonRadius;
			if (row == rows - 1) context.lastRow = true;
			for (int col = 0, cols = inlineRow.items.size(); col < cols; ++col) {
				if (left >= tox) break;

				auto item = inlineRow.items.at(col);
				auto w = item->width();
				if (left + w > fromx) {
					p.translate(left, top);
					item->paint(p, clip.translated(-left, -top), &context);
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

void StickerPanInner::paintFeaturedStickers(Painter &p, QRect clip) {
	auto fromColumn = floorclamp(clip.x() - stickersLeft(), st::stickerPanSize.width(), 0, kStickerPanPerRow);
	auto toColumn = ceilclamp(clip.x() + clip.width() - stickersLeft(), st::stickerPanSize.width(), 0, kStickerPanPerRow);
	if (rtl()) {
		qSwap(fromColumn, toColumn);
		fromColumn = kStickerPanPerRow - fromColumn;
		toColumn = kStickerPanPerRow - toColumn;
	}

	auto &sets = shownSets();
	auto selsection = (_selected >= 0) ? (_selected / MatrixRowShift) : -1;
	auto selindex = (selsection >= 0) ? (_selected % MatrixRowShift) : -1;
	auto seldelete = false;
	if (selsection >= sets.size()) {
		selsection = -1;
	} else if (selsection >= 0 && selindex >= sets[selsection].pack.size()) {
		selindex -= sets[selsection].pack.size();
		seldelete = true;
	}

	auto tilly = st::stickerPanPadding;
	auto ms = getms();
	for (auto c = 0, l = sets.size(); c != l; ++c) {
		auto y = tilly;
		auto &set = sets[c];
		tilly = y + featuredRowHeight();
		if (clip.top() >= tilly) continue;
		if (y >= clip.y() + clip.height()) break;

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
		if (y >= clip.y() + clip.height()) break;

		for (int j = fromColumn; j < toColumn; ++j) {
			int index = j;
			if (index >= size) break;

			auto selected = (selsection == c && selindex == index);
			auto deleteSelected = selected && seldelete;
			paintSticker(p, set, y, index, selected, deleteSelected);
		}
	}
}

void StickerPanInner::paintStickers(Painter &p, QRect clip) {
	auto fromColumn = floorclamp(clip.x() - stickersLeft(), st::stickerPanSize.width(), 0, kStickerPanPerRow);
	auto toColumn = ceilclamp(clip.x() + clip.width() - stickersLeft(), st::stickerPanSize.width(), 0, kStickerPanPerRow);
	if (rtl()) {
		qSwap(fromColumn, toColumn);
		fromColumn = kStickerPanPerRow - fromColumn;
		toColumn = kStickerPanPerRow - toColumn;
	}

	auto &sets = shownSets();
	auto selsection = (_selected >= 0) ? (_selected / MatrixRowShift) : -1;
	auto selindex = (selsection >= 0) ? (_selected % MatrixRowShift) : -1;
	auto seldelete = false;
	if (selsection >= sets.size()) {
		selsection = -1;
	} else if (selsection >= 0 && selindex >= sets[selsection].pack.size()) {
		selindex -= sets[selsection].pack.size();
		seldelete = true;
	}
	enumerateSections([this, &p, clip, fromColumn, toColumn, selsection, selindex, seldelete](const SectionInfo &info) {
		if (clip.top() >= info.rowsBottom) {
			return true;
		} else if (clip.top() + clip.height() <= info.top) {
			return false;
		}
		auto &set = _mySets[info.section];
		if (info.section > 0 && clip.top() < info.rowsTop) {
			// TODO delete button, elided text
			p.setFont(st::emojiPanHeaderFont);
			p.setPen(st::emojiPanHeaderFg);
			p.drawTextLeft(st::emojiPanHeaderLeft - st::buttonRadius, info.top + st::emojiPanHeaderTop, width(), set.title);
		}
		if (clip.top() + clip.height() > info.rowsTop) {
			auto special = (set.flags & MTPDstickerSet::Flag::f_official) != 0;
			auto fromRow = floorclamp(clip.y() - info.rowsTop, st::stickerPanSize.height(), 0, info.rowsCount);
			auto toRow = ceilclamp(clip.y() + clip.height() - info.rowsTop, st::stickerPanSize.height(), 0, info.rowsCount);
			for (int i = fromRow; i < toRow; ++i) {
				for (int j = fromColumn; j < toColumn; ++j) {
					int index = i * kStickerPanPerRow + j;
					if (index >= info.count) break;

					auto selected = (selsection == info.section && selindex == index);
					auto deleteSelected = selected && seldelete;
					paintSticker(p, set, info.rowsTop, index, selected, deleteSelected);
				}
			}
		}
		return true;
	});
}

void StickerPanInner::paintSticker(Painter &p, Set &set, int y, int index, bool selected, bool deleteSelected) {
	auto sticker = set.pack[index];
	if (!sticker->sticker()) return;

	int row = (index / kStickerPanPerRow), col = (index % kStickerPanPerRow);

	auto pos = QPoint(stickersLeft() + col * st::stickerPanSize.width(), y + row * st::stickerPanSize.height());
	if (selected) {
		auto tl = pos;
		if (rtl()) tl.setX(width() - tl.x() - st::stickerPanSize.width());
		App::roundRect(p, QRect(tl, st::stickerPanSize), st::emojiPanHover, StickerHoverCorners);
	}

	auto goodThumb = !sticker->thumb->isNull() && ((sticker->thumb->width() >= 128) || (sticker->thumb->height() >= 128));
	if (goodThumb) {
		sticker->thumb->load();
	} else {
		sticker->checkSticker();
	}

	auto coef = qMin((st::stickerPanSize.width() - st::buttonRadius * 2) / float64(sticker->dimensions.width()), (st::stickerPanSize.height() - st::buttonRadius * 2) / float64(sticker->dimensions.height()));
	if (coef > 1) coef = 1;
	auto w = qMax(qRound(coef * sticker->dimensions.width()), 1);
	auto h = qMax(qRound(coef * sticker->dimensions.height()), 1);
	auto ppos = pos + QPoint((st::stickerPanSize.width() - w) / 2, (st::stickerPanSize.height() - h) / 2);
	if (goodThumb) {
		p.drawPixmapLeft(ppos, width(), sticker->thumb->pix(w, h));
	} else if (!sticker->sticker()->img->isNull()) {
		p.drawPixmapLeft(ppos, width(), sticker->sticker()->img->pix(w, h));
	}

	if (selected && set.id == Stickers::RecentSetId && _custom.at(index)) {
		auto xPos = pos + QPoint(st::stickerPanSize.width() - st::stickerPanDelete.width(), 0);
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
	return stickersLeft() + (kStickerPanPerRow * st::stickerPanSize.width());
}

QRect StickerPanInner::featuredAddRect(int index) const {
	int addw = _addWidth - st::stickersTrendingAdd.width;
	int addh = st::stickersTrendingAdd.height;
	int addx = featuredContentWidth() - addw;
	int addy = st::stickerPanPadding + index * featuredRowHeight() + st::stickersTrendingAddTop;
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
		auto section = (_selected / MatrixRowShift);
		auto sel = _selected % MatrixRowShift;
		if (sets[section].id == Stickers::RecentSetId && sel >= sets[section].pack.size() && sel < sets[section].pack.size() * 2 && _custom.at(sel - sets[section].pack.size())) {
			removeRecentSticker(section, sel - sets[section].pack.size());
			return;
		}
		if (sel < sets[section].pack.size()) {
			emit selected(sets[section].pack[sel]);
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

void StickerPanInner::removeRecentSticker(int section, int index) {
	if (_section != Section::Stickers || section >= _mySets.size() || _mySets[section].id != Stickers::RecentSetId) {
		return;
	}

	clearSelection();
	bool refresh = false;
	auto sticker = _mySets[section].pack[index];
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

void StickerPanInner::resizeEvent(QResizeEvent *e) {
	_settings->moveToLeft((st::emojiPanWidth - _settings->width()) / 2, height() / 3);
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
	clearSelection();
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
		if (auto layout = InlineItem::createLayoutGif(this, doc)) {
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
		if (auto layout = InlineItem::createLayout(this, result, _inlineWithThumb)) {
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
		for (auto row = 0, rows = _inlineRows.size(); row != rows; ++row) {
			for (auto col = 0, cols = _inlineRows[row].items.size(); col != cols; ++col) {
				_inlineRows[row].items[col]->preload();
			}
		}
		return;
	}

	auto &sets = shownSets();
	for (int i = 0, l = sets.size(), k = 0; i < l; ++i) {
		int count = sets[i].pack.size();
		if (_section == Section::Featured) {
			accumulate_min(count, kStickerPanPerRow);
		}
		for (int j = 0; j != count; ++j) {
			if (++k > kStickerPanPerRow * (kStickerPanPerRow + 1)) break;

			auto sticker = sets.at(i).pack.at(j);
			if (!sticker || !sticker->sticker()) continue;

			bool goodThumb = !sticker->thumb->isNull() && ((sticker->thumb->width() >= 128) || (sticker->thumb->height() >= 128));
			if (goodThumb) {
				sticker->thumb->load();
			} else {
				sticker->automaticLoad(0);
			}
		}
		if (k > kStickerPanPerRow * (kStickerPanPerRow + 1)) break;
	}
}

uint64 StickerPanInner::currentSet(int yOffset) const {
	if (showingInlineItems()) {
		return Stickers::NoneSetId;
	} else if (_section == Section::Featured) {
		return Stickers::FeaturedSetId;
	}
	return _mySets.isEmpty() ? Stickers::RecentSetId : _mySets[sectionInfoByOffset(yOffset).section].id;
}

void StickerPanInner::hideInlineRowsPanel() {
	clearInlineRows(false);
	if (showingInlineItems()) {
		_section = Section::Gifs;
		refreshSavedGifs();
		emit scrollToY(0);
		emit scrollUpdated();
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
		auto buttonTop = st::stickerPanPadding;
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
			if (!_inlineBot) {
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

void StickerPanInner::inlineItemLayoutChanged(const InlineItem *layout) {
	if (_selected < 0 || !showingInlineItems() || !isVisible()) {
		return;
	}

	int row = _selected / MatrixRowShift, col = _selected % MatrixRowShift;
	if (row < _inlineRows.size() && col < _inlineRows.at(row).items.size()) {
		if (layout == _inlineRows.at(row).items.at(col)) {
			updateSelected();
		}
	}
}

void StickerPanInner::inlineItemRepaint(const InlineItem *layout) {
	auto ms = getms();
	if (_lastScrolled + 100 <= ms) {
		update();
	} else {
		_updateInlineItems.start(_lastScrolled + 100 - ms);
	}
}

bool StickerPanInner::inlineItemVisible(const InlineItem *layout) {
	auto position = layout->position();
	if (!showingInlineItems() || position < 0 || !isVisible()) {
		return false;
	}

	auto row = position / MatrixRowShift;
	auto col = position % MatrixRowShift;
	t_assert((row < _inlineRows.size()) && (col < _inlineRows[row].items.size()));

	auto &inlineItems = _inlineRows[row].items;
	auto top = 0;
	for (auto i = 0; i != row; ++i) {
		top += _inlineRows[i].height;
	}

	return (top < getVisibleBottom()) && (top + _inlineRows[row].items[col]->height() > getVisibleTop());
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
			update();
		}

		updateSelected();
	}
}

void StickerPanInner::fillIcons(QList<StickerIcon> &icons) {
	icons.clear();
	icons.reserve(_mySets.size() + 1);
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

void StickerPanInner::updateSelected() {
	if (_pressed >= 0 && !_previewShown) {
		return;
	}

	auto newSelected = -1;
	auto p = mapFromGlobal(_lastMousePos);

	if (showingInlineItems()) {
		int sx = (rtl() ? width() - p.x() : p.x()) - (st::inlineResultsLeft - st::buttonRadius);
		int sy = p.y() - st::stickerPanPadding;
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
				_inlineRows[srow].items[scol]->update();
			}
			_selected = sel;
			if (row >= 0 && col >= 0) {
				t_assert(row >= 0 && row < _inlineRows.size() && col >= 0 && col < _inlineRows.at(row).items.size());
				_inlineRows[row].items[col]->update();
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
	auto &sets = shownSets();
	int sx = (rtl() ? width() - p.x() : p.x()) - stickersLeft();
	if (_section == Section::Featured) {
		auto yOffset = p.y() - st::stickerPanPadding;
		auto section = (yOffset >= 0) ? (yOffset / featuredRowHeight()) : -1;
		if (section >= 0 && section < sets.size()) {
			yOffset -= section * featuredRowHeight();

			auto &set = sets[section];
			if (yOffset < st::stickersTrendingHeader) {
				if (featuredHasAddButton(section) && myrtlrect(featuredAddRect(section)).contains(p.x(), p.y())) {
					newSelectedFeaturedSetAdd = section;
				} else {
					newSelectedFeaturedSet = section;
				}
			} else if (yOffset >= st::stickersTrendingHeader  && yOffset < st::stickersTrendingHeader + st::stickerPanSize.height()) {
				if (sx >= 0 && sx < kStickerPanPerRow * st::stickerPanSize.width()) {
					newSelected = qFloor(sx / st::stickerPanSize.width());
					if (newSelected >= set.pack.size()) {
						newSelected = -1;
					} else {
						newSelected += section * MatrixRowShift;
					}
				}
			}
		}
	} else if (!_mySets.empty()) {
		auto info = sectionInfoByOffset(p.y());
		if (p.y() >= info.top && p.y() < info.rowsTop) {
			// TODO selected header / delete
		} else if (p.y() >= info.rowsTop && p.y() < info.rowsBottom) {
			auto yOffset = p.y() - info.rowsTop;
			auto &set = sets[info.section];
			auto special = ((set.flags & MTPDstickerSet::Flag::f_official) != 0);
			auto rowIndex = qFloor(yOffset / st::stickerPanSize.height());
			newSelected = rowIndex * kStickerPanPerRow + qFloor(sx / st::stickerPanSize.width());
			if (newSelected >= set.pack.size()) {
				newSelected = -1;
			} else {
				if (set.id == Stickers::RecentSetId && _custom[newSelected]) {
					auto inx = sx - (newSelected % kStickerPanPerRow) * st::stickerPanSize.width();
					auto iny = yOffset - ((newSelected / kStickerPanPerRow) * st::stickerPanSize.height());
					if (inx >= st::stickerPanSize.width() - st::stickerPanDelete.width() && iny < st::stickerPanDelete.height()) {
						newSelected += set.pack.size();
					}
				}
				newSelected += info.section * MatrixRowShift;
			}
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
			auto section = _selected / MatrixRowShift;
			auto sel = _selected % MatrixRowShift;
			if (section < sets.size() && sel >= sets[section].pack.size()) {
				sel -= sets[section].pack.size();
			}
			rtlupdate(stickerRect(section, sel));
		};
		updateSelected();
		_selected = newSelected;
		updateSelected();

		if (_previewShown && _selected >= 0 && _pressed != _selected) {
			_pressed = _selected;
			auto section = _selected / MatrixRowShift;
			auto sel = _selected % MatrixRowShift;
			if (section < sets.size() && sel < sets[section].pack.size()) {
				Ui::showMediaPreview(sets[section].pack[sel]);
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
			auto section = (_pressed / MatrixRowShift);
			auto sel = _pressed % MatrixRowShift;
			if (sel < sets[section].pack.size()) {
				Ui::showMediaPreview(sets[section].pack[sel]);
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
		refreshSavedGifs();
		emit scrollToY(0);
		emit scrollUpdated();
		return;
	}

	if (showingInlineItems()) {
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

	auto needRefresh = (_section != Section::Stickers);
	if (needRefresh) {
		_section = Section::Stickers;
		refreshRecentStickers(true);
	}

	auto y = 0;
	enumerateSections([this, setId, &y](const SectionInfo &info) {
		if (_mySets[info.section].id == setId) {
			y = info.top;
			return false;
		}
		return true;
	});
	emit scrollToY(y);
	emit scrollUpdated();

	if (needRefresh) {
		emit refreshIcons(kRefreshIconsScrollAnimation);
	}

	_lastMousePos = QCursor::pos();

	update();
}

} // namespace internal

namespace {

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
	//paintCorner(_topLeft, _innerLeft, _innerTop);
	//paintCorner(_topRight, _innerRight - _topRight.width, _innerTop);
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

EmojiPan::Tab::Tab(TabType type, object_ptr<internal::BasicPanInner> widget)
: _type(type)
, _widget(std::move(widget))
, _weak(_widget) {
}

object_ptr<internal::BasicPanInner> EmojiPan::Tab::takeWidget() {
	return std::move(_widget);
}

void EmojiPan::Tab::returnWidget(object_ptr<internal::BasicPanInner> widget) {
	_widget = std::move(widget);
	Ensures(_widget == _weak);
}

void EmojiPan::Tab::saveScrollTop() {
	_scrollTop = widget()->getVisibleTop();
}

EmojiPan::EmojiPan(QWidget *parent) : TWidget(parent)
, _recent(this, st::emojiCategoryRecent)
, _people(this, st::emojiCategoryPeople)
, _nature(this, st::emojiCategoryNature)
, _food(this, st::emojiCategoryFood)
, _activity(this, st::emojiCategoryActivity)
, _travel(this, st::emojiCategoryTravel)
, _objects(this, st::emojiCategoryObjects)
, _symbols(this, st::emojiCategorySymbols)
, _a_icons(animation(this, &EmojiPan::step_icons))
, _scroll(this, st::emojiScroll)
, _tabsSlider(this, st::emojiTabs)
, _topShadow(this, st::shadowFg)
, _bottomShadow(this, st::shadowFg)
, _tabs {
	Tab { TabType::Emoji, object_ptr<internal::EmojiPanInner>(this) },
	Tab { TabType::Stickers, object_ptr<internal::StickerPanInner>(this, false) },
	Tab { TabType::Gifs, object_ptr<internal::StickerPanInner>(this, true) },
}
, _currentTabType(AuthSession::Current().data().emojiPanTab()) {
	resize(QRect(0, 0, st::emojiPanWidth, st::emojiPanMaxHeight).marginsAdded(innerPadding()).size());
	_width = width();
	_height = height();

	createTabsSlider();

	_contentMaxHeight = st::emojiPanMaxHeight - marginTop() - marginBottom();
	_contentHeight = _contentMaxHeight;

	_scroll->resize(st::emojiPanWidth - st::buttonRadius, _contentHeight);
	_scroll->move(verticalRect().x(), verticalRect().y() + marginTop());
	setWidgetToScrollArea();

	_bottomShadow->setGeometry(_tabsSlider->x(), _scroll->y() + _scroll->height() - st::lineWidth, _tabsSlider->width(), st::lineWidth);

	int32 left = _iconsLeft = innerRect().x() + (st::emojiPanWidth - 8 * st::emojiCategory.width) / 2;
	int32 top = _iconsTop = innerRect().y() + innerRect().height() - st::emojiCategory.height;
	prepareSection(left, top, _width, _recent, dbiesRecent);
	prepareSection(left, top, _width, _people, dbiesPeople);
	prepareSection(left, top, _width, _nature, dbiesNature);
	prepareSection(left, top, _width, _food, dbiesFood);
	prepareSection(left, top, _width, _activity, dbiesActivity);
	prepareSection(left, top, _width, _travel, dbiesTravel);
	prepareSection(left, top, _width, _objects, dbiesObjects);
	prepareSection(left, top, _width, _symbols, dbiesSymbols);

	setCurrentSectionIcon(dbiesRecent);

	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(hideByTimerOrLeave()));

	for (auto &tab : _tabs) {
		connect(tab.widget(), &internal::BasicPanInner::scrollToY, this, [this, tab = &tab](int y) {
			if (tab == currentTab()) {
				_scroll->scrollToY(y);
			} else {
				tab->saveScrollTop(y);
			}
		});
		connect(tab.widget(), &internal::BasicPanInner::disableScroll, this, [this, tab = &tab](bool disabled) {
			if (tab == currentTab()) {
				_scroll->disableScroll(disabled);
			}
		});
		connect(tab.widget(), SIGNAL(saveConfigDelayed(int)), this, SLOT(onSaveConfigDelayed(int)));
	}

	connect(stickers(), SIGNAL(scrollUpdated()), this, SLOT(onScroll()));
	connect(_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));
	connect(emoji(), SIGNAL(selected(EmojiPtr)), this, SIGNAL(emojiSelected(EmojiPtr)));
	connect(stickers(), SIGNAL(selected(DocumentData*)), this, SIGNAL(stickerSelected(DocumentData*)));
	connect(gifs(), SIGNAL(selected(DocumentData*)), this, SIGNAL(stickerSelected(DocumentData*)));
	connect(gifs(), SIGNAL(selected(PhotoData*)), this, SIGNAL(photoSelected(PhotoData*)));
	connect(gifs(), SIGNAL(selected(InlineBots::Result*, UserData*)), this, SIGNAL(inlineResultSelected(InlineBots::Result*, UserData*)));

	connect(gifs(), SIGNAL(emptyInlineRows()), this, SLOT(onEmptyInlineRows()));

	connect(stickers(), SIGNAL(displaySet(quint64)), this, SLOT(onDisplaySet(quint64)));
	connect(stickers(), SIGNAL(installSet(quint64)), this, SLOT(onInstallSet(quint64)));
	connect(stickers(), SIGNAL(removeSet(quint64)), this, SLOT(onRemoveSet(quint64)));
	connect(stickers(), SIGNAL(refreshIcons(bool)), this, SLOT(onRefreshIcons(bool)));

	_saveConfigTimer.setSingleShot(true);
	connect(&_saveConfigTimer, SIGNAL(timeout()), this, SLOT(onSaveConfig()));

	// inline bots
	_inlineRequestTimer.setSingleShot(true);
	connect(&_inlineRequestTimer, SIGNAL(timeout()), this, SLOT(onInlineRequest()));

	if (cPlatform() == dbipMac || cPlatform() == dbipMacOld) {
		connect(App::wnd()->windowHandle(), SIGNAL(activeChanged()), this, SLOT(onWndActiveChanged()));
	}

	_topShadow->raise();
	_bottomShadow->raise();
	_tabsSlider->raise();

	setMouseTracking(true);
//	setAttribute(Qt::WA_AcceptTouchEvents);
	setAttribute(Qt::WA_OpaquePaintEvent, false);

	hideChildren();
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
	auto maxContentHeight = wantedBottom - st::emojiPanMargins.top() - st::emojiPanMargins.bottom() - marginTop() - marginBottom();
	auto contentHeight = qMin(_contentMaxHeight, maxContentHeight);
	auto resultTop = wantedBottom - st::emojiPanMargins.bottom() - marginBottom() - contentHeight - marginTop() - st::emojiPanMargins.top();
	accumulate_max(resultTop, _minTop);
	if (contentHeight == _contentHeight) {
		move(x(), resultTop);
		return;
	}

	auto was = _contentHeight;
	_contentHeight = contentHeight;

	resize(QRect(0, 0, innerRect().width(), marginTop() + _contentHeight + marginBottom()).marginsAdded(innerPadding()).size());
	_height = height();
	move(x(), resultTop);

	if (was > _contentHeight) {
		_scroll->resize(_scroll->width(), _contentHeight);
		auto scrollTop = _scroll->scrollTop();
		currentTab()->widget()->setVisibleTopBottom(scrollTop, scrollTop + _contentHeight);
	} else {
		auto scrollTop = _scroll->scrollTop();
		currentTab()->widget()->setVisibleTopBottom(scrollTop, scrollTop + _contentHeight);
		_scroll->resize(_scroll->width(), _contentHeight);
	}
	_bottomShadow->setGeometry(_tabsSlider->x(), _scroll->y() + _scroll->height() - st::lineWidth, _tabsSlider->width(), st::lineWidth);

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

void EmojiPan::prepareSection(int &left, int top, int _width, Ui::IconButton *sectionIcon, DBIEmojiSection value) {
	sectionIcon->moveToLeft(left, top, _width);
	left += sectionIcon->width();
	sectionIcon->setClickedCallback([this, value] { setActiveSection(value); });
}

void EmojiPan::onWndActiveChanged() {
	if (!App::wnd()->windowHandle()->isActive() && !isHidden()) {
		leaveEvent(0);
	}
}

void EmojiPan::onSaveConfig() {
	Local::writeUserSettings();
}

void EmojiPan::onSaveConfigDelayed(int delay) {
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
	} else if (opacityAnimating) {
		p.setOpacity(_a_opacity.current(_hiding ? 0. : 1.));
		p.drawPixmap(0, 0, _cache);
	} else if (_hiding || isHidden()) {
		hideFinished();
	} else if (switching) {
		paintSlideFrame(p, ms);
		if (!_a_slide.animating()) {
			_slideAnimation.reset();
			if (!opacityAnimating) {
				showAll();
			}
			InvokeQueued(this, [this] {
				if (_hideAfterSlide && !_a_slide.animating()) {
					startOpacityAnimation(true);
				}
			});
		}
	} else {
		if (!_cache.isNull()) _cache = QPixmap();
		if (!_inComplrexGrab) Ui::Shadow::paint(p, innerRect(), width(), st::emojiPanAnimation.shadow);
		paintContent(p);
	}
}

void EmojiPan::paintSlideFrame(Painter &p, TimeMs ms) {
	Ui::Shadow::paint(p, innerRect(), width(), st::emojiPanAnimation.shadow);

	auto inner = innerRect();
	auto topPart = QRect(inner.x(), inner.y(), inner.width(), _tabsSlider->height() + st::buttonRadius);
	App::roundRect(p, topPart, st::emojiPanBg, ImageRoundRadius::Small, App::RectPart::TopFull | App::RectPart::NoTopBottom);

	auto slideDt = _a_slide.current(ms, 1.);
	_slideAnimation->paintFrame(p, slideDt, _a_opacity.current(_hiding ? 0. : 1.));
}

void EmojiPan::paintContent(Painter &p) {
	auto inner = innerRect();
	auto topPart = QRect(inner.x(), inner.y(), inner.width(), _tabsSlider->height() + st::buttonRadius);
	App::roundRect(p, topPart, st::emojiPanBg, ImageRoundRadius::Small, App::RectPart::TopFull | App::RectPart::NoTopBottom);

	auto showSectionIcons = (_currentTabType != TabType::Gifs);
	auto bottomPart = QRect(inner.x(), _iconsTop - st::buttonRadius, inner.width(), st::emojiCategory.height + st::buttonRadius);
	auto &bottomBg = showSectionIcons ? st::emojiPanCategories : st::emojiPanBg;
	auto bottomParts = App::RectPart::NoTopBottom | App::RectPart::BottomFull;
	App::roundRect(p, bottomPart, bottomBg, ImageRoundRadius::Small, bottomParts);

	auto horizontal = horizontalRect();
	auto sidesTop = horizontal.y();
	auto sidesHeight = _scroll->y() + _scroll->height() - sidesTop;
	p.fillRect(myrtlrect(inner.x() + inner.width() - st::emojiScroll.width, sidesTop, st::emojiScroll.width, sidesHeight), st::emojiPanBg);
	p.fillRect(myrtlrect(inner.x(), sidesTop, st::buttonRadius, sidesHeight), st::emojiPanBg);

	switch (_currentTabType) {
	case TabType::Emoji: {
		auto vertical = verticalRect();
		p.fillRect(vertical.x(), _iconsTop, vertical.width(), st::emojiCategory.height - st::buttonRadius, st::emojiPanCategories);
	} break;

	case TabType::Stickers: {
		paintStickerSettingsIcon(p);

		if (!_icons.isEmpty()) {
			auto x = _iconsLeft;
			auto selxrel = _iconsLeft + qRound(_iconSelX.current());
			auto selx = selxrel - qRound(_iconsX.current());

			QRect clip(x, _iconsTop, _iconsLeft + 7 * st::emojiCategory.width - x, st::emojiCategory.height);
			if (rtl()) clip.moveLeft(width() - x - clip.width());
			p.setClipRect(clip);

			auto getSpecialSetIcon = [](uint64 setId, bool active) {
				if (setId == Stickers::FeaturedSetId) {
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
	} break;

	case TabType::Gifs: {
		p.fillRect(myrtlrect(inner.x() + inner.width() - st::emojiScroll.width, _iconsTop, st::emojiScroll.width, st::emojiCategory.height - st::buttonRadius), st::emojiPanBg);
		p.fillRect(myrtlrect(inner.x(), _iconsTop, st::buttonRadius, st::emojiCategory.height - st::buttonRadius), st::emojiPanBg);
	} break;

	default: Unexpected("Bad tab type.");
	}
}

int EmojiPan::marginTop() const {
	return _tabsSlider->height() - st::lineWidth;
}

int EmojiPan::marginBottom() const {
	return st::emojiCategory.height;
}

int EmojiPan::countBottom() const {
	return (parentWidget()->height() - _minBottom);
}

void EmojiPan::moveByBottom() {
	moveToRight(0, y());
	updateContentHeight();
}

void EmojiPan::enterEventHook(QEvent *e) {
	showAnimated();
}

bool EmojiPan::preventAutoHide() const {
	return _removingSetId || _displayingSetId;
}

void EmojiPan::leaveEventHook(QEvent *e) {
	if (preventAutoHide()) {
		return;
	}
	auto ms = getms();
	if (_a_show.animating(ms) || _a_opacity.animating(ms)) {
		hideAnimated();
	} else {
		_hideTimer.start(300);
	}
	return TWidget::leaveEventHook(e);
}

void EmojiPan::otherEnter() {
	showAnimated();
}

void EmojiPan::otherLeave() {
	if (preventAutoHide()) {
		return;
	}

	auto ms = getms();
	if (_a_opacity.animating(ms)) {
		hideByTimerOrLeave();
	} else {
		_hideTimer.start(0);
	}
}

void EmojiPan::mousePressEvent(QMouseEvent *e) {
	if (_currentTabType != TabType::Stickers || e->button() != Qt::LeftButton) {
		return;
	}
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
	if (_currentTabType != TabType::Stickers) {
		return;
	}
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
	if (_currentTabType != TabType::Stickers || _icons.isEmpty()) {
		return;
	}

	auto wasDown = _iconDown;
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
			stickers()->showStickerSet(_icons[_iconOver].setId);
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
	stickers()->refreshStickers();
	if (_currentTabType != TabType::Stickers) {
		stickers()->preloadImages();
	}
	update();
}

void EmojiPan::refreshSavedGifs() {
	gifs()->refreshSavedGifs();
	if (_currentTabType != TabType::Gifs) {
		gifs()->preloadImages();
	}
}

void EmojiPan::onRefreshIcons(bool scrollAnimation) {
	_iconOver = -1;
	stickers()->fillIcons(_icons);
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
	updateSelected();
	if (_currentTabType == TabType::Stickers) {
		validateSelectedIcon(scrollAnimation ? ValidateIconAnimations::Scroll : ValidateIconAnimations::None);
		updateContentHeight();
	}
	updateIcons();
}

void EmojiPan::leaveToChildEvent(QEvent *e, QWidget *child) {
	if (_currentTabType != TabType::Stickers) {
		return;
	}
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
	if (_currentTabType != TabType::Stickers) {
		return;
	}

	auto verticalInner = rect().marginsRemoved(st::emojiPanMargins).marginsRemoved(QMargins(st::buttonRadius, 0, st::buttonRadius, 0));
	update(verticalInner.left(), _iconsTop, verticalInner.width(), st::emojiCategory.height);
}

void EmojiPan::step_icons(TimeMs ms, bool timer) {
	if (_currentTabType != TabType::Stickers) {
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
	if (isHidden() || preventAutoHide()) return;

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
		hideChildren();
	}
}

void EmojiPan::startOpacityAnimation(bool hiding) {
	_hiding = false;
	prepareCache();
	_hiding = hiding;
	hideChildren();
	_a_opacity.start([this] { opacityAnimationCallback(); }, _hiding ? 1. : 0., _hiding ? 0. : 1., st::emojiPanDuration);
}

void EmojiPan::startShowAnimation() {
	if (!_a_show.animating()) {
		auto image = grabForComplexAnimation(GrabType::Panel);

		_showAnimation = std::make_unique<Ui::PanelAnimation>(st::emojiPanAnimation, Ui::PanelAnimation::Origin::BottomRight);
		auto inner = rect().marginsRemoved(st::emojiPanMargins);
		_showAnimation->setFinalImage(std::move(image), QRect(inner.topLeft() * cIntRetinaFactor(), inner.size() * cIntRetinaFactor()));
		auto corners = App::cornersMask(ImageRoundRadius::Small);
		_showAnimation->setCornerMasks(QImage(*corners[0]), QImage(*corners[1]), QImage(*corners[2]), QImage(*corners[3]));
		_showAnimation->start();
	}
	hideChildren();
	_a_show.start([this] { update(); }, 0., 1., st::emojiPanShowDuration);
}

QImage EmojiPan::grabForComplexAnimation(GrabType type) {
	auto cache = base::take(_cache);
	auto opacityAnimation = base::take(_a_opacity);
	auto slideAnimationData = base::take(_slideAnimation);
	auto slideAnimation = base::take(_a_slide);
	auto showAnimationData = base::take(_showAnimation);
	auto showAnimation = base::take(_a_show);

	showAll();
	if (type == GrabType::Slide) {
		_topShadow->hide();
		_tabsSlider->hide();
	}
	myEnsureResized(this);

	auto result = QImage(size() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	_inComplrexGrab = true;
	render(&result);
	_inComplrexGrab = false;

	_a_show = base::take(showAnimation);
	_showAnimation = base::take(showAnimationData);
	_a_slide = base::take(slideAnimation);
	_slideAnimation = base::take(slideAnimationData);
	_a_opacity = base::take(opacityAnimation);
	_cache = base::take(_cache);

	return result;
}

void EmojiPan::hideAnimated() {
	if (isHidden()) return;
	if (_hiding) return;

	_hideTimer.stop();
	if (_a_slide.animating()) {
		_hideAfterSlide = true;
	} else {
		startOpacityAnimation(true);
	}
}

EmojiPan::~EmojiPan() = default;

void EmojiPan::hideFinished() {
	hide();
	currentTab()->widget()->hideFinish(true);
	_a_show.finish();
	_showAnimation.reset();
	_a_slide.finish();
	_slideAnimation.reset();
	_cache = QPixmap();
	_horizontal = false;
	_hiding = false;

	_scroll->scrollToY(0);
	setCurrentSectionIcon(dbiesRecent);
	_iconOver = _iconDown = -1;
	_iconSel = 0;
	_iconsX = anim::value();
	_iconSelX = anim::value();
	_iconsStartAnim = 0;
	_a_icons.stop();

	Notify::clipStopperHidden(ClipStopperSavedGifsPanel);
}

void EmojiPan::showAnimated() {
	_hideTimer.stop();
	_hideAfterSlide = false;
	showStarted();
}

void EmojiPan::showStarted() {
	if (isHidden()) {
		emit updateStickers();
		currentTab()->widget()->refreshRecent();
		currentTab()->widget()->preloadImages();
		_a_slide.finish();
		_slideAnimation.reset();
		moveByBottom();
		show();
		startShowAnimation();
	} else if (_hiding) {
		startOpacityAnimation(false);
	}
}

bool EmojiPan::eventFilter(QObject *obj, QEvent *e) {
	if (e->type() == QEvent::Enter) {
		otherEnter();
	} else if (e->type() == QEvent::Leave) {
		otherLeave();
	} else if (e->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton/* && !dynamic_cast<StickerPan*>(obj)*/) {
		if (isHidden() || _hiding || _hideAfterSlide) {
			showAnimated();
		} else {
			hideAnimated();
		}
	}
	return false;
}

void EmojiPan::stickersInstalled(uint64 setId) {
	_tabsSlider->setActiveSection(static_cast<int>(TabType::Stickers));
	if (isHidden()) {
		moveByBottom();
		startShowAnimation();
		show();
	}
	showAll();
	stickers()->showStickerSet(setId);
	updateContentHeight();
	showAnimated();
}

bool EmojiPan::ui_isInlineItemBeingChosen() {
	return (_currentTabType == TabType::Gifs && !isHidden());
}

void EmojiPan::showAll() {
	if (_currentTabType == TabType::Emoji) {
		_recent->show();
		_people->show();
		_nature->show();
		_food->show();
		_activity->show();
		_travel->show();
		_objects->show();
		_symbols->show();
	} else {
		_recent->hide();
		_people->hide();
		_nature->hide();
		_food->hide();
		_activity->hide();
		_travel->hide();
		_objects->hide();
		_symbols->hide();
	}
	_scroll->show();
	_topShadow->show();
	_bottomShadow->setVisible(_currentTabType == TabType::Gifs);
	_tabsSlider->show();
}

void EmojiPan::hideForSliding() {
	hideChildren();
	_tabsSlider->show();
	_topShadow->show();
	currentTab()->widget()->clearSelection();
}

void EmojiPan::setActiveSection(DBIEmojiSection tab) {
	emoji()->showEmojiSection(tab);
}

void EmojiPan::onScroll() {
	auto scrollTop = _scroll->scrollTop();
	auto scrollBottom = scrollTop + _scroll->height();
	currentTab()->widget()->setVisibleTopBottom(scrollTop, scrollBottom);

	switch (_currentTabType) {
	case TabType::Emoji: {
		setCurrentSectionIcon(emoji()->currentSection(scrollTop));
	} break;

	case TabType::Stickers: {
		validateSelectedIcon(ValidateIconAnimations::Full);
	} break;

	case TabType::Gifs: {
		if (scrollBottom > _scroll->scrollTopMax()) {
			onInlineRequest();
		}
	} break;

	default: Unexpected("Bad type value.");
	}
}

void EmojiPan::setCurrentSectionIcon(DBIEmojiSection section) {
	_recent->setIconOverride((section == dbiesRecent) ? &st::emojiRecentActive : nullptr);
	_people->setIconOverride((section == dbiesPeople) ? &st::emojiPeopleActive : nullptr);
	_nature->setIconOverride((section == dbiesNature) ? &st::emojiNatureActive : nullptr);
	_food->setIconOverride((section == dbiesFood) ? &st::emojiFoodActive : nullptr);
	_activity->setIconOverride((section == dbiesActivity) ? &st::emojiActivityActive : nullptr);
	_travel->setIconOverride((section == dbiesTravel) ? &st::emojiTravelActive : nullptr);
	_objects->setIconOverride((section == dbiesObjects) ? &st::emojiObjectsActive : nullptr);
	_symbols->setIconOverride((section == dbiesSymbols) ? &st::emojiSymbolsActive : nullptr);
}

void EmojiPan::validateSelectedIcon(ValidateIconAnimations animations) {
	auto setId = stickers()->currentSet(_scroll->scrollTop());
	auto newSel = 0;
	for (auto i = 0, l = _icons.size(); i != l; ++i) {
		if (_icons[i].setId == setId) {
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

void EmojiPan::createTabsSlider() {
	_tabsSlider->setSectionActivatedCallback([this] {
		switchTab();
	});
	auto sections = QStringList();
	sections.push_back(lang(lng_switch_emoji).toUpper());
	sections.push_back(lang(lng_switch_stickers).toUpper());
	sections.push_back(lang(lng_switch_gifs).toUpper());
	_tabsSlider->setSections(sections);

	_tabsSlider->resizeToWidth(innerRect().width());
	_tabsSlider->moveToLeft(innerRect().x(), innerRect().y());
	_topShadow->setGeometry(_tabsSlider->x(), _tabsSlider->bottomNoMargins() - st::lineWidth, _tabsSlider->width(), st::lineWidth);
}

void EmojiPan::switchTab() {
	auto tab = _tabsSlider->activeSection();
	t_assert(tab >= 0 && tab < Tab::kCount);
	auto newTabType = static_cast<TabType>(tab);
	if (_currentTabType == newTabType) {
		return;
	}
	if (newTabType == TabType::Gifs) {
		gifs()->showStickerSet(Stickers::NoneSetId);
	}

	auto wasTab = _currentTabType;
	currentTab()->saveScrollTop();

	auto wasCache = grabForComplexAnimation(GrabType::Slide);

	auto widget = _scroll->takeWidget<internal::BasicPanInner>();
	widget->setParent(this);
	widget->hide();
	currentTab()->returnWidget(std::move(widget));

	_currentTabType = newTabType;
	if (_currentTabType != TabType::Gifs) {
		Notify::clipStopperHidden(ClipStopperSavedGifsPanel);
	}
	if (_currentTabType == TabType::Stickers) {
		validateSelectedIcon(ValidateIconAnimations::None);
	}
	updateContentHeight();
	_iconOver = -1;
	_a_icons.stop();

	setWidgetToScrollArea();

	auto nowCache = grabForComplexAnimation(GrabType::Slide);

	auto direction = (wasTab > _currentTabType) ? SlideAnimation::Direction::LeftToRight : SlideAnimation::Direction::RightToLeft;
	if (direction == SlideAnimation::Direction::LeftToRight) {
		std::swap(wasCache, nowCache);
	}
	_slideAnimation = std::make_unique<SlideAnimation>();
	auto inner = rect().marginsRemoved(st::emojiPanMargins);
	auto slidingRect = QRect(_tabsSlider->x() * cIntRetinaFactor(), _scroll->y() * cIntRetinaFactor(), _tabsSlider->width() * cIntRetinaFactor(), (inner.y() + inner.height() - _scroll->y()) * cIntRetinaFactor());
	_slideAnimation->setFinalImages(direction, std::move(wasCache), std::move(nowCache), slidingRect);
	auto corners = App::cornersMask(ImageRoundRadius::Small);
	_slideAnimation->setCornerMasks(QImage(*corners[0]), QImage(*corners[1]), QImage(*corners[2]), QImage(*corners[3]));
	_slideAnimation->start();

	hideForSliding();

	getTab(wasTab)->widget()->hideFinish(false);

	_a_slide.start([this] { update(); }, 0., 1., st::emojiPanSlideDuration, anim::linear);
	update();

	onSaveConfigDelayed(internal::kSaveChosenTabTimeout);
}

void EmojiPan::setWidgetToScrollArea() {
	_scroll->setOwnedWidget(currentTab()->takeWidget());
	_scroll->disableScroll(false);
	currentTab()->widget()->moveToLeft(0, 0);
	currentTab()->widget()->show();
	_scroll->scrollToY(currentTab()->getScrollTop());
	onScroll();
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
			stickers()->notInstalledLocally(setId);
			Stickers::undoInstallLocally(setId);
		}).send();

		stickers()->installedLocally(setId);
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
}

bool EmojiPan::overlaps(const QRect &globalRect) const {
	if (isHidden() || !_cache.isNull()) return false;

	auto testRect = QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size());
	auto inner = rect().marginsRemoved(st::emojiPanMargins);
	return inner.marginsRemoved(QMargins(st::buttonRadius, 0, st::buttonRadius, 0)).contains(testRect)
		|| inner.marginsRemoved(QMargins(0, st::buttonRadius, 0, st::buttonRadius)).contains(testRect);
}

void EmojiPan::inlineBotChanged() {
	if (!_inlineBot) return;

	if (!isHidden() && !_hiding) {
		if (!rect().contains(mapFromGlobal(QCursor::pos()))) {
			hideAnimated();
		}
	}

	if (_inlineRequestId) MTP::cancel(_inlineRequestId);
	_inlineRequestId = 0;
	_inlineQuery = _inlineNextQuery = _inlineNextOffset = QString();
	_inlineBot = nullptr;
	_inlineCache.clear();
	gifs()->inlineBotChanged();
	gifs()->hideInlineRowsPanel();

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
	onScroll();
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
	if (!_inlineBot) {
		gifs()->hideInlineRowsPanel();
	} else {
		gifs()->clearInlineRowsPanel();
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
	auto result = gifs()->refreshInlineRows(_inlineBot, entry, false);
	if (added) *added = result;
	return (entry != nullptr);
}

int32 EmojiPan::showInlineRows(bool newResults) {
	int32 added = 0;
	bool clear = !refreshInlineRows(&added);
	if (newResults) {
		_scroll->scrollToY(0);
	}

	auto hidden = isHidden();
	if (clear) {
		if (!_hiding) {
			_cache = QPixmap(); // clear after refreshInlineRows()
		}
	} else {
		if (_currentTabType != TabType::Gifs) {
			_tabsSlider->setActiveSection(static_cast<int>(TabType::Gifs));
		}
		showAnimated();
	}

	return added;
}
