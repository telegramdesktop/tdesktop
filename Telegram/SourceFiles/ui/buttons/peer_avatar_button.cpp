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
#include "stdafx.h"
#include "ui/buttons/peer_avatar_button.h"

#include "structs.h"

namespace Ui {

PeerAvatarButton::PeerAvatarButton(QWidget *parent, PeerData *peer, const style::PeerAvatarButton &st) : Button(parent)
, _peer(peer)
, _st(st) {
	resize(_st.size, _st.size);
}

void PeerAvatarButton::paintEvent(QPaintEvent *e) {
	if (_peer) {
		Painter p(this);
		_peer->paintUserpic(p, _st.photoSize, (_st.size - _st.photoSize) / 2, (_st.size - _st.photoSize) / 2);
	}
}

} // namespace Ui
