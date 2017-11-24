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
#include "info/info_wrap_widget.h"
#include "boxes/peer_list_controllers.h"

namespace style {
struct InfoTopBar;
} // namespace style

namespace Ui {
class IconButton;
class LabelWithNumbers;
struct StringWithNumbers;
} // namespace Ui

namespace Info {

class TopBarOverride : public Ui::RpWidget {
public:
	TopBarOverride(
		QWidget *parent,
		const style::InfoTopBar &st,
		SelectedItems &&items);

	void setItems(SelectedItems &&items);
	SelectedItems takeItems();

	rpl::producer<> cancelRequests() const;

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void updateControlsVisibility();
	void updateControlsGeometry(int newWidth);
	Ui::StringWithNumbers generateText() const;
	[[nodiscard]] bool computeCanDelete() const;
	[[nodiscard]] SelectedItemSet collectItems() const;

	void performForward();
	void performDelete();

	const style::InfoTopBar &_st;
	SelectedItems _items;
	bool _canDelete = false;
	object_ptr<Ui::IconButton> _cancel;
	object_ptr<Ui::LabelWithNumbers> _text;
	object_ptr<Ui::IconButton> _forward;
	object_ptr<Ui::IconButton> _delete;
	rpl::event_stream<> _correctionCancelRequests;

};


} // namespace Info
