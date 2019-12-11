/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_file.h"
#include "media/clip/media_clip_reader.h"

struct HistoryMessageVia;
struct HistoryMessageReply;
struct HistoryMessageForwarded;

namespace Media {
namespace View {
class PlaybackProgress;
} // namespace View
} // namespace Media

namespace Media {
namespace Streaming {
class Document;
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
		return isBubbleBottom() && _caption.isEmpty();
	}
	bool isReadyForOpen() const override;

	void parentTextUpdated() override;

private:
	struct Streamed;

	float64 dataProgress() const override;
	bool dataFinished() const override;
	bool dataLoaded() const override;

	[[nodiscard]] bool autoplayEnabled() const;

	void playAnimation(bool autoplay) override;
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;
	QSize videoSize() const;
	::Media::Streaming::Document *activeRoundStreamed() const;
	const ::Media::Streaming::Document *activeOwnStreamed() const;
	const ::Media::Streaming::Document *activeCurrentStreamed() const;
	::Media::View::PlaybackProgress *videoPlayback() const;

	bool createStreamedPlayer();
	void setStreamed(std::unique_ptr<Streamed> value);
	void handleStreamingUpdate(::Media::Streaming::Update &&update);
	void handleStreamingError(::Media::Streaming::Error &&error);
	void streamingReady(::Media::Streaming::Information &&info);

	bool needInfoDisplay() const;
	int additionalWidth(
		const HistoryMessageVia *via,
		const HistoryMessageReply *reply,
		const HistoryMessageForwarded *forwarded) const;
	int additionalWidth() const;
	QString mediaTypeString() const;
	bool isSeparateRoundVideo() const;

	not_null<DocumentData*> _data;
	int _thumbw = 1;
	int _thumbh = 1;
	Ui::Text::String _caption;
	std::unique_ptr<Streamed> _streamed;

	void setStatusSize(int newSize) const;
	void updateStatusText() const;

};

} // namespace HistoryView
