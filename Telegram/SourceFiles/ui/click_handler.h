/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QVariant>

class ClickHandler;
using ClickHandlerPtr = std::shared_ptr<ClickHandler>;

struct ClickContext {
	Qt::MouseButton button = Qt::LeftButton;
	QVariant other;
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

enum class EntityType;
class ClickHandler {
public:
	virtual ~ClickHandler() {
	}

	virtual void onClick(ClickContext context) const = 0;

	// What text to show in a tooltip when mouse is over that click handler as a link in Text.
	virtual QString tooltip() const {
		return QString();
	}

	// What to drop in the input fields when dragging that click handler as a link from Text.
	virtual QString dragText() const {
		return QString();
	}

	// Copy to clipboard support.
	virtual QString copyToClipboardText() const {
		return QString();
	}
	virtual QString copyToClipboardContextItemText() const {
		return QString();
	}

	// Entities in text support.
	struct TextEntity {
		EntityType type = EntityType();
		QString data;
	};
	virtual TextEntity getTextEntity() const;

	// This method should be called on mouse over a click handler.
	// It returns true if the active handler was changed or false otherwise.
	static bool setActive(const ClickHandlerPtr &p, ClickHandlerHost *host = nullptr);

	// This method should be called when mouse leaves the host.
	// It returns true if the active handler was changed or false otherwise.
	static bool clearActive(ClickHandlerHost *host = nullptr);

	// This method should be called on mouse press event.
	static void pressed();

	// This method should be called on mouse release event.
	// The activated click handler (if any) is returned.
	static ClickHandlerPtr unpressed();

	static ClickHandlerPtr getActive();
	static ClickHandlerPtr getPressed();

	static bool showAsActive(const ClickHandlerPtr &p);
	static bool showAsPressed(const ClickHandlerPtr &p);
	static void hostDestroyed(ClickHandlerHost *host);

private:
	static ClickHandlerHost *_activeHost;
	static ClickHandlerHost *_pressedHost;

};

class LeftButtonClickHandler : public ClickHandler {
public:
	void onClick(ClickContext context) const override final {
		if (context.button == Qt::LeftButton) {
			onClickImpl();
		}
	}

protected:
	virtual void onClickImpl() const = 0;

};

class LambdaClickHandler : public ClickHandler {
public:
	LambdaClickHandler(Fn<void()> handler) : _handler(std::move(handler)) {
	}
	void onClick(ClickContext context) const override final {
		if (context.button == Qt::LeftButton && _handler) {
			_handler();
		}
	}

private:
	Fn<void()> _handler;

};

void ActivateClickHandler(
	not_null<QWidget*> guard,
	ClickHandlerPtr handler,
	ClickContext context);
void ActivateClickHandler(
	not_null<QWidget*> guard,
	ClickHandlerPtr handler,
	Qt::MouseButton button);
