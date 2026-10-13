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
#include "tgw/tgw_runtime.h"
#include "ui/chat/chat_style.h"
#include "ui/cached_round_corners.h"
#include "ui/painter.h"

#include "styles/style_chat.h"
#include "styles/style_tgw.h"

namespace HistoryView {

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
})) {
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
	_runtime->setWidth(std::max(
		newWidth - st::msgPadding.left() - st::msgPadding.right(),
		0));
	return { newWidth, countHeight() };
}

QMargins WidgetDocument::cardMargins() const {
	return {
		st::msgPadding.left(),
		isBubbleTop() ? st::msgPadding.top() : 0,
		st::msgPadding.right(),
		isBubbleBottom() ? st::msgPadding.bottom() : 0,
	};
}

QRect WidgetDocument::cardRect() const {
	return QRect(0, 0, width(), height()).marginsRemoved(cardMargins());
}

int WidgetDocument::countHeight() const {
	const auto margins = cardMargins();
	const auto content = _runtime->height().value_or(
		st::tgwPlaceholderHeight);
	return margins.top() + content + margins.bottom();
}

bool WidgetDocument::drawsInfo() const {
	return isBubbleBottom() && (_parent->media() == this);
}

void WidgetDocument::draw(Painter &p, const PaintContext &context) const {
	const auto card = cardRect();
	if (card.isEmpty()) {
		return;
	}
	ensureDataMediaCreated();
	const auto loaded = _dataMedia->loaded();
	if (!loaded) {
		requestFallback();
	}

	p.fillRect(card, context.messageStyle()->msgWaveformInactive);
	if (_runtime->stopped()) {
		drawStopped(p, context, card);
	} else {
		drawFilename(p, context, card);
	}
	if (drawsInfo()) {
		_parent->drawInfo(
			p,
			context,
			card.x() + card.width(),
			card.y() + card.height(),
			width(),
			InfoDisplayType::Image);
	}
	if (loaded && card.intersects(context.clip)) {
		_runtime->painted();
	}
}

void WidgetDocument::drawFilename(
		Painter &p,
		const PaintContext &context,
		QRect card) const {
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
	p.setPen(context.messageStyle()->historyFileNameFg);
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
	if (drawsInfo()) {
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
		const auto info = _parent->bottomInfoTextState(
			card.x() + card.width(),
			card.y() + card.height(),
			point,
			InfoDisplayType::Image);
		if (info.link
			|| info.cursor != CursorState::None
			|| info.customTooltip) {
			return info;
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
