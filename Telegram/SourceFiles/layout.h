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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
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
style::sprite documentCorner(int32 colorIndex);
RoundCorners documentCorners(int32 colorIndex);

class LayoutMediaItem;
class OverviewItemInfo;

class LayoutItem {
public:
	LayoutItem() : _maxw(0), _minh(0) {
	}

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

	virtual void paint(Painter &p, const QRect &clip, uint32 selection, uint64 ms) const = 0;
	virtual void getState(TextLinkPtr &link, HistoryCursorState &cursor, int32 x, int32 y) const {
		link = TextLinkPtr();
		cursor = HistoryDefaultCursorState;
	}
	virtual void getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const { // from text
		upon = hasPoint(x, y);
		symbol = upon ? 0xFFFF : 0;
		after = false;
	}
	virtual void linkOver(const TextLinkPtr &lnk) {
	}
	virtual void linkOut(const TextLinkPtr &lnk) {
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
		return 0;
	}
	virtual const LayoutMediaItem *toLayoutMediaItem() const {
		return 0;
	}

	virtual HistoryItem *getItem() const {
		return 0;
	}
	virtual DocumentData *getDocument() const {
		return 0;
	}
	virtual OverviewItemInfo *getOverviewItemInfo() {
		return 0;
	}
	virtual const OverviewItemInfo *getOverviewItemInfo() const {
		return 0;
	}
	MsgId msgId() const {
		const HistoryItem *item = getItem();
		return item ? item->id : 0;
	}

protected:
	int32 _width, _height, _maxw, _minh;
	LayoutItem &operator=(const LayoutItem &);

};

class LayoutMediaItem : public LayoutItem {
public:
	LayoutMediaItem(HistoryItem *parent) : _parent(parent) {
	}

	virtual LayoutMediaItem *toLayoutMediaItem() {
		return this;
	}
	virtual const LayoutMediaItem *toLayoutMediaItem() const {
		return this;
	}
	virtual HistoryItem *getItem() const {
		return _parent;
	}

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

	void linkOver(const TextLinkPtr &lnk);
	void linkOut(const TextLinkPtr &lnk);

	~LayoutRadialProgressItem();

protected:
	TextLinkPtr _openl, _savel, _cancell;
	void setLinks(ITextLink *openl, ITextLink *savel, ITextLink *cancell);

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

class OverviewItemInfo {
public:
	OverviewItemInfo(int32 top) : _top(top) {
	}
	int32 top() const {
		return _top;
	}
	void setTop(int32 top) {
		_top = top;
	}
	
private:
	int32 _top;

};

class LayoutOverviewDate : public LayoutItem {
public:
	LayoutOverviewDate(const QDate &date, int32 top);
	
	virtual void initDimensions();
	virtual void paint(Painter &p, const QRect &clip, uint32 selection, uint64 ms) const;

	virtual OverviewItemInfo *getOverviewItemInfo() {
		return &_info;
	}
	virtual const OverviewItemInfo *getOverviewItemInfo() const {
		return &_info;
	}

private:
	OverviewItemInfo _info;

	QDate _date;
	QString _text;

};

class LayoutOverviewDocument : public LayoutAbstractFileItem {
public:
	LayoutOverviewDocument(DocumentData *document, HistoryItem *parent, int32 top);

	virtual void initDimensions();
	virtual void paint(Painter &p, const QRect &clip, uint32 selection, uint64 ms) const;
	virtual void getState(TextLinkPtr &link, HistoryCursorState &cursor, int32 x, int32 y) const;

	virtual DocumentData *getDocument() const {
		return _data;
	}
	virtual OverviewItemInfo *getOverviewItemInfo() {
		return &_info;
	}
	virtual const OverviewItemInfo *getOverviewItemInfo() const {
		return &_info;
	}

protected:
	virtual float64 dataProgress() const {
		return _data->progress();
	}
	virtual bool dataFinished() const {
		return !_data->loader;
	}
	virtual bool dataLoaded() const {
		return !_data->already().isEmpty() || !_data->data.isEmpty();
	}
	virtual bool iconAnimated() const {
		return !dataLoaded() || (_radial && _radial->animating());
	}

private:
	OverviewItemInfo _info;
	DocumentData *_data;
	TextLinkPtr _msgl;

	QString _name, _date, _ext;
	int32 _namew, _datew, _extw;
	int32 _thumbw, _colorIndex;

	bool withThumb() const {
		return !_data->thumb->isNull() && _data->thumb->width() && _data->thumb->height();
	}
	void updateStatusText() const;

};
