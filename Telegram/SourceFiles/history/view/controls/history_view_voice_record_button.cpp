/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_voice_record_button.h"

#include "ui/paint/blobs.h"

#include "styles/style_chat.h"
#include "styles/style_layers.h"

namespace HistoryView::Controls {

namespace {

constexpr auto kMaxLevel = 1800.;
constexpr auto kBlobAlpha = 76. / 255.;
constexpr auto kBlobMaxSpeed = 5.0;
constexpr auto kLevelDuration = 100. + 500. * 0.33;
constexpr auto kBlobsScaleEnterDuration = crl::time(250);

auto Blobs() {
	return std::vector<Ui::Paint::Blobs::BlobData>{
		{
			.segmentsCount = 9,
			.minScale = 0.605229,
			.minRadius = (float)st::historyRecordMinorBlobMinRadius,
			.maxRadius = (float)st::historyRecordMinorBlobMaxRadius,
			.speedScale = 1.,
			.alpha = kBlobAlpha,
			.maxSpeed = kBlobMaxSpeed,
		},
		{
			.segmentsCount = 12,
			.minScale = 0.553943,
			.minRadius = (float)st::historyRecordMajorBlobMinRadius,
			.maxRadius = (float)st::historyRecordMajorBlobMaxRadius,
			.speedScale = 1.,
			.alpha = kBlobAlpha,
			.maxSpeed = kBlobMaxSpeed,
		},
	};
}

} // namespace

VoiceRecordButton::VoiceRecordButton(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<> leaveWindowEventProducer)
: AbstractButton(parent)
, _blobs(std::make_unique<Ui::Paint::Blobs>(
	Blobs(),
	kLevelDuration,
	kMaxLevel))
, _center(_blobs->maxRadius()) {
	resize(_center * 2, _center * 2);
	std::move(
		leaveWindowEventProducer
	) | rpl::start_with_next([=] {
		_inCircle = false;
	}, lifetime());
	init();
}

VoiceRecordButton::~VoiceRecordButton() = default;

void VoiceRecordButton::requestPaintLevel(quint16 level) {
	if (_blobsHideLastTime) {
		 return;
	}
	_blobs->setLevel(level);
	update();
}

void VoiceRecordButton::init() {
	const auto currentState = lifetime().make_state<Type>(_state.current());

	rpl::single(
		anim::Disabled()
	) | rpl::then(
		anim::Disables()
	) | rpl::start_with_next([=](bool hide) {
		if (hide) {
			_blobs->setLevel(0.);
		}
		_blobsHideLastTime = hide ? crl::now() : 0;
		if (!hide && !_animation.animating() && isVisible()) {
			_animation.start();
		}
	}, lifetime());

	const auto &mainRadiusMin = st::historyRecordMainBlobMinRadius;
	const auto mainRadiusDiff = st::historyRecordMainBlobMaxRadius
		- mainRadiusMin;
	paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		Painter p(this);

		const auto hideProgress = _blobsHideLastTime
			? 1. - std::clamp(
				((crl::now() - _blobsHideLastTime)
					/ (float64)kBlobsScaleEnterDuration),
				0.,
				1.)
			: 1.;
		const auto showProgress = _showProgress.current();
		const auto complete = (showProgress == 1.);

		p.translate(_center, _center);
		PainterHighQualityEnabler hq(p);
		const auto brush = QBrush(anim::color(
			st::historyRecordVoiceFgInactive,
			st::historyRecordVoiceFgActive,
			_colorProgress));

		_blobs->paint(p, brush, showProgress * hideProgress);

		const auto radius = (mainRadiusMin
			+ (mainRadiusDiff * _blobs->currentLevel())) * showProgress;

		p.setPen(Qt::NoPen);
		p.setBrush(brush);
		p.drawEllipse(QPointF(), radius, radius);

		if (!complete) {
			p.setOpacity(showProgress);
		}

		// Paint icon.
		{
			const auto stateProgress = _stateChangedAnimation.value(0.);
			const auto scale = (std::cos(M_PI * 2 * stateProgress) + 1.) * .5;
			if (scale < 1.) {
				p.scale(scale, scale);
			}
			const auto state = *currentState;
			const auto icon = (state == Type::Send)
				? st::historySendIcon
				: st::historyRecordVoiceActive;
			const auto position = (state == Type::Send)
				? st::historyRecordSendIconPosition
				: QPoint(0, 0);
			icon.paint(
				p,
				-icon.width() / 2 + position.x(),
				-icon.height() / 2 + position.y(),
				0,
				st::historyRecordVoiceFgActiveIcon->c);
		}
	}, lifetime());

	_animation.init([=](crl::time now) {
		if (const auto &last = _blobsHideLastTime; (last > 0)
			&& (now - last >= kBlobsScaleEnterDuration)) {
			_animation.stop();
			return false;
		}
		_blobs->updateLevel(now - _lastUpdateTime);
		_lastUpdateTime = now;
		update();
		return true;
	});

	rpl::merge(
		shownValue(),
		_showProgress.value(
		) | rpl::map(rpl::mappers::_1 != 0.) | rpl::distinct_until_changed()
	) | rpl::start_with_next([=](bool show) {
		setVisible(show);
		setMouseTracking(show);
		if (!show) {
			_animation.stop();
			_showProgress = 0.;
			_blobs->resetLevel();
			_state = Type::Record;
		} else {
			if (!_animation.animating()) {
				_animation.start();
			}
		}
	}, lifetime());

	actives(
	) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](bool active) {
		setPointerCursor(active);
	}, lifetime());

	_state.changes(
	) | rpl::start_with_next([=](Type newState) {
		const auto to = 1.;
		auto callback = [=](float64 value) {
			if (value >= (to * .5)) {
				*currentState = newState;
			}
			update();
		};
		const auto duration = st::historyRecordVoiceDuration * 2;
		_stateChangedAnimation.start(std::move(callback), 0., to, duration);
	}, lifetime());
}

rpl::producer<bool> VoiceRecordButton::actives() const {
	return events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return (e->type() == QEvent::MouseMove
			|| e->type() == QEvent::Leave
			|| e->type() == QEvent::Enter);
	}) | rpl::map([=](not_null<QEvent*> e) {
		switch(e->type()) {
		case QEvent::MouseMove:
			return inCircle((static_cast<QMouseEvent*>(e.get()))->pos());
		case QEvent::Leave: return false;
		case QEvent::Enter: return inCircle(mapFromGlobal(QCursor::pos()));
		default: return false;
		}
	});
}

rpl::producer<> VoiceRecordButton::clicks() const {
	return Ui::AbstractButton::clicks(
	) | rpl::to_empty | rpl::filter([=] {
		return inCircle(mapFromGlobal(QCursor::pos()));
	});
}

bool VoiceRecordButton::inCircle(const QPoint &localPos) const {
	const auto &radii = st::historyRecordMainBlobMaxRadius;
	const auto dx = std::abs(localPos.x() - _center);
	if (dx > radii) {
		return false;
	}
	const auto dy = std::abs(localPos.y() - _center);
	if (dy > radii) {
		return false;
	} else if (dx + dy <= radii) {
		return true;
	}
	return ((dx * dx + dy * dy) <= (radii * radii));
}

void VoiceRecordButton::requestPaintProgress(float64 progress) {
	_showProgress = progress;
	update();
}

void VoiceRecordButton::requestPaintColor(float64 progress) {
	if (_colorProgress == progress) {
		return;
	}
	_colorProgress = progress;
	update();
}

void VoiceRecordButton::setType(Type state) {
	_state = state;
}

} // namespace HistoryView::Controls
