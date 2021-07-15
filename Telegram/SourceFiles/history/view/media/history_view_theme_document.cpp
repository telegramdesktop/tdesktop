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
#include "ui/text/format_values.h"
#include "ui/cached_round_corners.h"
#include "ui/ui_utility.h"
#include "layout.h" // FullSelection
#include "styles/style_chat.h"

namespace HistoryView {

ThemeDocument::ThemeDocument(
	not_null<Element*> parent,
	not_null<DocumentData*> document,
	const QString &url)
: File(parent, parent->data())
, _data(document) {
	Expects(_data->hasThumbnail() || _data->isTheme());

	if (_data->isWallPaper()) {
		fillPatternFieldsFrom(url);
	}

	_data->loadThumbnail(_parent->data()->fullId());
	setDocumentLinks(_data, parent->data());
	setStatusSize(Ui::FileStatusSizeReady, _data->size, -1, 0);
}

ThemeDocument::~ThemeDocument() {
	if (_dataMedia) {
		_data->owner().keepAlive(base::take(_dataMedia));
		_parent->checkHeavyPart();
	}
}

void ThemeDocument::fillPatternFieldsFrom(const QString &url) {
	const auto paramsPosition = url.indexOf('?');
	if (paramsPosition < 0) {
		return;
	}
	const auto paramsString = url.mid(paramsPosition + 1);
	const auto params = qthelp::url_parse_params(
		paramsString,
		qthelp::UrlParamNameTransform::ToLower);
	const auto kDefaultBackground = QColor(213, 223, 233);
	const auto paper = Data::DefaultWallPaper().withUrlParams(params);
	_intensity = paper.patternIntensity();
	_background = paper.backgroundColor().value_or(kDefaultBackground);
}

QSize ThemeDocument::countOptimalSize() {
	if (_data->isTheme()) {
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
	if (_data->isTheme()) {
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

void ThemeDocument::draw(Painter &p, const QRect &r, TextSelection selection, crl::time ms) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	ensureDataMediaCreated();

	_dataMedia->automaticLoad(_realParent->fullId(), _parent->data());
	auto selected = (selection == FullSelection);
	auto loaded = dataLoaded();
	auto displayLoading = _data->displayLoading();

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

void ThemeDocument::ensureDataMediaCreated() const {
	if (_dataMedia) {
		return;
	}
	_dataMedia = _data->createMediaView();
	_dataMedia->goodThumbnailWanted();
	_dataMedia->thumbnailWanted(_realParent->fullId());
	_parent->history()->owner().registerHeavyViewPart(_parent);
}

void ThemeDocument::validateThumbnail() const {
	if (_thumbnailGood > 0) {
		return;
	}
	ensureDataMediaCreated();
	if (const auto good = _dataMedia->goodThumbnail()) {
		prepareThumbnailFrom(good, 1);
		return;
	}
	if (_thumbnailGood >= 0) {
		return;
	}
	if (const auto normal = _dataMedia->thumbnail()) {
		prepareThumbnailFrom(normal, 0);
	} else if (_thumbnail.isNull()) {
		if (const auto blurred = _dataMedia->thumbnailInline()) {
			prepareThumbnailFrom(blurred, -1);
		}
	}
}

void ThemeDocument::prepareThumbnailFrom(
		not_null<Image*> image,
		int good) const {
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
		original = Data::PreparePatternImage(
			std::move(original),
			_background,
			Data::PatternColor(_background),
			_intensity);
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
		if (_data->uploading()) {
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
	return _dataMedia->progress();
}

bool ThemeDocument::dataFinished() const {
	return !_data->loading()
		&& (!_data->uploading() || _data->waitingForAlbum());
}

bool ThemeDocument::dataLoaded() const {
	ensureDataMediaCreated();
	return _dataMedia->loaded();
}

bool ThemeDocument::isReadyForOpen() const {
	ensureDataMediaCreated();
	return _dataMedia->loaded();
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
