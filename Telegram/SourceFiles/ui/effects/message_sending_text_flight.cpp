/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/message_sending_text_flight.h"

#include "data/data_session.h"
#include "history/view/media/history_view_media.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_style_radius.h"
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
#include "styles/style_chat_helpers.h"

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
constexpr auto kMorphMaxFieldHeights = 1.5;
constexpr auto kMorphHiddenLines = 3;

using SkipDrawingParts = ChatPaintContext::SkipDrawingParts;

struct InkCrop {
	QImage frame;
	QRect geometry;
};

struct MorphLayout {
	QRect source;
	QRect from;
	QPoint cut;
	QRect info;
	int edgeRow = 0;
};

struct MorphCut {
	int position = 0;
	int allowance = 0;
};

struct ResizeSpan {
	int source = 0;
	int sourceSize = 0;
	int target = 0;
	int targetSize = 0;
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

[[nodiscard]] QImage CropImage(const QImage &image, QRect geometry) {
	const auto ratio = int(image.devicePixelRatio());
	return image.copy(QRect(
		geometry.topLeft() * ratio,
		geometry.size() * ratio));
}

[[nodiscard]] InkCrop CropToInk(const QImage &image) {
	const auto geometry = InkGeometry(image);
	if (geometry.isEmpty()) {
		return {};
	}
	return {
		.frame = CropImage(image, geometry),
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

[[nodiscard]] bool LastBlockRightToLeft(
		not_null<const QTextDocument*> document) {
	auto block = document->lastBlock();
	while (block.isValid() && block.text().trimmed().isEmpty()) {
		block = block.previous();
	}
	return block.isValid()
		? (block.textDirection() == Qt::RightToLeft)
		: style::RightToLeft();
}

[[nodiscard]] QImage GrabWithoutCaret(not_null<QTextEdit*> edit) {
	const auto cursorWidth = edit->cursorWidth();
	const auto restore = gsl::finally([&] {
		edit->setCursorWidth(cursorWidth);
	});
	edit->setCursorWidth(0);
	return GrabWidgetToImage(edit);
}

void ApplyTopFade(QImage &image, not_null<InputField*> field) {
	const auto height = st::historyComposeFieldFadeHeight;
	auto gradient = QLinearGradient(0, 1, 0, height);
	gradient.setStops({
		{ 0., field->st().textBg->c },
		{ .9, Qt::transparent },
	});
	auto p = QPainter(&image);
	p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
	p.translate(-field->rawTextEdit()->mapTo(field.get(), QPoint()));
	p.fillRect(QRect(0, 0, field->width(), height), gradient);
}

[[nodiscard]] crl::time GiveUpDeadline(crl::time capturedAt) {
	return capturedAt + kGiveUpTimeout * anim::SlowMultiplier();
}

[[nodiscard]] crl::time StartDeadline(crl::time capturedAt) {
	return GiveUpDeadline(capturedAt) - kDuration * anim::SlowMultiplier();
}

[[nodiscard]] float64 RiseProgress(float64 progress) {
	return Premium::CubicBezier(0.25, 0.1, 0.25, 1., progress);
}

[[nodiscard]] int RevealHeight(
		int height,
		int lift,
		int lead,
		float64 progress) {
	const auto rise = RiseProgress(progress);
	if (lift <= 0) {
		return anim::interpolate(height, 0, rise);
	}
	// WHY: the morph box's top closes its lead over the bubble on the fade,
	// not on the rise, so that part of the lifted reveal follows the fade
	// to keep the message above clear of the flying box.
	const auto start = height - lift;
	const auto follow = std::clamp(lead, 0, std::max(start, 0));
	const auto fade = std::min(progress * kDuration / kFadeDuration, 1.);
	return anim::interpolate(start - follow, 0, rise)
		+ anim::interpolate(follow, 0, fade);
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

[[nodiscard]] bool PaintsOutsideBubble(
		not_null<HistoryView::Element*> view,
		const ChatPaintContext &context) {
	return view->Get<HistoryView::UnreadBar>()
		|| view->Get<HistoryView::ServicePreMessage>()
		|| (view->delegate()->elementInSelectionMode(view).progress > 0.)
		|| context.gestureHorizontal.visualTranslationFor(
			view->data()->id.bare);
}

[[nodiscard]] QRect BubbleSpan(
		not_null<HistoryView::Element*> view,
		const ChatPaintContext &context) {
	const auto whole = Rect(QSize(view->width(), view->height()));
	if (PaintsOutsideBubble(view, context)) {
		return whole;
	}
	const auto &st = context.st->messageStyle(view->hasOutLayout(), false);
	const auto tail = std::max(st.tailLeft.width(), st.tailRight.width());
	const auto body = view->innerGeometry();
	return whole.intersected(QRect(
		body.x() - tail,
		0,
		body.width() + 2 * tail,
		whole.height()));
}

[[nodiscard]] QImage RenderSpan(
		not_null<HistoryView::Element*> view,
		ChatPaintContext context,
		SkipDrawingParts skip,
		QRect span) {
	context.clip = Rect(QSize(view->width(), view->height()));
	context.outbg = view->hasOutLayout();
	return RenderLayer(view, std::move(context), skip, span);
}

[[nodiscard]] QRect MeasureLayer(
		not_null<HistoryView::Element*> view,
		ChatPaintContext context,
		SkipDrawingParts skip,
		QRect span) {
	return InkGeometry(
		RenderSpan(view, std::move(context), skip, span)
	).translated(span.topLeft());
}

[[nodiscard]] QImage MixText(
		const QImage &from,
		QPoint fromPosition,
		const QImage &to,
		QPoint toPosition,
		QSize size,
		float64 progress) {
	const auto ratio = to.devicePixelRatio();
	auto result = QImage(size * ratio, QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	auto p = QPainter(&result);
	p.setOpacity(1. - progress);
	p.drawImage(fromPosition, from);
	p.setCompositionMode(QPainter::CompositionMode_Plus);
	p.setOpacity(progress);
	p.drawImage(toPosition, to);
	p.end();
	return result;
}

[[nodiscard]] bool TooTallToMorph(int height) {
	return (height > st::historyComposeFieldMaxHeight * kMorphMaxFieldHeights);
}

[[nodiscard]] MorphCut ComputeCut(int from, int till, int fallback) {
	if (from >= till) {
		return { .position = fallback };
	}
	return {
		.position = from + (till - from) / 2,
		.allowance = till - from,
	};
}

[[nodiscard]] QRect InfoGeometry(QRect body, int width) {
	if (width <= 0) {
		return QRect();
	}
	const auto height = st::msgDateFont->height;
	const auto right = rect::right(body)
		- st::msgPadding.right()
		+ st::msgDateDelta.x();
	const auto bottom = rect::bottom(body)
		- st::msgPadding.bottom()
		+ st::msgDateDelta.y();
	return style::rtlrect(
		right - width,
		bottom - height,
		width,
		height,
		2 * body.x() + body.width());
}

[[nodiscard]] MorphLayout ComputeMorphLayout(
		QSize source,
		QRect text,
		QRect bubble,
		QRect body,
		int infoWidth,
		bool rightToLeft) {
	const auto radius = BubbleRadiusLarge();
	const auto &padding = st::msgPadding;
	const auto infoLeft = style::RightToLeft();
	const auto info = InfoGeometry(body, infoWidth);
	const auto columnsLimit = info.isEmpty()
		? (infoLeft ? (body.x() + radius) : (rect::right(body) - radius))
		: (infoLeft ? rect::right(info) : info.x());
	const auto columnsFrom = text.x() + std::max(
		infoLeft ? infoWidth : 0,
		radius - padding.left());
	const auto columnsTill = rect::right(text) - std::max(
		infoLeft ? 0 : infoWidth,
		radius - padding.right());
	const auto columns = infoLeft
		? ComputeCut(
			std::max(columnsFrom, columnsLimit),
			columnsTill,
			columnsLimit)
		: ComputeCut(
			columnsFrom,
			std::min(columnsTill, columnsLimit),
			columnsLimit - 1);
	const auto rowsLimit = info.isEmpty()
		? (rect::bottom(body) - radius)
		: std::min(info.y(), rect::bottom(body) - radius);
	const auto rows = ComputeCut(
		text.y() + std::max(0, radius - padding.top()),
		rowsLimit,
		rowsLimit - 1);
	const auto edgeRow = rows.allowance
		? rows.position
		: std::min(
			std::max(body.y() + radius, text.y()),
			rect::bottom(body) - radius - 1);
	const auto dw = std::max(
		source.width() - text.width(),
		-columns.allowance);
	const auto dh = std::max(
		source.height() - text.height(),
		-rows.allowance);
	const auto width = bubble.width() + dw;
	const auto height = bubble.height() + dh;
	const auto sourceLeft = rightToLeft
		? (rect::right(text) - source.width())
		: text.x();
	const auto fromLeft = rightToLeft
		? (rect::right(bubble) - width)
		: bubble.x();
	return {
		.source = QRect(
			QPoint(sourceLeft, rect::bottom(text) - source.height()),
			source),
		.from = QRect(fromLeft, rect::bottom(bubble) - height, width, height),
		.cut = QPoint(columns.position, rows.position),
		.info = info,
		.edgeRow = edgeRow,
	};
}

[[nodiscard]] std::array<ResizeSpan, 3> ResizeSpans(
		int size,
		int target,
		int cut) {
	if (target == size) {
		return { { { 0, size, 0, size } } };
	} else if (target > size) {
		const auto at = std::clamp(cut, 0, size);
		const auto growth = target - size;
		return { {
			{ 0, at, 0, at },
			{ std::min(at, size - 1), 1, at, growth },
			{ at, size - at, at + growth, size - at },
		} };
	}
	const auto crop = size - target;
	const auto start = std::clamp(cut - crop / 2, 0, size - crop);
	const auto kept = size - start - crop;
	return { {
		{ 0, start, 0, start },
		{ start + crop, kept, start, kept },
	} };
}

[[nodiscard]] std::array<ResizeSpan, 3> SplitSpan(
		const ResizeSpan &span,
		int from,
		int till) {
	if (span.sourceSize != span.targetSize) {
		const auto inside = (span.source >= from) && (span.source < till);
		return inside
			? std::array<ResizeSpan, 3>{ { {}, span, {} } }
			: std::array<ResizeSpan, 3>{ { span, {}, {} } };
	}
	const auto shift = span.target - span.source;
	const auto part = [&](int start, int end) {
		return ResizeSpan{ start, end - start, start + shift, end - start };
	};
	const auto end = span.source + span.sourceSize;
	const auto first = std::clamp(from, span.source, end);
	const auto last = std::clamp(till, first, end);
	return { {
		part(span.source, first),
		part(first, last),
		part(last, end),
	} };
}

void PaintSpans(
		QPainter &p,
		const QImage &layer,
		QPoint origin,
		const ResizeSpan &column,
		const std::array<ResizeSpan, 3> &rows) {
	if (!column.targetSize) {
		return;
	}
	const auto ratio = int(layer.devicePixelRatio());
	for (const auto &row : rows) {
		if (!row.targetSize) {
			continue;
		}
		p.drawImage(
			QRect(
				origin.x() + column.target,
				origin.y() + row.target,
				column.targetSize,
				row.targetSize),
			layer,
			QRect(
				column.source * ratio,
				row.source * ratio,
				column.sourceSize * ratio,
				row.sourceSize * ratio));
	}
}

void PaintResized(
		QPainter &p,
		const QImage &layer,
		const MessageSendingTextFlight::Morph &morph,
		QRect target) {
	const auto &source = morph.bubble;
	const auto origin = target.topLeft();
	const auto rowsAt = [&](int cut) {
		return ResizeSpans(
			source.height(),
			target.height(),
			cut - source.y());
	};
	const auto rows = rowsAt(morph.cut.y());
	const auto columns = ResizeSpans(
		source.width(),
		target.width(),
		morph.cut.x() - source.x());
	if (target.height() <= source.height()
		|| morph.edgeRow == morph.cut.y()) {
		for (const auto &column : columns) {
			PaintSpans(p, layer, origin, column, rows);
		}
		return;
	}
	const auto edgeRows = rowsAt(morph.edgeRow);
	const auto infoFrom = morph.info.x() - source.x();
	const auto infoTill = rect::right(morph.info) - source.x();
	for (const auto &column : columns) {
		const auto parts = SplitSpan(column, infoFrom, infoTill);
		PaintSpans(p, layer, origin, parts[0], edgeRows);
		PaintSpans(p, layer, origin, parts[1], rows);
		PaintSpans(p, layer, origin, parts[2], edgeRows);
	}
}

[[nodiscard]] auto GrabSendingFieldText(
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
	const auto &font = field->st().style.font;
	const auto hidden = field->scrollTopMax();
	const auto top = field->scrollTop().current();
	if (hidden > kMorphHiddenLines * font->height
		|| top > hidden
		|| top + font->descent < hidden
		|| edit->horizontalScrollBar()->value() != 0) {
		return std::nullopt;
	}
	auto image = GrabWithoutCaret(edit);
	if (top > 0) {
		ApplyTopFade(image, field);
	}
	auto ink = CropToInk(image);
	const auto invisible = QRegion(ink.geometry).subtracted(
		edit->visibleRegion());
	if (ink.geometry.isEmpty() || !invisible.isEmpty()) {
		return std::nullopt;
	}
	const auto document = field->document();
	return MessageSendingAnimationFrom::FieldText{
		.frame = std::move(ink.frame),
		.globalGeometry = QRect(
			edit->mapToGlobal(ink.geometry.topLeft()),
			ink.geometry.size()),
		.capturedAt = crl::now(),
		.singleLine = HasSingleVisualLine(document),
		.scrolled = (hidden > 0),
		.rightToLeft = LastBlockRightToLeft(document),
	};
}

} // namespace

std::optional<MessageSendingAnimationFrom> PrepareTextSending(
		not_null<InputField*> field,
		const TextWithTags &sending,
		MsgId localId,
		bool scheduled) {
	auto fieldText = scheduled
		? std::nullopt
		: GrabSendingFieldText(field, sending);
	if (!fieldText
		&& ((field->document()->size().height() > field->height())
			|| (!scheduled && field->scrollTopMax() > 0))) {
		return std::nullopt;
	}
	return MessageSendingAnimationFrom{
		.type = MessageSendingAnimationFrom::Type::Text,
		.localId = localId,
		.globalStartGeometry = field->mapToGlobal(Rect(field->size())),
		.fieldText = std::move(fieldText),
	};
}

base::unique_qptr<RpWidget> ShowSendingFieldText(
		not_null<RpWidget*> parent,
		const MessageSendingAnimationFrom::FieldText &source) {
	auto result = base::make_unique_q<RpWidget>(parent);
	const auto raw = result.get();
	raw->setAttribute(Qt::WA_TransparentForMouseEvents);
	raw->setGeometry(QRect(
		parent->mapFromGlobal(source.globalGeometry.topLeft()),
		source.globalGeometry.size()));
	raw->paintRequest() | rpl::on_next([=, frame = source.frame] {
		auto p = QPainter(raw);
		p.drawImage(0, 0, frame);
	}, raw->lifetime());
	const auto giveUp = raw->lifetime().make_state<base::Timer>([=] {
		raw->hide();
	});
	const auto left = StartDeadline(source.capturedAt) - crl::now();
	giveUp->callOnce(std::max(left, crl::time()), Qt::PreciseTimer);
	raw->show();
	raw->raise();
	return result;
}

auto MessageSendingTextFlight::Prepare(
		const FieldText &source,
		const SendingInfoTo &to)
-> Preparation {
	const auto fallback = source.scrolled
		? Preparation(Refused())
		: Preparation(v::null);
	const auto view = to.view();
	const auto textItem = view ? view->textItem() : nullptr;
	if (!textItem
		|| source.frame.isNull()
		|| source.globalGeometry.isEmpty()
		|| (crl::now() > StartDeadline(source.capturedAt))
		|| !view->hasBubble()
		|| !view->hasVisibleText()
		|| view->media()
		|| textItem->translatedRichPage()
		|| view->isolatedEmoji()
		|| view->onlyCustomEmoji()
		|| view->width() <= 0
		|| view->height() <= 0) {
		return fallback;
	} else if (!source.singleLine
		&& TooTallToMorph(view->innerGeometry().height())) {
		return Refused();
	}
	const auto context = to.paintContext();
	const auto span = BubbleSpan(view, context);
	const auto layer = RenderSpan(
		view,
		context,
		SkipDrawingParts::Bubble,
		span);
	const auto text = InkGeometry(layer).translated(span.topLeft());
	const auto bubble = MeasureLayer(
		view,
		context,
		SkipDrawingParts::Content,
		span);
	if (text.isEmpty() || bubble.isEmpty()) {
		return fallback;
	}
	const auto size = source.globalGeometry.size();
	const auto line = st::messageTextStyle.font->height;
	auto result = Prepared{
		.source = source,
		.sourceRect = QRect(text.topLeft(), size),
		.textRect = text,
		.bubbleRect = bubble,
		.viewSize = QSize(view->width(), view->height()),
	};
	if (source.singleLine && (text.height() < size.height() + line / 2)) {
		return result;
	} else if (TooTallToMorph(bubble.height())) {
		return Refused();
	}
	auto body = view->innerGeometry();
	body.setTop(view->marginTop());
	const auto layout = ComputeMorphLayout(
		size,
		text,
		bubble,
		body,
		view->infoWidth(),
		source.rightToLeft);
	result.sourceRect = layout.source;
	result.morph = Morph{
		.text = CropImage(layer, text.translated(-span.topLeft())),
		.bubble = bubble,
		.from = layout.from,
		.cut = layout.cut,
		.info = layout.info,
		.edgeRow = layout.edgeRow,
	};
	return result;
}

MessageSendingTextFlight::MessageSendingTextFlight(
	not_null<RpWidget*> parent,
	Prepared &&prepared,
	SendingInfoTo &&to)
: RpWidget(parent)
, _toInfo(std::move(to))
, _source(std::move(prepared.source.frame))
, _sourceRect(prepared.sourceRect)
, _textRect(prepared.textRect)
, _bounds(prepared.bubbleRect
	.united(_sourceRect)
	.united(_textRect)
	.united(prepared.morph ? prepared.morph->from : QRect()))
, _viewSize(prepared.viewSize)
, _morph(std::move(prepared.morph))
, _start(parent->mapFromGlobal(prepared.source.globalGeometry.topLeft())
	- _sourceRect.topLeft())
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

MessageSendingReveal MessageSendingTextFlight::reveal() const {
	const auto lead = _morph ? (_morph->from.y() - _morph->bubble.y()) : 0;
	return {
		.duration = kDuration,
		.height = [=](int height, int lift, float64 progress) {
			return RevealHeight(height, lift, lead, progress);
		},
	};
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
	const auto context = _toInfo.paintContext();
	const auto span = BubbleSpan(view, context);
	if (_morph) {
		_morph->text = QImage();
		const auto text = MeasureLayer(
			view,
			context,
			SkipDrawingParts::Bubble,
			span);
		if (!text.isEmpty()) {
			_textRect = text;
		}
	}
	const auto bubble = MeasureLayer(
		view,
		context,
		SkipDrawingParts::Content,
		span);
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
	} else if (_morph && !_morph->text.isNull()) {
		paintMorph(p, view, context, mix);
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
		QPoint(),
		text.size(),
		mix));
}

void MessageSendingTextFlight::paintMorph(
		QPainter &p,
		not_null<HistoryView::Element*> view,
		const ChatPaintContext &context,
		float64 progress) const {
	const auto &morph = *_morph;
	const auto edge = [&](int from, int to) {
		return anim::interpolate(from, to, progress);
	};
	const auto left = edge(morph.from.x(), morph.bubble.x());
	const auto top = edge(morph.from.y(), morph.bubble.y());
	const auto box = QRect(
		left,
		top,
		edge(rect::right(morph.from), rect::right(morph.bubble)) - left,
		edge(rect::bottom(morph.from), rect::bottom(morph.bubble)) - top);
	p.setOpacity(progress);
	PaintResized(
		p,
		RenderLayer(view, context, SkipDrawingParts::Content, morph.bubble),
		morph,
		box);
	p.setOpacity(1.);
	const auto text = _sourceRect.united(_textRect);
	p.setClipRect(box);
	p.drawImage(text.topLeft(), MixText(
		_source,
		_sourceRect.topLeft() - text.topLeft(),
		morph.text,
		_textRect.topLeft() - text.topLeft(),
		text.size(),
		progress));
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
