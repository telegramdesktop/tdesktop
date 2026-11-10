/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_html_widget.h"

#include "data/data_message_reaction_id.h"
#include "data/data_session.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "tgw/tgw_renderer.h"
#include "tgw/tgw_runtime.h"
#include "tgw/tgw_scroll.h"
#include "ui/chat/chat_style.h"
#include "ui/style/style_core_direction.h"
#include "ui/style/style_core_scale.h"
#include "ui/cached_round_corners.h"
#include "ui/painter.h"

#include "styles/style_chat.h"
#include "styles/style_tgw.h"

#include <QtGui/QGuiApplication>

namespace HistoryView {
namespace {

constexpr auto kCssPercent = 100.;
constexpr auto kMaxSafeAreaInset = 64;
constexpr auto kMaxSafeAreaCorner = 200;

[[nodiscard]] int CssCeil(int logical) {
	return int(std::ceil(Tgw::CssFromLogical(logical)));
}

[[nodiscard]] InfoDisplayType CornerInfoType() {
	return style::RightToLeft()
		? InfoDisplayType::ImageLeftToRight
		: InfoDisplayType::Image;
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
	Tgw::MessageContext context,
	Element *replacing)
: Media(parent)
, _anchors(anchors)
, _cardLink(std::make_shared<LambdaClickHandler>([] {})) {
	auto descriptor = Tgw::RuntimeDescriptor{
		.view = parent,
		.item = realParent,
		.html = std::move(html),
		.context = context,
		.resize = [=] { runtimeHeightChanged(); },
		.repaint = [=] { _parent->repaint(); },
		.heavyChanged = [=] { runtimeHeavyChanged(); },
	};
	const auto previous = (replacing && replacing->data() == realParent)
		? dynamic_cast<HtmlWidget*>(replacing->media())
		: nullptr;
	const auto heavy = previous && previous->_heavy;
	if (previous) {
		_placed = previous->handoffGeometry();
		_placedReplaced = _placed.has_value();
		_runtime = previous->takeRuntime(descriptor.html, descriptor.context);
		if (!_runtime && _placed) {
			_placed->serial = 0;
		}
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
		const QByteArray &html,
		const Tgw::MessageContext &context) {
	if (!_runtime || !_runtime->matches(html, context)) {
		return nullptr;
	}
	_heavy = false;
	_cardHovered = false;
	return base::take(_runtime);
}

std::optional<HtmlWidgetGeometry> HtmlWidget::handoffGeometry() const {
	auto result = placement();
	if (!result) {
		return currentGeometry();
	} else if (!placedTopValid()) {
		result->y = rowTop();
	}
	return result;
}

std::optional<HtmlWidgetGeometry> HtmlWidget::currentGeometry() const {
	const auto consumed = _runtime ? _runtime->heightLayout() : std::nullopt;
	if (!consumed || _parent->height() <= 0) {
		return std::nullopt;
	}
	return HtmlWidgetGeometry{
		.bubble = _parent->innerGeometry(),
		.y = rowTop(),
		.height = _parent->height(),
		.card = consumed->height,
		.serial = consumed->serial,
	};
}

std::optional<HtmlWidgetGeometry> HtmlWidget::placement() const {
	if (!_placed) {
		return std::nullopt;
	} else if (_placedEpoch) {
		return (_placedEpoch == ownerEpoch()) ? _placed : std::nullopt;
	}
	const auto kept = _placedReplaced
		|| (_placed->height == _parent->height());
	return kept ? _placed : std::nullopt;
}

bool HtmlWidget::placedTopValid() const {
	// Only history blocks move rows before their list restores the scroll.
	return _placedReplaced
		|| (_parent->block() && (_placedAt == ownerEpoch()));
}

uint32 HtmlWidget::ownerEpoch() const {
	return Tgw::ListScroll::Epoch(_parent->delegate());
}

int HtmlWidget::rowTop() const {
	const auto block = _parent->block();
	return _parent->y() + (block ? block->y() : 0);
}

std::optional<HtmlWidgetLayout> HtmlWidget::heightLayout() const {
	if (_anchors.height > 0 || !_runtime) {
		return std::nullopt;
	}
	const auto placed = placement();
	const auto moved = (placed && placedTopValid())
		? (rowTop() - placed->y)
		: 0;
	return HtmlWidgetLayout{
		.consumed = _runtime->heightLayout(),
		.runtime = base::make_weak(_runtime.get()),
		.placed = placed ? placed : currentGeometry(),
		.moved = moved,
	};
}

void HtmlWidget::rememberPlacement() const {
	if (_anchors.height > 0 || _parent->pendingResize()) {
		return;
	} else if (const auto now = currentGeometry()) {
		_placed = now;
		_placedEpoch = 0;
		_placedAt = ownerEpoch();
		_placedReplaced = false;
	}
}

QSize HtmlWidget::countOptimalSize() {
	return { optimalCardWidth(), cardHeight() + infoHeight() };
}

QSize HtmlWidget::countCurrentSize(int newWidth) {
	const auto cardWidth = std::min(newWidth, maxWidth());
	// customInfoLayout() needs it below, before Object stores the width.
	_cardWidth = cardWidth;
	if (!_runtime) {
		return { cardWidth, cardHeight() + infoHeight() };
	}
	const auto consumed = _runtime->heightLayout();
	const auto previous = consumed
		? std::make_optional(consumed->height)
		: (_placedReplaced && _placed)
		? std::make_optional(_placed->card)
		: std::nullopt;
	_runtime->setSize(cardWidth, maxCssHeight());
	_runtime->setSafeArea(contentSafeArea());
	const auto card = cardHeight();
	_runtime->laidOut(card);
	if (_anchors.height <= 0 && previous && *previous != card) {
		Tgw::ListScroll::Consume(_parent->delegate());
	}
	if (!_placedEpoch) {
		// The shown geometry stays the old one until a list positions this.
		_placedEpoch = ownerEpoch();
	}
	return { cardWidth, card + infoHeight() };
}

bool HtmlWidget::needsBubble() const {
	return true;
}

bool HtmlWidget::customInfoLayout() const {
	if ((_parent->media() != this)
		|| _parent->hidesBottomInfo()
		|| _parent->hasCommentsButton()
		|| ((_parent->context() != Context::MediaEditor)
			&& !_parent->data()->reactionsWithLocal().empty())) {
		return false;
	}
	const auto corner = cssCornerSize();
	const auto infoSkip = st::msgPadding.left()
		+ st::msgPadding.right()
		+ 2 * st::msgDateDelta.x();
	const auto cardWidth = _cardWidth ? _cardWidth : optimalCardWidth();
	return (corner.width() <= kMaxSafeAreaCorner)
		&& (corner.height() <= kMaxSafeAreaInset)
		&& (_parent->infoWidth() + infoSkip <= cardWidth);
}

QPoint HtmlWidget::resolveCustomInfoRightBottom() const {
	const auto skipx = st::msgDateImgDelta + st::msgDateImgPadding.x();
	const auto skipy = st::msgDateImgDelta + st::msgDateImgPadding.y();
	return QPoint(infoRight() - skipx, height() - skipy);
}

bool HtmlWidget::enforceBubbleWidth() const {
	return true;
}

QRect HtmlWidget::cardRect() const {
	return QRect(0, 0, width(), height() - infoHeight());
}

int HtmlWidget::optimalCardWidth() const {
	return (_anchors.width > 0)
		? std::min(
			Tgw::LogicalWidth(std::clamp(
				double(_anchors.width),
				Tgw::kMinWidth,
				Tgw::CssFromLogical(st::msgMaxWidth))),
			st::msgMaxWidth)
		: st::msgMaxWidth;
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
		|| _parent->hidesBottomInfo()
		|| customInfoLayout()) {
		return 0;
	}
	return st::tgwBubbleInfoSkip
		+ _parent->skipBlockHeight()
		+ st::msgPadding.bottom();
}

bool HtmlWidget::timeOverCorner() const {
	return isBubbleBottom() && customInfoLayout();
}

QSize HtmlWidget::infoBackingSize() const {
	const auto padding = st::msgDateImgPadding;
	return QSize(
		_parent->infoWidth() + 2 * padding.x(),
		st::msgDateFont->height + 2 * padding.y());
}

int HtmlWidget::infoRight() const {
	return style::RightToLeft()
		? (infoBackingSize().width() + 2 * st::msgDateImgDelta)
		: width();
}

QRect HtmlWidget::infoBacking() const {
	const auto size = infoBackingSize();
	const auto skip = st::msgDateImgDelta;
	return QRect(
		QPoint(
			infoRight() - skip - size.width(),
			height() - skip - size.height()),
		size);
}

bool HtmlWidget::infoContains(QPoint point) const {
	return infoBacking().contains(point) && timeOverCorner();
}

QSize HtmlWidget::cssCornerSize() const {
	const auto backing = infoBackingSize();
	const auto skip = st::msgDateImgDelta;
	return QSize(
		CssCeil(backing.width() + skip),
		CssCeil(backing.height() + skip));
}

Tgw::ContentSafeArea HtmlWidget::contentSafeArea() const {
	if (!timeOverCorner()) {
		return {};
	}
	const auto corner = cssCornerSize();
	const auto rtl = style::RightToLeft();
	return {
		.bottom = corner.height(),
		.cornerLeft = rtl ? corner.width() : 0,
		.cornerRight = rtl ? 0 : corner.width(),
	};
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

Tgw::RendererCore *HtmlWidget::touchCore(QPoint point) const {
	return (cardRect().contains(point) && !infoContains(point))
		? pointerCore()
		: nullptr;
}

QPointF HtmlWidget::gesturePoint(QPoint point) const {
	const auto css = cssPoint(point);
	if (!style::RightToLeft()) {
		return css;
	}
	const auto width = cardRect().width() * (kCssPercent / style::Scale());
	return QPointF(width - css.x(), css.y());
}

void HtmlWidget::draw(Painter &p, const PaintContext &context) const {
	const auto card = cardRect();
	if (card.isEmpty() || !_runtime) {
		return;
	}
	rememberPlacement();
	const auto themeKey = Tgw::BubbleThemeKey(context);
	if (_themeKey != themeKey) {
		_themeKey = themeKey;
		_runtime->setTheme(Tgw::BubbleTheme(context));
	}
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
			card.size(),
			(_anchors.height <= 0));
		if (!frame.isNull()) {
			p.save();
			p.setClipRect(card, Qt::IntersectClip);
			p.drawImage(card.topLeft(), frame);
			p.restore();
		}
	}
	if (visible) {
		_runtime->painted();
	}
	const auto state = _runtime->state();
	if (state == Tgw::Runtime::State::Stopped
		|| state == Tgw::Runtime::State::Failed) {
		drawStopped(p, context, card);
	}
	if (timeOverCorner()) {
		_parent->drawInfo(
			p,
			context,
			infoRight(),
			height(),
			width(),
			CornerInfoType());
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
	} else if (infoContains(point)) {
		const auto info = _parent->bottomInfoTextState(
			infoRight(),
			height(),
			point,
			CornerInfoType());
		const auto meaningful = info.link
			|| (info.cursor != CursorState::None)
			|| info.customTooltip;
		return meaningful ? info : result;
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

bool HtmlWidget::dragItemByHandler(const ClickHandlerPtr &p) const {
	return false;
}

bool HtmlWidget::keepFocusByHandler(const ClickHandlerPtr &p) const {
	return (p == _cardLink);
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
	if (const auto core = pointerCore()) {
		core->pointerPress(cssPoint(_pointerPoint), CurrentModifiers());
	}
}

void HtmlWidget::releaseCard() {
	if (const auto core = interactiveCore()) {
		core->pointerRelease(cssPoint(_pointerPoint), CurrentModifiers());
		if (!_cardHovered) {
			core->pointerLeave();
		}
	}
}

Tgw::RendererCore *HtmlWidget::menuPress(const ClickHandlerPtr &link) {
	if (link != _cardLink || ClickHandler::getPressed()) {
		return nullptr;
	}
	const auto core = pointerCore();
	const auto taken = core
		&& core->menuPress(cssPoint(_pointerPoint), CurrentModifiers());
	return taken ? core : nullptr;
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
