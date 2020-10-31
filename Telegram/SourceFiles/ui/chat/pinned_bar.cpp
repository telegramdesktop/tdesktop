/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/pinned_bar.h"

#include "ui/chat/message_bar.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/buttons.h"
#include "styles/style_chat.h"
#include "styles/palette.h"

#include <QtGui/QtEvents>

namespace Ui {

PinnedBar::PinnedBar(
	not_null<QWidget*> parent,
	rpl::producer<MessageBarContent> content)
: _wrap(parent, object_ptr<RpWidget>(parent))
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
	) | rpl::filter([=](const MessageBarContent &content) {
		return !content.title.isEmpty() || !content.text.text.isEmpty();
	}) | rpl::start_with_next([=](MessageBarContent &&content) {
		const auto creating = !_bar;
		if (creating) {
			createControls();
		}
		_bar->set(std::move(content));
		if (creating) {
			_bar->finishAnimating();
		}
	}, lifetime());

	std::move(
		copy
	) | rpl::map([=](const MessageBarContent &content) {
		return content.title.isEmpty() || content.text.text.isEmpty();
	}) | rpl::start_with_next_done([=](bool hidden) {
		_shouldBeShown = !hidden;
		if (!_forceHidden) {
			_wrap.toggle(_shouldBeShown, anim::type::normal);
		} else if (!_shouldBeShown) {
			_bar = nullptr;
		}
	}, [=] {
		_forceHidden = true;
		_wrap.toggle(false, anim::type::normal);
	}, lifetime());
}

PinnedBar::~PinnedBar() {
	_rightButton.destroy();
}

void PinnedBar::setRightButton(object_ptr<Ui::RpWidget> button) {
	_rightButton.destroy();
	_rightButton = std::move(button);
	if (_rightButton) {
		_rightButton->setParent(_wrap.entity());
		_rightButton->show();
	}
	if (_bar) {
		updateControlsGeometry(_wrap.geometry());
	}
}

void PinnedBar::updateControlsGeometry(QRect wrapGeometry) {
	_bar->widget()->resizeToWidth(
		wrapGeometry.width() - (_rightButton ? _rightButton->width() : 0));
	const auto hidden = _wrap.isHidden() || !wrapGeometry.height();
	if (_shadow->isHidden() != hidden) {
		_shadow->setVisible(!hidden);
	}
	if (_rightButton) {
		_rightButton->moveToRight(0, 0);
	}
}

void PinnedBar::setShadowGeometryPostprocess(Fn<QRect(QRect)> postprocess) {
	_shadowGeometryPostprocess = std::move(postprocess);
	updateShadowGeometry(_wrap.geometry());
}

void PinnedBar::updateShadowGeometry(QRect wrapGeometry) {
	const auto regular = QRect(
		wrapGeometry.x(),
		wrapGeometry.y() + wrapGeometry.height(),
		wrapGeometry.width(),
		st::lineWidth);
	_shadow->setGeometry(_shadowGeometryPostprocess
		? _shadowGeometryPostprocess(regular)
		: regular);
}

void PinnedBar::createControls() {
	Expects(!_bar);

	_bar = std::make_unique<MessageBar>(
		_wrap.entity(),
		st::defaultMessageBar);
	if (_rightButton) {
		_rightButton->raise();
	}

	// Clicks.
	_bar->widget()->setCursor(style::cur_pointer);
	_bar->widget()->events(
	) | rpl::filter([=](not_null<QEvent*> event) {
		return (event->type() == QEvent::MouseButtonPress);
	}) | rpl::map([=] {
		return _bar->widget()->events(
		) | rpl::filter([=](not_null<QEvent*> event) {
			return (event->type() == QEvent::MouseButtonRelease);
		}) | rpl::take(1) | rpl::filter([=](not_null<QEvent*> event) {
			return _bar->widget()->rect().contains(
				static_cast<QMouseEvent*>(event.get())->pos());
		});
	}) | rpl::flatten_latest(
	) | rpl::map([] {
		return rpl::empty_value();
	}) | rpl::start_to_stream(_barClicks, _bar->widget()->lifetime());

	_bar->widget()->move(0, 0);
	_bar->widget()->show();
	_wrap.entity()->resize(_wrap.entity()->width(), _bar->widget()->height());

	_wrap.geometryValue(
	) | rpl::start_with_next([=](QRect rect) {
		updateShadowGeometry(rect);
		updateControlsGeometry(rect);
	}, _bar->widget()->lifetime());

	_wrap.shownValue(
	) | rpl::skip(
		1
	) | rpl::filter([=](bool shown) {
		return !shown && !_forceHidden;
	}) | rpl::start_with_next([=] {
		_bar = nullptr;
	}, _bar->widget()->lifetime());

	Ensures(_bar != nullptr);
}

void PinnedBar::show() {
	if (!_forceHidden) {
		return;
	}
	_forceHidden = false;
	if (_shouldBeShown) {
		_wrap.show(anim::type::instant);
		_shadow->show();
	}
}

void PinnedBar::hide() {
	if (_forceHidden) {
		return;
	}
	_forceHidden = true;
	_wrap.hide(anim::type::instant);
	_shadow->hide();
}

void PinnedBar::raise() {
	_wrap.raise();
	_shadow->raise();
}

void PinnedBar::finishAnimating() {
	_wrap.finishAnimating();
}

void PinnedBar::move(int x, int y) {
	_wrap.move(x, y);
}

void PinnedBar::resizeToWidth(int width) {
	_wrap.entity()->resizeToWidth(width);
}

int PinnedBar::height() const {
	return !_forceHidden
		? _wrap.height()
		: _shouldBeShown
		? st::historyReplyHeight
		: 0;
}

rpl::producer<int> PinnedBar::heightValue() const {
	return _wrap.heightValue();
}

rpl::producer<> PinnedBar::barClicks() const {
	return _barClicks.events();
}

} // namespace Ui
