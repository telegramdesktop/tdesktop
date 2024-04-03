/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/background_preview_box.h"

#include "base/unixtime.h"
#include "boxes/peers/edit_peer_color_box.h"
#include "boxes/premium_preview_box.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "window/themes/window_theme.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/boost_box.h"
#include "ui/controls/chat_service_checkbox.h"
#include "ui/chat/chat_theme.h"
#include "ui/chat/chat_style.h"
#include "ui/toast/toast.h"
#include "ui/image/image.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/ui_utility.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "history/view/history_view_message.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_document_resolver.h"
#include "data/data_file_origin.h"
#include "data/data_peer_values.h"
#include "data/data_premium_limits.h"
#include "settings/settings_premium.h"
#include "storage/file_upload.h"
#include "storage/localimageloader.h"
#include "window/window_session_controller.h"
#include "window/themes/window_themes_embedded.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace {

constexpr auto kMaxWallPaperSlugLength = 255;

[[nodiscard]] bool IsValidWallPaperSlug(const QString &slug) {
	if (slug.isEmpty() || slug.size() > kMaxWallPaperSlugLength) {
		return false;
	}
	return ranges::none_of(slug, [](QChar ch) {
		return (ch != '.')
			&& (ch != '_')
			&& (ch != '-')
			&& (ch < '0' || ch > '9')
			&& (ch < 'a' || ch > 'z')
			&& (ch < 'A' || ch > 'Z');
	});
}

[[nodiscard]] AdminLog::OwnedItem GenerateServiceItem(
		not_null<HistoryView::ElementDelegate*> delegate,
		not_null<History*> history,
		const QString &text,
		bool out) {
	Expects(history->peer->isUser());

	const auto flags = MessageFlag::FakeHistoryItem
		| MessageFlag::HasFromId
		| (out ? MessageFlag::Outgoing : MessageFlag(0));
	const auto item = history->makeMessage({
		.id = history->owner().nextLocalMessageId(),
		.flags = flags,
		.date = base::unixtime::now(),
	}, PreparedServiceText{ { text } });
	return AdminLog::OwnedItem(delegate, item);
}

[[nodiscard]] AdminLog::OwnedItem GenerateTextItem(
		not_null<HistoryView::ElementDelegate*> delegate,
		not_null<History*> history,
		const QString &text,
		bool out) {
	Expects(history->peer->isUser());

	const auto item = history->makeMessage({
		.id = history->nextNonHistoryEntryId(),
		.flags = (MessageFlag::FakeHistoryItem
			| MessageFlag::HasFromId
			| (out ? MessageFlag::Outgoing : MessageFlag(0))),
		.from = (out
			? history->session().userId()
			: peerToUser(history->peer->id)),
		.date = base::unixtime::now(),
	}, TextWithEntities{ text }, MTP_messageMediaEmpty());
	return AdminLog::OwnedItem(delegate, item);
}

[[nodiscard]] QImage PrepareScaledNonPattern(
		const QImage &image,
		Images::Option blur) {
	const auto size = st::boxWideWidth;
	const auto width = std::max(image.width(), 1);
	const auto height = std::max(image.height(), 1);
	const auto takeWidth = (width > height)
		? (width * size / height)
		: size;
	const auto takeHeight = (width > height)
		? size
		: (height * size / width);
	const auto ratio = style::DevicePixelRatio();
	return Images::Prepare(image, QSize(takeWidth, takeHeight) * ratio, {
		.options = Images::Option::TransparentBackground | blur,
		.outer = { size, size },
	});
}

[[nodiscard]] QImage PrepareScaledFromFull(
		const QImage &image,
		bool isPattern,
		const std::vector<QColor> &background,
		int gradientRotation,
		float64 patternOpacity,
		Images::Option blur = Images::Option(0)) {
	auto result = PrepareScaledNonPattern(image, blur);
	if (isPattern) {
		result = Ui::PreparePatternImage(
			std::move(result),
			background,
			gradientRotation,
			patternOpacity);
	}
	return std::move(result).convertToFormat(
		QImage::Format_ARGB32_Premultiplied);
}

[[nodiscard]] QImage BlackImage(QSize size) {
	auto result = QImage(size, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::black);
	return result;
}

[[nodiscard]] Data::WallPaper Resolve(
		not_null<Main::Session*> session,
		const Data::WallPaper &paper,
		bool dark) {
	if (paper.emojiId().isEmpty()) {
		return paper;
	}
	const auto &themes = session->data().cloudThemes();
	if (const auto theme = themes.themeForEmoji(paper.emojiId())) {
		using Type = Data::CloudThemeType;
		const auto type = dark ? Type::Dark : Type::Light;
		const auto i = theme->settings.find(type);
		if (i != end(theme->settings) && i->second.paper) {
			return *i->second.paper;
		}
	}
	return paper;
}

} // namespace

struct BackgroundPreviewBox::OverridenStyle {
	style::Box box;
	style::IconButton toggle;
	style::MediaSlider slider;
	style::FlatLabel subtitle;
};

BackgroundPreviewBox::BackgroundPreviewBox(
	QWidget*,
	not_null<Window::SessionController*> controller,
	const Data::WallPaper &paper,
	BackgroundPreviewArgs args)
: SimpleElementDelegate(controller, [=] { update(); })
, _controller(controller)
, _forPeer(args.forPeer)
, _fromMessageId(args.fromMessageId)
, _chatStyle(std::make_unique<Ui::ChatStyle>(
	controller->session().colorIndicesValue()))
, _serviceHistory(_controller->session().data().history(
	PeerData::kServiceNotificationsId))
, _service(nullptr)
, _text1(GenerateTextItem(
	delegate(),
	_serviceHistory,
	(_forPeer
		? tr::lng_background_apply1(tr::now)
		: tr::lng_background_text1(tr::now)),
	false))
, _text2(GenerateTextItem(
	delegate(),
	_serviceHistory,
	(_forPeer
		? tr::lng_background_apply2(tr::now)
		: tr::lng_background_text2(tr::now)),
	true))
, _paperEmojiId(paper.emojiId())
, _paper(
	Resolve(&controller->session(), paper, Window::Theme::IsNightMode()))
, _media(_paper.document() ? _paper.document()->createMediaView() : nullptr)
, _radial([=](crl::time now) { radialAnimationCallback(now); })
, _appNightMode(Window::Theme::IsNightModeValue())
, _boxDarkMode(_appNightMode.current())
, _dimmingIntensity(std::clamp(_paper.patternIntensity(), 0, 100))
, _dimmed(_forPeer
	&& (_paper.document() || _paper.localThumbnail())
	&& !_paper.isPattern()) {
	if (_media) {
		_media->thumbnailWanted(_paper.fileOrigin());
	}
	generateBackground();
	_controller->session().downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());

	_appNightMode.changes(
	) | rpl::start_with_next([=](bool night) {
		_boxDarkMode = night;
		update();
	}, lifetime());

	_boxDarkMode.changes(
	) | rpl::start_with_next([=](bool dark) {
		applyDarkMode(dark);
	}, lifetime());

	const auto prepare = [=](bool dark, auto pointer) {
		const auto weak = Ui::MakeWeak(this);
		crl::async([=] {
			auto result = std::make_unique<style::palette>();
			Window::Theme::PreparePaletteCallback(dark, {})(*result);
			crl::on_main([=, result = std::move(result)]() mutable {
				if (const auto strong = weak.data()) {
					strong->*pointer = std::move(result);
					strong->paletteReady();
				}
			});
		});
	};
	prepare(false, &BackgroundPreviewBox::_lightPalette);
	prepare(true, &BackgroundPreviewBox::_darkPalette);
}

BackgroundPreviewBox::~BackgroundPreviewBox() = default;

void BackgroundPreviewBox::recreate(bool dark) {
	_paper = Resolve(
		&_controller->session(),
		Data::WallPaper::FromEmojiId(_paperEmojiId),
		dark);
	_media = _paper.document()
		? _paper.document()->createMediaView()
		: nullptr;
	if (_media) {
		_media->thumbnailWanted(_paper.fileOrigin());
	}
	_full = QImage();
	_generated = _scaled = _blurred = _fadeOutThumbnail = QPixmap();
	_generating = {};
	generateBackground();
	_paper.loadDocument();
	if (const auto document = _paper.document()) {
		if (document->loading()) {
			_radial.start(_media->progress());
		}
	}
	checkLoadedDocument();
	updateServiceBg(_paper.backgroundColors());
	update();
}

void BackgroundPreviewBox::applyDarkMode(bool dark) {
	if (!_paperEmojiId.isEmpty()) {
		recreate(dark);
	}
	const auto equals = (dark == Window::Theme::IsNightMode());
	const auto &palette = (dark ? _darkPalette : _lightPalette);
	if (!equals && !palette) {
		_waitingForPalette = true;
		return;
	}
	_waitingForPalette = false;
	if (equals) {
		setStyle(st::defaultBox);
		_chatStyle->applyCustomPalette(nullptr);
		_paletteServiceBg = rpl::single(
			rpl::empty
		) | rpl::then(
			style::PaletteChanged()
		) | rpl::map([=] {
			return st::msgServiceBg->c;
		});
	} else {
		setStyle(overridenStyle(dark));
		_chatStyle->applyCustomPalette(palette.get());
		_paletteServiceBg = palette->msgServiceBg()->c;
	}
	resetTitle();
	rebuildButtons(dark);
	update();
	if (const auto parent = parentWidget()) {
		parent->update();
	}

	if (_dimmed) {
		createDimmingSlider(dark);
	}
}

void BackgroundPreviewBox::createDimmingSlider(bool dark) {
	const auto created = !_dimmingWrap;
	if (created) {
		_dimmingWrap.create(this, object_ptr<Ui::RpWidget>(this));
		_dimmingContent = _dimmingWrap->entity();
	}
	_dimmingSlider = nullptr;
	for (const auto &child : _dimmingContent->children()) {
		if (child->isWidgetType()) {
			static_cast<QWidget*>(child)->hide();
			child->deleteLater();
		}
	}
	const auto equals = (dark == Window::Theme::IsNightMode());
	const auto inner = Ui::CreateChild<Ui::VerticalLayout>(_dimmingContent);
	inner->show();
	Ui::AddSubsectionTitle(
		inner,
		tr::lng_background_dimming(),
		style::margins(0, st::defaultVerticalListSkip, 0, 0),
		equals ? nullptr : dark ? &_dark->subtitle : &_light->subtitle);
	_dimmingSlider = inner->add(
		object_ptr<Ui::MediaSlider>(
			inner,
			(equals
				? st::defaultContinuousSlider
				: dark
				? _dark->slider
				: _light->slider)),
		st::localStorageLimitMargin);
	_dimmingSlider->setValue(_dimmingIntensity / 100.);
	_dimmingSlider->setAlwaysDisplayMarker(true);
	_dimmingSlider->resize(st::defaultContinuousSlider.seekSize);
	const auto handle = [=](float64 value) {
		const auto intensity = std::clamp(
			int(base::SafeRound(value * 100)),
			0,
			100);
		_paper = _paper.withPatternIntensity(intensity);
		_dimmingIntensity = intensity;
		update();
	};
	_dimmingSlider->setChangeProgressCallback(handle);
	_dimmingSlider->setChangeFinishedCallback(handle);
	inner->resizeToWidth(st::boxWideWidth);
	Ui::SendPendingMoveResizeEvents(inner);
	inner->move(0, 0);
	_dimmingContent->resize(inner->size());

	_dimmingContent->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(_dimmingContent);
		const auto palette = (dark ? _darkPalette : _lightPalette).get();
		p.fillRect(clip, equals ? st::boxBg : palette->boxBg());
	}, _dimmingContent->lifetime());

	_dimmingToggleScheduled = true;

	if (created) {
		rpl::combine(
			heightValue(),
			_dimmingWrap->heightValue(),
			rpl::mappers::_1 - rpl::mappers::_2
		) | rpl::start_with_next([=](int top) {
			_dimmingWrap->move(0, top);
		}, _dimmingWrap->lifetime());

		_dimmingWrap->toggle(dark, anim::type::instant);
		_dimmingHeight = _dimmingWrap->heightValue();
		_dimmingHeight.changes() | rpl::start_with_next([=] {
			update();
		}, _dimmingWrap->lifetime());
	}
}

void BackgroundPreviewBox::paletteReady() {
	if (_waitingForPalette) {
		applyDarkMode(_boxDarkMode.current());
	}
}

const style::Box &BackgroundPreviewBox::overridenStyle(bool dark) {
	auto &st = dark ? _dark : _light;
	if (!st) {
		st = std::make_unique<OverridenStyle>(prepareOverridenStyle(dark));
	}
	return st->box;
}

auto BackgroundPreviewBox::prepareOverridenStyle(bool dark)
-> OverridenStyle {
	const auto p = (dark ? _darkPalette : _lightPalette).get();
	Assert(p != nullptr);

	const auto &toggle = dark
		? st::backgroundSwitchToLight
		: st::backgroundSwitchToDark;
	auto result = OverridenStyle{
		.box = st::defaultBox,
		.toggle = toggle,
		.slider = st::defaultContinuousSlider,
		.subtitle = st::defaultSubsectionTitle,
	};
	result.box.button.textFg = p->lightButtonFg();
	result.box.button.textFgOver = p->lightButtonFgOver();
	result.box.button.numbersTextFg = p->lightButtonFg();
	result.box.button.numbersTextFgOver = p->lightButtonFgOver();
	result.box.button.textBg = p->lightButtonBg();
	result.box.button.textBgOver = p->lightButtonBgOver();
	result.box.button.ripple.color = p->lightButtonBgRipple();
	result.box.title.textFg = p->boxTitleFg();
	result.box.bg = p->boxBg();
	result.box.titleAdditionalFg = p->boxTitleAdditionalFg();

	result.toggle.ripple.color = p->windowBgOver();
	result.toggle.icon = toggle.icon.withPalette(*p);
	result.toggle.iconOver = toggle.iconOver.withPalette(*p);

	result.slider.activeFg = p->mediaPlayerActiveFg();
	result.slider.inactiveFg = p->mediaPlayerInactiveFg();
	result.slider.activeFgOver = p->mediaPlayerActiveFg();
	result.slider.inactiveFgOver = p->mediaPlayerInactiveFg();
	result.slider.activeFgDisabled = p->mediaPlayerInactiveFg();
	result.slider.inactiveFgDisabled = p->windowBg();
	result.slider.receivedTillFg = p->mediaPlayerInactiveFg();

	result.subtitle.textFg = p->windowActiveTextFg();

	return result;
}

bool BackgroundPreviewBox::forChannel() const {
	return _forPeer && _forPeer->isChannel();
}

bool BackgroundPreviewBox::forGroup() const {
	return forChannel() && _forPeer->isMegagroup();
}

void BackgroundPreviewBox::generateBackground() {
	if (_paper.backgroundColors().empty()) {
		return;
	}
	const auto size = QSize(st::boxWideWidth, st::boxWideWidth)
		* style::DevicePixelRatio();
	_generated = Ui::PixmapFromImage((_paper.patternOpacity() >= 0.)
		? Ui::GenerateBackgroundImage(
			size,
			_paper.backgroundColors(),
			_paper.gradientRotation())
		: BlackImage(size));
	_generated.setDevicePixelRatio(style::DevicePixelRatio());
}

not_null<HistoryView::ElementDelegate*> BackgroundPreviewBox::delegate() {
	return static_cast<HistoryView::ElementDelegate*>(this);
}

void BackgroundPreviewBox::resetTitle() {
	setTitle(tr::lng_background_header());
}

void BackgroundPreviewBox::rebuildButtons(bool dark) {
	clearButtons();
	addButton(forGroup()
		? tr::lng_background_apply_group()
		: forChannel()
		? tr::lng_background_apply_channel()
		: _forPeer
		? tr::lng_background_apply_button()
		: tr::lng_settings_apply(), [=] { apply(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });
	if (!_forPeer && _paper.hasShareUrl()) {
		addLeftButton(tr::lng_background_share(), [=] { share(); });
	}
	const auto equals = (dark == Window::Theme::IsNightMode());
	auto toggle = object_ptr<Ui::IconButton>(this, equals
		? (dark ? st::backgroundSwitchToLight : st::backgroundSwitchToDark)
		: dark ? _dark->toggle : _light->toggle);
	toggle->setClickedCallback([=] {
		_boxDarkMode = !_boxDarkMode.current();
	});
	addTopButton(std::move(toggle));
}

void BackgroundPreviewBox::prepare() {
	applyDarkMode(Window::Theme::IsNightMode());

	_paper.loadDocument();
	if (const auto document = _paper.document()) {
		if (document->loading()) {
			_radial.start(_media->progress());
		}
	}

	updateServiceBg(_paper.backgroundColors());

	setScaledFromThumb();
	checkLoadedDocument();

	_text1->setDisplayDate(false);
	_text1->initDimensions();
	_text1->resizeGetHeight(st::boxWideWidth);
	_text2->initDimensions();
	_text2->resizeGetHeight(st::boxWideWidth);

	setDimensions(st::boxWideWidth, st::boxWideWidth);
}

void BackgroundPreviewBox::recreateBlurCheckbox() {
	const auto document = _paper.document();
	if (_paper.isPattern()
		|| (!_paper.localThumbnail()
			&& (!document || !document->hasThumbnail()))) {
		return;
	}

	const auto blurred = _blur ? _blur->checked() : _paper.isBlurred();
	_blur = Ui::MakeChatServiceCheckbox(
		this,
		tr::lng_background_blur(tr::now),
		st::backgroundCheckbox,
		st::backgroundCheck,
		blurred,
		[=] { return _serviceBg.value_or(QColor(255, 255, 255, 0)); });
	_blur->show();

	rpl::combine(
		sizeValue(),
		_blur->sizeValue(),
		_dimmingHeight.value()
	) | rpl::start_with_next([=](QSize outer, QSize inner, int dimming) {
		const auto bottom = st::historyPaddingBottom;
		_blur->move(
			(outer.width() - inner.width()) / 2,
			outer.height() - dimming - bottom - inner.height());
	}, _blur->lifetime());

	_blur->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		checkBlurAnimationStart();
		update();
	}, _blur->lifetime());

	_blur->setDisabled(_paper.document() && _full.isNull());

	if (_forBothOverlay) {
		_forBothOverlay->raise();
	}
}

void BackgroundPreviewBox::apply() {
	if (_forPeer) {
		applyForPeer();
	} else {
		applyForEveryone();
	}
}

void BackgroundPreviewBox::uploadForPeer(bool both) {
	Expects(_forPeer != nullptr);

	if (_uploadId) {
		return;
	}

	const auto session = &_controller->session();
	const auto ready = Window::Theme::PrepareWallPaper(
		session->mainDcId(),
		_paper.localThumbnail()->original());
	const auto documentId = ready->id;
	_uploadId = FullMsgId(
		session->userPeerId(),
		session->data().nextLocalMessageId());
	session->uploader().upload(_uploadId, ready);
	if (_uploadLifetime) {
		return;
	}

	const auto document = session->data().document(documentId);
	document->uploadingData = std::make_unique<Data::UploadState>(
		document->size);

	session->uploader().documentProgress(
	) | rpl::start_with_next([=](const FullMsgId &fullId) {
		if (fullId != _uploadId) {
			return;
		}
		_uploadProgress = document->uploading()
			? ((document->uploadingData->offset * 100)
				/ document->uploadingData->size)
			: 0.;
		update(radialRect());
	}, _uploadLifetime);

	session->uploader().documentReady(
	) | rpl::start_with_next([=](const Storage::UploadedMedia &data) {
		if (data.fullId != _uploadId) {
			return;
		}
		_uploadProgress = 1.;
		_uploadLifetime.destroy();
		update(radialRect());
		session->api().request(MTPaccount_UploadWallPaper(
			MTP_flags(MTPaccount_UploadWallPaper::Flag::f_for_chat),
			data.info.file,
			MTP_string("image/jpeg"),
			_paper.mtpSettings()
		)).done([=](const MTPWallPaper &result) {
			result.match([&](const MTPDwallPaper &data) {
				session->data().documentConvert(
					session->data().document(documentId),
					data.vdocument());
			}, [&](const MTPDwallPaperNoFile &data) {
				LOG(("API Error: "
					"Got wallPaperNoFile after account.UploadWallPaper."));
			});
			if (const auto paper = Data::WallPaper::Create(session, result)) {
				setExistingForPeer(*paper, both);
			}
		}).send();
	}, _uploadLifetime);

	_uploadProgress = 0.;
	_radial.start(_uploadProgress);
}

void BackgroundPreviewBox::setExistingForPeer(
		const Data::WallPaper &paper,
		bool both) {
	Expects(_forPeer != nullptr);

	if (const auto already = _forPeer->wallPaper()) {
		if (already->equals(paper)) {
			_controller->finishChatThemeEdit(_forPeer);
			return;
		}
	}
	const auto api = &_controller->session().api();
	using Flag = MTPmessages_SetChatWallPaper::Flag;
	api->request(MTPmessages_SetChatWallPaper(
		MTP_flags((_fromMessageId ? Flag::f_id : Flag())
			| (_fromMessageId ? Flag() : Flag::f_wallpaper)
			| (both ? Flag::f_for_both : Flag())
			| Flag::f_settings),
		_forPeer->input,
		paper.mtpInput(&_controller->session()),
		paper.mtpSettings(),
		MTP_int(_fromMessageId.msg)
	)).done([=](const MTPUpdates &result) {
		api->applyUpdates(result);
	}).send();

	_forPeer->setWallPaper(paper);
	_controller->finishChatThemeEdit(_forPeer);
}

void BackgroundPreviewBox::checkLevelForChannel() {
	Expects(forChannel());

	const auto show = _controller->uiShow();
	_forPeerLevelCheck = true;
	const auto weak = Ui::MakeWeak(this);
	CheckBoostLevel(show, _forPeer, [=](int level) {
		if (!weak) {
			return std::optional<Ui::AskBoostReason>();
		}
		const auto limits = Data::LevelLimits(&_forPeer->session());
		const auto required = _paperEmojiId.isEmpty()
			? limits.channelCustomWallpaperLevelMin()
			: limits.channelWallpaperLevelMin();
		if (level >= required) {
			applyForPeer(false);
			return std::optional<Ui::AskBoostReason>();
		}
		return std::make_optional(Ui::AskBoostReason{
			Ui::AskBoostWallpaper{ required, _forPeer->isMegagroup()}
		});
	}, [=] { _forPeerLevelCheck = false; });
}

void BackgroundPreviewBox::applyForPeer() {
	Expects(_forPeer != nullptr);

	if (!Data::IsCustomWallPaper(_paper)) {
		if (const auto already = _forPeer->wallPaper()) {
			if (already->equals(_paper)) {
				_controller->finishChatThemeEdit(_forPeer);
				return;
			}
		}
	}

	if (forChannel()) {
		checkLevelForChannel();
		return;
	} else if (_fromMessageId || !_forPeer->session().premiumPossible()) {
		applyForPeer(false);
		return;
	} else if (_forBothOverlay) {
		return;
	}
	const auto size = this->size() * style::DevicePixelRatio();
	const auto bg = Images::DitherImage(
		Images::BlurLargeImage(
			Ui::GrabWidgetToImage(this).scaled(
				size / style::ConvertScale(4),
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation),
			24).scaled(
				size,
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation));

	_forBothOverlay = std::make_unique<Ui::FadeWrap<>>(
		this,
		object_ptr<Ui::RpWidget>(this));
	const auto overlay = _forBothOverlay->entity();

	sizeValue() | rpl::start_with_next([=](QSize size) {
		_forBothOverlay->setGeometry({ QPoint(), size });
		overlay->setGeometry({ QPoint(), size });
	}, _forBothOverlay->lifetime());

	overlay->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(overlay);
		p.drawImage(0, 0, bg);
		p.fillRect(clip, QColor(0, 0, 0, 64));
	}, overlay->lifetime());

	using namespace Ui;
	const auto forMe = CreateChild<RoundButton>(
		overlay,
		tr::lng_background_apply_me(),
		st::backgroundConfirm);
	forMe->setClickedCallback([=] {
		applyForPeer(false);
	});
	using namespace rpl::mappers;
	const auto forBoth = ::Settings::CreateLockedButton(
		overlay,
		tr::lng_background_apply_both(
			lt_user,
			rpl::single(_forPeer->shortName())),
		st::backgroundConfirm,
		Data::AmPremiumValue(&_forPeer->session()) | rpl::map(!_1));
	forBoth->setClickedCallback([=] {
		if (_forPeer->session().premium()) {
			applyForPeer(true);
		} else {
			ShowPremiumPreviewBox(
				_controller->uiShow(),
				PremiumFeature::Wallpapers);
		}
	});
	const auto cancel = CreateChild<RoundButton>(
		overlay,
		tr::lng_cancel(),
		st::backgroundConfirmCancel);
	cancel->setClickedCallback([=] {
		const auto raw = _forBothOverlay.release();
		raw->shownValue() | rpl::filter(
			!rpl::mappers::_1
		) | rpl::take(1) | rpl::start_with_next(crl::guard(raw, [=] {
			delete raw;
		}), raw->lifetime());
		raw->toggle(false, anim::type::normal);
	});
	forMe->setTextTransform(RoundButton::TextTransform::NoTransform);
	forBoth->setTextTransform(RoundButton::TextTransform::NoTransform);
	cancel->setTextTransform(RoundButton::TextTransform::NoTransform);

	overlay->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		const auto padding = st::backgroundConfirmPadding;
		const auto width = size.width()
			- padding.left()
			- padding.right();
		const auto height = cancel->height();
		auto top = size.height() - padding.bottom() - height;
		cancel->setGeometry(padding.left(), top, width, height);
		top -= height + padding.top();
		forBoth->setGeometry(padding.left(), top, width, height);
		top -= height + padding.top();
		forMe->setGeometry(padding.left(), top, width, height);
	}, _forBothOverlay->lifetime());

	_forBothOverlay->hide(anim::type::instant);
	_forBothOverlay->show(anim::type::normal);
}

void BackgroundPreviewBox::applyForPeer(bool both) {
	using namespace Data;
	if (forChannel() && !_paperEmojiId.isEmpty()) {
		setExistingForPeer(WallPaper::FromEmojiId(_paperEmojiId), both);
	} else if (IsCustomWallPaper(_paper)) {
		uploadForPeer(both);
	} else {
		setExistingForPeer(_paper, both);
	}
}

void BackgroundPreviewBox::applyForEveryone() {
	const auto install = (_paper.id() != Window::Theme::Background()->id())
		&& Data::IsCloudWallPaper(_paper);
	_controller->content()->setChatBackground(_paper, std::move(_full));
	if (install) {
		_controller->session().api().request(MTPaccount_InstallWallPaper(
			_paper.mtpInput(&_controller->session()),
			_paper.mtpSettings()
		)).send();
	}
	closeBox();
}

void BackgroundPreviewBox::share() {
	QGuiApplication::clipboard()->setText(
		_paper.shareUrl(&_controller->session()));
	showToast(tr::lng_background_link_copied(tr::now));
}

void BackgroundPreviewBox::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto ms = crl::now();
	if (_scaled.isNull()) {
		setScaledFromThumb();
	}
	if (!_generated.isNull()
		&& (_scaled.isNull()
			|| (_fadeOutThumbnail.isNull() && _fadeIn.animating()))) {
		p.drawPixmap(0, 0, _generated);
	}
	if (!_scaled.isNull()) {
		paintImage(p);
		const auto dimming = (_dimmed && _boxDarkMode.current())
			? _dimmingIntensity
			: 0;
		if (dimming > 0) {
			const auto alpha = 255 * dimming / 100;
			p.fillRect(e->rect(), QColor(0, 0, 0, alpha));
		}
		paintRadial(p);
	} else if (_generated.isNull()) {
		p.fillRect(e->rect(), st::boxBg);
		return;
	} else {
		// Progress of pattern loading.
		paintRadial(p);
	}
	paintTexts(p, ms);
	if (_dimmingToggleScheduled) {
		crl::on_main(this, [=] {
			if (!_dimmingToggleScheduled) {
				return;
			}
			_dimmingToggleScheduled = false;
			_dimmingWrap->toggle(_boxDarkMode.current(), anim::type::normal);
		});
	}
}

void BackgroundPreviewBox::paintImage(Painter &p) {
	Expects(!_scaled.isNull());

	const auto factor = style::DevicePixelRatio();
	const auto size = st::boxWideWidth;
	const auto from = QRect(
		0,
		(size - height()) / 2 * factor,
		size * factor,
		height() * factor);
	const auto guard = gsl::finally([&] { p.setOpacity(1.); });

	const auto fade = _fadeIn.value(1.);
	if (fade < 1. && !_fadeOutThumbnail.isNull()) {
		p.drawPixmap(rect(), _fadeOutThumbnail, from);
	}
	const auto &pixmap = (!_blurred.isNull() && _paper.isBlurred())
		? _blurred
		: _scaled;
	p.setOpacity(fade);
	p.drawPixmap(rect(), pixmap, from);
	checkBlurAnimationStart();
}

void BackgroundPreviewBox::paintRadial(Painter &p) {
	const auto radial = _radial.animating();
	const auto radialOpacity = radial ? _radial.opacity() : 0.;
	if (!radial) {
		return;
	}
	auto inner = radialRect();

	p.setPen(Qt::NoPen);
	p.setOpacity(radialOpacity);
	p.setBrush(st::radialBg);

	{
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(inner);
	}

	p.setOpacity(1);
	QRect arc(inner.marginsRemoved(QMargins(st::radialLine, st::radialLine, st::radialLine, st::radialLine)));
	_radial.draw(p, arc, st::radialLine, st::radialFg);
}

int BackgroundPreviewBox::textsTop() const {
	const auto bottom = _blur
		? _blur->y()
		: (height() - _dimmingHeight.current());
	return bottom
		- st::historyPaddingBottom
		- (_service ? _service->height() : 0)
		- _text1->height()
		- (forChannel() ? _text2->height() : 0);
}

QRect BackgroundPreviewBox::radialRect() const {
	const auto available = textsTop() - st::historyPaddingBottom;
	return QRect(
		QPoint(
			(width() - st::radialSize.width()) / 2,
			(available - st::radialSize.height()) / 2),
		st::radialSize);
}

void BackgroundPreviewBox::paintTexts(Painter &p, crl::time ms) {
	const auto heights = _service ? _service->height() : 0;
	const auto height1 = _text1->height();
	const auto height2 = _text2->height();
	auto context = _controller->defaultChatTheme()->preparePaintContext(
		_chatStyle.get(),
		rect(),
		rect(),
		_controller->isGifPausedAtLeastFor(Window::GifPauseReason::Layer));
	p.translate(0, textsTop());
	if (_service) {
		_service->draw(p, context);
		p.translate(0, heights);
	}

	context.outbg = _text1->hasOutLayout();
	_text1->draw(p, context);
	p.translate(0, height1);
	if (!forChannel()) {
		context.outbg = _text2->hasOutLayout();
		_text2->draw(p, context);
		p.translate(0, height2);
	}
}

void BackgroundPreviewBox::radialAnimationCallback(crl::time now) {
	const auto document = _paper.document();
	const auto wasAnimating = _radial.animating();
	const auto updated = _uploadId
		? _radial.update(_uploadProgress, !_uploadLifetime, now)
		: _radial.update(_media->progress(), !document->loading(), now);
	if ((wasAnimating || _radial.animating())
		&& (!anim::Disabled() || updated)) {
		update(radialRect());
	}
	checkLoadedDocument();
}

void BackgroundPreviewBox::setScaledFromThumb() {
	if (!_scaled.isNull()) {
		return;
	}
	const auto localThumbnail = _paper.localThumbnail();
	const auto thumbnail = localThumbnail
		? localThumbnail
		: _media
		? _media->thumbnail()
		: nullptr;
	if (!thumbnail) {
		return;
	} else if (_paper.isPattern() && _paper.document() != nullptr) {
		return;
	}
	auto scaled = PrepareScaledFromFull(
		thumbnail->original(),
		_paper.isPattern(),
		_paper.backgroundColors(),
		_paper.gradientRotation(),
		_paper.patternOpacity(),
		_paper.document() ? Images::Option::Blur : Images::Option());
	auto blurred = (_paper.document() || _paper.isPattern())
		? QImage()
		: PrepareScaledNonPattern(
			Ui::PrepareBlurredBackground(thumbnail->original()),
			Images::Option(0));
	setScaledFromImage(std::move(scaled), std::move(blurred));
}

void BackgroundPreviewBox::setScaledFromImage(
		QImage &&image,
		QImage &&blurred) {
	updateServiceBg({ Ui::CountAverageColor(image) });
	if (!_full.isNull()) {
		startFadeInFrom(std::move(_scaled));
	}
	_scaled = Ui::PixmapFromImage(std::move(image));
	_blurred = Ui::PixmapFromImage(std::move(blurred));
	if (_blur) {
		_blur->setDisabled(_paper.document() && _full.isNull());
	}
}

void BackgroundPreviewBox::startFadeInFrom(QPixmap previous) {
	_fadeOutThumbnail = std::move(previous);
	_fadeIn.start([=] { update(); }, 0., 1., st::backgroundCheck.duration);
}

void BackgroundPreviewBox::checkBlurAnimationStart() {
	if (_fadeIn.animating()
		|| _blurred.isNull()
		|| !_blur
		|| _paper.isBlurred() == _blur->checked()) {
		return;
	}
	_paper = _paper.withBlurred(_blur->checked());
	startFadeInFrom(_paper.isBlurred() ? _scaled : _blurred);
}

void BackgroundPreviewBox::updateServiceBg(const std::vector<QColor> &bg) {
	const auto count = int(bg.size());
	if (!count) {
		return;
	}
	auto red = 0LL, green = 0LL, blue = 0LL;
	for (const auto &color : bg) {
		red += color.red();
		green += color.green();
		blue += color.blue();
	}

	_serviceBgLifetime = _paletteServiceBg.value(
	) | rpl::start_with_next([=](QColor color) {
		_serviceBg = Ui::ThemeAdjustedColor(
			color,
			QColor(red / count, green / count, blue / count));
		_chatStyle->applyAdjustedServiceBg(*_serviceBg);
		recreateBlurCheckbox();
	});

	_service = GenerateServiceItem(
		delegate(),
		_serviceHistory,
		(forGroup()
			? tr::lng_background_other_group(tr::now)
			: forChannel()
			? tr::lng_background_other_channel(tr::now)
			: (_forPeer && !_fromMessageId)
			? tr::lng_background_other_info(
				tr::now,
				lt_user,
				_forPeer->shortName())
			: ItemDateText(_text1->data(), false)),
		false);
	_service->initDimensions();
	_service->resizeGetHeight(st::boxWideWidth);
}

void BackgroundPreviewBox::checkLoadedDocument() {
	const auto document = _paper.document();
	if (!_full.isNull()
		|| !document
		|| !_media->loaded(true)
		|| _generating) {
		return;
	}
	const auto generateCallback = [=](QImage &&image) {
		if (image.isNull()) {
			return;
		}
		crl::async([
			this,
			image = std::move(image),
			isPattern = _paper.isPattern(),
			background = _paper.backgroundColors(),
			gradientRotation = _paper.gradientRotation(),
			patternOpacity = _paper.patternOpacity(),
			guard = _generating.make_guard()
		]() mutable {
			auto scaled = PrepareScaledFromFull(
				image,
				isPattern,
				background,
				gradientRotation,
				patternOpacity);
			auto blurred = !isPattern
				? PrepareScaledNonPattern(
					Ui::PrepareBlurredBackground(image),
					Images::Option(0))
				: QImage();
			crl::on_main(std::move(guard), [
				this,
				image = std::move(image),
				scaled = std::move(scaled),
				blurred = std::move(blurred)
			]() mutable {
				_full = std::move(image);
				setScaledFromImage(std::move(scaled), std::move(blurred));
				update();
			});
		});
	};
	_generating = Data::ReadBackgroundImageAsync(
		_media.get(),
		Ui::PreprocessBackgroundImage,
		generateCallback);
}

bool BackgroundPreviewBox::Start(
		not_null<Window::SessionController*> controller,
		const QString &slug,
		const QMap<QString, QString> &params) {
	if (const auto paper = Data::WallPaper::FromColorsSlug(slug)) {
		controller->show(Box<BackgroundPreviewBox>(
			controller,
			paper->withUrlParams(params)));
		return true;
	}
	if (!IsValidWallPaperSlug(slug)) {
		controller->show(Ui::MakeInformBox(tr::lng_background_bad_link()));
		return false;
	}
	controller->session().api().requestWallPaper(slug, crl::guard(controller, [=](
			const Data::WallPaper &result) {
		controller->show(Box<BackgroundPreviewBox>(
			controller,
			result.withUrlParams(params)));
	}), crl::guard(controller, [=] {
		controller->show(Ui::MakeInformBox(tr::lng_background_bad_link()));
	}));
	return true;
}

HistoryView::Context BackgroundPreviewBox::elementContext() {
	return HistoryView::Context::ContactPreview;
}
