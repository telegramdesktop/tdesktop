/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_volume_item.h"

#include "calls/group/calls_group_common.h"
#include "ui/effects/animation_value.h"
#include "ui/effects/cross_line.h"
#include "ui/widgets/continuous_sliders.h"
#include "styles/style_calls.h"

#include "ui/paint/arcs.h"

namespace Calls {
namespace {

constexpr auto kMaxVolumePercent = 200;

const auto kSpeakerThreshold = std::vector<float>{
	10.0f / kMaxVolumePercent,
	50.0f / kMaxVolumePercent,
	150.0f / kMaxVolumePercent };

constexpr auto kVolumeStickedValues =
	std::array<std::pair<float64, float64>, 7>{{
		{ 25. / kMaxVolumePercent, 2. / kMaxVolumePercent },
		{ 50. / kMaxVolumePercent, 2. / kMaxVolumePercent },
		{ 75. / kMaxVolumePercent, 2. / kMaxVolumePercent },
		{ 100. / kMaxVolumePercent, 10. / kMaxVolumePercent },
		{ 125. / kMaxVolumePercent, 2. / kMaxVolumePercent },
		{ 150. / kMaxVolumePercent, 2. / kMaxVolumePercent },
		{ 175. / kMaxVolumePercent, 2. / kMaxVolumePercent },
	}};

} // namespace

MenuVolumeItem::MenuVolumeItem(
	not_null<RpWidget*> parent,
	const style::Menu &st,
	rpl::producer<Group::ParticipantState> participantState,
	int startVolume,
	int maxVolume,
	bool muted)
: Ui::Menu::ItemBase(parent, st)
, _maxVolume(maxVolume)
, _cloudMuted(muted)
, _localMuted(muted)
, _slider(base::make_unique_q<Ui::MediaSlider>(
	this,
	st::groupCallMenuVolumeSlider))
, _dummyAction(new QAction(parent))
, _st(st)
, _stCross(st::groupCallMuteCrossLine)
, _crossLineMute(std::make_unique<Ui::CrossLineAnimation>(_stCross, true))
, _arcs(std::make_unique<Ui::Paint::ArcsAnimation>(
	st::groupCallSpeakerArcsAnimation,
	kSpeakerThreshold,
	_localMuted ? 0. : (startVolume / float(maxVolume)),
	Ui::Paint::ArcsAnimation::Direction::Right)) {

	initResizeHook(parent->sizeValue());
	enableMouseSelecting();
	enableMouseSelecting(_slider.get());

	_slider->setAlwaysDisplayMarker(true);

	sizeValue(
	) | rpl::start_with_next([=](const QSize &size) {
		const auto geometry = QRect(QPoint(), size);
		_itemRect = geometry - st::groupCallMenuVolumePadding;
		_speakerRect = QRect(_itemRect.topLeft(), _stCross.icon.size());
		_arcPosition = _speakerRect.center()
			+ QPoint(0, st::groupCallMenuSpeakerArcsSkip);
		_slider->setGeometry(
			st::groupCallMenuVolumeMargin.left(),
			_speakerRect.y(),
			(geometry.width()
				- st::groupCallMenuVolumeMargin.left()
				- st::groupCallMenuVolumeMargin.right()),
			_speakerRect.height());
	}, lifetime());

	setCloudVolume(startVolume);

	paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		Painter p(this);

		const auto volume = _localMuted
			? 0
			: std::round(_slider->value() * kMaxVolumePercent);
		const auto muteProgress =
			_crossLineAnimation.value((!volume) ? 1. : 0.);

		const auto selected = isSelected();
		p.fillRect(clip, selected ? st.itemBgOver : st.itemBg);

		const auto mutePen = anim::color(
			unmuteColor(),
			muteColor(),
			muteProgress);

		_crossLineMute->paint(
			p,
			_speakerRect.topLeft(),
			muteProgress,
			(muteProgress > 0) ? std::make_optional(mutePen) : std::nullopt);

		{
			p.translate(_arcPosition);
			_arcs->paint(p);
		}
	}, lifetime());

	_slider->setChangeProgressCallback([=](float64 value) {
		const auto newMuted = (value == 0);
		if (_localMuted != newMuted) {
			_localMuted = newMuted;
			_toggleMuteLocallyRequests.fire_copy(newMuted);

			_crossLineAnimation.start(
				[=] { update(_speakerRect); },
				_localMuted ? 0. : 1.,
				_localMuted ? 1. : 0.,
				st::callPanelDuration);
		}
		if (value > 0) {
			_changeVolumeLocallyRequests.fire(value * _maxVolume);
		}
		_arcs->setValue(value);
		updateSliderColor(value);
	});

	const auto returnVolume = [=] {
		_changeVolumeLocallyRequests.fire_copy(_cloudVolume);
	};

	_slider->setChangeFinishedCallback([=](float64 value) {
		const auto newVolume = std::round(value * _maxVolume);
		const auto muted = (value == 0);

		if (!_cloudMuted && muted) {
			returnVolume();
			_localMuted = true;
			_toggleMuteRequests.fire(true);
		}
		if (_cloudMuted && muted) {
			returnVolume();
		}
		if (_cloudMuted && !muted) {
			_waitingForUpdateVolume = true;
			_localMuted = false;
			_toggleMuteRequests.fire(false);
		}
		if (!_cloudMuted && !muted) {
			_changeVolumeRequests.fire_copy(newVolume);
		}
		updateSliderColor(value);
	});

	std::move(
		participantState
	) | rpl::start_with_next([=](const Group::ParticipantState &state) {
		const auto newMuted = state.mutedByMe;
		const auto newVolume = state.volume.value_or(0);

		_cloudMuted = _localMuted = newMuted;

		if (!newVolume) {
			return;
		}
		if (_waitingForUpdateVolume) {
			const auto localVolume =
				std::round(_slider->value() * _maxVolume);
			if ((localVolume != newVolume)
				&& (_cloudVolume == newVolume)) {
				_changeVolumeRequests.fire(int(localVolume));
			}
		} else {
			setCloudVolume(newVolume);
		}
		_waitingForUpdateVolume = false;
	}, lifetime());

	_slider->setAdjustCallback([=](float64 value) {
		for (const auto &snap : kVolumeStickedValues) {
			if (value > (snap.first - snap.second)
				&& value < (snap.first + snap.second)) {
				return snap.first;
			}
		}
		return value;
	});

	initArcsAnimation();
}

void MenuVolumeItem::initArcsAnimation() {
	const auto lastTime = lifetime().make_state<int>(0);
	_arcsAnimation.init([=](crl::time now) {
		_arcs->update(now);
		update(_speakerRect);
	});

	_arcs->startUpdateRequests(
	) | rpl::start_with_next([=] {
		if (!_arcsAnimation.animating()) {
			*lastTime = crl::now();
			_arcsAnimation.start();
		}
	}, lifetime());

	_arcs->stopUpdateRequests(
	) | rpl::start_with_next([=] {
		_arcsAnimation.stop();
	}, lifetime());
}

QColor MenuVolumeItem::unmuteColor() const {
	return (isSelected()
		? _st.itemFgOver
		: isEnabled()
		? _st.itemFg
		: _st.itemFgDisabled)->c;
}

QColor MenuVolumeItem::muteColor() const {
	return (isSelected()
		? st::attentionButtonFgOver
		: st::attentionButtonFg)->c;
}

void MenuVolumeItem::setCloudVolume(int volume) {
	if (_cloudVolume == volume) {
		return;
	}
	_cloudVolume = volume;
	if (!_slider->isChanging()) {
		setSliderVolume(_cloudMuted ? 0. : volume);
	}
}

void MenuVolumeItem::setSliderVolume(int volume) {
	const auto value = float64(volume) / _maxVolume;
	_slider->setValue(value);
	updateSliderColor(value);
}

void MenuVolumeItem::updateSliderColor(float64 value) {
	value = std::clamp(value, 0., 1.);
	const auto color = [](int rgb) {
		return QColor(
			int((rgb & 0xFF0000) >> 16),
			int((rgb & 0x00FF00) >> 8),
			int(rgb & 0x0000FF));
	};
	const auto colors = std::array<QColor, 4>{ {
		color(0xF66464),
		color(0xD0B738),
		color(0x24CD80),
		color(0x3BBCEC),
	} };
	_slider->setActiveFgOverride((value < 0.25)
		? anim::color(colors[0], colors[1], value / 0.25)
		: (value < 0.5)
		? anim::color(colors[1], colors[2], (value - 0.25) / 0.25)
		: anim::color(colors[2], colors[3], (value - 0.5) / 0.5));
}

not_null<QAction*> MenuVolumeItem::action() const {
	return _dummyAction;
}

bool MenuVolumeItem::isEnabled() const {
	return true;
}

int MenuVolumeItem::contentHeight() const {
	return st::groupCallMenuVolumePadding.top()
		+ st::groupCallMenuVolumePadding.bottom()
		+ _stCross.icon.height();
}

rpl::producer<bool> MenuVolumeItem::toggleMuteRequests() const {
	return _toggleMuteRequests.events();
}

rpl::producer<bool> MenuVolumeItem::toggleMuteLocallyRequests() const {
	return _toggleMuteLocallyRequests.events();
}

rpl::producer<int> MenuVolumeItem::changeVolumeRequests() const {
	return _changeVolumeRequests.events();
}

rpl::producer<int> MenuVolumeItem::changeVolumeLocallyRequests() const {
	return _changeVolumeLocallyRequests.events();
}

} // namespace Calls
