/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "layout.h"
#include "core/click_handler_types.h"
#include "ui/effects/radial_animation.h"
#include "styles/style_overview.h"

namespace style {
struct RoundCheckbox;
} // namespace style

namespace Overview {
namespace Layout {

class Checkbox;

class PaintContext : public PaintContextBase {
public:
	PaintContext(TimeMs ms, bool selecting) : PaintContextBase(ms, selecting), isAfterDate(false) {
	}
	bool isAfterDate;

};

class ItemBase;
class AbstractItem : public LayoutItemBase {
public:
	virtual void paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) = 0;

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
		auto item = getItem();
		return item ? item->id : 0;
	}

	virtual void invalidateCache() {
	}

};

class ItemBase : public AbstractItem {
public:
	ItemBase(not_null<HistoryItem*> parent);

	void setPosition(int position) {
		_position = position;
	}
	int position() const {
		return _position;
	}

	ItemBase *toMediaItem() final override {
		return this;
	}
	const ItemBase *toMediaItem() const final override {
		return this;
	}
	HistoryItem *getItem() const final override {
		return _parent;
	}

	void clickHandlerActiveChanged(const ClickHandlerPtr &action, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &action, bool pressed) override;

	void invalidateCache() override;

	~ItemBase();

protected:
	not_null<HistoryItem*> parent() const {
		return _parent;
	}
	void paintCheckbox(
		Painter &p,
		QPoint position,
		bool selected,
		const PaintContext *context);
	virtual const style::RoundCheckbox &checkboxStyle() const;

private:
	void ensureCheckboxCreated();

	int _position = 0;
	not_null<HistoryItem*> _parent;
	std::unique_ptr<Checkbox> _check;

};

class RadialProgressItem : public ItemBase {
public:
	RadialProgressItem(not_null<HistoryItem*> parent) : ItemBase(parent) {
	}
	RadialProgressItem(const RadialProgressItem &other) = delete;

	void clickHandlerActiveChanged(const ClickHandlerPtr &action, bool active) override;

	~RadialProgressItem();

protected:
	ClickHandlerPtr _openl, _savel, _cancell;
	void setLinks(
		ClickHandlerPtr &&openl,
		ClickHandlerPtr &&savel,
		ClickHandlerPtr &&cancell);
	void setDocumentLinks(not_null<DocumentData*> document);

	void step_radial(TimeMs ms, bool timer);

	void ensureRadial();
	void checkRadialFinished();

	bool isRadialAnimation(TimeMs ms) const {
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

	std::unique_ptr<Ui::RadialAnimation> _radial;
	Animation _a_iconOver;

};

class StatusText {
public:
	// duration = -1 - no duration, duration = -2 - "GIF" duration
	void update(int newSize, int fullSize, int duration, TimeMs realDuration);
	void setSize(int newSize);

	int size() const {
		return _size;
	}
	QString text() const {
		return _text;
	}

private:
	// >= 0 will contain download / upload string, _size = loaded bytes
	// < 0 will contain played string, _size = -(seconds + 1) played
	// 0x7FFFFFF0 will contain status for not yet downloaded file
	// 0x7FFFFFF1 will contain status for already downloaded file
	// 0x7FFFFFF2 will contain status for failed to download / upload file
	int _size = 0;
	QString _text;

};

struct Info : public RuntimeComponent<Info> {
	int top = 0;
};

class Date : public AbstractItem {
public:
	Date(const QDate &date, bool month);

	void initDimensions() override;
	void paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) override;

private:
	QDate _date;
	QString _text;

};

class Photo : public ItemBase {
public:
	Photo(
		not_null<HistoryItem*> parent,
		not_null<PhotoData*> photo);

	void initDimensions() override;
	int32 resizeGetHeight(int32 width) override;
	void paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) override;
	HistoryTextState getState(
		QPoint point,
		HistoryStateRequest request) const override;

private:
	not_null<PhotoData*> _data;
	ClickHandlerPtr _link;

	QPixmap _pix;
	bool _goodLoaded = false;

};

class Video : public RadialProgressItem {
public:
	Video(
		not_null<HistoryItem*> parent,
		not_null<DocumentData*> video);

	void initDimensions() override;
	int32 resizeGetHeight(int32 width) override;
	void paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) override;
	HistoryTextState getState(
		QPoint point,
		HistoryStateRequest request) const override;

protected:
	float64 dataProgress() const override;
	bool dataFinished() const override;
	bool dataLoaded() const override;
	bool iconAnimated() const override;

private:
	not_null<DocumentData*> _data;
	StatusText _status;

	QString _duration;
	QPixmap _pix;
	bool _thumbLoaded = false;

	void updateStatusText();

};

class Voice : public RadialProgressItem {
public:
	Voice(
		not_null<HistoryItem*> parent,
		not_null<DocumentData*> voice,
		const style::OverviewFileLayout &st);

	void initDimensions() override;
	void paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) override;
	HistoryTextState getState(
		QPoint point,
		HistoryStateRequest request) const override;

protected:
	float64 dataProgress() const override;
	bool dataFinished() const override;
	bool dataLoaded() const override;
	bool iconAnimated() const override;
	const style::RoundCheckbox &checkboxStyle() const override;

private:
	not_null<DocumentData*> _data;
	StatusText _status;
	ClickHandlerPtr _namel;

	const style::OverviewFileLayout &_st;

	Text _name, _details;
	int _nameVersion;

	void updateName();
	bool updateStatusText();

};

class Document : public RadialProgressItem {
public:
	Document(
		not_null<HistoryItem*> parent,
		not_null<DocumentData*> document,
		const style::OverviewFileLayout &st);

	void initDimensions() override;
	void paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) override;
	HistoryTextState getState(
		QPoint point,
		HistoryStateRequest request) const override;

	virtual DocumentData *getDocument() const override {
		return _data;
	}

protected:
	float64 dataProgress() const override;
	bool dataFinished() const override;
	bool dataLoaded() const override;
	bool iconAnimated() const override;
	const style::RoundCheckbox &checkboxStyle() const override;

private:
	not_null<DocumentData*> _data;
	StatusText _status;
	ClickHandlerPtr _msgl, _namel;

	const style::OverviewFileLayout &_st;

	bool _thumbForLoaded = false;
	QPixmap _thumb;

	Text _name;
	QString _date, _ext;
	int32 _datew, _extw;
	int32 _thumbw, _colorIndex;

	bool withThumb() const;
	bool updateStatusText();

};

class Link : public ItemBase {
public:
	Link(
		not_null<HistoryItem*> parent,
		HistoryMedia *media);

	void initDimensions() override;
	int32 resizeGetHeight(int32 width) override;
	void paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) override;
	HistoryTextState getState(
		QPoint point,
		HistoryStateRequest request) const override;

protected:
	const style::RoundCheckbox &checkboxStyle() const override;

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
		std::shared_ptr<TextClickHandler> lnk;
	};
	QVector<LinkEntry> _links;

};

} // namespace Layout
} // namespace Overview
