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

#include "core/observer.h"

namespace Notify {
struct PeerUpdate;
} // namespace Notify

namespace Profile {

class UserpicButton final : public Button, public Notify::Observer {
public:
	UserpicButton(QWidget *parent, PeerData *peer);

	// If at the first moment the _userpic was not loaded,
	// we need to show it animated after the profile is fully shown.
	void showFinished();

protected:
	void paintEvent(QPaintEvent *e);

private:
	void notifyPeerUpdated(const Notify::PeerUpdate &update);
	void notifyImageLoaded();

	void refreshCallback() {
		update();
	}

	void processPeerPhoto();
	void processNewPeerPhoto();
	void startNewPhotoShowing();
	QPixmap prepareUserpicPixmap() const;

	bool _notShownYet;

	PeerData *_peer;
	bool _waiting = false;
	QPixmap _userpic, _oldUserpic;
	FloatAnimation _a_appearance;

};

} // namespace Profile
