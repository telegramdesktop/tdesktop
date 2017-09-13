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
#pragma once

#include "boxes/abstract_box.h"
#include "mtproto/sender.h"

namespace Ui {
class FlatLabel;
class LinkButton;
template <typename Enum>
class RadioenumGroup;
template <typename Enum>
class Radioenum;
template <typename Widget>
class SlideWrap;
} // namespace Ui

class EditPrivacyBox : public BoxContent, private MTP::Sender {
public:
	enum class Option {
		Everyone,
		Contacts,
		Nobody,
	};
	enum class Exception {
		Always,
		Never,
	};

	class Controller {
	public:
		virtual MTPInputPrivacyKey key() = 0;

		virtual QString title() = 0;
		virtual bool hasOption(Option option) {
			return true;
		}
		virtual QString description() = 0;
		virtual QString warning() {
			return QString();
		}
		virtual QString exceptionLinkText(Exception exception, int count) = 0;
		virtual QString exceptionBoxTitle(Exception exception) = 0;
		virtual QString exceptionsDescription() = 0;

		virtual void confirmSave(bool someAreDisallowed, base::lambda_once<void()> saveCallback) {
			saveCallback();
		}

		virtual ~Controller() = default;

	protected:
		EditPrivacyBox *view() const {
			return _view;
		}

	private:
		void setView(EditPrivacyBox *box) {
			_view = box;
		}

		EditPrivacyBox *_view = nullptr;

		friend class EditPrivacyBox;

	};

	EditPrivacyBox(QWidget*, std::unique_ptr<Controller> controller);

protected:
	void prepare() override;
	int resizeGetHeight(int newWidth) override;

	void resizeEvent(QResizeEvent *e) override;

private:
	style::margins exceptionLinkMargins() const;
	bool showExceptionLink(Exception exception) const;
	void createWidgets();
	QVector<MTPInputPrivacyRule> collectResult();
	void loadData();
	int countDefaultHeight(int newWidth);

	void editExceptionUsers(Exception exception);
	QString exceptionLinkText(Exception exception);
	std::vector<not_null<UserData*>> &exceptionUsers(Exception exception);
	object_ptr<Ui::SlideWrap<Ui::LinkButton>> &exceptionLink(Exception exception);

	std::unique_ptr<Controller> _controller;
	Option _option = Option::Everyone;

	std::shared_ptr<Ui::RadioenumGroup<Option>> _optionGroup;
	object_ptr<Ui::FlatLabel> _loading;
	object_ptr<Ui::FlatLabel> _description = { nullptr };
	object_ptr<Ui::Radioenum<Option>> _everyone = { nullptr };
	object_ptr<Ui::Radioenum<Option>> _contacts = { nullptr };
	object_ptr<Ui::Radioenum<Option>> _nobody = { nullptr };
	object_ptr<Ui::FlatLabel> _warning = { nullptr };
	object_ptr<Ui::FlatLabel> _exceptionsTitle = { nullptr };
	object_ptr<Ui::SlideWrap<Ui::LinkButton>> _alwaysLink = { nullptr };
	object_ptr<Ui::SlideWrap<Ui::LinkButton>> _neverLink = { nullptr };
	object_ptr<Ui::FlatLabel> _exceptionsDescription = { nullptr };

	std::vector<not_null<UserData*>> _alwaysUsers;
	std::vector<not_null<UserData*>> _neverUsers;

};
