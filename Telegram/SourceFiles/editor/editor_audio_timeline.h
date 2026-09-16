/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "editor/editor_trim_timeline.h"
#include "ui/effects/animations.h"

namespace Media::Audio {
struct Waveform;
} // namespace Media::Audio

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Editor {

class SegmentPlayer;
class TimelineSeeker;
struct AudioTrack;

class AudioTimeline final : public TrimTimeline {
public:
	AudioTimeline(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<AudioTrack> track);
	~AudioTimeline();

	[[nodiscard]] const std::shared_ptr<AudioTrack> &track() const;

	void setPlaying(bool playing);
	void refreshTrim();
	void refreshVolume();

	[[nodiscard]] rpl::producer<> removeRequests() const;

private:
	void paintStrip(QPainter &p, const QRect &strip) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

	void loadWaveform();
	void paintWaveform(QPainter &p, const QRect &strip);
	void paintText(QPainter &p, const QRect &strip);
	void paintTextLine(QPainter &p, int x, int centerY, int width);

	const std::shared_ptr<AudioTrack> _track;
	const QString _title;
	const QString _performer;

	std::unique_ptr<SegmentPlayer> _player;
	std::unique_ptr<TimelineSeeker> _seeker;

	std::shared_ptr<Media::Audio::Waveform> _waveform;
	std::shared_ptr<std::atomic<bool>> _waveformCancel;
	int _waveformMax = 0;
	Ui::Animations::Simple _loadedAnimation;

	base::unique_qptr<Ui::PopupMenu> _menu;
	rpl::event_stream<> _removeRequests;

};

} // namespace Editor
