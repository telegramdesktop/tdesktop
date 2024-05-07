/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class RpWidget;
} // namespace Ui

class Image;
class HistoryItem;

namespace HistoryView {

class MediaEditSpoilerManager final {
public:
	MediaEditSpoilerManager();

	void showMenu(
		not_null<Ui::RpWidget*> parent,
		not_null<HistoryItem*> item,
		Fn<void(bool)> callback);

	[[nodiscard]] Image *mediaPreview(not_null<HistoryItem*> item);

	void setSpoilerOverride(std::optional<bool> spoilerOverride);

	std::optional<bool> spoilerOverride() const;

private:
	std::optional<bool> _spoilerOverride;

};

} // namespace HistoryView
