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
#include "ui/boxes/single_choice_box.h"
#include "ui/boxes/confirm_box.h"
#include "platform/platform_specific.h"
#include "main/main_session.h"
#include "lang/lang_keys.h"
#include "styles/style_settings.h"
#include "ui/widgets/continuous_sliders.h"
#include "window/window_session_controller.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "calls/calls_call.h"
#include "calls/calls_instance.h"
#include "calls/calls_video_bubble.h"
#include "apiwrap.h"
#include "api/api_authorizations.h"
#include "webrtc/webrtc_media_devices.h"
#include "webrtc/webrtc_video_track.h"
#include "webrtc/webrtc_audio_input_tester.h"
#include "webrtc/webrtc_create_adm.h" // Webrtc::Backend.
#include "tgcalls/VideoCaptureInterface.h"
#include "facades.h"
#include "boxes/abstract_box.h" // Ui::hideLayer().
#include "styles/style_layers.h"

namespace Settings {
namespace {

using namespace Webrtc;

} // namespace

Calls::Calls(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent)
, _controller(controller) {
	setupContent();
	requestPermissionAndStartTestingMicrophone();
}

Calls::~Calls() = default;

rpl::producer<QString> Calls::title() {
	return tr::lng_settings_section_call_settings();
}

Webrtc::VideoTrack *Calls::AddCameraSubsection(
		std::shared_ptr<Ui::Show> show,
		not_null<Ui::VerticalLayout*> content,
		bool saveToSettings) {
	auto &lifetime = content->lifetime();

	const auto hasCall = (Core::App().calls().currentCall() != nullptr);

	const auto cameraNameStream = lifetime.make_state<
		rpl::event_stream<QString>
	>();

	auto capturerOwner = lifetime.make_state<
		std::shared_ptr<tgcalls::VideoCaptureInterface>
	>();

	const auto track = lifetime.make_state<VideoTrack>(
		(hasCall
			? VideoState::Inactive
			: VideoState::Active));

	const auto currentCameraName = [&] {
		const auto cameras = GetVideoInputList();
		const auto i = ranges::find(
			cameras,
			Core::App().settings().callVideoInputDeviceId(),
			&VideoInput::id);
		return (i != end(cameras))
			? i->name
			: tr::lng_settings_call_device_default(tr::now);
	}();

	AddButtonWithLabel(
		content,
		tr::lng_settings_call_input_device(),
		rpl::single(
			currentCameraName
		) | rpl::then(
			cameraNameStream->events()
		),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		const auto &devices = GetVideoInputList();
		const auto options = ranges::views::concat(
			ranges::views::single(
				tr::lng_settings_call_device_default(tr::now)),
			devices | ranges::views::transform(&VideoInput::name)
		) | ranges::to_vector;
		const auto i = ranges::find(
			devices,
			Core::App().settings().callVideoInputDeviceId(),
			&VideoInput::id);
		const auto currentOption = (i != end(devices))
			? int(i - begin(devices) + 1)
			: 0;
		const auto save = crl::guard(content, [=](int option) {
			cameraNameStream->fire_copy(options[option]);
			const auto deviceId = option
				? devices[option - 1].id
				: "default";
			if (saveToSettings) {
				Core::App().settings().setCallVideoInputDeviceId(deviceId);
				Core::App().saveSettingsDelayed();
			}
			if (const auto call = Core::App().calls().currentCall()) {
				call->setCurrentCameraDevice(deviceId);
			}
			if (*capturerOwner) {
				(*capturerOwner)->switchToDevice(
					deviceId.toStdString(),
					false);
			}
		});
		show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
			SingleChoiceBox(box, {
				.title = tr::lng_settings_call_camera(),
				.options = options,
				.initialSelection = currentOption,
				.callback = save,
			});
		}));
	});
	const auto bubbleWrap = content->add(object_ptr<Ui::RpWidget>(content));
	const auto bubble = lifetime.make_state<::Calls::VideoBubble>(
		bubbleWrap,
		track);
	const auto padding = st::settingsButtonNoIcon.padding.left();
	const auto top = st::boxRoundShadow.extend.top();
	const auto bottom = st::boxRoundShadow.extend.bottom();

	auto frameSize = track->renderNextFrame(
	) | rpl::map([=] {
		return track->frameSize();
	}) | rpl::filter([=](QSize size) {
		return !size.isEmpty()
			&& !Core::App().calls().currentCall()
			&& !Core::App().calls().currentGroupCall();
	});
	auto bubbleWidth = bubbleWrap->widthValue(
	) | rpl::filter([=](int width) {
		return width > 2 * padding + 1;
	});
	rpl::combine(
		std::move(bubbleWidth),
		std::move(frameSize)
	) | rpl::start_with_next([=](int width, QSize frame) {
		const auto useWidth = (width - 2 * padding);
		const auto useHeight = std::min(
			((useWidth * frame.height()) / frame.width()),
			(useWidth * 480) / 640);
		bubbleWrap->resize(width, top + useHeight + bottom);
		bubble->updateGeometry(
			::Calls::VideoBubble::DragMode::None,
			QRect(padding, top, useWidth, useHeight));
		bubbleWrap->update();
	}, bubbleWrap->lifetime());

	using namespace rpl::mappers;
	const auto checkCapturer = [=] {
		if (*capturerOwner
			|| Core::App().calls().currentCall()
			|| Core::App().calls().currentGroupCall()) {
			return;
		}
		*capturerOwner = Core::App().calls().getVideoCapture(
			Core::App().settings().callVideoInputDeviceId(),
			false);
		(*capturerOwner)->setPreferredAspectRatio(0.);
		track->setState(VideoState::Active);
		(*capturerOwner)->setState(tgcalls::VideoState::Active);
		(*capturerOwner)->setOutput(track->sink());
	};
	rpl::combine(
		Core::App().calls().currentCallValue(),
		Core::App().calls().currentGroupCallValue(),
		_1 || _2
	) | rpl::start_with_next([=](bool has) {
		if (has) {
			track->setState(VideoState::Inactive);
			bubbleWrap->resize(bubbleWrap->width(), 0);
			*capturerOwner = nullptr;
		} else {
			crl::on_main(content, checkCapturer);
		}
	}, lifetime);

	return track;
}

void Calls::sectionSaveChanges(FnMut<void()> done) {
	if (_micTester) {
		_micTester.reset();
	}
	done();
}

void Calls::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	if (!GetVideoInputList().empty()) {
		AddSkip(content);
		AddSubsectionTitle(content, tr::lng_settings_call_camera());
		AddCameraSubsection(
			std::make_shared<Window::Show>(_controller),
			content,
			true);
		AddSkip(content);
		AddDivider(content);
	}
	AddSkip(content);
	AddSubsectionTitle(content, tr::lng_settings_call_section_output());
	AddButtonWithLabel(
		content,
		tr::lng_settings_call_output_device(),
		rpl::single(
			CurrentAudioOutputName()
		) | rpl::then(
			_outputNameStream.events()
		),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		_controller->show(ChooseAudioOutputBox(crl::guard(this, [=](
				const QString &id,
				const QString &name) {
			_outputNameStream.fire_copy(name);
		})));
	});

	AddSkip(content);
	AddDivider(content);
	AddSkip(content);
	AddSubsectionTitle(content, tr::lng_settings_call_section_input());
	AddButtonWithLabel(
		content,
		tr::lng_settings_call_input_device(),
		rpl::single(
			CurrentAudioInputName()
		) | rpl::then(
			_inputNameStream.events()
		),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		_controller->show(ChooseAudioInputBox(crl::guard(this, [=](
				const QString &id,
				const QString &name) {
			_inputNameStream.fire_copy(name);
			if (_micTester) {
				_micTester->setDeviceId(id);
			}
		})));
	});

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

	const auto api = &_controller->session().api();
	AddButton(
		content,
		tr::lng_settings_call_accept_calls(),
		st::settingsButtonNoIcon
	)->toggleOn(
		api->authorizations().callsDisabledHereValue(
		) | rpl::map(!rpl::mappers::_1)
	)->toggledChanges(
	) | rpl::filter([=](bool value) {
		return (value == api->authorizations().callsDisabledHere());
	}) | start_with_next([=](bool value) {
		api->authorizations().toggleCallsDisabledHere(!value);
	}, content->lifetime());

	AddButton(
		content,
		tr::lng_settings_call_open_system_prefs(),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		const auto opened = Platform::OpenSystemSettings(
			Platform::SystemSettingsType::Audio);
		if (!opened) {
			_controller->show(
				Ui::MakeInformBox(tr::lng_linux_no_audio_prefs()));
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
		_controller->show(Ui::MakeConfirmBox({
			.text = tr::lng_no_mic_permission(),
			.confirmed = showSystemSettings,
			.confirmText = tr::lng_menu_settings(),
		}));
	}
}

void Calls::startTestingMicrophone() {
	_levelUpdateTimer.callEach(kMicTestUpdateInterval);
	_micTester = std::make_unique<AudioInputTester>(
		Core::App().settings().callAudioBackend(),
		Core::App().settings().callInputDeviceId());
}

QString CurrentAudioOutputName() {
	const auto &settings = Core::App().settings();
	const auto list = GetAudioOutputList(settings.callAudioBackend());
	const auto i = ranges::find(
		list,
		settings.callOutputDeviceId(),
		&AudioOutput::id);
	return (i != end(list))
		? i->name
		: tr::lng_settings_call_device_default(tr::now);
}

QString CurrentAudioInputName() {
	const auto &settings = Core::App().settings();
	const auto list = GetAudioInputList(settings.callAudioBackend());
	const auto i = ranges::find(
		list,
		settings.callInputDeviceId(),
		&AudioInput::id);
	return (i != end(list))
		? i->name
		: tr::lng_settings_call_device_default(tr::now);
}

object_ptr<Ui::GenericBox> ChooseAudioOutputBox(
		Fn<void(QString id, QString name)> chosen,
		const style::Checkbox *st,
		const style::Radio *radioSt) {
	const auto &settings = Core::App().settings();
	const auto list = GetAudioOutputList(settings.callAudioBackend());
	const auto options = ranges::views::concat(
		ranges::views::single(tr::lng_settings_call_device_default(tr::now)),
		list | ranges::views::transform(&AudioOutput::name)
	) | ranges::to_vector;
	const auto i = ranges::find(
		list,
		settings.callOutputDeviceId(),
		&AudioOutput::id);
	const auto currentOption = (i != end(list))
		? int(i - begin(list) + 1)
		: 0;
	const auto save = [=](int option) {
		const auto deviceId = option
			? list[option - 1].id
			: "default";
		Core::App().calls().setCurrentAudioDevice(false, deviceId);
		chosen(deviceId, options[option]);
	};
	return Box([=](not_null<Ui::GenericBox*> box) {
		SingleChoiceBox(box, {
			.title = tr::lng_settings_call_output_device(),
			.options = options,
			.initialSelection = currentOption,
			.callback = save,
			.st = st,
			.radioSt = radioSt,
		});
	});
}

object_ptr<Ui::GenericBox> ChooseAudioInputBox(
		Fn<void(QString id, QString name)> chosen,
		const style::Checkbox *st,
		const style::Radio *radioSt) {
	const auto &settings = Core::App().settings();
	const auto list = GetAudioInputList(settings.callAudioBackend());
	const auto options = ranges::views::concat(
		ranges::views::single(tr::lng_settings_call_device_default(tr::now)),
		list | ranges::views::transform(&AudioInput::name)
	) | ranges::to_vector;
	const auto i = ranges::find(
		list,
		Core::App().settings().callInputDeviceId(),
		&AudioInput::id);
	const auto currentOption = (i != end(list))
		? int(i - begin(list) + 1)
		: 0;
	const auto save = [=](int option) {
		const auto deviceId = option
			? list[option - 1].id
			: "default";
		Core::App().calls().setCurrentAudioDevice(true, deviceId);
		chosen(deviceId, options[option]);
	};
	return Box([=](not_null<Ui::GenericBox*> box) {
		SingleChoiceBox(box, {
			.title = tr::lng_settings_call_input_device(),
			.options = options,
			.initialSelection = currentOption,
			.callback = save,
			.st = st,
			.radioSt = radioSt,
		});
	});
}
//
//object_ptr<Ui::GenericBox> ChooseAudioBackendBox(
//		const style::Checkbox *st,
//		const style::Radio *radioSt) {
//	const auto &settings = Core::App().settings();
//	const auto list = GetAudioInputList(settings.callAudioBackend());
//	const auto options = std::vector<QString>{
//		"OpenAL",
//		"Webrtc ADM",
//#ifdef Q_OS_WIN
//		"Webrtc ADM2",
//#endif // Q_OS_WIN
//	};
//	const auto currentOption = static_cast<int>(settings.callAudioBackend());
//	const auto save = [=](int option) {
//		Core::App().settings().setCallAudioBackend(
//			static_cast<Webrtc::Backend>(option));
//		Core::App().saveSettings();
//		Core::Restart();
//	};
//	return Box([=](not_null<Ui::GenericBox*> box) {
//		SingleChoiceBox(box, {
//			.title = rpl::single<QString>("Calls audio backend"),
//			.options = options,
//			.initialSelection = currentOption,
//			.callback = save,
//			.st = st,
//			.radioSt = radioSt,
//		});
//	});
//}

} // namespace Settings

