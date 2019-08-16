/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/special_buttons.h"

#include "styles/style_boxes.h"
#include "styles/style_history.h"
#include "dialogs/dialogs_layout.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/radial_animation.h"
#include "ui/image/image_prepare.h"
#include "ui/empty_userpic.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_folder.h"
#include "data/data_channel.h"
#include "history/history.h"
#include "core/file_utilities.h"
#include "core/application.h"
#include "boxes/photo_crop_box.h"
#include "boxes/confirm_box.h"
#include "window/window_session_controller.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "observer_peer.h"

namespace Ui {
namespace {

constexpr int kWideScale = 5;

QString CropTitle(not_null<PeerData*> peer) {
	if (peer->isChat() || peer->isMegagroup()) {
		return tr::lng_create_group_crop(tr::now);
	} else if (peer->isChannel()) {
		return tr::lng_create_channel_crop(tr::now);
	} else {
		return tr::lng_settings_crop_profile(tr::now);
	}
}

template <typename Callback>
QPixmap CreateSquarePixmap(int width, Callback &&paintCallback) {
	auto size = QSize(width, width) * cIntRetinaFactor();
	auto image = QImage(size, QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(cRetinaFactor());
	image.fill(Qt::transparent);
	{
		Painter p(&image);
		paintCallback(p);
	}
	return App::pixmapFromImageInPlace(std::move(image));
};

template <typename Callback>
void SuggestPhoto(
		const QImage &image,
		const QString &title,
		Callback &&callback) {
	auto badAspect = [](int a, int b) {
		return (a >= 10 * b);
	};
	if (image.isNull()
		|| badAspect(image.width(), image.height())
		|| badAspect(image.height(), image.width())) {
		Ui::show(
			Box<InformBox>(tr::lng_bad_photo(tr::now)),
			LayerOption::KeepOther);
		return;
	}

	const auto box = Ui::show(
		Box<PhotoCropBox>(image, title),
		LayerOption::KeepOther);
	box->ready(
	) | rpl::start_with_next(
		std::forward<Callback>(callback),
		box->lifetime());
}

template <typename Callback>
void SuggestPhotoFile(
		const FileDialog::OpenResult &result,
		const QString &title,
		Callback &&callback) {
	if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
		return;
	}

	auto image = [&] {
		if (!result.remoteContent.isEmpty()) {
			return App::readImage(result.remoteContent);
		} else if (!result.paths.isEmpty()) {
			return App::readImage(result.paths.front());
		}
		return QImage();
	}();
	SuggestPhoto(
		image,
		title,
		std::forward<Callback>(callback));
}

template <typename Callback>
void ShowChoosePhotoBox(
		QPointer<QWidget> parent,
		const QString &title,
		Callback &&callback) {
	auto imgExtensions = cImgExtensions();
	auto filter = qsl("Image files (*")
		+ imgExtensions.join(qsl(" *"))
		+ qsl(");;")
		+ FileDialog::AllFilesFilter();
	auto handleChosenPhoto = [
		title,
		callback = std::forward<Callback>(callback)
	](auto &&result) mutable {
		SuggestPhotoFile(result, title, std::move(callback));
	};
	FileDialog::GetOpenPath(
		parent,
		tr::lng_choose_image(tr::now),
		filter,
		std::move(handleChosenPhoto));
}

} // namespace

HistoryDownButton::HistoryDownButton(QWidget *parent, const style::TwoIconButton &st) : RippleButton(parent, st.ripple)
, _st(st) {
	resize(_st.width, _st.height);
	setCursor(style::cur_pointer);

	hide();
}

QImage HistoryDownButton::prepareRippleMask() const {
	return Ui::RippleAnimation::ellipseMask(QSize(_st.rippleAreaSize, _st.rippleAreaSize));
}

QPoint HistoryDownButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos()) - _st.rippleAreaPosition;
}

void HistoryDownButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto over = isOver();
	const auto down = isDown();
	((over || down) ? _st.iconBelowOver : _st.iconBelow).paint(p, _st.iconPosition, width());
	paintRipple(p, _st.rippleAreaPosition.x(), _st.rippleAreaPosition.y());
	((over || down) ? _st.iconAboveOver : _st.iconAbove).paint(p, _st.iconPosition, width());
	if (_unreadCount > 0) {
		auto unreadString = QString::number(_unreadCount);

		Dialogs::Layout::UnreadBadgeStyle st;
		st.align = style::al_center;
		st.font = st::historyToDownBadgeFont;
		st.size = st::historyToDownBadgeSize;
		st.sizeId = Dialogs::Layout::UnreadBadgeInHistoryToDown;
		Dialogs::Layout::paintUnreadCount(p, unreadString, width(), 0, st, nullptr, 4);
	}
}

void HistoryDownButton::setUnreadCount(int unreadCount) {
	if (_unreadCount != unreadCount) {
		_unreadCount = unreadCount;
		update();
	}
}

EmojiButton::EmojiButton(QWidget *parent, const style::IconButton &st)
: RippleButton(parent, st.ripple)
, _st(st) {
	resize(_st.width, _st.height);
	setCursor(style::cur_pointer);
}

void EmojiButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(e->rect(), st::historyComposeAreaBg);
	paintRipple(p, _st.rippleAreaPosition.x(), _st.rippleAreaPosition.y(), _rippleOverride ? &(*_rippleOverride)->c : nullptr);

	const auto over = isOver();
	const auto loadingState = _loading
		? _loading->computeState()
		: Ui::RadialState{ 0., 0, FullArcLength };
	if (loadingState.shown < 1.) {
		p.setOpacity(1. - loadingState.shown);

		const auto icon = _iconOverride ? _iconOverride : &(over ? _st.iconOver : _st.icon);
		auto position = _st.iconPosition;
		if (position.x() < 0) {
			position.setX((width() - icon->width()) / 2);
		}
		if (position.y() < 0) {
			position.setY((height() - icon->height()) / 2);
		}
		icon->paint(p, position, width());

		p.setOpacity(1.);
	}

	QRect inner(QPoint((width() - st::historyEmojiCircle.width()) / 2, (height() - st::historyEmojiCircle.height()) / 2), st::historyEmojiCircle);
	const auto color = (_colorOverride
		? *_colorOverride
		: (over
			? st::historyEmojiCircleFgOver
			: st::historyEmojiCircleFg));
	if (anim::Disabled() && _loading && _loading->animating()) {
		anim::DrawStaticLoading(
			p,
			inner,
			st::historyEmojiCircleLine,
			color);
	} else {
		auto pen = color->p;
		pen.setWidth(st::historyEmojiCircleLine);
		pen.setCapStyle(Qt::RoundCap);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);

		PainterHighQualityEnabler hq(p);
		if (loadingState.arcLength < FullArcLength) {
			p.drawArc(inner, loadingState.arcFrom, loadingState.arcLength);
		} else {
			p.drawEllipse(inner);
		}
	}
}

void EmojiButton::loadingAnimationCallback() {
	if (!anim::Disabled()) {
		update();
	}
}

void EmojiButton::setLoading(bool loading) {
	if (loading && !_loading) {
		_loading = std::make_unique<Ui::InfiniteRadialAnimation>(
			[=] { loadingAnimationCallback(); },
			st::defaultInfiniteRadialAnimation);
	}
	if (loading) {
		_loading->start();
		update();
	} else if (_loading) {
		_loading->stop();
		update();
	}
}

void EmojiButton::setColorOverrides(const style::icon *iconOverride, const style::color *colorOverride, const style::color *rippleOverride) {
	_iconOverride = iconOverride;
	_colorOverride = colorOverride;
	_rippleOverride = rippleOverride;
	update();
}

void EmojiButton::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	auto wasOver = static_cast<bool>(was & StateFlag::Over);
	if (isOver() != wasOver) {
		update();
	}
}

QPoint EmojiButton::prepareRippleStartPosition() const {
	if (!_st.rippleAreaSize) {
		return DisabledRippleStartPosition();
	}
	return mapFromGlobal(QCursor::pos()) - _st.rippleAreaPosition;
}

QImage EmojiButton::prepareRippleMask() const {
	return RippleAnimation::ellipseMask(QSize(_st.rippleAreaSize, _st.rippleAreaSize));
}

SendButton::SendButton(QWidget *parent) : RippleButton(parent, st::historyReplyCancel.ripple) {
	resize(st::historySendSize);
}

void SendButton::setType(Type type) {
	Expects(isSlowmode() || type != Type::Slowmode);

	if (isSlowmode() && type != Type::Slowmode) {
		_afterSlowmodeType = type;
		return;
	}
	if (_type != type) {
		_contentFrom = grabContent();
		_type = type;
		_a_typeChanged.stop();
		_contentTo = grabContent();
		_a_typeChanged.start([=] { update(); }, 0., 1., st::historyRecordVoiceDuration);
		setPointerCursor(_type != Type::Slowmode);
		update();
	}
	if (_type != Type::Record) {
		_recordActive = false;
		_a_recordActive.stop();
	}
}

void SendButton::setRecordActive(bool recordActive) {
	if (_recordActive != recordActive) {
		_recordActive = recordActive;
		_a_recordActive.start([this] { recordAnimationCallback(); }, _recordActive ? 0. : 1., _recordActive ? 1. : 0, st::historyRecordVoiceDuration);
		update();
	}
}

void SendButton::setSlowmodeDelay(int seconds) {
	Expects(seconds >= 0 && seconds < kSlowmodeDelayLimit);

	if (_slowmodeDelay == seconds) {
		return;
	}
	_slowmodeDelay = seconds;
	_slowmodeDelayText = isSlowmode()
		? qsl("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'))
		: QString();
	setType(isSlowmode() ? Type::Slowmode : _afterSlowmodeType);
	update();
}

void SendButton::finishAnimating() {
	_a_typeChanged.stop();
	_a_recordActive.stop();
	update();
}

void SendButton::mouseMoveEvent(QMouseEvent *e) {
	AbstractButton::mouseMoveEvent(e);
	if (_recording) {
		if (_recordUpdateCallback) {
			_recordUpdateCallback(e->globalPos());
		}
	}
}

void SendButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto over = (isDown() || isOver());
	auto changed = _a_typeChanged.value(1.);
	if (changed < 1.) {
		PainterHighQualityEnabler hq(p);
		p.setOpacity(1. - changed);
		auto targetRect = QRect((1 - kWideScale) / 2 * width(), (1 - kWideScale) / 2 * height(), kWideScale * width(), kWideScale * height());
		auto hiddenWidth = anim::interpolate(0, (1 - kWideScale) / 2 * width(), changed);
		auto hiddenHeight = anim::interpolate(0, (1 - kWideScale) / 2 * height(), changed);
		p.drawPixmap(targetRect.marginsAdded(QMargins(hiddenWidth, hiddenHeight, hiddenWidth, hiddenHeight)), _contentFrom);
		p.setOpacity(changed);
		auto shownWidth = anim::interpolate((1 - kWideScale) / 2 * width(), 0, changed);
		auto shownHeight = anim::interpolate((1 - kWideScale) / 2 * height(), 0, changed);
		p.drawPixmap(targetRect.marginsAdded(QMargins(shownWidth, shownHeight, shownWidth, shownHeight)), _contentTo);
		return;
	}
	switch (_type) {
	case Type::Record: paintRecord(p, over); break;
	case Type::Save: paintSave(p, over); break;
	case Type::Cancel: paintCancel(p, over); break;
	case Type::Send: paintSend(p, over); break;
	case Type::Schedule: paintSchedule(p, over); break;
	case Type::Slowmode: paintSlowmode(p); break;
	}
}

void SendButton::paintRecord(Painter &p, bool over) {
	auto recordActive = recordActiveRatio();
	if (!isDisabled()) {
		auto rippleColor = anim::color(st::historyAttachEmoji.ripple.color, st::historyRecordVoiceRippleBgActive, recordActive);
		paintRipple(p, (width() - st::historyAttachEmoji.rippleAreaSize) / 2, st::historyAttachEmoji.rippleAreaPosition.y(), &rippleColor);
	}

	auto fastIcon = [&] {
		if (isDisabled()) {
			return &st::historyRecordVoice;
		} else if (recordActive == 1.) {
			return &st::historyRecordVoiceActive;
		} else if (over) {
			return &st::historyRecordVoiceOver;
		}
		return &st::historyRecordVoice;
	};
	fastIcon()->paintInCenter(p, rect());
	if (!isDisabled() && recordActive > 0. && recordActive < 1.) {
		p.setOpacity(recordActive);
		st::historyRecordVoiceActive.paintInCenter(p, rect());
		p.setOpacity(1.);
	}
}

void SendButton::paintSave(Painter &p, bool over) {
	const auto &saveIcon = over
		? st::historyEditSaveIconOver
		: st::historyEditSaveIcon;
	saveIcon.paint(p, st::historySendIconPosition, width());
}

void SendButton::paintCancel(Painter &p, bool over) {
	paintRipple(p, (width() - st::historyAttachEmoji.rippleAreaSize) / 2, st::historyAttachEmoji.rippleAreaPosition.y());

	const auto &cancelIcon = over
		? st::historyReplyCancelIconOver
		: st::historyReplyCancelIcon;
	cancelIcon.paintInCenter(p, rect());
}

void SendButton::paintSend(Painter &p, bool over) {
	const auto &sendIcon = over
		? st::historySendIconOver
		: st::historySendIcon;
	if (isDisabled()) {
		const auto color = st::historyRecordVoiceFg->c;
		sendIcon.paint(p, st::historySendIconPosition, width(), color);
	} else {
		sendIcon.paint(p, st::historySendIconPosition, width());
	}
}

void SendButton::paintSchedule(Painter &p, bool over) {
	{
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(over ? st::historySendIconFgOver : st::historySendIconFg);
		p.drawEllipse(
			st::historyScheduleIconPosition.x(),
			st::historyScheduleIconPosition.y(),
			st::historyScheduleIcon.width(),
			st::historyScheduleIcon.height());
	}
	st::historyScheduleIcon.paint(
		p,
		st::historyScheduleIconPosition,
		width());
}

void SendButton::paintSlowmode(Painter &p) {
	p.setFont(st::normalFont);
	p.setPen(st::windowSubTextFg);
	p.drawText(
		rect().marginsRemoved(st::historySlowmodeCounterMargins),
		_slowmodeDelayText,
		style::al_center);
}

void SendButton::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);

	auto down = (state() & StateFlag::Down);
	if ((was & StateFlag::Down) != down) {
		if (down) {
			if (_type == Type::Record) {
				_recording = true;
				if (_recordStartCallback) {
					_recordStartCallback();
				}
			}
		} else if (_recording) {
			_recording = false;
			if (_recordStopCallback) {
				_recordStopCallback(_recordActive);
			}
		}
	}
}

bool SendButton::isSlowmode() const {
	return (_slowmodeDelay > 0);
}

QPixmap SendButton::grabContent() {
	auto result = QImage(kWideScale * size() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	{
		Painter p(&result);
		p.drawPixmap(
			(kWideScale - 1) / 2 * width(),
			(kWideScale - 1) / 2 * height(),
			GrabWidget(this));
	}
	return App::pixmapFromImageInPlace(std::move(result));
}

QImage SendButton::prepareRippleMask() const {
	auto size = (_type == Type::Record) ? st::historyAttachEmoji.rippleAreaSize : st::historyReplyCancel.rippleAreaSize;
	return Ui::RippleAnimation::ellipseMask(QSize(size, size));
}

QPoint SendButton::prepareRippleStartPosition() const {
	auto real = mapFromGlobal(QCursor::pos());
	auto size = (_type == Type::Record) ? st::historyAttachEmoji.rippleAreaSize : st::historyReplyCancel.rippleAreaSize;
	auto y = (_type == Type::Record) ? st::historyAttachEmoji.rippleAreaPosition.y() : (height() - st::historyReplyCancel.rippleAreaSize) / 2;
	return real - QPoint((width() - size) / 2, y);
}

void SendButton::recordAnimationCallback() {
	update();
	if (_recordAnimationCallback) {
		_recordAnimationCallback();
	}
}

UserpicButton::UserpicButton(
	QWidget *parent,
	const QString &cropTitle,
	Role role,
	const style::UserpicButton &st)
: RippleButton(parent, st.changeButton.ripple)
, _st(st)
, _cropTitle(cropTitle)
, _role(role) {
	Expects(_role == Role::ChangePhoto);

	_waiting = false;
	prepare();
}

UserpicButton::UserpicButton(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	Role role,
	const style::UserpicButton &st)
: RippleButton(parent, st.changeButton.ripple)
, _st(st)
, _controller(controller)
, _peer(peer)
, _cropTitle(CropTitle(_peer))
, _role(role) {
	processPeerPhoto();
	prepare();
	setupPeerViewers();
}

UserpicButton::UserpicButton(
	QWidget *parent,
	not_null<PeerData*> peer,
	Role role,
	const style::UserpicButton &st)
: RippleButton(parent, st.changeButton.ripple)
, _st(st)
, _peer(peer)
, _cropTitle(CropTitle(_peer))
, _role(role) {
	Expects(_role != Role::OpenProfile);

	_waiting = false;
	prepare();
}

void UserpicButton::prepare() {
	resize(_st.size);
	_notShownYet = _waiting;
	if (!_waiting) {
		prepareUserpicPixmap();
	}
	setClickHandlerByRole();
}

void UserpicButton::setClickHandlerByRole() {
	switch (_role) {
	case Role::ChangePhoto:
		addClickHandler(App::LambdaDelayed(
			_st.changeButton.ripple.hideDuration,
			this,
			[this] { changePhotoLazy(); }));
		break;

	case Role::OpenPhoto:
		addClickHandler([this] {
			openPeerPhoto();
		});
		break;

	case Role::OpenProfile:
		addClickHandler([this] {
			Expects(_controller != nullptr);

			_controller->showPeerInfo(_peer);
		});
		break;
	}
}

void UserpicButton::changePhotoLazy() {
	auto callback = crl::guard(
		this,
		[this](QImage &&image) { setImage(std::move(image)); });
	ShowChoosePhotoBox(this, _cropTitle, std::move(callback));
}

void UserpicButton::uploadNewPeerPhoto() {
	auto callback = crl::guard(this, [=](QImage &&image) {
		_peer->session().api().uploadPeerPhoto(_peer, std::move(image));
	});
	ShowChoosePhotoBox(this, _cropTitle, std::move(callback));
}

void UserpicButton::openPeerPhoto() {
	Expects(_peer != nullptr);
	Expects(_controller != nullptr);

	if (_changeOverlayEnabled && _cursorInChangeOverlay) {
		uploadNewPeerPhoto();
		return;
	}

	const auto id = _peer->userpicPhotoId();
	if (!id) {
		return;
	}
	const auto photo = _peer->owner().photo(id);
	if (photo->date) {
		Core::App().showPhoto(photo, _peer);
	}
}

void UserpicButton::setupPeerViewers() {
	Notify::PeerUpdateViewer(
		_peer,
		Notify::PeerUpdate::Flag::PhotoChanged
	) | rpl::start_with_next([this] {
		processNewPeerPhoto();
		update();
	}, lifetime());

	base::ObservableViewer(
		_peer->session().downloaderTaskFinished()
	) | rpl::start_with_next([this] {
		if (_waiting && _peer->userpicLoaded()) {
			_waiting = false;
			startNewPhotoShowing();
		}
	}, lifetime());
}

void UserpicButton::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (!_waiting && _notShownYet) {
		_notShownYet = false;
		startAnimation();
	}

	auto photoPosition = countPhotoPosition();
	auto photoLeft = photoPosition.x();
	auto photoTop = photoPosition.y();

	if (showSavedMessages()) {
		Ui::EmptyUserpic::PaintSavedMessages(
			p,
			photoPosition.x(),
			photoPosition.y(),
			width(),
			_st.photoSize);
	} else {
		if (_a_appearance.animating()) {
			p.drawPixmapLeft(photoPosition, width(), _oldUserpic);
			p.setOpacity(_a_appearance.value(1.));
		}
		p.drawPixmapLeft(photoPosition, width(), _userpic);
	}

	if (_role == Role::ChangePhoto) {
		auto over = isOver() || isDown();
		if (over) {
			PainterHighQualityEnabler hq(p);
			p.setPen(Qt::NoPen);
			p.setBrush(_userpicHasImage
				? st::msgDateImgBg
				: _st.changeButton.textBgOver);
			p.drawEllipse(
				photoLeft,
				photoTop,
				_st.photoSize,
				_st.photoSize);
		}
		paintRipple(
			p,
			photoLeft,
			photoTop,
			_userpicHasImage
				? &st::shadowFg->c
				: &_st.changeButton.ripple.color->c);
		if (over || !_userpicHasImage) {
			auto iconLeft = (_st.changeIconPosition.x() < 0)
				? (_st.photoSize - _st.changeIcon.width()) / 2
				: _st.changeIconPosition.x();
			auto iconTop = (_st.changeIconPosition.y() < 0)
				? (_st.photoSize - _st.changeIcon.height()) / 2
				: _st.changeIconPosition.y();
			_st.changeIcon.paint(
				p,
				photoLeft + iconLeft,
				photoTop + iconTop,
				width());
		}
	} else if (_changeOverlayEnabled) {
		auto current = _changeOverlayShown.value(
			(isOver() || isDown()) ? 1. : 0.);
		auto barHeight = anim::interpolate(
			0,
			_st.uploadHeight,
			current);
		if (barHeight > 0) {
			auto barLeft = photoLeft;
			auto barTop = photoTop + _st.photoSize - barHeight;
			auto rect = QRect(
				barLeft,
				barTop,
				_st.photoSize,
				barHeight);
			p.setClipRect(rect);
			{
				PainterHighQualityEnabler hq(p);
				p.setPen(Qt::NoPen);
				p.setBrush(_st.uploadBg);
				p.drawEllipse(
					photoLeft,
					photoTop,
					_st.photoSize,
					_st.photoSize);
			}
			auto iconLeft = (_st.uploadIconPosition.x() < 0)
				? (_st.photoSize - _st.uploadIcon.width()) / 2
				: _st.uploadIconPosition.x();
			auto iconTop = (_st.uploadIconPosition.y() < 0)
				? (_st.uploadHeight - _st.uploadIcon.height()) / 2
				: _st.uploadIconPosition.y();
			if (iconTop < barHeight) {
				_st.uploadIcon.paint(
					p,
					barLeft + iconLeft,
					barTop + iconTop,
					width());
			}
		}
	}
}

QPoint UserpicButton::countPhotoPosition() const {
	auto photoLeft = (_st.photoPosition.x() < 0)
		? (width() - _st.photoSize) / 2
		: _st.photoPosition.x();
	auto photoTop = (_st.photoPosition.y() < 0)
		? (height() - _st.photoSize) / 2
		: _st.photoPosition.y();
	return { photoLeft, photoTop };
}

QImage UserpicButton::prepareRippleMask() const {
	return Ui::RippleAnimation::ellipseMask(QSize(
		_st.photoSize,
		_st.photoSize));
}

QPoint UserpicButton::prepareRippleStartPosition() const {
	return (_role == Role::ChangePhoto)
		? mapFromGlobal(QCursor::pos()) - countPhotoPosition()
		: DisabledRippleStartPosition();
}

void UserpicButton::processPeerPhoto() {
	Expects(_peer != nullptr);

	_waiting = !_peer->userpicLoaded();
	if (_waiting) {
		_peer->loadUserpic();
	}
	if (_role == Role::OpenPhoto) {
		if (_peer->userpicPhotoUnknown()) {
			_peer->updateFullForced();
		}
		_canOpenPhoto = (_peer->userpicPhotoId() != 0);
		updateCursor();
	}
}

void UserpicButton::updateCursor() {
	Expects(_role == Role::OpenPhoto);

	auto pointer = _canOpenPhoto
		|| (_changeOverlayEnabled && _cursorInChangeOverlay);
	setPointerCursor(pointer);
}

void UserpicButton::mouseMoveEvent(QMouseEvent *e) {
	RippleButton::mouseMoveEvent(e);
	if (_role == Role::OpenPhoto) {
		updateCursorInChangeOverlay(e->pos());
	}
}

void UserpicButton::updateCursorInChangeOverlay(QPoint localPos) {
	auto photoPosition = countPhotoPosition();
	auto overlayRect = QRect(
		photoPosition.x(),
		photoPosition.y() + _st.photoSize - _st.uploadHeight,
		_st.photoSize,
		_st.uploadHeight);
	auto inOverlay = overlayRect.contains(localPos);
	setCursorInChangeOverlay(inOverlay);
}

void UserpicButton::leaveEventHook(QEvent *e) {
	if (_role == Role::OpenPhoto) {
		setCursorInChangeOverlay(false);
	}
	return RippleButton::leaveEventHook(e);
}

void UserpicButton::setCursorInChangeOverlay(bool inOverlay) {
	Expects(_role == Role::OpenPhoto);

	if (_cursorInChangeOverlay != inOverlay) {
		_cursorInChangeOverlay = inOverlay;
		updateCursor();
	}
}

void UserpicButton::processNewPeerPhoto() {
	if (_userpicCustom) {
		return;
	}
	processPeerPhoto();
	if (!_waiting) {
		grabOldUserpic();
		startNewPhotoShowing();
	}
}

void UserpicButton::grabOldUserpic() {
	auto photoRect = QRect(
		countPhotoPosition(),
		QSize(_st.photoSize, _st.photoSize)
	);
	_oldUserpic = GrabWidget(this, photoRect);
}

void UserpicButton::startNewPhotoShowing() {
	auto oldUniqueKey = _userpicUniqueKey;
	prepareUserpicPixmap();
	update();

	if (_notShownYet) {
		return;
	}
	if (oldUniqueKey != _userpicUniqueKey
		|| _a_appearance.animating()) {
		startAnimation();
	}
}

void UserpicButton::startAnimation() {
	_a_appearance.stop();
	_a_appearance.start([this] { update(); }, 0, 1, _st.duration);
}

void UserpicButton::switchChangePhotoOverlay(bool enabled) {
	Expects(_role == Role::OpenPhoto);

	if (_changeOverlayEnabled != enabled) {
		_changeOverlayEnabled = enabled;
		if (enabled) {
			if (isOver()) {
				startChangeOverlayAnimation();
			}
			updateCursorInChangeOverlay(
				mapFromGlobal(QCursor::pos()));
		} else {
			_changeOverlayShown.stop();
			update();
		}
	}
}

void UserpicButton::showSavedMessagesOnSelf(bool enabled) {
	if (_showSavedMessagesOnSelf != enabled) {
		_showSavedMessagesOnSelf = enabled;
		update();
	}
}

bool UserpicButton::showSavedMessages() const {
	return _showSavedMessagesOnSelf && _peer && _peer->isSelf();
}

void UserpicButton::startChangeOverlayAnimation() {
	auto over = isOver() || isDown();
	_changeOverlayShown.start(
		[this] { update(); },
		over ? 0. : 1.,
		over ? 1. : 0.,
		st::slideWrapDuration);
	update();
}

void UserpicButton::onStateChanged(
		State was,
		StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	if (_changeOverlayEnabled) {
		auto mask = (StateFlag::Over | StateFlag::Down);
		auto wasOver = (was & mask) != 0;
		auto nowOver = (state() & mask) != 0;
		if (wasOver != nowOver) {
			startChangeOverlayAnimation();
		}
	}
}

void UserpicButton::setImage(QImage &&image) {
	grabOldUserpic();

	auto size = QSize(_st.photoSize, _st.photoSize);
	auto small = image.scaled(
		size * cIntRetinaFactor(),
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	Images::prepareCircle(small);
	_userpic = App::pixmapFromImageInPlace(std::move(small));
	_userpic.setDevicePixelRatio(cRetinaFactor());
	_userpicCustom = _userpicHasImage = true;
	_result = std::move(image);

	startNewPhotoShowing();
}

void UserpicButton::prepareUserpicPixmap() {
	if (_userpicCustom) {
		return;
	}
	auto size = _st.photoSize;
	auto paintButton = [&](Painter &p, const style::color &color) {
		PainterHighQualityEnabler hq(p);
		p.setBrush(color);
		p.setPen(Qt::NoPen);
		p.drawEllipse(0, 0, size, size);
	};
	_userpicHasImage = _peer
		? (_peer->currentUserpic() || _role != Role::ChangePhoto)
		: false;
	_userpic = CreateSquarePixmap(size, [&](Painter &p) {
		if (_userpicHasImage) {
			_peer->paintUserpic(p, 0, 0, _st.photoSize);
		} else {
			paintButton(p, _st.changeButton.textBg);
		}
	});
	_userpicUniqueKey = _userpicHasImage
		? _peer->userpicUniqueKey()
		: InMemoryKey();
}
// // #feed
//FeedUserpicButton::FeedUserpicButton(
//	QWidget *parent,
//	not_null<Window::SessionController*> controller,
//	not_null<Data::Feed*> feed,
//	const style::FeedUserpicButton &st)
//: AbstractButton(parent)
//, _st(st)
//, _controller(controller)
//, _feed(feed) {
//	prepare();
//}
//
//void FeedUserpicButton::prepare() {
//	resize(_st.size);
//
//	_feed->owner().feedUpdated(
//	) | rpl::filter([=](const Data::FeedUpdate &update) {
//		return (update.feed == _feed)
//			&& (update.flag == Data::FeedUpdateFlag::Channels);
//	}) | rpl::start_with_next([=] {
//		crl::on_main(this, [=] { checkParts(); });
//	}, lifetime());
//
//	refreshParts();
//}
//
//void FeedUserpicButton::checkParts() {
//	if (!partsAreValid()) {
//		refreshParts();
//	}
//}
//
//bool FeedUserpicButton::partsAreValid() const {
//	const auto &channels = _feed->channels();
//	const auto count = std::min(int(channels.size()), 4);
//	if (count != _parts.size()) {
//		return false;
//	}
//	for (auto i = 0; i != count; ++i) {
//		if (channels[i]->peer != _parts[i].channel) {
//			return false;
//		}
//	}
//	return true;
//}
//
//void FeedUserpicButton::refreshParts() {
//	const auto &channels = _feed->channels();
//	const auto count = std::min(int(channels.size()), 4);
//
//	const auto createButton = [&](not_null<ChannelData*> channel) {
//		auto result = base::make_unique_q<Ui::UserpicButton>(
//			this,
//			_controller,
//			channel,
//			Ui::UserpicButton::Role::Custom,
//			_st.innerPart);
//		result->setAttribute(Qt::WA_TransparentForMouseEvents);
//		result->show();
//		return result;
//	};
//
//	const auto position = countInnerPosition();
//	auto x = position.x();
//	auto y = position.y();
//	const auto delta = _st.innerSize - _st.innerPart.photoSize;
//	_parts.clear();
//	for (auto i = 0; i != count; ++i) {
//		const auto channel = channels[i]->peer->asChannel();
//		_parts.push_back({ channel, createButton(channel) });
//		_parts.back().button->moveToLeft(x, y);
//		switch (i) {
//		case 0:
//		case 2: x += delta; break;
//		case 1: x -= delta; y += delta; break;
//		}
//	}
//}
//
//QPoint FeedUserpicButton::countInnerPosition() const {
//	auto innerLeft = (_st.innerPosition.x() < 0)
//		? (width() - _st.innerSize) / 2
//		: _st.innerPosition.x();
//	auto innerTop = (_st.innerPosition.y() < 0)
//		? (height() - _st.innerSize) / 2
//		: _st.innerPosition.y();
//	return { innerLeft, innerTop };
//}

SilentToggle::SilentToggle(QWidget *parent, not_null<ChannelData*> channel)
: IconButton(parent, st::historySilentToggle)
, _channel(channel)
, _checked(channel->owner().notifySilentPosts(_channel)) {
	Expects(!channel->owner().notifySilentPostsUnknown(_channel));

	if (_checked) {
		refreshIconOverrides();
	}
	setMouseTracking(true);
}

void SilentToggle::mouseMoveEvent(QMouseEvent *e) {
	IconButton::mouseMoveEvent(e);
	if (rect().contains(e->pos())) {
		Ui::Tooltip::Show(1000, this);
	} else {
		Ui::Tooltip::Hide();
	}
}

void SilentToggle::setChecked(bool checked) {
	if (_checked != checked) {
		_checked = checked;
		refreshIconOverrides();
	}
}

void SilentToggle::refreshIconOverrides() {
	const auto iconOverride = _checked
		? &st::historySilentToggleOn
		: nullptr;
	const auto iconOverOverride = _checked
		? &st::historySilentToggleOnOver
		: nullptr;
	setIconOverride(iconOverride, iconOverOverride);
}

void SilentToggle::leaveEventHook(QEvent *e) {
	IconButton::leaveEventHook(e);
	Ui::Tooltip::Hide();
}

void SilentToggle::mouseReleaseEvent(QMouseEvent *e) {
	setChecked(!_checked);
	IconButton::mouseReleaseEvent(e);
	Ui::Tooltip::Show(0, this);
	_channel->owner().updateNotifySettings(
		_channel,
		std::nullopt,
		_checked);
}

QString SilentToggle::tooltipText() const {
	return _checked
		? tr::lng_wont_be_notified(tr::now)
		: tr::lng_will_be_notified(tr::now);
}

QPoint SilentToggle::tooltipPos() const {
	return QCursor::pos();
}

} // namespace Ui
