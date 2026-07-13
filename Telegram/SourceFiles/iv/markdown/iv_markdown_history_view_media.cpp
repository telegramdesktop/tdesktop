/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_history_view_media.h"

#include "iv/markdown/iv_markdown_article.h"
#include "base/unixtime.h"
#include "base/flat_set.h"
#include "iv/markdown/iv_markdown_media_block.h"
#include "iv/markdown/iv_markdown_slideshow_chrome.h"

#include <QtCore/QDate>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtWidgets/QApplication>
#include <algorithm>
#include <array>
#include <unordered_set>

#include "settings.h"
#include "ui/image/image_location.h"
#include "data/data_types.h"
#include "data/data_document.h"
#include "data/data_file_click_handler.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/admin_log/history_admin_log_item.h"
#include "history/history_location_manager.h"
#include "history/view/history_view_cursor_state.h"
#include "history/history_view_top_toast.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_message.h"
#include "history/view/media/history_view_media.h"
#include "history/view/media/history_view_media_grouped.h"
#include "ui/basic_click_handlers.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_iv.h"

#include "rpl/filter.h"
#include "rpl/lifetime.h"

#include <utility>

namespace Iv::Markdown {
namespace {

class IvHistoryViewDelegate final : public HistoryView::SimpleElementDelegate {
public:
	IvHistoryViewDelegate(
		not_null<Window::SessionController*> controller,
		not_null<::Data::Session*> session,
		Fn<void()> update);

	HistoryView::Context elementContext() override;

	HistoryView::ElementChatMode elementChatMode() override;

	bool elementAnimationsPaused() override;

	void elementOpenPhoto(
			not_null<PhotoData*> photo,
			FullMsgId context) override;

	void elementOpenDocument(
			not_null<DocumentData*> document,
			FullMsgId context,
			bool showInMediaView = false) override;

	void elementCancelUpload(const FullMsgId &context) override;

	void elementShowTooltip(
			const TextWithEntities &text,
			Fn<void()> hiddenCallback) override;

private:
	const not_null<::Data::Session*> _session;
	HistoryView::InfoTooltip _tooltip;
};

IvHistoryViewDelegate::IvHistoryViewDelegate(
	not_null<Window::SessionController*> controller,
	not_null<::Data::Session*> session,
	Fn<void()> update)
: HistoryView::SimpleElementDelegate(controller, std::move(update))
, _session(session) {
}

HistoryView::Context IvHistoryViewDelegate::elementContext() {
	return HistoryView::Context::TTLViewer;
}

HistoryView::ElementChatMode IvHistoryViewDelegate::elementChatMode() {
	return HistoryView::ElementChatMode::Default;
}

bool IvHistoryViewDelegate::elementAnimationsPaused() {
	return false;
}

void IvHistoryViewDelegate::elementOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context) {
	controller()->openPhoto(photo, { .id = context });
}

void IvHistoryViewDelegate::elementOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) {
	controller()->openDocument(
		document,
		showInMediaView,
		{ .id = context });
}

void IvHistoryViewDelegate::elementCancelUpload(const FullMsgId &context) {
	if (const auto item = _session->message(context)) {
		controller()->cancelUploadLayer(item);
	}
}

void IvHistoryViewDelegate::elementShowTooltip(
		const TextWithEntities &text,
		Fn<void()> hiddenCallback) {
	const auto widget = QApplication::activeWindow();
	if (!widget) {
		return;
	}
	_tooltip.show(
		not_null{ widget },
		&_session->session(),
		text,
		std::move(hiddenCallback));
}

struct IvHistoryViewHit {
	ClickHandlerPtr link;
	MediaActivation activation;
	bool supported = true;
};

[[nodiscard]] MediaActivation ExternalActivation(QString url) {
	auto result = MediaActivation();
	if (!url.isEmpty()) {
		result.kind = MediaActivationKind::ExternalUrl;
		result.url = std::move(url);
	}
	return result;
}

[[nodiscard]] bool IsSupportedInteractionHandler(
		const ClickHandlerPtr &handler) {
	return std::dynamic_pointer_cast<VoiceSeekClickHandler>(handler)
		|| std::dynamic_pointer_cast<LambdaClickHandler>(handler)
		|| std::dynamic_pointer_cast<PhotoSaveClickHandler>(handler)
		|| std::dynamic_pointer_cast<PhotoCancelClickHandler>(handler)
		|| std::dynamic_pointer_cast<DocumentSaveClickHandler>(handler)
		|| std::dynamic_pointer_cast<DocumentCancelClickHandler>(handler)
		|| std::dynamic_pointer_cast<DocumentOpenWithClickHandler>(handler);
}

[[nodiscard]] IvHistoryViewHit ClassifyGroupedHandler(
		const ClickHandlerPtr &handler,
		const base::flat_map<
			uint64,
			std::shared_ptr<PhotoRuntime>> &photos,
		const base::flat_map<
			uint64,
			std::shared_ptr<DocumentRuntime>> &documents,
		const base::flat_map<uint64, int> &indices) {
	auto result = IvHistoryViewHit();
	const auto activatesPhoto
		= std::dynamic_pointer_cast<PhotoOpenClickHandler>(handler)
		|| std::dynamic_pointer_cast<PhotoSaveClickHandler>(handler);
	const auto activatesDocument = !activatesPhoto
		&& !std::dynamic_pointer_cast<VoiceSeekClickHandler>(handler)
		&& (std::dynamic_pointer_cast<DocumentOpenClickHandler>(handler)
			|| std::dynamic_pointer_cast<DocumentSaveClickHandler>(
				handler));
	if (activatesPhoto) {
		const auto photo = std::dynamic_pointer_cast<PhotoClickHandler>(
			handler)->photo();
		const auto i = photos.find(photo->id);
		if (i != end(photos)) {
			result.activation.kind = MediaActivationKind::Photo;
			result.activation.photo = i->second;
			const auto j = indices.find(photo->id);
			if (j != end(indices)) {
				result.activation.itemIndex = j->second;
			}
			return result;
		}
	} else if (activatesDocument) {
		const auto document
			= std::dynamic_pointer_cast<DocumentClickHandler>(
				handler)->document();
		const auto i = documents.find(document->id);
		if (i != end(documents)) {
			result.activation.kind = MediaActivationKind::Document;
			result.activation.document = i->second;
			const auto j = indices.find(document->id);
			if (j != end(indices)) {
				result.activation.itemIndex = j->second;
			}
			return result;
		}
	}
	result.supported = false;
	return result;
}

[[nodiscard]] MediaActivation SpoileredGroupedItemActivation(
		int itemIndex,
		const base::flat_map<
			uint64,
			std::shared_ptr<PhotoRuntime>> &photos,
		const base::flat_map<
			uint64,
			std::shared_ptr<DocumentRuntime>> &documents,
		const base::flat_map<uint64, int> &indices,
		const base::flat_set<uint64> &spoilered) {
	auto result = MediaActivation();
	for (const auto &[id, index] : indices) {
		if (index != itemIndex || !spoilered.contains(id)) {
			continue;
		}
		const auto i = photos.find(id);
		if (i != end(photos)) {
			result.kind = MediaActivationKind::Photo;
			result.photo = i->second;
			result.itemIndex = itemIndex;
		} else {
			const auto j = documents.find(id);
			if (j != end(documents)) {
				result.kind = MediaActivationKind::Document;
				result.document = j->second;
				result.itemIndex = itemIndex;
			}
		}
		break;
	}
	return result;
}

[[nodiscard]] not_null<HistoryItem*> CreateIvHostMessage(
		not_null<History*> history,
		QString pageUrl) {
	const auto item = history->addNewLocalMessage({
		.id = history->nextNonHistoryEntryId(),
		.flags = (MessageFlag::FakeHistoryItem
			| MessageFlag::Local
			| MessageFlag::HideDisplayDate),
		.date = base::unixtime::now(),
	}, TextWithEntities(), MTP_messageMediaEmpty());
	item->setMediaForInstantView(std::move(pageUrl));
	return item;
}

class IvHistoryViewBlock final : public MediaBlock {
public:
	explicit IvHistoryViewBlock(IvHistoryViewMediaDescriptor descriptor);
	~IvHistoryViewBlock();

	[[nodiscard]] uint64 stableId() const override;

	[[nodiscard]] bool supported() const;

	[[nodiscard]] int resizeGetHeight(int width) override;

	void setGeometry(QRect geometry) override;

	[[nodiscard]] QRect geometry() const override;

	[[nodiscard]] int firstLineBaseline() const override;

	void paint(
		Painter &p,
		const MarkdownArticlePaintContext &context) const override;

	[[nodiscard]] ClickHandlerPtr linkAt(QPoint point) const override;

	[[nodiscard]] MediaActivation activationAt(QPoint point) const override;

	[[nodiscard]] MediaBlockSelectionData selectionData() const override;

	[[nodiscard]] bool hasHeavyPart() const override;

	void unloadHeavyPart() override;

	void hideSpoilers() override;

	[[nodiscard]] std::vector<QRect> itemRects() const override;

private:
	[[nodiscard]] bool alive() const override;

	[[nodiscard]] IvHistoryViewHit resolveHit(QPoint point) const;

	[[nodiscard]] IvHistoryViewHit resolveLocalHit(QPoint point) const;

	[[nodiscard]] IvHistoryViewHit classifyState(
			const HistoryView::TextState &state,
			QPoint localPoint) const;

	[[nodiscard]] IvHistoryViewHit classifyHandler(
			const ClickHandlerPtr &handler,
			QPoint localPoint) const;

	[[nodiscard]] bool probeSupport();

	[[nodiscard]] bool supportsHitClassification();

	void mediaPixelScaleUpdated() override;

	void hostUpdated() override;

	const uint64 _stableId = 0;
	const IvHistoryViewMediaKind _kind = IvHistoryViewMediaKind::Map;
	const QString _copyText;
	const QSize _layoutHint;
	const std::shared_ptr<PhotoRuntime> _photoRuntime;
	const std::shared_ptr<DocumentRuntime> _documentRuntime;
	const base::flat_map<
		uint64,
		std::shared_ptr<PhotoRuntime>> _groupedPhotoRuntimes;
	const base::flat_map<
		uint64,
		std::shared_ptr<DocumentRuntime>> _groupedDocumentRuntimes;
	const base::flat_map<uint64, int> _groupedItemIndices;
	const base::flat_set<uint64> _groupedSpoileredIds;
	const bool _spoiler = false;
	const std::shared_ptr<IvHistoryViewMediaHost> _host;
	const std::vector<std::shared_ptr<void>> _keepAlive;
	std::unique_ptr<HistoryView::Media> _media;
	QRect _geometry;
	int _requestedWidth = 0;
	bool _supported = false;
	MediaBlockHost *_registeredBridgeHost = nullptr;
};

IvHistoryViewBlock::IvHistoryViewBlock(
	IvHistoryViewMediaDescriptor descriptor)
: _stableId(descriptor.stableId)
, _kind(descriptor.kind)
, _copyText(std::move(descriptor.copyText))
, _layoutHint(descriptor.layoutHint)
, _photoRuntime(std::move(descriptor.photo))
, _documentRuntime(std::move(descriptor.document))
, _groupedPhotoRuntimes(std::move(descriptor.groupedPhotos))
, _groupedDocumentRuntimes(std::move(descriptor.groupedDocuments))
, _groupedItemIndices(std::move(descriptor.groupedItemIndices))
, _groupedSpoileredIds(std::move(descriptor.groupedSpoileredIds))
, _spoiler(descriptor.spoiler)
, _host(std::move(descriptor.host))
, _keepAlive(std::move(descriptor.keepAlive)) {
	if (descriptor.mediaFactory) {
		_media = descriptor.mediaFactory(_host->view());
	}
	if (_media) {
		_media->initDimensions();
	}
	_supported = _media && probeSupport();
}

IvHistoryViewBlock::~IvHistoryViewBlock() {
	if (const auto registered = _registeredBridgeHost) {
		_registeredBridgeHost = nullptr;
		_host->unregisterViewRequestBridge(registered);
	}
}

uint64 IvHistoryViewBlock::stableId() const {
	return _stableId;
}

bool IvHistoryViewBlock::supported() const {
	return _supported;
}

bool IvHistoryViewBlock::alive() const {
	return _host->itemAlive();
}

int IvHistoryViewBlock::resizeGetHeight(int width) {
	if (!_media || !alive()) {
		return 0;
	}
	_requestedWidth = std::max(width, 1);
	return _media->resizeGetHeight(_requestedWidth);
}

void IvHistoryViewBlock::setGeometry(QRect geometry) {
	if (!_media || !alive()) {
		_geometry = geometry;
		return;
	}
	const auto width = std::max(geometry.width(), 1);
	if (_requestedWidth != width) {
		_requestedWidth = width;
		_media->resizeGetHeight(_requestedWidth);
	}
	_geometry = QRect(geometry.topLeft(), _media->currentSize());
}

QRect IvHistoryViewBlock::geometry() const {
	return _geometry;
}

int IvHistoryViewBlock::firstLineBaseline() const {
	return _geometry.y();
}

void IvHistoryViewBlock::paint(
		Painter &p,
		const MarkdownArticlePaintContext &context) const {
	if (!_media || _geometry.isEmpty() || !alive()) {
		return;
	}
	const auto visible = context.clip.intersected(_geometry);
	if (visible.isEmpty()) {
		return;
	}
	p.save();
	p.translate(_geometry.topLeft());
	auto local = context.translated(-_geometry.topLeft());
	local.clip = visible.translated(-_geometry.topLeft());
	_media->draw(p, local);
	p.restore();
}

ClickHandlerPtr IvHistoryViewBlock::linkAt(QPoint point) const {
	return resolveHit(point).link;
}

MediaActivation IvHistoryViewBlock::activationAt(QPoint point) const {
	return resolveHit(point).activation;
}

MediaBlockSelectionData IvHistoryViewBlock::selectionData() const {
	return {
		.copyText = _copyText,
	};
}

bool IvHistoryViewBlock::hasHeavyPart() const {
	return alive() && _media && _media->hasHeavyPart();
}

void IvHistoryViewBlock::unloadHeavyPart() {
	if (!alive()) {
		return;
	}
	const auto had = hasHeavyPart();
	if (_media) {
		_media->unloadHeavyPart();
	}
	if (had) {
		_host->view()->checkHeavyPart();
	}
}

void IvHistoryViewBlock::hideSpoilers() {
	if (_media && alive()) {
		_media->hideSpoilers();
	}
}

std::vector<QRect> IvHistoryViewBlock::itemRects() const {
	if (!alive()) {
		return {};
	}
	const auto grouped = dynamic_cast<HistoryView::GroupedMedia*>(
		_media.get());
	if (!grouped) {
		return {};
	}
	auto result = std::vector<QRect>();
	const auto count = int(_groupedItemIndices.size());
	result.reserve(count);
	for (auto i = 0; i != count; ++i) {
		result.push_back(
			grouped->groupItemRect(i).translated(_geometry.topLeft()));
	}
	return result;
}

IvHistoryViewHit IvHistoryViewBlock::resolveHit(QPoint point) const {
	auto result = IvHistoryViewHit();
	if (!_supported || !_media || !alive() || !_geometry.contains(point)) {
		return result;
	}
	return resolveLocalHit(point - _geometry.topLeft());
}

IvHistoryViewHit IvHistoryViewBlock::resolveLocalHit(QPoint point) const {
	auto result = IvHistoryViewHit();
	if (!_media) {
		return result;
	}
	const auto state = _media->textState(
		point,
		HistoryView::StateRequest{
			.flags = Ui::Text::StateRequest::Flag::LookupLink
				| Ui::Text::StateRequest::Flag::LookupCustomTooltip,
		});
	return classifyState(state, point);
}

IvHistoryViewHit IvHistoryViewBlock::classifyState(
		const HistoryView::TextState &state,
		QPoint localPoint) const {
	return classifyHandler(state.link, localPoint);
}

IvHistoryViewHit IvHistoryViewBlock::classifyHandler(
		const ClickHandlerPtr &handler,
		QPoint localPoint) const {
	auto result = IvHistoryViewHit();
	if (!handler) {
		return result;
	}
	if (_kind == IvHistoryViewMediaKind::Photo) {
		if (std::dynamic_pointer_cast<LambdaClickHandler>(handler)) {
			if (_spoiler && _photoRuntime) {
				result.activation.kind = MediaActivationKind::Photo;
				result.activation.photo = _photoRuntime;
				return result;
			}
			result.link = handler;
			return result;
		}
		if (std::dynamic_pointer_cast<PhotoCancelClickHandler>(handler)) {
			result.link = handler;
			return result;
		}
		if ((std::dynamic_pointer_cast<PhotoOpenClickHandler>(handler)
			|| std::dynamic_pointer_cast<PhotoSaveClickHandler>(handler))
			&& _photoRuntime) {
			result.activation.kind = MediaActivationKind::Photo;
			result.activation.photo = _photoRuntime;
			return result;
		}
		result.supported = false;
		return result;
	}
	if (std::dynamic_pointer_cast<LambdaClickHandler>(handler)) {
		if (_kind == IvHistoryViewMediaKind::Document
			&& _spoiler
			&& _documentRuntime) {
			result.activation.kind = MediaActivationKind::Document;
			result.activation.document = _documentRuntime;
			return result;
		}
		if (_kind == IvHistoryViewMediaKind::GroupedMedia) {
			const auto grouped = dynamic_cast<HistoryView::GroupedMedia*>(
				_media.get());
			if (grouped) {
				const auto count = int(_groupedItemIndices.size());
				for (auto i = 0; i != count; ++i) {
					if (!grouped->groupItemRect(i).contains(localPoint)) {
						continue;
					}
					auto activation = SpoileredGroupedItemActivation(
						i,
						_groupedPhotoRuntimes,
						_groupedDocumentRuntimes,
						_groupedItemIndices,
						_groupedSpoileredIds);
					if (activation.kind != MediaActivationKind::None) {
						result.activation = std::move(activation);
						return result;
					}
					break;
				}
			}
		}
	}
	if (_kind == IvHistoryViewMediaKind::GroupedMedia) {
		auto grouped = ClassifyGroupedHandler(
			handler,
			_groupedPhotoRuntimes,
			_groupedDocumentRuntimes,
			_groupedItemIndices);
		if (grouped.activation.kind != MediaActivationKind::None
			|| !IsSupportedInteractionHandler(handler)) {
			return grouped;
		}
		result.link = handler;
		return result;
	}
	if (_kind == IvHistoryViewMediaKind::Document
		&& _documentRuntime
		&& std::dynamic_pointer_cast<DocumentSaveClickHandler>(handler)) {
		result.activation.kind = MediaActivationKind::Document;
		result.activation.document = _documentRuntime;
		return result;
	}
	if (IsSupportedInteractionHandler(handler)) {
		result.link = handler;
		return result;
	}
	if (_kind == IvHistoryViewMediaKind::Audio
		&& std::dynamic_pointer_cast<DocumentOpenClickHandler>(handler)) {
		result.link = handler;
		return result;
	}
	if (std::dynamic_pointer_cast<PhotoOpenClickHandler>(handler)
		&& _photoRuntime) {
		result.activation.kind = MediaActivationKind::Photo;
		result.activation.photo = _photoRuntime;
		return result;
	}
	if (std::dynamic_pointer_cast<DocumentOpenClickHandler>(handler)
		&& _documentRuntime) {
		result.activation.kind = MediaActivationKind::Document;
		result.activation.document = _documentRuntime;
		return result;
	}
	if (std::dynamic_pointer_cast<LocationClickHandler>(handler)) {
		result.activation = ExternalActivation(handler->url());
		return result;
	}
	if (std::dynamic_pointer_cast<UrlClickHandler>(handler)
		|| !handler->url().isEmpty()) {
		result.activation = ExternalActivation(handler->url());
		return result;
	}
	result.supported = false;
	return result;
}

bool IvHistoryViewBlock::probeSupport() {
	if (!_media) {
		return false;
	}
	switch (_kind) {
	case IvHistoryViewMediaKind::Photo:
		return supportsHitClassification();
	case IvHistoryViewMediaKind::Document:
		return supportsHitClassification();
	case IvHistoryViewMediaKind::GroupedMedia:
		return supportsHitClassification();
	case IvHistoryViewMediaKind::Map:
	case IvHistoryViewMediaKind::Audio:
		return true;
	}
	return false;
}

bool IvHistoryViewBlock::supportsHitClassification() {
	const auto width = std::max(_layoutHint.width(), 1);
	_media->resizeGetHeight(width);
	const auto size = _media->currentSize();
	if (size.isEmpty()) {
		return false;
	}
	const auto right = std::max(size.width() - 1, 0);
	const auto bottom = std::max(size.height() - 1, 0);
	const auto points = std::array{
		QPoint(size.width() / 2, size.height() / 2),
		QPoint(0, 0),
		QPoint(right, 0),
		QPoint(0, bottom),
		QPoint(right, bottom),
	};
	for (const auto &point : points) {
		if (!resolveLocalHit(point).supported) {
			return false;
		}
	}
	return true;
}

void IvHistoryViewBlock::mediaPixelScaleUpdated() {
	if (!alive()) {
		return;
	}
	const auto runtime = _host->view()->Get<
		HistoryView::InstantViewMediaRuntime>();
	if (runtime) {
		runtime->mediaPixelScale = mediaPixelScale();
	}
}

void IvHistoryViewBlock::hostUpdated() {
	if (!alive()) {
		return;
	}
	const auto current = host();
	if (_registeredBridgeHost == current) {
		return;
	}
	if (_registeredBridgeHost) {
		_host->unregisterViewRequestBridge(_registeredBridgeHost);
	}
	_registeredBridgeHost = current;
	if (_registeredBridgeHost) {
		_host->registerViewRequestBridge(_registeredBridgeHost);
	}
}

class IvHistoryViewSlideshowBlock final : public MediaBlock {
public:
	explicit IvHistoryViewSlideshowBlock(
		IvHistoryViewMediaDescriptor descriptor);
	~IvHistoryViewSlideshowBlock();

	[[nodiscard]] uint64 stableId() const override;

	[[nodiscard]] bool supported() const;

	[[nodiscard]] int resizeGetHeight(int width) override;

	void setGeometry(QRect geometry) override;

	[[nodiscard]] QRect geometry() const override;

	[[nodiscard]] int firstLineBaseline() const override;

	void paint(
		Painter &p,
		const MarkdownArticlePaintContext &context) const override;

	[[nodiscard]] ClickHandlerPtr linkAt(QPoint point) const override;

	[[nodiscard]] MediaActivation activationAt(QPoint point) const override;

	[[nodiscard]] MediaBlockSelectionData selectionData() const override;

	[[nodiscard]] bool hasHeavyPart() const override;

	void unloadHeavyPart() override;

	void hideSpoilers() override;

	[[nodiscard]] std::vector<QRect> itemRects() const override;

	[[nodiscard]] int activeItemIndex() const override;

	void setActiveItemIndex(int index) override;

private:
	[[nodiscard]] bool alive() const override;

	[[nodiscard]] HistoryView::Media *activeMedia() const;

	[[nodiscard]] int frameHeight(int width) const;

	void applyForcedSize();

	void ensureNavigationLinks();

	void stepActiveIndex(int delta);

	[[nodiscard]] IvHistoryViewHit classifyState(
		const HistoryView::TextState &state,
		HistoryView::Media *media,
		int index) const;

	[[nodiscard]] IvHistoryViewHit resolveHit(QPoint point) const;

	[[nodiscard]] bool probeSupport();

	void mediaPixelScaleUpdated() override;

	void hostUpdated() override;

	const uint64 _stableId = 0;
	const QString _copyText;
	const std::shared_ptr<IvHistoryViewMediaHost> _host;
	const std::vector<std::shared_ptr<void>> _keepAlive;
	const base::flat_map<
		uint64,
		std::shared_ptr<PhotoRuntime>> _groupedPhotoRuntimes;
	const base::flat_map<
		uint64,
		std::shared_ptr<DocumentRuntime>> _groupedDocumentRuntimes;
	const base::flat_map<uint64, int> _groupedItemIndices;
	const base::flat_set<uint64> _groupedSpoileredIds;
	std::vector<std::unique_ptr<HistoryView::Media>> _slides;
	std::vector<QSize> _slideOriginalSizes;
	QRect _geometry;
	QRect _previousRect;
	QRect _nextRect;
	ClickHandlerPtr _previousLink;
	ClickHandlerPtr _nextLink;
	int _activeIndex = 0;
	int _requestedWidth = 0;
	bool _supported = false;
	MediaBlockHost *_registeredBridgeHost = nullptr;
	mutable SlideshowDotsBackdrop _dotsBackdrop;

};

IvHistoryViewSlideshowBlock::IvHistoryViewSlideshowBlock(
	IvHistoryViewMediaDescriptor descriptor)
: _stableId(descriptor.stableId)
, _copyText(std::move(descriptor.copyText))
, _host(std::move(descriptor.host))
, _keepAlive(std::move(descriptor.keepAlive))
, _groupedPhotoRuntimes(std::move(descriptor.groupedPhotos))
, _groupedDocumentRuntimes(std::move(descriptor.groupedDocuments))
, _groupedItemIndices(std::move(descriptor.groupedItemIndices))
, _groupedSpoileredIds(std::move(descriptor.groupedSpoileredIds))
, _slideOriginalSizes(std::move(descriptor.slideOriginalSizes)) {
	_slides.reserve(descriptor.slideMediaFactories.size());
	for (const auto &factory : descriptor.slideMediaFactories) {
		auto media = factory ? factory(_host->view()) : nullptr;
		if (!media) {
			return;
		}
		media->initDimensions();
		_slides.push_back(std::move(media));
	}
	if (_slides.empty()
		|| _slides.size() != _slideOriginalSizes.size()) {
		return;
	}
	_supported = probeSupport();
}

IvHistoryViewSlideshowBlock::~IvHistoryViewSlideshowBlock() {
	if (const auto registered = _registeredBridgeHost) {
		_registeredBridgeHost = nullptr;
		_host->unregisterViewRequestBridge(registered);
	}
}

uint64 IvHistoryViewSlideshowBlock::stableId() const {
	return _stableId;
}

bool IvHistoryViewSlideshowBlock::supported() const {
	return _supported;
}

bool IvHistoryViewSlideshowBlock::alive() const {
	return _host->itemAlive();
}

HistoryView::Media *IvHistoryViewSlideshowBlock::activeMedia() const {
	return (alive() && _activeIndex >= 0 && _activeIndex < int(_slides.size()))
		? _slides[_activeIndex].get()
		: nullptr;
}

int IvHistoryViewSlideshowBlock::frameHeight(int width) const {
	const auto outer = std::max(width, 1);
	const auto &st = layoutStyle().groupedMedia;
	auto result = std::max(st.slideshowMinHeight, 1);
	for (const auto &original : _slideOriginalSizes) {
		const auto natural = MediaHeightForWidth(
			outer,
			original.width(),
			original.height());
		result = std::max(result, std::min(natural, outer));
	}
	return result;
}

int IvHistoryViewSlideshowBlock::resizeGetHeight(int width) {
	if (!alive()) {
		return 0;
	}
	_requestedWidth = std::max(width, 1);
	return frameHeight(_requestedWidth);
}

void IvHistoryViewSlideshowBlock::applyForcedSize() {
	if (_geometry.isEmpty() || !alive()) {
		return;
	}
	const auto media = activeMedia();
	if (!media) {
		return;
	}
	const auto runtime = _host->view()->Get<
		HistoryView::InstantViewMediaRuntime>();
	const auto guard = gsl::finally([&] {
		if (runtime) {
			runtime->forcedSize = QSize();
			runtime->forcedFor = nullptr;
		}
	});
	if (runtime) {
		runtime->forcedSize = _geometry.size();
		runtime->forcedFor = media;
	}
	media->resizeGetHeight(_geometry.width());
}

void IvHistoryViewSlideshowBlock::setGeometry(QRect geometry) {
	if (!alive()) {
		_geometry = geometry;
		return;
	}
	const auto width = std::max(geometry.width(), 1);
	const auto height = resizeGetHeight(width);
	_geometry = QRect(geometry.topLeft(), QSize(width, height));
	ensureNavigationLinks();
	const auto &st = layoutStyle().groupedMedia;
	if (_slides.size() >= 2) {
		const auto rects = ComputeSlideshowNavRects(
			_geometry,
			_geometry.height(),
			st.navButtonSize,
			st.navButtonSkip);
		_previousRect = rects.previous;
		_nextRect = rects.next;
	} else {
		_previousRect = QRect();
		_nextRect = QRect();
	}
	applyForcedSize();
}

QRect IvHistoryViewSlideshowBlock::geometry() const {
	return _geometry;
}

int IvHistoryViewSlideshowBlock::firstLineBaseline() const {
	return _geometry.y();
}

MediaBlockSelectionData IvHistoryViewSlideshowBlock::selectionData() const {
	return {
		.copyText = _copyText,
	};
}

bool IvHistoryViewSlideshowBlock::hasHeavyPart() const {
	return alive() && ranges::any_of(_slides, [](const auto &media) {
		return media && media->hasHeavyPart();
	});
}

void IvHistoryViewSlideshowBlock::unloadHeavyPart() {
	if (!alive()) {
		return;
	}
	const auto had = hasHeavyPart();
	for (const auto &media : _slides) {
		if (media) {
			media->unloadHeavyPart();
		}
	}
	if (had) {
		_host->view()->checkHeavyPart();
	}
}

void IvHistoryViewSlideshowBlock::hideSpoilers() {
	if (!alive()) {
		return;
	}
	for (const auto &media : _slides) {
		if (media) {
			media->hideSpoilers();
		}
	}
}

std::vector<QRect> IvHistoryViewSlideshowBlock::itemRects() const {
	if (!alive() || _geometry.isEmpty() || _slides.empty()) {
		return {};
	}
	return { _geometry };
}

int IvHistoryViewSlideshowBlock::activeItemIndex() const {
	return (_activeIndex >= 0 && _activeIndex < int(_slides.size()))
		? _activeIndex
		: -1;
}

void IvHistoryViewSlideshowBlock::setActiveItemIndex(int index) {
	const auto count = int(_slides.size());
	if (count < 1) {
		return;
	}
	const auto next = std::clamp(index, 0, count - 1);
	if (next == _activeIndex) {
		return;
	}
	_activeIndex = next;
	applyForcedSize();
	requestRepaint(_geometry);
}

void IvHistoryViewSlideshowBlock::ensureNavigationLinks() {
	if (_slides.size() < 2 || (_previousLink && _nextLink)) {
		return;
	}
	const auto weak = std::weak_ptr<IvHistoryViewSlideshowBlock>(
		std::static_pointer_cast<IvHistoryViewSlideshowBlock>(
			shared_from_this()));
	_previousLink = std::make_shared<LambdaClickHandler>([weak] {
		if (const auto block = weak.lock()) {
			block->stepActiveIndex(-1);
		}
	});
	_nextLink = std::make_shared<LambdaClickHandler>([weak] {
		if (const auto block = weak.lock()) {
			block->stepActiveIndex(1);
		}
	});
}

void IvHistoryViewSlideshowBlock::stepActiveIndex(int delta) {
	const auto count = int(_slides.size());
	if (count < 2) {
		return;
	}
	const auto next = (_activeIndex + delta % count + count) % count;
	if (next == _activeIndex) {
		return;
	}
	_activeIndex = next;
	applyForcedSize();
	requestRepaint(_geometry);
}

IvHistoryViewHit IvHistoryViewSlideshowBlock::classifyState(
		const HistoryView::TextState &state,
		HistoryView::Media *media,
		int index) const {
	auto result = IvHistoryViewHit();
	const auto &handler = state.link;
	if (!handler) {
		return result;
	}
	if (std::dynamic_pointer_cast<LambdaClickHandler>(handler)) {
		auto activation = SpoileredGroupedItemActivation(
			index,
			_groupedPhotoRuntimes,
			_groupedDocumentRuntimes,
			_groupedItemIndices,
			_groupedSpoileredIds);
		if (activation.kind != MediaActivationKind::None) {
			result.activation = std::move(activation);
			return result;
		}
	}
	auto grouped = ClassifyGroupedHandler(
		handler,
		_groupedPhotoRuntimes,
		_groupedDocumentRuntimes,
		_groupedItemIndices);
	if (grouped.activation.kind != MediaActivationKind::None) {
		grouped.activation.itemIndex = index;
		return grouped;
	}
	if (IsSupportedInteractionHandler(handler)) {
		result.link = handler;
		return result;
	}
	return grouped;
}

IvHistoryViewHit IvHistoryViewSlideshowBlock::resolveHit(QPoint point) const {
	const auto media = activeMedia();
	if (!media || !_geometry.contains(point)) {
		return IvHistoryViewHit();
	}
	const auto state = media->textState(
		point - _geometry.topLeft(),
		HistoryView::StateRequest{
			.flags = Ui::Text::StateRequest::Flag::LookupLink
				| Ui::Text::StateRequest::Flag::LookupCustomTooltip,
		});
	return classifyState(state, media, _activeIndex);
}

bool IvHistoryViewSlideshowBlock::probeSupport() {
	for (auto i = 0, count = int(_slides.size()); i != count; ++i) {
		const auto media = _slides[i].get();
		if (!media) {
			return false;
		}
		media->resizeGetHeight(std::max(_slideOriginalSizes[i].width(), 1));
		const auto size = media->currentSize();
		if (size.isEmpty()) {
			return false;
		}
		const auto right = std::max(size.width() - 1, 0);
		const auto bottom = std::max(size.height() - 1, 0);
		const auto points = std::array{
			QPoint(size.width() / 2, size.height() / 2),
			QPoint(0, 0),
			QPoint(right, 0),
			QPoint(0, bottom),
			QPoint(right, bottom),
		};
		for (const auto &point : points) {
			const auto state = media->textState(
				point,
				HistoryView::StateRequest{
					.flags = Ui::Text::StateRequest::Flag::LookupLink
						| Ui::Text::StateRequest::Flag::LookupCustomTooltip,
				});
			if (!classifyState(state, media, i).supported) {
				return false;
			}
		}
	}
	return true;
}

void IvHistoryViewSlideshowBlock::paint(
		Painter &p,
		const MarkdownArticlePaintContext &context) const {
	const auto media = activeMedia();
	if (!media || _geometry.isEmpty()) {
		return;
	}
	const auto visible = context.clip.intersected(_geometry);
	if (visible.isEmpty()) {
		return;
	}
	const auto &st = context.paintMarkdownStyle(layoutStyle()).groupedMedia;
	p.save();
	p.setClipRect(visible);
	p.setClipPath(
		RoundedRectPath(_geometry, st.radius),
		Qt::IntersectClip);

	p.save();
	p.translate(_geometry.topLeft());
	auto local = context.translated(-_geometry.topLeft());
	local.clip = visible.translated(-_geometry.topLeft());
	media->draw(p, local);
	p.restore();

	if (_slides.size() >= 2) {
		if (!_previousRect.isEmpty()) {
			const auto active = ClickHandler::showAsActive(_previousLink)
				|| ClickHandler::showAsPressed(_previousLink);
			PaintRoundButton(
				p,
				_previousRect,
				active ? st.navButtonBgOver : st.navButtonBg,
				active ? st.navPreviousIconOver : st.navPreviousIcon);
		}
		if (!_nextRect.isEmpty()) {
			const auto active = ClickHandler::showAsActive(_nextLink)
				|| ClickHandler::showAsPressed(_nextLink);
			PaintRoundButton(
				p,
				_nextRect,
				active ? st.navButtonBgOver : st.navButtonBg,
				active ? st.navNextIconOver : st.navNextIcon);
		}
		PaintSlideshowDots(
			p,
			ComputeSlideshowDots(
				_geometry,
				int(_slides.size()),
				_activeIndex,
				st),
			_activeIndex,
			st,
			_dotsBackdrop);
	}
	p.restore();
}

ClickHandlerPtr IvHistoryViewSlideshowBlock::linkAt(QPoint point) const {
	if (_slides.size() >= 2) {
		if (_previousLink && _previousRect.contains(point)) {
			return _previousLink;
		} else if (_nextLink && _nextRect.contains(point)) {
			return _nextLink;
		}
	}
	return _supported ? resolveHit(point).link : nullptr;
}

MediaActivation IvHistoryViewSlideshowBlock::activationAt(
		QPoint point) const {
	if ((_slides.size() >= 2)
		&& (_previousRect.contains(point) || _nextRect.contains(point))) {
		return MediaActivation();
	} else if (!_supported) {
		return MediaActivation();
	}
	return resolveHit(point).activation;
}

void IvHistoryViewSlideshowBlock::mediaPixelScaleUpdated() {
	if (!alive()) {
		return;
	}
	const auto runtime = _host->view()->Get<
		HistoryView::InstantViewMediaRuntime>();
	if (runtime) {
		runtime->mediaPixelScale = mediaPixelScale();
	}
}

void IvHistoryViewSlideshowBlock::hostUpdated() {
	if (!alive()) {
		return;
	}
	const auto current = host();
	if (_registeredBridgeHost == current) {
		return;
	}
	if (_registeredBridgeHost) {
		_host->unregisterViewRequestBridge(_registeredBridgeHost);
	}
	_registeredBridgeHost = current;
	if (_registeredBridgeHost) {
		_host->registerViewRequestBridge(_registeredBridgeHost);
	}
}

} // namespace

struct IvHistoryViewMediaHost::State {
	State(
		not_null<Window::SessionController*> controller,
		not_null<History*> history,
		QString pageUrl);
	State(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item);
	explicit State(not_null<HistoryView::Element*> view);

	void watchItemLifetime();
	void handleItemDeath();

	const not_null<::Data::Session*> session;
	const QString pageUrl;
	const std::unique_ptr<IvHistoryViewDelegate> delegate;
	const not_null<HistoryItem*> item;
	AdminLog::OwnedItem owned;
	std::unique_ptr<HistoryView::Element> realView;
	HistoryView::Element *view = nullptr;
	bool needsViewRequestBridge = true;
	MediaBlockHost *bridgeHost = nullptr;
	int bridgeHostReferences = 0;
	rpl::lifetime bridgeLifetime;
	const FullMsgId itemId;
	bool itemDead = false;
	bool itemTornDown = false;
	rpl::lifetime itemDeathLifetime;
};

IvHistoryViewMediaHost::State::State(
	not_null<Window::SessionController*> controller,
	not_null<History*> history,
	QString pageUrl)
: session(&history->owner())
, pageUrl(std::move(pageUrl))
, delegate(std::make_unique<IvHistoryViewDelegate>(
	controller,
	session,
	[=] {
		if (view) {
			view->repaint();
		}
	}))
, item(CreateIvHostMessage(history, this->pageUrl))
, owned(delegate.get(), item)
, view(static_cast<HistoryView::Message*>(owned.get()))
, itemId(item->fullId()) {
	static_cast<HistoryView::Message*>(view)->setInstantViewMediaRuntime(
		this->pageUrl);
	watchItemLifetime();
}

IvHistoryViewMediaHost::State::State(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item)
: session(&item->history()->owner())
, delegate(std::make_unique<IvHistoryViewDelegate>(
	controller,
	session,
	[=] {
		if (view) {
			view->repaint();
		}
	}))
, item(item)
, realView(this->item->createView(delegate.get()))
, view(static_cast<HistoryView::Message*>(realView.get()))
, itemId(this->item->fullId()) {
	static_cast<HistoryView::Message*>(view)->setInstantViewMediaRuntime(
		this->pageUrl);
	watchItemLifetime();
}

IvHistoryViewMediaHost::State::State(
	not_null<HistoryView::Element*> view)
: session(&view->history()->owner())
, item(view->data())
, view(view.get())
, needsViewRequestBridge(false)
, itemId(this->item->fullId()) {
	watchItemLifetime();
}

void IvHistoryViewMediaHost::State::watchItemLifetime() {
	session->itemRemoved(
		itemId
	) | rpl::on_next([this](not_null<const HistoryItem*>) {
		handleItemDeath();
	}, itemDeathLifetime);
}

void IvHistoryViewMediaHost::State::handleItemDeath() {
	if (itemTornDown) {
		return;
	}
	itemTornDown = true;
	itemDead = true;
	itemDeathLifetime.destroy();
	bridgeLifetime.destroy();
	view = nullptr;
	realView = nullptr;
	owned = {};
}

IvHistoryViewMediaHost::IvHistoryViewMediaHost(
	not_null<Window::SessionController*> controller,
	not_null<History*> history,
	QString pageUrl)
: _state(std::make_unique<State>(
	controller,
	history,
	std::move(pageUrl))) {
}

IvHistoryViewMediaHost::IvHistoryViewMediaHost(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item)
: _state(std::make_unique<State>(controller, item)) {
}

IvHistoryViewMediaHost::IvHistoryViewMediaHost(
	not_null<HistoryView::Element*> view)
: _state(std::make_unique<State>(view)) {
}

IvHistoryViewMediaHost::~IvHistoryViewMediaHost() = default;

not_null<::Data::Session*> IvHistoryViewMediaHost::session() const {
	return _state->session;
}

not_null<HistoryItem*> IvHistoryViewMediaHost::item() const {
	return _state->item;
}

bool IvHistoryViewMediaHost::itemAlive() const {
	return !_state->itemDead;
}

not_null<HistoryView::Element*> IvHistoryViewMediaHost::view() const {
	return not_null<HistoryView::Element*>{ _state->view };
}

const QString &IvHistoryViewMediaHost::pageUrl() const {
	return _state->pageUrl;
}

bool IvHistoryViewMediaHost::needsViewRequestBridge() const {
	return _state->needsViewRequestBridge;
}

void IvHistoryViewMediaHost::registerViewRequestBridge(MediaBlockHost *host) {
	if (!host || !_state->needsViewRequestBridge) {
		return;
	}
	if (_state->bridgeHost == host) {
		++_state->bridgeHostReferences;
		return;
	}
	_state->bridgeLifetime.destroy();
	_state->bridgeHost = host;
	_state->bridgeHostReferences = 1;
	_state->session->viewRepaintRequest(
	) | rpl::filter([=](::Data::RequestViewRepaint data) {
		return (data.view == _state->view);
	}) | rpl::on_next([=](::Data::RequestViewRepaint) {
		if (_state->bridgeHost) {
			_state->bridgeHost->requestRepaint(QRect());
		}
	}, _state->bridgeLifetime);
	_state->session->viewResizeRequest(
	) | rpl::filter([=](not_null<HistoryView::Element*> view) {
		return (view == _state->view);
	}) | rpl::on_next([=](not_null<HistoryView::Element*>) {
		if (_state->bridgeHost) {
			_state->bridgeHost->requestRelayout(QRect());
		}
	}, _state->bridgeLifetime);
}

void IvHistoryViewMediaHost::unregisterViewRequestBridge(MediaBlockHost *host) {
	if (!host
		|| !_state->needsViewRequestBridge
		|| _state->bridgeHost != host) {
		return;
	}
	--_state->bridgeHostReferences;
	if (_state->bridgeHostReferences > 0) {
		return;
	}
	_state->bridgeHostReferences = 0;
	_state->bridgeHost = nullptr;
	_state->bridgeLifetime.destroy();
}

void IvHistoryViewMediaHost::registerPhoto(not_null<PhotoData*> photo) const {
	_state->item->addPhotoForInstantView(photo);
}

void IvHistoryViewMediaHost::registerDocument(
		not_null<DocumentData*> document) const {
	_state->item->addDocumentForInstantView(document);
}

IvHistoryViewMediaBlockFactory::IvHistoryViewMediaBlockFactory(
	base::weak_ptr<Window::SessionController> controller,
	PhotoFactory createPhoto,
	VideoFactory createVideo,
	AudioFactory createAudio,
	MapFactory createMap,
	GroupedMediaFactory createGroupedMedia)
: _controller(std::move(controller))
, _createPhoto(std::move(createPhoto))
, _createVideo(std::move(createVideo))
, _createAudio(std::move(createAudio))
, _createMap(std::move(createMap))
, _createGroupedMedia(std::move(createGroupedMedia)) {
}

std::shared_ptr<MediaBlock> IvHistoryViewMediaBlockFactory::createPhoto(
		const PreparedPhotoBlockData &prepared) const {
	return create(prepared, _createPhoto);
}

std::shared_ptr<MediaBlock> IvHistoryViewMediaBlockFactory::createVideo(
		const PreparedVideoBlockData &prepared) const {
	return create(prepared, _createVideo);
}

std::shared_ptr<MediaBlock> IvHistoryViewMediaBlockFactory::createAudio(
		const PreparedAudioBlockData &prepared) const {
	return create(prepared, _createAudio);
}

std::shared_ptr<MediaBlock> IvHistoryViewMediaBlockFactory::createMap(
		const PreparedMapBlockData &prepared) const {
	return create(prepared, _createMap);
}

auto IvHistoryViewMediaBlockFactory::createGroupedMedia(
	const PreparedGroupedMediaBlockData &prepared) const
-> std::shared_ptr<MediaBlock> {
	return create(prepared, _createGroupedMedia);
}

std::shared_ptr<MediaBlock> CreateIvHistoryViewMediaBlock(
		IvHistoryViewMediaDescriptor descriptor) {
	if (!descriptor.host) {
		return nullptr;
	}
	if (!descriptor.mediaFactory
		&& descriptor.slideMediaFactories.empty()) {
		return nullptr;
	}
	if (descriptor.kind == IvHistoryViewMediaKind::Slideshow) {
		const auto block = std::make_shared<IvHistoryViewSlideshowBlock>(
			std::move(descriptor));
		return block->supported() ? block : nullptr;
	}
	const auto block = std::make_shared<IvHistoryViewBlock>(
		std::move(descriptor));
	return block->supported() ? block : nullptr;
}

} // namespace Iv::Markdown
