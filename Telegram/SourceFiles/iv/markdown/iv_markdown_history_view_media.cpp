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
#include "ui/effects/animations.h"
#include "ui/image/image_location.h"
#include "data/data_types.h"
#include "data/data_auto_download.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_click_handler.h"
#include "data/data_media_preload.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
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
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "ui/basic_click_handlers.h"
#include "window/window_session_controller.h"

#include "rpl/event_stream.h"
#include "rpl/filter.h"
#include "rpl/lifetime.h"

#include <utility>

namespace Iv::Markdown {
namespace {

constexpr auto kSlideshowSlideDuration = crl::time(200);
constexpr auto kSlideshowCommitRatio = 0.3;
constexpr auto kSlideshowWheelStep = 60.;
constexpr auto kSlideshowWheelResetDelay = crl::time(300);
constexpr auto kSlideshowVelocityWindow = crl::time(100);
constexpr auto kSlideshowCommitVelocity = 0.5;

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

	void clickHandlerActiveChanged(
		const ClickHandlerPtr &handler,
		bool active) override;

	void clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) override;

	void updatePressed(QPoint point) override;

	[[nodiscard]] MediaBlockSelectionData selectionData() const override;

	[[nodiscard]] bool hasHeavyPart() const override;

	void unloadHeavyPart() override;

	void hideSpoilers() override;

	[[nodiscard]] std::vector<QRect> itemRects() const override;

private:
	[[nodiscard]] bool alive() const override;

	[[nodiscard]] bool acceptsVoiceSeekHandler(
		const ClickHandlerPtr &handler) const;

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
	const bool _editMode = false;
	const std::shared_ptr<IvHistoryViewMediaHost> _host;
	const std::vector<std::shared_ptr<void>> _keepAlive;
	std::unique_ptr<HistoryView::Media> _media;
	rpl::lifetime _itemDeathLifetime;
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
, _editMode(descriptor.editMode)
, _host(std::move(descriptor.host))
, _keepAlive(std::move(descriptor.keepAlive)) {
	if (descriptor.mediaFactory) {
		_media = descriptor.mediaFactory(_host->view());
	}
	if (_media) {
		_media->initDimensions();
	}
	_supported = _media && probeSupport();

	// We outlive the view our media is parented to, so drop the media while
	// that view is still alive.
	_host->itemDeath() | rpl::on_next([this] {
		_media = nullptr;
	}, _itemDeathLifetime);
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

void IvHistoryViewBlock::clickHandlerActiveChanged(
		const ClickHandlerPtr &handler,
		bool active) {
	if (acceptsVoiceSeekHandler(handler)) {
		_media->clickHandlerActiveChanged(handler, active);
	}
}

void IvHistoryViewBlock::clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) {
	if (acceptsVoiceSeekHandler(handler)) {
		_media->clickHandlerPressedChanged(handler, pressed);
	}
}

void IvHistoryViewBlock::updatePressed(QPoint point) {
	if (acceptsVoiceSeekHandler(ClickHandler::getPressed())) {
		_media->updatePressed(point - _geometry.topLeft());
	}
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

bool IvHistoryViewBlock::acceptsVoiceSeekHandler(
		const ClickHandlerPtr &handler) const {
	return _supported
		&& _media
		&& alive()
		&& (_kind == IvHistoryViewMediaKind::DocumentRow)
		&& std::dynamic_pointer_cast<VoiceSeekClickHandler>(handler);
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
			if (_editMode && _spoiler && _photoRuntime) {
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
			&& _editMode
			&& _spoiler
			&& _documentRuntime) {
			result.activation.kind = MediaActivationKind::Document;
			result.activation.document = _documentRuntime;
			return result;
		}
		if (_editMode && _kind == IvHistoryViewMediaKind::GroupedMedia) {
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
	if (_kind == IvHistoryViewMediaKind::DocumentRow
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
	case IvHistoryViewMediaKind::DocumentRow:
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

	[[nodiscard]] bool canHandleHorizontalScroll() const override;

	bool handleHorizontalScroll(int delta, Qt::ScrollPhase phase) override;

private:
	[[nodiscard]] bool alive() const override;

	[[nodiscard]] HistoryView::Media *activeMedia() const;

	[[nodiscard]] int frameHeight(int width) const;

	void applyForcedSize();

	void applyForcedSizeFor(not_null<HistoryView::Media*> media);

	void ensureNavigationLinks();

	void stepActiveIndex(int delta);

	void startTransition(int from, int direction, float64 progress = 0.);

	[[nodiscard]] bool gestureActive() const;
	void adoptTransitionOffset();
	void beginDrag();
	void applyDragDelta(int delta);
	void finishDrag();
	void commitDrag(int direction);
	void revertDrag();
	void cancelDrag();
	bool handleMomentum(int delta);
	void handlePhaselessScroll(int delta);
	void registerDragSample(float64 offsetDelta);
	[[nodiscard]] float64 takeDragVelocity();

	struct DragSample {
		crl::time time = 0;
		float64 delta = 0.;
	};

	struct SlidePreload {
		std::shared_ptr<::Data::PhotoMedia> photo;
		std::shared_ptr<::Data::DocumentMedia> document;
		std::unique_ptr<::Data::VideoPreload> video;
		bool ready = false;
	};

	[[nodiscard]] int slideIndexAfter(int index, int delta) const;
	[[nodiscard]] bool activeSlideReady() const;
	[[nodiscard]] bool preloadReady(int index);
	void startPreload(int index);
	void releaseSlideRuntimes(int index);
	void refreshResidency();
	void updatePreloads();

	[[nodiscard]] IvHistoryViewHit classifyState(
		const HistoryView::TextState &state,
		HistoryView::Media *media,
		int index) const;

	[[nodiscard]] IvHistoryViewHit resolveHit(QPoint point) const;

	void paintSlide(
		Painter &p,
		const MarkdownArticlePaintContext &context,
		QRect visible,
		not_null<HistoryView::Media*> media,
		int shift) const;

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
	const ::Data::FileOrigin _fileOrigin;
	const bool _editMode = false;
	std::vector<std::unique_ptr<HistoryView::Media>> _slides;
	rpl::lifetime _itemDeathLifetime;
	std::vector<QSize> _slideOriginalSizes;
	QRect _geometry;
	QRect _previousRect;
	QRect _nextRect;
	ClickHandlerPtr _previousLink;
	ClickHandlerPtr _nextLink;
	int _activeIndex = 0;
	Ui::Animations::Simple _slideAnimation;
	int _transitionFrom = -1;
	int _transitionDirection = 0;
	float64 _dragOffset = 0.;
	bool _dragActive = false;
	bool _dragReverting = false;
	bool _momentumIgnore = false;
	std::vector<DragSample> _dragSamples;
	float64 _wheelAccumulated = 0.;
	crl::time _wheelLastTime = 0;
	int _requestedWidth = 0;
	bool _supported = false;
	MediaBlockHost *_registeredBridgeHost = nullptr;
	mutable SlideshowDotsBackdrop _dotsBackdrop;
	base::flat_map<int, SlidePreload> _preloads;
	rpl::lifetime _downloadLifetime;
	int _residencyIndex = -1;
	bool _painted = false;

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
, _fileOrigin(descriptor.fileOrigin)
, _editMode(descriptor.editMode)
, _slideOriginalSizes(std::move(descriptor.slideOriginalSizes)) {
	// Subscribed before the loop below, which can return early with some
	// slides already created. We outlive the view they are parented to, so
	// they must be dropped while it is still alive. Nulled in place rather
	// than erased, so the slide indices stay valid.
	_host->itemDeath() | rpl::on_next([this] {
		for (auto &media : _slides) {
			media = nullptr;
		}
	}, _itemDeathLifetime);

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
	if (_supported && _slides.size() > 1) {
		_host->session()->session().downloaderTaskFinished(
		) | rpl::on_next([=] {
			updatePreloads();
		}, _downloadLifetime);
	}
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

void IvHistoryViewSlideshowBlock::applyForcedSizeFor(
		not_null<HistoryView::Media*> media) {
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

void IvHistoryViewSlideshowBlock::applyForcedSize() {
	if (_geometry.isEmpty() || !alive()) {
		return;
	}
	if (const auto media = activeMedia()) {
		applyForcedSizeFor(media);
	}
	const auto count = int(_slides.size());
	if (_dragOffset != 0.) {
		const auto entering = slideIndexAfter(
			_activeIndex,
			(_dragOffset > 0.) ? 1 : -1);
		if (const auto media = _slides[entering].get()) {
			applyForcedSizeFor(media);
		}
	}
	if (_transitionFrom < 0 || _transitionFrom >= count) {
		return;
	}
	if (const auto media = _slides[_transitionFrom].get()) {
		applyForcedSizeFor(media);
	}
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
	return alive()
		&& (!_preloads.empty()
			|| ranges::any_of(_slides, [](const auto &media) {
				return media && media->hasHeavyPart();
			}));
}

void IvHistoryViewSlideshowBlock::unloadHeavyPart() {
	if (!alive()) {
		return;
	}
	const auto had = hasHeavyPart();
	_slideAnimation.stop();
	_transitionFrom = -1;
	_transitionDirection = 0;
	_dragActive = false;
	_dragOffset = 0.;
	_dragReverting = false;
	_momentumIgnore = false;
	_dragSamples.clear();
	_wheelAccumulated = 0.;
	_wheelLastTime = 0;
	_preloads.clear();
	_residencyIndex = -1;
	_painted = false;
	for (const auto &[id, runtime] : _groupedPhotoRuntimes) {
		runtime->releaseHeavyData();
	}
	for (const auto &[id, runtime] : _groupedDocumentRuntimes) {
		runtime->releaseHeavyData();
	}
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
	cancelDrag();
	const auto from = _activeIndex;
	_activeIndex = next;
	startTransition(from, (next > from) ? 1 : -1);
	updatePreloads();
	requestRepaint(_geometry);
}

bool IvHistoryViewSlideshowBlock::canHandleHorizontalScroll() const {
	return alive() && (_slides.size() > 1);
}

bool IvHistoryViewSlideshowBlock::handleHorizontalScroll(
		int delta,
		Qt::ScrollPhase phase) {
	if (!alive() || _slides.size() < 2 || _geometry.isEmpty()) {
		return false;
	}
	switch (phase) {
	case Qt::NoScrollPhase:
		if (gestureActive()) {
			finishDrag();
		}
		handlePhaselessScroll(delta);
		return true;
	case Qt::ScrollBegin:
		if (gestureActive()) {
			finishDrag();
		}
		beginDrag();
		applyDragDelta(delta);
		return true;
	case Qt::ScrollUpdate:
		if (!_dragActive) {
			beginDrag();
		}
		applyDragDelta(delta);
		return true;
	case Qt::ScrollEnd:
		if (_dragActive) {
			finishDrag();
		}
		return true;
	case Qt::ScrollMomentum:
		return handleMomentum(delta);
	}
	return false;
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
	cancelDrag();
	const auto from = _activeIndex;
	_activeIndex = next;
	startTransition(from, (delta > 0) ? 1 : -1);
	updatePreloads();
	requestRepaint(_geometry);
}

bool IvHistoryViewSlideshowBlock::gestureActive() const {
	return _dragActive || (_dragOffset != 0.);
}

void IvHistoryViewSlideshowBlock::adoptTransitionOffset() {
	if (!_slideAnimation.animating() || _transitionFrom < 0) {
		return;
	}
	_dragOffset = -_transitionDirection
		* _geometry.width()
		* (1. - _slideAnimation.value(1.));
	_slideAnimation.stop();
	_transitionFrom = -1;
	_transitionDirection = 0;
	_residencyIndex = -1;
	_dragReverting = false;
}

void IvHistoryViewSlideshowBlock::beginDrag() {
	adoptTransitionOffset();
	_dragReverting = false;
	_dragActive = true;
	_momentumIgnore = false;
	_dragSamples.clear();
}

void IvHistoryViewSlideshowBlock::applyDragDelta(int delta) {
	const auto width = _geometry.width();
	registerDragSample(-delta);
	_dragOffset = std::clamp(_dragOffset - delta, -1. * width, 1. * width);
	applyForcedSize();
	requestRepaint(_geometry);
}

void IvHistoryViewSlideshowBlock::finishDrag() {
	const auto direction = (_dragOffset > 0.)
		? 1
		: (_dragOffset < 0.)
		? -1
		: 0;
	_dragActive = false;
	_momentumIgnore = true;
	const auto velocity = takeDragVelocity();
	const auto fast = style::ConvertFloatScale(kSlideshowCommitVelocity);
	const auto towards = velocity * direction;
	const auto threshold = _geometry.width() * kSlideshowCommitRatio;
	const auto commit = (direction != 0)
		&& ((towards >= fast)
			|| ((std::abs(_dragOffset) >= threshold) && (towards > -fast)));
	if (commit) {
		commitDrag(direction);
	} else {
		revertDrag();
	}
}

void IvHistoryViewSlideshowBlock::registerDragSample(float64 offsetDelta) {
	const auto now = crl::now();
	while (!_dragSamples.empty()
		&& (now - _dragSamples.front().time > kSlideshowVelocityWindow)) {
		_dragSamples.erase(_dragSamples.begin());
	}
	_dragSamples.push_back({ now, offsetDelta });
}

float64 IvHistoryViewSlideshowBlock::takeDragVelocity() {
	const auto samples = base::take(_dragSamples);
	const auto now = crl::now();
	auto total = 0.;
	auto oldest = now;
	auto count = 0;
	for (const auto &sample : samples) {
		if (now - sample.time > kSlideshowVelocityWindow) {
			continue;
		}
		oldest = std::min(oldest, sample.time);
		total += sample.delta;
		++count;
	}
	const auto duration = now - oldest;
	return (count < 2 || duration <= 0) ? 0. : (total / duration);
}

void IvHistoryViewSlideshowBlock::commitDrag(int direction) {
	const auto width = std::max(_geometry.width(), 1);
	const auto progress = std::clamp(std::abs(_dragOffset) / width, 0., 1.);
	_dragOffset = 0.;
	const auto from = _activeIndex;
	_activeIndex = slideIndexAfter(_activeIndex, direction);
	startTransition(from, direction, progress);
	updatePreloads();
	requestRepaint(_geometry);
}

void IvHistoryViewSlideshowBlock::revertDrag() {
	const auto offset = base::take(_dragOffset);
	if (offset == 0. || _geometry.width() <= 0) {
		requestRepaint(_geometry);
		updatePreloads();
		return;
	}
	const auto direction = (offset > 0.) ? 1 : -1;
	_dragReverting = true;
	startTransition(
		slideIndexAfter(_activeIndex, direction),
		-direction,
		1. - std::abs(offset) / _geometry.width());
}

void IvHistoryViewSlideshowBlock::cancelDrag() {
	_dragActive = false;
	_dragOffset = 0.;
	_dragReverting = false;
	_momentumIgnore = false;
	_dragSamples.clear();
}

bool IvHistoryViewSlideshowBlock::handleMomentum(int delta) {
	if (_dragActive) {
		// On Windows a fling delivers momentum events right away,
		// without any ScrollEnd between the finger-driven scroll and
		// the inertial one, so the first momentum event is the earliest
		// sign that the fingers were lifted - decide here and ignore
		// the rest of the fling.
		registerDragSample(-delta);
		finishDrag();
		return true;
	}
	return _momentumIgnore;
}

void IvHistoryViewSlideshowBlock::handlePhaselessScroll(int delta) {
	const auto now = crl::now();
	const auto incoming = float64(-delta);
	if (!_wheelLastTime
		|| (now - _wheelLastTime > kSlideshowWheelResetDelay)
		|| (incoming * _wheelAccumulated < 0.)) {
		_wheelAccumulated = 0.;
	}
	_wheelAccumulated += incoming;
	_wheelLastTime = now;
	const auto step = style::ConvertFloatScale(kSlideshowWheelStep);
	while (std::abs(_wheelAccumulated) >= step) {
		const auto direction = (_wheelAccumulated > 0.) ? 1 : -1;
		_wheelAccumulated -= direction * step;
		stepActiveIndex(direction);
	}
}

void IvHistoryViewSlideshowBlock::startTransition(
		int from,
		int direction,
		float64 progress) {
	if (_geometry.isEmpty() || !_painted) {
		applyForcedSize();
		return;
	}
	_transitionFrom = from;
	_transitionDirection = direction;
	applyForcedSize();
	_slideAnimation.stop();
	_slideAnimation.start([=] {
		requestRepaint(_geometry);
	}, progress, 1., kSlideshowSlideDuration, anim::easeOutCirc);
	_slideAnimation.setFinishedCallback([=] {
		_transitionFrom = -1;
		_transitionDirection = 0;
		_dragReverting = false;
		_residencyIndex = -1;
		requestRepaint(_geometry);
		updatePreloads();
	});
}

int IvHistoryViewSlideshowBlock::slideIndexAfter(
		int index,
		int delta) const {
	const auto count = int(_slides.size());
	return ((index + delta) % count + count) % count;
}

bool IvHistoryViewSlideshowBlock::activeSlideReady() const {
	const auto media = activeMedia();
	if (!media) {
		return false;
	}
	if (const auto photo = media->getPhoto()) {
		const auto view = photo->activeMediaView();
		return view && view->loaded();
	} else if (const auto document = media->getDocument()) {
		const auto view = document->activeMediaView();
		return view && view->canBePlayed();
	}
	return false;
}

bool IvHistoryViewSlideshowBlock::preloadReady(int index) {
	const auto i = _preloads.find(index);
	if (i == end(_preloads)) {
		return false;
	}
	auto &entry = i->second;
	if (!entry.ready) {
		if (entry.photo && entry.photo->loaded()) {
			entry.ready = true;
		} else if (entry.document
			&& !entry.video
			&& entry.document->canBePlayed()) {
			entry.ready = true;
		}
	}
	return entry.ready;
}

void IvHistoryViewSlideshowBlock::startPreload(int index) {
	const auto media = _slides[index].get();
	auto &entry = _preloads[index];
	if (const auto photo = media->getPhoto()) {
		entry.photo = photo->createMediaView();
		if (entry.photo->loaded()) {
			entry.ready = true;
		} else if (::Data::PhotoPreload::Should(
				photo,
				_host->item()->history()->peer)) {
			photo->load(_fileOrigin, LoadFromCloudOrLocal, true);
		} else {
			entry.photo->wanted(::Data::PhotoSize::Small, _fileOrigin);
			entry.ready = true;
		}
	} else if (const auto document = media->getDocument()) {
		entry.document = document->createMediaView();
		entry.document->goodThumbnailWanted();
		entry.document->thumbnailWanted(_fileOrigin);
		const auto autoplay = ::Data::AutoDownload::ShouldAutoPlay(
			document->session().settings().autoDownload(),
			_host->item()->history()->peer,
			document);
		if (!autoplay) {
			entry.document->videoThumbnailWanted(_fileOrigin);
		}
		if (::Data::VideoPreload::Can(document)) {
			const auto weak = std::weak_ptr<IvHistoryViewSlideshowBlock>(
				std::static_pointer_cast<IvHistoryViewSlideshowBlock>(
					shared_from_this()));
			entry.video = std::make_unique<::Data::VideoPreload>(
				document,
				_fileOrigin,
				[weak, index] {
					if (const auto block = weak.lock()) {
						const auto i = block->_preloads.find(index);
						if (i != end(block->_preloads)) {
							i->second.ready = true;
						}
						block->updatePreloads();
					}
				});
		} else if (entry.document->canBePlayed()) {
			entry.ready = true;
		} else {
			entry.document->automaticLoad(_fileOrigin, _host->item());
			if (!document->loading()) {
				entry.ready = true;
			}
		}
	} else {
		entry.ready = true;
	}
}

void IvHistoryViewSlideshowBlock::releaseSlideRuntimes(int index) {
	for (const auto &[mediaId, itemIndex] : _groupedItemIndices) {
		if (itemIndex != index) {
			continue;
		}
		const auto photo = _groupedPhotoRuntimes.find(mediaId);
		if (photo != end(_groupedPhotoRuntimes)) {
			photo->second->releaseHeavyData();
		}
		const auto document = _groupedDocumentRuntimes.find(mediaId);
		if (document != end(_groupedDocumentRuntimes)) {
			document->second->releaseHeavyData();
		}
	}
}

void IvHistoryViewSlideshowBlock::refreshResidency() {
	const auto count = int(_slides.size());
	if (!alive() || !count) {
		return;
	}
	_residencyIndex = _activeIndex;
	if (count < 2) {
		return;
	}
	const auto next = slideIndexAfter(_activeIndex, 1);
	const auto previous = slideIndexAfter(_activeIndex, -1);
	for (auto i = 0; i != count; ++i) {
		if (i == _activeIndex || i == next || i == previous) {
			continue;
		}
		const auto media = _slides[i].get();
		if (media && media->hasHeavyPart()) {
			media->unloadHeavyPart();
		}
		releaseSlideRuntimes(i);
		_preloads.remove(i);
	}
	for (const auto index : { next, previous }) {
		if (index != _activeIndex) {
			if (const auto media = _slides[index].get()) {
				media->stopAnimation();
			}
		}
	}
}

void IvHistoryViewSlideshowBlock::updatePreloads() {
	const auto count = int(_slides.size());
	if (!_painted || !alive() || count < 2) {
		return;
	}
	if (_residencyIndex != _activeIndex
		&& _transitionFrom < 0
		&& !gestureActive()) {
		refreshResidency();
	}
	if (!activeSlideReady()) {
		return;
	}
	const auto next = slideIndexAfter(_activeIndex, 1);
	if (next == _activeIndex) {
		return;
	}
	if (!_preloads.contains(next)) {
		startPreload(next);
	}
	if (!preloadReady(next)) {
		return;
	}
	const auto previous = slideIndexAfter(_activeIndex, -1);
	if (previous != _activeIndex
		&& previous != next
		&& !_preloads.contains(previous)) {
		startPreload(previous);
	}
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
	if (_editMode
		&& std::dynamic_pointer_cast<LambdaClickHandler>(handler)) {
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

void IvHistoryViewSlideshowBlock::paintSlide(
		Painter &p,
		const MarkdownArticlePaintContext &context,
		QRect visible,
		not_null<HistoryView::Media*> media,
		int shift) const {
	p.save();
	const auto origin = _geometry.topLeft() + QPoint(shift, 0);
	p.translate(origin);
	auto local = context.translated(-origin);
	local.clip = visible.translated(-origin);
	media->draw(p, local);
	p.restore();
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

	const auto count = int(_slides.size());
	const auto transition = _slideAnimation.animating()
		&& (_transitionFrom >= 0)
		&& (_transitionFrom < count)
		&& (_transitionFrom != _activeIndex);
	const auto dragging = !transition && (_dragOffset != 0.);
	if (transition) {
		const auto progress = _slideAnimation.value(1.);
		const auto width = _geometry.width();
		const auto direction = _transitionDirection;
		const auto shift = anim::interpolate(
			0,
			-direction * width,
			progress);
		if (const auto outgoing = _slides[_transitionFrom].get()) {
			paintSlide(p, context, visible, outgoing, shift);
		}
		paintSlide(p, context, visible, media, shift + direction * width);
		if (progress > 1.) {
			const auto filler = _slides[slideIndexAfter(
				_activeIndex,
				direction)].get();
			if (filler) {
				paintSlide(
					p,
					context,
					visible,
					filler,
					shift + 2 * direction * width);
			}
		}
	} else if (dragging) {
		const auto width = _geometry.width();
		const auto offset = int(std::round(
			std::clamp(_dragOffset, -1. * width, 1. * width)));
		const auto direction = (offset > 0) ? 1 : -1;
		paintSlide(p, context, visible, media, -offset);
		const auto entering = _slides[slideIndexAfter(
			_activeIndex,
			direction)].get();
		if (entering) {
			paintSlide(
				p,
				context,
				visible,
				entering,
				-offset + direction * width);
		}
	} else {
		paintSlide(p, context, visible, media, 0);
	}

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
		const auto dotsIndex = (transition && !_dragReverting)
			? _transitionFrom
			: _activeIndex;
		PaintSlideshowDots(
			p,
			ComputeSlideshowDots(
				_geometry,
				int(_slides.size()),
				dotsIndex,
				st),
			dotsIndex,
			st,
			_dotsBackdrop);
	}
	p.restore();
	const auto that = const_cast<IvHistoryViewSlideshowBlock*>(this);
	that->_painted = true;
	that->updatePreloads();
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
	rpl::event_stream<> itemDeaths;
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

	// Blocks outlive the view and own medias parented to it, so they must
	// drop them while it is still alive: ~Photo / ~Gif call
	// _parent->checkHeavyPart(), which is what takes the view back out of
	// Data::Session::_heavyViewParts.
	itemDeaths.fire({});

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

rpl::producer<> IvHistoryViewMediaHost::itemDeath() const {
	return _state->itemDeaths.events();
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
	DocumentBlockFactory createDocument,
	MapFactory createMap,
	GroupedMediaFactory createGroupedMedia)
: _controller(std::move(controller))
, _createPhoto(std::move(createPhoto))
, _createVideo(std::move(createVideo))
, _createDocument(std::move(createDocument))
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

std::shared_ptr<MediaBlock> IvHistoryViewMediaBlockFactory::createDocument(
		const PreparedDocumentBlockData &prepared) const {
	return create(prepared, _createDocument);
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
