/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

class PeerListRow;

namespace Ui {
class Show;
class BoxContent;
class VerticalLayout;
} // namespace Ui

namespace Data {
class ChatFilter;
struct ChatFilterLink;
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

[[nodiscard]] std::vector<not_null<PeerData*>> CollectFilterLinkChats(
	const Data::ChatFilter &filter);
[[nodiscard]] bool GoodForExportFilterLink(
	not_null<Window::SessionController*> window,
	const Data::ChatFilter &filter);

void ExportFilterLink(
	FilterId id,
	const std::vector<not_null<PeerData*>> &peers,
	Fn<void(Data::ChatFilterLink)> done,
	Fn<void(QString)> fail);

[[nodiscard]] object_ptr<Ui::BoxContent> ShowLinkBox(
	not_null<Window::SessionController*> window,
	const Data::ChatFilter &filter,
	const Data::ChatFilterLink &link);
[[nodiscard]] QString FilterChatStatusText(not_null<PeerData*> peer);

void SetupFilterLinks(
	not_null<Ui::VerticalLayout*> container,
	not_null<Window::SessionController*> window,
	rpl::producer<std::vector<Data::ChatFilterLink>> value,
	Fn<Data::ChatFilter()> currentFilter);

void AddFilterSubtitleWithToggles(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> text,
	int selectableCount,
	rpl::producer<int> selectedCount,
	Fn<void(bool select)> toggle);

[[nodiscard]] std::unique_ptr<PeerListRow> MakeFilterChatRow(
	not_null<PeerData*> peer,
	const QString &status,
	bool disabled);
