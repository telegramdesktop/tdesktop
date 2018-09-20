/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/edit_privacy_box.h"

#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "history/history.h"
#include "boxes/peer_list_controllers.h"
#include "info/profile/info_profile_button.h"
#include "settings/settings_common.h"
#include "calls/calls_instance.h"
#include "base/binary_guard.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "auth_session.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"

namespace {

class PrivacyExceptionsBoxController : public ChatsListBoxController {
public:
	PrivacyExceptionsBoxController(Fn<QString()> titleFactory, const std::vector<not_null<UserData*>> &selected);
	void rowClicked(not_null<PeerListRow*> row) override;

	std::vector<not_null<UserData*>> getResult() const;

protected:
	void prepareViewHook() override;
	std::unique_ptr<Row> createRow(not_null<History*> history) override;

private:
	Fn<QString()> _titleFactory;
	std::vector<not_null<UserData*>> _selected;

};

PrivacyExceptionsBoxController::PrivacyExceptionsBoxController(Fn<QString()> titleFactory, const std::vector<not_null<UserData*>> &selected)
: _titleFactory(std::move(titleFactory))
, _selected(selected) {
}

void PrivacyExceptionsBoxController::prepareViewHook() {
	delegate()->peerListSetTitle(_titleFactory);
	delegate()->peerListAddSelectedRows(_selected);
}

std::vector<not_null<UserData*>> PrivacyExceptionsBoxController::getResult() const {
	auto peers = delegate()->peerListCollectSelectedRows();
	auto users = std::vector<not_null<UserData*>>();
	if (!peers.empty()) {
		users.reserve(peers.size());
		for_const (auto peer, peers) {
			auto user = peer->asUser();
			Assert(user != nullptr);
			users.push_back(user);
		}
	}
	return users;
}

void PrivacyExceptionsBoxController::rowClicked(not_null<PeerListRow*> row) {
	delegate()->peerListSetRowChecked(row, !row->checked());
}

std::unique_ptr<PrivacyExceptionsBoxController::Row> PrivacyExceptionsBoxController::createRow(not_null<History*> history) {
	if (history->peer->isSelf()) {
		return nullptr;
	}
	if (auto user = history->peer->asUser()) {
		return std::make_unique<Row>(history);
	}
	return nullptr;
}

} // namespace

EditPrivacyBox::EditPrivacyBox(
	QWidget*,
	std::unique_ptr<Controller> controller,
	const Value &value)
: _controller(std::move(controller))
, _value(value) {
}

void EditPrivacyBox::prepare() {
	_controller->setView(this);

	setupContent();
}

void EditPrivacyBox::editExceptionUsers(
		Exception exception,
		Fn<void()> done) {
	auto controller = std::make_unique<PrivacyExceptionsBoxController>(
		crl::guard(this, [=] {
			return _controller->exceptionBoxTitle(exception);
		}),
		exceptionUsers(exception));
	auto initBox = [=, controller = controller.get()](
			not_null<PeerListBox*> box) {
		box->addButton(langFactory(lng_settings_save), crl::guard(this, [=] {
			exceptionUsers(exception) = controller->getResult();
			const auto removeFrom = ([=] {
				switch (exception) {
				case Exception::Always: return Exception::Never;
				case Exception::Never: return Exception::Always;
				}
				Unexpected("Invalid exception value.");
			})();
			auto &removeFromUsers = exceptionUsers(removeFrom);
			for (const auto user : exceptionUsers(exception)) {
				const auto from = ranges::remove(removeFromUsers, user);
				removeFromUsers.erase(from, end(removeFromUsers));
			}
			done();
			box->closeBox();
		}));
		box->addButton(langFactory(lng_cancel), [=] { box->closeBox(); });
	};
	Ui::show(
		Box<PeerListBox>(std::move(controller), std::move(initBox)),
		LayerOption::KeepOther);
}

QVector<MTPInputPrivacyRule> EditPrivacyBox::collectResult() {
	auto collectInputUsers = [](auto &users) {
		auto result = QVector<MTPInputUser>();
		result.reserve(users.size());
		for (auto user : users) {
			result.push_back(user->inputUser);
		}
		return result;
	};

	constexpr auto kMaxRules = 3; // allow users, disallow users, option
	auto result = QVector<MTPInputPrivacyRule>();
	result.reserve(kMaxRules);
	if (showExceptionLink(Exception::Always) && !_value.always.empty()) {
		result.push_back(MTP_inputPrivacyValueAllowUsers(MTP_vector<MTPInputUser>(collectInputUsers(_value.always))));
	}
	if (showExceptionLink(Exception::Never) && !_value.never.empty()) {
		result.push_back(MTP_inputPrivacyValueDisallowUsers(MTP_vector<MTPInputUser>(collectInputUsers(_value.never))));
	}
	result.push_back([&] {
		switch (_value.option) {
		case Option::Everyone: return MTP_inputPrivacyValueAllowAll();
		case Option::Contacts: return MTP_inputPrivacyValueAllowContacts();
		case Option::Nobody: return MTP_inputPrivacyValueDisallowAll();
		}
		Unexpected("Option value in EditPrivacyBox::collectResult.");
	}());

	return result;
}

std::vector<not_null<UserData*>> &EditPrivacyBox::exceptionUsers(Exception exception) {
	switch (exception) {
	case Exception::Always: return _value.always;
	case Exception::Never: return _value.never;
	}
	Unexpected("Invalid exception value.");
}

bool EditPrivacyBox::showExceptionLink(Exception exception) const {
	switch (exception) {
	case Exception::Always:
		return (_value.option == Option::Contacts)
			|| (_value.option == Option::Nobody);
	case Exception::Never:
		return (_value.option == Option::Everyone)
			|| (_value.option == Option::Contacts);
	}
	Unexpected("Invalid exception value.");
}

Ui::Radioenum<EditPrivacyBox::Option> *EditPrivacyBox::AddOption(
		not_null<Ui::VerticalLayout*> container,
		const std::shared_ptr<Ui::RadioenumGroup<Option>> &group,
		Option option) {
	const auto label = [&] {
		switch (option) {
		case Option::Everyone: return lng_edit_privacy_everyone;
		case Option::Contacts: return lng_edit_privacy_contacts;
		case Option::Nobody: return lng_edit_privacy_nobody;
		}
		Unexpected("Option value in EditPrivacyBox::AddOption.");
	}();
	return container->add(
		object_ptr<Ui::Radioenum<Option>>(
			container,
			group,
			option,
			lang(label),
			st::settingsSendType),
		st::settingsSendTypePadding);
}

Ui::FlatLabel *EditPrivacyBox::AddLabel(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text) {
	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				rpl::duplicate(text),
				st::boxDividerLabel),
			st::settingsPrivacyEditLabelPadding));
	wrap->hide(anim::type::instant);
	wrap->toggleOn(std::move(
		text
	) | rpl::map([](const QString &text) {
		return !text.isEmpty();
	}));
	return wrap->entity();
}

void EditPrivacyBox::setupContent() {
	using namespace Settings;

	setTitle([=] { return _controller->title(); });

	auto wrap = object_ptr<Ui::VerticalLayout>(this);
	const auto content = wrap.data();
	setInnerWidget(object_ptr<Ui::OverrideMargins>(
		this,
		std::move(wrap)));

	const auto group = std::make_shared<Ui::RadioenumGroup<Option>>(
		_value.option);
	const auto toggle = Ui::AttachAsChild(content, rpl::event_stream<>());

	group->setChangedCallback([=](Option value) {
		_value.option = value;
		toggle->fire({});
	});

	const auto addOption = [&](Option option) {
		return (_controller->hasOption(option) || (_value.option == option))
			? AddOption(content, group, option)
			: nullptr;
	};
	const auto addExceptionLink = [=](Exception exception) {
		const auto update = Ui::AttachAsChild(
			content,
			rpl::event_stream<>());
		auto label = update->events_starting_with(
			rpl::empty_value()
		) | rpl::map([=] {
			return exceptionUsers(exception).size();
		}) | rpl::map([](int count) {
			return count
				? lng_edit_privacy_exceptions_count(lt_count, count)
				: lang(lng_edit_privacy_exceptions_add);
		});
		auto text = _controller->exceptionButtonTextKey(exception);
		const auto button = content->add(
			object_ptr<Ui::SlideWrap<Button>>(
				content,
				object_ptr<Button>(
					content,
					Lang::Viewer(text),
					st::settingsButton)));
		CreateRightLabel(
			button->entity(),
			std::move(label),
			st::settingsButton,
			text);
		button->toggleOn(toggle->events_starting_with(
			rpl::empty_value()
		) | rpl::map([=] {
			return showExceptionLink(exception);
		}))->entity()->addClickHandler([=] {
			editExceptionUsers(exception, [=] { update->fire({}); });
		});
		return button;
	};

	AddSubsectionTitle(content, _controller->optionsTitleKey());
	addOption(Option::Everyone);
	addOption(Option::Contacts);
	addOption(Option::Nobody);
	AddLabel(content, _controller->warning());
	AddSkip(content);

	AddDivider(content);
	AddSkip(content);
	AddSubsectionTitle(content, lng_edit_privacy_exceptions);
	const auto always = addExceptionLink(Exception::Always);
	const auto never = addExceptionLink(Exception::Never);
	AddLabel(content, _controller->exceptionsDescription());
	AddSkip(content);

	const auto saveAdditional = _controller->setupAdditional(content);

	addButton(langFactory(lng_settings_save), [=] {
		const auto someAreDisallowed = (_value.option != Option::Everyone)
			|| !_value.never.empty();
		_controller->confirmSave(someAreDisallowed, crl::guard(this, [=] {
			Auth().api().savePrivacy(
				_controller->apiKey(),
				collectResult());
			if (saveAdditional) {
				saveAdditional();
			}
			closeBox();
		}));
	});
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	const auto linkHeight = st::settingsButton.padding.top()
		+ st::settingsButton.height
		+ st::settingsButton.padding.bottom();

	widthValue(
	) | rpl::start_with_next([=](int width) {
		content->resizeToWidth(width);
	}, content->lifetime());

	content->heightValue(
	) | rpl::map([=](int height) {
		return height - always->height() - never->height() + 2 * linkHeight;
	}) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](int height) {
		setDimensions(st::boxWideWidth, height);
	}, content->lifetime());
}
