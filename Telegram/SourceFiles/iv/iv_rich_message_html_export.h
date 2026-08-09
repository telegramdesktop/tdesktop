/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "data/data_msg_id.h"
#include "iv/iv_rich_page.h"

class DocumentData;
class HistoryInner;
class HistoryItem;
class PhotoData;

namespace Data {
class PhotoMedia;
} // namespace Data

namespace HistoryView {
class ListWidget;
struct SelectedItem;
} // namespace HistoryView

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Iv {

class RichMessageHtmlExport final : public base::has_weak_ptr {
public:
	RichMessageHtmlExport(
		not_null<HistoryItem*> item,
		std::shared_ptr<const RichPage> page,
		const QString &basePath,
		base::weak_ptr<Window::SessionController> controller,
		Fn<void()> finished);
	~RichMessageHtmlExport();

	void start();

	[[nodiscard]] not_null<Main::Session*> session() const;
	[[nodiscard]] bool settled() const {
		return _settled;
	}
	[[nodiscard]] bool exporting(FullMsgId itemId) const {
		return !_settled && (_itemId == itemId);
	}

private:
	struct MediaJob {
		PhotoData *photo = nullptr;
		DocumentData *document = nullptr;
		std::shared_ptr<::Data::PhotoMedia> photoMedia;
		QString relative;
		int64 total = 0;
		bool copying = false;
		bool done = false;
		bool failed = false;
	};

	void collectJobs();
	void collectFromBlocks(const std::vector<RichPage::Block> &blocks);
	void collectFromText(const TextWithEntities &text);
	void addPhotoJob(PhotoData *photo, uint64 photoId);
	void addDocumentJob(
		DocumentData *document,
		uint64 documentId,
		const QString &fallbackPrefix);
	[[nodiscard]] QString reserveMediaName(uint64 id, const QString &name);
	[[nodiscard]] bool chooseFolder();
	void registerLoading();
	void startJobs();
	void startDocumentJob(MediaJob &job);
	void finishCopy(const QString &relative, bool ok);
	void checkJobs();
	void updateProgress();
	void finalize();
	void fail();
	void cancelFromManager();
	void stopJobs();
	void cleanupFiles();
	void showDoneToast();
	void showFailToast();
	void notifyFinished();

	const not_null<Main::Session*> _session;
	const FullMsgId _itemId;
	const std::shared_ptr<const RichPage> _page;
	const QString _basePath;
	const base::weak_ptr<Window::SessionController> _controller;
	const Fn<void()> _finished;

	QString _title;
	QString _folder;
	QString _htmlPath;
	std::vector<MediaJob> _jobs;
	base::flat_map<uint64, QString> _photoPaths;
	base::flat_map<uint64, QString> _documentPaths;
	base::flat_set<QString> _usedNames;
	HistoryItem *_fakeItem = nullptr;
	DocumentData *_fakeDocument = nullptr;
	base::Timer _timer;
	rpl::lifetime _downloadLifetime;
	bool _registered = false;
	bool _settled = false;

};

[[nodiscard]] QByteArray RichBlocksClipboardHtml(
	const RichPageBlocksSlice &slice,
	Main::Session *session = nullptr);

void SetRichBlocksClipboard(
	const TextForMimeData &text,
	RichPageBlocksSlice slice,
	Main::Session *session = nullptr);

void AddSaveRichMessageHtmlAction(
	not_null<Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> controller,
	const std::vector<HistoryView::SelectedItem> &selectedItems,
	not_null<HistoryView::ListWidget*> list);
void AddSaveRichMessageHtmlAction(
	not_null<Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> controller,
	const std::vector<not_null<HistoryItem*>> &items,
	not_null<HistoryInner*> list);

} // namespace Iv
