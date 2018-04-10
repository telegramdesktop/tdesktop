/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/continuous_sliders.h"

namespace Media {
namespace Player {
struct TrackState;
} // namespace Player

namespace Clip {

class Playback {
public:
	Playback();

	void setValueChangedCallback(base::lambda<void(float64)> callback) {
		_valueChanged = std::move(callback);
	}
	void setInLoadingStateChangedCallback(base::lambda<void(bool)> callback) {
		_inLoadingStateChanged = std::move(callback);
	}
	void setValue(float64 value, bool animated);
	float64 value() const;
	float64 value(TimeMs ms);

	void updateState(const Player::TrackState &state);
	void updateLoadingState(float64 progress);

private:
	void step_value(float64 ms, bool timer);

	// This can animate for a very long time (like in music playing),
	// so it should be a BasicAnimation, not an Animation.
	anim::value a_value;
	BasicAnimation _a_value;
	base::lambda<void(float64)> _valueChanged;

	bool _inLoadingState = false;
	base::lambda<void(bool)> _inLoadingStateChanged;

	int64 _position = 0;
	int64 _length = 0;

	bool _playing = false;

};

} // namespace Clip
} // namespace Media
