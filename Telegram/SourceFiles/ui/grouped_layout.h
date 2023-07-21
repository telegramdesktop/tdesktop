/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/exteraGramDesktop/exteraGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "ui/rect_part.h"

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
