/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "layout/layout_utils.h"

namespace Layout {

Layout::Position IndexToPosition(int index) {
	return {
		(index >= 0) ? (index / MatrixRowShift) : -1,
		(index >= 0) ? (index % MatrixRowShift) : -1 };
}

int PositionToIndex(int row, int column) {
	return row * MatrixRowShift + column;
}

int PositionToIndex(const Layout::Position &position) {
	return PositionToIndex(position.row, position.column);
}

} // namespace Layout
