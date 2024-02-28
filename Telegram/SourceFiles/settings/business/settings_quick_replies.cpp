/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/business/settings_quick_replies.h"

#include "core/application.h"
#include "data/business/data_shortcut_messages.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/business/settings_recipients_helper.h"
#include "settings/business/settings_shortcut_messages.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

class QuickReplies : public BusinessSection<QuickReplies> {
public:
	QuickReplies(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~QuickReplies();

	[[nodiscard]] rpl::producer<QString> title() override;
	[[nodiscard]] rpl::producer<Type> sectionShowOther() override;

private:
	void setupContent(not_null<Window::SessionController*> controller);
	void save();

	rpl::event_stream<Type> _showOther;

};

QuickReplies::QuickReplies(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: BusinessSection(parent, controller) {
	setupContent(controller);
}

QuickReplies::~QuickReplies() {
	if (!Core::Quitting()) {
		save();
	}
}

rpl::producer<QString> QuickReplies::title() {
	return tr::lng_replies_title();
}

rpl::producer<Type> QuickReplies::sectionShowOther() {
	return _showOther.events();
}

void QuickReplies::setupContent(
		not_null<Window::SessionController*> controller) {
	using namespace rpl::mappers;

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	AddDividerTextWithLottie(content, {
		.lottie = u"writing"_q,
		.lottieSize = st::settingsCloudPasswordIconSize,
		.lottieMargins = st::peerAppearanceIconPadding,
		.showFinished = showFinishes(),
		.about = tr::lng_replies_about(Ui::Text::WithEntities),
		.aboutMargins = st::peerAppearanceCoverLabelMargin,
	});

	Ui::AddSkip(content);
	const auto add = content->add(object_ptr<Ui::SettingsButton>(
		content,
		tr::lng_replies_add(),
		st::settingsButtonNoIcon
	));

	const auto owner = &controller->session().data();
	const auto messages = &owner->shortcutMessages();

	add->setClickedCallback([=] {
		controller->show(Box([=](not_null<Ui::GenericBox*> box) {
			box->setTitle(tr::lng_replies_add_title());
			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_replies_add_shortcut(),
				st::settingsAddReplyLabel));
			const auto field = box->addRow(object_ptr<Ui::InputField>(
				box,
				st::settingsAddReplyField,
				tr::lng_replies_add_placeholder(),
				QString()));
			box->setFocusCallback([=] {
				field->setFocusFast();
			});

			const auto submit = [=] {
				const auto weak = Ui::MakeWeak(box);
				const auto name = field->getLastText().trimmed();
				if (name.isEmpty()) {
					field->showError();
				} else {
					const auto id = messages->emplaceShortcut(name);
					_showOther.fire(ShortcutMessagesId(id));
				}
				if (const auto strong = weak.data()) {
					strong->closeBox();
				}
			};
			field->submits(
			) | rpl::start_with_next(submit, field->lifetime());
			box->addButton(tr::lng_settings_save(), submit);
			box->addButton(tr::lng_cancel(), [=] {
				box->closeBox();
			});
		}));
	});

	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);

	const auto inner = content->add(
		object_ptr<Ui::VerticalLayout>(content));
	rpl::single(rpl::empty) | rpl::then(
		messages->shortcutsChanged()
	) | rpl::start_with_next([=] {
		while (inner->count()) {
			delete inner->widgetAt(0);
		}
		const auto &shortcuts = messages->shortcuts();
		auto i = 0;
		for (const auto &shortcut : shortcuts.list) {
			const auto name = shortcut.second.name;
			AddButtonWithLabel(
				inner,
				rpl::single('/' + name),
				tr::lng_forum_messages(
					lt_count,
					rpl::single(1. * shortcut.second.count)),
				st::settingsButtonNoIcon
			)->setClickedCallback([=] {
				const auto id = messages->emplaceShortcut(name);
				_showOther.fire(ShortcutMessagesId(id));
			});
		}
	}, content->lifetime());

	Ui::ResizeFitChild(this, content);
}

void QuickReplies::save() {
}

} // namespace

Type QuickRepliesId() {
	return QuickReplies::Id();
}

} // namespace Settings
