/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_userpic_suggestion.h"

#include "core/click_handler_types.h" // ClickHandlerContext
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_user.h"
#include "data/data_photo_media.h"
#include "data/data_file_click_handler.h"
#include "data/data_session.h"
#include "editor/photo_editor_common.h"
#include "editor/photo_editor_layer_widget.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/media/history_view_sticker_player_abstract.h"
#include "history/view/history_view_element.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "ui/boxes/confirm_box.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/painter.h"
#include "mainwidget.h"
#include "apiwrap.h"
#include "api/api_peer_photo.h"
#include "settings/settings_information.h" // UpdatePhotoLocally
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

constexpr auto kToastDuration = 5 * crl::time(1000);

void ShowUserpicSuggestion(
		not_null<Window::SessionController*> controller,
		const std::shared_ptr<Data::PhotoMedia> &media,
		const FullMsgId itemId,
		not_null<PeerData*> peer,
		Fn<void()> setDone) {
	const auto photo = media->owner();
	const auto from = peer->asUser();
	const auto name = (from && !from->firstName.isEmpty())
		? from->firstName
		: peer->name();
	if (photo->hasVideo()) {
		const auto done = [=](Fn<void()> close) {
			using namespace Settings;
			const auto session = &photo->session();
			auto &peerPhotos = session->api().peerPhoto();
			peerPhotos.updateSelf(photo, itemId, setDone);
			close();
		};
		controller->show(Ui::MakeConfirmBox({
			.text = tr::lng_profile_accept_video_sure(
				tr::now,
				lt_user,
				name),
			.confirmed = done,
			.confirmText = tr::lng_profile_set_video_button(
				tr::now),
		}));
	} else {
		const auto original = std::make_shared<QImage>(
			media->image(Data::PhotoSize::Large)->original());
		const auto callback = [=](QImage &&image) {
			using namespace Settings;
			const auto session = &photo->session();
			const auto user = session->user();
			UpdatePhotoLocally(user, image);
			auto &peerPhotos = session->api().peerPhoto();
			if (original->size() == image.size()
				&& original->constBits() == image.constBits()) {
				peerPhotos.updateSelf(photo, itemId, setDone);
			} else {
				peerPhotos.upload(user, { std::move(image) }, setDone);
			}
		};
		using namespace Editor;
		PrepareProfilePhoto(
			controller->content(),
			&controller->window(),
			{
				.about = { tr::lng_profile_accept_photo_sure(
					tr::now,
					lt_user,
					name) },
				.confirm = tr::lng_profile_set_photo_button(tr::now),
				.cropType = EditorData::CropType::Ellipse,
				.keepAspectRatio = true,
			},
			callback,
			base::duplicate(*original));
	}
}

[[nodiscard]] QImage GrabUserpicFrame(base::weak_ptr<Photo> photo) {
	const auto strong = photo.get();
	if (!strong || !strong->width() || !strong->height()) {
		return {};
	}
	const auto ratio = style::DevicePixelRatio();
	auto frame = QImage(
		QSize(strong->width(), strong->height()) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	frame.fill(Qt::transparent);
	frame.setDevicePixelRatio(ratio);
	auto p = Painter(&frame);
	strong->paintUserpicFrame(p, QPoint(0, 0), false);
	p.end();
	return frame;
}

void ShowSetToast(
		not_null<Window::SessionController*> controller,
		const QImage &frame) {
	const auto text = Ui::Text::Bold(
		tr::lng_profile_changed_photo_title(tr::now)
	).append('\n').append(
		tr::lng_profile_changed_photo_about(
			tr::now,
			lt_link,
			Ui::Text::Link(
				tr::lng_profile_changed_photo_link(tr::now),
				u"tg://settings/edit_profile"_q),
			Ui::Text::WithEntities)
	);
	auto st = std::make_shared<style::Toast>(st::historyPremiumToast);
	const auto skip = st->padding.top();
	const auto size = st->style.font->height * 2;
	const auto ratio = style::DevicePixelRatio();
	auto copy = frame.scaled(
		QSize(size, size) * ratio,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	copy.setDevicePixelRatio(ratio);
	st->padding.setLeft(skip + size + skip);
	st->palette.linkFg = st->palette.selectLinkFg = st::mediaviewTextLinkFg;

	const auto weak = controller->showToast({
		.text = text,
		.st = st.get(),
		.duration = kToastDuration,
		.multiline = true,
		.dark = true,
		.slideSide = RectPart::Bottom,
	});
	if (const auto strong = weak.get()) {
		const auto widget = strong->widget();
		widget->lifetime().add([st = std::move(st)] {});

		const auto preview = Ui::CreateChild<Ui::RpWidget>(widget.get());
		preview->moveToLeft(skip, skip);
		preview->resize(size, size);
		preview->show();
		preview->setAttribute(Qt::WA_TransparentForMouseEvents);
		preview->paintRequest(
		) | rpl::start_with_next([=] {
			QPainter(preview).drawImage(0, 0, copy);
		}, preview->lifetime());
	}
}

[[nodiscard]] Fn<void()> ShowSetToastCallback(
		base::weak_ptr<Window::SessionController> weak,
		QImage frame) {
	return [weak = std::move(weak), frame = std::move(frame)] {
		if (const auto strong = weak.get()) {
			ShowSetToast(strong, frame);
		}
	};
}

} // namespace

UserpicSuggestion::UserpicSuggestion(
	not_null<Element*> parent,
	not_null<PeerData*> chat,
	not_null<PhotoData*> photo,
	int width)
: _photo(parent, chat, photo, width) {
	_photo.initDimensions();
	_photo.resizeGetHeight(_photo.maxWidth());
}

UserpicSuggestion::~UserpicSuggestion() = default;

int UserpicSuggestion::top() {
	return st::msgServiceGiftBoxButtonMargins.top();
}

QSize UserpicSuggestion::size() {
	return { _photo.maxWidth(), _photo.minHeight() };
}

QString UserpicSuggestion::title() {
	return QString();
}

rpl::producer<QString> UserpicSuggestion::button() {
	return _photo.getPhoto()->hasVideo()
		? (_photo.parent()->data()->out()
			? tr::lng_action_suggested_video_button()
			: tr::lng_profile_set_video_button())
		: tr::lng_action_suggested_photo_button();
}

TextWithEntities UserpicSuggestion::subtitle() {
	return _photo.parent()->data()->notificationText();
}

ClickHandlerPtr UserpicSuggestion::createViewLink() {
	const auto out = _photo.parent()->data()->out();
	const auto photo = _photo.getPhoto();
	const auto itemId = _photo.parent()->data()->fullId();
	const auto peer = _photo.parent()->data()->history()->peer;
	const auto weak = base::make_weak(&_photo);
	const auto show = crl::guard(weak, [=](FullMsgId id) {
		_photo.showPhoto(id);
	});
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		auto frame = GrabUserpicFrame(weak);
		if (frame.isNull()) {
			return;
		}
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			const auto media = photo->activeMediaView();
			if (media->loaded()) {
				if (out) {
					PhotoOpenClickHandler(photo, show, itemId).onClick(
						context);
				} else {
					ShowUserpicSuggestion(
						controller,
						media,
						itemId,
						peer,
						ShowSetToastCallback(controller, std::move(frame)));
				}
			} else if (!photo->loading()) {
				PhotoSaveClickHandler(photo, itemId).onClick(context);
			}
		}
	});
}

void UserpicSuggestion::draw(
		Painter &p,
		const PaintContext &context,
		const QRect &geometry) {
	p.translate(geometry.topLeft());
	_photo.draw(p, context);
	p.translate(-geometry.topLeft());
}

void UserpicSuggestion::stickerClearLoopPlayed() {
}

std::unique_ptr<StickerPlayer> UserpicSuggestion::stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) {
	return nullptr;
}

bool UserpicSuggestion::hasHeavyPart() {
	return _photo.hasHeavyPart();
}

void UserpicSuggestion::unloadHeavyPart() {
	_photo.unloadHeavyPart();
}

} // namespace HistoryView
