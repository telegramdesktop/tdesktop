/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/edit_privacy_box.h"

#include "styles/style_boxes.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
#include "history/history.h"
#include "boxes/peer_list_controllers.h"
#include "calls/calls_instance.h"
#include "base/binary_guard.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "auth_session.h"

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
	rpl::producer<Value> preloaded)
: _controller(std::move(controller))
, _loading(this, lang(lng_contacts_loading), Ui::FlatLabel::InitType::Simple, st::membersAbout) {
	std::move(
		preloaded
	) | rpl::take(
		1
	) | rpl::start_with_next([=](Value &&data) {
		dataReady(std::move(data));
	}, lifetime());
}

void EditPrivacyBox::prepare() {
	_controller->setView(this);

	setTitle([this] { return _controller->title(); });
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	if (_loading) {
		_prepared = true;
	} else {
		createWidgets();
	}

	setDimensions(st::boxWideWidth, countDefaultHeight(st::boxWideWidth));
}

int EditPrivacyBox::resizeGetHeight(int newWidth) {
	auto top = 0;
	auto layoutRow = [&](auto &widget, style::margins padding) {
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
	auto labelHeight = [newWidth](const QString &text, const style::FlatLabel &st, style::margins padding) {
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
	auto controller = std::make_unique<PrivacyExceptionsBoxController>(crl::guard(this, [this, exception] {
		return _controller->exceptionBoxTitle(exception);
	}), exceptionUsers(exception));
	auto initBox = [this, exception, controller = controller.get()](not_null<PeerListBox*> box) {
		box->addButton(langFactory(lng_settings_save), crl::guard(this, [this, box, exception, controller] {
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
	if (showExceptionLink(Exception::Always) && !_value.always.empty()) {
		result.push_back(MTP_inputPrivacyValueAllowUsers(MTP_vector<MTPInputUser>(collectInputUsers(_value.always))));
	}
	if (showExceptionLink(Exception::Never) && !_value.never.empty()) {
		result.push_back(MTP_inputPrivacyValueDisallowUsers(MTP_vector<MTPInputUser>(collectInputUsers(_value.never))));
	}
	switch (_value.option) {
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
	case Exception::Always: return _value.always;
	case Exception::Never: return _value.never;
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
	case Exception::Always: return (_value.option == Option::Contacts) || (_value.option == Option::Nobody);
	case Exception::Never: return (_value.option == Option::Everyone) || (_value.option == Option::Contacts);
	}
	Unexpected("Invalid exception value.");
}

void EditPrivacyBox::createWidgets() {
	_loading.destroy();
	_optionGroup = std::make_shared<Ui::RadioenumGroup<Option>>(_value.option);

	auto createOption = [this](object_ptr<Ui::Radioenum<Option>> &widget, Option option, const QString &label) {
		if (_controller->hasOption(option) || (_value.option == option)) {
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
		exceptionLink(exception)->heightValue(
		) | rpl::start_with_next([this] {
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
		auto someAreDisallowed = (_value.option != Option::Everyone) || !_value.never.empty();
		_controller->confirmSave(someAreDisallowed, crl::guard(this, [this] {
			Auth().api().savePrivacy(
				_controller->apiKey(),
				collectResult());
			closeBox();
		}));
	});
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	_optionGroup->setChangedCallback([this](Option value) {
		_value.option = value;
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

void EditPrivacyBox::dataReady(Value &&value) {
	_value = std::move(value);
	_loading.destroy();
	if (_prepared) {
		createWidgets();
	}
}

void EditCallsPeerToPeer::prepare() {
	setTitle(langFactory(lng_settings_peer_to_peer));

	addButton(langFactory(lng_box_ok), [=] { closeBox(); });

	const auto options = {
		PeerToPeer::Everyone,
		PeerToPeer::Contacts,
		PeerToPeer::Nobody
	};

	const auto value = Auth().settings().callsPeerToPeer();
	const auto adjusted = [&] {
		if (value == PeerToPeer::DefaultContacts) {
			return PeerToPeer::Contacts;
		} else if (value == PeerToPeer::DefaultEveryone) {
			return PeerToPeer::Everyone;
		}
		return value;
	}();
	const auto label = [](PeerToPeer value) {
		switch (value) {
		case PeerToPeer::Everyone: return lang(lng_edit_privacy_everyone);
		case PeerToPeer::Contacts: return lang(lng_edit_privacy_contacts);
		case PeerToPeer::Nobody: return lang(lng_edit_privacy_nobody);
		}
		Unexpected("Adjusted Calls::PeerToPeer value.");
	};
	auto group = std::make_shared<Ui::RadioenumGroup<PeerToPeer>>(adjusted);
	auto y = st::boxOptionListPadding.top() + st::langsButton.margin.top();
	auto count = int(options.size());
	_options.reserve(count);
	for (const auto option : options) {
		_options.emplace_back(
			this,
			group,
			option,
			label(option),
			st::langsButton);
		_options.back()->moveToLeft(
			st::boxPadding.left() + st::boxOptionListPadding.left(),
			y);
		y += _options.back()->heightNoMargins() + st::boxOptionListSkip;
	}
	group->setChangedCallback([=](PeerToPeer value) { chosen(value); });

	setDimensions(
		st::langsWidth,
		(st::boxOptionListPadding.top()
			+ count * _options.back()->heightNoMargins()
			+ (count - 1) * st::boxOptionListSkip
			+ st::boxOptionListPadding.bottom()
			+ st::boxPadding.bottom()));
}

void EditCallsPeerToPeer::chosen(PeerToPeer value) {
	Auth().settings().setCallsPeerToPeer(value);
	Auth().saveSettingsDelayed();
	closeBox();
}
