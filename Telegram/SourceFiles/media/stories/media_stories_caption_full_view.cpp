/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_caption_full_view.h"

#include "base/event_filter.h"
#include "core/ui_integration.h"
#include "chat_helpers/compose/compose_show.h"
#include "media/stories/media_stories_controller.h"
#include "media/stories/media_stories_view.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/labels.h"
#include "ui/click_handler.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "styles/style_media_view.h"

namespace Media::Stories {

CaptionFullView::CaptionFullView(not_null<Controller*> controller)
: _controller(controller)
, _scroll(std::make_unique<Ui::ElasticScroll>(controller->wrap()))
, _wrap(_scroll->setOwnedWidget(
	object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
		_scroll.get(),
		object_ptr<Ui::FlatLabel>(_scroll.get(), st::storiesCaptionFull),
		st::mediaviewCaptionPadding + _controller->repostCaptionPadding())))
, _text(_wrap->entity()) {
	_text->setMarkedText(controller->captionText(), Core::MarkedTextContext{
		.session = &controller->uiShow()->session(),
		.customEmojiRepaint = [=] { _text->update(); },
	});

	startAnimation();
	_controller->layoutValue(
	) | rpl::start_with_next([=](const Layout &layout) {
		if (_outer != layout.content) {
			const auto skip = layout.header.y()
				+ layout.header.height()
				- layout.content.y();
			_outer = layout.content.marginsRemoved({ 0, skip, 0, 0 });
			updateGeometry();
		}
	}, _scroll->lifetime());

	const auto filter = [=](not_null<QEvent*> e) {
		const auto mouse = [&] {
			return static_cast<QMouseEvent*>(e.get());
		};
		const auto type = e->type();
		if (type == QEvent::MouseButtonPress
			&& mouse()->button() == Qt::LeftButton
			&& !ClickHandler::getActive()) {
			_down = true;
		} else if (type == QEvent::MouseButtonRelease && _down) {
			_down = false;
			if (!ClickHandler::getPressed()) {
				close();
			}
		} else if (type == QEvent::KeyPress
			&& static_cast<QKeyEvent*>(e.get())->key() == Qt::Key_Escape) {
			close();
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	};
	base::install_event_filter(_text.get(), filter);
	if (_controller->repost()) {
		_wrap->setMouseTracking(true);
		base::install_event_filter(_wrap.get(), [=](not_null<QEvent*> e) {
			const auto mouse = [&] {
				return static_cast<QMouseEvent*>(e.get());
			};
			const auto type = e->type();
			if (type == QEvent::MouseMove) {
				const auto handler = _controller->lookupRepostHandler(
					mouse()->pos() - QPoint(
						st::mediaviewCaptionPadding.left(),
						(_wrap->padding().top()
							- _controller->repostCaptionPadding().top())));
				ClickHandler::setActive(handler.link, handler.host);
				_wrap->setCursor(handler.link
					? style::cur_pointer
					: style::cur_default);
			} else if (type == QEvent::MouseButtonPress
				&& mouse()->button() == Qt::LeftButton
				&& ClickHandler::getActive()) {
				ClickHandler::pressed();
			} else if (type == QEvent::MouseButtonRelease) {
				if (const auto activated = ClickHandler::unpressed()) {
					ActivateClickHandler(_wrap.get(), activated, {
						mouse()->button(), QVariant(),
					});
				}
			}
			return base::EventFilterResult::Continue;
		});
	}
	base::install_event_filter(_wrap.get(), filter);

	using Type = Ui::ElasticScroll::OverscrollType;

	rpl::combine(
		_scroll->positionValue(),
		_scroll->movementValue()
	) | rpl::filter([=] {
		return !_closing;
	}) | rpl::start_with_next([=](
			Ui::ElasticScrollPosition position,
			Ui::ElasticScrollMovement movement) {
		const auto overscrollTop = std::max(-position.overscroll, 0);
		using Phase = Ui::ElasticScrollMovement;
		if (movement == Phase::Progress) {
			if (overscrollTop > 0) {
				_pulling = true;
			} else {
				_pulling = false;
			}
		} else if (_pulling
			&& (movement == Phase::Momentum
				|| movement == Phase::Returning)) {
			_pulling = false;
			if (overscrollTop > st::storiesCaptionPullThreshold) {
				_closingTopAdded = overscrollTop;
				_scroll->setOverscrollTypes(Type::None, Type::Real);
				close();
				updateGeometry();
			}
		}
	}, _scroll->lifetime());

	_wrap->paintRequest() | rpl::start_with_next([=] {
		if (_controller->repost()) {
			auto p = Painter(_wrap.get());
			_controller->drawRepostInfo(
				p,
				st::mediaviewCaptionPadding.left(),
				(_wrap->padding().top()
					- _controller->repostCaptionPadding().top()),
				_wrap->width());
		}
	}, _wrap->lifetime());

	_scroll->show();
	_scroll->setOverscrollBg(QColor(0, 0, 0, 0));
	_scroll->setOverscrollTypes(Type::Real, Type::Real);
	_text->show();
	_text->setFocus();
}

CaptionFullView::~CaptionFullView() = default;

bool CaptionFullView::closing() const {
	return _closing;
}

bool CaptionFullView::focused() const {
	return Ui::InFocusChain(_scroll.get());
}

void CaptionFullView::close() {
	if (_closing) {
		return;
	}
	_closing = true;
	_controller->captionClosing();
	startAnimation();
}

void CaptionFullView::repaint() {
	_wrap->update();
}

void CaptionFullView::updateGeometry() {
	if (_outer.isEmpty()) {
		return;
	}
	const auto lineHeight = st::mediaviewCaptionStyle.font->height;
	const auto padding = st::mediaviewCaptionPadding
		+ _controller->repostCaptionPadding();
	_text->resizeToWidth(_outer.width() - padding.left() - padding.right());
	const auto add = padding.top() + padding.bottom();
	const auto maxShownHeight = lineHeight * kMaxShownCaptionLines;
	const auto shownHeight = (_text->height() > maxShownHeight)
		? (lineHeight * kCollapsedCaptionLines)
		: _text->height();
	const auto collapsedHeight = shownHeight + add;
	const auto addedToBottom = lineHeight;
	const auto expandedHeight = _text->height() + add + addedToBottom;
	const auto fullHeight = std::min(expandedHeight, _outer.height());
	const auto shown = _animation.value(_closing ? 0. : 1.);
	const auto height = (_closing || _animation.animating())
		? anim::interpolate(collapsedHeight, fullHeight, shown)
		: _outer.height();
	const auto added = anim::interpolate(0, _closingTopAdded, shown);
	const auto bottomPadding = anim::interpolate(0, addedToBottom, shown);
	const auto use = padding + ((_closing || _animation.animating())
		? QMargins(0, 0, 0, bottomPadding)
		: QMargins(0, height - fullHeight, 0, bottomPadding));
	_wrap->setPadding(use);
	_scroll->setGeometry(
		_outer.x(),
		added + _outer.y() + _outer.height() - height,
		_outer.width(),
		std::max(height - added, 0));
	if (_closing && !_animation.animating()) {
		_controller->captionClosed();
	}
}

void CaptionFullView::startAnimation() {
	_animation.start(
		[=] { updateGeometry(); },
		_closing ? 1. : 0.,
		_closing ? 0. : 1.,
		st::fadeWrapDuration,
		anim::sineInOut);
}

} // namespace Media::Stories
