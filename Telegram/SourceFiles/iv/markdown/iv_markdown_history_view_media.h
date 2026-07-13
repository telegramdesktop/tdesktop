/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_common.h"
#include "base/flat_map.h"
#include "base/flat_set.h"
#include "base/weak_ptr.h"

#include <functional>
#include <memory>
#include <vector>

namespace Window {
class SessionController;
} // namespace Window

class DocumentData;
class History;
class HistoryItem;
class PhotoData;

namespace HistoryView {
class Element;
class Media;
class Message;
} // namespace HistoryView

namespace Data {
class Session;
} // namespace Data

namespace Iv::Markdown {

class MediaBlockHost;

class IvHistoryViewMediaHost final {
public:
	IvHistoryViewMediaHost(
		not_null<Window::SessionController*> controller,
		not_null<History*> history,
		QString pageUrl);
	IvHistoryViewMediaHost(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item);
	explicit IvHistoryViewMediaHost(
		not_null<HistoryView::Element*> view);
	~IvHistoryViewMediaHost();

	[[nodiscard]] not_null<::Data::Session*> session() const;
	[[nodiscard]] not_null<HistoryItem*> item() const;
	[[nodiscard]] bool itemAlive() const;
	[[nodiscard]] not_null<HistoryView::Element*> view() const;
	[[nodiscard]] const QString &pageUrl() const;
	[[nodiscard]] bool needsViewRequestBridge() const;
	void registerViewRequestBridge(MediaBlockHost *host);
	void unregisterViewRequestBridge(MediaBlockHost *host);

	void registerPhoto(not_null<PhotoData*> photo) const;
	void registerDocument(not_null<DocumentData*> document) const;

private:
	struct State;
	std::unique_ptr<State> _state;
};

enum class IvHistoryViewMediaKind {
	Photo,
	Document,
	Map,
	Audio,
	GroupedMedia,
	Slideshow,
};

struct IvHistoryViewMediaDescriptor {
	using MediaFactory = std::function<std::unique_ptr<HistoryView::Media>(
		not_null<HistoryView::Element*> view)>;

	uint64 stableId = 0;
	IvHistoryViewMediaKind kind = IvHistoryViewMediaKind::Map;
	QString copyText;
	QSize layoutHint;
	std::shared_ptr<IvHistoryViewMediaHost> host;
	MediaFactory mediaFactory;
	std::vector<MediaFactory> slideMediaFactories;
	std::vector<QSize> slideOriginalSizes;
	std::vector<std::shared_ptr<void>> keepAlive;
	std::shared_ptr<PhotoRuntime> photo;
	std::shared_ptr<DocumentRuntime> document;
	base::flat_map<uint64, std::shared_ptr<PhotoRuntime>> groupedPhotos;
	base::flat_map<
		uint64,
		std::shared_ptr<DocumentRuntime>> groupedDocuments;
	base::flat_map<uint64, int> groupedItemIndices;
	base::flat_set<uint64> groupedSpoileredIds;
	bool spoiler = false;
};

class IvHistoryViewMediaBlockFactory final : public HostedMediaBlockFactory {
public:
	using PhotoFactory = std::function<std::shared_ptr<MediaBlock>(
		Window::SessionController *controller,
		const PreparedPhotoBlockData &prepared)>;
	using VideoFactory = std::function<std::shared_ptr<MediaBlock>(
		Window::SessionController *controller,
		const PreparedVideoBlockData &prepared)>;
	using AudioFactory = std::function<std::shared_ptr<MediaBlock>(
		Window::SessionController *controller,
		const PreparedAudioBlockData &prepared)>;
	using MapFactory = std::function<std::shared_ptr<MediaBlock>(
		Window::SessionController *controller,
		const PreparedMapBlockData &prepared)>;
	using GroupedMediaFactory = std::function<std::shared_ptr<MediaBlock>(
		Window::SessionController *controller,
		const PreparedGroupedMediaBlockData &prepared)>;

	IvHistoryViewMediaBlockFactory(
		base::weak_ptr<Window::SessionController> controller,
		PhotoFactory createPhoto = {},
		VideoFactory createVideo = {},
		AudioFactory createAudio = {},
		MapFactory createMap = {},
		GroupedMediaFactory createGroupedMedia = {});

	[[nodiscard]] std::shared_ptr<MediaBlock> createPhoto(
		const PreparedPhotoBlockData &prepared) const override;
	[[nodiscard]] std::shared_ptr<MediaBlock> createVideo(
		const PreparedVideoBlockData &prepared) const override;
	[[nodiscard]] std::shared_ptr<MediaBlock> createAudio(
		const PreparedAudioBlockData &prepared) const override;
	[[nodiscard]] std::shared_ptr<MediaBlock> createMap(
		const PreparedMapBlockData &prepared) const override;
	[[nodiscard]] std::shared_ptr<MediaBlock> createGroupedMedia(
		const PreparedGroupedMediaBlockData &prepared) const override;

private:
	template <typename Prepared, typename Factory>
	[[nodiscard]] std::shared_ptr<MediaBlock> create(
			const Prepared &prepared,
			const Factory &factory) const;

	const base::weak_ptr<Window::SessionController> _controller;
	const PhotoFactory _createPhoto;
	const VideoFactory _createVideo;
	const AudioFactory _createAudio;
	const MapFactory _createMap;
	const GroupedMediaFactory _createGroupedMedia;
};

template <typename Prepared, typename Factory>
std::shared_ptr<MediaBlock> IvHistoryViewMediaBlockFactory::create(
		const Prepared &prepared,
		const Factory &factory) const {
	if (!factory) {
		return nullptr;
	}
	const auto controller = _controller.get();
	return factory(controller, prepared);
}

[[nodiscard]] std::shared_ptr<MediaBlock> CreateIvHistoryViewMediaBlock(
	IvHistoryViewMediaDescriptor descriptor);

} // namespace Iv::Markdown
