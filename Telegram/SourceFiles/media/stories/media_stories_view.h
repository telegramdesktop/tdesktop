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

namespace Media::Stories {

class Delegate;
class Controller;

class View final {
public:
	explicit View(not_null<Delegate*> delegate);
	~View();

	void show(const Data::StoriesList &list, int index);
	[[nodiscard]] QRect contentGeometry() const;

private:
	const std::unique_ptr<Controller> _controller;

};

} // namespace Media::Stories
