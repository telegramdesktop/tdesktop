/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui {
class FlatInput;
class CrossButton;
class IconButton;
class FlatLabel;
} // namespace Ui

namespace Profile {
class GroupMembersWidget;
} // namespace Profile

namespace Info {

enum class Wrap;

namespace Profile {

class Members : public Ui::RpWidget {
public:
	Members(
		QWidget *parent,
		rpl::producer<Wrap> &&wrapValue,
		not_null<PeerData*> peer);

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;
	int resizeGetHeight(int newWidth) override;

private:
	using ListWidget = ::Profile::GroupMembersWidget;

	object_ptr<Ui::FlatLabel> setupHeader();
	object_ptr<ListWidget> setupList(
		RpWidget *parent) const;

	void setupButtons();
	void updateSearchOverrides();

	void addMember();
	void showSearch();
	void toggleSearch();
	void cancelSearch();
	void applySearch();
	void searchAnimationCallback();

	Wrap _wrap;
	not_null<PeerData*> _peer;
	object_ptr<Ui::RpWidget> _labelWrap;
	object_ptr<Ui::FlatLabel> _label;
	object_ptr<Ui::IconButton> _addMember;
	object_ptr<Ui::FlatInput> _searchField;
	object_ptr<Ui::IconButton> _search;
	object_ptr<Ui::CrossButton> _cancelSearch;
	object_ptr<ListWidget> _list;

	Animation _searchShownAnimation;
	bool _searchShown = false;
	base::Timer _searchTimer;

};

} // namespace Profile
} // namespace Info
