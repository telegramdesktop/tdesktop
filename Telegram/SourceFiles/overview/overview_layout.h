/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "layout.h"
#include "core/click_handler_types.h"
#include "ui/effects/animations.h"
#include "ui/effects/radial_animation.h"
#include "styles/style_overview.h"

class Image;

namespace style {
struct RoundCheckbox;
} // namespace style

namespace Data {
class Media;
class PhotoMedia;
class DocumentMedia;
} // namespace Data

namespace Overview {
namespace Layout {

class Checkbox;
class ItemBase;
class Delegate;

class PaintContext : public PaintContextBase {
public:
	PaintContext(crl::time ms, bool selecting) : PaintContextBase(ms, selecting) {
	}
	bool isAfterDate = false;

};

class ItemBase : public LayoutItemBase, public base::has_weak_ptr {
public:
	ItemBase(not_null<Delegate*> delegate, not_null<HistoryItem*> parent);
	~ItemBase();

	virtual void paint(
		Painter &p,
		const QRect &clip,
		TextSelection selection,
		const PaintContext *context) = 0;

	QDateTime dateTime() const;

	void setPosition(int position) {
		_position = position;
	}
	int position() const {
		return _position;
	}

	HistoryItem *getItem() const {
		return _parent;
	}

	void clickHandlerActiveChanged(const ClickHandlerPtr &action, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &action, bool pressed) override;

	void invalidateCache();

	virtual void clearHeavyPart() {
	}

protected:
	[[nodiscard]] not_null<HistoryItem*> parent() const {
		return _parent;
	}
	[[nodiscard]] not_null<Delegate*> delegate() const {
		return _delegate;
	}
	void paintCheckbox(
		Painter &p,
		QPoint position,
		bool selected,
		const PaintContext *context);
	virtual const style::RoundCheckbox &checkboxStyle() const;

private:
	void ensureCheckboxCreated();

	const not_null<Delegate*> _delegate;
	const not_null<HistoryItem*> _parent;
	const QDateTime _dateTime;
	std::unique_ptr<Checkbox> _check;
	int _position = 0;

};

class RadialProgressItem : public ItemBase {
public:
	RadialProgressItem(
		not_null<Delegate*> delegate,
		not_null<HistoryItem*> parent)
	: ItemBase(delegate, parent) {
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

	void radialAnimationCallback(crl::time now) const;

	void ensureRadial();
	void checkRadialFinished() const;

	bool isRadialAnimation() const {
		if (_radial) {
			if (_radial->animating()) {
				return true;
			}
			checkRadialFinished();
		}
		return false;
	}

	virtual float64 dataProgress() const = 0;
	virtual bool dataFinished() const = 0;
	virtual bool dataLoaded() const = 0;
	virtual bool iconAnimated() const {
		return false;
	}

	mutable std::unique_ptr<Ui::RadialAnimation> _radial;
	Ui::Animations::Simple _a_iconOver;

};

class StatusText {
public:
	// duration = -1 - no duration, duration = -2 - "GIF" duration
	void update(int newSize, int fullSize, int duration, crl::time realDuration);
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

struct Info : public RuntimeComponent<Info, LayoutItemBase> {
	int top = 0;
};

class Photo final : public ItemBase {
public:
	Photo(
		not_null<Delegate*> delegate,
		not_null<HistoryItem*> parent,
		not_null<PhotoData*> photo);

	void initDimensions() override;
	int32 resizeGetHeight(int32 width) override;
	void paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) override;
	TextState getState(
		QPoint point,
		StateRequest request) const override;

	void clearHeavyPart() override;

private:
	void ensureDataMediaCreated() const;
	void setPixFrom(not_null<Image*> image);

	const not_null<PhotoData*> _data;
	mutable std::shared_ptr<Data::PhotoMedia> _dataMedia;
	ClickHandlerPtr _link;

	QPixmap _pix;
	bool _goodLoaded = false;

};

class Video final : public RadialProgressItem {
public:
	Video(
		not_null<Delegate*> delegate,
		not_null<HistoryItem*> parent,
		not_null<DocumentData*> video);
	~Video();

	void initDimensions() override;
	int32 resizeGetHeight(int32 width) override;
	void paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) override;
	TextState getState(
		QPoint point,
		StateRequest request) const override;

	void clearHeavyPart() override;

protected:
	float64 dataProgress() const override;
	bool dataFinished() const override;
	bool dataLoaded() const override;
	bool iconAnimated() const override;

private:
	void ensureDataMediaCreated() const;
	void updateStatusText();

	const not_null<DocumentData*> _data;
	mutable std::shared_ptr<Data::DocumentMedia> _dataMedia;
	StatusText _status;

	QString _duration;
	QPixmap _pix;
	bool _pixBlurred = true;

};

class Voice final : public RadialProgressItem {
public:
	Voice(
		not_null<Delegate*> delegate,
		not_null<HistoryItem*> parent,
		not_null<DocumentData*> voice,
		const style::OverviewFileLayout &st);

	void initDimensions() override;
	void paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) override;
	TextState getState(
		QPoint point,
		StateRequest request) const override;

	void clearHeavyPart() override;

protected:
	float64 dataProgress() const override;
	bool dataFinished() const override;
	bool dataLoaded() const override;
	bool iconAnimated() const override;
	const style::RoundCheckbox &checkboxStyle() const override;

private:
	void ensureDataMediaCreated() const;
	int duration() const;

	not_null<DocumentData*> _data;
	mutable std::shared_ptr<Data::DocumentMedia> _dataMedia;
	StatusText _status;
	ClickHandlerPtr _namel;

	const style::OverviewFileLayout &_st;

	Ui::Text::String _name, _details;
	int _nameVersion;

	void updateName();
	bool updateStatusText();

};

class Document final : public RadialProgressItem {
public:
	Document(
		not_null<Delegate*> delegate,
		not_null<HistoryItem*> parent,
		not_null<DocumentData*> document,
		const style::OverviewFileLayout &st);

	void initDimensions() override;
	void paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) override;
	TextState getState(
		QPoint point,
		StateRequest request) const override;

	void clearHeavyPart() override;

protected:
	float64 dataProgress() const override;
	bool dataFinished() const override;
	bool dataLoaded() const override;
	bool iconAnimated() const override;
	const style::RoundCheckbox &checkboxStyle() const override;

private:
	[[nodiscard]] bool downloadInCorner() const;
	void drawCornerDownload(Painter &p, bool selected, const PaintContext *context) const;
	[[nodiscard]] TextState cornerDownloadTextState(
		QPoint point,
		StateRequest request) const;

	void ensureDataMediaCreated() const;

	not_null<DocumentData*> _data;
	mutable std::shared_ptr<Data::DocumentMedia> _dataMedia;
	StatusText _status;
	ClickHandlerPtr _msgl, _namel;

	const style::OverviewFileLayout &_st;

	bool _thumbLoaded = false;
	QPixmap _thumb;

	Ui::Text::String _name;
	QString _date, _ext;
	int32 _datew, _extw;
	int32 _thumbw, _colorIndex;

	bool withThumb() const;
	bool updateStatusText();

};

class Link final : public ItemBase {
public:
	Link(
		not_null<Delegate*> delegate,
		not_null<HistoryItem*> parent,
		Data::Media *media);

	void initDimensions() override;
	int32 resizeGetHeight(int32 width) override;
	void paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) override;
	TextState getState(
		QPoint point,
		StateRequest request) const override;

	void clearHeavyPart() override;

protected:
	const style::RoundCheckbox &checkboxStyle() const override;

private:
	void ensurePhotoMediaCreated();
	void ensureDocumentMediaCreated();
	void validateThumbnail();

	ClickHandlerPtr _photol;

	QString _title, _letter;
	int _titlew = 0;
	WebPageData *_page = nullptr;
	std::shared_ptr<Data::PhotoMedia> _photoMedia;
	std::shared_ptr<Data::DocumentMedia> _documentMedia;
	int _pixw = 0;
	int _pixh = 0;
	Ui::Text::String _text = { st::msgMinWidth };
	QPixmap _thumbnail;
	bool _thumbnailBlurred = true;

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
