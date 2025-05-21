#include "deleted_messages/deleted_messages_controller.h"
#include "window/window_session_controller.h"
#include "main/main_session.h"
#include "storage/deleted_messages_storage.h" // For Storage::DeletedMessagesStorage
#include "data/data_peer.h" // For PeerData
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/layout/verticallayout.h"
#include "styles/style_layers.h"        // For st::boxScroll
#include "styles/style_info.h"          // For st::infoProfileEmptyLabels, st::boxLabel
#include "styles/style_chat_helpers.h"  // For st::contextCopyText (used in FlatLabel)
#include "lang/lang_keys.h"             // For tr::lng_deleted_messages_loading, tr::lng_deleted_messages_empty etc.
#include "base/unixtime.h"              // For base::unixtime::QDateTimeFromTimeId
#include "data/data_text_utils.h"       // For TextWithEntities

#include <QtCore/QDateTime>

namespace DeletedMessages {

Controller::Controller(
    not_null<QWidget*> parent,
    not_null<Window::SessionController*> sessionController)
: Window::SectionWidget(parent, sessionController)
, _sessionController(sessionController)
, _loadTimer([=] { loadMessages(); }) { // Initialize timer with lambda
    setupControls();
}

void Controller::setupControls() {
    auto contentWrapper = object_ptr<Ui::RpWidget>(this);
    const auto contentLayout = contentWrapper->setLayout(object_ptr<Ui::VerticalLayout>(contentWrapper.get()));
    _content = contentLayout;

    _scroll = Ui::CreateChild<Ui::ScrollArea>(this, st::boxScroll);
    _scroll->setWidget(std::move(contentWrapper));

    // Placeholder is initially added to content, its text and visibility will be managed.
    _placeholder = Ui::CreateChild<Ui::FlatLabel>(
        _content,
        tr::lng_deleted_messages_loading(tr::now), // Initial "Loading..." state
        st::infoProfileEmptyLabels);
    _content->add(_placeholder);
}

void Controller::showFinishedHook() {
    SectionWidget::showFinishedHook();
    // Load messages when the section is shown for the first time or if it was previously emptied.
    if (_messages.empty() && (!_loading || _placeholder->text() != tr::lng_deleted_messages_loading(tr::now))) {
         _loadTimer.callOnce(0); // Load immediately
    }
}

void Controller::loadMessages() {
    if (_loading) {
        return;
    }
    _loading = true;
    _placeholder->setText(tr::lng_deleted_messages_loading(tr::now));
    _placeholder->show(); // Ensure placeholder is visible during loading

    // Clear previous messages before loading new ones, but after setting placeholder to "Loading..."
    if (_content->count() > 1) { // More than just the placeholder
        // Remove all widgets except the placeholder
        while (auto widget = _content->widgetAt(0)) {
            if (widget == _placeholder) break; // Should not happen if placeholder is last
             _content->removeWidget(widget);
             delete widget; // Or widget->deleteLater();
        }
         // If placeholder was not the first, and we need to clear all
        if (_content->widgetAt(0) != _placeholder) { // Should be rare
            while(_content->count() > 0) {
                auto w = _content->widgetAt(0);
                _content->removeWidget(w);
                delete w;
            }
            _content->add(_placeholder); // Re-add if it was removed
        }
    }
    _messages.clear();


    auto storage = _sessionController->session().deletedMessagesStorage();
    if (!storage) {
        LOG(("Error: DeletedMessagesStorage not available in Controller."));
        _placeholder->setText(tr::lng_deleted_messages_error(tr::now)); // "Error: Could not load messages."
        _loading = false;
        return;
    }

    // For now, fetch messages for the current user's self-chat.
    // This is a placeholder for a more sophisticated message loading strategy.
    // We could add a UI to select a peer, or a global view (requires getAllMessages in storage).
    auto selfPeerId = _sessionController->session().userPeerId();
    _messages = storage->getMessagesForPeer(selfPeerId, 100); // Get latest 100 messages for self

    // If we wanted a global view and had getAllMessages:
    // _messages = storage->getAllMessages(100);

    _loading = false;
    displayMessages();
}

void Controller::displayMessages() {
    // The placeholder management is now mostly in loadMessages before this call.
    // If _content was cleared except for the placeholder.
    if (_content->widgetAt(0) == _placeholder) {
        _placeholder->setVisible(_messages.empty()); // Show if no messages, hide if there are
    } else { // Should not happen if loadMessages clears correctly
        _content->clear();
        _content->add(_placeholder);
        _placeholder->setVisible(_messages.empty());
    }


    if (_messages.empty()) {
        _placeholder->setText(tr::lng_deleted_messages_empty(tr::now)); // "No deleted messages found."
    } else {
        // Remove placeholder before adding messages if it's still there
        if (_content->widgetAt(0) == _placeholder) {
             _content->removeWidget(_placeholder); // Temporarily remove
        }

        for (const auto &storedMsg : _messages) {
            QString displayText = QString("Msg ID: %1 (Peer: %2)\n")
                                      .arg(storedMsg.originalMessageId)
                                      .arg(storedMsg.peerId.value);
            displayText += QString("Sender: %1, Date: %2, Deleted: %3\n")
                                      .arg(storedMsg.senderId.value)
                                      .arg(base::unixtime::QDateTimeFromTimeId(storedMsg.date).toString(Qt::ISODate))
                                      .arg(base::unixtime::QDateTimeFromTimeId(storedMsg.deletedDate).toString(Qt::ISODate));
            if (!storedMsg.text.text.isEmpty()) {
                 displayText += "Text: " + storedMsg.text.text + "\n";
            }

            if (storedMsg.forwardInfo) {
                displayText += QString("[Forwarded from %1 at %2]\n")
                                     .arg(storedMsg.forwardInfo->originalSenderId.value)
                                     .arg(base::unixtime::QDateTimeFromTimeId(storedMsg.forwardInfo->originalDate).toString(Qt::ISODate));
            }
            if (storedMsg.replyInfo) {
                displayText += QString("[Reply to msg %1 in peer %2]\n")
                                     .arg(storedMsg.replyInfo->replyToMessageId)
                                     .arg(storedMsg.replyInfo->replyToPeerId.value);
            }

            for (const auto& media : storedMsg.mediaList) {
                QString mediaTypeStr;
                switch (media.type) {
                    case Data::StoredMediaType::Photo: mediaTypeStr = "Photo"; break;
                    case Data::StoredMediaType::Video: mediaTypeStr = "Video"; break;
                    case Data::StoredMediaType::AudioFile: mediaTypeStr = "Audio"; break;
                    case Data::StoredMediaType::VoiceMessage: mediaTypeStr = "Voice"; break;
                    case Data::StoredMediaType::Document: mediaTypeStr = "Document"; break;
                    case Data::StoredMediaType::Sticker: mediaTypeStr = "Sticker"; break;
                    case Data::StoredMediaType::AnimatedSticker: mediaTypeStr = "AnimatedSticker"; break;
                    case Data::StoredMediaType::WebPage: mediaTypeStr = "WebPage"; break;
                    // Add other types as needed
                    default: mediaTypeStr = "Media"; break;
                }
                displayText += QString("Media: %1, Path: '%2', Caption: '%3'\n")
                                     .arg(mediaTypeStr)
                                     .arg(media.filePath)
                                     .arg(media.caption.text);
            }
            displayText += "-----\n";

            auto label = Ui::CreateChild<Ui::FlatLabel>(
                nullptr, // Will be added to _content by _content->add()
                displayText,
                st::boxLabel);
            label->setSelectable(true);
            label->setContextCopyText(st::contextCopyText);
            _content->add(label);
        }
        // Add placeholder back at the end if it was removed, to keep it in the layout
        // but it will be hidden if messages were added.
        // This is only needed if we want to keep it in the widgets list.
        // If placeholder is only shown when _content is otherwise empty, we don't need to re-add it here.
    }

    _content->resizeToWidth(_scroll->width() - _scroll->getMargins().left() - _scroll->getMargins().right());
    _scroll->scrollToY(0);
}


void Controller::resizeEvent(QResizeEvent *e) {
    SectionWidget::resizeEvent(e);
    if (_scroll) {
        _scroll->setGeometry(rect());
        // Update content width if scrollbar visibility might have changed
        // or if the scroll area's viewport width changed.
        // The margins are important if the ScrollArea itself has padding/margins.
        if (_content) {
             _content->resizeToWidth(_scroll->width() - _scroll->getMargins().left() - _scroll->getMargins().right());
        }
    }
}

} // namespace DeletedMessages
