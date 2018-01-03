/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "chat_helpers/stickers.h"

class ConfirmBox;

namespace Ui {
class PlainShadow;
} // namespace Ui

class StickerSetBox : public BoxContent, public RPCSender {
	Q_OBJECT

public:
	StickerSetBox(QWidget*, const MTPInputStickerSet &set);

protected:
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;

private slots:
	void onAddStickers();
	void onShareStickers();
	void onUpdateButtons();

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

	void install();
	auto setInstalled() const {
		return _setInstalled.events();
	}

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
	Stickers::Pack _pack;
	Stickers::ByEmojiMap _emoji;
	bool _loaded = false;
	uint64 _setId = 0;
	uint64 _setAccess = 0;
	QString _setTitle, _setShortName;
	int32 _setCount = 0;
	int32 _setHash = 0;
	MTPDstickerSet::Flags _setFlags = 0;

	MTPInputStickerSet _input;

	mtpRequestId _installRequest = 0;

	int _selected = -1;

	QTimer _previewTimer;
	int _previewShown = -1;

	rpl::event_stream<uint64> _setInstalled;

};
