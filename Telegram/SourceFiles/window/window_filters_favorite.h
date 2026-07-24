/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "mtproto/sender.h"
#include "ui/text/text.h"
#include "ui/widgets/buttons.h"

class PeerData;

namespace style {
struct SideBarButton;
} // namespace style

namespace Ui {
class DynamicImage;
class GenericBox;
class PopupMenu;
} // namespace Ui

namespace Window {

class SessionController;

extern const char kOptionFolderFavoriteLink[];

[[nodiscard]] bool ValidFolderFavoriteLink(const QString &link);

void EditFolderFavoriteLinkBox(not_null<Ui::GenericBox*> box);

class FolderFavoriteButton final : public Ui::RippleButton {
public:
	FolderFavoriteButton(
		QWidget *parent,
		not_null<SessionController*> controller,
		const style::SideBarButton &st);
	~FolderFavoriteButton();

	[[nodiscard]] rpl::producer<bool> shownValue() const;
	[[nodiscard]] bool shown() const;

	void setLink(const QString &link);

	int resizeGetHeight(int newWidth) override;

private:
	struct Presentation {
		std::shared_ptr<Ui::DynamicImage> thumbnail;
		QString label;
	};

	void paintEvent(QPaintEvent *e) override;

	void resolvePeer(const QString &usernameOrPhone, bool phone);
	void resolveInvite(const QString &hash);
	void resolveStickerSet(const QString &shortName);
	void applyPeer(not_null<PeerData*> peer);
	void apply(Presentation presentation);
	void showGeneric();

	void openLink();
	void showMenu();
	void editLink();

	const not_null<SessionController*> _controller;
	const style::SideBarButton &_st;
	MTP::Sender _api;

	QString _link;
	std::shared_ptr<Ui::DynamicImage> _thumbnail;
	Ui::Text::String _label;
	rpl::variable<bool> _shown = false;

	mtpRequestId _requestId = 0;
	base::unique_qptr<Ui::PopupMenu> _menu;
	rpl::lifetime _resolveLifetime;

};

} // namespace Window
