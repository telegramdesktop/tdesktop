/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/message_sending_animation_controller.h"

#include "data/data_session.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_list_widget.h" // kItemRevealDuration
#include "history/view/media/history_view_media.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/animation_value.h"
#include "ui/effects/animation_value_f.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"
#include "window/window_session_controller.h"

namespace Ui {
namespace {

constexpr auto kSurroundingProgress = 0.5;

inline float64 OffsetMid(int value, float64 min, float64 max = 1.) {
	return ((value * max) - (value * min)) / 2.;
}

class Content final : public RpWidget {
public:
	Content(
		not_null<RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		QRect globalGeometryFrom,
		MessageSendingAnimationController::SendingInfoTo &&to);

	[[nodiscard]] rpl::producer<> destroyRequests() const;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
 	using Context = Ui::ChatPaintContext;

	void createSurrounding();

	const not_null<Window::SessionController*> _controller;
	Fn<not_null<HistoryView::Element*>()> _view;
	not_null<ChatTheme*> _theme;
	QRect _from;
	QRect _to;
	QRect _innerContentRect;

	Animations::Simple _animation;
	float64 _minScale = 0;

	base::unique_qptr<Ui::RpWidget> _surrounding;

	rpl::event_stream<> _destroyRequests;

};

Content::Content(
	not_null<RpWidget*> parent,
	not_null<Window::SessionController*> controller,
	QRect globalGeometryFrom,
	MessageSendingAnimationController::SendingInfoTo &&to)
: RpWidget(parent)
, _controller(controller)
, _view(std::move(to.view))
, _theme(to.theme)
, _from(parent->mapFromGlobal(globalGeometryFrom))
, _innerContentRect(_view()->media()->contentRectForReactions()) {
	Expects(_view != nullptr);

	show();
	setAttribute(Qt::WA_TransparentForMouseEvents);
	raise();

	base::take(
		to.globalEndGeometry
	) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](const QRect &r) {
		_to = parent->mapFromGlobal(r);
		_minScale = float64(_from.height()) / _to.height();
	}, lifetime());

	_controller->session().downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());

	resize(_innerContentRect.size());

	auto animationCallback = [=](float64 value) {
		auto resultFrom = rect();
		resultFrom.moveCenter(_from.center());

		const auto resultTo = _to.topLeft() + _innerContentRect.topLeft();
		const auto x = anim::interpolate(resultFrom.x(), resultTo.x(), value);
		const auto y = anim::interpolate(resultFrom.y(), resultTo.y(), value);
		moveToLeft(x, y);
		update();

		if ((value > kSurroundingProgress) && !_surrounding) {
			createSurrounding();
		}
		if (_surrounding) {
			_surrounding->moveToLeft(
				x - _innerContentRect.x(),
				y - _innerContentRect.y());
		}

		if (value == 1.) {
			const auto view = _view();
			const auto controller = _controller;
			_destroyRequests.fire({});
			controller->session().data().requestViewRepaint(view);
		}
	};
	animationCallback(0.);
	_animation.start(
		std::move(animationCallback),
		0.,
		1.,
		HistoryView::ListWidget::kItemRevealDuration);
}

void Content::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(e->rect(), Qt::transparent);

	const auto progress = _animation.value(_animation.animating() ? 0. : 1.);

	const auto scale = anim::interpolateF(_minScale, 1., progress);

	p.translate(
		(1 - progress) * OffsetMid(width(), _minScale),
		(1 - progress) * OffsetMid(height(), _minScale));
	p.scale(scale, scale);

	auto context = _controller->preparePaintContext({
		.theme = _theme,
	});
	const auto view = _view();
	context.skipDrawingParts = Context::SkipDrawingParts::Surrounding;
	context.outbg = view->hasOutLayout();
	p.translate(-_innerContentRect.topLeft());
	view->media()->draw(p, context);
}

rpl::producer<> Content::destroyRequests() const {
	return _destroyRequests.events();
}

void Content::createSurrounding() {
	_surrounding = base::make_unique_q<Ui::RpWidget>(parentWidget());
	_surrounding->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto view = _view();
	const auto surroundingSize = view->innerGeometry().size();
	const auto offset = _innerContentRect.topLeft();

	_surrounding->resize(surroundingSize);
	_surrounding->show();

	// Do not raise.
	_surrounding->stackUnder(this);
	stackUnder(_surrounding.get());

	_surrounding->paintRequest(
	) | rpl::start_with_next([=, size = surroundingSize](const QRect &r) {
		Painter p(_surrounding);

		p.fillRect(r, Qt::transparent);

		const auto progress = _animation.value(0.);
		const auto revProgress = 1. - progress;

		const auto divider = 1. - kSurroundingProgress;
		const auto alpha = (divider - revProgress) / divider;
		p.setOpacity(alpha);

	 	const auto scale = anim::interpolateF(_minScale, 1., progress);

	 	p.translate(
	 		revProgress * OffsetMid(size.width() + offset.x(), _minScale),
	 		revProgress * OffsetMid(size.height() + offset.y(), _minScale));
	 	p.scale(scale, scale);

		auto context = _controller->preparePaintContext({
			.theme = _theme,
		});

		const auto view = _view();

		context.skipDrawingParts = Context::SkipDrawingParts::Content;
		context.outbg = view->hasOutLayout();

		view->media()->draw(p, context);
	}, _surrounding->lifetime());
}

} // namespace

MessageSendingAnimationController::MessageSendingAnimationController(
	not_null<Window::SessionController*> controller)
: _controller(controller) {
}

void MessageSendingAnimationController::appendSending(
		MessageSendingAnimationFrom from) {
	if (anim::Disabled()) {
		return;
	}
	if (from.localId) {
		_itemSendPending[*from.localId] = from.globalStartGeometry;
	}
}

void MessageSendingAnimationController::startAnimation(SendingInfoTo &&to) {
	if (anim::Disabled()) {
		return;
	}
	const auto container = _controller->content();
	const auto item = to.view()->data();

	const auto it = _itemSendPending.find(item->fullId().msg);
	if (it == end(_itemSendPending)) {
		return;
	}
	const auto msg = it->first;

	auto content = base::make_unique_q<Content>(
		container,
		_controller,
		it->second,
		std::move(to));
	content->destroyRequests(
	) | rpl::start_with_next([=] {
		_itemSendPending.erase(msg);
		_processing.erase(item);
	}, content->lifetime());

	_processing.emplace(item, std::move(content));
}

bool MessageSendingAnimationController::hasLocalMessage(MsgId msgId) const {
	return _itemSendPending.contains(msgId);
}

bool MessageSendingAnimationController::hasAnimatedMessage(
		not_null<HistoryItem*> item) const {
	return _processing.contains(item);
}

void MessageSendingAnimationController::clear() {
	_itemSendPending.clear();
	_processing.clear();
}

} // namespace Ui
