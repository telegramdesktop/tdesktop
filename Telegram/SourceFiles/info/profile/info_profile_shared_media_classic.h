/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

class PeerData;

namespace Data {
class ForumTopic;
class SavedSublist;
} // namespace Data

namespace Ui {
class MultiSlideTracker;
class RpWidget;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Info {
class Controller;
} // namespace Info

namespace Info::Profile {

[[nodiscard]] object_ptr<Ui::SlideWrap<Ui::RpWidget>> SetupSharedMediaClassic(
	not_null<Ui::RpWidget*> parent,
	not_null<Controller*> controller,
	not_null<PeerData*> peer,
	Data::ForumTopic *topic,
	Data::SavedSublist *sublist,
	PeerData *migrated,
	Ui::MultiSlideTracker &tracker);

} // namespace Info::Profile
