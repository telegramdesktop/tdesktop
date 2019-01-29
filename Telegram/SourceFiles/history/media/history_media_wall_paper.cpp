/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/media/history_media_wall_paper.h"

#include "layout.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "base/qthelp_url.h"
#include "window/themes/window_theme.h"
#include "styles/style_history.h"

namespace {

using TextState = HistoryView::TextState;

} // namespace

HistoryWallPaper::HistoryWallPaper(
	not_null<Element*> parent,
	not_null<DocumentData*> document,
	const QString &url)
: HistoryFileMedia(parent, parent->data())
, _data(document) {
	Expects(_data->hasThumbnail());

	fillPatternFieldsFrom(url);

	_data->thumbnail()->load(parent->data()->fullId());
	setDocumentLinks(_data, parent->data());
	setStatusSize(FileStatusSizeReady, _data->size, -1, 0);
}

void HistoryWallPaper::fillPatternFieldsFrom(const QString &url) {
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

QSize HistoryWallPaper::countOptimalSize() {
	auto tw = ConvertScale(_data->thumbnail()->width());
	auto th = ConvertScale(_data->thumbnail()->height());
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

QSize HistoryWallPaper::countCurrentSize(int newWidth) {
	auto tw = ConvertScale(_data->thumbnail()->width());
	auto th = ConvertScale(_data->thumbnail()->height());
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

void HistoryWallPaper::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	_data->automaticLoad(_realParent->fullId(), _parent->data());
	auto selected = (selection == FullSelection);
	auto loaded = _data->loaded();
	auto displayLoading = _data->displayLoading();

	auto inWebPage = (_parent->media() != this);
	auto paintx = 0, painty = 0, paintw = width(), painth = height();

	auto captionw = paintw - st::msgPadding.left() - st::msgPadding.right();

	if (displayLoading) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(_data->progress());
		}
	}
	bool radial = isRadialAnimation(ms);

	auto rthumb = rtlrect(paintx, painty, paintw, painth, width());
	auto roundRadius = ImageRoundRadius::Small;
	auto roundCorners = RectPart::AllCorners;
	validateThumbnail();
	p.drawPixmap(rthumb.topLeft(), _thumbnail);
	if (selected) {
		App::complexOverlayRect(p, rthumb, roundRadius, roundCorners);
	}

	auto statusX = paintx + st::msgDateImgDelta + st::msgDateImgPadding.x();
	auto statusY = painty + st::msgDateImgDelta + st::msgDateImgPadding.y();
	auto statusW = st::normalFont->width(_statusText) + 2 * st::msgDateImgPadding.x();
	auto statusH = st::normalFont->height + 2 * st::msgDateImgPadding.y();
	App::roundRect(p, rtlrect(statusX - st::msgDateImgPadding.x(), statusY - st::msgDateImgPadding.y(), statusW, statusH, width()), selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);
	p.setFont(st::normalFont);
	p.setPen(st::msgDateImgFg);
	p.drawTextLeft(statusX, statusY, width(), _statusText, statusW - 2 * st::msgDateImgPadding.x());

	if (radial || (!loaded && !_data->loading())) {
		const auto radialOpacity = (radial && loaded && !_data->uploading())
			? _animation->radial.opacity() :
			1.;
		QRect inner(rthumb.x() + (rthumb.width() - st::msgFileSize) / 2, rthumb.y() + (rthumb.height() - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
		p.setPen(Qt::NoPen);
		if (selected) {
			p.setBrush(st::msgDateImgBgSelected);
		} else if (isThumbAnimation(ms)) {
			auto over = _animation->a_thumbOver.current();
			p.setBrush(anim::brush(st::msgDateImgBg, st::msgDateImgBgOver, over));
		} else {
			auto over = ClickHandler::showAsActive(_data->loading() ? _cancell : _savel);
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

void HistoryWallPaper::validateThumbnail() const {
	if (_thumbnailGood > 0) {
		return;
	}
	const auto good = _data->goodThumbnail();
	if (good) {
		if (good->loaded()) {
			prepareThumbnailFrom(good, 1);
			return;
		} else {
			good->load({});
		}
	}
	if (_thumbnailGood >= 0) {
		return;
	}
	if (_data->thumbnail()->loaded()) {
		prepareThumbnailFrom(_data->thumbnail(), 0);
	} else if (const auto blurred = _data->thumbnailInline()) {
		if (_thumbnail.isNull()) {
			prepareThumbnailFrom(blurred, -1);
		}
	}
}

void HistoryWallPaper::prepareThumbnailFrom(
		not_null<Image*> image,
		int good) const {
	Expects(_thumbnailGood <= good);

	const auto isPattern = _data->isPatternWallPaper();
	auto options = Images::Option::Smooth
		| (good >= 0 ? Images::Option(0) : Images::Option::Blurred)
		| (isPattern
			? Images::Option::TransparentBackground
			: Images::Option(0));
	auto original = image->original();
	auto tw = ConvertScale(_data->thumbnail()->width());
	auto th = ConvertScale(_data->thumbnail()->height());
	if (!tw || !th) {
		tw = th = 1;
	}
	original = Images::prepare(
		std::move(original),
		_pixw,
		(_pixw * th) / tw,
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
	_thumbnail = App::pixmapFromImageInPlace(std::move(original));
	_thumbnailGood = good;
}

TextState HistoryWallPaper::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);

	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	}
	auto paintx = 0, painty = 0, paintw = width(), painth = height();
	auto bubble = _parent->hasBubble();
	if (QRect(paintx, painty, paintw, painth).contains(point)) {
		if (_data->uploading()) {
			result.link = _cancell;
		} else if (_data->loaded()) {
			result.link = _openl;
		} else if (_data->loading()) {
			result.link = _cancell;
		} else {
			result.link = _savel;
		}
	}
	return result;
}

float64 HistoryWallPaper::dataProgress() const {
	return _data->progress();
}

bool HistoryWallPaper::dataFinished() const {
	return !_data->loading()
		&& (!_data->uploading() || _data->waitingForAlbum());
}

bool HistoryWallPaper::dataLoaded() const {
	return _data->loaded();
}

bool HistoryWallPaper::isReadyForOpen() const {
	return _data->loaded();
}

QString HistoryWallPaper::additionalInfoString() const {
	// This will force message info (time) to be displayed below
	// this attachment in HistoryWebPage media.
	static auto result = QString(" ");
	return result;
}
