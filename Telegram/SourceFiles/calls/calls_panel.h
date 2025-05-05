/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "calls/calls_call.h"
#include "calls/group/ui/desktop_capture_choose_source.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

class Image;

namespace base {
class PowerSaveBlocker;
} // namespace base

namespace Data {
class PhotoMedia;
} // namespace Data

namespace Main {
class SessionShow;
} // namespace Main

namespace Ui {
class Show;
class BoxContent;
class LayerWidget;
enum class LayerOption;
using LayerOptions = base::flags<LayerOption>;
class IconButton;
class CallButton;
class LayerManager;
class FlatLabel;
template <typename Widget>
class FadeWrap;
template <typename Widget>
class PaddingWrap;
class RpWindow;
class PopupMenu;
} // namespace Ui

namespace Ui::Toast {
class Instance;
struct Config;
} // namespace Ui::Toast

namespace Ui::Platform {
struct SeparateTitleControls;
} // namespace Ui::Platform

namespace style {
struct CallSignalBars;
struct CallBodyLayout;
} // namespace style

namespace Calls {

class Window;
class Userpic;
class SignalBars;
class VideoBubble;
struct DeviceSelection;
struct ConferencePanelMigration;

class Panel final
	: public base::has_weak_ptr
	, private Group::Ui::DesktopCapture::ChooseSourceDelegate {
public:
	Panel(not_null<Call*> call);
	~Panel();

	[[nodiscard]] not_null<Ui::RpWidget*> widget() const;
	[[nodiscard]] not_null<UserData*> user() const;
	[[nodiscard]] bool isVisible() const;
	[[nodiscard]] bool isActive() const;

	[[nodiscard]] ConferencePanelMigration migrationInfo() const;

	void showAndActivate();
	void minimize();
	void toggleFullScreen();
	void replaceCall(not_null<Call*> call);
	void closeBeforeDestroy(bool windowIsReused = false);

	QWidget *chooseSourceParent() override;
	QString chooseSourceActiveDeviceId() override;
	bool chooseSourceActiveWithAudio() override;
	bool chooseSourceWithAudioSupported() override;
	rpl::lifetime &chooseSourceInstanceLifetime() override;
	void chooseSourceAccepted(
		const QString &deviceId,
		bool withAudio) override;
	void chooseSourceStop() override;

	[[nodiscard]] rpl::producer<bool> startOutgoingRequests() const;

	[[nodiscard]] std::shared_ptr<Main::SessionShow> sessionShow();
	[[nodiscard]] std::shared_ptr<Ui::Show> uiShow();

	[[nodiscard]] not_null<Ui::RpWindow*> window() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	class Incoming;
	using State = Call::State;
	using Type = Call::Type;
	enum class AnswerHangupRedialState : uchar {
		Answer,
		Hangup,
		Redial,
		StartCall,
	};

	void paint(QRect clip);

	void initWindow();
	void initWidget();
	void initControls();
	void initConferenceInvite();
	void reinitWithCall(Call *call);
	void initLayout();
	void initMediaDeviceToggles();
	void initGeometry();

	[[nodiscard]] bool handleClose() const;

	void requestControlsHidden(bool hidden);
	void controlsShownForce(bool shown);
	void updateControlsShown();
	void updateControlsGeometry();
	void updateHangupGeometry();
	void updateStatusGeometry();
	void updateOutgoingVideoBubbleGeometry();
	void stateChanged(State state);
	void showControls();
	void updateStatusText(State state);
	void startDurationUpdateTimer(crl::time currentDuration);
	void setIncomingSize(QSize size);
	void refreshIncomingGeometry();

	void refreshOutgoingPreviewInBody(State state);
	void toggleFullScreen(bool fullscreen);
	void createRemoteAudioMute();
	void createRemoteLowBattery();
	void showRemoteLowBattery();
	void refreshAnswerHangupRedialLabel();

	void showDevicesMenu(
		not_null<QWidget*> button,
		std::vector<DeviceSelection> types);

	[[nodiscard]] QRect incomingFrameGeometry() const;
	[[nodiscard]] QRect outgoingFrameGeometry() const;

	Call *_call = nullptr;
	not_null<UserData*> _user;

	std::shared_ptr<Window> _window;
	std::unique_ptr<Incoming> _incoming;

	QSize _incomingFrameSize;

	rpl::lifetime _callLifetime;

	not_null<const style::CallBodyLayout*> _bodySt;
	base::unique_qptr<Ui::CallButton> _answerHangupRedial;
	base::unique_qptr<Ui::FadeWrap<Ui::CallButton>> _decline;
	base::unique_qptr<Ui::FadeWrap<Ui::CallButton>> _cancel;
	bool _hangupShown = false;
	bool _conferenceSupported = false;
	bool _outgoingPreviewInBody = false;
	std::optional<AnswerHangupRedialState> _answerHangupRedialState;
	Ui::Animations::Simple _hangupShownProgress;
	base::unique_qptr<Ui::FadeWrap<Ui::CallButton>> _screencast;
	base::unique_qptr<Ui::CallButton> _camera;
	Ui::CallButton *_cameraDeviceToggle = nullptr;
	base::unique_qptr<Ui::CallButton> _startVideo;
	base::unique_qptr<Ui::FadeWrap<Ui::CallButton>> _mute;
	Ui::CallButton *_audioDeviceToggle = nullptr;
	base::unique_qptr<Ui::FadeWrap<Ui::CallButton>> _addPeople;
	base::unique_qptr<Ui::FlatLabel> _name;
	base::unique_qptr<Ui::FlatLabel> _status;
	base::unique_qptr<Ui::RpWidget> _conferenceParticipants;
	base::unique_qptr<Ui::RpWidget> _fingerprint;
	base::unique_qptr<Ui::PaddingWrap<Ui::FlatLabel>> _remoteAudioMute;
	base::unique_qptr<Ui::PaddingWrap<Ui::FlatLabel>> _remoteLowBattery;
	std::unique_ptr<Userpic> _userpic;
	std::unique_ptr<VideoBubble> _outgoingVideoBubble;
	QPixmap _bottomShadow;
	int _bodyTop = 0;
	int _buttonsTopShown = 0;
	int _buttonsTop = 0;

	base::Timer _hideControlsTimer;
	base::Timer _controlsShownForceTimer;
	std::unique_ptr<QObject> _hideControlsFilter;
	bool _hideControlsRequested = false;
	rpl::variable<bool> _fullScreenOrMaximized;
	Ui::Animations::Simple _controlsShownAnimation;
	bool _controlsShownForce = false;
	bool _controlsShown = true;
	bool _mouseInside = false;

	base::unique_qptr<Ui::PopupMenu> _devicesMenu;

	base::Timer _updateDurationTimer;
	base::Timer _updateOuterRippleTimer;

	rpl::event_stream<bool> _startOutgoingRequests;

	rpl::lifetime _lifetime;

};

} // namespace Calls
