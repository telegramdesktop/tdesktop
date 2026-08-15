/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/info_wrap_widget.h"

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Data {
class SavedSublist;
} // namespace Data

namespace Info {
class AbstractController;
} // namespace Info

namespace Info::Saved {

struct InlineSublist {
	Ui::RpWidget *list = nullptr;
	Fn<void(int width, int viewportHeight)> updateGeometry;
	Fn<void(int top, int bottom)> setVisibleRegion;
	Fn<void(QPainter &p, QRect clip)> paintBackground;
	rpl::producer<SelectedItems> selectedItems;
	rpl::producer<> firstSliceLoaded;
	Fn<void(SelectionAction)> selectionAction;
	std::shared_ptr<void> guard;
};

[[nodiscard]] InlineSublist MakeInlineSublist(
	not_null<Ui::RpWidget*> parent,
	not_null<AbstractController*> controller,
	not_null<Data::SavedSublist*> sublist,
	Fn<void(int top)> scrollToRequest);

} // namespace Info::Saved
