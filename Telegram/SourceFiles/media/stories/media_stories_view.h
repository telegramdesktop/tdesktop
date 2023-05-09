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

struct SiblingView {
	QImage image;
	QRect geometry;

	[[nodiscard]] bool valid() const {
		return !image.isNull();
	}
	explicit operator bool() const {
		return valid();
	}
};

class View final {
public:
	explicit View(not_null<Delegate*> delegate);
	~View();

	void show(
		const std::vector<Data::StoriesList> &lists,
		int index,
		int subindex);
	void ready();

	[[nodiscard]] QRect contentGeometry() const;
	[[nodiscard]] rpl::producer<QRect> contentGeometryValue() const;
	[[nodiscard]] SiblingView siblingLeft() const;
	[[nodiscard]] SiblingView siblingRight() const;

	void updatePlayback(const Player::TrackState &state);

	[[nodiscard]] bool subjumpAvailable(int delta) const;
	[[nodiscard]] bool subjumpFor(int delta) const;
	[[nodiscard]] bool jumpFor(int delta) const;

	[[nodiscard]] bool paused() const;
	void togglePaused(bool paused);

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	const std::unique_ptr<Controller> _controller;

};

} // namespace Media::Stories
