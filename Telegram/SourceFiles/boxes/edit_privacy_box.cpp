/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "boxes/edit_privacy_box.h"

#include "styles/style_boxes.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
#include "boxes/peer_list_controllers.h"
#include "apiwrap.h"
#include "auth_session.h"
#include "lang/lang_keys.h"

namespace {

class PrivacyExceptionsBoxController : public ChatsListBoxController {
public:
	PrivacyExceptionsBoxController(base::lambda<QString()> titleFactory, const std::vector<not_null<UserData*>> &selected);
	void rowClicked(not_null<PeerListRow*> row) override;

	std::vector<not_null<UserData*>> getResult() const;

protected:
	void prepareViewHook() override;
	std::unique_ptr<Row> createRow(not_null<History*> history) override;

private:
	base::lambda<QString()> _titleFactory;
	std::vector<not_null<UserData*>> _selected;

};

PrivacyExceptionsBoxController::PrivacyExceptionsBoxController(base::lambda<QString()> titleFactory, const std::vector<not_null<UserData*>> &selected)
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

EditPrivacyBox::EditPrivacyBox(QWidget*, std::unique_ptr<Controller> controller) : BoxContent()
, _controller(std::move(controller))
, _loading(this, lang(lng_contacts_loading), Ui::FlatLabel::InitType::Simple, st::membersAbout) {
}

void EditPrivacyBox::prepare() {
	_controller->setView(this);

	setTitle([this] { return _controller->title(); });
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	loadData();

	setDimensions(st::boxWideWidth, countDefaultHeight(st::boxWideWidth));
}

int EditPrivacyBox::resizeGetHeight(int newWidth) {
	auto top = 0;
	auto layoutRow = [this, newWidth, &top](auto &widget, style::margins padding) {
		if (!widget) return;
		widget->resizeToNaturalWidth(newWidth - padding.left() - padding.right());
		widget->moveToLeft(padding.left(), top + padding.top());
		top = widget->bottomNoMargins() + padding.bottom();
	};

	layoutRow(_description, st::editPrivacyPadding);
	layoutRow(_everyone, st::editPrivacyOptionMargin);
	layoutRow(_contacts, st::editPrivacyOptionMargin);
	layoutRow(_nobody, st::editPrivacyOptionMargin);
	layoutRow(_warning, st::editPrivacyWarningPadding);
	layoutRow(_exceptionsTitle, st::editPrivacyTitlePadding);
	auto linksTop = top;
	layoutRow(_alwaysLink, st::editPrivacyPadding);
	layoutRow(_neverLink, st::editPrivacyPadding);
	auto linksHeight = top - linksTop;
	layoutRow(_exceptionsDescription, st::editPrivacyPadding);

	// Add full width of both links in any case
	auto linkMargins = exceptionLinkMargins();
	top -= linksHeight;
	top += linkMargins.top() + st::boxLinkButton.font->height + linkMargins.bottom();
	top += linkMargins.top() + st::boxLinkButton.font->height + linkMargins.bottom();

	return top;
}

void EditPrivacyBox::resizeEvent(QResizeEvent *e) {
	if (_loading) {
		_loading->moveToLeft((width() - _loading->width()) / 2, height() / 3);
	}
}

int EditPrivacyBox::countDefaultHeight(int newWidth) {
	auto height = 0;
	auto optionHeight = [this](Option option) {
		if (!_controller->hasOption(option)) {
			return 0;
		}
		return st::editPrivacyOptionMargin.top() + st::defaultCheck.diameter + st::editPrivacyOptionMargin.bottom();
	};
	auto labelHeight = [this, newWidth](const QString &text, const style::FlatLabel &st, style::margins padding) {
		if (text.isEmpty()) {
			return 0;
		}

		auto fake = object_ptr<Ui::FlatLabel>(nullptr, text, Ui::FlatLabel::InitType::Simple, st);
		fake->resizeToNaturalWidth(newWidth - padding.left() - padding.right());
		return padding.top() + fake->heightNoMargins() + padding.bottom();
	};
	auto linkHeight = [this]() {
		auto linkMargins = exceptionLinkMargins();
		return linkMargins.top() + st::boxLinkButton.font->height + linkMargins.bottom();
	};
	height += labelHeight(_controller->description(), st::editPrivacyLabel, st::editPrivacyPadding);
	height += optionHeight(Option::Everyone);
	height += optionHeight(Option::Contacts);
	height += optionHeight(Option::Nobody);
	height += labelHeight(_controller->warning(), st::editPrivacyLabel, st::editPrivacyWarningPadding);
	height += labelHeight(lang(lng_edit_privacy_exceptions), st::editPrivacyTitle, st::editPrivacyTitlePadding);
	height += linkHeight();
	height += linkHeight();
	height += labelHeight(_controller->exceptionsDescription(), st::editPrivacyLabel, st::editPrivacyPadding);
	return height;
}

void EditPrivacyBox::editExceptionUsers(Exception exception) {
	auto controller = std::make_unique<PrivacyExceptionsBoxController>(base::lambda_guarded(this, [this, exception] {
		return _controller->exceptionBoxTitle(exception);
	}), exceptionUsers(exception));
	auto initBox = [this, exception, controller = controller.get()](not_null<PeerListBox*> box) {
		box->addButton(langFactory(lng_settings_save), base::lambda_guarded(this, [this, box, exception, controller] {
			exceptionUsers(exception) = controller->getResult();
			exceptionLink(exception)->entity()->setText(exceptionLinkText(exception));
			auto removeFrom = ([exception] {
				switch (exception) {
				case Exception::Always: return Exception::Never;
				case Exception::Never: return Exception::Always;
				}
				Unexpected("Invalid exception value.");
			})();
			auto &removeFromUsers = exceptionUsers(removeFrom);
			auto removedSome = false;
			for (auto user : exceptionUsers(exception)) {
				auto removedStart = std::remove(removeFromUsers.begin(), removeFromUsers.end(), user);
				if (removedStart != removeFromUsers.end()) {
					removeFromUsers.erase(removedStart, removeFromUsers.end());
					removedSome = true;
				}
			}
			if (removedSome) {
				exceptionLink(removeFrom)->entity()->setText(exceptionLinkText(removeFrom));
			}
			box->closeBox();
		}));
		box->addButton(langFactory(lng_cancel), [box] { box->closeBox(); });
	};
	Ui::show(
		Box<PeerListBox>(std::move(controller), std::move(initBox)),
		LayerOption::KeepOther);
}

QString EditPrivacyBox::exceptionLinkText(Exception exception) {
	return _controller->exceptionLinkText(exception, exceptionUsers(exception).size());
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
	if (showExceptionLink(Exception::Always) && !_alwaysUsers.empty()) {
		result.push_back(MTP_inputPrivacyValueAllowUsers(MTP_vector<MTPInputUser>(collectInputUsers(_alwaysUsers))));
	}
	if (showExceptionLink(Exception::Never) && !_neverUsers.empty()) {
		result.push_back(MTP_inputPrivacyValueDisallowUsers(MTP_vector<MTPInputUser>(collectInputUsers(_neverUsers))));
	}
	switch (_option) {
	case Option::Everyone: result.push_back(MTP_inputPrivacyValueAllowAll()); break;
	case Option::Contacts: result.push_back(MTP_inputPrivacyValueAllowContacts()); break;
	case Option::Nobody: result.push_back(MTP_inputPrivacyValueDisallowAll()); break;
	}

	return result;
}

style::margins EditPrivacyBox::exceptionLinkMargins() const {
	return st::editPrivacyLinkMargin;
}

std::vector<not_null<UserData*>> &EditPrivacyBox::exceptionUsers(Exception exception) {
	switch (exception) {
	case Exception::Always: return _alwaysUsers;
	case Exception::Never: return _neverUsers;
	}
	Unexpected("Invalid exception value.");
}

object_ptr<Ui::SlideWrap<Ui::LinkButton>> &EditPrivacyBox::exceptionLink(Exception exception) {
	switch (exception) {
	case Exception::Always: return _alwaysLink;
	case Exception::Never: return _neverLink;
	}
	Unexpected("Invalid exception value.");
}

bool EditPrivacyBox::showExceptionLink(Exception exception) const {
	switch (exception) {
	case Exception::Always: return (_option == Option::Contacts) || (_option == Option::Nobody);
	case Exception::Never: return (_option == Option::Everyone) || (_option == Option::Contacts);
	}
	Unexpected("Invalid exception value.");
}

void EditPrivacyBox::createWidgets() {
	_loading.destroy();
	_optionGroup = std::make_shared<Ui::RadioenumGroup<Option>>(_option);

	auto createOption = [this](object_ptr<Ui::Radioenum<Option>> &widget, Option option, const QString &label) {
		if (_controller->hasOption(option) || (_option == option)) {
			widget.create(this, _optionGroup, option, label, st::defaultBoxCheckbox);
		}
	};
	auto createLabel = [this](object_ptr<Ui::FlatLabel> &widget, const QString &text, const style::FlatLabel &st) {
		if (text.isEmpty()) {
			return;
		}
		widget.create(this, text, Ui::FlatLabel::InitType::Simple, st);
	};
	auto createExceptionLink = [this](Exception exception) {
		exceptionLink(exception).create(this, object_ptr<Ui::LinkButton>(this, exceptionLinkText(exception)), exceptionLinkMargins());
		exceptionLink(exception)->heightValue()
			| rpl::start_with_next([this] {
				resizeToWidth(width());
			}, lifetime());
		exceptionLink(exception)->entity()->setClickedCallback([this, exception] { editExceptionUsers(exception); });
	};

	createLabel(_description, _controller->description(), st::editPrivacyLabel);
	createOption(_everyone, Option::Everyone, lang(lng_edit_privacy_everyone));
	createOption(_contacts, Option::Contacts, lang(lng_edit_privacy_contacts));
	createOption(_nobody, Option::Nobody, lang(lng_edit_privacy_nobody));
	createLabel(_warning, _controller->warning(), st::editPrivacyLabel);
	createLabel(_exceptionsTitle, lang(lng_edit_privacy_exceptions), st::editPrivacyTitle);
	createExceptionLink(Exception::Always);
	createExceptionLink(Exception::Never);
	createLabel(_exceptionsDescription, _controller->exceptionsDescription(), st::editPrivacyLabel);

	clearButtons();
	addButton(langFactory(lng_settings_save), [this] {
		auto someAreDisallowed = (_option != Option::Everyone) || !_neverUsers.empty();
		_controller->confirmSave(someAreDisallowed, base::lambda_guarded(this, [this] {
			Auth().api().savePrivacy(_controller->key(), collectResult());
			closeBox();
		}));
	});
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	_optionGroup->setChangedCallback([this](Option value) {
		_option = value;
		_alwaysLink->toggle(
			showExceptionLink(Exception::Always),
			anim::type::normal);
		_neverLink->toggle(
			showExceptionLink(Exception::Never),
			anim::type::normal);
	});

	showChildren();
	_alwaysLink->toggle(
		showExceptionLink(Exception::Always),
		anim::type::instant);
	_neverLink->toggle(
		showExceptionLink(Exception::Never),
		anim::type::instant);

	setDimensions(st::boxWideWidth, resizeGetHeight(st::boxWideWidth));
}

void EditPrivacyBox::loadData() {
        request(MTPaccount_GetPrivacy(_controller->key())).done([this](const MTPaccount_PrivacyRules &result) {
		Expects(result.type() == mtpc_account_privacyRules);
		auto &rules = result.c_account_privacyRules();
		App::feedUsers(rules.vusers);

		// This is simplified version of privacy rules interpretation.
		// But it should be fine for all the apps that use the same subset of features.
		auto optionSet = false;
		auto setOption = [this, &optionSet](Option option) {
			if (optionSet) return;
			optionSet = true;
			_option = option;
		};
		auto feedRule = [this, &setOption](const MTPPrivacyRule &rule) {
			switch (rule.type()) {
			case mtpc_privacyValueAllowAll: setOption(Option::Everyone); break;
			case mtpc_privacyValueAllowContacts: setOption(Option::Contacts); break;
			case mtpc_privacyValueAllowUsers: {
				auto &users = rule.c_privacyValueAllowUsers().vusers.v;
				_alwaysUsers.reserve(_alwaysUsers.size() + users.size());
				for (auto &userId : users) {
					auto user = App::user(UserId(userId.v));
					if (!base::contains(_neverUsers, user) && !base::contains(_alwaysUsers, user)) {
						_alwaysUsers.push_back(user);
					}
				}
			} break;
			case mtpc_privacyValueDisallowContacts: // not supported, fall through
			case mtpc_privacyValueDisallowAll: setOption(Option::Nobody); break;
			case mtpc_privacyValueDisallowUsers: {
				auto &users = rule.c_privacyValueDisallowUsers().vusers.v;
				_neverUsers.reserve(_neverUsers.size() + users.size());
				for (auto &userId : users) {
					auto user = App::user(UserId(userId.v));
					if (!base::contains(_alwaysUsers, user) && !base::contains(_neverUsers, user)) {
						_neverUsers.push_back(user);
					}
				}
			} break;
			}
		};
		for (auto &rule : rules.vrules.v) {
			feedRule(rule);
		}
		feedRule(MTP_privacyValueDisallowAll()); // disallow by default.

		createWidgets();
        }).send();
}

