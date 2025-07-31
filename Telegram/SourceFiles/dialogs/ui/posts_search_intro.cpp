/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/posts_search_intro.h"

#include "base/timer_rpl.h"
#include "base/unixtime.h"
#include "lang/lang_keys.h"
#include "ui/controls/button_two_labels.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_credits.h"
#include "styles/style_dialogs.h"

namespace Dialogs {
namespace {

[[nodiscard]] rpl::producer<QString> FormatCountdownTill(TimeId when) {
	return rpl::single(rpl::empty) | rpl::then(
		base::timer_each(1000)
	) | rpl::map([=] {
		const auto now = base::unixtime::now();
		const auto delta = std::max(when - now, 0);
		const auto hours = delta / 3600;
		const auto minutes = (delta % 3600) / 60;
		const auto seconds = delta % 60;
		constexpr auto kZero = QChar('0');
		return (hours > 0)
			? u"%1:%2:%3"_q
				.arg(hours)
				.arg(minutes, 2, 10, kZero)
				.arg(seconds, 2, 10, kZero)
			: u"%1:%2"_q
				.arg(minutes)
				.arg(seconds, 2, 10, kZero);
	});
}

} // namespace

PostsSearchIntro::PostsSearchIntro(
	not_null<Ui::RpWidget*> parent,
	PostsSearchIntroState state)
: RpWidget(parent)
, _state(std::move(state))
, _content(std::make_unique<Ui::VerticalLayout>(this)) {
	setup();
}

PostsSearchIntro::~PostsSearchIntro() = default;

void PostsSearchIntro::update(PostsSearchIntroState state) {
	_state = std::move(state);
}

rpl::producer<int> PostsSearchIntro::searchWithStars() const {
	return _button->clicks() | rpl::map([=] {
		const auto &now = _state.current();
		return (now.needsPremium || now.freeSearchesLeft)
			? 0
			: int(now.starsPerPaidSearch);
	});
}

void PostsSearchIntro::setup() {
	auto title = _state.value(
	) | rpl::map([](const PostsSearchIntroState &state) {
		return (state.needsPremium || state.freeSearchesLeft > 0)
			? tr::lng_posts_title()
			: tr::lng_posts_limit_reached();
	}) | rpl::flatten_latest();

	auto subtitle = _state.value(
	) | rpl::map([](const PostsSearchIntroState &state) {
		return (state.needsPremium || state.freeSearchesLeft > 0)
			? tr::lng_posts_start()
			: tr::lng_posts_limit_about(
				lt_count,
				rpl::single(state.freeSearchesPerDay * 1.));
	}) | rpl::flatten_latest();

	auto footer = _state.value(
	) | rpl::map([](const PostsSearchIntroState &state) {
		if (state.needsPremium) {
			return tr::lng_posts_need_subscribe();
		} else if (state.freeSearchesLeft > 0) {
			return tr::lng_posts_remaining(
				lt_count,
				rpl::single(state.freeSearchesLeft * 1.));
		} else {
			return rpl::single(QString());
		}
	}) | rpl::flatten_latest();

	_title = _content->add(
		object_ptr<Ui::FlatLabel>(
			_content.get(),
			std::move(title),
			st::postsSearchIntroTitle),
		st::postsSearchIntroTitleMargin);
	_title->setTryMakeSimilarLines(true);
	_subtitle = _content->add(
		object_ptr<Ui::FlatLabel>(
			_content.get(),
			std::move(subtitle),
			st::postsSearchIntroSubtitle),
		st::postsSearchIntroSubtitleMargin);
	_subtitle->setTryMakeSimilarLines(true);
	_button = _content->add(
		object_ptr<Ui::CenterWrap<Ui::RoundButton>>(
			_content.get(),
			object_ptr<Ui::RoundButton>(
				_content.get(),
				rpl::single(QString()),
				st::postsSearchIntroButton))
	)->entity();
	_button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	_footer = _content->add(
		object_ptr<Ui::FlatLabel>(
			_content.get(),
			std::move(footer),
			st::postsSearchIntroFooter),
		st::postsSearchIntroFooterMargin);
	_footer->setTryMakeSimilarLines(true);

	_state.value(
	) | rpl::start_with_next([=](const PostsSearchIntroState &state) {
		auto copy = _button->children();
		for (const auto child : copy) {
			delete child;
		}
		if (state.needsPremium) {
			_button->setText(tr::lng_posts_subscribe());
		} else if (state.freeSearchesLeft > 0) {
			_button->setText(tr::lng_posts_search_button(
				lt_query,
				rpl::single(state.query)));
		} else {
			_button->setText(rpl::single(QString()));

			Ui::SetButtonTwoLabels(
				_button,
				tr::lng_posts_limit_search_paid(
					lt_cost,
					rpl::single(Ui::Text::IconEmoji(
						&st::starIconEmoji
					).append(
						Lang::FormatCountDecimal(state.starsPerPaidSearch))),
					Ui::Text::WithEntities),
				tr::lng_posts_limit_unlocks(
					lt_duration,
					FormatCountdownTill(
						state.nextFreeSearchTime
					) | Ui::Text::ToWithEntities(),
					Ui::Text::WithEntities),
				st::resaleButtonTitle,
				st::resaleButtonSubtitle);
		}
		_content->resizeToWidth(width());
	}, _button->lifetime());
}

void PostsSearchIntro::resizeEvent(QResizeEvent *e) {
	_content->resizeToWidth(width());
	const auto top = std::max(0, (height() - _content->height()) / 3);
	_content->move(0, top);
}

} // namespace Dialogs
