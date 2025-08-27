/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/userpic_button.h"

#include "apiwrap.h"
#include "api/api_user_privacy.h"
#include "base/call_delayed.h"
#include "boxes/edit_privacy_box.h"
#include "boxes/peers/edit_peer_info_box.h" // EditPeerInfoBox::Available.
#include "ui/effects/ripple_animation.h"
#include "ui/empty_userpic.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_user.h"
#include "data/data_histories.h"
#include "data/data_streaming.h"
#include "data/data_file_origin.h"
#include "data/data_photo_media.h"
#include "history/history.h"
#include "calls/calls_instance.h"
#include "core/application.h"
#include "ui/effects/premium_graphics.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "editor/photo_editor_common.h"
#include "editor/photo_editor_layer_widget.h"
#include "info/userpic/info_userpic_emoji_builder_common.h"
#include "info/userpic/info_userpic_emoji_builder_menu_item.h"
#include "media/streaming/media_streaming_instance.h"
#include "media/streaming/media_streaming_player.h"
#include "media/streaming/media_streaming_document.h"
#include "settings/settings_calls.h" // Calls::AddCameraSubsection.
#include "settings/settings_privacy_controllers.h"
#include "webrtc/webrtc_environment.h"
#include "webrtc/webrtc_video_track.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "api/api_peer_photo.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_menu_icons.h"
#include "styles/style_premium.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace Ui {
namespace {

[[nodiscard]] bool IsCameraAvailable() {
	return (Core::App().calls().currentCall() == nullptr)
		&& !Core::App().mediaDevices().defaultId(
			Webrtc::DeviceType::Camera).isEmpty();
}

void CameraBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::Controller*> controller,
		PeerData *peer,
		bool forceForumShape,
		Fn<void(QImage &&image)> &&doneCallback) {
	using namespace Webrtc;

	const auto track = Settings::Calls::AddCameraSubsection(
		box->uiShow(),
		box->verticalLayout(),
		false);
	if (!track) {
		box->closeBox();
		return;
	}
	track->stateValue(
	) | rpl::start_with_next([=](const VideoState &state) {
		if (state == VideoState::Inactive) {
			box->closeBox();
		}
	}, box->lifetime());

	auto done = [=, done = std::move(doneCallback)]() mutable {
		using namespace Editor;
		auto callback = [=, done = std::move(done)](QImage &&image) {
			box->closeBox();
			done(std::move(image));
		};
		const auto useForumShape = forceForumShape
			|| (peer && peer->isForum());
		PrepareProfilePhoto(
			box,
			controller,
			{
				.confirm = tr::lng_profile_set_photo_button(tr::now),
				.cropType = (useForumShape
					? EditorData::CropType::RoundedRect
					: EditorData::CropType::Ellipse),
				.keepAspectRatio = true,
			},
			std::move(callback),
			track->frame(FrameRequest()).mirrored(true, false));
	};

	box->setTitle(tr::lng_profile_camera_title());
	box->addButton(tr::lng_continue(), std::move(done));
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

template <typename Callback>
QPixmap CreateSquarePixmap(int width, Callback &&paintCallback) {
	const auto size = QSize(width, width) * style::DevicePixelRatio();
	auto image = QImage(size, QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(style::DevicePixelRatio());
	image.fill(Qt::transparent);
	{
		Painter p(&image);
		paintCallback(p);
	}
	return Ui::PixmapFromImage(std::move(image));
};

void SetupSubButtonBackground(
		not_null<Ui::UserpicButton*> upload,
		not_null<Ui::RpWidget*> background) {
	const auto border = st::uploadUserpicButtonBorder;
	const auto size = upload->rect().marginsAdded(
		{ border, border, border, border }
	).size();

	background->resize(size);
	background->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(background);
		auto hq = PainterHighQualityEnabler(p);
		p.setBrush(st::boxBg);
		p.setPen(Qt::NoPen);
		p.drawEllipse(background->rect());
	}, background->lifetime());

	upload->positionValue(
	) | rpl::start_with_next([=](QPoint position) {
		background->move(position - QPoint(border, border));
	}, background->lifetime());
}

} // namespace

UserpicButton::UserpicButton(
	QWidget *parent,
	not_null<Window::Controller*> window,
	Role role,
	const style::UserpicButton &st,
	PeerUserpicShape shape)
: RippleButton(parent, st.changeButton.ripple)
, _st(st)
, _controller(window->sessionController())
, _window(window)
, _shape(shape)
, _role(role) {
	Expects(_role == Role::ChangePhoto || _role == Role::ChoosePhoto);

	showCustom({});
	_waiting = false;
	prepare();
}

UserpicButton::UserpicButton(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	Role role,
	Source source,
	const style::UserpicButton &st,
	PeerUserpicShape shape)
: RippleButton(parent, st.changeButton.ripple)
, _st(st)
, _controller(controller)
, _window(&controller->window())
, _peer(peer)
, _shape(shape)
, _role(role)
, _source(source) {
	if (_source == Source::Custom) {
		showCustom({});
	} else {
		processPeerPhoto();
		setupPeerViewers();
	}
	prepare();
}

UserpicButton::UserpicButton(
	QWidget *parent,
	not_null<PeerData*> peer,
	const style::UserpicButton &st,
	PeerUserpicShape shape)
: RippleButton(parent, st.changeButton.ripple)
, _st(st)
, _peer(peer)
, _shape(shape)
, _role(Role::Custom)
, _source(Source::PeerPhoto) {
	Expects(_role != Role::OpenPhoto);

	processPeerPhoto();
	setupPeerViewers();
	prepare();
}

UserpicButton::~UserpicButton() = default;

void UserpicButton::prepare() {
	resize(_st.size);
	setNaturalWidth(_st.size.width());
	_notShownYet = _waiting;
	if (!_waiting) {
		prepareUserpicPixmap();
	}
	setClickHandlerByRole();
}

void UserpicButton::showCustomOnChosen() {
	chosenImages(
	) | rpl::start_with_next([=](ChosenImage &&chosen) {
		showCustom(std::move(chosen.image));
	}, lifetime());
}

void UserpicButton::requestSuggestAvailability() {
	if (const auto user = _peer ? _peer->asUser() : nullptr) {
		if (!user->isSelf()) {
			const auto history = user->owner().history(user);
			if (!history->lastServerMessageKnown()) {
				// Server allows suggesting photos only in non-empty chats.
				user->owner().histories().requestDialogEntry(history);
			}
		}
	}
}

bool UserpicButton::canSuggestPhoto(not_null<UserData*> user) const {
	// Server allows suggesting photos only in non-empty chats.
	return !user->isSelf()
		&& !user->isBot()
		&& !user->starsPerMessageChecked()
		&& (user->owner().history(user)->lastServerMessage() != nullptr);
}

bool UserpicButton::hasPersonalPhotoLocally() const {
	if (const auto user = _peer->asUser()) {
		return _overrideHasPersonalPhoto.value_or(user->hasPersonalPhoto());
	}
	return false;
}

void UserpicButton::setClickHandlerByRole() {
	requestSuggestAvailability();

	switch (_role) {
	case Role::ChoosePhoto:
	case Role::ChangePhoto:
		addClickHandler([=] { choosePhotoLocally(); });
		break;

	case Role::OpenPhoto:
		addClickHandler([=] { openPeerPhoto(); });
		break;
	}
}

void UserpicButton::choosePhotoLocally() {
	if (!_window) {
		return;
	} else if (const auto controller = _window->sessionController()) {
		if (controller->showFrozenError()) {
			return;
		}
	}
	const auto callback = [=](ChosenType type) {
		return [=](QImage &&image) {
			_chosenImages.fire({ std::move(image), type });
		};
	};
	const auto editorData = [=](ChosenType type) {
		const auto user = _peer ? _peer->asUser() : nullptr;
		const auto name = (user && !user->firstName.isEmpty())
			? user->firstName
			: _peer
			? _peer->name()
			: QString();
		const auto phrase = (type == ChosenType::Suggest)
			? &tr::lng_profile_suggest_sure
			: (user && EditPeerInfoBox::Available(user))
			? nullptr
			: (user && !user->isSelf())
			? &tr::lng_profile_set_personal_sure
			: nullptr;
		return Editor::EditorData{
			.about = (phrase
				? (*phrase)(
					tr::now,
					lt_user,
					Ui::Text::Bold(name),
					Ui::Text::WithEntities)
				: TextWithEntities()),
			.confirm = ((type == ChosenType::Suggest)
				? tr::lng_profile_suggest_button(tr::now)
				: tr::lng_profile_set_photo_button(tr::now)),
			.cropType = (useForumShape()
				? Editor::EditorData::CropType::RoundedRect
				: Editor::EditorData::CropType::Ellipse),
			.keepAspectRatio = true,
		};
	};
	const auto chooseFile = [=](ChosenType type) {
		base::call_delayed(
			_st.changeButton.ripple.hideDuration,
			crl::guard(this, [=] {
				PrepareProfilePhotoFromFile(
					this,
					_window,
					editorData(type),
					callback(type));
			}));
	};
	const auto user = _peer ? _peer->asUser() : nullptr;
	const auto addUserpicBuilder = [&](ChosenType type) {
		if (!_controller) {
			return;
		}
		const auto done = [=](UserpicBuilder::Result data) {
			auto result = ChosenImage{ base::take(data.image), type };
			result.markup.documentId = data.id;
			result.markup.colors = base::take(data.colors);
			_chosenImages.fire(std::move(result));
		};
		UserpicBuilder::AddEmojiBuilderAction(
			_controller,
			_menu,
			_controller->session().api().peerPhoto().emojiListValue(user
				? Api::PeerPhoto::EmojiListType::Profile
				: Api::PeerPhoto::EmojiListType::Group),
			done,
			_peer ? _peer->isForum() : false);
	};
	const auto addFromClipboard = [=](ChosenType type, tr::phrase<> text) {
		if (const auto data = QGuiApplication::clipboard()->mimeData()) {
			if (data->hasImage()) {
				auto openEditor = crl::guard(this, [=, this] {
					Editor::PrepareProfilePhoto(
						this,
						_window,
						editorData(type),
						callback(type),
						qvariant_cast<QImage>(data->imageData()));
				});
				_menu->addAction(
					std::move(text)(tr::now),
					std::move(openEditor),
					&st::menuIconPhoto);
			}
		}
	};
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	if (user && !user->isSelf()) {
		_menu->addAction(
			tr::lng_profile_set_photo_for(tr::now),
			[=] { chooseFile(ChosenType::Set); },
			&st::menuIconPhotoSet);
		addFromClipboard(
			ChosenType::Set,
			tr::lng_profile_set_photo_for_from_clipboard);
		if (canSuggestPhoto(user)) {
			_menu->addAction(
				tr::lng_profile_suggest_photo(tr::now),
				[=] { chooseFile(ChosenType::Suggest); },
				&st::menuIconPhotoSuggest);
			addFromClipboard(
				ChosenType::Suggest,
				tr::lng_profile_suggest_photo_from_clipboard);
		}
		addUserpicBuilder(ChosenType::Set);
		if (hasPersonalPhotoLocally()) {
			_menu->addSeparator(&st::expandedMenuSeparator);
			_menu->addAction(makeResetToOriginalAction());
		}
	} else {
		const auto hasCamera = IsCameraAvailable();
		if (hasCamera || _controller) {
			_menu->addAction(tr::lng_attach_file(tr::now), [=] {
				chooseFile(ChosenType::Set);
			}, &st::menuIconPhoto);
			if (hasCamera) {
				_menu->addAction(tr::lng_attach_camera(tr::now), [=] {
					_window->show(Box(
						CameraBox,
						_window,
						_peer,
						(_shape == PeerUserpicShape::Forum),
						callback(ChosenType::Set)));
				}, &st::menuIconPhotoSet);
			}
			addFromClipboard(
				ChosenType::Set,
				tr::lng_profile_photo_from_clipboard);
			addUserpicBuilder(ChosenType::Set);
		} else {
			chooseFile(ChosenType::Set);
		}
		if (user && user->isSelf()) {
			const auto key = Api::UserPrivacy::Key::ProfilePhoto;
			const auto text = tr::lng_edit_privacy_profile_photo_public_set(
				tr::now);
			user->session().api().userPrivacy().reload(key);
			_menu->addAction(std::move(text), [=] {
				using namespace Api;
				user->session().api().userPrivacy().value(
					key
				) | rpl::take(
					1
				) | rpl::start_with_next([=](const UserPrivacy::Rule &value) {
					using namespace Settings;
					_window->show(Box<EditPrivacyBox>(
						_window->sessionController(),
						std::make_unique<ProfilePhotoPrivacyController>(),
						value));
				}, _menu->lifetime());
			}, &st::menuIconProfile);
		}
	}
	_menu->popup(QCursor::pos());
}

auto UserpicButton::makeResetToOriginalAction()
-> base::unique_qptr<Menu::ItemBase> {
	auto item = base::make_unique_q<Menu::Action>(
		_menu.get(),
		_menu->st().menu,
		Menu::CreateAction(
			_menu.get(),
			tr::lng_profile_photo_reset(tr::now),
			[=] { _resetPersonalRequests.fire({}); }),
		nullptr,
		nullptr);
	const auto icon = CreateChild<UserpicButton>(
		item.get(),
		_controller,
		_peer,
		Ui::UserpicButton::Role::Custom,
		Ui::UserpicButton::Source::NonPersonalIfHasPersonal,
		st::restoreUserpicIcon);
	if (_source == Source::Custom) {
		icon->showCustom(base::duplicate(_result));
	}
	icon->setAttribute(Qt::WA_TransparentForMouseEvents);
	icon->move(_menu->st().menu.itemIconPosition
		+ QPoint(
			(st::menuIconRemove.width() - icon->width()) / 2,
			(st::menuIconRemove.height() - icon->height()) / 2));
	return item;
}

void UserpicButton::openPeerPhoto() {
	Expects(_peer != nullptr);
	Expects(_controller != nullptr);

	if (_changeOverlayEnabled && _cursorInChangeOverlay) {
		choosePhotoLocally();
		return;
	}

	const auto id = _peer->userpicPhotoId();
	if (!id) {
		return;
	}
	const auto photo = _peer->owner().photo(id);
	if (photo->date() && _controller) {
		_controller->openPhoto(photo, _peer);
	}
}

void UserpicButton::setupPeerViewers() {
	const auto user = _peer->asUser();
	if (user
		&& (_source == Source::NonPersonalPhoto
			|| _source == Source::NonPersonalIfHasPersonal)) {
		user->session().changes().peerFlagsValue(
			user,
			Data::PeerUpdate::Flag::FullInfo
		) | rpl::map([=] {
			return std::pair(
				user->session().api().peerPhoto().nonPersonalPhoto(user),
				user->hasPersonalPhoto());
		}) | rpl::distinct_until_changed() | rpl::skip(
			1
		) | rpl::start_with_next([=] {
			processNewPeerPhoto();
			update();
		}, _sourceLifetime);
	}
	if (!user
		|| _source == Source::PeerPhoto
		|| _source == Source::NonPersonalIfHasPersonal) {
		_peer->session().changes().peerUpdates(
			_peer,
			Data::PeerUpdate::Flag::Photo
		) | rpl::start_with_next([=] {
			processNewPeerPhoto();
			update();
		}, _sourceLifetime);
	}
	_peer->session().downloaderTaskFinished(
	) | rpl::filter([=] {
		return _waiting;
	}) | rpl::start_with_next([=] {
		const auto loading = _showPeerUserpic
			? Ui::PeerUserpicLoading(_userpicView)
			: (_nonPersonalView && !_nonPersonalView->loaded());
		if (!loading) {
			_waiting = false;
			startNewPhotoShowing();
		}
	}, _sourceLifetime);
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
	} else if (showRepliesMessages()) {
		Ui::EmptyUserpic::PaintRepliesMessages(
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
		paintUserpicFrame(p, photoPosition);
	}

	const auto fillTranslatedShape = [&](const style::color &color) {
		p.translate(photoLeft, photoTop);
		fillShape(p, color);
		p.translate(-photoLeft, -photoTop);
	};

	if (_role == Role::ChangePhoto || _role == Role::ChoosePhoto) {
		auto over = isOver() || isDown();
		if (over) {
			fillTranslatedShape(_userpicHasImage
				? st::msgDateImgBg
				: _st.changeButton.textBgOver);
		}
		paintRipple(
			p,
			photoLeft,
			photoTop,
			(_userpicHasImage
				? &st::shadowFg->c
				: &_st.changeButton.ripple.color->c));
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
			fillTranslatedShape(_st.uploadBg);
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

void UserpicButton::paintUserpicFrame(Painter &p, QPoint photoPosition) {
	checkStreamedIsStarted();
	if (_streamed
		&& _streamed->player().ready()
		&& !_streamed->player().videoSize().isEmpty()) {
		const auto paused = _controller
			? _controller->isGifPausedAtLeastFor(
				Window::GifPauseReason::RoundPlaying)
			: false;
		auto request = Media::Streaming::FrameRequest();
		auto size = QSize{ _st.photoSize, _st.photoSize };
		const auto ratio = style::DevicePixelRatio();
		request.outer = request.resize = size * ratio;
		if (_shape == PeerUserpicShape::Monoforum) {
		} else if (useForumShape()) {
			const auto radius = int(_st.photoSize
				* Ui::ForumUserpicRadiusMultiplier());
			if (_roundingCorners[0].width() != radius * ratio) {
				_roundingCorners = Images::CornersMask(radius);
			}
			request.rounding = Images::CornersMaskRef(_roundingCorners);
		} else {
			if (_ellipseMask.size() != request.outer) {
				_ellipseMask = Images::EllipseMask(size);
			}
			request.mask = _ellipseMask;
		}
		auto frame = _streamed->frame(request);

		if (_shape == PeerUserpicShape::Monoforum) {
			if (_monoforumMask.isNull()) {
				_monoforumMask = MonoforumShapeMask(request.resize);
			}
			constexpr auto format = QImage::Format_ARGB32_Premultiplied;
			if (frame.format() != format) {
				frame = std::move(frame).convertToFormat(format);
			}
			auto q = QPainter(&frame);
			q.setCompositionMode(QPainter::CompositionMode_DestinationIn);
			q.drawImage(
				QRect(QPoint(), frame.size() / frame.devicePixelRatio()),
				_monoforumMask);
			q.end();
		}
		p.drawImage(QRect(photoPosition, size), frame);
		if (!paused) {
			_streamed->markFrameShown();
		}
	} else {
		p.drawPixmapLeft(photoPosition, width(), _userpic);
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
	return Ui::RippleAnimation::EllipseMask(QSize(
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

	const auto user = _peer->asUser();
	const auto nonPersonal = (user && _source != Source::PeerPhoto)
		? _peer->session().api().peerPhoto().nonPersonalPhoto(user)
		: nullptr;
	_showPeerUserpic = (_source == Source::PeerPhoto)
		|| (user
			&& !user->hasPersonalPhoto()
			&& (_source == Source::NonPersonalPhoto
				|| (_source == Source::NonPersonalIfHasPersonal
					&& hasPersonalPhotoLocally())));
	const auto showNonPersonal = _showPeerUserpic ? nullptr : nonPersonal;

	_userpicView = _showPeerUserpic
		? _peer->createUserpicView()
		: PeerUserpicView();
	_nonPersonalView = showNonPersonal
		? showNonPersonal->createMediaView()
		: nullptr;
	_waiting = _showPeerUserpic
		? Ui::PeerUserpicLoading(_userpicView)
		: (_nonPersonalView && !_nonPersonalView->loaded());
	if (_waiting) {
		if (_showPeerUserpic) {
			_peer->loadUserpic();
		} else if (_nonPersonalView) {
			showNonPersonal->load(Data::FileOriginFullUser{
				peerToUser(user->id),
			});
		}
	}
	if (_role == Role::OpenPhoto) {
		if (_peer->userpicPhotoUnknown()) {
			_peer->updateFullForced();
		}
		_canOpenPhoto = (_peer->userpicPhotoId() != 0);
		updateCursor();
		updateVideo();
	}
}

void UserpicButton::updateCursor() {
	Expects(_role == Role::OpenPhoto);

	const auto pointer = _canOpenPhoto
		|| (_changeOverlayEnabled && _cursorInChangeOverlay);
	setPointerCursor(pointer);
}

bool UserpicButton::createStreamingObjects(not_null<PhotoData*> photo) {
	Expects(_peer != nullptr);

	using namespace Media::Streaming;

	const auto origin = _peer->isUser()
		? Data::FileOriginUserPhoto(peerToUser(_peer->id), photo->id)
		: Data::FileOrigin(Data::FileOriginPeerPhoto(_peer->id));
	_streamed = std::make_unique<Instance>(
		photo->owner().streaming().sharedDocument(photo, origin),
		nullptr);
	_streamed->lockPlayer();
	_streamed->player().updates(
	) | rpl::start_with_next_error([=](Update &&update) {
		handleStreamingUpdate(std::move(update));
	}, [=](Error &&error) {
		handleStreamingError(std::move(error));
	}, _streamed->lifetime());
	if (_streamed->ready()) {
		streamingReady(base::duplicate(_streamed->info()));
	}
	if (!_streamed->valid()) {
		clearStreaming();
		return false;
	}
	return true;
}

void UserpicButton::clearStreaming() {
	_streamed = nullptr;
	_streamedPhoto = nullptr;
}

void UserpicButton::handleStreamingUpdate(Media::Streaming::Update &&update) {
	using namespace Media::Streaming;

	v::match(update.data, [&](Information &update) {
		streamingReady(std::move(update));
	}, [](PreloadedVideo) {
	}, [&](UpdateVideo) {
		this->update();
	}, [](PreloadedAudio) {
	}, [](UpdateAudio) {
	}, [](WaitingForData) {
	}, [](SpeedEstimate) {
	}, [](MutedByOther) {
	}, [](Finished) {
	});
}

void UserpicButton::handleStreamingError(Media::Streaming::Error &&error) {
	Expects(_peer != nullptr);

	_streamedPhoto->setVideoPlaybackFailed();
	_streamedPhoto = nullptr;
	_streamed = nullptr;
}

void UserpicButton::streamingReady(Media::Streaming::Information &&info) {
	update();
}

void UserpicButton::updateVideo() {
	Expects(_role == Role::OpenPhoto);

	const auto id = _peer->userpicPhotoId();
	if (!id) {
		clearStreaming();
		return;
	}
	const auto photo = _peer->owner().photo(id);
	if (!photo->date() || !photo->videoCanBePlayed()) {
		clearStreaming();
		return;
	} else if (_streamed && _streamedPhoto == photo) {
		return;
	}
	if (!createStreamingObjects(photo)) {
		photo->setVideoPlaybackFailed();
		return;
	}
	_streamedPhoto = photo;
	checkStreamedIsStarted();
}

void UserpicButton::checkStreamedIsStarted() {
	Expects(!_streamed || _streamedPhoto);

	if (!_streamed) {
		return;
	} else if (_streamed->paused()) {
		_streamed->resume();
	}
	if (_streamed && !_streamed->active() && !_streamed->failed()) {
		const auto position = _streamedPhoto->videoStartPosition();
		auto options = Media::Streaming::PlaybackOptions();
		options.position = position;
		options.mode = Media::Streaming::Mode::Video;
		options.loop = true;
		_streamed->play(options);
	}
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
	if (_source == Source::Custom) {
		return;
	}
	processPeerPhoto();
	if (!_waiting) {
		grabOldUserpic();
		startNewPhotoShowing();
	}
}

bool UserpicButton::useForumShape() const {
	return (_shape == PeerUserpicShape::Forum)
		|| (_peer && _peer->isForum() && _shape == PeerUserpicShape::Auto);
}

void UserpicButton::grabOldUserpic() {
	auto photoRect = QRect(
		countPhotoPosition(),
		QSize(_st.photoSize, _st.photoSize)
	);
	_oldUserpic = GrabWidget(this, photoRect);
}

void UserpicButton::startNewPhotoShowing() {
	const auto oldUniqueKey = _userpicUniqueKey;
	prepareUserpicPixmap();
	update();

	if (_notShownYet) {
		return;
	} else if (oldUniqueKey != _userpicUniqueKey
		|| _a_appearance.animating()) {
		startAnimation();
	}
}

void UserpicButton::startAnimation() {
	_a_appearance.stop();
	_a_appearance.start([this] { update(); }, 0, 1, _st.duration);
}

void UserpicButton::switchChangePhotoOverlay(
		bool enabled,
		Fn<void(ChosenImage)> chosen) {
	Expects(_role == Role::OpenPhoto);

	if (_changeOverlayEnabled != enabled) {
		_changeOverlayEnabled = enabled;
		if (enabled) {
			if (isOver()) {
				startChangeOverlayAnimation();
			}
			updateCursorInChangeOverlay(
				mapFromGlobal(QCursor::pos()));
			if (chosen) {
				chosenImages() | rpl::start_with_next(chosen, lifetime());
			}
		} else {
			_changeOverlayShown.stop();
			update();
		}
	}
}

void UserpicButton::overrideShape(PeerUserpicShape shape) {
	_shape = shape;
	prepare();
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

bool UserpicButton::showRepliesMessages() const {
	return _showSavedMessagesOnSelf && _peer && _peer->isRepliesChat();
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

void UserpicButton::showCustom(QImage &&image) {
	if (!_notShownYet) {
		grabOldUserpic();
	}

	clearStreaming();
	_sourceLifetime.destroy();
	_source = Source::Custom;

	_userpicHasImage = !image.isNull();
	if (_userpicHasImage) {
		auto size = QSize(_st.photoSize, _st.photoSize);
		auto small = image.scaled(
			size * style::DevicePixelRatio(),
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		_userpic = Ui::PixmapFromImage(useForumShape()
			? Images::Round(
				std::move(small),
				Images::CornersMask(_st.photoSize
					* Ui::ForumUserpicRadiusMultiplier()))
			: Images::Circle(std::move(small)));
	} else {
		_userpic = CreateSquarePixmap(_st.photoSize, [&](Painter &p) {
			fillShape(p, _st.changeButton.textBg);
		});
	}
	_userpic.setDevicePixelRatio(style::DevicePixelRatio());
	_userpicUniqueKey = {};
	_result = std::move(image);

	startNewPhotoShowing();
}

void UserpicButton::showSource(Source source) {
	Expects(_peer != nullptr);
	Expects(source != Source::Custom); // Show this using showCustom().
	Expects(source == Source::PeerPhoto || _peer->isUser());

	if (_source != source) {
		clearStreaming();
	}

	_sourceLifetime.destroy();
	_source = source;

	_result = QImage();

	processPeerPhoto();
	setupPeerViewers();

	prepareUserpicPixmap();
	update();
}

void UserpicButton::overrideHasPersonalPhoto(bool has) {
	Expects(_peer && _peer->isUser());

	_overrideHasPersonalPhoto = has;
}

rpl::producer<> UserpicButton::resetPersonalRequests() const {
	return _resetPersonalRequests.events();
}

void UserpicButton::fillShape(QPainter &p, const style::color &color) const {
	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	const auto size = _st.photoSize;
	if (useForumShape()) {
		const auto radius = size * Ui::ForumUserpicRadiusMultiplier();
		p.drawRoundedRect(0, 0, size, size, radius, radius);
	} else {
		p.drawEllipse(0, 0, size, size);
	}
}

void UserpicButton::prepareUserpicPixmap() {
	if (_source == Source::Custom) {
		return;
	}
	const auto size = _st.photoSize;
	_userpicHasImage = _showPeerUserpic
		? (_peer
			&& (_peer->userpicCloudImage(_userpicView)
				|| _role != Role::ChangePhoto))
		: (_source == Source::NonPersonalPhoto
			|| (_source == Source::NonPersonalIfHasPersonal
				&& hasPersonalPhotoLocally()));
	_userpic = CreateSquarePixmap(size, [&](Painter &p) {
		if (_userpicHasImage) {
			if (_showPeerUserpic) {
				_peer->paintUserpic(p, _userpicView, {
					.position = QPoint(),
					.size = size,
					.shape = _shape,
				});
			} else if (_nonPersonalView) {
				using Size = Data::PhotoSize;
				if (const auto full = _nonPersonalView->image(Size::Large)) {
					const auto ratio = style::DevicePixelRatio();
					auto image = full->original().scaled(
						QSize(size, size) * ratio,
						Qt::IgnoreAspectRatio,
						Qt::SmoothTransformation);
					image = useForumShape()
						? Images::Round(
							std::move(image),
							Images::CornersMask(size
								* Ui::ForumUserpicRadiusMultiplier()))
						: Images::Circle(std::move(image));
					image.setDevicePixelRatio(style::DevicePixelRatio());
					p.drawImage(0, 0, image);
				}
			} else {
				const auto user = _peer->asUser();
				auto empty = Ui::EmptyUserpic(
					Ui::EmptyUserpic::UserpicColor(_peer->colorIndex()),
					((user && user->isInaccessible())
						? Ui::EmptyUserpic::InaccessibleName()
						: _peer->name()));
				if (useForumShape()) {
					empty.paintRounded(
						p,
						0,
						0,
						size,
						size,
						size * Ui::ForumUserpicRadiusMultiplier());
				} else {
					empty.paintCircle(p, 0, 0, size, size);
				}
			}
		} else {
			fillShape(p, _st.changeButton.textBg);
		}
	});
	_userpicUniqueKey = _userpicHasImage
		? (_showPeerUserpic
			? _peer->userpicUniqueKey(_userpicView)
			: _nonPersonalView
			? InMemoryKey{ _nonPersonalView->owner()->id, 0 }
			: InMemoryKey{ _peer->id.value, _peer->id.value })
		: InMemoryKey();
}

not_null<Ui::UserpicButton*> CreateUploadSubButton(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller) {
	const auto background = Ui::CreateChild<Ui::RpWidget>(parent.get());
	const auto upload = Ui::CreateChild<Ui::UserpicButton>(
		parent.get(),
		&controller->window(),
		Ui::UserpicButton::Role::ChoosePhoto,
		st::uploadUserpicButton);
	SetupSubButtonBackground(upload, background);
	return upload;
}

not_null<Ui::UserpicButton*> CreateUploadSubButton(
		not_null<Ui::RpWidget*> parent,
		not_null<UserData*> contact,
		not_null<Window::SessionController*> controller) {
	const auto background = Ui::CreateChild<Ui::RpWidget>(parent.get());
	const auto upload = Ui::CreateChild<Ui::UserpicButton>(
		parent.get(),
		controller,
		contact,
		Ui::UserpicButton::Role::ChoosePhoto,
		Ui::UserpicButton::Source::NonPersonalIfHasPersonal,
		st::uploadUserpicButton);
	SetupSubButtonBackground(upload, background);
	return upload;
}

} // namespace Ui
