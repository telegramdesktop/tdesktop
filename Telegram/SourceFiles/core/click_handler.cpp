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
#include "core/click_handler.h"

ClickHandlerHost::~ClickHandlerHost() {
	ClickHandler::hostDestroyed(this);
}

NeverFreedPointer<ClickHandlerPtr> ClickHandler::_active;
NeverFreedPointer<ClickHandlerPtr> ClickHandler::_pressed;
ClickHandlerHost *ClickHandler::_activeHost = nullptr;
ClickHandlerHost *ClickHandler::_pressedHost = nullptr;

bool ClickHandler::setActive(const ClickHandlerPtr &p, ClickHandlerHost *host) {
	if ((_active && (*_active == p)) || (!_active && !p)) {
		return false;
	}

	// emit clickHandlerActiveChanged only when there is no
	// other pressed click handler currently, if there is
	// this method will be called when it is unpressed
	if (_active && *_active) {
		const auto emitClickHandlerActiveChanged = false
			|| !_pressed
			|| !*_pressed
			|| (*_pressed == *_active);
		const auto wasactive = base::take(*_active);
		if (_activeHost) {
			if (emitClickHandlerActiveChanged) {
				_activeHost->clickHandlerActiveChanged(wasactive, false);
			}
			_activeHost = nullptr;
		}
	}
	if (p) {
		_active.createIfNull();
		*_active = p;
		if ((_activeHost = host)) {
			bool emitClickHandlerActiveChanged = (!_pressed || !*_pressed || *_pressed == *_active);
			if (emitClickHandlerActiveChanged) {
				_activeHost->clickHandlerActiveChanged(*_active, true);
			}
		}
	}
	return true;
}

QString ClickHandler::getExpandedLinkText(ExpandLinksMode mode, const QStringRef &textPart) const {
	return QString();
}

TextWithEntities ClickHandler::getExpandedLinkTextWithEntities(ExpandLinksMode mode, int entityOffset, const QStringRef &textPart) const {
	return { QString(), EntitiesInText() };
}

TextWithEntities ClickHandler::simpleTextWithEntity(const EntityInText &entity) const {
	TextWithEntities result;
	result.entities.push_back(entity);
	return result;
}
