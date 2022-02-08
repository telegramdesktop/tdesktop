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

constexpr auto kSurroundingProgress = 0.5;

inline float64 OffsetMid(int value, float64 min, float64 max = 1.) {
	return ((value * max) - (value * min)) / 2.;
}

class Surrounding final : public RpWidget {
public:
	Surrounding(
		not_null<RpWidget*> parent,
		QPoint offset,
		QImage &&image,
		float64 minScale);

	void setProgress(float64 value);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const QPoint _offset;
	const float64 _minScale = 0;
	QImage _cache;
	float64 _progress = 0;

};

Surrounding::Surrounding(
	not_null<RpWidget*> parent,
	QPoint offset,
	QImage &&image,
	float64 minScale)
: RpWidget(parent)
, _offset(offset)
, _minScale(minScale)
, _cache(std::move(image)) {
	resize(_cache.size() / style::DevicePixelRatio());
}

void Surrounding::setProgress(float64 value) {
	_progress = value;
	update();
}

void Surrounding::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(e->rect(), Qt::transparent);

	if (_cache.isNull()) {
		return;
	}

	const auto revProgress = 1. - _progress;

	const auto divider = 1. - kSurroundingProgress;
	const auto alpha = (divider - revProgress) / divider;
	p.setOpacity(alpha);

 	const auto scale = anim::interpolateF(_minScale, 1., _progress);

 	const auto size = _cache.size() / style::DevicePixelRatio();
 	p.translate(
 		revProgress * OffsetMid(size.width() + _offset.x(), _minScale),
 		revProgress * OffsetMid(size.height() + _offset.y(), _minScale));
 	p.scale(scale, scale);

 	p.drawImage(QPoint(), _cache);
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

	QImage drawMedia(
		Context::SkipDrawingParts skipParts,
		const QRect &rect) const;
	void updateCache();

	const not_null<Window::SessionController*> _controller;
	not_null<HistoryItem*> _item;
	not_null<ChatTheme*> _theme;
	QImage _cache;
	QRect _from;
	QRect _to;

	Animations::Simple _animation;
	float64 _minScale = 0;

	base::unique_qptr<Surrounding> _surrounding;

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

	const auto innerContentRect
		= _item->mainView()->media()->contentRectForReactions();
	auto animationCallback = [=](float64 value) {
		auto resultFrom = QRect(
			QPoint(),
			_cache.size() / style::DevicePixelRatio());
		resultFrom.moveCenter(_from.center());

		const auto resultTo = _to.topLeft() + innerContentRect.topLeft();
		const auto x = anim::interpolate(resultFrom.x(), resultTo.x(), value);
		const auto y = anim::interpolate(resultFrom.y(), resultTo.y(), value);
		moveToLeft(x, y);
		update();

		if ((value > kSurroundingProgress) && !_surrounding) {
			_surrounding = base::make_unique_q<Surrounding>(
				parent,
				innerContentRect.topLeft(),
				drawMedia(
					Context::SkipDrawingParts::Content,
					QRect(
						QPoint(),
						_item->mainView()->innerGeometry().size())),
				_minScale);
			_surrounding->show();
			_surrounding->raise();
			stackUnder(_surrounding.get());
		}
		if (_surrounding) {
			_surrounding->moveToLeft(
				x - innerContentRect.x(),
				y - innerContentRect.y());
			_surrounding->setProgress(value);
		}

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
		(1 - progress) * OffsetMid(size.width(), _minScale),
		(1 - progress) * OffsetMid(size.height(), _minScale));
	p.scale(scale, scale);
	p.drawImage(QPoint(), _cache);
}

rpl::producer<> Content::destroyRequests() const {
	return _destroyRequests.events();
}

void Content::updateCache() {
	_cache = drawMedia(
		Context::SkipDrawingParts::Surrounding,
		_item->mainView()->media()->contentRectForReactions());
	resize(_cache.size() / style::DevicePixelRatio());
}

QImage Content::drawMedia(
		Context::SkipDrawingParts skipParts,
		const QRect &rect) const {
	auto image = QImage(
		rect.size() * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(style::DevicePixelRatio());
	image.fill(Qt::transparent);
	{
		Painter p(&image);
		PainterHighQualityEnabler hq(p);

		auto context = _controller->preparePaintContext({
			.theme = _theme,
		});
		context.skipDrawingParts = skipParts;
		context.outbg = _item->mainView()->hasOutLayout();
		p.translate(-rect.left(), -rect.top());
		_item->mainView()->media()->draw(p, context);
	}
	return image;
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
