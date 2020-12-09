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

	if (!_content.userpics.isNull()) {
		const auto imageSize = _content.userpics.size()
			/ _content.userpics.devicePixelRatio();
		// Skip shadow of the bar above.
		const auto imageTop = (st::historyReplyHeight
			- st::lineWidth
			- imageSize.height()) / 2 + st::lineWidth;
		const auto imageLeft = (_inner->width() - imageSize.width()) / 2;
		p.drawImage(imageLeft, imageTop, _content.userpics);
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
