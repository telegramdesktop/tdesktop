/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
	void setIsStackBottom(bool isStackBottom) {
		_isStackBottom = isStackBottom;
		setupOtherTypes();
	}

	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	void setScrollHeightValue(rpl::producer<int> value);

	rpl::producer<Ui::ScrollToRequest> scrollToRequests() const;
	rpl::producer<SelectedItems> selectedListValue() const;
	void selectionAction(SelectionAction action);

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
	// Used for shared media in Saved Messages.
	void setupOtherTypes();
	void createOtherTypes();
	void createTypeButtons();

	Type type() const;

	object_ptr<ListWidget> setupList();

	const not_null<Controller*> _controller;

	object_ptr<Ui::VerticalLayout> _otherTypes = { nullptr };
	object_ptr<ListWidget> _list = { nullptr };
	object_ptr<EmptyWidget> _empty;

	bool _inResize = false;
	bool _isStackBottom = false;

	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;
	rpl::event_stream<rpl::producer<SelectedItems>> _selectedLists;
	rpl::event_stream<rpl::producer<int>> _listTops;

};

} // namespace Media
} // namespace Info
