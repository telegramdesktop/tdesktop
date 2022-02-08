/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/message_sending_animation_controller.h"

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
	void updateCache();

	const not_null<Window::SessionController*> _controller;
	not_null<HistoryItem*> _item;
	not_null<ChatTheme*> _theme;
	not_null<HistoryView::Media*> _media;
	QImage _cache;
	QRect _from;
	QRect _to;

	Animations::Simple _animation;
	float64 _minScale = 0;

	rpl::event_stream<> _destroyRequests;

};

Content::Content(
	not_null<RpWidget*> parent,
	not_null<Window::SessionController*> controller,
	QRect globalGeometryFrom,
	MessageSendingAnimationController::SendingInfoTo &&to)
: RpWidget(parent)
, _controller(controller)
, _item(to.item)
, _theme(to.theme)
, _media(_item->mainView()->media())
, _from(parent->mapFromGlobal(globalGeometryFrom)) {

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

	updateCache();

	_controller->session().downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		updateCache();
	}, lifetime());

	const auto innerContentRect = _media->contentRectForReactions();
	auto animationCallback = [=](float64 value) {
		auto resultFrom = QRect(
			QPoint(),
			_cache.size() / style::DevicePixelRatio());
		resultFrom.moveCenter(_from.center());

		const auto resultTo = _to.topLeft() + innerContentRect.topLeft();
		moveToLeft(
			anim::interpolate(resultFrom.x(), resultTo.x(), value),
			anim::interpolate(resultFrom.y(), resultTo.y(), value));
		update();

		if (value == 1.) {
			_destroyRequests.fire({});
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

	if (_cache.isNull()) {
		return;
	}

	const auto progress = _animation.value(_animation.animating() ? 0. : 1.);

	const auto scale = anim::interpolateF(_minScale, 1., progress);

	const auto size = _cache.size() / style::DevicePixelRatio();
	p.translate(
		(1 - progress) * ((size.width() - (size.width() * _minScale)) / 2),
		(1 - progress) * ((size.height() - (size.height() * _minScale)) / 2));
	p.scale(scale, scale);
	p.drawImage(QPoint(), _cache);
}

rpl::producer<> Content::destroyRequests() const {
	return _destroyRequests.events();
}

void Content::updateCache() {
	const auto innerContentRect = _media->contentRectForReactions();
	_cache = QImage(
		innerContentRect.size() * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	_cache.setDevicePixelRatio(style::DevicePixelRatio());
	_cache.fill(Qt::transparent);
	{
		Painter p(&_cache);
		PainterHighQualityEnabler hq(p);

		auto context = _controller->preparePaintContext({
			.theme = _theme,
	 	});
	 	using Context = Ui::ChatPaintContext;
		context.skipDrawingParts = Context::SkipDrawingParts::Surrounding;
		context.outbg = true;
		p.translate(-innerContentRect.left(), -innerContentRect.top());
		_media->draw(p, context);
	}
	resize(
		_cache.width() / style::DevicePixelRatio(),
		_cache.height() / style::DevicePixelRatio());
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
	const auto item = to.item;

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

} // namespace Ui
