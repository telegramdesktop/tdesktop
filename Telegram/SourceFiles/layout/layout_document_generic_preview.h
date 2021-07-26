/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Layout {

struct DocumentGenericPreview final {
	static DocumentGenericPreview Create(DocumentData *document);
	const style::icon *icon() const;
	const int index;
	const style::color &color;
	const style::color &dark;
	const style::color &over;
	const style::color &selected;
	const QString ext;
};

// Ui::CachedRoundCorners DocumentCorners(int colorIndex);

} // namespace Layout
