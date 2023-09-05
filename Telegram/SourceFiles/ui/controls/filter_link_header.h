/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "base/required.h"

namespace Ui {

class RpWidget;
class RoundButton;

enum class FilterLinkHeaderType {
	AddingFilter,
	AddingChats,
	AllAdded,
	Removing,
};

struct FilterLinkHeaderDescriptor {
	base::required<FilterLinkHeaderType> type;
	base::required<QString> title;
	base::required<TextWithEntities> about;
	base::required<QString> folderTitle;
	not_null<const style::icon*> folderIcon;
	rpl::producer<int> badge;
};

struct FilterLinkHeader {
	not_null<RpWidget*> widget;
	rpl::producer<not_null<QWheelEvent*>> wheelEvents;
	rpl::producer<> closeRequests;
};

[[nodiscard]] FilterLinkHeader MakeFilterLinkHeader(
	not_null<QWidget*> parent,
	FilterLinkHeaderDescriptor &&descriptor);

[[nodiscard]] object_ptr<RoundButton> FilterLinkProcessButton(
	not_null<QWidget*> parent,
	FilterLinkHeaderType type,
	const QString &title,
	rpl::producer<int> badge);

} // namespace Ui
