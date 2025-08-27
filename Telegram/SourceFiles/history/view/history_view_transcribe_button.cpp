/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_transcribe_button.h"

#include "base/unixtime.h"
#include "boxes/premium_preview_box.h"
#include "core/click_handler_types.h" // ClickHandlerContext
#include "history/history.h"
#include "history/history_item.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "lang/lang_keys.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/radial_animation.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "api/api_transcribes.h"
#include "apiwrap.h"
#include "styles/style_chat.h"
#include "window/window_session_controller.h"

namespace HistoryView {
namespace {

constexpr auto kInNonChosenOpacity = 0.12;
constexpr auto kOutNonChosenOpacity = 0.18;

void ClipPainterForLock(QPainter &p, bool roundview, const QRect &r) {
	const auto &pos = roundview
		? st::historyFastTranscribeLockOverlayPos
		: st::historyTranscribeLockOverlayPos;
	const auto &size = roundview
		? st::historyFastTranscribeLockOverlaySize
		: st::historyTranscribeLockOverlaySize;

	auto clipPath = QPainterPath();
	clipPath.addRect(r);
	const auto clear = QRect(pos + r.topLeft(), size);
	clipPath.addRoundedRect(clear, clear.width() * 0.5, clear.height() * 0.5);
	p.setClipPath(clipPath);
}

} // namespace

TranscribeButton::TranscribeButton(
	not_null<HistoryItem*> item,
	bool roundview)
: _item(item)
, _roundview(roundview)
, _size(!roundview
	? st::historyTranscribeSize
	: QSize(st::historyFastShareSize, st::historyFastShareSize)) {
}

TranscribeButton::~TranscribeButton() = default;

QSize TranscribeButton::size() const {
	return _size;
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
	if (_roundview) {
		_lastPaintedPoint = { x, y };
		const auto r = QRect(QPoint(x, y), size());

		if (_ripple) {
			const auto colorOverride = &stm->msgWaveformInactive->c;
			_ripple->paint(
				p,
				x,
				y,
				r.width(),
				colorOverride);
			if (_ripple->empty()) {
				_ripple.reset();
			}
		}

		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(context.st->msgServiceBg());

		p.drawEllipse(r);
		if (!_loading && hasLock()) {
			ClipPainterForLock(p, true, r);
			context.st->historyFastTranscribeIcon().paintInCenter(p, r);
			p.setClipping(false);
			context.st->historyFastTranscribeLock().paint(
				p,
				r.topLeft() + st::historyFastTranscribeLockPos,
				r.width());
		} else {
			context.st->historyFastTranscribeIcon().paintInCenter(p, r);
		}

		const auto state = _animation
			? _animation->computeState()
			: Ui::RadialState();

		auto pen = QPen(st::msgServiceFg);
		pen.setCapStyle(Qt::RoundCap);
		p.setPen(pen);
		if (_animation && state.shown > 0 && anim::Disabled()) {
			const auto _st = &st::defaultRadio;
			anim::DrawStaticLoading(
				p,
				r,
				_st->thickness,
				pen.color(),
				_st->bg);
		} else if (state.arcLength < arc::kFullLength) {
			const auto opacity = p.opacity();
			p.setOpacity(state.shown * (1. - opened));
			p.drawArc(r, state.arcFrom, state.arcLength);
			p.setOpacity(opacity);
		}

		return;
	}
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

		if (!_loading && hasLock()) {
			ClipPainterForLock(p, false, r);
			stm->historyTranscribeIcon.paintInCenter(p, r);
			p.setClipping(false);
			stm->historyTranscribeLock.paint(
				p,
				r.topLeft() + st::historyTranscribeLockPos,
				r.width());
		} else {
			stm->historyTranscribeIcon.paintInCenter(p, r);
		}

		if (opened != 0.) {
			p.restore();
		}
	}
	p.setOpacity(1.);
}

bool TranscribeButton::hasLock() const {
	const auto session = &_item->history()->session();
	const auto transcribes = &session->api().transcribes();
	if (session->premium()
		|| transcribes->freeFor(_item)
		|| transcribes->trialsCount()) {
		return false;
	}
	const auto until = transcribes->trialsRefreshAt();
	if (!until || base::unixtime::now() >= until) {
		return false;
	}
	return true;
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
	_link = std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto item = session->data().message(id);
		if (!item) {
			return;
		}
		if (session->premium()) {
			return session->api().transcribes().toggle(item);
		}
		const auto my = context.other.value<ClickHandlerContext>();
		if (hasLock()) {
			if (const auto controller = my.sessionWindow.get()) {
				ShowPremiumPreviewBox(
					controller,
					PremiumFeature::VoiceToText);
			}
		} else {
			const auto max = session->api().transcribes().trialsMaxLengthMs();
			const auto doc = _item->media()
				? _item->media()->document()
				: nullptr;
			if (doc && (doc->isVoiceMessage() || doc->isVideoMessage())) {
				if (doc->duration() > max) {
					if (const auto controller = my.sessionWindow.get()) {
						controller->uiShow()->showToast(
							tr::lng_audio_transcribe_long(tr::now));
						return;
					}
				}
			}
			session->api().transcribes().toggle(item);
		}
	});
	return _link;
}

bool TranscribeButton::contains(const QPoint &p) {
	_lastStatePoint = p - _lastPaintedPoint;
	return QRect(_lastPaintedPoint, size()).contains(p);
}

void TranscribeButton::addRipple(Fn<void()> callback) {
	if (!_ripple) {
		_ripple = std::make_unique<Ui::RippleAnimation>(
			st::defaultRippleAnimation,
			Ui::RippleAnimation::EllipseMask(size()),
			std::move(callback));
	}
	_ripple->add(_lastStatePoint);
}

void TranscribeButton::stopRipple() const {
	if (_ripple) {
		_ripple->lastStop();
	}
}

} // namespace HistoryView
