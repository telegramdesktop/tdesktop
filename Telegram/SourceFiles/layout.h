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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

static const uint32 FullSelection = 0xFFFFFFFF;

extern TextParseOptions _textNameOptions, _textDlgOptions;
extern TextParseOptions _historyTextOptions, _historyBotOptions, _historyTextNoMonoOptions, _historyBotNoMonoOptions;

const TextParseOptions &itemTextOptions(History *h, PeerData *f);
const TextParseOptions &itemTextNoMonoOptions(History *h, PeerData *f);

enum RoundCorners {
	NoneCorners = 0x00, // for images
	BlackCorners,
	ServiceCorners,
	ServiceSelectedCorners,
	SelectedOverlayCorners,
	DateCorners,
	DateSelectedCorners,
	ForwardCorners,
	MediaviewSaveCorners,
	EmojiHoverCorners,
	StickerHoverCorners,
	BotKeyboardCorners,
	BotKeyboardOverCorners,
	BotKeyboardDownCorners,
	PhotoSelectOverlayCorners,

	DocBlueCorners,
	DocGreenCorners,
	DocRedCorners,
	DocYellowCorners,

	InShadowCorners, // for photos without bg
	InSelectedShadowCorners,

	MessageInCorners, // with shadow
	MessageInSelectedCorners,
	MessageOutCorners,
	MessageOutSelectedCorners,

	RoundCornersCount
};

static const int32 FileStatusSizeReady = 0x7FFFFFF0;
static const int32 FileStatusSizeLoaded = 0x7FFFFFF1;
static const int32 FileStatusSizeFailed = 0x7FFFFFF2;

QString formatSizeText(qint64 size);
QString formatDownloadText(qint64 ready, qint64 total);
QString formatDurationText(qint64 duration);
QString formatDurationAndSizeText(qint64 duration, qint64 size);
QString formatGifAndSizeText(qint64 size);
QString formatPlayedText(qint64 played, qint64 duration);

QString documentName(DocumentData *document);
int32 documentColorIndex(DocumentData *document, QString &ext);
style::color documentColor(int32 colorIndex);
style::color documentDarkColor(int32 colorIndex);
style::color documentOverColor(int32 colorIndex);
style::color documentSelectedColor(int32 colorIndex);
style::sprite documentCorner(int32 colorIndex);
RoundCorners documentCorners(int32 colorIndex);

class OverviewPaintContext;
class InlinePaintContext;
class PaintContext {
public:

	PaintContext(uint64 ms, bool selecting) : ms(ms), selecting(selecting) {
	}
	uint64 ms;
	bool selecting;

	virtual const OverviewPaintContext *toOverviewPaintContext() const {
		return nullptr;
	}
	virtual const InlinePaintContext *toInlinePaintContext() const {
		return nullptr;
	}

};

class LayoutMediaItem;
class LayoutItem : public Composer, public ClickHandlerHost {
public:
	LayoutItem() {
	}
	LayoutItem &operator=(const LayoutItem &) = delete;

	int32 maxWidth() const {
		return _maxw;
	}
	int32 minHeight() const {
		return _minh;
	}
	virtual void initDimensions() = 0;
	virtual int32 resizeGetHeight(int32 width) {
		_width = qMin(width, _maxw);
		_height = _minh;
		return _height;
	}

	virtual void paint(Painter &p, const QRect &clip, uint32 selection, const PaintContext *context) const = 0;
	virtual void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int32 x, int32 y) const {
		link.clear();
		cursor = HistoryDefaultCursorState;
	}
	virtual void getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const { // from text
		upon = hasPoint(x, y);
		symbol = upon ? 0xFFFF : 0;
		after = false;
	}

	int32 width() const {
		return _width;
	}
	int32 height() const {
		return _height;
	}

	bool hasPoint(int32 x, int32 y) const {
		return (x >= 0 && y >= 0 && x < width() && y < height());
	}

	virtual ~LayoutItem() {
	}

	virtual LayoutMediaItem *toLayoutMediaItem() {
		return nullptr;
	}
	virtual const LayoutMediaItem *toLayoutMediaItem() const {
		return nullptr;
	}

	virtual HistoryItem *getItem() const {
		return nullptr;
	}
	virtual DocumentData *getDocument() const {
		return nullptr;
	}
	MsgId msgId() const {
		const HistoryItem *item = getItem();
		return item ? item->id : 0;
	}

protected:
	int _width = 0;
	int _height = 0;
	int _maxw = 0;
	int _minh = 0;

};

class LayoutMediaItem : public LayoutItem {
public:
	LayoutMediaItem(HistoryItem *parent) : _parent(parent) {
	}

	LayoutMediaItem *toLayoutMediaItem() override {
		return this;
	}
	const LayoutMediaItem *toLayoutMediaItem() const override {
		return this;
	}
	HistoryItem *getItem() const override {
		return _parent;
	}

	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool active) override;

protected:
	HistoryItem *_parent;

};

class LayoutRadialProgressItem : public LayoutMediaItem {
public:
	LayoutRadialProgressItem(HistoryItem *parent) : LayoutMediaItem(parent)
		, _radial(0)
		, a_iconOver(0, 0)
		, _a_iconOver(animation(this, &LayoutRadialProgressItem::step_iconOver)) {
	}

	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool active) override;

	~LayoutRadialProgressItem();

protected:
	ClickHandlerPtr _openl, _savel, _cancell;
	void setLinks(ClickHandlerPtr &&openl, ClickHandlerPtr &&savel, ClickHandlerPtr &&cancell);
	void setDocumentLinks(DocumentData *document) {
		ClickHandlerPtr save;
		if (document->voice()) {
			save.reset(new DocumentOpenClickHandler(document));
		} else {
			save.reset(new DocumentSaveClickHandler(document));
		}
		setLinks(MakeShared<DocumentOpenClickHandler>(document), std_::move(save), MakeShared<DocumentCancelClickHandler>(document));
	}

	void step_iconOver(float64 ms, bool timer);
	void step_radial(uint64 ms, bool timer);

	void ensureRadial() const;
	void checkRadialFinished();

	bool isRadialAnimation(uint64 ms) const {
		if (!_radial || !_radial->animating()) return false;

		_radial->step(ms);
		return _radial && _radial->animating();
	}

	virtual float64 dataProgress() const = 0;
	virtual bool dataFinished() const = 0;
	virtual bool dataLoaded() const = 0;
	virtual bool iconAnimated() const {
		return false;
	}

	mutable RadialAnimation *_radial;
	anim::fvalue a_iconOver;
	mutable Animation _a_iconOver;

private:
	LayoutRadialProgressItem(const LayoutRadialProgressItem &other);

};

class LayoutAbstractFileItem : public LayoutRadialProgressItem {
public:
	LayoutAbstractFileItem(HistoryItem *parent) : LayoutRadialProgressItem(parent) {
	}

protected:
	// >= 0 will contain download / upload string, _statusSize = loaded bytes
	// < 0 will contain played string, _statusSize = -(seconds + 1) played
	// 0x7FFFFFF0 will contain status for not yet downloaded file
	// 0x7FFFFFF1 will contain status for already downloaded file
	// 0x7FFFFFF2 will contain status for failed to download / upload file
	mutable int32 _statusSize;
	mutable QString _statusText;

	// duration = -1 - no duration, duration = -2 - "GIF" duration
	void setStatusSize(int32 newSize, int32 fullSize, int32 duration, qint64 realDuration) const;

};

class OverviewPaintContext : public PaintContext {
public:
	OverviewPaintContext(uint64 ms, bool selecting) : PaintContext(ms, selecting), isAfterDate(false) {
	}
	const OverviewPaintContext *toOverviewPaintContext() const {
		return this;
	}
	bool isAfterDate;

};

struct OverviewItemInfo : public BaseComponent<OverviewItemInfo> {
	int top = 0;
};

class LayoutOverviewDate : public LayoutItem {
public:
	LayoutOverviewDate(const QDate &date, bool month);

	void initDimensions() override;
	void paint(Painter &p, const QRect &clip, uint32 selection, const PaintContext *context) const override;

private:
	QDate _date;
	QString _text;

};

class LayoutOverviewPhoto : public LayoutMediaItem {
public:
	LayoutOverviewPhoto(PhotoData *photo, HistoryItem *parent);

	void initDimensions() override;
	int32 resizeGetHeight(int32 width) override;
	void paint(Painter &p, const QRect &clip, uint32 selection, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int32 x, int32 y) const override;

private:
	PhotoData *_data;
	ClickHandlerPtr _link;

	mutable QPixmap _pix;
	mutable bool _goodLoaded;

};

class LayoutOverviewVideo : public LayoutAbstractFileItem {
public:
	LayoutOverviewVideo(DocumentData *video, HistoryItem *parent);

	void initDimensions() override;
	int32 resizeGetHeight(int32 width) override;
	void paint(Painter &p, const QRect &clip, uint32 selection, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int32 x, int32 y) const override;

protected:
	float64 dataProgress() const override {
		return _data->progress();
	}
	bool dataFinished() const override {
		return !_data->loading();
	}
	bool dataLoaded() const override {
		return _data->loaded();
	}
	bool iconAnimated() const override {
		return true;
	}

private:
	DocumentData *_data;

	QString _duration;
	mutable QPixmap _pix;
	mutable bool _thumbLoaded;

	void updateStatusText() const;

};

class LayoutOverviewVoice : public LayoutAbstractFileItem {
public:
	LayoutOverviewVoice(DocumentData *voice, HistoryItem *parent);

	void initDimensions() override;
	void paint(Painter &p, const QRect &clip, uint32 selection, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int32 x, int32 y) const override;

protected:
	float64 dataProgress() const override {
		return _data->progress();
	}
	bool dataFinished() const override {
		return !_data->loading();
	}
	bool dataLoaded() const override {
		return _data->loaded();
	}
	bool iconAnimated() const override {
		return true;
	}

private:
	DocumentData *_data;
	ClickHandlerPtr _namel;

	mutable Text _name, _details;
	mutable int32 _nameVersion;

	void updateName() const;
	bool updateStatusText() const;

};

class LayoutOverviewDocument : public LayoutAbstractFileItem {
public:
	LayoutOverviewDocument(DocumentData *document, HistoryItem *parent);

	void initDimensions() override;
	void paint(Painter &p, const QRect &clip, uint32 selection, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int32 x, int32 y) const override;

	virtual DocumentData *getDocument() const {
		return _data;
	}

protected:
	float64 dataProgress() const override {
		return _data->progress();
	}
	bool dataFinished() const override {
		return !_data->loading();
	}
	bool dataLoaded() const override {
		return _data->loaded();
	}
	bool iconAnimated() const override {
		return _data->song() || !_data->loaded() || (_radial && _radial->animating());
	}

private:
	DocumentData *_data;
	ClickHandlerPtr _msgl, _namel;

	mutable bool _thumbForLoaded;
	mutable QPixmap _thumb;

	QString _name, _date, _ext;
	int32 _namew, _datew, _extw;
	int32 _thumbw, _colorIndex;

	bool withThumb() const {
		return !_data->thumb->isNull() && _data->thumb->width() && _data->thumb->height();
	}
	bool updateStatusText() const;

};

class LayoutOverviewLink : public LayoutMediaItem {
public:
	LayoutOverviewLink(HistoryMedia *media, HistoryItem *parent);

	void initDimensions() override;
	int32 resizeGetHeight(int32 width) override;
	void paint(Painter &p, const QRect &clip, uint32 selection, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int32 x, int32 y) const override;

private:
	ClickHandlerPtr _photol;

	QString _title, _letter;
	int _titlew = 0;
	WebPageData *_page = nullptr;
	int _pixw = 0;
	int _pixh = 0;
	Text _text = { int(st::msgMinWidth) };

	struct Link {
		Link() : width(0) {
		}
		Link(const QString &url, const QString &text);
		QString text;
		int32 width;
		TextClickHandlerPtr lnk;
	};
	QVector<Link> _links;

};

class InlinePaintContext : public PaintContext {
public:
	InlinePaintContext(uint64 ms, bool selecting, bool paused, bool lastRow)
		: PaintContext(ms, selecting)
		, paused(paused)
		, lastRow(lastRow) {
	}
	const InlinePaintContext *toInlinePaintContext() const override {
		return this;
	}
	bool paused, lastRow;
};

class LayoutInlineItem : public LayoutItem {
public:

	LayoutInlineItem(InlineResult *result, DocumentData *doc, PhotoData *photo);

	virtual void setPosition(int32 position);
	int32 position() const;

	virtual bool fullLine() const {
		return true;
	}

	InlineResult *result() const;
	DocumentData *document() const;
	PhotoData *photo() const;
	void preload();

	void update();

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override {
		update();
	}
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override {
		update();
	}

protected:
	InlineResult *_result;
	DocumentData *_doc;
	PhotoData *_photo;

	int32 _position; // < 0 means removed from layout

};

// this type used as a flag, we dynamic_cast<> to it
class SendInlineItemClickHandler : public ClickHandler {
public:
	void onClick(Qt::MouseButton) const override {
	}
};

class DeleteSavedGifClickHandler : public LeftButtonClickHandler {
public:
	DeleteSavedGifClickHandler(DocumentData *data) : _data(data) {
	}

protected:
	void onClickImpl() const override;

private:
	DocumentData  *_data;

};

class LayoutInlineGif : public LayoutInlineItem {
public:
	LayoutInlineGif(InlineResult *result, DocumentData *doc, bool saved);

	void setPosition(int32 position) override;
	void initDimensions() override;

	bool fullLine() const override {
		return false;
	}

	void paint(Painter &p, const QRect &clip, uint32 selection, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int32 x, int32 y) const override;

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

	~LayoutInlineGif();

private:
	QSize countFrameSize() const;

	int32 content_width() const;
	int32 content_height() const;
	bool content_loading() const;
	bool content_displayLoading() const;
	bool content_loaded() const;
	float64 content_progress() const;
	void content_automaticLoad() const;
	void content_forget();
	FileLocation content_location() const;
	QByteArray content_data() const;

	enum StateFlags {
		StateOver       = 0x01,
		StateDeleteOver = 0x02,
	};
	int32 _state;

	ClipReader *_gif;
	ClickHandlerPtr _send, _delete;
	bool gif() const {
		return (!_gif || _gif == BadClipReader) ? false : true;
	}
	mutable QPixmap _thumb;
	void prepareThumb(int32 width, int32 height, const QSize &frame) const;

	void ensureAnimation() const;
	bool isRadialAnimation(uint64 ms) const;
	void step_radial(uint64 ms, bool timer);

	void clipCallback(ClipReaderNotification notification);

	struct AnimationData {
		AnimationData(AnimationCreator creator)
			: over(false)
			, radial(creator) {
		}
		bool over;
		FloatAnimation _a_over;
		RadialAnimation radial;
	};
	mutable AnimationData *_animation;
	mutable FloatAnimation _a_deleteOver;

};

class LayoutInlinePhoto : public LayoutInlineItem {
public:
	LayoutInlinePhoto(InlineResult *result, PhotoData *photo);

	void initDimensions() override;

	bool fullLine() const override {
		return false;
	}

	void paint(Painter &p, const QRect &clip, uint32 selection, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int32 x, int32 y) const override;

private:
	QSize countFrameSize() const;

	int32 content_width() const;
	int32 content_height() const;
	bool content_loaded() const;
	void content_forget();

	ClickHandlerPtr _send;

	mutable QPixmap _thumb;
	mutable bool _thumbLoaded;
	void prepareThumb(int32 width, int32 height, const QSize &frame) const;

};

class LayoutInlineWebVideo : public LayoutInlineItem {
public:
	LayoutInlineWebVideo(InlineResult *result);

	void initDimensions() override;

	void paint(Painter &p, const QRect &clip, uint32 selection, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int32 x, int32 y) const override;

private:

	ClickHandlerPtr _send, _link;

	mutable QPixmap _thumb;
	Text _title, _description;
	QString _duration;
	int32 _durationWidth;

	void prepareThumb(int32 width, int32 height) const;

};

class LayoutInlineArticle : public LayoutInlineItem {
public:
	LayoutInlineArticle(InlineResult *result, bool withThumb);

	void initDimensions() override;
	int32 resizeGetHeight(int32 width) override;

	void paint(Painter &p, const QRect &clip, uint32 selection, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int32 x, int32 y) const override;

private:

	ClickHandlerPtr _send, _url, _link;

	bool _withThumb;
	mutable QPixmap _thumb;
	Text _title, _description;
	QString _letter, _urlText;
	int32 _urlWidth;

	void prepareThumb(int32 width, int32 height) const;

};
