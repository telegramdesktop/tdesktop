/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QRect>

namespace Ui {

// LayoutMediaGroup without the per-item sides, so that targets which do
// not link lib_ui, like td_export, can use the same album geometry.
[[nodiscard]] std::vector<QRect> LayoutMediaGroupGeometry(
	const std::vector<QSize> &sizes,
	int maxWidth,
	int minWidth,
	int spacing);

} // namespace Ui
