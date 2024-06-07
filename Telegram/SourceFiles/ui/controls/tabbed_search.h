/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/qt/qt_compare.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
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

enum class EmojiGroupType {
	Normal,
	Greeting,
	Premium,
};

struct EmojiGroup {
	QString iconId;
	std::vector<QString> emoticons;
	EmojiGroupType type = EmojiGroupType::Normal;

	friend inline auto operator<=>(
		const EmojiGroup &a,
		const EmojiGroup &b) = default;
};

[[nodiscard]] const QString &PremiumGroupFakeEmoticon();

struct SearchDescriptor {
	const style::TabbedSearch &st;
	rpl::producer<std::vector<EmojiGroup>> groups;
	Text::CustomEmojiFactory customEmojiFactory;
};

class SearchWithGroups final : public RpWidget {
public:
	SearchWithGroups(QWidget *parent, SearchDescriptor descriptor);

	[[nodiscard]] rpl::producer<> escapes() const;
	[[nodiscard]] rpl::producer<std::vector<QString>> queryValue() const;
	[[nodiscard]] auto debouncedQueryValue() const
		-> rpl::producer<std::vector<QString>>;

	void cancel();
	void setLoading(bool loading);
	void stealFocus();
	void returnFocus();

	[[nodiscard]] static int IconSizeOverride();

private:
	int resizeGetHeight(int newWidth) override;
	void wheelEvent(QWheelEvent *e) override;

	[[nodiscard]] int clampGroupsLeft(int width, int desiredLeft) const;
	void moveGroupsBy(int width, int delta);
	void moveGroupsTo(int width, int to);
	void scrollGroupsToIcon(int iconLeft, int iconRight);
	void scrollGroupsToStart();
	void scrollGroupsTo(int left);

	[[nodiscard]] anim::type animated() const;
	void initField();
	void initGroups();
	void initEdges();
	void initButtons();

	void ensureRounding(int size, float64 rounding);

	const style::TabbedSearch &_st;
	not_null<FadeWrap<IconButton>*> _search;
	not_null<FadeWrap<IconButton>*> _back;
	not_null<CrossButton*> _cancel;
	not_null<InputField*> _field;
	QPointer<QWidget> _focusTakenFrom;
	not_null<FadeWrap<RpWidget>*> _groups;
	not_null<RpWidget*> _fade;
	rpl::variable<float64> _fadeOpacity = 0.;
	int _fadeLeftStart = 0;

	rpl::variable<int> _fieldPlaceholderWidth;
	rpl::variable<bool> _fieldEmpty = true;
	Ui::Animations::Simple _groupsLeftAnimation;
	int _groupsLeftTo = 0;

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
	[[nodiscard]] QImage grab();

	[[nodiscard]] rpl::producer<> escapes() const;
	[[nodiscard]] rpl::producer<std::vector<QString>> queryValue() const;
	[[nodiscard]] auto debouncedQueryValue() const
		->rpl::producer<std::vector<QString>>;

	void cancel();
	void setLoading(bool loading);
	void stealFocus();
	void returnFocus();

private:
	const style::EmojiPan &_st;
	SearchWithGroups _search;

};

} // namespace Ui
