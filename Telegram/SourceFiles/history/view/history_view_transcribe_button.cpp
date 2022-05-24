/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_transcribe_button.h"

#include "history/history.h"
#include "history/history_item.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "ui/chat/chat_style.h"
#include "ui/click_handler.h"
#include "api/api_transcribes.h"
#include "apiwrap.h"
#include "styles/style_chat.h"

namespace HistoryView {

TranscribeButton::TranscribeButton(not_null<HistoryItem*> item)
: _item(item) {
}

QSize TranscribeButton::size() const {
	return QSize(st::historyTranscribeSize, st::historyTranscribeSize);
}

void TranscribeButton::paint(
		QPainter &p,
		int x,
		int y,
		const PaintContext &context) {
	auto hq = PainterHighQualityEnabler(p);
	const auto stm = context.messageStyle();
	p.setBrush(stm->msgWaveformInactive);
	const auto radius = size().width() / 4;
	p.drawRoundedRect(QRect{ QPoint(x, y), size() }, radius, radius);
}

ClickHandlerPtr TranscribeButton::link() {
	if (!_item->isHistoryEntry() || _item->isLocal()) {
		return nullptr;
	} else if (_link) {
		return _link;
	}
	const auto session = &_item->history()->session();
	const auto id = _item->fullId();
	_link = std::make_shared<LambdaClickHandler>([=] {
		if (const auto item = session->data().message(id)) {
			session->api().transcribes().toggle(item);
		}
	});
	return _link;
}

} // namespace HistoryView
