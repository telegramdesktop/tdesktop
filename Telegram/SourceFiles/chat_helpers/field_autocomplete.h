/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/rp_widget.h"
#include "base/timer.h"
#include "base/object_ptr.h"

namespace Ui {
class ScrollArea;
} // namespace Ui

namespace Lottie {
class SinglePlayer;
class FrameRenderer;
} // namespace Lottie;

namespace Window {
class SessionController;
} // namespace Window

namespace Data {
class DocumentMedia;
class CloudImageView;
} // namespace Data

namespace internal {

struct StickerSuggestion {
	not_null<DocumentData*> document;
	std::shared_ptr<Data::DocumentMedia> documentMedia;
	std::unique_ptr<Lottie::SinglePlayer> animated;
};

struct MentionRow {
	not_null<UserData*> user;
	std::shared_ptr<Data::CloudImageView> userpic;
};

struct BotCommandRow {
	not_null<UserData*> user;
	not_null<const BotCommand*> command;
	std::shared_ptr<Data::CloudImageView> userpic;
};

using HashtagRows = std::vector<QString>;
using BotCommandRows = std::vector<BotCommandRow>;
using StickerRows = std::vector<StickerSuggestion>;
using MentionRows = std::vector<MentionRow>;

class FieldAutocompleteInner;

} // namespace internal

class FieldAutocomplete final : public Ui::RpWidget {
	Q_OBJECT

public:
	FieldAutocomplete(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~FieldAutocomplete();

	bool clearFilteredBotCommands();
	void showFiltered(
		not_null<PeerData*> peer,
		QString query,
		bool addInlineBots);
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
		return !_srows.empty();
	}

	bool overlaps(const QRect &globalRect) {
		if (isHidden() || !testAttribute(Qt::WA_OpaquePaintEvent)) return false;

		return rect().contains(QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size()));
	}

	void hideFast();

signals:
	void mentionChosen(not_null<UserData*> user, FieldAutocomplete::ChooseMethod method) const;
	void hashtagChosen(QString hashtag, FieldAutocomplete::ChooseMethod method) const;
	void botCommandChosen(QString command, FieldAutocomplete::ChooseMethod method) const;
	void stickerChosen(not_null<DocumentData*> sticker, FieldAutocomplete::ChooseMethod method) const;

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
	internal::StickerRows getStickerSuggestions();

	const not_null<Window::SessionController*> _controller;
	QPixmap _cache;
	internal::MentionRows _mrows;
	internal::HashtagRows _hrows;
	internal::BotCommandRows _brows;
	internal::StickerRows _srows;

	void rowsUpdated(
		internal::MentionRows &&mrows,
		internal::HashtagRows &&hrows,
		internal::BotCommandRows &&brows,
		internal::StickerRows &&srows,
		bool resetScroll);

	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<internal::FieldAutocompleteInner> _inner;

	ChatData *_chat = nullptr;
	UserData *_user = nullptr;
	ChannelData *_channel = nullptr;
	EmojiPtr _emoji;
	uint64 _stickersSeed = 0;
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

	Ui::Animations::Simple _a_opacity;

	friend class internal::FieldAutocompleteInner;

};

namespace internal {

class FieldAutocompleteInner final
	: public Ui::RpWidget
	, private base::Subscriber {
	Q_OBJECT

public:
	FieldAutocompleteInner(
		not_null<Window::SessionController*> controller,
		not_null<FieldAutocomplete*> parent,
		not_null<MentionRows*> mrows,
		not_null<HashtagRows*> hrows,
		not_null<BotCommandRows*> brows,
		not_null<StickerRows*> srows);

	void clearSel(bool hidden = false);
	bool moveSel(int key);
	bool chooseSelected(FieldAutocomplete::ChooseMethod method) const;

	void setRecentInlineBotsInRows(int32 bots);
	void rowsUpdated();

signals:
	void mentionChosen(not_null<UserData*> user, FieldAutocomplete::ChooseMethod method) const;
	void hashtagChosen(QString hashtag, FieldAutocomplete::ChooseMethod method) const;
	void botCommandChosen(QString command, FieldAutocomplete::ChooseMethod method) const;
	void stickerChosen(not_null<DocumentData*> sticker, FieldAutocomplete::ChooseMethod method) const;
	void mustScrollTo(int scrollToTop, int scrollToBottom);

public slots:
	void onParentGeometryChanged();

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
	void showPreview();
	void selectByMouse(QPoint global);

	QSize stickerBoundingBox() const;
	void setupLottie(StickerSuggestion &suggestion);
	void repaintSticker(not_null<DocumentData*> document);
	std::shared_ptr<Lottie::FrameRenderer> getLottieRenderer();

	const not_null<Window::SessionController*> _controller;
	const not_null<FieldAutocomplete*> _parent;
	const not_null<MentionRows*> _mrows;
	const not_null<HashtagRows*> _hrows;
	const not_null<BotCommandRows*> _brows;
	const not_null<StickerRows*> _srows;
	rpl::lifetime _stickersLifetime;
	std::weak_ptr<Lottie::FrameRenderer> _lottieRenderer;
	int _stickersPerRow = 1;
	int _recentInlineBotsInRows = 0;
	int _sel = -1;
	int _down = -1;
	std::optional<QPoint> _lastMousePosition;
	bool _mouseSelection = false;

	bool _overDelete = false;

	bool _previewShown = false;

	base::Timer _previewTimer;

};

} // namespace internal
