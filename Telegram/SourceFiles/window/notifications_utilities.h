/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "window/notifications_manager.h"
#include "base/timer.h"

namespace Data {
class CloudImageView;
} // namespace Data

namespace Window {
namespace Notifications {

class CachedUserpics : public QObject {
public:
	enum class Type {
		Rounded,
		Circled,
	};

	CachedUserpics(Type type);
	~CachedUserpics();

	[[nodiscard]] QString get(
		const InMemoryKey &key,
		not_null<PeerData*> peer,
		std::shared_ptr<Data::CloudImageView> &view);

private:
	void clear();
	void clearInMs(int ms);
	crl::time clear(crl::time ms);

	Type _type = Type::Rounded;
	struct Image {
		crl::time until = 0;
		QString path;
	};
	using Images = QMap<InMemoryKey, Image>;
	Images _images;
	bool _someSavedFlag = false;
	base::Timer _clearTimer;

};

} // namesapce Notifications
} // namespace Window
