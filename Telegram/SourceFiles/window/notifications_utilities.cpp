/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/notifications_utilities.h"

#include "base/platform/base_platform_file_utilities.h"
#include "base/openssl_help.h"
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
, _clearTimer([=] { clear(); }) {
	QDir().mkpath(cWorkingDir() + qsl("tdata/temp"));
}

CachedUserpics::~CachedUserpics() {
	if (_someSavedFlag) {
		for (const auto &item : std::as_const(_images)) {
			QFile(item.path).remove();
		}

		// This works about 1200ms on Windows for a folder with one image O_o
		//		base::Platform::DeleteDirectory(cWorkingDir() + qsl("tdata/temp"));
	}
}

QString CachedUserpics::get(
		const InMemoryKey &key,
		not_null<PeerData*> peer,
		std::shared_ptr<Data::CloudImageView> &view) {
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
		v.path = u"%1tdata/temp/%2.png"_q.arg(
			cWorkingDir(),
			QString::number(openssl::RandomValue<uint64>(), 16));
		if (key.first || key.second) {
			if (peer->isSelf()) {
				const auto method = (_type == Type::Rounded)
					? Ui::EmptyUserpic::GenerateSavedMessagesRounded
					: Ui::EmptyUserpic::GenerateSavedMessages;
				method(st::notifyMacPhotoSize).save(v.path, "PNG");
			} else if (peer->isRepliesChat()) {
				const auto method = (_type == Type::Rounded)
					? Ui::EmptyUserpic::GenerateRepliesMessagesRounded
					: Ui::EmptyUserpic::GenerateRepliesMessages;
				method(st::notifyMacPhotoSize).save(v.path, "PNG");
			} else if (_type == Type::Rounded) {
				peer->saveUserpicRounded(view, v.path, st::notifyMacPhotoSize);
			} else {
				peer->saveUserpic(view, v.path, st::notifyMacPhotoSize);
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

void CachedUserpics::clear() {
	auto ms = crl::now();
	auto minuntil = clear(ms);
	if (minuntil) {
		clearInMs(int(minuntil - ms));
	}
}

} // namespace Notifications
} // namespace Window
