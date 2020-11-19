/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/abstract_button.h"
#include "ui/widgets/tooltip.h"
#include "ui/effects/animations.h"
#include "ui/effects/panel_animation.h"
#include "base/timer.h"
#include "mtproto/sender.h"
#include "inline_bots/inline_bot_layout_item.h"

namespace Api {
struct SendOptions;
} // namespace Api

namespace Ui {
class ScrollArea;
class IconButton;
class LinkButton;
class RoundButton;
class FlatLabel;
class RippleAnimation;
class PopupMenu;
} // namespace Ui

namespace Dialogs {
struct EntryState;
} // namespace Dialogs

namespace Window {
class SessionController;
} // namespace Window

namespace InlineBots {
class Result;
struct ResultSelected;
} // namespace InlineBots

namespace SendMenu {
enum class Type;
} // namespace SendMenu

namespace InlineBots {
namespace Layout {

struct CacheEntry;
class Inner;

class Widget : public Ui::RpWidget {
public:
	Widget(QWidget *parent, not_null<Window::SessionController*> controller);
	~Widget();

	void moveBottom(int bottom);

	void hideFast();
	bool hiding() const {
		return _hiding;
	}

	void queryInlineBot(UserData *bot, PeerData *peer, QString query);
	void clearInlineBot();

	bool overlaps(const QRect &globalRect) const;

	void showAnimated();
	void hideAnimated();

	void setResultSelectedCallback(Fn<void(ResultSelected)> callback);
	void setSendMenuType(Fn<SendMenu::Type()> &&callback);
	void setCurrentDialogsEntryState(Dialogs::EntryState state);

	[[nodiscard]] rpl::producer<bool> requesting() const {
		return _requesting.events();
	}

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void moveByBottom();
	void paintContent(Painter &p);

	style::margins innerPadding() const;

	void onScroll();
	void onInlineRequest();

	// Rounded rect which has shadow around it.
	QRect innerRect() const;

	// Inner rect with removed st::buttonRadius from top and bottom.
	// This one is allowed to be not rounded.
	QRect horizontalRect() const;

	// Inner rect with removed st::buttonRadius from left and right.
	// This one is allowed to be not rounded.
	QRect verticalRect() const;

	QImage grabForPanelAnimation();
	void startShowAnimation();
	void startOpacityAnimation(bool hiding);
	void prepareCache();

	class Container;
	void opacityAnimationCallback();

	void hideFinished();
	void showStarted();

	void updateContentHeight();

	void inlineBotChanged();
	int showInlineRows(bool newResults);
	void recountContentMaxHeight();
	bool refreshInlineRows(int *added = nullptr);
	void inlineResultsDone(const MTPmessages_BotResults &result);

	const not_null<Window::SessionController*> _controller;
	MTP::Sender _api;

	int _contentMaxHeight = 0;
	int _contentHeight = 0;
	bool _horizontal = false;

	int _width = 0;
	int _height = 0;
	int _bottom = 0;

	std::unique_ptr<Ui::PanelAnimation> _showAnimation;
	Ui::Animations::Simple _a_show;

	bool _hiding = false;
	QPixmap _cache;
	Ui::Animations::Simple _a_opacity;
	bool _inPanelGrab = false;

	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<Inner> _inner;

	std::map<QString, std::unique_ptr<CacheEntry>> _inlineCache;
	base::Timer _inlineRequestTimer;

	UserData *_inlineBot = nullptr;
	PeerData *_inlineQueryPeer = nullptr;
	QString _inlineQuery, _inlineNextQuery, _inlineNextOffset;
	mtpRequestId _inlineRequestId = 0;

	rpl::event_stream<bool> _requesting;

};

} // namespace Layout
} // namespace InlineBots
