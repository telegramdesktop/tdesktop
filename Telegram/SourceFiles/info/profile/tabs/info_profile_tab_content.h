/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/profile/tabs/info_profile_tab_top_bar_bindings.h"
#include "storage/storage_shared_media.h"

class PeerData;

namespace Data {
class ForumTopic;
class SavedSublist;
enum class ProfileTab : uchar;
} // namespace Data

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Info {
class Controller;
} // namespace Info

namespace Info::Profile {

struct MediaTabContext {
	not_null<Controller*> controller;
	not_null<PeerData*> peer;
	Data::ForumTopic *topic = nullptr;
	Data::SavedSublist *sublist = nullptr;
	PeerData *migrated = nullptr;
	QWidget *parent = nullptr;
	Fn<void(int ymin, int ymax)> scrollToRequest;
	Fn<void(int count)> onlineCountChanged;
};

class MediaTabContent {
public:
	virtual ~MediaTabContent() = default;

	[[nodiscard]] virtual not_null<Ui::RpWidget*> widget() = 0;
	[[nodiscard]] virtual TabTopBarBindings topBarBindings() = 0;

	virtual void resizeToWidth(int newWidth) = 0;

	virtual void deactivated() {
	}

	virtual void setVisibleRegion(int top, int bottom) {
	}

	virtual void setTopOverlay(int height) {
	}

	virtual void saveScrollState(QByteArray &out) {
	}
	virtual void restoreScrollState(const QByteArray &in) {
	}
};

struct MediaTabDescriptor {
	QString id;
	rpl::producer<TextWithEntities> title;
	rpl::producer<bool> shown;
	std::optional<Storage::SharedMediaType> sharedMediaType;
	Fn<std::unique_ptr<MediaTabContent>(MediaTabContext)> factory;
	Data::ProfileTab profileTab = {};
};

} // namespace Info::Profile
