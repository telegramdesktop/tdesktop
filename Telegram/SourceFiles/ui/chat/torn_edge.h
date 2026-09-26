/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {

struct TornEdgeCache {
	int width = 0;
	QImage maskTop;
	QImage maskBottom;
	QImage patternCacheTop;
	QImage patternCacheBottom;
	QImage solidCacheTop;
	QImage solidCacheBottom;
	QColor solidColorTop;
	QColor solidColorBottom;
};

void ValidateTornEdges(TornEdgeCache &cache, int width);

} // namespace Ui
