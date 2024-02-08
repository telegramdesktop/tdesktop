/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_common_session.h"
#include "ui/effects/animations.h"
#include "base/timer.h"

namespace style {
struct Checkbox;
struct Radio;
} // namespace style

namespace Calls {
class Call;
} // namespace Calls

namespace Ui {
class LevelMeter;
class GenericBox;
class Show;
} // namespace Ui

namespace Webrtc {
class AudioInputTester;
class VideoTrack;
} // namespace Webrtc

namespace Settings {

class Calls : public Section<Calls> {
public:
	Calls(QWidget *parent, not_null<Window::SessionController*> controller);
	~Calls();

	[[nodiscard]] rpl::producer<QString> title() override;

	void sectionSaveChanges(FnMut<void()> done) override;

	static Webrtc::VideoTrack *AddCameraSubsection(
		std::shared_ptr<Ui::Show> show,
		not_null<Ui::VerticalLayout*> content,
		bool saveToSettings);

private:
	void setupContent();
	void requestPermissionAndStartTestingMicrophone();

	void initPlaybackButton(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text,
		rpl::producer<QString> resolvedId,
		Fn<void(QString)> set);
	void initCaptureButton(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text,
		rpl::producer<QString> resolvedId,
		Fn<void(QString)> set);

	const not_null<Window::SessionController*> _controller;
	rpl::event_stream<QString> _cameraNameStream;
	rpl::variable<bool> _testingMicrophone;

};

inline constexpr auto kMicTestUpdateInterval = crl::time(100);
inline constexpr auto kMicTestAnimationDuration = crl::time(200);

[[nodiscard]] rpl::producer<QString> PlaybackDeviceNameValue(
	rpl::producer<QString> id);
[[nodiscard]] rpl::producer<QString> CaptureDeviceNameValue(
	rpl::producer<QString> id);
[[nodiscard]] rpl::producer<QString> CameraDeviceNameValue(
	rpl::producer<QString> id);
[[nodiscard]] object_ptr<Ui::GenericBox> ChoosePlaybackDeviceBox(
	rpl::producer<QString> currentId,
	Fn<void(QString id)> chosen,
	const style::Checkbox *st = nullptr,
	const style::Radio *radioSt = nullptr);
[[nodiscard]] object_ptr<Ui::GenericBox> ChooseCaptureDeviceBox(
	rpl::producer<QString> currentId,
	Fn<void(QString id)> chosen,
	const style::Checkbox *st = nullptr,
	const style::Radio *radioSt = nullptr);
[[nodiscard]] object_ptr<Ui::GenericBox> ChooseCameraDeviceBox(
	rpl::producer<QString> currentId,
	Fn<void(QString id)> chosen,
	const style::Checkbox *st = nullptr,
	const style::Radio *radioSt = nullptr);

} // namespace Settings

