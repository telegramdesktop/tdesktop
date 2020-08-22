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
#include "ui/widgets/buttons.h"
#include "boxes/single_choice_box.h"
#include "boxes/confirm_box.h"
#include "platform/platform_specific.h"
#include "main/main_session.h"
#include "lang/lang_keys.h"
#include "layout.h"
#include "styles/style_settings.h"
#include "ui/widgets/continuous_sliders.h"
#include "window/window_session_controller.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "calls/calls_instance.h"
#include "calls/calls_video_bubble.h"
#include "webrtc/webrtc_media_devices.h"
#include "webrtc/webrtc_video_track.h"
#include "webrtc/webrtc_audio_input_tester.h"
#include "tgcalls/VideoCaptureInterface.h"
#include "facades.h"
#include "styles/style_layers.h"

namespace Settings {
namespace {

constexpr auto kMicTestUpdateInterval = crl::time(100);
constexpr auto kMicTestAnimationDuration = crl::time(200);

} // namespace

Calls::Calls(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent)
, _controller(controller) {
	setupContent();
	requestPermissionAndStartTestingMicrophone();
}

Calls::~Calls() {
	if (_needWriteSettings) {
		Core::App().saveSettingsDelayed();
	}
}

void Calls::sectionSaveChanges(FnMut<void()> done) {
	if (_micTester) {
		_micTester.reset();
	}
	done();
}

void Calls::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto getId = [](const auto &device) {
		return device.id;
	};
	const auto getName = [](const auto &device) {
		return device.name;
	};

	const auto &settings = Core::App().settings();

	const auto currentOutputName = [&] {
		const auto list = Webrtc::GetAudioOutputList();
		const auto i = ranges::find(
			list,
			settings.callOutputDeviceId(),
			getId);
		return (i != end(list))
			? getName(*i)
			: tr::lng_settings_call_device_default(tr::now);
	}();

	const auto currentInputName = [&] {
		const auto list = Webrtc::GetAudioInputList();
		const auto i = ranges::find(
			list,
			settings.callInputDeviceId(),
			getId);
		return (i != end(list))
			? getName(*i)
			: tr::lng_settings_call_device_default(tr::now);
	}();

	const auto cameras = Webrtc::GetVideoInputList();
	if (!cameras.empty()) {
		const auto hasCall = (Core::App().calls().currentCall() != nullptr);

		auto capturerOwner = Core::App().calls().getVideoCapture();
		const auto capturer = capturerOwner.get();
		content->lifetime().add([owner = std::move(capturerOwner)]{});

		const auto track = content->lifetime().make_state<Webrtc::VideoTrack>(
			(hasCall
				? Webrtc::VideoState::Inactive
				: Webrtc::VideoState::Active));

		const auto currentCameraName = [&] {
			const auto i = ranges::find(
				cameras,
				settings.callVideoInputDeviceId(),
				getId);
			return (i != end(cameras))
				? getName(*i)
				: tr::lng_settings_call_device_default(tr::now);
		}();

		AddSkip(content);
		AddSubsectionTitle(content, tr::lng_settings_call_camera());
		AddButtonWithLabel(
			content,
			tr::lng_settings_call_input_device(),
			rpl::single(
				currentCameraName
			) | rpl::then(
				_cameraNameStream.events()
			),
			st::settingsButton
		)->addClickHandler([=] {
			const auto &devices = Webrtc::GetVideoInputList();
			const auto options = ranges::view::concat(
				ranges::view::single(tr::lng_settings_call_device_default(tr::now)),
				devices | ranges::view::transform(getName)
			) | ranges::to_vector;
			const auto i = ranges::find(
				devices,
				Core::App().settings().callVideoInputDeviceId(),
				getId);
			const auto currentOption = (i != end(devices))
				? int(i - begin(devices) + 1)
				: 0;
			const auto save = crl::guard(this, [=](int option) {
				_cameraNameStream.fire_copy(options[option]);
				const auto deviceId = option
					? devices[option - 1].id
					: "default";
				capturer->switchToDevice(deviceId.toStdString());
				Core::App().settings().setCallVideoInputDeviceId(deviceId);
				Core::App().saveSettingsDelayed();
				if (const auto call = Core::App().calls().currentCall()) {
					call->setCurrentVideoDevice(deviceId);
				}
			});
			Ui::show(Box<SingleChoiceBox>(
				tr::lng_settings_call_camera(),
				options,
				currentOption,
				save));
		});
		const auto bubbleWrap = content->add(object_ptr<Ui::RpWidget>(content));
		const auto bubble = content->lifetime().make_state<::Calls::VideoBubble>(
			bubbleWrap,
			track);
		const auto padding = st::settingsButton.padding.left();
		const auto top = st::boxRoundShadow.extend.top();
		const auto bottom = st::boxRoundShadow.extend.bottom();

		bubbleWrap->widthValue(
		) | rpl::filter([=](int width) {
			return (width > 2 * padding + 1);
		}) | rpl::start_with_next([=](int width) {
			const auto use = (width - 2 * padding);
			bubble->updateGeometry(
				::Calls::VideoBubble::DragMode::None,
				QRect(padding, top, use, (use * 480) / 640));
		}, bubbleWrap->lifetime());

		track->renderNextFrame(
		) | rpl::start_with_next([=] {
			const auto size = track->frameSize();
			if (size.isEmpty() || Core::App().calls().currentCall()) {
				return;
			}
			const auto width = bubbleWrap->width();
			const auto use = (width - 2 * padding);
			const auto height = std::min(
				((use * size.height()) / size.width()),
				(use * 480) / 640);
			bubbleWrap->resize(width, top + height + bottom);
			bubbleWrap->update();
		}, bubbleWrap->lifetime());

		Core::App().calls().currentCallValue(
		) | rpl::start_with_next([=](::Calls::Call *value) {
			if (value) {
				track->setState(Webrtc::VideoState::Inactive);
				bubbleWrap->resize(bubbleWrap->width(), 0);
			} else {
				capturer->setPreferredAspectRatio(0.);
				track->setState(Webrtc::VideoState::Active);
				capturer->setOutput(track->sink());
			}
		}, content->lifetime());

		AddSkip(content);
		AddDivider(content);
	}
	AddSkip(content);
	AddSubsectionTitle(content, tr::lng_settings_call_section_output());
	AddButtonWithLabel(
		content,
		tr::lng_settings_call_output_device(),
		rpl::single(
			currentOutputName
		) | rpl::then(
			_outputNameStream.events()
		),
		st::settingsButton
	)->addClickHandler([=] {
		const auto &devices = Webrtc::GetAudioOutputList();
		const auto options = ranges::view::concat(
			ranges::view::single(tr::lng_settings_call_device_default(tr::now)),
			devices | ranges::view::transform(getName)
		) | ranges::to_vector;
		const auto i = ranges::find(
			devices,
			Core::App().settings().callOutputDeviceId(),
			getId);
		const auto currentOption = (i != end(devices))
			? int(i - begin(devices) + 1)
			: 0;
		const auto save = crl::guard(this, [=](int option) {
			_outputNameStream.fire_copy(options[option]);
			const auto deviceId = option
				? devices[option - 1].id
				: "default";
			Core::App().settings().setCallOutputDeviceId(deviceId);
			Core::App().saveSettingsDelayed();
			if (const auto call = Core::App().calls().currentCall()) {
				call->setCurrentAudioDevice(false, deviceId);
			}
		});
		Ui::show(Box<SingleChoiceBox>(
			tr::lng_settings_call_output_device(),
			options,
			currentOption,
			save));
	});

	//const auto outputLabel = content->add(
	//	object_ptr<Ui::LabelSimple>(
	//		content,
	//		st::settingsAudioVolumeLabel),
	//	st::settingsAudioVolumeLabelPadding);
	//const auto outputSlider = content->add(
	//	object_ptr<Ui::MediaSlider>(
	//		content,
	//		st::settingsAudioVolumeSlider),
	//	st::settingsAudioVolumeSliderPadding);
	//const auto updateOutputLabel = [=](int value) {
	//	const auto percent = QString::number(value);
	//	outputLabel->setText(
	//		tr::lng_settings_call_output_volume(tr::now, lt_percent, percent));
	//};
	//const auto updateOutputVolume = [=](int value) {
	//	_needWriteSettings = true;
	//	updateOutputLabel(value);
	//	Core::App().settings().setCallOutputVolume(value);
	//	if (const auto call = Core::App().calls().currentCall()) {
	//		call->setAudioVolume(false, value / 100.0f);
	//	}
	//};
	//outputSlider->resize(st::settingsAudioVolumeSlider.seekSize);
	//outputSlider->setPseudoDiscrete(
	//	101,
	//	[](int val) { return val; },
	//	settings.callOutputVolume(),
	//	updateOutputVolume);
	//updateOutputLabel(Core::App().settings().callOutputVolume());

	AddSkip(content);
	AddDivider(content);
	AddSkip(content);
	AddSubsectionTitle(content, tr::lng_settings_call_section_input());
	AddButtonWithLabel(
		content,
		tr::lng_settings_call_input_device(),
		rpl::single(
			currentInputName
		) | rpl::then(
			_inputNameStream.events()
		),
		st::settingsButton
	)->addClickHandler([=] {
		const auto devices = Webrtc::GetAudioInputList();
		const auto options = ranges::view::concat(
			ranges::view::single(tr::lng_settings_call_device_default(tr::now)),
			devices | ranges::view::transform(getName)
		) | ranges::to_vector;
		const auto i = ranges::find(
			devices,
			Core::App().settings().callInputDeviceId(),
			getId);
		const auto currentOption = (i != end(devices))
			? int(i - begin(devices) + 1)
			: 0;
		const auto save = crl::guard(this, [=](int option) {
			_inputNameStream.fire_copy(options[option]);
			const auto deviceId = option
				? devices[option - 1].id
				: "default";
			Core::App().settings().setCallInputDeviceId(deviceId);
			Core::App().saveSettingsDelayed();
			if (_micTester) {
				_micTester->setDeviceId(deviceId);
			}
			if (const auto call = Core::App().calls().currentCall()) {
				call->setCurrentAudioDevice(true, deviceId);
			}
		});
		Ui::show(Box<SingleChoiceBox>(
			tr::lng_settings_call_input_device(),
			options,
			currentOption,
			save));
	});

	//const auto inputLabel = content->add(
	//	object_ptr<Ui::LabelSimple>(
	//		content,
	//		st::settingsAudioVolumeLabel),
	//	st::settingsAudioVolumeLabelPadding);
	//const auto inputSlider = content->add(
	//	object_ptr<Ui::MediaSlider>(
	//		content,
	//		st::settingsAudioVolumeSlider),
	//	st::settingsAudioVolumeSliderPadding);
	//const auto updateInputLabel = [=](int value) {
	//	const auto percent = QString::number(value);
	//	inputLabel->setText(
	//		tr::lng_settings_call_input_volume(tr::now, lt_percent, percent));
	//};
	//const auto updateInputVolume = [=](int value) {
	//	_needWriteSettings = true;
	//	updateInputLabel(value);
	//	Core::App().settings().setCallInputVolume(value);
	//	if (const auto call = Core::App().calls().currentCall()) {
	//		call->setAudioVolume(true, value / 100.0f);
	//	}
	//};
	//inputSlider->resize(st::settingsAudioVolumeSlider.seekSize);
	//inputSlider->setPseudoDiscrete(101,
	//	[](int val) { return val; },
	//	settings.callInputVolume(),
	//	updateInputVolume);
	//updateInputLabel(settings.callInputVolume());

	//AddButton(
	//	content,
	//	rpl::single(
	//		tr::lng_settings_call_test_mic(tr::now)
	//	) | rpl::then(
	//		_micTestTextStream.events()
	//	),
	//	st::settingsButton
	//)->addClickHandler([=] {
	//	if (!_micTester) {
	//		requestPermissionAndStartTestingMicrophone();
	//	} else {
	//		stopTestingMicrophone();
	//	}
	//});

	_micTestLevel = content->add(
		object_ptr<Ui::LevelMeter>(
			content,
			st::defaultLevelMeter),
		st::settingsLevelMeterPadding);
	_micTestLevel->resize(QSize(0, st::defaultLevelMeter.height));

	_levelUpdateTimer.setCallback([=] {
		const auto was = _micLevel;
		_micLevel = _micTester->getAndResetLevel();
		_micLevelAnimation.start([=] {
			_micTestLevel->setValue(_micLevelAnimation.value(_micLevel));
		}, was, _micLevel, kMicTestAnimationDuration);
	});

	AddSkip(content);
	AddDivider(content);
	AddSkip(content);
	AddSubsectionTitle(content, tr::lng_settings_call_section_other());

//#if defined Q_OS_MAC && !defined OS_MAC_STORE
//	AddButton(
//		content,
//		tr::lng_settings_call_audio_ducking(),
//		st::settingsButton
//	)->toggleOn(
//		rpl::single(settings.callAudioDuckingEnabled())
//	)->toggledValue() | rpl::filter([](bool enabled) {
//		return (enabled != Core::App().settings().callAudioDuckingEnabled());
//	}) | rpl::start_with_next([=](bool enabled) {
//		Core::App().settings().setCallAudioDuckingEnabled(enabled);
//		Core::App().saveSettingsDelayed();
//		if (const auto call = Core::App().calls().currentCall()) {
//			call->setAudioDuckingEnabled(enabled);
//		}
//	}, content->lifetime());
//#endif // Q_OS_MAC && !OS_MAC_STORE

	AddButton(
		content,
		tr::lng_settings_call_open_system_prefs(),
		st::settingsButton
	)->addClickHandler([] {
		const auto opened = Platform::OpenSystemSettings(
			Platform::SystemSettingsType::Audio);
		if (!opened) {
			Ui::show(Box<InformBox>(tr::lng_linux_no_audio_prefs(tr::now)));
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
			tr::lng_no_mic_permission(tr::now),
			tr::lng_menu_settings(tr::now),
			showSystemSettings));
	}
}

void Calls::startTestingMicrophone() {
	//_micTestTextStream.fire(tr::lng_settings_call_stop_mic_test(tr::now));
	_levelUpdateTimer.callEach(kMicTestUpdateInterval);
	_micTester = std::make_unique<Webrtc::AudioInputTester>(
		Core::App().settings().callInputDeviceId());
	//if (_micTester->Failed()) {
	//	stopTestingMicrophone();
	//	Ui::show(Box<InformBox>(tr::lng_call_error_audio_io(tr::now)));
	//}
}

//void Calls::stopTestingMicrophone() {
//	_micTestTextStream.fire(tr::lng_settings_call_test_mic(tr::now));
//	_levelUpdateTimer.cancel();
//	_micTester.reset();
//	_micTestLevel->setValue(0.0f);
//}

} // namespace Settings

