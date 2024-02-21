/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_similar_channels.h"

#include "api/api_chat_participants.h"
#include "apiwrap.h"
#include "boxes/peer_lists_box.h"
#include "core/click_handler_types.h"
#include "data/data_channel.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "history/history.h"
#include "history/history_item.h"
#include "info/similar_channels/info_similar_channels_widget.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_premium.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/text_utilities.h"
#include "ui/dynamic_image.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/painter.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

using Channels = Api::ChatParticipants::Channels;

//void SimilarChannelsController::prepare() {
//	for (const auto &channel : _channels.list) {
//		auto row = std::make_unique<PeerListRow>(channel);
//		if (const auto count = channel->membersCount(); count > 1) {
//			row->setCustomStatus(tr::lng_chat_status_subscribers(
//				tr::now,
//				lt_count,
//				count));
//		}
//		delegate()->peerListAppendRow(std::move(row));
//	}
//	delegate()->peerListRefreshRows();
//}

[[nodiscard]] ClickHandlerPtr MakeViewAllLink(
		not_null<ChannelData*> channel,
		bool promoForNonPremium) {
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto strong = my.sessionWindow.get()) {
			Assert(channel != nullptr);
			if (promoForNonPremium && !channel->session().premium()) {
				const auto upto = Data::PremiumLimits(
					&channel->session()).similarChannelsPremium();
				Settings::ShowPremiumPromoToast(
					strong->uiShow(),
					tr::lng_similar_channels_premium_all(
						tr::now,
						lt_count,
						upto,
						lt_link,
						Ui::Text::Link(
							Ui::Text::Bold(
								tr::lng_similar_channels_premium_all_link(
									tr::now))),
						Ui::Text::RichLangValue),
					u"similar_channels"_q);
				return;
			}
			const auto api = &channel->session().api();
			const auto &list = api->chatParticipants().similar(channel);
			if (list.list.empty()) {
				return;
			}
			strong->showSection(
				std::make_shared<Info::Memento>(
					channel,
					Info::Section::Type::SimilarChannels));
		}
	});
}

} // namespace

SimilarChannels::SimilarChannels(not_null<Element*> parent)
: Media(parent) {
}

SimilarChannels::~SimilarChannels() {
	if (hasHeavyPart()) {
		unloadHeavyPart();
		parent()->checkHeavyPart();
	}
}

void SimilarChannels::clickHandlerActiveChanged(
		const ClickHandlerPtr &p,
		bool active) {
}

void SimilarChannels::clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) {
	for (auto &channel : _channels) {
		if (channel.link != p) {
			continue;
		}
		if (pressed) {
			if (!channel.ripple) {
				channel.ripple = std::make_unique<Ui::RippleAnimation>(
					st::defaultRippleAnimation,
					Ui::RippleAnimation::RoundRectMask(
						channel.geometry.size(),
						st::roundRadiusLarge),
					[=] { repaint(); });
			}
			channel.ripple->add(_lastPoint);
		} else if (channel.ripple) {
			channel.ripple->lastStop();
		}
		break;
	}
}

void SimilarChannels::draw(Painter &p, const PaintContext &context) const {
	if (!_toggled) {
		return;
	}
	const auto large = Ui::BubbleCornerRounding::Large;
	const auto geometry = QRect(0, 0, width(), height());
	Ui::PaintBubble(
		p,
		Ui::SimpleBubble{
			.st = context.st,
			.geometry = geometry,
			.pattern = context.bubblesPattern,
			.patternViewport = context.viewport,
			.outerWidth = width(),
			.rounding = { large, large, large, large },
		});
	const auto stm = context.messageStyle();
	{
		auto hq = PainterHighQualityEnabler(p);
		auto path = QPainterPath();
		const auto x = geometry.center().x();
		const auto y = geometry.y();
		const auto size = st::chatSimilarArrowSize;
		path.moveTo(x, y - size);
		path.lineTo(x + size, y);
		path.lineTo(x - size, y);
		path.lineTo(x, y - size);
		p.fillPath(path, stm->msgBg);
	}
	const auto padding = st::chatSimilarChannelPadding;
	p.setClipRect(geometry);
	_hasHeavyPart = 1;
	validateLastPremiumLock();
	const auto drawOne = [&](const Channel &channel) {
		const auto geometry = channel.geometry.translated(-_scrollLeft, 0);
		const auto right = geometry.x() + geometry.width();
		if (right <= 0) {
			return;
		}
		const auto subscribing = !channel.subscribed;
		if (subscribing) {
			channel.subscribed = 1;
			const auto raw = channel.thumbnail.get();
			channel.thumbnail->subscribeToUpdates([=] {
				for (const auto &channel : _channels) {
					if (channel.thumbnail.get() == raw) {
						channel.counterBgValid = 0;
						repaint();
					}
				}
			});
		}
		auto cachedp = std::optional<Painter>();
		const auto cached = (geometry.x() < padding.left())
			|| (right > width() - padding.right());
		if (cached) {
			ensureCacheReady(geometry.size());
			_roundedCache.fill(Qt::transparent);
			cachedp.emplace(&_roundedCache);
			cachedp->translate(-geometry.topLeft());
		}
		const auto q = cachedp ? &*cachedp : &p;
		if (channel.more) {
			channel.ripple.reset();
		} else if (channel.ripple) {
			q->setOpacity(st::historyPollRippleOpacity);
			channel.ripple->paint(
				*q,
				geometry.x(),
				geometry.y(),
				width(),
				&stm->msgWaveformInactive->c);
			if (channel.ripple->empty()) {
				channel.ripple.reset();
			}
			q->setOpacity(1.);
		}

		auto pen = stm->msgBg->p;
		auto left = geometry.x() + 2 * padding.left();
		const auto stroke = st::lineWidth * 2.;
		const auto add = stroke / 2.;
		const auto top = geometry.y() + padding.top();
		const auto size = st::chatSimilarChannelPhoto;
		const auto paintCircle = [&] {
			auto hq = PainterHighQualityEnabler(*q);
			q->drawEllipse(QRectF(left, top, size, size).marginsAdded(
				{ add, add, add, add }));
		};
		if (channel.more) {
			pen.setWidthF(stroke);
			p.setPen(pen);
			for (auto i = 2; i != 0;) {
				--i;
				if (const auto &thumbnail = _moreThumbnails[i]) {
					if (subscribing) {
						thumbnail->subscribeToUpdates([=] {
							repaint();
						});
					}
					q->drawImage(left, top, thumbnail->image(size));
					q->setBrush(Qt::NoBrush);
				} else {
					q->setBrush(st::windowBgRipple->c);
				}
				if (!i || !_moreThumbnails[i]) {
					paintCircle();
				}
				left -= padding.left();
			}
		} else {
			left -= padding.left();
		}
		q->drawImage(
			left,
			top,
			channel.thumbnail->image(size));
		if (channel.more) {
			q->setBrush(Qt::NoBrush);
			paintCircle();
		}
		if (!channel.counter.isEmpty()) {
			validateCounterBg(channel);
			const auto participants = channel.counterRect.translated(
				geometry.topLeft());
			q->drawImage(participants.topLeft(), channel.counterBg);
			const auto badge = participants.marginsRemoved(
				st::chatSimilarBadgePadding);
			auto textLeft = badge.x();
			const auto &font = st::chatSimilarBadgeFont;
			const auto textTop = badge.y() + font->ascent;
			const auto icon = !channel.more
				? &st::chatSimilarBadgeIcon
				: channel.moreLocked
				? &st::chatSimilarLockedIcon
				: nullptr;
			const auto position = !channel.more
				? st::chatSimilarBadgeIconPosition
				: st::chatSimilarLockedIconPosition;
			if (icon) {
				const auto skip = channel.more
					? (badge.width() - icon->width())
					: 0;
				icon->paint(
					*q,
					badge.x() + position.x() + skip,
					badge.y() + position.y(),
					width());
				if (!channel.more) {
					textLeft += position.x() + icon->width();
				}
			}
			q->setFont(font);
			q->setPen(st::premiumButtonFg);
			q->drawText(textLeft, textTop, channel.counter);
		}
		q->setPen(channel.more ? st::windowSubTextFg : stm->historyTextFg);
		channel.name.drawLeftElided(
			*q,
			geometry.x() + st::normalFont->spacew,
			geometry.y() + st::chatSimilarNameTop,
			(geometry.width() - 2 * st::normalFont->spacew),
			width(),
			2,
			style::al_top);
		if (cachedp) {
			q->setCompositionMode(QPainter::CompositionMode_DestinationIn);
			const auto corners = _roundedCorners.data();
			const auto side = st::bubbleRadiusLarge;
			q->drawImage(0, 0, corners[Images::kTopLeft]);
			q->drawImage(width() - side, 0, corners[Images::kTopRight]);
			q->drawImage(0, height() - side, corners[Images::kBottomLeft]);
			q->drawImage(
				QPoint(width() - side, height() - side),
				corners[Images::kBottomRight]);
			cachedp.reset();
			p.drawImage(geometry.topLeft(), _roundedCache);
		}
	};
	for (const auto &channel : _channels) {
		if (channel.geometry.x() >= _scrollLeft + width()) {
			break;
		}
		drawOne(channel);
	}
	p.setPen(stm->historyTextFg);
	p.setFont(st::chatSimilarTitle);
	p.drawTextLeft(
		st::chatSimilarTitlePosition.x(),
		st::chatSimilarTitlePosition.y(),
		width(),
		_title);
	if (!_hasViewAll) {
		return;
	}
	p.setFont(ClickHandler::showAsActive(_viewAllLink)
		? st::normalFont->underline()
		: st::normalFont);
	p.setPen(stm->textPalette.linkFg);
	const auto add = st::normalFont->ascent - st::chatSimilarTitle->ascent;
	p.drawTextRight(
		st::chatSimilarTitlePosition.x(),
		st::chatSimilarTitlePosition.y() + add,
		width(),
		_viewAll);
	p.setClipping(false);
}

void SimilarChannels::validateLastPremiumLock() const {
	if (_channels.empty()) {
		return;
	}
	if (!_moreThumbnailsValid) {
		_moreThumbnailsValid = 1;
		fillMoreThumbnails();
	}
	const auto &last = _channels.back();
	if (!last.more) {
		return;
	}
	const auto premium = history()->session().premium();
	const auto locked = !premium && history()->session().premiumPossible();
	if (last.moreLocked == locked) {
		return;
	}
	last.moreLocked = locked ? 1 : 0;
	last.counterBgValid = 0;
}

void SimilarChannels::fillMoreThumbnails() const {
	const auto channel = parent()->history()->peer->asChannel();
	Assert(channel != nullptr);

	_moreThumbnails = {};
	const auto api = &channel->session().api();
	const auto &similar = api->chatParticipants().similar(channel);
	for (auto i = 0, count = int(_moreThumbnails.size()); i != count; ++i) {
		if (similar.list.size() <= _channels.size() + i) {
			break;
		}
		_moreThumbnails[i] = Ui::MakeUserpicThumbnail(
			similar.list[_channels.size() + i]);
	}
}

void SimilarChannels::validateCounterBg(const Channel &channel) const {
	if (channel.counterBgValid) {
		return;
	}
	channel.counterBgValid = 1;

	const auto photo = st::chatSimilarChannelPhoto;
	const auto inner = QRect(0, 0, photo, photo);
	const auto outer = inner.marginsAdded(st::chatSimilarChannelPadding);
	const auto length = st::chatSimilarBadgeFont->width(channel.counter);
	const auto contents = length
		+ (!channel.more
			? st::chatSimilarBadgeIcon.width()
			: channel.moreLocked
			? st::chatSimilarLockedIcon.width()
			: 0);
	const auto delta = (outer.width() - contents) / 2;
	const auto badge = QRect(
		delta,
		st::chatSimilarBadgeTop,
		outer.width() - 2 * delta,
		st::chatSimilarBadgeFont->height);
	channel.counterRect = badge.marginsAdded(
		st::chatSimilarBadgePadding);

	constexpr auto kMinSaturation = 0;
	constexpr auto kMaxSaturation = 96;
	constexpr auto kMinLightness = 160;
	constexpr auto kMaxLightness = 208;

	const auto width = channel.counterRect.width();
	const auto height = channel.counterRect.height();
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		channel.counterRect.size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	auto color = channel.more
		? QColor(kMinLightness, kMinLightness, kMinLightness)
		: Ui::CountAverageColor(
			channel.thumbnail->image(photo).copy(
				QRect(photo / 3, photo / 3, photo / 3, photo / 3)));

	const auto hsl = color.toHsl();
	if (!base::in_range(hsl.saturation(), kMinSaturation, kMaxSaturation)
		|| !base::in_range(hsl.lightness(), kMinLightness, kMaxLightness)) {
		color = QColor::fromHsl(
			hsl.hue(),
			std::clamp(hsl.saturation(), kMinSaturation, kMaxSaturation),
			std::clamp(hsl.lightness(), kMinLightness, kMaxLightness)
		).toRgb();
	}

	result.fill(color);
	result.setDevicePixelRatio(ratio);
	const auto radius = height / 2;
	auto corners = Images::CornersMask(radius);
	auto p = QPainter(&result);
	p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
	p.drawImage(0, 0, corners[Images::kTopLeft]);
	p.drawImage(width - radius, 0, corners[Images::kTopRight]);
	p.drawImage(0, height - radius, corners[Images::kBottomLeft]);
	p.drawImage(
		width - radius,
		height - radius,
		corners[Images::kBottomRight]);
	p.end();
	channel.counterBg = std::move(result);
}

ClickHandlerPtr SimilarChannels::ensureToggleLink() const {
	if (_toggleLink) {
		return _toggleLink;
	}
	_toggleLink = std::make_shared<LambdaClickHandler>(crl::guard(this, [=](
			ClickContext context) {
		const auto channel = history()->peer->asChannel();
		Assert(channel != nullptr);
		using Flag = ChannelDataFlag;
		const auto flags = channel->flags();
		channel->setFlags((flags & Flag::SimilarExpanded)
			? (flags & ~Flag::SimilarExpanded)
			: (flags | Flag::SimilarExpanded));
	}));
	return _toggleLink;
}

void SimilarChannels::ensureCacheReady(QSize size) const {
	const auto ratio = style::DevicePixelRatio();
	if (_roundedCache.size() != size * ratio) {
		_roundedCache = QImage(
			size * ratio,
			QImage::Format_ARGB32_Premultiplied);
		_roundedCache.setDevicePixelRatio(ratio);
	}
	const auto radius = st::bubbleRadiusLarge;
	if (_roundedCorners.front().size() != QSize(radius, radius) * ratio) {
		_roundedCorners = Images::CornersMask(radius);
	}
}

TextState SimilarChannels::textState(
		QPoint point,
		StateRequest request) const {
	auto result = TextState();
	if (point.y() < 0 && !_empty) {
		result.link = ensureToggleLink();
		return result;
	}
	result.horizontalScroll = (_scrollMax > 0);
	const auto skip = st::chatSimilarTitlePosition;
	const auto viewWidth = _hasViewAll ? (_viewAllWidth + 2 * skip.x()) : 0;
	const auto viewHeight = st::normalFont->height + 2 * skip.y();
	const auto viewLeft = width() - viewWidth;
	if (QRect(viewLeft, 0, viewWidth, viewHeight).contains(point)) {
		if (!_viewAllLink) {
			const auto channel = parent()->history()->peer->asChannel();
			Assert(channel != nullptr);
			_viewAllLink = MakeViewAllLink(channel, false);
		}
		result.link = _viewAllLink;
		return result;
	}
	for (const auto &channel : _channels) {
		if (channel.geometry.translated(-_scrollLeft, 0).contains(point)) {
			result.link = channel.link;
			_lastPoint = point
				+ QPoint(_scrollLeft, 0)
				- channel.geometry.topLeft();
			break;
		}
	}
	return result;
}

QSize SimilarChannels::countOptimalSize() {
	const auto channel = parent()->history()->peer->asChannel();
	Assert(channel != nullptr);

	_channels.clear();
	_moreThumbnails = {};
	const auto api = &channel->session().api();
	api->chatParticipants().loadSimilarChannels(channel);
	const auto premium = channel->session().premium();
	const auto &similar = api->chatParticipants().similar(channel);
	_empty = similar.list.empty() ? 1 : 0;
	_moreThumbnailsValid = 0;
	using Flag = ChannelDataFlag;
	_toggled = (channel->flags() & Flag::SimilarExpanded) ? 1 : 0;
	if (_empty || !_toggled) {
		return {};
	}

	_channels.reserve(similar.list.size());
	auto x = st::chatSimilarPadding.left();
	auto y = st::chatSimilarPadding.top();
	const auto skip = st::chatSimilarSkip;
	const auto photo = st::chatSimilarChannelPhoto;
	const auto inner = QRect(0, 0, photo, photo);
	const auto outer = inner.marginsAdded(st::chatSimilarChannelPadding);
	const auto limit = Data::PremiumLimits(
		&channel->session()).similarChannelsDefault();
	const auto take = (similar.more > 0 || similar.list.size() > 2 * limit)
		? limit
		: int(similar.list.size());
	const auto more = similar.more + int(similar.list.size() - take);
	auto &&channels = ranges::views::all(similar.list)
		| ranges::views::take(limit);
	for (const auto &channel : channels) {
		const auto moreCounter = (_channels.size() + 1 == take) ? more : 0;
		_channels.push_back({
			.geometry = QRect(QPoint(x, y), outer.size()),
			.name = Ui::Text::String(
				st::chatSimilarName,
				(moreCounter
					? tr::lng_similar_channels_more(tr::now)
					: channel->name()),
				kDefaultTextOptions,
				st::chatSimilarChannelPhoto),
			.thumbnail = Ui::MakeUserpicThumbnail(channel),
			.more = uint32(moreCounter),
			.moreLocked = uint32((moreCounter && !premium) ? 1 : 0),
		});
		auto &last = _channels.back();
		last.link = moreCounter
			? MakeViewAllLink(parent()->history()->peer->asChannel(), true)
			: channel->openLink();

		const auto counter = moreCounter
			? moreCounter :
			channel->membersCount();
		if (moreCounter || counter > 1) {
			last.counter = (moreCounter ? u"+"_q : QString())
				+ Lang::FormatCountToShort(counter).string;
		}
		x += outer.width() + skip;
	}
	_title = tr::lng_similar_channels_title(tr::now);
	_titleWidth = st::chatSimilarTitle->width(_title);
	_viewAll = tr::lng_similar_channels_view_all(tr::now);
	_viewAllWidth = std::max(st::normalFont->width(_viewAll), 0);
	const auto count = int(_channels.size());
	const auto desired = (count ? (x - skip) : x)
		- st::chatSimilarPadding.left();
	const auto full = QRect(0, 0, desired, outer.height());
	const auto bubble = full.marginsAdded(st::chatSimilarPadding);
	_fullWidth = bubble.width();
	const auto titleSkip = st::chatSimilarTitlePosition.x();
	const auto min = int(_titleWidth) + 2 * titleSkip;
	const auto limited = std::max(
		std::min(int(_fullWidth), st::chatSimilarWidthMax),
		min);
	if (limited > _fullWidth) {
		const auto shift = (limited - _fullWidth) / 2;
		for (auto &channel : _channels) {
			channel.geometry.translate(shift, 0);
		}
	}
	return { limited, bubble.height() };
}

QSize SimilarChannels::countCurrentSize(int newWidth) {
	if (!_toggled) {
		return {};
	}
	_scrollMax = std::max(int(_fullWidth) - newWidth, 0);
	_scrollLeft = std::clamp(_scrollLeft, uint32(), _scrollMax);
	_hasViewAll = (_scrollMax != 0) ? 1 : 0;
	return { newWidth, minHeight() };
}

bool SimilarChannels::hasHeavyPart() const {
	return _hasHeavyPart != 0;
}

void SimilarChannels::unloadHeavyPart() {
	_hasHeavyPart = 0;
	for (const auto &channel : _channels) {
		channel.subscribed = 0;
		channel.thumbnail->subscribeToUpdates(nullptr);
	}
	for (const auto &thumbnail : _moreThumbnails) {
		if (thumbnail) {
			thumbnail->subscribeToUpdates(nullptr);
		}
	}
}

bool SimilarChannels::consumeHorizontalScroll(QPoint position, int delta) {
	if (_scrollMax == 0) {
		return false;
	}
	const auto left = _scrollLeft;
	_scrollLeft = std::clamp(
		int(_scrollLeft) - delta,
		0,
		int(_scrollMax));
	if (_scrollLeft == left) {
		return false;
	}
	repaint();
	return true;
}

} // namespace HistoryView
