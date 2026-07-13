/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/iv_instance.h"

#include "apiwrap.h"
#include "base/platform/base_platform_info.h"
#include "base/unixtime.h"
#include "boxes/share_box.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "core/shortcuts.h"
#include "core/click_handler_types.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "history/history.h"
#include "history/history_item.h"
#include "iv/markdown/iv_markdown_article.h"
#include "iv/markdown/iv_markdown_controller.h"
#include "iv/iv_cached_media.h"
#include "iv/iv_controller.h"
#include "iv/iv_data.h"
#include "iv/iv_rich_page.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/session/session_show.h"
#include "media/view/media_view_open_common.h"
#include "storage/storage_account.h"
#include "ui/toast/toast.h"
#include "ui/basic_click_handlers.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "window/window_session_controller_link_info.h"

#include "styles/style_iv.h"

#include <QtCore/QByteArray>
#include <QtCore/QFileInfo>

#include <optional>

namespace Iv {
namespace {

constexpr auto kAllowPageReloadAfter = 3 * crl::time(1000);

struct NativeIvChannelContext {
	uint64 channelId = 0;
	QString username;
};

[[nodiscard]] NativeIvChannelContext ParseNativeIvChannelContext(
		const QString &context) {
	const auto separator = context.indexOf(u'\n');
	return {
		.channelId = (separator >= 0)
			? context.mid(0, separator).toULongLong()
			: context.toULongLong(),
		.username = (separator >= 0) ? context.mid(separator + 1) : QString(),
	};
}

[[nodiscard]] QString ResolveNativeIvChannelUsername(
		const QString &channelUsername,
		const QString &contextUsername) {
	return !channelUsername.isEmpty() ? channelUsername : contextUsername;
}

struct MarkdownMessageContext {
	ClickHandlerContext clickHandlerContext;
	base::weak_ptr<Window::SessionController> sessionWindow;
};

struct LocalMarkdownTarget {
	QString key;
	QString path;
	QString sourceName;
	QString fragment;
};

[[nodiscard]] QString NormalizeLocalMarkdownFragment(QString fragment) {
	fragment = QString::fromUtf8(
		QByteArray::fromPercentEncoding(fragment.toUtf8()));
	fragment = fragment.trimmed().toLower();
	while (fragment.startsWith(QChar('#'))) {
		fragment.remove(0, 1);
	}
	return fragment;
}

[[nodiscard]] LocalMarkdownTarget ParseLocalMarkdownTarget(QString path) {
	auto sourcePath = path;
	auto fragment = QString();
	if (!QFileInfo(sourcePath).exists()) {
		const auto hash = sourcePath.lastIndexOf(QChar('#'));
		const auto candidate = (hash > 0) ? sourcePath.mid(0, hash) : QString();
		if (!candidate.isEmpty() && QFileInfo(candidate).exists()) {
			fragment = NormalizeLocalMarkdownFragment(sourcePath.mid(hash + 1));
			sourcePath = candidate;
		}
	}
	const auto info = QFileInfo(sourcePath);
	if (!info.exists()) {
		return {
			.key = path,
			.path = std::move(path),
		};
	}
	auto result = LocalMarkdownTarget{
		.key = info.absoluteFilePath(),
		.path = info.absoluteFilePath(),
		.sourceName = info.fileName(),
		.fragment = std::move(fragment),
	};
	if (!result.fragment.isEmpty()) {
		result.path += u"#"_q + result.fragment;
	}
	return result;
}

[[nodiscard]] auto ExtractMarkdownMessageContext(const QVariant &context) {
	if (!context.isValid() || !context.canConvert<ClickHandlerContext>()) {
		return std::optional<MarkdownMessageContext>();
	}
	const auto clickHandlerContext = context.value<ClickHandlerContext>();
	return std::make_optional(MarkdownMessageContext{
		.clickHandlerContext = clickHandlerContext,
		.sessionWindow = clickHandlerContext.sessionWindow,
	});
}

[[nodiscard]] Main::Session *ResolveMarkdownSession(
		const MarkdownMessageContext &context) {
	if (const auto controller = context.sessionWindow.get()) {
		return &controller->session();
	}
	return nullptr;
}

[[nodiscard]] HistoryItem *ResolveMarkdownItem(
		const MarkdownMessageContext &context) {
	const auto session = ResolveMarkdownSession(context);
	const auto itemId = context.clickHandlerContext.itemId;
	return (session && itemId) ? session->data().message(itemId) : nullptr;
}

[[nodiscard]] bool CanShareMarkdownItem(not_null<HistoryItem*> item) {
	const auto peer = item->history()->peer;
	return peer->allowsForwarding() && !item->forbidsForward();
}

[[nodiscard]] QString RichMessageKey(FullMsgId itemId) {
	return u"rich-message:%1:%2"_q
		.arg(itemId.peer.value)
		.arg(itemId.msg.bare);
}

[[nodiscard]] uint64 RichMessagePageId(FullMsgId itemId) {
	return uint64(itemId.peer.value) ^ (uint64(itemId.msg.bare) << 1);
}

[[nodiscard]] bool PreparedContentHasAnchor(
		const Markdown::MarkdownArticleContent &content,
		const QString &anchorId) {
	if (anchorId.isEmpty()) {
		return false;
	}
	auto article = Markdown::MarkdownArticle(st::defaultMarkdown);
	article.setContent(content);
	const auto width = article.maxWidth();
	static_cast<void>(article.resizeGetHeight(width));
	auto top = article.anchorTop(anchorId);
	if (top < 0) {
		const auto expansion = article.expandDetailsToAnchor(anchorId);
		if (expansion.changed) {
			static_cast<void>(article.resizeGetHeight(width));
		}
		top = article.anchorTop(anchorId);
	}
	return (top >= 0);
}

void OpenRichMessageChannel(
		not_null<Main::Session*> session,
		const QString &context) {
	const auto parsed = ParseNativeIvChannelContext(context);
	if (const auto channelId = ChannelId(parsed.channelId)) {
		const auto channel = session->data().channel(channelId);
		if (channel->isLoaded()) {
			if (const auto controller = session->tryResolveWindow(channel)) {
				controller->showPeerHistory(channel);
			}
		} else if (const auto username = ResolveNativeIvChannelUsername(
				channel->username(),
				parsed.username); !username.isEmpty()) {
			if (const auto controller = session->tryResolveWindow(channel)) {
				controller->showPeerByLink({
					.usernameOrId = username,
				});
			}
		}
	}
}

void JoinRichMessageChannel(
		not_null<Main::Session*> session,
		const QString &context) {
	const auto parsed = ParseNativeIvChannelContext(context);
	if (const auto channelId = ChannelId(parsed.channelId)) {
		const auto channel = session->data().channel(channelId);
		if (channel->isLoaded()) {
			session->api().joinChannel(channel);
		} else if (const auto username = ResolveNativeIvChannelUsername(
				channel->username(),
				parsed.username); !username.isEmpty()) {
			if (const auto controller = session->tryResolveWindow(channel)) {
				controller->showPeerByLink({
					.usernameOrId = username,
					.joinChannel = true,
				});
			}
		}
	}
}

[[nodiscard]] bool ActivateRichMessageMedia(
		const Markdown::MediaActivation &activation,
		Qt::MouseButton button,
		const QVariant &clickHandlerContext) {
	if (button != Qt::LeftButton && button != Qt::MiddleButton) {
		return false;
	}
	switch (activation.kind) {
	case Markdown::MediaActivationKind::None:
		return false;
	case Markdown::MediaActivationKind::ExternalUrl:
		if (activation.url.isEmpty()) {
			return false;
		}
		HiddenUrlClickHandler::Open(activation.url, clickHandlerContext);
		return true;
	case Markdown::MediaActivationKind::Embed:
		return false;
	case Markdown::MediaActivationKind::Photo:
		if (!activation.photo) {
			return false;
		}
		activation.photo->open(button);
		return true;
	case Markdown::MediaActivationKind::Document:
		if (!activation.document) {
			return false;
		}
		activation.document->open(button);
		return true;
	case Markdown::MediaActivationKind::OpenChannel:
		if (!activation.channel) {
			return false;
		}
		activation.channel->open(button);
		return true;
	case Markdown::MediaActivationKind::JoinChannel:
		if (!activation.channel) {
			return false;
		}
		activation.channel->join(button);
		return true;
	}
	return false;
}

[[nodiscard]] Markdown::OpenOptions PrepareLocalMarkdownOptions(
		QVariant context) {
	auto options = Markdown::OpenOptions{
		.viewerKind = Markdown::ViewerKind::LocalFile,
		.clickHandlerContext = std::move(context),
	};
	const auto messageContext = ExtractMarkdownMessageContext(
		options.clickHandlerContext);
	const auto item = messageContext
		? ResolveMarkdownItem(*messageContext)
		: nullptr;
	if (item && CanShareMarkdownItem(not_null{ item })) {
		options.share = [context = *messageContext](
				std::shared_ptr<Ui::Show> show) {
			const auto session = ResolveMarkdownSession(context);
			const auto itemId = context.clickHandlerContext.itemId;
			const auto current = (session && itemId)
				? session->data().message(itemId)
				: nullptr;
			if (!show || !current || !CanShareMarkdownItem(not_null{ current })) {
				return;
			}
			FastShareMessage(
				Main::MakeSessionShow(show, not_null{ session }),
				not_null{ current });
		};
	}
	return options;
}

} // namespace

class Shown final : public base::has_weak_ptr {
public:
	Shown(
		not_null<Delegate*> delegate,
		not_null<Main::Session*> session,
		not_null<Data*> data,
		QString hash,
		Fn<void(QString)> openChannel,
		Fn<void(QString)> joinChannel);

	[[nodiscard]] bool showing(
		not_null<Main::Session*> session,
		not_null<Data*> data) const;
	[[nodiscard]] bool showingFrom(not_null<Main::Session*> session) const;
	[[nodiscard]] bool activeFor(not_null<Main::Session*> session) const;
	[[nodiscard]] bool active() const;

	void moveTo(not_null<Data*> data, QString hash);
	void update(not_null<Data*> data);

	void showJoinedTooltip();
	void minimize();

	[[nodiscard]] rpl::producer<Controller::Event> events() const {
		return _events.events();
	}

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	void prepare(not_null<Data*> data, const QString &hash);
	void createMarkdownController(
		Markdown::MarkdownArticleContent content,
		QString title,
		QString initialFragment,
		not_null<WebPageData*> page);
	[[nodiscard]] Markdown::OpenOptions markdownOpenOptions(
		QString initialFragment,
		not_null<WebPageData*> page);

	void showMarkdownWindowed(
		Markdown::MarkdownArticleContent content,
		QString title,
		QString initialFragment,
		not_null<WebPageData*> page,
		bool refresh);
	[[nodiscard]] std::shared_ptr<Markdown::MediaRuntime> createMediaRuntime(
		not_null<WebPageData*> page);
	[[nodiscard]] bool activateMarkdownMedia(
		const Markdown::MediaActivation &activation,
		Qt::MouseButton button,
		const QVariant &clickHandlerContext) const;

	const not_null<Delegate*> _delegate;
	const not_null<Main::Session*> _session;
	const Fn<void(QString)> _openChannel;
	const Fn<void(QString)> _joinChannel;
	QString _id;
	std::unique_ptr<Markdown::Controller> _markdownController;
	std::shared_ptr<Markdown::MediaRuntime> _markdownMediaRuntime;
	QString _markdownRuntimeUrl;

	rpl::event_stream<Controller::Event> _events;

	rpl::lifetime _lifetime;

};

class TonSite final : public base::has_weak_ptr {
public:
	TonSite(not_null<Delegate*> delegate, QString uri);

	[[nodiscard]] bool active() const;

	void moveTo(QString uri);

	void minimize();

	[[nodiscard]] rpl::producer<Controller::Event> events() const {
		return _events.events();
	}

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	void createController();

	void showWindowed();

	const not_null<Delegate*> _delegate;
	QString _uri;
	std::unique_ptr<Controller> _controller;

	rpl::event_stream<Controller::Event> _events;

	rpl::lifetime _lifetime;

};

Shown::Shown(
	not_null<Delegate*> delegate,
	not_null<Main::Session*> session,
	not_null<Data*> data,
	QString hash,
	Fn<void(QString)> openChannel,
	Fn<void(QString)> joinChannel)
: _delegate(delegate)
, _session(session)
, _openChannel(std::move(openChannel))
, _joinChannel(std::move(joinChannel)) {
	prepare(data, hash);
}

void Shown::prepare(not_null<Data*> data, const QString &hash) {
	const auto richPage = data->richPage();
	const auto id = data->id();
	const auto page = _session->data().webpage(data->pageId());

	if (_markdownRuntimeUrl != page->url) {
		_markdownMediaRuntime = nullptr;
		_markdownRuntimeUrl = QString();
	}
	_id = id;

	auto prepared = Markdown::TryPrepareNativeInstantView({
		.richPage = richPage,
		.mediaRuntime = createMediaRuntime(page),
	});
	showMarkdownWindowed(
		std::move(prepared.content),
		data->name(),
		hash,
		page,
		false);
}

Markdown::OpenOptions Shown::markdownOpenOptions(
		QString initialFragment,
		not_null<WebPageData*> page) {
	const auto clickHandlerContext = std::make_shared<QVariant>();
	auto options = Markdown::OpenOptions{
		.sourceName = page->displayedSiteName(),
		.sourceUrl = page->url,
		.initialFragment = std::move(initialFragment),
		.currentPageId = page->id,
		.viewerKind = Markdown::ViewerKind::InstantView,
		.clickHandlerContextRef = clickHandlerContext,
		.ivWebviewStorageId = _session->local().resolveStorageIdOther(),
		.activateMedia = [=](
				const Markdown::MediaActivation &activation,
				Qt::MouseButton button) {
			return activateMarkdownMedia(activation, button, *clickHandlerContext);
		},
		.downloadTaskFinished = page->session().downloaderTaskFinished(),
	};
	if (!page->url.isEmpty()) {
		options.share = [=, url = page->url](std::shared_ptr<Ui::Show> show) {
			if (!show) {
				return;
			}
			FastShareLink(Main::MakeSessionShow(show, _session), url);
		};
	}
	return options;
}

void Shown::createMarkdownController(
		Markdown::MarkdownArticleContent content,
		QString title,
		QString initialFragment,
		not_null<WebPageData*> page) {
	Expects(!_markdownController);

	auto options = markdownOpenOptions(std::move(initialFragment), page);
	_markdownController = std::make_unique<Markdown::Controller>(
		_delegate,
		std::move(content),
		std::move(title),
		nullptr,
		std::move(options));
	_markdownController->events() | rpl::on_next([=](Markdown::Event event) {
		using FromType = Markdown::Event::Type;
		using ToType = Controller::Event::Type;
		switch (event.type) {
		case FromType::Close:
			_events.fire({ .type = ToType::Close });
			break;
		case FromType::Quit:
			_events.fire({ .type = ToType::Quit });
			break;
		case FromType::OpenPage:
			_events.fire({
				.type = ToType::OpenPage,
				.url = event.url,
				.context = QString::number(event.webpageId),
				.webpageId = event.webpageId,
			});
			break;
		case FromType::OpenFile:
			break;
		case FromType::Report:
			_events.fire({
				.type = ToType::Report,
				.context = QString::number(event.webpageId),
			});
			break;
		}
	}, _markdownController->lifetime());
}

void Shown::showMarkdownWindowed(
		Markdown::MarkdownArticleContent content,
		QString title,
		QString initialFragment,
		not_null<WebPageData*> page,
		bool refresh) {
	if (!_markdownController) {
		createMarkdownController(
			std::move(content),
			std::move(title),
			std::move(initialFragment),
			page);
		_markdownController->activate();
		return;
	}
	auto options = markdownOpenOptions(std::move(initialFragment), page);
	if (refresh) {
		_markdownController->update(
			std::move(content),
			std::move(title),
			std::move(options));
	} else {
		_markdownController->show(
			std::move(content),
			std::move(title),
			std::move(options));
	}
}

std::shared_ptr<Markdown::MediaRuntime> Shown::createMediaRuntime(
		not_null<WebPageData*> page) {
	if (_markdownMediaRuntime && (page->url == _markdownRuntimeUrl)) {
		return _markdownMediaRuntime;
	}
	_markdownRuntimeUrl = page->url;
	_markdownMediaRuntime = CreateCachedPageMediaRuntime(
		_session,
		page,
		_openChannel,
		_joinChannel);
	return _markdownMediaRuntime;
}

bool Shown::activateMarkdownMedia(
		const Markdown::MediaActivation &activation,
		Qt::MouseButton button,
		const QVariant &clickHandlerContext) const {
	if (button != Qt::LeftButton && button != Qt::MiddleButton) {
		return false;
	}
	switch (activation.kind) {
	case Markdown::MediaActivationKind::None:
		return false;
	case Markdown::MediaActivationKind::ExternalUrl:
		if (activation.url.isEmpty()) {
			return false;
		}
		HiddenUrlClickHandler::Open(activation.url, clickHandlerContext);
		return true;
	case Markdown::MediaActivationKind::Embed:
		return false;
	case Markdown::MediaActivationKind::Photo:
		if (!activation.photo) {
			return false;
		}
		activation.photo->open(button);
		return true;
	case Markdown::MediaActivationKind::Document:
		if (!activation.document) {
			return false;
		}
		activation.document->open(button);
		return true;
	case Markdown::MediaActivationKind::OpenChannel:
		if (!activation.channel) {
			return false;
		}
		activation.channel->open(button);
		return true;
	case Markdown::MediaActivationKind::JoinChannel:
		if (!activation.channel) {
			return false;
		}
		activation.channel->join(button);
		return true;
	}
	return false;
}

bool Shown::showing(
		not_null<Main::Session*> session,
		not_null<Data*> data) const {
	return showingFrom(session) && (_id == data->id());
}

bool Shown::showingFrom(not_null<Main::Session*> session) const {
	return (_session == session);
}

bool Shown::activeFor(not_null<Main::Session*> session) const {
	return showingFrom(session) && _markdownController;
}

bool Shown::active() const {
	return _markdownController && _markdownController->active();
}

void Shown::moveTo(not_null<Data*> data, QString hash) {
	prepare(data, hash);
}

void Shown::update(not_null<Data*> data) {
	const auto richPage = data->richPage();
	const auto id = data->id();
	const auto page = _session->data().webpage(data->pageId());

	if (_markdownRuntimeUrl != page->url) {
		_markdownMediaRuntime = nullptr;
		_markdownRuntimeUrl = QString();
	}
	_id = id;

	auto prepared = Markdown::TryPrepareNativeInstantView({
		.richPage = richPage,
		.mediaRuntime = createMediaRuntime(page),
	});
	showMarkdownWindowed(
		std::move(prepared.content),
		data->name(),
		QString(),
		page,
		true);
}

void Shown::showJoinedTooltip() {
	if (_markdownController) {
		_markdownController->showJoinedTooltip();
	}
}

void Shown::minimize() {
	if (_markdownController) {
		_markdownController->minimize();
	}
}

TonSite::TonSite(not_null<Delegate*> delegate, QString uri)
: _delegate(delegate)
, _uri(uri) {
	showWindowed();
}

void TonSite::createController() {
	Expects(!_controller);

	_controller = std::make_unique<Controller>(_delegate);

	_controller->events(
	) | rpl::start_to_stream(_events, _controller->lifetime());
}

void TonSite::showWindowed() {
	if (!_controller) {
		createController();
	}

	_controller->showTonSite(Storage::TonSiteStorageId(), _uri);
}

bool TonSite::active() const {
	return _controller && _controller->active();
}

void TonSite::moveTo(QString uri) {
	_controller->showTonSite({}, uri);
}

void TonSite::minimize() {
	if (_controller) {
		_controller->minimize();
	}
}

Instance::Instance(not_null<Delegate*> delegate) : _delegate(delegate) {
}

Instance::~Instance() = default;

void Instance::show(
		not_null<Window::SessionController*> controller,
		not_null<Data*> data,
		QString hash) {
	showOpenedPage(&controller->session(), data, std::move(hash), true);
}

void Instance::show(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Data*> data,
		QString hash) {
	showOpenedPage(&show->session(), data, std::move(hash), true);
}

void Instance::show(
		not_null<Main::Session*> session,
		not_null<Data*> data,
		QString hash) {
	showOpenedPage(session, data, std::move(hash), true);
}

void Instance::showOpenedPage(
		not_null<Main::Session*> session,
		not_null<Data*> data,
		QString hash,
		bool requestFullOnOpen) {
	if (Platform::IsMac()) {
		// Otherwise IV is not visible under the media viewer.
		Core::App().hideMediaView();
	}

	if (Core::App().settings().normalizeIvZoom()) {
		Core::App().saveSettingsDelayed();
	}

	primeFullRequest(session, data);
	const auto guard = gsl::finally([&] {
		if (requestFullOnOpen) {
			requestFull(session, data->id());
		}
	});
	if (_shown && _shownSession == session) {
		_shown->moveTo(data, hash);
		return;
	}
	_shown = std::make_unique<Shown>(
		_delegate,
		session,
		data,
		hash,
		[=](QString context) {
			processOpenChannel(context);
		},
		[=](QString context) {
			processJoinChannel(context);
		});
	_shownSession = session;
	_shown->events() | rpl::on_next([=](Controller::Event event) {
		using Type = Controller::Event::Type;
		const auto lower = event.url.toLower();
		const auto urlChecked = lower.startsWith("http://")
			|| lower.startsWith("https://");
		const auto tonsite = lower.startsWith("tonsite://");
		switch (event.type) {
		case Type::Close:
			_shown = nullptr;
			break;
		case Type::Quit:
			Shortcuts::Launch(Shortcuts::Command::Quit);
			break;
		case Type::OpenChannel:
			processOpenChannel(event.context);
			break;
		case Type::JoinChannel:
			processJoinChannel(event.context);
			break;
		case Type::OpenLinkExternal:
			if (urlChecked) {
				File::OpenUrl(event.url);
				closeAll();
			} else if (tonsite) {
				showTonSite(event.url);
			}
			break;
		case Type::OpenMedia:
			if (const auto window = Core::App().activeWindow()) {
				const auto current = window->sessionController();
				const auto controller = (current
					&& &current->session() == _shownSession)
					? current
					: nullptr;
				const auto item = (HistoryItem*)nullptr;
				const auto topicRootId = MsgId(0);
				const auto monoforumPeerId = PeerId(0);
				if (event.context.startsWith("-photo")) {
					const auto id = event.context.mid(6).toULongLong();
					const auto photo = _shownSession->data().photo(id);
					if (!photo->isNull()) {
						window->openInMediaView({
							controller,
							photo,
							item,
							topicRootId,
							monoforumPeerId
						});
					}
				} else if (event.context.startsWith("-video")) {
					const auto id = event.context.mid(6).toULongLong();
					const auto video = _shownSession->data().document(id);
					if (!video->isNull()) {
						window->openInMediaView({
							controller,
							video,
							item,
							topicRootId,
							monoforumPeerId
						});
					}
				}
			}
			break;
		case Type::OpenPage:
		case Type::OpenLink: {
			if (tonsite) {
				showTonSite(event.url);
				break;
			} else if (!urlChecked) {
				break;
			}
			const auto session = _shownSession;
			const auto url = event.url;
			const auto parts = event.url.split('#');
			const auto hash = (parts.size() > 1) ? parts[1] : u""_q;
			if (event.webpageId) {
				const auto page = session->data().webpage(
					WebPageId(event.webpageId)).get();
				if (page->iv) {
					this->showOpenedPage(session, page->iv.get(), hash, false);
					break;
				}
			}
			const auto requestKey = event.webpageId
				? QString::number(event.webpageId)
				: url;
			auto &requested = _fullRequested[session][requestKey];
			if (event.webpageId) {
				const auto page = session->data().webpage(
					WebPageId(event.webpageId)).get();
				if (page->iv) {
					requested.page = page;
					requested.hash = page->iv->hash();
				} else {
					requested.hash = 0;
				}
			}
			requested.lastRequestedAt = crl::now();
			session->api().request(MTPmessages_GetWebPage(
				MTP_string(url),
				MTP_int(requested.hash)
			)).done([=](const MTPmessages_WebPage &result) {
				const auto processed = processReceivedPage(
					session,
					requestKey,
					result);
				if (const auto page = processed.page; page && page->iv) {
					if (event.webpageId && page->id != event.webpageId) {
						const auto expected = session->data().webpage(
							WebPageId(event.webpageId)).get();
						if (expected->iv) {
							this->showOpenedPage(
								session,
								expected->iv.get(),
								hash,
								false);
						} else {
							UrlClickHandler::Open(event.url);
						}
						return;
					}
					this->showOpenedPage(session, page->iv.get(), hash, false);
				} else {
					UrlClickHandler::Open(event.url);
				}
			}).fail([=] {
				UrlClickHandler::Open(event.url);
			}).send();
		} break;
		case Type::Report:
			if (const auto controller = _shownSession->tryResolveWindow()) {
				controller->window().activate();
				controller->showPeerByLink(Window::PeerByLinkInfo{
					.usernameOrId = "previews",
					.resolveType = Window::ResolveType::BotStart,
					.startToken = ("webpage"
						+ QString::number(event.context.toULongLong())),
				});
			}
			break;
		}
	}, _shown->lifetime());

	session->changes().peerUpdates(
		::Data::PeerUpdate::Flag::ChannelAmIn
	) | rpl::on_next([=](const ::Data::PeerUpdate &update) {
		if (const auto channel = update.peer->asChannel()) {
			if (channel->amIn()) {
				const auto i = _joining.find(session);
				const auto value = not_null{ channel };
				if (i != end(_joining) && i->second.remove(value)) {
					_shown->showJoinedTooltip();
				}
			}
		}
	}, _shown->lifetime());

	trackSession(session);
}

void Instance::primeFullRequest(
		not_null<Main::Session*> session,
		not_null<Data*> data) {
	auto &requested = _fullRequested[session][data->id()];
	requested.page = session->data().webpage(data->pageId()).get();
	requested.hash = data->hash();
}

void Instance::trackSession(not_null<Main::Session*> session) {
	if (!_tracking.emplace(session).second) {
		return;
	}
	session->data().sessionDataAboutToBeCleared(
	) | rpl::on_next([=] {
		closeSessionDataViews(session);
	}, session->lifetime());
	session->data().itemRemoved(
	) | rpl::on_next([=](not_null<const HistoryItem*> item) {
		closeMarkdownsForItem(session, item->fullId());
	}, session->lifetime());
	session->data().itemsAboutToBeDestroyed(
	) | rpl::on_next([=](std::vector<not_null<HistoryItem*>> items) {
		for (const auto &item : items) {
			closeMarkdownsForItem(session, item->fullId());
		}
	}, session->lifetime());
	session->lifetime().add([=] {
		closeSessionDataViews(session);
		_tracking.remove(session);
		_joining.remove(session);
		_fullRequested.remove(session);
		if (const auto i = _richMessageRequested.find(session)
			; i != end(_richMessageRequested)) {
			for (const auto &[itemId, requested] : i->second) {
				if (requested.requestId) {
					session->api().request(requested.requestId).cancel();
				}
			}
			_richMessageRequested.erase(i);
		}
		_ivCache.remove(session);
		if (_ivRequestSession == session) {
			session->api().request(_ivRequestId).cancel();
			_ivRequestSession = nullptr;
			_ivRequestUri = QString();
			_ivRequestId = 0;
		}
	});
}

void Instance::bindMarkdown(
		const QString &key,
		not_null<Main::Session*> session,
		FullMsgId itemId) {
	_markdownBindings[key] = {
		.session = session.get(),
		.itemId = itemId,
	};
	trackSession(session);
}

void Instance::closeMarkdownsForItem(
		not_null<Main::Session*> session,
		FullMsgId itemId) {
	if (!itemId) {
		return;
	}
	auto keys = std::vector<QString>();
	for (const auto &[key, binding] : _markdownBindings) {
		if (binding.session == session.get() && binding.itemId == itemId) {
			keys.push_back(key);
		}
	}
	for (const auto &key : keys) {
		_markdownBindings.remove(key);
		_markdowns.take(key);
	}
}

void Instance::closeMarkdownsForSession(not_null<Main::Session*> session) {
	auto keys = std::vector<QString>();
	for (const auto &[key, binding] : _markdownBindings) {
		if (binding.session == session.get()) {
			keys.push_back(key);
		}
	}
	for (const auto &key : keys) {
		_markdownBindings.remove(key);
		_markdowns.take(key);
	}
}

void Instance::closeSessionDataViews(not_null<Main::Session*> session) {
	closeMarkdownsForSession(session);
	if (_shownSession == session) {
		_shownSession = nullptr;
	}
	if (_shown && _shown->showingFrom(session)) {
		_shown = nullptr;
	}
}

void Instance::openWithIvPreferred(
		not_null<Window::SessionController*> controller,
		QString uri,
		QVariant context) {
	auto my = context.value<ClickHandlerContext>();
	my.sessionWindow = controller;
	openWithIvPreferred(
		&controller->session(),
		uri,
		QVariant::fromValue(my));
}

void Instance::openWithIvPreferred(
		not_null<Main::Session*> session,
		QString uri,
		QVariant context) {
	const auto openExternal = [=] {
		auto my = context.value<ClickHandlerContext>();
		my.ignoreIv = true;
		const auto updated = QVariant::fromValue(my);
		if (my.forceExternalUrlConfirmation) {
			HiddenUrlClickHandler::Open(uri, updated);
		} else {
			UrlClickHandler::Open(uri, updated);
		}
	};
	const auto parts = uri.split('#');
	if (parts.isEmpty() || parts[0].isEmpty()) {
		return;
	}
	trackSession(session);
	const auto hash = (parts.size() > 1) ? parts[1] : u""_q;
	const auto url = parts[0];
	auto &cache = _ivCache[session];
	if (const auto i = cache.find(url); i != end(cache)) {
		const auto page = i->second;
		if (page && page->iv) {
			auto my = context.value<ClickHandlerContext>();
			if (const auto window = my.sessionWindow.get()) {
				show(window, page->iv.get(), hash);
			} else {
				show(session, page->iv.get(), hash);
			}
		} else {
			openExternal();
		}
		return;
	} else if (_ivRequestSession == session.get() && _ivRequestUri == uri) {
		return;
	} else if (_ivRequestId) {
		_ivRequestSession->api().request(_ivRequestId).cancel();
	}
	const auto finish = [=](WebPageData *page) {
		Expects(_ivRequestSession == session);

		_ivRequestId = 0;
		_ivRequestUri = QString();
		_ivRequestSession = nullptr;
		_ivCache[session][url] = page;
		if (page && page->iv) {
			this->showOpenedPage(session, page->iv.get(), hash, false);
		} else {
			openExternal();
		}
	};
	_ivRequestSession = session;
	_ivRequestUri = uri;
	auto &requested = _fullRequested[session][url];
	requested.lastRequestedAt = crl::now();
	_ivRequestId = session->api().request(MTPmessages_GetWebPage(
		MTP_string(url),
		MTP_int(requested.hash)
	)).done([=](const MTPmessages_WebPage &result) {
		finish(processReceivedPage(session, url, result).page);
	}).fail([=] {
		finish(nullptr);
	}).send();
}

void Instance::showTonSite(
		const QString &uri,
		QVariant context) {
	if (!Controller::IsGoodTonSiteUrl(uri)) {
		Ui::Toast::Show(tr::lng_iv_not_supported(tr::now));
		return;
	} else if (Platform::IsMac()) {
		// Otherwise IV is not visible under the media viewer.
		Core::App().hideMediaView();
	}
	if (_tonSite) {
		_tonSite->moveTo(uri);
		return;
	}
	_tonSite = std::make_unique<TonSite>(_delegate, uri);
	_tonSite->events() | rpl::on_next([=](Controller::Event event) {
		using Type = Controller::Event::Type;
		const auto lower = event.url.toLower();
		const auto urlChecked = lower.startsWith("http://")
			|| lower.startsWith("https://");
		const auto tonsite = lower.startsWith("tonsite://");
		switch (event.type) {
		case Type::Close:
			_tonSite = nullptr;
			break;
		case Type::Quit:
			Shortcuts::Launch(Shortcuts::Command::Quit);
			break;
		case Type::OpenLinkExternal:
			if (urlChecked) {
				File::OpenUrl(event.url);
				closeAll();
			} else if (tonsite) {
				showTonSite(event.url);
			}
			break;
		case Type::OpenPage:
		case Type::OpenLink:
			if (urlChecked) {
				UrlClickHandler::Open(event.url);
			} else if (tonsite) {
				showTonSite(event.url);
			}
			break;
		}
	}, _tonSite->lifetime());
}

Instance::RichMessageGeneration Instance::CaptureRichMessageGeneration(
		not_null<HistoryItem*> item) {
	auto generation = RichMessageGeneration();
	generation.inlinePage = item->richPage();
	generation.fullPage = item->fullRichPage();
	generation.fullPageVersion = item->fullRichPageVersion();
	return generation;
}

bool Instance::MatchesRichMessageGeneration(
		not_null<HistoryItem*> item,
		const RichMessageGeneration &generation) {
	return (item->richPage() == generation.inlinePage)
		&& (item->fullRichPage() == generation.fullPage)
		&& (item->fullRichPageVersion() == generation.fullPageVersion);
}

void Instance::resolveRichMessage(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		RichMessageResolved done) {
	if (const auto page = item->fullRichPage()) {
		done(page);
		return;
	}
	const auto richPage = item->richPage();
	if (richPage && !richPage->part) {
		done(richPage);
		return;
	}
	const auto session = &controller->session();
	const auto itemId = item->fullId();
	const auto generation = CaptureRichMessageGeneration(item);
	trackSession(session);
	auto &requested = _richMessageRequested[session][itemId];
	if (requested.requestId) {
		if (MatchesRichMessageGeneration(item, requested.generation)) {
			requested.callbacks.push_back(std::move(done));
			return;
		}
		session->api().request(requested.requestId).cancel();
		requested = RichMessageRequest();
	}
	requested.callbacks.push_back(std::move(done));
	const auto now = crl::now();
	if (requested.lastRequestedAt
		&& MatchesRichMessageGeneration(item, requested.generation)
		&& (now - requested.lastRequestedAt) < kAllowPageReloadAfter) {
		finishRichMessageRequest(session, itemId, requested.token, nullptr);
		return;
	}
	requested.lastRequestedAt = now;
	requested.generation = generation;
	requested.token = ++_nextRichMessageRequestToken;
	const auto token = requested.token;
	requested.requestId = session->api().request(MTPmessages_GetRichMessage(
		item->history()->peer->input(),
		MTP_int(item->id)
	)).done([=](const MTPmessages_Messages &result) {
		const auto processed = processReceivedRichMessage(
			session,
			itemId,
			generation,
			result);
		finishRichMessageRequest(
			session,
			itemId,
			token,
			processed.page,
			processed.notifyCallbacks);
	}).fail([=] {
		finishRichMessageRequest(
			session,
			itemId,
			token,
			nullptr);
	}).send();
}

void Instance::showRichMessage(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		QString initialFragment) {
	const auto weak = base::make_weak(controller);
	const auto itemId = item->fullId();
	resolveRichMessage(controller, item, [=](std::shared_ptr<const RichPage> page) {
		const auto strong = weak.get();
		const auto current = strong
			? strong->session().data().message(itemId)
			: nullptr;
		if (!page || !current) {
			if (strong && !page) {
				Ui::Toast::Show(tr::lng_iv_not_supported(tr::now));
			}
			return;
		}
		showRichMessage(
			not_null{ strong },
			not_null{ current },
			std::move(page),
			initialFragment);
	});
}

void Instance::showRichMessage(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		std::shared_ptr<const RichPage> richPage,
		QString initialFragment) {
	if (Platform::IsMac()) {
		Core::App().hideMediaView();
	}
	const auto session = &controller->session();
	const auto itemId = item->fullId();
	const auto key = RichMessageKey(itemId);
	const auto title = item->history()->peer->name();
	auto clickHandlerContext = ClickHandlerContext();
	clickHandlerContext.sessionWindow = controller;
	clickHandlerContext.itemId = itemId;
	auto context = QVariant::fromValue(clickHandlerContext);
	auto mediaRuntime = CreateMessageMediaRuntime(
		session,
		itemId,
		[=](QString context) {
			OpenRichMessageChannel(session, context);
		},
		[=](QString context) {
			JoinRichMessageChannel(session, context);
		});
	const auto richLimits = ResolveRichMessageLimits(session);
	auto prepared = Markdown::TryPrepareNativeInstantView({
		.richPage = richPage,
		.mediaRuntime = std::move(mediaRuntime),
		.tableRenderLimits = Markdown::PrepareTableRenderLimitsForRichMessage(
			richLimits),
	});
	if (!prepared.supported()) {
		Ui::Toast::Show(tr::lng_iv_not_supported(tr::now));
		return;
	}
	if (!initialFragment.isEmpty()
		&& !PreparedContentHasAnchor(prepared.content, initialFragment)) {
		controller->showToast(tr::lng_iv_not_found_in_message(tr::now));
		return;
	}
	auto options = Markdown::OpenOptions{
		.sourceName = title,
		.currentPageId = RichMessagePageId(itemId),
		.viewerKind = Markdown::ViewerKind::InstantView,
		.clickHandlerContext = context,
		.activateMedia = [=](
				const Markdown::MediaActivation &activation,
				Qt::MouseButton button) {
			return ActivateRichMessageMedia(activation, button, context);
		},
		.downloadTaskFinished = session->downloaderTaskFinished(),
	};
	options.initialFragment = std::move(initialFragment);
	if (CanShareMarkdownItem(item)) {
		options.share = [=](std::shared_ptr<Ui::Show> show) {
			const auto current = session->data().message(itemId);
			if (!show || !current || !CanShareMarkdownItem(not_null{ current })) {
				return;
			}
			FastShareMessage(
				Main::MakeSessionShow(show, not_null{ session }),
				not_null{ current });
		};
	}

	auto i = _markdowns.find(key);
	if (i == end(_markdowns)) {
		auto controller = std::make_unique<Markdown::Controller>(
			_delegate,
			std::move(prepared.content),
			title,
			nullptr,
			std::move(options));
		controller->events() | rpl::on_next([=](Markdown::Event event) {
			using Type = Markdown::Event::Type;
			switch (event.type) {
			case Type::Close:
				_markdownBindings.remove(key);
				_markdowns.take(key);
				break;
			case Type::Quit:
				Shortcuts::Launch(Shortcuts::Command::Quit);
				break;
			case Type::OpenPage:
			case Type::OpenFile:
				if (!event.url.isEmpty()) {
					UrlClickHandler::Open(event.url, event.context);
				}
				break;
			case Type::Report:
				break;
			}
		}, controller->lifetime());
		i = _markdowns.emplace(key, std::move(controller)).first;
	} else {
		i->second->show(
			std::move(prepared.content),
			title,
			std::move(options));
	}
	bindMarkdown(key, session, itemId);
	i->second->activate();
}

bool Instance::showMarkdown(
		const QString &path,
		QVariant context) {
	const auto target = ParseLocalMarkdownTarget(path);
	const auto messageContext = ExtractMarkdownMessageContext(context);
	const auto session = messageContext
		? ResolveMarkdownSession(*messageContext)
		: nullptr;
	const auto itemId = messageContext
		? messageContext->clickHandlerContext.itemId
		: FullMsgId();
	auto options = PrepareLocalMarkdownOptions(context);
	if (!target.sourceName.isEmpty()) {
		options.sourceName = target.sourceName;
		options.sourcePath = target.key;
	}
	options.initialFragment = target.fragment;
	auto i = _markdowns.find(target.key);
	if (i == end(_markdowns)) {
		if (auto controller = Markdown::TryOpenLocalFile(
				_delegate,
				target.path,
				std::move(options))) {
			controller->events() | rpl::on_next([=](Markdown::Event event) {
				using Type = Markdown::Event::Type;
				switch (event.type) {
				case Type::Close:
					_markdownBindings.remove(target.key);
					_markdowns.take(target.key);
					break;
				case Type::Quit:
					Shortcuts::Launch(Shortcuts::Command::Quit);
					break;
				case Type::Report:
					break;
				// Don't try opening markdown links inside markdown viewer,
				// messenger-provided markdown files should know nothing
				// about other local files and their paths.
				//
				//case Type::OpenFile:
				//	if (!showMarkdown(event.url, event.context)) {
				//		DEBUG_LOG(("Native Markdown IV: "
				//			"failed local markdown link: %1"
				//			).arg(event.url));
				//	}
				//	break;
				}
			}, controller->lifetime());

			i = _markdowns.emplace(target.key, std::move(controller)).first;
		} else {
			return false;
		}
	} else {
		i->second->updateOptions(std::move(options));
	}
	if (session) {
		bindMarkdown(target.key, not_null{ session }, itemId);
	} else {
		_markdownBindings.remove(target.key);
	}
	i->second->activate();
	return true;
}

void Instance::requestFull(
		not_null<Main::Session*> session,
		const QString &id) {
	if (!_tracking.contains(session)) {
		return;
	}
	auto &requested = _fullRequested[session][id];
	const auto last = requested.lastRequestedAt;
	const auto now = crl::now();
	if (last && (now - last) < kAllowPageReloadAfter) {
		return;
	}
	requested.lastRequestedAt = now;
	session->api().request(MTPmessages_GetWebPage(
		MTP_string(id),
		MTP_int(requested.hash)
	)).done([=](const MTPmessages_WebPage &result) {
		const auto processed = processReceivedPage(session, id, result);
		if (processed.articleChanged
			&& processed.page
			&& processed.page->iv
			&& _shown
			&& _shownSession == session) {
			_shown->update(processed.page->iv.get());
		}
	}).send();
}

Instance::ProcessReceivedPageResult Instance::processReceivedPage(
		not_null<Main::Session*> session,
		const QString &url,
		const MTPmessages_WebPage &result) {
	const auto &data = result.data();
	const auto owner = &session->data();
	owner->processUsers(data.vusers());
	owner->processChats(data.vchats());
	auto &requested = _fullRequested[session][url];
	auto processed = ProcessReceivedPageResult();
	const auto &mtp = data.vwebpage();
	mtp.match([&](const MTPDwebPageNotModified &data) {
		Q_UNUSED(data);

		processed.page = requested.page;
	}, [&](const MTPDwebPage &data) {
		const auto oldPage = requested.page;
		const auto oldVersion = oldPage ? oldPage->version : 0;
		const auto oldIvHash = (oldPage && oldPage->iv)
			? oldPage->iv->hash()
			: 0;
		requested.hash = data.vhash().v;
		processed.page = owner->processWebpage(data).get();
		requested.page = processed.page;
		processed.articleChanged = processed.page
			&& processed.page->iv
			&& (!oldPage
				|| processed.page->version != oldVersion
				|| processed.page->iv->hash() != oldIvHash);
	}, [&](const auto &) {
		processed.page = owner->processWebpage(mtp).get();
		requested.page = processed.page;
	});
	return processed;
}

auto Instance::processReceivedRichMessage(
	not_null<Main::Session*> session,
	FullMsgId itemId,
	const RichMessageGeneration &generation,
	const MTPmessages_Messages &result)
-> ProcessReceivedRichMessageResult {
	const auto owner = &session->data();
	auto processed = ProcessReceivedRichMessageResult();
	auto page = std::shared_ptr<const RichPage>();
	result.match([&](const MTPDmessages_messagesNotModified &) {
		LOG(("API Error: received messages.messagesNotModified!"));
	}, [&](const auto &data) {
		owner->processUsers(data.vusers());
		owner->processChats(data.vchats());
		for (const auto &message : data.vmessages().v) {
			if (message.type() != mtpc_message) {
				continue;
			}
			const auto &parsed = message.c_message();
			if (MsgId(parsed.vid().v) != itemId.msg) {
				continue;
			}
			const auto richMessage = parsed.vrich_message();
			page = richMessage ? ParseRichPage(session, *richMessage) : nullptr;
			break;
		}
	});
	const auto peer = owner->peer(itemId.peer);
	result.match([&](const MTPDmessages_channelMessages &data) {
		if (const auto channel = peer->asChannel()) {
			channel->ptsReceived(data.vpts().v);
		} else {
			LOG(("App Error: received messages.channelMessages!"));
		}
	}, [](const auto &) {});
	result.match([&](const MTPDmessages_messagesNotModified &) {
	}, [&](const auto &data) {
		peer->processTopics(data.vtopics());
	});
	const auto current = owner->message(itemId);
	if (!current
		|| !MatchesRichMessageGeneration(not_null{ current }, generation)) {
		return processed;
	}
	if (page) {
		current->setFullRichPage(page);
	}
	processed.page = std::move(page);
	return processed;
}

void Instance::finishRichMessageRequest(
		not_null<Main::Session*> session,
		FullMsgId itemId,
		uint64 token,
		std::shared_ptr<const RichPage> page,
		bool notifyCallbacks) {
	const auto sessionIt = _richMessageRequested.find(session);
	if (sessionIt == end(_richMessageRequested)) {
		return;
	}
	const auto itemIt = sessionIt->second.find(itemId);
	if (itemIt == end(sessionIt->second)) {
		return;
	}
	if (itemIt->second.token != token) {
		return;
	}
	auto callbacks = std::move(itemIt->second.callbacks);
	itemIt->second.callbacks.clear();
	itemIt->second.requestId = 0;
	if (!notifyCallbacks) {
		return;
	}
	for (auto &callback : callbacks) {
		callback(page);
	}
}

void Instance::processOpenChannel(const QString &context) {
	if (!_shownSession) {
		return;
	}
	const auto parsed = ParseNativeIvChannelContext(context);
	if (const auto channelId = ChannelId(parsed.channelId)) {
		const auto channel = _shownSession->data().channel(channelId);
		if (channel->isLoaded()) {
			if (const auto controller = _shownSession->tryResolveWindow(channel)) {
				controller->showPeerHistory(channel);
				_shown = nullptr;
			}
		} else if (const auto username = ResolveNativeIvChannelUsername(
				channel->username(),
				parsed.username); !username.isEmpty()) {
			if (const auto controller = _shownSession->tryResolveWindow(channel)) {
				controller->showPeerByLink({
					.usernameOrId = username,
				});
				_shown = nullptr;
			}
		}
	}
}

void Instance::processJoinChannel(const QString &context) {
	if (!_shownSession) {
		return;
	}
	const auto parsed = ParseNativeIvChannelContext(context);
	if (const auto channelId = ChannelId(parsed.channelId)) {
		const auto channel = _shownSession->data().channel(channelId);
		_joining[_shownSession].emplace(channel);
		if (channel->isLoaded()) {
			_shownSession->api().joinChannel(channel);
		} else if (const auto username = ResolveNativeIvChannelUsername(
				channel->username(),
				parsed.username); !username.isEmpty()) {
			if (const auto controller = _shownSession->tryResolveWindow(channel)) {
				controller->showPeerByLink({
					.usernameOrId = username,
					.joinChannel = true,
				});
			}
		}
	}
}

bool Instance::hasActiveWindow(not_null<Main::Session*> session) const {
	return _shown && _shown->activeFor(session);
}

bool Instance::closeActive() {
	if (_shown && _shown->active()) {
		_shown = nullptr;
		return true;
	} else if (_tonSite && _tonSite->active()) {
		_tonSite = nullptr;
		return true;
	}
	for (auto &[key, controller] : _markdowns) {
		if (controller->active()) {
			_markdowns.take(key);
			return true;
		}
	}
	return false;
}

bool Instance::minimizeActive() {
	if (_shown && _shown->active()) {
		_shown->minimize();
		return true;
	} else if (_tonSite && _tonSite->active()) {
		_tonSite->minimize();
		return true;
	}
	return false;
}

void Instance::closeAll() {
	_shown = nullptr;
	_tonSite = nullptr;
}

bool PreferForUri(const QString &uri) {
	const auto url = QUrl(uri);
	const auto host = url.host().toLower();
	const auto path = url.path().toLower();
	return (host == u"telegra.ph"_q)
		|| (host == u"te.legra.ph"_q)
		|| (host == u"graph.org"_q)
		|| (host == u"telegram.org"_q
			&& (path.startsWith(u"/faq"_q)
				|| path.startsWith(u"/privacy"_q)
				|| path.startsWith(u"/blog"_q)));
}

} // namespace Iv
