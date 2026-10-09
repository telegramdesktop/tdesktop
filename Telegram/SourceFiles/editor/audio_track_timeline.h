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

class AudioTimeline;
struct AudioTrack;

class AudioTrackTimeline final : public Ui::RpWidget {
public:
	explicit AudioTrackTimeline(not_null<QWidget*> parent);
	~AudioTrackTimeline();

	void setTrack(std::shared_ptr<AudioTrack> track);
	void refreshTrim();
	void commitPendingEdit();
	void refreshVolume();

	[[nodiscard]] rpl::producer<> removeRequests() const;

	[[nodiscard]] rpl::producer<crl::time> lengthChanges() const;

	int resizeGetHeight(int newWidth) override;

private:
	std::shared_ptr<AudioTrack> _track;
	base::unique_qptr<AudioTimeline> _timeline;
	rpl::event_stream<> _removeRequests;
	rpl::event_stream<crl::time> _lengthChanges;

};

} // namespace Editor
