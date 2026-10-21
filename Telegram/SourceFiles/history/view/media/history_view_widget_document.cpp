/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_widget_document.h"

#include "core/click_handler_types.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_click_handler.h"
#include "data/data_session.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_element.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "lang/lang_keys.h"
#include "tgw/tgw_document.h"
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

WidgetDocument::WidgetDocument(
	not_null<Element*> parent,
	not_null<HistoryItem*> realParent,
	not_null<DocumentData*> document)
: File(parent, realParent)
, _data(document)
, _runtime(std::make_unique<Tgw::Runtime>(Tgw::RuntimeDescriptor{
	.view = parent,
	.item = realParent,
	.document = document,
	.resize = [=] { _data->owner().requestViewResize(_parent); },
	.repaint = [=] { _parent->repaint(); },
	.heavyChanged = [=] { runtimeHeavyChanged(); },
}))
, _cardLink(std::make_shared<LambdaClickHandler>([] {}))
, _bare(Tgw::IsBareWidget(document))
, _hideReply(parent->delegate()->elementHideReply(parent)
	|| parent->data()->Has<HistoryMessageSuggestion>()) {
	setDocumentLinks(_data, realParent);
	setCardClaimed(false);

	// Adopts bytes the replaced file view may hold only in memory.
	_dataMedia = _data->createMediaView();
	_data->owner().registerHeavyViewPart(_parent);
}

WidgetDocument::~WidgetDocument() {
	setCardClaimed(false);
	const auto heavy = hasHeavyPart();
	_runtime = nullptr;
	if (_dataMedia) {
		_data->owner().keepAlive(base::take(_dataMedia));
	}
	if (heavy) {
		_parent->checkHeavyPart();
	}
}

QSize WidgetDocument::countOptimalSize() {
	return { st::msgMaxWidth, countHeight() };
}

QSize WidgetDocument::countCurrentSize(int newWidth) {
	const auto margins = cardMargins();
	_runtime->setWidth(std::max(
		newWidth - margins.left() - margins.right(),
		0));
	return { newWidth, countHeight() };
}

bool WidgetDocument::needsBubble() const {
	return !_bare || bareKeepsBubble();
}

bool WidgetDocument::unwrapped() const {
	return _bare && !bareKeepsBubble();
}

bool WidgetDocument::customInfoLayout() const {
	return !_parent->hasBubble();
}

bool WidgetDocument::bareKeepsBubble() const {
	const auto item = _parent->data();
	return item->isSponsored()
		|| _parent->hasCommentsButton()
		|| item->viaBot()
		|| !item->emptyText()
		|| (!_hideReply && item->Has<HistoryMessageReply>())
		|| _parent->displayForwardedFrom();
}

QMargins WidgetDocument::cardMargins() const {
	if (!_parent->hasBubble()) {
		return {};
	}
	return {
		st::msgPadding.left(),
		isBubbleTop() ? st::msgPadding.top() : 0,
		st::msgPadding.right(),
		isBubbleBottom() ? st::msgPadding.bottom() : 0,
	};
}

QRect WidgetDocument::cardRect() const {
	const auto full = QRect(0, 0, width(), height() - infoHeight());
	return full.marginsRemoved(cardMargins());
}

int WidgetDocument::countHeight() const {
	const auto margins = cardMargins();
	const auto content = _runtime->height().value_or(
		st::tgwPlaceholderHeight);
	return margins.top() + content + margins.bottom() + infoHeight();
}

int WidgetDocument::infoHeight() const {
	if (!infoBelowCard() || _parent->hidesBottomInfo()) {
		return 0;
	} else if (_parent->hasBubble()) {
		return st::tgwBubbleInfoSkip + _parent->skipBlockHeight();
	}
	const auto line = st::msgDateFont->height;
	const auto effect = _parent->data()->effectId() ? line : 0;
	return st::tgwBareInfoSkip
		+ line
		+ effect
		+ 2 * st::msgDateImgPadding.y();
}

bool WidgetDocument::infoBelowCard() const {
	return isBubbleBottom() && (_parent->media() == this);
}

std::optional<QRect> WidgetDocument::rightActionRect() const {
	if (_parent->hasBubble() || (_parent->media() != this)) {
		return std::nullopt;
	}
	const auto size = _parent->rightActionSize();
	if (!size) {
		return std::nullopt;
	}
	const auto left = _parent->hasRightLayout()
		? -(size->width() + st::historyFastShareLeft)
		: (width() + st::historyFastShareLeft);
	const auto top = height() - st::historyFastShareBottom - size->height();
	return QRect(QPoint(left, top), *size);
}

Tgw::RendererCore *WidgetDocument::pointerCore() const {
	const auto selection = _parent->delegate()->elementInSelectionMode(
		_parent);
	return selection.inSelectionMode ? nullptr : _runtime->interactive();
}

QPointF WidgetDocument::cssPoint(QPoint point) const {
	const auto local = point - cardRect().topLeft();
	return QPointF(local) * (kCssPercent / style::Scale());
}

void WidgetDocument::draw(Painter &p, const PaintContext &context) const {
	const auto card = cardRect();
	if (card.isEmpty()) {
		return;
	}
	_runtime->setBubbleColor(BubbleColor(context));
	if (const auto widget = PaintWidget(p)) {
		_runtime->setWindow(widget->window());
	}
	ensureDataMediaCreated();
	const auto loaded = _dataMedia->loaded();
	if (!loaded) {
		requestFallback();
	}

	const auto bubble = _parent->hasBubble();
	const auto visible = card.intersects(context.clip);
	const auto core = visible ? _runtime->core() : nullptr;
	const auto frame = core ? core->frame() : QImage();
	if (frame.isNull()) {
		if (bubble) {
			p.fillRect(card, context.messageStyle()->msgWaveformInactive);
		} else {
			const auto sti = context.imageStyle();
			Ui::FillRoundRect(
				p,
				card,
				sti->msgServiceBg,
				sti->msgServiceBgCornersSmall);
		}
		if (!_runtime->stopped()) {
			drawFilename(
				p,
				card,
				(bubble
					? context.messageStyle()->historyFileNameFg
					: context.st->msgServiceFg()));
		}
	} else {
		p.save();
		p.setClipRect(card, Qt::IntersectClip);
		p.drawImage(card.topLeft(), frame);
		p.restore();
	}
	if (!bubble && context.selected()) {
		fillImageOverlay(p, card, std::nullopt, context);
	}
	if (_runtime->stopped()) {
		drawStopped(p, context, card);
	}
	if (!bubble && infoBelowCard()) {
		_parent->drawInfo(
			p,
			context,
			width(),
			height(),
			width(),
			InfoDisplayType::Background);
	}
	if (const auto action = rightActionRect()) {
		_parent->drawRightAction(
			p,
			context,
			action->x(),
			action->y(),
			width());
	}
	if (loaded && visible) {
		_runtime->painted();
	}
}

void WidgetDocument::drawFilename(
		Painter &p,
		QRect card,
		const style::color &fg) const {
	const auto name = card.marginsRemoved({
		st::msgPadding.left(),
		0,
		st::msgPadding.right(),
		0,
	});
	if (name.isEmpty()) {
		return;
	}
	p.setFont(st::semiboldFont);
	p.setPen(fg);
	p.drawText(
		name,
		st::semiboldFont->elided(
			_data->filename(),
			name.width(),
			Qt::ElideMiddle),
		style::al_center);
}

void WidgetDocument::drawStopped(
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

TextState WidgetDocument::textState(
		QPoint point,
		StateRequest request) const {
	auto result = TextState(_parent);
	const auto card = cardRect();
	if (card.isEmpty()) {
		return result;
	} else if (!_parent->hasBubble() && infoBelowCard()) {
		const auto info = _parent->bottomInfoTextState(
			width(),
			height(),
			point,
			InfoDisplayType::Background);
		if (info.link
			|| info.cursor != CursorState::None
			|| info.customTooltip) {
			return info;
		}
	}
	if (const auto action = rightActionRect()) {
		if (action->contains(point)) {
			result.link = _parent->rightActionLink(
				point - action->topLeft());
			return result;
		}
	}
	if (!card.contains(point)) {
		return result;
	} else if (const auto core = pointerCore()) {
		using Flag = Ui::Text::StateRequest::Flag;
		const auto hand = (request.flags & Flag::LookupLink)
			&& !OtherLinkPressed(_cardLink)
			&& core->handCursorAt(cssPoint(point));
		result.link = _cardLink;
		result.cursor = hand ? CursorState::None : CursorState::Default;
		return result;
	}
	result.link = _data->uploading()
		? _cancell
		: dataLoaded()
		? _openl
		: _data->loading()
		? _cancell
		: _savel;
	return result;
}

void WidgetDocument::updatePressed(QPoint point) {
	_pointerPoint = point;
	const auto core = _runtime->interactive();
	if (!core || OtherLinkPressed(_cardLink)) {
		return;
	} else if (core->pressed() || (_cardHovered && pointerCore())) {
		core->pointerMove(cssPoint(point), CurrentModifiers());
	}
}

bool WidgetDocument::toggleSelectionByHandlerClick(
		const ClickHandlerPtr &p) const {
	return (p == _cardLink) || File::toggleSelectionByHandlerClick(p);
}

bool WidgetDocument::allowTextSelectionByHandler(
		const ClickHandlerPtr &p) const {
	return (p == _cardLink) && !_cardClaimed;
}

bool WidgetDocument::dragItemByHandler(const ClickHandlerPtr &p) const {
	return (p == _cardLink) ? !_cardClaimed : File::dragItemByHandler(p);
}

void WidgetDocument::clickHandlerActiveChanged(
		const ClickHandlerPtr &p,
		bool active) {
	if (p != _cardLink) {
		File::clickHandlerActiveChanged(p, active);
		return;
	}
	_cardHovered = active;
	if (!active) {
		if (const auto core = _runtime->interactive()) {
			core->pointerLeave();
		}
	} else if (const auto core = pointerCore()) {
		core->pointerEnter();
	}
}

void WidgetDocument::clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) {
	if (p == _cardLink) {
		if (pressed) {
			pressCard();
		} else {
			releaseCard();
		}
	}
	File::clickHandlerPressedChanged(p, pressed);
}

void WidgetDocument::pressCard() {
	const auto core = pointerCore();
	if (!core) {
		return;
	}
	core->pointerPress(cssPoint(_pointerPoint), CurrentModifiers());
	if (core->claimed()) {
		setCardClaimed(true);
	}
}

void WidgetDocument::releaseCard() {
	if (const auto core = _runtime->interactive()) {
		core->pointerRelease(cssPoint(_pointerPoint), CurrentModifiers());
		if (!_cardHovered) {
			core->pointerLeave();
		}
	}
	setCardClaimed(false);
}

void WidgetDocument::setCardClaimed(bool claimed) {
	_cardClaimed = claimed;
	const auto document = claimed ? nullptr : _data.get();
	_cardLink->setProperty(
		kDocumentLinkMediaProperty,
		reinterpret_cast<qulonglong>(document));
}

QPoint WidgetDocument::resolveCustomInfoRightBottom() const {
	return QPoint(
		width() - st::msgDateImgPadding.x(),
		height() - st::msgDateImgPadding.y());
}

void WidgetDocument::ensureDataMediaCreated() const {
	if (_dataMedia) {
		return;
	}
	_dataMedia = _data->createMediaView();
	_data->owner().registerHeavyViewPart(_parent);
	if (!Tgw::HasValidatedContent(_data)) {
		requestFallback();
	}
}

void WidgetDocument::requestFallback() const {
	if (_fallbackRequested) {
		return;
	}
	_fallbackRequested = true;
	crl::on_main(this, [=] {
		if (Tgw::HasValidatedContent(_data)) {
			_fallbackRequested = false;
		} else {
			_data->owner().requestItemViewRefresh(_realParent);
		}
	});
}

float64 WidgetDocument::dataProgress() const {
	ensureDataMediaCreated();
	return _dataMedia->progress();
}

bool WidgetDocument::dataFinished() const {
	return !_data->loading()
		&& (!_data->uploading() || _data->waitingForAlbum());
}

bool WidgetDocument::dataLoaded() const {
	ensureDataMediaCreated();
	return _dataMedia->loaded();
}

bool WidgetDocument::hasHeavyPart() const {
	return (_dataMedia != nullptr) || (_runtime && _runtime->heavy());
}

void WidgetDocument::unloadHeavyPart() {
	_dataMedia = nullptr;
	_runtime->unload();
}

void WidgetDocument::runtimeHeavyChanged() {
	if (_runtime->heavy()) {
		_data->owner().registerHeavyViewPart(_parent);
	} else {
		_parent->checkHeavyPart();
	}
}

} // namespace HistoryView
