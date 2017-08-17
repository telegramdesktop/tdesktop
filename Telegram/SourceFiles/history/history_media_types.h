/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "history/history_media.h"
#include "ui/effects/radial_animation.h"

namespace Media {
namespace Clip {
class Playback;
} // namespace Clip
} // namespace Media

void HistoryInitMedia();

class HistoryFileMedia : public HistoryMedia {
public:
	using HistoryMedia::HistoryMedia;

	bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const override {
		return p == _openl || p == _savel || p == _cancell;
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return p == _openl || p == _savel || p == _cancell;
	}

	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

	bool allowsFastShare() const override {
		return true;
	}

	~HistoryFileMedia();

protected:
	ClickHandlerPtr _openl, _savel, _cancell;
	void setLinks(ClickHandlerPtr &&openl, ClickHandlerPtr &&savel, ClickHandlerPtr &&cancell);
	void setDocumentLinks(DocumentData *document, bool inlinegif = false) {
		ClickHandlerPtr open, save;
		if (inlinegif) {
			open = MakeShared<GifOpenClickHandler>(document);
		} else {
			open = MakeShared<DocumentOpenClickHandler>(document);
		}
		if (inlinegif) {
			save = MakeShared<GifOpenClickHandler>(document);
		} else if (document->voice()) {
			save = MakeShared<DocumentOpenClickHandler>(document);
		} else {
			save = MakeShared<DocumentSaveClickHandler>(document);
		}
		setLinks(std::move(open), std::move(save), MakeShared<DocumentCancelClickHandler>(document));
	}

	// >= 0 will contain download / upload string, _statusSize = loaded bytes
	// < 0 will contain played string, _statusSize = -(seconds + 1) played
	// 0x7FFFFFF0 will contain status for not yet downloaded file
	// 0x7FFFFFF1 will contain status for already downloaded file
	// 0x7FFFFFF2 will contain status for failed to download / upload file
	mutable int32 _statusSize;
	mutable QString _statusText;

	// duration = -1 - no duration, duration = -2 - "GIF" duration
	void setStatusSize(int32 newSize, int32 fullSize, int32 duration, qint64 realDuration) const;

	void step_radial(TimeMs ms, bool timer);
	void thumbAnimationCallback();

	void ensureAnimation() const;
	void checkAnimationFinished() const;

	bool isRadialAnimation(TimeMs ms) const {
		if (!_animation || !_animation->radial.animating()) return false;

		_animation->radial.step(ms);
		return _animation && _animation->radial.animating();
	}
	bool isThumbAnimation(TimeMs ms) const {
		if (_animation) {
			if (_animation->a_thumbOver.animating(ms)) {
				return true;
			}
			checkAnimationFinished();
		}
		return false;
	}

	virtual float64 dataProgress() const = 0;
	virtual bool dataFinished() const = 0;
	virtual bool dataLoaded() const = 0;

	struct AnimationData {
		AnimationData(AnimationCallbacks &&radialCallbacks)
			: radial(std::move(radialCallbacks)) {
		}
		Animation a_thumbOver;
		Ui::RadialAnimation radial;
	};
	mutable std::unique_ptr<AnimationData> _animation;

};

class HistoryPhoto : public HistoryFileMedia {
public:
	HistoryPhoto(not_null<HistoryItem*> parent, not_null<PhotoData*> photo, const QString &caption);
	HistoryPhoto(not_null<HistoryItem*> parent, not_null<PeerData*> chat, not_null<PhotoData*> photo, int width);
	HistoryPhoto(not_null<HistoryItem*> parent, not_null<PeerData*> chat, const MTPDphoto &photo, int width);
	HistoryPhoto(not_null<HistoryItem*> parent, const HistoryPhoto &other);

	void init();
	HistoryMediaType type() const override {
		return MediaTypePhoto;
	}
	std::unique_ptr<HistoryMedia> clone(HistoryItem *newParent) const override {
		return std::make_unique<HistoryPhoto>(newParent, *this);
	}

	void initDimensions() override;
	int resizeGetHeight(int width) override;

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	HistoryTextState getState(QPoint point, HistoryStateRequest request) const override;

	TextSelection adjustSelection(TextSelection selection, TextSelectType type) const override WARN_UNUSED_RESULT {
		return _caption.adjustSelection(selection, type);
	}
	uint16 fullSelectionLength() const override {
		return _caption.length();
	}
	bool hasTextForCopy() const override {
		return !_caption.isEmpty();
	}

	QString notificationText() const override;
	QString inDialogsText() const override;
	TextWithEntities selectedText(TextSelection selection) const override;

	int32 addToOverview(AddToOverviewMethod method) override;
	void eraseFromOverview() override;

	PhotoData *photo() const {
		return _data;
	}

	void updateSentMedia(const MTPMessageMedia &media) override;
	bool needReSetInlineResultMedia(const MTPMessageMedia &media) override;

	void attachToParent() override;
	void detachFromParent() override;

	bool hasReplyPreview() const override {
		return !_data->thumb->isNull();
	}
	ImagePtr replyPreview() override;

	TextWithEntities getCaption() const override {
		return _caption.originalTextWithEntities();
	}
	bool needsBubble() const override;
	bool customInfoLayout() const override {
		return _caption.isEmpty();
	}
	bool skipBubbleTail() const override {
		return isBubbleBottom() && _caption.isEmpty();
	}
	bool canEditCaption() const override {
		return true;
	}
	bool isReadyForOpen() const override {
		return _data->loaded();
	}

protected:
	float64 dataProgress() const override {
		return _data->progress();
	}
	bool dataFinished() const override {
		return !_data->loading() && !_data->uploading();
	}
	bool dataLoaded() const override {
		return _data->loaded();
	}

private:
	not_null<PhotoData*> _data;
	int16 _pixw = 1;
	int16 _pixh = 1;
	Text _caption;

};

class HistoryVideo : public HistoryFileMedia {
public:
	HistoryVideo(not_null<HistoryItem*> parent, DocumentData *document, const QString &caption);
	HistoryVideo(not_null<HistoryItem*> parent, const HistoryVideo &other);
	HistoryMediaType type() const override {
		return MediaTypeVideo;
	}
	std::unique_ptr<HistoryMedia> clone(HistoryItem *newParent) const override {
		return std::make_unique<HistoryVideo>(newParent, *this);
	}

	void initDimensions() override;
	int resizeGetHeight(int width) override;

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	HistoryTextState getState(QPoint point, HistoryStateRequest request) const override;

	TextSelection adjustSelection(TextSelection selection, TextSelectType type) const override WARN_UNUSED_RESULT {
		return _caption.adjustSelection(selection, type);
	}
	uint16 fullSelectionLength() const override {
		return _caption.length();
	}
	bool hasTextForCopy() const override {
		return !_caption.isEmpty();
	}

	QString notificationText() const override;
	QString inDialogsText() const override;
	TextWithEntities selectedText(TextSelection selection) const override;

	int32 addToOverview(AddToOverviewMethod method) override;
	void eraseFromOverview() override;

	DocumentData *getDocument() override {
		return _data;
	}

	bool uploading() const override {
		return _data->uploading();
	}

	void attachToParent() override;
	void detachFromParent() override;

	void updateSentMedia(const MTPMessageMedia &media) override;
	bool needReSetInlineResultMedia(const MTPMessageMedia &media) override;

	bool hasReplyPreview() const override {
		return !_data->thumb->isNull();
	}
	ImagePtr replyPreview() override;

	TextWithEntities getCaption() const override {
		return _caption.originalTextWithEntities();
	}
	bool needsBubble() const override;
	bool customInfoLayout() const override {
		return _caption.isEmpty();
	}
	bool skipBubbleTail() const override {
		return isBubbleBottom() && _caption.isEmpty();
	}
	bool canEditCaption() const override {
		return true;
	}

protected:
	float64 dataProgress() const override {
		return _data->progress();
	}
	bool dataFinished() const override {
		return !_data->loading() && !_data->uploading();
	}
	bool dataLoaded() const override {
		return _data->loaded();
	}

private:
	not_null<DocumentData*> _data;
	int32 _thumbw;
	Text _caption;

	void setStatusSize(int32 newSize) const;
	void updateStatusText() const;

};

struct HistoryDocumentThumbed : public RuntimeComponent<HistoryDocumentThumbed> {
	ClickHandlerPtr _linksavel, _linkcancell;
	int _thumbw = 0;

	mutable int _linkw = 0;
	mutable QString _link;
};
struct HistoryDocumentCaptioned : public RuntimeComponent<HistoryDocumentCaptioned> {
	HistoryDocumentCaptioned();

	Text _caption;
};
struct HistoryDocumentNamed : public RuntimeComponent<HistoryDocumentNamed> {
	QString _name;
	int _namew = 0;
};
class HistoryDocument;
struct HistoryDocumentVoicePlayback {
	HistoryDocumentVoicePlayback(const HistoryDocument *that);

	int32 _position = 0;
	anim::value a_progress;
	BasicAnimation _a_progress;
};
class HistoryDocumentVoice : public RuntimeComponent<HistoryDocumentVoice> {
	// We don't use float64 because components should align to pointer even on 32bit systems.
	static constexpr float64 kFloatToIntMultiplier = 65536.;

public:
	void ensurePlayback(const HistoryDocument *interfaces) const;
	void checkPlaybackFinished() const;

	mutable std::unique_ptr<HistoryDocumentVoicePlayback> _playback;
	QSharedPointer<VoiceSeekClickHandler> _seekl;
	mutable int _lastDurationMs = 0;

	bool seeking() const {
		return _seeking;
	}
	void startSeeking();
	void stopSeeking();
	float64 seekingStart() const {
		return _seekingStart / kFloatToIntMultiplier;
	}
	void setSeekingStart(float64 seekingStart) const {
		_seekingStart = qRound(seekingStart * kFloatToIntMultiplier);
	}
	float64 seekingCurrent() const {
		return _seekingCurrent / kFloatToIntMultiplier;
	}
	void setSeekingCurrent(float64 seekingCurrent) {
		_seekingCurrent = qRound(seekingCurrent * kFloatToIntMultiplier);
	}

private:
	bool _seeking = false;

	mutable int _seekingStart = 0;
	mutable int _seekingCurrent = 0;

};

class HistoryDocument : public HistoryFileMedia, public RuntimeComposer {
public:
	HistoryDocument(not_null<HistoryItem*> parent, DocumentData *document, const QString &caption);
	HistoryDocument(not_null<HistoryItem*> parent, const HistoryDocument &other);
	HistoryMediaType type() const override {
		return _data->voice() ? MediaTypeVoiceFile : (_data->song() ? MediaTypeMusicFile : MediaTypeFile);
	}
	std::unique_ptr<HistoryMedia> clone(HistoryItem *newParent) const override {
		return std::make_unique<HistoryDocument>(newParent, *this);
	}

	void initDimensions() override;
	int resizeGetHeight(int width) override;

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	HistoryTextState getState(QPoint point, HistoryStateRequest request) const override;
	void updatePressed(QPoint point) override;

	TextSelection adjustSelection(TextSelection selection, TextSelectType type) const override WARN_UNUSED_RESULT {
		if (auto captioned = Get<HistoryDocumentCaptioned>()) {
			return captioned->_caption.adjustSelection(selection, type);
		}
		return selection;
	}
	uint16 fullSelectionLength() const override {
		if (auto captioned = Get<HistoryDocumentCaptioned>()) {
			return captioned->_caption.length();
		}
		return 0;
	}
	bool hasTextForCopy() const override {
		return Has<HistoryDocumentCaptioned>();
	}

	QString notificationText() const override;
	QString inDialogsText() const override;
	TextWithEntities selectedText(TextSelection selection) const override;

	int32 addToOverview(AddToOverviewMethod method) override;
	void eraseFromOverview() override;

	bool uploading() const override {
		return _data->uploading();
	}

	DocumentData *getDocument() override {
		return _data;
	}

	bool playInline(bool autoplay) override;

	void attachToParent() override;
	void detachFromParent() override;

	void updateSentMedia(const MTPMessageMedia &media) override;
	bool needReSetInlineResultMedia(const MTPMessageMedia &media) override;

	bool hasReplyPreview() const override {
		return !_data->thumb->isNull();
	}
	ImagePtr replyPreview() override;

	TextWithEntities getCaption() const override {
		if (const HistoryDocumentCaptioned *captioned = Get<HistoryDocumentCaptioned>()) {
			return captioned->_caption.originalTextWithEntities();
		}
		return TextWithEntities();
	}
	bool needsBubble() const override {
		return true;
	}
	bool customInfoLayout() const override {
		return false;
	}
	QMargins bubbleMargins() const override;
	bool hideForwardedFrom() const override {
		return _data->song();
	}
	bool canEditCaption() const override {
		return true;
	}

	void step_voiceProgress(float64 ms, bool timer);

	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

protected:
	float64 dataProgress() const override {
		return _data->progress();
	}
	bool dataFinished() const override {
		return !_data->loading() && !_data->uploading();
	}
	bool dataLoaded() const override {
		return _data->loaded();
	}

private:
	void createComponents(bool caption);
	void fillNamedFromData(HistoryDocumentNamed *named);

	void setStatusSize(int32 newSize, qint64 realDuration = 0) const;
	bool updateStatusText() const; // returns showPause

								   // Callback is a void(const QString &, const QString &, const Text &) functor.
								   // It will be called as callback(attachType, attachFileName, attachCaption).
	template <typename Callback>
	void buildStringRepresentation(Callback callback) const;

	not_null<DocumentData*> _data;

};

class HistoryGif : public HistoryFileMedia {
public:
	HistoryGif(not_null<HistoryItem*> parent, DocumentData *document, const QString &caption);
	HistoryGif(not_null<HistoryItem*> parent, const HistoryGif &other);
	HistoryMediaType type() const override {
		return MediaTypeGif;
	}
	std::unique_ptr<HistoryMedia> clone(HistoryItem *newParent) const override {
		return std::make_unique<HistoryGif>(newParent, *this);
	}

	void initDimensions() override;
	int resizeGetHeight(int width) override;

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	HistoryTextState getState(QPoint point, HistoryStateRequest request) const override;

	TextSelection adjustSelection(TextSelection selection, TextSelectType type) const override WARN_UNUSED_RESULT {
		return _caption.adjustSelection(selection, type);
	}
	uint16 fullSelectionLength() const override {
		return _caption.length();
	}
	bool hasTextForCopy() const override {
		return !_caption.isEmpty();
	}

	QString notificationText() const override;
	QString inDialogsText() const override;
	TextWithEntities selectedText(TextSelection selection) const override;

	int32 addToOverview(AddToOverviewMethod method) override;
	void eraseFromOverview() override;

	bool uploading() const override {
		return _data->uploading();
	}

	DocumentData *getDocument() override {
		return _data;
	}
	Media::Clip::Reader *getClipReader() override {
		return _gif.get();
	}

	bool playInline(bool autoplay) override;
	void stopInline() override;
	bool isRoundVideoPlaying() const override;

	void attachToParent() override;
	void detachFromParent() override;

	void updateSentMedia(const MTPMessageMedia &media) override;
	bool needReSetInlineResultMedia(const MTPMessageMedia &media) override;

	bool hasReplyPreview() const override {
		return !_data->thumb->isNull();
	}
	ImagePtr replyPreview() override;

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
	bool canEditCaption() const override {
		return !_data->isRoundVideo();
	}
	bool isReadyForOpen() const override {
		return _data->loaded();
	}

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
	int additionalWidth(const HistoryMessageVia *via, const HistoryMessageReply *reply, const HistoryMessageForwarded *forwarded) const;
	int additionalWidth() const {
		return additionalWidth(_parent->Get<HistoryMessageVia>(), _parent->Get<HistoryMessageReply>(), _parent->Get<HistoryMessageForwarded>());
	}
	QString mediaTypeString() const;
	bool isSeparateRoundVideo() const;

	not_null<DocumentData*> _data;
	ClickHandlerPtr _openInMediaviewLink;
	int32 _thumbw = 1;
	int32 _thumbh = 1;
	Text _caption;

	mutable std::unique_ptr<Media::Clip::Playback> _roundPlayback;
	Media::Clip::ReaderPointer _gif;

	void setStatusSize(int32 newSize) const;
	void updateStatusText() const;

};

class HistorySticker : public HistoryMedia {
public:
	HistorySticker(not_null<HistoryItem*> parent, DocumentData *document);
	HistoryMediaType type() const override {
		return MediaTypeSticker;
	}
	std::unique_ptr<HistoryMedia> clone(HistoryItem *newParent) const override {
		return std::make_unique<HistorySticker>(newParent, _data);
	}

	void initDimensions() override;
	int resizeGetHeight(int width) override;

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	HistoryTextState getState(QPoint point, HistoryStateRequest request) const override;

	bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const override {
		return true;
	}
	bool dragItem() const override {
		return true;
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return true;
	}

	QString notificationText() const override;
	TextWithEntities selectedText(TextSelection selection) const override;

	DocumentData *getDocument() override {
		return _data;
	}

	void attachToParent() override;
	void detachFromParent() override;

	void updateSentMedia(const MTPMessageMedia &media) override;
	bool needReSetInlineResultMedia(const MTPMessageMedia &media) override;

	bool hasReplyPreview() const override {
		return !_data->thumb->isNull();
	}
	ImagePtr replyPreview() override;

	bool needsBubble() const override {
		return false;
	}
	bool customInfoLayout() const override {
		return true;
	}
	QString emoji() const {
		return _emoji;
	}

private:
	int additionalWidth(const HistoryMessageVia *via, const HistoryMessageReply *reply) const;
	int additionalWidth() const {
		return additionalWidth(_parent->Get<HistoryMessageVia>(), _parent->Get<HistoryMessageReply>());
	}
	QString toString() const;

	int16 _pixw = 1;
	int16 _pixh = 1;
	ClickHandlerPtr _packLink;
	not_null<DocumentData*> _data;
	QString _emoji;

};

class HistoryContact : public HistoryMedia {
public:
	HistoryContact(not_null<HistoryItem*> parent, int32 userId, const QString &first, const QString &last, const QString &phone);
	HistoryMediaType type() const override {
		return MediaTypeContact;
	}
	std::unique_ptr<HistoryMedia> clone(HistoryItem *newParent) const override {
		return std::make_unique<HistoryContact>(newParent, _userId, _fname, _lname, _phone);
	}

	void initDimensions() override;

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	HistoryTextState getState(QPoint point, HistoryStateRequest request) const override;

	bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const override {
		return true;
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return true;
	}

	QString notificationText() const override;
	TextWithEntities selectedText(TextSelection selection) const override;

	void attachToParent() override;
	void detachFromParent() override;

	void updateSentMedia(const MTPMessageMedia &media) override;

	bool needsBubble() const override {
		return true;
	}
	bool customInfoLayout() const override {
		return false;
	}

	const QString &fname() const {
		return _fname;
	}
	const QString &lname() const {
		return _lname;
	}
	const QString &phone() const {
		return _phone;
	}

private:
	int32 _userId = 0;
	UserData *_contact = nullptr;

	int _phonew = 0;
	QString _fname, _lname, _phone;
	Text _name;
	EmptyUserpic _photoEmpty;

	ClickHandlerPtr _linkl;
	int _linkw = 0;
	QString _link;

};

class HistoryCall : public HistoryMedia {
public:
	HistoryCall(not_null<HistoryItem*> parent, const MTPDmessageActionPhoneCall &call);
	HistoryMediaType type() const override {
		return MediaTypeCall;
	}
	std::unique_ptr<HistoryMedia> clone(HistoryItem *newParent) const override {
		Unexpected("Clone HistoryCall.");
	}

	void initDimensions() override;

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	HistoryTextState getState(QPoint point, HistoryStateRequest request) const override;

	bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const override {
		return true;
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return false;
	}

	QString notificationText() const override;
	TextWithEntities selectedText(TextSelection selection) const override;

	bool needsBubble() const override {
		return true;
	}
	bool customInfoLayout() const override {
		return true;
	}

	enum class FinishReason {
		Missed,
		Busy,
		Disconnected,
		Hangup,
	};
	FinishReason reason() const {
		return _reason;
	}

private:
	static FinishReason GetReason(const MTPDmessageActionPhoneCall &call);

	FinishReason _reason = FinishReason::Missed;
	int _duration = 0;

	QString _text;
	QString _status;

	ClickHandlerPtr _link;

};

class HistoryWebPage : public HistoryMedia {
public:
	HistoryWebPage(not_null<HistoryItem*> parent, not_null<WebPageData*> data);
	HistoryWebPage(not_null<HistoryItem*> parent, const HistoryWebPage &other);
	HistoryMediaType type() const override {
		return MediaTypeWebPage;
	}
	std::unique_ptr<HistoryMedia> clone(HistoryItem *newParent) const override {
		return std::make_unique<HistoryWebPage>(newParent, *this);
	}

	void initDimensions() override;
	int resizeGetHeight(int width) override;

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	HistoryTextState getState(QPoint point, HistoryStateRequest request) const override;

	TextSelection adjustSelection(TextSelection selection, TextSelectType type) const override WARN_UNUSED_RESULT;
	uint16 fullSelectionLength() const override {
		return _title.length() + _description.length();
	}
	bool hasTextForCopy() const override {
		return false; // we do not add _title and _description in FullSelection text copy.
	}

	bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const override {
		return _attach && _attach->toggleSelectionByHandlerClick(p);
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return _attach && _attach->dragItemByHandler(p);
	}

	TextWithEntities selectedText(TextSelection selection) const override;

	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

	bool isDisplayed() const override {
		return !_data->pendingTill && !_parent->Has<HistoryMessageLogEntryOriginal>();
	}
	DocumentData *getDocument() override {
		return _attach ? _attach->getDocument() : nullptr;
	}
	Media::Clip::Reader *getClipReader() override {
		return _attach ? _attach->getClipReader() : nullptr;
	}
	bool playInline(bool autoplay) override {
		return _attach ? _attach->playInline(autoplay) : false;
	}
	void stopInline() override {
		if (_attach) _attach->stopInline();
	}

	void attachToParent() override;
	void detachFromParent() override;

	bool hasReplyPreview() const override;
	ImagePtr replyPreview() override;

	not_null<WebPageData*> webpage() {
		return _data;
	}

	bool needsBubble() const override {
		return true;
	}
	bool customInfoLayout() const override {
		return false;
	}
	bool allowsFastShare() const override {
		return true;
	}

	HistoryMedia *attach() const {
		return _attach.get();
	}

private:
	TextSelection toDescriptionSelection(TextSelection selection) const {
		return internal::unshiftSelection(selection, _title);
	}
	TextSelection fromDescriptionSelection(TextSelection selection) const {
		return internal::shiftSelection(selection, _title);
	}
	QMargins inBubblePadding() const;
	int bottomInfoPadding() const;
	bool isLogEntryOriginal() const;

	not_null<WebPageData*> _data;
	ClickHandlerPtr _openl;
	std::unique_ptr<HistoryMedia> _attach;

	bool _asArticle = false;
	int32 _titleLines, _descriptionLines;

	Text _title, _description;
	int32 _siteNameWidth = 0;

	QString _duration;
	int32 _durationWidth = 0;

	int16 _pixw = 0;
	int16 _pixh = 0;

};

class HistoryGame : public HistoryMedia {
public:
	HistoryGame(not_null<HistoryItem*> parent, GameData *data);
	HistoryGame(not_null<HistoryItem*> parent, const HistoryGame &other);
	HistoryMediaType type() const override {
		return MediaTypeGame;
	}
	std::unique_ptr<HistoryMedia> clone(HistoryItem *newParent) const override {
		return std::make_unique<HistoryGame>(newParent, *this);
	}

	void initDimensions() override;
	int resizeGetHeight(int width) override;
	void updateMessageId() override;

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	HistoryTextState getState(QPoint point, HistoryStateRequest request) const override;

	TextSelection adjustSelection(TextSelection selection, TextSelectType type) const override WARN_UNUSED_RESULT;
	uint16 fullSelectionLength() const override {
		return _title.length() + _description.length();
	}
	bool isAboveMessage() const override {
		return true;
	}
	bool hasTextForCopy() const override {
		return false; // we do not add _title and _description in FullSelection text copy.
	}
	bool consumeMessageText(const TextWithEntities &textWithEntities) override;

	bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const override {
		return _attach && _attach->toggleSelectionByHandlerClick(p);
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return _attach && _attach->dragItemByHandler(p);
	}

	QString notificationText() const override;
	TextWithEntities selectedText(TextSelection selection) const override;

	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

	DocumentData *getDocument() override {
		return _attach ? _attach->getDocument() : nullptr;
	}
	Media::Clip::Reader *getClipReader() override {
		return _attach ? _attach->getClipReader() : nullptr;
	}
	bool playInline(bool autoplay) override {
		return _attach ? _attach->playInline(autoplay) : false;
	}
	void stopInline() override {
		if (_attach) _attach->stopInline();
	}

	void attachToParent() override;
	void detachFromParent() override;

	bool hasReplyPreview() const override {
		return (_data->photo && !_data->photo->thumb->isNull()) || (_data->document && !_data->document->thumb->isNull());
	}
	ImagePtr replyPreview() override;

	GameData *game() {
		return _data;
	}

	void updateSentMedia(const MTPMessageMedia &media) override;
	bool needReSetInlineResultMedia(const MTPMessageMedia &media) override;

	bool needsBubble() const override {
		return true;
	}
	bool customInfoLayout() const override {
		return false;
	}
	bool allowsFastShare() const override {
		return true;
	}

	HistoryMedia *attach() const {
		return _attach.get();
	}

private:
	TextSelection toDescriptionSelection(TextSelection selection) const {
		return internal::unshiftSelection(selection, _title);
	}
	TextSelection fromDescriptionSelection(TextSelection selection) const {
		return internal::shiftSelection(selection, _title);
	}
	QMargins inBubblePadding() const;
	int bottomInfoPadding() const;

	GameData *_data;
	ClickHandlerPtr _openl;
	std::unique_ptr<HistoryMedia> _attach;

	int32 _titleLines, _descriptionLines;

	Text _title, _description;

	int _gameTagWidth = 0;

};

class HistoryInvoice : public HistoryMedia {
public:
	HistoryInvoice(not_null<HistoryItem*> parent, const MTPDmessageMediaInvoice &data);
	HistoryInvoice(not_null<HistoryItem*> parent, const HistoryInvoice &other);
	HistoryMediaType type() const override {
		return MediaTypeInvoice;
	}
	std::unique_ptr<HistoryMedia> clone(HistoryItem *newParent) const override {
		return std::make_unique<HistoryInvoice>(newParent, *this);
	}

	void initDimensions() override;
	int resizeGetHeight(int width) override;

	MsgId getReceiptMsgId() const {
		return _receiptMsgId;
	}
	QString getTitle() const {
		return _title.originalText();
	}
	static QString fillAmountAndCurrency(int amount, const QString &currency);

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	HistoryTextState getState(QPoint point, HistoryStateRequest request) const override;

	TextSelection adjustSelection(TextSelection selection, TextSelectType type) const override WARN_UNUSED_RESULT;
	uint16 fullSelectionLength() const override {
		return _title.length() + _description.length();
	}
	bool hasTextForCopy() const override {
		return false; // we do not add _title and _description in FullSelection text copy.
	}

	bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const override {
		return _attach && _attach->toggleSelectionByHandlerClick(p);
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return _attach && _attach->dragItemByHandler(p);
	}

	QString notificationText() const override;
	TextWithEntities selectedText(TextSelection selection) const override;

	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

	void attachToParent() override;
	void detachFromParent() override;

	bool hasReplyPreview() const override {
		return _attach && _attach->hasReplyPreview();
	}
	ImagePtr replyPreview() override {
		return _attach ? _attach->replyPreview() : ImagePtr();
	}

	bool needsBubble() const override {
		return true;
	}
	bool customInfoLayout() const override {
		return false;
	}

	HistoryMedia *attach() const {
		return _attach.get();
	}

private:
	void fillFromData(const MTPDmessageMediaInvoice &data);

	TextSelection toDescriptionSelection(TextSelection selection) const {
		return internal::unshiftSelection(selection, _title);
	}
	TextSelection fromDescriptionSelection(TextSelection selection) const {
		return internal::shiftSelection(selection, _title);
	}
	QMargins inBubblePadding() const;
	int bottomInfoPadding() const;

	std::unique_ptr<HistoryMedia> _attach;

	int _titleHeight = 0;
	int _descriptionHeight = 0;
	Text _title;
	Text _description;
	Text _status;

	MsgId _receiptMsgId = 0;

};

class LocationCoords;
struct LocationData;

class HistoryLocation : public HistoryMedia {
public:
	HistoryLocation(not_null<HistoryItem*> parent, const LocationCoords &coords, const QString &title = QString(), const QString &description = QString());
	HistoryLocation(not_null<HistoryItem*> parent, const HistoryLocation &other);
	HistoryMediaType type() const override {
		return MediaTypeLocation;
	}
	std::unique_ptr<HistoryMedia> clone(HistoryItem *newParent) const override {
		return std::make_unique<HistoryLocation>(newParent, *this);
	}

	void initDimensions() override;
	int resizeGetHeight(int32 width) override;

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	HistoryTextState getState(QPoint point, HistoryStateRequest request) const override;

	TextSelection adjustSelection(TextSelection selection, TextSelectType type) const override WARN_UNUSED_RESULT;
	uint16 fullSelectionLength() const override {
		return _title.length() + _description.length();
	}
	bool hasTextForCopy() const override {
		return !_title.isEmpty() || !_description.isEmpty();
	}

	bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const override {
		return p == _link;
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return p == _link;
	}

	QString notificationText() const override;
	QString inDialogsText() const override;
	TextWithEntities selectedText(TextSelection selection) const override;

	bool needsBubble() const override;
	bool customInfoLayout() const override {
		return true;
	}

	bool skipBubbleTail() const override {
		return isBubbleBottom();
	}

private:
	TextSelection toDescriptionSelection(TextSelection selection) const {
		return internal::unshiftSelection(selection, _title);
	}
	TextSelection fromDescriptionSelection(TextSelection selection) const {
		return internal::shiftSelection(selection, _title);
	}

	LocationData *_data;
	Text _title, _description;
	ClickHandlerPtr _link;

	int32 fullWidth() const;
	int32 fullHeight() const;

};
