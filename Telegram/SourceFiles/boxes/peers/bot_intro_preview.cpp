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
#include "window/themes/window_theme.h"
#include "window/section_widget.h"

#include "styles/style_chat.h"

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

private:
	void paintEvent(QPaintEvent *e) override;

	void resizeTo(int width);

	const not_null<UserData*> _bot;
	const not_null<History*> _history;
	const std::unique_ptr<Ui::ChatTheme> _theme;
	const std::unique_ptr<Ui::ChatStyle> _style;
	const std::unique_ptr<PreviewDelegate> _delegate;

	std::unique_ptr<AboutView> _view;
	bool _empty = true;

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
		_empty = text.isEmpty() && !photo && !document;
		_view->makeBotPreview(
			TextUtilities::ParseEntities(
				text,
				Ui::ItemTextBotNoMonoOptions().flags),
			document,
			photo);
		resizeTo(width());
		update();
	}, lifetime());

	widthValue() | rpl::on_next([=](int width) {
		resizeTo(width);
	}, lifetime());
}

Preview::~Preview() {
	_view = nullptr;
}

void Preview::resizeTo(int width) {
	if (_empty || (width < st::msgMinWidth)) {
		resize(width, 0);
		return;
	}
	const auto skip = st::msgMargin.bottom();
	resize(width, skip + _view->view()->resizeGetHeight(width) + skip);
}

void Preview::paintEvent(QPaintEvent *e) {
	if (_empty) {
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
	p.translate(0, st::msgMargin.bottom());
	_view->view()->draw(p, context);
}

} // namespace

object_ptr<Ui::RpWidget> CreateBotIntroPreview(
		not_null<QWidget*> parent,
		not_null<UserData*> bot,
		rpl::producer<QString> text) {
	return object_ptr<Preview>(parent, bot, std::move(text));
}
