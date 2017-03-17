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
#include "ui/effects/widget_slide_wrap.h"
#include "boxes/peer_list_box.h"
#include "lang.h"

namespace {

class PrivacyExceptionsBoxController : public ChatsListBoxController {
public:
	PrivacyExceptionsBoxController(const QString &title, const QVector<UserData*> &selected, base::lambda_once<void(QVector<UserData*> &&result)> saveCallback);
	void rowClicked(PeerListBox::Row *row) override;

protected:
	void prepareViewHook() override;
	std::unique_ptr<Row> createRow(History *history) override;

private:
	QString _title;
	QVector<UserData*> _selected;
	base::lambda_once<void(QVector<UserData*> &&result)> _saveCallback;

};

PrivacyExceptionsBoxController::PrivacyExceptionsBoxController(const QString &title, const QVector<UserData*> &selected, base::lambda_once<void(QVector<UserData*> &&result)> saveCallback)
: _title(title)
, _selected(selected)
, _saveCallback(std::move(saveCallback)) {
}

void PrivacyExceptionsBoxController::prepareViewHook() {
	view()->setTitle(_title);
	view()->addButton(lang(lng_settings_save), [this] {
		auto peers = view()->collectSelectedRows();
		auto users = QVector<UserData*>();
		if (!peers.empty()) {
			users.reserve(peers.size());
			for_const (auto peer, peers) {
				auto user = peer->asUser();
				t_assert(user != nullptr);
				users.push_back(user);
			}
		}
		_saveCallback(std::move(users));
		view()->closeBox();
	});
	view()->addButton(lang(lng_cancel), [this] { view()->closeBox(); });
	view()->addSelectedRows(_selected);
}

void PrivacyExceptionsBoxController::rowClicked(PeerListBox::Row *row) {
	view()->setRowChecked(row, !row->checked());
}

std::unique_ptr<PrivacyExceptionsBoxController::Row> PrivacyExceptionsBoxController::createRow(History *history) {
	if (auto user = history->peer->asUser()) {
		if (!user->isSelf()) {
			return std::make_unique<Row>(history);
		}
	}
	return std::unique_ptr<Row>();
}

} // namespace

class EditPrivacyBox::OptionWidget : public TWidget {
public:
	OptionWidget(QWidget *parent, int value, bool selected, const QString &text, const QString &description);

	void setChangedCallback(base::lambda<void()> callback) {
		connect(_option, SIGNAL(changed()), base::lambda_slot(this, std::move(callback)), SLOT(action()));
	}
	bool checked() const {
		return _option->checked();
	}

protected:
	int resizeGetHeight(int newWidth) override;

private:
	object_ptr<Ui::Radiobutton> _option;
	object_ptr<Ui::FlatLabel> _description;

};

EditPrivacyBox::OptionWidget::OptionWidget(QWidget *parent, int value, bool selected, const QString &text, const QString &description) : TWidget(parent)
, _option(this, qsl("privacy_option"), value, text, selected, st::defaultBoxCheckbox)
, _description(this, description, Ui::FlatLabel::InitType::Simple, st::editPrivacyLabel) {
}

int EditPrivacyBox::OptionWidget::resizeGetHeight(int newWidth) {
	_option->resizeToNaturalWidth(newWidth);
	auto optionTextLeft = st::defaultBoxCheckbox.textPosition.x();
	_description->resizeToWidth(newWidth - optionTextLeft);
	_option->moveToLeft(0, 0);
	_description->moveToLeft(optionTextLeft, _option->bottomNoMargins());
	return _description->bottomNoMargins();
}

EditPrivacyBox::EditPrivacyBox(QWidget*, std::unique_ptr<Controller> controller) : BoxContent()
, _controller(std::move(controller))
, _loading(this, lang(lng_contacts_loading), Ui::FlatLabel::InitType::Simple, st::membersAbout) {
}

void EditPrivacyBox::prepare() {
	_controller->setView(this);

	setTitle(_controller->title());
	addButton(lang(lng_cancel), [this] { closeBox(); });

	_loadRequestId = MTP::send(MTPaccount_GetPrivacy(_controller->key()), rpcDone(base::lambda_guarded(this, [this](const MTPaccount_PrivacyRules &result) {
		_loadRequestId = 0;
		loadDone(result);
	})), rpcFail(base::lambda_guarded(this, [this](const RPCError &error) {
		if (MTP::isDefaultHandledError(error)) {
			return false;
		}
		_loadRequestId = 0;
		return true;
	})));

	setDimensions(st::boxWideWidth, countDefaultHeight(st::boxWideWidth));

	_loading->resizeToWidth(width());
	_loading->moveToLeft(0, height() / 3);
}

int EditPrivacyBox::resizeGetHeight(int newWidth) {
	auto top = 0;
	auto layoutRow = [this, newWidth, &top](auto &widget, style::margins padding) {
		if (!widget) return;
		widget->resizeToNaturalWidth(newWidth - padding.left() - padding.right());
		widget->moveToLeft(padding.left(), top + padding.top());
		top = widget->bottomNoMargins() + padding.bottom();
	};

	layoutRow(_everyone, st::editPrivacyOptionMargin);
	layoutRow(_contacts, st::editPrivacyOptionMargin);
	layoutRow(_nobody, st::editPrivacyOptionMargin);
	layoutRow(_description, st::editPrivacyPadding);
	layoutRow(_exceptionsTitle, st::editPrivacyPadding);
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

int EditPrivacyBox::countDefaultHeight(int newWidth) {
	auto height = 0;
	auto optionHeight = [this, newWidth](Option option, const QString &label) {
		auto description = _controller->optionDescription(option);
		if (description.isEmpty()) {
			return 0;
		}

		auto value = static_cast<int>(Option::Everyone);
		auto selected = false;
		auto fake = object_ptr<OptionWidget>(nullptr, value, selected, label, description);
		fake->resizeToNaturalWidth(newWidth - st::editPrivacyOptionMargin.left() - st::editPrivacyOptionMargin.right());
		return st::editPrivacyOptionMargin.top() + fake->heightNoMargins() + st::editPrivacyOptionMargin.bottom();
	};
	auto labelHeight = [this, newWidth](const QString &text, const style::FlatLabel &st) {
		auto fake = object_ptr<Ui::FlatLabel>(nullptr, text, Ui::FlatLabel::InitType::Simple, st);
		fake->resizeToNaturalWidth(newWidth - st::editPrivacyPadding.left() - st::editPrivacyPadding.right());
		return st::editPrivacyPadding.top() + fake->heightNoMargins() + st::editPrivacyPadding.bottom();
	};
	auto linkMargins = exceptionLinkMargins();
	height += optionHeight(Option::Everyone, lang(lng_edit_privacy_everyone));
	height += optionHeight(Option::Contacts, lang(lng_edit_privacy_contacts));
	height += optionHeight(Option::Nobody, lang(lng_edit_privacy_nobody));
	height += labelHeight(_controller->description(), st::editPrivacyLabel);
	height += labelHeight(lang(lng_edit_privacy_exceptions), st::editPrivacyTitle);
	height += linkMargins.top() + st::boxLinkButton.font->height + linkMargins.bottom(); // linkHeight(_controller->alwaysLinkText(0))
	height += linkMargins.top() + st::boxLinkButton.font->height + linkMargins.bottom(); // linkHeight(_controller->neverLinkText(0))
	height += labelHeight(_controller->exceptionsDescription(), st::editPrivacyLabel);
	return height;
}

void EditPrivacyBox::editExceptionUsers(Exception exception) {
	auto controller = std::make_unique<PrivacyExceptionsBoxController>(_controller->exceptionBoxTitle(exception), exceptionUsers(exception), base::lambda_guarded(this, [this, exception](QVector<UserData*> &&users) {
		exceptionUsers(exception) = std::move(users);
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
			if (removeFromUsers.contains(user)) {
				removeFromUsers.erase(std::remove(removeFromUsers.begin(), removeFromUsers.end(), user), removeFromUsers.end());
				removedSome = true;
			}
		}
		if (removedSome) {
			exceptionLink(removeFrom)->entity()->setText(exceptionLinkText(removeFrom));
		}
	}));
	Ui::show(Box<PeerListBox>(std::move(controller)), KeepOtherLayers);
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

	auto result = QVector<MTPInputPrivacyRule>();
	result.reserve(3);
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

QVector<UserData*> &EditPrivacyBox::exceptionUsers(Exception exception) {
	switch (exception) {
	case Exception::Always: return _alwaysUsers;
	case Exception::Never: return _neverUsers;
	}
	Unexpected("Invalid exception value.");
}

object_ptr<Ui::WidgetSlideWrap<Ui::LinkButton>> &EditPrivacyBox::exceptionLink(Exception exception) {
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

void EditPrivacyBox::createOption(Option option, object_ptr<OptionWidget> &widget, const QString &label) {
	auto description = _controller->optionDescription(option);
	auto selected = (_option == option);
	if (!description.isEmpty() || selected) {
		auto value = static_cast<int>(option);
		widget.create(this, value, selected, label, description);
		widget->setChangedCallback([this, option, widget = widget.data()] {
			if (widget->checked()) {
				_option = option;
				_alwaysLink->toggleAnimated(showExceptionLink(Exception::Always));
				_neverLink->toggleAnimated(showExceptionLink(Exception::Never));
			}
		});
	}
}

void EditPrivacyBox::createWidgets() {
	_loading.destroy();

	createOption(Option::Everyone, _everyone, lang(lng_edit_privacy_everyone));
	createOption(Option::Contacts, _contacts, lang(lng_edit_privacy_contacts));
	createOption(Option::Nobody, _nobody, lang(lng_edit_privacy_nobody));
	_description.create(this, _controller->description(), Ui::FlatLabel::InitType::Simple, st::editPrivacyLabel);

	_exceptionsTitle.create(this, lang(lng_edit_privacy_exceptions), Ui::FlatLabel::InitType::Simple, st::editPrivacyTitle);
	auto createExceptionLink = [this](Exception exception) {
		exceptionLink(exception).create(this, object_ptr<Ui::LinkButton>(this, exceptionLinkText(exception)), exceptionLinkMargins(), [this] {
			resizeGetHeight(width());
		});
		exceptionLink(exception)->entity()->setClickedCallback([this, exception] { editExceptionUsers(exception); });
	};
	createExceptionLink(Exception::Always);
	createExceptionLink(Exception::Never);
	_exceptionsDescription.create(this, _controller->exceptionsDescription(), Ui::FlatLabel::InitType::Simple, st::editPrivacyLabel);

	clearButtons();
	addButton(lang(lng_settings_save), [this] { _controller->save(collectResult()); });
	addButton(lang(lng_cancel), [this] { closeBox(); });

	showChildren();
	_alwaysLink->toggleFast(showExceptionLink(Exception::Always));
	_neverLink->toggleFast(showExceptionLink(Exception::Never));

	setDimensions(st::boxWideWidth, resizeGetHeight(st::boxWideWidth));
}

void EditPrivacyBox::loadDone(const MTPaccount_PrivacyRules &result) {
	t_assert(result.type() == mtpc_account_privacyRules);
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
				if (!_neverUsers.contains(user) && !_alwaysUsers.contains(user)) {
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
				if (!_alwaysUsers.contains(user) && !_neverUsers.contains(user)) {
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
}

EditPrivacyBox::~EditPrivacyBox() {
	MTP::cancel(_loadRequestId);
}
