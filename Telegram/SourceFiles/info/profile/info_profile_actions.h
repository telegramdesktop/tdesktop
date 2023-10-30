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
} // namespace Ui

namespace Data {
class ForumTopic;
} // namespace Data

namespace Info {
class Controller;
} // namespace Info

namespace Info::Profile {

extern const char kOptionShowPeerIdBelowAbout[];

object_ptr<Ui::RpWidget> SetupDetails(
	not_null<Controller*> controller,
	not_null<Ui::RpWidget*> parent,
	not_null<PeerData*> peer);

object_ptr<Ui::RpWidget> SetupDetails(
	not_null<Controller*> controller,
	not_null<Ui::RpWidget*> parent,
	not_null<Data::ForumTopic*> topic);

object_ptr<Ui::RpWidget> SetupActions(
	not_null<Controller*> controller,
	not_null<Ui::RpWidget*> parent,
	not_null<PeerData*> peer);

object_ptr<Ui::RpWidget> SetupChannelMembers(
	not_null<Controller*> controller,
	not_null<Ui::RpWidget*> parent,
	not_null<PeerData*> peer);

} // namespace Info::Profile
