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

namespace Data {
class DocumentMedia;
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

class Gif final : public File {
public:
	Gif(
		not_null<Element*> parent,
		not_null<HistoryItem*> realParent,
		not_null<DocumentData*> document);
	~Gif();

	void draw(Painter &p, const QRect &r, TextSelection selection, crl::time ms) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	[[nodiscard]] TextSelection adjustSelection(
			TextSelection selection,
			TextSelectType type) const override {
		return _caption.adjustSelection(selection, type);
	}
	uint16 fullSelectionLength() const override {
		return _caption.length();
	}
	bool hasTextForCopy() const override {
		return !_caption.isEmpty();
	}

	TextForMimeData selectedText(TextSelection selection) const override;

	bool uploading() const override;

	DocumentData *getDocument() const override {
		return _data;
	}

	bool fullFeaturedGrouped(RectParts sides) const;
	QSize sizeForGroupingOptimal(int maxWidth) const override;
	QSize sizeForGrouping(int width) const override;
	void drawGrouped(
		Painter &p,
		const QRect &clip,
		TextSelection selection,
		crl::time ms,
		const QRect &geometry,
		RectParts sides,
		RectParts corners,
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

	TextWithEntities getCaption() const override {
		return _caption.toTextWithEntities();
	}
	bool needsBubble() const override;
	bool customInfoLayout() const override {
		return _caption.isEmpty();
	}
	QString additionalInfoString() const override;

	bool skipBubbleTail() const override {
		return isRoundedInBubbleBottom() && _caption.isEmpty();
	}
	bool isReadyForOpen() const override;

	void parentTextUpdated() override;

	bool hasHeavyPart() const override;
	void unloadHeavyPart() override;

	void refreshParentId(not_null<HistoryItem*> realParent) override;

	[[nodiscard]] static bool CanPlayInline(not_null<DocumentData*> document);

private:
	struct Streamed;

	void validateVideoThumbnail() const;

	float64 dataProgress() const override;
	bool dataFinished() const override;
	bool dataLoaded() const override;

	void ensureDataMediaCreated() const;
	void dataMediaCreated() const;
	void refreshCaption();

	[[nodiscard]] bool autoplayEnabled() const;

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

	[[nodiscard]] bool needInfoDisplay() const;
	[[nodiscard]] bool needCornerStatusDisplay() const;
	[[nodiscard]] int additionalWidth(
		const HistoryMessageVia *via,
		const HistoryMessageReply *reply,
		const HistoryMessageForwarded *forwarded) const;
	[[nodiscard]] int additionalWidth() const;
	[[nodiscard]] QString mediaTypeString() const;
	[[nodiscard]] bool isSeparateRoundVideo() const;

	void validateGroupedCache(
		const QRect &geometry,
		RectParts corners,
		not_null<uint64*> cacheKey,
		not_null<QPixmap*> cache) const;
	void setStatusSize(int newSize) const;
	void updateStatusText() const;
	[[nodiscard]] QSize sizeForAspectRatio() const;

	[[nodiscard]] bool downloadInCorner() const;
	void drawCornerStatus(Painter &p, bool selected, QPoint position) const;
	[[nodiscard]] TextState cornerStatusTextState(
		QPoint point,
		StateRequest request,
		QPoint position) const;

	const not_null<DocumentData*> _data;
	int _thumbw = 1;
	int _thumbh = 1;
	Ui::Text::String _caption;
	std::unique_ptr<Streamed> _streamed;
	mutable std::shared_ptr<Data::DocumentMedia> _dataMedia;
	mutable std::unique_ptr<Image> _videoThumbnailFrame;

	QString _downloadSize;

};

} // namespace HistoryView
