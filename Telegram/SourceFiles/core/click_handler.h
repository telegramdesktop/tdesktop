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

class ClickHandler;
using ClickHandlerPtr = QSharedPointer<ClickHandler>;

class ClickHandlerHost {
protected:

	virtual void clickHandlerActiveChanged(const ClickHandlerPtr &action, bool active) {
	}
	virtual void clickHandlerPressedChanged(const ClickHandlerPtr &action, bool pressed) {
	}
	virtual ~ClickHandlerHost() = 0;
	friend class ClickHandler;

};

class ClickHandler {
public:

	virtual void onClick(Qt::MouseButton) const = 0;

	virtual QString tooltip() const {
		return QString();
	}
	virtual void copyToClipboard() const {
	}
	virtual QString copyToClipboardContextItem() const {
		return QString();
	}
	virtual QString text() const {
		return QString();
	}
	virtual QString dragText() const {
		return text();
	}

	virtual ~ClickHandler() {
	}

	// this method should be called on mouse over a click handler
	// it returns true if something was changed or false otherwise
	static bool setActive(const ClickHandlerPtr &p, ClickHandlerHost *host = nullptr);

	// this method should be called when mouse leaves the host
	// it returns true if something was changed or false otherwise
	static bool clearActive(ClickHandlerHost *host = nullptr) {
		if (host && _activeHost != host) {
			return false;
		}
		return setActive(ClickHandlerPtr(), host);
	}

	// this method should be called on mouse pressed
	static void pressed() {
		unpressed();
		if (!_active || !*_active) {
			return;
		}
		_pressed.makeIfNull();
		*_pressed = *_active;
		if ((_pressedHost = _activeHost)) {
			_pressedHost->clickHandlerPressedChanged(*_pressed, true);
		}
	}

	// this method should be called on mouse released
	// the activated click handler is returned
	static ClickHandlerPtr unpressed() {
		if (_pressed && *_pressed) {
			bool activated = (_active && *_active == *_pressed);
			ClickHandlerPtr waspressed = *_pressed;
			(*_pressed).clear();
			if (_pressedHost) {
				_pressedHost->clickHandlerPressedChanged(waspressed, false);
				_pressedHost = nullptr;
			}

			if (activated) {
				return *_active;
			} else if (_active && *_active && _activeHost) {
				// emit clickHandlerActiveChanged for current active
				// click handler, which we didn't emit while we has
				// a pressed click handler
				_activeHost->clickHandlerActiveChanged(*_active, true);
			}
		}
		return ClickHandlerPtr();
	}

	static ClickHandlerPtr getActive() {
		return _active ? *_active : ClickHandlerPtr();
	}
	static ClickHandlerPtr getPressed() {
		return _pressed ? *_pressed : ClickHandlerPtr();
	}

	static bool showAsActive(const ClickHandlerPtr &p) {
		if (!p || !_active || p != *_active) {
			return false;
		}
		return !_pressed || !*_pressed || (p == *_pressed);
	}
	static bool showAsPressed(const ClickHandlerPtr &p) {
		if (!p || !_active || p != *_active) {
			return false;
		}
		return _pressed && (p == *_pressed);
	}
	static void hostDestroyed(ClickHandlerHost *host) {
		if (_activeHost == host) {
			_activeHost = nullptr;
		}
		if (_pressedHost == host) {
			_pressedHost = nullptr;
		}
	}

private:

	static NeverFreedPointer<ClickHandlerPtr> _active;
	static NeverFreedPointer<ClickHandlerPtr> _pressed;
	static ClickHandlerHost *_activeHost;
	static ClickHandlerHost *_pressedHost;

};

class LeftButtonClickHandler : public ClickHandler {
public:
	void onClick(Qt::MouseButton button) const override final {
		if (button != Qt::LeftButton) return;
		onClickImpl();
	}

protected:
	virtual void onClickImpl() const = 0;

};
