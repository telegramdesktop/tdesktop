/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_call.h"

#include "lang/lang_keys.h"
#include "ui/chat/chat_style.h"
#include "ui/text/format_values.h"
#include "ui/painter.h"
#include "layout/layout_selection.h" // FullSelection
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "calls/calls_instance.h"
#include "data/data_media_types.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

using State = Data::CallState;

[[nodiscard]] int ComputeDuration(State state, int duration) {
	return (state != State::Missed && state != State::Busy)
		? duration
		: 0;
}

} // namespace

Call::Call(
	not_null<Element*> parent,
	not_null<Data::Call*> call)
: Media(parent)
, _duration(ComputeDuration(call->state, call->duration))
, _state(call->state)
, _conference(call->conferenceId != 0)
, _video(call->video) {
	const auto item = parent->data();
	_text = Data::MediaCall::Text(item, _state, _conference, _video);
	_status = QLocale().toString(
		parent->dateTime().time(),
		QLocale::ShortFormat);
	if (_duration) {
		_status = tr::lng_call_duration_info(
			tr::now,
			lt_time,
			_status,
			lt_duration,
			Ui::FormatDurationWords(_duration));
	}
}

QSize Call::countOptimalSize() {
	const auto user = _parent->history()->peer->asUser();
	const auto conference = _conference;
	const auto video = _video;
	const auto contextId = _parent->data()->fullId();
	const auto id = _parent->data()->id;
	_link = std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		if (conference) {
			const auto my = context.other.value<ClickHandlerContext>();
			const auto weak = my.sessionWindow;
			if (const auto strong = weak.get()) {
				QSize();
				strong->resolveConferenceCall(id, contextId);
			}
		} else if (user) {
			Core::App().calls().startOutgoingCall(user, video);
		}
	});
	auto maxWidth = st::historyCallWidth;
	auto minHeight = st::historyCallHeight;
	if (!isBubbleTop()) {
		minHeight -= st::msgFileTopMinus;
	}
	return { maxWidth, minHeight };
}

void Call::draw(Painter &p, const PaintContext &context) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	auto paintw = width();

	const auto stm = context.messageStyle();

	accumulate_min(paintw, maxWidth());

	auto nameleft = 0, nametop = 0, statustop = 0;
	auto topMinus = isBubbleTop() ? 0 : st::msgFileTopMinus;

	nameleft = st::historyCallLeft;
	nametop = st::historyCallTop - topMinus;
	statustop = st::historyCallStatusTop - topMinus;

	p.setFont(st::semiboldFont);
	p.setPen(stm->historyFileNameFg);
	p.drawTextLeft(nameleft, nametop, paintw, _text);

	auto statusleft = nameleft;
	auto missed = (_state == State::Missed) || (_state == State::Busy);
	const auto &arrow = missed
		? stm->historyCallArrowMissed
		: stm->historyCallArrow;
	arrow.paint(p, statusleft + st::historyCallArrowPosition.x(), statustop + st::historyCallArrowPosition.y(), paintw);
	statusleft += arrow.width() + st::historyCallStatusSkip;

	p.setFont(st::normalFont);
	p.setPen(stm->mediaFg);
	p.drawTextLeft(statusleft, statustop, paintw, _status);

	const auto &icon = _video
		? stm->historyCallCameraIcon
		: _conference
		? stm->historyCallGroupIcon
		: stm->historyCallIcon;
	icon.paint(p, paintw - st::historyCallIconPosition.x() - icon.width(), st::historyCallIconPosition.y() - topMinus, paintw);
}

TextState Call::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);
	if (QRect(0, 0, width(), height()).contains(point)) {
		result.link = _link;
		return result;
	}
	return result;
}

} // namespace HistoryView
