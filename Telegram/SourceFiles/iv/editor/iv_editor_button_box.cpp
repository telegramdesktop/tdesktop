/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_button_box.h"

#include "base/flat_set.h"
#include "base/weak_qptr.h"
#include "boxes/peers/edit_peer_invite_link.h"
#include "boxes/peer_list_box.h"
#include "boxes/peer_list_controllers.h"
#include "chat_helpers/message_field.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "iv/editor/iv_editor_text_entities.h"
#include "iv/markdown/iv_markdown_button_row.h"
#include "iv/markdown/iv_markdown_prepare_serialize.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"

#include "styles/style_boxes.h"
#include "styles/style_iv.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtGui/QMouseEvent>
#include <QtGui/QPaintEvent>

#include <array>
#include <utility>

namespace Iv::Editor {
namespace {

using ButtonType = HistoryMessageMarkupButton::Type;
using ButtonColor = HistoryMessageMarkupButton::Color;

constexpr auto kColors = std::array{
	ButtonColor::Normal,
	ButtonColor::Primary,
	ButtonColor::Success,
	ButtonColor::Danger,
};

[[nodiscard]] int RichButtonColorIndex(ButtonColor color) {
	const auto i = ranges::find(kColors, color);
	return (i != end(kColors)) ? int(i - begin(kColors)) : 0;
}

// A button label stores only emoji and dates, so the field must offer no
// markdown tag at all. An empty subset means "every tag is enabled", and
// Ui::MarkdownDisabled would also drop the Date item, which lives in the
// still enabled branch of InputField::addMarkdownActions. A non-empty subset
// that matches no real tag suppresses every tag item and tag shortcut instead.
[[nodiscard]] base::flat_set<QString> NoMarkdownTags() {
	return { QString() };
}

[[nodiscard]] int RichButtonOutlineExtend() {
	return st::ivButtonPreviewOutlineSkip
		+ st::ivButtonPreviewOutlineStroke;
}

class RichButtonPreviewRow final : public Ui::RpWidget {
public:
	explicit RichButtonPreviewRow(QWidget *parent);

	void setSelected(int index);
	void setSelectedChanged(Fn<void(int)> callback);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	void paintSelectionOutline(QPainter &p);

	std::vector<Markdown::LaidOutButton> _buttons;
	Fn<void(int)> _selectedChanged;
	int _selected = 0;
	int _pressed = -1;

};

class RichButtonUserController final : public ContactsBoxController {
public:
	RichButtonUserController(
		not_null<Main::Session*> session,
		Fn<void(not_null<UserData*>)> chosen);

protected:
	std::unique_ptr<PeerListRow> createRow(
		not_null<UserData*> user) override;
	void rowClicked(not_null<PeerListRow*> row) override;

private:
	const Fn<void(not_null<UserData*>)> _chosen;

};

RichButtonPreviewRow::RichButtonPreviewRow(QWidget *parent)
: RpWidget(parent) {
	const auto labels = std::array{
		tr::lng_formatting_button_style_default(tr::now),
		tr::lng_formatting_button_style_primary(tr::now),
		tr::lng_formatting_button_style_success(tr::now),
		tr::lng_formatting_button_style_danger(tr::now),
	};
	const auto count = int(labels.size());
	_buttons.reserve(count);
	for (auto i = 0; i != count; ++i) {
		auto button = Markdown::LaidOutButton();
		button.label.setText(
			st::messageMarkdown.buttonRow.labelStyle,
			labels[i]);
		button.type = ButtonType::Default;
		button.color = kColors[i];
		_buttons.push_back(std::move(button));
	}
	setMouseTracking(true);
}

void RichButtonPreviewRow::setSelected(int index) {
	if ((index < 0)
		|| (index >= int(_buttons.size()))
		|| (_selected == index)) {
		return;
	}
	_selected = index;
	update();
}

void RichButtonPreviewRow::setSelectedChanged(Fn<void(int)> callback) {
	_selectedChanged = std::move(callback);
}

int RichButtonPreviewRow::resizeGetHeight(int newWidth) {
	const auto extend = RichButtonOutlineExtend();
	Markdown::LayoutButtonRowButtons(
		&_buttons,
		Markdown::TableAlignment::None,
		newWidth - 2 * extend,
		st::messageMarkdown.buttonRow);
	for (auto &button : _buttons) {
		button.rect.translate(extend, extend);
		button.labelRect.translate(extend, extend);
		button.iconRect.translate(extend, extend);
	}
	return st::messageMarkdown.buttonRow.height + 2 * extend;
}

void RichButtonPreviewRow::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	p.setTextPalette(st::messageMarkdown.textPalette);
	const auto clip = e->rect();
	const auto now = crl::now();
	for (const auto &button : _buttons) {
		Markdown::PaintRichButtonPreview(
			p,
			button,
			st::messageMarkdown,
			clip,
			now,
			width());
	}
	paintSelectionOutline(p);
}

void RichButtonPreviewRow::paintSelectionOutline(QPainter &p) {
	const auto stroke = st::ivButtonPreviewOutlineStroke;
	const auto shift = st::ivButtonPreviewOutlineSkip + (stroke / 2.);
	const auto radius = (st::messageMarkdown.buttonRow.height / 2) + shift;
	auto hq = PainterHighQualityEnabler(p);
	auto pen = st::ivButtonPreviewOutlineFg->p;
	pen.setWidthF(stroke);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(
		QRectF(_buttons[_selected].rect).marginsAdded(
			{ shift, shift, shift, shift }),
		radius,
		radius);
}

void RichButtonPreviewRow::mouseMoveEvent(QMouseEvent *e) {
	const auto index = Markdown::ButtonRowHitIndex(_buttons, e->pos());
	setCursor((index >= 0) ? style::cur_pointer : style::cur_default);
}

void RichButtonPreviewRow::mousePressEvent(QMouseEvent *e) {
	_pressed = Markdown::ButtonRowHitIndex(_buttons, e->pos());
}

void RichButtonPreviewRow::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = std::exchange(_pressed, -1);
	const auto index = Markdown::ButtonRowHitIndex(_buttons, e->pos());
	if ((index < 0) || (index != pressed)) {
		return;
	}
	setSelected(index);
	if (_selectedChanged) {
		_selectedChanged(index);
	}
}

RichButtonUserController::RichButtonUserController(
	not_null<Main::Session*> session,
	Fn<void(not_null<UserData*>)> chosen)
: ContactsBoxController(session)
, _chosen(std::move(chosen)) {
}

std::unique_ptr<PeerListRow> RichButtonUserController::createRow(
		not_null<UserData*> user) {
	if (user->isSelf()
		|| user->isBot()
		|| user->isServiceUser()
		|| user->isInaccessible()) {
		return nullptr;
	}
	return ContactsBoxController::createRow(user);
}

void RichButtonUserController::rowClicked(not_null<PeerListRow*> row) {
	if (const auto user = row->peer()->asUser()) {
		_chosen(user);
	}
}

void ChooseRichButtonUser(
		std::shared_ptr<Main::SessionShow> show,
		Fn<void(not_null<UserData*>)> chosen,
		Fn<void()> cancelled) {
	Expects(chosen != nullptr);

	struct State {
		base::weak_qptr<PeerListBox> box;
		bool picked = false;
	};
	const auto state = std::make_shared<State>();
	auto controller = std::make_unique<RichButtonUserController>(
		&show->session(),
		[=](not_null<UserData*> user) {
			state->picked = true;
			chosen(user);
			if (const auto strong = state->box.get()) {
				strong->closeBox();
			}
		});
	auto initBox = [=](not_null<PeerListBox*> box) {
		state->box = box;
		box->setTitle(tr::lng_formatting_button_choose_user());
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
		box->boxClosing() | rpl::on_next([=] {
			if (!state->picked && cancelled) {
				cancelled();
			}
		}, box->lifetime());
	};
	show->showBox(
		Box<PeerListBox>(std::move(controller), std::move(initBox)),
		Ui::LayerOption::KeepOther);
}

} // namespace

void EditRichButtonBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		RichButtonEditBoxArgs args,
		Fn<void(RichButtonEditResult)> callback,
		Fn<void(bool)> setExternalInteractionActive,
		Fn<void()> restoreFocus) {
	Expects(callback != nullptr);
	Expects(setExternalInteractionActive != nullptr);

	setExternalInteractionActive(true);
	box->boxClosing() | rpl::on_next([=] {
		setExternalInteractionActive(false);
		if (restoreFocus) {
			restoreFocus();
		}
	}, box->lifetime());

	struct State {
		Fn<void()> refreshPeerRow;
		UserData *user = nullptr;
		ButtonType previous = ButtonType::Url;
		ButtonColor color = ButtonColor::Normal;
		bool reverting = false;
	};
	const auto state = box->lifetime().make_state<State>();
	state->color = args.data.color;
	if (args.data.type != ButtonType::UserProfile) {
		state->previous = args.data.type;
	} else if (const auto bare = args.data.payload.toULongLong()) {
		state->user = show->session().data().user(UserId(bare));
	}

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_formatting_button_text(),
			st::ivFormulaSectionTitle),
		st::ivFormulaPreviewLabelMargin);
	const auto label = box->addRow(
		object_ptr<Ui::InputField>(
			box,
			st::ivFormulaSourceField,
			Ui::InputField::Mode::SingleLine,
			nullptr),
		st::markdownLinkFieldPadding);
	InitMessageFieldHandlers({
		.session = &show->session(),
		.show = show,
		.field = label,
		.fieldStyle = &st::ivFormulaSourceField,
		.allowMarkdownTags = NoMarkdownTags(),
		.allowTypedMarkdown = false,
	});
	const auto date = DefaultEditLinkCallback(
		show,
		label,
		&st::ivFormulaSourceField);
	label->setEditLinkCallback([=](
			Ui::InputField::EditLinkSelection selection,
			TextWithTags text,
			QString link,
			Ui::InputField::EditLinkAction action) {
		return Ui::InputField::IsCustomDateLink(link)
			&& date(selection, std::move(text), link, action);
	}, Ui::InputField::EditLinkItems::DateOnly);
	label->setTextWithTags(
		ConvertRichTextToEditorTags(args.data.label).text,
		Ui::InputField::HistoryAction::Clear);

	const auto separateLineField = args.separateLine
		? box->addRow(
			object_ptr<Ui::Checkbox>(
				box,
				tr::lng_formatting_button_separate_line(tr::now),
				*args.separateLine,
				st::defaultBoxCheckbox),
			st::markdownMathCheckboxMargin)
		: nullptr;

	const auto extend = RichButtonOutlineExtend();
	const auto preview = box->addRow(
		object_ptr<RichButtonPreviewRow>(box),
		st::ivButtonPreviewMargin - style::margins(
			extend,
			extend,
			extend,
			extend));
	preview->setSelected(RichButtonColorIndex(args.data.color));
	preview->setSelectedChanged([=](int index) {
		state->color = kColors[index];
	});

	const auto group = std::make_shared<Ui::RadioenumGroup<ButtonType>>(
		args.data.type);
	const auto addAction = [&](ButtonType type, const QString &text) {
		box->addRow(
			object_ptr<Ui::Radioenum<ButtonType>>(
				box,
				group,
				type,
				text,
				st::settingsPrivacyOption),
			st::ivButtonRadioPadding);
	};
	addAction(ButtonType::Url, tr::lng_formatting_button_action_url(tr::now));
	addAction(
		ButtonType::CopyText,
		tr::lng_formatting_button_action_copy(tr::now));
	addAction(
		ButtonType::UserProfile,
		tr::lng_formatting_button_action_mention(tr::now));

	const auto payloadHost = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		style::margins());
	const auto urlWrap = payloadHost->add(
		object_ptr<Ui::SlideWrap<Ui::InputField>>(
			payloadHost,
			object_ptr<Ui::InputField>(
				payloadHost,
				st::defaultInputField,
				tr::lng_formatting_link_url(),
				(args.data.type == ButtonType::Url
					? QString::fromUtf8(args.data.payload)
					: QString())),
			st::markdownLinkFieldPadding));
	const auto url = urlWrap->entity();
	const auto copyWrap = payloadHost->add(
		object_ptr<Ui::SlideWrap<Ui::InputField>>(
			payloadHost,
			object_ptr<Ui::InputField>(
				payloadHost,
				st::defaultInputField,
				tr::lng_formatting_button_copy_text(),
				(args.data.type == ButtonType::CopyText
					? QString::fromUtf8(args.data.payload)
					: QString())),
			st::markdownLinkFieldPadding));
	const auto copy = copyWrap->entity();
	const auto peerWrap = payloadHost->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			payloadHost,
			object_ptr<Ui::VerticalLayout>(payloadHost),
			st::ivButtonPeerRowMargin));
	const auto peerRow = peerWrap->entity();

	const auto chooseUser = [=] {
		ChooseRichButtonUser(show, [=](not_null<UserData*> user) {
			state->user = user;
			state->refreshPeerRow();
		}, nullptr);
	};
	state->refreshPeerRow = [=] {
		while (peerRow->count()) {
			delete peerRow->widgetAt(0);
		}
		if (state->user) {
			const auto inner = peerRow->add(
				object_ptr<Ui::VerticalLayout>(peerRow));
			AddSinglePeerRow(inner, state->user, nullptr, chooseUser);
		}
		peerRow->resizeToWidth(payloadHost->width());
	};
	state->refreshPeerRow();

	const auto showPayload = [=](ButtonType type) {
		urlWrap->toggle(type == ButtonType::Url, anim::type::instant);
		copyWrap->toggle(type == ButtonType::CopyText, anim::type::instant);
		peerWrap->toggle(
			type == ButtonType::UserProfile,
			anim::type::instant);
	};
	showPayload(args.data.type);
	const auto groupRaw = group.get();
	group->setChangedCallback([=](ButtonType value) {
		showPayload(value);
		if (state->reverting) {
			return;
		} else if (value != ButtonType::UserProfile) {
			state->previous = value;
		} else if (!state->user) {
			ChooseRichButtonUser(show, [=](not_null<UserData*> user) {
				state->user = user;
				state->refreshPeerRow();
			}, [=] {
				state->reverting = true;
				groupRaw->setValue(state->previous);
				state->reverting = false;
			});
		}
	});

	const auto submit = [=] {
		auto data = RichButtonEditData();
		data.label = Markdown::NormalizeRichButtonLabel(
			ConvertEditorTagsToRichText(label->getTextWithAppliedMarkdown()));
		if (data.label.empty()) {
			label->showError();
			return;
		}
		data.color = state->color;
		data.type = groupRaw->current();
		switch (data.type) {
		case ButtonType::Url: {
			const auto validated = args.validateUrl(url->getLastText());
			if (validated.isEmpty()) {
				url->showError();
				return;
			}
			data.payload = validated.toUtf8();
		} break;
		case ButtonType::CopyText: {
			const auto text = copy->getLastText().trimmed();
			if (text.isEmpty()) {
				copy->showError();
				return;
			}
			data.payload = text.toUtf8();
		} break;
		case ButtonType::UserProfile: {
			if (!state->user) {
				box->uiShow()->showToast(
					tr::lng_formatting_button_no_user(tr::now));
				return;
			}
			data.payload = QByteArray::number(
				peerToUser(state->user->id).bare);
		} break;
		}
		const auto weak = base::make_weak(box);
		callback({
			.data = std::move(data),
			.separateLine = (separateLineField
				&& separateLineField->checked()),
		});
		if (weak) {
			box->closeBox();
		}
	};
	label->submits(
	) | rpl::on_next(submit, label->lifetime());
	url->submits(
	) | rpl::on_next(submit, url->lifetime());
	copy->submits(
	) | rpl::on_next(submit, copy->lifetime());

	box->setTitle(args.editingExisting
		? tr::lng_formatting_button_edit_title()
		: tr::lng_formatting_button_create_title());
	box->addButton(tr::lng_settings_save(), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });

	box->verticalLayout()->resizeToWidth(st::boxWidth);
	box->verticalLayout()->moveToLeft(0, 0);
	box->setWidth(st::boxWidth);

	box->setFocusCallback([=] {
		label->setFocusFast();
	});
}

} // namespace Iv::Editor
