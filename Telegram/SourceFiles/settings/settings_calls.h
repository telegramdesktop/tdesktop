/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_common.h"
#include "ui/effects/animations.h"
#include "base/timer.h"

namespace Calls {
class Call;
} // namespace Calls

namespace Ui {
class LevelMeter;
} // namespace Ui

namespace Webrtc {
class AudioInputTester;
} // namespace Webrtc

namespace Settings {

class Calls : public Section {
public:
	Calls(QWidget *parent, not_null<Window::SessionController*> controller);
	~Calls();

	void sectionSaveChanges(FnMut<void()> done) override;

private:
	void setupContent();
	void requestPermissionAndStartTestingMicrophone();
	void startTestingMicrophone();
	//void stopTestingMicrophone();

	const not_null<Window::SessionController*> _controller;
	rpl::event_stream<QString> _cameraNameStream;
	rpl::event_stream<QString> _outputNameStream;
	rpl::event_stream<QString> _inputNameStream;
	//rpl::event_stream<QString> _micTestTextStream;
	bool _needWriteSettings = false;
	std::unique_ptr<Webrtc::AudioInputTester> _micTester;
	Ui::LevelMeter *_micTestLevel = nullptr;
	float _micLevel = 0.;
	Ui::Animations::Simple _micLevelAnimation;
	base::Timer _levelUpdateTimer;

};

} // namespace Settings

