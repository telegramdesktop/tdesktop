/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/scroll_area.h"
#include "base/qt_connection.h"

namespace Ui {

// This class is designed for seamless scrolling of
// on-demand augmented content.
class ContinuousScroll final : public ScrollArea {
public:
	using ScrollArea::ScrollArea;

	[[nodiscard]] rpl::producer<> addContentRequests() const;
	void contentAdded();

	void setTrackingContent(bool value);

protected:
	void wheelEvent(QWheelEvent *e) override;

private:
	void reconnect();

	base::qt_connection _connection;

	bool _contentAdded = false;
	bool _tracking = false;

	rpl::event_stream<> _addContentRequests;

};

} // namespace Ui
