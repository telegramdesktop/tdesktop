/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "ui/wrap/vertical_blocks.h"

namespace Ui {
class RpWidget;
class VerticalLayout;
} // namespace Ui

namespace Data {
class ForumTopic;
class SavedSublist;
} // namespace Data

namespace Info {
class Controller;
} // namespace Info

namespace Info::Profile {

extern const char kOptionShowPeerIdBelowAbout[];
extern const char kOptionShowChannelJoinedBelowAbout[];

struct Origin;

void AddActionsBlocks(
	Ui::VerticalBlocks blocks,
	not_null<Controller*> controller,
	not_null<PeerData*> peer);

object_ptr<Ui::RpWidget> SetupChannelMembersAndManage(
	not_null<Controller*> controller,
	not_null<Ui::RpWidget*> parent,
	not_null<PeerData*> peer);

void AddDetailsBlocks(
	Ui::VerticalBlocks blocks,
	not_null<Controller*> controller,
	not_null<PeerData*> peer,
	Data::ForumTopic *topic,
	Data::SavedSublist *sublist,
	Origin origin);

} // namespace Info::Profile

