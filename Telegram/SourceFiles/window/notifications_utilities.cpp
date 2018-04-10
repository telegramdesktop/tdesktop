/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/notifications_utilities.h"

#include "platform/platform_specific.h"
#include "messenger.h"
#include "styles/style_window.h"

namespace Window {
namespace Notifications {
namespace {

// Delete notify photo file after 1 minute of not using.
constexpr int kNotifyDeletePhotoAfterMs = 60000;

} // namespace

CachedUserpics::CachedUserpics(Type type) : _type(type) {
	connect(&_clearTimer, SIGNAL(timeout()), this, SLOT(onClear()));
	QDir().mkpath(cWorkingDir() + qsl("tdata/temp"));
}

QString CachedUserpics::get(const StorageKey &key, PeerData *peer) {
	auto ms = getms(true);
	auto i = _images.find(key);
	if (i != _images.cend()) {
		if (i->until) {
			i->until = ms + kNotifyDeletePhotoAfterMs;
			clearInMs(-kNotifyDeletePhotoAfterMs);
		}
	} else {
		Image v;
		if (key.first) {
			v.until = ms + kNotifyDeletePhotoAfterMs;
			clearInMs(-kNotifyDeletePhotoAfterMs);
		} else {
			v.until = 0;
		}
		v.path = cWorkingDir() + qsl("tdata/temp/") + QString::number(rand_value<uint64>(), 16) + qsl(".png");
		if (key.first || key.second) {
			if (_type == Type::Rounded) {
				peer->saveUserpicRounded(v.path, st::notifyMacPhotoSize);
			} else {
				peer->saveUserpic(v.path, st::notifyMacPhotoSize);
			}
		} else {
			Messenger::Instance().logoNoMargin().save(v.path, "PNG");
		}
		i = _images.insert(key, v);
		_someSavedFlag = true;
	}
	return i->path;
}

TimeMs CachedUserpics::clear(TimeMs ms) {
	TimeMs result = 0;
	for (auto i = _images.begin(); i != _images.end();) {
		if (!i->until) {
			++i;
			continue;
		}
		if (i->until <= ms) {
			QFile(i->path).remove();
			i = _images.erase(i);
		} else {
			if (!result) {
				result = i->until;
			} else {
				accumulate_min(result, i->until);
			}
			++i;
		}
	}
	return result;
}

void CachedUserpics::clearInMs(int ms) {
	if (ms < 0) {
		ms = -ms;
		if (_clearTimer.isActive() && _clearTimer.remainingTime() <= ms) {
			return;
		}
	}
	_clearTimer.start(ms);
}

void CachedUserpics::onClear() {
	auto ms = getms(true);
	auto minuntil = clear(ms);
	if (minuntil) {
		clearInMs(int(minuntil - ms));
	}
}

CachedUserpics::~CachedUserpics() {
	if (_someSavedFlag) {
		TimeMs result = 0;
		for_const (auto &item, _images) {
			QFile(item.path).remove();
		}

// This works about 1200ms on Windows for a folder with one image O_o
//		psDeleteDir(cWorkingDir() + qsl("tdata/temp"));
	}
}

} // namespace Notifications
} // namespace Window
