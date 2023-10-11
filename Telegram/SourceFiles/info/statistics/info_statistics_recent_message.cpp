/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/statistics/info_statistics_recent_message.h"

#include "core/ui_integration.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "history/view/history_view_item_preview.h"
#include "info/statistics/info_statistics_common.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/spoiler_mess.h"
#include "ui/power_saving.h"
#include "ui/rect.h"
#include "ui/text/format_values.h"
#include "ui/text/text_options.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_statistics.h"

namespace Info::Statistics {
namespace {

[[nodiscard]] QImage PreparePreviewImage(
		QImage original,
		ImageRoundRadius radius,
		bool spoiler) {
	if (original.width() * 10 < original.height()
		|| original.height() * 10 < original.width()) {
		return QImage();
	}
	const auto factor = style::DevicePixelRatio();
	const auto size = st::peerListBoxItem.photoSize * factor;
	const auto scaled = original.scaled(
		QSize(size, size),
		Qt::KeepAspectRatioByExpanding,
		Qt::FastTransformation);
	auto square = scaled.copy(
		(scaled.width() - size) / 2,
		(scaled.height() - size) / 2,
		size,
		size
	).convertToFormat(QImage::Format_ARGB32_Premultiplied);
	if (spoiler) {
		square = Images::BlurLargeImage(
			std::move(square),
			style::ConvertScale(3) * factor);
	}
	square = Images::Round(std::move(square), radius);
	square.setDevicePixelRatio(factor);
	return square;
}

} // namespace

MessagePreview::MessagePreview(
	not_null<Ui::RpWidget*> parent,
	not_null<HistoryItem*> item,
	int views,
	int shares,
	QImage cachedPreview)
: Ui::RpWidget(parent)
, _item(item)
, _date(
	st::statisticsHeaderTitleTextStyle,
	Ui::FormatDateTime(ItemDateTime(item)))
, _views(
	st::defaultPeerListItem.nameStyle,
	tr::lng_stats_recent_messages_views(
		tr::now,
		lt_count_decimal,
		views))
, _shares(
	st::statisticsHeaderTitleTextStyle,
	tr::lng_stats_recent_messages_shares(
		tr::now,
		lt_count_decimal,
		shares))
, _viewsWidth(_views.maxWidth())
, _sharesWidth(_shares.maxWidth())
, _preview(std::move(cachedPreview)) {
	_text.setMarkedText(
		st::defaultPeerListItem.nameStyle,
		_item->toPreview({ .generateImages = false }).text,
		Ui::DialogTextOptions(),
		Core::MarkedTextContext{
			.session = &item->history()->session(),
			.customEmojiRepaint = [=] { update(); },
		});
	if (_preview.isNull()) {
		processPreview(item);
	}
}

void MessagePreview::processPreview(not_null<HistoryItem*> item) {
	if (const auto media = item->media()) {
		if (item->media()->hasSpoiler()) {
			_spoiler = std::make_unique<Ui::SpoilerAnimation>([=] {
				update();
			});
		}
		if (const auto photo = media->photo()) {
			_photoMedia = photo->createMediaView();
			_photoMedia->wanted(Data::PhotoSize::Large, item->fullId());
		} else if (const auto document = media->document()) {
			_documentMedia = document->createMediaView();
			_documentMedia->thumbnailWanted(item->fullId());
		}
	}
	const auto session = _photoMedia
		? &_photoMedia->owner()->session()
		: _documentMedia
		? &_documentMedia->owner()->session()
		: nullptr;
	if (!session) {
		return;
	}

	struct ThumbInfo final {
		bool loaded = false;
		Image *image = nullptr;
	};

	const auto computeThumbInfo = [=]() -> ThumbInfo {
		using Size = Data::PhotoSize;
		if (_documentMedia) {
			return { true, _documentMedia->thumbnail() };
		} else if (const auto large = _photoMedia->image(Size::Large)) {
			return { true, large };
		} else if (const auto thumbnail = _photoMedia->image(
				Size::Thumbnail)) {
			return { false, thumbnail };
		} else if (const auto small = _photoMedia->image(Size::Small)) {
			return { false, small };
		} else {
			return { false, _photoMedia->thumbnailInline() };
		}
	};

	rpl::single(rpl::empty) | rpl::then(
		session->downloaderTaskFinished()
	) | rpl::start_with_next([=] {
		const auto computed = computeThumbInfo();
		if (!computed.image) {
			if (_documentMedia && !_documentMedia->owner()->hasThumbnail()) {
				_preview = QImage();
				_lifetimeDownload.destroy();
			}
			return;
		} else if (computed.loaded) {
			_lifetimeDownload.destroy();
		}
		_preview = PreparePreviewImage(
			computed.image->original(),
			ImageRoundRadius::Large,
			!!_spoiler);
	}, _lifetimeDownload);
}

int MessagePreview::resizeGetHeight(int newWidth) {
	return st::peerListBoxItem.height;
}

void MessagePreview::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto padding = st::boxRowPadding.left() / 2;
	const auto rightWidth = std::max(_viewsWidth, _sharesWidth) + padding;
	const auto left = _preview.isNull()
		? st::peerListBoxItem.photoPosition.x()
		: st::peerListBoxItem.namePosition.x();
	if (left) {
		p.drawImage(st::peerListBoxItem.photoPosition, _preview);
		if (_spoiler) {
			const auto rect = QRect(
				st::peerListBoxItem.photoPosition,
				Size(st::peerListBoxItem.photoSize));
			const auto paused = On(PowerSaving::kChatSpoiler);
			FillSpoilerRect(
				p,
				rect,
				Images::CornersMaskRef(
					Images::CornersMask(st::roundRadiusLarge)),
				Ui::DefaultImageSpoiler().frame(
					_spoiler->index(crl::now(), paused)),
				_cornerCache);
		}
	}
	const auto topTextTop = st::peerListBoxItem.namePosition.y();
	const auto bottomTextTop = st::peerListBoxItem.statusPosition.y();

	p.setBrush(Qt::NoBrush);
	p.setPen(st::boxTextFg);
	_text.draw(p, {
		.position = { left, topTextTop },
		.outerWidth = width() - left,
		.availableWidth = width() - rightWidth - left,
		.spoiler = Ui::Text::DefaultSpoilerCache(),
		.now = crl::now(),
		.elisionHeight = st::statisticsDetailsPopupHeaderStyle.font->height,
	});
	_views.draw(p, {
		.position = { width() - _viewsWidth, topTextTop },
		.outerWidth = _viewsWidth,
		.availableWidth = _viewsWidth,
	});

	p.setPen(st::windowSubTextFg);
	_date.draw(p, {
		.position = { left, bottomTextTop },
		.outerWidth = width() - left,
		.availableWidth = width() - rightWidth - left,
	});
	_shares.draw(p, {
		.position = { width() - _sharesWidth, bottomTextTop },
		.outerWidth = _sharesWidth,
		.availableWidth = _sharesWidth,
	});
}

void MessagePreview::saveState(SavedState &state) const {
	if (!_lifetimeDownload) {
		state.recentPostPreviews[_item->fullId().msg] = _preview;
	}
}

} // namespace Info::Statistics
