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
#include "chat_helpers/stickers_list_widget.h"

#include "styles/style_chat_helpers.h"
#include "ui/widgets/buttons.h"
#include "ui/effects/ripple_animation.h"
#include "boxes/stickers_box.h"
#include "inline_bots/inline_bot_result.h"
#include "chat_helpers/stickers.h"
#include "storage/localstorage.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "dialogs/dialogs_layout.h"
#include "boxes/sticker_set_box.h"
#include "boxes/stickers_box.h"
#include "boxes/confirm_box.h"
#include "auth_session.h"
#include "observer_peer.h"
#include "apiwrap.h"

namespace ChatHelpers {
namespace {

constexpr auto kStickersPanelPerRow = Stickers::kPanelPerRow;
constexpr auto kInlineItemsMaxPerRow = 5;

} // namespace

struct StickerIcon {
	StickerIcon(uint64 setId) : setId(setId) {
	}
	StickerIcon(uint64 setId, DocumentData *sticker, int32 pixw, int32 pixh) : setId(setId), sticker(sticker), pixw(pixw), pixh(pixh) {
	}
	uint64 setId = 0;
	DocumentData *sticker = nullptr;
	ChannelData *megagroup = nullptr;
	int pixw = 0;
	int pixh = 0;

};

class StickersListWidget::Footer : public TabbedSelector::InnerFooter, private base::Subscriber {
public:
	Footer(not_null<StickersListWidget*> parent);

	void preloadImages();
	void validateSelectedIcon(uint64 setId, ValidateIconAnimations animations);
	void refreshIcons(ValidateIconAnimations animations);
	bool hasOnlyFeaturedSets() const;

	void leaveToChildEvent(QEvent *e, QWidget *child) override;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	bool event(QEvent *e) override;

	void processHideFinished() override;

private:
	template <typename Callback>
	void enumerateVisibleIcons(Callback callback);

	void step_icons(TimeMs ms, bool timer);

	void updateSelected();
	void paintStickerSettingsIcon(Painter &p) const;
	void paintFeaturedStickerSetsBadge(Painter &p, int iconLeft) const;

	not_null<StickersListWidget*> _pan;

	static constexpr auto kVisibleIconsCount = 8;

	QList<StickerIcon> _icons;
	int _iconOver = -1;
	int _iconSel = 0;
	int _iconDown = -1;
	bool _iconsDragging = false;
	BasicAnimation _a_icons;
	QPoint _iconsMousePos, _iconsMouseDown;
	int _iconsLeft = 0;
	int _iconsTop = 0;
	int _iconsStartX = 0;
	int _iconsMax = 0;
	anim::value _iconsX;
	anim::value _iconSelX;
	TimeMs _iconsStartAnim = 0;

	bool _horizontal = false;

};

StickersListWidget::Footer::Footer(not_null<StickersListWidget*> parent) : InnerFooter(parent)
, _pan(parent)
, _a_icons(animation(this, &Footer::step_icons)) {
	setMouseTracking(true);

	_iconsLeft = (st::emojiPanWidth - kVisibleIconsCount * st::emojiCategory.width) / 2;

	subscribe(Auth().downloaderTaskFinished(), [this] {
		update();
	});
}

template <typename Callback>
void StickersListWidget::Footer::enumerateVisibleIcons(Callback callback) {
	auto iconsX = qRound(_iconsX.current());
	auto index = iconsX / st::emojiCategory.width;
	auto x = _iconsLeft - (iconsX % st::emojiCategory.width);
	for (auto index = qMin(_icons.size(), iconsX / st::emojiCategory.width),
		last = qMin(_icons.size(), index + kVisibleIconsCount); index != last; ++index) {
		callback(_icons[index], x);
		x += st::emojiCategory.width;
	}
}

void StickersListWidget::Footer::preloadImages() {
	enumerateVisibleIcons([this](const StickerIcon &icon, int x) {
		if (auto sticker = icon.sticker) {
			sticker->thumb->load();
		}
	});
}

void StickersListWidget::Footer::validateSelectedIcon(uint64 setId, ValidateIconAnimations animations) {
	auto newSel = 0;
	for (auto i = 0, l = _icons.size(); i != l; ++i) {
		if (_icons[i].setId == setId) {
			newSel = i;
			break;
		}
	}
	if (newSel != _iconSel) {
		_iconSel = newSel;
		auto iconSelXFinal = _iconSel * st::emojiCategory.width;
		if (animations == ValidateIconAnimations::Full) {
			_iconSelX.start(iconSelXFinal);
		} else {
			_iconSelX = anim::value(iconSelXFinal, iconSelXFinal);
		}
		auto iconsXFinal = snap((2 * _iconSel + 1 - kVisibleIconsCount) * st::emojiCategory.width / 2, 0, _iconsMax);
		if (animations == ValidateIconAnimations::None) {
			_iconsX = anim::value(iconsXFinal, iconsXFinal);
			_a_icons.stop();
		} else {
			_iconsX.start(iconsXFinal);
			_iconsStartAnim = getms();
			_a_icons.start();
		}
		updateSelected();
		update();
	}
}

void StickersListWidget::Footer::processHideFinished() {
	_iconOver = _iconDown = -1;
	_iconsStartAnim = 0;
	_a_icons.stop();
	_iconsX.finish();
	_iconSelX.finish();
	_horizontal = false;
}

void StickersListWidget::Footer::leaveToChildEvent(QEvent *e, QWidget *child) {
	_iconsMousePos = QCursor::pos();
	updateSelected();
}

void StickersListWidget::Footer::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (_icons.isEmpty()) {
		return;
	}

	if (!hasOnlyFeaturedSets()) {
		paintStickerSettingsIcon(p);
	}

	auto selxrel = _iconsLeft + qRound(_iconSelX.current());
	auto selx = selxrel - qRound(_iconsX.current());

	auto clip = QRect(_iconsLeft, _iconsTop, _iconsLeft + (kVisibleIconsCount - 1) * st::emojiCategory.width - _iconsLeft, st::emojiCategory.height);
	if (rtl()) clip.moveLeft(width() - _iconsLeft - clip.width());
	p.setClipRect(clip);

	enumerateVisibleIcons([this, &p](const StickerIcon &icon, int x) {
		if (icon.sticker) {
			icon.sticker->thumb->load();
			auto pix = icon.sticker->thumb->pix(icon.pixw, icon.pixh);

			p.drawPixmapLeft(x + (st::emojiCategory.width - icon.pixw) / 2, _iconsTop + (st::emojiCategory.height - icon.pixh) / 2, width(), pix);
		} else if (icon.megagroup) {
			icon.megagroup->paintUserpicLeft(p, x + (st::emojiCategory.width - st::stickerGroupCategorySize) / 2, _iconsTop + (st::emojiCategory.height - st::stickerGroupCategorySize) / 2, width(), st::stickerGroupCategorySize);
		} else {
			auto getSpecialSetIcon = [](uint64 setId) {
				if (setId == Stickers::FeaturedSetId) {
					return &st::stickersTrending;
				} else if (setId == Stickers::FavedSetId) {
					return &st::stickersFaved;
				}
				return &st::emojiRecent;
			};
			getSpecialSetIcon(icon.setId)->paint(p, x + st::emojiCategory.iconPosition.x(), _iconsTop + st::emojiCategory.iconPosition.y(), width());
			if (icon.setId == Stickers::FeaturedSetId) {
				paintFeaturedStickerSetsBadge(p, x);
			}
		}
	});

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
		st::stickerIconRight.fill(p, rtlrect(_iconsLeft + (kVisibleIconsCount - 1) * st::emojiCategory.width - st::stickerIconRight.width(), _iconsTop, st::stickerIconRight.width(), st::emojiCategory.height, width()));
		p.setOpacity(1.);
	}
}

void StickersListWidget::Footer::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_iconsMousePos = e ? e->globalPos() : QCursor::pos();
	updateSelected();

	if (_iconOver == _icons.size()) {
		Ui::show(Box<StickersBox>(hasOnlyFeaturedSets() ? StickersBox::Section::Featured : StickersBox::Section::Installed));
	} else {
		_iconDown = _iconOver;
		_iconsMouseDown = _iconsMousePos;
		_iconsStartX = qRound(_iconsX.current());
	}
}

void StickersListWidget::Footer::mouseMoveEvent(QMouseEvent *e) {
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
			update();
		}
	}
}

void StickersListWidget::Footer::mouseReleaseEvent(QMouseEvent *e) {
	if (_icons.isEmpty()) {
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
			update();
		}
		_iconsDragging = false;
		updateSelected();
	} else {
		updateSelected();

		if (wasDown == _iconOver && _iconOver >= 0 && _iconOver < _icons.size()) {
			_iconSelX = anim::value(_iconOver * st::emojiCategory.width, _iconOver * st::emojiCategory.width);
			_pan->showStickerSet(_icons[_iconOver].setId);
		}
	}
}

bool StickersListWidget::Footer::event(QEvent *e) {
	if (e->type() == QEvent::TouchBegin) {

	} else if (e->type() == QEvent::Wheel) {
		if (!_icons.isEmpty() && _iconOver >= 0 && _iconOver < _icons.size() && _iconDown < 0) {
			auto ev = static_cast<QWheelEvent*>(e);
			auto horizontal = (ev->angleDelta().x() != 0 || ev->orientation() == Qt::Horizontal);
			auto vertical = (ev->angleDelta().y() != 0 || ev->orientation() == Qt::Vertical);
			if (horizontal) _horizontal = true;
			auto newX = qRound(_iconsX.current());
			if (/*_horizontal && */horizontal) {
				newX = snap(newX - (rtl() ? -1 : 1) * (ev->pixelDelta().x() ? ev->pixelDelta().x() : ev->angleDelta().x()), 0, _iconsMax);
			} else if (/*!_horizontal && */vertical) {
				newX = snap(newX - (ev->pixelDelta().y() ? ev->pixelDelta().y() : ev->angleDelta().y()), 0, _iconsMax);
			}
			if (newX != qRound(_iconsX.current())) {
				_iconsX = anim::value(newX, newX);
				_iconsStartAnim = 0;
				_a_icons.stop();
				updateSelected();
				update();
			}
		}
	}
	return TWidget::event(e);
}

void StickersListWidget::Footer::updateSelected() {
	if (_iconDown >= 0) {
		return;
	}

	auto p = mapFromGlobal(_iconsMousePos);
	int32 x = p.x(), y = p.y(), newOver = -1;
	if (rtl()) x = width() - x;
	x -= _iconsLeft;
	if (x >= st::emojiCategory.width * (kVisibleIconsCount - 1) && x < st::emojiCategory.width * kVisibleIconsCount && y >= _iconsTop && y < _iconsTop + st::emojiCategory.height) {
		if (!_icons.isEmpty() && !hasOnlyFeaturedSets()) {
			newOver = _icons.size();
		}
	} else if (!_icons.isEmpty()) {
		if (y >= _iconsTop && y < _iconsTop + st::emojiCategory.height && x >= 0 && x < (kVisibleIconsCount - 1) * st::emojiCategory.width && x < _icons.size() * st::emojiCategory.width) {
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

void StickersListWidget::Footer::refreshIcons(ValidateIconAnimations animations) {
	_iconOver = -1;
	_pan->fillIcons(_icons);
	_iconsX.finish();
	_iconSelX.finish();
	_iconsStartAnim = 0;
	_a_icons.stop();
	if (_icons.isEmpty()) {
		_iconsMax = 0;
	} else {
		_iconsMax = qMax(int((_icons.size() + 1 - kVisibleIconsCount) * st::emojiCategory.width), 0);
	}
	if (_iconsX.current() > _iconsMax) {
		_iconsX = anim::value(_iconsMax, _iconsMax);
	}
	updateSelected();
	_pan->validateSelectedIcon(animations);
	update();
}

bool StickersListWidget::Footer::hasOnlyFeaturedSets() const {
	return (_icons.size() == 1) && (_icons[0].setId == Stickers::FeaturedSetId);
}

void StickersListWidget::Footer::paintStickerSettingsIcon(Painter &p) const {
	int settingsLeft = _iconsLeft + (kVisibleIconsCount - 1) * st::emojiCategory.width;
	st::stickersSettings.paint(p, settingsLeft + st::emojiCategory.iconPosition.x(), _iconsTop + st::emojiCategory.iconPosition.y(), width());
}

void StickersListWidget::Footer::paintFeaturedStickerSetsBadge(Painter &p, int iconLeft) const {
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

void StickersListWidget::validateSelectedIcon(ValidateIconAnimations animations) {
	if (_footer) {
		_footer->validateSelectedIcon(currentSet(getVisibleTop()), animations);
	}
}

void StickersListWidget::Footer::step_icons(TimeMs ms, bool timer) {
	if (_iconsStartAnim) {
		auto dt = (ms - _iconsStartAnim) / float64(st::stickerIconMove);
		if (dt >= 1) {
			_iconsStartAnim = 0;
			_iconsX.finish();
			_iconSelX.finish();
		} else {
			_iconsX.update(dt, anim::linear);
			_iconSelX.update(dt, anim::linear);
		}
	}

	if (timer) update();

	if (!_iconsStartAnim) {
		_a_icons.stop();
	}
}

StickersListWidget::StickersListWidget(QWidget *parent, not_null<Window::Controller*> controller) : Inner(parent, controller)
, _section(Section::Stickers)
, _megagroupSetAbout(st::emojiPanWidth - st::emojiScroll.width - st::emojiPanHeaderLeft)
, _addText(lang(lng_stickers_featured_add).toUpper())
, _addWidth(st::stickersTrendingAdd.font->width(_addText))
, _settings(this, lang(lng_stickers_you_have)) {
	resize(st::emojiPanWidth - st::emojiScroll.width - st::buttonRadius, countHeight());

	setMouseTracking(true);
	setAttribute(Qt::WA_OpaquePaintEvent);

	connect(_settings, SIGNAL(clicked()), this, SLOT(onSettings()));

	_previewTimer.setSingleShot(true);
	connect(&_previewTimer, SIGNAL(timeout()), this, SLOT(onPreview()));

	subscribe(Auth().downloaderTaskFinished(), [this] {
		update();
		readVisibleSets();
	});
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::ChannelStickersChanged, [this](const Notify::PeerUpdate &update) {
		if (update.peer == _megagroupSet) {
			refreshStickers();
		}
	}));
}

object_ptr<TabbedSelector::InnerFooter> StickersListWidget::createFooter() {
	Expects(_footer == nullptr);
	auto result = object_ptr<Footer>(this);
	_footer = result;
	return std::move(result);
}

void StickersListWidget::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	auto top = getVisibleTop();
	Inner::setVisibleTopBottom(visibleTop, visibleBottom);
	if (_section == Section::Featured) {
		readVisibleSets();
	}
	validateSelectedIcon(ValidateIconAnimations::Full);
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
			Stickers::MarkFeaturedAsRead(set.id);
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
		auto &set = _mySets[i];
		info.section = i;
		info.count = set.pack.size();
		info.rowsTop = info.top + (setHasTitle(set) ? st::emojiPanHeader : st::stickerPanPadding);
		if (set.id == Stickers::MegagroupSetId && !info.count) {
			info.rowsCount = 0;
			info.rowsBottom = info.rowsTop + _megagroupSetButtonRect.y() + _megagroupSetButtonRect.height() + st::stickerGroupCategoryAddMargin.bottom();
		} else {
			info.rowsCount = (info.count / kStickersPanelPerRow) + ((info.count % kStickersPanelPerRow) ? 1 : 0);
			info.rowsBottom = info.rowsTop + info.rowsCount * st::stickerPanSize.height();
		}
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
	auto selectedSticker = base::get_if<OverSticker>(&_selected);
	auto selectedButton = base::get_if<OverButton>(_pressed ? &_pressed : &_selected);

	auto tilly = st::stickerPanPadding;
	auto ms = getms();
	for (auto c = 0, l = sets.size(); c != l; ++c) {
		auto y = tilly;
		auto &set = sets[c];
		tilly = y + featuredRowHeight();
		if (clip.top() >= tilly) continue;
		if (y >= clip.y() + clip.height()) break;

		auto size = int(set.pack.size());

		auto widthForTitle = stickersRight() - (st::emojiPanHeaderLeft - st::buttonRadius);
		if (featuredHasAddButton(c)) {
			auto add = featuredAddRect(c);
			auto selected = selectedButton ? (selectedButton->section == c) : false;
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

		auto statusText = (size > 0) ? lng_stickers_count(lt_count, size) : lang(lng_contacts_loading);
		p.setFont(st::stickersTrendingSubheaderFont);
		p.setPen(st::stickersTrendingSubheaderFg);
		p.drawTextLeft(st::emojiPanHeaderLeft - st::buttonRadius, y + st::stickersTrendingSubheaderTop, width(), statusText);

		y += st::stickersTrendingHeader;
		if (y >= clip.y() + clip.height()) break;

		for (int j = fromColumn; j < toColumn; ++j) {
			int index = j;
			if (index >= size) break;

			auto selected = selectedSticker ? (selectedSticker->section == c && selectedSticker->index == index) : false;
			auto deleteSelected = false;
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

	auto ms = getms();
	auto &sets = shownSets();
	auto selectedSticker = base::get_if<OverSticker>(&_selected);
	auto selectedButton = base::get_if<OverButton>(_pressed ? &_pressed : &_selected);
	enumerateSections([this, &p, clip, fromColumn, toColumn, selectedSticker, selectedButton, ms](const SectionInfo &info) {
		if (clip.top() >= info.rowsBottom) {
			return true;
		} else if (clip.top() + clip.height() <= info.top) {
			return false;
		}
		auto &set = _mySets[info.section];
		if (setHasTitle(set) && clip.top() < info.rowsTop) {
			auto titleText = set.title;
			auto titleWidth = st::stickersTrendingHeaderFont->width(titleText);
			auto widthForTitle = stickersRight() - (st::emojiPanHeaderLeft - st::buttonRadius);
			if (hasRemoveButton(info.section)) {
				auto remove = removeButtonRect(info.section);
				auto selected = selectedButton ? (selectedButton->section == info.section) : false;
				if (set.ripple) {
					set.ripple->paint(p, remove.x() + st::stickerPanRemoveSet.rippleAreaPosition.x(), remove.y() + st::stickerPanRemoveSet.rippleAreaPosition.y(), width(), ms);
					if (set.ripple->empty()) {
						set.ripple.reset();
					}
				}
				(selected ? st::stickerPanRemoveSet.iconOver : st::stickerPanRemoveSet.icon).paint(p, remove.topLeft() + st::stickerPanRemoveSet.iconPosition, width());

				widthForTitle -= remove.width();
			}
			if (titleWidth > widthForTitle) {
				titleText = st::stickersTrendingHeaderFont->elided(titleText, widthForTitle);
				titleWidth = st::stickersTrendingHeaderFont->width(titleText);
			}
			p.setFont(st::emojiPanHeaderFont);
			p.setPen(st::emojiPanHeaderFg);
			p.drawTextLeft(st::emojiPanHeaderLeft - st::buttonRadius, info.top + st::emojiPanHeaderTop, width(), titleText, titleWidth);
		}
		if (clip.top() + clip.height() > info.rowsTop) {
			if (set.id == Stickers::MegagroupSetId && set.pack.empty()) {
				auto buttonSelected = (base::get_if<OverGroupAdd>(&_selected) != nullptr);
				paintMegagroupEmptySet(p, info.rowsTop, buttonSelected, ms);
			} else {
				auto special = (set.flags & MTPDstickerSet::Flag::f_official) != 0;
				auto fromRow = floorclamp(clip.y() - info.rowsTop, st::stickerPanSize.height(), 0, info.rowsCount);
				auto toRow = ceilclamp(clip.y() + clip.height() - info.rowsTop, st::stickerPanSize.height(), 0, info.rowsCount);
				for (int i = fromRow; i < toRow; ++i) {
					for (int j = fromColumn; j < toColumn; ++j) {
						int index = i * kStickersPanelPerRow + j;
						if (index >= info.count) break;

						auto selected = selectedSticker ? (selectedSticker->section == info.section && selectedSticker->index == index) : false;
						auto deleteSelected = selected && selectedSticker->overDelete;
						paintSticker(p, set, info.rowsTop, index, selected, deleteSelected);
					}
				}
			}
		}
		return true;
	});
}

int StickersListWidget::megagroupSetInfoLeft() const {
	return st::emojiPanHeaderLeft - st::buttonRadius;
}

void StickersListWidget::paintMegagroupEmptySet(Painter &p, int y, bool buttonSelected, TimeMs ms) {
	auto infoLeft = megagroupSetInfoLeft();
	_megagroupSetAbout.drawLeft(p, infoLeft, y, width() - infoLeft, width());

	auto &textBg = buttonSelected ? st::stickerGroupCategoryAdd.textBgOver : st::stickerGroupCategoryAdd.textBg;

	auto button = _megagroupSetButtonRect.translated(0, y);
	App::roundRect(p, myrtlrect(button), textBg, ImageRoundRadius::Small);
	if (_megagroupSetButtonRipple) {
		_megagroupSetButtonRipple->paint(p, button.x(), button.y(), width(), ms);
		if (_megagroupSetButtonRipple->empty()) {
			_megagroupSetButtonRipple.reset();
		}
	}
	p.setFont(st::stickerGroupCategoryAdd.font);
	p.setPen(buttonSelected ? st::stickerGroupCategoryAdd.textFgOver : st::stickerGroupCategoryAdd.textFg);
	p.drawTextLeft(button.x() - (st::stickerGroupCategoryAdd.width / 2), button.y() + st::stickerGroupCategoryAdd.textTop, width(), _megagroupSetButtonText, _megagroupSetButtonTextWidth);
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

	if (selected && stickerHasDeleteButton(set, index)) {
		auto xPos = pos + QPoint(st::stickerPanSize.width() - st::stickerPanDelete.width(), 0);
		if (!deleteSelected) p.setOpacity(st::stickerPanDeleteOpacity);
		st::stickerPanDelete.paint(p, xPos, width());
		if (!deleteSelected) p.setOpacity(1.);
	}
}

int StickersListWidget::stickersRight() const {
	return stickersLeft() + (kStickersPanelPerRow * st::stickerPanSize.width());
}

bool StickersListWidget::featuredHasAddButton(int index) const {
	if (index < 0 || index >= _featuredSets.size()) {
		return false;
	}
	auto flags = _featuredSets[index].flags;
	return !(flags & MTPDstickerSet::Flag::f_installed) || (flags & MTPDstickerSet::Flag::f_archived);
}

QRect StickersListWidget::featuredAddRect(int index) const {
	auto addw = _addWidth - st::stickersTrendingAdd.width;
	auto addh = st::stickersTrendingAdd.height;
	auto addx = stickersRight() - addw;
	auto addy = st::stickerPanPadding + index * featuredRowHeight() + st::stickersTrendingAddTop;
	return QRect(addx, addy, addw, addh);
}

bool StickersListWidget::hasRemoveButton(int index) const {
	if (index < 0 || index >= _mySets.size()) {
		return false;
	}
	auto &set = _mySets[index];
	auto flags = set.flags;
	if (!(flags & MTPDstickerSet_ClientFlag::f_special)) {
		return true;
	}
	if (set.id == Stickers::MegagroupSetId) {
		Assert(_megagroupSet != nullptr);
		if (index + 1 != _mySets.size()) {
			return true;
		}
		return !set.pack.empty() && _megagroupSet->canEditStickers();
	}
	return false;
}

QRect StickersListWidget::removeButtonRect(int index) const {
	auto buttonw = st::stickerPanRemoveSet.width;
	auto buttonh = st::stickerPanRemoveSet.height;
	auto buttonx = stickersRight() - buttonw;
	auto buttony = sectionInfo(index).top + (st::emojiPanHeader - buttonh) / 2;
	return QRect(buttonx, buttony, buttonw, buttonh);
}

void StickersListWidget::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_lastMousePosition = e->globalPos();
	updateSelected();

	setPressed(_selected);
	ClickHandler::pressed();
	_previewTimer.start(QApplication::startDragTime());
}

void StickersListWidget::setPressed(OverState newPressed) {
	if (auto button = base::get_if<OverButton>(&_pressed)) {
		auto &sets = shownSets();
		Assert(button->section >= 0 && button->section < sets.size());
		auto &set = sets[button->section];
		if (set.ripple) {
			set.ripple->lastStop();
		}
	} else if (base::get_if<OverGroupAdd>(&_pressed)) {
		if (_megagroupSetButtonRipple) {
			_megagroupSetButtonRipple->lastStop();
		}
	}
	_pressed = newPressed;
	if (auto button = base::get_if<OverButton>(&_pressed)) {
		auto &sets = shownSets();
		Assert(button->section >= 0 && button->section < sets.size());
		auto &set = sets[button->section];
		if (!set.ripple) {
			set.ripple = createButtonRipple(button->section);
		}
		set.ripple->add(mapFromGlobal(QCursor::pos()) - buttonRippleTopLeft(button->section));
	} else if (base::get_if<OverGroupAdd>(&_pressed)) {
		if (!_megagroupSetButtonRipple) {
			auto maskSize = _megagroupSetButtonRect.size();
			auto mask = Ui::RippleAnimation::roundRectMask(maskSize, st::buttonRadius);
			_megagroupSetButtonRipple = std::make_unique<Ui::RippleAnimation>(st::stickerGroupCategoryAdd.ripple, std::move(mask), [this] {
				rtlupdate(megagroupSetButtonRectFinal());
			});
		}
		_megagroupSetButtonRipple->add(mapFromGlobal(QCursor::pos()) - myrtlrect(megagroupSetButtonRectFinal()).topLeft());
	}
}

QRect StickersListWidget::megagroupSetButtonRectFinal() const {
	auto result = QRect();
	if (_section == Section::Stickers) {
		enumerateSections([this, &result](const SectionInfo &info) {
			if (_mySets[info.section].id == Stickers::MegagroupSetId) {
				result = _megagroupSetButtonRect.translated(0, info.rowsTop);
				return false;
			}
			return true;
		});
	}
	return result;
}

QSharedPointer<Ui::RippleAnimation> StickersListWidget::createButtonRipple(int section) {
	if (_section == Section::Featured) {
		auto maskSize = QSize(_addWidth - st::stickersTrendingAdd.width, st::stickersTrendingAdd.height);
		auto mask = Ui::RippleAnimation::roundRectMask(maskSize, st::buttonRadius);
		return MakeShared<Ui::RippleAnimation>(st::stickersTrendingAdd.ripple, std::move(mask), [this, section] {
			rtlupdate(featuredAddRect(section));
		});
	}
	auto maskSize = QSize(st::stickerPanRemoveSet.rippleAreaSize, st::stickerPanRemoveSet.rippleAreaSize);
	auto mask = Ui::RippleAnimation::ellipseMask(maskSize);
	return MakeShared<Ui::RippleAnimation>(st::stickerPanRemoveSet.ripple, std::move(mask), [this, section] {
		rtlupdate(removeButtonRect(section));
	});
}

QPoint StickersListWidget::buttonRippleTopLeft(int section) const {
	if (_section == Section::Featured) {
		return myrtlrect(featuredAddRect(section)).topLeft();
	}
	return myrtlrect(removeButtonRect(section)).topLeft() + st::stickerPanRemoveSet.rippleAreaPosition;
}

void StickersListWidget::mouseReleaseEvent(QMouseEvent *e) {
	_previewTimer.stop();

	auto pressed = _pressed;
	setPressed(base::none);
	if (pressed != _selected) {
		update();
	}

	auto activated = ClickHandler::unpressed();
	if (_previewShown) {
		_previewShown = false;
		return;
	}

	_lastMousePosition = e->globalPos();
	updateSelected();

	auto &sets = shownSets();
	if (pressed && pressed == _selected) {
		if (auto sticker = base::get_if<OverSticker>(&pressed)) {
			Assert(sticker->section >= 0 && sticker->section < sets.size());
			auto &set = sets[sticker->section];
			Assert(sticker->index >= 0 && sticker->index < set.pack.size());
			if (stickerHasDeleteButton(set, sticker->index) && sticker->overDelete) {
				if (set.id == Stickers::RecentSetId) {
					removeRecentSticker(sticker->section, sticker->index);
				} else if (set.id == Stickers::FavedSetId) {
					removeFavedSticker(sticker->section, sticker->index);
				} else {
					Unexpected("Single sticker delete click.");
				}
				return;
			}
			emit selected(set.pack[sticker->index]);
		} else if (auto set = base::get_if<OverSet>(&pressed)) {
			Assert(set->section >= 0 && set->section < sets.size());
			displaySet(sets[set->section].id);
		} else if (auto button = base::get_if<OverButton>(&pressed)) {
			Assert(button->section >= 0 && button->section < sets.size());
			if (_section == Section::Featured) {
				installSet(sets[button->section].id);
			} else if (sets[button->section].id == Stickers::MegagroupSetId) {
				auto removeLocally = sets[button->section].pack.empty()
					|| !_megagroupSet->canEditStickers();
				removeMegagroupSet(removeLocally);
			} else {
				removeSet(sets[button->section].id);
			}
		} else if (base::get_if<OverGroupAdd>(&pressed)) {
			Ui::show(Box<StickersBox>(_megagroupSet));
		}
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

void StickersListWidget::removeFavedSticker(int section, int index) {
	if (_section != Section::Stickers || section >= _mySets.size() || _mySets[section].id != Stickers::FavedSetId) {
		return;
	}

	clearSelection();
	auto sticker = _mySets[section].pack[index];
	Stickers::SetFaved(sticker, false);
	auto unfave = true;
	MTP::send(MTPmessages_FaveSticker(sticker->mtpInput(), MTP_bool(unfave)));
}

void StickersListWidget::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePosition = e->globalPos();
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
	_lastMousePosition = QCursor::pos();
	updateSelected();
}

void StickersListWidget::clearSelection() {
	setPressed(base::none);
	setSelected(base::none);
	update();
}

TabbedSelector::InnerFooter *StickersListWidget::getFooter() const {
	return _footer;
}

void StickersListWidget::processHideFinished() {
	clearSelection();
}

void StickersListWidget::processPanelHideFinished() {
	clearInstalledLocally();

	// Preserve panel state through visibility toggles.
	//// Reset to the recent stickers section.
	//if (_section == Section::Featured && (!_footer || !_footer->hasOnlyFeaturedSets())) {
	//	_section = Section::Stickers;
	//	validateSelectedIcon(ValidateIconAnimations::None);
	//}
}

void StickersListWidget::refreshStickers() {
	clearSelection();

	_mySets.clear();
	_favedStickersMap.clear();
	_mySets.reserve(Global::StickerSetsOrder().size() + 3);

	refreshFavedStickers();
	refreshRecentStickers(false);
	refreshMegagroupStickers(GroupStickersPlace::Visible);
	for_const (auto setId, Global::StickerSetsOrder()) {
		appendSet(_mySets, setId, AppendSkip::Archived);
	}
	refreshMegagroupStickers(GroupStickersPlace::Hidden);

	_featuredSets.clear();
	_featuredSets.reserve(Global::FeaturedStickerSetsOrder().size());

	for_const (auto setId, Global::FeaturedStickerSetsOrder()) {
		appendSet(_featuredSets, setId, AppendSkip::Installed);
	}

	auto newHeight = countHeight();
	if (newHeight != height()) {
		resize(width(), newHeight);
	}

	if (_footer) {
		_footer->refreshIcons(ValidateIconAnimations::None);
		if (_footer->hasOnlyFeaturedSets() && _section != Section::Featured) {
			showStickerSet(Stickers::FeaturedSetId);
		}
	}

	_settings->setVisible(_section == Section::Stickers && _mySets.isEmpty());

	_lastMousePosition = QCursor::pos();
	updateSelected();
	update();
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
	if (_footer) {
		_footer->preloadImages();
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
	if (_footer && _footer->hasOnlyFeaturedSets() && _section != Section::Featured) {
		showStickerSet(Stickers::FeaturedSetId);
	}
}

void StickersListWidget::refreshRecentStickers(bool performResize) {
	_custom.clear();
	clearSelection();
	auto &sets = Global::StickerSets();
	auto &recent = cGetRecentStickers();
	auto customIt = sets.constFind(Stickers::CustomSetId);
	auto cloudIt = sets.constFind(Stickers::CloudRecentSetId);

	StickerPack recentPack;
	int customCnt = (customIt == sets.cend()) ? 0 : customIt->stickers.size();
	int cloudCnt = (cloudIt == sets.cend()) ? 0 : cloudIt->stickers.size();
	recentPack.reserve(cloudCnt + recent.size() + customCnt);
	_custom.reserve(cloudCnt + recent.size() + customCnt);
	if (cloudCnt > 0) {
		for_const (auto sticker, cloudIt->stickers) {
			if (!_favedStickersMap.contains(sticker)) {
				recentPack.push_back(sticker);
				_custom.push_back(false);
			}
		}
	}
	for_const (auto &recentSticker, recent) {
		auto sticker = recentSticker.first;
		if (!_favedStickersMap.contains(sticker)) {
			recentPack.push_back(sticker);
			_custom.push_back(false);
		}
	}
	if (customCnt > 0) {
		for_const (auto &sticker, customIt->stickers) {
			auto index = recentPack.indexOf(sticker);
			if (index >= cloudCnt) {
				_custom[index] = true; // mark stickers from recent as custom
			} else if (!_favedStickersMap.contains(sticker)) {
				recentPack.push_back(sticker);
				_custom.push_back(true);
			}
		}
	}
	auto recentIt = std::find_if(_mySets.begin(), _mySets.end(), [](auto &set) {
		return set.id == Stickers::RecentSetId;
	});
	if (!recentPack.empty()) {
		if (recentIt == _mySets.end()) {
			_mySets.push_back(Set(Stickers::RecentSetId, MTPDstickerSet::Flag::f_official | MTPDstickerSet_ClientFlag::f_special, lang(lng_recent_stickers), recentPack.size() * 2, recentPack));
		} else {
			recentIt->pack = recentPack;
		}
	} else if (recentIt != _mySets.end()) {
		_mySets.erase(recentIt);
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

void StickersListWidget::refreshFavedStickers() {
	clearSelection();
	auto &sets = Global::StickerSets();
	auto it = sets.constFind(Stickers::FavedSetId);
	if (it == sets.cend() || it->stickers.isEmpty()) {
		return;
	}
	_mySets.push_back(Set(Stickers::FavedSetId, MTPDstickerSet::Flag::f_official | MTPDstickerSet_ClientFlag::f_special, lang(lng_faved_stickers), it->stickers.size() * 2, it->stickers));
	_favedStickersMap = base::flat_set<not_null<DocumentData*>> { it->stickers.begin(), it->stickers.end() };
}

void StickersListWidget::refreshMegagroupStickers(GroupStickersPlace place) {
	if (!_megagroupSet) {
		return;
	}
	auto canEdit = _megagroupSet->canEditStickers();
	auto isShownHere = [place](bool hidden) {
		return (hidden == (place == GroupStickersPlace::Hidden));
	};
	if (_megagroupSet->mgInfo->stickerSet.type() == mtpc_inputStickerSetEmpty) {
		if (canEdit) {
			auto hidden = Auth().data().isGroupStickersSectionHidden(_megagroupSet->id);
			if (isShownHere(hidden)) {
				_mySets.push_back(Set(Stickers::MegagroupSetId, MTPDstickerSet_ClientFlag::f_special | 0, lang(lng_group_stickers), 0));
			}
		}
		return;
	}
	auto hidden = Auth().data().isGroupStickersSectionHidden(_megagroupSet->id);
	auto removeHiddenForGroup = [this, &hidden] {
		if (hidden) {
			Auth().data().removeGroupStickersSectionHidden(_megagroupSet->id);
			Local::writeUserSettings();
			hidden = false;
		}
	};
	if (canEdit && hidden) {
		removeHiddenForGroup();
	}
	if (_megagroupSet->mgInfo->stickerSet.type() == mtpc_inputStickerSetID) {
		auto &set = _megagroupSet->mgInfo->stickerSet.c_inputStickerSetID();
		auto &sets = Global::StickerSets();
		auto it = sets.constFind(set.vid.v);
		if (it != sets.cend()) {
			auto isInstalled = (it->flags & MTPDstickerSet::Flag::f_installed)
				&& !(it->flags & MTPDstickerSet::Flag::f_archived);
			if (isInstalled && !canEdit) {
				removeHiddenForGroup();
			} else if (isShownHere(hidden)) {
				_mySets.push_back(Set(Stickers::MegagroupSetId, MTPDstickerSet_ClientFlag::f_special | 0, lang(lng_group_stickers), it->stickers.size() + 1, it->stickers));
			}
			return;
		}
	}
	if (!isShownHere(hidden)) {
		return;
	}
	request(MTPmessages_GetStickerSet(_megagroupSet->mgInfo->stickerSet)).done([this](const MTPmessages_StickerSet &result) {
		if (auto set = Stickers::FeedSetFull(result)) {
			refreshStickers();
		}
	}).send();
}

void StickersListWidget::fillIcons(QList<StickerIcon> &icons) {
	icons.clear();
	icons.reserve(_mySets.size() + 1);
	if (Global::FeaturedStickerSetsUnreadCount() && !_featuredSets.isEmpty()) {
		icons.push_back(StickerIcon(Stickers::FeaturedSetId));
	}

	auto i = 0;
	if (i != _mySets.size() && _mySets[i].id == Stickers::FavedSetId) {
		++i;
		icons.push_back(StickerIcon(Stickers::FavedSetId));
	}
	if (i != _mySets.size() && _mySets[i].id == Stickers::RecentSetId) {
		++i;
		icons.push_back(StickerIcon(Stickers::RecentSetId));
	}
	for (auto l = _mySets.size(); i != l; ++i) {
		if (_mySets[i].id == Stickers::MegagroupSetId) {
			icons.push_back(StickerIcon(Stickers::MegagroupSetId));
			icons.back().megagroup = _megagroupSet;
			continue;
		}
		auto s = _mySets[i].pack[0];
		auto availw = st::emojiCategory.width - 2 * st::stickerIconPadding, availh = st::emojiCategory.height - 2 * st::stickerIconPadding;
		auto thumbw = s->thumb->width(), thumbh = s->thumb->height(), pixw = 1, pixh = 1;
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

	if (!Global::FeaturedStickerSetsUnreadCount() && !_featuredSets.isEmpty()) {
		icons.push_back(StickerIcon(Stickers::FeaturedSetId));
	}
}

bool StickersListWidget::preventAutoHide() {
	return _removingSetId != 0 || _displayingSetId != 0;
}

void StickersListWidget::updateSelected() {
	if (_pressed && !_previewShown) {
		return;
	}

	auto newSelected = OverState { base::none };
	auto p = mapFromGlobal(_lastMousePosition);
	if (!rect().contains(p)
		|| p.y() < getVisibleTop() || p.y() >= getVisibleBottom()
		|| !isVisible()) {
		clearSelection();
		return;
	}
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
					newSelected = OverButton { section };
				} else {
					newSelected = OverSet { section };
				}
			} else if (yOffset >= st::stickersTrendingHeader  && yOffset < st::stickersTrendingHeader + st::stickerPanSize.height()) {
				if (sx >= 0 && sx < kStickersPanelPerRow * st::stickerPanSize.width()) {
					auto index = qFloor(sx / st::stickerPanSize.width());
					if (index >= 0 && index < set.pack.size()) {
						auto overDelete = false;
						newSelected = OverSticker { section, index, overDelete };
					}
				}
			}
		}
	} else if (!_mySets.empty()) {
		auto info = sectionInfoByOffset(p.y());
		auto section = info.section;
		if (p.y() >= info.top && p.y() < info.rowsTop) {
			if (hasRemoveButton(section) && myrtlrect(removeButtonRect(section)).contains(p.x(), p.y())) {
				newSelected = OverButton { section };
			} else if (!(sets[section].flags & MTPDstickerSet_ClientFlag::f_special)) {
				newSelected = OverSet { section };
			} else if (sets[section].id == Stickers::MegagroupSetId
					&& (_megagroupSet->canEditStickers() || !sets[section].pack.empty())) {
				newSelected = OverSet { section };
			}
		} else if (p.y() >= info.rowsTop && p.y() < info.rowsBottom && sx >= 0) {
			auto yOffset = p.y() - info.rowsTop;
			auto &set = sets[section];
			if (set.id == Stickers::MegagroupSetId && set.pack.empty()) {
				if (_megagroupSetButtonRect.contains(stickersLeft() + sx, yOffset)) {
					newSelected = OverGroupAdd {};
				}
			} else {
				auto special = ((set.flags & MTPDstickerSet::Flag::f_official) != 0);
				auto rowIndex = qFloor(yOffset / st::stickerPanSize.height());
				auto columnIndex = qFloor(sx / st::stickerPanSize.width());
				auto index = rowIndex * kStickersPanelPerRow + columnIndex;
				if (index >= 0 && index < set.pack.size()) {
					auto overDelete = false;
					if (stickerHasDeleteButton(set, index)) {
						auto inx = sx - (columnIndex * st::stickerPanSize.width());
						auto iny = yOffset - (rowIndex * st::stickerPanSize.height());
						if (inx >= st::stickerPanSize.width() - st::stickerPanDelete.width() && iny < st::stickerPanDelete.height()) {
							overDelete = true;
						}
					}
					newSelected = OverSticker { section, index, overDelete };
				}
			}
		}
	}

	setSelected(newSelected);
}

bool StickersListWidget::setHasTitle(const Set &set) const {
	if (set.id == Stickers::FavedSetId) {
		return false;
	} else if (set.id == Stickers::RecentSetId) {
		return !_mySets.empty() && _mySets[0].id == Stickers::FavedSetId;
	}
	return true;
}

bool StickersListWidget::stickerHasDeleteButton(const Set &set, int index) const {
	if (set.id == Stickers::RecentSetId) {
		Assert(index >= 0 && index < _custom.size());
		return _custom[index];
	}
	return (set.id == Stickers::FavedSetId);
}

void StickersListWidget::setSelected(OverState newSelected) {
	if (_selected != newSelected) {
		setCursor(newSelected ? style::cur_pointer : style::cur_default);

		auto &sets = shownSets();
		auto updateSelected = [this, &sets]() {
			if (auto sticker = base::get_if<OverSticker>(&_selected)) {
				rtlupdate(stickerRect(sticker->section, sticker->index));
			} else if (auto button = base::get_if<OverButton>(&_selected)) {
				if (_section == Section::Featured) {
					rtlupdate(featuredAddRect(button->section));
				} else {
					rtlupdate(removeButtonRect(button->section));
				}
			}
		};
		updateSelected();
		_selected = newSelected;
		updateSelected();

		if (_previewShown && _pressed != _selected) {
			if (auto sticker = base::get_if<OverSticker>(&_selected)) {
				_pressed = _selected;
				Assert(sticker->section >= 0 && sticker->section < sets.size());
				auto &set = sets[sticker->section];
				Assert(sticker->index >= 0 && sticker->index < set.pack.size());
				Ui::showMediaPreview(set.pack[sticker->index]);
			}
		}
	}
}

void StickersListWidget::onSettings() {
	Ui::show(Box<StickersBox>(StickersBox::Section::Installed));
}

void StickersListWidget::onPreview() {
	if (auto sticker = base::get_if<OverSticker>(&_pressed)) {
		auto &sets = shownSets();
		Assert(sticker->section >= 0 && sticker->section < sets.size());
		auto &set = sets[sticker->section];
		Assert(sticker->index >= 0 && sticker->index < set.pack.size());
		Ui::showMediaPreview(set.pack[sticker->index]);
		_previewShown = true;
	}
}

void StickersListWidget::showStickerSet(uint64 setId) {
	clearSelection();

	if (setId == Stickers::FeaturedSetId) {
		if (_section != Section::Featured) {
			_section = Section::Featured;

			refreshRecentStickers(true);
			if (_footer) {
				_footer->refreshIcons(ValidateIconAnimations::Scroll);
			}
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

	if (needRefresh && _footer) {
		_footer->refreshIcons(ValidateIconAnimations::Scroll);
	}

	_lastMousePosition = QCursor::pos();

	update();
}

void StickersListWidget::refreshMegagroupSetGeometry() {
	auto left = megagroupSetInfoLeft();
	auto availableWidth = (width() - left);
	auto top = _megagroupSetAbout.countHeight(availableWidth) + st::stickerGroupCategoryAddMargin.top();
	_megagroupSetButtonTextWidth = st::stickerGroupCategoryAdd.font->width(_megagroupSetButtonText);
	auto buttonWidth = _megagroupSetButtonTextWidth - st::stickerGroupCategoryAdd.width;
	_megagroupSetButtonRect = QRect(left, top, buttonWidth, st::stickerGroupCategoryAdd.height);
}

void StickersListWidget::showMegagroupSet(ChannelData *megagroup) {
	Expects(!megagroup || megagroup->isMegagroup());
	if (_megagroupSet != megagroup) {
		_megagroupSet = megagroup;

		if (_megagroupSetAbout.isEmpty()) {
			_megagroupSetAbout.setText(st::stickerGroupCategoryAbout, lang(lng_group_stickers_description));
			_megagroupSetButtonText = lang(lng_group_stickers_add).toUpper();
			refreshMegagroupSetGeometry();
		}
		_megagroupSetButtonRipple.reset();

		refreshStickers();
	}
}

void StickersListWidget::displaySet(uint64 setId) {
	if (setId == Stickers::MegagroupSetId) {
		if (_megagroupSet->canEditStickers()) {
			_displayingSetId = setId;
			auto box = Ui::show(Box<StickersBox>(_megagroupSet));
			connect(box, &QObject::destroyed, this, [this] {
				_displayingSetId = 0;
				emit checkForHide();
			});
			return;
		} else if (_megagroupSet->mgInfo->stickerSet.type() == mtpc_inputStickerSetID) {
			setId = _megagroupSet->mgInfo->stickerSet.c_inputStickerSetID().vid.v;
		} else {
			return;
		}
	}
	auto &sets = Global::StickerSets();
	auto it = sets.constFind(setId);
	if (it != sets.cend()) {
		_displayingSetId = setId;
		auto box = Ui::show(Box<StickerSetBox>(Stickers::inputSetId(*it)), KeepOtherLayers);
		connect(box, &QObject::destroyed, this, [this] {
			_displayingSetId = 0;
			emit checkForHide();
		});
	}
}

void StickersListWidget::installSet(uint64 setId) {
	auto &sets = Global::StickerSets();
	auto it = sets.constFind(setId);
	if (it != sets.cend()) {
		request(MTPmessages_InstallStickerSet(Stickers::inputSetId(*it), MTP_bool(false))).done([this](const MTPmessages_StickerSetInstallResult &result) {
			if (result.type() == mtpc_messages_stickerSetInstallResultArchive) {
				Stickers::ApplyArchivedResult(result.c_messages_stickerSetInstallResultArchive());
			}
		}).fail([this, setId](const RPCError &error) {
			notInstalledLocally(setId);
			Stickers::UndoInstallLocally(setId);
		}).send();

		installedLocally(setId);
		Stickers::InstallLocally(setId);
	}
}

void StickersListWidget::removeMegagroupSet(bool locally) {
	if (locally) {
		Auth().data().setGroupStickersSectionHidden(_megagroupSet->id);
		Local::writeUserSettings();
		refreshStickers();
		return;
	}
	_removingSetId = Stickers::MegagroupSetId;
	Ui::show(Box<ConfirmBox>(lang(lng_stickers_remove_group_set), base::lambda_guarded(this, [this, group = _megagroupSet] {
		Expects(group->mgInfo != nullptr);
		if (group->mgInfo->stickerSet.type() != mtpc_inputStickerSetEmpty) {
			Auth().api().setGroupStickerSet(group, MTP_inputStickerSetEmpty());
		}
		Ui::hideLayer();
		_removingSetId = 0;
		emit checkForHide();
	}), base::lambda_guarded(this, [this] {
		_removingSetId = 0;
		emit checkForHide();
	})));
}

void StickersListWidget::removeSet(uint64 setId) {
	auto &sets = Global::StickerSets();
	auto it = sets.constFind(setId);
	if (it != sets.cend()) {
		_removingSetId = it->id;
		auto text = lng_stickers_remove_pack(lt_sticker_pack, it->title);
		Ui::show(Box<ConfirmBox>(text, lang(lng_box_remove), base::lambda_guarded(this, [this] {
			Ui::hideLayer();
			auto &sets = Global::RefStickerSets();
			auto it = sets.find(_removingSetId);
			if (it != sets.cend()) {
				if (it->id && it->access) {
					request(MTPmessages_UninstallStickerSet(MTP_inputStickerSetID(MTP_long(it->id), MTP_long(it->access)))).send();
				} else if (!it->shortName.isEmpty()) {
					request(MTPmessages_UninstallStickerSet(MTP_inputStickerSetShortName(MTP_string(it->shortName)))).send();
				}
				auto writeRecent = false;
				auto &recent = cGetRecentStickers();
				for (auto i = recent.begin(); i != recent.cend();) {
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
			emit checkForHide();
		}), base::lambda_guarded(this, [this] {
			_removingSetId = 0;
			emit checkForHide();
		})));
	}
}

StickersListWidget::~StickersListWidget() = default;

} // namespace ChatHelpers
