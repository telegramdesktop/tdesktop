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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

namespace Ui {

class SendActionAnimation {
public:
	using Type = SendAction::Type;

	void start(Type type);
	void stop();

	int width() const {
		return _impl ? _impl->width() : 0;
	}
	void paint(Painter &p, style::color color, int x, int y, int outerWidth, TimeMs ms) {
		if (_impl) {
			_impl->paint(p, color, x, y, outerWidth, ms);
		}
	}

	explicit operator bool() const {
		return _impl != nullptr;
	}

	class Impl {
	public:
		using Type = SendAction::Type;

		Impl(int period) : _period(period), _started(getms()) {
		}

		struct MetaData {
			int index;
			std_::unique_ptr<Impl> (*creator)();
		};
		virtual const MetaData *metaData() const = 0;
		bool supports(Type type) const;

		virtual int width() const = 0;
		void paint(Painter &p, style::color color, int x, int y, int outerWidth, TimeMs ms) {
			paintFrame(p, color, x, y, outerWidth, qMax(ms - _started, 0LL) % _period);
		}

		virtual ~Impl() = default;

	private:
		virtual void paintFrame(Painter &p, style::color color, int x, int y, int outerWidth, int frameMs) = 0;

		int _period = 1;
		TimeMs _started = 0;

	};

	~SendActionAnimation();

private:
	std_::unique_ptr<Impl> createByType(Type type);

	std_::unique_ptr<Impl> _impl;

};

} // namespace Ui
