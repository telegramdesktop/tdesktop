/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class webFileLoader;

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Ui::BotWebView {
struct DownloadsProgress;
} // namespace Ui::BotWebView

namespace InlineBots {

using DownloadId = uint32;

using ::Ui::BotWebView::DownloadsProgress;

struct DownloadsEntry {
	DownloadId id = 0;
	QString url;
	QString path;
	uint64 ready = 0;
	uint64 total = 0;
};

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

	void cancel(DownloadId id);

	[[nodiscard]] auto downloadsProgress(not_null<UserData*> bot)
		-> rpl::producer<DownloadsProgress>;

private:
	struct List {
		std::vector<DownloadsEntry> list;
		bool checked = false;
	};
	struct Loader {
		std::unique_ptr<webFileLoader> loader;
		PeerId botId = 0;
	};

	void read();
	void write();

	void progress(PeerId botId, DownloadId id);
	void fail(PeerId botId, DownloadId id);
	void done(PeerId botId, DownloadId id);
	void applyProgress(
		PeerId botId,
		DownloadId id,
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
