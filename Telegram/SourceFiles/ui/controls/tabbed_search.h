/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "ui/rp_widget.h"
#include "ui/text/text_custom_emoji.h"

namespace style {
struct EmojiPan;
struct TabbedSearch;
} // namespace style

namespace anim {
enum class type : uchar;
} // namespace anim

namespace Ui {

class InputField;
class IconButton;
class CrossButton;
class RpWidget;
template <typename Widget>
class FadeWrap;

struct EmojiGroup {
	QString iconId;
	std::vector<QString> emoticons;

	friend inline auto operator<=>(
		const EmojiGroup &a,
		const EmojiGroup &b) = default;
};

struct SearchDescriptor {
	const style::TabbedSearch &st;
	rpl::producer<std::vector<EmojiGroup>> groups;
	Text::CustomEmojiFactory customEmojiFactory;
};

class SearchWithGroups final : public RpWidget {
public:
	SearchWithGroups(QWidget *parent, SearchDescriptor descriptor);

	[[nodiscard]] rpl::producer<std::vector<QString>> queryValue() const;
	[[nodiscard]] auto debouncedQueryValue() const
		-> rpl::producer<std::vector<QString>>;

private:
	int resizeGetHeight(int newWidth) override;

	[[nodiscard]] anim::type animated() const;
	void initField();
	void initGroups();
	void initEdges();

	void ensureRounding(int size, float64 rounding);

	const style::TabbedSearch &_st;
	not_null<FadeWrap<IconButton>*> _search;
	not_null<FadeWrap<IconButton>*> _back;
	not_null<CrossButton*> _cancel;
	not_null<InputField*> _field;
	not_null<FadeWrap<RpWidget>*> _groups;
	not_null<FadeWrap<RpWidget>*> _fadeLeft;
	not_null<FadeWrap<RpWidget>*> _fadeRight;

	rpl::variable<int> _fieldPlaceholderWidth;

	QImage _rounding;

	rpl::variable<std::vector<QString>> _query;
	rpl::variable<std::vector<QString>> _debouncedQuery;
	rpl::variable<QString> _chosenGroup;
	base::Timer _debounceTimer;
	bool _inited = false;

};

class TabbedSearch final {
public:
	TabbedSearch(
		not_null<RpWidget*> parent,
		const style::EmojiPan &st,
		SearchDescriptor &&descriptor);

	[[nodiscard]] int height() const;

	[[nodiscard]] rpl::producer<std::vector<QString>> queryValue() const;
	[[nodiscard]] auto debouncedQueryValue() const
		->rpl::producer<std::vector<QString>>;

private:
	const style::EmojiPan &_st;
	SearchWithGroups _search;

};

} // namespace Ui
