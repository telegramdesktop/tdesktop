/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

class HistoryItem;
class PeerListController;

namespace Data {
struct ReactionId;
} // namespace Data

namespace Api {
struct WhoReadList;
} // namespace Api

namespace Window {
class SessionController;
class SessionNavigation;
} // namespace Window

namespace Ui {
class BoxContent;
} // namespace Ui

namespace HistoryView::Reactions {

[[nodiscard]] Data::ReactionId DefaultSelectedTab(
	not_null<HistoryItem*> item,
	std::shared_ptr<Api::WhoReadList> whoReadIds);

[[nodiscard]] Data::ReactionId DefaultSelectedTab(
	not_null<HistoryItem*> item,
	Data::ReactionId selected,
	std::shared_ptr<Api::WhoReadList> whoReadIds = nullptr);

struct Tabs;
[[nodiscard]] not_null<Tabs*> CreateReactionsTabs(
	not_null<QWidget*> parent,
	not_null<Window::SessionNavigation*> window,
	FullMsgId itemId,
	Data::ReactionId selected,
	std::shared_ptr<Api::WhoReadList> whoReadIds);

struct PreparedFullList {
	std::unique_ptr<PeerListController> controller;
	Fn<void(Data::ReactionId)> switchTab;
};
[[nodiscard]] PreparedFullList FullListController(
	not_null<Window::SessionNavigation*> window,
	FullMsgId itemId,
	Data::ReactionId selected,
	std::shared_ptr<Api::WhoReadList> whoReadIds = nullptr);

} // namespace HistoryView::Reactions
