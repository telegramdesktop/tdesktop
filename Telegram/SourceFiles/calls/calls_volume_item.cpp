/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_volume_item.h"

#include "calls/calls_group_common.h"
#include "ui/widgets/continuous_sliders.h"
#include "styles/style_calls.h"
#include "styles/style_media_player.h"

namespace Calls {
namespace {

constexpr auto kMaxVolumePercent = 200;

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
	st::mediaPlayerPanelPlayback))
, _dummyAction(new QAction(parent))
, _font(st.itemStyle.font) {

	initResizeHook(parent->sizeValue());
	enableMouseSelecting();

	paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		Painter p(this);

		const auto enabled = isEnabled();
		const auto selected = isSelected();
		p.fillRect(clip, selected ? st.itemBgOver : st.itemBg);
		p.setPen(_localMuted
			? (selected ? st::attentionButtonFgOver : st::attentionButtonFg)
			: selected
			? st.itemFgOver
			: (enabled ? st.itemFg : st.itemFgDisabled));
		p.setFont(_font);
		const auto volume = std::round(_slider->value() * kMaxVolumePercent);
		p.drawText(QPoint(0, _font->ascent), u"%1%"_q.arg(volume));
	}, lifetime());

	setCloudVolume(startVolume);

	_slider->setChangeProgressCallback([=](float64 value) {
		const auto newMuted = (value == 0);
		if (_localMuted != newMuted) {
			_localMuted = newMuted;
			_toggleMuteLocallyRequests.fire_copy(newMuted);
		}
		if (value > 0) {
			_changeVolumeLocallyRequests.fire(value * _maxVolume);
		}
	});

	const auto returnVolume = [=] {
		_changeVolumeLocallyRequests.fire_copy(_cloudVolume);
		crl::on_main(_slider.get(), [=] {
			setSliderVolume(_cloudVolume);
		});
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
				_changeVolumeRequests.fire(localVolume);
			}
		} else {
			setCloudVolume(newVolume);
		}
		_waitingForUpdateVolume = false;
	}, lifetime());

	_slider->setGeometry(rect());
}

void MenuVolumeItem::setCloudVolume(int volume) {
	if (_cloudVolume == volume) {
		return;
	}
	_cloudVolume = volume;
	if (!_slider->isChanging()) {
		setSliderVolume(volume);
	}
}

void MenuVolumeItem::setSliderVolume(int volume) {
	_slider->setValue(float64(volume) / _maxVolume);
}

not_null<QAction*> MenuVolumeItem::action() const {
	return _dummyAction;
}

bool MenuVolumeItem::isEnabled() const {
	return true;
}

int MenuVolumeItem::contentHeight() const {
	return st::groupCallMenuVolumeItemHeight;
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
