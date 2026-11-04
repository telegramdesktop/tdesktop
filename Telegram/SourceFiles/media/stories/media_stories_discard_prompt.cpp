/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_discard_prompt.h"

#include "lang/lang_keys.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/shadow.h"
#include "styles/style_layers.h"
#include "styles/style_media_stories.h"
#include "styles/style_widgets.h"

namespace Media::Stories {
namespace {

class DiscardPrompt final : public Ui::RpWidget {
public:
	DiscardPrompt(
		not_null<Ui::RpWidget*> parent,
		DiscardStoryArgs args);

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void closeWith(Fn<void()> after);

	DiscardStoryArgs _args;
	not_null<Ui::RpWidget*> _panel;
	bool _handled = false;

};

DiscardPrompt::DiscardPrompt(
	not_null<Ui::RpWidget*> parent,
	DiscardStoryArgs args)
: Ui::RpWidget(parent)
, _args(std::move(args))
, _panel(Ui::CreateChild<Ui::RpWidget>(this)) {
	setFocusPolicy(Qt::StrongFocus);

	parent->sizeValue(
	) | rpl::on_next([=](QSize size) {
		setGeometry(QRect(QPoint(), size));
	}, lifetime());

	const auto inner = Ui::CreateChild<Ui::VerticalLayout>(_panel);
	inner->add(
		object_ptr<Ui::FlatLabel>(
			inner,
			tr::lng_story_discard_title(),
			st::storiesDiscardPromptTitle),
		QMargins(
			st::boxTitlePosition.x(),
			st::boxTitlePosition.y(),
			st::boxTitlePosition.x(),
			0));
	inner->add(
		object_ptr<Ui::FlatLabel>(
			inner,
			tr::lng_story_discard_text(),
			st::storiesDiscardPromptText),
		st::boxPadding);
	Ui::AddSkip(inner);
	if (_args.allowDraft) {
		const auto save = inner->add(
			object_ptr<Ui::RoundButton>(
				inner,
				(_args.draft
					? tr::lng_story_keep_draft()
					: tr::lng_story_save_draft()),
				st::storiesDiscardPromptButton),
			st::boxRowPadding,
			style::al_justify);
		save->setFullRadius(true);
		save->setClickedCallback([=] {
			closeWith(_args.saveDraft);
		});
		Ui::AddSkip(inner);
	}
	const auto cancel = inner->add(
		object_ptr<Ui::RoundButton>(
			inner,
			tr::lng_cancel(),
			st::storiesDiscardPromptButton),
		st::boxRowPadding,
		style::al_justify);
	cancel->setFullRadius(true);
	cancel->setClickedCallback([=] {
		closeWith(nullptr);
	});
	Ui::AddSkip(inner);
	const auto discard = inner->add(
		object_ptr<Ui::RoundButton>(
			inner,
			(_args.draft
				? tr::lng_story_delete_draft()
				: tr::lng_story_discard_button()),
			st::storiesDiscardPromptAttention),
		st::boxRowPadding,
		style::al_justify);
	discard->setFullRadius(true);
	discard->setClickedCallback([=] {
		closeWith(_args.discard);
	});
	Ui::AddSkip(inner, st::storiesDiscardPromptBottomSkip);

	_panel->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_panel);
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::mediaviewMenuBg);
		p.drawRoundedRect(_panel->rect(), st::boxRadius, st::boxRadius);
	}, _panel->lifetime());

	sizeValue(
	) | rpl::on_next([=](QSize size) {
		inner->resizeToWidth(std::min(
			st::boxWidth,
			size.width() - 2 * st::boxPadding.left()));
	}, lifetime());

	inner->sizeValue(
	) | rpl::on_next([=](QSize size) {
		_panel->setGeometry(
			(width() - size.width()) / 2,
			(height() - size.height()) / 2,
			size.width(),
			size.height());
	}, inner->lifetime());
}

void DiscardPrompt::closeWith(Fn<void()> after) {
	if (_handled) {
		return;
	}
	_handled = true;
	deleteLater();
	if (!after) {
		return;
	}
	crl::on_main([after = std::move(after)] {
		after();
	});
}

void DiscardPrompt::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		closeWith(nullptr);
	}
	e->accept();
}

void DiscardPrompt::mousePressEvent(QMouseEvent *e) {
	if (!_panel->geometry().contains(e->pos())) {
		closeWith(nullptr);
	}
}

void DiscardPrompt::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.fillRect(rect(), st::layerBg);
	Ui::Shadow::paint(p, _panel->geometry(), width(), st::boxRoundShadow);
}

} // namespace

void ShowDiscardStoryPrompt(
		not_null<Ui::RpWidget*> parent,
		DiscardStoryArgs args) {
	const auto prompt = Ui::CreateChild<DiscardPrompt>(
		parent,
		std::move(args));
	prompt->show();
	prompt->raise();
	prompt->setFocus();
}

} // namespace Media::Stories
