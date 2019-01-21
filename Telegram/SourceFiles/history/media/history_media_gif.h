/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/media/history_media_file.h"

struct HistoryMessageVia;
struct HistoryMessageReply;
struct HistoryMessageForwarded;

namespace Media {
namespace Clip {
class Playback;
} // namespace Clip

namespace Player {
class RoundController;
} // namespace Player
} // namespace Media

class HistoryGif : public HistoryFileMedia {
public:
	HistoryGif(
		not_null<Element*> parent,
		not_null<DocumentData*> document);

	void refreshParentId(not_null<HistoryItem*> realParent) override;

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
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

	TextWithEntities selectedText(TextSelection selection) const override;

	bool uploading() const override;

	DocumentData *getDocument() const override {
		return _data;
	}

	void stopAnimation() override;

	TextWithEntities getCaption() const override {
		return _caption.originalTextWithEntities();
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

	~HistoryGif();

protected:
	float64 dataProgress() const override;
	bool dataFinished() const override;
	bool dataLoaded() const override;

	void setClipReader(Media::Clip::ReaderPointer gif);
	void clearClipReader() {
		setClipReader(Media::Clip::ReaderPointer());
	}

private:
	void playAnimation(bool autoplay) override;
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;
	Media::Player::RoundController *activeRoundVideo() const;
	Media::Clip::Reader *activeRoundPlayer() const;
	Media::Clip::Reader *currentReader() const;
	Media::Clip::Playback *videoPlayback() const;
	void clipCallback(Media::Clip::Notification notification);

	bool needInfoDisplay() const;
	int additionalWidth(
		const HistoryMessageVia *via,
		const HistoryMessageReply *reply,
		const HistoryMessageForwarded *forwarded) const;
	int additionalWidth() const;
	QString mediaTypeString() const;
	bool isSeparateRoundVideo() const;

	not_null<DocumentData*> _data;
	FileClickHandlerPtr _openInMediaviewLink;
	int _thumbw = 1;
	int _thumbh = 1;
	Text _caption;
	Media::Clip::ReaderPointer _gif;

	void setStatusSize(int newSize) const;
	void updateStatusText() const;

};
