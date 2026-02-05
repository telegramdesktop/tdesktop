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

	[[nodiscard]] QRect ivGeometry(not_null<Ui::RpWindow*> window) const override;
	void ivSaveGeometry(not_null<Ui::RpWindow*> window) override;

	[[nodiscard]] int ivZoom() const override;
	[[nodiscard]] rpl::producer<int> ivZoomValue() const override;
	void ivSetZoom(int value) override;

};

} // namespace Iv
