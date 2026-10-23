/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_html_widget.h"

#include "data/data_session.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "tgw/tgw_renderer.h"
#include "tgw/tgw_runtime.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/style/style_core_scale.h"
#include "ui/cached_round_corners.h"
#include "ui/painter.h"

#include "styles/style_chat.h"
#include "styles/style_tgw.h"

#include <QtGui/QGuiApplication>

namespace HistoryView {
namespace {

constexpr auto kPatternColors = 4;
constexpr auto kCssPercent = 100.;

struct PatternColor {
	qint64 key = 0;
	QColor color;
};

[[nodiscard]] QColor PatternAverageColor(const QPixmap &pattern) {
	static auto cache = std::array<PatternColor, kPatternColors>();
	const auto key = pattern.cacheKey();
	for (const auto &entry : cache) {
		if (entry.key == key) {
			return entry.color;
		}
	}
	std::rotate(begin(cache), end(cache) - 1, end(cache));
	cache.front() = {
		.key = key,
		.color = Ui::CountAverageColor(
			pattern.toImage().convertToFormat(
				QImage::Format_ARGB32_Premultiplied)),
	};
	return cache.front().color;
}

[[nodiscard]] QColor BubbleColor(const PaintContext &context) {
	const auto pattern = context.bubblesPattern;
	if (!context.outbg
		|| !pattern
		|| context.viewport.isEmpty()
		|| pattern->pixmap.size().isEmpty()) {
		return context.st->messageStyle(context.outbg, false).msgBg->c;
	}
	return PatternAverageColor(pattern->pixmap);
}

[[nodiscard]] QWidget *PaintWidget(const QPainter &p) {
	const auto device = p.device();
	return (device && device->devType() == QInternal::Widget)
		? static_cast<QWidget*>(device)
		: nullptr;
}

[[nodiscard]] int CurrentModifiers() {
	return Tgw::DomModifiers(QGuiApplication::keyboardModifiers());
}

[[nodiscard]] bool OtherLinkPressed(const ClickHandlerPtr &link) {
	const auto pressed = ClickHandler::getPressed();
	return pressed && (pressed != link);
}

} // namespace

HtmlWidget::HtmlWidget(
	not_null<Element*> parent,
	not_null<HistoryItem*> realParent,
	Tgw::Anchors anchors,
	QByteArray html,
	Element *replacing)
: Media(parent)
, _anchors(anchors)
, _cardLink(std::make_shared<LambdaClickHandler>([] {})) {
	auto descriptor = Tgw::RuntimeDescriptor{
		.view = parent,
		.item = realParent,
		.html = std::move(html),
		.resize = [=] { runtimeHeightChanged(); },
		.repaint = [=] { _parent->repaint(); },
		.heavyChanged = [=] { runtimeHeavyChanged(); },
	};
	const auto previous = (replacing && replacing->data() == realParent)
		? dynamic_cast<HtmlWidget*>(replacing->media())
		: nullptr;
	const auto heavy = previous && previous->_heavy;
	if (previous) {
		_runtime = previous->takeRuntime(descriptor.html);
	}
	if (_runtime) {
		_heavy = heavy;
		_runtime->attachView(
			parent,
			std::move(descriptor.resize),
			std::move(descriptor.repaint),
			std::move(descriptor.heavyChanged));
		replacing->checkHeavyPart();
	} else {
		_runtime = std::make_unique<Tgw::Runtime>(std::move(descriptor));
	}
	if (_heavy || _runtime->heavy()) {
		history()->owner().registerHeavyViewPart(_parent);
	}
}

HtmlWidget::~HtmlWidget() {
	const auto heavy = hasHeavyPart();
	_runtime = nullptr;
	if (heavy) {
		_parent->checkHeavyPart();
	}
}

std::unique_ptr<Tgw::Runtime> HtmlWidget::takeRuntime(
		const QByteArray &html) {
	if (!_runtime || _runtime->html() != html) {
		return nullptr;
	}
	_heavy = false;
	_cardHovered = false;
	_cardClaimed = false;
	return base::take(_runtime);
}

QSize HtmlWidget::countOptimalSize() {
	const auto cardWidth = (_anchors.width > 0)
		? std::min(
			Tgw::LogicalWidth(std::clamp(
				double(_anchors.width),
				Tgw::kMinWidth,
				Tgw::CssFromLogical(st::msgMaxWidth))),
			st::msgMaxWidth)
		: st::msgMaxWidth;
	return { cardWidth, cardHeight() + infoHeight() };
}

QSize HtmlWidget::countCurrentSize(int newWidth) {
	const auto cardWidth = std::min(newWidth, maxWidth());
	if (_runtime) {
		_runtime->setSize(cardWidth, maxCssHeight());
	}
	return { cardWidth, cardHeight() + infoHeight() };
}

bool HtmlWidget::needsBubble() const {
	return true;
}

bool HtmlWidget::customInfoLayout() const {
	return false;
}

bool HtmlWidget::enforceBubbleWidth() const {
	return true;
}

QRect HtmlWidget::cardRect() const {
	return QRect(0, 0, width(), height() - infoHeight());
}

int HtmlWidget::cardHeight() const {
	if (_anchors.height > 0) {
		return Tgw::LogicalHeight(maxCssHeight());
	}
	const auto shown = _runtime ? _runtime->height() : std::nullopt;
	return shown.value_or(st::tgwPlaceholderHeight);
}

double HtmlWidget::maxCssHeight() const {
	return (_anchors.height > 0)
		? std::clamp(
			double(_anchors.height),
			Tgw::kMinHeight,
			Tgw::kMaxHeight)
		: Tgw::kMaxHeight;
}

int HtmlWidget::infoHeight() const {
	if (!isBubbleBottom()
		|| (_parent->media() != this)
		|| _parent->hidesBottomInfo()) {
		return 0;
	}
	return st::tgwBubbleInfoSkip
		+ _parent->skipBlockHeight()
		+ st::msgPadding.bottom();
}

Tgw::RendererCore *HtmlWidget::interactiveCore() const {
	return _runtime ? _runtime->interactive() : nullptr;
}

Tgw::RendererCore *HtmlWidget::pointerCore() const {
	const auto selection = _parent->delegate()->elementInSelectionMode(
		_parent);
	return selection.inSelectionMode ? nullptr : interactiveCore();
}

QPointF HtmlWidget::cssPoint(QPoint point) const {
	const auto local = point - cardRect().topLeft();
	return QPointF(local) * (kCssPercent / style::Scale());
}

void HtmlWidget::draw(Painter &p, const PaintContext &context) const {
	const auto card = cardRect();
	if (card.isEmpty() || !_runtime) {
		return;
	}
	_runtime->setBubbleColor(BubbleColor(context));
	if (const auto widget = PaintWidget(p)) {
		_runtime->setWindow(widget->window());
	}
	if (!_heavy) {
		_heavy = true;
		history()->owner().registerHeavyViewPart(_parent);
	}

	const auto visible = card.intersects(context.clip);
	if (const auto core = visible ? _runtime->core() : nullptr) {
		const auto square = infoHeight()
			? RectParts(RectPart::FullBottom)
			: RectParts();
		const auto &frame = core->frame(
			MediaRoundingMask(adjustedBubbleRounding(square)),
			card.size());
		if (!frame.isNull()) {
			p.save();
			p.setClipRect(card, Qt::IntersectClip);
			p.drawImage(card.topLeft(), frame);
			p.restore();
		}
	}
	const auto state = _runtime->state();
	if (state == Tgw::Runtime::State::Stopped
		|| state == Tgw::Runtime::State::Failed) {
		drawStopped(p, context, card);
	}
	if (visible) {
		_runtime->painted();
	}
}

void HtmlWidget::drawStopped(
		Painter &p,
		const PaintContext &context,
		QRect card) const {
	const auto &font = st::msgDateFont;
	const auto padding = st::msgDateImgPadding;
	const auto skip = st::msgDateImgDelta;
	const auto pillHeight = font->height + 2 * padding.y();
	const auto top = card.y() + (card.height() - pillHeight) / 2;
	const auto left = card.x() + skip;
	const auto right = card.x() + card.width() - skip;
	const auto available = right - left - 2 * padding.x();
	if (available <= 0) {
		return;
	}
	const auto label = font->elided(tr::lng_tgw_stopped(tr::now), available);
	if (label.isEmpty()) {
		return;
	}
	const auto labelWidth = std::min(font->width(label), available);
	const auto pillWidth = labelWidth + 2 * padding.x();
	const auto pill = QRect(
		left + (right - left - pillWidth) / 2,
		top,
		pillWidth,
		pillHeight);
	const auto sti = context.imageStyle();
	p.save();
	p.setClipRect(card, Qt::IntersectClip);
	Ui::FillRoundRect(p, pill, sti->msgDateImgBg, sti->msgDateImgBgCorners);
	p.setFont(font);
	p.setPen(context.st->msgDateImgFg());
	p.drawText(
		pill.x() + padding.x(),
		pill.y() + padding.y() + font->ascent,
		label);
	p.restore();
}

TextState HtmlWidget::textState(
		QPoint point,
		StateRequest request) const {
	auto result = TextState(_parent);
	if (!cardRect().contains(point)) {
		return result;
	} else if (const auto core = pointerCore()) {
		using Flag = Ui::Text::StateRequest::Flag;
		const auto hand = (request.flags & Flag::LookupLink)
			&& !OtherLinkPressed(_cardLink)
			&& core->handCursorAt(cssPoint(point));
		result.link = _cardLink;
		result.cursor = hand ? CursorState::None : CursorState::Default;
	}
	return result;
}

void HtmlWidget::updatePressed(QPoint point) {
	_pointerPoint = point;
	const auto core = interactiveCore();
	if (!core || OtherLinkPressed(_cardLink)) {
		return;
	} else if (core->pressed() || (_cardHovered && pointerCore())) {
		core->pointerMove(cssPoint(point), CurrentModifiers());
	}
}

bool HtmlWidget::toggleSelectionByHandlerClick(
		const ClickHandlerPtr &p) const {
	return (p == _cardLink);
}

bool HtmlWidget::allowTextSelectionByHandler(
		const ClickHandlerPtr &p,
		bool fromTouch) const {
	return fromTouch && (p == _cardLink) && !_cardClaimed;
}

bool HtmlWidget::dragItemByHandler(const ClickHandlerPtr &p) const {
	return false;
}

void HtmlWidget::clickHandlerActiveChanged(
		const ClickHandlerPtr &p,
		bool active) {
	if (p != _cardLink) {
		return;
	}
	_cardHovered = active;
	if (!active) {
		if (const auto core = interactiveCore()) {
			core->pointerLeave();
		}
	} else if (const auto core = pointerCore()) {
		core->pointerEnter();
	}
}

void HtmlWidget::clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) {
	if (p != _cardLink) {
		return;
	} else if (pressed) {
		pressCard();
	} else {
		releaseCard();
	}
}

void HtmlWidget::pressCard() {
	const auto core = pointerCore();
	if (!core) {
		return;
	}
	core->pointerPress(cssPoint(_pointerPoint), CurrentModifiers());
	if (core->claimed()) {
		_cardClaimed = true;
	}
}

void HtmlWidget::releaseCard() {
	if (const auto core = interactiveCore()) {
		core->pointerRelease(cssPoint(_pointerPoint), CurrentModifiers());
		if (!_cardHovered) {
			core->pointerLeave();
		}
	}
	_cardClaimed = false;
}

bool HtmlWidget::hasHeavyPart() const {
	return _heavy || (_runtime && _runtime->heavy());
}

void HtmlWidget::unloadHeavyPart() {
	_heavy = false;
	if (_runtime) {
		_runtime->unload();
	}
}

void HtmlWidget::runtimeHeightChanged() {
	if (_anchors.height > 0) {
		_parent->repaint();
	} else {
		history()->owner().requestViewResize(_parent);
	}
}

void HtmlWidget::runtimeHeavyChanged() {
	if (_runtime && _runtime->heavy()) {
		history()->owner().registerHeavyViewPart(_parent);
	} else {
		_parent->checkHeavyPart();
	}
}

} // namespace HistoryView
