/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_panel_background.h"

#include "apiwrap.h"
#include "api/api_peer_colors.h"
#include "data/data_changes.h"
#include "data/data_document.h"
#include "data/data_emoji_statuses.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "main/main_session.h"
#include "ui/color_contrast.h"
#include "ui/top_background_gradient.h"
#include "styles/style_calls.h"

namespace Calls {

PanelBackground::PanelBackground(
	not_null<PeerData*> peer,
	Fn<void()> updateCallback)
: _peer(peer)
, _updateCallback(std::move(updateCallback)) {
	updateColors();
	updateEmojiId();

	_peer->session().changes().peerFlagsValue(
		_peer,
		Data::PeerUpdate::Flag::ColorProfile
			| Data::PeerUpdate::Flag::EmojiStatus
	) | rpl::on_next([=] {
		updateColors();
		_brushSize = QSize();
		if (_updateCallback) {
			_updateCallback();
		}
	}, _lifetime);

	_peer->session().changes().peerFlagsValue(
		_peer,
		Data::PeerUpdate::Flag::BackgroundEmoji
			| Data::PeerUpdate::Flag::EmojiStatus
	) | rpl::on_next([=] {
		updateEmojiId();
		if (_updateCallback) {
			_updateCallback();
		}
	}, _lifetime);
}

void PanelBackground::paint(
		QPainter &p,
		QSize widgetSize,
		int bodyTop,
		int photoTop,
		int photoSize,
		const QRegion &region) {
	if (!_colors || _colors->bg.empty()) {
		for (const auto &rect : region) {
			p.fillRect(rect, st::callBgOpaque);
		}
		return;
	}

	const auto &colors = _colors->bg;
	if (colors.size() == 1) {
		for (const auto &rect : region) {
			p.fillRect(rect, colors.front());
		}
	} else {
		const auto center = QPoint(
			widgetSize.width() / 2,
			bodyTop + photoTop + photoSize / 2);
		const auto radius = std::max(
			std::hypot(center.x(), center.y()),
			std::hypot(widgetSize.width() - center.x(),
				widgetSize.height() - center.y()));
		if (_brushSize != widgetSize) {
			updateBrush(widgetSize, center, radius, colors);
		}
		for (const auto &rect : region) {
			p.fillRect(rect, _brush);
		}
	}

	const auto emojiId = _currentEmojiId;
	if (!emojiId) {
		_emoji = nullptr;
		_cachedImage = QImage();
		_cachedEmojiId = 0;
		return;
	}

	const auto userpicX = (widgetSize.width() - photoSize) / 2;
	const auto userpicY = bodyTop + photoTop;
	const auto padding = photoSize;
	const auto patternRect = QRect(
		userpicX - padding,
		userpicY - padding / 2,
		photoSize + padding * 2,
		photoSize + padding);

	if (!_emoji
		|| _emoji->entityData()
			!= Data::SerializeCustomEmojiId(emojiId)) {
		const auto document = _peer->owner().document(emojiId);
		_emoji = document->owner().customEmojiManager().create(
			document,
			[=] {
				_cachedImage = QImage();
				if (_updateCallback) {
					_updateCallback();
				}
			},
			Data::CustomEmojiSizeTag::Large);
		_cachedImage = QImage();
		_cachedEmojiId = 0;
	}

	if (_emoji && _emoji->ready()) {
		if (_cachedImage.isNull()
			|| _cachedRect != patternRect
			|| _cachedEmojiId != emojiId) {
			renderPattern(patternRect, emojiId);
		}
		p.drawImage(patternRect, _cachedImage);
	}
}

void PanelBackground::updateBrush(
		QSize widgetSize,
		QPoint center,
		float64 radius,
		const std::vector<QColor> &colors) {
	_brushSize = widgetSize;
	auto gradient = QRadialGradient(center, radius);
	const auto step = 1.0 / (colors.size() - 1);
	for (auto i = 0; i < colors.size(); ++i) {
		gradient.setColorAt(i * step, colors[colors.size() - 1 - i]);
	}
	_brush = QBrush(gradient);
}

void PanelBackground::renderPattern(const QRect &rect, DocumentId emojiId) {
	const auto ratio = style::DevicePixelRatio();
	_cachedImage = QImage(
		rect.size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	_cachedImage.setDevicePixelRatio(ratio);
	_cachedImage.fill(Qt::transparent);

	auto painter = QPainter(&_cachedImage);
	const auto patternColor = QColor(0, 0, 0, int(0.6 * 255));
	const auto &points = Ui::PatternBgPoints();
	const auto localRect = QRect(QPoint(), rect.size());
	Ui::PaintBgPoints(
		painter,
		points,
		_cache,
		_emoji.get(),
		patternColor,
		localRect,
		1.);

	_cachedRect = rect;
	_cachedEmojiId = emojiId;
}

void PanelBackground::updateColors() {
	const auto collectible = _peer->emojiStatusId().collectible;
	if (collectible && collectible->centerColor.isValid()) {
		_colors = Data::ColorProfileSet{
			.bg = { collectible->edgeColor, collectible->centerColor },
		};
	} else {
		_colors = _peer->session().api().peerColors().colorProfileFor(_peer);
	}
}

void PanelBackground::updateEmojiId() {
	const auto collectible = _peer->emojiStatusId().collectible;
	_currentEmojiId = (collectible && collectible->patternDocumentId)
		? collectible->patternDocumentId
		: _peer->profileBackgroundEmojiId();
}

std::optional<QColor> PanelBackground::edgeColor() const {
	const auto collectible = _peer->emojiStatusId().collectible;
	if (collectible && collectible->edgeColor.isValid()) {
		return collectible->edgeColor;
	}
	if (_colors && !_colors->bg.empty()) {
		return _colors->bg.front();
	}
	return std::nullopt;
}

std::optional<QColor> PanelBackground::textColorOverride(
		const style::color &defaultColor) const {
	const auto collectible = _peer->emojiStatusId().collectible;
	if (collectible && collectible->textColor.isValid()) {
		return collectible->textColor;
	}
	const auto edge = edgeColor();
	if (!edge) {
		return std::nullopt;
	}
	constexpr auto kMinContrast = 5.5;
	if (kMinContrast > Ui::CountContrast(defaultColor->c, *edge)) {
		return st::groupCallMembersFg->c;
	}
	return std::nullopt;
}

rpl::lifetime &PanelBackground::lifetime() {
	return _lifetime;
}

} // namespace Calls
