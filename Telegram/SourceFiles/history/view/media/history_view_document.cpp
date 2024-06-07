/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_document.h"

#include "base/random.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "storage/localstorage.h"
#include "main/main_session.h"
#include "media/player/media_player_float.h" // Media::Player::RoundPainter.
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h" // ClearMediaAsExpired.
#include "history/history.h"
#include "core/click_handler_types.h" // kDocumentFilenameTooltipProperty.
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_transcribe_button.h"
#include "history/view/media/history_view_media_common.h"
#include "ui/text/format_values.h"
#include "ui/text/format_song_document_name.h"
#include "ui/text/text_utilities.h"
#include "ui/chat/chat_style.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/rect.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_document_resolver.h"
#include "data/data_file_click_handler.h"
#include "api/api_transcribes.h"
#include "apiwrap.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"

namespace HistoryView {
namespace {

constexpr auto kAudioVoiceMsgUpdateView = crl::time(100);

[[nodiscard]] QRect TTLRectFromInner(const QRect &inner) {
	return QRect(
		rect::right(inner)
			- st::dialogsTTLBadgeSize
			+ rect::m::sum::h(st::dialogsTTLBadgeInnerMargins)
			- st::dialogsTTLBadgeSkip.x(),
		rect::bottom(inner)
			- st::dialogsTTLBadgeSize
			+ rect::m::sum::v(st::dialogsTTLBadgeInnerMargins)
			- st::dialogsTTLBadgeSkip.y(),
		st::dialogsTTLBadgeSize,
		st::dialogsTTLBadgeSize);
}

[[nodiscard]] HistoryView::TtlPaintCallback CreateTtlPaintCallback(
		std::shared_ptr<rpl::lifetime> lifetime,
		Fn<void()> update) {
	struct State final {
		std::unique_ptr<Lottie::Icon> start;
		std::unique_ptr<Lottie::Icon> idle;
		bool started = false;
	};
	const auto iconSize = Size(std::min(
		st::historyFileInPause.width(),
		st::historyFileInPause.height()));
	const auto state = lifetime->make_state<State>();
	//state->start = Lottie::MakeIcon({
	//	.name = u"voice_ttl_start"_q,
	//	.color = &st::historyFileInIconFg,
	//	.sizeOverride = iconSize,
	//});
	state->idle = Lottie::MakeIcon({
		.name = u"voice_ttl_idle"_q,
		.color = &st::historyFileInIconFg,
		.sizeOverride = iconSize,
	});

	const auto weak = std::weak_ptr(lifetime);
	return [=](QPainter &p, QRect r, QColor c) {
		if (weak.expired()) {
			return;
		}
		{
			const auto &icon = state->idle;
			if (icon) {
				icon->paintInCenter(p, r, c);
				if (!icon->animating()) {
					icon->animate(update, 0, icon->framesCount());
				}
				return;
			}
		}
		{
			const auto &icon = state->start;
			icon->paintInCenter(p, r, c);
			if (!icon->animating()) {
				if (!state->started) {
					icon->animate(update, 0, icon->framesCount());
					state->started = true;
				} else {
					state->idle = Lottie::MakeIcon({
						.name = u"voice_ttl_idle"_q,
						.color = &st::historyFileInIconFg,
						.sizeOverride = iconSize,
					});
				}
			}
		}
	};
}

void FillThumbnailOverlay(
		QPainter &p,
		QRect rect,
		Ui::BubbleRounding rounding,
		const PaintContext &context) {
	using Corner = Ui::BubbleCornerRounding;
	using Radius = Ui::CachedCornerRadius;
	auto corners = Ui::CornersPixmaps();
	const auto &st = context.st;
	const auto lookup = [&](Corner corner) {
		switch (corner) {
		case Corner::None: return Radius::Small;
		case Corner::Small: return Radius::ThumbSmall;
		case Corner::Large: return Radius::ThumbLarge;
		}
		Unexpected("Corner value in FillThumbnailOverlay.");
	};
	for (auto i = 0; i != 4; ++i) {
		corners.p[i] = st->msgSelectOverlayCorners(lookup(rounding[i])).p[i];
	}
	Ui::FillComplexOverlayRect(p, rect, st->msgSelectOverlay(), corners);
}

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

void FillWaveform(VoiceData *roundData) {
	if (!roundData->waveform.empty()) {
		return;
	}
	const auto &size = ::Media::Player::kWaveformSamplesCount;
	auto randomBytes = bytes::vector(size);
	base::RandomFill(randomBytes.data(), randomBytes.size());
	roundData->waveform.resize(size);
	for (auto i = 1; i < size; i += 2) {
		const auto peak = uchar(randomBytes[i]) % 31;
		roundData->waveform[i - 1] = char(std::max(
			0,
			peak - (uchar(randomBytes[i - 1]) % 3 + 2)));
		roundData->waveform[i] = char(peak);
	}
	roundData->wavemax = *ranges::max_element(roundData->waveform);
}

void PaintWaveform(
		Painter &p,
		const PaintContext &context,
		const VoiceData *voiceData,
		int availableWidth,
		float64 progress,
		bool ttl) {
	const auto wf = [&]() -> const VoiceWaveform* {
		if (!voiceData) {
			return nullptr;
		}
		if (voiceData->waveform.isEmpty()) {
			return nullptr;
		} else if (voiceData->waveform.at(0) < 0) {
			return nullptr;
		}
		return &voiceData->waveform;
	}();
	if (ttl) {
		progress = 1. - progress;
	}
	const auto stm = context.messageStyle();

	// Rescale waveform by going in waveform.size * bar_count 1D grid.
	const auto active = stm->msgWaveformActive;
	const auto inactive = ttl ? stm->msgBg : stm->msgWaveformInactive;
	const auto wfSize = wf
		? int(wf->size())
		: ::Media::Player::kWaveformSamplesCount;
	const auto activeWidth = base::SafeRound(availableWidth * progress);

	const auto &barWidth = st::msgWaveformBar;
	const auto barCount = std::min(
		availableWidth / (barWidth + st::msgWaveformSkip),
		wfSize);
	const auto barNormValue = (wf ? voiceData->wavemax : 0) + 1;
	const auto maxDelta = st::msgWaveformMax - st::msgWaveformMin;
	p.setPen(Qt::NoPen);
	auto hq = PainterHighQualityEnabler(p);
	for (auto i = 0, barLeft = 0, sum = 0, maxValue = 0; i < wfSize; ++i) {
		const auto value = wf ? wf->at(i) : 0;
		if (sum + barCount < wfSize) {
			maxValue = std::max(maxValue, value);
			sum += barCount;
			continue;
		}
		// Draw bar.
		sum = sum + barCount - wfSize;
		if (sum < (barCount + 1) / 2) {
			maxValue = std::max(maxValue, value);
		}
		const auto barValue = ((maxValue * maxDelta) + (barNormValue / 2))
			/ barNormValue;
		const auto barHeight = st::msgWaveformMin + barValue;
		const auto barTop = st::lineWidth + (st::msgWaveformMax - barValue) / 2.;

		if ((barLeft < activeWidth) && (barLeft + barWidth > activeWidth)) {
			const auto leftWidth = activeWidth - barLeft;
			const auto rightWidth = barWidth - leftWidth;
			p.fillRect(
				QRectF(barLeft, barTop, leftWidth, barHeight),
				active);
			if (!ttl) {
				p.fillRect(
					QRectF(activeWidth, barTop, rightWidth, barHeight),
					inactive);
			}
		} else if (!ttl || barLeft < activeWidth) {
			const auto &color = (barLeft >= activeWidth) ? inactive : active;
			p.fillRect(QRectF(barLeft, barTop, barWidth, barHeight), color);
		}
		barLeft += barWidth + st::msgWaveformSkip;

		maxValue = (sum < (barCount + 1) / 2) ? 0 : value;
	}
}

[[nodiscard]] int MaxStatusWidth(not_null<DocumentData*> document) {
	using namespace Ui;
	auto result = 0;
	const auto add = [&](const QString &text) {
		accumulate_max(result, st::normalFont->width(text));
	};
	add(FormatDownloadText(document->size, document->size));
	const auto duration = document->duration() / 1000;
	if (const auto song = document->song()) {
		add(FormatPlayedText(duration, duration));
		add(FormatDurationAndSizeText(duration, document->size));
	} else if (const auto voice = document->voice() ? document->voice() : document->round()) {
		add(FormatPlayedText(duration, duration));
		add(FormatDurationAndSizeText(duration, document->size));
	} else if (document->isVideoFile()) {
		add(FormatDurationAndSizeText(duration, document->size));
	} else {
		add(FormatSizeText(document->size));
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
	const auto isRound = _data->isVideoMessage();
	if (isRound) {
		const auto &entry = _data->session().api().transcribes().entry(
			realParent);
		_transcribedRound = entry.shown;
	}

	createComponents();
	if (const auto named = Get<HistoryDocumentNamed>()) {
		fillNamedFromData(named);
		_tooltipFilename.setTooltipText(named->name);
	}

	if ((_data->isVoiceMessage() || isRound)
		&& _parent->data()->media()->ttlSeconds()) {
		const auto fullId = _realParent->fullId();
		if (_parent->delegate()->elementContext() == Context::TTLViewer) {
			auto lifetime = std::make_shared<rpl::lifetime>();
			TTLVoiceStops(fullId) | rpl::start_with_next([=]() mutable {
				if (lifetime) {
					base::take(lifetime)->destroy();
				}
			}, *lifetime);
			_drawTtl = CreateTtlPaintCallback(lifetime, [=] { repaint(); });
		} else if (!_parent->data()->out()) {
			const auto &data = &_parent->data()->history()->owner();
			_parent->data()->removeFromSharedMediaIndex();
			setDocumentLinks(_data, realParent, [=] {
				_openl = nullptr;

				auto lifetime = std::make_shared<rpl::lifetime>();
				TTLVoiceStops(fullId) | rpl::start_with_next([=]() mutable {
					if (lifetime) {
						base::take(lifetime)->destroy();
					}
					if (const auto item = data->message(fullId)) {
						// Destroys this.
						ClearMediaAsExpired(item);
					}
				}, *lifetime);

				return false;
			});
		} else {
			setDocumentLinks(_data, realParent);
		}
	} else {
		setDocumentLinks(_data, realParent);
	}

	setStatusSize(Ui::FileStatusSizeReady);
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

void Document::createComponents() {
	uint64 mask = 0;
	if (_data->isVoiceMessage() || _transcribedRound) {
		mask |= HistoryDocumentVoice::Bit();
	} else {
		mask |= HistoryDocumentNamed::Bit();
		if (_data->hasThumbnail() && !_data->isSong()) {
			_data->loadThumbnail(_realParent->fullId());
			mask |= HistoryDocumentThumbed::Bit();
		}
	}
	UpdateComponents(mask);
	if (const auto thumbed = Get<HistoryDocumentThumbed>()) {
		thumbed->linksavel = std::make_shared<DocumentSaveClickHandler>(
			_data,
			_realParent->fullId());
		thumbed->linkopenwithl = std::make_shared<DocumentOpenWithClickHandler>(
			_data,
			_realParent->fullId());
		thumbed->linkcancell = std::make_shared<DocumentCancelClickHandler>(
			_data,
			crl::guard(this, [=](FullMsgId id) {
				_parent->delegate()->elementCancelUpload(id);
			}),
			_realParent->fullId());
	}
	if (const auto voice = Get<HistoryDocumentVoice>()) {
		voice->seekl = !_parent->data()->media()->ttlSeconds()
			? std::make_shared<VoiceSeekClickHandler>(_data, [](FullMsgId) {})
			: nullptr;
		if (_transcribedRound) {
			voice->round = std::make_unique<::Media::Player::RoundPainter>(
				_realParent);
		}
	}
}

void Document::fillNamedFromData(not_null<HistoryDocumentNamed*> named) {
	const auto nameString = named->name = CleanTagSymbols(
		Ui::Text::FormatSongNameFor(_data).string());
	named->namew = st::semiboldFont->width(nameString);
}

QSize Document::countOptimalSize() {
	auto hasTranscribe = false;
	const auto voice = Get<HistoryDocumentVoice>();
	if (voice) {
		const auto history = _realParent->history();
		const auto session = &history->session();
		const auto transcribes = &session->api().transcribes();
		if (_parent->data()->media()->ttlSeconds()
			|| _realParent->isScheduled()
			|| (!session->premium()
				&& !transcribes->freeFor(_realParent)
				&& !transcribes->trialsSupport())) {
			voice->transcribe = nullptr;
			voice->transcribeText = {};
		} else {
			const auto creating = !voice->transcribe;
			if (creating) {
				voice->transcribe = std::make_unique<TranscribeButton>(
					_realParent,
					false);
			}
			const auto &entry = transcribes->entry(_realParent);
			const auto update = [=] { repaint(); };
			voice->transcribe->setLoading(
				entry.shown && (entry.requestId || entry.pending),
				update);
			auto text = (entry.requestId || !entry.shown)
				? TextWithEntities()
				: entry.toolong
				? Ui::Text::Italic(tr::lng_audio_transcribe_long(tr::now))
				: entry.failed
				? Ui::Text::Italic(tr::lng_attach_failed(tr::now))
				: TextWithEntities{
					entry.result + (entry.pending ? " [...]" : ""),
				};
			voice->transcribe->setOpened(
				!text.empty(),
				creating ? Fn<void()>() : update);
			if (text.empty()) {
				voice->transcribeText = {};
			} else {
				const auto minResizeWidth = st::minPhotoSize
					- st::msgPadding.left()
					- st::msgPadding.right();
				voice->transcribeText = Ui::Text::String(minResizeWidth);
				voice->transcribeText.setMarkedText(
					st::messageTextStyle,
					text);
				hasTranscribe = true;
				if (const auto skipBlockWidth = _parent->hasVisibleText()
					? 0
					: _parent->skipBlockWidth()) {
					voice->transcribeText.updateSkipBlock(
						skipBlockWidth,
						_parent->skipBlockHeight());
				}
			}
		}
	}

	auto thumbed = Get<HistoryDocumentThumbed>();
	const auto &st = thumbed ? st::msgFileThumbLayout : st::msgFileLayout;
	if (thumbed) {
		const auto &location = _data->thumbnailLocation();
		auto tw = style::ConvertScale(location.width());
		auto th = style::ConvertScale(location.height());
		if (tw > th) {
			thumbed->thumbw = (tw * st.thumbSize) / th;
		} else {
			thumbed->thumbw = st.thumbSize;
		}
	}

	auto maxWidth = st::msgFileMinWidth;

	const auto tleft = st.padding.left() + st.thumbSize + st.thumbSkip;
	const auto tright = st.padding.right();
	if (thumbed) {
		accumulate_max(maxWidth, tleft + MaxStatusWidth(_data) + tright);
	} else {
		auto unread = (_data->isVoiceMessage() || _transcribedRound)
			? (st::mediaUnreadSkip + st::mediaUnreadSize)
			: 0;
		accumulate_max(maxWidth, tleft + MaxStatusWidth(_data) + unread + _parent->skipBlockWidth() + st::msgPadding.right());
	}

	if (auto named = Get<HistoryDocumentNamed>()) {
		accumulate_max(maxWidth, tleft + named->namew + tright);
		accumulate_min(maxWidth, st::msgMaxWidth);
	}
	if (voice && voice->transcribe) {
		maxWidth += st::historyTranscribeSkip
			+ voice->transcribe->size().width();
	}

	auto minHeight = st.padding.top() + st.thumbSize + st.padding.bottom();
	if (isBubbleBottom() && !hasTranscribe && _parent->bottomInfoIsWide()) {
		minHeight += st::msgDateFont->height - st::msgDateDelta.y();
	}
	if (!isBubbleTop()) {
		minHeight -= st::msgFileTopMinus;
	}

	if (hasTranscribe) {
		auto captionw = maxWidth
			- st::msgPadding.left()
			- st::msgPadding.right();
		minHeight += voice->transcribeText.countHeight(captionw);
		if (isBubbleBottom()) {
			minHeight += st::msgPadding.bottom();
		}
	}
	return { maxWidth, minHeight };
}

QSize Document::countCurrentSize(int newWidth) {
	const auto captioned = Get<HistoryDocumentCaptioned>();
	const auto voice = Get<HistoryDocumentVoice>();
	const auto hasTranscribe = voice && !voice->transcribeText.isEmpty();
	if (!captioned && !hasTranscribe) {
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
	if (hasTranscribe) {
		newHeight += voice->transcribeText.countHeight(captionw);
		if (captioned) {
			newHeight += st::mediaCaptionSkip;
		} else if (isBubbleBottom()) {
			newHeight += st::msgPadding.bottom();
		}
	}
	if (captioned) {
		newHeight += captioned->caption.countHeight(captionw);
		if (isBubbleBottom()) {
			newHeight += st::msgPadding.bottom();
		}
	}

	return { newWidth, newHeight };
}

void Document::draw(Painter &p, const PaintContext &context) const {
	draw(p, context, width(), LayoutMode::Full, adjustedBubbleRounding());
}

void Document::draw(
		Painter &p,
		const PaintContext &context,
		int width,
		LayoutMode mode,
		Ui::BubbleRounding outsideRounding) const {
	if (width < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	ensureDataMediaCreated();

	const auto cornerDownload = downloadInCorner();

	if (!_dataMedia->canBePlayed(_realParent)) {
		_dataMedia->automaticLoad(_realParent->fullId(), _realParent);
	}
	bool loaded = dataLoaded(), displayLoading = _data->displayLoading();
	const auto sti = context.imageStyle();
	const auto stm = context.messageStyle();

	int captionw = width - st::msgPadding.left() - st::msgPadding.right();

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
	const auto nameleft = st.padding.left() + st.thumbSize + st.thumbSkip;
	const auto nametop = st.nameTop - topMinus;
	const auto nameright = st.padding.right();
	const auto statustop = st.statusTop - topMinus;
	const auto linktop = st.linkTop - topMinus;
	const auto bottom = st.padding.top() + st.thumbSize + st.padding.bottom() - topMinus;
	const auto rthumb = style::rtlrect(st.padding.left(), st.padding.top() - topMinus, st.thumbSize, st.thumbSize, width);
	const auto innerSize = st::msgFileLayout.thumbSize;
	const auto inner = QRect(rthumb.x() + (rthumb.width() - innerSize) / 2, rthumb.y() + (rthumb.height() - innerSize) / 2, innerSize, innerSize);
	const auto radialOpacity = radial ? _animation->radial.opacity() : 1.;
	if (thumbed) {
		const auto rounding = thumbRounding(mode, outsideRounding);
		validateThumbnail(thumbed, st.thumbSize, rounding);
		p.drawImage(rthumb, thumbed->thumbnail);
		if (context.selected()) {
			FillThumbnailOverlay(p, rthumb, rounding, context);
		}

		if (radial || (!loaded && !_data->loading()) || _data->waitingForAlbum()) {
			const auto backOpacity = (loaded && !_data->uploading()) ? radialOpacity : 1.;
			p.setPen(Qt::NoPen);
			p.setBrush(sti->msgDateImgBg);
			p.setOpacity(backOpacity * p.opacity());

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(inner);
			}

			const auto &icon = _data->waitingForAlbum()
				? sti->historyFileThumbWaiting
				: (radial || _data->loading())
				? sti->historyFileThumbCancel
				: sti->historyFileThumbDownload;
			const auto previous = _data->waitingForAlbum()
				? &sti->historyFileThumbCancel
				: nullptr;
			p.setOpacity(backOpacity);
			if (previous && radialOpacity > 0. && radialOpacity < 1.) {
				PaintInterpolatedIcon(p, icon, *previous, radialOpacity, inner);
			} else {
				icon.paintInCenter(p, inner);
			}
			p.setOpacity(1.);
			if (radial) {
				QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
				_animation->radial.draw(p, rinner, st::msgFileRadialLine, sti->historyFileThumbRadialFg);
			}
		}

		if (_data->status != FileUploadFailed) {
			const auto &lnk = (_data->loading() || _data->uploading())
				? thumbed->linkcancell
				: dataLoaded()
				? thumbed->linkopenwithl
				: thumbed->linksavel;
			bool over = ClickHandler::showAsActive(lnk);
			p.setFont(over ? st::semiboldFont->underline() : st::semiboldFont);
			p.setPen(stm->msgFileThumbLinkFg);
			p.drawTextLeft(nameleft, linktop, width, thumbed->link, thumbed->linkw);
		}
	} else {
		p.setPen(Qt::NoPen);

		const auto hasTtlBadge = _parent->data()->media()
			&& _parent->data()->media()->ttlSeconds()
			&& _openl;
		const auto ttlRect = hasTtlBadge ? TTLRectFromInner(inner) : QRect();

		const auto coverDrawn = _data->isSongWithCover()
			&& DrawThumbnailAsSongCover(
				p,
				context.st->songCoverOverlayFg(),
				_dataMedia,
				inner,
				context.selected());
		if (!coverDrawn) {
			if (_transcribedRound) {
				if (const auto voice = Get<HistoryDocumentVoice>()) {
					if (const auto &round = voice->round) {
						if (round->fillFrame(inner.size())) {
							p.drawImage(inner.topLeft(), round->frame());
						} else {
							DrawThumbnailAsSongCover(
								p,
								st::transparent,
								_dataMedia,
								inner,
								context.selected());
						}
					}
				}
			} else {
				auto hq = PainterHighQualityEnabler(p);
				p.setBrush(stm->msgFileBg);
				p.drawEllipse(inner);
			}
		}

		const auto &icon = [&]() -> const style::icon& {
			if (_data->waitingForAlbum()) {
				return _data->isSongWithCover()
					? sti->historyFileThumbWaiting
					: stm->historyFileWaiting;
			} else if (!cornerDownload
				&& (_data->loading() || _data->uploading())) {
				return _data->isSongWithCover()
					? sti->historyFileThumbCancel
					: stm->historyFileCancel;
			} else if (showPause) {
				return _data->isSongWithCover()
					? sti->historyFileThumbPause
					: stm->historyFilePause;
			} else if (loaded || _dataMedia->canBePlayed(_realParent)) {
				return _dataMedia->canBePlayed(_realParent)
					? (_data->isSongWithCover()
						? sti->historyFileThumbPlay
						: stm->historyFilePlay)
					: _data->isImage()
					? stm->historyFileImage
					: stm->historyFileDocument;
			} else {
				return _data->isSongWithCover()
					? sti->historyFileThumbDownload
					: stm->historyFileDownload;
			}
		}();
		const auto previous = _data->waitingForAlbum()
			? &stm->historyFileCancel
			: nullptr;

		const auto paintContent = [&](QPainter &q) {
			constexpr auto kPenWidth = 1.5;
			if (_drawTtl) {
				_drawTtl(q, inner, context.st->historyFileInIconFg()->c);

				const auto voice = Get<HistoryDocumentVoice>();
				const auto progress = (voice && voice->playback)
					? voice->playback->progress.current()
					: 0.;

				if (progress > 0.) {
					auto pen = stm->msgBg->p;
					pen.setWidthF(style::ConvertScaleExact(kPenWidth));
					pen.setCapStyle(Qt::RoundCap);
					q.setPen(pen);

					const auto from = arc::kQuarterLength;
					const auto len = std::round(arc::kFullLength
						* (1. - progress));
					const auto stepInside = pen.widthF() * 2;
					auto hq = PainterHighQualityEnabler(q);
					q.drawArc(inner - Margins(stepInside), from, len);
				}
			} else if (previous && radialOpacity > 0. && radialOpacity < 1.) {
				PaintInterpolatedIcon(q, icon, *previous, radialOpacity, inner);
			} else {
				icon.paintInCenter(q, inner);
			}

			if (radial && !cornerDownload) {
				QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
				_animation->radial.draw(q, rinner, st::msgFileRadialLine, stm->historyFileRadialFg);
			}
			if (hasTtlBadge) {
				{
					auto hq = PainterHighQualityEnabler(q);
					p.setBrush(stm->msgFileBg);
					q.setPen(Qt::NoPen);
					p.drawEllipse(ttlRect);
					auto pen = stm->msgBg->p;
					pen.setWidthF(style::ConvertScaleExact(kPenWidth));
					q.setPen(pen);
					q.setBrush(Qt::NoBrush);
					q.drawEllipse(ttlRect);
				}
				stm->historyVoiceMessageTTL.paintInCenter(q, ttlRect);
			}
		};
		if (_data->isSongWithCover() || !usesBubblePattern(context)) {
			paintContent(p);
		} else {
			Ui::PaintPatternBubblePart(
				p,
				context.viewport,
				context.bubblesPattern->pixmap,
				hasTtlBadge ? inner.united(ttlRect) : inner,
				paintContent,
				_iconCache);
		}

		drawCornerDownload(p, context, mode);
	}
	auto namewidth = width - nameleft - nameright;
	auto statuswidth = namewidth;

	auto voiceStatusOverride = QString();
	const auto voice = Get<HistoryDocumentVoice>();
	if (voice) {
		ensureDataMediaCreated();

		{
			const auto voiceData = _data->isVideoMessage()
				? _data->round()
				: _data->voice();
			if (voiceData && voiceData->waveform.isEmpty()) {
				if (loaded) {
					Local::countVoiceWaveform(_dataMedia.get());
				}
			}
		}

		const auto progress = [&] {
			if (!context.outbg
				&& !voice->playback
				&& _realParent->hasUnreadMediaFlag()) {
				return 1.;
			}
			if (voice->seeking()) {
				return voice->seekingCurrent();
			} else if (voice->playback) {
				return voice->playback->progress.current();
			}
			return 0.;
		}();
		if (voice->seeking()) {
			voiceStatusOverride = Ui::FormatPlayedText(
				base::SafeRound(progress * voice->lastDurationMs) / 1000,
				voice->lastDurationMs / 1000);
		}
		if (voice->transcribe) {
			const auto size = voice->transcribe->size();
			namewidth -= st::historyTranscribeSkip + size.width();
			const auto x = nameleft + namewidth + st::historyTranscribeSkip;
			const auto y = st.padding.top() - topMinus;
			voice->transcribe->paint(p, x, y, context);
		}
		p.save();
		p.translate(nameleft, st.padding.top() - topMinus);

		if (_transcribedRound) {
			FillWaveform(_data->round());
		}
		const auto inTTLViewer = _parent->delegate()->elementContext()
			== Context::TTLViewer;
		PaintWaveform(p,
			context,
			_transcribedRound ? _data->round() : _data->voice(),
			namewidth + st::msgWaveformSkip,
			progress,
			inTTLViewer);
		p.restore();
	} else if (auto named = Get<HistoryDocumentNamed>()) {
		p.setFont(st::semiboldFont);
		p.setPen(stm->historyFileNameFg);
		const auto elided = (namewidth < named->namew);
		if (elided) {
			p.drawTextLeft(nameleft, nametop, width, st::semiboldFont->elided(named->name, namewidth, Qt::ElideMiddle));
		} else {
			p.drawTextLeft(nameleft, nametop, width, named->name, named->namew);
		}
		_tooltipFilename.setElided(elided);
	}

	auto statusText = voiceStatusOverride.isEmpty() ? _statusText : voiceStatusOverride;
	p.setFont(st::normalFont);
	p.setPen(stm->mediaFg);
	p.drawTextLeft(nameleft, statustop, width, statusText);

	if (_realParent->hasUnreadMediaFlag()) {
		auto w = st::normalFont->width(statusText);
		if (w + st::mediaUnreadSkip + st::mediaUnreadSize <= statuswidth) {
			p.setPen(Qt::NoPen);
			p.setBrush(stm->msgFileBg);

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(style::rtlrect(nameleft + w + st::mediaUnreadSkip, statustop + st::mediaUnreadTop, st::mediaUnreadSize, st::mediaUnreadSize, width));
			}
		}
	}

	auto selection = context.selection;
	auto captiontop = bottom;
	if (voice && !voice->transcribeText.isEmpty()) {
		p.setPen(stm->historyTextFg);
		voice->transcribeText.draw(p, st::msgPadding.left(), bottom, captionw, style::al_left, 0, -1, selection);
		captiontop += voice->transcribeText.countHeight(captionw) + st::mediaCaptionSkip;
		selection = HistoryView::UnshiftItemSelection(selection, voice->transcribeText);
	}
	if (const auto captioned = Get<HistoryDocumentCaptioned>()) {
		p.setPen(stm->historyTextFg);
		_parent->prepareCustomEmojiPaint(p, context, captioned->caption);
		auto highlightRequest = context.computeHighlightCache();
		captioned->caption.draw(p, {
			.position = { st::msgPadding.left(), captiontop },
			.availableWidth = captionw,
			.palette = &stm->textPalette,
			.pre = stm->preCache.get(),
			.blockquote = context.quoteCache(parent()->contentColorIndex()),
			.colors = context.st->highlightColors(),
			.spoiler = Ui::Text::DefaultSpoilerCache(),
			.now = context.now,
			.pausedEmoji = context.paused || On(PowerSaving::kEmojiChat),
			.pausedSpoiler = context.paused || On(PowerSaving::kChatSpoiler),
			.selection = selection,
			.highlight = highlightRequest ? &*highlightRequest : nullptr,
		});
	}
}

Ui::BubbleRounding Document::thumbRounding(
		LayoutMode mode,
		Ui::BubbleRounding outsideRounding) const {
	using Corner = Ui::BubbleCornerRounding;
	if (mode != LayoutMode::Grouped && _parent->media() != this) {
		return Ui::BubbleRounding(); // In a WebPage preview.
	}
	const auto hasCaption = Has<HistoryDocumentCaptioned>();
	const auto adjust = [&](Corner already, bool skip = false) {
		return (already == Corner::Large && !skip)
			? Corner::Large
			: Corner::Small;
	};
	auto result = Ui::BubbleRounding();
	result.topLeft = adjust(outsideRounding.topLeft);
	result.bottomLeft = adjust(outsideRounding.bottomLeft, hasCaption);
	result.topRight = result.bottomRight = Corner::Small;
	return result;
}

void Document::validateThumbnail(
		not_null<const HistoryDocumentThumbed*> thumbed,
		int size,
		Ui::BubbleRounding rounding) const {
	const auto normal = _dataMedia->thumbnail();
	const auto blurred = _dataMedia->thumbnailInline();
	if (!normal && !blurred) {
		return;
	}
	const auto outer = QSize(size, size);
	if ((thumbed->thumbnail.size() == outer * style::DevicePixelRatio())
		&& (thumbed->blurred == !normal)
		&& (thumbed->rounding == rounding)) {
		return;
	}
	const auto small = (rounding == Ui::BubbleRounding());
	auto image = normal ? normal : blurred;
	const auto imageWidth = thumbed->thumbw * style::DevicePixelRatio();
	auto thumbnail = Images::Prepare(image->original(), imageWidth, {
		.options = (normal ? Images::Option() : Images::Option::Blur)
			| (small ? Images::Option::RoundSmall : Images::Option()),
		.outer = outer,
	});
	if (!small) {
		using Corner = Ui::BubbleCornerRounding;
		using Radius = Ui::CachedCornerRadius;
		auto corners = std::array<QImage, 4>();
		const auto &small = Ui::CachedCornersMasks(Radius::ThumbSmall);
		const auto &large = Ui::CachedCornersMasks(Radius::ThumbLarge);
		for (auto i = 0; i != 4; ++i) {
			switch (rounding[i]) {
			case Corner::Small: corners[i] = small[i]; break;
			case Corner::Large: corners[i] = large[i]; break;
			}
		}
		thumbnail = Images::Round(std::move(thumbnail), corners);
	}
	thumbed->thumbnail = std::move(thumbnail);
	thumbed->blurred = !normal;
	thumbed->rounding = rounding;
}

bool Document::hasHeavyPart() const {
	return (_dataMedia != nullptr);
}

void Document::unloadHeavyPart() {
	_dataMedia = nullptr;
	if (const auto captioned = Get<HistoryDocumentCaptioned>()) {
		captioned->caption.unloadPersistentAnimation();
	}
}

void Document::ensureDataMediaCreated() const {
	if (_dataMedia) {
		return;
	}
	_dataMedia = _data->createMediaView();
	if (Get<HistoryDocumentThumbed>()
		|| _data->isSongWithCover()
		|| _transcribedRound) {
		_dataMedia->thumbnailWanted(_realParent->fullId());
	}
	history()->owner().registerHeavyViewPart(_parent);
}

bool Document::downloadInCorner() const {
	return _data->isAudioFile()
		&& _realParent->allowsForward()
		&& _data->canBeStreamed(_realParent)
		&& !_data->inappPlaybackFailed();
}

void Document::drawCornerDownload(
		Painter &p,
		const PaintContext &context,
		LayoutMode mode) const {
	if (dataLoaded()
		|| _data->loadedInMediaCache()
		|| !downloadInCorner()) {
		return;
	}
	auto topMinus = isBubbleTop() ? 0 : st::msgFileTopMinus;
	const auto stm = context.messageStyle();
	const auto thumbed = false;
	const auto &st = (mode == LayoutMode::Full)
		? (thumbed ? st::msgFileThumbLayout : st::msgFileLayout)
		: (thumbed ? st::msgFileThumbLayoutGrouped : st::msgFileLayoutGrouped);
	const auto shift = st::historyAudioDownloadShift;
	const auto size = st::historyAudioDownloadSize;
	const auto inner = style::rtlrect(st.padding.left() + shift, st.padding.top() - topMinus + shift, size, size, width());
	const auto bubblePattern = usesBubblePattern(context);
	if (bubblePattern) {
		p.setPen(Qt::NoPen);
	} else {
		auto pen = stm->msgBg->p;
		pen.setWidth(st::lineWidth);
		p.setPen(pen);
	}
	p.setBrush(stm->msgFileBg);
	{
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(inner);
	}
	const auto &icon = _data->loading()
		? stm->historyAudioCancel
		: stm->historyAudioDownload;
	const auto paintContent = [&](QPainter &q) {
		if (bubblePattern) {
			auto hq = PainterHighQualityEnabler(q);
			auto pen = stm->msgBg->p;
			pen.setWidth(st::lineWidth);
			q.setPen(pen);
			q.setBrush(Qt::NoBrush);
			q.drawEllipse(inner);
		}
		icon.paintInCenter(q, inner);
		if (_animation && _animation->radial.animating()) {
			const auto rinner = inner.marginsRemoved(QMargins(st::historyAudioRadialLine, st::historyAudioRadialLine, st::historyAudioRadialLine, st::historyAudioRadialLine));
			_animation->radial.draw(q, rinner, st::historyAudioRadialLine, stm->historyFileRadialFg);
		}
	};
	if (bubblePattern) {
		const auto add = st::lineWidth * 2;
		const auto target = inner.marginsAdded({ add, add, add, add });
		Ui::PaintPatternBubblePart(
			p,
			context.viewport,
			context.bubblesPattern->pixmap,
			target,
			paintContent,
			_cornerDownloadCache);
	} else {
		paintContent(p);
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

	auto result = TextState(_parent);

	if (width < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	}

	ensureDataMediaCreated();
	bool loaded = dataLoaded();

	updateStatusText();

	const auto topMinus = isBubbleTop() ? 0 : st::msgFileTopMinus;
	const auto thumbed = Get<HistoryDocumentThumbed>();
	const auto &st = (mode == LayoutMode::Full)
		? (thumbed ? st::msgFileThumbLayout : st::msgFileLayout)
		: (thumbed ? st::msgFileThumbLayoutGrouped : st::msgFileLayoutGrouped);
	const auto nameleft = st.padding.left() + st.thumbSize + st.thumbSkip;
	const auto nametop = st.nameTop - topMinus;
	const auto nameright = st.padding.right();
	auto namewidth = width - nameleft - nameright;
	const auto linktop = st.linkTop - topMinus;
	auto bottom = st.padding.top() + st.thumbSize + st.padding.bottom() - topMinus;
	const auto rthumb = style::rtlrect(st.padding.left(), st.padding.top() - topMinus, st.thumbSize, st.thumbSize, width);
	const auto innerSize = st::msgFileLayout.thumbSize;
	const auto inner = QRect(rthumb.x() + (rthumb.width() - innerSize) / 2, rthumb.y() + (rthumb.height() - innerSize) / 2, innerSize, innerSize);

	const auto filenameMoused = QRect(nameleft, nametop, namewidth, st::semiboldFont->height).contains(point);
	_tooltipFilename.setMoused(filenameMoused);
	if (const auto thumbed = Get<HistoryDocumentThumbed>()) {
		if ((_data->loading() || _data->uploading()) && rthumb.contains(point)) {
			result.link = _cancell;
			return result;
		}

		if (_data->status != FileUploadFailed) {
			if (style::rtlrect(nameleft, linktop, thumbed->linkw, st::semiboldFont->height, width).contains(point)) {
				result.link = (_data->loading() || _data->uploading())
					? thumbed->linkcancell
					: dataLoaded()
					? thumbed->linkopenwithl
					: thumbed->linksavel;
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

	const auto voice = Get<HistoryDocumentVoice>();
	auto transcribeLength = 0;
	auto transcribeHeight = 0;
	auto painth = layout.height();
	if (voice) {
		auto waveformbottom = st.padding.top() - topMinus + st::msgWaveformMax + st::msgWaveformMin;
		if (voice->transcribe) {
			const auto size = voice->transcribe->size();
			namewidth -= st::historyTranscribeSkip + size.width();
			const auto x = nameleft + namewidth + st::historyTranscribeSkip;
			const auto y = st.padding.top() - topMinus;
			if (QRect(QPoint(x, y), size).contains(point)) {
				result.link = voice->transcribe->link();
				return result;
			}
		}
		if (QRect(nameleft, nametop, namewidth, waveformbottom - nametop).contains(point)) {
			const auto state = ::Media::Player::instance()->getState(AudioMsgId::Type::Voice);
			if (state.id == AudioMsgId(_data, _realParent->fullId(), state.id.externalPlayId())
				&& !::Media::Player::IsStoppedOrStopping(state.state)) {
				if (!voice->seeking()) {
					voice->setSeekingStart((point.x() - nameleft) / float64(namewidth));
				}
				result.link = voice->seekl;
				return result;
			}
		}
		transcribeLength = voice->transcribeText.length();
		if (transcribeLength > 0) {
			auto captionw = width - st::msgPadding.left() - st::msgPadding.right();
			transcribeHeight = voice->transcribeText.countHeight(captionw);
			painth -= transcribeHeight;
			if (point.y() >= bottom && point.y() < bottom + transcribeHeight) {
				result = TextState(_parent, voice->transcribeText.getState(
					point - QPoint(st::msgPadding.left(), bottom),
					width - st::msgPadding.left() - st::msgPadding.right(),
					request.forText()));
				return result;
			}
			bottom += transcribeHeight;
		}
	}

	if (const auto captioned = Get<HistoryDocumentCaptioned>()) {
		if (point.y() >= bottom) {
			result.symbol += transcribeLength;
		}
		if (transcribeHeight) {
			painth -= st::mediaCaptionSkip;
			bottom += st::mediaCaptionSkip;
		}
		if (point.y() >= bottom) {
			result = TextState(_parent, captioned->caption.getState(
				point - QPoint(st::msgPadding.left(), bottom),
				width - st::msgPadding.left() - st::msgPadding.right(),
				request.forText()));
			result.symbol += transcribeLength;
			return result;
		}
		auto captionw = width - st::msgPadding.left() - st::msgPadding.right();
		painth -= captioned->caption.countHeight(captionw);
		if (isBubbleBottom()) {
			painth -= st::msgPadding.bottom();
		}
	} else if (transcribeHeight && isBubbleBottom()) {
		painth -= st::msgPadding.bottom();
	}
	const auto till = voice ? (nameleft + namewidth) : width;
	if (QRect(0, 0, till, painth).contains(point)
		&& (!_data->loading() || downloadInCorner())
		&& !_data->uploading()
		&& !_data->isNull()) {
		if (loaded || _dataMedia->canBePlayed(_realParent)) {
			result.link = _openl;
		} else {
			result.link = _savel;
		}
		_tooltipFilename.updateTooltipForLink(result.link.get());
		return result;
	}
	_tooltipFilename.updateTooltipForState(result);
	return result;
}

void Document::updatePressed(QPoint point) {
	// LayoutMode should be passed here.
	if (const auto voice = Get<HistoryDocumentVoice>()) {
		if (!voice->seeking()) {
			return;
		}
		const auto thumbed = Get<HistoryDocumentThumbed>();
		const auto &st = thumbed ? st::msgFileThumbLayout : st::msgFileLayout;
		const auto nameleft = st.padding.left() + st.thumbSize + st.thumbSkip;
		const auto nameright = st.padding.right();
		const auto transcribeWidth = voice->transcribe
			? (st::historyTranscribeSkip + voice->transcribe->size().width())
			: 0;
		voice->setSeekingCurrent(std::clamp(
			(point.x() - nameleft)
				/ float64(width() - transcribeWidth - nameleft - nameright),
			0.,
			1.));
		repaint();
	}
}

TextSelection Document::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	auto transcribe = (const Ui::Text::String*)nullptr;
	auto caption = (const Ui::Text::String*)nullptr;
	if (const auto voice = Get<HistoryDocumentVoice>()) {
		transcribe = &voice->transcribeText;
	}
	if (const auto captioned = Get<HistoryDocumentCaptioned>()) {
		caption = &captioned->caption;
	}
	const auto transcribeLength = transcribe ? transcribe->length() : 0;
	if (transcribe && selection.from < transcribeLength) {
		const auto adjusted = transcribe->adjustSelection(selection, type);
		if (selection.to <= transcribeLength) {
			return adjusted;
		}
		selection = TextSelection(adjusted.from, selection.to);
	}
	if (caption && selection.to > transcribeLength) {
		auto unshifted = transcribe
			? HistoryView::UnshiftItemSelection(selection, *transcribe)
			: selection;
		const auto adjusted = caption->adjustSelection(unshifted, type);
		const auto shifted = transcribe
			? HistoryView::ShiftItemSelection(adjusted, *transcribe)
			: adjusted;
		if (selection.from >= transcribeLength) {
			return shifted;
		}
		selection = TextSelection(selection.from, shifted.to);
	}
	return selection;
}

uint16 Document::fullSelectionLength() const {
	auto result = uint16();
	if (const auto voice = Get<HistoryDocumentVoice>()) {
		result += voice->transcribeText.length();
	}
	if (const auto captioned = Get<HistoryDocumentCaptioned>()) {
		result += captioned->caption.length();
	}
	return result;
}

bool Document::hasTextForCopy() const {
	if (const auto voice = Get<HistoryDocumentVoice>()) {
		if (!voice->transcribeText.isEmpty()) {
			return true;
		}
	}
	return Has<HistoryDocumentCaptioned>();
}

TextForMimeData Document::selectedText(TextSelection selection) const {
	auto result = TextForMimeData();
	if (const auto voice = Get<HistoryDocumentVoice>()) {
		const auto length = voice->transcribeText.length();
		if (selection.from < length) {
			result.append(
				voice->transcribeText.toTextForMimeData(selection));
		}
		if (selection.to <= length) {
			return result;
		}
		selection = HistoryView::UnshiftItemSelection(
			selection,
			voice->transcribeText);
	}
	if (const auto captioned = Get<HistoryDocumentCaptioned>()) {
		if (!result.empty()) {
			result.append("\n\n");
		}
		result.append(captioned->caption.toTextForMimeData(selection));
	}
	return result;
}

SelectedQuote Document::selectedQuote(TextSelection selection) const {
	if (const auto voice = Get<HistoryDocumentVoice>()) {
		const auto length = voice->transcribeText.length();
		if (selection.from < length) {
			return {};
		}
		selection = HistoryView::UnshiftItemSelection(
			selection,
			voice->transcribeText);
	}
	if (const auto captioned = Get<HistoryDocumentCaptioned>()) {
		return Element::FindSelectedQuote(
			captioned->caption,
			selection,
			_realParent);
	}
	return {};
}

TextSelection Document::selectionFromQuote(
		const SelectedQuote &quote) const {
	if (const auto captioned = Get<HistoryDocumentCaptioned>()) {
		const auto result = Element::FindSelectionFromQuote(
			captioned->caption,
			quote);
		if (result.empty()) {
			return {};
		} else if (const auto voice = Get<HistoryDocumentVoice>()) {
			return HistoryView::ShiftItemSelection(
				result,
				voice->transcribeText);
		}
		return result;
	}
	return {};
}

bool Document::uploading() const {
	return _data->uploading();
}

void Document::setStatusSize(int64 newSize, TimeId realDuration) const {
	const auto duration = (_data->isSong()
		|| _data->isVoiceMessage()
		|| _transcribedRound)
		? _data->duration()
		: -1;
	File::setStatusSize(
		newSize,
		_data->size,
		(duration >= 0) ? duration / 1000 : -1,
		realDuration);
	if (auto thumbed = Get<HistoryDocumentThumbed>()) {
		if (_statusSize == Ui::FileStatusSizeReady) {
			thumbed->link = tr::lng_media_download(tr::now).toUpper();
		} else if (_statusSize == Ui::FileStatusSizeLoaded) {
			thumbed->link = tr::lng_media_open_with(tr::now).toUpper();
		} else if (_statusSize == Ui::FileStatusSizeFailed) {
			thumbed->link = tr::lng_media_download(tr::now).toUpper();
		} else if (_statusSize >= 0) {
			thumbed->link = tr::lng_media_cancel(tr::now).toUpper();
		} else {
			thumbed->link = tr::lng_media_open_with(tr::now).toUpper();
		}
		thumbed->linkw = st::semiboldFont->width(thumbed->link);
	}
}

bool Document::updateStatusText() const {
	auto showPause = false;
	auto statusSize = int64();
	auto realDuration = TimeId();
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

	if (_data->isVoiceMessage() || _transcribedRound) {
		const auto state = ::Media::Player::instance()->getState(AudioMsgId::Type::Voice);
		if (state.id == AudioMsgId(_data, _realParent->fullId(), state.id.externalPlayId())
			&& !::Media::Player::IsStoppedOrStopping(state.state)) {
			if (auto voice = Get<HistoryDocumentVoice>()) {
				bool was = (voice->playback != nullptr);
				voice->ensurePlayback(this);
				if (!was || state.position != voice->playback->position) {
					auto prg = state.length
						? std::clamp(
							float64(state.position) / state.length,
							0.,
							1.)
						: 0.;
					if (voice->playback->position < state.position) {
						voice->playback->progress.start(prg);
					} else {
						voice->playback->progress = anim::value(0., prg);
					}
					voice->playback->position = state.position;
					voice->playback->progressAnimation.start();
				}
				voice->lastDurationMs = static_cast<int>((state.length * 1000LL) / state.frequency); // Bad :(
			}

			statusSize = -1 - (state.position / state.frequency);
			realDuration = (state.length / state.frequency);
			showPause = ::Media::Player::ShowPauseIcon(state.state);
		} else {
			if (auto voice = Get<HistoryDocumentVoice>()) {
				voice->checkPlaybackFinished();
			}
		}
		if (!showPause && (state.id == AudioMsgId(_data, _realParent->fullId(), state.id.externalPlayId()))) {
			showPause = ::Media::Player::instance()->isSeeking(AudioMsgId::Type::Voice);
		}
	} else if (_data->isAudioFile()) {
		const auto state = ::Media::Player::instance()->getState(AudioMsgId::Type::Song);
		if (state.id == AudioMsgId(_data, _realParent->fullId(), state.id.externalPlayId())
			&& !::Media::Player::IsStoppedOrStopping(state.state)) {
			statusSize = -1 - (state.position / state.frequency);
			realDuration = (state.length / state.frequency);
			showPause = ::Media::Player::ShowPauseIcon(state.state);
		} else {
		}
		if (!showPause && (state.id == AudioMsgId(_data, _realParent->fullId(), state.id.externalPlayId()))) {
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
	return QMargins(padding.left(), padding.top(), padding.right(), padding.bottom());
}

void Document::refreshCaption(bool last) {
	const auto now = Get<HistoryDocumentCaptioned>();
	auto caption = createCaption();
	if (!caption.isEmpty()) {
		if (now) {
			return;
		}
		AddComponents(HistoryDocumentCaptioned::Bit());
		auto captioned = Get<HistoryDocumentCaptioned>();
		captioned->caption = std::move(caption);
		const auto skip = last ? _parent->skipBlockWidth() : 0;
		if (skip) {
			captioned->caption.updateSkipBlock(
				_parent->skipBlockWidth(),
				_parent->skipBlockHeight());
		} else {
			captioned->caption.removeSkipBlock();
		}
	} else if (now) {
		RemoveComponents(HistoryDocumentCaptioned::Bit());
	}
}

QSize Document::sizeForGroupingOptimal(int maxWidth, bool last) const {
	const auto thumbed = Get<HistoryDocumentThumbed>();
	const auto &st = (thumbed ? st::msgFileThumbLayoutGrouped : st::msgFileLayoutGrouped);
	auto height = st.padding.top() + st.thumbSize + st.padding.bottom();

	const_cast<Document*>(this)->refreshCaption(last);

	if (const auto captioned = Get<HistoryDocumentCaptioned>()) {
		auto captionw = maxWidth
			- st::msgPadding.left()
			- st::msgPadding.right();
		height += captioned->caption.countHeight(captionw);
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
		height += captioned->caption.countHeight(captionw);
	}
	return { maxWidth(), height };
}

void Document::drawGrouped(
		Painter &p,
		const PaintContext &context,
		const QRect &geometry,
		RectParts sides,
		Ui::BubbleRounding rounding,
		float64 highlightOpacity,
		not_null<uint64*> cacheKey,
		not_null<QPixmap*> cache) const {
	const auto maybeMediaHighlight = context.highlightPathCache
		&& context.highlightPathCache->isEmpty();
	p.translate(geometry.topLeft());
	draw(
		p,
		context.translated(-geometry.topLeft()),
		geometry.width(),
		LayoutMode::Grouped,
		rounding);
	if (maybeMediaHighlight
		&& !context.highlightPathCache->isEmpty()) {
		context.highlightPathCache->translate(geometry.topLeft());
	}
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
		if (voice->playback) {
			const auto dt = (now - voice->playback->progressAnimation.started())
				/ float64(2 * kAudioVoiceMsgUpdateView);
			if (dt >= 1.) {
				voice->playback->progressAnimation.stop();
				voice->playback->progress.finish();
			} else {
				voice->playback->progress.update(qMin(dt, 1.), anim::linear);
			}
			repaint();
			return (dt < 1.);
		}
	}
	return false;
}

void Document::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	if (auto voice = Get<HistoryDocumentVoice>()) {
		if (pressed && p == voice->seekl && !voice->seeking()) {
			voice->startSeeking();
		} else if (!pressed && voice->seeking()) {
			const auto type = AudioMsgId::Type::Voice;
			const auto state = ::Media::Player::instance()->getState(type);
			if (state.id == AudioMsgId(_data, _realParent->fullId(), state.id.externalPlayId()) && state.length) {
				const auto currentProgress = voice->seekingCurrent();
				::Media::Player::instance()->finishSeeking(
					AudioMsgId::Type::Voice,
					currentProgress);

				voice->ensurePlayback(this);
				voice->playback->position = 0;
				voice->playback->progress = anim::value(currentProgress, currentProgress);
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
		if (thumbed->linksavel) {
			thumbed->linksavel->setMessageId(fullId);
			thumbed->linkcancell->setMessageId(fullId);
		}
	}
	if (auto voice = Get<HistoryDocumentVoice>()) {
		if (voice->seekl) {
			voice->seekl->setMessageId(fullId);
		}
	}
}

void Document::parentTextUpdated() {
	RemoveComponents(HistoryDocumentCaptioned::Bit());
}

void Document::hideSpoilers() {
	if (const auto captioned = Get<HistoryDocumentCaptioned>()) {
		captioned->caption.setSpoilerRevealed(false, anim::type::instant);
	}
}

Ui::Text::String Document::createCaption() const {
	return File::createCaption(_realParent);
}

void Document::TooltipFilename::setElided(bool value) {
	if (_elided != value) {
		_elided = value;
		_stale = true;
	}
}

void Document::TooltipFilename::setMoused(bool value) {
	if (_moused != value) {
		_moused = value;
		_stale = true;
	}
}

void Document::TooltipFilename::setTooltipText(QString text) {
	if (_tooltip != text) {
		_tooltip = text;
		_stale = true;
	}
}

void Document::TooltipFilename::updateTooltipForLink(ClickHandler *link) {
	if (_lastLink != link) {
		_lastLink = link;
		_stale = true;
	}
	if (_stale && link) {
		_stale = false;
		link->setProperty(
			kDocumentFilenameTooltipProperty,
			(_elided && _moused) ? _tooltip : QString());
	}
}

void Document::TooltipFilename::updateTooltipForState(
		TextState &state) const {
	if (_elided && _moused) {
		state.customTooltip = true;
		state.customTooltipText = _tooltip;
	}
}

bool DrawThumbnailAsSongCover(
		Painter &p,
		const style::color &colored,
		const std::shared_ptr<Data::DocumentMedia> &dataMedia,
		const QRect &rect,
		bool selected) {
	if (!dataMedia) {
		return false;
	}

	auto cover = QPixmap();
	const auto scaled = [&](not_null<Image*> image) {
		const auto aspectRatio = Qt::KeepAspectRatioByExpanding;
		return image->size().scaled(rect.size(), aspectRatio);
	};
	const auto args = Images::PrepareArgs{
		.colored = &colored,
		.options = Images::Option::RoundCircle,
		.outer = rect.size(),
	};
	if (const auto normal = dataMedia->thumbnail()) {
		cover = normal->pixSingle(scaled(normal), args);
	} else if (const auto blurred = dataMedia->thumbnailInline()) {
		cover = blurred->pixSingle(scaled(blurred), args.blurred());
	} else {
		return false;
	}
	if (selected) {
		auto selectedCover = Images::Colored(
			cover.toImage(),
			p.textPalette().selectOverlay);
		cover = QPixmap::fromImage(
			std::move(selectedCover),
			Qt::ColorOnly);
	}
	p.drawPixmap(rect.topLeft(), cover);

	return true;
}

rpl::producer<> TTLVoiceStops(FullMsgId fullId) {
	return rpl::merge(
		::Media::Player::instance()->updatedNotifier(
		) | rpl::filter([=](::Media::Player::TrackState state) {
			using State = ::Media::Player::State;
			const auto badState = state.state == State::Stopped
				|| state.state == State::StoppedAtEnd
				|| state.state == State::StoppedAtError
				|| state.state == State::StoppedAtStart;
			return (state.id.contextId() != fullId) && !badState;
		}) | rpl::to_empty,
		::Media::Player::instance()->tracksFinished(
		) | rpl::filter([=](AudioMsgId::Type type) {
			return (type == AudioMsgId::Type::Voice);
		}) | rpl::to_empty,
		::Media::Player::instance()->stops(AudioMsgId::Type::Voice)
	);
}

} // namespace HistoryView
