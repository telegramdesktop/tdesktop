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

class TopBar : public Ui::RpWidget {
public:
	TopBar(QWidget *parent, const base::weak_ptr<Call> &call);
	TopBar(QWidget *parent, const base::weak_ptr<GroupCall> &call);
	~TopBar();

	void initBlobsUnder(
		QWidget *blobsParent,
		rpl::producer<QRect> barGeometry);

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	TopBar(
		QWidget *parent,
		const base::weak_ptr<Call> &call,
		const base::weak_ptr<GroupCall> &groupCall);

	void initControls();
	void updateInfoLabels();
	void setInfoLabels();
	void updateDurationText();
	void updateControlsGeometry();
	void startDurationUpdateTimer(crl::time currentDuration);
	void setMuted(bool mute);

	void subscribeToMembersChanges(not_null<GroupCall*> call);

	const base::weak_ptr<Call> _call;
	const base::weak_ptr<GroupCall> _groupCall;

	bool _muted = false;
	QImage _userpics;
	object_ptr<Ui::LabelSimple> _durationLabel;
	object_ptr<SignalBars> _signalBars;
	object_ptr<Ui::FlatLabel> _fullInfoLabel;
	object_ptr<Ui::FlatLabel> _shortInfoLabel;
	object_ptr<Ui::LabelSimple> _hangupLabel;
	object_ptr<Mute> _mute;
	object_ptr<Ui::AbstractButton> _info;
	object_ptr<Ui::IconButton> _hangup;
	base::unique_qptr<Ui::RpWidget> _blobs;

	QBrush _groupBrush;
	anim::linear_gradients<MuteState> _gradients;
	Ui::Animations::Simple _switchStateAnimation;

	base::Timer _updateDurationTimer;

};

} // namespace Calls
