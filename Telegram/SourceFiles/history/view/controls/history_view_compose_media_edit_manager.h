/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_common.h"
#include "base/unique_qptr.h"
#include "history/view/controls/history_view_video_cover_uploader.h"

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

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
class Painter;

namespace HistoryView {

class MediaEditManager final {
public:
	MediaEditManager();

	void start(
		not_null<HistoryItem*> item,
		std::optional<bool> spoilered = {},
		std::optional<bool> invertCaption = {});
	void apply(
		SendMenu::Action action,
		std::shared_ptr<ChatHelpers::Show> show = nullptr);
	void cancel();

	void showMenu(
		not_null<Ui::RpWidget*> parent,
		Fn<void()> finished,
		bool hasCaptionText,
		std::shared_ptr<ChatHelpers::Show> show = nullptr);

	[[nodiscard]] Image *mediaPreview();
	void paintCoverUpload(Painter &p, QRect to);

	[[nodiscard]] bool spoilered() const;
	[[nodiscard]] bool invertCaption() const;
	[[nodiscard]] Api::VideoCoverEdit videoCover();
	[[nodiscard]] bool videoCoverUploading() const;

	[[nodiscard]] SendMenu::Details sendMenuDetails(
		bool hasCaptionText) const;

	[[nodiscard]] rpl::producer<> updateRequests() const;

	[[nodiscard]] explicit operator bool() const {
		return _item != nullptr;
	}

	[[nodiscard]] static bool CanBeSpoilered(not_null<HistoryItem*> item);
	[[nodiscard]] static bool CanEditVideoCover(not_null<HistoryItem*> item);

private:
	void editVideoCover(std::shared_ptr<ChatHelpers::Show> show);
	void removeVideoCover();
	void resetCover();

	base::unique_qptr<Ui::PopupMenu> _menu;
	HistoryItem *_item = nullptr;
	bool _spoilered = false;
	bool _invertCaption = false;
	bool _coverCleared = false;

	VideoCoverUploader _coverUploader;
	rpl::lifetime _coverClearedLifetime;

	rpl::event_stream<> _updateRequests;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
