/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/business/settings_quick_replies.h"

#include "boxes/premium_preview_box.h"
#include "core/application.h"
#include "data/business/data_shortcut_messages.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
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

private:
	void setupContent(not_null<Window::SessionController*> controller);

	rpl::variable<int> _count;

};

QuickReplies::QuickReplies(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: BusinessSection(parent, controller) {
	setupContent(controller);
}

QuickReplies::~QuickReplies() = default;

rpl::producer<QString> QuickReplies::title() {
	return tr::lng_replies_title();
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

	const auto addWrap = content->add(
		object_ptr<Ui::VerticalLayout>(content));

	const auto owner = &controller->session().data();
	const auto messages = &owner->shortcutMessages();

	rpl::combine(
		_count.value(),
		ShortcutsLimitValue(&controller->session())
	) | rpl::start_with_next([=](int count, int limit) {
		while (addWrap->count()) {
			delete addWrap->widgetAt(0);
		}
		if (count < limit) {
			const auto add = addWrap->add(object_ptr<Ui::SettingsButton>(
				addWrap,
				tr::lng_replies_add(),
				st::settingsButtonNoIcon
			));

			add->setClickedCallback([=] {
				if (!controller->session().premium()) {
					ShowPremiumPreviewToBuy(
						controller,
						PremiumFeature::QuickReplies);
					return;
				}
				const auto submit = [=](QString name, Fn<void()> close) {
					const auto id = messages->emplaceShortcut(name);
					showOther(ShortcutMessagesId(id));
					close();
				};
				controller->show(
					Box(EditShortcutNameBox, QString(), crl::guard(this, submit)));
			});
			if (count > 0) {
				AddSkip(addWrap);
				AddDivider(addWrap);
				AddSkip(addWrap);
			}
		}
		if (const auto width = content->width()) {
			content->resizeToWidth(width);
		}
	}, lifetime());

	const auto inner = content->add(
		object_ptr<Ui::VerticalLayout>(content));
	rpl::single(rpl::empty) | rpl::then(
		messages->shortcutsChanged()
	) | rpl::start_with_next([=] {
		auto old = inner->count();

		const auto &shortcuts = messages->shortcuts();
		auto i = 0;
		for (const auto &[_, shortcut]
			: shortcuts.list | ranges::views::reverse) {
			if (!shortcut.count) {
				continue;
			}
			const auto name = shortcut.name;
			AddButtonWithLabel(
				inner,
				rpl::single('/' + name),
				tr::lng_forum_messages(
					lt_count,
					rpl::single(1. * shortcut.count)),
				st::settingsButtonNoIcon
			)->setClickedCallback([=] {
				const auto id = messages->emplaceShortcut(name);
				showOther(ShortcutMessagesId(id));
			});
			if (old) {
				delete inner->widgetAt(0);
				--old;
			}
		}
		while (old--) {
			delete inner->widgetAt(0);
		}
		_count = inner->count();
	}, content->lifetime());

	Ui::ResizeFitChild(this, content);
}

[[nodiscard]] bool ValidShortcutName(const QString &name) {
	if (name.isEmpty() || name.size() > 32) {
		return false;
	}
	for (const auto &ch : name) {
		if (!ch.isLetterOrNumber()
			&& (ch != '_')
			&& (ch != 0x200c)
			&& (ch != 0x00b7)
			&& (ch < 0x0d80 || ch > 0x0dff)) {
			return false;
		}
	}
	return true;
}

} // namespace

Type QuickRepliesId() {
	return QuickReplies::Id();
}

void EditShortcutNameBox(
		not_null<Ui::GenericBox*> box,
		QString name,
		Fn<void(QString, Fn<void()>)> submit) {
	name = name.trimmed();
	const auto editing = !name.isEmpty();
	box->setTitle(editing
		? tr::lng_replies_edit_title()
		: tr::lng_replies_add_title());
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		(editing
			? tr::lng_replies_edit_about()
			: tr::lng_replies_add_shortcut()),
		st::settingsAddReplyLabel));
	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::settingsAddReplyField,
		tr::lng_replies_add_placeholder(),
		name));
	box->setFocusCallback([=] {
		field->setFocusFast();
	});
	field->selectAll();

	const auto callback = [=] {
		const auto name = field->getLastText().trimmed();
		if (!ValidShortcutName(name)) {
			field->showError();
		} else {
			submit(name, [weak = Ui::MakeWeak(box)] {
				if (const auto strong = weak.data()) {
					strong->closeBox();
				}
			});
		}
	};
	field->submits(
	) | rpl::start_with_next(callback, field->lifetime());
	box->addButton(tr::lng_settings_save(), callback);
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

} // namespace Settings
