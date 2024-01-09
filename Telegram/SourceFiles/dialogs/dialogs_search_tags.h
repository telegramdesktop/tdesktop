/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

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
	[[nodiscard]] auto selectedValue() const
		-> rpl::producer<std::vector<Data::ReactionId>>;

	void paint(
		QPainter &p,
		QPoint position,
		crl::time now,
		bool paused) const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	struct Tag;

	void fill(const std::vector<Data::Reaction> &list);
	void paintCustomFrame(
		QPainter &p,
		not_null<Ui::Text::CustomEmoji*> emoji,
		QPoint innerTopLeft,
		crl::time now,
		bool paused,
		const QColor &textColor) const;
	void layout();
	[[nodiscard]] std::vector<Data::ReactionId> collectSelected() const;
	[[nodiscard]] const QImage &validateBg(bool selected) const;

	const not_null<Data::Session*> _owner;
	std::vector<Data::ReactionId> _added;
	std::vector<Tag> _tags;
	rpl::event_stream<> _selectedChanges;
	rpl::event_stream<> _repaintRequests;
	mutable QImage _normalBg;
	mutable QImage _selectedBg;
	mutable QImage _customCache;
	mutable int _customSkip = 0;
	rpl::variable<int> _height;
	int _width = 0;

	rpl::lifetime _lifetime;

};

} // namespace Dialogs
