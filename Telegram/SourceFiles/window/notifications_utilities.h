/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "window/notifications_manager.h"
#include "core/single_timer.h"

namespace Window {
namespace Notifications {

class CachedUserpics : public QObject {
	Q_OBJECT

public:
	enum class Type {
		Rounded,
		Circled,
	};
	CachedUserpics(Type type);

	QString get(const StorageKey &key, PeerData *peer);

	~CachedUserpics();

private slots:
	void onClear();

private:
	void clearInMs(int ms);
	TimeMs clear(TimeMs ms);

	Type _type = Type::Rounded;
	struct Image {
		TimeMs until;
		QString path;
	};
	using Images = QMap<StorageKey, Image>;
	Images _images;
	bool _someSavedFlag = false;
	SingleTimer _clearTimer;

};

} // namesapce Notifications
} // namespace Window
