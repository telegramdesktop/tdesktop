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

namespace Platform {
namespace Toasts {

void start();
void finish();

bool supported();
bool create(PeerData *peer, int32 msgId, bool showpix, const QString &title, const QString &subtitle, const QString &msg);

// Returns the next ms when clearImages() should be called.
uint64 clearImages(uint64 ms);

void clearNotifies(PeerId peerId);

} // namespace Toasts
} // namespace Platform
