/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "overview/overview_layout.h"

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
#include "history/history_media_types.h"
#include "ui/effects/round_checkbox.h"

namespace Overview {
namespace Layout {
namespace {

TextParseOptions _documentNameOptions = {
	TextParseMultiline | TextParseRichText | TextParseLinks | TextParseMarkdown, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

TextWithEntities ComposeNameWithEntities(DocumentData *document) {
	TextWithEntities result;
	auto song = document->song();
	if (!song || (song->title.isEmpty() && song->performer.isEmpty())) {
		result.text = document->name.isEmpty() ? qsl("Unknown File") : document->name;
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

void ItemBase::clickHandlerActiveChanged(const ClickHandlerPtr &action, bool active) {
	App::hoveredLinkItem(active ? _parent : nullptr);
	Ui::repaintHistoryItem(_parent);
}

void ItemBase::clickHandlerPressedChanged(const ClickHandlerPtr &action, bool pressed) {
	App::pressedLinkItem(pressed ? _parent : nullptr);
	Ui::repaintHistoryItem(_parent);
}

void RadialProgressItem::clickHandlerActiveChanged(const ClickHandlerPtr &action, bool active) {
	ItemBase::clickHandlerActiveChanged(action, active);
	if (action == _openl || action == _savel || action == _cancell) {
		if (iconAnimated()) {
			_a_iconOver.start([this] { Ui::repaintHistoryItem(_parent); }, active ? 0. : 1., active ? 1. : 0., st::msgFileOverDuration);
		}
	}
}

void RadialProgressItem::setLinks(ClickHandlerPtr &&openl, ClickHandlerPtr &&savel, ClickHandlerPtr &&cancell) {
	_openl = std::move(openl);
	_savel = std::move(savel);
	_cancell = std::move(cancell);
}

void RadialProgressItem::step_radial(TimeMs ms, bool timer) {
	if (timer) {
		Ui::repaintHistoryItem(_parent);
	} else {
		_radial->update(dataProgress(), dataFinished(), ms);
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

class PhotoVideoCheckbox {
public:
	template <typename UpdateCallback>
	PhotoVideoCheckbox(UpdateCallback callback) : _updateCallback(callback), _check(st::overviewCheck, _updateCallback) {
	}

	void paint(Painter &p, TimeMs ms, int width, int height, bool selected, bool selecting);

	void setActive(bool active);
	void setPressed(bool pressed);

	void invalidateCache() {
		_check.invalidateCache();
	}

private:
	void startAnimation();

	base::lambda<void()> _updateCallback;
	Ui::RoundCheckbox _check;

	Animation _pression;
	bool _active = false;
	bool _pressed = false;

};

void PhotoVideoCheckbox::paint(Painter &p, TimeMs ms, int width, int height, bool selected, bool selecting) {
	if (selected) {
		p.fillRect(0, 0, width, height, st::overviewPhotoSelectOverlay);
	}
	_check.setDisplayInactive(selecting);
	_check.setChecked(selected);
	auto pression = _pression.current(ms, (_active && _pressed) ? 1. : 0.);
	auto masterScale = 1. - (1. - st::overviewCheckPressedSize) * pression;
	_check.paint(p, ms, width - st::overviewCheckSkip - st::overviewCheck.size, height - st::overviewCheckSkip - st::overviewCheck.size, width, masterScale);
}

void PhotoVideoCheckbox::setActive(bool active) {
	_active = active;
	if (_pressed) {
		startAnimation();
	}
}

void PhotoVideoCheckbox::setPressed(bool pressed) {
	_pressed = pressed;
	if (_active) {
		startAnimation();
	}
}

void PhotoVideoCheckbox::startAnimation() {
	auto showPressed = (_pressed && _active);
	_pression.start(_updateCallback, showPressed ? 0. : 1., showPressed ? 1. : 0., st::overviewCheck.duration);
}

Photo::Photo(PhotoData *photo, HistoryItem *parent) : ItemBase(parent)
, _data(photo)
, _link(new PhotoOpenClickHandler(photo)) {
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
		_data->medium->automaticLoad(_parent);
		good = _data->medium->loaded();
	}
	if ((good && !_goodLoaded) || _pix.width() != _width * cIntRetinaFactor()) {
		_goodLoaded = good;

		int32 size = _width * cIntRetinaFactor();
		if (_goodLoaded || _data->thumb->loaded()) {
			auto img = (_data->loaded() ? _data->full : (_data->medium->loaded() ? _data->medium : _data->thumb))->pix().toImage();
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
			_data->forget();

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

	if (selected || context->selecting) {
		ensureCheckboxCreated();
	}
	if (_check) {
		_check->paint(p, context->ms, _width, _height, selected, context->selecting);
	}
}

void Photo::ensureCheckboxCreated() {
	if (!_check) _check = std::make_unique<PhotoVideoCheckbox>([this] {
		Ui::repaintHistoryItem(_parent);
	});
}

void Photo::getState(ClickHandlerPtr &link, HistoryCursorState &cursor, QPoint point) const {
	if (hasPoint(point)) {
		link = _link;
	}
}

void Photo::clickHandlerActiveChanged(const ClickHandlerPtr &action, bool active) {
	ItemBase::clickHandlerActiveChanged(action, active);
	if (_check) {
		_check->setActive(active);
	}
}

void Photo::clickHandlerPressedChanged(const ClickHandlerPtr &action, bool pressed) {
	ItemBase::clickHandlerPressedChanged(action, pressed);
	if (_check) {
		_check->setPressed(pressed);
	}
}

void Photo::invalidateCache() {
	if (_check) {
		_check->invalidateCache();
	}
}

Video::Video(DocumentData *video, HistoryItem *parent) : RadialProgressItem(parent)
, _data(video)
, _duration(formatDurationText(_data->duration()))
, _thumbLoaded(false) {
	setDocumentLinks(_data);
}

void Video::initDimensions() {
	_maxw = 2 * st::minPhotoSize;
	_minh = _maxw;
}

int32 Video::resizeGetHeight(int32 width) {
	_width = qMin(width, _maxw);
	_height = _width;
	return _height;
}

void Video::paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) {
	bool selected = (selection == FullSelection), thumbLoaded = _data->thumb->loaded();

	_data->automaticLoad(_parent);
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
			auto img = Images::prepareBlur(_data->thumb->pix().toImage());
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
			_data->forget();

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

	if (selected || context->selecting) {
		ensureCheckboxCreated();
	}
	if (_check) {
		_check->paint(p, context->ms, _width, _height, selected, context->selecting);
	}
}

void Video::ensureCheckboxCreated() {
	if (!_check) _check = std::make_unique<PhotoVideoCheckbox>([this] {
		Ui::repaintHistoryItem(_parent);
	});
}

void Video::clickHandlerActiveChanged(const ClickHandlerPtr &action, bool active) {
	RadialProgressItem::clickHandlerActiveChanged(action, active);
	if (_check) {
		_check->setActive(active);
	}
}

void Video::clickHandlerPressedChanged(const ClickHandlerPtr &action, bool pressed) {
	RadialProgressItem::clickHandlerPressedChanged(action, pressed);
	if (_check) {
		_check->setPressed(pressed);
	}
}

void Video::invalidateCache() {
	if (_check) {
		_check->invalidateCache();
	}
}

void Video::getState(ClickHandlerPtr &link, HistoryCursorState &cursor, QPoint point) const {
	bool loaded = _data->loaded();

	if (hasPoint(point)) {
		link = loaded ? _openl : (_data->loading() ? _cancell : _savel);
	}
}

void Video::updateStatusText() {
	bool showPause = false;
	int statusSize = 0;
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = FileStatusSizeFailed;
	} else if (_data->status == FileUploading) {
		statusSize = _data->uploadOffset;
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

Voice::Voice(DocumentData *voice, HistoryItem *parent, const style::OverviewFileLayout &st) : RadialProgressItem(parent)
, _data(voice)
, _namel(new DocumentOpenClickHandler(_data))
, _st(st) {
	AddComponents(Info::Bit());

	Assert(_data->voice() != 0);

	setDocumentLinks(_data);

	updateName();
	QString d = textcmdLink(1, TextUtilities::EscapeForRichParsing(langDateTime(date(_data->date))));
	TextParseOptions opts = { TextParseRichText, 0, 0, Qt::LayoutDirectionAuto };
	_details.setText(st::defaultTextStyle, lng_date_and_duration(lt_date, d, lt_duration, formatDurationText(_data->voice()->duration)), opts);
	_details.setLink(1, goToMessageClickHandler(parent));
}

void Voice::initDimensions() {
	_maxw = _st.maxWidth;
	_minh = _st.songPadding.top() + _st.songThumbSize + _st.songPadding.bottom() + st::lineWidth;
}

void Voice::paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) {
	bool selected = (selection == FullSelection);

	_data->automaticLoad(_parent);
	bool loaded = _data->loaded(), displayLoading = _data->displayLoading();

	if (displayLoading) {
		ensureRadial();
		if (!_radial->animating()) {
			_radial->start(_data->progress());
		}
	}
	bool showPause = updateStatusText();
	int32 nameVersion = _parent->fromOriginal()->nameVersion;
	if (nameVersion > _nameVersion) {
		updateName();
	}
	bool radial = isRadialAnimation(context->ms);

	int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0, datetop = -1;

	nameleft = _st.songPadding.left() + _st.songThumbSize + _st.songPadding.right();
	nameright = _st.songPadding.left();
	nametop = _st.songNameTop;
	statustop = _st.songStatusTop;

	if (selected) {
		p.fillRect(clip.intersected(QRect(0, 0, _width, _height)), st::msgInBgSelected);
	}

	QRect inner(rtlrect(_st.songPadding.left(), _st.songPadding.top(), _st.songThumbSize, _st.songThumbSize, _width));
	if (clip.intersects(inner)) {
		p.setPen(Qt::NoPen);
		if (selected) {
			p.setBrush(st::msgFileInBgSelected);
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
			auto &bg = selected ? st::msgInBgSelected : st::msgInBg;
			_radial->draw(p, rinner, st::msgFileRadialLine, bg);
		}

		auto icon = ([showPause, this, selected] {
			if (showPause) {
				return &(selected ? _st.songPauseSelected : _st.songPause);
			} else if (_status.size() < 0 || _status.size() == FileStatusSizeLoaded) {
				return &(selected ? _st.songPlaySelected : _st.songPlay);
			} else if (_data->loading()) {
				return &(selected ? _st.songCancelSelected : _st.songCancel);
			}
			return &(selected ? _st.songDownloadSelected : _st.songDownload);
		})();
		icon->paintInCenter(p, inner);
	}

	int32 namewidth = _width - nameleft - nameright;

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
		if (_parent->isMediaUnread() && unreadx + st::mediaUnreadSkip + st::mediaUnreadSize <= _width) {
			p.setPen(Qt::NoPen);
			p.setBrush(selected ? st::msgFileInBgSelected : st::msgFileInBg);

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(rtlrect(unreadx + st::mediaUnreadSkip, statustop + st::mediaUnreadTop, st::mediaUnreadSize, st::mediaUnreadSize, _width));
			}
		}
	}
}

void Voice::getState(ClickHandlerPtr &link, HistoryCursorState &cursor, QPoint point) const {
	bool loaded = _data->loaded();

	int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0, datetop = 0;

	nameleft = _st.songPadding.left() + _st.songThumbSize + _st.songPadding.right();
	nameright = _st.songPadding.left();
	nametop = _st.songNameTop;
	statustop = _st.songStatusTop;

	auto inner = rtlrect(_st.songPadding.left(), _st.songPadding.top(), _st.songThumbSize, _st.songThumbSize, _width);
	if (inner.contains(point)) {
		link = loaded ? _openl : ((_data->loading() || _data->status == FileUploading) ? _cancell : _openl);
		return;
	}
	if (rtlrect(nameleft, statustop, _width - nameleft - nameright, st::normalFont->height, _width).contains(point)) {
		if (_status.size() == FileStatusSizeLoaded || _status.size() == FileStatusSizeReady) {
			auto textState = _details.getStateLeft(point - QPoint(nameleft, statustop), _width, _width);
			link = textState.link;
			cursor = textState.uponSymbol ? HistoryInTextCursorState : HistoryDefaultCursorState;
		}
	}
	if (hasPoint(point) && !link && !_data->loading()) {
		link = _namel;
		return;
	}
}

void Voice::updateName() {
	auto version = 0;
	if (auto forwarded = _parent->Get<HistoryMessageForwarded>()) {
		if (_parent->fromOriginal()->isChannel()) {
			_name.setText(st::semiboldTextStyle, lng_forwarded_channel(lt_channel, App::peerName(_parent->fromOriginal())), _textNameOptions);
		} else {
			_name.setText(st::semiboldTextStyle, lng_forwarded(lt_user, App::peerName(_parent->fromOriginal())), _textNameOptions);
		}
	} else {
		_name.setText(st::semiboldTextStyle, App::peerName(_parent->from()), _textNameOptions);
	}
	version = _parent->fromOriginal()->nameVersion;
	_nameVersion = version;
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
		if (state.id == AudioMsgId(_data, _parent->fullId()) && !Media::Player::IsStoppedOrStopping(state.state)) {
			statusSize = -1 - (state.position / state.frequency);
			realDuration = (state.length / state.frequency);
			showPause = (state.state == State::Playing || state.state == State::Resuming || state.state == State::Starting);
		}
	} else {
		statusSize = FileStatusSizeReady;
	}
	if (statusSize != _status.size()) {
		_status.update(statusSize, _data->size, _data->voice()->duration, realDuration);
	}
	return showPause;
}

Document::Document(DocumentData *document, HistoryItem *parent, const style::OverviewFileLayout &st) : RadialProgressItem(parent)
, _data(document)
, _msgl(goToMessageClickHandler(parent))
, _namel(new DocumentOpenClickHandler(_data))
, _st(st)
, _date(langDateTime(date(_data->date)))
, _datew(st::normalFont->width(_date))
, _colorIndex(documentColorIndex(_data, _ext)) {
	_name.setMarkedText(st::defaultTextStyle, ComposeNameWithEntities(_data), _documentNameOptions);

	AddComponents(Info::Bit());

	setDocumentLinks(_data);

	_status.update(FileStatusSizeReady, _data->size, _data->song() ? _data->song()->duration : -1, 0);

	if (withThumb()) {
		_data->thumb->load();
		int32 tw = convertScale(_data->thumb->width()), th = convertScale(_data->thumb->height());
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
	if (_data->song()) {
		_minh = _st.songPadding.top() + _st.songThumbSize + _st.songPadding.bottom();
	} else {
		_minh = _st.filePadding.top() + _st.fileThumbSize + _st.filePadding.bottom() + st::lineWidth;
	}
}

void Document::paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) {
	bool selected = (selection == FullSelection);

	_data->automaticLoad(_parent);
	bool loaded = _data->loaded() || Local::willStickerImageLoad(_data->mediaKey()), displayLoading = _data->displayLoading();

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

	auto isSong = (_data->song() != nullptr);
	if (isSong) {
		nameleft = _st.songPadding.left() + _st.songThumbSize + _st.songPadding.right();
		nameright = _st.songPadding.left();
		nametop = _st.songNameTop;
		statustop = _st.songStatusTop;

		if (selected) {
			p.fillRect(QRect(0, 0, _width, _height), st::msgInBgSelected);
		}

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
				auto &bg = selected ? st::msgInBgSelected : st::msgInBg;
				_radial->draw(p, rinner, st::msgFileRadialLine, bg);
			}

			auto icon = ([showPause, loaded, this, selected] {
				if (showPause) {
					return &(selected ? _st.songPauseSelected : _st.songPause);
				} else if (loaded) {
					return &(selected ? _st.songPlaySelected : _st.songPlay);
				} else if (_data->loading()) {
					return &(selected ? _st.songCancelSelected : _st.songCancel);
				}
				return &(selected ? _st.songDownloadSelected : _st.songDownload);
			})();
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
						_thumb = _data->thumb->pixNoCache(_thumbw * cIntRetinaFactor(), 0, options, _st.fileThumbSize, _st.fileThumbSize);
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
			if (selected || context->selecting) {
				QRect check(rthumb.topLeft() + QPoint(rtl() ? 0 : (rthumb.width() - st::defaultCheck.diameter), rthumb.height() - st::defaultCheck.diameter), QSize(st::defaultCheck.diameter, st::defaultCheck.diameter));
				p.fillRect(check, selected ? st::overviewFileChecked : st::overviewFileCheck);
				st::defaultCheck.icon.paint(p, QPoint(rthumb.width() - st::defaultCheck.diameter, rthumb.y() + rthumb.height() - st::defaultCheck.diameter), _width);
			}
		}
	}

	int availwidth = _width - nameleft - nameright;
	int namewidth = qMin(availwidth, _name.maxWidth());
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
}

void Document::getState(ClickHandlerPtr &link, HistoryCursorState &cursor, QPoint point) const {
	bool loaded = _data->loaded() || Local::willStickerImageLoad(_data->mediaKey());

	int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0, datetop = 0;
	bool wthumb = withThumb();

	if (_data->song()) {
		nameleft = _st.songPadding.left() + _st.songThumbSize + _st.songPadding.right();
		nameright = _st.songPadding.left();
		nametop = _st.songNameTop;
		statustop = _st.songStatusTop;

		auto inner = rtlrect(_st.songPadding.left(), _st.songPadding.top(), _st.songThumbSize, _st.songThumbSize, _width);
		if (inner.contains(point)) {
			link = loaded ? _openl : ((_data->loading() || _data->status == FileUploading) ? _cancell : _openl);
			return;
		}
		if (hasPoint(point) && !_data->loading()) {
			link = _namel;
			return;
		}
	} else {
		nameleft = _st.fileThumbSize + _st.filePadding.right();
		nametop = st::linksBorder + _st.fileNameTop;
		statustop = st::linksBorder + _st.fileStatusTop;
		datetop = st::linksBorder + _st.fileDateTop;

		auto rthumb = rtlrect(0, st::linksBorder + _st.filePadding.top(), _st.fileThumbSize, _st.fileThumbSize, _width);

		if (rthumb.contains(point)) {
			link = loaded ? _openl : ((_data->loading() || _data->status == FileUploading) ? _cancell : _savel);
			return;
		}

		if (_data->status != FileUploadFailed) {
			if (rtlrect(nameleft, datetop, _datew, st::normalFont->height, _width).contains(point)) {
				link = _msgl;
				return;
			}
		}
		if (!_data->loading() && _data->isValid()) {
			if (loaded && rtlrect(0, st::linksBorder, nameleft, _height - st::linksBorder, _width).contains(point)) {
				link = _namel;
				return;
			}
			if (rtlrect(nameleft, nametop, qMin(_width - nameleft - nameright, _name.maxWidth()), st::semiboldFont->height, _width).contains(point)) {
				link = _namel;
				return;
			}
		}
	}
}

bool Document::updateStatusText() {
	bool showPause = false;
	int32 statusSize = 0, realDuration = 0;
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = FileStatusSizeFailed;
	} else if (_data->status == FileUploading) {
		statusSize = _data->uploadOffset;
	} else if (_data->loading()) {
		statusSize = _data->loadOffset();
	} else if (_data->loaded()) {
		if (_data->song()) {
			statusSize = FileStatusSizeLoaded;
			using State = Media::Player::State;
			auto state = Media::Player::mixer()->currentState(AudioMsgId::Type::Song);
			if (state.id == AudioMsgId(_data, _parent->fullId()) && !Media::Player::IsStoppedOrStopping(state.state)) {
				statusSize = -1 - (state.position / state.frequency);
				realDuration = (state.length / state.frequency);
				showPause = (state.state == State::Playing || state.state == State::Resuming || state.state == State::Starting);
			}
			if (!showPause && (state.id == AudioMsgId(_data, _parent->fullId())) && Media::Player::instance()->isSeeking(AudioMsgId::Type::Song)) {
				showPause = true;
			}
		} else {
			statusSize = FileStatusSizeLoaded;
		}
	} else {
		statusSize = FileStatusSizeReady;
	}
	if (statusSize != _status.size()) {
		_status.update(statusSize, _data->size, _data->song() ? _data->song()->duration : -1, realDuration);
	}
	return showPause;
}

Link::Link(HistoryMedia *media, HistoryItem *parent) : ItemBase(parent) {
	AddComponents(Info::Bit());

	auto textWithEntities = _parent->originalText();
	QString mainUrl;

	auto text = textWithEntities.text;
	auto &entities = textWithEntities.entities;
	int32 from = 0, till = text.size(), lnk = entities.size();
	for_const (auto &entity, entities) {
		auto type = entity.type();
		if (type != EntityInTextUrl && type != EntityInTextCustomUrl && type != EntityInTextEmail) {
			continue;
		}
		auto customUrl = entity.data(), entityText = text.mid(entity.offset(), entity.length());
		auto url = customUrl.isEmpty() ? entityText : customUrl;
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

	_page = (media && media->type() == MediaTypeWebPage) ? static_cast<HistoryWebPage*>(media)->webpage().get() : nullptr;
	if (_page) {
		mainUrl = _page->url;
		if (_page->document) {
			_photol = MakeShared<DocumentOpenClickHandler>(_page->document);
		} else if (_page->photo) {
			if (_page->type == WebPageProfile || _page->type == WebPageVideo) {
				_photol = MakeShared<UrlClickHandler>(_page->url);
			} else if (_page->type == WebPagePhoto || _page->siteName == qstr("Twitter") || _page->siteName == qstr("Facebook")) {
				_photol = MakeShared<PhotoOpenClickHandler>(_page->photo);
			} else {
				_photol = MakeShared<UrlClickHandler>(_page->url);
			}
		} else {
			_photol = MakeShared<UrlClickHandler>(_page->url);
		}
	} else if (!mainUrl.isEmpty()) {
		_photol = MakeShared<UrlClickHandler>(mainUrl);
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
		if (!_page->photo->loaded()) _page->photo->thumb->load(false, false);

		tw = convertScale(_page->photo->thumb->width());
		th = convertScale(_page->photo->thumb->height());
	} else if (_page && _page->document) {
		if (!_page->document->thumb->loaded()) _page->document->thumb->load(false, false);

		tw = convertScale(_page->document->thumb->width());
		th = convertScale(_page->document->thumb->height());
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
	int32 left = st::linksPhotoSize + st::linksPhotoPadding, top = st::linksMargin.top() + st::linksBorder, w = _width - left;
	if (clip.intersects(rtlrect(0, top, st::linksPhotoSize, st::linksPhotoSize, _width))) {
		if (_page && _page->photo) {
			QPixmap pix;
			if (_page->photo->medium->loaded()) {
				pix = _page->photo->medium->pixSingle(_pixw, _pixh, st::linksPhotoSize, st::linksPhotoSize, ImageRoundRadius::Small);
			} else if (_page->photo->loaded()) {
				pix = _page->photo->full->pixSingle(_pixw, _pixh, st::linksPhotoSize, st::linksPhotoSize, ImageRoundRadius::Small);
			} else {
				pix = _page->photo->thumb->pixSingle(_pixw, _pixh, st::linksPhotoSize, st::linksPhotoSize, ImageRoundRadius::Small);
			}
			p.drawPixmapLeft(0, top, _width, pix);
		} else if (_page && _page->document && !_page->document->thumb->isNull()) {
			auto roundRadius = _page->document->isRoundVideo() ? ImageRoundRadius::Ellipse : ImageRoundRadius::Small;
			p.drawPixmapLeft(0, top, _width, _page->document->thumb->pixSingle(_pixw, _pixh, st::linksPhotoSize, st::linksPhotoSize, roundRadius));
		} else {
			int32 index = _letter.isEmpty() ? 0 : (_letter.at(0).unicode() % 4);
			switch (index) {
			case 0: App::roundRect(p, rtlrect(0, top, st::linksPhotoSize, st::linksPhotoSize, _width), st::msgFile1Bg, Doc1Corners); break;
			case 1: App::roundRect(p, rtlrect(0, top, st::linksPhotoSize, st::linksPhotoSize, _width), st::msgFile2Bg, Doc2Corners); break;
			case 2: App::roundRect(p, rtlrect(0, top, st::linksPhotoSize, st::linksPhotoSize, _width), st::msgFile3Bg, Doc3Corners); break;
			case 3: App::roundRect(p, rtlrect(0, top, st::linksPhotoSize, st::linksPhotoSize, _width), st::msgFile4Bg, Doc4Corners); break;
			}

			if (!_letter.isEmpty()) {
				p.setFont(st::linksLetterFont);
				p.setPen(st::linksLetterFg);
				p.drawText(rtlrect(0, top, st::linksPhotoSize, st::linksPhotoSize, _width), _letter, style::al_center);
			}
		}

		if (selection == FullSelection) {
			App::roundRect(p, rtlrect(0, top, st::linksPhotoSize, st::linksPhotoSize, _width), st::overviewPhotoSelectOverlay, PhotoSelectOverlayCorners);
			st::overviewLinksChecked.paint(p, QPoint(st::linksPhotoSize - st::overviewLinksChecked.width(), top + st::linksPhotoSize - st::overviewLinksChecked.height()), _width);
		} else if (context->selecting) {
			st::overviewLinksCheck.paint(p, QPoint(st::linksPhotoSize - st::overviewLinksCheck.width(), top + st::linksPhotoSize - st::overviewLinksCheck.height()), _width);
		}
	}

	if (!_title.isEmpty() && _text.isEmpty() && _links.size() == 1) {
		top += (st::linksPhotoSize - st::semiboldFont->height - st::normalFont->height) / 2;
	} else {
		top = st::linksTextTop;
	}

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
}

void Link::getState(ClickHandlerPtr &link, HistoryCursorState &cursor, QPoint point) const {
	int32 left = st::linksPhotoSize + st::linksPhotoPadding, top = st::linksMargin.top() + st::linksBorder, w = _width - left;
	if (rtlrect(0, top, st::linksPhotoSize, st::linksPhotoSize, _width).contains(point)) {
		link = _photol;
		return;
	}

	if (!_title.isEmpty() && _text.isEmpty() && _links.size() == 1) {
		top += (st::linksPhotoSize - st::semiboldFont->height - st::normalFont->height) / 2;
	}
	if (!_title.isEmpty()) {
		if (rtlrect(left, top, qMin(w, _titlew), st::semiboldFont->height, _width).contains(point)) {
			link = _photol;
			return;
		}
		top += st::webPageTitleFont->height;
	}
	if (!_text.isEmpty()) {
		top += qMin(st::normalFont->height * 3, _text.countHeight(w));
	}
	for (int32 i = 0, l = _links.size(); i < l; ++i) {
		if (rtlrect(left, top, qMin(w, _links.at(i).width), st::normalFont->height, _width).contains(point)) {
			link = _links.at(i).lnk;
			return;
		}
		top += st::normalFont->height;
	}
}

Link::LinkEntry::LinkEntry(const QString &url, const QString &text)
: text(text)
, width(st::normalFont->width(text))
, lnk(MakeShared<UrlClickHandler>(url)) {
}

} // namespace Layout
} // namespace Overview
