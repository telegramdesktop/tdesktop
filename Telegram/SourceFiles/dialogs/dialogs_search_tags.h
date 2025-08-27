/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

class Painter;

namespace Data {
class Session;
struct Reaction;
struct ReactionId;
} // namespace Data

namespace Ui::Text {
class CustomEmoji;
} // namespace Ui::Text

namespace Dialogs {

class SearchTags final : public base::has_weak_ptr {
public:
	SearchTags(
		not_null<Data::Session*> owner,
		rpl::producer<std::vector<Data::Reaction>> tags,
		std::vector<Data::ReactionId> selected);
	~SearchTags();

	void resizeToWidth(int width);
	[[nodiscard]] int height() const;
	[[nodiscard]] rpl::producer<int> heightValue() const;
	[[nodiscard]] rpl::producer<> repaintRequests() const;

	[[nodiscard]] ClickHandlerPtr lookupHandler(QPoint point) const;
	[[nodiscard]] auto selectedChanges() const
		-> rpl::producer<std::vector<Data::ReactionId>>;

	[[nodiscard]] rpl::producer<Data::ReactionId> menuRequests() const;

	void paint(
		Painter &p,
		QPoint position,
		crl::time now,
		bool paused) const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	struct Tag;

	void fill(const std::vector<Data::Reaction> &list, bool premium);
	void paintCustomFrame(
		QPainter &p,
		not_null<Ui::Text::CustomEmoji*> emoji,
		QPoint innerTopLeft,
		crl::time now,
		bool paused,
		const QColor &textColor) const;
	void layout();
	[[nodiscard]] std::vector<Data::ReactionId> collectSelected() const;
	[[nodiscard]] QColor bgColor(bool selected, bool promo) const;
	[[nodiscard]] const QImage &validateBg(bool selected, bool promo) const;
	void paintAdditionalText(Painter &p, QPoint position) const;
	void paintBackground(QPainter &p, QRect geometry, const Tag &tag) const;
	void paintText(QPainter &p, QRect geometry, const Tag &tag) const;

	const not_null<Data::Session*> _owner;
	std::vector<Data::ReactionId> _added;
	std::vector<Tag> _tags;
	Ui::Text::String _additionalText;
	rpl::event_stream<> _selectedChanges;
	rpl::event_stream<> _repaintRequests;
	rpl::event_stream<Data::ReactionId> _menuRequests;
	mutable QImage _normalBg;
	mutable QImage _selectedBg;
	mutable QImage _promoBg;
	mutable QImage _customCache;
	mutable int _customSkip = 0;
	rpl::variable<int> _height;
	int _width = 0;
	int _additionalLeft = 0;

	rpl::lifetime _lifetime;

};

} // namespace Dialogs
