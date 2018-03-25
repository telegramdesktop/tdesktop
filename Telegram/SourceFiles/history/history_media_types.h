/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/runtime_composer.h"
#include "history/history_media.h"
#include "ui/effects/radial_animation.h"
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_web_page.h"
#include "data/data_game.h"

class ReplyMarkupClickHandler;
struct HistoryDocumentNamed;
struct HistoryMessageVia;
struct HistoryMessageReply;
struct HistoryMessageForwarded;

namespace Data {
enum class CallFinishReason : char;
struct Invoice;
struct Call;
} // namespace Data

namespace Media {
namespace Clip {
class Playback;
} // namespace Clip

namespace Player {
class RoundController;
} // namespace Player
} // namespace Media

namespace Ui {
class EmptyUserpic;
} // namespace Ui

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

	void refreshParentId(not_null<HistoryItem*> realParent) override;

	bool allowsFastShare() const override {
		return true;
	}

	~HistoryFileMedia();

protected:
	using FileClickHandlerPtr = std::shared_ptr<FileClickHandler>;
	FileClickHandlerPtr _openl, _savel, _cancell;

	void setLinks(
		FileClickHandlerPtr &&openl,
		FileClickHandlerPtr &&savel,
		FileClickHandlerPtr &&cancell);
	void setDocumentLinks(
		not_null<DocumentData*> document,
		not_null<HistoryItem*> realParent,
		bool inlinegif = false);

	// >= 0 will contain download / upload string, _statusSize = loaded bytes
	// < 0 will contain played string, _statusSize = -(seconds + 1) played
	// 0x7FFFFFF0 will contain status for not yet downloaded file
	// 0x7FFFFFF1 will contain status for already downloaded file
	// 0x7FFFFFF2 will contain status for failed to download / upload file
	mutable int _statusSize;
	mutable QString _statusText;

	// duration = -1 - no duration, duration = -2 - "GIF" duration
	void setStatusSize(int newSize, int fullSize, int duration, qint64 realDuration) const;

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
	HistoryPhoto(
		not_null<Element*> parent,
		not_null<HistoryItem*> realParent,
		not_null<PhotoData*> photo);
	HistoryPhoto(
		not_null<Element*> parent,
		not_null<PeerData*> chat,
		not_null<PhotoData*> photo,
		int width);

	HistoryMediaType type() const override {
		return MediaTypePhoto;
	}

	void draw(Painter &p, const QRect &clip, TextSelection selection, TimeMs ms) const override;
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

	PhotoData *getPhoto() const override {
		return _data;
	}

	QSize sizeForGrouping() const override;
	void drawGrouped(
		Painter &p,
		const QRect &clip,
		TextSelection selection,
		TimeMs ms,
		const QRect &geometry,
		RectParts corners,
		not_null<uint64*> cacheKey,
		not_null<QPixmap*> cache) const override;
	TextState getStateGrouped(
		const QRect &geometry,
		QPoint point,
		StateRequest request) const override;

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
	bool isReadyForOpen() const override {
		return _data->loaded();
	}

	void parentTextUpdated() override;

protected:
	float64 dataProgress() const override;
	bool dataFinished() const override;
	bool dataLoaded() const override;

private:
	void create(FullMsgId contextId, PeerData *chat = nullptr);

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	bool needInfoDisplay() const;
	void validateGroupedCache(
		const QRect &geometry,
		RectParts corners,
		not_null<uint64*> cacheKey,
		not_null<QPixmap*> cache) const;

	not_null<PhotoData*> _data;
	int _serviceWidth = 0;
	int _pixw = 1;
	int _pixh = 1;
	Text _caption;

};

class HistoryVideo : public HistoryFileMedia {
public:
	HistoryVideo(
		not_null<Element*> parent,
		not_null<HistoryItem*> realParent,
		not_null<DocumentData*> document);

	HistoryMediaType type() const override {
		return MediaTypeVideo;
	}

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

	DocumentData *getDocument() const override {
		return _data;
	}

	QSize sizeForGrouping() const override;
	void drawGrouped(
		Painter &p,
		const QRect &clip,
		TextSelection selection,
		TimeMs ms,
		const QRect &geometry,
		RectParts corners,
		not_null<uint64*> cacheKey,
		not_null<QPixmap*> cache) const override;
	TextState getStateGrouped(
		const QRect &geometry,
		QPoint point,
		StateRequest request) const override;

	bool uploading() const override {
		return _data->uploading();
	}

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

	void parentTextUpdated() override;

protected:
	float64 dataProgress() const override;
	bool dataFinished() const override;
	bool dataLoaded() const override;

private:
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	void validateGroupedCache(
		const QRect &geometry,
		RectParts corners,
		not_null<uint64*> cacheKey,
		not_null<QPixmap*> cache) const;
	void setStatusSize(int newSize) const;
	void updateStatusText() const;

	not_null<DocumentData*> _data;
	int _thumbw;
	Text _caption;

};

class HistoryDocument
	: public HistoryFileMedia
	, public RuntimeComposer<HistoryDocument> {
public:
	HistoryDocument(
		not_null<Element*> parent,
		not_null<DocumentData*> document);

	HistoryMediaType type() const override {
		return _data->isVoiceMessage()
			? MediaTypeVoiceFile
			: (_data->isSong()
				? MediaTypeMusicFile
				: MediaTypeFile);
	}

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	TextState textState(QPoint point, StateRequest request) const override;
	void updatePressed(QPoint point) override;

	[[nodiscard]] TextSelection adjustSelection(
		TextSelection selection,
		TextSelectType type) const override;
	uint16 fullSelectionLength() const override;
	bool hasTextForCopy() const override;

	TextWithEntities selectedText(TextSelection selection) const override;

	bool uploading() const override {
		return _data->uploading();
	}

	DocumentData *getDocument() const override {
		return _data;
	}

	TextWithEntities getCaption() const override;
	bool needsBubble() const override {
		return true;
	}
	bool customInfoLayout() const override {
		return false;
	}
	QMargins bubbleMargins() const override;
	bool hideForwardedFrom() const override {
		return _data->isSong();
	}

	void step_voiceProgress(float64 ms, bool timer);

	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

	void refreshParentId(not_null<HistoryItem*> realParent) override;
	void parentTextUpdated() override;

protected:
	float64 dataProgress() const override;
	bool dataFinished() const override;
	bool dataLoaded() const override;

private:
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	void createComponents(bool caption);
	void fillNamedFromData(HistoryDocumentNamed *named);

	void setStatusSize(int newSize, qint64 realDuration = 0) const;
	bool updateStatusText() const; // returns showPause

	not_null<DocumentData*> _data;

};

class HistoryGif : public HistoryFileMedia {
public:
	HistoryGif(
		not_null<Element*> parent,
		not_null<DocumentData*> document);

	HistoryMediaType type() const override {
		return MediaTypeGif;
	}

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

	bool uploading() const override {
		return _data->uploading();
	}

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
	bool isReadyForOpen() const override {
		return _data->loaded();
	}

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

class HistorySticker : public HistoryMedia {
public:
	HistorySticker(
		not_null<Element*> parent,
		not_null<DocumentData*> document);

	HistoryMediaType type() const override {
		return MediaTypeSticker;
	}

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const override {
		return true;
	}
	bool dragItem() const override {
		return true;
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return true;
	}

	DocumentData *getDocument() const override {
		return _data;
	}

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
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	int additionalWidth(const HistoryMessageVia *via, const HistoryMessageReply *reply) const;
	int additionalWidth() const;

	int _pixw = 1;
	int _pixh = 1;
	ClickHandlerPtr _packLink;
	not_null<DocumentData*> _data;
	QString _emoji;

};

class HistoryContact : public HistoryMedia {
public:
	HistoryContact(
		not_null<Element*> parent,
		UserId userId,
		const QString &first,
		const QString &last,
		const QString &phone);
	~HistoryContact();

	HistoryMediaType type() const override {
		return MediaTypeContact;
	}

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const override {
		return true;
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return true;
	}

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

	// Should be called only by Data::Session.
	void updateSharedContactUserId(UserId userId) override;

private:
	QSize countOptimalSize() override;

	UserId _userId = 0;
	UserData *_contact = nullptr;

	int _phonew = 0;
	QString _fname, _lname, _phone;
	Text _name;
	std::unique_ptr<Ui::EmptyUserpic> _photoEmpty;

	ClickHandlerPtr _linkl;
	int _linkw = 0;
	QString _link;

};

class HistoryCall : public HistoryMedia {
public:
	HistoryCall(
		not_null<Element*> parent,
		not_null<Data::Call*> call);

	HistoryMediaType type() const override {
		return MediaTypeCall;
	}

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const override {
		return true;
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return false;
	}

	bool needsBubble() const override {
		return true;
	}
	bool customInfoLayout() const override {
		return true;
	}

	Data::CallFinishReason reason() const;

private:
	using FinishReason = Data::CallFinishReason;

	QSize countOptimalSize() override;

	FinishReason _reason;
	int _duration = 0;

	QString _text;
	QString _status;

	ClickHandlerPtr _link;

};

class HistoryWebPage : public HistoryMedia {
public:
	HistoryWebPage(
		not_null<Element*> parent,
		not_null<WebPageData*> data);

	HistoryMediaType type() const override {
		return MediaTypeWebPage;
	}

	void refreshParentId(not_null<HistoryItem*> realParent) override;

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	bool hideMessageText() const override {
		return false;
	}

	[[nodiscard]] TextSelection adjustSelection(
		TextSelection selection,
		TextSelectType type) const override;
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

	bool isDisplayed() const override;
	PhotoData *getPhoto() const override {
		return _attach ? _attach->getPhoto() : nullptr;
	}
	DocumentData *getDocument() const override {
		return _attach ? _attach->getDocument() : nullptr;
	}
	void stopAnimation() override {
		if (_attach) _attach->stopAnimation();
	}

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

	~HistoryWebPage();

private:
	void playAnimation(bool autoplay) override;
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	TextSelection toDescriptionSelection(TextSelection selection) const;
	TextSelection fromDescriptionSelection(TextSelection selection) const;
	QMargins inBubblePadding() const;
	int bottomInfoPadding() const;
	bool isLogEntryOriginal() const;

	not_null<WebPageData*> _data;
	ClickHandlerPtr _openl;
	std::unique_ptr<HistoryMedia> _attach;

	bool _asArticle = false;
	int _dataVersion = -1;
	int _titleLines = 0;
	int _descriptionLines = 0;

	Text _title, _description;
	int _siteNameWidth = 0;

	QString _duration;
	int _durationWidth = 0;

	int _pixw = 0;
	int _pixh = 0;

};

class HistoryGame : public HistoryMedia {
public:
	HistoryGame(
		not_null<Element*> parent,
		not_null<GameData*> data,
		const TextWithEntities &consumed);

	HistoryMediaType type() const override {
		return MediaTypeGame;
	}

	void refreshParentId(not_null<HistoryItem*> realParent) override;

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	[[nodiscard]] TextSelection adjustSelection(
		TextSelection selection,
		TextSelectType type) const override;
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

	PhotoData *getPhoto() const override {
		return _attach ? _attach->getPhoto() : nullptr;
	}
	DocumentData *getDocument() const override {
		return _attach ? _attach->getDocument() : nullptr;
	}
	void stopAnimation() override {
		if (_attach) _attach->stopAnimation();
	}

	not_null<GameData*> game() {
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

	~HistoryGame();

private:
	void playAnimation(bool autoplay) override;
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	TextSelection toDescriptionSelection(TextSelection selection) const;
	TextSelection fromDescriptionSelection(TextSelection selection) const;
	QMargins inBubblePadding() const;
	int bottomInfoPadding() const;

	not_null<GameData*> _data;
	std::shared_ptr<ReplyMarkupClickHandler> _openl;
	std::unique_ptr<HistoryMedia> _attach;

	int _titleLines, _descriptionLines;

	Text _title, _description;

	int _gameTagWidth = 0;

};

class HistoryInvoice : public HistoryMedia {
public:
	HistoryInvoice(
		not_null<Element*> parent,
		not_null<Data::Invoice*> invoice);

	HistoryMediaType type() const override {
		return MediaTypeInvoice;
	}

	void refreshParentId(not_null<HistoryItem*> realParent) override;

	MsgId getReceiptMsgId() const {
		return _receiptMsgId;
	}
	QString getTitle() const {
		return _title.originalText();
	}
	static QString fillAmountAndCurrency(uint64 amount, const QString &currency);

	bool hideMessageText() const override {
		return false;
	}

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	[[nodiscard]] TextSelection adjustSelection(
		TextSelection selection,
		TextSelectType type) const override;
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
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	void fillFromData(not_null<Data::Invoice*> invoice);

	TextSelection toDescriptionSelection(TextSelection selection) const;
	TextSelection fromDescriptionSelection(TextSelection selection) const;
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
	HistoryLocation(
		not_null<Element*> parent,
		not_null<LocationData*> location,
		const QString &title = QString(),
		const QString &description = QString());

	HistoryMediaType type() const override {
		return MediaTypeLocation;
	}

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	[[nodiscard]] TextSelection adjustSelection(
		TextSelection selection,
		TextSelectType type) const override;
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

	TextWithEntities selectedText(TextSelection selection) const override;

	bool needsBubble() const override;
	bool customInfoLayout() const override {
		return true;
	}

	bool skipBubbleTail() const override {
		return isBubbleBottom();
	}

private:
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	TextSelection toDescriptionSelection(TextSelection selection) const;
	TextSelection fromDescriptionSelection(TextSelection selection) const;

	LocationData *_data;
	Text _title, _description;
	ClickHandlerPtr _link;

	int fullWidth() const;
	int fullHeight() const;

};
