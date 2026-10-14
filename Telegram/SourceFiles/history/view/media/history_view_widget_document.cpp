/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_widget_document.h"

#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_click_handler.h"
#include "data/data_session.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_element.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "tgw/tgw_document.h"
#include "tgw/tgw_renderer.h"
#include "tgw/tgw_runtime.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/cached_round_corners.h"
#include "ui/painter.h"

#include "styles/style_chat.h"
#include "styles/style_tgw.h"

namespace HistoryView {
namespace {

constexpr auto kPatternColors = 4;

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
, _bare(Tgw::IsBareWidget(document)) {
	setDocumentLinks(_data, realParent);

	// Adopts bytes the replaced file view may hold only in memory.
	_dataMedia = _data->createMediaView();
	_data->owner().registerHeavyViewPart(_parent);
}

WidgetDocument::~WidgetDocument() {
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
	if (!_bare) {
		return true;
	}
	const auto item = _parent->data();
	return item->isSponsored()
		|| _parent->hasCommentsButton()
		|| item->viaBot()
		|| !item->emptyText()
		|| _parent->displayReply()
		|| _parent->displayForwardedFrom()
		|| _parent->displayFromName()
		|| _parent->displayedTopicButton();
}

bool WidgetDocument::unwrapped() const {
	return _bare && !_parent->hasBubble();
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
	const auto full = QRect(0, 0, width(), height() - bareInfoHeight());
	return full.marginsRemoved(cardMargins());
}

int WidgetDocument::countHeight() const {
	const auto margins = cardMargins();
	const auto content = _runtime->height().value_or(
		st::tgwPlaceholderHeight);
	return margins.top() + content + margins.bottom() + bareInfoHeight();
}

int WidgetDocument::bareInfoHeight() const {
	if (_parent->hasBubble()
		|| !drawsInfo()
		|| _parent->hidesBottomInfo()) {
		return 0;
	}
	const auto line = st::msgDateFont->height;
	const auto effect = _parent->data()->effectId() ? line : 0;
	return st::tgwBareInfoSkip
		+ line
		+ effect
		+ 2 * st::msgDateImgPadding.y();
}

bool WidgetDocument::drawsInfo() const {
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

void WidgetDocument::draw(Painter &p, const PaintContext &context) const {
	const auto card = cardRect();
	if (card.isEmpty()) {
		return;
	}
	_runtime->setBubbleColor(BubbleColor(context));
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
	if (drawsInfo()) {
		if (bubble) {
			_parent->drawInfo(
				p,
				context,
				card.x() + card.width(),
				card.y() + card.height(),
				width(),
				InfoDisplayType::Image);
		} else {
			_parent->drawInfo(
				p,
				context,
				width(),
				height(),
				width(),
				InfoDisplayType::Background);
		}
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
	auto right = card.x() + card.width() - skip;
	if (_parent->hasBubble() && drawsInfo()) {
		const auto infoBottom = card.y() + card.height() - skip;
		const auto infoTop = infoBottom - pillHeight;
		if (top < infoBottom && top + pillHeight > infoTop) {
			right -= _parent->infoWidth() + 2 * padding.x() + skip;
		}
	}
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
	} else if (drawsInfo()) {
		const auto info = _parent->hasBubble()
			? _parent->bottomInfoTextState(
				card.x() + card.width(),
				card.y() + card.height(),
				point,
				InfoDisplayType::Image)
			: _parent->bottomInfoTextState(
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
	if (card.contains(point)) {
		result.link = _data->uploading()
			? _cancell
			: dataLoaded()
			? _openl
			: _data->loading()
			? _cancell
			: _savel;
	}
	return result;
}

QPoint WidgetDocument::resolveCustomInfoRightBottom() const {
	if (!_parent->hasBubble()) {
		return QPoint(
			width() - st::msgDateImgPadding.x(),
			height() - st::msgDateImgPadding.y());
	}
	const auto card = cardRect();
	const auto skipx = st::msgDateImgDelta + st::msgDateImgPadding.x();
	const auto skipy = st::msgDateImgDelta + st::msgDateImgPadding.y();
	return QPoint(
		card.x() + card.width() - skipx,
		card.y() + card.height() - skipy);
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
