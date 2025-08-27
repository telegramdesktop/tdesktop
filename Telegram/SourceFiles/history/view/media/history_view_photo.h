/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_file.h"

class Image;
enum class ImageRoundRadius;

namespace Data {
class PhotoMedia;
} // namespace Data

namespace Media {
namespace Streaming {
class Instance;
struct Update;
enum class Error;
struct Information;
} // namespace Streaming
} // namespace Media

namespace HistoryView {

class Photo final : public File {
public:
	Photo(
		not_null<Element*> parent,
		not_null<HistoryItem*> realParent,
		not_null<PhotoData*> photo,
		bool spoiler);
	Photo(
		not_null<Element*> parent,
		not_null<PeerData*> chat,
		not_null<PhotoData*> photo,
		int width);
	~Photo();

	bool hideMessageText() const override {
		return false;
	}

	void draw(Painter &p, const PaintContext &context) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	PhotoData *getPhoto() const override {
		return _data;
	}
	void showPhoto(FullMsgId id);

	void paintUserpicFrame(
		Painter &p,
		QPoint photoPosition,
		bool markFrameShown) const;

	QSize sizeForGroupingOptimal(int maxWidth, bool last) const override;
	QSize sizeForGrouping(int width) const override;
	void drawGrouped(
		Painter &p,
		const PaintContext &context,
		const QRect &geometry,
		RectParts sides,
		Ui::BubbleRounding rounding,
		float64 highlightOpacity,
		not_null<uint64*> cacheKey,
		not_null<QPixmap*> cache) const override;
	TextState getStateGrouped(
		const QRect &geometry,
		RectParts sides,
		QPoint point,
		StateRequest request) const override;

	void drawSpoilerTag(
		Painter &p,
		QRect rthumb,
		const PaintContext &context,
		Fn<QImage()> generateBackground) const override;
	ClickHandlerPtr spoilerTagLink() const override;
	QImage spoilerTagBackground() const override;

	void hideSpoilers() override;
	bool needsBubble() const override;
	bool customInfoLayout() const override {
		return true;
	}
	QPoint resolveCustomInfoRightBottom() const override;
	bool skipBubbleTail() const override {
		return isRoundedInBubbleBottom();
	}
	bool isReadyForOpen() const override;

	bool hasHeavyPart() const override;
	void unloadHeavyPart() override;
	bool enforceBubbleWidth() const override;

protected:
	float64 dataProgress() const override;
	bool dataFinished() const override;
	bool dataLoaded() const override;

private:
	struct Streamed;

	void create(FullMsgId contextId, PeerData *chat = nullptr);

	void playAnimation(bool autoplay) override;
	void stopAnimation() override;
	void checkAnimation() override;

	void ensureDataMediaCreated() const;
	void dataMediaCreated() const;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;
	[[nodiscard]] int adjustHeightForLessCrop(
		QSize dimensions,
		QSize current) const;

	bool needInfoDisplay() const;
	void validateGroupedCache(
		const QRect &geometry,
		Ui::BubbleRounding rounding,
		not_null<uint64*> cacheKey,
		not_null<QPixmap*> cache) const;
	void validateImageCache(
		QSize outer,
		std::optional<Ui::BubbleRounding> rounding) const;
	void validateUserpicImageCache(QSize size, bool forum) const;
	[[nodiscard]] QImage prepareImageCache(QSize outer) const;
	void validateSpoilerImageCache(
		QSize outer,
		std::optional<Ui::BubbleRounding> rounding) const;
	[[nodiscard]] QImage prepareImageCacheWithLarge(
		QSize outer,
		Image *large) const;

	bool videoAutoplayEnabled() const;
	void setStreamed(std::unique_ptr<Streamed> value);
	void repaintStreamedContent();
	void checkStreamedIsStarted() const;
	bool createStreamingObjects();
	void handleStreamingUpdate(::Media::Streaming::Update &&update);
	void handleStreamingError(::Media::Streaming::Error &&error);
	void streamingReady(::Media::Streaming::Information &&info);
	void paintUserpicFrame(
		Painter &p,
		const PaintContext &context,
		QPoint photoPosition) const;

	[[nodiscard]] QSize photoSize() const;
	[[nodiscard]] QRect enlargeRect() const;

	void togglePollingStory(bool enabled) const;

	const not_null<PhotoData*> _data;
	const FullStoryId _storyId;
	mutable std::shared_ptr<Data::PhotoMedia> _dataMedia;
	mutable std::unique_ptr<Streamed> _streamed;
	const std::unique_ptr<MediaSpoiler> _spoiler;
	mutable std::unique_ptr<MediaSpoilerTag> _spoilerTag;
	mutable QImage _imageCache;
	mutable std::optional<Ui::BubbleRounding> _imageCacheRounding;
	uint32 _serviceWidth : 26 = 0;
	uint32 _purchasedPriceTag : 1 = 0;
	const uint32 _sensitiveSpoiler : 1 = 0;
	mutable uint32 _imageCacheForum : 1 = 0;
	mutable uint32 _imageCacheBlurred : 1 = 0;
	mutable uint32 _pollingStory : 1 = 0;
	mutable uint32 _showEnlarge : 1 = 0;

};

} // namespace HistoryView
