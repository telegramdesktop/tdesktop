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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "settings/settings_block_widget.h"

class FlatLabel;

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

	bool usernameClickHandlerHook(const ClickHandlerPtr &handler, Qt::MouseButton button);

	void createControls();
	void refreshControls();
	void refreshMobileNumber();
	void refreshUsername();
	void refreshLink();

	class LabeledWidget : public TWidget {
	public:
		LabeledWidget(QWidget *parent);

		void setLabeledText(const QString &label, const TextWithEntities &textWithEntities, const TextWithEntities &shortTextWithEntities, const QString &copyText);

		FlatLabel *textLabel() {
			return _text;
		}
		FlatLabel *shortTextLabel() {
			return _shortText;
		}

		int naturalWidth() const override;

	protected:
		int resizeGetHeight(int newWidth) override;

	private:
		void setLabelText(ChildWidget<FlatLabel> &text, const TextWithEntities &textWithEntities, const QString &copyText);

		ChildWidget<FlatLabel> _label = { nullptr };
		ChildWidget<FlatLabel> _text = { nullptr };
		ChildWidget<FlatLabel> _shortText = { nullptr };

	};

	using LabeledWrap = Ui::WidgetSlideWrap<LabeledWidget>;
	void setLabeledText(ChildWidget<LabeledWrap> &row, const QString &label, const TextWithEntities &textWithEntities, const TextWithEntities &shortTextWithEntities, const QString &copyText);

	ChildWidget<LabeledWrap> _mobileNumber = { nullptr };
	ChildWidget<LabeledWrap> _username = { nullptr };
	ChildWidget<LabeledWrap> _link = { nullptr };

};

} // namespace Settings
