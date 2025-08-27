/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_file.h"
#include "media/streaming/media_streaming_common.h"

class Image;
struct HistoryMessageVia;
struct HistoryMessageReply;
struct HistoryMessageForwarded;
class Painter;
class PhotoData;

namespace Data {
class DocumentMedia;
class PhotoMedia;
} // namespace Data

namespace Media {
namespace View {
class PlaybackProgress;
} // namespace View
} // namespace Media

namespace Media {
namespace Streaming {
class Instance;
struct Update;
struct Information;
enum class Error;
} // namespace Streaming
} // namespace Media

namespace HistoryView {

class Photo;
class Reply;
class TranscribeButton;

using TtlRoundPaintCallback = Fn<void(
	QPainter&,
	QRect,
	const PaintContext &context)>;

class Gif final : public File {
public:
	Gif(
		not_null<Element*> parent,
		not_null<HistoryItem*> realParent,
		not_null<DocumentData*> document,
		bool spoiler);
	~Gif();

	bool hideMessageText() const override;

	void draw(Painter &p, const PaintContext &context) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	void clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) override;

	bool uploading() const override;

	DocumentData *getDocument() const override {
		return _data;
	}

	bool fullFeaturedGrouped(RectParts sides) const;
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

	void stopAnimation() override;
	void checkAnimation() override;

	void drawSpoilerTag(
		Painter &p,
		QRect rthumb,
		const PaintContext &context,
		Fn<QImage()> generateBackground) const override;
	ClickHandlerPtr spoilerTagLink() const override;
	QImage spoilerTagBackground() const override;

	void hideSpoilers() override;
	bool needsBubble() const override;
	bool unwrapped() const override;
	bool customInfoLayout() const override {
		return true;
	}
	QRect contentRectForReactions() const override;
	std::optional<int> reactionButtonCenterOverride() const override;
	QPoint resolveCustomInfoRightBottom() const override;
	QString additionalInfoString() const override;

	bool skipBubbleTail() const override {
		return isRoundedInBubbleBottom();
	}
	bool isReadyForOpen() const override;

	bool hasHeavyPart() const override;
	void unloadHeavyPart() override;
	bool enforceBubbleWidth() const override;

	[[nodiscard]] static bool CanPlayInline(not_null<DocumentData*> document);

private:
	struct Streamed;

	void validateVideoThumbnail() const;
	[[nodiscard]] QSize countThumbSize(int &inOutWidthMax) const;
	[[nodiscard]] int adjustHeightForLessCrop(
		QSize dimensions,
		QSize current) const;

	float64 dataProgress() const override;
	bool dataFinished() const override;
	bool dataLoaded() const override;

	void ensureDataMediaCreated() const;
	void dataMediaCreated() const;

	[[nodiscard]] bool autoplayEnabled() const;
	[[nodiscard]] bool autoplayUnderCursor() const;
	[[nodiscard]] bool underCursor() const;

	void playAnimation(bool autoplay) override;
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;
	QSize videoSize() const;
	::Media::Streaming::Instance *activeRoundStreamed() const;
	Streamed *activeOwnStreamed() const;
	::Media::Streaming::Instance *activeCurrentStreamed() const;
	::Media::View::PlaybackProgress *videoPlayback() const;

	void createStreamedPlayer();
	void checkStreamedIsStarted() const;
	void startStreamedPlayer() const;
	void setStreamed(std::unique_ptr<Streamed> value);
	void handleStreamingUpdate(::Media::Streaming::Update &&update);
	void handleStreamingError(::Media::Streaming::Error &&error);
	void streamingReady(::Media::Streaming::Information &&info);
	void repaintStreamedContent();
	void ensureTranscribeButton() const;

	void paintTranscribe(
		Painter &p,
		int x,
		int y,
		bool right,
		const PaintContext &context) const;
	void paintTimestampMark(
		Painter &p,
		QRect rthumb,
		std::optional<Ui::BubbleRounding> rounding) const;

	[[nodiscard]] bool needInfoDisplay() const;
	[[nodiscard]] bool needCornerStatusDisplay() const;
	[[nodiscard]] int additionalWidth(
		const Reply *reply,
		const HistoryMessageVia *via,
		const HistoryMessageForwarded *forwarded) const;
	[[nodiscard]] int additionalWidth() const;
	[[nodiscard]] bool isUnwrapped() const;

	void validateThumbCache(
		QSize outer,
		bool isEllipse,
		std::optional<Ui::BubbleRounding> rounding) const;
	[[nodiscard]] QImage prepareThumbCache(QSize outer) const;
	void validateSpoilerImageCache(
		QSize outer,
		std::optional<Ui::BubbleRounding> rounding) const;

	void validateGroupedCache(
		const QRect &geometry,
		Ui::BubbleRounding rounding,
		not_null<uint64*> cacheKey,
		not_null<QPixmap*> cache) const;
	void setStatusSize(int64 newSize) const;
	void updateStatusText() const;
	[[nodiscard]] QSize sizeForAspectRatio() const;

	void validateRoundingMask(QSize size) const;

	[[nodiscard]] bool downloadInCorner() const;
	void drawCornerStatus(
		Painter &p,
		const PaintContext &context,
		QPoint position) const;
	[[nodiscard]] TextState cornerStatusTextState(
		QPoint point,
		StateRequest request,
		QPoint position) const;
	[[nodiscard]] ClickHandlerPtr currentVideoLink() const;

	void togglePollingStory(bool enabled) const;

	TtlRoundPaintCallback _drawTtl;

	const not_null<DocumentData*> _data;
	PhotoData *_videoCover = nullptr;
	const FullStoryId _storyId;
	std::unique_ptr<Streamed> _streamed;
	const std::unique_ptr<MediaSpoiler> _spoiler;
	mutable std::unique_ptr<MediaSpoilerTag> _spoilerTag;
	mutable std::unique_ptr<TranscribeButton> _transcribe;
	mutable std::shared_ptr<Data::DocumentMedia> _dataMedia;
	mutable std::shared_ptr<Data::PhotoMedia> _videoCoverMedia;
	mutable std::unique_ptr<Image> _videoThumbnailFrame;
	QString _downloadSize;
	mutable QImage _thumbCache;
	mutable QImage _roundingMask;
	mutable crl::time _videoPosition = 0;
	mutable TimeId _videoTimestamp = 0;
	mutable std::optional<Ui::BubbleRounding> _thumbCacheRounding;
	mutable bool _thumbCacheBlurred : 1 = false;
	mutable bool _thumbIsEllipse : 1 = false;
	mutable bool _pollingStory : 1 = false;
	mutable bool _purchasedPriceTag : 1 = false;
	mutable bool _smallGroupPart : 1 = false;
	const bool _sensitiveSpoiler : 1 = false;
	const bool _hasVideoCover : 1 = false;

};

} // namespace HistoryView
