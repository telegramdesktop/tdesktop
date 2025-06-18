/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

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

class Cover;
struct Origin;

object_ptr<Ui::RpWidget> SetupDetails(
	not_null<Controller*> controller,
	not_null<Ui::RpWidget*> parent,
	not_null<PeerData*> peer,
	Origin origin);

object_ptr<Ui::RpWidget> SetupDetails(
	not_null<Controller*> controller,
	not_null<Ui::RpWidget*> parent,
	not_null<Data::ForumTopic*> topic);

object_ptr<Ui::RpWidget> SetupActions(
	not_null<Controller*> controller,
	not_null<Ui::RpWidget*> parent,
	not_null<PeerData*> peer);

object_ptr<Ui::RpWidget> SetupChannelMembersAndManage(
	not_null<Controller*> controller,
	not_null<Ui::RpWidget*> parent,
	not_null<PeerData*> peer);

Cover *AddCover(
	not_null<Ui::VerticalLayout*> container,
	not_null<Controller*> controller,
	not_null<PeerData*> peer,
	Data::ForumTopic *topic,
	Data::SavedSublist *sublist);
void AddDetails(
	not_null<Ui::VerticalLayout*> container,
	not_null<Controller*> controller,
	not_null<PeerData*> peer,
	Data::ForumTopic *topic,
	Data::SavedSublist *sublist,
	Origin origin);

} // namespace Info::Profile

