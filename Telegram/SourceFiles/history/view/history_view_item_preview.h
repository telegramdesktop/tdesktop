/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace HistoryView {

struct ItemPreviewImage {
	QImage data;
	uint64 cacheKey = 0;

	explicit operator bool() const {
		return !data.isNull();
	}
};

struct ItemPreview {
	TextWithEntities text;
	std::vector<ItemPreviewImage> images;
	int imagesInTextPosition = 0;
	std::any loadingContext;
};

struct ToPreviewOptions {
	const std::vector<ItemPreviewImage> *existing = nullptr;
	bool hideSender = false;
	bool hideCaption = false;
	bool generateImages = true;
	bool ignoreGroup = false;
	bool ignoreSpoilers = false;
};

} // namespace HistoryView
