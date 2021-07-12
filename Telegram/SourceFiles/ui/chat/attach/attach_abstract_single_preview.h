/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui {

class AbstractSinglePreview : public RpWidget {
public:
	using RpWidget::RpWidget;

	[[nodiscard]] virtual rpl::producer<> deleteRequests() const = 0;
	[[nodiscard]] virtual rpl::producer<> editRequests() const = 0;
	[[nodiscard]] virtual rpl::producer<> modifyRequests() const = 0;

};

} // namespace Ui
