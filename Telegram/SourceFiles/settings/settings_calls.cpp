/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_calls.h"

#include "settings/settings_common.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/level_meter.h"
#include "info/profile/info_profile_button.h"
#include "boxes/single_choice_box.h"
#include "boxes/confirm_box.h"
#include "platform/platform_specific.h"
#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "layout.h"
#include "styles/style_settings.h"
#include "ui/widgets/continuous_sliders.h"
#include "calls/calls_instance.h"

#ifdef slots
#undef slots
#define NEED_TO_RESTORE_SLOTS
#endif // slots

#include <VoIPController.h>

#ifdef NEED_TO_RESTORE_SLOTS
#define slots Q_SLOTS
#undef NEED_TO_RESTORE_SLOTS
#endif // NEED_TO_RESTORE_SLOTS

namespace Settings {

Calls::Calls(QWidget *parent, UserData *self)
: Section(parent) {
	setupContent();
}

Calls::~Calls(){
	if (_needWriteSettings) {
		Local::writeUserSettings();
	}
}

void Calls::sectionSaveChanges(FnMut<void()> done){
	if (_micTester) {
		_micTester.reset();
	}
	done();
}

void Calls::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	QString currentOutputName;
	if (Global::CallOutputDeviceID() == qsl("default")) {
		currentOutputName = lang(lng_settings_call_device_default);
	} else {
		std::vector<tgvoip::AudioOutputDevice> outputDevices = tgvoip::VoIPController::EnumerateAudioOutputs();
		currentOutputName=Global::CallOutputDeviceID();
		for (auto &dev : outputDevices) {
			if (QString::fromUtf8(dev.id.c_str()) == Global::CallOutputDeviceID()) {
				currentOutputName = QString::fromUtf8(dev.displayName.c_str());
				break;
			}
		}
	}

	QString currentInputName;
	if (Global::CallInputDeviceID() == qsl("default")) {
		currentInputName = lang(lng_settings_call_device_default);
	} else {
		std::vector<tgvoip::AudioInputDevice> inputDevices = tgvoip::VoIPController::EnumerateAudioInputs();
		currentInputName = Global::CallInputDeviceID();
		for (auto &dev : inputDevices) {
			if (QString::fromUtf8(dev.id.c_str()) == Global::CallInputDeviceID()) {
				currentInputName = QString::fromUtf8(dev.displayName.c_str());
				break;
			}
		}
	}

	AddSkip(content);
	AddSubsectionTitle(content, lng_settings_call_section_output);
	const auto outputButton = AddButtonWithLabel(
		content,
		lng_settings_call_output_device,
		rpl::single(currentOutputName) | rpl::then(_outputNameStream.events()),
		st::settingsButton);
	outputButton->addClickHandler([this] {
		int selectedOption = 0;
		std::vector<tgvoip::AudioOutputDevice> devices = tgvoip::VoIPController::EnumerateAudioOutputs();
		std::vector<QString> options;
		options.push_back(lang(lng_settings_call_device_default));
		int i = 1;
		for (auto &device : devices) {
			QString displayName = QString::fromUtf8(device.displayName.c_str());
			options.push_back(displayName);
			if (QString::fromUtf8(device.id.c_str()) == Global::CallOutputDeviceID()) {
				selectedOption = i;
			}
			i++;
		}
		const auto save = crl::guard(this, [=](int selectedOption) {
			QString name = options[selectedOption];
			_outputNameStream.fire(std::move(name));
			std::string selectedDeviceID;
			if (selectedOption == 0) {
				selectedDeviceID = "default";
			} else {
				selectedDeviceID = devices[selectedOption-1].id;
			}
			Global::SetCallOutputDeviceID(QString::fromStdString(selectedDeviceID));
			Local::writeUserSettings();

			::Calls::Call *currentCall = ::Calls::Current().currentCall();
			if (currentCall) {
				currentCall->setCurrentAudioDevice(false, selectedDeviceID);
			}
		});
		Ui::show(Box<SingleChoiceBox>(lng_settings_call_output_device, options, selectedOption, save));
	});

	const auto outputLabel = content->add(object_ptr<Ui::LabelSimple>(content, st::settingsAudioVolumeLabel), st::settingsAudioVolumeLabelPadding);
	const auto outputSlider = content->add(object_ptr<Ui::MediaSlider>(content, st::settingsAudioVolumeSlider), st::settingsAudioVolumeSliderPadding);
	auto updateOutputLabel = [outputLabel](int value){
		QString percent = QString::number(value);
		outputLabel->setText(lng_settings_call_output_volume(lt_percent, percent));
	};
	outputSlider->resize(st::settingsAudioVolumeSlider.seekSize);
	outputSlider->setPseudoDiscrete(
		201,
		[](int val){
			return val;
		},
		Global::CallOutputVolume(),
		[updateOutputLabel, this](int value) {
			_needWriteSettings = true;
			updateOutputLabel(value);
			Global::SetCallOutputVolume(value);
			::Calls::Call* currentCall = ::Calls::Current().currentCall();
			if (currentCall) {
				currentCall->setAudioVolume(false, value/100.0f);
			}
	});
	updateOutputLabel(Global::CallOutputVolume());

	AddSkip(content);
	AddDivider(content);
	AddSkip(content);
	AddSubsectionTitle(content, lng_settings_call_section_input);
	const auto inputButton = AddButtonWithLabel(
		content,
		lng_settings_call_input_device,
		rpl::single(currentInputName) | rpl::then(_inputNameStream.events()),
		st::settingsButton);
	inputButton->addClickHandler([this] {
		int selectedOption = 0;
		std::vector<tgvoip::AudioInputDevice> devices = tgvoip::VoIPController::EnumerateAudioInputs();
		std::vector<QString> options;
		options.push_back(lang(lng_settings_call_device_default));
		int i = 1;
		for (auto &device : devices) {
			QString displayName = QString::fromUtf8(device.displayName.c_str());
			options.push_back(displayName);
			if(QString::fromUtf8(device.id.c_str()) == Global::CallInputDeviceID())
				selectedOption = i;
			i++;
		}
		const auto save = crl::guard(this, [=](int selectedOption) {
			QString name=options[selectedOption];
			_inputNameStream.fire(std::move(name));
			std::string selectedDeviceID;
			if (selectedOption == 0) {
				selectedDeviceID = "default";
			} else {
				selectedDeviceID = devices[selectedOption - 1].id;
			}
			Global::SetCallInputDeviceID(QString::fromUtf8(selectedDeviceID.c_str()));
			Local::writeUserSettings();
			if (_micTester) {
				stopTestingMicrophone();
			}
			::Calls::Call *currentCall = ::Calls::Current().currentCall();
			if(currentCall){
				currentCall->setCurrentAudioDevice(true, selectedDeviceID);
			}
		});
		Ui::show(Box<SingleChoiceBox>(lng_settings_call_input_device, options, selectedOption, save));
	});

	const auto inputLabel = content->add(object_ptr<Ui::LabelSimple>(content, st::settingsAudioVolumeLabel), st::settingsAudioVolumeLabelPadding);
	const auto inputSlider = content->add(object_ptr<Ui::MediaSlider>(content, st::settingsAudioVolumeSlider), st::settingsAudioVolumeSliderPadding);
	auto updateInputLabel = [inputLabel](int value){
		QString percent = QString::number(value);
		inputLabel->setText(lng_settings_call_input_volume(lt_percent, percent));
	};
	inputSlider->resize(st::settingsAudioVolumeSlider.seekSize);
	inputSlider->setPseudoDiscrete(101,
		[](int val){
			return val;
		},
		Global::CallInputVolume(),
		[updateInputLabel, this](int value) {
			_needWriteSettings = true;
			updateInputLabel(value);
			Global::SetCallInputVolume(value);
			::Calls::Call *currentCall = ::Calls::Current().currentCall();
			if (currentCall) {
				currentCall->setAudioVolume(true, value / 100.0f);
			}
		});
	updateInputLabel(Global::CallInputVolume());

	_micTestButton=AddButton(content, rpl::single(lang(lng_settings_call_test_mic)) | rpl::then(_micTestTextStream.events()), st::settingsButton);

	_micTestLevel=content->add(object_ptr<Ui::LevelMeter>(content, st::defaultLevelMeter), st::settingsLevelMeterPadding);
	_micTestLevel->resize(QSize(0, st::defaultLevelMeter.height));

	_micTestButton->addClickHandler([this]{
		if (!_micTester) {
			requestPermissionAndStartTestingMicrophone();
		} else {
			stopTestingMicrophone();
		}
	});
	_levelUpdateTimer.setCallback([this](){
		_micTestLevel->setValue(_micTester->GetAndResetLevel());
	});

	AddSkip(content);
	AddDivider(content);
	AddSkip(content);
	AddSubsectionTitle(content, lng_settings_call_section_other);

#ifdef Q_OS_MAC
	AddButton(
		content,
		lng_settings_call_audio_ducking,
		st::settingsButton
	)->toggleOn(
		rpl::single(Global::CallAudioDuckingEnabled())
	)->toggledValue() | rpl::filter([](bool enabled) {
		return (enabled != Global::CallAudioDuckingEnabled());
	}) | rpl::start_with_next([](bool enabled) {
		Global::SetCallAudioDuckingEnabled(enabled);
		Local::writeUserSettings();
		::Calls::Call *currentCall = ::Calls::Current().currentCall();
		if (currentCall) {
			currentCall->setAudioDuckingEnabled(enabled);
		}
	}, content->lifetime());
#endif // Q_OS_MAC

	const auto systemSettingsButton=AddButton(content, lng_settings_call_open_system_prefs, st::settingsButton);
	systemSettingsButton->addClickHandler([]{
		if (!Platform::OpenSystemSettings(Platform::SystemSettingsType::Audio)) {
			Ui::show(Box<InformBox>(lang(lng_linux_no_audio_prefs)));
		}
	});
	AddSkip(content);

	Ui::ResizeFitChild(this, content);
}

void Calls::requestPermissionAndStartTestingMicrophone(){
	Platform::PermissionStatus status = Platform::GetPermissionStatus(Platform::PermissionType::Microphone);
	if (status == Platform::PermissionStatus::Granted) {
		startTestingMicrophone();
	} else if (status == Platform::PermissionStatus::CanRequest) {
		Platform::RequestPermission(Platform::PermissionType::Microphone, crl::guard(this, [this](Platform::PermissionStatus status) {
			if (status == Platform::PermissionStatus::Granted) {
				crl::on_main(crl::guard(this, [this]{
					startTestingMicrophone();
				}));
			}
		}));
	} else {
		Ui::show(Box<ConfirmBox>(lang(lng_no_mic_permission), lang(lng_menu_settings), crl::guard(this, [] {
			Platform::OpenSystemSettingsForPermission(Platform::PermissionType::Microphone);
			Ui::hideLayer();
		})));
	}
}

void Calls::startTestingMicrophone(){
	_micTestTextStream.fire(lang(lng_settings_call_stop_mic_test));
	_levelUpdateTimer.callEach(50);
	_micTester = std::make_unique<tgvoip::AudioInputTester>(Global::CallInputDeviceID().toStdString());
	if (_micTester->Failed()) {
		Ui::show(Box<InformBox>(lang(lng_call_error_audio_io)));
		stopTestingMicrophone();
	}
}

void Calls::stopTestingMicrophone(){
	_micTestTextStream.fire(lang(lng_settings_call_test_mic));
	_levelUpdateTimer.cancel();
	_micTester.reset();
	_micTestLevel->setValue(0.0f);
}

} // namespace Settings

