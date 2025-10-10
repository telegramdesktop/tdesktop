/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/subsection_tabs_slider.h"

#include "dialogs/dialogs_three_state_icon.h"
#include "ui/effects/ripple_animation.h"
#include "ui/widgets/scroll_area.h"
#include "ui/dynamic_image.h"
#include "ui/unread_badge_paint.h"
#include "ui/unread_counter_format.h"
#include "ui/painter.h"
#include "ui/round_rect.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"
#include "styles/style_filter_icons.h"
#include "styles/style_layers.h"

namespace Ui {
namespace {

constexpr auto kMaxNameLines = 3;
constexpr auto kVerticalScale = 0.6;
constexpr auto kHorizontalScale = 0.5;

void PaintPinnedIcon(
		QPainter &p,
		int width,
		int backgroundMargin,
		float64 scale = kVerticalScale,
		bool isHorizontal = false) {
	constexpr auto kOffset = 5;
	p.scale(scale, scale);
	if (isHorizontal) {
		p.translate(
			st::lineWidth * kOffset,
			st::lineWidth * kOffset + backgroundMargin);
	} else {
		p.translate(
			st::lineWidth * kOffset + backgroundMargin,
			st::lineWidth * kOffset);
	}
	st::dialogsPinnedIcon.icon.paint(p, 0, 0, width);
}

class VerticalButton final : public SubsectionButton {
public:
	VerticalButton(
		not_null<QWidget*> parent,
		not_null<SubsectionButtonDelegate*> delegate,
		SubsectionTab &&data);

private:
	void paintEvent(QPaintEvent *e) override;

	void dataUpdatedHook() override;
	void invalidateCache() override;
	QImage prepareRippleMask() const override final {
		return isPinned()
			? _rippleMask
			: Ui::RippleButton::prepareRippleMask();
	}

	void updateSize();
	void paintPinnedBackground(QPainter &p, const QRect &bgRect);
	QPainterPath createClipPath(const QRect &rect) const;
	[[nodiscard]] const QPainterPath &cachedClipPath(const QRect &rect);

	const style::ChatTabsVertical &_st;
	Text::String _text;
	bool _subscribed = false;
	RoundRect _roundRect;
	QImage _rippleMask;
	QPainterPath _clipPathCache;
	QRect _clipPathRect;
	bool _clipPathValid = false;

};

class HorizontalButton final : public SubsectionButton {
public:
	HorizontalButton(
		not_null<QWidget*> parent,
		const style::SettingsSlider &st,
		not_null<SubsectionButtonDelegate*> delegate,
		SubsectionTab &&data);

private:
	void paintEvent(QPaintEvent *e) override;

	void dataUpdatedHook() override;
	void invalidateCache() override;
	QImage prepareRippleMask() const override final {
		return isPinned()
			? _rippleMask
			: Ui::RippleButton::prepareRippleMask();
	}
	void updateSize();
	void paintPinnedBackground(QPainter &p, const QRect &bgRect);
	QPainterPath createClipPath(const QRect &rect) const;
	[[nodiscard]] const QPainterPath &cachedClipPath(const QRect &rect);

	const style::SettingsSlider &_st;
	Text::String _text;
	RoundRect _roundRect;
	QImage _rippleMask;
	QPainterPath _clipPathCache;
	QRect _clipPathRect;
	bool _clipPathValid = false;

};

VerticalButton::VerticalButton(
	not_null<QWidget*> parent,
	not_null<SubsectionButtonDelegate*> delegate,
	SubsectionTab &&data)
: SubsectionButton(parent, delegate, std::move(data))
, _st(st::chatTabsVertical)
, _text(_st.nameStyle, _data.text, kDefaultTextOptions, _st.nameWidth)
, _roundRect(st::boxRadius, st::windowBgOver) {
	updateSize();
}

void VerticalButton::dataUpdatedHook() {
	_text.setMarkedText(_st.nameStyle, _data.text, kDefaultTextOptions);
	updateSize();
}

void VerticalButton::invalidateCache() {
	_roundRect.setColor(st::white);
	if (isPinned()) {
		const auto bgRect = rect()
			- QMargins(_backgroundMargin, 0, _backgroundMargin, 0);
		const auto ratio = style::DevicePixelRatio();
		_rippleMask = QImage(
			bgRect.size() * ratio,
			QImage::Format_ARGB32_Premultiplied);
		_rippleMask.setDevicePixelRatio(ratio);
		_rippleMask.fill(Qt::transparent);
		{
			auto p = QPainter(&_rippleMask);
			_roundRect.paintSomeRounded(p, QRect(QPoint(), bgRect.size()), 0);
		}
	} else {
		_rippleMask = QImage();
	}
	_roundRect.setColor(st::shadowFg);
	_clipPathValid = false;
}

void VerticalButton::updateSize() {
	resize(_st.width, _st.baseHeight + std::min(
		_st.nameStyle.font->height * kMaxNameLines,
		_text.countHeight(_st.nameWidth, true)));
	_clipPathValid = false;
}

void VerticalButton::paintPinnedBackground(QPainter &p, const QRect &bgRect) {
	if (isFirstPinned() && isLastPinned()) {
		_roundRect.paint(p, bgRect);
	} else if (isFirstPinned()) {
		_roundRect.paintSomeRounded(
			p,
			bgRect,
			RectPart::TopLeft | RectPart::TopRight);
	} else if (isLastPinned()) {
		_roundRect.paintSomeRounded(
			p,
			bgRect,
			RectPart::BottomLeft | RectPart::BottomRight);
	} else {
		_roundRect.paintSomeRounded(p, bgRect, 0);
	}
}

QPainterPath VerticalButton::createClipPath(const QRect &rect) const {
	QPainterPath path;
	path.setFillRule(Qt::WindingFill);
	const auto radius = st::boxRadius;
	if (isFirstPinned() && isLastPinned()) {
		path.addRoundedRect(rect, radius, radius);
	} else if (isFirstPinned()) {
		path.addRoundedRect(rect, radius, radius);
		path.addRect(rect.adjusted(0, rect.height() / 2, 0, 0));
	} else if (isLastPinned()) {
		path.addRoundedRect(rect, radius, radius);
		path.addRect(rect.adjusted(0, 0, 0, -rect.height() / 2));
	}
	return path;
}

const QPainterPath &VerticalButton::cachedClipPath(const QRect &rect) {
	if (!_clipPathValid || _clipPathRect != rect) {
		_clipPathCache = createClipPath(rect);
		_clipPathRect = rect;
		_clipPathValid = true;
	}
	return _clipPathCache;
}

void VerticalButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto active = _delegate->buttonActive(this);
	const auto color = anim::color(
		_st.rippleBg,
		_st.rippleBgActive,
		active);

	if (isPinned()) {
		const auto bgRect = rect()
			- QMargins(_backgroundMargin, 0, _backgroundMargin, 0);
		if (isFirstPinned() || isLastPinned()) {
			p.setClipPath(cachedClipPath(bgRect));
		}
		paintPinnedBackground(p, bgRect);
		paintRipple(p, QPoint(_backgroundMargin, 0), &color);
	} else {
		paintRipple(p, QPoint(0, 0), &color);
	}

	if (!_subscribed) {
		_subscribed = true;
		_data.userpic->subscribeToUpdates([=] { update(); });
	}
	const auto &image = _data.userpic->image(_st.userpicSize);
	const auto userpicLeft = (width() - _st.userpicSize) / 2;
	p.drawImage(userpicLeft, _st.userpicTop, image);
	p.setPen(anim::pen(_st.nameFg, _st.nameFgActive, active));

	const auto textLeft = (width() - _st.nameWidth) / 2;
	_text.draw(p, {
		.position = QPoint(textLeft, _st.nameTop),
		.outerWidth = width(),
		.availableWidth = _st.nameWidth,
		.align = style::al_top,
		.paused = _delegate->buttonPaused(),
		.elisionLines = kMaxNameLines,
	});

	const auto &state = _data.badges;
	const auto top = _st.userpicTop / 2;
	auto right = width() - textLeft;
	UnreadBadgeStyle st;
	if (state.unread) {
		st.muted = state.unreadMuted;
		const auto counter = FormatUnreadCounter(
			state.unreadCounter,
			state.mention || state.reaction,
			true);
		const auto badge = PaintUnreadBadge(p, counter, right, top, st);
		right -= badge.width() + st.padding;
	}
	if (state.mention || state.reaction) {
		UnreadBadgeStyle st;
		st.sizeId = state.mention
			? UnreadBadgeSize::Dialogs
			: UnreadBadgeSize::ReactionInDialogs;
		st.muted = state.mention
			? state.mentionMuted
			: state.reactionMuted;
		st.padding = 0;
		st.textTop = 0;
		const auto counter = QString();
		const auto badge = PaintUnreadBadge(p, counter, right, top, st);
		(state.mention
			? st::dialogsUnreadMention.icon
			: st::dialogsUnreadReaction.icon).paintInCenter(p, badge);
		right -= badge.width() + st.padding + st::dialogsUnreadPadding;
	}
	if (isPinned() && isFirstPinned()) {
		PaintPinnedIcon(p, width(), _backgroundMargin);
	}
}

HorizontalButton::HorizontalButton(
	not_null<QWidget*> parent,
	const style::SettingsSlider &st,
	not_null<SubsectionButtonDelegate*> delegate,
	SubsectionTab &&data)
: SubsectionButton(parent, delegate, std::move(data))
, _st(st)
, _roundRect(st::boxRadius, st::windowBgOver) {
	dataUpdatedHook();
}

void HorizontalButton::updateSize() {
	auto width = _st.strictSkip + _text.maxWidth();

	const auto &state = _data.badges;
	UnreadBadgeStyle st;
	if (state.unread) {
		const auto counter = FormatUnreadCounter(
			state.unreadCounter,
			false,
			false);
		const auto badge = CountUnreadBadgeSize(counter, st);
		width += badge.width() + st.padding;
	}
	if (state.mention || state.reaction) {
		st.sizeId = state.mention
			? UnreadBadgeSize::Dialogs
			: UnreadBadgeSize::ReactionInDialogs;
		st.padding = 0;
		st.textTop = 0;
		const auto counter = QString();
		const auto badge = CountUnreadBadgeSize(counter, st);
		width += badge.width() + st.padding + st::dialogsUnreadPadding;
	}
	resize(width, _st.height);
	_clipPathValid = false;
}

void HorizontalButton::paintPinnedBackground(
		QPainter &p,
		const QRect &bgRect) {
	if (isFirstPinned() && isLastPinned()) {
		_roundRect.paint(p, bgRect);
	} else if (isFirstPinned()) {
		_roundRect.paintSomeRounded(
			p,
			bgRect,
			RectPart::TopLeft | RectPart::BottomLeft);
	} else if (isLastPinned()) {
		_roundRect.paintSomeRounded(
			p,
			bgRect,
			RectPart::TopRight | RectPart::BottomRight);
	} else {
		_roundRect.paintSomeRounded(p, bgRect, 0);
	}
}

QPainterPath HorizontalButton::createClipPath(const QRect &rect) const {
	QPainterPath path;
	path.setFillRule(Qt::WindingFill);
	const auto radius = st::boxRadius;
	if (isFirstPinned() && isLastPinned()) {
		path.addRoundedRect(rect, radius, radius);
	} else if (isFirstPinned()) {
		path.addRoundedRect(rect, radius, radius);
		path.addRect(rect.adjusted(rect.width() / 2, 0, 0, 0));
	} else if (isLastPinned()) {
		path.addRoundedRect(rect, radius, radius);
		path.addRect(rect.adjusted(0, 0, -rect.width() / 2, 0));
	}
	return path;
}

const QPainterPath &HorizontalButton::cachedClipPath(const QRect &rect) {
	if (!_clipPathValid || _clipPathRect != rect) {
		_clipPathCache = createClipPath(rect);
		_clipPathRect = rect;
		_clipPathValid = true;
	}
	return _clipPathCache;
}

void HorizontalButton::dataUpdatedHook() {
	auto context = _delegate->buttonContext();
	context.repaint = [=] { update(); };
	_text.setMarkedText(
		_st.labelStyle,
		_data.text,
		kDefaultTextOptions,
		context);
	updateSize();
}

void HorizontalButton::invalidateCache() {
	_roundRect.setColor(st::white);
	if (isPinned()) {
		const auto bgRect = rect()
			- QMargins(0, _backgroundMargin, 0, _backgroundMargin);
		const auto ratio = style::DevicePixelRatio();
		_rippleMask = QImage(
			bgRect.size() * ratio,
			QImage::Format_ARGB32_Premultiplied);
		_rippleMask.setDevicePixelRatio(ratio);
		_rippleMask.fill(Qt::transparent);
		{
			auto p = QPainter(&_rippleMask);
			_roundRect.paintSomeRounded(p, QRect(QPoint(), bgRect.size()), 0);
		}
	} else {
		_rippleMask = QImage();
	}
	_roundRect.setColor(st::shadowFg);
	_clipPathValid = false;
}

void HorizontalButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto active = _delegate->buttonActive(this);

	const auto color = anim::color(
		_st.rippleBg,
		_st.rippleBgActive,
		active);

	if (isPinned()) {
		const auto bgRect = rect()
			- QMargins(0, _backgroundMargin, 0, _backgroundMargin);
		if (isFirstPinned() || isLastPinned()) {
			p.setClipPath(cachedClipPath(bgRect));
		}
		paintPinnedBackground(p, bgRect);
		paintRipple(p, QPoint(0, _backgroundMargin), &color);
	} else {
		paintRipple(p, QPoint(0, 0), &color);
	}

	p.setPen(anim::pen(_st.labelFg, _st.labelFgActive, active));
	_text.draw(p, {
		.position = QPoint(_st.strictSkip / 2, _st.labelTop),
		.outerWidth = width(),
		.availableWidth = _text.maxWidth(),
		.paused = _delegate->buttonPaused(),
	});

	auto right = width() - _st.strictSkip + (_st.strictSkip / 2);
	UnreadBadgeStyle st;
	const auto &state = _data.badges;
	const auto badgeTop = (height() - st.size) / 2;
	if (state.unread) {
		st.muted = state.unreadMuted;
		const auto counter = FormatUnreadCounter(
			state.unreadCounter,
			false,
			false);
		const auto badge = PaintUnreadBadge(p, counter, right, badgeTop, st);
		right -= badge.width() + st.padding;
	}
	if (state.mention || state.reaction) {
		UnreadBadgeStyle st;
		st.sizeId = state.mention
			? UnreadBadgeSize::Dialogs
			: UnreadBadgeSize::ReactionInDialogs;
		st.muted = state.mention
			? state.mentionMuted
			: state.reactionMuted;
		st.padding = 0;
		st.textTop = 0;
		const auto counter = QString();
		const auto badge = PaintUnreadBadge(p, counter, right, badgeTop, st);
		(state.mention
			? st::dialogsUnreadMention.icon
			: st::dialogsUnreadReaction.icon).paintInCenter(p, badge);
		right -= badge.width() + st.padding + st::dialogsUnreadPadding;
	}

	if (isPinned() && isFirstPinned()) {
		PaintPinnedIcon(
			p,
			width(),
			_backgroundMargin,
			kHorizontalScale,
			true);
	}
}

} // namespace

SubsectionButton::SubsectionButton(
	not_null<QWidget*> parent,
	not_null<SubsectionButtonDelegate*> delegate,
	SubsectionTab &&data)
: RippleButton(parent, st::defaultRippleAnimationBgOver)
, _delegate(delegate)
, _data(std::move(data)) {
}

SubsectionButton::~SubsectionButton() = default;

void SubsectionButton::setData(SubsectionTab &&data) {
	_data = std::move(data);
	dataUpdatedHook();
	update();
}

DynamicImage *SubsectionButton::userpic() const {
	return _data.userpic.get();
}

void SubsectionButton::setActiveShown(float64 activeShown) {
	if (_activeShown != activeShown) {
		_activeShown = activeShown;
		update();
	}
}

void SubsectionButton::setIsPinned(bool pinned) {
	if (_isPinned != pinned) {
		_isPinned = pinned;
		invalidateCache();
		update();
	}
}

bool SubsectionButton::isPinned() const {
	return _isPinned;
}

void SubsectionButton::setPinnedPosition(bool isFirst, bool isLast) {
	if (_isFirstPinned != isFirst || _isLastPinned != isLast) {
		_isFirstPinned = isFirst;
		_isLastPinned = isLast;
		invalidateCache();
		update();
	}
}

bool SubsectionButton::isFirstPinned() const {
	return _isFirstPinned;
}

bool SubsectionButton::isLastPinned() const {
	return _isLastPinned;
}

void SubsectionButton::setBackgroundMargin(int margin) {
	_backgroundMargin = margin;
	invalidateCache();
}

void SubsectionButton::setShift(int shift) {
	_shift = shift;
}

void SubsectionButton::contextMenuEvent(QContextMenuEvent *e) {
	_delegate->buttonContextMenu(this, e);
}

SubsectionSlider::SubsectionSlider(not_null<QWidget*> parent, bool vertical)
: RpWidget(parent)
, _vertical(vertical)
, _barSt(vertical
	? st::chatTabsOutlineVertical
	: st::chatTabsOutlineHorizontal)
, _bar(CreateChild<RpWidget>(this))
, _barRect(_barSt.radius, _barSt.fg) {
	setupBar();
}

SubsectionSlider::~SubsectionSlider() = default;

void SubsectionSlider::setupBar() {
	_bar->setAttribute(Qt::WA_TransparentForMouseEvents);
	sizeValue() | rpl::start_with_next([=](QSize size) {
		const auto thickness = _barSt.stroke - (_barSt.stroke / 2);
		_bar->setGeometry(
			0,
			_vertical ? 0 : (size.height() - thickness),
			_vertical ? thickness : size.width(),
			_vertical ? size.height() : thickness);
	}, _bar->lifetime());
	_bar->paintRequest() | rpl::start_with_next([=](QRect clip) {
		const auto start = -_barSt.stroke / 2;
		const auto currentRange = getCurrentActiveRange();
		const auto from = currentRange.from + _barSt.skip;
		const auto size = currentRange.size - 2 * _barSt.skip;
		if (size <= 0) {
			return;
		}
		const auto rect = myrtlrect(
			_vertical ? start : from,
			_vertical ? from : 0,
			_vertical ? _barSt.stroke : size,
			_vertical ? size : _barSt.stroke);
		if (rect.intersects(clip)) {
			auto p = QPainter(_bar);
			_barRect.paint(p, rect);
		}
	}, _bar->lifetime());
}

void SubsectionSlider::setSections(
		SubsectionTabs sections,
		Fn<bool()> paused) {
	Expects(!sections.tabs.empty());

	_context = sections.context;
	_paused = std::move(paused);
	_fixedCount = sections.fixed;
	_pinnedCount = sections.pinned;
	_reorderAllowed = sections.reorder;

	auto old = base::take(_tabs);
	_tabs.reserve(sections.tabs.size());

	auto size = 0;
	for (auto &data : sections.tabs) {
		const auto i = data.userpic
			? ranges::find(
				old,
				data.userpic.get(),
				&SubsectionButton::userpic)
			: old.empty()
			? end(old)
			: (end(old) - 1);
		if (i != end(old)) {
			_tabs.push_back(std::move(*i));
			old.erase(i);
			_tabs.back()->setData(std::move(data));
		} else {
			_tabs.push_back(makeButton(std::move(data)));
			_tabs.back()->show();
		}
		_tabs.back()->setBackgroundMargin(_barSt.radius);
		_tabs.back()->move(_vertical ? 0 : size, _vertical ? size : 0);

		const auto index = int(_tabs.size()) - 1;
		const auto isPinned = (index >= _fixedCount)
			&& (index < _fixedCount + _pinnedCount);
		_tabs.back()->setIsPinned(isPinned);
		if (isPinned) {
			const auto isFirst = (index == _fixedCount);
			const auto isLast = (index == _fixedCount + _pinnedCount - 1);
			_tabs.back()->setPinnedPosition(isFirst, isLast);
		}
		_tabs.back()->setClickedCallback([=, raw = _tabs.back().get()] {
			if (_tabsReorderedOnce) {
				const auto i = ranges::find(
					_tabs,
					raw,
					&std::unique_ptr<SubsectionButton>::get);
				if (i != end(_tabs)) {
					activate(int(i - begin(_tabs)));
				}
			} else {
				activate(index);
			}
		});
		size += _vertical ? _tabs.back()->height() : _tabs.back()->width();
	}

	for (auto i = 0; i < int(_tabs.size()); ++i) {
		const auto isPinned = (i >= _fixedCount)
			&& (i < _fixedCount + _pinnedCount);
		if (isPinned) {
			const auto isFirst = (i == _fixedCount);
			const auto isLast = (i == _fixedCount + _pinnedCount - 1);
			_tabs[i]->setPinnedPosition(isFirst, isLast);
		}
	}

	if (!_tabs.empty()) {
		resize(
			_vertical ? _tabs.front()->width() : size,
			_vertical ? size : _tabs.front()->height());
	}

	_bar->raise();
}

void SubsectionSlider::activate(int index) {
	if (_active == index) {
		return;
	}
	if (_isReorderingCallback && _isReorderingCallback()) {
		return;
	}
	const auto old = _active;
	const auto was = getFinalActiveRange();
	_active = index;
	const auto now = getFinalActiveRange();
	const auto callback = [=] {
		_bar->update();
		for (auto i = std::min(old, index); i != std::max(old, index); ++i) {
			if (i >= 0 && i < int(_tabs.size())) {
				_tabs[i]->update();
			}
		}
	};
	const auto weak = base::make_weak(_bar);
	_sectionActivated.fire_copy(index);
	if (weak) {
		const auto duration = st::chatTabsSlider.duration;
		_activeFrom.start(callback, was.from, now.from, duration);
		_activeSize.start(callback, was.size, now.size, duration);
		_requestShown.fire_copy({ now.from, now.from + now.size });
	}
}

void SubsectionSlider::setActiveSectionFast(int active, bool ignoreScroll) {
	Expects(active < int(_tabs.size()));

	if (_active == active) {
		return;
	}
	_active = active;
	_activeFrom.stop();
	_activeSize.stop();
	if (_active >= 0 && !ignoreScroll) {
		const auto now = getFinalActiveRange();
		_requestShown.fire({ now.from, now.from + now.size });
	}
	_bar->update();
}

rpl::producer<ScrollToRequest> SubsectionSlider::requestShown() const {
	return _requestShown.events();
}

void SubsectionSlider::setIsReorderingCallback(Fn<bool()> callback) {
	_isReorderingCallback = std::move(callback);
}

int SubsectionSlider::sectionsCount() const {
	return int(_tabs.size());
}

rpl::producer<int> SubsectionSlider::sectionActivated() const {
	return _sectionActivated.events();
}

rpl::producer<int> SubsectionSlider::sectionContextMenu() const {
	return _sectionContextMenu.events();
}

int SubsectionSlider::lookupSectionPosition(int index) const {
	Expects(!_tabs.empty());
	Expects(index >= 0 && index < _tabs.size());

	return _vertical ? _tabs[index]->y() : _tabs[index]->x();
}

void SubsectionSlider::paintEvent(QPaintEvent *e) {
}

int SubsectionSlider::lookupSectionIndex(QPoint position) const {
	Expects(!_tabs.empty());

	const auto count = sectionsCount();
	if (_vertical) {
		for (auto i = 0; i != count; ++i) {
			const auto tab = _tabs[i].get();
			if (position.y() < tab->y() + tab->height()) {
				return i;
			}
		}
	} else {
		for (auto i = 0; i != count; ++i) {
			const auto tab = _tabs[i].get();
			if (position.x() < tab->x() + tab->width()) {
				return i;
			}
		}
	}
	return count - 1;
}

SubsectionSlider::Range SubsectionSlider::getFinalActiveRange() const {
	if (_active < 0 || _active >= _tabs.size()) {
		return {};
	}
	const auto tab = _tabs[_active].get();
	return Range{
		.from = _vertical ? tab->y() : tab->x(),
		.size = _vertical ? tab->height() : tab->width(),
	};
}

SubsectionSlider::Range SubsectionSlider::getCurrentActiveRange() const {
	const auto finalRange = getFinalActiveRange();
	return {
		.from = int(base::SafeRound(_activeFrom.value(finalRange.from))),
		.size = int(base::SafeRound(_activeSize.value(finalRange.size))),
	};
}

bool SubsectionSlider::buttonPaused() {
	return _paused && _paused();
}

float64 SubsectionSlider::buttonActive(not_null<SubsectionButton*> button) {
	const auto currentRange = getCurrentActiveRange();
	const auto from = _vertical ? button->y() : button->x();
	const auto size = _vertical ? button->height() : button->width();
	const auto checkSize = std::min(size, currentRange.size);
	return (checkSize > 0)
		? (1. - (std::abs(currentRange.from - from) / float64(checkSize)))
		: 0.;
}

void SubsectionSlider::buttonContextMenu(
		not_null<SubsectionButton*> button,
		not_null<QContextMenuEvent*> e) {
	const auto i = ranges::find(
		_tabs,
		button.get(),
		&std::unique_ptr<SubsectionButton>::get);
	Assert(i != end(_tabs));

	_sectionContextMenu.fire(int(i - begin(_tabs)));
	e->accept();
}

Text::MarkedContext SubsectionSlider::buttonContext() {
	return _context;
}

not_null<SubsectionButton*> SubsectionSlider::buttonAt(int index) {
	Expects(index >= 0 && index < _tabs.size());

	return _tabs[index].get();
}

void SubsectionSlider::setButtonShift(int index, int shift) {
	Expects(index >= 0 && index < _tabs.size());

	auto position = 0;
	for (auto i = 0; i < index; ++i) {
		position += _vertical ? _tabs[i]->height() : _tabs[i]->width();
	}

	const auto targetPos = position + shift;

	_tabs[index]->move(
		_vertical ? 0 : targetPos,
		_vertical ? targetPos : 0);
	recalculatePinnedPositionsByUI();
}

void SubsectionSlider::reorderButtons(int from, int to) {
	Expects(from >= 0 && from < _tabs.size());
	Expects(to >= 0 && to < _tabs.size());
	if (from == to) {
		return;
	}

	_active = base::reorder_index(_active, from, to);
	base::reorder(_tabs, from, to);

	auto position = 0;
	for (auto i = 0; i < int(_tabs.size()); ++i) {
		_tabs[i]->move(_vertical ? 0 : position, _vertical ? position : 0);
		position += _vertical ? _tabs[i]->height() : _tabs[i]->width();
	}
	_tabsReorderedOnce = true;
}

void SubsectionSlider::recalculatePinnedPositions() {
	for (auto i = 0; i < int(_tabs.size()); ++i) {
		const auto isPinned = (i >= _fixedCount)
			&& (i < _fixedCount + _pinnedCount);
		_tabs[i]->setIsPinned(isPinned);
		if (isPinned) {
			const auto isFirst = (i == _fixedCount);
			const auto isLast = (i == _fixedCount + _pinnedCount - 1);
			_tabs[i]->setPinnedPosition(isFirst, isLast);
		}
	}
}

void SubsectionSlider::recalculatePinnedPositionsByUI() {
	if (_pinnedCount == 0) {
		return;
	}

	auto pinnedIndices = std::vector<int>();
	for (auto i = 0; i < int(_tabs.size()); ++i) {
		if (_tabs[i]->isPinned()) {
			pinnedIndices.push_back(i);
		}
	}

	if (pinnedIndices.empty()) {
		return;
	}

	ranges::sort(pinnedIndices, [&](int a, int b) {
		const auto posA = _vertical ? _tabs[a]->y() : _tabs[a]->x();
		const auto posB = _vertical ? _tabs[b]->y() : _tabs[b]->x();
		return posA < posB;
	});

	for (auto i = 0; i < int(pinnedIndices.size()); ++i) {
		const auto index = pinnedIndices[i];
		const auto isFirst = (i == 0);
		const auto isLast = (i == int(pinnedIndices.size()) - 1);
		_tabs[index]->setPinnedPosition(isFirst, isLast);
	}
}

VerticalSlider::VerticalSlider(not_null<QWidget*> parent)
: SubsectionSlider(parent, true) {
}

VerticalSlider::~VerticalSlider() = default;

std::unique_ptr<SubsectionButton> VerticalSlider::makeButton(
		SubsectionTab &&data) {
	return std::make_unique<VerticalButton>(
		this,
		static_cast<SubsectionButtonDelegate*>(this),
		std::move(data));
}

HorizontalSlider::HorizontalSlider(not_null<QWidget*> parent)
: SubsectionSlider(parent, false)
, _st(st::chatTabsSlider) {
}

HorizontalSlider::~HorizontalSlider() = default;

std::unique_ptr<SubsectionButton> HorizontalSlider::makeButton(
		SubsectionTab &&data) {
	return std::make_unique<HorizontalButton>(
		this,
		_st,
		static_cast<SubsectionButtonDelegate*>(this),
		std::move(data));
}

std::shared_ptr<DynamicImage> MakeIconSubsectionsThumbnail(
		const style::icon &icon,
		Fn<QColor()> textColor,
		std::optional<QMargins> invertedPadding = {}) {
	class Image final : public DynamicImage {
	public:
		Image(
			const style::icon &icon,
			Fn<QColor()> textColor,
			std::optional<QMargins> invertedPadding)
		: _icon(icon)
		, _textColor(std::move(textColor))
		, _invertedPadding(invertedPadding) {
			Expects(_textColor != nullptr);
		}

		std::shared_ptr<DynamicImage> clone() override {
			return std::make_shared<Image>(
				_icon,
				_textColor,
				_invertedPadding);
		}

		QImage image(int size) override {
			const auto ratio = style::DevicePixelRatio();
			const auto full = size * ratio;
			const auto color = _textColor();
			if (_cache.size() != QSize(full, full)) {
				_cache = QImage(
					QSize(full, full),
					QImage::Format_ARGB32_Premultiplied);
				_cache.setDevicePixelRatio(ratio);
			} else if (_color == color) {
				return _cache;
			}
			_color = color;
			if (_invertedPadding) {
				_cache.fill(Qt::transparent);
				auto p = QPainter(&_cache);
				const auto fill = QRect(QPoint(), _icon.size()).marginsAdded(
					*_invertedPadding).size();
				const auto inner = QRect(
					(size - fill.width()) / 2,
					(size - fill.height()) / 2,
					fill.width(),
					fill.height());
				auto hq = PainterHighQualityEnabler(p);
				const auto radius = fill.width() / 6.;
				p.setPen(Qt::NoPen);
				p.setBrush(color);
				p.drawRoundedRect(inner, radius, radius);
				_icon.paint(
					p,
					(inner.topLeft()
						+ QPoint(
							_invertedPadding->left(),
							_invertedPadding->top())),
					size);
				return _cache;
			}

			if (_mask.isNull()) {
				_mask = _icon.instance(QColor(255, 255, 255));
			}
			const auto position = ratio * QPoint(
				(size - (_mask.width() / ratio)) / 2,
				(size - (_mask.height() / ratio)) / 2);
			if (_mask.width() <= full && _mask.height() <= full) {
				style::colorizeImage(
					_mask,
					color,
					&_cache,
					QRect(),
					position);
			} else {
				_cache = style::colorizeImage(_mask, color).scaled(
					full,
					full,
					Qt::IgnoreAspectRatio,
					Qt::SmoothTransformation);
				_cache.setDevicePixelRatio(ratio);
			}
			return _cache;
		}
		void subscribeToUpdates(Fn<void()> callback) override {
			if (!callback) {
				_cache = QImage();
				_mask = QImage();
			}
		}

	private:
		const style::icon &_icon;
		Fn<QColor()> _textColor;
		QImage _mask;
		QImage _cache;
		QColor _color;
		std::optional<QMargins> _invertedPadding;

	};
	return std::make_shared<Image>(
		icon,
		std::move(textColor),
		invertedPadding);
}

std::shared_ptr<DynamicImage> MakeAllSubsectionsThumbnail(
		Fn<QColor()> textColor) {
	return MakeIconSubsectionsThumbnail(
		st::foldersAll,
		std::move(textColor));
}

std::shared_ptr<DynamicImage> MakeNewChatSubsectionsThumbnail(
		Fn<QColor()> textColor) {
	return MakeIconSubsectionsThumbnail(
		st::newChatIcon,
		std::move(textColor),
		st::newChatIconPadding);
}

} // namespace Ui
