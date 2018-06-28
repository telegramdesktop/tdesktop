/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "layout.h"
#include "ui/text/text.h"

namespace InlineBots {
class Result;

namespace Layout {

class PaintContext : public PaintContextBase {
public:
	PaintContext(TimeMs ms, bool selecting, bool paused, bool lastRow)
		: PaintContextBase(ms, selecting)
		, paused(paused)
		, lastRow(lastRow) {
	}
	bool paused, lastRow;

};

// this type used as a flag, we dynamic_cast<> to it
class SendClickHandler : public ClickHandler {
public:
	void onClick(Qt::MouseButton) const override {
	}
};

class Context {
public:
	virtual void inlineItemLayoutChanged(const ItemBase *layout) = 0;
	virtual bool inlineItemVisible(const ItemBase *item) = 0;
	virtual void inlineItemRepaint(const ItemBase *item) = 0;
};

class ItemBase : public LayoutItemBase {
public:
	ItemBase(not_null<Context*> context, not_null<Result*> result)
	: _result(result)
	, _context(context) {
	}
	ItemBase(not_null<Context*> context, DocumentData *doc)
	: _doc(doc)
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

	void update();
	void layoutChanged();

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override {
		update();
	}
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override {
		update();
	}

	static std::unique_ptr<ItemBase> createLayout(not_null<Context*> context, Result *result, bool forceThumb);
	static std::unique_ptr<ItemBase> createLayoutGif(not_null<Context*> context, DocumentData *document);

protected:
	DocumentData *getResultDocument() const;
	PhotoData *getResultPhoto() const;
	ImagePtr getResultThumb() const;
	QPixmap getResultContactAvatar(int width, int height) const;
	int getResultDuration() const;
	QString getResultUrl() const;
	ClickHandlerPtr getResultUrlHandler() const;
	ClickHandlerPtr getResultContentUrlHandler() const;
	QString getResultThumbLetter() const;

	not_null<Context*> context() const {
		return _context;
	}

	Result *_result = nullptr;
	DocumentData *_doc = nullptr;
	PhotoData *_photo = nullptr;

	ClickHandlerPtr _send = ClickHandlerPtr{ new SendClickHandler() };

	int _position = 0; // < 0 means removed from layout

private:
	not_null<Context*> _context;

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
