/*
This file is part of AhiGram,
a fork of Telegram Desktop with additional features.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

// This code is licensed under GPLv3. Attribution to original source is appreciated.
// Author: https://github.com/DyingLay

AhiGram: Custom badge tooltip widget
*/

#include "ui/custom_badge_tooltip.h"

#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "base/event_filter.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "styles/style_info.h"
#include "styles/style_widgets.h"
#include "ui/rect.h"

#include <QFontMetrics>
#include <memory>

namespace AhiGram {
namespace UI {
namespace {

constexpr auto kIconSize = 24;
constexpr auto kPaddingLeft = 12;
constexpr auto kPaddingRight = 12;
constexpr auto kPaddingTop = 10;
constexpr auto kPaddingBottom = 10;
constexpr auto kSpacing = 8;
constexpr auto kHideDuration = 2500;
constexpr auto kShift = 2;
constexpr auto kArrow = 8;
constexpr auto kRadius = 8;
constexpr auto kShadowAlpha = 32;
constexpr auto kTextSpacing = 4;

} // namespace

CustomBadgeTooltip::CustomBadgeTooltip(
	not_null<QWidget*> parent,
	const QString &title,
	const QString &description,
	const style::icon *badgeIcon,
	not_null<QWidget*> pointTo)
: Ui::RpWidget(parent)
, _st(st::defaultImportantTooltip)
, _title(title)
, _description(description)
, _badgeIcon(badgeIcon)
, _titleFont(st::semiboldFont)
, _descFont(st::normalFont) {
	const auto titleMetrics = QFontMetrics(_titleFont);
	const auto descMetrics = QFontMetrics(_descFont);

	const auto titleWidth = titleMetrics.horizontalAdvance(_title);
	const auto descWidth = _description.isEmpty()
		? 0
		: descMetrics.horizontalAdvance(_description);
	const auto maxTextWidth = std::max(titleWidth, descWidth);
	const auto textHeight = _description.isEmpty()
		? titleMetrics.height()
		: (titleMetrics.height() + kTextSpacing + descMetrics.height());

	const auto iconWidth = _badgeIcon ? kIconSize : 0;
	const auto iconSpacing = _badgeIcon ? kSpacing : 0;
	const auto contentWidth = iconWidth + iconSpacing + maxTextWidth;
	const auto contentHeight = std::max(kIconSize, textHeight);

	_inner = QSize(contentWidth, contentHeight);
	_outer = QSize(
		_inner.width() + kPaddingLeft + kPaddingRight,
		_inner.height() + kPaddingTop + kPaddingBottom);

	_stroke = st::lineWidth;
	_skip = 2 * _stroke;
	_full = QSize(
		_outer.width() + 2 * _skip,
		_outer.height() + kArrow + 2 * _skip);

	resize(_full + QSize(0, kShift));
	setupGeometry(pointTo);
}

void CustomBadgeTooltip::setupGeometry(not_null<QWidget*> pointTo) {
	const auto parent = parentWidget();
	const auto refresh = [=, weak = base::make_weak(pointTo)] {
		const auto strong = weak.get();
		if (!strong) {
			hide();
			setGeometry({});
			return;
		}
		const auto badgeGlobalTopLeft = pointTo->mapToGlobal(QPoint(0, 0));
		const auto badgeGlobalBottomRight = pointTo->mapToGlobal(
			QPoint(pointTo->width(), pointTo->height()));
		const auto badgeParentTopLeft = parent->mapFromGlobal(badgeGlobalTopLeft);
		const auto badgeParentBottomRight = parent->mapFromGlobal(badgeGlobalBottomRight);
		
		const auto badgeRect = QRect(
			badgeParentTopLeft,
			badgeParentBottomRight);
		const auto badgeCenterX = badgeRect.center().x();
		const auto badgeTop = badgeRect.y();
		
		const auto desiredArrowX = badgeCenterX;
		
		const auto idealArrowCenterX = width() / 2;
		const auto tooltipLeft = desiredArrowX - idealArrowCenterX;
		const auto tooltipTop = badgeTop - height() - kShift;
		
		const auto skip = kPaddingLeft;
		const auto maxLeft = parent->width() - width() - skip;
		const auto minTop = skip;
		
		auto clampedLeft = tooltipLeft;
		if (clampedLeft < skip) {
			clampedLeft = skip;
		} else if (clampedLeft > maxLeft) {
			clampedLeft = maxLeft;
		}
		
		_arrowCenterX = desiredArrowX - clampedLeft;
		
		const auto finalTop = std::max(tooltipTop, minTop);
		
		setGeometry(
			clampedLeft,
			finalTop,
			width(),
			height());
	};

	refresh();

	auto widget = pointTo.get();
	while (widget && widget != parent) {
		base::install_event_filter(this, widget, [=](not_null<QEvent*> e) {
			const auto type = e->type();
			if (type == QEvent::Resize
				|| type == QEvent::Move
				|| type == QEvent::ZOrderChange) {
				refresh();
				raise();
			}
			return base::EventFilterResult::Continue;
		});
		widget = widget->parentWidget();
	}
}

void CustomBadgeTooltip::fade(bool shown) {
	if (_shown == shown) {
		return;
	}
	
	if (_isAnimating) {
		//LOG(("AhiGram: CustomBadgeTooltip: fade: already animating"));
		return;
	}
	
	_isAnimating = true;
	
	show();
	_shown = shown;

	const auto duration = _shown ? _st.duration : (_st.duration * 3);
	const auto easing = _shown ? anim::easeOutCubic : anim::sineInOut;
	const auto from = _shown ? 0. : 1.;
	const auto to = _shown ? 1. : 0.;

	_showAnimation.start([=] {
		update();
		if (!_showAnimation.animating()) {
			_isAnimating = false;
			_shownVariable = _shown;
			if (!_shown) {
				hide();
			}
		}
	}, from, to, duration, easing);

	if (_shown) {
		_shownVariable = true;
	}
}

void CustomBadgeTooltip::finishAnimating() {
	_showAnimation.stop();
	_isAnimating = false;
	_shownVariable = _shown;
	if (!_shown) {
		hide();
	}
	update();
}

rpl::producer<bool> CustomBadgeTooltip::shownValue() const {
	return _shownVariable.value();
}

crl::time CustomBadgeTooltip::hideDuration() const {
	return kHideDuration;
}

void CustomBadgeTooltip::paintEvent(QPaintEvent *e) {
	Painter p(this);
	const auto opacity = _showAnimation.value(_shown ? 1. : 0.);
	p.setOpacity(opacity);

	const auto rect = QRect(_skip, _skip, _outer.width(), _outer.height());
	const auto shadowRect = rect.adjusted(-2, -2, 2, 2);

	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0, 0, 0, kShadowAlpha));
	p.drawRoundedRect(shadowRect, kRadius, kRadius);

	p.setBrush(st::windowBg->c);
	p.drawRoundedRect(rect, kRadius, kRadius);

	const auto arrowTop = _skip + _outer.height();
	const auto arrowLeft = _arrowCenterX - (kArrow / 2);
	QPainterPath arrowPath;
	arrowPath.moveTo(arrowLeft, arrowTop);
	arrowPath.lineTo(arrowLeft + kArrow, arrowTop);
	arrowPath.lineTo(arrowLeft + kArrow / 2, arrowTop + kArrow);
	arrowPath.closeSubpath();
	p.fillPath(arrowPath, st::windowBg->c);

	auto contentLeft = _skip + kPaddingLeft;
	const auto contentTop = _skip + kPaddingTop;

	if (_badgeIcon) {
		const auto iconTop = contentTop + (_inner.height() - kIconSize) / 2;
		_badgeIcon->paint(p, contentLeft, iconTop, width());
		contentLeft += kIconSize + kSpacing;
	}

	const auto titleMetrics = QFontMetrics(_titleFont);
	const auto descMetrics = QFontMetrics(_descFont);
	const auto titleY = contentTop + titleMetrics.ascent();
	const auto descY = contentTop + titleMetrics.height() + kTextSpacing + descMetrics.ascent();

	p.setPen(st::windowBoldFg->c);
	p.setFont(_titleFont);
	p.drawText(contentLeft, titleY, _title);

	if (!_description.isEmpty()) {
		p.setPen(st::windowSubTextFg->c);
		p.setFont(_descFont);
		p.drawText(contentLeft, descY, _description);
	}
}

[[nodiscard]] CustomBadgeTooltip* CreateImportantTooltip(
	not_null<QWidget*> parent,
	const QString &title,
	const QString &description,
	const style::icon *badgeIcon,
	not_null<QWidget*> pointTo,
	crl::time hideAfter) {
	const auto tooltip = Ui::CreateChild<CustomBadgeTooltip>(
		parent,
		title,
		description,
		badgeIcon,
		pointTo);

	base::install_event_filter(tooltip, qApp, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::MouseButtonPress) {
			tooltip->fade(false);
		}
		return base::EventFilterResult::Continue;
	});

	tooltip->fade(true);

	if (hideAfter > 0) {
		const auto weak = base::make_weak(tooltip);
		const auto hideTimer = std::make_shared<base::Timer>([=] {
			if (const auto strong = weak.get()) {
				strong->fade(false);
			}
		});
		hideTimer->callOnce(hideAfter);
		tooltip->shownValue() | rpl::filter(
			!rpl::mappers::_1
		) | rpl::start_with_next([=] {
			hideTimer->cancel();
		}, tooltip->lifetime());
	}

	tooltip->shownValue() | rpl::filter(
		!rpl::mappers::_1
	) | rpl::start_with_next([=] {
		crl::on_main(tooltip, [=] {
			if (tooltip->isHidden()) {
				tooltip->deleteLater();
			}
		});
	}, tooltip->lifetime());

	return tooltip;
}

} // namespace UI
} // namespace AhiGram