/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "base/timer.h"
#include "ui/rp_widget.h"

namespace Ui {
class IconButton;
class AbstractButton;
class LabelSimple;
class FlatLabel;
} // namespace Ui

namespace Calls {

class Call;

class TopBar : public Ui::RpWidget, private base::Subscriber {
public:
	TopBar(QWidget *parent, const base::weak_ptr<Call> &call);

	~TopBar();

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void initControls();
	void updateInfoLabels();
	void setInfoLabels();
	void updateDurationText();
	void updateControlsGeometry();
	void startDurationUpdateTimer(TimeMs currentDuration);
	void setMuted(bool mute);

	base::weak_ptr<Call> _call;

	bool _muted = false;
	object_ptr<Ui::LabelSimple> _durationLabel;
	object_ptr<Ui::FlatLabel> _fullInfoLabel;
	object_ptr<Ui::FlatLabel> _shortInfoLabel;
	object_ptr<Ui::LabelSimple> _hangupLabel;
	object_ptr<Ui::IconButton> _mute;
	object_ptr<Ui::AbstractButton> _info;
	object_ptr<Ui::IconButton> _hangup;

	base::Timer _updateDurationTimer;

};

} // namespace Calls
