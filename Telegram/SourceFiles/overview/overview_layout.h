/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "layout/layout_item_base.h"
#include "layout/layout_document_generic_preview.h"
#include "media/clip/media_clip_reader.h"
#include "core/click_handler_types.h"
#include "ui/effects/animations.h"
#include "ui/effects/radial_animation.h"

class Image;

namespace style {
struct RoundCheckbox;
struct OverviewFileLayout;
} // namespace style

namespace Data {
class Media;
class PhotoMedia;
class DocumentMedia;
} // namespace Data

namespace Ui {
class SpoilerAnimation;
} // namespace Ui

namespace Overview::Layout {

class Checkbox;
class ItemBase;
class Delegate;

class PaintContext : public PaintContextBase {
public:
	PaintContext(crl::time ms, bool selecting, bool paused)
	: PaintContextBase(ms, selecting)
	, paused(paused) {
	}
	bool skipBorder = false;
	bool paused = false;

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

	[[nodiscard]] QDateTime dateTime() const;

	[[nodiscard]] not_null<HistoryItem*> getItem() const {
		return _parent;
	}

	void clickHandlerActiveChanged(const ClickHandlerPtr &action, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &action, bool pressed) override;

	void invalidateCache();

	virtual void itemDataChanged() {
	}
	virtual void clearHeavyPart() {
	}

	virtual void maybeClearSensitiveSpoiler() {
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
	[[nodiscard]] virtual const style::RoundCheckbox &checkboxStyle() const;

private:
	void ensureCheckboxCreated();

	const not_null<Delegate*> _delegate;
	const not_null<HistoryItem*> _parent;
	const QDateTime _dateTime;
	std::unique_ptr<Checkbox> _check;

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

	virtual void clearSpoiler() {
	}

	~RadialProgressItem();

protected:
	ClickHandlerPtr _openl, _savel, _cancell;
	void setLinks(
		ClickHandlerPtr &&openl,
		ClickHandlerPtr &&savel,
		ClickHandlerPtr &&cancell);
	void setDocumentLinks(
		not_null<DocumentData*> document,
		bool forceOpen = false);

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
	void update(
		int64 newSize,
		int64 fullSize,
		TimeId duration,
		TimeId realDuration);
	void setSize(int64 newSize);

	[[nodiscard]] int64 size() const {
		return _size;
	}
	[[nodiscard]] QString text() const {
		return _text;
	}

private:
	// >= 0 will contain download / upload string, _size = loaded bytes
	// < 0 will contain played string, _size = -(seconds + 1) played
	// 0xFFFFFFF0LL will contain status for not yet downloaded file
	// 0xFFFFFFF1LL will contain status for already downloaded file
	// 0xFFFFFFF2LL will contain status for failed to download / upload file
	int64 _size = 0;
	QString _text;

};

struct Info : RuntimeComponent<Info, LayoutItemBase> {
	int top = 0;
};

struct MediaOptions {
	bool spoiler = false;
	bool story = false;
	bool storyPinned = false;
	bool storyShowPinned = false;
	bool storyHidden = false;
	bool storyShowHidden = false;
};

class Photo final : public ItemBase {
public:
	Photo(
		not_null<Delegate*> delegate,
		not_null<HistoryItem*> parent,
		not_null<PhotoData*> photo,
		MediaOptions options);
	~Photo();

	void initDimensions() override;
	int32 resizeGetHeight(int32 width) override;
	void paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) override;
	TextState getState(
		QPoint point,
		StateRequest request) const override;

	void itemDataChanged() override;
	void clearHeavyPart() override;

	void maybeClearSensitiveSpoiler() override;

private:
	void ensureDataMediaCreated() const;
	void setPixFrom(not_null<Image*> image);
	[[nodiscard]] ClickHandlerPtr makeOpenPhotoHandler();
	void clearSpoiler();

	const not_null<PhotoData*> _data;
	mutable std::shared_ptr<Data::PhotoMedia> _dataMedia;
	std::unique_ptr<Ui::SpoilerAnimation> _spoiler;

	QImage _pix;
	QImage _hiddenBgCache;
	bool _goodLoaded : 1 = false;
	bool _sensitiveSpoiler : 1 = false;
	bool _story : 1 = false;
	bool _storyPinned : 1 = false;
	bool _storyShowPinned : 1 = false;
	bool _storyHidden : 1 = false;
	bool _storyShowHidden : 1 = false;

	ClickHandlerPtr _link;

};

class Gif final : public RadialProgressItem {
public:
	Gif(
		not_null<Delegate*> delegate,
		not_null<HistoryItem*> parent,
		not_null<DocumentData*> gif);
	~Gif();

	void initDimensions() override;
	int32 resizeGetHeight(int32 width) override;
	void paint(
		Painter &p,
		const QRect &clip,
		TextSelection selection,
		const PaintContext *context) override;
	TextState getState(
		QPoint point,
		StateRequest request) const override;

	void clearHeavyPart() override;
	void setPosition(int32 position) override;

	void clearSpoiler() override;
	void maybeClearSensitiveSpoiler() override;

protected:
	float64 dataProgress() const override;
	bool dataFinished() const override;
	bool dataLoaded() const override;
	bool iconAnimated() const override;

private:
	QSize countFrameSize() const;
	int contentWidth() const;
	int contentHeight() const;

	void validateThumbnail(
		Image *image,
		QSize size,
		QSize frame,
		bool good);
	void prepareThumbnail(QSize size, QSize frame);

	void update();

	void ensureDataMediaCreated() const;
	void updateStatusText();

	void clipCallback(Media::Clip::Notification notification);

	Media::Clip::ReaderPointer _gif;

	const not_null<DocumentData*> _data;
	mutable std::shared_ptr<Data::DocumentMedia> _dataMedia;
	StatusText _status;
	std::unique_ptr<Ui::SpoilerAnimation> _spoiler;

	QImage _thumb;
	bool _thumbGood = false;
	bool _sensitiveSpoiler = false;

};

class Video final : public RadialProgressItem {
public:
	Video(
		not_null<Delegate*> delegate,
		not_null<HistoryItem*> parent,
		not_null<DocumentData*> video,
		MediaOptions options);
	~Video();

	void initDimensions() override;
	int32 resizeGetHeight(int32 width) override;
	void paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) override;
	TextState getState(
		QPoint point,
		StateRequest request) const override;

	void itemDataChanged() override;
	void clearHeavyPart() override;
	void clearSpoiler() override;

	void maybeClearSensitiveSpoiler() override;

protected:
	float64 dataProgress() const override;
	bool dataFinished() const override;
	bool dataLoaded() const override;
	bool iconAnimated() const override;

private:
	void ensureDataMediaCreated() const;
	void updateStatusText();

	const not_null<DocumentData*> _data;
	PhotoData *_videoCover = nullptr;
	mutable std::shared_ptr<Data::DocumentMedia> _dataMedia;
	mutable std::shared_ptr<Data::PhotoMedia> _videoCoverMedia;
	StatusText _status;

	QString _duration;
	std::unique_ptr<Ui::SpoilerAnimation> _spoiler;

	QImage _pix;
	QImage _hiddenBgCache;
	bool _pixBlurred : 1 = true;
	bool _sensitiveSpoiler : 1 = false;
	bool _story : 1 = false;
	bool _storyPinned : 1 = false;
	bool _storyShowPinned : 1 = false;
	bool _storyHidden : 1 = false;
	bool _storyShowHidden : 1 = false;

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

	const not_null<DocumentData*> _data;
	mutable std::shared_ptr<Data::DocumentMedia> _dataMedia;
	StatusText _status;
	ClickHandlerPtr _namel;

	const style::OverviewFileLayout &_st;

	Ui::Text::String _name;
	Ui::Text::String _details;
	Ui::Text::String _caption;
	int _nameVersion = 0;

	void updateName();
	bool updateStatusText();

};

struct DocumentFields {
	not_null<DocumentData*> document;
	TimeId dateOverride = 0;
	bool forceFileLayout = false;
};

class Document final : public RadialProgressItem {
public:
	Document(
		not_null<Delegate*> delegate,
		not_null<HistoryItem*> parent,
		DocumentFields fields,
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
	void drawCornerDownload(QPainter &p, bool selected, const PaintContext *context) const;
	[[nodiscard]] TextState cornerDownloadTextState(
		QPoint point,
		StateRequest request) const;

	[[nodiscard]] bool songLayout() const;
	void ensureDataMediaCreated() const;

	not_null<DocumentData*> _data;
	mutable std::shared_ptr<Data::DocumentMedia> _dataMedia;
	StatusText _status;
	ClickHandlerPtr _msgl, _namel;

	const style::OverviewFileLayout &_st;
	const ::Layout::DocumentGenericPreview _generic;

	bool _thumbLoaded = false;
	bool _forceFileLayout = false;
	QPixmap _thumb;

	Ui::Text::String _name;
	QString _date, _ext;
	int _datew = 0;
	int _extw = 0;
	int _thumbw = 0;

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
	Ui::Text::String _text;
	QPixmap _thumbnail;
	bool _thumbnailBlurred = true;

	struct LinkEntry {
		LinkEntry() = default;
		LinkEntry(const QString &url, const QString &text);

		QString text;
		int width = 0;
		std::shared_ptr<TextClickHandler> lnk;
	};
	QVector<LinkEntry> _links;

};

} // namespace Overview::Layout
