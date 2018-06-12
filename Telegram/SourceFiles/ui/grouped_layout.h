/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {

struct GroupMediaLayout {
	QRect geometry;
	RectParts sides = RectPart::None;
};

std::vector<GroupMediaLayout> LayoutMediaGroup(
	const std::vector<QSize> &sizes,
	int maxWidth,
	int minWidth,
	int spacing);

RectParts GetCornersFromSides(RectParts sides);
QSize GetImageScaleSizeForGeometry(QSize original, QSize geometry);

} // namespace Ui
