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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "boxes/abstract_box.h"
#include <vector>

class ConfirmBox;

namespace Ui {
class PlainShadow;
} // namespace Ui

class StickerSetBox : public BoxContent, public RPCSender {
	Q_OBJECT

public:
	StickerSetBox(QWidget*, const MTPInputStickerSet &set);

signals:
	void installed(uint64 id);

protected:
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;

private slots:
	void onAddStickers();
	void onShareStickers();
	void onUpdateButtons();

	void onInstalled(uint64 id);

private:
	void updateButtons();

	MTPInputStickerSet _set;

	class Inner;
	QPointer<Inner> _inner;

};

// This class is hold in header because it requires Qt preprocessing.
class StickerSetBox::Inner : public TWidget, public RPCSender, private base::Subscriber {
	Q_OBJECT

public:
	Inner(QWidget *parent, const MTPInputStickerSet &set);

	bool loaded() const;
	int32 notInstalled() const;
	bool official() const;
	base::lambda<TextWithEntities()> title() const;
	QString shortName() const;

	void setVisibleTopBottom(int visibleTop, int visibleBottom) override;
	void install();

	~Inner();

protected:
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private slots:
	void onPreview();

signals:
	void updateButtons();
	void installed(uint64 id);

private:
	void updateSelected();
	void setSelected(int selected);
	void startOverAnimation(int index, float64 from, float64 to);
	int stickerFromGlobalPos(const QPoint &p) const;

	void gotSet(const MTPmessages_StickerSet &set);
	bool failedSet(const RPCError &error);

	void installDone(const MTPmessages_StickerSetInstallResult &result);
	bool installFail(const RPCError &error);

	bool isMasksSet() const {
		return (_setFlags & MTPDstickerSet::Flag::f_masks);
	}

	std::vector<Animation> _packOvers;
	StickerPack _pack;
	StickersByEmojiMap _emoji;
	bool _loaded = false;
	uint64 _setId = 0;
	uint64 _setAccess = 0;
	QString _setTitle, _setShortName;
	int32 _setCount = 0;
	int32 _setHash = 0;
	MTPDstickerSet::Flags _setFlags = 0;

	int _visibleTop = 0;
	int _visibleBottom = 0;
	MTPInputStickerSet _input;

	mtpRequestId _installRequest = 0;

	int _selected = -1;

	QTimer _previewTimer;
	int _previewShown = -1;

};
