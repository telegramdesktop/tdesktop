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

#include "layout.h"
#include "core/click_handler_types.h"

namespace Overview {
namespace Layout {

class PaintContext : public PaintContextBase {
public:
	PaintContext(uint64 ms, bool selecting) : PaintContextBase(ms, selecting), isAfterDate(false) {
	}
	bool isAfterDate;

};

class ItemBase;
class AbstractItem : public LayoutItemBase {
public:

	virtual void paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) const = 0;

	virtual ItemBase *toMediaItem() {
		return nullptr;
	}
	virtual const ItemBase *toMediaItem() const {
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

};

class ItemBase : public AbstractItem {
public:
	ItemBase(HistoryItem *parent) : _parent(parent) {
	}

	ItemBase *toMediaItem() override {
		return this;
	}
	const ItemBase *toMediaItem() const override {
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

class RadialProgressItem : public ItemBase {
public:
	RadialProgressItem(HistoryItem *parent) : ItemBase(parent)
		, _radial(0)
		, a_iconOver(0, 0)
		, _a_iconOver(animation(this, &RadialProgressItem::step_iconOver)) {
	}
	RadialProgressItem(const RadialProgressItem &other) = delete;

	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool active) override;

	~RadialProgressItem();

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

};

class FileBase : public RadialProgressItem {
public:
	FileBase(HistoryItem *parent) : RadialProgressItem(parent) {
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

struct Info : public BaseComponent<Info> {
	int top = 0;
};

class Date : public AbstractItem {
public:
	Date(const QDate &date, bool month);

	void initDimensions() override;
	void paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) const override;

private:
	QDate _date;
	QString _text;

};

class Photo : public ItemBase {
public:
	Photo(PhotoData *photo, HistoryItem *parent);

	void initDimensions() override;
	int32 resizeGetHeight(int32 width) override;
	void paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int x, int y) const override;

private:
	PhotoData *_data;
	ClickHandlerPtr _link;

	mutable QPixmap _pix;
	mutable bool _goodLoaded;

};

class Video : public FileBase {
public:
	Video(DocumentData *video, HistoryItem *parent);

	void initDimensions() override;
	int32 resizeGetHeight(int32 width) override;
	void paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int x, int y) const override;

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

class Voice : public FileBase {
public:
	Voice(DocumentData *voice, HistoryItem *parent);

	void initDimensions() override;
	void paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int x, int y) const override;

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

class Document : public FileBase {
public:
	Document(DocumentData *document, HistoryItem *parent);

	void initDimensions() override;
	void paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int x, int y) const override;

	virtual DocumentData *getDocument() const override {
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

class Link : public ItemBase {
public:
	Link(HistoryMedia *media, HistoryItem *parent);

	void initDimensions() override;
	int32 resizeGetHeight(int32 width) override;
	void paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int x, int y) const override;

private:
	ClickHandlerPtr _photol;

	QString _title, _letter;
	int _titlew = 0;
	WebPageData *_page = nullptr;
	int _pixw = 0;
	int _pixh = 0;
	Text _text = { int(st::msgMinWidth) };

	struct LinkEntry {
		LinkEntry() : width(0) {
		}
		LinkEntry(const QString &url, const QString &text);
		QString text;
		int32 width;
		TextClickHandlerPtr lnk;
	};
	QVector<LinkEntry> _links;

};

} // namespace Layout
} // namespace Overview
