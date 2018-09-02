/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "old_settings/settings_block_widget.h"
#include "ui/rp_widget.h"

namespace Ui {
class FlatLabel;
} // namespace Ui

namespace Notify {
struct PeerUpdate;
} // namespace Notify

namespace OldSettings {

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
	void refreshBio();

	class LabeledWidget : public Ui::RpWidget {
	public:
		LabeledWidget(QWidget *parent, const style::FlatLabel &valueSt);

		void setLabeledText(
			const QString &label,
			const TextWithEntities &textWithEntities,
			const TextWithEntities &shortTextWithEntities,
			const QString &copyText,
			int availableWidth);

		Ui::FlatLabel *textLabel() const;
		Ui::FlatLabel *shortTextLabel() const;

		int naturalWidth() const override;

	protected:
		int resizeGetHeight(int newWidth) override;

	private:
		void setLabelText(
			object_ptr<Ui::FlatLabel> &text,
			const TextWithEntities &textWithEntities,
			const QString &copyText);

		const style::FlatLabel &_valueSt;
		object_ptr<Ui::FlatLabel> _label = { nullptr };
		object_ptr<Ui::FlatLabel> _text = { nullptr };
		object_ptr<Ui::FlatLabel> _shortText = { nullptr };

	};

	using LabeledWrap = Ui::SlideWrap<LabeledWidget>;
	void setLabeledText(
		LabeledWrap *row,
		const QString &label,
		const TextWithEntities &textWithEntities,
		const TextWithEntities &shortTextWithEntities,
		const QString &copyText);

	LabeledWrap *_mobileNumber = nullptr;
	LabeledWrap *_username = nullptr;
	LabeledWrap *_bio = nullptr;

};

} // namespace Settings
