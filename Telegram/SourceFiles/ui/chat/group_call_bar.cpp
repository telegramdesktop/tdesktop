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
#include "styles/palette.h"

#include <QtGui/QtEvents>

namespace Ui {

GroupCallBar::GroupCallBar(
	not_null<QWidget*> parent,
	rpl::producer<GroupCallBarContent> content)
: _wrap(parent, object_ptr<RpWidget>(parent))
, _inner(_wrap.entity())
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


	_wrap.geometryValue(
	) | rpl::start_with_next([=](QRect rect) {
		updateShadowGeometry(rect);
		updateControlsGeometry(rect);
	}, _inner->lifetime());
}

void GroupCallBar::paint(Painter &p) {
	p.fillRect(_inner->rect(), st::historyComposeAreaBg);
	p.setPen(st::defaultMessageBar.textFg);
	p.setFont(st::defaultMessageBar.text.font);
	p.drawText(_inner->rect(), tr::lng_group_call_title(tr::now), style::al_center);
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
	return rpl::never<>();
}

} // namespace Ui
