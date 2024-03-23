/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Iv {

class Delegate {
public:
	virtual void ivSetLastSourceWindow(not_null<QWidget*> window) = 0;
	[[nodiscard]] virtual QRect ivGeometry() const = 0;
	virtual void ivSaveGeometry(not_null<QWidget*> window) = 0;
};

} // namespace Iv
