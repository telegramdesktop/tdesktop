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

#include "settings/settings_block_widget.h"

namespace Ui {
class FlatLabel;
} // namespace Ui

namespace Notify {
struct PeerUpdate;
} // namespace Notify

namespace Settings {

class InfoWidget : public BlockWidget {
public:
	InfoWidget(QWidget *parent, UserData *self);

private:
	// Observed notifications.
	void notifyPeerUpdated(const Notify::PeerUpdate &update);

	void createControls();
	void refreshControls();
	void refreshMobileNumber();
	void refreshUsername();
	void refreshLink();
	void refreshBio();

	class LabeledWidget : public TWidget {
	public:
		LabeledWidget(QWidget *parent, const style::FlatLabel &valueSt);

		void setLabeledText(const QString &label, const TextWithEntities &textWithEntities, const TextWithEntities &shortTextWithEntities, const QString &copyText);

		Ui::FlatLabel *textLabel() const;
		Ui::FlatLabel *shortTextLabel() const;

		int naturalWidth() const override;

	protected:
		int resizeGetHeight(int newWidth) override;

	private:
		void setLabelText(object_ptr<Ui::FlatLabel> &text, const TextWithEntities &textWithEntities, const QString &copyText);

		const style::FlatLabel &_valueSt;
		object_ptr<Ui::FlatLabel> _label = { nullptr };
		object_ptr<Ui::FlatLabel> _text = { nullptr };
		object_ptr<Ui::FlatLabel> _shortText = { nullptr };

	};

	using LabeledWrap = Ui::WidgetSlideWrap<LabeledWidget>;
	void setLabeledText(object_ptr<LabeledWrap> &row, const QString &label, const TextWithEntities &textWithEntities, const TextWithEntities &shortTextWithEntities, const QString &copyText);

	object_ptr<LabeledWrap> _mobileNumber = { nullptr };
	object_ptr<LabeledWrap> _username = { nullptr };
	object_ptr<LabeledWrap> _link = { nullptr };
	object_ptr<LabeledWrap> _bio = { nullptr };

};

} // namespace Settings
