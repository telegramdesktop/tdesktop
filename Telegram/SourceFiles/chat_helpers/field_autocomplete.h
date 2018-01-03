/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/twidget.h"
#include "chat_helpers/stickers.h"

namespace Ui {
class ScrollArea;
} // namespace Ui

namespace internal {

using MentionRows = QList<UserData*>;
using HashtagRows = QList<QString>;
using BotCommandRows = QList<QPair<UserData*, const BotCommand*>>;

class FieldAutocompleteInner;

} // namespace internal

class FieldAutocomplete final : public TWidget {
	Q_OBJECT

public:
	FieldAutocomplete(QWidget *parent);

	bool clearFilteredBotCommands();
	void showFiltered(PeerData *peer, QString query, bool addInlineBots);
	void showStickers(EmojiPtr emoji);
	void setBoundings(QRect boundings);

	const QString &filter() const;
	ChatData *chat() const;
	ChannelData *channel() const;
	UserData *user() const;

	int32 innerTop();
	int32 innerBottom();

	bool eventFilter(QObject *obj, QEvent *e) override;

	enum class ChooseMethod {
		ByEnter,
		ByTab,
		ByClick,
	};
	bool chooseSelected(ChooseMethod method) const;

	bool stickersShown() const {
		return !_srows.isEmpty();
	}

	bool overlaps(const QRect &globalRect) {
		if (isHidden() || !testAttribute(Qt::WA_OpaquePaintEvent)) return false;

		return rect().contains(QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size()));
	}

	void hideFast();

	~FieldAutocomplete();

signals:
	void mentionChosen(UserData *user, FieldAutocomplete::ChooseMethod method) const;
	void hashtagChosen(QString hashtag, FieldAutocomplete::ChooseMethod method) const;
	void botCommandChosen(QString command, FieldAutocomplete::ChooseMethod method) const;
	void stickerChosen(DocumentData *sticker, FieldAutocomplete::ChooseMethod method) const;

	void moderateKeyActivate(int key, bool *outHandled) const;

public slots:
	void showAnimated();
	void hideAnimated();

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void animationCallback();
	void hideFinish();

	void updateFiltered(bool resetScroll = false);
	void recount(bool resetScroll = false);

	QPixmap _cache;
	internal::MentionRows _mrows;
	internal::HashtagRows _hrows;
	internal::BotCommandRows _brows;
	Stickers::Pack _srows;

	void rowsUpdated(const internal::MentionRows &mrows, const internal::HashtagRows &hrows, const internal::BotCommandRows &brows, const Stickers::Pack &srows, bool resetScroll);

	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<internal::FieldAutocompleteInner> _inner;

	ChatData *_chat = nullptr;
	UserData *_user = nullptr;
	ChannelData *_channel = nullptr;
	EmojiPtr _emoji;
	enum class Type {
		Mentions,
		Hashtags,
		BotCommands,
		Stickers,
	};
	Type _type = Type::Mentions;
	QString _filter;
	QRect _boundings;
	bool _addInlineBots;

	int32 _width, _height;
	bool _hiding = false;

	Animation _a_opacity;

	friend class internal::FieldAutocompleteInner;

};

namespace internal {

class FieldAutocompleteInner final : public TWidget, private base::Subscriber {
	Q_OBJECT

public:
	FieldAutocompleteInner(FieldAutocomplete *parent, MentionRows *mrows, HashtagRows *hrows, BotCommandRows *brows, Stickers::Pack *srows);

	void clearSel(bool hidden = false);
	bool moveSel(int key);
	bool chooseSelected(FieldAutocomplete::ChooseMethod method) const;

	void setRecentInlineBotsInRows(int32 bots);

signals:
	void mentionChosen(UserData *user, FieldAutocomplete::ChooseMethod method) const;
	void hashtagChosen(QString hashtag, FieldAutocomplete::ChooseMethod method) const;
	void botCommandChosen(QString command, FieldAutocomplete::ChooseMethod method) const;
	void stickerChosen(DocumentData *sticker, FieldAutocomplete::ChooseMethod method) const;
	void mustScrollTo(int scrollToTop, int scrollToBottom);

public slots:
	void onParentGeometryChanged();
	void onUpdateSelected(bool force = false);
	void onPreview();

private:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	void updateSelectedRow();
	void setSel(int sel, bool scroll = false);

	FieldAutocomplete *_parent;
	MentionRows *_mrows;
	HashtagRows *_hrows;
	BotCommandRows *_brows;
	Stickers::Pack *_srows;
	int32 _stickersPerRow, _recentInlineBotsInRows;
	int32 _sel, _down;
	bool _mouseSel;
	QPoint _mousePos;

	bool _overDelete;

	bool _previewShown;

	QTimer _previewTimer;

};

} // namespace internal
