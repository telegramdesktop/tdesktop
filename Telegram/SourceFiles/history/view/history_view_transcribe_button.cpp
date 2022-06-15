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
#include "ui/effects/radial_animation.h"
#include "api/api_transcribes.h"
#include "apiwrap.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

constexpr auto kInNonChosenOpacity = 0.12;
constexpr auto kOutNonChosenOpacity = 0.18;

} // namespace

TranscribeButton::TranscribeButton(not_null<HistoryItem*> item)
: _item(item) {
}

TranscribeButton::~TranscribeButton() = default;

QSize TranscribeButton::size() const {
	return st::historyTranscribeSize;
}

void TranscribeButton::setLoading(bool loading, Fn<void()> update) {
	if (_loading == loading) {
		return;
	}
	_loading = loading;
	if (_loading) {
		_animation = std::make_unique<Ui::InfiniteRadialAnimation>(
			update,
			st::defaultInfiniteRadialAnimation);
		_animation->start();
	} else if (_animation) {
		_animation->stop();
	}
}

void TranscribeButton::paint(
		QPainter &p,
		int x,
		int y,
		const PaintContext &context) {
	auto hq = PainterHighQualityEnabler(p);
	const auto opened = _openedAnimation.value(_opened ? 1. : 0.);
	const auto stm = context.messageStyle();
	auto bg = stm->msgFileBg->c;
	bg.setAlphaF(bg.alphaF() * (context.outbg
		? kOutNonChosenOpacity
		: kInNonChosenOpacity));
	p.setBrush(bg);
	const auto radius = st::historyTranscribeRadius;
	const auto state = _animation
		? _animation->computeState()
		: Ui::RadialState();
	if (state.shown > 0.) {
		auto fg = stm->msgWaveformActive->c;
		fg.setAlphaF(fg.alphaF() * state.shown * (1. - opened));
		auto pen = QPen(fg);
		const auto thickness = style::ConvertScaleExact(2.);
		const auto widthNoRadius = size().width() - 2 * radius;
		const auto heightNoRadius = size().height() - 2 * radius;
		const auto length = 2 * (widthNoRadius + heightNoRadius)
			+ 2 * M_PI * radius;
		pen.setWidthF(thickness);
		pen.setCapStyle(Qt::RoundCap);
		const auto ratio = length / (Ui::RadialState::kFull * thickness);
		const auto filled = ratio * state.arcLength;
		pen.setDashPattern({ filled, (length / thickness) - filled });
		pen.setDashOffset(ratio * (state.arcFrom + state.arcLength));
		p.setPen(pen);
	} else {
		p.setPen(Qt::NoPen);
		if (!_loading) {
			_animation = nullptr;
		}
	}
	const auto r = QRect{ QPoint(x, y), size() };
	p.drawRoundedRect(r, radius, radius);
	if (opened > 0.) {
		if (opened != 1.) {
			p.save();
			p.setOpacity(opened);
			p.translate(r.center());
			p.scale(opened, opened);
			p.translate(-r.center());
		}
		stm->historyTranscribeHide.paintInCenter(p, r);
		if (opened != 1.) {
			p.restore();
		}
	}
	if (opened < 1.) {
		if (opened != 0.) {
			p.save();
			p.setOpacity(1. - opened);
			p.translate(r.center());
			p.scale(1. - opened, 1. - opened);
			p.translate(-r.center());
		}
		stm->historyTranscribeIcon.paintInCenter(p, r);
		if (opened != 0.) {
			p.restore();
		}
	}
	p.setOpacity(1.);
}

void TranscribeButton::setOpened(bool opened, Fn<void()> update) {
	if (_opened == opened) {
		return;
	}
	_opened = opened;
	if (update) {
		_openedAnimation.start(
			std::move(update),
			_opened ? 0. : 1.,
			_opened ? 1. : 0.,
			st::fadeWrapDuration);
	} else {
		_openedAnimation.stop();
	}
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
