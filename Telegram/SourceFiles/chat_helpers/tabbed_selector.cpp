/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/tabbed_selector.h"

#include "chat_helpers/emoji_list_widget.h"
#include "chat_helpers/stickers_list_widget.h"
#include "chat_helpers/gifs_list_widget.h"
#include "chat_helpers/send_context_menu.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/image/image_prepare.h"
#include "ui/cached_round_corners.h"
#include "window/window_session_controller.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "storage/localstorage.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/stickers/data_stickers.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "styles/style_chat_helpers.h"

namespace ChatHelpers {

class TabbedSelector::SlideAnimation : public Ui::RoundShadowAnimation {
public:
	enum class Direction {
		LeftToRight,
		RightToLeft,
	};
	void setFinalImages(Direction direction, QImage &&left, QImage &&right, QRect inner, bool wasSectionIcons);

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
	bool _wasSectionIcons = false;

};

void TabbedSelector::SlideAnimation::setFinalImages(Direction direction, QImage &&left, QImage &&right, QRect inner, bool wasSectionIcons) {
	Expects(!started());
	_direction = direction;
	_leftImage = QPixmap::fromImage(std::move(left).convertToFormat(QImage::Format_ARGB32_Premultiplied), Qt::ColorOnly);
	_rightImage = QPixmap::fromImage(std::move(right).convertToFormat(QImage::Format_ARGB32_Premultiplied), Qt::ColorOnly);

	Assert(!_leftImage.isNull());
	Assert(!_rightImage.isNull());
	_width = _leftImage.width();
	_height = _rightImage.height();
	Assert(!(_width % cIntRetinaFactor()));
	Assert(!(_height % cIntRetinaFactor()));
	Assert(_leftImage.devicePixelRatio() == _rightImage.devicePixelRatio());
	Assert(_rightImage.width() == _width);
	Assert(_rightImage.height() == _height);
	Assert(QRect(0, 0, _width, _height).contains(inner));
	_innerLeft = inner.x();
	_innerTop = inner.y();
	_innerWidth = inner.width();
	_innerHeight = inner.height();
	Assert(!(_innerLeft % cIntRetinaFactor()));
	Assert(!(_innerTop % cIntRetinaFactor()));
	Assert(!(_innerWidth % cIntRetinaFactor()));
	Assert(!(_innerHeight % cIntRetinaFactor()));
	_innerRight = _innerLeft + _innerWidth;
	_innerBottom = _innerTop + _innerHeight;

	_painterInnerLeft = _innerLeft / cIntRetinaFactor();
	_painterInnerTop = _innerTop / cIntRetinaFactor();
	_painterInnerRight = _innerRight / cIntRetinaFactor();
	_painterInnerBottom = _innerBottom / cIntRetinaFactor();
	_painterInnerWidth = _innerWidth / cIntRetinaFactor();
	_painterInnerHeight = _innerHeight / cIntRetinaFactor();
	_painterCategoriesTop = _painterInnerBottom - st::emojiFooterHeight;

	_wasSectionIcons = wasSectionIcons;
}

void TabbedSelector::SlideAnimation::start() {
	Assert(!_leftImage.isNull());
	Assert(!_rightImage.isNull());
	RoundShadowAnimation::start(_width, _height, _leftImage.devicePixelRatio());
	auto checkCorner = [this](const Corner &corner) {
		if (!corner.valid()) return;
		Assert(corner.width <= _innerWidth);
		Assert(corner.height <= _innerHeight);
	};
	checkCorner(_topLeft);
	checkCorner(_topRight);
	checkCorner(_bottomLeft);
	checkCorner(_bottomRight);
	_frameIntsPerLineAdd = (_width - _innerWidth) + _frameIntsPerLineAdded;
}

void TabbedSelector::SlideAnimation::paintFrame(QPainter &p, float64 dt, float64 opacity) {
	Expects(started());
	Expects(dt >= 0.);

	_frameAlpha = anim::interpolate(1, 256, opacity);

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
	auto leftTo = _innerLeft
		+ std::clamp(_innerWidth + leftCoord, 0, _innerWidth);
	auto rightFrom = _innerLeft + std::clamp(rightCoord, 0, _innerWidth);
	auto painterRightFrom = rightFrom / cIntRetinaFactor();
	if (opacity < 1.) {
		_frame.fill(Qt::transparent);
	}
	{
		Painter p(&_frame);
		p.setOpacity(opacity);
		p.fillRect(_painterInnerLeft, _painterInnerTop, _painterInnerWidth, _painterCategoriesTop - _painterInnerTop, st::emojiPanBg);
		p.fillRect(_painterInnerLeft, _painterCategoriesTop, _painterInnerWidth, _painterInnerBottom - _painterCategoriesTop, _wasSectionIcons ? st::emojiPanCategories : st::emojiPanBg);
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
	//auto frameInts = _frameInts;
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

TabbedSelector::Tab::Tab(
	SelectorTab type,
	int index,
	object_ptr<Inner> widget)
: _type(type)
, _index(index)
, _widget(std::move(widget))
, _weak(_widget)
, _footer(_widget ? _widget->createFooter() : nullptr) {
	if (_footer) {
		_footer->setParent(_widget->parentWidget());
	}
}

object_ptr<TabbedSelector::Inner> TabbedSelector::Tab::takeWidget() {
	return std::move(_widget);
}

void TabbedSelector::Tab::returnWidget(object_ptr<Inner> widget) {
	Expects(widget == _weak);

	_widget = std::move(widget);
}

void TabbedSelector::Tab::saveScrollTop() {
	Expects(widget() != nullptr);

	_scrollTop = widget()->getVisibleTop();
}

TabbedSelector::TabbedSelector(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	Mode mode)
: RpWidget(parent)
, _controller(controller)
, _mode(mode)
, _topShadow(full() ? object_ptr<Ui::PlainShadow>(this) : nullptr)
, _bottomShadow(this)
, _scroll(this, st::emojiScroll)
, _tabs([&] {
	std::vector<Tab> tabs;
	if (full()) {
		tabs.reserve(3);
		tabs.push_back(createTab(SelectorTab::Emoji, 0));
		tabs.push_back(createTab(SelectorTab::Stickers, 1));
		tabs.push_back(createTab(SelectorTab::Gifs, 2));
	} else if (mediaEditor()) {
		tabs.reserve(2);
		tabs.push_back(createTab(SelectorTab::Stickers, 0));
		tabs.push_back(createTab(SelectorTab::Masks, 1));
	} else {
		tabs.reserve(1);
		tabs.push_back(createTab(SelectorTab::Emoji, 0));
	}
	return tabs;
}())
, _currentTabType(full()
		? session().settings().selectorTab()
		: mediaEditor()
		? SelectorTab::Stickers
		: SelectorTab::Emoji)
, _hasEmojiTab(ranges::contains(_tabs, SelectorTab::Emoji, &Tab::type))
, _hasStickersTab(ranges::contains(_tabs, SelectorTab::Stickers, &Tab::type))
, _hasGifsTab(ranges::contains(_tabs, SelectorTab::Gifs, &Tab::type))
, _hasMasksTab(ranges::contains(_tabs, SelectorTab::Masks, &Tab::type))
, _tabbed(_tabs.size() > 1) {
	resize(st::emojiPanWidth, st::emojiPanMaxHeight);

	for (auto &tab : _tabs) {
		tab.footer()->hide();
		tab.widget()->hide();
	}
	if (tabbed()) {
		createTabsSlider();
	}
	setWidgetToScrollArea();

	_bottomShadow->setGeometry(
		0,
		_scroll->y() + _scroll->height() - st::lineWidth,
		width(),
		st::lineWidth);

	for (auto &tab : _tabs) {
		const auto widget = tab.widget();

		widget->scrollToRequests(
		) | rpl::start_with_next([=, tab = &tab](int y) {
			if (tab == currentTab()) {
				scrollToY(y);
			} else {
				tab->saveScrollTop(y);
			}
		}, widget->lifetime());

		widget->disableScrollRequests(
		) | rpl::start_with_next([=, tab = &tab](bool disabled) {
			if (tab == currentTab()) {
				_scroll->disableScroll(disabled);
			}
		}, widget->lifetime());
	}

	rpl::merge(
		(hasStickersTab()
			? stickers()->scrollUpdated() | rpl::map_to(0)
			: rpl::never<int>() | rpl::type_erased()),
		_scroll->scrollTopChanges()
	) | rpl::start_with_next([=] {
		handleScroll();
	}, lifetime());

	if (_topShadow) {
		_topShadow->raise();
	}
	_bottomShadow->raise();
	if (_tabsSlider) {
		_tabsSlider->raise();
	}

	if (hasStickersTab() || hasGifsTab()) {
		session().changes().peerUpdates(
			Data::PeerUpdate::Flag::Rights
		) | rpl::filter([=](const Data::PeerUpdate &update) {
			return (update.peer.get() == _currentPeer);
		}) | rpl::start_with_next([=] {
			checkRestrictedPeer();
		}, lifetime());
	}

	if (hasStickersTab()) {
		session().data().stickers().stickerSetInstalled(
		) | rpl::start_with_next([=](uint64 setId) {
			_tabsSlider->setActiveSection(indexByType(SelectorTab::Stickers));
			stickers()->showStickerSet(setId);
			_showRequests.fire({});
		}, lifetime());

		session().data().stickers().updated(
		) | rpl::start_with_next([=] {
			refreshStickers();
		}, lifetime());
		refreshStickers();
	}
	//setAttribute(Qt::WA_AcceptTouchEvents);
	setAttribute(Qt::WA_OpaquePaintEvent, false);
	showAll();
	hide();
}

TabbedSelector::~TabbedSelector() = default;

Main::Session &TabbedSelector::session() const {
	return _controller->session();
}

TabbedSelector::Tab TabbedSelector::createTab(SelectorTab type, int index) {
	auto createWidget = [&]() -> object_ptr<Inner> {
		switch (type) {
		case SelectorTab::Emoji:
			return object_ptr<EmojiListWidget>(this, _controller);
		case SelectorTab::Stickers:
			return object_ptr<StickersListWidget>(this, _controller);
		case SelectorTab::Gifs:
			return object_ptr<GifsListWidget>(this, _controller);
		case SelectorTab::Masks:
			return object_ptr<StickersListWidget>(this, _controller, true);
		}
		Unexpected("Type in TabbedSelector::createTab.");
	};
	return Tab{ type, index, createWidget() };
}

bool TabbedSelector::full() const {
	return (_mode == Mode::Full);
}

bool TabbedSelector::mediaEditor() const {
	return (_mode == Mode::MediaEditor);
}

bool TabbedSelector::tabbed() const {
	return _tabbed;
}

bool TabbedSelector::hasEmojiTab() const {
	return _hasEmojiTab;
}

bool TabbedSelector::hasStickersTab() const {
	return _hasStickersTab;
}

bool TabbedSelector::hasGifsTab() const {
	return _hasGifsTab;
}

bool TabbedSelector::hasMasksTab() const {
	return _hasMasksTab;
}

rpl::producer<EmojiPtr> TabbedSelector::emojiChosen() const {
	return emoji()->chosen();
}

rpl::producer<TabbedSelector::FileChosen> TabbedSelector::fileChosen() const {
	auto never = rpl::never<TabbedSelector::FileChosen>(
	) | rpl::type_erased();
	return rpl::merge(
		hasStickersTab() ? stickers()->chosen() : never,
		hasGifsTab() ? gifs()->fileChosen() : never,
		hasMasksTab() ? masks()->chosen() : never);
}

auto TabbedSelector::photoChosen() const
-> rpl::producer<TabbedSelector::PhotoChosen>{
	return hasGifsTab() ? gifs()->photoChosen() : nullptr;
}

auto TabbedSelector::inlineResultChosen() const
-> rpl::producer<InlineChosen> {
	return hasGifsTab() ? gifs()->inlineResultChosen() : nullptr;
}

rpl::producer<> TabbedSelector::cancelled() const {
	return hasGifsTab() ? gifs()->cancelRequests() : nullptr;
}

rpl::producer<> TabbedSelector::checkForHide() const {
	auto never = rpl::never<>();
	return rpl::merge(
		hasStickersTab() ? stickers()->checkForHide() : never,
		hasMasksTab() ? masks()->checkForHide() : never);
}

rpl::producer<> TabbedSelector::slideFinished() const {
	return _slideFinished.events();
}

void TabbedSelector::updateTabsSliderGeometry() {
	if (!_tabsSlider) {
		return;
	}
	const auto w = mediaEditor() && hasMasksTab() && masks()->mySetsEmpty()
		? width() / 2
		: width();
	_tabsSlider->resizeToWidth(w);
	_tabsSlider->moveToLeft(0, 0);
}

void TabbedSelector::resizeEvent(QResizeEvent *e) {
	updateTabsSliderGeometry();
	if (_topShadow && _tabsSlider) {
		_topShadow->setGeometry(
			_tabsSlider->x(),
			_tabsSlider->bottomNoMargins() - st::lineWidth,
			_tabsSlider->width(),
			st::lineWidth);
	}

	auto scrollWidth = width() - st::roundRadiusSmall;
	auto scrollHeight = height() - scrollTop() - marginBottom();
	auto inner = currentTab()->widget();
	auto innerWidth = scrollWidth - st::emojiScroll.width;
	auto updateScrollGeometry = [&] {
		_scroll->setGeometryToLeft(
			st::roundRadiusSmall,
			scrollTop(),
			scrollWidth,
			scrollHeight);
	};
	auto updateInnerGeometry = [&] {
		auto scrollTop = _scroll->scrollTop();
		auto scrollBottom = scrollTop + scrollHeight;
		inner->setMinimalHeight(innerWidth, scrollHeight);
		inner->setVisibleTopBottom(scrollTop, scrollBottom);
	};
	if (e->oldSize().height() > height()) {
		updateScrollGeometry();
		updateInnerGeometry();
	} else {
		updateInnerGeometry();
		updateScrollGeometry();
	}
	_bottomShadow->setGeometry(
		0,
		_scroll->y() + _scroll->height() - st::lineWidth,
		width(),
		st::lineWidth);
	updateRestrictedLabelGeometry();

	_footerTop = height() - st::emojiFooterHeight;
	for (auto &tab : _tabs) {
		tab.footer()->resizeToWidth(width());
		tab.footer()->moveToLeft(0, _footerTop);
	}

	update();
}

void TabbedSelector::updateRestrictedLabelGeometry() {
	if (!_restrictedLabel) {
		return;
	}

	auto labelWidth = width() - st::stickerPanPadding * 2;
	_restrictedLabel->resizeToWidth(labelWidth);
	_restrictedLabel->moveToLeft(
		(width() - _restrictedLabel->width()) / 2,
		(height() / 3 - _restrictedLabel->height() / 2));
}

void TabbedSelector::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto switching = (_slideAnimation != nullptr);
	if (switching) {
		paintSlideFrame(p);
		if (!_a_slide.animating()) {
			_slideAnimation.reset();
			afterShown();
			_slideFinished.fire({});
		}
	} else {
		paintContent(p);
	}
}

void TabbedSelector::paintSlideFrame(Painter &p) {
	if (_roundRadius > 0) {
		const auto topPart = QRect(
			0,
			0,
			width(),
			_tabsSlider
				? _tabsSlider->height() + _roundRadius
				: 3 * _roundRadius);
		Ui::FillRoundRect(
			p,
			topPart,
			st::emojiPanBg,
			ImageRoundRadius::Small,
			tabbed()
				? RectPart::FullTop | RectPart::NoTopBottom
				: RectPart::FullTop);
	} else if (_tabsSlider) {
		p.fillRect(0, 0, width(), _tabsSlider->height(), st::emojiPanBg);
	}
	auto slideDt = _a_slide.value(1.);
	_slideAnimation->paintFrame(p, slideDt, 1.);
}

void TabbedSelector::paintContent(Painter &p) {
	auto &bottomBg = hasSectionIcons()
		? st::emojiPanCategories
		: st::emojiPanBg;
	if (_roundRadius > 0) {
		const auto topPart = QRect(
			0,
			0,
			width(),
			_tabsSlider
				? _tabsSlider->height() + _roundRadius
				: 3 * _roundRadius);
		Ui::FillRoundRect(
			p,
			topPart,
			st::emojiPanBg,
			ImageRoundRadius::Small,
			tabbed()
				? RectPart::FullTop | RectPart::NoTopBottom
				: RectPart::FullTop);

		const auto bottomPart = QRect(
			0,
			_footerTop - _roundRadius,
			width(),
			st::emojiFooterHeight + _roundRadius);
		Ui::FillRoundRect(
			p,
			bottomPart,
			bottomBg,
			ImageRoundRadius::Small,
			RectPart::NoTopBottom | RectPart::FullBottom);
	} else {
		if (_tabsSlider) {
			p.fillRect(0, 0, width(), _tabsSlider->height(), st::emojiPanBg);
		}
		p.fillRect(0, _footerTop, width(), st::emojiFooterHeight, bottomBg);
	}

	auto sidesTop = marginTop();
	auto sidesHeight = height() - sidesTop - marginBottom();
	if (_restrictedLabel) {
		p.fillRect(0, sidesTop, width(), sidesHeight, st::emojiPanBg);
	} else {
		p.fillRect(
			myrtlrect(
				width() - st::emojiScroll.width,
				sidesTop,
				st::emojiScroll.width,
				sidesHeight),
			st::emojiPanBg);
		p.fillRect(
			myrtlrect(0, sidesTop, st::roundRadiusSmall, sidesHeight),
			st::emojiPanBg);
	}
}

int TabbedSelector::marginTop() const {
	return _tabsSlider
		? (_tabsSlider->height() - st::lineWidth)
		: _roundRadius;
}

int TabbedSelector::scrollTop() const {
	return tabbed() ? marginTop() : 0;
}

int TabbedSelector::marginBottom() const {
	return st::emojiFooterHeight;
}

void TabbedSelector::refreshStickers() {
	if (hasStickersTab()) {
		stickers()->refreshStickers();
		if (isHidden() || _currentTabType != SelectorTab::Stickers) {
			stickers()->preloadImages();
		}
	}
	if (hasMasksTab()) {
		const auto masksList = masks();
		masksList->refreshStickers();
		if (isHidden() || _currentTabType != SelectorTab::Masks) {
			masksList->preloadImages();
		}

		fillTabsSliderSections();
		updateTabsSliderGeometry();
		if (hasStickersTab() && masksList->mySetsEmpty()) {
			_tabsSlider->setActiveSection(indexByType(SelectorTab::Stickers));
		}
	}
}

bool TabbedSelector::preventAutoHide() const {
	return (hasStickersTab() ? stickers()->preventAutoHide() : false)
		|| (hasMasksTab() ? masks()->preventAutoHide() : false)
		|| hasMenu();
}

bool TabbedSelector::hasMenu() const {
	return (_menu && !_menu->empty());
}

QImage TabbedSelector::grabForAnimation() {
	auto slideAnimationData = base::take(_slideAnimation);
	auto slideAnimation = base::take(_a_slide);

	showAll();
	if (_topShadow) {
		_topShadow->hide();
	}
	if (_tabsSlider) {
		_tabsSlider->hide();
	}
	Ui::SendPendingMoveResizeEvents(this);

	auto result = QImage(
		size() * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	render(&result);

	_a_slide = base::take(slideAnimation);
	_slideAnimation = base::take(slideAnimationData);

	return result;
}

bool TabbedSelector::floatPlayerHandleWheelEvent(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect TabbedSelector::floatPlayerAvailableRect() const {
	return mapToGlobal(_scroll->geometry());
}

void TabbedSelector::hideFinished() {
	for (auto &tab : _tabs) {
		tab.widget()->panelHideFinished();
	}
	_a_slide.stop();
	_slideAnimation.reset();
}

void TabbedSelector::showStarted() {
	if (hasStickersTab()) {
		session().api().updateStickers();
	}
	if (hasMasksTab()) {
		session().api().updateMasks();
	}
	currentTab()->widget()->refreshRecent();
	currentTab()->widget()->preloadImages();
	_a_slide.stop();
	_slideAnimation.reset();
	showAll();
}

void TabbedSelector::beforeHiding() {
	if (!_scroll->isHidden()) {
		currentTab()->widget()->beforeHiding();
		if (_beforeHidingCallback) {
			_beforeHidingCallback(_currentTabType);
		}
	}
}

void TabbedSelector::afterShown() {
	if (!_a_slide.animating()) {
		showAll();
		currentTab()->widget()->afterShown();
		if (_afterShownCallback) {
			_afterShownCallback(_currentTabType);
		}
	}
}

void TabbedSelector::setCurrentPeer(PeerData *peer) {
	if (hasGifsTab()) {
		gifs()->setInlineQueryPeer(peer);
	}
	_currentPeer = peer;
	checkRestrictedPeer();
	if (hasStickersTab()) {
		stickers()->showMegagroupSet(peer ? peer->asMegagroup() : nullptr);
	}
}

void TabbedSelector::checkRestrictedPeer() {
	if (_currentPeer) {
		const auto error = (_currentTabType == SelectorTab::Stickers)
			? Data::RestrictionError(
				_currentPeer,
				ChatRestriction::SendStickers)
			: (_currentTabType == SelectorTab::Gifs)
			? Data::RestrictionError(
				_currentPeer,
				ChatRestriction::SendGifs)
			: std::nullopt;
		if (error) {
			if (!_restrictedLabel) {
				_restrictedLabel.create(
					this,
					*error,
					st::stickersRestrictedLabel);
				_restrictedLabel->show();
				updateRestrictedLabelGeometry();
				currentTab()->footer()->hide();
				_scroll->hide();
				_bottomShadow->hide();
				update();
			}
			return;
		}
	}
	if (_restrictedLabel) {
		_restrictedLabel.destroy();
		if (!_a_slide.animating()) {
			currentTab()->footer()->show();
			_scroll->show();
			_bottomShadow->setVisible(_currentTabType == SelectorTab::Gifs);
			update();
		}
	}
}

bool TabbedSelector::isRestrictedView() {
	checkRestrictedPeer();
	return (_restrictedLabel != nullptr);
}

void TabbedSelector::showAll() {
	if (isRestrictedView()) {
		_restrictedLabel->show();
	} else {
		currentTab()->footer()->show();
		_scroll->show();
		_bottomShadow->setVisible(_currentTabType == SelectorTab::Gifs);
	}
	if (_topShadow) {
		_topShadow->show();
	}
	if (_tabsSlider) {
		_tabsSlider->show();
	}
}

void TabbedSelector::hideForSliding() {
	hideChildren();
	if (_topShadow) {
		_topShadow->show();
	}
	if (_tabsSlider) {
		_tabsSlider->show();
	}
	currentTab()->widget()->clearSelection();
}

void TabbedSelector::handleScroll() {
	auto scrollTop = _scroll->scrollTop();
	auto scrollBottom = scrollTop + _scroll->height();
	currentTab()->widget()->setVisibleTopBottom(scrollTop, scrollBottom);
}

void TabbedSelector::setRoundRadius(int radius) {
	_roundRadius = radius;
	if (_tabsSlider) {
		_tabsSlider->setRippleTopRoundRadius(_roundRadius);
	}
}

void TabbedSelector::createTabsSlider() {
	_tabsSlider.create(this, st::emojiTabs);

	fillTabsSliderSections();

	_tabsSlider->setActiveSectionFast(indexByType(_currentTabType));
	_tabsSlider->sectionActivated(
	) | rpl::start_with_next([=] {
		switchTab();
	}, lifetime());
}

void TabbedSelector::fillTabsSliderSections() {
	if (!_tabsSlider) {
		return;
	}

	const auto sections = ranges::views::all(
		_tabs
	) | ranges::views::filter([&](const Tab &tab) {
		return (tab.type() == SelectorTab::Masks)
			? !masks()->mySetsEmpty()
			: true;
	}) | ranges::views::transform([&](const Tab &tab) {
		return [&] {
			switch (tab.type()) {
			case SelectorTab::Emoji:
				return tr::lng_switch_emoji;
			case SelectorTab::Stickers:
				return tr::lng_switch_stickers;
			case SelectorTab::Gifs:
				return tr::lng_switch_gifs;
			case SelectorTab::Masks:
				return tr::lng_switch_masks;
			}
			Unexpected("SelectorTab value in fillTabsSliderSections.");
		}()(tr::now).toUpper();
	}) | ranges::to_vector;
	_tabsSlider->setSections(sections);
}

bool TabbedSelector::hasSectionIcons() const {
	return (_currentTabType != SelectorTab::Gifs) && !_restrictedLabel;
}

void TabbedSelector::switchTab() {
	Expects(tabbed());

	const auto tab = _tabsSlider->activeSection();
	Assert(tab >= 0 && tab < _tabs.size());
	const auto newTabType = typeByIndex(tab);
	if (_currentTabType == newTabType) {
		_scroll->scrollToY(0);
		return;
	}

	const auto wasSectionIcons = hasSectionIcons();
	const auto wasIndex = indexByType(_currentTabType);
	currentTab()->saveScrollTop();

	beforeHiding();

	auto wasCache = grabForAnimation();

	auto widget = _scroll->takeWidget<Inner>();
	widget->setParent(this);
	widget->hide();
	currentTab()->footer()->hide();
	currentTab()->returnWidget(std::move(widget));

	_currentTabType = newTabType;
	_restrictedLabel.destroy();
	checkRestrictedPeer();

	currentTab()->widget()->refreshRecent();
	currentTab()->widget()->preloadImages();
	setWidgetToScrollArea();

	auto nowCache = grabForAnimation();

	auto direction = (wasIndex > indexByType(_currentTabType))
		? SlideAnimation::Direction::LeftToRight
		: SlideAnimation::Direction::RightToLeft;
	if (direction == SlideAnimation::Direction::LeftToRight) {
		std::swap(wasCache, nowCache);
	}
	_slideAnimation = std::make_unique<SlideAnimation>();
	const auto slidingRect = QRect(
		0,
		_scroll->y() * cIntRetinaFactor(),
		width() * cIntRetinaFactor(),
		(height() - _scroll->y()) * cIntRetinaFactor());
	_slideAnimation->setFinalImages(
		direction,
		std::move(wasCache),
		std::move(nowCache),
		slidingRect,
		wasSectionIcons);
	_slideAnimation->setCornerMasks(
		Images::CornersMask(ImageRoundRadius::Small));
	_slideAnimation->start();

	hideForSliding();

	getTab(wasIndex)->widget()->hideFinished();

	_a_slide.start(
		[=] { update(); },
		0.,
		1.,
		st::emojiPanSlideDuration,
		anim::linear);
	update();

	if (full()) {
		session().settings().setSelectorTab(_currentTabType);
		session().saveSettingsDelayed();
	}
}

not_null<EmojiListWidget*> TabbedSelector::emoji() const {
	Expects(hasEmojiTab());

	return static_cast<EmojiListWidget*>(
		getTab(indexByType(SelectorTab::Emoji))->widget());
}

not_null<StickersListWidget*> TabbedSelector::stickers() const {
	Expects(hasStickersTab());

	return static_cast<StickersListWidget*>(
		getTab(indexByType(SelectorTab::Stickers))->widget());
}

not_null<GifsListWidget*> TabbedSelector::gifs() const {
	Expects(hasGifsTab());

	return static_cast<GifsListWidget*>(
		getTab(indexByType(SelectorTab::Gifs))->widget());
}

not_null<StickersListWidget*> TabbedSelector::masks() const {
	Expects(hasMasksTab());

	return static_cast<StickersListWidget*>(
		getTab(indexByType(SelectorTab::Masks))->widget());
}

void TabbedSelector::setWidgetToScrollArea() {
	auto inner = _scroll->setOwnedWidget(currentTab()->takeWidget());
	auto innerWidth = _scroll->width() - st::emojiScroll.width;
	auto scrollHeight = _scroll->height();
	inner->setMinimalHeight(innerWidth, scrollHeight);
	inner->moveToLeft(0, 0);
	inner->show();

	_scroll->disableScroll(false);
	scrollToY(currentTab()->getScrollTop());
	handleScroll();
}

void TabbedSelector::scrollToY(int y) {
	_scroll->scrollToY(y);

	// Qt render glitch workaround, shadow sometimes disappears if we just scroll to y.
	if (_topShadow) {
		_topShadow->update();
	}
}

void TabbedSelector::showMenuWithType(SendMenu::Type type) {
	_menu = base::make_unique_q<Ui::PopupMenu>(this);
	currentTab()->widget()->fillContextMenu(_menu, type);

	if (!_menu->empty()) {
		_menu->popup(QCursor::pos());
	}
}

rpl::producer<> TabbedSelector::contextMenuRequested() const {
	return events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return e->type() == QEvent::ContextMenu;
	}) | rpl::to_empty;
}

SelectorTab TabbedSelector::typeByIndex(int index) const {
	for (const auto &tab : _tabs) {
		if (tab.index() == index) {
			return tab.type();
		}
	}
	Unexpected("Type in TabbedSelector::typeByIndex.");
}

int TabbedSelector::indexByType(SelectorTab type) const {
	for (const auto &tab : _tabs) {
		if (tab.type() == type) {
			return tab.index();
		}
	}
	Unexpected("Index in TabbedSelector::indexByType.");
}

not_null<TabbedSelector::Tab*> TabbedSelector::getTab(int index) {
	return &(_tabs[index]);
}

not_null<const TabbedSelector::Tab*> TabbedSelector::getTab(int index) const {
	return &_tabs[index];
}

not_null<TabbedSelector::Tab*> TabbedSelector::currentTab() {
	return &_tabs[indexByType(_currentTabType)];
}

not_null<const TabbedSelector::Tab*> TabbedSelector::currentTab() const {
	return &_tabs[indexByType(_currentTabType)];
}

TabbedSelector::Inner::Inner(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller) {
}

rpl::producer<int> TabbedSelector::Inner::scrollToRequests() const {
	return _scrollToRequests.events();
}

rpl::producer<bool> TabbedSelector::Inner::disableScrollRequests() const {
	return _disableScrollRequests.events();
}

void TabbedSelector::Inner::scrollTo(int y) {
	_scrollToRequests.fire_copy(y);
}

void TabbedSelector::Inner::disableScroll(bool disabled) {
	_disableScrollRequests.fire_copy(disabled);
}

void TabbedSelector::Inner::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
}

void TabbedSelector::Inner::setMinimalHeight(
		int newWidth,
		int newMinimalHeight) {
	if (_minimalHeight != newMinimalHeight) {
		_minimalHeight = newMinimalHeight;
		resizeToWidth(newWidth);
	} else if (newWidth != width()) {
		resizeToWidth(newWidth);
	}
}

int TabbedSelector::Inner::resizeGetHeight(int newWidth) {
	auto result = std::max(
		countDesiredHeight(newWidth),
		minimalHeight());
	if (result != height()) {
		update();
	}
	return result;
}

int TabbedSelector::Inner::minimalHeight() const {
	return (_minimalHeight > 0)
		? _minimalHeight
		: (st::emojiPanMaxHeight - st::emojiFooterHeight);
}

void TabbedSelector::Inner::hideFinished() {
	processHideFinished();
	if (auto footer = getFooter()) {
		footer->processHideFinished();
	}
}

void TabbedSelector::Inner::panelHideFinished() {
	hideFinished();
	processPanelHideFinished();
	if (auto footer = getFooter()) {
		footer->processPanelHideFinished();
	}
}

TabbedSelector::InnerFooter::InnerFooter(QWidget *parent)
: RpWidget(parent) {
	resize(st::emojiPanWidth, st::emojiFooterHeight);
}

} // namespace ChatHelpers
