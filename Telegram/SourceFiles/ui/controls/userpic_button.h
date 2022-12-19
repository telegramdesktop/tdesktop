/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/buttons.h"
#include "ui/userpic_view.h"

class PeerData;

namespace Window {
class Controller;
class SessionController;
} // namespace Window

namespace Media {
namespace Streaming {
class Instance;
struct Update;
enum class Error;
struct Information;
} // namespace Streaming
} // namespace Media

namespace style {
struct UserpicButton;
} // namespace style

namespace Ui {

class PopupMenu;

class UserpicButton final : public RippleButton {
public:
	enum class Role {
		ChoosePhoto,
		ChangePhoto,
		OpenPhoto,
		OpenProfile,
		Custom,
	};

	UserpicButton(
		QWidget *parent,
		not_null<::Window::Controller*> window,
		not_null<PeerData*> peer,
		Role role,
		const style::UserpicButton &st);
	UserpicButton(
		QWidget *parent,
		not_null<::Window::Controller*> window,
		Role role,
		const style::UserpicButton &st);
	UserpicButton(
		QWidget *parent,
		not_null<::Window::SessionController*> controller,
		not_null<PeerData*> peer,
		Role role,
		const style::UserpicButton &st);
	UserpicButton(
		QWidget *parent,
		not_null<PeerData*> peer,
		Role role,
		const style::UserpicButton &st);
	~UserpicButton();

	enum class ChosenType {
		Set,
		Suggest,
	};
	struct ChosenImage {
		QImage image;
		ChosenType type = ChosenType::Set;
	};

	// Role::OpenPhoto
	void switchChangePhotoOverlay(
		bool enabled,
		Fn<void(ChosenImage)> chosen);
	void showSavedMessagesOnSelf(bool enabled);

	// Role::ChoosePhoto or Role::ChangePhoto
	[[nodiscard]] rpl::producer<ChosenImage> chosenImages() const {
		return _chosenImages.events();
	}
	[[nodiscard]] QImage takeResultImage() {
		return std::move(_result);
	}

	// For Role::OpenPhoto as if it is Role::ChangePhoto.
	void changeTo(QImage &&image);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	void prepare();
	void setImage(QImage &&image);
	void setupPeerViewers();
	void startAnimation();
	void processPeerPhoto();
	void processNewPeerPhoto();
	void startNewPhotoShowing();
	void prepareUserpicPixmap();
	void fillShape(QPainter &p, const style::color &color) const;
	[[nodiscard]] QPoint countPhotoPosition() const;
	void startChangeOverlayAnimation();
	void updateCursorInChangeOverlay(QPoint localPos);
	void setCursorInChangeOverlay(bool inOverlay);
	void updateCursor();
	void updateVideo();
	bool showSavedMessages() const;
	bool showRepliesMessages() const;
	void checkStreamedIsStarted();
	bool createStreamingObjects(not_null<PhotoData*> photo);
	void clearStreaming();
	void handleStreamingUpdate(Media::Streaming::Update &&update);
	void handleStreamingError(Media::Streaming::Error &&error);
	void streamingReady(Media::Streaming::Information &&info);
	void paintUserpicFrame(Painter &p, QPoint photoPosition);

	void grabOldUserpic();
	void setClickHandlerByRole();
	void openPeerPhoto();
	void choosePhotoLocally();

	const style::UserpicButton &_st;
	::Window::SessionController *_controller = nullptr;
	::Window::Controller *_window = nullptr;
	PeerData *_peer = nullptr;
	PeerUserpicView _userpicView;
	Role _role = Role::ChangePhoto;
	bool _notShownYet = true;
	bool _waiting = false;
	QPixmap _userpic, _oldUserpic;
	bool _userpicHasImage = false;
	bool _userpicCustom = false;
	InMemoryKey _userpicUniqueKey;
	Animations::Simple _a_appearance;
	QImage _result;
	QImage _ellipseMask;
	std::array<QImage, 4> _roundingCorners;
	std::unique_ptr<Media::Streaming::Instance> _streamed;
	PhotoData *_streamedPhoto = nullptr;

	base::unique_qptr<PopupMenu> _menu;

	bool _showSavedMessagesOnSelf = false;
	bool _canOpenPhoto = false;
	bool _cursorInChangeOverlay = false;
	bool _changeOverlayEnabled = false;
	Animations::Simple _changeOverlayShown;

	rpl::event_stream<ChosenImage> _chosenImages;

};

[[nodiscard]] not_null<Ui::UserpicButton*> CreateUploadSubButton(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> controller);

[[nodiscard]] not_null<Ui::UserpicButton*> CreateUploadSubButton(
	not_null<Ui::RpWidget*> parent,
	not_null<UserData*> contact,
	not_null<Window::SessionController*> controller);

} // namespace Ui
