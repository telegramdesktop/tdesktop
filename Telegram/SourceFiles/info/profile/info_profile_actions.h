/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "info/profile/info_profile_section_stack.h"

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

object_ptr<Ui::RpWidget> SetupActions(
	not_null<Controller*> controller,
	not_null<Ui::RpWidget*> parent,
	not_null<PeerData*> peer);

object_ptr<Ui::RpWidget> SetupChannelMembersAndManage(
	not_null<Controller*> controller,
	not_null<Ui::RpWidget*> parent,
	not_null<PeerData*> peer);

void AddDetails(
	not_null<Ui::VerticalLayout*> container,
	not_null<Controller*> controller,
	not_null<PeerData*> peer,
	Data::ForumTopic *topic,
	Data::SavedSublist *sublist,
	Origin origin);

void BuildProfileDetailsSections(
	SectionStack &stack,
	not_null<Controller*> controller,
	not_null<PeerData*> peer,
	Data::ForumTopic *topic,
	Data::SavedSublist *sublist,
	Origin origin);

} // namespace Info::Profile

