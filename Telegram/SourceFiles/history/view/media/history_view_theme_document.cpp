/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_theme_document.h"

#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_document_media.h"
#include "data/data_file_click_handler.h"
#include "data/data_file_origin.h"
#include "data/data_wall_paper.h"
#include "base/qthelp_url.h"
#include "core/local_url_handlers.h"
#include "ui/text/format_values.h"
#include "ui/chat/chat_theme.h"
#include "ui/cached_round_corners.h"
#include "ui/ui_utility.h"
#include "layout/layout_selection.h" // FullSelection
#include "styles/style_chat.h"

namespace HistoryView {

ThemeDocument::ThemeDocument(
	not_null<Element*> parent,
	DocumentData *document)
: ThemeDocument(parent, document, std::nullopt) {
}

ThemeDocument::ThemeDocument(
	not_null<Element*> parent,
	DocumentData *document,
	const std::optional<Data::WallPaper> &params)
: File(parent, parent->data())
, _data(document) {
	Expects(params.has_value() || _data->hasThumbnail() || _data->isTheme());

	if (params) {
		_background = params->backgroundColors();
		_patternOpacity = params->patternOpacity();
		_gradientRotation = params->gradientRotation();
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
	auto selected = (context.selection == FullSelection);
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
	auto roundRadius = ImageRoundRadius::Small;
	auto roundCorners = RectPart::AllCorners;
	validateThumbnail();
	p.drawPixmap(rthumb.topLeft(), _thumbnail);
	if (selected) {
		Ui::FillComplexOverlayRect(p, rthumb, roundRadius, roundCorners);
	}

	if (_data) {
		auto statusX = paintx + st::msgDateImgDelta + st::msgDateImgPadding.x();
		auto statusY = painty + st::msgDateImgDelta + st::msgDateImgPadding.y();
		auto statusW = st::normalFont->width(_statusText) + 2 * st::msgDateImgPadding.x();
		auto statusH = st::normalFont->height + 2 * st::msgDateImgPadding.y();
		Ui::FillRoundRect(p, style::rtlrect(statusX - st::msgDateImgPadding.x(), statusY - st::msgDateImgPadding.y(), statusW, statusH, width()), selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? Ui::DateSelectedCorners : Ui::DateCorners);
		p.setFont(st::normalFont);
		p.setPen(st::msgDateImgFg);
		p.drawTextLeft(statusX, statusY, width(), _statusText, statusW - 2 * st::msgDateImgPadding.x());
		if (radial || (!loaded && !_data->loading())) {
			const auto radialOpacity = (radial && loaded && !_data->uploading())
				? _animation->radial.opacity() :
				1.;
			const auto innerSize = st::msgFileLayout.thumbSize;
			QRect inner(rthumb.x() + (rthumb.width() - innerSize) / 2, rthumb.y() + (rthumb.height() - innerSize) / 2, innerSize, innerSize);
			p.setPen(Qt::NoPen);
			if (selected) {
				p.setBrush(st::msgDateImgBgSelected);
			} else if (isThumbAnimation()) {
				auto over = _animation->a_thumbOver.value(1.);
				p.setBrush(anim::brush(st::msgDateImgBg, st::msgDateImgBgOver, over));
			} else {
				auto over = ClickHandler::showAsActive(_data->loading() ? _cancell : _openl);
				p.setBrush(over ? st::msgDateImgBgOver : st::msgDateImgBg);
			}

			p.setOpacity(radialOpacity * p.opacity());

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(inner);
			}

			p.setOpacity(radialOpacity);
			auto icon = ([radial, this, selected]() -> const style::icon* {
				if (radial || _data->loading()) {
					return &(selected ? st::historyFileThumbCancelSelected : st::historyFileThumbCancel);
				}
				return &(selected ? st::historyFileThumbDownloadSelected : st::historyFileThumbDownload);
			})();
			if (icon) {
				icon->paintInCenter(p, inner);
			}
			p.setOpacity(1);
			if (radial) {
				QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
				_animation->radial.draw(p, rinner, st::msgFileRadialLine, selected ? st::historyFileThumbRadialFgSelected : st::historyFileThumbRadialFg);
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

void ThemeDocument::generateThumbnail() const {
	_thumbnail = Ui::PixmapFromImage(Ui::GenerateBackgroundImage(
		QSize(_pixw, _pixh) * cIntRetinaFactor(),
		_background,
		_gradientRotation,
		_patternOpacity));
	_thumbnail.setDevicePixelRatio(cRetinaFactor());
	_thumbnailGood = 1;
}

void ThemeDocument::prepareThumbnailFrom(
		not_null<Image*> image,
		int good) const {
	Expects(_data != nullptr);
	Expects(_thumbnailGood <= good);

	const auto isTheme = _data->isTheme();
	const auto isPattern = _data->isPatternWallPaper();
	auto options = Images::Option::Smooth
		| (good >= 0 ? Images::Option(0) : Images::Option::Blurred)
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
	original = Images::prepare(
		std::move(original),
		_pixw * cIntRetinaFactor(),
		((_pixw * th) / tw) * cIntRetinaFactor(),
		options,
		_pixw,
		_pixh);
	if (isPattern) {
		original = Ui::PreparePatternImage(
			std::move(original),
			_background,
			_gradientRotation,
			_patternOpacity);
		original.setDevicePixelRatio(cRetinaFactor());
	}
	_thumbnail = Ui::PixmapFromImage(std::move(original));
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

QString ThemeDocument::additionalInfoString() const {
	// This will force message info (time) to be displayed below
	// this attachment in WebPage media.
	static auto result = QString(" ");
	return result;
}

bool ThemeDocument::hasHeavyPart() const {
	return (_dataMedia != nullptr);
}

void ThemeDocument::unloadHeavyPart() {
	_dataMedia = nullptr;
}

} // namespace HistoryView
