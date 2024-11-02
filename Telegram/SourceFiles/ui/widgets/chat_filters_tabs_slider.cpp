/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/chat_filters_tabs_slider.h"

#include "ui/effects/ripple_animation.h"
#include "styles/style_widgets.h"

#include <QScrollBar>

namespace Ui {

ChatsFiltersTabs::ChatsFiltersTabs(
	not_null<Ui::RpWidget*> parent,
	const style::SettingsSlider &st)
: Ui::SettingsSlider(parent, st)
, _st(st)
, _unreadSt([&] {
	auto st = Ui::UnreadBadgeStyle();
	st.align = style::al_left;
	return st;
}())
, _unreadMaxString(u"99+"_q)
, _unreadSkip(st::lineWidth * 5) {
	Expects(_st.barSnapToLabel && _st.strictSkip);
	if (_st.barRadius > 0) {
		_bar.emplace(_st.barRadius, _st.barFg);
		_barActive.emplace(_st.barRadius, _st.barFgActive);
	}
	{
		const auto one = Ui::CountUnreadBadgeSize(u"9"_q, _unreadSt, 1);
		_cachedBadgeWidths = {
			one.width(),
			Ui::CountUnreadBadgeSize(u"99"_q, _unreadSt, 2).width(),
			Ui::CountUnreadBadgeSize(u"999"_q, _unreadSt, 2).width(),
		};
		_cachedBadgeHeight = one.height();
	}
}

int ChatsFiltersTabs::centerOfSection(int section) const {
	const auto widths = countSectionsWidths(0);
	auto result = 0;
	if (section >= 0 && section < widths.size()) {
		for (auto i = 0; i < section; i++) {
			result += widths[i];
		}
		result += widths[section] / 2;
	}
	return result;
}

void ChatsFiltersTabs::fitWidthToSections() {
	const auto widths = countSectionsWidths(0);
	resizeToWidth(ranges::accumulate(widths, .0));
}

void ChatsFiltersTabs::setUnreadCount(int index, int unreadCount) {
	const auto it = _unreadCounts.find(index);
	if (it == _unreadCounts.end()) {
		if (unreadCount) {
			_unreadCounts.emplace(index, Unread{
				.cache = cacheUnreadCount(unreadCount),
				.count = unreadCount,
			});
		}
	} else {
		if (unreadCount) {
			it->second.count = unreadCount;
			it->second.cache = cacheUnreadCount(unreadCount);
		} else {
			_unreadCounts.erase(it);
		}
	}
	if (unreadCount) {
		const auto widthIndex = (unreadCount < 10)
			? 0
			: (unreadCount < 100)
			? 1
			: 2;
		setAdditionalContentWidthToSection(
			index,
			_cachedBadgeWidths[widthIndex] + _unreadSkip);
	} else {
		setAdditionalContentWidthToSection(index, 0);
	}
}

QImage ChatsFiltersTabs::cacheUnreadCount(int count) const {
	const auto widthIndex = (count < 10) ? 0 : (count < 100) ? 1 : 2;
	auto image = QImage(
		QSize(_cachedBadgeWidths[widthIndex], _cachedBadgeHeight)
			* style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(style::DevicePixelRatio());
	image.fill(Qt::transparent);
	const auto string = (count > 99)
		? _unreadMaxString
		: QString::number(count);
	{
		auto p = QPainter(&image);
		Ui::PaintUnreadBadge(p, string, 0, 0, _unreadSt, 0);
	}
	return image;
}

void ChatsFiltersTabs::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto clip = e->rect();
	const auto range = getCurrentActiveRange();

	auto index = 0;
	enumerateSections([&](Section &section) {
		const auto activeWidth = _st.barSnapToLabel
			? section.contentWidth
			: section.width;
		const auto activeLeft = section.left
			+ (section.width - activeWidth) / 2;
		const auto active = 1.
			- std::clamp(
				std::abs(range.left - activeLeft) / float64(range.width),
				0.,
				1.);
		if (section.ripple) {
			const auto color = anim::color(
				_st.rippleBg,
				_st.rippleBgActive,
				active);
			section.ripple->paint(p, section.left, 0, width(), &color);
			if (section.ripple->empty()) {
				section.ripple.reset();
			}
		}
		const auto labelLeft = section.left
			+ (section.width - section.contentWidth) / 2;
		const auto rect = myrtlrect(
			labelLeft,
			_st.labelTop,
			section.contentWidth,
			_st.labelStyle.font->height);
		if (rect.intersects(clip)) {
			p.setPen(anim::pen(_st.labelFg, _st.labelFgActive, active));
			section.label.draw(p, {
				.position = QPoint(labelLeft, _st.labelTop),
				.outerWidth = width(),
				.availableWidth = section.label.maxWidth(),
			});
			{
				const auto it = _unreadCounts.find(index);
				if (it != _unreadCounts.end()) {
					p.drawImage(
						labelLeft
							+ _unreadSkip
							+ section.label.maxWidth(),
						_st.labelTop,
						it->second.cache);
				}
			}
		}
		index++;
		return true;
	});
	if (_st.barSnapToLabel) {
		const auto drawRect = [&](QRect rect, bool active) {
			const auto &bar = active ? _barActive : _bar;
			if (bar) {
				bar->paint(p, rect);
			} else {
				p.fillRect(rect, active ? _st.barFgActive : _st.barFg);
			}
		};
		const auto add = _st.barStroke / 2;
		const auto from = std::max(range.left - add, 0);
		const auto till = std::min(range.left + range.width + add, width());
		if (from < till) {
			drawRect(
				myrtlrect(from, _st.barTop, till - from, _st.barStroke),
				true);
		}
	}
}

void ChatsFiltersTabs::mousePressEvent(QMouseEvent *e) {
	const auto mouseButton = e->button();
	if (mouseButton == Qt::MouseButton::LeftButton) {
		Ui::SettingsSlider::mousePressEvent(e);
	} else {
		Ui::RpWidget::mousePressEvent(e);
	}
}

void ChatsFiltersTabs::contextMenuEvent(QContextMenuEvent *e) {
	const auto pos = e->pos();
	auto left = 0;
	auto index = 0;
	enumerateSections([&](const Section &section) {
		const auto currentRight = section.left + section.width;
		if (pos.x() > left && pos.x() < currentRight) {
			return false;
		}
		left = currentRight;
		index++;
		return true;
	});
	_contextMenuRequested.fire_copy(index);
}

rpl::producer<int> ChatsFiltersTabs::contextMenuRequested() const {
	return _contextMenuRequested.events();
}

} // namespace Ui
