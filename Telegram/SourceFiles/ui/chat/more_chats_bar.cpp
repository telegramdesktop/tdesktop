/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/more_chats_bar.h"

#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/text/text_options.h"
#include "ui/painter.h"
#include "lang/lang_keys.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_window.h" // st::columnMinimalWidthLeft

namespace Ui {

MoreChatsBar::MoreChatsBar(
	not_null<QWidget*> parent,
	rpl::producer<MoreChatsBarContent> content)
: _wrap(parent, object_ptr<RpWidget>(parent))
, _inner(_wrap.entity())
, _shadow(std::make_unique<PlainShadow>(_wrap.parentWidget()))
, _close(_inner.get(), st::moreChatsBarClose) {
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
	) | rpl::start_with_next([=](MoreChatsBarContent &&content) {
		_content = content;
		if (_content.count > 0) {
			_text.setText(
				st::defaultMessageBar.title,
				tr::lng_filters_bar_you_can(
					tr::now,
					lt_count,
					_content.count),
				Ui::NameTextOptions());
			_status.setText(
				st::defaultMessageBar.text,
				tr::lng_filters_bar_view(
					tr::now,
					lt_count,
					_content.count),
				Ui::NameTextOptions());
		}
		_inner->update();
	}, lifetime());

	std::move(
		copy
	) | rpl::map([=](const MoreChatsBarContent &content) {
		return !content.count;
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

MoreChatsBar::~MoreChatsBar() = default;

void MoreChatsBar::setupInner() {
	_inner->resize(0, st::moreChatsBarHeight);
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
	) | rpl::to_empty | rpl::start_to_stream(_barClicks, _inner->lifetime());

	_wrap.geometryValue(
	) | rpl::start_with_next([=](QRect rect) {
		updateShadowGeometry(rect);
		updateControlsGeometry(rect);
	}, _inner->lifetime());
}

void MoreChatsBar::paint(Painter &p) {
	p.fillRect(_inner->rect(), st::historyComposeAreaBg);

	const auto width = std::max(
		_inner->width(),
		st::columnMinimalWidthLeft);
	const auto available = width
		- st::moreChatsBarTextPosition.x()
		- st::moreChatsBarClose.width;

	p.setPen(st::defaultMessageBar.titleFg);
	_text.drawElided(
		p,
		st::moreChatsBarTextPosition.x(),
		st::moreChatsBarTextPosition.y(),
		available);

	p.setPen(st::defaultMessageBar.textFg);
	_status.drawElided(
		p,
		st::moreChatsBarStatusPosition.x(),
		st::moreChatsBarStatusPosition.y(),
		available);
}

void MoreChatsBar::updateControlsGeometry(QRect wrapGeometry) {
	const auto hidden = _wrap.isHidden() || !wrapGeometry.height();
	if (_shadow->isHidden() != hidden) {
		_shadow->setVisible(!hidden);
	}
	const auto width = std::max(
		wrapGeometry.width(),
		st::columnMinimalWidthLeft);
	_close->move(width - _close->width(), 0);
}

void MoreChatsBar::setShadowGeometryPostprocess(Fn<QRect(QRect)> postprocess) {
	_shadowGeometryPostprocess = std::move(postprocess);
	updateShadowGeometry(_wrap.geometry());
}

void MoreChatsBar::updateShadowGeometry(QRect wrapGeometry) {
	const auto regular = QRect(
		wrapGeometry.x(),
		wrapGeometry.y() + wrapGeometry.height(),
		wrapGeometry.width(),
		st::lineWidth);
	_shadow->setGeometry(_shadowGeometryPostprocess
		? _shadowGeometryPostprocess(regular)
		: regular);
}

void MoreChatsBar::show() {
	if (!_forceHidden) {
		return;
	}
	_forceHidden = false;
	if (_shouldBeShown) {
		_wrap.show(anim::type::instant);
		_shadow->show();
	}
}

void MoreChatsBar::hide() {
	if (_forceHidden) {
		return;
	}
	_forceHidden = true;
	_wrap.hide(anim::type::instant);
	_shadow->hide();
}

void MoreChatsBar::raise() {
	_wrap.raise();
	_shadow->raise();
}

void MoreChatsBar::finishAnimating() {
	_wrap.finishAnimating();
}

void MoreChatsBar::move(int x, int y) {
	_wrap.move(x, y);
}

void MoreChatsBar::resizeToWidth(int width) {
	_wrap.entity()->resizeToWidth(width);
	_inner->resizeToWidth(width);
}

int MoreChatsBar::height() const {
	return !_forceHidden
		? _wrap.height()
		: _shouldBeShown
		? st::moreChatsBarHeight
		: 0;
}

rpl::producer<int> MoreChatsBar::heightValue() const {
	return _wrap.heightValue();
}

rpl::producer<> MoreChatsBar::barClicks() const {
	return _barClicks.events();
}

rpl::producer<> MoreChatsBar::closeClicks() const {
	return _close->clicks() | rpl::to_empty;
}

} // namespace Ui
