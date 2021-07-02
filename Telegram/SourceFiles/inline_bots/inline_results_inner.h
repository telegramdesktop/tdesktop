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
enum class Type;
} // namespace SendMenu

namespace InlineBots {
namespace Layout {

constexpr int kInlineItemsMaxPerRow = 5;

class ItemBase;
using Results = std::vector<std::unique_ptr<Result>>;

struct CacheEntry {
	QString nextOffset;
	QString switchPmText, switchPmStartToken;
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
	void setCurrentDialogsEntryState(Dialogs::EntryState state) {
		_currentDialogsEntryState = state;
	}
	void setSendMenuType(Fn<SendMenu::Type()> &&callback);

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

	struct Row {
		int height = 0;
		QVector<ItemBase*> items;
	};

	void switchPm();

	void updateSelected();
	void checkRestrictedPeer();
	bool isRestrictedView();
	void clearHeavyData();

	void paintInlineItems(Painter &p, const QRect &r);

	void refreshSwitchPmButton(const CacheEntry *entry);

	void showPreview();
	void updateInlineItems();
	void clearInlineRows(bool resultsDeleted);
	ItemBase *layoutPrepareInlineResult(Result *result, int32 position);

	bool inlineRowsAddItem(Result *result, Row &row, int32 &sumWidth);
	bool inlineRowFinalize(Row &row, int32 &sumWidth, bool force = false);

	Row &layoutInlineRow(Row &row, int32 sumWidth = 0);
	void deleteUnusedInlineLayouts();

	int validateExistingInlineRows(const Results &results);
	void selectInlineResult(
		int row,
		int column,
		Api::SendOptions options,
		bool open);

	not_null<Window::SessionController*> _controller;
	const std::unique_ptr<Ui::PathShiftGradient> _pathGradient;

	int _visibleTop = 0;
	int _visibleBottom = 0;

	UserData *_inlineBot = nullptr;
	PeerData *_inlineQueryPeer = nullptr;
	crl::time _lastScrolled = 0;
	base::Timer _updateInlineItems;
	bool _inlineWithThumb = false;

	object_ptr<Ui::RoundButton> _switchPmButton = { nullptr };
	QString _switchPmStartToken;
	Dialogs::EntryState _currentDialogsEntryState;

	object_ptr<Ui::FlatLabel> _restrictedLabel = { nullptr };

	base::unique_qptr<Ui::PopupMenu> _menu;

	QVector<Row> _rows;

	std::map<Result*, std::unique_ptr<ItemBase>> _inlineLayouts;

	rpl::event_stream<> _inlineRowsCleared;

	int _selected = -1;
	int _pressed = -1;
	QPoint _lastMousePos;

	base::Timer _previewTimer;
	bool _previewShown = false;

	Fn<void(ResultSelected)> _resultSelectedCallback;
	Fn<SendMenu::Type()> _sendMenuType;

};

} // namespace Layout
} // namespace InlineBots
