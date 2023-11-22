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
#include "data/data_session.h"
#include "dialogs/ui/dialogs_stories_content.h"
#include "dialogs/ui/dialogs_stories_list.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

class SimilarChannelsController final : public PeerListController {
public:
	SimilarChannelsController(
		not_null<Window::SessionController*> controller,
		std::vector<not_null<ChannelData*>> channels);

	void prepare() override;
	void loadMoreRows() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	Main::Session &session() const override;

private:
	const not_null<Window::SessionController*> _controller;
	const std::vector<not_null<ChannelData*>> _channels;

};

SimilarChannelsController::SimilarChannelsController(
	not_null<Window::SessionController*> controller,
	std::vector<not_null<ChannelData*>> channels)
: _controller(controller)
, _channels(std::move(channels)) {
}

void SimilarChannelsController::prepare() {
	for (const auto &channel : _channels) {
		auto row = std::make_unique<PeerListRow>(channel);
		if (const auto count = channel->membersCount(); count > 1) {
			row->setCustomStatus(tr::lng_chat_status_subscribers(
				tr::now,
				lt_count,
				count));
		}
		delegate()->peerListAppendRow(std::move(row));
	}
	delegate()->peerListRefreshRows();
}

void SimilarChannelsController::loadMoreRows() {
}

void SimilarChannelsController::rowClicked(not_null<PeerListRow*> row) {
	const auto other = ClickHandlerContext{
		.sessionWindow = _controller,
		.show = _controller->uiShow(),
	};
	row->peer()->openLink()->onClick({
		Qt::LeftButton,
		QVariant::fromValue(other)
	});
}

Main::Session &SimilarChannelsController::session() const {
	return _channels.front()->session();
}

[[nodiscard]] object_ptr<Ui::BoxContent> SimilarChannelsBox(
		not_null<Window::SessionController*> controller,
		const std::vector<not_null<ChannelData*>> &channels) {
	const auto initBox = [=](not_null<PeerListBox*> box) {
		box->setTitle(tr::lng_similar_channels_title());
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	};
	return Box<PeerListBox>(
		std::make_unique<SimilarChannelsController>(controller, channels),
		initBox);
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
	const auto photo = st::chatSimilarChannelPhoto;
	const auto padding = st::chatSimilarChannelPadding;
	p.setClipRect(geometry);
	_hasHeavyPart = 1;
	const auto drawOne = [&](const Channel &channel) {
		const auto geometry = channel.geometry.translated(-_scrollLeft, 0);
		const auto right = geometry.x() + geometry.width();
		if (right <= 0) {
			return;
		}
		if (!channel.subscribed) {
			channel.subscribed = true;
			const auto raw = channel.thumbnail.get();
			const auto view = parent();
			channel.thumbnail->subscribeToUpdates([=] {
				for (const auto &channel : _channels) {
					if (channel.thumbnail.get() == raw) {
						channel.participantsBgValid = false;
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
		if (channel.ripple) {
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
		q->drawImage(
			geometry.x() + padding.left(),
			geometry.y() + padding.top(),
			channel.thumbnail->image(st::chatSimilarChannelPhoto));
		if (!channel.participants.isEmpty()) {
			validateParticipansBg(channel);
			const auto participants = channel.participantsRect.translated(
				geometry.topLeft());
			q->drawImage(participants.topLeft(), channel.participantsBg);
			const auto badge = participants.marginsRemoved(
				st::chatSimilarBadgePadding);
			const auto &icon = st::chatSimilarBadgeIcon;
			const auto &font = st::chatSimilarBadgeFont;
			const auto position = st::chatSimilarBadgeIconPosition;
			const auto ascent = font->ascent;
			icon.paint(*q, badge.topLeft() + position, width());
			q->setFont(font);
			q->setPen(st::premiumButtonFg);
			q->drawText(
				badge.x() + position.x() + icon.width(),
				badge.y() + font->ascent,
				channel.participants);
		}
		q->setPen(stm->historyTextFg);
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

void SimilarChannels::validateParticipansBg(const Channel &channel) const {
	if (channel.participantsBgValid) {
		return;
	}
	channel.participantsBgValid = true;
	const auto photo = st::chatSimilarChannelPhoto;
	const auto width = channel.participantsRect.width();
	const auto height = channel.participantsRect.height();
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		channel.participantsRect.size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	auto color = Ui::CountAverageColor(
		channel.thumbnail->image(photo).copy(
			QRect(photo / 3, photo / 3, photo / 3, photo / 3)));

	const auto hsl = color.toHsl();
	constexpr auto kMinSaturation = 0;
	constexpr auto kMaxSaturation = 96;
	constexpr auto kMinLightness = 160;
	constexpr auto kMaxLightness = 208;
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
	channel.participantsBg = std::move(result);
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
			_viewAllLink = std::make_shared<LambdaClickHandler>([=](
					ClickContext context) {
				Assert(channel != nullptr);
				const auto api = &channel->session().api();
				const auto &list = api->chatParticipants().similar(channel);
				if (list.empty()) {
					return;
				}
				const auto my = context.other.value<ClickHandlerContext>();
				if (const auto strong = my.sessionWindow.get()) {
					strong->show(SimilarChannelsBox(strong, list));
				}
			});
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
	const auto api = &channel->session().api();
	api->chatParticipants().loadSimilarChannels(channel);
	const auto similar = api->chatParticipants().similar(channel);
	_empty = similar.empty() ? 1 : 0;
	using Flag = ChannelDataFlag;
	_toggled = (channel->flags() & Flag::SimilarExpanded) ? 1 : 0;
	if (_empty || !_toggled) {
		return {};
	}

	_channels.reserve(similar.size());
	auto x = st::chatSimilarPadding.left();
	auto y = st::chatSimilarPadding.top();
	const auto skip = st::chatSimilarSkip;
	const auto photo = st::chatSimilarChannelPhoto;
	const auto inner = QRect(0, 0, photo, photo);
	const auto outer = inner.marginsAdded(st::chatSimilarChannelPadding);
	for (const auto &channel : similar) {
		const auto participants = channel->membersCount();
		const auto count = (participants > 1)
			? Lang::FormatCountToShort(participants).string
			: QString();
		_channels.push_back({
			.geometry = QRect(QPoint(x, y), outer.size()),
			.name = Ui::Text::String(
				st::chatSimilarName,
				channel->name(),
				kDefaultTextOptions,
				st::chatSimilarChannelPhoto),
			.thumbnail = Dialogs::Stories::MakeUserpicThumbnail(channel),
			.link = channel->openLink(),
			.participants = count,
		});
		if (!count.isEmpty()) {
			const auto length = st::chatSimilarBadgeFont->width(count);
			const auto width = length + st::chatSimilarBadgeIcon.width();
			const auto delta = (outer.width() - width) / 2;
			const auto badge = QRect(
				delta,
				st::chatSimilarBadgeTop,
				outer.width() - 2 * delta,
				st::chatSimilarBadgeFont->height);
			_channels.back().participantsRect = badge.marginsAdded(
				st::chatSimilarBadgePadding);
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
	const auto min = _titleWidth + 2 * titleSkip;
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
		channel.subscribed = false;
		channel.thumbnail->subscribeToUpdates(nullptr);
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