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

class Painter;

[[nodiscard]] QString FilterChatsTypeName(Data::ChatFilter::Flag flag);
void PaintFilterChatsTypeIcon(
	Painter &p,
	Data::ChatFilter::Flag flag,
	int x,
	int y,
	int outerWidth,
	int size);

class EditFilterChatsListController final : public ChatsListBoxController {
public:
	using Flag = Data::ChatFilter::Flag;
	using Flags = Data::ChatFilter::Flags;

	EditFilterChatsListController(
		not_null<Main::Session*> session,
		rpl::producer<QString> title,
		Flags options,
		Flags selected,
		const base::flat_set<not_null<History*>> &peers);

	[[nodiscard]] Main::Session &session() const override;
	[[nodiscard]] Flags chosenOptions() const {
		return _selected;
	}

	void rowClicked(not_null<PeerListRow*> row) override;
	void itemDeselectedHook(not_null<PeerData*> peer) override;
	bool isForeignRow(PeerListRowId itemId) override;
	bool handleDeselectForeignRow(PeerListRowId itemId) override;

private:
	void prepareViewHook() override;
	std::unique_ptr<Row> createRow(not_null<History*> history) override;
	[[nodiscard]] object_ptr<Ui::RpWidget> prepareTypesList();

	void updateTitle();

	const not_null<Main::Session*> _session;
	rpl::producer<QString> _title;
	base::flat_set<not_null<History*>> _peers;
	Flags _options;
	Flags _selected;

	Fn<void(PeerListRowId)> _deselectOption;

	rpl::lifetime _lifetime;

};
