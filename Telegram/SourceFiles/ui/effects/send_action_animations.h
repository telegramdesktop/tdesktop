/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
			std::unique_ptr<Impl> (*creator)();
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
	std::unique_ptr<Impl> createByType(Type type);

	std::unique_ptr<Impl> _impl;

};

} // namespace Ui
