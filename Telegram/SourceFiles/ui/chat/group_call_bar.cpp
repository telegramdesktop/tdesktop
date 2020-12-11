/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/group_call_bar.h"

#include "ui/chat/message_bar.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/buttons.h"
#include "lang/lang_keys.h"
#include "styles/style_chat.h"
#include "styles/style_calls.h"
#include "styles/style_info.h" // st::topBarArrowPadding, like TopBarWidget.
#include "styles/palette.h"

#include <QtGui/QtEvents>

namespace Ui {
namespace {

constexpr auto kDuration = 160;
constexpr auto kMaxUserpics = 4;
constexpr auto kWideScale = 5;

} // namespace

GroupCallBar::GroupCallBar(
	not_null<QWidget*> parent,
	rpl::producer<GroupCallBarContent> content)
: _wrap(parent, object_ptr<RpWidget>(parent))
, _inner(_wrap.entity())
, _join(std::make_unique<RoundButton>(
	_inner.get(),
	tr::lng_group_call_join(),
	st::groupCallTopBarJoin))
, _shadow(std::make_unique<PlainShadow>(_wrap.parentWidget())) {
	_wrap.hide(anim::type::instant);
	_shadow->hide();

	const auto limit = kMaxUserpics;
	const auto single = st::historyGroupCallUserpicSize;
	const auto shift = st::historyGroupCallUserpicShift;
	// + 1 * single for the blobs.
	_maxUserpicsWidth = 2 * single + (limit - 1) * (single - shift);

	_wrap.entity()->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		QPainter(_wrap.entity()).fillRect(clip, st::historyPinnedBg);
	}, lifetime());
	_wrap.setAttribute(Qt::WA_OpaquePaintEvent);

	auto copy = std::move(
		content
	) | rpl::start_spawning(_wrap.lifetime());

	rpl::duplicate(
		copy
	) | rpl::start_with_next([=](GroupCallBarContent &&content) {
		_content = content;
		updateUserpicsFromContent();
		_inner->update();
	}, lifetime());

	std::move(
		copy
	) | rpl::map([=](const GroupCallBarContent &content) {
		return !content.shown;
	}) | rpl::start_with_next_done([=](bool hidden) {
		_shouldBeShown = !hidden;
		if (!_forceHidden) {
			_wrap.toggle(_shouldBeShown, anim::type::normal);
		}
	}, [=] {
		_forceHidden = true;
		_wrap.toggle(false, anim::type::normal);
	}, lifetime());

	setupInner();
}

GroupCallBar::~GroupCallBar() {
}

void GroupCallBar::setupInner() {
	_inner->resize(0, st::historyReplyHeight);
	_inner->paintRequest(
	) | rpl::start_with_next([=](QRect rect) {
		auto p = Painter(_inner);
		paint(p);
	}, _inner->lifetime());

	// Clicks.
	_inner->setCursor(style::cur_pointer);
	_inner->events(
	) | rpl::filter([=](not_null<QEvent*> event) {
		return (event->type() == QEvent::MouseButtonPress);
	}) | rpl::map([=] {
		return _inner->events(
		) | rpl::filter([=](not_null<QEvent*> event) {
			return (event->type() == QEvent::MouseButtonRelease);
		}) | rpl::take(1) | rpl::filter([=](not_null<QEvent*> event) {
			return _inner->rect().contains(
				static_cast<QMouseEvent*>(event.get())->pos());
		});
	}) | rpl::flatten_latest(
	) | rpl::map([] {
		return rpl::empty_value();
	}) | rpl::start_to_stream(_barClicks, _inner->lifetime());

	rpl::combine(
		_inner->widthValue(),
		_join->widthValue()
	) | rpl::start_with_next([=](int outerWidth, int) {
		// Skip shadow of the bar above.
		const auto top = (st::historyReplyHeight
			- st::lineWidth
			- _join->height()) / 2 + st::lineWidth;
		_join->moveToRight(top, top, outerWidth);
	}, _join->lifetime());

	_wrap.geometryValue(
	) | rpl::start_with_next([=](QRect rect) {
		updateShadowGeometry(rect);
		updateControlsGeometry(rect);
	}, _inner->lifetime());
}

void GroupCallBar::paint(Painter &p) {
	p.fillRect(_inner->rect(), st::historyComposeAreaBg);

	const auto left = st::topBarArrowPadding.right();
	const auto titleTop = st::msgReplyPadding.top();
	const auto textTop = titleTop + st::msgServiceNameFont->height;
	const auto width = _inner->width();
	p.setPen(st::defaultMessageBar.textFg);
	p.setFont(st::defaultMessageBar.title.font);
	p.drawTextLeft(left, titleTop, width, tr::lng_group_call_title(tr::now));
	p.setPen(st::historyComposeAreaFgService);
	p.setFont(st::defaultMessageBar.text.font);
	p.drawTextLeft(
		left,
		textTop,
		width,
		(_content.count > 0
			? tr::lng_group_call_members(tr::now, lt_count, _content.count)
			: tr::lng_group_call_no_members(tr::now)));

	// Skip shadow of the bar above.
	paintUserpics(p);
}

void GroupCallBar::paintUserpics(Painter &p) {
	const auto top = (st::historyReplyHeight
		- st::lineWidth
		- st::historyGroupCallUserpicSize) / 2 + st::lineWidth;
	const auto middle = _inner->width()  / 2;
	const auto size = st::historyGroupCallUserpicSize;
	const auto factor = style::DevicePixelRatio();
	for (auto &userpic : ranges::view::reverse(_userpics)) {
		const auto shown = userpic.shownAnimation.value(
			userpic.hiding ? 0. : 1.);
		if (shown == 0.) {
			continue;
		}
		validateUserpicCache(userpic);
		p.setOpacity(shown);
		const auto left = middle + userpic.leftAnimation.value(userpic.left);
		if (userpic.data.speaking) {
			//p.fillRect(left, top, size, size, QColor(255, 128, 128));
		}
		if (shown == 1.) {
			const auto skip = ((kWideScale - 1) / 2) * size * factor;
			p.drawImage(
				QRect(left, top, size, size),
				userpic.cache,
				QRect(skip, skip, size * factor, size * factor));
		} else {
			const auto scale = 0.5 + shown / 2.;
			auto target = QRect(
				left + (1 - kWideScale) / 2 * size,
				top + (1 - kWideScale) / 2 * size,
				kWideScale * size,
				kWideScale * size);
			auto shrink = anim::interpolate(
				(1 - kWideScale) / 2 * size,
				0,
				scale);
			auto margins = QMargins(shrink, shrink, shrink, shrink);
			p.drawImage(target.marginsAdded(margins), userpic.cache);
		}
	}
	p.setOpacity(1.);

	const auto hidden = [](const Userpic &userpic) {
		return userpic.hiding && !userpic.shownAnimation.animating();
	};
	_userpics.erase(ranges::remove_if(_userpics, hidden), end(_userpics));
}

bool GroupCallBar::needUserpicCacheRefresh(Userpic &userpic) {
	if (userpic.cache.isNull()) {
		return true;
	} else if (userpic.hiding) {
		return false;
	} else if (userpic.cacheKey != userpic.data.userpicKey) {
		return true;
	}
	const auto shouldBeMasked = !userpic.topMost;
	if (userpic.cacheMasked == shouldBeMasked || !shouldBeMasked) {
		return true;
	}
	return !userpic.leftAnimation.animating();
}

void GroupCallBar::validateUserpicCache(Userpic &userpic) {
	if (!needUserpicCacheRefresh(userpic)) {
		return;
	}
	const auto factor = style::DevicePixelRatio();
	const auto size = st::historyGroupCallUserpicSize;
	const auto shift = st::historyGroupCallUserpicShift;
	const auto full = QSize(size, size) * kWideScale * factor;
	if (userpic.cache.isNull()) {
		userpic.cache = QImage(full, QImage::Format_ARGB32_Premultiplied);
		userpic.cache.setDevicePixelRatio(factor);
	}
	userpic.cacheKey = userpic.data.userpicKey;
	userpic.cacheMasked = !userpic.topMost;
	userpic.cache.fill(Qt::transparent);
	{
		Painter p(&userpic.cache);
		const auto skip = (kWideScale - 1) / 2 * size;
		p.drawImage(skip, skip, userpic.data.userpic);

		if (userpic.cacheMasked) {
			auto hq = PainterHighQualityEnabler(p);
			auto pen = QPen(Qt::transparent);
			pen.setWidth(st::historyGroupCallUserpicStroke);
			p.setCompositionMode(QPainter::CompositionMode_Source);
			p.setBrush(Qt::transparent);
			p.setPen(pen);
			p.drawEllipse(skip - size + shift, skip, size, size);
		}
	}
}

void GroupCallBar::updateControlsGeometry(QRect wrapGeometry) {
	_inner->resizeToWidth(wrapGeometry.width());
	const auto hidden = _wrap.isHidden() || !wrapGeometry.height();
	if (_shadow->isHidden() != hidden) {
		_shadow->setVisible(!hidden);
	}
}

void GroupCallBar::setShadowGeometryPostprocess(Fn<QRect(QRect)> postprocess) {
	_shadowGeometryPostprocess = std::move(postprocess);
	updateShadowGeometry(_wrap.geometry());
}

void GroupCallBar::updateShadowGeometry(QRect wrapGeometry) {
	const auto regular = QRect(
		wrapGeometry.x(),
		wrapGeometry.y() + wrapGeometry.height(),
		wrapGeometry.width(),
		st::lineWidth);
	_shadow->setGeometry(_shadowGeometryPostprocess
		? _shadowGeometryPostprocess(regular)
		: regular);
}

void GroupCallBar::updateUserpicsFromContent() {
	const auto idFromUserpic = [](const Userpic &userpic) {
		return userpic.data.id;
	};

	// Use "topMost" as "willBeHidden" flag.
	for (auto &userpic : _userpics) {
		userpic.topMost = true;
	}
	for (const auto &user : _content.users) {
		const auto i = ranges::find(_userpics, user.id, idFromUserpic);
		if (i == end(_userpics)) {
			_userpics.push_back(Userpic{ user });
			toggleUserpic(_userpics.back(), true);
			continue;
		}
		i->topMost = false;

		if (i->hiding) {
			toggleUserpic(*i, true);
		}
		i->data = user;

		// Put this one after the last we are not hiding.
		for (auto j = end(_userpics) - 1; j != i; --j) {
			if (!j->topMost) {
				ranges::rotate(i, i + 1, j + 1);
				break;
			}
		}
	}

	// Hide the ones that "willBeHidden" (currently having "topMost" flag).
	// Set correct real values of "topMost" flag.
	const auto userpicsBegin = begin(_userpics);
	const auto userpicsEnd = end(_userpics);
	auto markedTopMost = userpicsEnd;
	for (auto i = userpicsBegin; i != userpicsEnd; ++i) {
		auto &userpic = *i;
		if (userpic.topMost) {
			toggleUserpic(userpic, false);
			userpic.topMost = false;
		} else if (markedTopMost == userpicsEnd) {
			userpic.topMost = true;
			markedTopMost = i;
		}
	}
	if (markedTopMost != userpicsEnd && markedTopMost != userpicsBegin) {
		// Bring the topMost userpic to the very beginning, above all hiding.
		std::rotate(userpicsBegin, markedTopMost, markedTopMost + 1);
	}
	updateUserpicsPositions();
}

void GroupCallBar::toggleUserpic(Userpic &userpic, bool shown) {
	userpic.hiding = !shown;
	userpic.shownAnimation.start(
		[=] { updateUserpics(); },
		shown ? 0. : 1.,
		shown ? 1. : 0.,
		kDuration);
}

void GroupCallBar::updateUserpicsPositions() {
	const auto shownCount = ranges::count(_userpics, false, &Userpic::hiding);
	if (!shownCount) {
		return;
	}
	const auto single = st::historyGroupCallUserpicSize;
	const auto shift = st::historyGroupCallUserpicShift;
	// + 1 * single for the blobs.
	const auto fullWidth = single + (shownCount - 1) * (single - shift);
	auto left = (-fullWidth / 2);
	for (auto &userpic : _userpics) {
		if (userpic.hiding) {
			continue;
		}
		if (!userpic.positionInited) {
			userpic.positionInited = true;
			userpic.left = left;
		} else if (userpic.left != left) {
			userpic.leftAnimation.start(
				[=] { updateUserpics(); },
				userpic.left,
				left,
				kDuration);
			userpic.left = left;
		}
		left += (single - shift);
	}
}

void GroupCallBar::updateUserpics() {
	const auto widget = _wrap.entity();
	const auto middle = widget->width() / 2;
	_wrap.entity()->update(
		(middle - _maxUserpicsWidth / 2),
		0,
		_maxUserpicsWidth,
		widget->height());
}

void GroupCallBar::show() {
	if (!_forceHidden) {
		return;
	}
	_forceHidden = false;
	if (_shouldBeShown) {
		_wrap.show(anim::type::instant);
		_shadow->show();
	}
}

void GroupCallBar::hide() {
	if (_forceHidden) {
		return;
	}
	_forceHidden = true;
	_wrap.hide(anim::type::instant);
	_shadow->hide();
}

void GroupCallBar::raise() {
	_wrap.raise();
	_shadow->raise();
}

void GroupCallBar::finishAnimating() {
	_wrap.finishAnimating();
}

void GroupCallBar::move(int x, int y) {
	_wrap.move(x, y);
}

void GroupCallBar::resizeToWidth(int width) {
	_wrap.entity()->resizeToWidth(width);
}

int GroupCallBar::height() const {
	return !_forceHidden
		? _wrap.height()
		: _shouldBeShown
		? st::historyReplyHeight
		: 0;
}

rpl::producer<int> GroupCallBar::heightValue() const {
	return _wrap.heightValue();
}

rpl::producer<> GroupCallBar::barClicks() const {
	return _barClicks.events();
}

rpl::producer<> GroupCallBar::joinClicks() const {
	return _join->clicks() | rpl::to_empty;
}

} // namespace Ui
