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

Calls::~Calls() {
	if (_needWriteSettings) {
		Local::writeUserSettings();
	}
}

void Calls::sectionSaveChanges(FnMut<void()> done) {
	if (_micTester) {
		_micTester.reset();
	}
	done();
}

void Calls::setupContent() {
	using namespace tgvoip;

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto getId = [](const auto &device) {
		return QString::fromStdString(device.id);
	};
	const auto getName = [](const auto &device) {
		return QString::fromStdString(device.displayName);
	};

	const auto currentOutputName = [&] {
		if (Global::CallOutputDeviceID() == qsl("default")) {
			return lang(lng_settings_call_device_default);
		}
		const auto &list = VoIPController::EnumerateAudioOutputs();
		const auto i = ranges::find(
			list,
			Global::CallOutputDeviceID(),
			getId);
		return (i != end(list))
			? getName(*i)
			: Global::CallOutputDeviceID();
	}();

	const auto currentInputName = [&] {
		if (Global::CallInputDeviceID() == qsl("default")) {
			return lang(lng_settings_call_device_default);
		}
		const auto &list = VoIPController::EnumerateAudioInputs();
		const auto i = ranges::find(
			list,
			Global::CallInputDeviceID(),
			getId);
		return (i != end(list))
			? getName(*i)
			: Global::CallInputDeviceID();
	}();

	AddSkip(content);
	AddSubsectionTitle(content, lng_settings_call_section_output);
	AddButtonWithLabel(
		content,
		lng_settings_call_output_device,
		rpl::single(
			currentOutputName
		) | rpl::then(
			_outputNameStream.events()
		),
		st::settingsButton
	)->addClickHandler([=] {
		const auto &devices = VoIPController::EnumerateAudioOutputs();
		const auto options = ranges::view::concat(
			ranges::view::single(lang(lng_settings_call_device_default)),
			devices | ranges::view::transform(getName)
		) | ranges::to_vector;
		const auto i = ranges::find(
			devices,
			Global::CallOutputDeviceID(),
			getId);
		const auto currentOption = (i != end(devices))
			? int(i - begin(devices) + 1)
			: 0;
		const auto save = crl::guard(this, [=](int option) {
			_outputNameStream.fire_copy(options[option]);
			const auto deviceId = option
				? devices[option - 1].id
				: "default";
			Global::SetCallOutputDeviceID(QString::fromStdString(deviceId));
			Local::writeUserSettings();
			if (const auto call = ::Calls::Current().currentCall()) {
				call->setCurrentAudioDevice(false, deviceId);
			}
		});
		Ui::show(Box<SingleChoiceBox>(
			lng_settings_call_output_device,
			options,
			currentOption,
			save));
	});

	const auto outputLabel = content->add(
		object_ptr<Ui::LabelSimple>(
			content,
			st::settingsAudioVolumeLabel),
		st::settingsAudioVolumeLabelPadding);
	const auto outputSlider = content->add(
		object_ptr<Ui::MediaSlider>(
			content,
			st::settingsAudioVolumeSlider),
		st::settingsAudioVolumeSliderPadding);
	const auto updateOutputLabel = [=](int value) {
		const auto percent = QString::number(value);
		outputLabel->setText(
			lng_settings_call_output_volume(lt_percent, percent));
	};
	const auto updateOutputVolume = [=](int value) {
		_needWriteSettings = true;
		updateOutputLabel(value);
		Global::SetCallOutputVolume(value);
		if (const auto call = ::Calls::Current().currentCall()) {
			call->setAudioVolume(false, value / 100.0f);
		}
	};
	outputSlider->resize(st::settingsAudioVolumeSlider.seekSize);
	outputSlider->setPseudoDiscrete(
		201,
		[](int val) { return val; },
		Global::CallOutputVolume(),
		updateOutputVolume);
	updateOutputLabel(Global::CallOutputVolume());

	AddSkip(content);
	AddDivider(content);
	AddSkip(content);
	AddSubsectionTitle(content, lng_settings_call_section_input);
	AddButtonWithLabel(
		content,
		lng_settings_call_input_device,
		rpl::single(
			currentInputName
		) | rpl::then(
			_inputNameStream.events()
		),
		st::settingsButton
	)->addClickHandler([=] {
		const auto &devices = VoIPController::EnumerateAudioInputs();
		const auto options = ranges::view::concat(
			ranges::view::single(lang(lng_settings_call_device_default)),
			devices | ranges::view::transform(getName)
		) | ranges::to_vector;
		const auto i = ranges::find(
			devices,
			Global::CallInputDeviceID(),
			getId);
		const auto currentOption = (i != end(devices))
			? int(i - begin(devices) + 1)
			: 0;
		const auto save = crl::guard(this, [=](int option) {
			_inputNameStream.fire_copy(options[option]);
			const auto deviceId = option
				? devices[option - 1].id
				: "default";
			Global::SetCallInputDeviceID(QString::fromStdString(deviceId));
			Local::writeUserSettings();
			if (_micTester) {
				stopTestingMicrophone();
			}
			if (const auto call = ::Calls::Current().currentCall()) {
				call->setCurrentAudioDevice(true, deviceId);
			}
		});
		Ui::show(Box<SingleChoiceBox>(
			lng_settings_call_input_device,
			options,
			currentOption,
			save));
	});

	const auto inputLabel = content->add(
		object_ptr<Ui::LabelSimple>(
			content,
			st::settingsAudioVolumeLabel),
		st::settingsAudioVolumeLabelPadding);
	const auto inputSlider = content->add(
		object_ptr<Ui::MediaSlider>(
			content,
			st::settingsAudioVolumeSlider),
		st::settingsAudioVolumeSliderPadding);
	const auto updateInputLabel = [=](int value) {
		const auto percent = QString::number(value);
		inputLabel->setText(
			lng_settings_call_input_volume(lt_percent, percent));
	};
	const auto updateInputVolume = [=](int value) {
		_needWriteSettings = true;
		updateInputLabel(value);
		Global::SetCallInputVolume(value);
		::Calls::Call *currentCall = ::Calls::Current().currentCall();
		if (currentCall) {
			currentCall->setAudioVolume(true, value / 100.0f);
		}
	};
	inputSlider->resize(st::settingsAudioVolumeSlider.seekSize);
	inputSlider->setPseudoDiscrete(101,
		[](int val) { return val; },
		Global::CallInputVolume(),
		updateInputVolume);
	updateInputLabel(Global::CallInputVolume());

	AddButton(
		content,
		rpl::single(
			lang(lng_settings_call_test_mic)
		) | rpl::then(
			_micTestTextStream.events()
		),
		st::settingsButton
	)->addClickHandler([=] {
		if (!_micTester) {
			requestPermissionAndStartTestingMicrophone();
		} else {
			stopTestingMicrophone();
		}
	});

	_micTestLevel = content->add(
		object_ptr<Ui::LevelMeter>(
			content,
			st::defaultLevelMeter),
		st::settingsLevelMeterPadding);
	_micTestLevel->resize(QSize(0, st::defaultLevelMeter.height));

	_levelUpdateTimer.setCallback([=] {
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
		if (const auto call = ::Calls::Current().currentCall()) {
			call->setAudioDuckingEnabled(enabled);
		}
	}, content->lifetime());
#endif // Q_OS_MAC

	AddButton(
		content,
		lng_settings_call_open_system_prefs,
		st::settingsButton
	)->addClickHandler([] {
		const auto opened = Platform::OpenSystemSettings(
			Platform::SystemSettingsType::Audio);
		if (!opened) {
			Ui::show(Box<InformBox>(lang(lng_linux_no_audio_prefs)));
		}
	});
	AddSkip(content);

	Ui::ResizeFitChild(this, content);
}

void Calls::requestPermissionAndStartTestingMicrophone() {
	const auto status = Platform::GetPermissionStatus(
		Platform::PermissionType::Microphone);
	if (status == Platform::PermissionStatus::Granted) {
		startTestingMicrophone();
	} else if (status == Platform::PermissionStatus::CanRequest) {
		const auto startTestingChecked = crl::guard(this, [=](
				Platform::PermissionStatus status) {
			if (status == Platform::PermissionStatus::Granted) {
				crl::on_main(crl::guard(this, [=] {
					startTestingMicrophone();
				}));
			}
		});
		Platform::RequestPermission(
			Platform::PermissionType::Microphone,
			startTestingChecked);
	} else {
		const auto showSystemSettings = [] {
			Platform::OpenSystemSettingsForPermission(
				Platform::PermissionType::Microphone);
			Ui::hideLayer();
		};
		Ui::show(Box<ConfirmBox>(
			lang(lng_no_mic_permission),
			lang(lng_menu_settings),
			showSystemSettings));
	}
}

void Calls::startTestingMicrophone() {
	_micTestTextStream.fire(lang(lng_settings_call_stop_mic_test));
	_levelUpdateTimer.callEach(50);
	_micTester = std::make_unique<tgvoip::AudioInputTester>(
		Global::CallInputDeviceID().toStdString());
	if (_micTester->Failed()) {
		Ui::show(Box<InformBox>(lang(lng_call_error_audio_io)));
		stopTestingMicrophone();
	}
}

void Calls::stopTestingMicrophone() {
	_micTestTextStream.fire(lang(lng_settings_call_test_mic));
	_levelUpdateTimer.cancel();
	_micTester.reset();
	_micTestLevel->setValue(0.0f);
}

} // namespace Settings

