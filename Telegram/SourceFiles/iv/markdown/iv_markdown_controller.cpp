/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_controller.h"
#include "base/event_filter.h"
#include "core/click_handler_types.h"
#include "core/credits_amount.h"
#include "core/file_utilities.h"
#include "iv/markdown/iv_markdown_article.h"
#include "iv/markdown/iv_markdown_parse.h"
#include "iv/markdown/iv_markdown_view.h"
#include "iv/iv_delegate_impl.h"
#include "iv/iv_search_controller.h"
#include "iv/iv_zoom_controls.h"
#include "lang/lang_keys.h"
#include "ui/layers/layer_manager.h"
#include "ui/layers/show.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/rp_window.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/ui_utility.h"
#include "logs.h"

#include "styles/palette.h"
#include "styles/style_iv.h"
#include "styles/style_menu_icons.h"
#include "styles/style_window.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QUrl>
#include <QtGui/QKeyEvent>
#include <QtGui/QKeySequence>
#include <QtGui/QPainter>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace Iv::Markdown {
namespace {

constexpr auto kZoomStep = int(10);
constexpr auto kZoomDropdownHideDelay = 3 * crl::time(1000);

[[nodiscard]] OpenOptions PrepareOpenOptions(
		OpenOptions options,
		not_null<Delegate*> delegate,
		const QString &title,
		Fn<void()> zoomActivated) {
	options.delegate = delegate;
	options.zoomActivated = std::move(zoomActivated);
	Q_UNUSED(title);
	return options;
}

[[nodiscard]] ViewerKind ResolveViewerKind(const OpenOptions &options) {
	return (options.viewerKind != ViewerKind::Auto)
		? options.viewerKind
		: options.sourcePath.isEmpty()
		? ViewerKind::InstantView
		: ViewerKind::LocalFile;
}

[[nodiscard]] QString SubtitleText(
		const OpenOptions &options,
		const QString &title) {
	if (!options.sourceName.trimmed().isEmpty()) {
		return options.sourceName.trimmed();
	}
	const auto host = QUrl(options.sourceUrl).host().trimmed();
	return !host.isEmpty() ? host : title.trimmed();
}

[[nodiscard]] QString OpenSourceLabel(ViewerKind kind) {
	return (kind == ViewerKind::InstantView)
		? tr::lng_iv_open_in_browser(tr::now)
		: tr::lng_markdown_preview_open_file(tr::now);
}

[[nodiscard]] const style::icon *OpenSourceIcon(ViewerKind kind) {
	return (kind == ViewerKind::InstantView)
		? &st::menuIconIpAddress
		: &st::menuIconFile;
}

[[nodiscard]] QVariant ExtendClickHandlerContext(
		QVariant context,
		const std::shared_ptr<Ui::Show> &show) {
	if (!show) {
		return context;
	} else if (!context.isValid()
		|| context.canConvert<ClickHandlerContext>()) {
		auto clickContext = context.isValid()
			? context.value<ClickHandlerContext>()
			: ClickHandlerContext();
		clickContext.show = show;
		return QVariant::fromValue(clickContext);
	}
	return context;
}

[[nodiscard]] std::shared_ptr<QVariant> ResolveClickHandlerContextRef(
		const std::shared_ptr<QVariant> &current,
		const OpenOptions &options) {
	return options.clickHandlerContextRef
		? options.clickHandlerContextRef
		: (current ? current : std::make_shared<QVariant>());
}

bool ProcessZoomShortcut(not_null<Delegate*> delegate, QKeyEvent *event) {
	if (!(event->modifiers() & Qt::ControlModifier)) {
		return false;
	} else if (event->key() == Qt::Key_Plus
		|| event->key() == Qt::Key_Equal) {
		event->accept();
		delegate->ivSetZoom(delegate->ivZoom() + kZoomStep);
	} else if (event->key() == Qt::Key_Minus) {
		event->accept();
		delegate->ivSetZoom(delegate->ivZoom() - kZoomStep);
	} else if (event->key() == Qt::Key_0) {
		event->accept();
		delegate->ivSetZoom(0);
	} else {
		return false;
	}
	return true;
}

struct OpenTarget {
	QString path;
	QString fragment;
};

struct PageHistoryTarget {
	uint64 pageId = 0;
	QString sourceUrl;
	QString hash;
};

[[nodiscard]] bool IsReadableLocalFile(const QFileInfo &info) {
	return info.exists() && info.isFile() && info.isReadable();
}

struct ReadSource {
	QString path;
	QString name;
	QByteArray bytes;

	explicit operator bool() const {
		return !path.isEmpty();
	}
};
[[nodiscard]] ReadSource ReadLocalSource(
		const QString &path,
		const MarkdownParseLimits &limits) {
	const auto info = QFileInfo(path);
	auto name = info.fileName();
	if (!IsReadableLocalFile(info) || !LooksLikeMarkdownFile(name)) {
		return {};
	} else if (info.size() > limits.maxSourceBytes) {
		DEBUG_LOG(("Native Markdown IV: rejected local file too large: %1"
			).arg(path));
		return {};
	}
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	auto data = file.read(limits.maxSourceBytes);
	if (file.error() != QFileDevice::NoError || !file.atEnd()) {
		DEBUG_LOG(("Native Markdown IV: could not read local file: %1"
			).arg(path));
		return {};
	}
	return {
		.path = info.absoluteFilePath(),
		.name = std::move(name),
		.bytes = std::move(data),
	};
}

[[nodiscard]] QString NormalizeFragmentId(QString fragment) {
	fragment = QString::fromUtf8(
		QByteArray::fromPercentEncoding(fragment.toUtf8()));
	fragment = fragment.trimmed().toLower();
	while (fragment.startsWith(QChar('#'))) {
		fragment.remove(0, 1);
	}
	return fragment;
}

[[nodiscard]] PageHistoryTarget ParsePageHistoryTarget(
		uint64 pageId,
		QString url) {
	const auto hash = url.indexOf(QChar('#'));
	return (hash < 0)
		? PageHistoryTarget{
			.pageId = pageId,
			.sourceUrl = std::move(url),
		}
		: PageHistoryTarget{
			.pageId = pageId,
			.sourceUrl = url.mid(0, hash),
			.hash = NormalizeFragmentId(url.mid(hash + 1)),
		};
}

[[nodiscard]] QString ComposePageHistoryUrl(const PageHistoryTarget &target) {
	return target.hash.isEmpty()
		? target.sourceUrl
		: (target.sourceUrl + u"#"_q + target.hash);
}

[[nodiscard]] OpenTarget ParseOpenTarget(QString path) {
	const auto direct = QFileInfo(path);
	if (direct.exists()) {
		return { path, QString() };
	}
	const auto hash = path.lastIndexOf(QChar('#'));
	if (hash <= 0) {
		return { path, QString() };
	}
	const auto candidate = path.mid(0, hash);
	if (candidate.isEmpty()) {
		return { path, QString() };
	}
	const auto info = QFileInfo(candidate);
	return info.exists()
		? OpenTarget{ candidate, NormalizeFragmentId(path.mid(hash + 1)) }
		: OpenTarget{ path, QString() };
}

[[nodiscard]] bool HasPreviewableContent(const MarkdownNode &node) {
	switch (node.kind) {
	case NodeKind::Document:
	case NodeKind::Unsupported:
		break;
	default:
		return true;
	}
	return std::any_of(
		node.children.begin(),
		node.children.end(),
		[](const MarkdownNode &child) {
			return HasPreviewableContent(child);
		});
}

[[nodiscard]] bool AcceptsPreview(const PreparedDocument &document) {
	return HasPreviewableContent(document.document)
		|| !document.formulas.empty();
}

void LogDocumentWarnings(
		const PreparedDocument &document,
		const QString &path) {
	for (const auto &warning : document.warnings) {
		DEBUG_LOG(("Native Markdown IV: warning (%1): %2"
			).arg(warning
			).arg(path));
	}
}

} // namespace

Controller::Controller(
	not_null<Delegate*> delegate,
	PreparedDocument document,
	QString title,
	OpenOptions options)
: _delegate(delegate)
, _document(std::make_shared<PreparedDocument>(std::move(document)))
, _title(std::move(title))
, _renderer(nullptr)
, _options(PrepareOpenOptions(
	std::move(options),
	delegate,
	_title,
	zoomActivatedCallback())) {
	_clickHandlerContextRef = ResolveClickHandlerContextRef(
		_clickHandlerContextRef,
		_options);
	_options.clickHandlerContextRef = _clickHandlerContextRef;
	createWindow();
}

Controller::Controller(
	not_null<Delegate*> delegate,
	MarkdownArticleContent content,
	QString title,
	std::shared_ptr<MathRenderer> renderer,
	OpenOptions options)
: _delegate(delegate)
, _preparedContent(std::move(content))
, _title(std::move(title))
, _renderer(renderer ? std::move(renderer) : std::make_shared<MathRenderer>())
, _options(PrepareOpenOptions(
	std::move(options),
	delegate,
	_title,
	zoomActivatedCallback())) {
	_clickHandlerContextRef = ResolveClickHandlerContextRef(
		_clickHandlerContextRef,
		_options);
	_options.clickHandlerContextRef = _clickHandlerContextRef;
	createWindow();
}

Controller::~Controller() = default;

rpl::lifetime &Controller::lifetime() {
	return _lifetime;
}

void Controller::activate() {
	if (_window->isMinimized()) {
		_window->showNormal();
	} else if (_window->isHidden()) {
		_window->show();
	}
	_window->raise();
	_window->activateWindow();
	_window->setFocus();
	if (_preview) {
		_preview->setFocus();
	}
}

void Controller::update(
		MarkdownArticleContent content,
		QString title,
		OpenOptions options) {
	setContent(
		std::move(content),
		std::move(title),
		std::move(options),
		true);
}

void Controller::show(
		MarkdownArticleContent content,
		QString title,
		OpenOptions options) {
	setContent(
		std::move(content),
		std::move(title),
		std::move(options),
		false);
	activate();
}

void Controller::setContent(
		MarkdownArticleContent content,
		QString title,
		OpenOptions options,
		bool preserveScroll) {
	const auto scrollTop = (preserveScroll && _preview)
		? MarkdownPreviewScrollTop(_preview.get())
		: 0;
	if (preserveScroll) {
		saveCurrentHistoryScroll(scrollTop);
	}
	auto refreshed = PrepareOpenOptions(
		std::move(options),
		_delegate,
		title,
		zoomActivatedCallback());
	if (preserveScroll && historyEnabled(refreshed)) {
		auto activeIndex = -1;
		if (_shownHistoryIndex >= 0
			&& _shownHistoryIndex < int(_history.size())) {
			activeIndex = _shownHistoryIndex;
		} else if (_historyIndex >= 0
			&& _historyIndex < int(_history.size())) {
			activeIndex = _historyIndex;
		}
		if (activeIndex >= 0
			&& sameHistoryPage(
				_history[activeIndex],
				refreshed.currentPageId,
				refreshed.sourceUrl)) {
			refreshed.initialFragment = _history[activeIndex].hash;
		}
	}
	if (preserveScroll
		&& _preview
		&& historyEnabled(refreshed)) {
		auto activeIndex = -1;
		if (_shownHistoryIndex >= 0
			&& _shownHistoryIndex < int(_history.size())) {
			activeIndex = _shownHistoryIndex;
		} else if (_historyIndex >= 0
			&& _historyIndex < int(_history.size())) {
			activeIndex = _historyIndex;
		}
		if (activeIndex >= 0
			&& sameHistoryLocation(
				_history[activeIndex],
				refreshed.currentPageId,
				refreshed.sourceUrl,
				refreshed.initialFragment)) {
			_title = std::move(title);
			if (_menu) {
				_menu = nullptr;
				_menuToggle->setForceRippled(false);
			}
			const auto updated = updateExistingPreview(
				std::move(content),
				std::move(refreshed),
				scrollTop);
			Assert(updated);
			if (updated) {
				refreshTitle();
				if (_window && _window->isActiveWindow() && _preview) {
					_preview->setFocus();
				}
				updateHistoryButtons();
				if (_search) {
					_search->refresh();
				}
			}
			return;
		}
	}
	if (historyEnabled(refreshed)
		&& _shownHistoryIndex >= 0
		&& _shownHistoryIndex < int(_history.size())
		&& !sameHistoryPage(
			_history[_shownHistoryIndex],
			refreshed.currentPageId,
			refreshed.sourceUrl)) {
		saveCurrentHistoryScroll();
	}
	if (historyEnabled(refreshed)) {
		auto index = findHistoryEntry(
			refreshed.currentPageId,
			refreshed.sourceUrl,
			refreshed.initialFragment);
		if (index < 0) {
			if (_historyIndex < 0) {
				_history.push_back({});
				index = 0;
			} else {
				_history.resize(_historyIndex + 1);
				index = int(_history.size());
				_history.push_back({});
			}
			_historyIndex = index;
		} else if (index != _historyIndex) {
			updateCurrentHistoryEntry(content, title, refreshed);
			updateHistoryButtons();
			return;
		}
	}
	_preparedContent = std::move(content);
	_title = std::move(title);
	_options = refreshed;
	_clickHandlerContextRef = ResolveClickHandlerContextRef(
		_clickHandlerContextRef,
		_options);
	_options.clickHandlerContextRef = _clickHandlerContextRef;
	if (_menu) {
		_menu = nullptr;
		_menuToggle->setForceRippled(false);
	}
	refreshTitle();
	createPreview();
	if (preserveScroll && _preview) {
		ScrollMarkdownPreviewToY(_preview.get(), scrollTop);
	}
	if (_window && _window->isActiveWindow() && _preview) {
		_preview->setFocus();
	}
	updateHistoryButtons();
}

bool Controller::updateExistingPreview(
		MarkdownArticleContent content,
		OpenOptions options,
		int scrollTop) {
	Expects(_preview != nullptr);

	_preparedContent = std::move(content);
	_options = std::move(options);
	_clickHandlerContextRef = ResolveClickHandlerContextRef(
		_clickHandlerContextRef,
		_options);
	_options.clickHandlerContextRef = _clickHandlerContextRef;
	auto previewOptions = _options;
	previewOptions.clickHandlerContextRef = _clickHandlerContextRef;
	previewOptions.clickHandlerContext = ExtendClickHandlerContext(
		std::move(previewOptions.clickHandlerContext),
		_show);
	if (previewOptions.clickHandlerContextRef) {
		*previewOptions.clickHandlerContextRef
			= previewOptions.clickHandlerContext;
	}
	if (historyEnabled(_options)) {
		updateCurrentHistoryEntry(*_preparedContent, _title, _options);
		const auto index = findHistoryEntry(
			_options.currentPageId,
			_options.sourceUrl,
			_options.initialFragment);
		Assert(index >= 0);
		if (index >= 0) {
			_historyIndex = index;
			_shownHistoryIndex = index;
		}
	} else {
		_historyIndex = -1;
		_shownHistoryIndex = -1;
	}
	previewOptions.initialFragment = QString();
	if (!UpdateMarkdownPreviewWidget(
			_preview.get(),
			std::move(*_preparedContent),
			previewOptions)) {
		return false;
	}
	_preparedContent.reset();
	ScrollMarkdownPreviewToY(_preview.get(), scrollTop);
	return true;
}

void Controller::updateOptions(OpenOptions options) {
	const auto initialFragment = options.initialFragment;
	auto refreshed = PrepareOpenOptions(
		std::move(options),
		_delegate,
		_title,
		zoomActivatedCallback());
	if (refreshed.sourceName.isEmpty()) {
		refreshed.sourceName = _options.sourceName;
	}
	if (refreshed.sourcePath.isEmpty()) {
		refreshed.sourcePath = _options.sourcePath;
	}
	if (refreshed.sourceUrl.isEmpty()) {
		refreshed.sourceUrl = _options.sourceUrl;
	}
	if (!refreshed.ivWebviewStorageId) {
		refreshed.ivWebviewStorageId = _options.ivWebviewStorageId;
	}
	if (refreshed.viewerKind == ViewerKind::Auto) {
		refreshed.viewerKind = _options.viewerKind;
	}
	if (!refreshed.openSource) {
		refreshed.openSource = _options.openSource;
	}
	if (!refreshed.activateMedia) {
		refreshed.activateMedia = _options.activateMedia;
	}
	_options = std::move(refreshed);
	if (!_clickHandlerContextRef) {
		_clickHandlerContextRef = std::make_shared<QVariant>();
	}
	_options.clickHandlerContextRef = _clickHandlerContextRef;
	if (_clickHandlerContextRef) {
		*_clickHandlerContextRef = ExtendClickHandlerContext(
			_options.clickHandlerContext,
			_show);
	}
	if (_menu) {
		_menu = nullptr;
		_menuToggle->setForceRippled(false);
	}
	refreshTitle();
	if (!initialFragment.isEmpty() && _preview) {
		const auto scrolled = ScrollMarkdownPreviewToAnchor(
			_preview.get(),
			initialFragment);
		static_cast<void>(scrolled);
	}
	if (_window && _window->isActiveWindow() && _preview) {
		_preview->setFocus();
	}
}

bool Controller::active() const {
	return _window && _window->isActiveWindow();
}

void Controller::showJoinedTooltip() {
	if (_show) {
		_show->showToast(tr::lng_action_you_joined(tr::now));
	}
}

void Controller::minimize() {
	if (_window) {
		_window->setWindowState(_window->windowState() | Qt::WindowMinimized);
	}
}

void Controller::close() {
	_events.fire({ Event::Type::Close });
}

ViewerKind Controller::viewerKind() const {
	return ResolveViewerKind(_options);
}

QString Controller::subtitleText() const {
	return SubtitleText(_options, _title);
}

bool Controller::canOpenSource() const {
	if (_options.openSource) {
		return true;
	}
	return (viewerKind() == ViewerKind::InstantView)
		? !_options.sourceUrl.isEmpty()
		: !_options.sourcePath.isEmpty();
}

bool Controller::canShare() const {
	return static_cast<bool>(_options.share);
}

bool Controller::historyEnabled(const OpenOptions &options) const {
	return (ResolveViewerKind(options) == ViewerKind::InstantView)
		&& (options.currentPageId != 0);
}

bool Controller::sameHistoryPage(
		const HistoryEntry &entry,
		uint64 pageId,
		const QString &sourceUrl) const {
	return entry.pageId
		? (entry.pageId == pageId)
		: (!sourceUrl.isEmpty() && entry.sourceUrl == sourceUrl);
}

bool Controller::sameCurrentPage(uint64 pageId, const QString &sourceUrl) const {
	return (pageId != 0
		&& _options.currentPageId != 0
		&& pageId == _options.currentPageId)
		|| (!sourceUrl.isEmpty()
			&& !_options.sourceUrl.isEmpty()
			&& sourceUrl == _options.sourceUrl);
}

bool Controller::sameHistoryLocation(
		const HistoryEntry &entry,
		uint64 pageId,
		const QString &sourceUrl,
		const QString &hash) const {
	return sameHistoryPage(entry, pageId, sourceUrl)
		&& (entry.hash == hash);
}

int Controller::findHistoryEntry(
		uint64 pageId,
		const QString &sourceUrl,
		const QString &hash) const {
	if (_historyIndex >= 0
		&& _historyIndex < int(_history.size())
		&& sameHistoryLocation(
			_history[_historyIndex],
			pageId,
			sourceUrl,
			hash)) {
		return _historyIndex;
	}
	if (_shownHistoryIndex >= 0
		&& _shownHistoryIndex < int(_history.size())
		&& _shownHistoryIndex != _historyIndex
		&& sameHistoryLocation(
			_history[_shownHistoryIndex],
			pageId,
			sourceUrl,
			hash)) {
		return _shownHistoryIndex;
	}
	for (auto i = 0, count = int(_history.size()); i != count; ++i) {
		if (sameHistoryLocation(_history[i], pageId, sourceUrl, hash)) {
			return i;
		}
	}
	return -1;
}

void Controller::saveCurrentHistoryScroll(std::optional<int> scrollTop) {
	const auto index = (_shownHistoryIndex >= 0)
		? _shownHistoryIndex
		: _historyIndex;
	if (!_preview
		|| index < 0
		|| index >= int(_history.size())) {
		return;
	}
	_history[index].scrollTop = scrollTop.value_or(
		MarkdownPreviewScrollTop(_preview.get()));
}

void Controller::updateCurrentHistoryEntry(
		const MarkdownArticleContent &content,
		const QString &title,
		const OpenOptions &options) {
	if (!historyEnabled(options)) {
		return;
	}
	auto index = findHistoryEntry(
		options.currentPageId,
		options.sourceUrl,
		options.initialFragment);
	if (index < 0) {
		if (_historyIndex < 0) {
			_history.push_back({});
			index = 0;
			_historyIndex = 0;
		} else {
			_history.resize(_historyIndex + 1);
			index = int(_history.size());
			_history.push_back({});
			_historyIndex = index;
		}
	}
	auto &entry = _history[index];
	entry.pageId = options.currentPageId;
	entry.sourceUrl = options.sourceUrl;
	entry.hash = options.initialFragment;
	entry.title = title;
	entry.preparedContent = std::make_shared<MarkdownArticleContent>(content);
	entry.options = options;
}

void Controller::handleOpenPage(Event event) {
	if (!historyEnabled(_options)) {
		_events.fire(std::move(event));
		return;
	}
	const auto target = ParsePageHistoryTarget(event.webpageId, event.url);
	if (!target.pageId || target.sourceUrl.isEmpty()) {
		_events.fire(std::move(event));
		return;
	}
	const auto currentIndex = (_historyIndex >= 0
		&& _historyIndex < int(_history.size()))
		? _historyIndex
		: -1;
	const auto current = (currentIndex >= 0) ? &_history[currentIndex] : nullptr;
	const auto samePage = sameCurrentPage(target.pageId, target.sourceUrl)
		|| (current
			&& sameHistoryPage(*current, target.pageId, target.sourceUrl));
	if (samePage) {
		if (target.hash.isEmpty()) {
			if (_preview) {
				ScrollMarkdownPreviewToY(
					_preview.get(),
					0,
					MarkdownPreviewScrollMode::Animated);
			}
			return;
		}
		if (_preview
			&& ScrollMarkdownPreviewToAnchor(
				_preview.get(),
				target.hash,
				MarkdownPreviewScrollMode::Animated)) {
			return;
		}
		DEBUG_LOG(("Native Markdown IV: unresolved anchor: %1"
			).arg(target.hash));
		return;
	}
	saveCurrentHistoryScroll();
	auto targetIndex = -1;
	if ((_historyIndex + 1) < int(_history.size())
		&& sameHistoryLocation(
			_history[_historyIndex + 1],
			target.pageId,
			target.sourceUrl,
			target.hash)) {
		targetIndex = _historyIndex + 1;
	} else {
		auto options = _options;
		options.sourceUrl = target.sourceUrl;
		options.initialFragment = target.hash;
		options.currentPageId = target.pageId;
		_history.resize(_historyIndex + 1);
		_history.push_back({
			.pageId = target.pageId,
			.sourceUrl = target.sourceUrl,
			.hash = target.hash,
			.options = std::move(options),
		});
		targetIndex = int(_history.size()) - 1;
	}
	auto &entry = _history[targetIndex];
	entry.pageId = target.pageId;
	entry.sourceUrl = target.sourceUrl;
	entry.hash = target.hash;
	entry.options.sourceUrl = target.sourceUrl;
	entry.options.initialFragment = target.hash;
	entry.options.currentPageId = target.pageId;
	_historyIndex = targetIndex;
	updateHistoryButtons();
	if (!showHistoryEntry(targetIndex)) {
		event.url = ComposePageHistoryUrl(target);
		_events.fire(std::move(event));
	}
}

bool Controller::showHistoryEntry(int index) {
	if (index < 0 || index >= int(_history.size())) {
		return false;
	}
	const auto &entry = _history[index];
	if (!entry.hydrated()) {
		return false;
	}
	auto options = entry.options;
	options.sourceUrl = entry.sourceUrl;
	options.initialFragment = entry.hash;
	options.currentPageId = entry.pageId;
	setContent(*entry.preparedContent, entry.title, std::move(options), false);
	if (_preview) {
		ScrollMarkdownPreviewToY(_preview.get(), entry.scrollTop);
	}
	return true;
}

void Controller::stepHistory(int delta) {
	const auto index = _historyIndex + delta;
	if (index < 0 || index >= int(_history.size())) {
		return;
	}
	saveCurrentHistoryScroll();
	_historyIndex = index;
	updateHistoryButtons();
	if (!showHistoryEntry(index)) {
		const auto &entry = _history[index];
		_events.fire({
			.type = Event::Type::OpenPage,
			.webpageId = entry.pageId,
			.url = ComposePageHistoryUrl(PageHistoryTarget{
				.pageId = entry.pageId,
				.sourceUrl = entry.sourceUrl,
				.hash = entry.hash,
			}),
		});
	}
}

void Controller::updateHistoryButtons() {
	if (!_back || !_forward) {
		return;
	}
	const auto canGoBack = (_historyIndex > 0);
	const auto canGoForward = (_historyIndex >= 0)
		&& ((_historyIndex + 1) < int(_history.size()));
	const auto updateButton = [](
			Ui::FadeWrapScaled<Ui::IconButton> *button,
			bool enabled,
			const style::icon &disabledIcon) {
		Expects(button != nullptr);
		button->entity()->setDisabled(!enabled);
		button->entity()->setIconOverride(
			enabled ? nullptr : &disabledIcon,
			enabled ? nullptr : &disabledIcon);
		button->entity()->setPointerCursor(enabled);
		button->setAttribute(Qt::WA_TransparentForMouseEvents, !enabled);
		if (!enabled) {
			button->entity()->clearState();
		}
	};
	_back->toggle(canGoBack || canGoForward, anim::type::normal);
	_forward->toggle(canGoForward, anim::type::normal);
	updateButton(_back, canGoBack, st::ivBackIconDisabled);
	updateButton(_forward, canGoForward, st::ivForwardIconDisabled);
	if (_window) {
		updateTitleGeometry(_window->body()->width());
	}
}

void Controller::refreshTitle() {
	if (_window) {
		_window->setTitle(_title);
		_window->setWindowTitle(_title);
	}
	if (_subtitle) {
		_subtitle->setText(subtitleText());
		updateTitleGeometry(_window->body()->width());
	}
}

void Controller::updateTitleGeometry(int newWidth) const {
	_subtitleWrap->setGeometry(0, 0, newWidth, st::ivSubtitleHeight);
	const auto progressBack = _back
		? _subtitleBackShift.value(_back->toggled() ? 1. : 0.)
		: 0.;
	const auto progressForward = _forward
		? _subtitleForwardShift.value(_forward->toggled() ? 1. : 0.)
		: 0.;
	const auto backAdded = (_back
		? (_back->width() + st::ivSubtitleSkip - st::ivSubtitleLeft)
		: 0);
	const auto forwardAdded = _forward ? _forward->width() : 0;
	const auto left = int(st::ivSubtitleLeft
		+ anim::interpolate(0, backAdded, progressBack)
		+ anim::interpolate(0, forwardAdded, progressForward));
	_subtitle->resizeToWidth(newWidth - left - _menuToggle->width());
	_subtitle->moveToLeft(left, st::ivSubtitleTop);
	if (_back) {
		_back->moveToLeft(0, 0);
	}
	if (_forward) {
		_forward->moveToLeft(
			_back ? int(_back->width() * progressBack) : 0,
			0);
	}
	_menuToggle->moveToRight(0, 0);
	_titleShadow->resizeToWidth(newWidth);
	_titleShadow->move(0, st::ivSubtitleHeight);
}

void Controller::openSource() {
	if (_options.openSource) {
		_options.openSource();
	} else if (viewerKind() == ViewerKind::InstantView) {
		File::OpenUrl(_options.sourceUrl);
	} else {
		File::Launch(_options.sourcePath);
	}
}

void Controller::showMenu() {
	if (!_window || _menu) {
		return;
	}
	hideZoomDropdown();
	_menu = base::make_unique_q<Ui::PopupMenu>(
		_window.get(),
		st::popupMenuWithIcons);
	_menu->setDestroyedCallback(crl::guard(_window.get(), [
			this,
			menu = _menu.get()] {
		if (_menu == menu) {
			_menuToggle->setForceRippled(false);
		}
	}));
	_menuToggle->setForceRippled(true);

	const auto hasOpenSource = canOpenSource();
	if (hasOpenSource) {
		_menu->addAction(
			OpenSourceLabel(viewerKind()),
			crl::guard(_window.get(), [=] {
				openSource();
			}),
			OpenSourceIcon(viewerKind()));
	}

	if (canShare()) {
		_menu->addAction(
			tr::lng_iv_share(tr::now),
			crl::guard(_window.get(), [=, share = _options.share] {
				share(_show);
			}),
			&st::menuIconShare);
	}

	if (hasOpenSource || canShare()) {
		_menu->addSeparator();
	}
	_menu->addAction(CreateZoomMenuAction(_menu->menu(), _delegate));

	_menu->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
	_menu->popup(_window->body()->mapToGlobal(
		QPoint(_window->body()->width(), 0) + st::ivMenuPosition));
}

void Controller::createZoomDropdown() {
	_zoomDropdown = base::make_unique_q<Ui::DropdownMenu>(
		_window->body().get(),
		st::dropdownMenuWithIcons);
	_zoomDropdown->addAction(
		CreateZoomMenuAction(_zoomDropdown->menu(), _delegate));
	_zoomDropdown->events() | rpl::on_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Enter) {
			_zoomDropdownHideTimer.cancel();
		}
	}, _zoomDropdown->lifetime());
	_zoomDropdownHideTimer.setCallback([=] {
		if (_zoomDropdown) {
			_zoomDropdown->hideAnimated();
		}
	});
}

void Controller::showZoomDropdown() {
	if (!_window || _menu) {
		return;
	}
	if (!_zoomDropdown) {
		createZoomDropdown();
	}
	updateZoomDropdownGeometry();
	_zoomDropdown->raise();
	_zoomDropdown->showAnimated(Ui::PanelAnimation::Origin::TopRight);
	if (!_zoomDropdown->underMouse()) {
		_zoomDropdownHideTimer.callOnce(kZoomDropdownHideDelay);
	}
}

void Controller::hideZoomDropdown() {
	_zoomDropdownHideTimer.cancel();
	if (_zoomDropdown) {
		_zoomDropdown->hideFast();
	}
}

void Controller::updateZoomDropdownGeometry() {
	if (!_zoomDropdown || !_window) {
		return;
	}
	_zoomDropdown->resizeToContent();
	Ui::SendPendingMoveResizeEvents(_zoomDropdown.get());
	const auto &padding = st::dropdownMenuWithIcons.wrap.padding;
	const auto visibleHeight = _zoomDropdown->height()
		- padding.top()
		- padding.bottom();
	const auto visibleRight = _window->body()->width()
		- st::ivMenuToggle.width
		+ st::ivZoomDropdownPosition.x();
	const auto visibleTop = ((st::ivSubtitleHeight - visibleHeight) / 2)
		+ st::ivZoomDropdownPosition.y();
	_zoomDropdown->move(
		visibleRight + padding.right() - _zoomDropdown->width(),
		visibleTop - padding.top());
}

Fn<void()> Controller::zoomActivatedCallback() {
	return crl::guard(this, [=] {
		showZoomDropdown();
	});
}

void Controller::createLayerManager() {
	if (!_window || _layerManager) {
		return;
	}
	_layerManager = std::make_unique<Ui::LayerManager>(
		not_null{ _window->body().get() });
	_layerManager->setHideByBackgroundClick(false);
	_show = _layerManager->uiShow();
}

void Controller::createPreview() {
	if (!_container) {
		return;
	}
	const auto parent = _container;
	auto options = _options;
	options.clickHandlerContextRef = _clickHandlerContextRef;
	options.clickHandlerContext = ExtendClickHandlerContext(
		std::move(options.clickHandlerContext),
		_show);
	if (options.clickHandlerContextRef) {
		*options.clickHandlerContextRef = options.clickHandlerContext;
	}
	const auto callback = [=](Event event) {
		switch (event.type) {
		case Event::Type::OpenPage:
			handleOpenPage(std::move(event));
			break;
		case Event::Type::Close:
		case Event::Type::Quit:
		case Event::Type::OpenFile:
		case Event::Type::Report:
			_events.fire(std::move(event));
			break;
		}
	};
	_preview = nullptr;
	const auto preparedForHistory = (_preparedContent && historyEnabled(options))
		? std::make_shared<MarkdownArticleContent>(*_preparedContent)
		: nullptr;
	_preview = _preparedContent
		? CreateMarkdownPreviewWidget(
			parent,
			std::move(*_preparedContent),
			_renderer,
			callback,
			options)
		: CreateMarkdownPreviewWidget(
			parent,
			*_document,
			callback,
			options);
	_preparedContent.reset();
	if (preparedForHistory) {
		updateCurrentHistoryEntry(*preparedForHistory, _title, options);
		_shownHistoryIndex = _historyIndex;
	} else {
		_shownHistoryIndex = -1;
	}
	_preview->setGeometry(parent->rect());
	parent->sizeValue() | rpl::on_next([=](QSize size) {
		_preview->resize(size);
	}, _preview->lifetime());

	_titleShadow->show(anim::type::instant);

	_preview->show();
	updateHistoryButtons();
	if (_search) {
		_search->refresh();
	}
}

void Controller::createSearchController() {
	auto host = SearchHost{
		.ready = [=] { return _preview != nullptr; },
		.sources = [=] {
			return MarkdownPreviewSearchSources(_preview.get());
		},
		.applyMatches = [=](
				std::vector<MarkdownArticleSearchMatch> matches,
				int current) {
			SetMarkdownPreviewSearchMatches(
				_preview.get(),
				std::move(matches),
				current);
		},
		.scrollToSegment = [=](int segmentIndex) {
			ScrollMarkdownPreviewToSegment(
				_preview.get(),
				segmentIndex,
				0);
		},
		.expandDetails = [=](const QString &anchorId) {
			return ExpandMarkdownPreviewDetails(
				_preview.get(),
				anchorId);
		},
		.focusContent = [=] {
			_preview->setFocus();
		},
	};
	_search = std::make_unique<SearchController>(
		_window->body().get(),
		_window->body()->widthValue(),
		std::move(host));
	_searchBarHeight = _search->barHeightValue();
	_search->moveBar(0, st::ivSubtitleHeight);
	_search->raiseBar();
	_titleShadow->raise();
}

void Controller::toggleSearchBar() {
	if (!_window) {
		return;
	} else if (_search && _search->shown()) {
		_search->hide();
		return;
	}
	if (!_search) {
		createSearchController();
	}
	_search->toggle();
}

void Controller::createWindow() {
	_window = std::make_unique<Ui::RpWindow>();
	const auto window = _window.get();
	_titleShadow.create(window->body().get());
	_subtitleWrap = std::make_unique<Ui::RpWidget>(window->body().get());
	_subtitle = std::make_unique<Ui::FlatLabel>(
		_subtitleWrap.get(),
		subtitleText(),
		st::ivSubtitle);
	_subtitle->setSelectable(true);
	_menuToggle.create(_subtitleWrap.get(), st::ivMenuToggle);
	_menuToggle->setClickedCallback([=] {
		showMenu();
	});
	_back.create(
		_subtitleWrap.get(),
		object_ptr<Ui::IconButton>(_subtitleWrap.get(), st::ivBack));
	_back->entity()->setClickedCallback([=] {
		stepHistory(-1);
	});
	_forward.create(
		_subtitleWrap.get(),
		object_ptr<Ui::IconButton>(_subtitleWrap.get(), st::ivForward));
	_forward->entity()->setClickedCallback([=] {
		stepHistory(1);
	});
	_back->toggledValue(
	) | rpl::on_next([=](bool toggled) {
		_subtitleBackShift.start(
			[=] { updateTitleGeometry(_window->body()->width()); },
			toggled ? 0. : 1.,
			toggled ? 1. : 0.,
			st::fadeWrapDuration);
	}, _back->lifetime());
	_back->hide(anim::type::instant);
	_forward->toggledValue(
	) | rpl::on_next([=](bool toggled) {
		_subtitleForwardShift.start(
			[=] { updateTitleGeometry(_window->body()->width()); },
			toggled ? 0. : 1.,
			toggled ? 1. : 0.,
			st::fadeWrapDuration);
	}, _forward->lifetime());
	_forward->hide(anim::type::instant);
	_subtitleBackShift.stop();
	_subtitleForwardShift.stop();
	_subtitleWrap->paintRequest() | rpl::on_next([=](QRect clip) {
		QPainter(_subtitleWrap.get()).fillRect(clip, st::windowBg);
	}, _subtitleWrap->lifetime());
	window->body()->widthValue() | rpl::on_next([=](int width) {
		updateTitleGeometry(width);
		updateZoomDropdownGeometry();
	}, _subtitle->lifetime());
	window->setTitle(_title);
	window->setWindowTitle(_title);
	window->setGeometry(_delegate->ivGeometry(window));
	window->setMinimumSize({ st::ivWidthMin, st::ivHeightMin });
	window->geometryValue(
	) | rpl::distinct_until_changed(
	) | rpl::skip(1) | rpl::on_next([=] {
		_delegate->ivSaveGeometry(window);
	}, window->lifetime());
	window->body()->paintRequest() | rpl::on_next([=](QRect clip) {
		QPainter(window->body().get()).fillRect(clip, st::windowBg);
	}, window->body()->lifetime());
	_container = Ui::CreateChild<Ui::RpWidget>(window->body().get());
	rpl::combine(
		window->body()->sizeValue(),
		_subtitleWrap->heightValue(),
		_searchBarHeight.value()
	) | rpl::on_next([=](QSize size, int titleHeight, int barHeight) {
		_container->setGeometry(QRect(QPoint(), size).marginsRemoved(
			{ 0, titleHeight + barHeight, 0, 0 }));
	}, _container->lifetime());
	_container->paintRequest() | rpl::on_next([=](QRect clip) {
		QPainter(_container).fillRect(clip, st::windowBg);
	}, _container->lifetime());
	updateTitleGeometry(window->body()->width());

	createLayerManager();
	createPreview();
	_container->show();
	updateHistoryButtons();

	window->events() | rpl::on_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Close) {
			close();
		} else if (e->type() == QEvent::KeyPress) {
			const auto event = static_cast<QKeyEvent*>(e.get());
			if (event->key() == Qt::Key_Escape) {
				event->accept();
				if (_search && _search->shown()) {
					_search->hide();
				} else {
					close();
				}
			}
		}
	}, window->lifetime());
	base::install_event_filter(window, qApp, [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::ShortcutOverride || !window->isActiveWindow()) {
			return base::EventFilterResult::Continue;
		}
		const auto event = static_cast<QKeyEvent*>(e.get());
		const auto previousAccepted = event->isAccepted();
		if (ProcessZoomShortcut(_delegate, event)) {
			showZoomDropdown();
		}
		if (!event->isAccepted()
			&& (event->modifiers() & Qt::ControlModifier)
			&& event->key() == Qt::Key_F) {
			event->accept();
			toggleSearchBar();
		}
		return event->isAccepted() && !previousAccepted
			? base::EventFilterResult::Cancel
			: base::EventFilterResult::Continue;
	});

	_titleShadow->raise();
	window->show();
}

std::unique_ptr<Controller> TryOpenLocalFile(
		not_null<Delegate*> delegate,
		const QString &path,
		OpenOptions options) {
	const auto &limits = ParseLimitsForIv();
	const auto target = ParseOpenTarget(path);

	const auto source = ReadLocalSource(target.path, limits);
	if (!source) {
		return nullptr;
	}
	const auto &bytes = source.bytes;
	const auto &fallbackTitle = source.name;

	const auto start = crl::now();
	auto validateResult = ValidateMarkdownSourceForIv(
		bytes,
		ParseOptions{ fallbackTitle });
	const auto validated = crl::now();
	if (!validateResult.ok) {
		DEBUG_LOG(("Native Markdown IV: "
			"source validation failure (%1, %2 ms): %3"
			).arg(validateResult.error
			).arg(validated - start
			).arg(target.path));
		return nullptr;
	}

	auto parseResult = ParseMarkdownForIv(std::move(validateResult.source));
	const auto parsed = crl::now();
	if (!parseResult.ok) {
		const auto &error = parseResult.error;
		if (error.startsWith(u"cmark-"_q)) {
			DEBUG_LOG(("Native Markdown IV: "
				"cmark parse failure (%1, %2 ms): %3"
				).arg(error
				).arg(parsed - validated
				).arg(target.path));
		} else {
			DEBUG_LOG(("Native Markdown IV: parse failure (%1, %2 ms): %3"
				).arg(error
				).arg(parsed - validated
				).arg(target.path));
		}
		return nullptr;
	} else if (!AcceptsPreview(parseResult.document)) {
		DEBUG_LOG(("Native Markdown IV: "
			"unsupported or empty document (%1 ms): %2"
			).arg(crl::now() - parsed
			).arg(target.path));
		return nullptr;
	}
	LogDocumentWarnings(parseResult.document, target.path);

	DEBUG_LOG(("Native Markdown IV: "
		"opening as native Markdown IV (%1 ms validate, %2 ms parse): %3"
		).arg(validated - start
		).arg(parsed - validated
		).arg(target.path));

	if (options.sourceName.isEmpty()) {
		options.sourceName = source.name;
	}
	options.sourcePath = std::move(source.path);
	options.initialFragment = std::move(target.fragment);
	if (options.viewerKind == ViewerKind::Auto) {
		options.viewerKind = ViewerKind::LocalFile;
	}
	return std::make_unique<Controller>(
		delegate,
		std::move(parseResult.document),
		(parseResult.document.title.trimmed().isEmpty()
			? fallbackTitle
			: parseResult.document.title.trimmed()),
		std::move(options));
}

} // namespace Iv::Markdown
