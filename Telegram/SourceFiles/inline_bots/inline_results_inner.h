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
#include "dialogs/dialogs_key.h"
#include "base/timer.h"
#include "mtproto/sender.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "layout/layout_mosaic.h"

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
class PathShiftGradient;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace InlineBots {
class Result;
struct ResultSelected;
} // namespace InlineBots

namespace SendMenu {
struct Details;
} // namespace SendMenu

namespace InlineBots {
namespace Layout {

class ItemBase;
using Results = std::vector<std::unique_ptr<Result>>;

struct CacheEntry {
	QString nextOffset;
	QString switchPmText;
	QString switchPmStartToken;
	QByteArray switchPmUrl;
	Results results;
};

class Inner
	: public Ui::RpWidget
	, public Ui::AbstractTooltipShower
	, public Context {

public:
	Inner(QWidget *parent, not_null<Window::SessionController*> controller);

	void hideFinished();

	void clearSelection();

	int refreshInlineRows(PeerData *queryPeer, UserData *bot, const CacheEntry *results, bool resultsDeleted);
	void inlineBotChanged();
	void hideInlineRowsPanel();
	void clearInlineRowsPanel();

	void preloadImages();

	void inlineItemLayoutChanged(const ItemBase *layout) override;
	void inlineItemRepaint(const ItemBase *layout) override;
	bool inlineItemVisible(const ItemBase *layout) override;
	Data::FileOrigin inlineItemFileOrigin() override;

	int countHeight();

	void setResultSelectedCallback(Fn<void(ResultSelected)> callback) {
		_resultSelectedCallback = std::move(callback);
	}
	void setSendMenuDetails(Fn<SendMenu::Details()> &&callback);

	// Ui::AbstractTooltipShower interface.
	QString tooltipText() const override;
	QPoint tooltipPos() const override;
	bool tooltipWindowActive() const override;

	rpl::producer<> inlineRowsCleared() const;

	~Inner();

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void leaveToChildEvent(QEvent *e, QWidget *child) override;
	void enterFromChildEvent(QEvent *e, QWidget *child) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

private:
	static constexpr bool kRefreshIconsScrollAnimation = true;
	static constexpr bool kRefreshIconsNoAnimation = false;

	void switchPm();

	void updateSelected();
	void checkRestrictedPeer();
	bool isRestrictedView();
	void clearHeavyData();

	void paintInlineItems(Painter &p, const QRect &r);

	void refreshSwitchPmButton(const CacheEntry *entry);
	void refreshMosaicOffset();

	void showPreview();
	void updateInlineItems();
	void repaintItems(crl::time now = 0);
	void clearInlineRows(bool resultsDeleted);
	ItemBase *layoutPrepareInlineResult(Result *result);

	void deleteUnusedInlineLayouts();

	int validateExistingInlineRows(const Results &results);
	void selectInlineResult(
		int index,
		Api::SendOptions options,
		bool open);

	not_null<Window::SessionController*> _controller;
	const std::unique_ptr<Ui::PathShiftGradient> _pathGradient;

	int _visibleTop = 0;
	int _visibleBottom = 0;

	UserData *_inlineBot = nullptr;
	PeerData *_inlineQueryPeer = nullptr;
	crl::time _lastScrolledAt = 0;
	crl::time _lastUpdatedAt = 0;
	base::Timer _updateInlineItems;
	bool _inlineWithThumb = false;

	object_ptr<Ui::RoundButton> _switchPmButton = { nullptr };
	QString _switchPmStartToken;
	QByteArray _switchPmUrl;

	object_ptr<Ui::FlatLabel> _restrictedLabel = { nullptr };

	base::unique_qptr<Ui::PopupMenu> _menu;

	Mosaic::Layout::MosaicLayout<InlineBots::Layout::ItemBase> _mosaic;

	std::map<Result*, std::unique_ptr<ItemBase>> _inlineLayouts;

	rpl::event_stream<> _inlineRowsCleared;

	int _selected = -1;
	int _pressed = -1;
	QPoint _lastMousePos;

	base::Timer _previewTimer;
	bool _previewShown = false;

	Fn<void(ResultSelected)> _resultSelectedCallback;
	Fn<SendMenu::Details()> _sendMenuDetails;

};

} // namespace Layout
} // namespace InlineBots
