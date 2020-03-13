/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/peer_list_controllers.h"
#include "data/data_chat_filters.h"

class History;

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Main {
class Session;
} // namespace Main

class EditFilterChatsListController final : public ChatsListBoxController {
public:
	using Flag = Data::ChatFilter::Flag;
	using Flags = Data::ChatFilter::Flags;

	EditFilterChatsListController(
		not_null<Window::SessionNavigation*> navigation,
		rpl::producer<QString> title,
		Flags options,
		Flags selected,
		const base::flat_set<not_null<History*>> &peers);

	Main::Session &session() const override;

	void rowClicked(not_null<PeerListRow*> row) override;
	void itemDeselectedHook(not_null<PeerData*> peer) override;

protected:
	void prepareViewHook() override;
	std::unique_ptr<Row> createRow(not_null<History*> history) override;

private:
	void updateTitle();

	const not_null<Window::SessionNavigation*> _navigation;
	rpl::producer<QString> _title;
	base::flat_set<not_null<History*>> _peers;

};
