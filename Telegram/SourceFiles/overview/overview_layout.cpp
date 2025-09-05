/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "overview/overview_layout.h"

#include "overview/overview_checkbox.h"
#include "overview/overview_layout_delegate.h"
#include "core/ui_integration.h" // TextContext
#include "data/data_document.h"
#include "data/data_document_resolver.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "data/data_peer.h"
#include "data/data_photo_media.h"
#include "data/data_document_media.h"
#include "data/data_file_click_handler.h"
#include "ui/boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "layout/layout_selection.h"
#include "storage/file_upload.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "storage/localstorage.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/media/history_view_media_common.h"
#include "history/view/media/history_view_document.h" // DrawThumbnailAsSongCover
#include "base/unixtime.h"
#include "boxes/sticker_set_box.h"
#include "ui/effects/round_checkbox.h"
#include "ui/effects/spoiler_mess.h"
#include "ui/image/image.h"
#include "ui/text/format_song_document_name.h"
#include "ui/text/format_values.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/cached_round_corners.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/ui_utility.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_overview.h"

namespace Overview::Layout {
namespace {

using TextState = HistoryView::TextState;

TextParseOptions _documentNameOptions = {
	TextParseMultiline | TextParseLinks | TextParseMarkdown, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

constexpr auto kMaxInlineArea = 1280 * 720;
constexpr auto kStoryRatio = 1.46;

[[nodiscard]] bool CanPlayInline(not_null<DocumentData*> document) {
	const auto dimensions = document->dimensions;
	return dimensions.width() * dimensions.height() <= kMaxInlineArea;
}

[[nodiscard]] QImage CropMediaFrame(QImage image, int width, int height) {
	const auto ratio = style::DevicePixelRatio();
	width *= ratio;
	height *= ratio;
	const auto finalize = [&](QImage result) {
		result = result.scaled(
			width,
			height,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		result.setDevicePixelRatio(ratio);
		return result;
	};
	if (image.width() * height == image.height() * width) {
		if (image.width() != width) {
			return finalize(std::move(image));
		}
		image.setDevicePixelRatio(ratio);
		return image;
	} else if (image.width() * height > image.height() * width) {
		const auto use = (image.height() * width) / height;
		const auto skip = (image.width() - use) / 2;
		return finalize(image.copy(skip, 0, use, image.height()));
	} else {
		const auto use = (image.width() * height) / width;
		const auto skip = (image.height() - use) / 2;
		return finalize(image.copy(0, skip, image.width(), use));
	}
}

void PaintSensitiveTag(Painter &p, QRect r) {
	auto text = Ui::Text::String();
	text.setText(
		st::semiboldTextStyle,
		tr::lng_sensitive_tag(tr::now));
	const auto width = text.maxWidth();
	const auto inner = QRect(0, 0, width, text.minHeight());
	const auto outer = style::centerrect(r, inner.marginsAdded(st::paidTagPadding));
	const auto size = outer.size();
	const auto radius = std::min(size.width(), size.height()) / 2;
	auto hq = PainterHighQualityEnabler(p);

	p.setPen(Qt::NoPen);
	p.setBrush(st::radialBg);
	p.drawRoundedRect(outer, radius, radius);
	p.setPen(st::radialFg);
	text.draw(p, {
		.position = outer.marginsRemoved(st::paidTagPadding).topLeft(),
	});
}

} // namespace

ItemBase::ItemBase(
	not_null<Delegate*> delegate,
	not_null<HistoryItem*> parent)
: _delegate(delegate)
, _parent(parent)
, _dateTime(ItemDateTime(parent)) {
}

ItemBase::~ItemBase() = default;

QDateTime ItemBase::dateTime() const {
	return _dateTime;
}

void ItemBase::clickHandlerActiveChanged(
		const ClickHandlerPtr &action,
		bool active) {
	_parent->history()->session().data().requestItemRepaint(_parent);
	if (_check) {
		_check->setActive(active);
	}
}

void ItemBase::clickHandlerPressedChanged(
		const ClickHandlerPtr &action,
		bool pressed) {
	_parent->history()->session().data().requestItemRepaint(_parent);
	if (_check) {
		_check->setPressed(pressed);
	}
}

void ItemBase::invalidateCache() {
	if (_check) {
		_check->invalidateCache();
	}
}

void ItemBase::paintCheckbox(
		Painter &p,
		QPoint position,
		bool selected,
		const PaintContext *context) {
	if (selected || context->selecting) {
		ensureCheckboxCreated();
	}
	if (_check) {
		_check->paint(p, position, _width, selected, context->selecting);
	}
}

const style::RoundCheckbox &ItemBase::checkboxStyle() const {
	return st::overviewCheck;
}

void ItemBase::ensureCheckboxCreated() {
	if (_check) {
		return;
	}
	const auto repaint = [=] {
		_parent->history()->session().data().requestItemRepaint(_parent);
	};
	_check = std::make_unique<Checkbox>(repaint, checkboxStyle());
}

void RadialProgressItem::setDocumentLinks(
		not_null<DocumentData*> document,
		bool forceOpen) {
	const auto context = parent()->fullId();
	setLinks(
		std::make_shared<DocumentOpenClickHandler>(
			document,
			crl::guard(this, [=](FullMsgId id) {
				clearSpoiler();
				delegate()->openDocument(document, id, forceOpen);
			}),
			context),
		std::make_shared<DocumentSaveClickHandler>(document, context),
		std::make_shared<DocumentCancelClickHandler>(
			document,
			nullptr,
			context));
}

void RadialProgressItem::clickHandlerActiveChanged(
		const ClickHandlerPtr &action,
		bool active) {
	ItemBase::clickHandlerActiveChanged(action, active);
	if (action == _openl || action == _savel || action == _cancell) {
		if (iconAnimated()) {
			const auto repaint = [=] {
				parent()->history()->session().data().requestItemRepaint(
					parent());
			};
			_a_iconOver.start(
				repaint,
				active ? 0. : 1.,
				active ? 1. : 0.,
				st::msgFileOverDuration);
		}
	}
}

void RadialProgressItem::setLinks(
		ClickHandlerPtr &&openl,
		ClickHandlerPtr &&savel,
		ClickHandlerPtr &&cancell) {
	_openl = std::move(openl);
	_savel = std::move(savel);
	_cancell = std::move(cancell);
}

void RadialProgressItem::radialAnimationCallback(crl::time now) const {
	const auto updated = [&] {
		return _radial->update(dataProgress(), dataFinished(), now);
	}();
	if (!anim::Disabled() || updated) {
		parent()->history()->session().data().requestItemRepaint(parent());
	}
	if (!_radial->animating()) {
		checkRadialFinished();
	}
}

void RadialProgressItem::ensureRadial() {
	if (_radial) {
		return;
	}
	_radial = std::make_unique<Ui::RadialAnimation>([=](crl::time now) {
		radialAnimationCallback(now);
	});
}

void RadialProgressItem::checkRadialFinished() const {
	if (_radial && !_radial->animating() && dataLoaded()) {
		_radial.reset();
	}
}

RadialProgressItem::~RadialProgressItem() = default;

void StatusText::update(
		int64 newSize,
		int64 fullSize,
		TimeId duration,
		TimeId realDuration) {
	setSize(newSize);
	if (_size == Ui::FileStatusSizeReady) {
		_text = (duration >= 0) ? Ui::FormatDurationAndSizeText(duration, fullSize) : (duration < -1 ? Ui::FormatGifAndSizeText(fullSize) : Ui::FormatSizeText(fullSize));
	} else if (_size == Ui::FileStatusSizeLoaded) {
		_text = (duration >= 0) ? Ui::FormatDurationText(duration) : (duration < -1 ? u"GIF"_q : Ui::FormatSizeText(fullSize));
	} else if (_size == Ui::FileStatusSizeFailed) {
		_text = tr::lng_attach_failed(tr::now);
	} else if (_size >= 0) {
		_text = Ui::FormatDownloadText(_size, fullSize);
	} else {
		_text = Ui::FormatPlayedText(-_size - 1, realDuration);
	}
}

void StatusText::setSize(int64 newSize) {
	_size = newSize;
}

Photo::Photo(
	not_null<Delegate*> delegate,
	not_null<HistoryItem*> parent,
	not_null<PhotoData*> photo,
	MediaOptions options)
: ItemBase(delegate, parent)
, _data(photo)
, _spoiler((options.spoiler || parent->isMediaSensitive())
	? std::make_unique<Ui::SpoilerAnimation>([=] {
		delegate->repaintItem(this);
	})
	: nullptr)
, _sensitiveSpoiler(parent->isMediaSensitive() ? 1 : 0)
, _story(options.story)
, _storyPinned(options.storyPinned)
, _storyShowPinned(options.storyShowPinned)
, _storyHidden(options.storyHidden)
, _storyShowHidden(options.storyShowHidden)
, _link(_sensitiveSpoiler
	? HistoryView::MakeSensitiveMediaLink(
		std::make_shared<LambdaClickHandler>(crl::guard(this, [=] {
			maybeClearSensitiveSpoiler();
		})),
		parent)
	: makeOpenPhotoHandler()) {
	if (_data->inlineThumbnailBytes().isEmpty()
		&& (_data->hasExact(Data::PhotoSize::Small)
			|| _data->hasExact(Data::PhotoSize::Thumbnail))) {
		_data->load(Data::PhotoSize::Small, parent->fullId());
	}
}

Photo::~Photo() = default;

ClickHandlerPtr Photo::makeOpenPhotoHandler() {
	return std::make_shared<PhotoOpenClickHandler>(
		_data,
		crl::guard(this, [=](FullMsgId id) {
			clearSpoiler();
			delegate()->openPhoto(_data, id);
		}),
		parent()->fullId());
}

void Photo::initDimensions() {
	_maxw = 2 * st::overviewPhotoMinSize;
	_minh = _story ? qRound(_maxw * kStoryRatio) : _maxw;
}

int32 Photo::resizeGetHeight(int32 width) {
	width = qMin(width, _maxw);
	if (_width != width) {
		_width = width;
		_height = _story ? qRound(_width * kStoryRatio) : _width;
	}
	return _height;
}

void Photo::paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) {
	const auto selected = (selection == FullSelection);
	const auto widthChanged = (_pix.width()
		!= (_width * style::DevicePixelRatio()));
	if (!_goodLoaded || widthChanged) {
		ensureDataMediaCreated();
		const auto good = !_spoiler
			&& (_dataMedia->loaded()
				|| _dataMedia->image(Data::PhotoSize::Thumbnail));
		if ((good && !_goodLoaded) || widthChanged) {
			_goodLoaded = good;
			_pix = QImage();
			if (_goodLoaded) {
				setPixFrom(_dataMedia->image(Data::PhotoSize::Large)
					? _dataMedia->image(Data::PhotoSize::Large)
					: _dataMedia->image(Data::PhotoSize::Thumbnail));
			} else if (const auto small = _spoiler
				? nullptr
				: _dataMedia->image(Data::PhotoSize::Small)) {
				setPixFrom(small);
			} else if (const auto blurred = _dataMedia->thumbnailInline()) {
				setPixFrom(blurred);
			}
		}
	}

	if (_pix.isNull()) {
		p.fillRect(0, 0, _width, _height, st::overviewPhotoBg);
	} else {
		p.drawImage(0, 0, _pix);
	}

	if (_spoiler) {
		const auto paused = context->paused || On(PowerSaving::kChatSpoiler);
		Ui::FillSpoilerRect(
			p,
			QRect(0, 0, _width, _height),
			Ui::DefaultImageSpoiler().frame(
				_spoiler->index(context->ms, paused)));

		if (_sensitiveSpoiler) {
			PaintSensitiveTag(p, QRect(0, 0, _width, _height));
		}
	}

	if (_storyHidden) {
		delegate()->hiddenMark()->paint(
			p,
			_pix,
			_hiddenBgCache,
			QPoint(),
			QSize(_width, _height),
			_width);
	}

	if (selected) {
		p.fillRect(0, 0, _width, _height, st::overviewPhotoSelectOverlay);
	}

	if (_storyPinned) {
		const auto &icon = selected
			? st::storyPinnedIconSelected
			: st::storyPinnedIcon;
		icon.paint(p, _width - icon.width(), 0, _width);
	}

	const auto checkDelta = st::overviewCheckSkip + st::overviewCheck.size;
	const auto checkLeft = _width - checkDelta;
	const auto checkTop = _height - checkDelta;
	paintCheckbox(p, { checkLeft, checkTop }, selected, context);
}

void Photo::setPixFrom(not_null<Image*> image) {
	Expects(_width > 0 && _height > 0);

	auto img = image->original();
	if (!_goodLoaded) {
		img = Images::Blur(std::move(img));
	}
	_pix = CropMediaFrame(std::move(img), _width, _height);

	// In case we have inline thumbnail we can unload all images and we still
	// won't get a blank image in the media viewer when the photo is opened.
	if (!_data->inlineThumbnailBytes().isEmpty()) {
		_dataMedia = nullptr;
		delegate()->unregisterHeavyItem(this);
	}
}

void Photo::ensureDataMediaCreated() const {
	if (_dataMedia) {
		return;
	}
	_dataMedia = _data->createMediaView();
	if (_data->inlineThumbnailBytes().isEmpty()) {
		_dataMedia->wanted(Data::PhotoSize::Small, parent()->fullId());
	}
	_dataMedia->wanted(Data::PhotoSize::Thumbnail, parent()->fullId());
	delegate()->registerHeavyItem(this);
}

void Photo::clearSpoiler() {
	if (_spoiler) {
		_spoiler = nullptr;
		_sensitiveSpoiler = false;
		_pix = QImage();
		delegate()->repaintItem(this);
	}
}

void Photo::maybeClearSensitiveSpoiler() {
	if (_sensitiveSpoiler) {
		clearSpoiler();
		_link = makeOpenPhotoHandler();
	}
}

void Photo::itemDataChanged() {
	const auto pinned = _storyShowPinned && parent()->isPinned();
	const auto hidden = _storyShowHidden && !parent()->storyInProfile();
	if (_storyPinned != pinned || _storyHidden != hidden) {
		_storyPinned = pinned;
		_storyHidden = hidden;
		delegate()->repaintItem(this);
	}
}

void Photo::clearHeavyPart() {
	_dataMedia = nullptr;
}

TextState Photo::getState(
		QPoint point,
		StateRequest request) const {
	if (hasPoint(point)) {
		return { parent(), _link };
	}
	return {};
}

Video::Video(
	not_null<Delegate*> delegate,
	not_null<HistoryItem*> parent,
	not_null<DocumentData*> video,
	MediaOptions options)
: RadialProgressItem(delegate, parent)
, _data(video)
, _videoCover(LookupVideoCover(video, parent))
, _duration(Ui::FormatDurationText(_data->duration() / 1000))
, _spoiler((options.spoiler || parent->isMediaSensitive())
	? std::make_unique<Ui::SpoilerAnimation>([=] {
		delegate->repaintItem(this);
	})
	: nullptr)
, _sensitiveSpoiler(parent->isMediaSensitive() ? 1 : 0)
, _story(options.story)
, _storyPinned(options.storyPinned)
, _storyShowPinned(options.storyShowPinned)
, _storyHidden(options.storyHidden)
, _storyShowHidden(options.storyShowHidden) {
	setDocumentLinks(_data);
	if (_sensitiveSpoiler) {
		_openl = HistoryView::MakeSensitiveMediaLink(
			std::make_shared<LambdaClickHandler>(crl::guard(this, [=] {
				clearSpoiler();
				setDocumentLinks(_data);
			})),
			parent);
	}
	if (!_videoCover) {
		_data->loadThumbnail(parent->fullId());
	} else if (_videoCover->inlineThumbnailBytes().isEmpty()
		&& (_videoCover->hasExact(Data::PhotoSize::Small)
			|| _videoCover->hasExact(Data::PhotoSize::Thumbnail))) {
		_videoCover->load(Data::PhotoSize::Small, parent->fullId());
	}
}

Video::~Video() = default;

void Video::initDimensions() {
	_maxw = 2 * st::overviewPhotoMinSize;
	_minh = _story ? qRound(_maxw * kStoryRatio) : _maxw;
}

int32 Video::resizeGetHeight(int32 width) {
	width = qMin(width, _maxw);
	if (_width != width) {
		_width = width;
		_height = _story ? qRound(_width * kStoryRatio) : _width;
	}
	return _height;
}

void Video::paint(
		Painter &p,
		const QRect &clip,
		TextSelection selection,
		const PaintContext *context) {
	ensureDataMediaCreated();

	const auto selected = (selection == FullSelection);
	const auto blurred = _videoCover
		? _videoCoverMedia->thumbnailInline()
		: _dataMedia->thumbnailInline();
	const auto thumbnail = _spoiler
		? nullptr
		: _videoCover
		? _videoCoverMedia->image(Data::PhotoSize::Small)
		: _dataMedia->thumbnail();
	const auto good = _spoiler
		? nullptr
		: _videoCover
		? _videoCoverMedia->image(Data::PhotoSize::Large)
		: _dataMedia->goodThumbnail();

	bool loaded = dataLoaded(), displayLoading = _data->displayLoading();
	if (displayLoading) {
		ensureRadial();
		if (!_radial->animating()) {
			_radial->start(dataProgress());
		}
	}
	updateStatusText();
	const auto radial = isRadialAnimation();
	const auto radialOpacity = radial ? _radial->opacity() : 0.;

	if ((blurred || thumbnail || good)
		&& ((_pix.width() != _width * style::DevicePixelRatio())
			|| (_pixBlurred && (thumbnail || good)))) {
		auto img = good
			? good->original()
			: thumbnail
			? thumbnail->original()
			: Images::Blur(blurred->original());
		_pix = CropMediaFrame(std::move(img), _width, _height);
		_pixBlurred = !(thumbnail || good);
	}

	if (_pix.isNull()) {
		p.fillRect(0, 0, _width, _height, st::overviewPhotoBg);
	} else {
		p.drawImage(0, 0, _pix);
	}

	if (_spoiler) {
		const auto paused = context->paused || On(PowerSaving::kChatSpoiler);
		Ui::FillSpoilerRect(
			p,
			QRect(0, 0, _width, _height),
			Ui::DefaultImageSpoiler().frame(
				_spoiler->index(context->ms, paused)));

		if (_sensitiveSpoiler) {
			PaintSensitiveTag(p, QRect(0, 0, _width, _height));
		}
	}

	if (_storyHidden) {
		delegate()->hiddenMark()->paint(
			p,
			_pix,
			_hiddenBgCache,
			QPoint(),
			QSize(_width, _height),
			_width);
	}

	if (selected) {
		p.fillRect(QRect(0, 0, _width, _height), st::overviewPhotoSelectOverlay);
	}

	if (_storyPinned) {
		const auto &icon = selected
			? st::storyPinnedIconSelected
			: st::storyPinnedIcon;
		icon.paint(p, _width - icon.width(), 0, _width);
	}

	if (!selected && !context->selecting && radialOpacity < 1.) {
		if (clip.intersects(QRect(0, _height - st::normalFont->height, _width, st::normalFont->height))) {
			const auto download = !loaded && !_dataMedia->canBePlayed(parent());
			const auto &icon = download
				? (selected ? st::overviewVideoDownloadSelected : st::overviewVideoDownload)
				: (selected ? st::overviewVideoPlaySelected : st::overviewVideoPlay);
			const auto text = download ? _status.text() : _duration;
			const auto margin = st::overviewVideoStatusMargin;
			const auto padding = st::overviewVideoStatusPadding;
			const auto statusX = margin + padding.x(), statusY = _height - margin - padding.y() - st::normalFont->height;
			const auto statusW = icon.width() + padding.x() + st::normalFont->width(text) + 2 * padding.x();
			const auto statusH = st::normalFont->height + 2 * padding.y();
			p.setOpacity(1. - radialOpacity);
			Ui::FillRoundRect(p, statusX - padding.x(), statusY - padding.y(), statusW, statusH, selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? Ui::OverviewVideoSelectedCorners : Ui::OverviewVideoCorners);
			p.setFont(st::normalFont);
			p.setPen(st::msgDateImgFg);
			icon.paint(p, statusX, statusY + (st::normalFont->height - icon.height()) / 2, _width);
			p.drawTextLeft(statusX + icon.width() + padding.x(), statusY, _width, text, statusW - 2 * padding.x());
		}
	}

	QRect inner((_width - st::overviewVideoRadialSize) / 2, (_height - st::overviewVideoRadialSize) / 2, st::overviewVideoRadialSize, st::overviewVideoRadialSize);
	if (radial && clip.intersects(inner)) {
		p.setOpacity(radialOpacity);
		p.setPen(Qt::NoPen);
		if (selected) {
			p.setBrush(st::msgDateImgBgSelected);
		} else {
			auto over = ClickHandler::showAsActive((_data->loading() || _data->uploading()) ? _cancell : (loaded || _dataMedia->canBePlayed(parent())) ? _openl : _savel);
			p.setBrush(anim::brush(st::msgDateImgBg, st::msgDateImgBgOver, _a_iconOver.value(over ? 1. : 0.)));
		}

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}

		const auto icon = [&] {
			return &(selected ? st::historyFileThumbCancelSelected : st::historyFileThumbCancel);
		}();
		icon->paintInCenter(p, inner);
		if (radial) {
			p.setOpacity(1);
			QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
			_radial->draw(p, rinner, st::msgFileRadialLine, selected ? st::historyFileThumbRadialFgSelected : st::historyFileThumbRadialFg);
		}
	}
	p.setOpacity(1);

	const auto checkDelta = st::overviewCheckSkip + st::overviewCheck.size;
	const auto checkLeft = _width - checkDelta;
	const auto checkTop = _height - checkDelta;
	paintCheckbox(p, { checkLeft, checkTop }, selected, context);
}

void Video::ensureDataMediaCreated() const {
	if (_dataMedia && (!_videoCover || _videoCoverMedia)) {
		return;
	}
	_dataMedia = _data->createMediaView();
	if (_videoCover) {
		_videoCoverMedia = _videoCover->createMediaView();
		_videoCover->load(Data::PhotoSize::Large, parent()->fullId());
	} else {
		_dataMedia->goodThumbnailWanted();
		_dataMedia->thumbnailWanted(parent()->fullId());
	}
	delegate()->registerHeavyItem(this);
}

void Video::clearSpoiler() {
	if (_spoiler) {
		_spoiler = nullptr;
		_sensitiveSpoiler = false;
		_pix = QImage();
		delegate()->repaintItem(this);
	}
}

void Video::maybeClearSensitiveSpoiler() {
	if (_sensitiveSpoiler) {
		clearSpoiler();
		setDocumentLinks(_data);
	}
}

void Video::itemDataChanged() {
	const auto pinned = _storyShowPinned && parent()->isPinned();
	const auto hidden = _storyShowHidden && !parent()->storyInProfile();
	if (_storyPinned != pinned || _storyHidden != hidden) {
		_storyPinned = pinned;
		_storyHidden = hidden;
		delegate()->repaintItem(this);
	}
}

void Video::clearHeavyPart() {
	_dataMedia = nullptr;
}

float64 Video::dataProgress() const {
	ensureDataMediaCreated();
	return _dataMedia->progress();
}

bool Video::dataFinished() const {
	return !_data->loading();
}

bool Video::dataLoaded() const {
	ensureDataMediaCreated();
	return _dataMedia->loaded();
}

bool Video::iconAnimated() const {
	return true;
}

TextState Video::getState(
		QPoint point,
		StateRequest request) const {
	if (hasPoint(point)) {
		ensureDataMediaCreated();
		const auto link = _sensitiveSpoiler
			? _openl
			: (_data->loading() || _data->uploading())
			? _cancell
			: (dataLoaded() || _dataMedia->canBePlayed(parent()))
			? _openl
			: _savel;
		return { parent(), link };
	}
	return {};
}

void Video::updateStatusText() {
	auto statusSize = int64();
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = Ui::FileStatusSizeFailed;
	} else if (_data->uploading()) {
		statusSize = _data->uploadingData->offset;
	} else if (dataLoaded()) {
		statusSize = Ui::FileStatusSizeLoaded;
	} else {
		statusSize = Ui::FileStatusSizeReady;
	}
	if (statusSize != _status.size()) {
		auto status = statusSize;
		auto size = _data->size;
		if (statusSize >= 0 && statusSize < 0xFF000000LL) {
			size = status;
			status = Ui::FileStatusSizeReady;
		}
		_status.update(status, size, -1, 0);
		_status.setSize(statusSize);
	}
}

Voice::Voice(
	not_null<Delegate*> delegate,
	not_null<HistoryItem*> parent,
	not_null<DocumentData*> voice,
	const style::OverviewFileLayout &st)
: RadialProgressItem(delegate, parent)
, _data(voice)
, _namel(std::make_shared<DocumentOpenClickHandler>(
	_data,
	crl::guard(this, [=](FullMsgId id) {
		delegate->openDocument(_data, id);
	}),
	parent->fullId()))
, _st(st) {
	AddComponents(Info::Bit());

	setDocumentLinks(_data);
	_data->loadThumbnail(parent->fullId());

	updateName();
	const auto dateText = Ui::Text::Link(
		langDateTime(base::unixtime::parse(parent->date()))); // Link 1.
	_details.setMarkedText(
		st::defaultTextStyle,
		tr::lng_date_and_duration(
			tr::now,
			lt_date,
			dateText,
			lt_duration,
			{ .text = Ui::FormatDurationText(_data->duration() / 1000) },
			Ui::Text::WithEntities));
	_details.setLink(1, JumpToMessageClickHandler(parent));
}

void Voice::initDimensions() {
	_maxw = _st.maxWidth;
	_minh = _st.songPadding.top() + _st.songThumbSize + _st.songPadding.bottom() + st::lineWidth;
}

void Voice::paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) {
	ensureDataMediaCreated();
	bool selected = (selection == FullSelection);
	bool loaded = dataLoaded(), displayLoading = _data->displayLoading();

	if (displayLoading) {
		ensureRadial();
		if (!_radial->animating()) {
			_radial->start(dataProgress());
		}
	}
	const auto showPause = updateStatusText();
	const auto nameVersion = parent()->fromOriginal()->nameVersion();
	if (_nameVersion < nameVersion) {
		updateName();
	}
	const auto radial = isRadialAnimation();

	const auto nameleft = _st.songPadding.left()
		+ _st.songThumbSize
		+ _st.songPadding.right();
	const auto nameright = _st.songPadding.left();
	const auto nametop = _st.songNameTop;
	const auto statustop = _st.songStatusTop;
	const auto namewidth = _width - nameleft - nameright;

	const auto inner = style::rtlrect(
		_st.songPadding.left(),
		_st.songPadding.top(),
		_st.songThumbSize,
		_st.songThumbSize,
		_width);
	if (clip.intersects(inner)) {
		if (_data->hasThumbnail()) {
			ensureDataMediaCreated();
		}
		const auto thumbnail = _dataMedia
			? _dataMedia->thumbnail()
			: nullptr;
		const auto blurred = _dataMedia
			? _dataMedia->thumbnailInline()
			: nullptr;

		p.setPen(Qt::NoPen);
		if (thumbnail || blurred) {
			const auto options = Images::Option::RoundCircle
				| (blurred ? Images::Option::Blur : Images::Option());
			const auto thumb = (thumbnail ? thumbnail : blurred)->pix(
				inner.size(),
				{ .options = options });
			p.drawPixmap(inner.topLeft(), thumb);
		} else if (_data->hasThumbnail()) {
			PainterHighQualityEnabler hq(p);
			p.setBrush(st::imageBg);
			p.drawEllipse(inner);
		}
		const auto &checkLink = (_data->loading() || _data->uploading())
			? _cancell
			: (_dataMedia->canBePlayed(parent()) || loaded)
			? _openl
			: _savel;
		if (selected) {
			p.setBrush((thumbnail || blurred) ? st::msgDateImgBgSelected : st::msgFileInBgSelected);
		} else if (_data->hasThumbnail()) {
			auto over = ClickHandler::showAsActive(checkLink);
			p.setBrush(anim::brush(st::msgDateImgBg, st::msgDateImgBgOver, _a_iconOver.value(over ? 1. : 0.)));
		} else {
			auto over = ClickHandler::showAsActive(checkLink);
			p.setBrush(anim::brush(st::msgFileInBg, st::msgFileInBgOver, _a_iconOver.value(over ? 1. : 0.)));
		}
		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}

		if (radial) {
			QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
			auto &bg = selected ? st::historyFileInRadialFgSelected : st::historyFileInRadialFg;
			_radial->draw(p, rinner, st::msgFileRadialLine, bg);
		}

		const auto icon = [&] {
			if (_data->loading() || _data->uploading()) {
				return &(selected ? _st.voiceCancelSelected : _st.voiceCancel);
			} else if (showPause) {
				return &(selected ? _st.voicePauseSelected : _st.voicePause);
			} else if (_dataMedia->canBePlayed(parent())) {
				return &(selected ? _st.voicePlaySelected : _st.voicePlay);
			}
			return &(selected
				? _st.voiceDownloadSelected
				: _st.voiceDownload);
		}();
		icon->paintInCenter(p, inner);
	}

	if (clip.intersects(style::rtlrect(nameleft, nametop, namewidth, st::semiboldFont->height, _width))) {
		p.setPen(st::historyFileNameInFg);
		_name.drawLeftElided(p, nameleft, nametop, namewidth, _width);
	}

	if (clip.intersects(style::rtlrect(nameleft, statustop, namewidth, st::normalFont->height, _width))) {
		p.setFont(st::normalFont);
		p.setPen(selected ? st::mediaInFgSelected : st::mediaInFg);
		int32 unreadx = nameleft;
		if (_status.size() == Ui::FileStatusSizeLoaded || _status.size() == Ui::FileStatusSizeReady) {
			p.setTextPalette(selected ? st::mediaInPaletteSelected : st::mediaInPalette);
			_details.drawLeftElided(p, nameleft, statustop, namewidth, _width);
			p.restoreTextPalette();
			unreadx += _details.maxWidth();
		} else {
			int32 statusw = st::normalFont->width(_status.text());
			p.drawTextLeft(nameleft, statustop, _width, _status.text(), statusw);
			unreadx += statusw;
		}
		auto captionLeft = unreadx + st::mediaUnreadSkip;
		if (parent()->hasUnreadMediaFlag() && unreadx + st::mediaUnreadSkip + st::mediaUnreadSize <= _width) {
			p.setPen(Qt::NoPen);
			p.setBrush(selected ? st::msgFileInBgSelected : st::msgFileInBg);

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(style::rtlrect(unreadx + st::mediaUnreadSkip, statustop + st::mediaUnreadTop, st::mediaUnreadSize, st::mediaUnreadSize, _width));
			}
			captionLeft += st::mediaUnreadSkip + st::mediaUnreadSize;
		}
		if (!_caption.isEmpty()) {
			p.setPen(st::historyFileNameInFg);
			const auto w = _width - captionLeft - st::defaultScrollArea.width;
			_caption.draw(p, Ui::Text::PaintContext{
				.position = QPoint(captionLeft, statustop),
				.availableWidth = w,
				.spoiler = Ui::Text::DefaultSpoilerCache(),
				.paused = context
					? context->paused
					: On(PowerSaving::kEmojiChat),
				.pausedEmoji = On(PowerSaving::kEmojiChat),
				.pausedSpoiler = On(PowerSaving::kChatSpoiler),
				.elisionLines = 1,
			});
		}
	}

	const auto checkDelta = _st.songThumbSize
		+ st::overviewCheckSkip
		- st::overviewSmallCheck.size;
	const auto checkLeft = _st.songPadding.left() + checkDelta;
	const auto checkTop = _st.songPadding.top() + checkDelta;
	paintCheckbox(p, { checkLeft, checkTop }, selected, context);
}

TextState Voice::getState(
		QPoint point,
		StateRequest request) const {
	ensureDataMediaCreated();
	const auto loaded = dataLoaded();

	const auto nameleft = _st.songPadding.left()
		+ _st.songThumbSize
		+ _st.songPadding.right();
	const auto nameright = _st.songPadding.left();
	const auto nametop = _st.songNameTop;
	const auto statustop = _st.songStatusTop;

	const auto inner = style::rtlrect(
		_st.songPadding.left(),
		_st.songPadding.top(),
		_st.songThumbSize,
		_st.songThumbSize,
		_width);
	if (inner.contains(point)) {
		const auto link = (_data->loading() || _data->uploading())
			? _cancell
			: (_dataMedia->canBePlayed(parent()) || loaded)
			? _openl
			: _savel;
		return { parent(), link };
	}
	auto result = TextState(parent());
	const auto statusmaxwidth = _width - nameleft - nameright;
	const auto statusrect = style::rtlrect(
		nameleft,
		statustop,
		statusmaxwidth,
		st::normalFont->height,
		_width);
	if (statusrect.contains(point)) {
		if (_status.size() == Ui::FileStatusSizeLoaded || _status.size() == Ui::FileStatusSizeReady) {
			auto textState = _details.getStateLeft(point - QPoint(nameleft, statustop), _width, _width);
			result.link = textState.link;
			result.cursor = textState.uponSymbol
				? HistoryView::CursorState::Text
				: HistoryView::CursorState::None;
		}
	}
	const auto namewidth = std::min(
		_width - nameleft - nameright,
		_name.maxWidth());
	const auto namerect = style::rtlrect(
		nameleft,
		nametop,
		namewidth,
		st::normalFont->height,
		_width);
	if (namerect.contains(point) && !result.link && !_data->loading()) {
		return { parent(), _namel };
	}
	return result;
}

void Voice::ensureDataMediaCreated() const {
	if (_dataMedia) {
		return;
	}
	_dataMedia = _data->createMediaView();
	delegate()->registerHeavyItem(this);
}

void Voice::clearHeavyPart() {
	_dataMedia = nullptr;
}

float64 Voice::dataProgress() const {
	ensureDataMediaCreated();
	return _dataMedia->progress();
}

bool Voice::dataFinished() const {
	return !_data->loading();
}

bool Voice::dataLoaded() const {
	ensureDataMediaCreated();
	return _dataMedia->loaded();
}

bool Voice::iconAnimated() const {
	return true;
}

const style::RoundCheckbox &Voice::checkboxStyle() const {
	return st::overviewSmallCheck;
}

void Voice::updateName() {
	if (parent()->Has<HistoryMessageForwarded>()) {
		const auto info = parent()->originalHiddenSenderInfo();
		const auto name = info
			? tr::lng_forwarded(tr::now, lt_user, info->nameText().toString())
			: parent()->fromOriginal()->isChannel()
			? tr::lng_forwarded_channel(
				tr::now,
				lt_channel,
				parent()->fromOriginal()->name())
			: tr::lng_forwarded(
				tr::now,
				lt_user,
				parent()->fromOriginal()->name());
		_name.setText(st::semiboldTextStyle, name, Ui::NameTextOptions());
	} else {
		_name.setText(
			st::semiboldTextStyle,
			parent()->from()->name(),
			Ui::NameTextOptions());
	}
	_nameVersion = parent()->fromOriginal()->nameVersion();
	_caption.setMarkedText(
		st::defaultTextStyle,
		parent()->originalText(),
		Ui::DialogTextOptions(),
		Core::TextContext({
			.session = &parent()->history()->session(),
			.repaint = [=] { delegate()->repaintItem(this); },
		}));
}

bool Voice::updateStatusText() {
	auto showPause = false;
	auto statusSize = int64();
	auto realDuration = TimeId();
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = Ui::FileStatusSizeFailed;
	} else if (dataLoaded()) {
		statusSize = Ui::FileStatusSizeLoaded;
	} else {
		statusSize = Ui::FileStatusSizeReady;
	}

	const auto state = Media::Player::instance()->getState(AudioMsgId::Type::Voice);
	if (state.id == AudioMsgId(_data, parent()->fullId(), state.id.externalPlayId())
		&& !Media::Player::IsStoppedOrStopping(state.state)) {
		statusSize = -1 - (state.position / state.frequency);
		realDuration = (state.length / state.frequency);
		showPause = Media::Player::ShowPauseIcon(state.state);
	}

	if (statusSize != _status.size()) {
		_status.update(statusSize, _data->size, _data->duration() / 1000, realDuration);
	}
	return showPause;
}

Document::Document(
	not_null<Delegate*> delegate,
	not_null<HistoryItem*> parent,
	DocumentFields fields,
	const style::OverviewFileLayout &st)
: RadialProgressItem(delegate, parent)
, _data(fields.document)
, _msgl(parent->isHistoryEntry()
	? JumpToMessageClickHandler(parent)
	: nullptr)
, _namel(std::make_shared<DocumentOpenClickHandler>(
	_data,
	crl::guard(this, [=](FullMsgId id) {
		delegate->openDocument(_data, id);
	}),
	parent->fullId()))
, _st(st)
, _generic(::Layout::DocumentGenericPreview::Create(_data))
, _forceFileLayout(fields.forceFileLayout)
, _date(langDateTime(base::unixtime::parse(fields.dateOverride
	? fields.dateOverride
	: parent->date())))
, _ext(_generic.ext)
, _datew(st::normalFont->width(_date)) {
	_name.setMarkedText(
		st::defaultTextStyle,
		(!_forceFileLayout
			? Ui::Text::FormatSongNameFor(_data).textWithEntities()
			: Ui::Text::FormatDownloadsName(_data)),
		_documentNameOptions);

	AddComponents(Info::Bit());

	setDocumentLinks(_data);

	_status.update(
		Ui::FileStatusSizeReady,
		_data->size,
		songLayout() ? (_data->duration() / 1000) : -1,
		0);

	if (withThumb()) {
		_data->loadThumbnail(parent->fullId());
		auto tw = style::ConvertScale(_data->thumbnailLocation().width());
		auto th = style::ConvertScale(_data->thumbnailLocation().height());
		if (tw > th) {
			_thumbw = (tw * _st.fileThumbSize) / th;
		} else {
			_thumbw = _st.fileThumbSize;
		}
	} else {
		_thumbw = 0;
	}

	_extw = st::overviewFileExtFont->width(_ext);
	if (_extw > _st.fileThumbSize - st::overviewFileExtPadding * 2) {
		_ext = st::overviewFileExtFont->elided(_ext, _st.fileThumbSize - st::overviewFileExtPadding * 2, Qt::ElideMiddle);
		_extw = st::overviewFileExtFont->width(_ext);
	}
}

bool Document::downloadInCorner() const {
	return _data->isAudioFile()
		&& parent()->allowsForward()
		&& _data->canBeStreamed(parent())
		&& !_data->inappPlaybackFailed();
}

void Document::initDimensions() {
	_maxw = _st.maxWidth;
	if (songLayout()) {
		_minh = _st.songPadding.top() + _st.songThumbSize + _st.songPadding.bottom();
	} else {
		_minh = _st.filePadding.top() + _st.fileThumbSize + _st.filePadding.bottom() + st::lineWidth;
	}
}

void Document::paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) {
	ensureDataMediaCreated();

	const auto selected = (selection == FullSelection);

	const auto cornerDownload = downloadInCorner();

	_dataMedia->automaticLoad(parent()->fullId(), parent());
	const auto loaded = dataLoaded();
	const auto displayLoading = _data->displayLoading();

	if (displayLoading) {
		ensureRadial();
		if (!_radial->animating()) {
			_radial->start(dataProgress());
		}
	}
	const auto showPause = updateStatusText();
	const auto radial = isRadialAnimation();

	int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0, datetop = -1;
	const auto wthumb = withThumb();

	const auto isSong = songLayout();
	if (isSong) {
		nameleft = _st.songPadding.left() + _st.songThumbSize + _st.songPadding.right();
		nameright = _st.songPadding.left();
		nametop = _st.songNameTop;
		statustop = _st.songStatusTop;

		auto inner = style::rtlrect(_st.songPadding.left(), _st.songPadding.top(), _st.songThumbSize, _st.songThumbSize, _width);
		if (clip.intersects(inner)) {
			const auto isLoading = (!cornerDownload
				&& (_data->loading() || _data->uploading()));
			p.setPen(Qt::NoPen);

			using namespace HistoryView;
			const auto coverDrawn = _data->isSongWithCover()
				&& DrawThumbnailAsSongCover(
					p,
					st::songCoverOverlayFg,
					_dataMedia,
					inner,
					selected);
			if (!coverDrawn) {
				if (selected) {
					p.setBrush(st::msgFileInBgSelected);
				} else {
					const auto over = ClickHandler::showAsActive(isLoading
						? _cancell
						: (loaded || _dataMedia->canBePlayed(parent()))
						? _openl
						: _savel);
					p.setBrush(anim::brush(
						_st.songIconBg,
						_st.songOverBg,
						_a_iconOver.value(over ? 1. : 0.)));
				}
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(inner);
			}

			const auto icon = [&] {
				if (!coverDrawn) {
					if (isLoading) {
						return &(selected
							? _st.voiceCancelSelected
							: _st.voiceCancel);
					} else if (showPause) {
						return &(selected
							? _st.voicePauseSelected
							: _st.voicePause);
					} else if (loaded || _dataMedia->canBePlayed(parent())) {
						return &(selected
							? _st.voicePlaySelected
							: _st.voicePlay);
					}
					return &(selected
						? _st.voiceDownloadSelected
						: _st.voiceDownload);
				}
				if (isLoading) {
					return &(selected ? _st.songCancelSelected : _st.songCancel);
				} else if (showPause) {
					return &(selected ? _st.songPauseSelected : _st.songPause);
				} else if (loaded || _dataMedia->canBePlayed(parent())) {
					return &(selected ? _st.songPlaySelected : _st.songPlay);
				}
				return &(selected ? _st.songDownloadSelected : _st.songDownload);
			}();
			icon->paintInCenter(p, inner);

			if (radial && !cornerDownload) {
				auto rinner = inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine));
				auto &bg = selected ? st::historyFileInRadialFgSelected : st::historyFileInRadialFg;
				_radial->draw(p, rinner, st::msgFileRadialLine, bg);
			}

			drawCornerDownload(p, selected, context);
		}
	} else {
		nameleft = _st.fileThumbSize + _st.filePadding.right();
		nametop = st::linksBorder + _st.fileNameTop;
		statustop = st::linksBorder + _st.fileStatusTop;
		datetop = st::linksBorder + _st.fileDateTop;

		QRect border(style::rtlrect(nameleft, 0, _width - nameleft, st::linksBorder, _width));
		if (!context->skipBorder && clip.intersects(border)) {
			p.fillRect(clip.intersected(border), st::linksBorderFg);
		}

		QRect rthumb(style::rtlrect(0, st::linksBorder + _st.filePadding.top(), _st.fileThumbSize, _st.fileThumbSize, _width));
		if (clip.intersects(rthumb)) {
			if (wthumb) {
				ensureDataMediaCreated();
				const auto thumbnail = _dataMedia->thumbnail();
				const auto blurred = _dataMedia->thumbnailInline();
				if (thumbnail || blurred) {
					if (_thumb.isNull() || (thumbnail && !_thumbLoaded)) {
						_thumbLoaded = (thumbnail != nullptr);
						const auto options = Images::Option::RoundSmall
							| (_thumbLoaded
								? Images::Option()
								: Images::Option::Blur);
						const auto image = thumbnail ? thumbnail : blurred;
						_thumb = image->pixNoCache(
							_thumbw * style::DevicePixelRatio(),
							{
								.options = options,
								.outer = QSize(
									_st.fileThumbSize,
									_st.fileThumbSize),
							});
					}
					p.drawPixmap(rthumb.topLeft(), _thumb);
				} else {
					p.setPen(Qt::NoPen);
					p.setBrush(st::overviewFileThumbBg);
					p.drawRoundedRect(
						rthumb,
						st::roundRadiusSmall,
						st::roundRadiusSmall);
				}
			} else {
				p.setPen(Qt::NoPen);
				p.setBrush(_generic.color);
				p.drawRoundedRect(
					rthumb,
					st::roundRadiusSmall,
					st::roundRadiusSmall);
				if (!radial && loaded && !_ext.isEmpty()) {
					p.setFont(st::overviewFileExtFont);
					p.setPen(st::overviewFileExtFg);
					p.drawText(rthumb.left() + (rthumb.width() - _extw) / 2, rthumb.top() + st::overviewFileExtTop + st::overviewFileExtFont->ascent, _ext);
				}
			}
			if (selected) {
				p.setPen(Qt::NoPen);
				p.setBrush(st::defaultTextPalette.selectOverlay);
				p.drawRoundedRect(
					rthumb,
					st::roundRadiusSmall,
					st::roundRadiusSmall);
			}

			if (radial || (!loaded && !_data->loading())) {
				QRect inner(rthumb.x() + (rthumb.width() - _st.songThumbSize) / 2, rthumb.y() + (rthumb.height() - _st.songThumbSize) / 2, _st.songThumbSize, _st.songThumbSize);
				if (clip.intersects(inner)) {
					auto radialOpacity = (radial && loaded && !_data->uploading()) ? _radial->opacity() : 1;
					p.setPen(Qt::NoPen);
					if (selected) {
						p.setBrush(wthumb
							? st::msgDateImgBgSelected
							: _generic.selected);
					} else {
						auto over = ClickHandler::showAsActive(_data->loading() ? _cancell : _savel);
						p.setBrush(anim::brush(
							wthumb ? st::msgDateImgBg : _generic.dark,
							wthumb ? st::msgDateImgBgOver : _generic.over,
							_a_iconOver.value(over ? 1. : 0.)));
					}
					p.setOpacity(radialOpacity * p.opacity());

					{
						PainterHighQualityEnabler hq(p);
						p.drawEllipse(inner);
					}

					p.setOpacity(radialOpacity);
					auto icon = ([loaded, this, selected] {
						if (loaded || _data->loading()) {
							return &(selected ? st::historyFileThumbCancelSelected : st::historyFileThumbCancel);
						}
						return &(selected ? st::historyFileThumbDownloadSelected : st::historyFileThumbDownload);
					})();
					icon->paintInCenter(p, inner);
					if (radial) {
						p.setOpacity(1);

						QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
						_radial->draw(p, rinner, st::msgFileRadialLine, selected ? st::historyFileThumbRadialFgSelected : st::historyFileThumbRadialFg);
					}
				}
			}
		}
	}

	const auto availwidth = _width - nameleft - nameright;
	const auto namewidth = std::min(availwidth, _name.maxWidth());
	if (clip.intersects(style::rtlrect(nameleft, nametop, namewidth, st::semiboldFont->height, _width))) {
		p.setPen(st::historyFileNameInFg);
		_name.drawLeftElided(p, nameleft, nametop, namewidth, _width);
	}

	if (clip.intersects(style::rtlrect(nameleft, statustop, availwidth, st::normalFont->height, _width))) {
		p.setFont(st::normalFont);
		p.setPen((isSong && selected) ? st::mediaInFgSelected : st::mediaInFg);
		p.drawTextLeft(nameleft, statustop, _width, _status.text());
	}
	if (datetop >= 0 && clip.intersects(style::rtlrect(nameleft, datetop, _datew, st::normalFont->height, _width))) {
		p.setFont((_msgl && ClickHandler::showAsActive(_msgl))
			? st::normalFont->underline()
			: st::normalFont);
		p.setPen(st::mediaInFg);
		p.drawTextLeft(nameleft, datetop, _width, _date, _datew);
	}

	const auto checkDelta = (isSong ? _st.songThumbSize : _st.fileThumbSize)
		+ (isSong ? st::overviewCheckSkip : -st::overviewCheckSkip)
		- st::overviewSmallCheck.size;
	const auto checkLeft = (isSong
		? _st.songPadding.left()
		: 0) + checkDelta;
	const auto checkTop = (isSong
		? _st.songPadding.top()
		: (st::linksBorder + _st.filePadding.top())) + checkDelta;
	paintCheckbox(p, { checkLeft, checkTop }, selected, context);
}

void Document::drawCornerDownload(QPainter &p, bool selected, const PaintContext *context) const {
	if (dataLoaded()
		|| _data->loadedInMediaCache()
		|| !downloadInCorner()) {
		return;
	}
	const auto size = st::overviewSmallCheck.size;
	const auto shift = _st.songThumbSize + st::overviewCheckSkip - size;
	const auto inner = style::rtlrect(_st.songPadding.left() + shift, _st.songPadding.top() + shift, size, size, _width);
	auto pen = st::windowBg->p;
	pen.setWidth(st::lineWidth);
	p.setPen(pen);
	if (selected) {
		p.setBrush(st::msgFileInBgSelected);
	} else {
		p.setBrush(_st.songIconBg);
	}
	{
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(inner);
	}
	const auto icon = [&] {
		if (_data->loading()) {
			return &(selected ? st::overviewSmallCancelSelected : st::overviewSmallCancel);
		}
		return &(selected ? st::overviewSmallDownloadSelected : st::overviewSmallDownload);
	}();
	icon->paintInCenter(p, inner);
	if (_radial && _radial->animating()) {
		const auto rinner = inner.marginsRemoved(QMargins(st::historyAudioRadialLine, st::historyAudioRadialLine, st::historyAudioRadialLine, st::historyAudioRadialLine));
		const auto &fg = selected
			? st::historyFileInIconFgSelected
			: st::historyFileInIconFg;
		_radial->draw(p, rinner, st::historyAudioRadialLine, fg);
	}
}

TextState Document::cornerDownloadTextState(
		QPoint point,
		StateRequest request) const {
	auto result = TextState(parent());
	if (!downloadInCorner()
		|| dataLoaded()
		|| _data->loadedInMediaCache()) {
		return result;
	}
	const auto size = st::overviewSmallCheck.size;
	const auto shift = _st.songThumbSize + st::overviewCheckSkip - size;
	const auto inner = style::rtlrect(_st.songPadding.left() + shift, _st.songPadding.top() + shift, size, size, _width);
	if (inner.contains(point)) {
		result.link = _data->loading() ? _cancell : _savel;
	}
	return result;

}

TextState Document::getState(
		QPoint point,
		StateRequest request) const {
	ensureDataMediaCreated();
	const auto loaded = dataLoaded();

	if (songLayout()) {
		const auto nameleft = _st.songPadding.left() + _st.songThumbSize + _st.songPadding.right();
		const auto nameright = _st.songPadding.left();
		const auto namewidth = std::min(
			_width - nameleft - nameright,
			_name.maxWidth());
		const auto nametop = _st.songNameTop;

		if (const auto state = cornerDownloadTextState(point, request); state.link) {
			return state;
		}

		const auto inner = style::rtlrect(
			_st.songPadding.left(),
			_st.songPadding.top(),
			_st.songThumbSize,
			_st.songThumbSize,
			_width);
		if (inner.contains(point)) {
			const auto link = (!downloadInCorner()
				&& (_data->loading() || _data->uploading()))
				? _cancell
				: (loaded || _dataMedia->canBePlayed(parent()))
				? _openl
				: _savel;
			return { parent(), link };
		}
		const auto namerect = style::rtlrect(
			nameleft,
			nametop,
			namewidth,
			st::semiboldFont->height,
			_width);
		if (namerect.contains(point) && !_data->loading()) {
			return { parent(), _namel };
		}
	} else {
		const auto nameleft = _st.fileThumbSize + _st.filePadding.right();
		const auto nameright = 0;
		const auto nametop = st::linksBorder + _st.fileNameTop;
		const auto namewidth = std::min(
			_width - nameleft - nameright,
			_name.maxWidth());
		const auto datetop = st::linksBorder + _st.fileDateTop;

		const auto rthumb = style::rtlrect(
			0,
			st::linksBorder + _st.filePadding.top(),
			_st.fileThumbSize,
			_st.fileThumbSize,
			_width);

		if (rthumb.contains(point)) {
			const auto link = (_data->loading() || _data->uploading())
				? _cancell
				: loaded
				? _openl
				: _savel;
			return { parent(), link };
		}

		if (_data->status != FileUploadFailed) {
			auto daterect = style::rtlrect(
				nameleft,
				datetop,
				_datew,
				st::normalFont->height,
				_width);
			if (daterect.contains(point)) {
				return { parent(), _msgl };
			}
		}
		if (!_data->loading() && !_data->isNull()) {
			auto leftofnamerect = style::rtlrect(
				0,
				st::linksBorder,
				nameleft,
				_height - st::linksBorder,
				_width);
			if (loaded && leftofnamerect.contains(point)) {
				return { parent(), _namel };
			}
			const auto namerect = style::rtlrect(
				nameleft,
				nametop,
				namewidth,
				st::semiboldFont->height,
				_width);
			if (namerect.contains(point)) {
				return { parent(), _namel };
			}
		}
	}
	return {};
}

const style::RoundCheckbox &Document::checkboxStyle() const {
	return st::overviewSmallCheck;
}

bool Document::songLayout() const {
	return !_forceFileLayout && _data->isSong();
}

void Document::ensureDataMediaCreated() const {
	if (_dataMedia) {
		return;
	}
	_dataMedia = _data->createMediaView();
	_dataMedia->thumbnailWanted(parent()->fullId());
	delegate()->registerHeavyItem(this);
}

void Document::clearHeavyPart() {
	_dataMedia = nullptr;
}

float64 Document::dataProgress() const {
	ensureDataMediaCreated();
	return _dataMedia->progress();
}

bool Document::dataFinished() const {
	return !_data->loading();
}

bool Document::dataLoaded() const {
	ensureDataMediaCreated();
	return _dataMedia->loaded();
}

bool Document::iconAnimated() const {
	return songLayout()
		|| !dataLoaded()
		|| (_radial && _radial->animating());
}

bool Document::withThumb() const {
	return !songLayout() && _data->hasThumbnail();
}

bool Document::updateStatusText() {
	auto showPause = false;
	auto statusSize = int64();
	auto realDuration = TimeId();
	if (_data->status == FileDownloadFailed
		|| _data->status == FileUploadFailed) {
		statusSize = Ui::FileStatusSizeFailed;
	} else if (_data->uploading()) {
		statusSize = _data->uploadingData->offset;
	} else if (_data->loading()) {
		statusSize = _data->loadOffset();
	} else if (dataLoaded()) {
		statusSize = Ui::FileStatusSizeLoaded;
	} else {
		statusSize = Ui::FileStatusSizeReady;
	}

	const auto isSong = songLayout();
	if (isSong) {
		const auto state = Media::Player::instance()->getState(AudioMsgId::Type::Song);
		if (state.id == AudioMsgId(_data, parent()->fullId(), state.id.externalPlayId()) && !Media::Player::IsStoppedOrStopping(state.state)) {
			statusSize = -1 - (state.position / state.frequency);
			realDuration = (state.length / state.frequency);
			showPause = Media::Player::ShowPauseIcon(state.state);
		}
		if (!showPause && (state.id == AudioMsgId(_data, parent()->fullId(), state.id.externalPlayId())) && Media::Player::instance()->isSeeking(AudioMsgId::Type::Song)) {
			showPause = true;
		}
	}

	if (statusSize != _status.size()) {
		_status.update(
			statusSize,
			_data->size,
			isSong ? (_data->duration() / 1000) : -1,
			realDuration);
	}
	return showPause;
}

Link::Link(
	not_null<Delegate*> delegate,
	not_null<HistoryItem*> parent,
	Data::Media *media)
: ItemBase(delegate, parent)
, _text(st::msgMinWidth) {
	AddComponents(Info::Bit());

	auto textWithEntities = parent->originalText();
	QString mainUrl;

	auto text = textWithEntities.text;
	const auto &entities = textWithEntities.entities;
	int32 from = 0, till = text.size(), lnk = entities.size();
	for (const auto &entity : entities) {
		auto type = entity.type();
		if (type != EntityType::Url && type != EntityType::CustomUrl && type != EntityType::Email) {
			continue;
		}
		const auto customUrl = entity.data();
		const auto entityText = text.mid(entity.offset(), entity.length());
		const auto url = customUrl.isEmpty() ? entityText : customUrl;
		if (_links.isEmpty()) {
			mainUrl = url;
		}
		_links.push_back(LinkEntry(url, entityText));
	}
	if (_links.empty()) {
		if (const auto media = parent->media()) {
			if (const auto webpage = media->webpage()) {
				if (!webpage->displayUrl.isEmpty()
					&& !webpage->url.isEmpty()) {
					_links.push_back(
						LinkEntry(webpage->displayUrl, webpage->url));
				}
			}
		}
	}
	while (lnk > 0 && till > from) {
		--lnk;
		auto &entity = entities.at(lnk);
		auto type = entity.type();
		if (type != EntityType::Url && type != EntityType::CustomUrl && type != EntityType::Email) {
			++lnk;
			break;
		}
		int32 afterLinkStart = entity.offset() + entity.length();
		if (till > afterLinkStart) {
			if (!QRegularExpression(u"^[,.\\s_=+\\-;:`'\"\\(\\)\\[\\]\\{\\}<>*&^%\\$#@!\\\\/]+$"_q).match(text.mid(afterLinkStart, till - afterLinkStart)).hasMatch()) {
				++lnk;
				break;
			}
		}
		till = entity.offset();
	}
	if (!lnk) {
		if (QRegularExpression(u"^[,.\\s\\-;:`'\"\\(\\)\\[\\]\\{\\}<>*&^%\\$#@!\\\\/]+$"_q).match(text.mid(from, till - from)).hasMatch()) {
			till = from;
		}
	}

	const auto createHandler = [](const QString &url) {
		return UrlClickHandler::IsSuspicious(url)
			? std::make_shared<HiddenUrlClickHandler>(url)
			: std::make_shared<UrlClickHandler>(url, false);
	};
	_page = media ? media->webpage() : nullptr;
	if (_page) {
		mainUrl = _page->url;
		if (_page->document) {
			_photol = std::make_shared<DocumentOpenClickHandler>(
				_page->document,
				crl::guard(this, [=](FullMsgId id) {
					delegate->openDocument(_page->document, id);
				}),
				parent->fullId());
		} else if (_page->photo) {
			if (_page->type == WebPageType::Profile
				|| _page->type == WebPageType::Video) {
				_photol = createHandler(_page->url);
			} else if (_page->type == WebPageType::Photo
				|| _page->type == WebPageType::Document
				|| _page->siteName == u"Twitter"_q
				|| _page->siteName == u"Facebook"_q) {
				_photol = std::make_shared<PhotoOpenClickHandler>(
					_page->photo,
					crl::guard(this, [=](FullMsgId id) {
						delegate->openPhoto(_page->photo, id);
					}),
					parent->fullId());
			} else {
				_photol = createHandler(_page->url);
			}
		} else {
			_photol = createHandler(_page->url);
		}
	} else if (!mainUrl.isEmpty()) {
		_photol = createHandler(mainUrl);
	}
	if (from >= till && _page) {
		text = _page->description.text;
		from = 0;
		till = text.size();
	}
	if (till > from) {
		TextParseOptions opts = { TextParseMultiline, int32(st::linksMaxWidth), 3 * st::normalFont->height, Qt::LayoutDirectionAuto };
		_text.setText(st::defaultTextStyle, text.mid(from, till - from), opts);
	}
	int32 tw = 0, th = 0;
	if (_page && _page->photo) {
		const auto photo = _page->photo;
		if (photo->hasExact(Data::PhotoSize::Small)
			|| photo->hasExact(Data::PhotoSize::Thumbnail)) {
			photo->load(Data::PhotoSize::Small, parent->fullId());
		}
		tw = style::ConvertScale(photo->width());
		th = style::ConvertScale(photo->height());
	} else if (_page && _page->document && _page->document->hasThumbnail()) {
		_page->document->loadThumbnail(parent->fullId());
		const auto &location = _page->document->thumbnailLocation();
		tw = style::ConvertScale(location.width());
		th = style::ConvertScale(location.height());
	}
	if (tw > st::linksPhotoSize) {
		if (th > tw) {
			th = th * st::linksPhotoSize / tw;
			tw = st::linksPhotoSize;
		} else if (th > st::linksPhotoSize) {
			tw = tw * st::linksPhotoSize / th;
			th = st::linksPhotoSize;
		}
	}
	_pixw = qMax(tw, 1);
	_pixh = qMax(th, 1);

	if (_page) {
		_title = _page->title;
	}

	auto parts = QStringView(mainUrl).split('/');
	if (!parts.isEmpty()) {
		auto domain = parts.at(0);
		if (parts.size() > 2 && domain.endsWith(':') && parts.at(1).isEmpty()) { // http:// and others
			domain = parts.at(2);
		}

		parts = domain.split('@').constLast().split('.', Qt::SkipEmptyParts);
		if (parts.size() > 1) {
			_letter = parts.at(parts.size() - 2).at(0).toUpper();
			if (_title.isEmpty()) {
				_title.reserve(parts.at(parts.size() - 2).size());
				_title.append(_letter).append(parts.at(parts.size() - 2).mid(1));
			}
		}
	}
	_titlew = st::semiboldFont->width(_title);
}

void Link::initDimensions() {
	_maxw = st::linksMaxWidth;
	_minh = 0;
	if (!_title.isEmpty()) {
		_minh += st::semiboldFont->height;
	}
	if (!_text.isEmpty()) {
		_minh += qMin(3 * st::normalFont->height, _text.countHeight(_maxw - st::linksPhotoSize - st::linksPhotoPadding));
	}
	_minh += _links.size() * st::normalFont->height;
	_minh = qMax(_minh, int32(st::linksPhotoSize)) + st::linksMargin.top() + st::linksMargin.bottom() + st::linksBorder;
}

int32 Link::resizeGetHeight(int32 width) {
	_width = qMin(width, _maxw);
	int32 w = _width - st::linksPhotoSize - st::linksPhotoPadding;
	for (const auto &link : std::as_const(_links)) {
		link.lnk->setFullDisplayed(w >= link.width);
	}

	_height = 0;
	if (!_title.isEmpty()) {
		_height += st::semiboldFont->height;
	}
	if (!_text.isEmpty()) {
		_height += qMin(3 * st::normalFont->height, _text.countHeight(_width - st::linksPhotoSize - st::linksPhotoPadding));
	}
	_height += _links.size() * st::normalFont->height;
	_height = qMax(_height, int32(st::linksPhotoSize)) + st::linksMargin.top() + st::linksMargin.bottom() + st::linksBorder;
	return _height;
}

void Link::paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) {
	auto selected = (selection == FullSelection);

	const auto pixLeft = 0;
	const auto pixTop = st::linksMargin.top() + st::linksBorder;
	if (clip.intersects(style::rtlrect(0, pixTop, st::linksPhotoSize, st::linksPhotoSize, _width))) {
		validateThumbnail();
		if (!_thumbnail.isNull()) {
			p.drawPixmap(pixLeft, pixTop, _thumbnail);
		}
	}

	const auto left = st::linksPhotoSize + st::linksPhotoPadding;
	const auto w = _width - left;
	auto top = [&] {
		if (!_title.isEmpty() && _text.isEmpty() && _links.size() == 1) {
			return pixTop + (st::linksPhotoSize - st::semiboldFont->height - st::normalFont->height) / 2;
		}
		return st::linksTextTop;
	}();

	p.setPen(st::linksTextFg);
	p.setFont(st::semiboldFont);
	if (!_title.isEmpty()) {
		if (clip.intersects(style::rtlrect(left, top, qMin(w, _titlew), st::semiboldFont->height, _width))) {
			p.drawTextLeft(left, top, _width, (w < _titlew) ? st::semiboldFont->elided(_title, w) : _title);
		}
		top += st::semiboldFont->height;
	}
	p.setFont(st::msgFont);
	if (!_text.isEmpty()) {
		int32 h = qMin(st::normalFont->height * 3, _text.countHeight(w));
		if (clip.intersects(style::rtlrect(left, top, w, h, _width))) {
			_text.drawLeftElided(p, left, top, w, _width, 3);
		}
		top += h;
	}

	p.setPen(st::windowActiveTextFg);
	for (const auto &link : std::as_const(_links)) {
		if (clip.intersects(style::rtlrect(left, top, qMin(w, link.width), st::normalFont->height, _width))) {
			p.setFont(ClickHandler::showAsActive(link.lnk) ? st::normalFont->underline() : st::normalFont);
			p.drawTextLeft(left, top, _width, (w < link.width) ? st::normalFont->elided(link.text, w) : link.text);
		}
		top += st::normalFont->height;
	}

	QRect border(style::rtlrect(left, 0, w, st::linksBorder, _width));
	if (!context->skipBorder && clip.intersects(border)) {
		p.fillRect(clip.intersected(border), st::linksBorderFg);
	}

	const auto checkDelta = st::linksPhotoSize + st::overviewCheckSkip
		- st::overviewSmallCheck.size;
	const auto checkLeft = pixLeft + checkDelta;
	const auto checkTop = pixTop + checkDelta;
	paintCheckbox(p, { checkLeft, checkTop }, selected, context);
}

void Link::validateThumbnail() {
	if (!_thumbnail.isNull() && !_thumbnailBlurred) {
		return;
	}
	const auto size = QSize(_pixw, _pixh);
	const auto outer = QSize(st::linksPhotoSize, st::linksPhotoSize);
	if (_page && _page->photo) {
		using Data::PhotoSize;
		ensurePhotoMediaCreated();
		const auto args = Images::PrepareArgs{
			.options = Images::Option::RoundSmall,
			.outer = outer,
		};
		if (const auto thumbnail = _photoMedia->image(PhotoSize::Thumbnail)) {
			_thumbnail = thumbnail->pixSingle(size, args);
			_thumbnailBlurred = false;
		} else if (const auto large = _photoMedia->image(PhotoSize::Large)) {
			_thumbnail = large->pixSingle(size, args);
			_thumbnailBlurred = false;
		} else if (const auto small = _photoMedia->image(PhotoSize::Small)) {
			_thumbnail = small->pixSingle(size, args);
			_thumbnailBlurred = false;
		} else if (const auto blurred = _photoMedia->thumbnailInline()) {
			_thumbnail = blurred->pixSingle(size, args.blurred());
			return;
		} else {
			return;
		}
		_photoMedia = nullptr;
		delegate()->unregisterHeavyItem(this);
	} else if (_page && _page->document && _page->document->hasThumbnail()) {
		ensureDocumentMediaCreated();
		const auto args = Images::PrepareArgs{
			.options = (_page->document->isVideoMessage()
				? Images::Option::RoundCircle
				: Images::Option::RoundSmall),
			.outer = outer,
		};
		if (const auto thumbnail = _documentMedia->thumbnail()) {
			_thumbnail = thumbnail->pixSingle(size, args);
			_thumbnailBlurred = false;
		} else if (const auto blurred = _documentMedia->thumbnailInline()) {
			_thumbnail = blurred->pixSingle(size, args.blurred());
			return;
		} else {
			return;
		}
		_documentMedia = nullptr;
		delegate()->unregisterHeavyItem(this);
	} else {
		const auto size = QSize(st::linksPhotoSize, st::linksPhotoSize);
		_thumbnail = QPixmap(size * style::DevicePixelRatio());
		_thumbnail.fill(Qt::transparent);
		auto p = Painter(&_thumbnail);
		const auto index = _letter.isEmpty()
			? 0
			: (_letter[0].unicode() % 4);
		const auto fill = [&](style::color color, Ui::CachedRoundCorners corners) {
			auto pixRect = QRect(
				0,
				0,
				st::linksPhotoSize,
				st::linksPhotoSize);
			Ui::FillRoundRect(p, pixRect, color, corners);
		};
		switch (index) {
		case 0: fill(st::msgFile1Bg, Ui::Doc1Corners); break;
		case 1: fill(st::msgFile2Bg, Ui::Doc2Corners); break;
		case 2: fill(st::msgFile3Bg, Ui::Doc3Corners); break;
		case 3: fill(st::msgFile4Bg, Ui::Doc4Corners); break;
		}

		if (!_letter.isEmpty()) {
			p.setFont(st::linksLetterFont);
			p.setPen(st::linksLetterFg);
			p.drawText(
				QRect(0, 0, st::linksPhotoSize, st::linksPhotoSize),
				_letter,
				style::al_center);
		}
		_thumbnailBlurred = false;
	}
}

void Link::ensurePhotoMediaCreated() {
	if (_photoMedia) {
		return;
	}
	_photoMedia = _page->photo->createMediaView();
	_photoMedia->wanted(Data::PhotoSize::Small, parent()->fullId());
	delegate()->registerHeavyItem(this);
}

void Link::ensureDocumentMediaCreated() {
	if (_documentMedia) {
		return;
	}
	_documentMedia = _page->document->createMediaView();
	_documentMedia->thumbnailWanted(parent()->fullId());
	delegate()->registerHeavyItem(this);
}

void Link::clearHeavyPart() {
	_photoMedia = nullptr;
	_documentMedia = nullptr;
}

TextState Link::getState(
		QPoint point,
		StateRequest request) const {
	int32 left = st::linksPhotoSize + st::linksPhotoPadding, top = st::linksMargin.top() + st::linksBorder, w = _width - left;
	if (style::rtlrect(0, top, st::linksPhotoSize, st::linksPhotoSize, _width).contains(point)) {
		return { parent(), _photol };
	}

	if (!_title.isEmpty() && _text.isEmpty() && _links.size() == 1) {
		top += (st::linksPhotoSize - st::semiboldFont->height - st::normalFont->height) / 2;
	}
	if (!_title.isEmpty()) {
		if (style::rtlrect(left, top, qMin(w, _titlew), st::semiboldFont->height, _width).contains(point)) {
			return { parent(), _photol };
		}
		top += st::webPageTitleFont->height;
	}
	if (!_text.isEmpty()) {
		top += qMin(st::normalFont->height * 3, _text.countHeight(w));
	}
	for (const auto &link : _links) {
		if (style::rtlrect(left, top, qMin(w, link.width), st::normalFont->height, _width).contains(point)) {
			return { parent(), ClickHandlerPtr(link.lnk) };
		}
		top += st::normalFont->height;
	}
	return {};
}

const style::RoundCheckbox &Link::checkboxStyle() const {
	return st::overviewSmallCheck;
}

Link::LinkEntry::LinkEntry(const QString &url, const QString &text)
: text(text)
, width(st::normalFont->width(text))
, lnk(UrlClickHandler::IsSuspicious(url)
	? std::make_shared<HiddenUrlClickHandler>(url)
	: std::make_shared<UrlClickHandler>(url)) {
}

// Copied from inline_bot_layout_internal.
Gif::Gif(
	not_null<Delegate*> delegate,
	not_null<HistoryItem*> parent,
	not_null<DocumentData*> gif)
: RadialProgressItem(delegate, parent)
, _data(gif)
, _spoiler(parent->isMediaSensitive()
	? std::make_unique<Ui::SpoilerAnimation>([=] {
		delegate->repaintItem(this);
	})
	: nullptr)
, _sensitiveSpoiler(parent->isMediaSensitive() ? 1 : 0) {
	setDocumentLinks(_data, true);
	if (_sensitiveSpoiler) {
		_openl = HistoryView::MakeSensitiveMediaLink(
			std::make_shared<LambdaClickHandler>(crl::guard(this, [=] {
				clearSpoiler();
				setDocumentLinks(_data, true);
			})),
			parent);
	}
	_data->loadThumbnail(parent->fullId());
}

Gif::~Gif() = default;

int Gif::contentWidth() const {
	if (_data->dimensions.width() > 0) {
		return _data->dimensions.width();
	}
	return style::ConvertScale(_data->thumbnailLocation().width());
}

int Gif::contentHeight() const {
	if (_data->dimensions.height() > 0) {
		return _data->dimensions.height();
	}
	return style::ConvertScale(_data->thumbnailLocation().height());
}

void Gif::initDimensions() {
	int32 w = contentWidth(), h = contentHeight();
	if (w <= 0 || h <= 0) {
		_maxw = 0;
	} else {
		w = w * st::inlineMediaHeight / h;
		_maxw = qMax(w, int32(st::inlineResultsMinWidth));
	}
	_minh = st::inlineMediaHeight + st::inlineResultsSkip;
}

int32 Gif::resizeGetHeight(int32 width) {
	_width = width;
	_height = _minh;
	return _height;
}

QSize Gif::countFrameSize() const {
	const auto animating = (_gif && _gif->ready());
	auto framew = animating ? _gif->width() : contentWidth();
	auto frameh = animating ? _gif->height() : contentHeight();
	const auto height = st::inlineMediaHeight;
	const auto maxSize = st::maxStickerSize;
	if (framew * height > frameh * _width) {
		if (framew < maxSize || frameh > height) {
			if (frameh > height || (framew * height / frameh) <= maxSize) {
				framew = framew * height / frameh;
				frameh = height;
			} else {
				frameh = int32(frameh * maxSize) / framew;
				framew = maxSize;
			}
		}
	} else {
		if (frameh < maxSize || framew > _width) {
			if (framew > _width || (frameh * _width / framew) <= maxSize) {
				frameh = frameh * _width / framew;
				framew = _width;
			} else {
				framew = int32(framew * maxSize) / frameh;
				frameh = maxSize;
			}
		}
	}
	return QSize(framew, frameh);
}

void Gif::clipCallback(Media::Clip::Notification notification) {
	using namespace Media::Clip;
	switch (notification) {
	case Notification::Reinit: {
		if (_gif) {
			if (_gif->state() == State::Error) {
				_gif.setBad();
			} else if (_gif->ready() && !_gif->started()) {
				if (_gif->width() * _gif->height() > kMaxInlineArea) {
					_data->dimensions = QSize(
						_gif->width(),
						_gif->height());
					_gif.reset();
				} else {
					_gif->start({
						.frame = countFrameSize(),
						.outer = { _width, st::inlineMediaHeight },
					});
				}
			} else if (_gif->autoPausedGif()
					&& !delegate()->itemVisible(this)) {
				clearHeavyPart();
			}
		}

		update();
	} break;

	case Notification::Repaint: {
		if (_gif && !_gif->currentDisplayed()) {
			update();
		}
	} break;
	}
}

void Gif::clearSpoiler() {
	if (_spoiler) {
		_spoiler = nullptr;
		_sensitiveSpoiler = false;
		_thumb = QImage();
		_thumbGood = false;
		delegate()->repaintItem(this);
	}
}

void Gif::maybeClearSensitiveSpoiler() {
	if (_sensitiveSpoiler) {
		clearSpoiler();
		setDocumentLinks(_data);
	}
}

void Gif::validateThumbnail(
		Image *image,
		QSize size,
		QSize frame,
		bool good) {
	if (!image || (_thumbGood && !good)) {
		return;
	} else if ((_thumb.size() == size * style::DevicePixelRatio())
		&& (_thumbGood || !good)) {
		return;
	}
	_thumbGood = good;
	_thumb = image->pixNoCache(
		frame * style::DevicePixelRatio(),
		{
			.options = (good ? Images::Option() : Images::Option::Blur),
			.outer = size,
		}).toImage();
}

void Gif::prepareThumbnail(QSize size, QSize frame) {
	const auto document = _data;
	Assert(document != nullptr);

	ensureDataMediaCreated();
	if (!_spoiler) {
		validateThumbnail(_dataMedia->thumbnail(), size, frame, true);
	}
	validateThumbnail(_dataMedia->thumbnailInline(), size, frame, false);
}

void Gif::paint(
		Painter &p,
		const QRect &clip,
		TextSelection selection,
		const PaintContext *context) {
	const auto document = _data;
	ensureDataMediaCreated();
	const auto preview = Data::VideoPreviewState(_dataMedia.get());
	preview.automaticLoad(getItem()->fullId());

	const auto displayLoading = !preview.usingThumbnail()
		&& document->displayLoading();
	const auto loaded = preview.loaded();
	const auto loading = preview.loading();
	if (loaded
		&& !_gif
		&& !_gif.isBad()
		&& CanPlayInline(document)) {
		auto that = const_cast<Gif*>(this);
		that->_gif = preview.makeAnimation([=](
				Media::Clip::Notification notification) {
			that->clipCallback(notification);
		});
	}

	const auto animating = !_spoiler && (_gif && _gif->started());
	if (displayLoading) {
		ensureRadial();
		if (!_radial->animating()) {
			_radial->start(dataProgress());
		}
	}
	const auto radial = isRadialAnimation();

	const auto frame = countFrameSize();
	const auto r = QRect(0, 0, _width, st::inlineMediaHeight);
	if (animating) {
		const auto pixmap = _gif->current({
			.frame = frame,
			.outer = r.size(),
		}, context->paused ? 0 : context->ms);
		if (_thumb.isNull()) {
			_thumb = pixmap;
			_thumbGood = true;
		}
		p.drawImage(r.topLeft(), pixmap);
	} else {
		prepareThumbnail(r.size(), frame);
		if (_thumb.isNull()) {
			p.fillRect(r, st::overviewPhotoBg);
		} else {
			p.drawImage(r.topLeft(), _thumb);
		}
	}

	if (_spoiler) {
		const auto paused = context->paused || On(PowerSaving::kChatSpoiler);
		Ui::FillSpoilerRect(
			p,
			r,
			Ui::DefaultImageSpoiler().frame(
				_spoiler->index(context->ms, paused)));

		if (_sensitiveSpoiler) {
			PaintSensitiveTag(p, r);
		}
	}

	const auto selected = (selection == FullSelection);

	if (radial
		|| _gif.isBad()
		|| (!_gif && !loaded && !loading && !preview.usingThumbnail())) {
		const auto radialOpacity = (radial && loaded)
			? _radial->opacity()
			: 1.;
		p.fillRect(r, st::msgDateImgBg);

		p.setOpacity(radialOpacity);
		auto icon = [&] {
			if (radial || loading) {
				return &st::historyFileInCancel;
			} else if (loaded) {
				return &st::historyFileInPlay;
			}
			return &st::historyFileInDownload;
		}();
		const auto size = st::overviewVideoRadialSize;
		QRect inner(
			(r.width() - size) / 2,
			(r.height() - size) / 2,
			size,
			size);
		icon->paintInCenter(p, inner);
		if (radial) {
			p.setOpacity(1);
			const auto margin = st::msgFileRadialLine;
			const auto rinner = inner
				- QMargins(margin, margin, margin, margin);
			auto &bg = selected
				? st::historyFileInRadialFgSelected
				: st::historyFileInRadialFg;
			_radial->draw(p, rinner, st::msgFileRadialLine, bg);
		}
	}

	const auto checkDelta = st::overviewCheckSkip + st::overviewCheck.size;
	const auto checkLeft = _width - checkDelta;
	const auto checkTop = st::overviewCheckSkip;
	paintCheckbox(p, { checkLeft, checkTop }, selected, context);
}

void Gif::update() {
	delegate()->repaintItem(this);
}

void Gif::ensureDataMediaCreated() const {
	if (_dataMedia) {
		return;
	}
	_dataMedia = _data->createMediaView();
	_dataMedia->goodThumbnailWanted();
	_dataMedia->thumbnailWanted(parent()->fullId());
	delegate()->registerHeavyItem(this);
}

void Gif::clearHeavyPart() {
	_gif.reset();
	_dataMedia = nullptr;
}

void Gif::setPosition(int32 position) {
	AbstractLayoutItem::setPosition(position);
	if (position < 0) {
		_gif.reset();
	}
}

float64 Gif::dataProgress() const {
	ensureDataMediaCreated();
	return _dataMedia->progress();
}

bool Gif::dataFinished() const {
	return !_data->loading();
}

bool Gif::dataLoaded() const {
	ensureDataMediaCreated();
	const auto preview = Data::VideoPreviewState(_dataMedia.get());
	return preview.loaded();
}

bool Gif::iconAnimated() const {
	return true;
}

TextState Gif::getState(
		QPoint point,
		StateRequest request) const {
	if (hasPoint(point)) {
		const auto link = (_data->loading() || _data->uploading())
			? _cancell
			: dataLoaded()
			? _openl
			: _savel;
		return { parent(), link };
	}
	return {};
}

void Gif::updateStatusText() {
	auto statusSize = int64();
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = Ui::FileStatusSizeFailed;
	} else if (_data->uploading()) {
		statusSize = _data->uploadingData->offset;
	} else if (dataLoaded()) {
		statusSize = Ui::FileStatusSizeLoaded;
	} else {
		statusSize = Ui::FileStatusSizeReady;
	}
	if (statusSize != _status.size()) {
		auto status = statusSize;
		auto size = _data->size;
		if (statusSize >= 0 && statusSize < 0xFF000000LL) {
			size = status;
			status = Ui::FileStatusSizeReady;
		}
		_status.update(status, size, -1, 0);
		_status.setSize(statusSize);
	}
}

} // namespace Overview::Layout
