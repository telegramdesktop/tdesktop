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

namespace View {

class PlaybackProgress {
public:
	PlaybackProgress();

	void setValueChangedCallback(Fn<void(float64,float64)> callback) {
		_valueChanged = std::move(callback);
	}
	void setInLoadingStateChangedCallback(Fn<void(bool)> callback) {
		_inLoadingStateChanged = std::move(callback);
	}
	void setValue(float64 value, bool animated);
	float64 value() const;
	float64 value(crl::time ms);

	void updateState(const Player::TrackState &state);
	void updateLoadingState(float64 progress);

private:
	void step_value(float64 ms, bool timer);
	void step_receivedTill(float64 ms, bool timer);
	void setReceivedTill(float64 value);
	void emitUpdatedValue();

	// This can animate for a very long time (like in music playing),
	// so it should be a BasicAnimation, not an Animation, because
	// Animation pauses mtproto responses/updates handling while playing.
	anim::value a_value, a_receivedTill;
	BasicAnimation _a_value, _a_receivedTill;
	Fn<void(float64,float64)> _valueChanged;

	bool _inLoadingState = false;
	Fn<void(bool)> _inLoadingStateChanged;

	int64 _position = 0;
	int64 _length = 0;
	int64 _receivedTill = -1;

	bool _playing = false;

};

} // namespace View
} // namespace Media
