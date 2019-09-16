/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/click_handler.h"

#include "base/algorithm.h"
#include "ui/text/text_entity.h"

#include <QtCore/QPointer>

namespace {

ClickHandlerPtr &ClickHandlerActive() {
	static auto result = ClickHandlerPtr();
	return result;
}

ClickHandlerPtr &ClickHandlerPressed() {
	static auto result = ClickHandlerPtr();
	return result;
}

} // namespace

ClickHandlerHost *ClickHandler::_activeHost = nullptr;
ClickHandlerHost *ClickHandler::_pressedHost = nullptr;

ClickHandlerHost::~ClickHandlerHost() {
	ClickHandler::hostDestroyed(this);
}

bool ClickHandler::setActive(
		const ClickHandlerPtr &p,
		ClickHandlerHost *host) {
	auto &active = ClickHandlerActive();
	auto &pressed = ClickHandlerPressed();

	if (active == p) {
		return false;
	}

	// emit clickHandlerActiveChanged only when there is no
	// other pressed click handler currently, if there is
	// this method will be called when it is unpressed
	if (active) {
		const auto emitClickHandlerActiveChanged = false
			|| !pressed
			|| (pressed == active);
		const auto wasactive = base::take(active);
		if (_activeHost) {
			if (emitClickHandlerActiveChanged) {
				_activeHost->clickHandlerActiveChanged(wasactive, false);
			}
			_activeHost = nullptr;
		}
	}
	if (p) {
		active = p;
		if ((_activeHost = host)) {
			bool emitClickHandlerActiveChanged = (!pressed || pressed == active);
			if (emitClickHandlerActiveChanged) {
				_activeHost->clickHandlerActiveChanged(active, true);
			}
		}
	}
	return true;
}

bool ClickHandler::clearActive(ClickHandlerHost *host) {
	if (host && _activeHost != host) {
		return false;
	}
	return setActive(ClickHandlerPtr(), host);
}

void ClickHandler::pressed() {
	auto &active = ClickHandlerActive();
	auto &pressed = ClickHandlerPressed();

	unpressed();
	if (!active) {
		return;
	}
	pressed = active;
	if ((_pressedHost = _activeHost)) {
		_pressedHost->clickHandlerPressedChanged(pressed, true);
	}
}

ClickHandlerPtr ClickHandler::unpressed() {
	auto &active = ClickHandlerActive();
	auto &pressed = ClickHandlerPressed();

	if (pressed) {
		const auto activated = (active == pressed);
		const auto waspressed = base::take(pressed);
		if (_pressedHost) {
			_pressedHost->clickHandlerPressedChanged(waspressed, false);
			_pressedHost = nullptr;
		}

		if (activated) {
			return active;
		} else if (active && _activeHost) {
			// emit clickHandlerActiveChanged for current active
			// click handler, which we didn't emit while we has
			// a pressed click handler
			_activeHost->clickHandlerActiveChanged(active, true);
		}
	}
	return ClickHandlerPtr();
}

ClickHandlerPtr ClickHandler::getActive() {
	return ClickHandlerActive();
}

ClickHandlerPtr ClickHandler::getPressed() {
	return ClickHandlerPressed();
}

bool ClickHandler::showAsActive(const ClickHandlerPtr &p) {
	auto &active = ClickHandlerActive();
	auto &pressed = ClickHandlerPressed();

	return p && (p == active) && (!pressed || (p == pressed));
}

bool ClickHandler::showAsPressed(const ClickHandlerPtr &p) {
	auto &active = ClickHandlerActive();
	auto &pressed = ClickHandlerPressed();

	return p && (p == active) && (p == pressed);
}

void ClickHandler::hostDestroyed(ClickHandlerHost *host) {
	auto &active = ClickHandlerActive();
	auto &pressed = ClickHandlerPressed();

	if (_activeHost == host) {
		active = nullptr;
		_activeHost = nullptr;
	}
	if (_pressedHost == host) {
		pressed = nullptr;
		_pressedHost = nullptr;
	}
}

auto ClickHandler::getTextEntity() const -> TextEntity {
	return { EntityType::Invalid };
}

void ActivateClickHandler(
		not_null<QWidget*> guard,
		ClickHandlerPtr handler,
		ClickContext context) {
	crl::on_main(guard, [=] {
		handler->onClick(context);
	});
}

void ActivateClickHandler(
		not_null<QWidget*> guard,
		ClickHandlerPtr handler,
		Qt::MouseButton button) {
	ActivateClickHandler(guard, handler, ClickContext{ button });
}
