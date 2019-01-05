/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_common.h"
#include "base/timer.h"

namespace Calls {
	class Call;
} // namespace Calls

namespace Ui {
	class LevelMeter;
}

namespace tgvoip {
	class AudioInputTester;
}

namespace Settings {

class Calls : public Section {
public:
	explicit Calls(QWidget *parent, UserData *self = nullptr);
	virtual ~Calls();
	virtual void sectionSaveChanges(FnMut<void()> done) override;

private:
	void setupContent();
	void requestPermissionAndStartTestingMicrophone();
	void startTestingMicrophone();
	void stopTestingMicrophone();

	rpl::event_stream<QString> _outputNameStream;
	rpl::event_stream<QString> _inputNameStream;
	rpl::event_stream<QString> _micTestTextStream;
	bool _needWriteSettings = false;
	std::unique_ptr<tgvoip::AudioInputTester> _micTester;
	Button *_micTestButton = nullptr;
	Ui::LevelMeter *_micTestLevel = nullptr;
	base::Timer _levelUpdateTimer;
};

} // namespace Settings

