/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/iv_delegate.h"

namespace Iv {

class DelegateImpl final : public Delegate {
public:
	DelegateImpl() = default;

	void ivSetLastSourceWindow(not_null<QWidget*> window) override;
	[[nodiscard]] QRect ivGeometry() const override;
	void ivSaveGeometry(not_null<QWidget*> window) override;

private:
	QPointer<QWidget> _lastSourceWindow;

};

} // namespace Iv
