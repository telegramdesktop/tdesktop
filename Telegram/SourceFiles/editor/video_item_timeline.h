/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/rp_widget.h"

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Editor {

class TimelineSeeker;
class VideoClip;
class VideoTimeline;

class VideoItemTimeline final : public Ui::RpWidget {
public:
	explicit VideoItemTimeline(not_null<QWidget*> parent);
	~VideoItemTimeline();

	void setClip(std::shared_ptr<VideoClip> clip);
	void refreshTrim();
	void commitPendingEdit();

	[[nodiscard]] rpl::producer<crl::time> lengthChanges() const;

	int resizeGetHeight(int newWidth) override;

protected:
	void contextMenuEvent(QContextMenuEvent *e) override;

private:
	std::shared_ptr<VideoClip> _clip;
	base::unique_qptr<VideoTimeline> _timeline;
	std::unique_ptr<TimelineSeeker> _seeker;
	base::unique_qptr<Ui::PopupMenu> _menu;
	rpl::event_stream<crl::time> _lengthChanges;

	rpl::lifetime _clipLifetime;

};

} // namespace Editor
