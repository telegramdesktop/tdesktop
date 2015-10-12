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

#include "abstractbox.h"

class StickerSetInner : public TWidget, public RPCSender {
	Q_OBJECT

public:

	StickerSetInner(const MTPInputStickerSet &set);

	void init();

	void paintEvent(QPaintEvent *e);
	
	bool loaded() const;
	int32 notInstalled() const;
	bool official() const;
	QString title() const;
	QString shortName() const;

	void setScrollBottom(int32 bottom);
	void install();

	QString getTitle() const;

	~StickerSetInner();

signals:

	void updateButtons();
	void installed(uint64 id);

private:

	void gotSet(const MTPmessages_StickerSet &set);
	bool failedSet(const RPCError &error);

	void installDone(const MTPBool &result);
	bool installFailed(const RPCError &error);

	StickerPack _pack;
	bool _loaded;
	uint64 _setId, _setAccess;
	QString _title, _setTitle, _setShortName;
	int32 _setCount, _setHash, _setFlags;

	int32 _bottom;
	MTPInputStickerSet _input;

	mtpRequestId _installRequest;
};

class StickerSetBox : public ScrollableBox, public RPCSender {
	Q_OBJECT

public:

	StickerSetBox(const MTPInputStickerSet &set);

	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);

public slots:

	void onStickersUpdated();
	void onAddStickers();
	void onShareStickers();
	void onUpdateButtons();

	void onScroll();

signals:

	void installed(uint64 id);

protected:

	void hideAll();
	void showAll();

private:

	StickerSetInner _inner;
	ScrollableBoxShadow _shadow;
	BoxButton _add, _share, _cancel, _done;
	QString _title;
};
