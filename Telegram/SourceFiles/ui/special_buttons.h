/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/buttons.h"
#include "ui/widgets/tooltip.h"
#include "ui/effects/animations.h"
#include "styles/style_window.h"
#include "styles/style_widgets.h"

class PeerData;

namespace Ui {
class InfiniteRadialAnimation;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {

class HistoryDownButton : public RippleButton {
public:
	HistoryDownButton(QWidget *parent, const style::TwoIconButton &st);

	void setUnreadCount(int unreadCount);
	int unreadCount() const {
		return _unreadCount;
	}

protected:
	void paintEvent(QPaintEvent *e) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	const style::TwoIconButton &_st;

	int _unreadCount = 0;

};

class EmojiButton : public RippleButton {
public:
	EmojiButton(QWidget *parent, const style::IconButton &st);

	void setLoading(bool loading);
	void setColorOverrides(const style::icon *iconOverride, const style::color *colorOverride, const style::color *rippleOverride);

protected:
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	void loadingAnimationCallback();

	const style::IconButton &_st;

	std::unique_ptr<Ui::InfiniteRadialAnimation> _loading;

	const style::icon *_iconOverride = nullptr;
	const style::color *_colorOverride = nullptr;
	const style::color *_rippleOverride = nullptr;

};

class SendButton : public RippleButton {
public:
	SendButton(QWidget *parent);

	static constexpr auto kSlowmodeDelayLimit = 100 * 60;

	enum class Type {
		Send,
		Schedule,
		Save,
		Record,
		Cancel,
		Slowmode,
	};
	Type type() const {
		return _type;
	}
	void setType(Type state);
	void setRecordActive(bool recordActive);
	void setSlowmodeDelay(int seconds);
	void finishAnimating();

	void setRecordStartCallback(Fn<void()> callback) {
		_recordStartCallback = std::move(callback);
	}
	void setRecordUpdateCallback(Fn<void(QPoint globalPos)> callback) {
		_recordUpdateCallback = std::move(callback);
	}
	void setRecordStopCallback(Fn<void(bool active)> callback) {
		_recordStopCallback = std::move(callback);
	}
	void setRecordAnimationCallback(Fn<void()> callback) {
		_recordAnimationCallback = std::move(callback);
	}

	float64 recordActiveRatio() {
		return _a_recordActive.value(_recordActive ? 1. : 0.);
	}

protected:
	void mouseMoveEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	void recordAnimationCallback();
	QPixmap grabContent();
	bool isSlowmode() const;

	void paintRecord(Painter &p, bool over);
	void paintSave(Painter &p, bool over);
	void paintCancel(Painter &p, bool over);
	void paintSend(Painter &p, bool over);
	void paintSchedule(Painter &p, bool over);
	void paintSlowmode(Painter &p);

	Type _type = Type::Send;
	Type _afterSlowmodeType = Type::Send;
	bool _recordActive = false;
	QPixmap _contentFrom, _contentTo;

	Ui::Animations::Simple _a_typeChanged;
	Ui::Animations::Simple _a_recordActive;

	bool _recording = false;
	Fn<void()> _recordStartCallback;
	Fn<void(bool active)> _recordStopCallback;
	Fn<void(QPoint globalPos)> _recordUpdateCallback;
	Fn<void()> _recordAnimationCallback;

	int _slowmodeDelay = 0;
	QString _slowmodeDelayText;

};

class UserpicButton : public RippleButton {
public:
	enum class Role {
		ChangePhoto,
		OpenPhoto,
		OpenProfile,
		Custom,
	};

	UserpicButton(
		QWidget *parent,
		const QString &cropTitle,
		Role role,
		const style::UserpicButton &st);
	UserpicButton(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		Role role,
		const style::UserpicButton &st);
	UserpicButton(
		QWidget *parent,
		not_null<PeerData*> peer,
		Role role,
		const style::UserpicButton &st);

	void switchChangePhotoOverlay(bool enabled);
	void showSavedMessagesOnSelf(bool enabled);

	QImage takeResultImage() {
		return std::move(_result);
	}

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
	QPoint countPhotoPosition() const;
	void startChangeOverlayAnimation();
	void updateCursorInChangeOverlay(QPoint localPos);
	void setCursorInChangeOverlay(bool inOverlay);
	void updateCursor();
	bool showSavedMessages() const;

	void grabOldUserpic();
	void setClickHandlerByRole();
	void openPeerPhoto();
	void changePhotoLazy();
	void uploadNewPeerPhoto();

	const style::UserpicButton &_st;
	Window::SessionController *_controller = nullptr;
	PeerData *_peer = nullptr;
	QString _cropTitle;
	Role _role = Role::ChangePhoto;
	bool _notShownYet = true;
	bool _waiting = false;
	QPixmap _userpic, _oldUserpic;
	bool _userpicHasImage = false;
	bool _userpicCustom = false;
	InMemoryKey _userpicUniqueKey;
	Ui::Animations::Simple _a_appearance;
	QImage _result;

	bool _showSavedMessagesOnSelf = false;
	bool _canOpenPhoto = false;
	bool _cursorInChangeOverlay = false;
	bool _changeOverlayEnabled = false;
	Ui::Animations::Simple _changeOverlayShown;

};
// // #feed
//class FeedUserpicButton : public AbstractButton {
//public:
//	FeedUserpicButton(
//		QWidget *parent,
//		not_null<Window::SessionController*> controller,
//		not_null<Data::Feed*> feed,
//		const style::FeedUserpicButton &st);
//
//private:
//	struct Part {
//		not_null<ChannelData*> channel;
//		base::unique_qptr<UserpicButton> button;
//	};
//
//	void prepare();
//	void checkParts();
//	bool partsAreValid() const;
//	void refreshParts();
//	QPoint countInnerPosition() const;
//
//	const style::FeedUserpicButton &_st;
//	not_null<Window::SessionController*> _controller;
//	not_null<Data::Feed*> _feed;
//	std::vector<Part> _parts;
//
//};

class SilentToggle : public Ui::IconButton, public Ui::AbstractTooltipShower {
public:
	SilentToggle(QWidget *parent, not_null<ChannelData*> channel);

	void setChecked(bool checked);
	bool checked() const {
		return _checked;
	}

	// AbstractTooltipShower interface
	QString tooltipText() const override;
	QPoint tooltipPos() const override;

protected:
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	void refreshIconOverrides();

	not_null<ChannelData*> _channel;
	bool _checked = false;

};

} // namespace Ui
