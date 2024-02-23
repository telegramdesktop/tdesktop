/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_calls.h"

#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/level_meter.h"
#include "ui/widgets/buttons.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/vertical_list.h"
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
#include "webrtc/webrtc_environment.h"
#include "webrtc/webrtc_video_track.h"
#include "webrtc/webrtc_audio_input_tester.h"
#include "webrtc/webrtc_create_adm.h" // Webrtc::Backend.
#include "tgcalls/VideoCaptureInterface.h"
#include "styles/style_layers.h"

namespace Settings {
namespace {

using namespace Webrtc;

[[nodiscard]] rpl::producer<QString> DeviceNameValue(
		DeviceType type,
		rpl::producer<QString> id) {
	return std::move(id) | rpl::map([type](const QString &id) {
		return Core::App().mediaDevices().devicesValue(
			type
		) | rpl::map([id](const std::vector<DeviceInfo> &list) {
			const auto i = ranges::find(list, id, &DeviceInfo::id);
			return (i != end(list) && !i->inactive)
				? i->name
				: tr::lng_settings_call_device_default(tr::now);
		});
	}) | rpl::flatten_latest();
}

} // namespace

Calls::Calls(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent)
, _controller(controller) {
	// Request valid value of calls disabled flag.
	controller->session().api().authorizations().reload();

	setupContent();
	requestPermissionAndStartTestingMicrophone();
}

Calls::~Calls() = default;

rpl::producer<QString> Calls::title() {
	return tr::lng_settings_section_devices();
}

Webrtc::VideoTrack *Calls::AddCameraSubsection(
		std::shared_ptr<Ui::Show> show,
		not_null<Ui::VerticalLayout*> content,
		bool saveToSettings) {
	auto &lifetime = content->lifetime();

	const auto hasCall = (Core::App().calls().currentCall() != nullptr);

	auto capturerOwner = lifetime.make_state<
		std::shared_ptr<tgcalls::VideoCaptureInterface>
	>();

	const auto track = lifetime.make_state<VideoTrack>(
		(hasCall
			? VideoState::Inactive
			: VideoState::Active));

	const auto deviceId = lifetime.make_state<rpl::variable<QString>>(
		Core::App().settings().cameraDeviceId());
	auto resolvedId = rpl::deferred([=] {
		return DeviceIdOrDefault(deviceId->value());
	});
	AddButtonWithLabel(
		content,
		tr::lng_settings_call_input_device(),
		CameraDeviceNameValue(rpl::duplicate(resolvedId)),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		show->show(ChooseCameraDeviceBox(
			rpl::duplicate(resolvedId),
			[=](const QString &id) {
				*deviceId = id;
				if (saveToSettings) {
					Core::App().settings().setCameraDeviceId(id);
					Core::App().saveSettingsDelayed();
				}
				if (*capturerOwner) {
					(*capturerOwner)->switchToDevice(
						id.toStdString(),
						false);
				}
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
			Core::App().settings().cameraDeviceId(),
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
	_testingMicrophone = false;
	done();
}

void Calls::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto settings = &Core::App().settings();

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, tr::lng_settings_call_section_output());

	initPlaybackButton(
		content,
		tr::lng_settings_call_output_device(),
		rpl::deferred([=] {
			return DeviceIdOrDefault(settings->playbackDeviceIdValue());
		}),
		[=](const QString &id) { settings->setPlaybackDeviceId(id); });

	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, tr::lng_settings_call_section_input());
	initCaptureButton(
		content,
		tr::lng_settings_call_input_device(),
		rpl::deferred([=] {
			return DeviceIdOrDefault(settings->captureDeviceIdValue());
		}),
		[=](const QString &id) { settings->setCaptureDeviceId(id); });

	Ui::AddSkip(content);
	Ui::AddDivider(content);

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, tr::lng_settings_devices_calls());
	const auto orDefault = [](const QString &value) {
		return value.isEmpty() ? kDefaultDeviceId : value;
	};
	const auto same = content->add(object_ptr<Ui::SettingsButton>(
		content,
		tr::lng_settings_devices_calls_same(),
		st::settingsButtonNoIcon));
	same->toggleOn(rpl::combine(
		settings->callPlaybackDeviceIdValue(),
		settings->callCaptureDeviceIdValue()
	) | rpl::map([](const QString &playback, const QString &capture) {
		return playback.isEmpty() && capture.isEmpty();
	}));
	same->toggledValue() | rpl::filter([=](bool toggled) {
		const auto empty = settings->callPlaybackDeviceId().isEmpty()
			&& settings->callCaptureDeviceId().isEmpty();
		return (empty != toggled);
	}) | rpl::start_with_next([=](bool toggled) {
		if (toggled) {
			settings->setCallPlaybackDeviceId(QString());
			settings->setCallCaptureDeviceId(QString());
		} else {
			settings->setCallPlaybackDeviceId(
				orDefault(settings->playbackDeviceId()));
			settings->setCallCaptureDeviceId(
				orDefault(settings->captureDeviceId()));
		}
		Core::App().saveSettingsDelayed();
	}, same->lifetime());
	const auto different = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content)));
	const auto calls = different->entity();
	initPlaybackButton(
		calls,
		tr::lng_group_call_speakers(),
		rpl::deferred([=] {
			return DeviceIdValueWithFallback(
				settings->callPlaybackDeviceIdValue(),
				settings->playbackDeviceIdValue());
		}),
		[=](const QString &id) { settings->setCallPlaybackDeviceId(id); });
	initCaptureButton(
		calls,
		tr::lng_group_call_microphone(),
		rpl::deferred([=] {
			return DeviceIdValueWithFallback(
				settings->callCaptureDeviceIdValue(),
				settings->captureDeviceIdValue());
		}),
		[=](const QString &id) { settings->setCallCaptureDeviceId(id); });
	different->toggleOn(same->toggledValue() | rpl::map(!rpl::mappers::_1));
	Ui::AddSkip(content);
	Ui::AddDivider(content);

	if (!Core::App().mediaDevices().defaultId(
			Webrtc::DeviceType::Camera).isEmpty()) {
		Ui::AddSkip(content);
		Ui::AddSubsectionTitle(content, tr::lng_settings_call_camera());
		AddCameraSubsection(_controller->uiShow(), content, true);
		Ui::AddSkip(content);
		Ui::AddDivider(content);
	}

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, tr::lng_settings_call_section_other());

	const auto api = &_controller->session().api();
	content->add(object_ptr<Ui::SettingsButton>(
		content,
		tr::lng_settings_call_accept_calls(),
		st::settingsButtonNoIcon
	))->toggleOn(
		api->authorizations().callsDisabledHereValue(
		) | rpl::map(!rpl::mappers::_1)
	)->toggledChanges(
	) | rpl::filter([=](bool value) {
		return (value == api->authorizations().callsDisabledHere());
	}) | start_with_next([=](bool value) {
		api->authorizations().toggleCallsDisabledHere(!value);
	}, content->lifetime());

	content->add(object_ptr<Ui::SettingsButton>(
		content,
		tr::lng_settings_call_open_system_prefs(),
		st::settingsButtonNoIcon
	))->addClickHandler([=] {
		using namespace ::Platform;
		const auto opened = OpenSystemSettings(SystemSettingsType::Audio);
		if (!opened) {
			_controller->show(
				Ui::MakeInformBox(tr::lng_linux_no_audio_prefs()));
		}
	});

	Ui::AddSkip(content);

	Ui::ResizeFitChild(this, content);
}

void Calls::initPlaybackButton(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text,
		rpl::producer<QString> resolvedId,
		Fn<void(QString)> set) {
	AddButtonWithLabel(
		container,
		tr::lng_settings_call_output_device(),
		PlaybackDeviceNameValue(rpl::duplicate(resolvedId)),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		_controller->show(ChoosePlaybackDeviceBox(
			rpl::duplicate(resolvedId),
			[=](const QString &id) {
				set(id);
				Core::App().saveSettingsDelayed();
			}));
	});

}

void Calls::initCaptureButton(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text,
		rpl::producer<QString> resolvedId,
		Fn<void(QString)> set) {
	AddButtonWithLabel(
		container,
		tr::lng_settings_call_input_device(),
		CaptureDeviceNameValue(rpl::duplicate(resolvedId)),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		_controller->show(ChooseCaptureDeviceBox(
			rpl::duplicate(resolvedId),
			[=](const QString &id) {
				set(id);
				Core::App().saveSettingsDelayed();
			}));
	});

	struct LevelState {
		std::unique_ptr<Webrtc::DeviceResolver> deviceId;
		std::unique_ptr<Webrtc::AudioInputTester> tester;
		base::Timer timer;
		Ui::Animations::Simple animation;
		float level = 0.;
	};
	const auto level = container->add(
		object_ptr<Ui::LevelMeter>(
			container,
			st::defaultLevelMeter),
		st::settingsLevelMeterPadding);
	const auto state = level->lifetime().make_state<LevelState>();
	level->resize(QSize(0, st::defaultLevelMeter.height));

	state->timer.setCallback([=] {
		const auto was = state->level;
		state->level = state->tester->getAndResetLevel();
		state->animation.start([=] {
			level->setValue(state->animation.value(state->level));
		}, was, state->level, kMicTestAnimationDuration);
	});
	_testingMicrophone.value() | rpl::start_with_next([=](bool testing) {
		if (testing) {
			state->deviceId = std::make_unique<Webrtc::DeviceResolver>(
				&Core::App().mediaDevices(),
				Webrtc::DeviceType::Capture,
				rpl::duplicate(resolvedId));
			state->tester = std::make_unique<AudioInputTester>(
				state->deviceId->value());
			state->timer.callEach(kMicTestUpdateInterval);
		} else {
			state->timer.cancel();
			state->animation.stop();
			state->tester = nullptr;
			state->deviceId = nullptr;
		}
	}, level->lifetime());
}

void Calls::requestPermissionAndStartTestingMicrophone() {
	using namespace ::Platform;
	const auto status = GetPermissionStatus(
		PermissionType::Microphone);
	if (status == PermissionStatus::Granted) {
		_testingMicrophone = true;
	} else if (status == PermissionStatus::CanRequest) {
		const auto startTestingChecked = crl::guard(this, [=](
				PermissionStatus status) {
			if (status == PermissionStatus::Granted) {
				crl::on_main(crl::guard(this, [=] {
					_testingMicrophone = true;
				}));
			}
		});
		RequestPermission(
			PermissionType::Microphone,
			startTestingChecked);
	} else {
		const auto showSystemSettings = [controller = _controller] {
			OpenSystemSettingsForPermission(
				PermissionType::Microphone);
			controller->hideLayer();
		};
		_controller->show(Ui::MakeConfirmBox({
			.text = tr::lng_no_mic_permission(),
			.confirmed = showSystemSettings,
			.confirmText = tr::lng_menu_settings(),
		}));
	}
}

rpl::producer<QString> PlaybackDeviceNameValue(rpl::producer<QString> id) {
	return DeviceNameValue(DeviceType::Playback, std::move(id));
}

rpl::producer<QString> CaptureDeviceNameValue(rpl::producer<QString> id) {
	return DeviceNameValue(DeviceType::Capture, std::move(id));
}

rpl::producer<QString> CameraDeviceNameValue(
		rpl::producer<QString> id) {
	return DeviceNameValue(DeviceType::Camera, std::move(id));
}

void ChooseMediaDeviceBox(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> title,
		rpl::producer<std::vector<DeviceInfo>> devicesValue,
		rpl::producer<QString> currentId,
		Fn<void(QString id)> chosen,
		const style::Checkbox *st,
		const style::Radio *radioSt) {
	box->setTitle(std::move(title));
	box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
	const auto layout = box->verticalLayout();
	const auto skip = st::boxOptionListPadding.top()
		+ st::defaultBoxCheckbox.margin.top();
	layout->add(object_ptr<Ui::FixedHeightWidget>(layout, skip));

	if (!st) {
		st = &st::defaultBoxCheckbox;
	}
	if (!radioSt) {
		radioSt = &st::defaultRadio;
	}

	struct State {
		std::vector<DeviceInfo> list;
		base::flat_map<int, QString> ids;
		rpl::variable<QString> currentId;
		QString currentName;
		bool ignoreValueChange = false;
	};
	const auto state = box->lifetime().make_state<State>();
	state->currentId = std::move(currentId);

	const auto choose = [=](const QString &id) {
		const auto weak = Ui::MakeWeak(box);
		chosen(id);
		if (weak) {
			box->closeBox();
		}
	};

	const auto group = std::make_shared<Ui::RadiobuttonGroup>();
	const auto fake = std::make_shared<Ui::RadiobuttonGroup>(0);
	const auto buttons = layout->add(object_ptr<Ui::VerticalLayout>(layout));
	const auto other = layout->add(object_ptr<Ui::VerticalLayout>(layout));
	const auto margins = QMargins(
		st::boxPadding.left() + st::boxOptionListPadding.left(),
		0,
		st::boxPadding.right(),
		st::boxOptionListSkip);
	const auto def = buttons->add(
		object_ptr<Ui::Radiobutton>(
			buttons,
			group,
			0,
			tr::lng_settings_call_device_default(tr::now),
			*st,
			*radioSt),
		margins);
	def->clicks(
	) | rpl::filter([=] {
		return !group->value();
	}) | rpl::start_with_next([=] {
		choose(kDefaultDeviceId);
	}, def->lifetime());
	const auto showUnavailable = [=](QString text) {
		AddSkip(other);
		AddSubsectionTitle(other, tr::lng_settings_devices_inactive());
		const auto &radio = *radioSt;
		const auto button = other->add(
			object_ptr<Ui::Radiobutton>(other, fake, 0, text, *st, radio),
			margins);
		button->show();

		button->setDisabled(true);
		button->finishAnimating();
		button->setAttribute(Qt::WA_TransparentForMouseEvents);
		while (other->count() > 3) {
			delete other->widgetAt(0);
		}
		if (const auto width = box->width()) {
			other->resizeToWidth(width);
		}
	};
	const auto hideUnavailable = [=] {
		while (other->count() > 0) {
			delete other->widgetAt(0);
		}
	};

	const auto selectCurrent = [=](QString current) {
		state->ignoreValueChange = true;
		const auto guard = gsl::finally([&] {
			state->ignoreValueChange = false;
		});
		if (current.isEmpty() || current == kDefaultDeviceId) {
			group->setValue(0);
			hideUnavailable();
		} else {
			auto found = false;
			for (const auto &[index, id] : state->ids) {
				if (id == current) {
					group->setValue(index);
					found = true;
					break;
				}
			}
			if (found) {
				hideUnavailable();
			} else {
				group->setValue(0);
				const auto i = ranges::find(
					state->list,
					current,
					&DeviceInfo::id);
				if (i != end(state->list)) {
					showUnavailable(i->name);
				} else {
					hideUnavailable();
				}
			}
		}
	};

	std::move(
		devicesValue
	) | rpl::start_with_next([=](std::vector<DeviceInfo> &&list) {
		auto count = buttons->count();
		auto index = 1;
		state->ids.clear();
		state->list = std::move(list);

		state->ignoreValueChange = true;
		const auto guard = gsl::finally([&] {
			state->ignoreValueChange = false;
		});

		const auto current = state->currentId.current();
		for (const auto &info : state->list) {
			const auto id = info.id;
			if (info.inactive) {
				continue;
			} else if (current == id) {
				group->setValue(index);
			}
			const auto button = buttons->insert(
				index,
				object_ptr<Ui::Radiobutton>(
					buttons,
					group,
					index,
					info.name,
					*st,
					*radioSt),
				margins);
			button->show();
			button->finishAnimating();
			button->clicks(
			) | rpl::filter([=] {
				return (group->current() == index);
			}) | rpl::start_with_next([=] {
				choose(id);
			}, button->lifetime());

			state->ids.emplace(index, id);
			if (index < count) {
				delete buttons->widgetAt(index + 1);
			}
			++index;
		}
		while (index < count) {
			delete buttons->widgetAt(index);
			--count;
		}
		if (const auto width = box->width()) {
			buttons->resizeToWidth(width);
		}
		selectCurrent(current);
	}, box->lifetime());

	state->currentId.changes(
	) | rpl::start_with_next(selectCurrent, box->lifetime());

	def->finishAnimating();

	group->setChangedCallback([=](int value) {
		if (state->ignoreValueChange) {
			return;
		}
		const auto i = state->ids.find(value);
		choose((i != end(state->ids)) ? i->second : kDefaultDeviceId);
	});
}

object_ptr<Ui::GenericBox> ChoosePlaybackDeviceBox(
		rpl::producer<QString> currentId,
		Fn<void(QString id)> chosen,
		const style::Checkbox *st,
		const style::Radio *radioSt) {
	return Box(
		ChooseMediaDeviceBox,
		tr::lng_settings_call_output_device(),
		Core::App().mediaDevices().devicesValue(DeviceType::Playback),
		std::move(currentId),
		std::move(chosen),
		st,
		radioSt);
}

object_ptr<Ui::GenericBox> ChooseCaptureDeviceBox(
		rpl::producer<QString> currentId,
		Fn<void(QString id)> chosen,
		const style::Checkbox *st,
		const style::Radio *radioSt) {
	return Box(
		ChooseMediaDeviceBox,
		tr::lng_settings_call_input_device(),
		Core::App().mediaDevices().devicesValue(DeviceType::Capture),
		std::move(currentId),
		std::move(chosen),
		st,
		radioSt);
}

object_ptr<Ui::GenericBox> ChooseCameraDeviceBox(
		rpl::producer<QString> currentId,
		Fn<void(QString id)> chosen,
		const style::Checkbox *st,
		const style::Radio *radioSt) {
	return Box(
		ChooseMediaDeviceBox,
		tr::lng_settings_call_device_default(),
		Core::App().mediaDevices().devicesValue(DeviceType::Camera),
		std::move(currentId),
		std::move(chosen),
		st,
		radioSt);
}

} // namespace Settings

