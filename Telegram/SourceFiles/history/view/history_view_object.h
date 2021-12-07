/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace HistoryView {

class Object {
public:
	Object() = default;
	Object(const Object &other) = delete;
	Object &operator=(const Object &other) = delete;

	void initDimensions() {
		setOptimalSize(countOptimalSize());
	}
	int resizeGetHeight(int newWidth) {
		setCurrentSize(countCurrentSize(newWidth));
		return _height;
	}

	[[nodiscard]] QSize optimalSize() const {
		return { _maxWidth, _minHeight };
	}
	[[nodiscard]] QSize currentSize() const {
		return { _width, _height };
	}

	[[nodiscard]] int maxWidth() const {
		return _maxWidth;
	}
	[[nodiscard]] int minHeight() const {
		return _minHeight;
	}
	[[nodiscard]] int width() const {
		return _width;
	}
	[[nodiscard]] int height() const {
		return _height;
	}

	virtual ~Object() = default;

protected:
	void setOptimalSize(QSize size) {
		_maxWidth = size.width();
		_minHeight = size.height();
	}
	void setCurrentSize(QSize size) {
		_width = size.width();
		_height = size.height();
	}

private:
	virtual QSize countOptimalSize() = 0;
	virtual QSize countCurrentSize(int newWidth) = 0;

	int _maxWidth = 0;
	int _minHeight = 0;
	int _width = 0;
	int _height = 0;

};

} // namespace HistoryView
