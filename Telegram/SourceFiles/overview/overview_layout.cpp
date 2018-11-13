/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "overview/overview_layout.h"

#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "data/data_media_types.h"
#include "styles/style_overview.h"
#include "styles/style_history.h"
#include "core/file_utilities.h"
#include "boxes/add_contact_box.h"
#include "boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "application.h"
#include "storage/file_upload.h"
#include "mainwindow.h"
#include "media/media_audio.h"
#include "media/player/media_player_instance.h"
#include "storage/localstorage.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_cursor_state.h"
#include "ui/effects/round_checkbox.h"
#include "ui/image/image.h"
#include "ui/text_options.h"

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
		result.entities.push_back({ EntityInTextBold, 0, result.text.size() });
	} else if (song->performer.isEmpty()) {
		result.text = song->title;
		result.entities.push_back({ EntityInTextBold, 0, result.text.size() });
	} else {
		result.text = song->performer + QString::fromUtf8(" \xe2\x80\x93 ") + (song->title.isEmpty() ? qsl("Unknown Track") : song->title);
		result.entities.push_back({ EntityInTextBold, 0, song->performer.size() });
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

	void paint(Painter &p, TimeMs ms, QPoint position, int outerWidth, bool selected, bool selecting);

	void setActive(bool active);
	void setPressed(bool pressed);

	void invalidateCache() {
		_check.invalidateCache();
	}

private:
	void startAnimation();

	Fn<void()> _updateCallback;
	Ui::RoundCheckbox _check;

	Animation _pression;
	bool _active = false;
	bool _pressed = false;

};

void Checkbox::paint(Painter &p, TimeMs ms, QPoint position, int outerWidth, bool selected, bool selecting) {
	_check.setDisplayInactive(selecting);
	_check.setChecked(selected);
	const auto pression = _pression.current(ms, (_active && _pressed) ? 1. : 0.);
	const auto masterScale = 1. - (1. - st::overviewCheckPressedSize) * pression;
	_check.paint(p, ms, position.x(), position.y(), outerWidth, masterScale);
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

MsgId AbstractItem::msgId() const {
	auto item = getItem();
	return item ? item->id : 0;
}

ItemBase::ItemBase(not_null<HistoryItem*> parent)
: _parent(parent)
, _dateTime(ItemDateTime(parent)) {
}

QDateTime ItemBase::dateTime() const {
	return _dateTime;
}

void ItemBase::clickHandlerActiveChanged(
		const ClickHandlerPtr &action,
		bool active) {
	Auth().data().requestItemRepaint(_parent);
	if (_check) {
		_check->setActive(active);
	}
}

void ItemBase::clickHandlerPressedChanged(
		const ClickHandlerPtr &action,
		bool pressed) {
	Auth().data().requestItemRepaint(_parent);
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
		_check->paint(p, context->ms, position, _width, selected, context->selecting);
	}
}

const style::RoundCheckbox &ItemBase::checkboxStyle() const {
	return st::overviewCheck;
}

void ItemBase::ensureCheckboxCreated() {
	if (!_check) {
		_check = std::make_unique<Checkbox>(
			[=] { Auth().data().requestItemRepaint(_parent); },
			checkboxStyle());
	}
}

ItemBase::~ItemBase() = default;

void RadialProgressItem::setDocumentLinks(
		not_null<DocumentData*> document) {
	const auto context = parent()->fullId();
	const auto createSaveHandler = [&]() -> ClickHandlerPtr {
		if (document->isVideoMessage()) {
			return std::make_shared<GifOpenClickHandler>(
				document,
				context);
		} else if (document->isVoiceMessage()) {
			return std::make_shared<DocumentOpenClickHandler>(
				document,
				context);
		}
		return std::make_shared<DocumentSaveClickHandler>(
			document,
			context);
	};
	setLinks(
		(document->isVideoMessage()
			? std::make_shared<GifOpenClickHandler>(
				document,
				context)
			: std::make_shared<DocumentOpenClickHandler>(
				document,
				context)),
		createSaveHandler(),
		std::make_shared<DocumentCancelClickHandler>(
			document,
			context));
}

void RadialProgressItem::clickHandlerActiveChanged(const ClickHandlerPtr &action, bool active) {
	ItemBase::clickHandlerActiveChanged(action, active);
	if (action == _openl || action == _savel || action == _cancell) {
		if (iconAnimated()) {
			_a_iconOver.start(
				[=] { Auth().data().requestItemRepaint(parent()); },
				active ? 0. : 1.,
				active ? 1. : 0.,
				st::msgFileOverDuration);
		}
	}
}

void RadialProgressItem::setLinks(ClickHandlerPtr &&openl, ClickHandlerPtr &&savel, ClickHandlerPtr &&cancell) {
	_openl = std::move(openl);
	_savel = std::move(savel);
	_cancell = std::move(cancell);
}

void RadialProgressItem::step_radial(TimeMs ms, bool timer) {
	const auto updateRadial = [&] {
		return _radial->update(dataProgress(), dataFinished(), ms);
	};
	if (timer) {
		if (!anim::Disabled() || updateRadial()) {
			Auth().data().requestItemRepaint(parent());
		}
	} else {
		updateRadial();
		if (!_radial->animating()) {
			checkRadialFinished();
		}
	}
}

void RadialProgressItem::ensureRadial() {
	if (!_radial) {
		_radial = std::make_unique<Ui::RadialAnimation>(animation(const_cast<RadialProgressItem*>(this), &RadialProgressItem::step_radial));
	}
}

void RadialProgressItem::checkRadialFinished() {
	if (_radial && !_radial->animating() && dataLoaded()) {
		_radial.reset();
	}
}

RadialProgressItem::~RadialProgressItem() = default;

void StatusText::update(int newSize, int fullSize, int duration, TimeMs realDuration) {
	setSize(newSize);
	if (_size == FileStatusSizeReady) {
		_text = (duration >= 0) ? formatDurationAndSizeText(duration, fullSize) : (duration < -1 ? formatGifAndSizeText(fullSize) : formatSizeText(fullSize));
	} else if (_size == FileStatusSizeLoaded) {
		_text = (duration >= 0) ? formatDurationText(duration) : (duration < -1 ? qsl("GIF") : formatSizeText(fullSize));
	} else if (_size == FileStatusSizeFailed) {
		_text = lang(lng_attach_failed);
	} else if (_size >= 0) {
		_text = formatDownloadText(_size, fullSize);
	} else {
		_text = formatPlayedText(-_size - 1, realDuration);
	}
}

void StatusText::setSize(int newSize) {
	_size = newSize;
}

Date::Date(const QDate &date, bool month)
: _date(date)
, _text(month ? langMonthFull(date) : langDayOfMonthFull(date)) {
	AddComponents(Info::Bit());
}

void Date::initDimensions() {
	_maxw = st::normalFont->width(_text);
	_minh = st::linksDateMargin.top() + st::normalFont->height + st::linksDateMargin.bottom() + st::linksBorder;
}

void Date::paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) {
	if (clip.intersects(QRect(0, st::linksDateMargin.top(), _width, st::normalFont->height))) {
		p.setPen(st::linksDateColor);
		p.setFont(st::semiboldFont);
		p.drawTextLeft(0, st::linksDateMargin.top(), _width, _text);
	}
}

Photo::Photo(
	not_null<HistoryItem*> parent,
	not_null<PhotoData*> photo)
: ItemBase(parent)
, _data(photo)
, _link(std::make_shared<PhotoOpenClickHandler>(photo, parent->fullId())) {
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
	bool good = _data->loaded(), selected = (selection == FullSelection);
	if (!good) {
		_data->medium->automaticLoad(parent()->fullId(), parent());
		good = _data->medium->loaded();
	}
	if ((good && !_goodLoaded) || _pix.width() != _width * cIntRetinaFactor()) {
		_goodLoaded = good;

		int32 size = _width * cIntRetinaFactor();
		if (_goodLoaded || _data->thumb->loaded()) {
			auto img = (_data->loaded() ? _data->full : (_data->medium->loaded() ? _data->medium : _data->thumb))->pix(parent()->fullId()).toImage();
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
			_data->unload();

			_pix = App::pixmapFromImageInPlace(std::move(img));
		} else if (!_pix.isNull()) {
			_pix = QPixmap();
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

TextState Photo::getState(
		QPoint point,
		StateRequest request) const {
	if (hasPoint(point)) {
		return { parent(), _link };
	}
	return {};
}

Video::Video(
	not_null<HistoryItem*> parent,
	not_null<DocumentData*> video)
: RadialProgressItem(parent)
, _data(video)
, _duration(formatDurationText(_data->duration()))
, _thumbLoaded(false) {
	setDocumentLinks(_data);
	_data->thumb->load(parent->fullId());
}

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
	bool selected = (selection == FullSelection), thumbLoaded = _data->thumb->loaded();

	_data->automaticLoad(parent()->fullId(), parent());
	bool loaded = _data->loaded(), displayLoading = _data->displayLoading();
	if (displayLoading) {
		ensureRadial();
		if (!_radial->animating()) {
			_radial->start(_data->progress());
		}
	}
	updateStatusText();
	bool radial = isRadialAnimation(context->ms);

	if ((thumbLoaded && !_thumbLoaded) || (_pix.width() != _width * cIntRetinaFactor())) {
		_thumbLoaded = thumbLoaded;

		if (_thumbLoaded && !_data->thumb->isNull()) {
			auto size = _width * cIntRetinaFactor();
			auto img = Images::prepareBlur(_data->thumb->pix(parent()->fullId()).toImage());
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
			_data->unload();

			_pix = App::pixmapFromImageInPlace(std::move(img));
		} else if (!_pix.isNull()) {
			_pix = QPixmap();
		}
	}

	if (_pix.isNull()) {
		p.fillRect(0, 0, _width, _height, st::overviewPhotoBg);
	} else {
		p.drawPixmap(0, 0, _pix);
	}

	if (selected) {
		p.fillRect(QRect(0, 0, _width, _height), st::overviewPhotoSelectOverlay);
	}

	if (!selected && !context->selecting && !loaded) {
		if (clip.intersects(QRect(0, _height - st::normalFont->height, _width, st::normalFont->height))) {
			int32 statusX = st::msgDateImgPadding.x(), statusY = _height - st::normalFont->height - st::msgDateImgPadding.y();
			int32 statusW = st::normalFont->width(_status.text()) + 2 * st::msgDateImgPadding.x();
			int32 statusH = st::normalFont->height + 2 * st::msgDateImgPadding.y();
			statusX = _width - statusW + statusX;
			p.fillRect(rtlrect(statusX - st::msgDateImgPadding.x(), statusY - st::msgDateImgPadding.y(), statusW, statusH, _width), selected ? st::msgDateImgBgSelected : st::msgDateImgBg);
			p.setFont(st::normalFont);
			p.setPen(st::msgDateImgFg);
			p.drawTextLeft(statusX, statusY, _width, _status.text(), statusW - 2 * st::msgDateImgPadding.x());
		}
	}
	if (clip.intersects(QRect(0, 0, _width, st::normalFont->height))) {
		int32 statusX = st::msgDateImgPadding.x(), statusY = st::msgDateImgPadding.y();
		int32 statusW = st::normalFont->width(_duration) + 2 * st::msgDateImgPadding.x();
		int32 statusH = st::normalFont->height + 2 * st::msgDateImgPadding.y();
		p.fillRect(rtlrect(statusX - st::msgDateImgPadding.x(), statusY - st::msgDateImgPadding.y(), statusW, statusH, _width), selected ? st::msgDateImgBgSelected : st::msgDateImgBg);
		p.setFont(st::normalFont);
		p.setPen(st::msgDateImgFg);
		p.drawTextLeft(statusX, statusY, _width, _duration, statusW - 2 * st::msgDateImgPadding.x());
	}

	QRect inner((_width - st::msgFileSize) / 2, (_height - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
	if (clip.intersects(inner)) {
		p.setPen(Qt::NoPen);
		if (selected) {
			p.setBrush(st::msgDateImgBgSelected);
		} else {
			auto over = ClickHandler::showAsActive(loaded ? _openl : (_data->loading() ? _cancell : _savel));
			p.setBrush(anim::brush(st::msgDateImgBg, st::msgDateImgBgOver, _a_iconOver.current(context->ms, over ? 1. : 0.)));
		}

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}

		p.setOpacity((radial && loaded) ? _radial->opacity() : 1);
		auto icon = ([radial, loaded, selected] {
			if (radial) {
				return &(selected ? st::historyFileThumbCancelSelected : st::historyFileThumbCancel);
			} else if (loaded) {
				return &(selected ? st::historyFileThumbPlaySelected : st::historyFileThumbPlay);
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

	const auto checkDelta = st::overviewCheckSkip + st::overviewCheck.size;
	const auto checkLeft = _width - checkDelta;
	const auto checkTop = _height - checkDelta;
	paintCheckbox(p, { checkLeft, checkTop }, selected, context);
}

float64 Video::dataProgress() const {
	return _data->progress();
}

bool Video::dataFinished() const {
	return !_data->loading();
}

bool Video::dataLoaded() const {
	return _data->loaded();
}

bool Video::iconAnimated() const {
	return true;
}

TextState Video::getState(
		QPoint point,
		StateRequest request) const {
	bool loaded = _data->loaded();

	if (hasPoint(point)) {
		const auto link = loaded
			? _openl
			: _data->loading()
			? _cancell
			: _savel;
		return { parent(), link };
	}
	return {};
}

void Video::updateStatusText() {
	bool showPause = false;
	int statusSize = 0;
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = FileStatusSizeFailed;
	} else if (_data->uploading()) {
		statusSize = _data->uploadingData->offset;
	} else if (_data->loading()) {
		statusSize = _data->loadOffset();
	} else if (_data->loaded()) {
		statusSize = FileStatusSizeLoaded;
	} else {
		statusSize = FileStatusSizeReady;
	}
	if (statusSize != _status.size()) {
		int status = statusSize, size = _data->size;
		if (statusSize >= 0 && statusSize < 0x7F000000) {
			size = status;
			status = FileStatusSizeReady;
		}
		_status.update(status, size, -1, 0);
		_status.setSize(statusSize);
	}
}

Voice::Voice(
	not_null<HistoryItem*> parent,
	not_null<DocumentData*> voice,
	const style::OverviewFileLayout &st)
: RadialProgressItem(parent)
, _data(voice)
, _namel(_data->isVideoMessage()
	? std::make_shared<GifOpenClickHandler>(_data, parent->fullId())
	: std::make_shared<DocumentOpenClickHandler>(_data, parent->fullId()))
, _st(st) {
	AddComponents(Info::Bit());

	setDocumentLinks(_data);
	_data->thumb->load(parent->fullId());

	updateName();
	const auto dateText = textcmdLink(
		1,
		TextUtilities::EscapeForRichParsing(
			langDateTime(ParseDateTime(_data->date))));
	TextParseOptions opts = { TextParseRichText, 0, 0, Qt::LayoutDirectionAuto };
	_details.setText(
		st::defaultTextStyle,
		lng_date_and_duration(
			lt_date,
			dateText,
			lt_duration,
			formatDurationText(duration())),
		opts);
	_details.setLink(1, goToMessageClickHandler(parent));
}

void Voice::initDimensions() {
	_maxw = _st.maxWidth;
	_minh = _st.songPadding.top() + _st.songThumbSize + _st.songPadding.bottom() + st::lineWidth;
}

void Voice::paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) {
	bool selected = (selection == FullSelection);

	_data->automaticLoad(parent()->fullId(), parent());
	bool loaded = _data->loaded(), displayLoading = _data->displayLoading();

	if (displayLoading) {
		ensureRadial();
		if (!_radial->animating()) {
			_radial->start(_data->progress());
		}
	}
	bool showPause = updateStatusText();
	int32 nameVersion = parent()->fromOriginal()->nameVersion;
	if (nameVersion > _nameVersion) {
		updateName();
	}
	bool radial = isRadialAnimation(context->ms);

	const auto nameleft = _st.songPadding.left()
		+ _st.songThumbSize
		+ _st.songPadding.right();
	const auto nameright = _st.songPadding.left();
	const auto nametop = _st.songNameTop;
	const auto statustop = _st.songStatusTop;
	const auto namewidth = _width - nameleft - nameright;

	const auto inner = rtlrect(
		_st.songPadding.left(),
		_st.songPadding.top(),
		_st.songThumbSize,
		_st.songThumbSize,
		_width);
	if (clip.intersects(inner)) {
		p.setPen(Qt::NoPen);
		const auto drawThumb = !_data->thumb->isNull()
			&& _data->thumb->loaded();
		if (drawThumb) {
			const auto thumb = _data->thumb->pixCircled(
				parent()->fullId(),
				inner.width(),
				inner.height());
			p.drawPixmap(inner.topLeft(), thumb);
		} else if (!_data->thumb->isNull()) {
			PainterHighQualityEnabler hq(p);
			p.setBrush(st::imageBg);
			p.drawEllipse(inner);
		}
		if (selected) {
			p.setBrush(drawThumb ? st::msgDateImgBgSelected : st::msgFileInBgSelected);
		} else if (!_data->thumb->isNull()) {
			auto over = ClickHandler::showAsActive(loaded ? _openl : (_data->loading() ? _cancell : _openl));
			p.setBrush(anim::brush(st::msgDateImgBg, st::msgDateImgBgOver, _a_iconOver.current(context->ms, over ? 1. : 0.)));
		} else {
			auto over = ClickHandler::showAsActive(loaded ? _openl : (_data->loading() ? _cancell : _openl));
			p.setBrush(anim::brush(st::msgFileInBg, st::msgFileInBgOver, _a_iconOver.current(context->ms, over ? 1. : 0.)));
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

		auto icon = [&] {
			if (showPause) {
				return &(selected ? _st.songPauseSelected : _st.songPause);
			} else if (_status.size() < 0 || _status.size() == FileStatusSizeLoaded) {
				return &(selected ? _st.songPlaySelected : _st.songPlay);
			} else if (_data->loading()) {
				return &(selected ? _st.songCancelSelected : _st.songCancel);
			}
			return &(selected ? _st.songDownloadSelected : _st.songDownload);
		}();
		icon->paintInCenter(p, inner);
	}

	if (clip.intersects(rtlrect(nameleft, nametop, namewidth, st::semiboldFont->height, _width))) {
		p.setPen(st::historyFileNameInFg);
		_name.drawLeftElided(p, nameleft, nametop, namewidth, _width);
	}

	if (clip.intersects(rtlrect(nameleft, statustop, namewidth, st::normalFont->height, _width))) {
		p.setFont(st::normalFont);
		p.setPen(selected ? st::mediaInFgSelected : st::mediaInFg);
		int32 unreadx = nameleft;
		if (_status.size() == FileStatusSizeLoaded || _status.size() == FileStatusSizeReady) {
			p.setTextPalette(selected ? st::mediaInPaletteSelected : st::mediaInPalette);
			_details.drawLeftElided(p, nameleft, statustop, namewidth, _width);
			p.restoreTextPalette();
			unreadx += _details.maxWidth();
		} else {
			int32 statusw = st::normalFont->width(_status.text());
			p.drawTextLeft(nameleft, statustop, _width, _status.text(), statusw);
			unreadx += statusw;
		}
		if (parent()->isMediaUnread() && unreadx + st::mediaUnreadSkip + st::mediaUnreadSize <= _width) {
			p.setPen(Qt::NoPen);
			p.setBrush(selected ? st::msgFileInBgSelected : st::msgFileInBg);

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(rtlrect(unreadx + st::mediaUnreadSkip, statustop + st::mediaUnreadTop, st::mediaUnreadSize, st::mediaUnreadSize, _width));
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
	const auto loaded = _data->loaded();

	const auto nameleft = _st.songPadding.left()
		+ _st.songThumbSize
		+ _st.songPadding.right();
	const auto nameright = _st.songPadding.left();
	const auto nametop = _st.songNameTop;
	const auto statustop = _st.songStatusTop;

	const auto inner = rtlrect(
		_st.songPadding.left(),
		_st.songPadding.top(),
		_st.songThumbSize,
		_st.songThumbSize,
		_width);
	if (inner.contains(point)) {
		const auto link = loaded
			? _openl
			: (_data->loading() || _data->uploading())
			? _cancell
			: _openl;
		return { parent(), link };
	}
	auto result = TextState(parent());
	const auto statusmaxwidth = _width - nameleft - nameright;
	const auto statusrect = rtlrect(
		nameleft,
		statustop,
		statusmaxwidth,
		st::normalFont->height,
		_width);
	if (statusrect.contains(point)) {
		if (_status.size() == FileStatusSizeLoaded || _status.size() == FileStatusSizeReady) {
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
	const auto namerect = rtlrect(
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

float64 Voice::dataProgress() const {
	return _data->progress();
}

bool Voice::dataFinished() const {
	return !_data->loading();
}

bool Voice::dataLoaded() const {
	return _data->loaded();
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
			_name.setText(st::semiboldTextStyle, lng_forwarded_channel(lt_channel, App::peerName(parent()->fromOriginal())), Ui::NameTextOptions());
		} else {
			_name.setText(st::semiboldTextStyle, lng_forwarded(lt_user, App::peerName(parent()->fromOriginal())), Ui::NameTextOptions());
		}
	} else {
		_name.setText(st::semiboldTextStyle, App::peerName(parent()->from()), Ui::NameTextOptions());
	}
	version = parent()->fromOriginal()->nameVersion;
	_nameVersion = version;
}

int Voice::duration() const {
	return _data->voice()
		? _data->voice()->duration
		: std::max(_data->duration(), 0);
}

bool Voice::updateStatusText() {
	bool showPause = false;
	int32 statusSize = 0, realDuration = 0;
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = FileStatusSizeFailed;
	} else if (_data->loaded()) {
		statusSize = FileStatusSizeLoaded;
		using State = Media::Player::State;
		auto state = Media::Player::mixer()->currentState(AudioMsgId::Type::Voice);
		if (state.id == AudioMsgId(_data, parent()->fullId(), state.id.playId()) && !Media::Player::IsStoppedOrStopping(state.state)) {
			statusSize = -1 - (state.position / state.frequency);
			realDuration = (state.length / state.frequency);
			showPause = (state.state == State::Playing || state.state == State::Resuming || state.state == State::Starting);
		}
	} else {
		statusSize = FileStatusSizeReady;
	}
	if (statusSize != _status.size()) {
		_status.update(statusSize, _data->size, duration(), realDuration);
	}
	return showPause;
}

Document::Document(
	not_null<HistoryItem*> parent,
	not_null<DocumentData*> document,
	const style::OverviewFileLayout &st)
: RadialProgressItem(parent)
, _data(document)
, _msgl(goToMessageClickHandler(parent))
, _namel(std::make_shared<DocumentOpenClickHandler>(_data, parent->fullId()))
, _st(st)
, _date(langDateTime(ParseDateTime(_data->date)))
, _datew(st::normalFont->width(_date))
, _colorIndex(documentColorIndex(_data, _ext)) {
	_name.setMarkedText(st::defaultTextStyle, ComposeNameWithEntities(_data), _documentNameOptions);

	AddComponents(Info::Bit());

	setDocumentLinks(_data);

	_status.update(FileStatusSizeReady, _data->size, _data->isSong() ? _data->song()->duration : -1, 0);

	if (withThumb()) {
		_data->thumb->load(parent->fullId());
		int32 tw = ConvertScale(_data->thumb->width()), th = ConvertScale(_data->thumb->height());
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

void Document::initDimensions() {
	_maxw = _st.maxWidth;
	if (_data->isSong()) {
		_minh = _st.songPadding.top() + _st.songThumbSize + _st.songPadding.bottom();
	} else {
		_minh = _st.filePadding.top() + _st.fileThumbSize + _st.filePadding.bottom() + st::lineWidth;
	}
}

void Document::paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) {
	bool selected = (selection == FullSelection);

	_data->automaticLoad(parent()->fullId(), parent());
	bool loaded = _data->loaded(), displayLoading = _data->displayLoading();

	if (displayLoading) {
		ensureRadial();
		if (!_radial->animating()) {
			_radial->start(_data->progress());
		}
	}
	bool showPause = updateStatusText();
	bool radial = isRadialAnimation(context->ms);

	int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0, datetop = -1;
	bool wthumb = withThumb();

	auto isSong = _data->isSong();
	if (isSong) {
		nameleft = _st.songPadding.left() + _st.songThumbSize + _st.songPadding.right();
		nameright = _st.songPadding.left();
		nametop = _st.songNameTop;
		statustop = _st.songStatusTop;

		auto inner = rtlrect(_st.songPadding.left(), _st.songPadding.top(), _st.songThumbSize, _st.songThumbSize, _width);
		if (clip.intersects(inner)) {
			p.setPen(Qt::NoPen);
			if (selected) {
				p.setBrush(st::msgFileInBgSelected);
			} else {
				auto over = ClickHandler::showAsActive(loaded ? _openl : (_data->loading() ? _cancell : _openl));
				p.setBrush(anim::brush(_st.songIconBg, _st.songOverBg, _a_iconOver.current(context->ms, over ? 1. : 0.)));
			}

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(inner);
			}

			if (radial) {
				auto rinner = inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine));
				auto &bg = selected ? st::historyFileInRadialFgSelected : st::historyFileInRadialFg;
				_radial->draw(p, rinner, st::msgFileRadialLine, bg);
			}

			auto icon = [&] {
				if (showPause) {
					return &(selected ? _st.songPauseSelected : _st.songPause);
				} else if (loaded) {
					return &(selected ? _st.songPlaySelected : _st.songPlay);
				} else if (_data->loading()) {
					return &(selected ? _st.songCancelSelected : _st.songCancel);
				}
				return &(selected ? _st.songDownloadSelected : _st.songDownload);
			}();
			icon->paintInCenter(p, inner);
		}
	} else {
		nameleft = _st.fileThumbSize + _st.filePadding.right();
		nametop = st::linksBorder + _st.fileNameTop;
		statustop = st::linksBorder + _st.fileStatusTop;
		datetop = st::linksBorder + _st.fileDateTop;

		QRect border(rtlrect(nameleft, 0, _width - nameleft, st::linksBorder, _width));
		if (!context->isAfterDate && clip.intersects(border)) {
			p.fillRect(clip.intersected(border), st::linksBorderFg);
		}

		QRect rthumb(rtlrect(0, st::linksBorder + _st.filePadding.top(), _st.fileThumbSize, _st.fileThumbSize, _width));
		if (clip.intersects(rthumb)) {
			if (wthumb) {
				if (_data->thumb->loaded()) {
					if (_thumb.isNull() || loaded != _thumbForLoaded) {
						_thumbForLoaded = loaded;
						auto options = Images::Option::Smooth | Images::Option::None;
						if (!_thumbForLoaded) options |= Images::Option::Blurred;
						_thumb = _data->thumb->pixNoCache(parent()->fullId(), _thumbw * cIntRetinaFactor(), 0, options, _st.fileThumbSize, _st.fileThumbSize);
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
						p.setBrush(anim::brush(wthumb ? st::msgDateImgBg : documentDarkColor(_colorIndex), wthumb ? st::msgDateImgBgOver : documentOverColor(_colorIndex), _a_iconOver.current(context->ms, over ? 1. : 0.)));
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
	if (clip.intersects(rtlrect(nameleft, nametop, namewidth, st::semiboldFont->height, _width))) {
		p.setPen(st::historyFileNameInFg);
		_name.drawLeftElided(p, nameleft, nametop, namewidth, _width);
	}

	if (clip.intersects(rtlrect(nameleft, statustop, availwidth, st::normalFont->height, _width))) {
		p.setFont(st::normalFont);
		p.setPen((isSong && selected) ? st::mediaInFgSelected : st::mediaInFg);
		p.drawTextLeft(nameleft, statustop, _width, _status.text());
	}
	if (datetop >= 0 && clip.intersects(rtlrect(nameleft, datetop, _datew, st::normalFont->height, _width))) {
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

TextState Document::getState(
		QPoint point,
		StateRequest request) const {
	const auto loaded = _data->loaded();
	const auto wthumb = withThumb();

	if (_data->isSong()) {
		const auto nameleft = _st.songPadding.left() + _st.songThumbSize + _st.songPadding.right();
		const auto nameright = _st.songPadding.left();
		const auto namewidth = std::min(
			_width - nameleft - nameright,
			_name.maxWidth());
		const auto nametop = _st.songNameTop;
		const auto statustop = _st.songStatusTop;

		const auto inner = rtlrect(
			_st.songPadding.left(),
			_st.songPadding.top(),
			_st.songThumbSize,
			_st.songThumbSize,
			_width);
		if (inner.contains(point)) {
			const auto link = loaded
				? _openl
				: (_data->loading() || _data->uploading())
				? _cancell
				: _openl;
			return { parent(), link };
		}
		const auto namerect = rtlrect(
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

		const auto rthumb = rtlrect(
			0,
			st::linksBorder + _st.filePadding.top(),
			_st.fileThumbSize,
			_st.fileThumbSize,
			_width);

		if (rthumb.contains(point)) {
			const auto link = loaded
				? _openl
				: (_data->loading() || _data->uploading())
				? _cancell
				: _savel;
			return { parent(), link };
		}

		if (_data->status != FileUploadFailed) {
			auto daterect = rtlrect(
				nameleft,
				datetop,
				_datew,
				st::normalFont->height,
				_width);
			if (daterect.contains(point)) {
				return { parent(), _msgl };
			}
		}
		if (!_data->loading() && _data->isValid()) {
			auto leftofnamerect = rtlrect(
				0,
				st::linksBorder,
				nameleft,
				_height - st::linksBorder,
				_width);
			if (loaded && leftofnamerect.contains(point)) {
				return { parent(), _namel };
			}
			const auto namerect = rtlrect(
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

float64 Document::dataProgress() const {
	return _data->progress();
}

bool Document::dataFinished() const {
	return !_data->loading();
}

bool Document::dataLoaded() const {
	return _data->loaded();
}

bool Document::iconAnimated() const {
	return _data->isSong()
		|| !_data->loaded()
		|| (_radial && _radial->animating());
}

bool Document::withThumb() const {
	return !_data->isSong()
		&& !_data->thumb->isNull()
		&& _data->thumb->width()
		&& _data->thumb->height()
		&& !documentIsExecutableName(_data->filename());
}

bool Document::updateStatusText() {
	bool showPause = false;
	int32 statusSize = 0, realDuration = 0;
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = FileStatusSizeFailed;
	} else if (_data->uploading()) {
		statusSize = _data->uploadingData->offset;
	} else if (_data->loading()) {
		statusSize = _data->loadOffset();
	} else if (_data->loaded()) {
		if (_data->isSong()) {
			statusSize = FileStatusSizeLoaded;
			using State = Media::Player::State;
			auto state = Media::Player::mixer()->currentState(AudioMsgId::Type::Song);
			if (state.id == AudioMsgId(_data, parent()->fullId()) && !Media::Player::IsStoppedOrStopping(state.state)) {
				statusSize = -1 - (state.position / state.frequency);
				realDuration = (state.length / state.frequency);
				showPause = (state.state == State::Playing || state.state == State::Resuming || state.state == State::Starting);
			}
			if (!showPause && (state.id == AudioMsgId(_data, parent()->fullId())) && Media::Player::instance()->isSeeking(AudioMsgId::Type::Song)) {
				showPause = true;
			}
		} else {
			statusSize = FileStatusSizeLoaded;
		}
	} else {
		statusSize = FileStatusSizeReady;
	}
	if (statusSize != _status.size()) {
		_status.update(statusSize, _data->size, _data->isSong() ? _data->song()->duration : -1, realDuration);
	}
	return showPause;
}

Link::Link(
	not_null<HistoryItem*> parent,
	Data::Media *media)
: ItemBase(parent) {
	AddComponents(Info::Bit());

	auto textWithEntities = parent->originalText();
	QString mainUrl;

	auto text = textWithEntities.text;
	const auto &entities = textWithEntities.entities;
	int32 from = 0, till = text.size(), lnk = entities.size();
	for (const auto &entity : entities) {
		auto type = entity.type();
		if (type != EntityInTextUrl && type != EntityInTextCustomUrl && type != EntityInTextEmail) {
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
		if (type != EntityInTextUrl && type != EntityInTextCustomUrl && type != EntityInTextEmail) {
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

	_page = media ? media->webpage() : nullptr;
	if (_page) {
		mainUrl = _page->url;
		if (_page->document) {
			_photol = std::make_shared<DocumentOpenClickHandler>(
				_page->document,
				parent->fullId());
		} else if (_page->photo) {
			if (_page->type == WebPageType::Profile || _page->type == WebPageType::Video) {
				_photol = std::make_shared<UrlClickHandler>(_page->url);
			} else if (_page->type == WebPageType::Photo
				|| _page->siteName == qstr("Twitter")
				|| _page->siteName == qstr("Facebook")) {
				_photol = std::make_shared<PhotoOpenClickHandler>(
					_page->photo,
					parent->fullId());
			} else {
				_photol = std::make_shared<UrlClickHandler>(_page->url);
			}
		} else {
			_photol = std::make_shared<UrlClickHandler>(_page->url);
		}
	} else if (!mainUrl.isEmpty()) {
		_photol = std::make_shared<UrlClickHandler>(mainUrl);
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
		if (!_page->photo->loaded()) {
			_page->photo->thumb->load(parent->fullId(), false, false);
		}

		tw = ConvertScale(_page->photo->thumb->width());
		th = ConvertScale(_page->photo->thumb->height());
	} else if (_page && _page->document) {
		if (!_page->document->thumb->loaded()) {
			_page->document->thumb->load(parent->fullId(), false, false);
		}

		tw = ConvertScale(_page->document->thumb->width());
		th = ConvertScale(_page->document->thumb->height());
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

		parts = domain.split('@').back().split('.');
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
	for (int32 i = 0, l = _links.size(); i < l; ++i) {
		_links.at(i).lnk->setFullDisplayed(w >= _links.at(i).width);
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
	if (clip.intersects(rtlrect(0, pixTop, st::linksPhotoSize, st::linksPhotoSize, _width))) {
		if (_page && _page->photo) {
			QPixmap pix;
			if (_page->photo->medium->loaded()) {
				pix = _page->photo->medium->pixSingle(parent()->fullId(), _pixw, _pixh, st::linksPhotoSize, st::linksPhotoSize, ImageRoundRadius::Small);
			} else if (_page->photo->loaded()) {
				pix = _page->photo->full->pixSingle(parent()->fullId(), _pixw, _pixh, st::linksPhotoSize, st::linksPhotoSize, ImageRoundRadius::Small);
			} else {
				pix = _page->photo->thumb->pixSingle(parent()->fullId(), _pixw, _pixh, st::linksPhotoSize, st::linksPhotoSize, ImageRoundRadius::Small);
			}
			p.drawPixmapLeft(pixLeft, pixTop, _width, pix);
		} else if (_page && _page->document && !_page->document->thumb->isNull()) {
			auto roundRadius = _page->document->isVideoMessage()
				? ImageRoundRadius::Ellipse
				: ImageRoundRadius::Small;
			p.drawPixmapLeft(pixLeft, pixTop, _width, _page->document->thumb->pixSingle(parent()->fullId(), _pixw, _pixh, st::linksPhotoSize, st::linksPhotoSize, roundRadius));
		} else {
			const auto index = _letter.isEmpty()
				? 0
				: (_letter[0].unicode() % 4);
			const auto fill = [&](style::color color, RoundCorners corners) {
				auto pixRect = rtlrect(
					pixLeft,
					pixTop,
					st::linksPhotoSize,
					st::linksPhotoSize,
					_width);
				App::roundRect(p, pixRect, color, corners);
			};
			switch (index) {
			case 0: fill(st::msgFile1Bg, Doc1Corners); break;
			case 1: fill(st::msgFile2Bg, Doc2Corners); break;
			case 2: fill(st::msgFile3Bg, Doc3Corners); break;
			case 3: fill(st::msgFile4Bg, Doc4Corners); break;
			}

			if (!_letter.isEmpty()) {
				p.setFont(st::linksLetterFont);
				p.setPen(st::linksLetterFg);
				p.drawText(rtlrect(pixLeft, pixTop, st::linksPhotoSize, st::linksPhotoSize, _width), _letter, style::al_center);
			}
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
		if (clip.intersects(rtlrect(left, top, qMin(w, _titlew), st::semiboldFont->height, _width))) {
			p.drawTextLeft(left, top, _width, (w < _titlew) ? st::semiboldFont->elided(_title, w) : _title);
		}
		top += st::semiboldFont->height;
	}
	p.setFont(st::msgFont);
	if (!_text.isEmpty()) {
		int32 h = qMin(st::normalFont->height * 3, _text.countHeight(w));
		if (clip.intersects(rtlrect(left, top, w, h, _width))) {
			_text.drawLeftElided(p, left, top, w, _width, 3);
		}
		top += h;
	}

	p.setPen(st::windowActiveTextFg);
	for (int32 i = 0, l = _links.size(); i < l; ++i) {
		if (clip.intersects(rtlrect(left, top, qMin(w, _links.at(i).width), st::normalFont->height, _width))) {
			p.setFont(ClickHandler::showAsActive(_links.at(i).lnk) ? st::normalFont->underline() : st::normalFont);
			p.drawTextLeft(left, top, _width, (w < _links.at(i).width) ? st::normalFont->elided(_links.at(i).text, w) : _links.at(i).text);
		}
		top += st::normalFont->height;
	}

	QRect border(rtlrect(left, 0, w, st::linksBorder, _width));
	if (!context->isAfterDate && clip.intersects(border)) {
		p.fillRect(clip.intersected(border), st::linksBorderFg);
	}

	const auto checkDelta = st::linksPhotoSize + st::overviewCheckSkip
		- st::overviewSmallCheck.size;
	const auto checkLeft = pixLeft + checkDelta;
	const auto checkTop = pixTop + checkDelta;
	paintCheckbox(p, { checkLeft, checkTop }, selected, context);
}

TextState Link::getState(
		QPoint point,
		StateRequest request) const {
	int32 left = st::linksPhotoSize + st::linksPhotoPadding, top = st::linksMargin.top() + st::linksBorder, w = _width - left;
	if (rtlrect(0, top, st::linksPhotoSize, st::linksPhotoSize, _width).contains(point)) {
		return { parent(), _photol };
	}

	if (!_title.isEmpty() && _text.isEmpty() && _links.size() == 1) {
		top += (st::linksPhotoSize - st::semiboldFont->height - st::normalFont->height) / 2;
	}
	if (!_title.isEmpty()) {
		if (rtlrect(left, top, qMin(w, _titlew), st::semiboldFont->height, _width).contains(point)) {
			return { parent(), _photol };
		}
		top += st::webPageTitleFont->height;
	}
	if (!_text.isEmpty()) {
		top += qMin(st::normalFont->height * 3, _text.countHeight(w));
	}
	for (int32 i = 0, l = _links.size(); i < l; ++i) {
		if (rtlrect(left, top, qMin(w, _links.at(i).width), st::normalFont->height, _width).contains(point)) {
			return { parent(), ClickHandlerPtr(_links[i].lnk) };
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
, lnk(std::make_shared<UrlClickHandler>(url)) {
}

} // namespace Layout
} // namespace Overview
