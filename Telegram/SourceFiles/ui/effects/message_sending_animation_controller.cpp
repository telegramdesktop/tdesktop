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
#include "styles/style_chat.h"

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
		const MessageSendingAnimationFrom &fromInfo,
		MessageSendingAnimationController::SendingInfoTo &&to);

	[[nodiscard]] rpl::producer<> destroyRequests() const;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
 	using Context = Ui::ChatPaintContext;

	void createSurrounding();
	void createBubble();
	HistoryView::Element *maybeView() const;
	bool checkView(HistoryView::Element *currentView) const;
	void drawContent(Painter &p, float64 progress) const;

	const not_null<Window::SessionController*> _controller;
	const bool _crop;
	MessageSendingAnimationController::SendingInfoTo _toInfo;
	QRect _from;
	QPoint _to;
	QRect _innerContentRect;

	Animations::Simple _animation;
	float64 _minScale = 0;

	struct {
		base::unique_qptr<Ui::RpWidget> widget;
		QPoint offsetFromContent;
	} _bubble;
	base::unique_qptr<Ui::RpWidget> _surrounding;

	rpl::event_stream<> _destroyRequests;

};

Content::Content(
	not_null<RpWidget*> parent,
	not_null<Window::SessionController*> controller,
	const MessageSendingAnimationFrom &fromInfo,
	MessageSendingAnimationController::SendingInfoTo &&to)
: RpWidget(parent)
, _controller(controller)
, _crop(fromInfo.crop)
, _toInfo(std::move(to))
, _from(parent->mapFromGlobal(fromInfo.globalStartGeometry))
, _innerContentRect(maybeView()->media()->contentRectForReactions())
, _minScale(float64(_from.height()) / _innerContentRect.height()) {
	Expects(_toInfo.view != nullptr);
	Expects(_toInfo.paintContext != nullptr);

	show();
	setAttribute(Qt::WA_TransparentForMouseEvents);
	raise();

	base::take(
		_toInfo.globalEndTopLeft
	) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](const QPoint &p) {
		_to = parent->mapFromGlobal(p);
	}, lifetime());

	_controller->session().downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());

	resize(_innerContentRect.size());

	const auto innerGeometry = maybeView()->innerGeometry();

	auto animationCallback = [=](float64 value) {
		auto resultFrom = rect();
		resultFrom.moveCenter(_from.center());

		const auto resultTo = _to
			+ innerGeometry.topLeft()
			+ _innerContentRect.topLeft();
		const auto x = anim::interpolate(resultFrom.x(), resultTo.x(), value);
		const auto y = anim::interpolate(resultFrom.y(), resultTo.y(), value);
		moveToLeft(x, y);
		update();

		if ((value > kSurroundingProgress)
			&& !_surrounding
			&& !_bubble.widget) {
			const auto currentView = maybeView();
			if (!checkView(currentView)) {
				return;
			}
			if (currentView->hasBubble()) {
				createBubble();
			} else {
				createSurrounding();
			}
		}
		if (_surrounding) {
			_surrounding->moveToLeft(
				x - _innerContentRect.x(),
				y - _innerContentRect.y());
		}
		if (_bubble.widget) {
			_bubble.widget->moveToLeft(
				x - _bubble.offsetFromContent.x(),
				y - _bubble.offsetFromContent.y());
		}

		if (value == 1.) {
			const auto currentView = maybeView();
			if (!checkView(currentView)) {
				return;
			}
			const auto controller = _controller;
			_destroyRequests.fire({});
			controller->session().data().requestViewRepaint(currentView);
		}
	};
	animationCallback(0.);
	_animation.start(
		std::move(animationCallback),
		0.,
		1.,
		HistoryView::ListWidget::kItemRevealDuration);
}

HistoryView::Element *Content::maybeView() const {
	return _toInfo.view();
}

bool Content::checkView(HistoryView::Element *currentView) const {
	if (!currentView) {
		_destroyRequests.fire({});
		return false;
	}
	return true;
}

void Content::paintEvent(QPaintEvent *e) {
	const auto progress = _animation.value(_animation.animating() ? 0. : 1.);

	if (!_crop) {
		Painter p(this);
		p.fillRect(e->rect(), Qt::transparent);
		drawContent(p, progress);
	} else {
		// Use QImage to make CompositionMode_Clear work.
		auto image = QImage(
			size() * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(style::DevicePixelRatio());
		image.fill(Qt::transparent);

		const auto scaledFromSize = _from.size().scaled(
			_innerContentRect.size(),
			Qt::KeepAspectRatio);
		const auto cropW = std::ceil(
			(_innerContentRect.width() - scaledFromSize.width())
				/ 2.
				* (1. - std::clamp(progress / kSurroundingProgress, 0., 1.)));

		{
			Painter p(&image);
			drawContent(p, progress);
			p.setCompositionMode(QPainter::CompositionMode_Clear);
			p.fillRect(
				QRect(0, 0, cropW, _innerContentRect.height()),
				Qt::black);
			p.fillRect(
				QRect(
					_innerContentRect.width() - cropW,
					0,
					cropW,
					_innerContentRect.height()),
				Qt::black);
		}

		Painter p(this);
		p.drawImage(QPoint(), std::move(image));
	}
}

void Content::drawContent(Painter &p, float64 progress) const {
	const auto scale = anim::interpolateF(_minScale, 1., progress);

	p.translate(
		(1 - progress) * OffsetMid(width(), _minScale),
		(1 - progress) * OffsetMid(height(), _minScale));
	p.scale(scale, scale);

	const auto currentView = maybeView();
	if (!checkView(currentView)) {
		return;
	}

	auto context = _toInfo.paintContext();
	context.skipDrawingParts = Context::SkipDrawingParts::Surrounding;
	context.outbg = currentView->hasOutLayout();
	p.translate(-_innerContentRect.topLeft());
	currentView->media()->draw(p, context);
}

rpl::producer<> Content::destroyRequests() const {
	return _destroyRequests.events();
}

void Content::createSurrounding() {
	_surrounding = base::make_unique_q<Ui::RpWidget>(parentWidget());
	_surrounding->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto currentView = maybeView();
	if (!checkView(currentView)) {
		return;
	}
	const auto surroundingSize = currentView->innerGeometry().size();
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

		const auto currentView = maybeView();
		if (!checkView(currentView)) {
			return;
		}

		auto context = _toInfo.paintContext();
		context.skipDrawingParts = Context::SkipDrawingParts::Content;
		context.outbg = currentView->hasOutLayout();

		currentView->media()->draw(p, context);
	}, _surrounding->lifetime());
}

void Content::createBubble() {
	_bubble.widget = base::make_unique_q<Ui::RpWidget>(parentWidget());
	_bubble.widget->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto currentView = maybeView();
	if (!checkView(currentView)) {
		return;
	}
	const auto innerGeometry = currentView->innerGeometry();

	const auto tailWidth = st::historyBubbleTailOutLeft.width();
	_bubble.offsetFromContent = QPoint(
		currentView->hasOutLayout() ? 0 : tailWidth,
		innerGeometry.y());

	const auto scaleOffset = QPoint(0, innerGeometry.y());
	const auto paintOffsetLeft = innerGeometry.x()
		- _bubble.offsetFromContent.x();

	const auto hasCommentsButton = currentView->data()->repliesAreComments()
		|| currentView->data()->externalReply();
	_bubble.widget->resize(innerGeometry.size()
		+ QSize(
			currentView->hasOutLayout() ? tailWidth : 0,
			hasCommentsButton ? innerGeometry.y() : 0));
	_bubble.widget->show();

	_bubble.widget->stackUnder(this);

	_bubble.widget->paintRequest(
	) | rpl::start_with_next([=](const QRect &r) {
		Painter p(_bubble.widget);

		p.fillRect(r, Qt::transparent);

		const auto progress = _animation.value(0.);
		const auto revProgress = 1. - progress;

		const auto divider = 1. - kSurroundingProgress;
		const auto alpha = (divider - revProgress) / divider;
		p.setOpacity(alpha);

	 	const auto scale = anim::interpolateF(_minScale, 1., progress);

	 	p.translate(
	 		revProgress * OffsetMid(width() + scaleOffset.x(), _minScale),
	 		revProgress * OffsetMid(height() + scaleOffset.y(), _minScale));
	 	p.scale(scale, scale);

		const auto currentView = maybeView();
		if (!checkView(currentView)) {
			return;
		}

		auto context = _toInfo.paintContext();
		context.skipDrawingParts = Context::SkipDrawingParts::Content;
		context.outbg = currentView->hasOutLayout();

		context.translate(paintOffsetLeft, 0);
		p.translate(-paintOffsetLeft, 0);

		currentView->draw(p, context);
	}, _bubble.widget->lifetime());
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
		_itemSendPending[*from.localId] = std::move(from);
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
