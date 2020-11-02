/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_document.h"

#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "history/history_item_components.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/media/history_view_media_common.h"
#include "ui/image/image.h"
#include "ui/text/format_values.h"
#include "ui/cached_round_corners.h"
#include "layout.h" // FullSelection
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_media_types.h"
#include "data/data_file_origin.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

constexpr auto kAudioVoiceMsgUpdateView = crl::time(100);

[[nodiscard]] QString CleanTagSymbols(const QString &value) {
	auto result = QString();
	const auto begin = value.begin(), end = value.end();
	auto from = begin;
	for (auto ch = begin; ch != end; ++ch) {
		if (ch->isHighSurrogate()
			&& (ch + 1) != end
			&& (ch + 1)->isLowSurrogate()
			&& QChar::surrogateToUcs4(
				ch->unicode(),
				(ch + 1)->unicode()) >= 0xe0000) {
			if (ch > from) {
				if (result.isEmpty()) {
					result.reserve(value.size());
				}
				result.append(from, ch - from);
			}
			++ch;
			from = ch + 1;
		}
	}
	if (from == begin) {
		return value;
	} else if (end > from) {
		result.append(from, end - from);
	}
	return result;
}

} // namespace

Document::Document(
	not_null<Element*> parent,
	not_null<HistoryItem*> realParent,
	not_null<DocumentData*> document)
: File(parent, realParent)
, _data(document) {
	const auto item = parent->data();
	auto caption = createCaption();

	createComponents(!caption.isEmpty());
	if (const auto named = Get<HistoryDocumentNamed>()) {
		fillNamedFromData(named);
	}

	setDocumentLinks(_data, realParent);

	setStatusSize(Ui::FileStatusSizeReady);

	if (const auto captioned = Get<HistoryDocumentCaptioned>()) {
		captioned->_caption = std::move(caption);
	}
}

Document::~Document() {
	if (_dataMedia) {
		_data->owner().keepAlive(base::take(_dataMedia));
		_parent->checkHeavyPart();
	}
}

float64 Document::dataProgress() const {
	ensureDataMediaCreated();
	return _dataMedia->progress();
}

bool Document::dataFinished() const {
	return !_data->loading()
		&& (!_data->uploading() || _data->waitingForAlbum());
}

bool Document::dataLoaded() const {
	ensureDataMediaCreated();
	return _dataMedia->loaded();
}

void Document::createComponents(bool caption) {
	uint64 mask = 0;
	if (_data->isVoiceMessage()) {
		mask |= HistoryDocumentVoice::Bit();
	} else {
		mask |= HistoryDocumentNamed::Bit();
		if (_data->hasThumbnail()) {
			if (!_data->isSong()
				&& !Data::IsExecutableName(_data->filename())) {
				_data->loadThumbnail(_realParent->fullId());
				mask |= HistoryDocumentThumbed::Bit();
			}
		}
	}
	if (caption) {
		mask |= HistoryDocumentCaptioned::Bit();
	}
	UpdateComponents(mask);
	if (const auto thumbed = Get<HistoryDocumentThumbed>()) {
		thumbed->_linksavel = std::make_shared<DocumentSaveClickHandler>(
			_data,
			_parent->data()->fullId());
		thumbed->_linkopenwithl = std::make_shared<DocumentOpenWithClickHandler>(
			_data,
			_parent->data()->fullId());
		thumbed->_linkcancell = std::make_shared<DocumentCancelClickHandler>(
			_data,
			_parent->data()->fullId());
	}
	if (const auto voice = Get<HistoryDocumentVoice>()) {
		voice->_seekl = std::make_shared<VoiceSeekClickHandler>(
			_data,
			_parent->data()->fullId());
	}
}

void Document::fillNamedFromData(HistoryDocumentNamed *named) {
	const auto nameString = named->_name = CleanTagSymbols(
		_data->composeNameString());
	named->_namew = st::semiboldFont->width(nameString);
}

QSize Document::countOptimalSize() {
	const auto item = _parent->data();

	auto captioned = Get<HistoryDocumentCaptioned>();
	if (_parent->media() != this && !_realParent->groupId()) {
		if (captioned) {
			RemoveComponents(HistoryDocumentCaptioned::Bit());
			captioned = nullptr;
		}
	} else if (captioned && captioned->_caption.hasSkipBlock()) {
		captioned->_caption.updateSkipBlock(
			_parent->skipBlockWidth(),
			_parent->skipBlockHeight());
	}
	auto thumbed = Get<HistoryDocumentThumbed>();
	const auto &st = thumbed ? st::msgFileThumbLayout : st::msgFileLayout;
	if (thumbed) {
		const auto &location = _data->thumbnailLocation();
		auto tw = style::ConvertScale(location.width());
		auto th = style::ConvertScale(location.height());
		if (tw > th) {
			thumbed->_thumbw = (tw * st.thumbSize) / th;
		} else {
			thumbed->_thumbw = st.thumbSize;
		}
	}

	auto maxWidth = st::msgFileMinWidth;

	const auto tleft = st.padding.left() + st.thumbSize + st.padding.right();
	const auto tright = st.padding.left();
	if (thumbed) {
		accumulate_max(maxWidth, tleft + documentMaxStatusWidth(_data) + tright);
	} else {
		auto unread = _data->isVoiceMessage() ? (st::mediaUnreadSkip + st::mediaUnreadSize) : 0;
		accumulate_max(maxWidth, tleft + documentMaxStatusWidth(_data) + unread + _parent->skipBlockWidth() + st::msgPadding.right());
	}

	if (auto named = Get<HistoryDocumentNamed>()) {
		accumulate_max(maxWidth, tleft + named->_namew + tright);
		accumulate_min(maxWidth, st::msgMaxWidth);
	}

	auto minHeight = st.padding.top() + st.thumbSize + st.padding.bottom();
	const auto msgsigned = item->Get<HistoryMessageSigned>();
	const auto views = item->Get<HistoryMessageViews>();
	if (!captioned && ((msgsigned && !msgsigned->isAnonymousRank)
		|| (views
			&& (views->views.count >= 0 || views->replies.count > 0))
		|| _parent->displayEditedBadge())) {
		minHeight += st::msgDateFont->height - st::msgDateDelta.y();
	}
	if (!isBubbleTop()) {
		minHeight -= st::msgFileTopMinus;
	}

	if (captioned) {
		auto captionw = maxWidth
			- st::msgPadding.left()
			- st::msgPadding.right();
		minHeight += captioned->_caption.countHeight(captionw);
		if (isBubbleBottom()) {
			minHeight += st::msgPadding.bottom();
		}
	}
	return { maxWidth, minHeight };
}

QSize Document::countCurrentSize(int newWidth) {
	const auto captioned = Get<HistoryDocumentCaptioned>();
	if (!captioned) {
		return File::countCurrentSize(newWidth);
	}

	accumulate_min(newWidth, maxWidth());
	const auto thumbed = Get<HistoryDocumentThumbed>();
	const auto &st = thumbed ? st::msgFileThumbLayout : st::msgFileLayout;
	auto newHeight = st.padding.top() + st.thumbSize + st.padding.bottom();
	if (!isBubbleTop()) {
		newHeight -= st::msgFileTopMinus;
	}
	auto captionw = newWidth - st::msgPadding.left() - st::msgPadding.right();
	newHeight += captioned->_caption.countHeight(captionw);
	if (isBubbleBottom()) {
		newHeight += st::msgPadding.bottom();
	}

	return { newWidth, newHeight };
}

void Document::draw(
		Painter &p,
		const QRect &r,
		TextSelection selection,
		crl::time ms) const {
	draw(p, width(), selection, ms, LayoutMode::Full);
}

void Document::draw(
		Painter &p,
		int width,
		TextSelection selection,
		crl::time ms,
		LayoutMode mode) const {
	if (width < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	ensureDataMediaCreated();

	const auto cornerDownload = downloadInCorner();

	if (!_dataMedia->canBePlayed()) {
		_dataMedia->automaticLoad(_realParent->fullId(), _parent->data());
	}
	bool loaded = dataLoaded(), displayLoading = _data->displayLoading();
	bool selected = (selection == FullSelection);

	int captionw = width - st::msgPadding.left() - st::msgPadding.right();
	auto outbg = _parent->hasOutLayout();

	if (displayLoading) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(dataProgress());
		}
	}
	const auto showPause = updateStatusText();
	const auto radial = isRadialAnimation();

	const auto topMinus = isBubbleTop() ? 0 : st::msgFileTopMinus;
	const auto thumbed = Get<HistoryDocumentThumbed>();
	const auto &st = (mode == LayoutMode::Full)
		? (thumbed ? st::msgFileThumbLayout : st::msgFileLayout)
		: (thumbed ? st::msgFileThumbLayoutGrouped : st::msgFileLayoutGrouped);
	const auto nameleft = st.padding.left() + st.thumbSize + st.padding.right();
	const auto nametop = st.nameTop - topMinus;
	const auto nameright = st.padding.left();
	const auto statustop = st.statusTop - topMinus;
	const auto linktop = st.linkTop - topMinus;
	const auto bottom = st.padding.top() + st.thumbSize + st.padding.bottom() - topMinus;
	const auto rthumb = style::rtlrect(st.padding.left(), st.padding.top() - topMinus, st.thumbSize, st.thumbSize, width);
	const auto innerSize = st::msgFileLayout.thumbSize;
	const auto inner = QRect(rthumb.x() + (rthumb.width() - innerSize) / 2, rthumb.y() + (rthumb.height() - innerSize) / 2, innerSize, innerSize);
	const auto radialOpacity = radial ? _animation->radial.opacity() : 1.;
	if (thumbed) {
		auto inWebPage = (_parent->media() != this);
		auto roundRadius = inWebPage ? ImageRoundRadius::Small : ImageRoundRadius::Large;
		QPixmap thumb;
		if (const auto normal = _dataMedia->thumbnail()) {
			thumb = normal->pixSingle(thumbed->_thumbw, 0, st.thumbSize, st.thumbSize, roundRadius);
		} else if (const auto blurred = _dataMedia->thumbnailInline()) {
			thumb = blurred->pixBlurredSingle(thumbed->_thumbw, 0, st.thumbSize, st.thumbSize, roundRadius);
		}
		p.drawPixmap(rthumb.topLeft(), thumb);
		if (selected) {
			auto overlayCorners = inWebPage ? Ui::SelectedOverlaySmallCorners : Ui::SelectedOverlayLargeCorners;
			Ui::FillRoundRect(p, rthumb, p.textPalette().selectOverlay, overlayCorners);
		}

		if (radial || (!loaded && !_data->loading()) || _data->waitingForAlbum()) {
			const auto backOpacity = (loaded && !_data->uploading()) ? radialOpacity : 1.;
			p.setPen(Qt::NoPen);
			if (selected) {
				p.setBrush(st::msgDateImgBgSelected);
			} else {
				p.setBrush(st::msgDateImgBg);
			}
			p.setOpacity(backOpacity * p.opacity());

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(inner);
			}

			const auto icon = [&] {
				if (_data->waitingForAlbum()) {
					return &(selected ? st::historyFileThumbWaitingSelected : st::historyFileThumbWaiting);
				} else if (radial || _data->loading()) {
					return &(selected ? st::historyFileThumbCancelSelected : st::historyFileThumbCancel);
				}
				return &(selected ? st::historyFileThumbDownloadSelected : st::historyFileThumbDownload);
			}();
			const auto previous = [&]() -> const style::icon* {
				if (_data->waitingForAlbum()) {
					return &(selected ? st::historyFileThumbCancelSelected : st::historyFileThumbCancel);
				}
				return nullptr;
			}();
			p.setOpacity(backOpacity);
			if (previous && radialOpacity > 0. && radialOpacity < 1.) {
				PaintInterpolatedIcon(p, *icon, *previous, radialOpacity, inner);
			} else {
				icon->paintInCenter(p, inner);
			}
			p.setOpacity(1.);
			if (radial) {
				QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
				_animation->radial.draw(p, rinner, st::msgFileRadialLine, selected ? st::historyFileThumbRadialFgSelected : st::historyFileThumbRadialFg);
			}
		}

		if (_data->status != FileUploadFailed) {
			const auto &lnk = (_data->loading() || _data->uploading())
				? thumbed->_linkcancell
				: dataLoaded()
				? thumbed->_linkopenwithl
				: thumbed->_linksavel;
			bool over = ClickHandler::showAsActive(lnk);
			p.setFont(over ? st::semiboldFont->underline() : st::semiboldFont);
			p.setPen(outbg ? (selected ? st::msgFileThumbLinkOutFgSelected : st::msgFileThumbLinkOutFg) : (selected ? st::msgFileThumbLinkInFgSelected : st::msgFileThumbLinkInFg));
			p.drawTextLeft(nameleft, linktop, width, thumbed->_link, thumbed->_linkw);
		}
	} else {
		p.setPen(Qt::NoPen);
		if (selected) {
			p.setBrush(outbg ? st::msgFileOutBgSelected : st::msgFileInBgSelected);
		} else {
			p.setBrush(outbg ? st::msgFileOutBg : st::msgFileInBg);
		}

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}

		const auto icon = [&] {
			if (_data->waitingForAlbum()) {
				return &(outbg ? (selected ? st::historyFileOutWaitingSelected : st::historyFileOutWaiting) : (selected ? st::historyFileInWaitingSelected : st::historyFileInWaiting));
			} else if (!cornerDownload && (_data->loading() || _data->uploading())) {
				return &(outbg ? (selected ? st::historyFileOutCancelSelected : st::historyFileOutCancel) : (selected ? st::historyFileInCancelSelected : st::historyFileInCancel));
			} else if (showPause) {
				return &(outbg ? (selected ? st::historyFileOutPauseSelected : st::historyFileOutPause) : (selected ? st::historyFileInPauseSelected : st::historyFileInPause));
			} else if (loaded || _dataMedia->canBePlayed()) {
				if (_dataMedia->canBePlayed()) {
					return &(outbg ? (selected ? st::historyFileOutPlaySelected : st::historyFileOutPlay) : (selected ? st::historyFileInPlaySelected : st::historyFileInPlay));
				} else if (_data->isImage()) {
					return &(outbg ? (selected ? st::historyFileOutImageSelected : st::historyFileOutImage) : (selected ? st::historyFileInImageSelected : st::historyFileInImage));
				}
				return &(outbg ? (selected ? st::historyFileOutDocumentSelected : st::historyFileOutDocument) : (selected ? st::historyFileInDocumentSelected : st::historyFileInDocument));
			}
			return &(outbg ? (selected ? st::historyFileOutDownloadSelected : st::historyFileOutDownload) : (selected ? st::historyFileInDownloadSelected : st::historyFileInDownload));
		}();
		const auto previous = [&]() -> const style::icon* {
			if (_data->waitingForAlbum()) {
				return &(outbg ? (selected ? st::historyFileOutCancelSelected : st::historyFileOutCancel) : (selected ? st::historyFileInCancelSelected : st::historyFileInCancel));
			}
			return nullptr;
		}();
		if (previous && radialOpacity > 0. && radialOpacity < 1.) {
			PaintInterpolatedIcon(p, *icon, *previous, radialOpacity, inner);
		} else {
			icon->paintInCenter(p, inner);
		}

		if (radial && !cornerDownload) {
			QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
			auto fg = outbg ? (selected ? st::historyFileOutRadialFgSelected : st::historyFileOutRadialFg) : (selected ? st::historyFileInRadialFgSelected : st::historyFileInRadialFg);
			_animation->radial.draw(p, rinner, st::msgFileRadialLine, fg);
		}

		drawCornerDownload(p, selected, mode);
	}
	auto namewidth = width - nameleft - nameright;
	auto statuswidth = namewidth;

	auto voiceStatusOverride = QString();
	if (const auto voice = Get<HistoryDocumentVoice>()) {
		ensureDataMediaCreated();

		const VoiceWaveform *wf = nullptr;
		uchar norm_value = 0;
		if (const auto voiceData = _data->voice()) {
			wf = &voiceData->waveform;
			if (wf->isEmpty()) {
				wf = nullptr;
				if (loaded) {
					Local::countVoiceWaveform(_dataMedia.get());
				}
			} else if (wf->at(0) < 0) {
				wf = nullptr;
			} else {
				norm_value = voiceData->wavemax;
			}
		}
		auto progress = ([voice] {
			if (voice->seeking()) {
				return voice->seekingCurrent();
			} else if (voice->_playback) {
				return voice->_playback->progress.current();
			}
			return 0.;
		})();
		if (voice->seeking()) {
			voiceStatusOverride = Ui::FormatPlayedText(qRound(progress * voice->_lastDurationMs) / 1000, voice->_lastDurationMs / 1000);
		}

		// rescale waveform by going in waveform.size * bar_count 1D grid
		auto active = outbg ? (selected ? st::msgWaveformOutActiveSelected : st::msgWaveformOutActive) : (selected ? st::msgWaveformInActiveSelected : st::msgWaveformInActive);
		auto inactive = outbg ? (selected ? st::msgWaveformOutInactiveSelected : st::msgWaveformOutInactive) : (selected ? st::msgWaveformInInactiveSelected : st::msgWaveformInInactive);
		auto wf_size = wf ? wf->size() : ::Media::Player::kWaveformSamplesCount;
		auto availw = namewidth + st::msgWaveformSkip;
		auto activew = qRound(availw * progress);
		if (!outbg
			&& !voice->_playback
			&& _parent->data()->hasUnreadMediaFlag()) {
			activew = availw;
		}
		auto bar_count = qMin(availw / (st::msgWaveformBar + st::msgWaveformSkip), wf_size);
		auto max_value = 0;
		auto max_delta = st::msgWaveformMax - st::msgWaveformMin;
		auto bottom = st.padding.top() - topMinus + st::msgWaveformMax;
		p.setPen(Qt::NoPen);
		for (auto i = 0, bar_x = 0, sum_i = 0; i < wf_size; ++i) {
			auto value = wf ? wf->at(i) : 0;
			if (sum_i + bar_count >= wf_size) { // draw bar
				sum_i = sum_i + bar_count - wf_size;
				if (sum_i < (bar_count + 1) / 2) {
					if (max_value < value) max_value = value;
				}
				auto bar_value = ((max_value * max_delta) + ((norm_value + 1) / 2)) / (norm_value + 1);

				if (bar_x >= activew) {
					p.fillRect(nameleft + bar_x, bottom - bar_value, st::msgWaveformBar, st::msgWaveformMin + bar_value, inactive);
				} else if (bar_x + st::msgWaveformBar <= activew) {
					p.fillRect(nameleft + bar_x, bottom - bar_value, st::msgWaveformBar, st::msgWaveformMin + bar_value, active);
				} else {
					p.fillRect(nameleft + bar_x, bottom - bar_value, activew - bar_x, st::msgWaveformMin + bar_value, active);
					p.fillRect(nameleft + activew, bottom - bar_value, st::msgWaveformBar - (activew - bar_x), st::msgWaveformMin + bar_value, inactive);
				}
				bar_x += st::msgWaveformBar + st::msgWaveformSkip;

				if (sum_i < (bar_count + 1) / 2) {
					max_value = 0;
				} else {
					max_value = value;
				}
			} else {
				if (max_value < value) max_value = value;

				sum_i += bar_count;
			}
		}
	} else if (auto named = Get<HistoryDocumentNamed>()) {
		p.setFont(st::semiboldFont);
		p.setPen(outbg ? (selected ? st::historyFileNameOutFgSelected : st::historyFileNameOutFg) : (selected ? st::historyFileNameInFgSelected : st::historyFileNameInFg));
		if (namewidth < named->_namew) {
			p.drawTextLeft(nameleft, nametop, width, st::semiboldFont->elided(named->_name, namewidth, Qt::ElideMiddle));
		} else {
			p.drawTextLeft(nameleft, nametop, width, named->_name, named->_namew);
		}
	}

	auto statusText = voiceStatusOverride.isEmpty() ? _statusText : voiceStatusOverride;
	auto status = outbg ? (selected ? st::mediaOutFgSelected : st::mediaOutFg) : (selected ? st::mediaInFgSelected : st::mediaInFg);
	p.setFont(st::normalFont);
	p.setPen(status);
	p.drawTextLeft(nameleft, statustop, width, statusText);

	if (_parent->data()->hasUnreadMediaFlag()) {
		auto w = st::normalFont->width(statusText);
		if (w + st::mediaUnreadSkip + st::mediaUnreadSize <= statuswidth) {
			p.setPen(Qt::NoPen);
			p.setBrush(outbg ? (selected ? st::msgFileOutBgSelected : st::msgFileOutBg) : (selected ? st::msgFileInBgSelected : st::msgFileInBg));

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(style::rtlrect(nameleft + w + st::mediaUnreadSkip, statustop + st::mediaUnreadTop, st::mediaUnreadSize, st::mediaUnreadSize, width));
			}
		}
	}

	if (auto captioned = Get<HistoryDocumentCaptioned>()) {
		p.setPen(outbg ? (selected ? st::historyTextOutFgSelected : st::historyTextOutFg) : (selected ? st::historyTextInFgSelected : st::historyTextInFg));
		captioned->_caption.draw(p, st::msgPadding.left(), bottom, captionw, style::al_left, 0, -1, selection);
	}
}

bool Document::hasHeavyPart() const {
	return (_dataMedia != nullptr);
}

void Document::unloadHeavyPart() {
	_dataMedia = nullptr;
}

void Document::ensureDataMediaCreated() const {
	if (_dataMedia) {
		return;
	}
	_dataMedia = _data->createMediaView();
	if (Get<HistoryDocumentThumbed>()) {
		_dataMedia->thumbnailWanted(_realParent->fullId());
	}
	history()->owner().registerHeavyViewPart(_parent);
}

bool Document::downloadInCorner() const {
	return _data->isAudioFile()
		&& _data->canBeStreamed()
		&& !_data->inappPlaybackFailed()
		&& IsServerMsgId(_parent->data()->id);
}

void Document::drawCornerDownload(Painter &p, bool selected, LayoutMode mode) const {
	if (dataLoaded()
		|| _data->loadedInMediaCache()
		|| !downloadInCorner()) {
		return;
	}
	auto outbg = _parent->hasOutLayout();
	auto topMinus = isBubbleTop() ? 0 : st::msgFileTopMinus;
	const auto thumbed = false;
	const auto &st = (mode == LayoutMode::Full)
		? (thumbed ? st::msgFileThumbLayout : st::msgFileLayout)
		: (thumbed ? st::msgFileThumbLayoutGrouped : st::msgFileLayoutGrouped);
	const auto shift = st::historyAudioDownloadShift;
	const auto size = st::historyAudioDownloadSize;
	const auto inner = style::rtlrect(st.padding.left() + shift, st.padding.top() - topMinus + shift, size, size, width());
	auto pen = (selected
		? (outbg ? st::msgOutBgSelected : st::msgInBgSelected)
		: (outbg ? st::msgOutBg : st::msgInBg))->p;
	pen.setWidth(st::lineWidth);
	p.setPen(pen);
	if (selected) {
		p.setBrush(outbg ? st::msgFileOutBgSelected : st::msgFileInBgSelected);
	} else {
		p.setBrush(outbg ? st::msgFileOutBg : st::msgFileInBg);
	}
	{
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(inner);
	}
	const auto icon = [&] {
		if (_data->loading()) {
			return &(outbg ? (selected ? st::historyAudioOutCancelSelected : st::historyAudioOutCancel) : (selected ? st::historyAudioInCancelSelected : st::historyAudioInCancel));
		}
		return &(outbg ? (selected ? st::historyAudioOutDownloadSelected : st::historyAudioOutDownload) : (selected ? st::historyAudioInDownloadSelected : st::historyAudioInDownload));
	}();
	icon->paintInCenter(p, inner);
	if (_animation && _animation->radial.animating()) {
		const auto rinner = inner.marginsRemoved(QMargins(st::historyAudioRadialLine, st::historyAudioRadialLine, st::historyAudioRadialLine, st::historyAudioRadialLine));
		auto fg = outbg ? (selected ? st::historyFileOutRadialFgSelected : st::historyFileOutRadialFg) : (selected ? st::historyFileInRadialFgSelected : st::historyFileInRadialFg);
		_animation->radial.draw(p, rinner, st::historyAudioRadialLine, fg);
	}
}

TextState Document::cornerDownloadTextState(
		QPoint point,
		StateRequest request,
		LayoutMode mode) const {
	auto result = TextState(_parent);
	if (dataLoaded()
		|| _data->loadedInMediaCache()
		|| !downloadInCorner()) {
		return result;
	}
	auto topMinus = isBubbleTop() ? 0 : st::msgFileTopMinus;
	const auto thumbed = false;
	const auto &st = (mode == LayoutMode::Full)
		? (thumbed ? st::msgFileThumbLayout : st::msgFileLayout)
		: (thumbed ? st::msgFileThumbLayoutGrouped : st::msgFileLayoutGrouped);
	const auto shift = st::historyAudioDownloadShift;
	const auto size = st::historyAudioDownloadSize;
	const auto inner = style::rtlrect(st.padding.left() + shift, st.padding.top() - topMinus + shift, size, size, width());
	if (inner.contains(point)) {
		result.link = _data->loading() ? _cancell : _savel;
	}
	return result;
}

TextState Document::textState(QPoint point, StateRequest request) const {
	return textState(point, { width(), height() }, request, LayoutMode::Full);
}

TextState Document::textState(
		QPoint point,
		QSize layout,
		StateRequest request,
		LayoutMode mode) const {
	const auto width = layout.width();
	const auto height = layout.height();

	auto result = TextState(_parent);

	if (width < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	}

	ensureDataMediaCreated();
	bool loaded = dataLoaded();

	bool showPause = updateStatusText();

	const auto topMinus = isBubbleTop() ? 0 : st::msgFileTopMinus;
	const auto thumbed = Get<HistoryDocumentThumbed>();
	const auto &st = (mode == LayoutMode::Full)
		? (thumbed ? st::msgFileThumbLayout : st::msgFileLayout)
		: (thumbed ? st::msgFileThumbLayoutGrouped : st::msgFileLayoutGrouped);
	const auto nameleft = st.padding.left() + st.thumbSize + st.padding.right();
	const auto nametop = st.nameTop - topMinus;
	const auto nameright = st.padding.left();
	const auto statustop = st.statusTop - topMinus;
	const auto linktop = st.linkTop - topMinus;
	const auto bottom = st.padding.top() + st.thumbSize + st.padding.bottom() - topMinus;
	const auto rthumb = style::rtlrect(st.padding.left(), st.padding.top() - topMinus, st.thumbSize, st.thumbSize, width);
	const auto innerSize = st::msgFileLayout.thumbSize;
	const auto inner = QRect(rthumb.x() + (rthumb.width() - innerSize) / 2, rthumb.y() + (rthumb.height() - innerSize) / 2, innerSize, innerSize);
	if (const auto thumbed = Get<HistoryDocumentThumbed>()) {
		if ((_data->loading() || _data->uploading()) && rthumb.contains(point)) {
			result.link = _cancell;
			return result;
		}

		if (_data->status != FileUploadFailed) {
			if (style::rtlrect(nameleft, linktop, thumbed->_linkw, st::semiboldFont->height, width).contains(point)) {
				result.link = (_data->loading() || _data->uploading())
					? thumbed->_linkcancell
					: dataLoaded()
					? thumbed->_linkopenwithl
					: thumbed->_linksavel;
				return result;
			}
		}
	} else {
		if (const auto state = cornerDownloadTextState(point, request, mode); state.link) {
			return state;
		}
		if ((_data->loading() || _data->uploading()) && inner.contains(point) && !downloadInCorner()) {
			result.link = _cancell;
			return result;
		}
	}

	if (const auto voice = Get<HistoryDocumentVoice>()) {
		auto namewidth = width - nameleft - nameright;
		auto waveformbottom = st.padding.top() - topMinus + st::msgWaveformMax + st::msgWaveformMin;
		if (QRect(nameleft, nametop, namewidth, waveformbottom - nametop).contains(point)) {
			const auto state = ::Media::Player::instance()->getState(AudioMsgId::Type::Voice);
			if (state.id == AudioMsgId(_data, _parent->data()->fullId(), state.id.externalPlayId())
				&& !::Media::Player::IsStoppedOrStopping(state.state)) {
				if (!voice->seeking()) {
					voice->setSeekingStart((point.x() - nameleft) / float64(namewidth));
				}
				result.link = voice->_seekl;
				return result;
			}
		}
	}

	auto painth = layout.height();
	if (const auto captioned = Get<HistoryDocumentCaptioned>()) {
		if (point.y() >= bottom) {
			result = TextState(_parent, captioned->_caption.getState(
				point - QPoint(st::msgPadding.left(), bottom),
				width - st::msgPadding.left() - st::msgPadding.right(),
				request.forText()));
			return result;
		}
		auto captionw = width - st::msgPadding.left() - st::msgPadding.right();
		painth -= captioned->_caption.countHeight(captionw);
		if (isBubbleBottom()) {
			painth -= st::msgPadding.bottom();
		}
	}
	if (QRect(0, 0, width, painth).contains(point)
		&& (!_data->loading() || downloadInCorner())
		&& !_data->uploading()
		&& !_data->isNull()) {
		if (loaded || _dataMedia->canBePlayed()) {
			result.link = _openl;
		} else {
			result.link = _savel;
		}
		return result;
	}
	return result;
}

void Document::updatePressed(QPoint point) {
	// LayoutMode should be passed here.
	if (auto voice = Get<HistoryDocumentVoice>()) {
		if (voice->seeking()) {
			const auto thumbed = Get<HistoryDocumentThumbed>();
			const auto &st = thumbed ? st::msgFileThumbLayout : st::msgFileLayout;
			const auto nameleft = st.padding.left() + st.thumbSize + st.padding.right();
			const auto nameright = st.padding.left();
			voice->setSeekingCurrent(snap((point.x() - nameleft) / float64(width() - nameleft - nameright), 0., 1.));
			history()->owner().requestViewRepaint(_parent);
		}
	}
}

TextSelection Document::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	if (const auto captioned = Get<HistoryDocumentCaptioned>()) {
		return captioned->_caption.adjustSelection(selection, type);
	}
	return selection;
}

uint16 Document::fullSelectionLength() const {
	if (const auto captioned = Get<HistoryDocumentCaptioned>()) {
		return captioned->_caption.length();
	}
	return 0;
}

bool Document::hasTextForCopy() const {
	return Has<HistoryDocumentCaptioned>();
}

TextForMimeData Document::selectedText(TextSelection selection) const {
	if (const auto captioned = Get<HistoryDocumentCaptioned>()) {
		const auto &caption = captioned->_caption;
		return captioned->_caption.toTextForMimeData(selection);
	}
	return TextForMimeData();
}

bool Document::uploading() const {
	return _data->uploading();
}

void Document::setStatusSize(int newSize, qint64 realDuration) const {
	auto duration = _data->isSong()
		? _data->song()->duration
		: (_data->isVoiceMessage()
			? _data->voice()->duration
			: -1);
	File::setStatusSize(newSize, _data->size, duration, realDuration);
	if (auto thumbed = Get<HistoryDocumentThumbed>()) {
		if (_statusSize == Ui::FileStatusSizeReady) {
			thumbed->_link = tr::lng_media_download(tr::now).toUpper();
		} else if (_statusSize == Ui::FileStatusSizeLoaded) {
			thumbed->_link = tr::lng_media_open_with(tr::now).toUpper();
		} else if (_statusSize == Ui::FileStatusSizeFailed) {
			thumbed->_link = tr::lng_media_download(tr::now).toUpper();
		} else if (_statusSize >= 0) {
			thumbed->_link = tr::lng_media_cancel(tr::now).toUpper();
		} else {
			thumbed->_link = tr::lng_media_open_with(tr::now).toUpper();
		}
		thumbed->_linkw = st::semiboldFont->width(thumbed->_link);
	}
}

bool Document::updateStatusText() const {
	auto showPause = false;
	auto statusSize = 0;
	auto realDuration = 0;
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
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

	if (_data->isVoiceMessage()) {
		const auto state = ::Media::Player::instance()->getState(AudioMsgId::Type::Voice);
		if (state.id == AudioMsgId(_data, _parent->data()->fullId(), state.id.externalPlayId())
			&& !::Media::Player::IsStoppedOrStopping(state.state)) {
			if (auto voice = Get<HistoryDocumentVoice>()) {
				bool was = (voice->_playback != nullptr);
				voice->ensurePlayback(this);
				if (!was || state.position != voice->_playback->position) {
					auto prg = state.length ? snap(float64(state.position) / state.length, 0., 1.) : 0.;
					if (voice->_playback->position < state.position) {
						voice->_playback->progress.start(prg);
					} else {
						voice->_playback->progress = anim::value(0., prg);
					}
					voice->_playback->position = state.position;
					voice->_playback->progressAnimation.start();
				}
				voice->_lastDurationMs = static_cast<int>((state.length * 1000LL) / state.frequency); // Bad :(
			}

			statusSize = -1 - (state.position / state.frequency);
			realDuration = (state.length / state.frequency);
			showPause = ::Media::Player::ShowPauseIcon(state.state);
		} else {
			if (auto voice = Get<HistoryDocumentVoice>()) {
				voice->checkPlaybackFinished();
			}
		}
		if (!showPause && (state.id == AudioMsgId(_data, _parent->data()->fullId(), state.id.externalPlayId()))) {
			showPause = ::Media::Player::instance()->isSeeking(AudioMsgId::Type::Voice);
		}
	} else if (_data->isAudioFile()) {
		const auto state = ::Media::Player::instance()->getState(AudioMsgId::Type::Song);
		if (state.id == AudioMsgId(_data, _parent->data()->fullId(), state.id.externalPlayId())
			&& !::Media::Player::IsStoppedOrStopping(state.state)) {
			statusSize = -1 - (state.position / state.frequency);
			realDuration = (state.length / state.frequency);
			showPause = ::Media::Player::ShowPauseIcon(state.state);
		} else {
		}
		if (!showPause && (state.id == AudioMsgId(_data, _parent->data()->fullId(), state.id.externalPlayId()))) {
			showPause = ::Media::Player::instance()->isSeeking(AudioMsgId::Type::Song);
		}
	}

	if (statusSize != _statusSize) {
		setStatusSize(statusSize, realDuration);
	}
	return showPause;
}

QMargins Document::bubbleMargins() const {
	if (!Has<HistoryDocumentThumbed>()) {
		return st::msgPadding;
	}
	const auto padding = st::msgFileThumbLayout.padding;
	return QMargins(padding.left(), padding.top(), padding.left(), padding.bottom());
}

bool Document::hideForwardedFrom() const {
	return _data->isSong();
}

QSize Document::sizeForGroupingOptimal(int maxWidth) const {
	const auto thumbed = Get<HistoryDocumentThumbed>();
	const auto &st = (thumbed ? st::msgFileThumbLayoutGrouped : st::msgFileLayoutGrouped);
	auto height = st.padding.top() + st.thumbSize + st.padding.bottom();
	if (const auto captioned = Get<HistoryDocumentCaptioned>()) {
		auto captionw = maxWidth
			- st::msgPadding.left()
			- st::msgPadding.right();
		height += captioned->_caption.countHeight(captionw);
	}
	return { maxWidth, height };
}

QSize Document::sizeForGrouping(int width) const {
	const auto thumbed = Get<HistoryDocumentThumbed>();
	const auto &st = (thumbed ? st::msgFileThumbLayoutGrouped : st::msgFileLayoutGrouped);
	auto height = st.padding.top() + st.thumbSize + st.padding.bottom();
	if (const auto captioned = Get<HistoryDocumentCaptioned>()) {
		auto captionw = width
			- st::msgPadding.left()
			- st::msgPadding.right();
		height += captioned->_caption.countHeight(captionw);
	}
	return { maxWidth(), height };
}

void Document::drawGrouped(
		Painter &p,
		const QRect &clip,
		TextSelection selection,
		crl::time ms,
		const QRect &geometry,
		RectParts sides,
		RectParts corners,
		not_null<uint64*> cacheKey,
		not_null<QPixmap*> cache) const {
	p.translate(geometry.topLeft());
	draw(
		p,
		geometry.width(),
		selection,
		ms,
		LayoutMode::Grouped);
	p.translate(-geometry.topLeft());
}

TextState Document::getStateGrouped(
		const QRect &geometry,
		RectParts sides,
		QPoint point,
		StateRequest request) const {
	point -= geometry.topLeft();
	return textState(
		point,
		geometry.size(),
		request,
		LayoutMode::Grouped);
}

bool Document::voiceProgressAnimationCallback(crl::time now) {
	if (anim::Disabled()) {
		now += (2 * kAudioVoiceMsgUpdateView);
	}
	if (const auto voice = Get<HistoryDocumentVoice>()) {
		if (voice->_playback) {
			const auto dt = (now - voice->_playback->progressAnimation.started())
				/ float64(2 * kAudioVoiceMsgUpdateView);
			if (dt >= 1.) {
				voice->_playback->progressAnimation.stop();
				voice->_playback->progress.finish();
			} else {
				voice->_playback->progress.update(qMin(dt, 1.), anim::linear);
			}
			history()->owner().requestViewRepaint(_parent);
			return (dt < 1.);
		}
	}
	return false;
}

void Document::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	if (auto voice = Get<HistoryDocumentVoice>()) {
		if (pressed && p == voice->_seekl && !voice->seeking()) {
			voice->startSeeking();
		} else if (!pressed && voice->seeking()) {
			const auto type = AudioMsgId::Type::Voice;
			const auto state = ::Media::Player::instance()->getState(type);
			if (state.id == AudioMsgId(_data, _parent->data()->fullId(), state.id.externalPlayId()) && state.length) {
				const auto currentProgress = voice->seekingCurrent();
				::Media::Player::instance()->finishSeeking(
					AudioMsgId::Type::Voice,
					currentProgress);

				voice->ensurePlayback(this);
				voice->_playback->position = 0;
				voice->_playback->progress = anim::value(currentProgress, currentProgress);
			}
			voice->stopSeeking();
		}
	}
	File::clickHandlerPressedChanged(p, pressed);
}

void Document::refreshParentId(not_null<HistoryItem*> realParent) {
	File::refreshParentId(realParent);

	const auto fullId = realParent->fullId();
	if (auto thumbed = Get<HistoryDocumentThumbed>()) {
		if (thumbed->_linksavel) {
			thumbed->_linksavel->setMessageId(fullId);
			thumbed->_linkcancell->setMessageId(fullId);
		}
	}
	if (auto voice = Get<HistoryDocumentVoice>()) {
		if (voice->_seekl) {
			voice->_seekl->setMessageId(fullId);
		}
	}
}

void Document::parentTextUpdated() {
	auto caption = (_parent->media() == this || _realParent->groupId())
		? createCaption()
		: Ui::Text::String();
	if (!caption.isEmpty()) {
		AddComponents(HistoryDocumentCaptioned::Bit());
		auto captioned = Get<HistoryDocumentCaptioned>();
		captioned->_caption = std::move(caption);
	} else {
		RemoveComponents(HistoryDocumentCaptioned::Bit());
	}
	history()->owner().requestViewResize(_parent);
}

TextWithEntities Document::getCaption() const {
	if (const auto captioned = Get<HistoryDocumentCaptioned>()) {
		return captioned->_caption.toTextWithEntities();
	}
	return TextWithEntities();
}

Ui::Text::String Document::createCaption() {
	const auto timestampLinksDuration = _data->isSong()
		? _data->getDuration()
		: 0;
	const auto timestampLinkBase = timestampLinksDuration
		? DocumentTimestampLinkBase(_data, _realParent->fullId())
		: QString();
	return File::createCaption(
		_realParent,
		timestampLinksDuration,
		timestampLinkBase);
}

} // namespace HistoryView
