/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class PopupMenu;
enum class ReportReason;
} // namespace Ui

namespace Window {
class SessionNavigation;
class SessionController;
} // namespace Main

namespace HistoryView {

enum class Context : char;
enum class PointState : char;
class ListWidget;
class Element;
struct SelectedItem;
using SelectedItems = std::vector<SelectedItem>;

struct ContextMenuRequest {
	explicit ContextMenuRequest(
		not_null<Window::SessionNavigation*> navigation);

	const not_null<Window::SessionNavigation*> navigation;
	ClickHandlerPtr link;
	Element *view = nullptr;
	HistoryItem *item = nullptr;
	SelectedItems selectedItems;
	TextForMimeData selectedText;
	bool overSelection = false;
	PointState pointState = PointState();
};

base::unique_qptr<Ui::PopupMenu> FillContextMenu(
	not_null<ListWidget*> list,
	const ContextMenuRequest &request);

void CopyPostLink(
	not_null<Main::Session*> session,
	FullMsgId itemId,
	Context context);
void StopPoll(not_null<Main::Session*> session, FullMsgId itemId);
void AddPollActions(
	not_null<Ui::PopupMenu*> menu,
	not_null<PollData*> poll,
	not_null<HistoryItem*> item,
	Context context);
void AddSaveSoundForNotifications(
	not_null<Ui::PopupMenu*> menu,
	not_null<HistoryItem*> item,
	not_null<DocumentData*> document,
	not_null<Window::SessionController*> controller);
void AddWhoReactedAction(
	not_null<Ui::PopupMenu*> menu,
	not_null<QWidget*> context,
	not_null<HistoryItem*> item,
	not_null<Window::SessionController*> controller);
void ShowWhoReactedMenu(
	not_null<base::unique_qptr<Ui::PopupMenu>*> menu,
	QPoint position,
	not_null<QWidget*> context,
	not_null<HistoryItem*> item,
	const QString &emoji,
	not_null<Window::SessionController*> controller,
	rpl::lifetime &lifetime);

void ShowReportItemsBox(not_null<PeerData*> peer, MessageIdsList ids);
void ShowReportPeerBox(
	not_null<Window::SessionController*> window,
	not_null<PeerData*> peer);
void SendReport(
	not_null<PeerData*> peer,
	Ui::ReportReason reason,
	const QString &comment,
	MessageIdsList ids = {});

} // namespace HistoryView
