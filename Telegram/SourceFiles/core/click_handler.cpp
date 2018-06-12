/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
