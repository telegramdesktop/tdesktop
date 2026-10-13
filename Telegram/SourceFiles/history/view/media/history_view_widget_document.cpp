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
#include "tgw/tgw_document.h"
#include "ui/chat/chat_style.h"
#include "ui/painter.h"

#include "styles/style_chat.h"
#include "styles/style_tgw.h"

namespace HistoryView {

WidgetDocument::WidgetDocument(
	not_null<Element*> parent,
	not_null<HistoryItem*> realParent,
	not_null<DocumentData*> document)
: File(parent, realParent)
, _data(document) {
	setDocumentLinks(_data, realParent);

	// Adopts bytes the replaced file view may hold only in memory.
	_dataMedia = _data->createMediaView();
	_data->owner().registerHeavyViewPart(_parent);
}

WidgetDocument::~WidgetDocument() {
	if (_dataMedia) {
		_data->owner().keepAlive(base::take(_dataMedia));
		_parent->checkHeavyPart();
	}
}

QSize WidgetDocument::countOptimalSize() {
	return { st::msgMaxWidth, countHeight() };
}

QSize WidgetDocument::countCurrentSize(int newWidth) {
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
	return margins.top() + st::tgwPlaceholderHeight + margins.bottom();
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
	if (!_dataMedia->loaded()) {
		requestFallback();
	}

	const auto stm = context.messageStyle();
	p.fillRect(card, stm->msgWaveformInactive);

	const auto name = card.marginsRemoved({
		st::msgPadding.left(),
		0,
		st::msgPadding.right(),
		0,
	});
	if (!name.isEmpty()) {
		p.setFont(st::semiboldFont);
		p.setPen(stm->historyFileNameFg);
		p.drawText(
			name,
			st::semiboldFont->elided(
				_data->filename(),
				name.width(),
				Qt::ElideMiddle),
			style::al_center);
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
	return (_dataMedia != nullptr);
}

void WidgetDocument::unloadHeavyPart() {
	_dataMedia = nullptr;
}

} // namespace HistoryView
