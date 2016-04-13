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

static constexpr TextSelection FullSelection = { 0xFFFF, 0xFFFF };

extern TextParseOptions _textNameOptions, _textDlgOptions;
extern TextParseOptions _historyTextOptions, _historyBotOptions, _historyTextNoMonoOptions, _historyBotNoMonoOptions;

const TextParseOptions &itemTextOptions(History *h, PeerData *f);
const TextParseOptions &itemTextNoMonoOptions(History *h, PeerData *f);

enum RoundCorners {
	NoneCorners = 0x00, // for images
	BlackCorners,
	WhiteCorners,
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

class PaintContextBase {
public:
	PaintContextBase(uint64 ms, bool selecting) : ms(ms), selecting(selecting) {
	}
	uint64 ms;
	bool selecting;

};

class LayoutItemBase : public Composer, public ClickHandlerHost {
public:
	LayoutItemBase() {
	}

	LayoutItemBase(const LayoutItemBase &other) = delete;
	LayoutItemBase &operator=(const LayoutItemBase &other) = delete;

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

	virtual void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int x, int y) const {
		link.clear();
		cursor = HistoryDefaultCursorState;
	}
	virtual void getSymbol(uint16 &symbol, bool &after, bool &upon, int x, int y) const { // from text
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

	bool hasPoint(int x, int y) const {
		return (x >= 0 && y >= 0 && x < width() && y < height());
	}

	virtual ~LayoutItemBase() {
	}

protected:
	int _width = 0;
	int _height = 0;
	int _maxw = 0;
	int _minh = 0;

};
