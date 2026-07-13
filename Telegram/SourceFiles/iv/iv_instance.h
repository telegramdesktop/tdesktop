/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_msg_id.h"
#include "iv/iv_delegate.h"

#include <vector>

namespace Main {
class Session;
class SessionShow;
} // namespace Main

class HistoryItem;

namespace Window {
class SessionController;
} // namespace Window

namespace Iv::Markdown {
class Controller;
} // namespace Iv::Markdown

namespace Iv {

class Data;
struct RichPage;
class Shown;
class TonSite;

class Instance final {
public:
	using RichMessageResolved = Fn<void(std::shared_ptr<const RichPage>)>;

	explicit Instance(not_null<Delegate*> delegate);
	~Instance();

	void show(
		not_null<Window::SessionController*> controller,
		not_null<Data*> data,
		QString hash);
	void show(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Data*> data,
		QString hash);
	void show(
		not_null<Main::Session*> session,
		not_null<Data*> data,
		QString hash);

	void openWithIvPreferred(
		not_null<Window::SessionController*> controller,
		QString uri,
		QVariant context = {});
	void openWithIvPreferred(
		not_null<Main::Session*> session,
		QString uri,
		QVariant context = {});

	void showTonSite(
		const QString &uri,
		QVariant context = {});

	void showRichMessage(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		QString initialFragment = QString());
	void resolveRichMessage(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		RichMessageResolved done);

	bool showMarkdown(
		const QString &path,
		QVariant context = {});

	[[nodiscard]] bool hasActiveWindow(
		not_null<Main::Session*> session) const;

	bool closeActive();
	bool minimizeActive();

	void closeAll();

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	struct FullResult {
		crl::time lastRequestedAt = 0;
		WebPageData *page = nullptr;
		int32 hash = 0;
	};
	struct ProcessReceivedPageResult {
		WebPageData *page = nullptr;
		bool articleChanged = false;
	};
	struct RichMessageGeneration {
		std::shared_ptr<const RichPage> inlinePage;
		std::shared_ptr<const RichPage> fullPage;
		uint64 fullPageVersion = 0;
	};
	struct RichMessageRequest {
		crl::time lastRequestedAt = 0;
		RichMessageGeneration generation;
		uint64 token = 0;
		mtpRequestId requestId = 0;
		std::vector<RichMessageResolved> callbacks;
	};
	struct ProcessReceivedRichMessageResult {
		std::shared_ptr<const RichPage> page;
		bool notifyCallbacks = true;
	};

	[[nodiscard]] static RichMessageGeneration CaptureRichMessageGeneration(
		not_null<HistoryItem*> item);
	[[nodiscard]] static bool MatchesRichMessageGeneration(
		not_null<HistoryItem*> item,
		const RichMessageGeneration &generation);

	void processOpenChannel(const QString &context);
	void processJoinChannel(const QString &context);
	void showOpenedPage(
		not_null<Main::Session*> session,
		not_null<Data*> data,
		QString hash,
		bool requestFullOnOpen);
	void primeFullRequest(
		not_null<Main::Session*> session,
		not_null<Data*> data);
	void requestFull(not_null<Main::Session*> session, const QString &id);
	void showRichMessage(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		std::shared_ptr<const RichPage> page,
		QString initialFragment = QString());
	void finishRichMessageRequest(
		not_null<Main::Session*> session,
		FullMsgId itemId,
		uint64 token,
		std::shared_ptr<const RichPage> page,
		bool notifyCallbacks = true);

	void trackSession(not_null<Main::Session*> session);
	void bindMarkdown(
		const QString &key,
		not_null<Main::Session*> session,
		FullMsgId itemId);
	void closeMarkdownsForItem(
		not_null<Main::Session*> session,
		FullMsgId itemId);
	void closeMarkdownsForSession(not_null<Main::Session*> session);
	void closeSessionDataViews(not_null<Main::Session*> session);

	ProcessReceivedPageResult processReceivedPage(
		not_null<Main::Session*> session,
		const QString &url,
		const MTPmessages_WebPage &result);
	ProcessReceivedRichMessageResult processReceivedRichMessage(
		not_null<Main::Session*> session,
		FullMsgId itemId,
		const RichMessageGeneration &generation,
		const MTPmessages_Messages &result);

	const not_null<Delegate*> _delegate;

	std::unique_ptr<Shown> _shown;
	Main::Session *_shownSession = nullptr;
	base::flat_set<not_null<Main::Session*>> _tracking;
	base::flat_map<
		not_null<Main::Session*>,
		base::flat_set<not_null<ChannelData*>>> _joining;
	base::flat_map<
		not_null<Main::Session*>,
		base::flat_map<QString, FullResult>> _fullRequested;
	base::flat_map<
		not_null<Main::Session*>,
		base::flat_map<FullMsgId, RichMessageRequest>> _richMessageRequested;
	uint64 _nextRichMessageRequestToken = 0;

	base::flat_map<
		not_null<Main::Session*>,
		base::flat_map<QString, WebPageData*>> _ivCache;
	Main::Session *_ivRequestSession = nullptr;
	QString _ivRequestUri;
	mtpRequestId _ivRequestId = 0;

	std::unique_ptr<TonSite> _tonSite;

	struct MarkdownBinding {
		Main::Session *session = nullptr;
		FullMsgId itemId;
	};

	base::flat_map<
		QString,
		std::unique_ptr<Markdown::Controller>> _markdowns;
	base::flat_map<QString, MarkdownBinding> _markdownBindings;

	rpl::lifetime _lifetime;

};

[[nodiscard]] bool PreferForUri(const QString &uri);

} // namespace Iv
