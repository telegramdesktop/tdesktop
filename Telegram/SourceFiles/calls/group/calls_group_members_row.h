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

namespace Data {
struct GroupCallParticipant;
} // namespace Data

namespace Ui {
class RippleAnimation;
struct PeerUserpicView;
} // namespace Ui

namespace Calls::Group {

enum class MembersRowStyle : uchar {
	Default,
	Narrow,
	Video,
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
		bool invited = false;
		bool calling = false;
		MembersRowStyle style = MembersRowStyle::Default;
	};
	virtual bool rowIsMe(not_null<PeerData*> participantPeer) = 0;
	virtual bool rowCanMuteMembers() = 0;
	virtual void rowUpdateRow(not_null<MembersRow*> row) = 0;
	virtual void rowScheduleRaisedHandStatusRemove(
		not_null<MembersRow*> row) = 0;
	virtual void rowPaintIcon(
		QPainter &p,
		QRect rect,
		const IconState &state) = 0;
	virtual int rowPaintStatusIcon(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		not_null<MembersRow*> row,
		const IconState &state) = 0;
	virtual bool rowIsNarrow() = 0;
	virtual void rowShowContextMenu(not_null<PeerListRow*> row) = 0;
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
		Invited,
		Calling,
		WithAccess,
	};

	void setAbout(const QString &about);
	void setSkipLevelUpdate(bool value);
	void updateState(const Data::GroupCallParticipant &participant);
	void updateStateInvited(bool calling);
	void updateStateWithAccess();
	void updateLevel(float level);
	void updateBlobAnimation(crl::time now);
	void clearRaisedHandStatus();
	[[nodiscard]] State state() const {
		return _state;
	}
	[[nodiscard]] bool sounding() const {
		return _sounding;
	}
	[[nodiscard]] bool speaking() const {
		return _speaking;
	}
	[[nodiscard]] bool mutedByMe() const {
		return _mutedByMe;
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

	void refreshName(const style::PeerListItem &st) override;

	void rightActionAddRipple(
		QPoint point,
		Fn<void()> updateCallback) override;
	void rightActionStopLastRipple() override;
	QSize rightActionSize() const override;
	bool rightActionDisabled() const override;
	QMargins rightActionMargins() const override;
	void rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) override;

	QString generateName() override;
	QString generateShortName() override;
	PaintRoundImageCallback generatePaintUserpicCallback(
		bool forceRound) override;
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
		QPainter &p,
		QRect iconRect,
		MembersRowStyle style = MembersRowStyle::Default);
	[[nodiscard]] MembersRowDelegate::IconState computeIconState(
		MembersRowStyle style = MembersRowStyle::Default) const;

	void showContextMenu();

private:
	struct BlobsAnimation;
	struct StatusIcon;

	int statusIconWidth(bool skipIcon) const;
	int statusIconHeight() const;
	void paintStatusIcon(
		Painter &p,
		int x,
		int y,
		const style::PeerListItem &st,
		const style::font &font,
		bool selected,
		bool skipIcon);

	void refreshStatus() override;
	void setSounding(bool sounding);
	void setSpeaking(bool speaking);
	void setState(State state);
	void setVolume(int volume);

	void ensureUserpicCache(
		Ui::PeerUserpicView &view,
		int size);
	void paintBlobs(
		Painter &p,
		int x,
		int y,
		int sizew,
		int sizeh, PanelMode mode);
	void paintScaledUserpic(
		Painter &p,
		Ui::PeerUserpicView &userpic,
		int x,
		int y,
		int outerWidth,
		int sizew,
		int sizeh,
		PanelMode mode);

	const not_null<MembersRowDelegate*> _delegate;
	State _state = State::Inactive;
	std::unique_ptr<Ui::RippleAnimation> _actionRipple;
	std::unique_ptr<BlobsAnimation> _blobsAnimation;
	std::unique_ptr<StatusIcon> _statusIcon;
	Ui::Animations::Simple _speakingAnimation; // For gray-red/green icon.
	Ui::Animations::Simple _mutedAnimation; // For gray/red icon.
	Ui::Animations::Simple _activeAnimation; // For icon cross animation.
	Ui::Text::String _about;
	crl::time _speakingLastTime = 0;
	uint64 _raisedHandRating = 0;
	int _volume = Group::kDefaultVolume;
	bool _sounding : 1 = false;
	bool _speaking : 1 = false;
	bool _raisedHandStatus : 1 = false;
	bool _skipLevelUpdate : 1 = false;
	bool _mutedByMe : 1 = false;

};

} // namespace Calls::Group
