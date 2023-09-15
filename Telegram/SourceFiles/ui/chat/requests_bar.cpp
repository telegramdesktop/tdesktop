/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/requests_bar.h"

#include "ui/chat/group_call_userpics.h"
#include "ui/widgets/shadow.h"
#include "ui/text/text_options.h"
#include "ui/painter.h"
#include "lang/lang_keys.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_calls.h"
#include "styles/style_info.h" // st::topBarArrowPadding, like TopBarWidget.
#include "styles/style_window.h" // st::columnMinimalWidthLeft
#include "styles/palette.h"

#include <QtGui/QtEvents>

namespace Ui {

RequestsBar::RequestsBar(
	not_null<QWidget*> parent,
	rpl::producer<RequestsBarContent> content)
: _wrap(parent, object_ptr<RpWidget>(parent))
, _inner(_wrap.entity())
, _shadow(std::make_unique<PlainShadow>(_wrap.parentWidget()))
, _userpics(std::make_unique<GroupCallUserpics>(
		st::historyRequestsUserpics,
		rpl::single(false),
		[=] { _inner->update(); })) {
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
	) | rpl::start_with_next([=](RequestsBarContent &&content) {
		_content = content;
		if (_content.count > 0) {
			if (_content.count == 1 && !_content.nameFull.isEmpty()) {
				_textFull.setText(
					st::defaultMessageBar.title,
					tr::lng_group_requests_pending_user(
						tr::now,
						lt_user,
						_content.nameFull),
					Ui::NameTextOptions());
				_textShort.setText(
					st::defaultMessageBar.title,
					tr::lng_group_requests_pending_user(
						tr::now,
						lt_user,
						_content.nameShort),
					Ui::NameTextOptions());
			} else {
				_textShort.setText(
					st::defaultMessageBar.title,
					tr::lng_group_requests_pending(
						tr::now,
						lt_count_decimal,
						_content.count),
					Ui::NameTextOptions());
				_textFull.clear();
			}
		}
		_userpics->update(_content.users, !_wrap.isHidden());
		_inner->update();
	}, lifetime());

	std::move(
		copy
	) | rpl::map([=](const RequestsBarContent &content) {
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

	_userpics->widthValue(
	) | rpl::start_with_next([=](int width) {
		_userpicsWidth = width;
	}, lifetime());

	setupInner();
}

RequestsBar::~RequestsBar() = default;

void RequestsBar::setupInner() {
	_inner->resize(0, st::historyRequestsHeight);
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

void RequestsBar::paint(Painter &p) {
	p.fillRect(_inner->rect(), st::historyComposeAreaBg);

	const auto userpicsSize = st::historyRequestsUserpics.size;
	const auto userpicsTop = st::lineWidth + (st::historyRequestsHeight
		- st::lineWidth
		- userpicsSize) / 2;
	const auto userpicsLeft = userpicsTop * 2;
	const auto textTop = st::lineWidth + (st::historyRequestsHeight
		- st::lineWidth
		- st::semiboldFont->height) / 2;
	const auto width = _inner->width();
	const auto &font = st::defaultMessageBar.title.font;
	p.setPen(st::defaultMessageBar.titleFg);
	p.setFont(font);

	if (width >= st::columnMinimalWidthLeft / 2) {
		const auto textLeft = userpicsLeft + _userpicsWidth + userpicsLeft;
		const auto available = width - textLeft - userpicsLeft;
		if (_textFull.isEmpty() || available < _textFull.maxWidth()) {
			_textShort.drawElided(p, textLeft, textTop, available);
		} else {
			_textFull.drawElided(p, textLeft, textTop, available);
		}
	}

	// Skip shadow of the bar above.
	_userpics->paint(p, userpicsLeft, userpicsTop, userpicsSize);
}

void RequestsBar::updateControlsGeometry(QRect wrapGeometry) {
	const auto hidden = _wrap.isHidden() || !wrapGeometry.height();
	if (_shadow->isHidden() != hidden) {
		_shadow->setVisible(!hidden);
	}
}

void RequestsBar::setShadowGeometryPostprocess(Fn<QRect(QRect)> postprocess) {
	_shadowGeometryPostprocess = std::move(postprocess);
	updateShadowGeometry(_wrap.geometry());
}

void RequestsBar::updateShadowGeometry(QRect wrapGeometry) {
	const auto regular = QRect(
		wrapGeometry.x(),
		wrapGeometry.y() + wrapGeometry.height(),
		wrapGeometry.width(),
		st::lineWidth);
	_shadow->setGeometry(_shadowGeometryPostprocess
		? _shadowGeometryPostprocess(regular)
		: regular);
}

void RequestsBar::show() {
	if (!_forceHidden) {
		return;
	}
	_forceHidden = false;
	if (_shouldBeShown) {
		_wrap.show(anim::type::instant);
		_shadow->show();
	}
}

void RequestsBar::hide() {
	if (_forceHidden) {
		return;
	}
	_forceHidden = true;
	_wrap.hide(anim::type::instant);
	_shadow->hide();
}

void RequestsBar::raise() {
	_wrap.raise();
	_shadow->raise();
}

void RequestsBar::finishAnimating() {
	_wrap.finishAnimating();
}

void RequestsBar::move(int x, int y) {
	_wrap.move(x, y);
}

void RequestsBar::resizeToWidth(int width) {
	_wrap.entity()->resizeToWidth(width);
	_inner->resizeToWidth(width);
}

int RequestsBar::height() const {
	return !_forceHidden
		? _wrap.height()
		: _shouldBeShown
		? st::historyRequestsHeight
		: 0;
}

rpl::producer<int> RequestsBar::heightValue() const {
	return _wrap.heightValue();
}

rpl::producer<> RequestsBar::barClicks() const {
	return _barClicks.events();
}


} // namespace Ui
