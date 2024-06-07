/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"

namespace SendMenu {
struct Details;
struct Action;
} // namespace SendMenu

namespace Ui {
class RpWidget;
class PopupMenu;
} // namespace Ui

class Image;
class HistoryItem;

namespace HistoryView {

class MediaEditManager final {
public:
	MediaEditManager();

	void start(
		not_null<HistoryItem*> item,
		std::optional<bool> spoilered = {},
		std::optional<bool> invertCaption = {});
	void apply(SendMenu::Action action);
	void cancel();

	void showMenu(
		not_null<Ui::RpWidget*> parent,
		Fn<void()> finished,
		bool hasCaptionText);

	[[nodiscard]] Image *mediaPreview();

	[[nodiscard]] bool spoilered() const;
	[[nodiscard]] bool invertCaption() const;

	[[nodiscard]] SendMenu::Details sendMenuDetails(
		bool hasCaptionText) const;

	[[nodiscard]] explicit operator bool() const {
		return _item != nullptr;
	}

	[[nodiscard]] static bool CanBeSpoilered(not_null<HistoryItem*> item);

private:
	base::unique_qptr<Ui::PopupMenu> _menu;
	HistoryItem *_item = nullptr;
	bool _spoilered = false;
	bool _invertCaption = false;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
