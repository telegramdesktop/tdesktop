/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;
class DocumentData;
enum class PremiumFeature;

namespace Api {
struct SendOptions;
} // namespace Api

namespace SendMenu {
struct Details;
} // namespace SendMenu

namespace HistoryView {
class Element;
} // namespace HistoryView

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {

class GenericBox;
class InputField;

void SetupCaptionFieldInBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	not_null<Ui::InputField*> field,
	PeerData *panelPeer,
	Fn<bool(not_null<DocumentData*>)> allowWithoutPremium,
	PremiumFeature premiumFeature);

void EditCaptionBox(
	not_null<Ui::GenericBox*> box,
	not_null<HistoryView::Element*> view);

void SendGifWithCaptionBox(
	not_null<Ui::GenericBox*> box,
	not_null<DocumentData*> document,
	not_null<PeerData*> peer,
	const SendMenu::Details &details,
	Fn<void(Api::SendOptions, TextWithTags)> done);

} // namespace Ui
