/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/streaming/media_streaming_common.h"

class DocumentData;

namespace Ui {
struct RadialState;
} // namespace Ui

namespace Data {
struct FileOrigin;
} // namespace Data

namespace Media {
namespace Streaming {

class Document;
class Player;

class Instance {
public:
	Instance(
		std::shared_ptr<Document> shared,
		Fn<void()> waitingCallback);
	Instance(
		not_null<DocumentData*> document,
		Data::FileOrigin origin,
		Fn<void()> waitingCallback);
	Instance(
		not_null<PhotoData*> photo,
		Data::FileOrigin origin,
		Fn<void()> waitingCallback);
	~Instance();

	[[nodiscard]] bool valid() const;
	[[nodiscard]] std::shared_ptr<Document> shared() const;

	[[nodiscard]] const Player &player() const;
	[[nodiscard]] const Information &info() const;

	void play(const PlaybackOptions &options);
	void pause();
	void resume();
	void stop();
	void stopAudio();
	void saveFrameToCover();

	[[nodiscard]] bool active() const;
	[[nodiscard]] bool ready() const;
	[[nodiscard]] std::optional<Error> failed() const;

	[[nodiscard]] bool paused() const;

	[[nodiscard]] float64 speed() const;
	void setSpeed(float64 speed); // 0.5 <= speed <= 2.

	[[nodiscard]] bool waitingShown() const;
	[[nodiscard]] float64 waitingOpacity() const;
	[[nodiscard]] Ui::RadialState waitingState() const;

	void callWaitingCallback();

	[[nodiscard]] QImage frame(const FrameRequest &request) const;
	[[nodiscard]] FrameWithInfo frameWithInfo() const;
	bool markFrameShown() const;

	void lockPlayer();
	void unlockPlayer();
	[[nodiscard]] bool playerLocked() const;

	void setPriority(int priority);
	[[nodiscard]] int priority() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	const std::shared_ptr<Document> _shared;
	Fn<void()> _waitingCallback;
	int _priority = 1;
	bool _playerLocked = false;
	rpl::lifetime _lifetime;

};

} // namespace Streaming
} // namespace Media
