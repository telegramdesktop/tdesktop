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
#include "stickers/emoji_panel.h"

#include "stickers/emoji_list_widget.h"
#include "stickers/stickers_list_widget.h"
#include "stickers/gifs_list_widget.h"
#include "styles/style_stickers.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/scroll_area.h"
#include "boxes/stickersetbox.h"
#include "boxes/stickers_box.h"
#include "boxes/confirmbox.h"
#include "inline_bots/inline_bot_result.h"
#include "dialogs/dialogs_layout.h"
#include "stickers/stickers.h"
#include "storage/localstorage.h"
#include "lang.h"
#include "mainwindow.h"

namespace ChatHelpers {
namespace {

constexpr auto kSaveChosenTabTimeout = 1000;

} // namespace

class EmojiPanel::SlideAnimation : public Ui::RoundShadowAnimation {
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

void EmojiPanel::SlideAnimation::setFinalImages(Direction direction, QImage &&left, QImage &&right, QRect inner) {
	Expects(!started());
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

void EmojiPanel::SlideAnimation::start() {
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

void EmojiPanel::SlideAnimation::paintFrame(QPainter &p, float64 dt, float64 opacity) {
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

EmojiPanel::Tab::Tab(TabType type, object_ptr<Inner> widget)
: _type(type)
, _widget(std::move(widget))
, _weak(_widget) {
}

object_ptr<EmojiPanel::Inner> EmojiPanel::Tab::takeWidget() {
	return std::move(_widget);
}

void EmojiPanel::Tab::returnWidget(object_ptr<Inner> widget) {
	_widget = std::move(widget);
	Ensures(_widget == _weak);
}

void EmojiPanel::Tab::saveScrollTop() {
	_scrollTop = widget()->getVisibleTop();
}

EmojiPanel::EmojiPanel(QWidget *parent) : TWidget(parent)
, _recent(this, st::emojiCategoryRecent)
, _people(this, st::emojiCategoryPeople)
, _nature(this, st::emojiCategoryNature)
, _food(this, st::emojiCategoryFood)
, _activity(this, st::emojiCategoryActivity)
, _travel(this, st::emojiCategoryTravel)
, _objects(this, st::emojiCategoryObjects)
, _symbols(this, st::emojiCategorySymbols)
, _a_icons(animation(this, &EmojiPanel::step_icons))
, _scroll(this, st::emojiScroll)
, _tabsSlider(this, st::emojiTabs)
, _topShadow(this, st::shadowFg)
, _bottomShadow(this, st::shadowFg)
, _tabs {
	Tab { TabType::Emoji, object_ptr<EmojiListWidget>(this) },
	Tab { TabType::Stickers, object_ptr<StickersListWidget>(this) },
	Tab { TabType::Gifs, object_ptr<GifsListWidget>(this) },
}
, _currentTabType(AuthSession::Current().data().emojiPanelTab()) {
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
		connect(tab.widget(), &Inner::scrollToY, this, [this, tab = &tab](int y) {
			if (tab == currentTab()) {
				_scroll->scrollToY(y);
			} else {
				tab->saveScrollTop(y);
			}
		});
		connect(tab.widget(), &Inner::disableScroll, this, [this, tab = &tab](bool disabled) {
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

void EmojiPanel::setMinTop(int minTop) {
	_minTop = minTop;
	updateContentHeight();
}

void EmojiPanel::setMinBottom(int minBottom) {
	_minBottom = minBottom;
	updateContentHeight();
}

void EmojiPanel::moveBottom(int bottom) {
	_bottom = bottom;
	updateContentHeight();
}

void EmojiPanel::updateContentHeight() {
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

void EmojiPanel::prepareSection(int &left, int top, int _width, Ui::IconButton *sectionIcon, DBIEmojiSection value) {
	sectionIcon->moveToLeft(left, top, _width);
	left += sectionIcon->width();
	sectionIcon->setClickedCallback([this, value] { setActiveSection(value); });
}

void EmojiPanel::onWndActiveChanged() {
	if (!App::wnd()->windowHandle()->isActive() && !isHidden()) {
		leaveEvent(0);
	}
}

void EmojiPanel::onSaveConfig() {
	Local::writeUserSettings();
}

void EmojiPanel::onSaveConfigDelayed(int delay) {
	_saveConfigTimer.start(delay);
}

void EmojiPanel::paintStickerSettingsIcon(Painter &p) const {
	int settingsLeft = _iconsLeft + 7 * st::emojiCategory.width;
	st::stickersSettings.paint(p, settingsLeft + st::emojiCategory.iconPosition.x(), _iconsTop + st::emojiCategory.iconPosition.y(), width());
}

void EmojiPanel::paintFeaturedStickerSetsBadge(Painter &p, int iconLeft) const {
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

void EmojiPanel::paintEvent(QPaintEvent *e) {
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

void EmojiPanel::paintSlideFrame(Painter &p, TimeMs ms) {
	Ui::Shadow::paint(p, innerRect(), width(), st::emojiPanAnimation.shadow);

	auto inner = innerRect();
	auto topPart = QRect(inner.x(), inner.y(), inner.width(), _tabsSlider->height() + st::buttonRadius);
	App::roundRect(p, topPart, st::emojiPanBg, ImageRoundRadius::Small, App::RectPart::TopFull | App::RectPart::NoTopBottom);

	auto slideDt = _a_slide.current(ms, 1.);
	_slideAnimation->paintFrame(p, slideDt, _a_opacity.current(_hiding ? 0. : 1.));
}

void EmojiPanel::paintContent(Painter &p) {
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

int EmojiPanel::marginTop() const {
	return _tabsSlider->height() - st::lineWidth;
}

int EmojiPanel::marginBottom() const {
	return st::emojiCategory.height;
}

int EmojiPanel::countBottom() const {
	return (parentWidget()->height() - _minBottom);
}

void EmojiPanel::moveByBottom() {
	moveToRight(0, y());
	updateContentHeight();
}

void EmojiPanel::enterEventHook(QEvent *e) {
	showAnimated();
}

bool EmojiPanel::preventAutoHide() const {
	return _removingSetId || _displayingSetId;
}

void EmojiPanel::leaveEventHook(QEvent *e) {
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

void EmojiPanel::otherEnter() {
	showAnimated();
}

void EmojiPanel::otherLeave() {
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

void EmojiPanel::mousePressEvent(QMouseEvent *e) {
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

void EmojiPanel::mouseMoveEvent(QMouseEvent *e) {
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

void EmojiPanel::mouseReleaseEvent(QMouseEvent *e) {
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

bool EmojiPanel::event(QEvent *e) {
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

void EmojiPanel::hideFast() {
	if (isHidden()) return;

	_hideTimer.stop();
	_hiding = false;
	_a_opacity.finish();
	hideFinished();
}

void EmojiPanel::refreshStickers() {
	stickers()->refreshStickers();
	if (isHidden() || _currentTabType != TabType::Stickers) {
		stickers()->preloadImages();
	}
	update();
}

void EmojiPanel::refreshSavedGifs() {
	gifs()->refreshSavedGifs();
	if (isHidden() || _currentTabType != TabType::Gifs) {
		gifs()->preloadImages();
	}
	update();
}

void EmojiPanel::onRefreshIcons(bool scrollAnimation) {
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

void EmojiPanel::leaveToChildEvent(QEvent *e, QWidget *child) {
	if (_currentTabType != TabType::Stickers) {
		return;
	}
	_iconsMousePos = QCursor::pos();
	updateSelected();
}

void EmojiPanel::updateSelected() {
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

void EmojiPanel::updateIcons() {
	if (_currentTabType != TabType::Stickers) {
		return;
	}

	auto verticalInner = rect().marginsRemoved(st::emojiPanMargins).marginsRemoved(QMargins(st::buttonRadius, 0, st::buttonRadius, 0));
	update(verticalInner.left(), _iconsTop, verticalInner.width(), st::emojiCategory.height);
}

void EmojiPanel::step_icons(TimeMs ms, bool timer) {
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

void EmojiPanel::opacityAnimationCallback() {
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

void EmojiPanel::hideByTimerOrLeave() {
	if (isHidden() || preventAutoHide()) return;

	hideAnimated();
}

void EmojiPanel::prepareCache() {
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

void EmojiPanel::startOpacityAnimation(bool hiding) {
	_hiding = false;
	prepareCache();
	_hiding = hiding;
	hideChildren();
	_a_opacity.start([this] { opacityAnimationCallback(); }, _hiding ? 1. : 0., _hiding ? 0. : 1., st::emojiPanDuration);
}

void EmojiPanel::startShowAnimation() {
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

QImage EmojiPanel::grabForComplexAnimation(GrabType type) {
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

void EmojiPanel::hideAnimated() {
	if (isHidden()) return;
	if (_hiding) return;

	_hideTimer.stop();
	if (_a_slide.animating()) {
		_hideAfterSlide = true;
	} else {
		startOpacityAnimation(true);
	}
}

EmojiPanel::~EmojiPanel() = default;

void EmojiPanel::hideFinished() {
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

void EmojiPanel::showAnimated() {
	_hideTimer.stop();
	_hideAfterSlide = false;
	showStarted();
}

void EmojiPanel::showStarted() {
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

bool EmojiPanel::eventFilter(QObject *obj, QEvent *e) {
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

void EmojiPanel::stickersInstalled(uint64 setId) {
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

bool EmojiPanel::ui_isInlineItemBeingChosen() {
	return (_currentTabType == TabType::Gifs && !isHidden());
}

void EmojiPanel::showAll() {
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

void EmojiPanel::hideForSliding() {
	hideChildren();
	_tabsSlider->show();
	_topShadow->show();
	currentTab()->widget()->clearSelection();
}

void EmojiPanel::setActiveSection(DBIEmojiSection tab) {
	emoji()->showEmojiSection(tab);
}

void EmojiPanel::onScroll() {
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

void EmojiPanel::setCurrentSectionIcon(DBIEmojiSection section) {
	_recent->setIconOverride((section == dbiesRecent) ? &st::emojiRecentActive : nullptr);
	_people->setIconOverride((section == dbiesPeople) ? &st::emojiPeopleActive : nullptr);
	_nature->setIconOverride((section == dbiesNature) ? &st::emojiNatureActive : nullptr);
	_food->setIconOverride((section == dbiesFood) ? &st::emojiFoodActive : nullptr);
	_activity->setIconOverride((section == dbiesActivity) ? &st::emojiActivityActive : nullptr);
	_travel->setIconOverride((section == dbiesTravel) ? &st::emojiTravelActive : nullptr);
	_objects->setIconOverride((section == dbiesObjects) ? &st::emojiObjectsActive : nullptr);
	_symbols->setIconOverride((section == dbiesSymbols) ? &st::emojiSymbolsActive : nullptr);
}

void EmojiPanel::validateSelectedIcon(ValidateIconAnimations animations) {
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

style::margins EmojiPanel::innerPadding() const {
	return st::emojiPanMargins;
}

QRect EmojiPanel::innerRect() const {
	return rect().marginsRemoved(innerPadding());
}

QRect EmojiPanel::horizontalRect() const {
	return innerRect().marginsRemoved(style::margins(0, st::buttonRadius, 0, st::buttonRadius));
}

QRect EmojiPanel::verticalRect() const {
	return innerRect().marginsRemoved(style::margins(st::buttonRadius, 0, st::buttonRadius, 0));
}

void EmojiPanel::createTabsSlider() {
	auto sections = QStringList();
	sections.push_back(lang(lng_switch_emoji).toUpper());
	sections.push_back(lang(lng_switch_stickers).toUpper());
	sections.push_back(lang(lng_switch_gifs).toUpper());
	_tabsSlider->setSections(sections);

	_tabsSlider->setActiveSectionFast(static_cast<int>(_currentTabType));
	_tabsSlider->setSectionActivatedCallback([this] {
		switchTab();
	});

	_tabsSlider->resizeToWidth(innerRect().width());
	_tabsSlider->moveToLeft(innerRect().x(), innerRect().y());
	_topShadow->setGeometry(_tabsSlider->x(), _tabsSlider->bottomNoMargins() - st::lineWidth, _tabsSlider->width(), st::lineWidth);
}

void EmojiPanel::switchTab() {
	auto tab = _tabsSlider->activeSection();
	t_assert(tab >= 0 && tab < Tab::kCount);
	auto newTabType = static_cast<TabType>(tab);
	if (_currentTabType == newTabType) {
		return;
	}

	auto wasTab = _currentTabType;
	currentTab()->saveScrollTop();

	auto wasCache = grabForComplexAnimation(GrabType::Slide);

	auto widget = _scroll->takeWidget<Inner>();
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

	AuthSession::Current().data().setEmojiPanelTab(_currentTabType);
	onSaveConfigDelayed(kSaveChosenTabTimeout);
}

gsl::not_null<EmojiListWidget*> EmojiPanel::emoji() const {
	return static_cast<EmojiListWidget*>(getTab(TabType::Emoji)->widget().get());
}

gsl::not_null<StickersListWidget*> EmojiPanel::stickers() const {
	return static_cast<StickersListWidget*>(getTab(TabType::Stickers)->widget().get());
}

gsl::not_null<GifsListWidget*> EmojiPanel::gifs() const {
	return static_cast<GifsListWidget*>(getTab(TabType::Gifs)->widget().get());
}

void EmojiPanel::setWidgetToScrollArea() {
	_scroll->setOwnedWidget(currentTab()->takeWidget());
	_scroll->disableScroll(false);
	currentTab()->widget()->moveToLeft(0, 0);
	currentTab()->widget()->show();
	_scroll->scrollToY(currentTab()->getScrollTop());
	onScroll();
}

void EmojiPanel::onDisplaySet(quint64 setId) {
	auto &sets = Global::StickerSets();
	auto it = sets.constFind(setId);
	if (it != sets.cend()) {
		_displayingSetId = setId;
		auto box = Ui::show(Box<StickerSetBox>(Stickers::inputSetId(*it)), KeepOtherLayers);
		connect(box, SIGNAL(destroyed(QObject*)), this, SLOT(onDelayedHide()));
	}
}

void EmojiPanel::onInstallSet(quint64 setId) {
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

void EmojiPanel::onRemoveSet(quint64 setId) {
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

void EmojiPanel::onDelayedHide() {
	if (!rect().contains(mapFromGlobal(QCursor::pos()))) {
		_hideTimer.start(3000);
	}
	_removingSetId = 0;
	_displayingSetId = 0;
}

void EmojiPanel::clearInlineBot() {
	inlineBotChanged();
}

bool EmojiPanel::overlaps(const QRect &globalRect) const {
	if (isHidden() || !_cache.isNull()) return false;

	auto testRect = QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size());
	auto inner = rect().marginsRemoved(st::emojiPanMargins);
	return inner.marginsRemoved(QMargins(st::buttonRadius, 0, st::buttonRadius, 0)).contains(testRect)
		|| inner.marginsRemoved(QMargins(0, st::buttonRadius, 0, st::buttonRadius)).contains(testRect);
}

void EmojiPanel::inlineBotChanged() {
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

void EmojiPanel::inlineResultsDone(const MTPmessages_BotResults &result) {
	_inlineRequestId = 0;
	Notify::inlineBotRequesting(false);

	auto it = _inlineCache.find(_inlineQuery);
	auto adding = (it != _inlineCache.cend());
	if (result.type() == mtpc_messages_botResults) {
		auto &d = result.c_messages_botResults();
		auto &v = d.vresults.v;
		auto queryId = d.vquery_id.v;

		if (it == _inlineCache.cend()) {
			it = _inlineCache.emplace(_inlineQuery, std::make_unique<InlineCacheEntry>()).first;
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

void EmojiPanel::queryInlineBot(UserData *bot, PeerData *peer, QString query) {
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

void EmojiPanel::onInlineRequest() {
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

void EmojiPanel::onEmptyInlineRows() {
	if (!_inlineBot) {
		gifs()->hideInlineRowsPanel();
	} else {
		gifs()->clearInlineRowsPanel();
	}
}

bool EmojiPanel::refreshInlineRows(int32 *added) {
	auto it = _inlineCache.find(_inlineQuery);
	const InlineCacheEntry *entry = nullptr;
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

int32 EmojiPanel::showInlineRows(bool newResults) {
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

EmojiPanel::Inner::Inner(QWidget *parent) : TWidget(parent) {
}

void EmojiPanel::Inner::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	auto oldVisibleHeight = getVisibleBottom() - getVisibleTop();
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
	auto visibleHeight = getVisibleBottom() - getVisibleTop();
	if (visibleHeight != oldVisibleHeight) {
		resize(st::emojiPanWidth - st::emojiScroll.width - st::buttonRadius, countHeight());
	}
}

} // namespace ChatHelpers
