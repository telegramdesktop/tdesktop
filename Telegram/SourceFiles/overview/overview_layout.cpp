/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "overview/overview_layout.h"

#include "overview/overview_layout_delegate.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_file_origin.h"
#include "data/data_photo_media.h"
#include "data/data_document_media.h"
#include "styles/style_overview.h"
#include "styles/style_chat.h"
#include "core/file_utilities.h"
#include "boxes/add_contact_box.h"
#include "boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "storage/file_upload.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "storage/localstorage.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/media/history_view_document.h" // DrawThumbnailAsSongCover
#include "base/unixtime.h"
#include "base/qt_adapters.h"
#include "ui/effects/round_checkbox.h"
#include "ui/image/image.h"
#include "ui/text/format_values.h"
#include "ui/text/text_options.h"
#include "ui/cached_round_corners.h"
#include "app.h"

namespace Overview {
namespace Layout {
namespace {

using TextState = HistoryView::TextState;

TextParseOptions _documentNameOptions = {
	TextParseMultiline | TextParseRichText | TextParseLinks | TextParseMarkdown, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

TextWithEntities ComposeNameWithEntities(DocumentData *document) {
	TextWithEntities result;
	const auto song = document->song();
	if (!song || (song->title.isEmpty() && song->performer.isEmpty())) {
		result.text = document->filename().isEmpty()
			? qsl("Unknown File")
			: document->filename();
		result.entities.push_back({
			EntityType::Semibold,
			0,
			result.text.size()
		});
	} else if (song->performer.isEmpty()) {
		result.text = song->title;
		result.entities.push_back({
			EntityType::Semibold,
			0,
			result.text.size()
		});
	} else {
		result.text = song->performer
			+ QString::fromUtf8(" \xe2\x80\x93 ")
			+ (song->title.isEmpty() ? qsl("Unknown Track") : song->title);
		result.entities.push_back({
			EntityType::Semibold,
			0,
			song->performer.size()
		});
	}
	return result;
}

} // namespace

class Checkbox {
public:
	template <typename UpdateCallback>
	Checkbox(UpdateCallback callback, const style::RoundCheckbox &st)
	: _updateCallback(callback)
	, _check(st, _updateCallback) {
	}

	void paint(Painter &p, QPoint position, int outerWidth, bool selected, bool selecting);

	void setActive(bool active);
	void setPressed(bool pressed);

	void invalidateCache() {
		_check.invalidateCache();
	}

private:
	void startAnimation();

	Fn<void()> _updateCallback;
	Ui::RoundCheckbox _check;

	Ui::Animations::Simple _pression;
	bool _active = false;
	bool _pressed = false;

};

void Checkbox::paint(Painter &p, QPoint position, int outerWidth, bool selected, bool selecting) {
	_check.setDisplayInactive(selecting);
	_check.setChecked(selected);
	const auto pression = _pression.value((_active && _pressed) ? 1. : 0.);
	const auto masterScale = 1. - (1. - st::overviewCheckPressedSize) * pression;
	_check.paint(p, position.x(), position.y(), outerWidth, masterScale);
}

void Checkbox::setActive(bool active) {
	_active = active;
	if (_pressed) {
		startAnimation();
	}
}

void Checkbox::setPressed(bool pressed) {
	_pressed = pressed;
	if (_active) {
		startAnimation();
	}
}

void Checkbox::startAnimation() {
	auto showPressed = (_pressed && _active);
	_pression.start(_updateCallback, showPressed ? 0. : 1., showPressed ? 1. : 0., st::overviewCheck.duration);
}

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
		not_null<DocumentData*> document) {
	const auto context = parent()->fullId();
	setLinks(
		std::make_shared<DocumentOpenClickHandler>(document, context),
		std::make_shared<DocumentSaveClickHandler>(document, context),
		std::make_shared<DocumentCancelClickHandler>(document, context));
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

void StatusText::update(int newSize, int fullSize, int duration, crl::time realDuration) {
	setSize(newSize);
	if (_size == Ui::FileStatusSizeReady) {
		_text = (duration >= 0) ? Ui::FormatDurationAndSizeText(duration, fullSize) : (duration < -1 ? Ui::FormatGifAndSizeText(fullSize) : Ui::FormatSizeText(fullSize));
	} else if (_size == Ui::FileStatusSizeLoaded) {
		_text = (duration >= 0) ? Ui::FormatDurationText(duration) : (duration < -1 ? qsl("GIF") : Ui::FormatSizeText(fullSize));
	} else if (_size == Ui::FileStatusSizeFailed) {
		_text = tr::lng_attach_failed(tr::now);
	} else if (_size >= 0) {
		_text = Ui::FormatDownloadText(_size, fullSize);
	} else {
		_text = Ui::FormatPlayedText(-_size - 1, realDuration);
	}
}

void StatusText::setSize(int newSize) {
	_size = newSize;
}

Photo::Photo(
	not_null<Delegate*> delegate,
	not_null<HistoryItem*> parent,
	not_null<PhotoData*> photo)
: ItemBase(delegate, parent)
, _data(photo)
, _link(std::make_shared<PhotoOpenClickHandler>(photo, parent->fullId())) {
	if (_data->inlineThumbnailBytes().isEmpty()
		&& (_data->hasExact(Data::PhotoSize::Small)
			|| _data->hasExact(Data::PhotoSize::Thumbnail))) {
		_data->load(Data::PhotoSize::Small, parent->fullId());
	}
}

void Photo::initDimensions() {
	_maxw = 2 * st::overviewPhotoMinSize;
	_minh = _maxw;
}

int32 Photo::resizeGetHeight(int32 width) {
	width = qMin(width, _maxw);
	if (width != _width || width != _height) {
		_width = qMin(width, _maxw);
		_height = _width;
	}
	return _height;
}

void Photo::paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) {
	const auto selected = (selection == FullSelection);
	const auto widthChanged = _pix.width() != _width * cIntRetinaFactor();
	if (!_goodLoaded || widthChanged) {
		ensureDataMediaCreated();
		const auto good = _dataMedia->loaded()
			|| (_dataMedia->image(Data::PhotoSize::Thumbnail) != nullptr);
		if ((good && !_goodLoaded) || widthChanged) {
			_goodLoaded = good;
			_pix = QPixmap();
			if (_goodLoaded) {
				setPixFrom(_dataMedia->image(Data::PhotoSize::Large)
					? _dataMedia->image(Data::PhotoSize::Large)
					: _dataMedia->image(Data::PhotoSize::Thumbnail));
			} else if (const auto small = _dataMedia->image(
					Data::PhotoSize::Small)) {
				setPixFrom(small);
			} else if (const auto blurred = _dataMedia->thumbnailInline()) {
				setPixFrom(blurred);
			}
		}
	}

	if (_pix.isNull()) {
		p.fillRect(0, 0, _width, _height, st::overviewPhotoBg);
	} else {
		p.drawPixmap(0, 0, _pix);
	}

	if (selected) {
		p.fillRect(0, 0, _width, _height, st::overviewPhotoSelectOverlay);
	}
	const auto checkDelta = st::overviewCheckSkip + st::overviewCheck.size;
	const auto checkLeft = _width - checkDelta;
	const auto checkTop = _height - checkDelta;
	paintCheckbox(p, { checkLeft, checkTop }, selected, context);
}

void Photo::setPixFrom(not_null<Image*> image) {
	const auto size = _width * cIntRetinaFactor();
	auto img = image->original();
	if (!_goodLoaded) {
		img = Images::prepareBlur(std::move(img));
	}
	if (img.width() == img.height()) {
		if (img.width() != size) {
			img = img.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
		}
	} else if (img.width() > img.height()) {
		img = img.copy((img.width() - img.height()) / 2, 0, img.height(), img.height()).scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
	} else {
		img = img.copy(0, (img.height() - img.width()) / 2, img.width(), img.width()).scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
	}
	img.setDevicePixelRatio(cRetinaFactor());

	// In case we have inline thumbnail we can unload all images and we still
	// won't get a blank image in the media viewer when the photo is opened.
	if (!_data->inlineThumbnailBytes().isEmpty()) {
		_dataMedia = nullptr;
		delegate()->unregisterHeavyItem(this);
	}

	_pix = App::pixmapFromImageInPlace(std::move(img));
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
	not_null<DocumentData*> video)
: RadialProgressItem(delegate, parent)
, _data(video)
, _duration(Ui::FormatDurationText(_data->getDuration())) {
	setDocumentLinks(_data);
	_data->loadThumbnail(parent->fullId());
}

Video::~Video() = default;

void Video::initDimensions() {
	_maxw = 2 * st::overviewPhotoMinSize;
	_minh = _maxw;
}

int32 Video::resizeGetHeight(int32 width) {
	_width = qMin(width, _maxw);
	_height = _width;
	return _height;
}

void Video::paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) {
	ensureDataMediaCreated();

	const auto selected = (selection == FullSelection);
	const auto blurred = _dataMedia->thumbnailInline();
	const auto thumbnail = _dataMedia->thumbnail();
	const auto good = _dataMedia->goodThumbnail();

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
		&& ((_pix.width() != _width * cIntRetinaFactor())
			|| (_pixBlurred && (thumbnail || good)))) {
		auto size = _width * cIntRetinaFactor();
		auto img = good
			? good->original()
			: thumbnail
			? thumbnail->original()
			: Images::prepareBlur(blurred->original());
		if (img.width() == img.height()) {
			if (img.width() != size) {
				img = img.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
			}
		} else if (img.width() > img.height()) {
			img = img.copy((img.width() - img.height()) / 2, 0, img.height(), img.height()).scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
		} else {
			img = img.copy(0, (img.height() - img.width()) / 2, img.width(), img.width()).scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
		}
		img.setDevicePixelRatio(cRetinaFactor());

		_pix = App::pixmapFromImageInPlace(std::move(img));
		_pixBlurred = !(thumbnail || good);
	}

	if (_pix.isNull()) {
		p.fillRect(0, 0, _width, _height, st::overviewPhotoBg);
	} else {
		p.drawPixmap(0, 0, _pix);
	}

	if (selected) {
		p.fillRect(QRect(0, 0, _width, _height), st::overviewPhotoSelectOverlay);
	}

	if (!selected && !context->selecting && radialOpacity < 1.) {
		if (clip.intersects(QRect(0, _height - st::normalFont->height, _width, st::normalFont->height))) {
			const auto download = !loaded && !_dataMedia->canBePlayed();
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
			auto over = ClickHandler::showAsActive((_data->loading() || _data->uploading()) ? _cancell : (loaded || _dataMedia->canBePlayed()) ? _openl : _savel);
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
	if (_dataMedia) {
		return;
	}
	_dataMedia = _data->createMediaView();
	_dataMedia->goodThumbnailWanted();
	_dataMedia->thumbnailWanted(parent()->fullId());
	delegate()->registerHeavyItem(this);
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
		const auto link = (_data->loading() || _data->uploading())
			? _cancell
			: (dataLoaded() || _dataMedia->canBePlayed())
			? _openl
			: _savel;
		return { parent(), link };
	}
	return {};
}

void Video::updateStatusText() {
	bool showPause = false;
	int statusSize = 0;
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
		int status = statusSize, size = _data->size;
		if (statusSize >= 0 && statusSize < 0x7F000000) {
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
, _namel(std::make_shared<DocumentOpenClickHandler>(_data, parent->fullId()))
, _st(st) {
	AddComponents(Info::Bit());

	setDocumentLinks(_data);
	_data->loadThumbnail(parent->fullId());

	updateName();
	const auto dateText = textcmdLink(
		1,
		TextUtilities::EscapeForRichParsing(
			langDateTime(base::unixtime::parse(_data->date))));
	TextParseOptions opts = { TextParseRichText, 0, 0, Qt::LayoutDirectionAuto };
	_details.setText(
		st::defaultTextStyle,
		tr::lng_date_and_duration(
			tr::now,
			lt_date,
			dateText,
			lt_duration,
			Ui::FormatDurationText(duration())),
		opts);
	_details.setLink(1, goToMessageClickHandler(parent));
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
	const auto nameVersion = parent()->fromOriginal()->nameVersion;
	if (nameVersion > _nameVersion) {
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
			const auto thumb = thumbnail
				? thumbnail->pixCircled(inner.width(), inner.height())
				: blurred->pixBlurredCircled(inner.width(), inner.height());
			p.drawPixmap(inner.topLeft(), thumb);
		} else if (_data->hasThumbnail()) {
			PainterHighQualityEnabler hq(p);
			p.setBrush(st::imageBg);
			p.drawEllipse(inner);
		}
		const auto &checkLink = (_data->loading() || _data->uploading())
			? _cancell
			: (_dataMedia->canBePlayed() || loaded)
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
			} else if (_dataMedia->canBePlayed()) {
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
		if (parent()->hasUnreadMediaFlag() && unreadx + st::mediaUnreadSkip + st::mediaUnreadSize <= _width) {
			p.setPen(Qt::NoPen);
			p.setBrush(selected ? st::msgFileInBgSelected : st::msgFileInBg);

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(style::rtlrect(unreadx + st::mediaUnreadSkip, statustop + st::mediaUnreadTop, st::mediaUnreadSize, st::mediaUnreadSize, _width));
			}
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
			: (_dataMedia->canBePlayed() || loaded)
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
	auto version = 0;
	if (const auto forwarded = parent()->Get<HistoryMessageForwarded>()) {
		if (parent()->fromOriginal()->isChannel()) {
			_name.setText(st::semiboldTextStyle, tr::lng_forwarded_channel(tr::now, lt_channel, parent()->fromOriginal()->name), Ui::NameTextOptions());
		} else {
			_name.setText(st::semiboldTextStyle, tr::lng_forwarded(tr::now, lt_user, parent()->fromOriginal()->name), Ui::NameTextOptions());
		}
	} else {
		_name.setText(st::semiboldTextStyle, parent()->from()->name, Ui::NameTextOptions());
	}
	version = parent()->fromOriginal()->nameVersion;
	_nameVersion = version;
}

int Voice::duration() const {
	return std::max(_data->getDuration(), 0);
}

bool Voice::updateStatusText() {
	bool showPause = false;
	int32 statusSize = 0, realDuration = 0;
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
		_status.update(statusSize, _data->size, duration(), realDuration);
	}
	return showPause;
}

Document::Document(
	not_null<Delegate*> delegate,
	not_null<HistoryItem*> parent,
	not_null<DocumentData*> document,
	const style::OverviewFileLayout &st)
: RadialProgressItem(delegate, parent)
, _data(document)
, _msgl(goToMessageClickHandler(parent))
, _namel(std::make_shared<DocumentOpenClickHandler>(_data, parent->fullId()))
, _st(st)
, _date(langDateTime(base::unixtime::parse(_data->date)))
, _datew(st::normalFont->width(_date))
, _colorIndex(documentColorIndex(_data, _ext)) {
	_name.setMarkedText(st::defaultTextStyle, ComposeNameWithEntities(_data), _documentNameOptions);

	AddComponents(Info::Bit());

	setDocumentLinks(_data);

	_status.update(Ui::FileStatusSizeReady, _data->size, _data->isSong() ? _data->song()->duration : -1, 0);

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
		&& _data->canBeStreamed()
		&& !_data->inappPlaybackFailed()
		&& IsServerMsgId(parent()->id);
}

void Document::initDimensions() {
	_maxw = _st.maxWidth;
	if (_data->isSong()) {
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

	const auto isSong = _data->isSong();
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
				&& DrawThumbnailAsSongCover(p, _dataMedia, inner, selected);
			if (!coverDrawn) {
				if (selected) {
					p.setBrush(st::msgFileInBgSelected);
				} else {
					const auto over = ClickHandler::showAsActive(isLoading
						? _cancell
						: (loaded || _dataMedia->canBePlayed())
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
					} else if (loaded || _dataMedia->canBePlayed()) {
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
				} else if (loaded || _dataMedia->canBePlayed()) {
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
		if (!context->isAfterDate && clip.intersects(border)) {
			p.fillRect(clip.intersected(border), st::linksBorderFg);
		}

		QRect rthumb(style::rtlrect(0, st::linksBorder + _st.filePadding.top(), _st.fileThumbSize, _st.fileThumbSize, _width));
		if (clip.intersects(rthumb)) {
			if (wthumb) {
				ensureDataMediaCreated();
				const auto thumbnail = _dataMedia->thumbnail();
				const auto thumbLoaded = (thumbnail != nullptr);
				const auto blurred = _dataMedia->thumbnailInline();
				if (thumbnail || blurred) {
					if (_thumb.isNull() || (thumbnail && !_thumbLoaded)) {
						_thumbLoaded = (thumbnail != nullptr);
						auto options = Images::Option::Smooth
							| (_thumbLoaded
								? Images::Option::None
								: Images::Option::Blurred);
						const auto image = thumbnail ? thumbnail : blurred;
						_thumb = image->pixNoCache(_thumbw * cIntRetinaFactor(), 0, options, _st.fileThumbSize, _st.fileThumbSize);
					}
					p.drawPixmap(rthumb.topLeft(), _thumb);
				} else {
					p.fillRect(rthumb, st::overviewFileThumbBg);
				}
			} else {
				p.fillRect(rthumb, documentColor(_colorIndex));
				if (!radial && loaded && !_ext.isEmpty()) {
					p.setFont(st::overviewFileExtFont);
					p.setPen(st::overviewFileExtFg);
					p.drawText(rthumb.left() + (rthumb.width() - _extw) / 2, rthumb.top() + st::overviewFileExtTop + st::overviewFileExtFont->ascent, _ext);
				}
			}
			if (selected) {
				p.fillRect(rthumb, st::defaultTextPalette.selectOverlay);
			}

			if (radial || (!loaded && !_data->loading())) {
				QRect inner(rthumb.x() + (rthumb.width() - _st.songThumbSize) / 2, rthumb.y() + (rthumb.height() - _st.songThumbSize) / 2, _st.songThumbSize, _st.songThumbSize);
				if (clip.intersects(inner)) {
					auto radialOpacity = (radial && loaded && !_data->uploading()) ? _radial->opacity() : 1;
					p.setPen(Qt::NoPen);
					if (selected) {
						p.setBrush(wthumb ? st::msgDateImgBgSelected : documentSelectedColor(_colorIndex));
					} else {
						auto over = ClickHandler::showAsActive(_data->loading() ? _cancell : _savel);
						p.setBrush(anim::brush(wthumb ? st::msgDateImgBg : documentDarkColor(_colorIndex), wthumb ? st::msgDateImgBgOver : documentOverColor(_colorIndex), _a_iconOver.value(over ? 1. : 0.)));
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
		p.setFont(ClickHandler::showAsActive(_msgl) ? st::normalFont->underline() : st::normalFont);
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

void Document::drawCornerDownload(Painter &p, bool selected, const PaintContext *context) const {
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
		auto fg = selected ? st::historyFileThumbRadialFgSelected : st::historyFileThumbRadialFg;
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
	const auto wthumb = withThumb();

	if (_data->isSong()) {
		const auto nameleft = _st.songPadding.left() + _st.songThumbSize + _st.songPadding.right();
		const auto nameright = _st.songPadding.left();
		const auto namewidth = std::min(
			_width - nameleft - nameright,
			_name.maxWidth());
		const auto nametop = _st.songNameTop;
		const auto statustop = _st.songStatusTop;

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
				: (loaded || _dataMedia->canBePlayed())
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
		const auto statustop = st::linksBorder + _st.fileStatusTop;
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
	return _data->isSong()
		|| !dataLoaded()
		|| (_radial && _radial->animating());
}

bool Document::withThumb() const {
	return !_data->isSong()
		&& _data->hasThumbnail()
		&& !Data::IsExecutableName(_data->filename());
}

bool Document::updateStatusText() {
	bool showPause = false;
	int32 statusSize = 0, realDuration = 0;
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

	if (_data->isSong()) {
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
		_status.update(statusSize, _data->size, _data->isSong() ? _data->song()->duration : -1, realDuration);
	}
	return showPause;
}

Link::Link(
	not_null<Delegate*> delegate,
	not_null<HistoryItem*> parent,
	Data::Media *media)
: ItemBase(delegate, parent) {
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
			if (!QRegularExpression(qsl("^[,.\\s_=+\\-;:`'\"\\(\\)\\[\\]\\{\\}<>*&^%\\$#@!\\\\/]+$")).match(text.mid(afterLinkStart, till - afterLinkStart)).hasMatch()) {
				++lnk;
				break;
			}
		}
		till = entity.offset();
	}
	if (!lnk) {
		if (QRegularExpression(qsl("^[,.\\s\\-;:`'\"\\(\\)\\[\\]\\{\\}<>*&^%\\$#@!\\\\/]+$")).match(text.mid(from, till - from)).hasMatch()) {
			till = from;
		}
	}

	const auto createHandler = [](const QString &url) {
		return UrlClickHandler::IsSuspicious(url)
			? std::make_shared<HiddenUrlClickHandler>(url)
			: std::make_shared<UrlClickHandler>(url);
	};
	_page = media ? media->webpage() : nullptr;
	if (_page) {
		mainUrl = _page->url;
		if (_page->document) {
			_photol = std::make_shared<DocumentOpenClickHandler>(
				_page->document,
				parent->fullId());
		} else if (_page->photo) {
			if (_page->type == WebPageType::Profile || _page->type == WebPageType::Video) {
				_photol = createHandler(_page->url);
			} else if (_page->type == WebPageType::Photo
				|| _page->siteName == qstr("Twitter")
				|| _page->siteName == qstr("Facebook")) {
				_photol = std::make_shared<PhotoOpenClickHandler>(
					_page->photo,
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

#ifndef OS_MAC_OLD
	auto parts = mainUrl.splitRef('/');
#else // OS_MAC_OLD
	auto parts = mainUrl.split('/');
#endif // OS_MAC_OLD
	if (!parts.isEmpty()) {
		auto domain = parts.at(0);
		if (parts.size() > 2 && domain.endsWith(':') && parts.at(1).isEmpty()) { // http:// and others
			domain = parts.at(2);
		}

		parts = domain.split('@').back().split('.', base::QStringSkipEmptyParts);
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
	if (!context->isAfterDate && clip.intersects(border)) {
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
	if (_page && _page->photo) {
		using Data::PhotoSize;
		ensurePhotoMediaCreated();
		if (const auto thumbnail = _photoMedia->image(PhotoSize::Thumbnail)) {
			_thumbnail = thumbnail->pixSingle(_pixw, _pixh, st::linksPhotoSize, st::linksPhotoSize, ImageRoundRadius::Small);
			_thumbnailBlurred = false;
		} else if (const auto large = _photoMedia->image(PhotoSize::Large)) {
			_thumbnail = large->pixSingle(_pixw, _pixh, st::linksPhotoSize, st::linksPhotoSize, ImageRoundRadius::Small);
			_thumbnailBlurred = false;
		} else if (const auto small = _photoMedia->image(PhotoSize::Small)) {
			_thumbnail = small->pixSingle(_pixw, _pixh, st::linksPhotoSize, st::linksPhotoSize, ImageRoundRadius::Small);
			_thumbnailBlurred = false;
		} else if (const auto blurred = _photoMedia->thumbnailInline()) {
			_thumbnail = blurred->pixBlurredSingle(_pixw, _pixh, st::linksPhotoSize, st::linksPhotoSize, ImageRoundRadius::Small);
			return;
		} else {
			return;
		}
		_photoMedia = nullptr;
		delegate()->unregisterHeavyItem(this);
	} else if (_page && _page->document && _page->document->hasThumbnail()) {
		ensureDocumentMediaCreated();
		const auto roundRadius = _page->document->isVideoMessage()
			? ImageRoundRadius::Ellipse
			: ImageRoundRadius::Small;
		if (const auto thumbnail = _documentMedia->thumbnail()) {
			_thumbnail = thumbnail->pixSingle(_pixw, _pixh, st::linksPhotoSize, st::linksPhotoSize, roundRadius);
			_thumbnailBlurred = false;
		} else if (const auto blurred = _documentMedia->thumbnailInline()) {
			_thumbnail = blurred->pixBlurredSingle(_pixw, _pixh, st::linksPhotoSize, st::linksPhotoSize, roundRadius);
			return;
		} else {
			return;
		}
		_documentMedia = nullptr;
		delegate()->unregisterHeavyItem(this);
	} else {
		const auto size = QSize(st::linksPhotoSize, st::linksPhotoSize);
		_thumbnail = QPixmap(size * cIntRetinaFactor());
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

} // namespace Layout
} // namespace Overview
