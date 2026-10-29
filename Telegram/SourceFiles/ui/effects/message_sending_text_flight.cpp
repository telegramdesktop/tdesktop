/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/message_sending_text_flight.h"

#include "data/data_session.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/animation_value.h"
#include "ui/effects/premium_3d_support.h"
#include "ui/image/image_opaque_bounds.h"
#include "ui/text/text_isolated_emoji.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/rect.h"
#include "ui/ui_utility.h"

#include "styles/style_chat.h"

#include <QtGui/QTextBlock>
#include <QtGui/QTextDocument>
#include <QtGui/QTextLayout>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QTextEdit>

namespace Ui {
namespace {

constexpr auto kDuration = crl::time(280);
constexpr auto kSlideDuration = crl::time(160);
constexpr auto kFadeDuration = crl::time(110);
constexpr auto kGiveUpTimeout = crl::time(1000);

using SkipDrawingParts = ChatPaintContext::SkipDrawingParts;

struct InkCrop {
	QImage frame;
	QRect geometry;
};

[[nodiscard]] QRect InkGeometry(const QImage &image) {
	const auto ink = OpaqueBounds(image);
	if (ink.isEmpty()) {
		return QRect();
	}
	const auto ratio = int(image.devicePixelRatio());
	const auto left = ink.x() / ratio;
	const auto top = ink.y() / ratio;
	const auto right = (ink.x() + ink.width() + ratio - 1) / ratio;
	const auto bottom = (ink.y() + ink.height() + ratio - 1) / ratio;
	return QRect(left, top, right - left, bottom - top);
}

[[nodiscard]] InkCrop CropToInk(const QImage &image) {
	const auto geometry = InkGeometry(image);
	if (geometry.isEmpty()) {
		return {};
	}
	const auto ratio = int(image.devicePixelRatio());
	return {
		.frame = image.copy(QRect(
			geometry.topLeft() * ratio,
			geometry.size() * ratio)),
		.geometry = geometry,
	};
}

[[nodiscard]] bool HasSingleVisualLine(
		not_null<const QTextDocument*> document) {
	auto lines = 0;
	for (auto block = document->begin(); block.isValid(); block = block.next()) {
		const auto layout = block.layout();
		if (!layout
			|| block.blockFormat().hasProperty(InputField::kQuoteId)) {
			return false;
		}
		lines += layout->lineCount();
	}
	return (lines == 1);
}

[[nodiscard]] QImage GrabWithoutCaret(not_null<QTextEdit*> edit) {
	const auto cursorWidth = edit->cursorWidth();
	const auto restore = gsl::finally([&] {
		edit->setCursorWidth(cursorWidth);
	});
	edit->setCursorWidth(0);
	return GrabWidgetToImage(edit);
}

[[nodiscard]] crl::time GiveUpDeadline(crl::time capturedAt) {
	return capturedAt + kGiveUpTimeout * anim::SlowMultiplier();
}

[[nodiscard]] float64 RiseProgress(float64 progress) {
	return Premium::CubicBezier(0.25, 0.1, 0.25, 1., progress);
}

[[nodiscard]] QImage TransparentImage(QSize size) {
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		size * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	return result;
}

[[nodiscard]] QImage RenderLayer(
		not_null<HistoryView::Element*> view,
		ChatPaintContext context,
		SkipDrawingParts skip,
		QRect rect) {
	auto result = TransparentImage(rect.size());
	auto p = Painter(&result);
	p.translate(-rect.topLeft());
	if (skip == SkipDrawingParts::Bubble) {
		p.setClipRect(QRect(0, 0, 0, 0));
	}
	context.skipDrawingParts = skip;
	view->draw(p, context);
	p.end();
	return result;
}

[[nodiscard]] QRect MeasureLayer(
		not_null<HistoryView::Element*> view,
		ChatPaintContext context,
		SkipDrawingParts skip) {
	const auto rect = Rect(QSize(view->width(), view->height()));
	context.clip = rect;
	context.outbg = view->hasOutLayout();
	return InkGeometry(RenderLayer(view, std::move(context), skip, rect));
}

[[nodiscard]] QImage MixText(
		const QImage &from,
		QPoint position,
		const QImage &to,
		float64 progress) {
	auto result = QImage(to.size(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(to.devicePixelRatio());
	result.fill(Qt::transparent);
	auto p = QPainter(&result);
	p.setOpacity(1. - progress);
	p.drawImage(position, from);
	p.setCompositionMode(QPainter::CompositionMode_Plus);
	p.setOpacity(progress);
	p.drawImage(0, 0, to);
	p.end();
	return result;
}

} // namespace

auto GrabSendingFieldText(
		not_null<InputField*> field,
		const TextWithTags &sending)
-> std::optional<MessageSendingAnimationFrom::FieldText> {
	const auto edit = field->rawTextEdit();
	if (anim::Disabled()
		|| PowerSaving::On(PowerSaving::Flag::kChatEffects)
		|| field->empty()
		|| !edit->isVisible()
		|| edit->textCursor().hasSelection()
		|| (field->getTextWithAppliedMarkdown() != sending)) {
		return std::nullopt;
	}
	const auto document = field->document();
	if (field->scrollTopMax() > 0
		|| field->scrollTop().current() != 0
		|| edit->horizontalScrollBar()->value() != 0
		|| document->size().height() > edit->viewport()->height()
		|| !HasSingleVisualLine(document)) {
		return std::nullopt;
	}
	auto ink = CropToInk(GrabWithoutCaret(edit));
	const auto hidden = QRegion(ink.geometry).subtracted(
		edit->visibleRegion());
	if (ink.geometry.isEmpty() || !hidden.isEmpty()) {
		return std::nullopt;
	}
	return MessageSendingAnimationFrom::FieldText{
		.frame = std::move(ink.frame),
		.globalGeometry = QRect(
			edit->mapToGlobal(ink.geometry.topLeft()),
			ink.geometry.size()),
		.capturedAt = crl::now(),
	};
}

auto MessageSendingTextFlight::Prepare(
		const FieldText &source,
		const SendingInfoTo &to)
-> std::optional<Prepared> {
	const auto view = to.view();
	const auto textItem = view ? view->textItem() : nullptr;
	const auto left = GiveUpDeadline(source.capturedAt) - crl::now();
	if (!textItem
		|| source.frame.isNull()
		|| source.globalGeometry.isEmpty()
		|| (left < kDuration * anim::SlowMultiplier())
		|| !view->hasBubble()
		|| !view->hasVisibleText()
		|| view->media()
		|| textItem->translatedRichPage()
		|| view->isolatedEmoji()
		|| view->onlyCustomEmoji()
		|| view->width() <= 0
		|| view->height() <= 0) {
		return std::nullopt;
	}
	const auto context = to.paintContext();
	const auto text = MeasureLayer(view, context, SkipDrawingParts::Bubble);
	const auto bubble = MeasureLayer(
		view,
		context,
		SkipDrawingParts::Content);
	const auto line = st::messageTextStyle.font->height;
	if (text.isEmpty()
		|| bubble.isEmpty()
		|| (text.height() >= source.globalGeometry.height() + line / 2)) {
		return std::nullopt;
	}
	return Prepared{
		.source = source,
		.textRect = text,
		.bubbleRect = bubble,
		.viewSize = QSize(view->width(), view->height()),
	};
}

MessageSendingReveal MessageSendingTextFlight::Reveal() {
	return {
		.duration = kDuration,
		.transition = [](float64 delta, float64 dt) {
			return delta * RiseProgress(dt);
		},
	};
}

MessageSendingTextFlight::MessageSendingTextFlight(
	not_null<RpWidget*> parent,
	Prepared &&prepared,
	SendingInfoTo &&to)
: RpWidget(parent)
, _toInfo(std::move(to))
, _source(std::move(prepared.source.frame))
, _sourceRect(
	prepared.textRect.topLeft(),
	prepared.source.globalGeometry.size())
, _textRect(prepared.textRect)
, _bounds(prepared.bubbleRect.united(_sourceRect).united(_textRect))
, _viewSize(prepared.viewSize)
, _start(parent->mapFromGlobal(prepared.source.globalGeometry.topLeft())
	- _textRect.topLeft())
, _position(_start)
, _giveUpTimer([=] { finish(); }) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setGeometry(QRect(_position + _bounds.topLeft(), _bounds.size()));
	show();
	raise();

	base::take(
		_toInfo.globalLiveTopLeft
	) | rpl::on_next([=](const std::optional<QPoint> &point) {
		_target = point
			? std::make_optional(parent->mapFromGlobal(*point))
			: std::nullopt;
		if (_residual) {
			refresh();
		}
	}, lifetime());

	_giveUpTimer.callOnce(std::max(
		GiveUpDeadline(prepared.source.capturedAt) - crl::now(),
		crl::time()));
	_animation.start([=](float64 value) {
		tick(value);
	}, 0., 1., kDuration);
}

rpl::producer<> MessageSendingTextFlight::destroyRequests() const {
	return _destroyRequests.events();
}

void MessageSendingTextFlight::tick(float64 progress) {
	const auto context = _toInfo.paintContext();

	// Not in target emissions: a scroll moves the list before its visible top.
	_bandTop = context.area.y() - context.viewport.y();
	_bandBottom = _bandTop + context.area.height();
	_progress = progress;
	refresh();
}

void MessageSendingTextFlight::refresh() {
	const auto view = _toInfo.view();
	if (!view || !_target) {
		finish();
		return;
	}
	refreshBounds(view);
	const auto target = clampedTarget(view->height());
	if (!_residual) {
		_residual = _start - target;
	}
	const auto elapsed = _progress * kDuration;
	const auto slide = std::min(elapsed / kSlideDuration, 1.);
	const auto x = 1. - anim::easeOutQuint(1., slide);
	const auto y = 1. - RiseProgress(_progress);
	_position = target + QPoint(
		int(base::SafeRound(_residual->x() * x)),
		int(base::SafeRound(_residual->y() * y)));
	setGeometry(QRect(_position + _bounds.topLeft(), _bounds.size()));
	update();
	if (_progress == 1. && target == *_target) {
		finish();
	}
}

void MessageSendingTextFlight::refreshBounds(
		not_null<HistoryView::Element*> view) {
	const auto size = QSize(view->width(), view->height());
	if (_viewSize == size) {
		return;
	}
	_viewSize = size;
	const auto bubble = MeasureLayer(
		view,
		_toInfo.paintContext(),
		SkipDrawingParts::Content);
	if (!bubble.isEmpty()) {
		_bounds = bubble.united(_sourceRect).united(_textRect);
	}
}

QPoint MessageSendingTextFlight::clampedTarget(int height) const {
	const auto lowest = std::max(
		_bandTop,
		_bandBottom - height - st::historyPaddingBottom);
	return QPoint(
		_target->x(),
		std::clamp(_target->y(), _bandTop, lowest));
}

ChatPaintContext MessageSendingTextFlight::prepareContext(
		not_null<HistoryView::Element*> view) const {
	auto result = _toInfo.paintContext();
	result.translate(
		_target->x() - _position.x(),
		-(_position.y() + result.viewport.y()));
	result.clip = _bounds;
	result.outbg = view->hasOutLayout();
	return result;
}

void MessageSendingTextFlight::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	p.translate(-_bounds.topLeft());

	const auto elapsed = _progress * kDuration;
	const auto mix = std::min(elapsed / kFadeDuration, 1.);
	const auto view = (mix > 0.) ? _toInfo.view() : nullptr;
	if (!view || !_target) {
		p.drawImage(_sourceRect.topLeft(), _source);
		return;
	}
	const auto context = prepareContext(view);
	if (mix == 1.) {
		view->draw(p, context);
		return;
	}
	p.setOpacity(mix);
	p.drawImage(
		_bounds.topLeft(),
		RenderLayer(view, context, SkipDrawingParts::Content, _bounds));
	p.setOpacity(1.);
	const auto text = _sourceRect.united(_textRect);
	p.drawImage(text.topLeft(), MixText(
		_source,
		_sourceRect.topLeft() - text.topLeft(),
		RenderLayer(view, context, SkipDrawingParts::Bubble, text),
		mix));
}

void MessageSendingTextFlight::finish() {
	const auto view = _toInfo.view();
	const auto owner = view ? &view->history()->owner() : nullptr;
	_destroyRequests.fire({});
	if (owner) {
		owner->requestViewRepaint(view);
	}
}

} // namespace Ui
