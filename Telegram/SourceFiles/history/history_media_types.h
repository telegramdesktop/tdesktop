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

#include "ui/effects/radial_animation.h"

void historyInitMedia();

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

	~HistoryFileMedia();

protected:
	ClickHandlerPtr _openl, _savel, _cancell;
	void setLinks(ClickHandlerPtr &&openl, ClickHandlerPtr &&savel, ClickHandlerPtr &&cancell);
	void setDocumentLinks(DocumentData *document, bool inlinegif = false) {
		ClickHandlerPtr open, save;
		if (inlinegif) {
			open.reset(new GifOpenClickHandler(document));
		} else {
			open.reset(new DocumentOpenClickHandler(document));
		}
		if (inlinegif) {
			save.reset(new GifOpenClickHandler(document));
		} else if (document->voice()) {
			save.reset(new DocumentOpenClickHandler(document));
		} else {
			save.reset(new DocumentSaveClickHandler(document));
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
	HistoryPhoto(HistoryItem *parent, PhotoData *photo, const QString &caption);
	HistoryPhoto(HistoryItem *parent, PeerData *chat, const MTPDphoto &photo, int width);
	HistoryPhoto(HistoryItem *parent, const HistoryPhoto &other);

	void init();
	HistoryMediaType type() const override {
		return MediaTypePhoto;
	}
	HistoryPhoto *clone(HistoryItem *newParent) const override {
		return new HistoryPhoto(newParent, *this);
	}

	void initDimensions() override;
	int resizeGetHeight(int width) override;

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	HistoryTextState getState(int x, int y, HistoryStateRequest request) const override;

	TextSelection adjustSelection(TextSelection selection, TextSelectType type) const override {
		return _caption.adjustSelection(selection, type);
	}
	bool hasTextForCopy() const override {
		return !_caption.isEmpty();
	}

	QString notificationText() const override;
	QString inDialogsText() const override;
	TextWithEntities selectedText(TextSelection selection) const override;

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
	bool needsBubble() const override {
		if (!_caption.isEmpty()) {
			return true;
		}
		if (auto message = _parent->toHistoryMessage()) {
			return message->viaBot()
				|| message->Has<HistoryMessageForwarded>()
				|| message->Has<HistoryMessageReply>()
				|| message->displayFromName();
		}
		return false;
	}
	bool customInfoLayout() const override {
		return _caption.isEmpty();
	}
	bool skipBubbleTail() const override {
		return isBubbleBottom() && _caption.isEmpty();
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
	PhotoData *_data;
	int16 _pixw = 1;
	int16 _pixh = 1;
	Text _caption;

};

class HistoryVideo : public HistoryFileMedia {
public:
	HistoryVideo(HistoryItem *parent, DocumentData *document, const QString &caption);
	HistoryVideo(HistoryItem *parent, const HistoryVideo &other);
	HistoryMediaType type() const override {
		return MediaTypeVideo;
	}
	HistoryVideo *clone(HistoryItem *newParent) const override {
		return new HistoryVideo(newParent, *this);
	}

	void initDimensions() override;
	int resizeGetHeight(int width) override;

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	HistoryTextState getState(int x, int y, HistoryStateRequest request) const override;

	TextSelection adjustSelection(TextSelection selection, TextSelectType type) const override {
		return _caption.adjustSelection(selection, type);
	}
	bool hasTextForCopy() const override {
		return !_caption.isEmpty();
	}

	QString notificationText() const override;
	QString inDialogsText() const override;
	TextWithEntities selectedText(TextSelection selection) const override;

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
	bool needsBubble() const override {
		if (!_caption.isEmpty()) {
			return true;
		}
		if (auto message = _parent->toHistoryMessage()) {
			return message->viaBot()
				|| message->Has<HistoryMessageForwarded>()
				|| message->Has<HistoryMessageReply>()
				|| message->displayFromName();
		}
		return false;
	}
	bool customInfoLayout() const override {
		return _caption.isEmpty();
	}
	bool skipBubbleTail() const override {
		return isBubbleBottom() && _caption.isEmpty();
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
	DocumentData *_data;
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
	HistoryDocument(HistoryItem *parent, DocumentData *document, const QString &caption);
	HistoryDocument(HistoryItem *parent, const HistoryDocument &other);
	HistoryMediaType type() const override {
		return _data->voice() ? MediaTypeVoiceFile : (_data->song() ? MediaTypeMusicFile : MediaTypeFile);
	}
	HistoryDocument *clone(HistoryItem *newParent) const override {
		return new HistoryDocument(newParent, *this);
	}

	void initDimensions() override;
	int resizeGetHeight(int width) override;

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	HistoryTextState getState(int x, int y, HistoryStateRequest request) const override;
	void updatePressed(int x, int y) override;

	TextSelection adjustSelection(TextSelection selection, TextSelectType type) const override {
		if (auto captioned = Get<HistoryDocumentCaptioned>()) {
			return captioned->_caption.adjustSelection(selection, type);
		}
		return selection;
	}
	bool hasTextForCopy() const override {
		return Has<HistoryDocumentCaptioned>();
	}

	QString notificationText() const override;
	QString inDialogsText() const override;
	TextWithEntities selectedText(TextSelection selection) const override;

	bool uploading() const override {
		return _data->uploading();
	}

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

	DocumentData *_data;

};

class HistoryGif : public HistoryFileMedia {
public:
	HistoryGif(HistoryItem *parent, DocumentData *document, const QString &caption);
	HistoryGif(HistoryItem *parent, const HistoryGif &other);
	HistoryMediaType type() const override {
		return MediaTypeGif;
	}
	HistoryGif *clone(HistoryItem *newParent) const override {
		return new HistoryGif(newParent, *this);
	}

	void initDimensions() override;
	int resizeGetHeight(int width) override;

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	HistoryTextState getState(int x, int y, HistoryStateRequest request) const override;

	TextSelection adjustSelection(TextSelection selection, TextSelectType type) const override {
		return _caption.adjustSelection(selection, type);
	}
	bool hasTextForCopy() const override {
		return !_caption.isEmpty();
	}

	QString notificationText() const override;
	QString inDialogsText() const override;
	TextWithEntities selectedText(TextSelection selection) const override;

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
	bool needsBubble() const override {
		if (!_caption.isEmpty()) {
			return true;
		}
		if (auto message = _parent->toHistoryMessage()) {
			return message->viaBot()
				|| message->Has<HistoryMessageForwarded>()
				|| message->Has<HistoryMessageReply>()
				|| message->displayFromName();
		}
		return false;
	}
	bool customInfoLayout() const override {
		return _caption.isEmpty();
	}
	bool skipBubbleTail() const override {
		return isBubbleBottom() && _caption.isEmpty();
	}
	bool isReadyForOpen() const override {
		return _data->loaded();
	}

	~HistoryGif();

protected:
	float64 dataProgress() const override;
	bool dataFinished() const override;
	bool dataLoaded() const override;

private:
	DocumentData *_data;
	int32 _thumbw = 1;
	int32 _thumbh = 1;
	Text _caption;

	Media::Clip::ReaderPointer _gif;

	void setStatusSize(int32 newSize) const;
	void updateStatusText() const;

};

class HistorySticker : public HistoryMedia {
public:
	HistorySticker(HistoryItem *parent, DocumentData *document);
	HistoryMediaType type() const override {
		return MediaTypeSticker;
	}
	HistorySticker *clone(HistoryItem *newParent) const override {
		return new HistorySticker(newParent, _data);
	}

	void initDimensions() override;
	int resizeGetHeight(int width) override;

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	HistoryTextState getState(int x, int y, HistoryStateRequest request) const override;

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
	DocumentData *_data;
	QString _emoji;

};

class HistoryContact : public HistoryMedia {
public:
	HistoryContact(HistoryItem *parent, int32 userId, const QString &first, const QString &last, const QString &phone);
	HistoryMediaType type() const override {
		return MediaTypeContact;
	}
	HistoryContact *clone(HistoryItem *newParent) const override {
		return new HistoryContact(newParent, _userId, _fname, _lname, _phone);
	}

	void initDimensions() override;

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	HistoryTextState getState(int x, int y, HistoryStateRequest request) const override;

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

	int32 _userId;
	UserData *_contact = nullptr;

	int _phonew = 0;
	QString _fname, _lname, _phone;
	Text _name;
	EmptyUserpic _photoEmpty;

	ClickHandlerPtr _linkl;
	int _linkw = 0;
	QString _link;
};

class HistoryWebPage : public HistoryMedia {
public:
	HistoryWebPage(HistoryItem *parent, WebPageData *data);
	HistoryWebPage(HistoryItem *parent, const HistoryWebPage &other);
	HistoryMediaType type() const override {
		return MediaTypeWebPage;
	}
	HistoryWebPage *clone(HistoryItem *newParent) const override {
		return new HistoryWebPage(newParent, *this);
	}

	void initDimensions() override;
	int resizeGetHeight(int width) override;

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	HistoryTextState getState(int x, int y, HistoryStateRequest request) const override;

	TextSelection adjustSelection(TextSelection selection, TextSelectType type) const override;
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
		return !_data->pendingTill;
	}
	DocumentData *getDocument() override {
		return _attach ? _attach->getDocument() : 0;
	}
	Media::Clip::Reader *getClipReader() override {
		return _attach ? _attach->getClipReader() : 0;
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

	WebPageData *webpage() {
		return _data;
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
	TextSelection toDescriptionSelection(TextSelection selection) const {
		return internal::unshiftSelection(selection, _title);
	}
	TextSelection fromDescriptionSelection(TextSelection selection) const {
		return internal::shiftSelection(selection, _title);
	}
	QMargins inBubblePadding() const;
	int bottomInfoPadding() const;

	WebPageData *_data;
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
	HistoryGame(HistoryItem *parent, GameData *data);
	HistoryGame(HistoryItem *parent, const HistoryGame &other);
	HistoryMediaType type() const override {
		return MediaTypeGame;
	}
	HistoryGame *clone(HistoryItem *newParent) const override {
		return new HistoryGame(newParent, *this);
	}

	void initDimensions() override;
	int resizeGetHeight(int width) override;

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	HistoryTextState getState(int x, int y, HistoryStateRequest request) const override;

	TextSelection adjustSelection(TextSelection selection, TextSelectType type) const override;
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

class LocationCoords;
struct LocationData;

class HistoryLocation : public HistoryMedia {
public:
	HistoryLocation(HistoryItem *parent, const LocationCoords &coords, const QString &title = QString(), const QString &description = QString());
	HistoryLocation(HistoryItem *parent, const HistoryLocation &other);
	HistoryMediaType type() const override {
		return MediaTypeLocation;
	}
	HistoryLocation *clone(HistoryItem *newParent) const override {
		return new HistoryLocation(newParent, *this);
	}

	void initDimensions() override;
	int resizeGetHeight(int32 width) override;

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	HistoryTextState getState(int x, int y, HistoryStateRequest request) const override;

	TextSelection adjustSelection(TextSelection selection, TextSelectType type) const override;
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

	bool needsBubble() const override {
		if (!_title.isEmpty() || !_description.isEmpty()) {
			return true;
		}
		if (auto message = _parent->toHistoryMessage()) {
			return message->viaBot()
				|| message->Has<HistoryMessageForwarded>()
				|| message->Has<HistoryMessageReply>()
				|| message->displayFromName();
		}
		return false;
	}
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
