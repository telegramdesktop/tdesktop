/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "window/notifications_manager.h"
#include "base/timer.h"

namespace Ui {
struct PeerUserpicView;
} // namespace Ui

namespace Window::Notifications {

[[nodiscard]] QImage GenerateUserpic(
	not_null<PeerData*> peer,
	Ui::PeerUserpicView &view);

class CachedUserpics : public QObject {
public:
	CachedUserpics();
	~CachedUserpics();

	[[nodiscard]] QString get(
		const InMemoryKey &key,
		not_null<PeerData*> peer,
		Ui::PeerUserpicView &view);

private:
	void clear();
	void clearInMs(int ms);
	crl::time clear(crl::time ms);

	struct Image {
		crl::time until = 0;
		QString path;
	};
	using Images = QMap<InMemoryKey, Image>;
	Images _images;
	bool _someSavedFlag = false;
	base::Timer _clearTimer;

};

} // namespace Window::Notifications
