/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui {
class FlatLabel;
class RoundButton;
class VerticalLayout;
} // namespace Ui

namespace Dialogs {

struct PostsSearchIntroState {
	QString query;
	int freeSearchesPerDay = 0;
	int freeSearchesLeft = 0;
	TimeId nextFreeSearchTime = 0;
	uint32 starsPerPaidSearch : 31 = 0;
	uint32 needsPremium : 1 = 0;

	friend inline bool operator==(
		PostsSearchIntroState,
		PostsSearchIntroState) = default;
};

class PostsSearchIntro final : public Ui::RpWidget {
public:
	PostsSearchIntro(
		not_null<Ui::RpWidget*> parent,
		PostsSearchIntroState state);
	~PostsSearchIntro();

	void update(PostsSearchIntroState state);

	[[nodiscard]] rpl::producer<int> searchWithStars() const;

private:
	void resizeEvent(QResizeEvent *e) override;

	void setup();

	rpl::variable<PostsSearchIntroState> _state;

	std::unique_ptr<Ui::VerticalLayout> _content;
	Ui::FlatLabel *_title = nullptr;
	Ui::FlatLabel *_subtitle = nullptr;
	Ui::RoundButton *_button = nullptr;
	Ui::FlatLabel *_footer = nullptr;

};

} // namespace Dialogs
