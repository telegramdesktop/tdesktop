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
#include "stickers/stickers_list_widget.h"

#include "styles/style_stickers.h"
#include "ui/widgets/buttons.h"
#include "ui/effects/ripple_animation.h"
#include "boxes/stickers_box.h"
#include "inline_bots/inline_bot_result.h"
#include "stickers/stickers.h"
#include "storage/localstorage.h"
#include "lang.h"
#include "mainwindow.h"

namespace ChatHelpers {
namespace {

constexpr auto kStickersPanelPerRow = Stickers::kPanelPerRow;
constexpr auto kInlineItemsMaxPerRow = 5;

} // namespace

class StickersListWidget::Controller : public TWidget {
public:
	Controller(gsl::not_null<StickersListWidget*> parent);

private:
	gsl::not_null<StickersListWidget*> _pan;

};

StickersListWidget::Controller::Controller(gsl::not_null<StickersListWidget*> parent) : TWidget(parent)
, _pan(parent) {
}

StickersListWidget::StickersListWidget(QWidget *parent) : Inner(parent)
, _section(Section::Stickers)
, _addText(lang(lng_stickers_featured_add).toUpper())
, _addWidth(st::stickersTrendingAdd.font->width(_addText))
, _settings(this, lang(lng_stickers_you_have)) {
	resize(st::emojiPanWidth - st::emojiScroll.width - st::buttonRadius, countHeight());

	setMouseTracking(true);
	setAttribute(Qt::WA_OpaquePaintEvent);

	connect(_settings, SIGNAL(clicked()), this, SLOT(onSettings()));

	_previewTimer.setSingleShot(true);
	connect(&_previewTimer, SIGNAL(timeout()), this, SLOT(onPreview()));

	subscribe(AuthSession::CurrentDownloaderTaskFinished(), [this] {
		update();
		readVisibleSets();
	});
}

object_ptr<TWidget> StickersListWidget::createController() {
	return object_ptr<Controller>(this);
}

void StickersListWidget::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	auto top = getVisibleTop();
	Inner::setVisibleTopBottom(visibleTop, visibleBottom);
	if (_section == Section::Featured) {
		readVisibleSets();
	}
}

void StickersListWidget::readVisibleSets() {
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
		int count = qMin(set.pack.size(), static_cast<int>(kStickersPanelPerRow));
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

int StickersListWidget::featuredRowHeight() const {
	return st::stickersTrendingHeader + st::stickerPanSize.height() + st::stickersTrendingSkip;
}

template <typename Callback>
bool StickersListWidget::enumerateSections(Callback callback) const {
	Expects(_section == Section::Stickers);
	auto info = SectionInfo();
	for (auto i = 0; i != _mySets.size(); ++i) {
		info.section = i;
		info.count = _mySets[i].pack.size();
		info.rowsCount = (info.count / kStickersPanelPerRow) + ((info.count % kStickersPanelPerRow) ? 1 : 0);
		info.rowsTop = info.top + (i == 0 ? st::stickerPanPadding : st::emojiPanHeader);
		info.rowsBottom = info.rowsTop + info.rowsCount * st::stickerPanSize.height();
		if (!callback(info)) {
			return false;
		}
		info.top = info.rowsBottom;
	}
	return true;
}

StickersListWidget::SectionInfo StickersListWidget::sectionInfo(int section) const {
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

StickersListWidget::SectionInfo StickersListWidget::sectionInfoByOffset(int yOffset) const {
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

int StickersListWidget::countHeight() {
	auto visibleHeight = getVisibleBottom() - getVisibleTop();
	if (visibleHeight <= 0) {
		visibleHeight = st::emojiPanMaxHeight - st::emojiCategory.height;
	}
	auto minimalLastHeight = (visibleHeight - st::stickerPanPadding);
	auto countResult = [this, minimalLastHeight] {
		if (_section == Section::Featured) {
			return st::stickerPanPadding + shownSets().size() * featuredRowHeight();
		} else if (!shownSets().empty()) {
			auto info = sectionInfo(shownSets().size() - 1);
			return info.top + qMax(info.rowsBottom - info.top, minimalLastHeight);
		}
		return 0;
	};
	return qMax(minimalLastHeight, countResult()) + st::stickerPanPadding;
}

void StickersListWidget::installedLocally(uint64 setId) {
	_installedLocallySets.insert(setId);
}

void StickersListWidget::notInstalledLocally(uint64 setId) {
	_installedLocallySets.remove(setId);
}

void StickersListWidget::clearInstalledLocally() {
	if (!_installedLocallySets.empty()) {
		_installedLocallySets.clear();
		refreshStickers();
	}
}

int StickersListWidget::stickersLeft() const {
	return (st::stickerPanPadding - st::buttonRadius);
}

QRect StickersListWidget::stickerRect(int section, int sel) {
	int x = 0, y = 0;
	if (_section == Section::Featured) {
		x = stickersLeft() + (sel * st::stickerPanSize.width());
		y = st::stickerPanPadding + (section * featuredRowHeight()) + st::stickersTrendingHeader;
	} else if (_section == Section::Stickers) {
		auto info = sectionInfo(section);
		if (sel >= _mySets[section].pack.size()) {
			sel -= _mySets[section].pack.size();
		}
		auto countTillItem = (sel - (sel % kStickersPanelPerRow));
		auto rowsToSkip = (countTillItem / kStickersPanelPerRow) + ((countTillItem % kStickersPanelPerRow) ? 1 : 0);
		x = stickersLeft() + ((sel % kStickersPanelPerRow) * st::stickerPanSize.width());
		y = info.rowsTop + rowsToSkip * st::stickerPanSize.height();
	}
	return QRect(x, y, st::stickerPanSize.width(), st::stickerPanSize.height());
}

void StickersListWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto clip = e->rect();
	p.fillRect(clip, st::emojiPanBg);

	if (_section == Section::Featured) {
		paintFeaturedStickers(p, clip);
	} else {
		paintStickers(p, clip);
	}
}

void StickersListWidget::paintFeaturedStickers(Painter &p, QRect clip) {
	auto fromColumn = floorclamp(clip.x() - stickersLeft(), st::stickerPanSize.width(), 0, kStickersPanelPerRow);
	auto toColumn = ceilclamp(clip.x() + clip.width() - stickersLeft(), st::stickerPanSize.width(), 0, kStickersPanelPerRow);
	if (rtl()) {
		qSwap(fromColumn, toColumn);
		fromColumn = kStickersPanelPerRow - fromColumn;
		toColumn = kStickersPanelPerRow - toColumn;
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

void StickersListWidget::paintStickers(Painter &p, QRect clip) {
	auto fromColumn = floorclamp(clip.x() - stickersLeft(), st::stickerPanSize.width(), 0, kStickersPanelPerRow);
	auto toColumn = ceilclamp(clip.x() + clip.width() - stickersLeft(), st::stickerPanSize.width(), 0, kStickersPanelPerRow);
	if (rtl()) {
		qSwap(fromColumn, toColumn);
		fromColumn = kStickersPanelPerRow - fromColumn;
		toColumn = kStickersPanelPerRow - toColumn;
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
					int index = i * kStickersPanelPerRow + j;
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

void StickersListWidget::paintSticker(Painter &p, Set &set, int y, int index, bool selected, bool deleteSelected) {
	auto sticker = set.pack[index];
	if (!sticker->sticker()) return;

	int row = (index / kStickersPanelPerRow), col = (index % kStickersPanelPerRow);

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

bool StickersListWidget::featuredHasAddButton(int index) const {
	if (index < 0 || index >= _featuredSets.size()) {
		return false;
	}
	auto flags = _featuredSets[index].flags;
	return !(flags & MTPDstickerSet::Flag::f_installed) || (flags & MTPDstickerSet::Flag::f_archived);
}

int StickersListWidget::featuredContentWidth() const {
	return stickersLeft() + (kStickersPanelPerRow * st::stickerPanSize.width());
}

QRect StickersListWidget::featuredAddRect(int index) const {
	int addw = _addWidth - st::stickersTrendingAdd.width;
	int addh = st::stickersTrendingAdd.height;
	int addx = featuredContentWidth() - addw;
	int addy = st::stickerPanPadding + index * featuredRowHeight() + st::stickersTrendingAddTop;
	return QRect(addx, addy, addw, addh);
}

void StickersListWidget::mousePressEvent(QMouseEvent *e) {
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

void StickersListWidget::setPressedFeaturedSetAdd(int newPressedFeaturedSetAdd) {
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

void StickersListWidget::mouseReleaseEvent(QMouseEvent *e) {
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

void StickersListWidget::removeRecentSticker(int section, int index) {
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

void StickersListWidget::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
}

void StickersListWidget::resizeEvent(QResizeEvent *e) {
	_settings->moveToLeft((st::emojiPanWidth - _settings->width()) / 2, height() / 3);
}

void StickersListWidget::leaveEventHook(QEvent *e) {
	clearSelection();
}

void StickersListWidget::leaveToChildEvent(QEvent *e, QWidget *child) {
	clearSelection();
}

void StickersListWidget::enterFromChildEvent(QEvent *e, QWidget *child) {
	_lastMousePos = QCursor::pos();
	updateSelected();
}

void StickersListWidget::clearSelection() {
	_pressed = -1;
	_pressedFeaturedSet = -1;
	setSelected(-1, -1, -1);
	setPressedFeaturedSetAdd(-1);
	update();
}

void StickersListWidget::hideFinish(bool completely) {
	clearSelection();
	if (completely) {
		clearInstalledLocally();
	}

	// Reset to the recent stickers section.
	if (_section == Section::Featured) {
		_section = Section::Stickers;
	}
}

void StickersListWidget::refreshStickers() {
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

void StickersListWidget::preloadImages() {
	auto &sets = shownSets();
	for (int i = 0, l = sets.size(), k = 0; i < l; ++i) {
		int count = sets[i].pack.size();
		if (_section == Section::Featured) {
			accumulate_min(count, kStickersPanelPerRow);
		}
		for (int j = 0; j != count; ++j) {
			if (++k > kStickersPanelPerRow * (kStickersPanelPerRow + 1)) break;

			auto sticker = sets.at(i).pack.at(j);
			if (!sticker || !sticker->sticker()) continue;

			bool goodThumb = !sticker->thumb->isNull() && ((sticker->thumb->width() >= 128) || (sticker->thumb->height() >= 128));
			if (goodThumb) {
				sticker->thumb->load();
			} else {
				sticker->automaticLoad(0);
			}
		}
		if (k > kStickersPanelPerRow * (kStickersPanelPerRow + 1)) break;
	}
}

uint64 StickersListWidget::currentSet(int yOffset) const {
	if (_section == Section::Featured) {
		return Stickers::FeaturedSetId;
	}
	return _mySets.isEmpty() ? Stickers::RecentSetId : _mySets[sectionInfoByOffset(yOffset).section].id;
}

void StickersListWidget::appendSet(Sets &to, uint64 setId, AppendSkip skip) {
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

void StickersListWidget::refreshRecent() {
	if (_section == Section::Stickers) {
		refreshRecentStickers();
	}
}

void StickersListWidget::refreshRecentStickers(bool performResize) {
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

void StickersListWidget::fillIcons(QList<StickerIcon> &icons) {
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

void StickersListWidget::updateSelected() {
	if (_pressed >= 0 && !_previewShown) {
		return;
	}

	auto newSelected = -1;
	auto p = mapFromGlobal(_lastMousePos);
	auto newSelectedFeaturedSet = -1;
	auto newSelectedFeaturedSetAdd = -1;
	auto &sets = shownSets();
	auto sx = (rtl() ? width() - p.x() : p.x()) - stickersLeft();
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
				if (sx >= 0 && sx < kStickersPanelPerRow * st::stickerPanSize.width()) {
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
		} else if (p.y() >= info.rowsTop && p.y() < info.rowsBottom && sx >= 0) {
			auto yOffset = p.y() - info.rowsTop;
			auto &set = sets[info.section];
			auto special = ((set.flags & MTPDstickerSet::Flag::f_official) != 0);
			auto rowIndex = qFloor(yOffset / st::stickerPanSize.height());
			newSelected = rowIndex * kStickersPanelPerRow + qFloor(sx / st::stickerPanSize.width());
			if (newSelected >= set.pack.size()) {
				newSelected = -1;
			} else {
				if (set.id == Stickers::RecentSetId && _custom[newSelected]) {
					auto inx = sx - (newSelected % kStickersPanelPerRow) * st::stickerPanSize.width();
					auto iny = yOffset - ((newSelected / kStickersPanelPerRow) * st::stickerPanSize.height());
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

void StickersListWidget::setSelected(int newSelected, int newSelectedFeaturedSet, int newSelectedFeaturedSetAdd) {
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

void StickersListWidget::onSettings() {
	Ui::show(Box<StickersBox>(StickersBox::Section::Installed));
}

void StickersListWidget::onPreview() {
	if (_pressed < 0) return;
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

void StickersListWidget::showStickerSet(uint64 setId) {
	clearSelection();

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

} // namespace ChatHelpers
