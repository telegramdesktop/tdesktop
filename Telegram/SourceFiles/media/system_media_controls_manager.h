/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_audio_msg_id.h"
#include "media/player/media_player_instance.h"
#include "media/media_common.h"
#include "media/system_media_controls_video.h"
#include "ui/userpic_view.h"

namespace base::Platform {
class SystemMediaControls;
} // namespace base::Platform

namespace Data {
class DocumentMedia;
} // namespace Data

namespace Window {
class Controller;
} // namespace Window

namespace Media::Streaming {
class Instance;
} // namespace Media::Streaming

namespace Media {

class SystemMediaControlsManager final : public SystemMediaControlsVideoSink {
public:
	SystemMediaControlsManager();
	~SystemMediaControlsManager();

	static bool Supported();

	void videoStart(
		not_null<SystemMediaControlsVideoDelegate*> delegate,
		VideoState state) override;
	void videoUpdate(VideoState state) override;
	void videoSetThumbnail(const QImage &thumbnail) override;
	void videoFinish(
		not_null<SystemMediaControlsVideoDelegate*> delegate) override;

private:
	[[nodiscard]] float64 lookupPlaybackRate() const;
	void applyPlayerTrack(AudioMsgId::Type audioType);
	void syncPlayerStateToControls();

	const std::unique_ptr<base::Platform::SystemMediaControls> _controls;

	std::vector<std::shared_ptr<Data::DocumentMedia>> _cachedMediaView;
	Ui::PeerUserpicView _cachedUserpicView;
	std::unique_ptr<Streaming::Instance> _streamed;
	AudioMsgId _lastAudioMsgId;
	OrderMode _lastOrderMode = OrderMode::Default;
	bool _inited = false;
	SystemMediaControlsVideoDelegate *_videoDelegate = nullptr;
	VideoState _videoState;

	rpl::lifetime _lifetimeDownload;
	rpl::lifetime _lifetime;
};

} // namespace Media
