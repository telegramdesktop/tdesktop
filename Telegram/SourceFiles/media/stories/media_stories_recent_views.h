/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/text/text.h"
#include "ui/userpic_view.h"

namespace Data {
struct StoryView;
} // namespace Data

namespace Ui {
class RpWidget;
class GroupCallUserpics;
class PopupMenu;
class WhoReactedEntryAction;
} // namespace Ui

namespace Main {
class Session;
} // namespace Main

namespace Media::Stories {

class Controller;

struct RecentViewsData {
	std::vector<not_null<PeerData*>> list;
	int reactions = 0;
	int total = 0;
	bool valid = false;

	friend inline auto operator<=>(
		const RecentViewsData &,
		const RecentViewsData &) = default;
	friend inline bool operator==(
		const RecentViewsData &,
		const RecentViewsData &) = default;
};

class RecentViews final {
public:
	explicit RecentViews(not_null<Controller*> controller);
	~RecentViews();

	void show(RecentViewsData data);

private:
	struct MenuEntry {
		not_null<Ui::WhoReactedEntryAction*> action;
		PeerData *peer = nullptr;
		QString date;
		QString customEntityData;
		Fn<void()> callback;
		Ui::PeerUserpicView view;
		InMemoryKey key;
	};

	void setupWidget();
	void setupUserpics();
	void updateUserpics();
	void updateText();
	void updatePartsGeometry();
	void showMenu();

	void addMenuRow(Data::StoryView entry, const QDateTime &now);
	void addMenuRowPlaceholder(not_null<Main::Session*> session);
	void rebuildMenuTail();
	void subscribeToMenuUserpicsLoading(not_null<Main::Session*> session);
	void refreshClickHandler();

	const not_null<Controller*> _controller;

	std::unique_ptr<Ui::RpWidget> _widget;
	std::unique_ptr<Ui::GroupCallUserpics> _userpics;
	Ui::Text::String _text;
	RecentViewsData _data;
	rpl::lifetime _userpicsLifetime;

	base::unique_qptr<Ui::PopupMenu> _menu;
	rpl::lifetime _menuShortLifetime;
	std::vector<MenuEntry> _menuEntries;
	rpl::variable<int> _menuEntriesCount = 0;
	int _menuPlaceholderCount = 0;
	base::flat_set<int> _waitingForUserpics;
	rpl::variable<bool> _shortAnimationPlaying;
	bool _waitingUserpicsCheck = false;
	rpl::lifetime _waitingForUserpicsLifetime;
	rpl::lifetime _clickHandlerLifetime;

	QRect _outer;
	QPoint _userpicsPosition;
	QPoint _textPosition;
	int _userpicsWidth = 0;

};

} // namespace Media::Stories