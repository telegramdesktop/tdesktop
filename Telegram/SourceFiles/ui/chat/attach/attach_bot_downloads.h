/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Ui::BotWebView {

struct DownloadsProgress {
	uint64 ready = 0;
	uint64 total : 63 = 0;
	uint64 loading : 1 = 0;

	friend inline bool operator==(
		const DownloadsProgress &a,
		const DownloadsProgress &b) = default;
};

struct DownloadsEntry {
	uint32 id = 0;
	QString url;
	QString path;
	uint64 ready : 63 = 0;
	uint64 loading : 1 = 0;
	uint64 total : 63 = 0;
	uint64 failed : 1 = 0;
};

enum class DownloadsAction {
	Open,
	Retry,
	Cancel,
};

[[nodiscard]] auto FillAttachBotDownloadsSubmenu(
	rpl::producer<std::vector<DownloadsEntry>> content,
	Fn<void(uint32, DownloadsAction)> callback)
-> FnMut<void(not_null<PopupMenu*>)>;

} // namespace Ui::BotWebView
