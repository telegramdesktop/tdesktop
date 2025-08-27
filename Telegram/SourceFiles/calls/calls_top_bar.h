/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "base/timer.h"
#include "base/object_ptr.h"
#include "base/unique_qptr.h"
#include "ui/effects/animations.h"
#include "ui/effects/gradient.h"
#include "ui/rp_widget.h"

namespace Ui {
class IconButton;
class AbstractButton;
class LabelSimple;
class FlatLabel;
struct GroupCallUser;
class GroupCallUserpics;
class Show;
} // namespace Ui

namespace Main {
class Session;
} // namespace Main

namespace Calls {

class Call;
class GroupCall;
class SignalBars;
class Mute;
enum class MuteState;
enum class BarState;

class TopBar : public Ui::RpWidget {
public:
	TopBar(
		QWidget *parent,
		Call *call,
		std::shared_ptr<Ui::Show> show);
	TopBar(
		QWidget *parent,
		GroupCall *call,
		std::shared_ptr<Ui::Show> show);
	~TopBar();

	void initBlobsUnder(
		QWidget *blobsParent,
		rpl::producer<QRect> barGeometry);

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	struct User;

	TopBar(
		QWidget *parent,
		std::shared_ptr<Ui::Show> show,
		Call *call,
		GroupCall *groupCall);

	void initControls();
	void setupInitialBrush();
	void updateInfoLabels();
	void setInfoLabels();
	void updateDurationText();
	void updateControlsGeometry();
	void startDurationUpdateTimer(crl::time currentDuration);
	void setMuted(bool mute);

	void subscribeToMembersChanges(not_null<GroupCall*> call);
	void updateUserpics();

	const base::weak_ptr<Call> _call;
	const base::weak_ptr<GroupCall> _groupCall;
	const std::shared_ptr<Ui::Show> _show;

	bool _muted = false;
	std::vector<Ui::GroupCallUser> _users;
	int _usersCount = 0;
	std::unique_ptr<Ui::GroupCallUserpics> _userpics;
	int _userpicsWidth = 0;
	object_ptr<Ui::LabelSimple> _durationLabel;
	object_ptr<SignalBars> _signalBars;
	object_ptr<Ui::FlatLabel> _fullInfoLabel;
	object_ptr<Ui::FlatLabel> _shortInfoLabel;
	object_ptr<Ui::LabelSimple> _hangupLabel;
	object_ptr<Mute> _mute;
	object_ptr<Ui::AbstractButton> _info;
	object_ptr<Ui::IconButton> _hangup;
	base::unique_qptr<Ui::RpWidget> _blobs;

	rpl::variable<bool> _isGroupConnecting = false;

	std::vector<not_null<PeerData*>> _conferenceFirstUsers;
	int _conferenceUsersCount = 0;

	QBrush _groupBrush;
	anim::linear_gradients<BarState> _gradients;
	Ui::Animations::Simple _switchStateAnimation;
	Fn<void(float64)> _switchStateCallback;

	base::Timer _updateDurationTimer;

};

} // namespace Calls
