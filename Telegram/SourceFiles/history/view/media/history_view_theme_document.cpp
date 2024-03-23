/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_theme_document.h"

#include "apiwrap.h"
#include "boxes/background_preview_box.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/media/history_view_sticker_player_abstract.h"
#include "data/data_changes.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_document_media.h"
#include "data/data_file_click_handler.h"
#include "data/data_file_origin.h"
#include "data/data_wall_paper.h"
#include "base/qthelp_url.h"
#include "core/click_handler_types.h"
#include "core/local_url_handlers.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/text/format_values.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/cached_round_corners.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "window/themes/window_theme.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

[[nodiscard]] bool WallPaperRevertable(
		not_null<PeerData*> peer,
		const Data::WallPaper &paper) {
	if (!peer->wallPaperOverriden()) {
		return false;
	}
	const auto now = peer->wallPaper();
	return now && now->equals(paper);
}

[[nodiscard]] bool WallPaperRevertable(not_null<HistoryItem*> item) {
	const auto media = item->media();
	const auto paper = media ? media->paper() : nullptr;
	return paper
		&& media->paperForBoth()
		&& WallPaperRevertable(item->history()->peer, *paper);
}

[[nodiscard]] rpl::producer<bool> WallPaperRevertableValue(
		not_null<HistoryItem*> item) {
	const auto media = item->media();
	const auto paper = media ? media->paper() : nullptr;
	if (!paper || !media->paperForBoth()) {
		return rpl::single(false);
	}
	const auto peer = item->history()->peer;
	return peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::ChatWallPaper
	) | rpl::map([peer, paper = *paper] {
		return WallPaperRevertable(peer, paper);
	});
}

} // namespace

ThemeDocument::ThemeDocument(
	not_null<Element*> parent,
	DocumentData *document)
: ThemeDocument(parent, document, std::nullopt, 0) {
}

ThemeDocument::ThemeDocument(
	not_null<Element*> parent,
	DocumentData *document,
	const std::optional<Data::WallPaper> &params,
	int serviceWidth)
: File(parent, parent->data())
, _data(document)
, _serviceWidth(serviceWidth) {
	Expects(params.has_value() || _data->hasThumbnail() || _data->isTheme());

	if (params) {
		_background = params->backgroundColors();
		_patternOpacity = params->patternOpacity();
		_gradientRotation = params->gradientRotation();
		_blurredWallPaper = params->isBlurred();
		_dimmingIntensity = (!params->document()
			|| params->isPattern()
			|| !_serviceWidth)
			? 0
			: std::max(params->patternIntensity(), 0);
	}
	const auto fullId = _parent->data()->fullId();
	if (_data) {
		_data->loadThumbnail(fullId);
		setDocumentLinks(_data, parent->data());
		setStatusSize(Ui::FileStatusSizeReady, _data->size, -1, 0);
	} else {
		class EmptyFileClickHandler final : public FileClickHandler {
		public:
			using FileClickHandler::FileClickHandler;

		private:
			void onClickImpl() const override {
			}

		};

		// We could open BackgroundPreviewBox here, but right now
		// WebPage that created ThemeDocument as its attachment does it.
		//
		// So just provide a non-null click handler for this hack to work.
		setLinks(
			std::make_shared<EmptyFileClickHandler>(fullId),
			nullptr,
			nullptr);
	}
}

ThemeDocument::~ThemeDocument() {
	if (_dataMedia) {
		_data->owner().keepAlive(base::take(_dataMedia));
		_parent->checkHeavyPart();
	}
}

std::optional<Data::WallPaper> ThemeDocument::ParamsFromUrl(
		const QString &url) {
	const auto local = Core::TryConvertUrlToLocal(url);
	const auto paramsPosition = local.indexOf('?');
	if (paramsPosition < 0) {
		return std::nullopt;
	}
	const auto paramsString = local.mid(paramsPosition + 1);
	const auto params = qthelp::url_parse_params(
		paramsString,
		qthelp::UrlParamNameTransform::ToLower);
	auto paper = Data::DefaultWallPaper().withUrlParams(params);
	return paper.backgroundColors().empty()
		? std::nullopt
		: std::make_optional(std::move(paper));
}

QSize ThemeDocument::countOptimalSize() {
	if (_serviceWidth > 0) {
		return { _serviceWidth, _serviceWidth };
	}

	if (!_data) {
		return { st::maxWallPaperWidth, st::maxWallPaperHeight };
	} else if (_data->isTheme()) {
		return st::historyThemeSize;
	}
	const auto &location = _data->thumbnailLocation();
	auto tw = style::ConvertScale(location.width());
	auto th = style::ConvertScale(location.height());
	if (!tw || !th) {
		tw = th = 1;
	}
	th = (st::maxWallPaperWidth * th) / tw;
	tw = st::maxWallPaperWidth;

	const auto maxWidth = tw;
	const auto minHeight = std::clamp(
		th,
		st::minPhotoSize,
		st::maxWallPaperHeight);
	return { maxWidth, minHeight };
}

QSize ThemeDocument::countCurrentSize(int newWidth) {
	if (_serviceWidth) {
		_pixw = _pixh = _serviceWidth;
		return { _serviceWidth, _serviceWidth };
	}
	if (!_data) {
		_pixw = st::maxWallPaperWidth;
		_pixh = st::maxWallPaperHeight;
		return { _pixw, _pixh };
	} else if (_data->isTheme()) {
		_pixw = st::historyThemeSize.width();
		_pixh = st::historyThemeSize.height();
		return st::historyThemeSize;
	}
	const auto &location = _data->thumbnailLocation();
	auto tw = style::ConvertScale(location.width());
	auto th = style::ConvertScale(location.height());
	if (!tw || !th) {
		tw = th = 1;
	}

	// We use pix() for image copies, because we rely that backgrounds
	// are always displayed with the same dimensions (not pixSingle()).
	_pixw = maxWidth();// std::min(newWidth, maxWidth());
	_pixh = minHeight();// (_pixw * th / tw);

	newWidth = _pixw;
	const auto newHeight = _pixh; /*std::clamp(
		_pixh,
		st::minPhotoSize,
		st::maxWallPaperHeight);*/
	return { newWidth, newHeight };
}

void ThemeDocument::draw(Painter &p, const PaintContext &context) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	ensureDataMediaCreated();

	if (_data) {
		_dataMedia->automaticLoad(_realParent->fullId(), _parent->data());
	}
	const auto st = context.st;
	const auto sti = context.imageStyle();
	auto loaded = dataLoaded();
	auto displayLoading = _data && _data->displayLoading();

	auto paintx = 0, painty = 0, paintw = width(), painth = height();

	if (displayLoading) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(dataProgress());
		}
	}
	const auto radial = isRadialAnimation();

	auto rthumb = style::rtlrect(paintx, painty, paintw, painth, width());
	validateThumbnail();
	p.drawPixmap(rthumb.topLeft(), _thumbnail);
	if (context.selected()) {
		Ui::FillComplexOverlayRect(
			p,
			rthumb,
			st->msgSelectOverlay(),
			st->msgSelectOverlayCorners(Ui::CachedCornerRadius::Small));
	}

	if (_data) {
		if (!_serviceWidth) {
			auto statusX = paintx + st::msgDateImgDelta + st::msgDateImgPadding.x();
			auto statusY = painty + st::msgDateImgDelta + st::msgDateImgPadding.y();
			auto statusW = st::normalFont->width(_statusText) + 2 * st::msgDateImgPadding.x();
			auto statusH = st::normalFont->height + 2 * st::msgDateImgPadding.y();
			Ui::FillRoundRect(p, style::rtlrect(statusX - st::msgDateImgPadding.x(), statusY - st::msgDateImgPadding.y(), statusW, statusH, width()), sti->msgDateImgBg, sti->msgDateImgBgCorners);
			p.setFont(st::normalFont);
			p.setPen(st->msgDateImgFg());
			p.drawTextLeft(statusX, statusY, width(), _statusText, statusW - 2 * st::msgDateImgPadding.x());
		}
		if (radial || (!loaded && !_data->loading())) {
			const auto radialOpacity = (radial && loaded && !_data->uploading())
				? _animation->radial.opacity() :
				1.;
			const auto innerSize = st::msgFileLayout.thumbSize;
			QRect inner(rthumb.x() + (rthumb.width() - innerSize) / 2, rthumb.y() + (rthumb.height() - innerSize) / 2, innerSize, innerSize);
			p.setPen(Qt::NoPen);
			if (context.selected()) {
				p.setBrush(st->msgDateImgBgSelected());
			} else if (isThumbAnimation()) {
				auto over = _animation->a_thumbOver.value(1.);
				p.setBrush(anim::brush(st->msgDateImgBg(), st->msgDateImgBgOver(), over));
			} else {
				auto over = ClickHandler::showAsActive(_data->loading() ? _cancell : _openl);
				p.setBrush(over ? st->msgDateImgBgOver() : st->msgDateImgBg());
			}

			p.setOpacity(radialOpacity * p.opacity());

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(inner);
			}

			p.setOpacity(radialOpacity);
			const auto &icon = (radial || _data->loading())
				? sti->historyFileThumbCancel
				: sti->historyFileThumbDownload;
			icon.paintInCenter(p, inner);
			p.setOpacity(1);
			if (radial) {
				QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
				_animation->radial.draw(p, rinner, st::msgFileRadialLine, sti->historyFileThumbRadialFg);
			}
		}
	}
}

void ThemeDocument::ensureDataMediaCreated() const {
	if (_dataMedia || !_data) {
		return;
	}
	_dataMedia = _data->createMediaView();
	if (checkGoodThumbnail()) {
		_dataMedia->goodThumbnailWanted();
	}
	_dataMedia->thumbnailWanted(_realParent->fullId());
	_parent->history()->owner().registerHeavyViewPart(_parent);
}

bool ThemeDocument::checkGoodThumbnail() const {
	return _data && (!_data->hasThumbnail() || !_data->isPatternWallPaper());
}

void ThemeDocument::validateThumbnail() const {
	const auto isDark = Window::Theme::IsNightMode();
	if (_isDark != isDark) {
		_isDark = isDark;
		_thumbnailGood = -1;
	}
	if (checkGoodThumbnail()) {
		if (_thumbnailGood > 0) {
			return;
		}
		ensureDataMediaCreated();
		if (const auto good = _dataMedia->goodThumbnail()) {
			prepareThumbnailFrom(good, 1);
			return;
		}
	}
	if (_thumbnailGood >= 0) {
		return;
	}
	if (!_data) {
		generateThumbnail();
		return;
	}
	ensureDataMediaCreated();
	if (const auto normal = _dataMedia->thumbnail()) {
		prepareThumbnailFrom(normal, 0);
	} else if (_thumbnail.isNull()) {
		if (const auto blurred = _dataMedia->thumbnailInline()) {
			prepareThumbnailFrom(blurred, -1);
		}
	}
}

QImage ThemeDocument::finishServiceThumbnail(QImage image) const {
	if (!_serviceWidth) {
		return image;
	} else if (_isDark && _dimmingIntensity > 0) {
		image.setDevicePixelRatio(style::DevicePixelRatio());
		auto p = QPainter(&image);
		const auto alpha = 255 * _dimmingIntensity / 100;
		p.fillRect(0, 0, _pixw, _pixh, QColor(0, 0, 0, alpha));
	}
	if (_blurredWallPaper) {
		constexpr auto kRadius = 16;
		image = Images::BlurLargeImage(std::move(image), kRadius);
	}
	return Images::Circle(std::move(image));
}

void ThemeDocument::generateThumbnail() const {
	auto image = Ui::GenerateBackgroundImage(
		QSize(_pixw, _pixh) * style::DevicePixelRatio(),
		_background,
		_gradientRotation,
		_patternOpacity);
	_thumbnail = Ui::PixmapFromImage(
		finishServiceThumbnail(std::move(image)));
	_thumbnail.setDevicePixelRatio(style::DevicePixelRatio());
	_thumbnailGood = 1;
}

void ThemeDocument::prepareThumbnailFrom(
		not_null<Image*> image,
		int good) const {
	Expects(_data != nullptr);
	Expects(_thumbnailGood <= good);

	const auto isTheme = _data->isTheme();
	const auto isPattern = _data->isPatternWallPaper();
	auto options = (good >= 0 ? Images::Option(0) : Images::Option::Blur)
		| (isPattern
			? Images::Option::TransparentBackground
			: Images::Option(0));
	auto original = image->original();
	const auto &location = _data->thumbnailLocation();
	auto tw = isTheme ? _pixw : style::ConvertScale(location.width());
	auto th = isTheme ? _pixh : style::ConvertScale(location.height());
	if (!tw || !th) {
		tw = th = 1;
	}
	const auto ratio = style::DevicePixelRatio();
	const auto resizeTo = _serviceWidth
		? QSize(tw, th).scaled(_pixw, _pixh, Qt::KeepAspectRatioByExpanding)
		: QSize(_pixw, (_pixw * th) / tw);
	original = Images::Prepare(
		std::move(original),
		resizeTo * ratio,
		{ .options = options, .outer = { _pixw, _pixh } });
	if (isPattern) {
		original = Ui::PreparePatternImage(
			std::move(original),
			_background,
			_gradientRotation,
			_patternOpacity);
		original.setDevicePixelRatio(ratio);
	}
	_thumbnail = Ui::PixmapFromImage(
		finishServiceThumbnail(std::move(original)));
	_thumbnailGood = good;
}

TextState ThemeDocument::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);

	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	}
	auto paintx = 0, painty = 0, paintw = width(), painth = height();
	if (QRect(paintx, painty, paintw, painth).contains(point)) {
		if (!_data) {
			result.link = _openl;
		} else if (_data->uploading()) {
			result.link = _cancell;
		} else if (dataLoaded()) {
			result.link = _openl;
		} else if (_data->loading()) {
			result.link = _cancell;
		} else {
			result.link = _openl;
		}
	}
	return result;
}

float64 ThemeDocument::dataProgress() const {
	ensureDataMediaCreated();
	return _data ? _dataMedia->progress() : 1.;
}

bool ThemeDocument::dataFinished() const {
	return !_data
		|| (!_data->loading()
			&& (!_data->uploading() || _data->waitingForAlbum()));
}

bool ThemeDocument::dataLoaded() const {
	ensureDataMediaCreated();
	return !_data || _dataMedia->loaded();
}

bool ThemeDocument::isReadyForOpen() const {
	ensureDataMediaCreated();
	return !_data || _dataMedia->loaded();
}

bool ThemeDocument::hasHeavyPart() const {
	return (_dataMedia != nullptr);
}

void ThemeDocument::unloadHeavyPart() {
	_dataMedia = nullptr;
}

ThemeDocumentBox::ThemeDocumentBox(
	not_null<Element*> parent,
	const Data::WallPaper &paper)
: _parent(parent)
, _emojiId(paper.emojiId()) {
	Window::WallPaperResolved(
		&_parent->history()->owner(),
		&paper
	) | rpl::start_with_next([=](const Data::WallPaper *paper) {
		_parent->repaint();
		if (!paper) {
			_preview.reset();
		} else {
			createPreview(*paper);
		}
	}, _lifetime);
}

void ThemeDocumentBox::createPreview(const Data::WallPaper &paper) {
	_preview.emplace(
		_parent,
		paper.document(),
		paper,
		st::msgServicePhotoWidth);
	_preview->initDimensions();
	_preview->resizeGetHeight(_preview->maxWidth());
}

ThemeDocumentBox::~ThemeDocumentBox() = default;

int ThemeDocumentBox::top() {
	return st::msgServiceGiftBoxButtonMargins.top();
}

QSize ThemeDocumentBox::size() {
	return _preview
		? QSize(_preview->maxWidth(), _preview->minHeight())
		: QSize(st::msgServicePhotoWidth, st::msgServicePhotoWidth);
}

QString ThemeDocumentBox::title() {
	return QString();
}

TextWithEntities ThemeDocumentBox::subtitle() {
	return _parent->data()->notificationText();
}

rpl::producer<QString> ThemeDocumentBox::button() {
	if (_parent->data()->out()) {
		return {};
	}
	return rpl::conditional(
		WallPaperRevertableValue(_parent->data()),
		tr::lng_action_set_wallpaper_remove(),
		tr::lng_action_set_wallpaper_button());
}

ClickHandlerPtr ThemeDocumentBox::createViewLink() {
	const auto to = _parent->history()->peer;
	if (to->isChannel()) {
		return nullptr;
	}
	const auto out = _parent->data()->out();
	const auto media = _parent->data()->media();
	const auto weak = base::make_weak(_parent);
	const auto paper = media ? media->paper() : nullptr;
	const auto maybe = paper ? *paper : std::optional<Data::WallPaper>();
	const auto itemId = _parent->data()->fullId();
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			const auto view = weak.get();
			if (view
				&& !view->data()->out()
				&& WallPaperRevertable(view->data())) {
				const auto reset = crl::guard(weak, [=](Fn<void()> close) {
					const auto api = &controller->session().api();
					api->request(MTPmessages_SetChatWallPaper(
						MTP_flags(MTPmessages_SetChatWallPaper::Flag::f_revert),
						view->data()->history()->peer->input,
						MTPInputWallPaper(),
						MTPWallPaperSettings(),
						MTPint()
					)).done([=](const MTPUpdates &result) {
						api->applyUpdates(result);
					}).send();
					close();
				});
				controller->show(Ui::MakeConfirmBox({
					.text = tr::lng_background_sure_reset_default(),
					.confirmed = reset,
					.confirmText = tr::lng_background_reset_default(),
				}));
			} else if (out) {
				controller->toggleChooseChatTheme(to);
			} else if (maybe) {
				controller->show(Box<BackgroundPreviewBox>(
					controller,
					*maybe,
					BackgroundPreviewArgs{ to, itemId }));
			}
		}
	});
}

void ThemeDocumentBox::draw(
		Painter &p,
		const PaintContext &context,
		const QRect &geometry) {
	if (_preview) {
		p.translate(geometry.topLeft());
		_preview->draw(p, context);
		p.translate(-geometry.topLeft());
	}
}

void ThemeDocumentBox::stickerClearLoopPlayed() {
}

std::unique_ptr<StickerPlayer> ThemeDocumentBox::stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) {
	return nullptr;
}

bool ThemeDocumentBox::hasHeavyPart() {
	return _preview && _preview->hasHeavyPart();
}

void ThemeDocumentBox::unloadHeavyPart() {
	if (_preview) {
		_preview->unloadHeavyPart();
	}
}

} // namespace HistoryView
