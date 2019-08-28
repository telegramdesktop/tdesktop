/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/notifications_utilities.h"

#include "platform/platform_specific.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "ui/empty_userpic.h"
#include "styles/style_window.h"

namespace Window {
namespace Notifications {
namespace {

// Delete notify photo file after 1 minute of not using.
constexpr int kNotifyDeletePhotoAfterMs = 60000;

} // namespace

CachedUserpics::CachedUserpics(Type type)
: _type(type)
, _clearTimer([=] { onClear(); }) {
	QDir().mkpath(cWorkingDir() + qsl("tdata/temp"));
}

QString CachedUserpics::get(const InMemoryKey &key, PeerData *peer) {
	auto ms = crl::now();
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
			if (peer->isSelf()) {
				const auto method = _type == Type::Rounded
					? Ui::EmptyUserpic::GenerateSavedMessagesRounded
					: Ui::EmptyUserpic::GenerateSavedMessages;
				method(st::notifyMacPhotoSize).save(v.path, "PNG");
			} else if (_type == Type::Rounded) {
				peer->saveUserpicRounded(v.path, st::notifyMacPhotoSize);
			} else {
				peer->saveUserpic(v.path, st::notifyMacPhotoSize);
			}
		} else {
			Core::App().logoNoMargin().save(v.path, "PNG");
		}
		i = _images.insert(key, v);
		_someSavedFlag = true;
	}
	return i->path;
}

crl::time CachedUserpics::clear(crl::time ms) {
	crl::time result = 0;
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
	_clearTimer.callOnce(ms);
}

void CachedUserpics::onClear() {
	auto ms = crl::now();
	auto minuntil = clear(ms);
	if (minuntil) {
		clearInMs(int(minuntil - ms));
	}
}

CachedUserpics::~CachedUserpics() {
	if (_someSavedFlag) {
		crl::time result = 0;
		for (const auto &item : std::as_const(_images)) {
			QFile(item.path).remove();
		}

// This works about 1200ms on Windows for a folder with one image O_o
//		psDeleteDir(cWorkingDir() + qsl("tdata/temp"));
	}
}

} // namespace Notifications
} // namespace Window
