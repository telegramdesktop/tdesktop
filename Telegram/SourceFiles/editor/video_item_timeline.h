/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/rp_widget.h"

namespace Editor {

class ItemVideo;
class TimelineSeeker;
class VideoTimeline;

class VideoItemTimeline final : public Ui::RpWidget {
public:
	explicit VideoItemTimeline(not_null<QWidget*> parent);
	~VideoItemTimeline();

	void setItem(std::shared_ptr<ItemVideo> item);
	void refreshTrim();
	void commitPendingEdit();

	[[nodiscard]] rpl::producer<crl::time> lengthChanges() const;

	int resizeGetHeight(int newWidth) override;

private:
	std::shared_ptr<ItemVideo> _item;
	base::unique_qptr<VideoTimeline> _timeline;
	std::unique_ptr<TimelineSeeker> _seeker;
	rpl::event_stream<crl::time> _lengthChanges;

	rpl::lifetime _itemLifetime;

};

} // namespace Editor
