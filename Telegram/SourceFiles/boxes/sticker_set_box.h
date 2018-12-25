/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "base/timer.h"
#include "chat_helpers/stickers.h"

class ConfirmBox;

namespace Ui {
class PlainShadow;
} // namespace Ui

class StickerSetBox : public BoxContent, public RPCSender {
public:
	StickerSetBox(QWidget*, const MTPInputStickerSet &set);

	static void Show(DocumentData *document);

protected:
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;

private:
	void updateTitleAndButtons();
	void updateButtons();
	void addStickers();
	void shareStickers();

	MTPInputStickerSet _set;

	class Inner;
	QPointer<Inner> _inner;

};
