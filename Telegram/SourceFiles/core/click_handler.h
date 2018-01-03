/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class ClickHandler;
using ClickHandlerPtr = std::shared_ptr<ClickHandler>;

enum ExpandLinksMode {
	ExpandLinksNone,
	ExpandLinksShortened,
	ExpandLinksAll,
	ExpandLinksUrlOnly, // For custom urls leaves only url instead of text.
};

class ClickHandlerHost {
protected:
	virtual void clickHandlerActiveChanged(const ClickHandlerPtr &action, bool active) {
	}
	virtual void clickHandlerPressedChanged(const ClickHandlerPtr &action, bool pressed) {
	}
	virtual ~ClickHandlerHost() = 0;
	friend class ClickHandler;

};

class EntityInText;
struct TextWithEntities;
class ClickHandler {
public:
	virtual ~ClickHandler() {
	}

	virtual void onClick(Qt::MouseButton) const = 0;

	// What text to show in a tooltip when mouse is over that click handler as a link in Text.
	virtual QString tooltip() const {
		return QString();
	}

	// What to drop in the input fields when dragging that click handler as a link from Text.
	virtual QString dragText() const {
		return QString();
	}

	// Copy to clipboard support.
	virtual void copyToClipboard() const {
	}
	virtual QString copyToClipboardContextItemText() const {
		return QString();
	}

	// Entities in text support.

	// This method returns empty string if just textPart should be used (nothing to expand).
	virtual QString getExpandedLinkText(ExpandLinksMode mode, const QStringRef &textPart) const;
	virtual TextWithEntities getExpandedLinkTextWithEntities(ExpandLinksMode mode, int entityOffset, const QStringRef &textPart) const;

	// This method should be called on mouse over a click handler.
	// It returns true if the active handler was changed or false otherwise.
	static bool setActive(const ClickHandlerPtr &p, ClickHandlerHost *host = nullptr);

	// This method should be called when mouse leaves the host.
	// It returns true if the active handler was changed or false otherwise.
	static bool clearActive(ClickHandlerHost *host = nullptr) {
		if (host && _activeHost != host) {
			return false;
		}
		return setActive(ClickHandlerPtr(), host);
	}

	// This method should be called on mouse press event.
	static void pressed() {
		unpressed();
		if (!_active || !*_active) {
			return;
		}
		_pressed.createIfNull();
		*_pressed = *_active;
		if ((_pressedHost = _activeHost)) {
			_pressedHost->clickHandlerPressedChanged(*_pressed, true);
		}
	}

	// This method should be called on mouse release event.
	// The activated click handler (if any) is returned.
	static ClickHandlerPtr unpressed() {
		if (_pressed && *_pressed) {
			const auto activated = (_active && *_active == *_pressed);
			const auto waspressed = base::take(*_pressed);
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
			if (_active) {
				*_active = nullptr;
			}
			_activeHost = nullptr;
		}
		if (_pressedHost == host) {
			if (_pressed) {
				*_pressed = nullptr;
			}
			_pressedHost = nullptr;
		}
	}

protected:
	// For click handlers like mention or hashtag in getExpandedLinkTextWithEntities()
	// we return just an empty string ("use original string part") with single entity.
	TextWithEntities simpleTextWithEntity(const EntityInText &entity) const;

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

class LambdaClickHandler : public ClickHandler {
public:
	LambdaClickHandler(base::lambda<void()> handler) : _handler(std::move(handler)) {
	}
	void onClick(Qt::MouseButton button) const override final {
		if (button == Qt::LeftButton && _handler) {
			_handler();
		}
	}

private:
	base::lambda<void()> _handler;

};
