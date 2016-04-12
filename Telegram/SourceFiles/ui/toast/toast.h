/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

namespace Ui {
namespace Toast {

namespace internal {
	class Manager;
	class Widget;
} // namespace internal

static constexpr const int DefaultDuration = 1500;
struct Config {
	QString text;
	int durationMs = DefaultDuration;
};
void Show(QWidget *parent, const Config &config);

class Instance {
	struct Private {
	};

public:

	Instance(const Config &config, QWidget *widgetParent, const Private &);
	Instance(const Instance &other) = delete;
	Instance &operator=(const Instance &other) = delete;

	void fadeOut();
	void hide();

private:
	void step_fade(float64 ms, bool timer);
	bool _fadingOut = false;
	Animation _a_fade;

	const uint64 _hideAtMs;

	// ToastManager should reset _widget pointer if _widget is destroyed.
	friend class internal::Manager;
	friend void Show(QWidget *parent, const Config &config);
	std_::unique_ptr<internal::Widget> _widget;

};

} // namespace Toast
} // namespace Ui
