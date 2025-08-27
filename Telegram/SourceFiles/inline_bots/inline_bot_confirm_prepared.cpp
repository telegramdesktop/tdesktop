/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "inline_bots/inline_bot_confirm_prepared.h"

#include "boxes/peers/edit_peer_invite_link.h"
#include "data/data_forum_topic.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/admin_log/history_admin_log_item.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/layers/generic_box.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "window/themes/window_theme.h"
#include "window/section_widget.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"

namespace InlineBots {
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

class PreviewWrap final : public Ui::RpWidget {
public:
	PreviewWrap(not_null<QWidget*> parent, not_null<HistoryItem*> item);
	~PreviewWrap();

private:
	void paintEvent(QPaintEvent *e) override;

	void resizeTo(int width);
	void prepare(not_null<HistoryItem*> item);

	const not_null<History*> _history;
	const std::unique_ptr<Ui::ChatTheme> _theme;
	const std::unique_ptr<Ui::ChatStyle> _style;
	const std::unique_ptr<PreviewDelegate> _delegate;
	AdminLog::OwnedItem _item;
	QPoint _position;

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

PreviewWrap::PreviewWrap(
	not_null<QWidget*> parent,
	not_null<HistoryItem*> item)
: RpWidget(parent)
, _history(item->history())
, _theme(Window::Theme::DefaultChatThemeOn(lifetime()))
, _style(std::make_unique<Ui::ChatStyle>(
	_history->session().colorIndicesValue()))
, _delegate(std::make_unique<PreviewDelegate>(
	parent,
	_style.get(),
	[=] { update(); }))
, _position(0, st::msgMargin.bottom()) {
	_style->apply(_theme.get());

	using namespace HistoryView;
	_history->owner().viewRepaintRequest(
	) | rpl::start_with_next([=](not_null<const Element*> view) {
		if (view == _item.get()) {
			update();
		}
	}, lifetime());

	_history->session().downloaderTaskFinished() | rpl::start_with_next([=] {
		update();
	}, lifetime());

	prepare(item);
}

PreviewWrap::~PreviewWrap() {
	_item = {};
}

void PreviewWrap::prepare(not_null<HistoryItem*> item) {
	_item = AdminLog::OwnedItem(_delegate.get(), item);
	if (width() >= st::msgMinWidth) {
		resizeTo(width());
	}

	widthValue(
	) | rpl::filter([=](int width) {
		return width >= st::msgMinWidth;
	}) | rpl::start_with_next([=](int width) {
		resizeTo(width);
	}, lifetime());
}

void PreviewWrap::resizeTo(int width) {
	const auto height = _position.y()
		+ _item->resizeGetHeight(width)
		+ _position.y()
		+ st::msgServiceMargin.top()
		+ st::msgServiceGiftBoxTopSkip
		- st::msgServiceMargin.bottom();
	resize(width, height);
}

void PreviewWrap::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	const auto clip = e->rect();
	if (!clip.isEmpty()) {
		p.setClipRect(clip);
		Window::SectionWidget::PaintBackground(
			p,
			_theme.get(),
			QSize(width(), window()->height()),
			clip);
	}

	auto context = _theme->preparePaintContext(
		_style.get(),
		rect(),
		e->rect(),
		!window()->isActiveWindow());
	p.translate(_position);
	_item->draw(p, context);
}

} // namespace

void PreparedPreviewBox(
		not_null<Ui::GenericBox*> box,
		not_null<HistoryItem*> item,
		rpl::producer<not_null<Data::Thread*>> recipient,
		Fn<void()> choose,
		Fn<void(not_null<Data::Thread*>)> send) {
	box->setTitle(tr::lng_bot_share_prepared_title());
	const auto container = box->verticalLayout();
	container->add(object_ptr<PreviewWrap>(container, item));
	const auto bot = item->viaBot();
	const auto name = bot ? bot->name() : u"Bot"_q;
	const auto info = container->add(
		object_ptr<Ui::SlideWrap<Ui::DividerLabel>>(
			container,
			object_ptr<Ui::DividerLabel>(
				container,
				object_ptr<Ui::FlatLabel>(
					container,
					tr::lng_bot_share_prepared_about(lt_bot, rpl::single(name)),
					st::boxDividerLabel),
				st::defaultBoxDividerLabelPadding,
				RectPart::Top | RectPart::Bottom)));
	const auto row = container->add(object_ptr<Ui::VerticalLayout>(
		container));

	const auto reset = [=] {
		info->show(anim::type::instant);
		while (row->count()) {
			delete row->widgetAt(0);
		}
		box->addButton(tr::lng_bot_share_prepared_button(), choose);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	};
	reset();

	const auto lifetime = box->lifetime().make_state<rpl::lifetime>();
	std::move(
		recipient
	) | rpl::start_with_next([=](not_null<Data::Thread*> thread) {
		info->hide(anim::type::instant);
		while (row->count()) {
			delete row->widgetAt(0);
		}
		AddSkip(row);
		AddSinglePeerRow(row, thread, nullptr, choose);
		if (const auto topic = thread->asTopic()) {
			*lifetime = topic->destroyed() | rpl::start_with_next(reset);
		} else {
			*lifetime = rpl::lifetime();
		}
		row->resizeToWidth(container->width());
		box->clearButtons();
		box->addButton(tr::lng_send_button(), [=] { send(thread); });
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}, info->lifetime());

	item->history()->owner().itemRemoved(
	) | rpl::start_with_next([=](not_null<const HistoryItem*> removed) {
		if (removed == item) {
			box->closeBox();
		}
	}, box->lifetime());
}

} // namespace InlineBots
