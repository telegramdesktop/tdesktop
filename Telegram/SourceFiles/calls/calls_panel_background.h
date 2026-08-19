// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "data/data_peer_colors.h"

class DocumentData;
class PeerData;

namespace Data {
class CustomEmoji;
} // namespace Data

namespace Ui {
namespace Text {
class CustomEmoji;
} // namespace Text
} // namespace Ui

namespace Calls {

class PanelBackground final {
public:
	explicit PanelBackground(
		not_null<PeerData*> peer,
		Fn<void()> updateCallback);

	void paint(
		QPainter &p,
		QSize widgetSize,
		int bodyTop,
		int photoTop,
		int photoSize,
		const QRegion &region);

	[[nodiscard]] rpl::lifetime &lifetime();

	[[nodiscard]] std::optional<QColor> textColorOverride(
		const style::color &defaultColor) const;

private:
	void updateBrush(
		QSize widgetSize,
		QPoint center,
		float64 radius,
		const std::vector<QColor> &colors);
	void renderPattern(const QRect &rect, DocumentId emojiId);
	void updateColors();
	void updateEmojiId();
	[[nodiscard]] std::optional<QColor> edgeColor() const;

	const not_null<PeerData*> _peer;
	const Fn<void()> _updateCallback;

	QBrush _brush;
	QSize _brushSize;

	std::optional<Data::ColorProfileSet> _colors;

	std::unique_ptr<Ui::Text::CustomEmoji> _emoji;
	base::flat_map<float64, QImage> _cache;
	QImage _cachedImage;
	QRect _cachedRect;
	DocumentId _cachedEmojiId = 0;
	DocumentId _currentEmojiId = 0;

	rpl::lifetime _lifetime;

};

} // namespace Calls
