/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/streaming/media_streaming_player.h"
#include "ui/effects/radial_animation.h"
#include "ui/effects/animations.h"
#include "base/timer.h"

class DocumentData;

namespace Data {
struct FileOrigin;
} // namespace Data

namespace Media {
namespace Streaming {

class Instance {
public:
	void setWaitingCallback(Fn<void()> callback);

	void callWaitingCallback();

private:
	Fn<void()> _waitingCallback;

};

class Document {
public:
	Document(
		not_null<DocumentData*> document,
		std::shared_ptr<Reader> reader);

	[[nodiscard]] const Player &player() const;
	[[nodiscard]] const Information &info() const;

	void play(const PlaybackOptions &options);
	void pause();
	void resume();
	void stop();
	void saveFrameToCover();

	[[nodiscard]] bool active() const;
	[[nodiscard]] bool ready() const;

	[[nodiscard]] bool paused() const;

	[[nodiscard]] float64 speed() const;
	void setSpeed(float64 speed); // 0.5 <= speed <= 2.

	[[nodiscard]] not_null<Instance*> addInstance();
	void removeInstance(not_null<Instance*> instance);

	[[nodiscard]] bool waitingShown() const;
	[[nodiscard]] float64 waitingOpacity() const;
	[[nodiscard]] Ui::RadialState waitingState() const;

private:
	void waitingCallback();

	void handleUpdate(Update &&update);
	void handleError(Error &&error);

	void ready(Information &&info);
	void waitingChange(bool waiting);

	void validateGoodThumbnail();

	Player _player;
	Information _info;

	bool _waiting = false;
	mutable Ui::InfiniteRadialAnimation _radial;
	Ui::Animations::Simple _fading;
	base::Timer _timer;
	base::flat_set<std::unique_ptr<Instance>> _instances;

	not_null<DocumentData*> _document;

};


} // namespace Streaming
} // namespace Media
