/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "profile/profile_block_peer_list.h"

namespace Notify {
struct PeerUpdate;
} // namespace Notify

namespace Profile {

struct CommonGroupsEvent {
	QList<PeerData*> groups;

	// If initialHeight >= 0 the common groups widget will
	// slide down starting from height() == initialHeight.
	// Otherwise it will just show instantly.
	int initialHeight = -1;
};

class CommonGroupsWidget : public PeerListWidget {
public:
	CommonGroupsWidget(QWidget *parent, PeerData *peer);

	void setShowCommonGroupsObservable(base::Observable<CommonGroupsEvent> *observable) {
		subscribe(observable, [this](const CommonGroupsEvent &event) { onShowCommonGroups(event); });
	}

	void saveState(SectionMemento *memento) const override;
	void restoreState(const SectionMemento *memento) override;

	~CommonGroupsWidget();

protected:
	int resizeGetHeight(int newWidth) override;
	void paintContents(Painter &p) override;

private:
	// Observed notifications.
	void notifyPeerUpdated(const Notify::PeerUpdate &update);

	void updateStatusText(Item *item);
	void onShowCommonGroups(const CommonGroupsEvent &event);
	void preloadMore();

	Item *computeItem(PeerData *group);
	QMap<PeerData*, Item*> _dataMap;

	IntAnimation _height;

	int32 _preloadGroupId = 0;
	mtpRequestId _preloadRequestId = 0;

};

} // namespace Profile
