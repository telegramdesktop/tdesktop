/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/bot_intro_preview.h"

#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/history_view_about_view.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "main/main_session.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/text/text_options.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/wrap/slide_wrap.h"
#include "window/themes/window_theme.h"
#include "window/section_widget.h"

#include "styles/style_chat.h"
#include "styles/style_edit_peer_members.h"

namespace {

using namespace HistoryView;

class PreviewDelegate final : public DefaultElementDelegate {
public:
	PreviewDelegate(
		not_null<QWidget*> parent,
		not_null<Ui::ChatStyle*> st,
		Fn<void()> update);

	bool elementAnimationsPaused() override;
	not_null<Ui::PathShiftGradient*> elementPathShiftGradient() override;
	Context elementContext() override;

private:
	const not_null<QWidget*> _parent;
	const std::unique_ptr<Ui::PathShiftGradient> _pathGradient;

};

class Preview final : public Ui::RpWidget {
public:
	Preview(
		not_null<QWidget*> parent,
		not_null<UserData*> bot,
		rpl::producer<QString> text);
	~Preview();

	[[nodiscard]] rpl::producer<bool> shownValue() const;

private:
	void paintEvent(QPaintEvent *e) override;

	void resizeTo(int width);

	const not_null<UserData*> _bot;
	const not_null<History*> _history;
	const std::unique_ptr<Ui::ChatTheme> _theme;
	const std::unique_ptr<Ui::ChatStyle> _style;
	const std::unique_ptr<PreviewDelegate> _delegate;

	std::unique_ptr<AboutView> _view;
	rpl::variable<bool> _shown = false;
	int _shift = 0;

};

PreviewDelegate::PreviewDelegate(
	not_null<QWidget*> parent,
	not_null<Ui::ChatStyle*> st,
	Fn<void()> update)
: _parent(parent)
, _pathGradient(MakePathShiftGradient(st, update)) {
}

bool PreviewDelegate::elementAnimationsPaused() {
	return _parent->window()->isActiveWindow();
}

auto PreviewDelegate::elementPathShiftGradient()
-> not_null<Ui::PathShiftGradient*> {
	return _pathGradient.get();
}

Context PreviewDelegate::elementContext() {
	return Context::History;
}

Preview::Preview(
	not_null<QWidget*> parent,
	not_null<UserData*> bot,
	rpl::producer<QString> text)
: RpWidget(parent)
, _bot(bot)
, _history(bot->owner().history(bot))
, _theme(Window::Theme::DefaultChatThemeOn(lifetime()))
, _style(std::make_unique<Ui::ChatStyle>(
	bot->session().colorIndicesValue()))
, _delegate(std::make_unique<PreviewDelegate>(
	parent,
	_style.get(),
	[=] { update(); }))
, _view(std::make_unique<AboutView>(_history.get(), _delegate.get())) {
	_style->apply(_theme.get());

	bot->owner().viewRepaintRequest(
	) | rpl::on_next([=](Data::RequestViewRepaint data) {
		if (data.view == _view->view()) {
			update();
		}
	}, lifetime());

	bot->session().downloaderTaskFinished() | rpl::on_next([=] {
		update();
	}, lifetime());

	std::move(text) | rpl::on_next([=](const QString &text) {
		const auto info = _bot->botInfo.get();
		const auto photo = info ? info->photo : nullptr;
		const auto document = info ? info->document : nullptr;
		const auto shown = !text.isEmpty() || photo || document;
		if (shown) {
			_view->makeBotPreview(
				TextUtilities::ParseEntities(
					text,
					Ui::ItemTextBotNoMonoOptions().flags),
				document,
				photo);
			resizeTo(width());
			update();
		}
		_shown = shown;
	}, lifetime());

	widthValue() | rpl::on_next([=](int width) {
		resizeTo(width);
	}, lifetime());
}

Preview::~Preview() {
	_view = nullptr;
}

rpl::producer<bool> Preview::shownValue() const {
	return _shown.value();
}

void Preview::resizeTo(int width) {
	const auto view = _view->view();
	if (!view || (width < st::msgMinWidth)) {
		resize(width, 0);
		return;
	}
	const auto height = view->resizeGetHeight(width);
	const auto top = view->marginTop();
	const auto skip = st::botIntroPreviewSkip;
	_shift = skip - top;
	resize(width, skip + (height - top - view->marginBottom()) + skip);
}

void Preview::paintEvent(QPaintEvent *e) {
	const auto view = _view->view();
	if (!view) {
		return;
	}
	auto p = Painter(this);

	const auto clip = e->rect();
	p.setClipRect(clip);
	Window::SectionWidget::PaintBackground(
		p,
		_theme.get(),
		QSize(width(), window()->height()),
		clip);

	auto context = _theme->preparePaintContext(
		_style.get(),
		rect(),
		rect(),
		clip,
		!window()->isActiveWindow());
	p.translate(0, _shift);
	view->draw(p, context);
}

} // namespace

object_ptr<Ui::RpWidget> CreateBotIntroPreview(
		not_null<QWidget*> parent,
		not_null<UserData*> bot,
		rpl::producer<QString> text) {
	auto result = object_ptr<Ui::SlideWrap<Preview>>(
		parent,
		object_ptr<Preview>(parent, bot, std::move(text)),
		st::botIntroPreviewPadding);
	result->toggleOn(result->entity()->shownValue());
	result->finishAnimating();
	return result;
}
