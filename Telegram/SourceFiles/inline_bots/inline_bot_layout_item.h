/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "layout.h"
#include "ui/text/text.h"

class Image;

namespace Ui {
class PathShiftGradient;
} // namespace Ui

namespace Data {
class CloudImageView;
} // namespace Data

namespace InlineBots {

class Result;

namespace Layout {

class ItemBase;

class PaintContext : public PaintContextBase {
public:
	PaintContext(crl::time ms, bool selecting, bool paused, bool lastRow)
	: PaintContextBase(ms, selecting)
	, paused(paused)
	, lastRow(lastRow) {
	}
	bool paused, lastRow;
	Ui::PathShiftGradient *pathGradient = nullptr;

};

// this type used as a flag, we dynamic_cast<> to it
class SendClickHandler : public ClickHandler {
public:
	void onClick(ClickContext context) const override {
	}
};

class OpenFileClickHandler : public ClickHandler {
public:
	void onClick(ClickContext context) const override {
	}
};

class Context {
public:
	virtual void inlineItemLayoutChanged(const ItemBase *layout) = 0;
	virtual bool inlineItemVisible(const ItemBase *item) = 0;
	virtual void inlineItemRepaint(const ItemBase *item) = 0;
	virtual Data::FileOrigin inlineItemFileOrigin() = 0;
};

class ItemBase : public LayoutItemBase {
public:
	ItemBase(not_null<Context*> context, not_null<Result*> result)
	: _result(result)
	, _context(context) {
	}
	ItemBase(not_null<Context*> context, not_null<DocumentData*> document)
	: _document(document)
	, _context(context) {
	}
	// Not used anywhere currently.
	//ItemBase(not_null<Context*> context, PhotoData *photo) : _photo(photo), _context(context) {
	//}

	virtual void paint(Painter &p, const QRect &clip, const PaintContext *context) const = 0;

	virtual void setPosition(int32 position);
	int32 position() const;

	virtual bool isFullLine() const {
		return true;
	}
	virtual bool hasRightSkip() const {
		return false;
	}

	Result *getResult() const;
	DocumentData *getDocument() const;
	PhotoData *getPhoto() const;

	// Get document or photo (possibly from InlineBots::Result) for
	// showing sticker / GIF / photo preview by long mouse press.
	DocumentData *getPreviewDocument() const;
	PhotoData *getPreviewPhoto() const;

	virtual void preload() const;
	virtual void unloadHeavyPart() {
		_thumbnail = nullptr;
	}

	void update() const;
	void layoutChanged();

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override {
		update();
	}
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override {
		update();
	}

	static std::unique_ptr<ItemBase> createLayout(
		not_null<Context*> context,
		not_null<Result*> result,
		bool forceThumb);
	static std::unique_ptr<ItemBase> createLayoutGif(
		not_null<Context*> context,
		not_null<DocumentData*> document);

protected:
	DocumentData *getResultDocument() const;
	PhotoData *getResultPhoto() const;
	bool hasResultThumb() const;
	Image *getResultThumb(Data::FileOrigin origin) const;
	QPixmap getResultContactAvatar(int width, int height) const;
	int getResultDuration() const;
	QString getResultUrl() const;
	ClickHandlerPtr getResultUrlHandler() const;
	ClickHandlerPtr getResultPreviewHandler() const;
	QString getResultThumbLetter() const;

	not_null<Context*> context() const {
		return _context;
	}
	Data::FileOrigin fileOrigin() const;

	Result *_result = nullptr;
	DocumentData *_document = nullptr;
	PhotoData *_photo = nullptr;

	ClickHandlerPtr _send = ClickHandlerPtr{ new SendClickHandler() };
	ClickHandlerPtr _open = ClickHandlerPtr{ new OpenFileClickHandler() };

	int _position = 0; // < 0 means removed from layout

private:
	not_null<Context*> _context;
	mutable std::shared_ptr<Data::CloudImageView> _thumbnail;

};

using DocumentItems = std::map<
	not_null<const DocumentData*>,
	base::flat_set<not_null<ItemBase*>>>;
const DocumentItems *documentItems();

namespace internal {

void regDocumentItem(
	not_null<const DocumentData*> document,
	not_null<ItemBase*> item);
void unregDocumentItem(
	not_null<const DocumentData*> document,
	not_null<ItemBase*> item);

} // namespace internal
} // namespace Layout
} // namespace InlineBots
