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
#include "ui/widgets/scroll_area.h"
#include "base/unique_qptr.h"
#include "info/media/info_media_widget.h"
#include "info/media/info_media_list_widget.h"

namespace Ui {
class SettingsSlider;
class VerticalLayout;
class SearchFieldController;
} // namespace Ui

namespace Info {

class Controller;

namespace Media {

class Memento;
class ListWidget;
class EmptyWidget;

class InnerWidget final : public Ui::RpWidget {
public:
	InnerWidget(
		QWidget *parent,
		not_null<Controller*> controller);

	bool showInternal(not_null<Memento*> memento);

	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	void setScrollHeightValue(rpl::producer<int> value);

	rpl::producer<Ui::ScrollToRequest> scrollToRequests() const;
	rpl::producer<SelectedItems> selectedListValue() const;
	void cancelSelection();

	~InnerWidget();

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

private:
	int recountHeight();
	void refreshHeight();
	// Allows showing additional shared media links and tabs.
	// Was done for top level tabs support.
	//
	//void setupOtherTypes();
	//void createOtherTypes();
	//void createTypeButtons();
	//void createTabs();
	//void switchToTab(Memento &&memento);
	//void refreshSearchField();
	//void scrollToSearchField();

	Type type() const;

	object_ptr<ListWidget> setupList();

	const not_null<Controller*> _controller;

	//Ui::SettingsSlider *_otherTabs = nullptr;
	//object_ptr<Ui::VerticalLayout> _otherTypes = { nullptr };
	//object_ptr<Ui::PlainShadow> _otherTabsShadow = { nullptr };
	//base::unique_qptr<Ui::RpWidget> _searchField = nullptr;
	object_ptr<ListWidget> _list = { nullptr };
	object_ptr<EmptyWidget> _empty;
	//bool _searchEnabled = false;

	bool _inResize = false;

	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;
	rpl::event_stream<rpl::producer<SelectedItems>> _selectedLists;
	rpl::event_stream<rpl::producer<int>> _listTops;

};

} // namespace Media
} // namespace Info
