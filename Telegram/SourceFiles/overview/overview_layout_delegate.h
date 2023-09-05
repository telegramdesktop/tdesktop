/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace Overview {
namespace Layout {

class ItemBase;

class Delegate {
public:
	virtual void registerHeavyItem(not_null<const ItemBase*> item) = 0;
	virtual void unregisterHeavyItem(not_null<const ItemBase*> item) = 0;
	virtual void repaintItem(not_null<const ItemBase*> item) = 0;
	virtual bool itemVisible(not_null<const ItemBase*> item) = 0;

	virtual void openPhoto(not_null<PhotoData*> photo, FullMsgId id) = 0;
	virtual void openDocument(
		not_null<DocumentData*> document,
		FullMsgId id,
		bool showInMediaView = false) = 0;

};

} // namespace Layout
} // namespace Overview
