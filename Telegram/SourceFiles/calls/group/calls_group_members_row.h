/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/peer_list_box.h"
#include "calls/group/calls_group_common.h"

class PeerData;
class Painter;

namespace Webrtc {
class VideoTrack;
} // namespace Webrtc

namespace Data {
struct GroupCallParticipant;
} // namespace Data

namespace Ui {
class RippleAnimation;
} // namespace Ui

namespace Calls::Group {

enum class MembersRowStyle {
	None,
	Userpic,
	Video,
	LargeVideo,
};

class MembersRow;
class MembersRowDelegate {
public:
	struct IconState {
		float64 speaking = 0.;
		float64 active = 0.;
		float64 muted = 0.;
		bool mutedByMe = false;
		bool raisedHand = false;
		MembersRowStyle style = MembersRowStyle::None;
	};
	virtual bool rowIsMe(not_null<PeerData*> participantPeer) = 0;
	virtual bool rowCanMuteMembers() = 0;
	virtual void rowUpdateRow(not_null<MembersRow*> row) = 0;
	virtual void rowScheduleRaisedHandStatusRemove(not_null<MembersRow*> row) = 0;
	virtual void rowPaintIcon(
		Painter &p,
		QRect rect,
		const IconState &state) = 0;
	virtual void rowPaintNarrowBackground(
		Painter &p,
		int x,
		int y,
		bool selected) = 0;
	virtual void rowPaintNarrowBorder(
		Painter &p,
		int x,
		int y,
		not_null<MembersRow*> row) = 0;
	virtual void rowPaintNarrowShadow(
		Painter &p,
		int x,
		int y,
		int sizew,
		int sizeh) = 0;
};

class MembersRow final : public PeerListRow {
public:
	MembersRow(
		not_null<MembersRowDelegate*> delegate,
		not_null<PeerData*> participantPeer);
	~MembersRow();

	enum class State {
		Active,
		Inactive,
		Muted,
		RaisedHand,
		MutedByMe,
		Invited,
	};

	void setAbout(const QString &about);
	void setSkipLevelUpdate(bool value);
	void updateState(const Data::GroupCallParticipant *participant);
	void updateLevel(float level);
	void updateBlobAnimation(crl::time now);
	void clearRaisedHandStatus();
	[[nodiscard]] State state() const {
		return _state;
	}
	[[nodiscard]] uint32 ssrc() const {
		return _ssrc;
	}
	[[nodiscard]] bool sounding() const {
		return _sounding;
	}
	[[nodiscard]] bool speaking() const {
		return _speaking;
	}
	[[nodiscard]] crl::time speakingLastTime() const {
		return _speakingLastTime;
	}
	[[nodiscard]] int volume() const {
		return _volume;
	}
	[[nodiscard]] uint64 raisedHandRating() const {
		return _raisedHandRating;
	}

	[[nodiscard]] not_null<Webrtc::VideoTrack*> createVideoTrack(
		const std::string &endpoint);
	void clearVideoTrack();
	[[nodiscard]] const std::string &videoTrackEndpoint() const;
	void setVideoTrack(not_null<Webrtc::VideoTrack*> track);

	void addActionRipple(QPoint point, Fn<void()> updateCallback) override;
	void stopLastActionRipple() override;

	void refreshName(const style::PeerListItem &st) override;

	QSize actionSize() const override;
	bool actionDisabled() const override;
	QMargins actionMargins() const override;
	void paintAction(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) override;

	PaintRoundImageCallback generatePaintUserpicCallback() override;
	void paintComplexUserpic(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int sizew,
		int sizeh,
		PanelMode mode,
		bool selected = false);

	void paintStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		bool selected) override;
	void paintComplexStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		bool selected,
		MembersRowStyle style);
	void paintMuteIcon(
		Painter &p,
		QRect iconRect,
		MembersRowStyle style = MembersRowStyle::None);

private:
	struct BlobsAnimation;
	struct StatusIcon;

	int statusIconWidth() const;
	int statusIconHeight() const;
	void paintStatusIcon(
		Painter &p,
		const style::PeerListItem &st,
		const style::font &font,
		bool selected);

	void refreshStatus() override;
	void setSounding(bool sounding);
	void setSpeaking(bool speaking);
	void setState(State state);
	void setSsrc(uint32 ssrc);
	void setVolume(int volume);

	void ensureUserpicCache(
		std::shared_ptr<Data::CloudImageView> &view,
		int size);
	bool paintVideo(
		Painter &p,
		int x,
		int y,
		int sizew,
		int sizeh,
		PanelMode mode);
	[[nodiscard]] static std::tuple<int, int, int> UserpicInNarrowMode(
		int x,
		int y,
		int sizew,
		int sizeh);
	void paintBlobs(
		Painter &p,
		int x,
		int y,
		int sizew,
		int sizeh, PanelMode mode);
	void paintScaledUserpic(
		Painter &p,
		std::shared_ptr<Data::CloudImageView> &userpic,
		int x,
		int y,
		int outerWidth,
		int sizew,
		int sizeh,
		PanelMode mode);
	void paintNarrowName(
		Painter &p,
		int x,
		int y,
		int sizew,
		int sizeh,
		MembersRowStyle style);
	[[nodiscard]] MembersRowDelegate::IconState computeIconState(
		MembersRowStyle style = MembersRowStyle::None) const;

	const not_null<MembersRowDelegate*> _delegate;
	State _state = State::Inactive;
	std::unique_ptr<Ui::RippleAnimation> _actionRipple;
	std::unique_ptr<BlobsAnimation> _blobsAnimation;
	std::unique_ptr<StatusIcon> _statusIcon;
	std::unique_ptr<Webrtc::VideoTrack> _videoTrack;
	Webrtc::VideoTrack *_videoTrackShown = nullptr;
	std::string _videoTrackEndpoint;
	rpl::lifetime _videoTrackLifetime; // #TODO calls move to unique_ptr.
	Ui::Animations::Simple _speakingAnimation; // For gray-red/green icon.
	Ui::Animations::Simple _mutedAnimation; // For gray/red icon.
	Ui::Animations::Simple _activeAnimation; // For icon cross animation.
	Ui::Text::String _narrowName;
	QString _aboutText;
	crl::time _speakingLastTime = 0;
	uint64 _raisedHandRating = 0;
	uint32 _ssrc = 0;
	int _volume = Group::kDefaultVolume;
	bool _sounding = false;
	bool _speaking = false;
	bool _raisedHandStatus = false;
	bool _skipLevelUpdate = false;

};

} // namespace Calls::Group
