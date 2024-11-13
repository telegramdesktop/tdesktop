/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/chat/attach/attach_bot_webview.h"

class webFileLoader;

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class GenericBox;
} // namespace Ui

namespace InlineBots {

using DownloadId = uint32;

using ::Ui::BotWebView::DownloadsProgress;
using ::Ui::BotWebView::DownloadsEntry;
using ::Ui::BotWebView::DownloadsAction;

class Downloads final {
public:
	explicit Downloads(not_null<Main::Session*> session);
	~Downloads();

	struct StartArgs {
		not_null<UserData*> bot;
		QString url;
		QString path;
	};
	uint32 start(StartArgs &&args); // Returns download id.

	void action(
		not_null<UserData*> bot,
		DownloadId id,
		DownloadsAction type);

	[[nodiscard]] rpl::producer<DownloadsProgress> progress(
		not_null<UserData*> bot);
	[[nodiscard]] const std::vector<DownloadsEntry> &list(
		not_null<UserData*> bot,
		bool check = false);

private:
	struct List {
		std::vector<DownloadsEntry> list;
	};
	struct Loader {
		std::unique_ptr<webFileLoader> loader;
		PeerId botId = 0;
	};

	void read();
	void write();

	void load(
		PeerId botId,
		DownloadId id,
		DownloadsEntry &entry);
	void progress(PeerId botId, DownloadId id);
	void fail(PeerId botId, DownloadId id, bool cancel = false);
	void done(PeerId botId, DownloadId id);
	void applyProgress(
		PeerId botId,
		DownloadId id,
		int64 total,
		int64 ready);
	void applyProgress(
		PeerId botId,
		DownloadsEntry &entry,
		int64 total,
		int64 ready);

	const not_null<Main::Session*> _session;

	base::flat_map<PeerId, List> _lists;
	base::flat_map<DownloadId, Loader> _loaders;

	base::flat_map<
		PeerId,
		rpl::variable<DownloadsProgress>> _progressView;

	DownloadId _autoIncrementId = 0;

};

struct DownloadBoxArgs {
	not_null<Main::Session*> session;
	QString bot;
	QString name;
	QString url;
	Fn<void(QString)> done;
};
void DownloadFileBox(not_null<Ui::GenericBox*> box, DownloadBoxArgs args);

} // namespace InlineBots
