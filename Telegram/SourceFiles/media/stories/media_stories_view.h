/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
struct StoriesList;
} // namespace Data

namespace Media::Player {
struct TrackState;
} // namespace Media::Player

namespace Media::Stories {

class Delegate;
class Controller;

class View final {
public:
	explicit View(not_null<Delegate*> delegate);
	~View();

	void show(const Data::StoriesList &list, int index);
	void ready();

	[[nodiscard]] QRect contentGeometry() const;

	void updatePlayback(const Player::TrackState &state);

	[[nodiscard]] bool jumpAvailable(int delta) const;
	[[nodiscard]] bool jumpFor(int delta) const;

	[[nodiscard]] bool paused() const;
	void togglePaused(bool paused);

private:
	const std::unique_ptr<Controller> _controller;

};

} // namespace Media::Stories
