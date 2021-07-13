/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_call.h"

#include "lang/lang_keys.h"
#include "ui/text/format_values.h"
#include "layout.h" // FullSelection
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "core/application.h"
#include "calls/calls_instance.h"
#include "data/data_media_types.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

using FinishReason = Data::CallFinishReason;

[[nodiscard]] int ComputeDuration(FinishReason reason, int duration) {
	return (reason != FinishReason::Missed
		&& reason != FinishReason::Busy)
		? duration
		: 0;
}

} // namespace

Call::Call(
	not_null<Element*> parent,
	not_null<Data::Call*> call)
: Media(parent)
, _duration(ComputeDuration(call->finishReason, call->duration))
, _reason(call->finishReason)
, _video(call->video) {
	const auto item = parent->data();
	_text = Data::MediaCall::Text(item, _reason, _video);
	_status = parent->dateTime().time().toString(cTimeFormat());
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
	const auto user = _parent->data()->history()->peer->asUser();
	const auto video = _video;
	_link = std::make_shared<LambdaClickHandler>([=] {
		if (user) {
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

void Call::draw(Painter &p, const QRect &r, TextSelection selection, crl::time ms) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	auto paintw = width();

	auto outbg = _parent->hasOutLayout();
	auto selected = (selection == FullSelection);

	accumulate_min(paintw, maxWidth());

	auto nameleft = 0, nametop = 0, statustop = 0;
	auto topMinus = isBubbleTop() ? 0 : st::msgFileTopMinus;

	nameleft = st::historyCallLeft;
	nametop = st::historyCallTop - topMinus;
	statustop = st::historyCallStatusTop - topMinus;

	p.setFont(st::semiboldFont);
	p.setPen(outbg ? (selected ? st::historyFileNameOutFgSelected : st::historyFileNameOutFg) : (selected ? st::historyFileNameInFgSelected : st::historyFileNameInFg));
	p.drawTextLeft(nameleft, nametop, paintw, _text);

	auto statusleft = nameleft;
	auto missed = (_reason == FinishReason::Missed || _reason == FinishReason::Busy);
	auto &arrow = outbg ? (selected ? st::historyCallArrowOutSelected : st::historyCallArrowOut) : missed ? (selected ? st::historyCallArrowMissedInSelected : st::historyCallArrowMissedIn) : (selected ? st::historyCallArrowInSelected : st::historyCallArrowIn);
	arrow.paint(p, statusleft + st::historyCallArrowPosition.x(), statustop + st::historyCallArrowPosition.y(), paintw);
	statusleft += arrow.width() + st::historyCallStatusSkip;

	auto &statusFg = outbg ? (selected ? st::mediaOutFgSelected : st::mediaOutFg) : (selected ? st::mediaInFgSelected : st::mediaInFg);
	p.setFont(st::normalFont);
	p.setPen(statusFg);
	p.drawTextLeft(statusleft, statustop, paintw, _status);

	const auto &icon = _video
		? (outbg
			? (selected ? st::historyCallCameraOutIconSelected : st::historyCallCameraOutIcon)
			: (selected ? st::historyCallCameraInIconSelected : st::historyCallCameraInIcon))
		: (outbg
			? (selected ? st::historyCallOutIconSelected : st::historyCallOutIcon)
			: (selected ? st::historyCallInIconSelected : st::historyCallInIcon));
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
