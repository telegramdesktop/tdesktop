/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Media::Stories {

class Controller;

struct SliderData {
	int index = 0;
	int total = 0;

	friend inline auto operator<=>(SliderData, SliderData) = default;
	friend inline bool operator==(SliderData, SliderData) = default;
};

class Slider final {
public:
	explicit Slider(not_null<Controller*> controller);
	~Slider();

	void show(SliderData data);

private:
	const not_null<Controller*> _controller;

	std::unique_ptr<Ui::RpWidget> _widget;

	SliderData _data;

};

} // namespace Media::Stories
