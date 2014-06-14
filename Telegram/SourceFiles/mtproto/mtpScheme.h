/*
Created from '/SourceFiles/mtproto/scheme.tl' by '/SourceFiles/mtproto/generate.py' script

WARNING! All changes made in this file will be lost!

This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#pragma once

#include "mtpCoreTypes.h"

// Type id constants
enum {
	mtpc_resPQ = 0x05162463,
	mtpc_p_q_inner_data = 0x83c95aec,
	mtpc_server_DH_params_fail = 0x79cb045d,
	mtpc_server_DH_params_ok = 0xd0e8075c,
	mtpc_server_DH_inner_data = 0xb5890dba,
	mtpc_client_DH_inner_data = 0x6643b654,
	mtpc_dh_gen_ok = 0x3bcbf734,
	mtpc_dh_gen_retry = 0x46dc1fb9,
	mtpc_dh_gen_fail = 0xa69dae02,
	mtpc_req_pq = 0x60469778,
	mtpc_req_DH_params = 0xd712e4be,
	mtpc_set_client_DH_params = 0xf5045f1f,
	mtpc_msgs_ack = 0x62d6b459,
	mtpc_bad_msg_notification = 0xa7eff811,
	mtpc_bad_server_salt = 0xedab447b,
	mtpc_msgs_state_req = 0xda69fb52,
	mtpc_msgs_state_info = 0x04deb57d,
	mtpc_msgs_all_info = 0x8cc0d131,
	mtpc_msg_detailed_info = 0x276d3ec6,
	mtpc_msg_new_detailed_info = 0x809db6df,
	mtpc_msg_resend_req = 0x7d861a08,
	mtpc_rpc_error = 0x2144ca19,
	mtpc_rpc_answer_unknown = 0x5e2ad36e,
	mtpc_rpc_answer_dropped_running = 0xcd78e586,
	mtpc_rpc_answer_dropped = 0xa43ad8b7,
	mtpc_future_salt = 0x0949d9dc,
	mtpc_future_salts = 0xae500895,
	mtpc_pong = 0x347773c5,
	mtpc_destroy_session_ok = 0xe22045fc,
	mtpc_destroy_session_none = 0x62d350c9,
	mtpc_new_session_created = 0x9ec20908,
	mtpc_http_wait = 0x9299359f,
	mtpc_rpc_drop_answer = 0x58e4a740,
	mtpc_get_future_salts = 0xb921bd04,
	mtpc_ping = 0x7abe77ec,
	mtpc_ping_delay_disconnect = 0xf3427b8c,
	mtpc_destroy_session = 0xe7512126,
	mtpc_register_saveDeveloperInfo = 0x9a5f6e95,
	mtpc_inputPeerEmpty = 0x7f3b18ea,
	mtpc_inputPeerSelf = 0x7da07ec9,
	mtpc_inputPeerContact = 0x1023dbe8,
	mtpc_inputPeerForeign = 0x9b447325,
	mtpc_inputPeerChat = 0x179be863,
	mtpc_inputUserEmpty = 0xb98886cf,
	mtpc_inputUserSelf = 0xf7c1b13f,
	mtpc_inputUserContact = 0x86e94f65,
	mtpc_inputUserForeign = 0x655e74ff,
	mtpc_inputPhoneContact = 0xf392b7f4,
	mtpc_inputFile = 0xf52ff27f,
	mtpc_inputMediaEmpty = 0x9664f57f,
	mtpc_inputMediaUploadedPhoto = 0x2dc53a7d,
	mtpc_inputMediaPhoto = 0x8f2ab2ec,
	mtpc_inputMediaGeoPoint = 0xf9c44144,
	mtpc_inputMediaContact = 0xa6e45987,
	mtpc_inputMediaUploadedVideo = 0x133ad6f6,
	mtpc_inputMediaUploadedThumbVideo = 0x9912dabf,
	mtpc_inputMediaVideo = 0x7f023ae6,
	mtpc_inputChatPhotoEmpty = 0x1ca48f57,
	mtpc_inputChatUploadedPhoto = 0x94254732,
	mtpc_inputChatPhoto = 0xb2e1bf08,
	mtpc_inputGeoPointEmpty = 0xe4c123d6,
	mtpc_inputGeoPoint = 0xf3b7acc9,
	mtpc_inputPhotoEmpty = 0x1cd7bf0d,
	mtpc_inputPhoto = 0xfb95c6c4,
	mtpc_inputVideoEmpty = 0x5508ec75,
	mtpc_inputVideo = 0xee579652,
	mtpc_inputFileLocation = 0x14637196,
	mtpc_inputVideoFileLocation = 0x3d0364ec,
	mtpc_inputPhotoCropAuto = 0xade6b004,
	mtpc_inputPhotoCrop = 0xd9915325,
	mtpc_inputAppEvent = 0x770656a8,
	mtpc_peerUser = 0x9db1bc6d,
	mtpc_peerChat = 0xbad0e5bb,
	mtpc_storage_fileUnknown = 0xaa963b05,
	mtpc_storage_fileJpeg = 0x7efe0e,
	mtpc_storage_fileGif = 0xcae1aadf,
	mtpc_storage_filePng = 0xa4f63c0,
	mtpc_storage_filePdf = 0xae1e508d,
	mtpc_storage_fileMp3 = 0x528a0677,
	mtpc_storage_fileMov = 0x4b09ebbc,
	mtpc_storage_filePartial = 0x40bc6f52,
	mtpc_storage_fileMp4 = 0xb3cea0e4,
	mtpc_storage_fileWebp = 0x1081464c,
	mtpc_fileLocationUnavailable = 0x7c596b46,
	mtpc_fileLocation = 0x53d69076,
	mtpc_userEmpty = 0x200250ba,
	mtpc_userSelf = 0x720535ec,
	mtpc_userContact = 0xf2fb8319,
	mtpc_userRequest = 0x22e8ceb0,
	mtpc_userForeign = 0x5214c89d,
	mtpc_userDeleted = 0xb29ad7cc,
	mtpc_userProfilePhotoEmpty = 0x4f11bae1,
	mtpc_userProfilePhoto = 0xd559d8c8,
	mtpc_userStatusEmpty = 0x9d05049,
	mtpc_userStatusOnline = 0xedb93949,
	mtpc_userStatusOffline = 0x8c703f,
	mtpc_chatEmpty = 0x9ba2d800,
	mtpc_chat = 0x6e9c9bc7,
	mtpc_chatForbidden = 0xfb0ccc41,
	mtpc_chatFull = 0x630e61be,
	mtpc_chatParticipant = 0xc8d7493e,
	mtpc_chatParticipantsForbidden = 0xfd2bb8a,
	mtpc_chatParticipants = 0x7841b415,
	mtpc_chatPhotoEmpty = 0x37c1011c,
	mtpc_chatPhoto = 0x6153276a,
	mtpc_messageEmpty = 0x83e5de54,
	mtpc_message = 0x22eb6aba,
	mtpc_messageForwarded = 0x5f46804,
	mtpc_messageService = 0x9f8d60bb,
	mtpc_messageMediaEmpty = 0x3ded6320,
	mtpc_messageMediaPhoto = 0xc8c45a2a,
	mtpc_messageMediaVideo = 0xa2d24290,
	mtpc_messageMediaGeo = 0x56e0d474,
	mtpc_messageMediaContact = 0x5e7d2f39,
	mtpc_messageMediaUnsupported = 0x29632a36,
	mtpc_messageActionEmpty = 0xb6aef7b0,
	mtpc_messageActionChatCreate = 0xa6638b9a,
	mtpc_messageActionChatEditTitle = 0xb5a1ce5a,
	mtpc_messageActionChatEditPhoto = 0x7fcb13a8,
	mtpc_messageActionChatDeletePhoto = 0x95e3fbef,
	mtpc_messageActionChatAddUser = 0x5e3cfc4b,
	mtpc_messageActionChatDeleteUser = 0xb2ae9b0c,
	mtpc_dialog = 0xab3a99ac,
	mtpc_photoEmpty = 0x2331b22d,
	mtpc_photo = 0x22b56751,
	mtpc_photoSizeEmpty = 0xe17e23c,
	mtpc_photoSize = 0x77bfb61b,
	mtpc_photoCachedSize = 0xe9a734fa,
	mtpc_videoEmpty = 0xc10658a8,
	mtpc_video = 0x388fa391,
	mtpc_geoPointEmpty = 0x1117dd5f,
	mtpc_geoPoint = 0x2049d70c,
	mtpc_auth_checkedPhone = 0xe300cc3b,
	mtpc_auth_sentCode = 0xefed51d9,
	mtpc_auth_authorization = 0xf6b673a4,
	mtpc_auth_exportedAuthorization = 0xdf969c2d,
	mtpc_inputNotifyPeer = 0xb8bc5b0c,
	mtpc_inputNotifyUsers = 0x193b4417,
	mtpc_inputNotifyChats = 0x4a95e84e,
	mtpc_inputNotifyAll = 0xa429b886,
	mtpc_inputPeerNotifyEventsEmpty = 0xf03064d8,
	mtpc_inputPeerNotifyEventsAll = 0xe86a2c74,
	mtpc_inputPeerNotifySettings = 0x46a2ce98,
	mtpc_peerNotifyEventsEmpty = 0xadd53cb3,
	mtpc_peerNotifyEventsAll = 0x6d1ded88,
	mtpc_peerNotifySettingsEmpty = 0x70a68512,
	mtpc_peerNotifySettings = 0x8d5e11ee,
	mtpc_wallPaper = 0xccb03657,
	mtpc_userFull = 0x771095da,
	mtpc_contact = 0xf911c994,
	mtpc_importedContact = 0xd0028438,
	mtpc_contactBlocked = 0x561bc879,
	mtpc_contactFound = 0xea879f95,
	mtpc_contactSuggested = 0x3de191a1,
	mtpc_contactStatus = 0xaa77b873,
	mtpc_chatLocated = 0x3631cf4c,
	mtpc_contacts_foreignLinkUnknown = 0x133421f8,
	mtpc_contacts_foreignLinkRequested = 0xa7801f47,
	mtpc_contacts_foreignLinkMutual = 0x1bea8ce1,
	mtpc_contacts_myLinkEmpty = 0xd22a1c60,
	mtpc_contacts_myLinkRequested = 0x6c69efee,
	mtpc_contacts_myLinkContact = 0xc240ebd9,
	mtpc_contacts_link = 0xeccea3f5,
	mtpc_contacts_contacts = 0x6f8b8cb2,
	mtpc_contacts_contactsNotModified = 0xb74ba9d2,
	mtpc_contacts_importedContacts = 0xad524315,
	mtpc_contacts_blocked = 0x1c138d15,
	mtpc_contacts_blockedSlice = 0x900802a1,
	mtpc_contacts_found = 0x566000e,
	mtpc_contacts_suggested = 0x5649dcc5,
	mtpc_messages_dialogs = 0x15ba6c40,
	mtpc_messages_dialogsSlice = 0x71e094f3,
	mtpc_messages_messages = 0x8c718e87,
	mtpc_messages_messagesSlice = 0xb446ae3,
	mtpc_messages_messageEmpty = 0x3f4e0648,
	mtpc_messages_message = 0xff90c417,
	mtpc_messages_statedMessages = 0x969478bb,
	mtpc_messages_statedMessage = 0xd07ae726,
	mtpc_messages_sentMessage = 0xd1f4d35c,
	mtpc_messages_chat = 0x40e9002a,
	mtpc_messages_chats = 0x8150cbd8,
	mtpc_messages_chatFull = 0xe5d7d19c,
	mtpc_messages_affectedHistory = 0xb7de36f2,
	mtpc_inputMessagesFilterEmpty = 0x57e2f66c,
	mtpc_inputMessagesFilterPhotos = 0x9609a51c,
	mtpc_inputMessagesFilterVideo = 0x9fc00e65,
	mtpc_inputMessagesFilterPhotoVideo = 0x56e9f0e4,
	mtpc_inputMessagesFilterDocument = 0x9eddf188,
	mtpc_updateNewMessage = 0x13abdb3,
	mtpc_updateMessageID = 0x4e90bfd6,
	mtpc_updateReadMessages = 0xc6649e31,
	mtpc_updateDeleteMessages = 0xa92bfe26,
	mtpc_updateRestoreMessages = 0xd15de04d,
	mtpc_updateUserTyping = 0x6baa8508,
	mtpc_updateChatUserTyping = 0x3c46cfe6,
	mtpc_updateChatParticipants = 0x7761198,
	mtpc_updateUserStatus = 0x1bfbd823,
	mtpc_updateUserName = 0xda22d9ad,
	mtpc_updateUserPhoto = 0x95313b0c,
	mtpc_updateContactRegistered = 0x2575bbb9,
	mtpc_updateContactLink = 0x51a48a9a,
	mtpc_updateActivation = 0x6f690963,
	mtpc_updateNewAuthorization = 0x8f06529a,
	mtpc_updates_state = 0xa56c2a3e,
	mtpc_updates_differenceEmpty = 0x5d75a138,
	mtpc_updates_difference = 0xf49ca0,
	mtpc_updates_differenceSlice = 0xa8fb1981,
	mtpc_updatesTooLong = 0xe317af7e,
	mtpc_updateShortMessage = 0xd3f45784,
	mtpc_updateShortChatMessage = 0x2b2fbd4e,
	mtpc_updateShort = 0x78d4dec1,
	mtpc_updatesCombined = 0x725b04c3,
	mtpc_updates = 0x74ae4240,
	mtpc_photos_photos = 0x8dca6aa5,
	mtpc_photos_photosSlice = 0x15051f54,
	mtpc_photos_photo = 0x20212ca8,
	mtpc_upload_file = 0x96a18d5,
	mtpc_dcOption = 0x2ec2a43c,
	mtpc_config = 0x2e54dd74,
	mtpc_nearestDc = 0x8e1a1775,
	mtpc_help_appUpdate = 0x8987f311,
	mtpc_help_noAppUpdate = 0xc45a6536,
	mtpc_help_inviteText = 0x18cb9f78,
	mtpc_messages_statedMessagesLinks = 0x3e74f5c6,
	mtpc_messages_statedMessageLink = 0xa9af2881,
	mtpc_messages_sentMessageLink = 0xe9db4a3f,
	mtpc_inputGeoChat = 0x74d456fa,
	mtpc_inputNotifyGeoChatPeer = 0x4d8ddec8,
	mtpc_geoChat = 0x75eaea5a,
	mtpc_geoChatMessageEmpty = 0x60311a9b,
	mtpc_geoChatMessage = 0x4505f8e1,
	mtpc_geoChatMessageService = 0xd34fa24e,
	mtpc_geochats_statedMessage = 0x17b1578b,
	mtpc_geochats_located = 0x48feb267,
	mtpc_geochats_messages = 0xd1526db1,
	mtpc_geochats_messagesSlice = 0xbc5863e8,
	mtpc_messageActionGeoChatCreate = 0x6f038ebc,
	mtpc_messageActionGeoChatCheckin = 0xc7d53de,
	mtpc_updateNewGeoChatMessage = 0x5a68e3f7,
	mtpc_wallPaperSolid = 0x63117f24,
	mtpc_updateNewEncryptedMessage = 0x12bcbd9a,
	mtpc_updateEncryptedChatTyping = 0x1710f156,
	mtpc_updateEncryption = 0xb4a2e88d,
	mtpc_updateEncryptedMessagesRead = 0x38fe25b7,
	mtpc_encryptedChatEmpty = 0xab7ec0a0,
	mtpc_encryptedChatWaiting = 0x3bf703dc,
	mtpc_encryptedChatRequested = 0xc878527e,
	mtpc_encryptedChat = 0xfa56ce36,
	mtpc_encryptedChatDiscarded = 0x13d6dd27,
	mtpc_inputEncryptedChat = 0xf141b5e1,
	mtpc_encryptedFileEmpty = 0xc21f497e,
	mtpc_encryptedFile = 0x4a70994c,
	mtpc_inputEncryptedFileEmpty = 0x1837c364,
	mtpc_inputEncryptedFileUploaded = 0x64bd0306,
	mtpc_inputEncryptedFile = 0x5a17b5e5,
	mtpc_inputEncryptedFileLocation = 0xf5235d55,
	mtpc_encryptedMessage = 0xed18c118,
	mtpc_encryptedMessageService = 0x23734b06,
	mtpc_decryptedMessageLayer = 0x99a438cf,
	mtpc_decryptedMessage = 0x1f814f1f,
	mtpc_decryptedMessageService = 0xaa48327d,
	mtpc_decryptedMessageMediaEmpty = 0x89f5c4a,
	mtpc_decryptedMessageMediaPhoto = 0x32798a8c,
	mtpc_decryptedMessageMediaVideo = 0x524a415d,
	mtpc_decryptedMessageMediaGeoPoint = 0x35480a59,
	mtpc_decryptedMessageMediaContact = 0x588a0a97,
	mtpc_decryptedMessageActionSetMessageTTL = 0xa1733aec,
	mtpc_messages_dhConfigNotModified = 0xc0e24635,
	mtpc_messages_dhConfig = 0x2c221edd,
	mtpc_messages_sentEncryptedMessage = 0x560f8935,
	mtpc_messages_sentEncryptedFile = 0x9493ff32,
	mtpc_inputFileBig = 0xfa4f0bb5,
	mtpc_inputEncryptedFileBigUploaded = 0x2dc173c8,
	mtpc_updateChatParticipantAdd = 0x3a0eeb22,
	mtpc_updateChatParticipantDelete = 0x6e5f8c22,
	mtpc_updateDcOptions = 0x8e5e9873,
	mtpc_inputMediaUploadedAudio = 0x4e498cab,
	mtpc_inputMediaAudio = 0x89938781,
	mtpc_inputMediaUploadedDocument = 0x34e794bd,
	mtpc_inputMediaUploadedThumbDocument = 0x3e46de5d,
	mtpc_inputMediaDocument = 0xd184e841,
	mtpc_messageMediaDocument = 0x2fda2204,
	mtpc_messageMediaAudio = 0xc6b68300,
	mtpc_inputAudioEmpty = 0xd95adc84,
	mtpc_inputAudio = 0x77d440ff,
	mtpc_inputDocumentEmpty = 0x72f0eaae,
	mtpc_inputDocument = 0x18798952,
	mtpc_inputAudioFileLocation = 0x74dc404d,
	mtpc_inputDocumentFileLocation = 0x4e45abe9,
	mtpc_decryptedMessageMediaDocument = 0xb095434b,
	mtpc_decryptedMessageMediaAudio = 0x57e0a9cb,
	mtpc_audioEmpty = 0x586988d8,
	mtpc_audio = 0xc7ac6496,
	mtpc_documentEmpty = 0x36f8c871,
	mtpc_document = 0x9efc6326,
	mtpc_help_support = 0x17c6b5f6,
	mtpc_decryptedMessageActionReadMessages = 0xc4f40be,
	mtpc_decryptedMessageActionDeleteMessages = 0x65614304,
	mtpc_decryptedMessageActionScreenshotMessages = 0x8ac1f475,
	mtpc_decryptedMessageActionFlushHistory = 0x6719e45c,
	mtpc_decryptedMessageActionNotifyLayer = 0xf3048883,
	mtpc_notifyPeer = 0x9fd40bd8,
	mtpc_notifyUsers = 0xb4c83b4c,
	mtpc_notifyChats = 0xc007cec3,
	mtpc_notifyAll = 0x74d07c60,
	mtpc_updateUserBlocked = 0x80ece81a,
	mtpc_updateNotifySettings = 0xbec268ef,
	mtpc_invokeAfterMsg = 0xcb9f372d,
	mtpc_invokeAfterMsgs = 0x3dc4b4f0,
	mtpc_auth_checkPhone = 0x6fe51dfb,
	mtpc_auth_sendCode = 0x768d5f4d,
	mtpc_auth_sendCall = 0x3c51564,
	mtpc_auth_signUp = 0x1b067634,
	mtpc_auth_signIn = 0xbcd51581,
	mtpc_auth_logOut = 0x5717da40,
	mtpc_auth_resetAuthorizations = 0x9fab0d1a,
	mtpc_auth_sendInvites = 0x771c1d97,
	mtpc_auth_exportAuthorization = 0xe5bfffcd,
	mtpc_auth_importAuthorization = 0xe3ef9613,
	mtpc_account_registerDevice = 0x446c712c,
	mtpc_account_unregisterDevice = 0x65c55b40,
	mtpc_account_updateNotifySettings = 0x84be5b93,
	mtpc_account_getNotifySettings = 0x12b3ad31,
	mtpc_account_resetNotifySettings = 0xdb7e1747,
	mtpc_account_updateProfile = 0xf0888d68,
	mtpc_account_updateStatus = 0x6628562c,
	mtpc_account_getWallPapers = 0xc04cfac2,
	mtpc_users_getUsers = 0xd91a548,
	mtpc_users_getFullUser = 0xca30a5b1,
	mtpc_contacts_getStatuses = 0xc4a353ee,
	mtpc_contacts_getContacts = 0x22c6aa08,
	mtpc_contacts_importContacts = 0xda30b32d,
	mtpc_contacts_search = 0x11f812d8,
	mtpc_contacts_getSuggested = 0xcd773428,
	mtpc_contacts_deleteContact = 0x8e953744,
	mtpc_contacts_deleteContacts = 0x59ab389e,
	mtpc_contacts_block = 0x332b49fc,
	mtpc_contacts_unblock = 0xe54100bd,
	mtpc_contacts_getBlocked = 0xf57c350f,
	mtpc_messages_getMessages = 0x4222fa74,
	mtpc_messages_getDialogs = 0xeccf1df6,
	mtpc_messages_getHistory = 0x92a1df2f,
	mtpc_messages_search = 0x7e9f2ab,
	mtpc_messages_readHistory = 0xb04f2510,
	mtpc_messages_deleteHistory = 0xf4f8fb61,
	mtpc_messages_deleteMessages = 0x14f2dd0a,
	mtpc_messages_restoreMessages = 0x395f9d7e,
	mtpc_messages_receivedMessages = 0x28abcb68,
	mtpc_messages_setTyping = 0x719839e9,
	mtpc_messages_sendMessage = 0x4cde0aab,
	mtpc_messages_sendMedia = 0xa3c85d76,
	mtpc_messages_forwardMessages = 0x514cd10f,
	mtpc_messages_getChats = 0x3c6aa187,
	mtpc_messages_getFullChat = 0x3b831c66,
	mtpc_messages_editChatTitle = 0xb4bc68b5,
	mtpc_messages_editChatPhoto = 0xd881821d,
	mtpc_messages_addChatUser = 0x2ee9ee9e,
	mtpc_messages_deleteChatUser = 0xc3c5cd23,
	mtpc_messages_createChat = 0x419d9aee,
	mtpc_updates_getState = 0xedd4882a,
	mtpc_updates_getDifference = 0xa041495,
	mtpc_photos_updateProfilePhoto = 0xeef579a0,
	mtpc_photos_uploadProfilePhoto = 0xd50f9c88,
	mtpc_upload_saveFilePart = 0xb304a621,
	mtpc_upload_getFile = 0xe3a6cfb5,
	mtpc_help_getConfig = 0xc4f9186b,
	mtpc_help_getNearestDc = 0x1fb33026,
	mtpc_help_getAppUpdate = 0xc812ac7e,
	mtpc_help_saveAppLog = 0x6f02f748,
	mtpc_help_getInviteText = 0xa4a95186,
	mtpc_photos_getUserPhotos = 0xb7ee553c,
	mtpc_messages_forwardMessage = 0x3f3f4f2,
	mtpc_messages_sendBroadcast = 0x41bb0972,
	mtpc_geochats_getLocated = 0x7f192d8f,
	mtpc_geochats_getRecents = 0xe1427e6f,
	mtpc_geochats_checkin = 0x55b3e8fb,
	mtpc_geochats_getFullChat = 0x6722dd6f,
	mtpc_geochats_editChatTitle = 0x4c8e2273,
	mtpc_geochats_editChatPhoto = 0x35d81a95,
	mtpc_geochats_search = 0xcfcdc44d,
	mtpc_geochats_getHistory = 0xb53f7a68,
	mtpc_geochats_setTyping = 0x8b8a729,
	mtpc_geochats_sendMessage = 0x61b0044,
	mtpc_geochats_sendMedia = 0xb8f0deff,
	mtpc_geochats_createGeoChat = 0xe092e16,
	mtpc_messages_getDhConfig = 0x26cf8950,
	mtpc_messages_requestEncryption = 0xf64daf43,
	mtpc_messages_acceptEncryption = 0x3dbc0415,
	mtpc_messages_discardEncryption = 0xedd923c5,
	mtpc_messages_setEncryptedTyping = 0x791451ed,
	mtpc_messages_readEncryptedHistory = 0x7f4b690a,
	mtpc_messages_sendEncrypted = 0xa9776773,
	mtpc_messages_sendEncryptedFile = 0x9a901b66,
	mtpc_messages_sendEncryptedService = 0x32d439a4,
	mtpc_messages_receivedQueue = 0x55a5bb66,
	mtpc_upload_saveBigFilePart = 0xde7b673d,
	mtpc_initConnection = 0x69796de9,
	mtpc_help_getSupport = 0x9cdf08cd
};

// Type forward declarations
class MTPresPQ;
class MTPDresPQ;

class MTPp_Q_inner_data;
class MTPDp_q_inner_data;

class MTPserver_DH_Params;
class MTPDserver_DH_params_fail;
class MTPDserver_DH_params_ok;

class MTPserver_DH_inner_data;
class MTPDserver_DH_inner_data;

class MTPclient_DH_Inner_Data;
class MTPDclient_DH_inner_data;

class MTPset_client_DH_params_answer;
class MTPDdh_gen_ok;
class MTPDdh_gen_retry;
class MTPDdh_gen_fail;

class MTPmsgsAck;
class MTPDmsgs_ack;

class MTPbadMsgNotification;
class MTPDbad_msg_notification;
class MTPDbad_server_salt;

class MTPmsgsStateReq;
class MTPDmsgs_state_req;

class MTPmsgsStateInfo;
class MTPDmsgs_state_info;

class MTPmsgsAllInfo;
class MTPDmsgs_all_info;

class MTPmsgDetailedInfo;
class MTPDmsg_detailed_info;
class MTPDmsg_new_detailed_info;

class MTPmsgResendReq;
class MTPDmsg_resend_req;

class MTPrpcError;
class MTPDrpc_error;

class MTPrpcDropAnswer;
class MTPDrpc_answer_dropped;

class MTPfutureSalt;
class MTPDfuture_salt;

class MTPfutureSalts;
class MTPDfuture_salts;

class MTPpong;
class MTPDpong;

class MTPdestroySessionRes;
class MTPDdestroy_session_ok;
class MTPDdestroy_session_none;

class MTPnewSession;
class MTPDnew_session_created;

class MTPhttpWait;
class MTPDhttp_wait;

class MTPinputPeer;
class MTPDinputPeerContact;
class MTPDinputPeerForeign;
class MTPDinputPeerChat;

class MTPinputUser;
class MTPDinputUserContact;
class MTPDinputUserForeign;

class MTPinputContact;
class MTPDinputPhoneContact;

class MTPinputFile;
class MTPDinputFile;
class MTPDinputFileBig;

class MTPinputMedia;
class MTPDinputMediaUploadedPhoto;
class MTPDinputMediaPhoto;
class MTPDinputMediaGeoPoint;
class MTPDinputMediaContact;
class MTPDinputMediaUploadedVideo;
class MTPDinputMediaUploadedThumbVideo;
class MTPDinputMediaVideo;
class MTPDinputMediaUploadedAudio;
class MTPDinputMediaAudio;
class MTPDinputMediaUploadedDocument;
class MTPDinputMediaUploadedThumbDocument;
class MTPDinputMediaDocument;

class MTPinputChatPhoto;
class MTPDinputChatUploadedPhoto;
class MTPDinputChatPhoto;

class MTPinputGeoPoint;
class MTPDinputGeoPoint;

class MTPinputPhoto;
class MTPDinputPhoto;

class MTPinputVideo;
class MTPDinputVideo;

class MTPinputFileLocation;
class MTPDinputFileLocation;
class MTPDinputVideoFileLocation;
class MTPDinputEncryptedFileLocation;
class MTPDinputAudioFileLocation;
class MTPDinputDocumentFileLocation;

class MTPinputPhotoCrop;
class MTPDinputPhotoCrop;

class MTPinputAppEvent;
class MTPDinputAppEvent;

class MTPpeer;
class MTPDpeerUser;
class MTPDpeerChat;

class MTPstorage_fileType;

class MTPfileLocation;
class MTPDfileLocationUnavailable;
class MTPDfileLocation;

class MTPuser;
class MTPDuserEmpty;
class MTPDuserSelf;
class MTPDuserContact;
class MTPDuserRequest;
class MTPDuserForeign;
class MTPDuserDeleted;

class MTPuserProfilePhoto;
class MTPDuserProfilePhoto;

class MTPuserStatus;
class MTPDuserStatusOnline;
class MTPDuserStatusOffline;

class MTPchat;
class MTPDchatEmpty;
class MTPDchat;
class MTPDchatForbidden;
class MTPDgeoChat;

class MTPchatFull;
class MTPDchatFull;

class MTPchatParticipant;
class MTPDchatParticipant;

class MTPchatParticipants;
class MTPDchatParticipantsForbidden;
class MTPDchatParticipants;

class MTPchatPhoto;
class MTPDchatPhoto;

class MTPmessage;
class MTPDmessageEmpty;
class MTPDmessage;
class MTPDmessageForwarded;
class MTPDmessageService;

class MTPmessageMedia;
class MTPDmessageMediaPhoto;
class MTPDmessageMediaVideo;
class MTPDmessageMediaGeo;
class MTPDmessageMediaContact;
class MTPDmessageMediaUnsupported;
class MTPDmessageMediaDocument;
class MTPDmessageMediaAudio;

class MTPmessageAction;
class MTPDmessageActionChatCreate;
class MTPDmessageActionChatEditTitle;
class MTPDmessageActionChatEditPhoto;
class MTPDmessageActionChatAddUser;
class MTPDmessageActionChatDeleteUser;
class MTPDmessageActionGeoChatCreate;

class MTPdialog;
class MTPDdialog;

class MTPphoto;
class MTPDphotoEmpty;
class MTPDphoto;

class MTPphotoSize;
class MTPDphotoSizeEmpty;
class MTPDphotoSize;
class MTPDphotoCachedSize;

class MTPvideo;
class MTPDvideoEmpty;
class MTPDvideo;

class MTPgeoPoint;
class MTPDgeoPoint;

class MTPauth_checkedPhone;
class MTPDauth_checkedPhone;

class MTPauth_sentCode;
class MTPDauth_sentCode;

class MTPauth_authorization;
class MTPDauth_authorization;

class MTPauth_exportedAuthorization;
class MTPDauth_exportedAuthorization;

class MTPinputNotifyPeer;
class MTPDinputNotifyPeer;
class MTPDinputNotifyGeoChatPeer;

class MTPinputPeerNotifyEvents;

class MTPinputPeerNotifySettings;
class MTPDinputPeerNotifySettings;

class MTPpeerNotifyEvents;

class MTPpeerNotifySettings;
class MTPDpeerNotifySettings;

class MTPwallPaper;
class MTPDwallPaper;
class MTPDwallPaperSolid;

class MTPuserFull;
class MTPDuserFull;

class MTPcontact;
class MTPDcontact;

class MTPimportedContact;
class MTPDimportedContact;

class MTPcontactBlocked;
class MTPDcontactBlocked;

class MTPcontactFound;
class MTPDcontactFound;

class MTPcontactSuggested;
class MTPDcontactSuggested;

class MTPcontactStatus;
class MTPDcontactStatus;

class MTPchatLocated;
class MTPDchatLocated;

class MTPcontacts_foreignLink;
class MTPDcontacts_foreignLinkRequested;

class MTPcontacts_myLink;
class MTPDcontacts_myLinkRequested;

class MTPcontacts_link;
class MTPDcontacts_link;

class MTPcontacts_contacts;
class MTPDcontacts_contacts;

class MTPcontacts_importedContacts;
class MTPDcontacts_importedContacts;

class MTPcontacts_blocked;
class MTPDcontacts_blocked;
class MTPDcontacts_blockedSlice;

class MTPcontacts_found;
class MTPDcontacts_found;

class MTPcontacts_suggested;
class MTPDcontacts_suggested;

class MTPmessages_dialogs;
class MTPDmessages_dialogs;
class MTPDmessages_dialogsSlice;

class MTPmessages_messages;
class MTPDmessages_messages;
class MTPDmessages_messagesSlice;

class MTPmessages_message;
class MTPDmessages_message;

class MTPmessages_statedMessages;
class MTPDmessages_statedMessages;
class MTPDmessages_statedMessagesLinks;

class MTPmessages_statedMessage;
class MTPDmessages_statedMessage;
class MTPDmessages_statedMessageLink;

class MTPmessages_sentMessage;
class MTPDmessages_sentMessage;
class MTPDmessages_sentMessageLink;

class MTPmessages_chat;
class MTPDmessages_chat;

class MTPmessages_chats;
class MTPDmessages_chats;

class MTPmessages_chatFull;
class MTPDmessages_chatFull;

class MTPmessages_affectedHistory;
class MTPDmessages_affectedHistory;

class MTPmessagesFilter;

class MTPupdate;
class MTPDupdateNewMessage;
class MTPDupdateMessageID;
class MTPDupdateReadMessages;
class MTPDupdateDeleteMessages;
class MTPDupdateRestoreMessages;
class MTPDupdateUserTyping;
class MTPDupdateChatUserTyping;
class MTPDupdateChatParticipants;
class MTPDupdateUserStatus;
class MTPDupdateUserName;
class MTPDupdateUserPhoto;
class MTPDupdateContactRegistered;
class MTPDupdateContactLink;
class MTPDupdateActivation;
class MTPDupdateNewAuthorization;
class MTPDupdateNewGeoChatMessage;
class MTPDupdateNewEncryptedMessage;
class MTPDupdateEncryptedChatTyping;
class MTPDupdateEncryption;
class MTPDupdateEncryptedMessagesRead;
class MTPDupdateChatParticipantAdd;
class MTPDupdateChatParticipantDelete;
class MTPDupdateDcOptions;
class MTPDupdateUserBlocked;
class MTPDupdateNotifySettings;

class MTPupdates_state;
class MTPDupdates_state;

class MTPupdates_difference;
class MTPDupdates_differenceEmpty;
class MTPDupdates_difference;
class MTPDupdates_differenceSlice;

class MTPupdates;
class MTPDupdateShortMessage;
class MTPDupdateShortChatMessage;
class MTPDupdateShort;
class MTPDupdatesCombined;
class MTPDupdates;

class MTPphotos_photos;
class MTPDphotos_photos;
class MTPDphotos_photosSlice;

class MTPphotos_photo;
class MTPDphotos_photo;

class MTPupload_file;
class MTPDupload_file;

class MTPdcOption;
class MTPDdcOption;

class MTPconfig;
class MTPDconfig;

class MTPnearestDc;
class MTPDnearestDc;

class MTPhelp_appUpdate;
class MTPDhelp_appUpdate;

class MTPhelp_inviteText;
class MTPDhelp_inviteText;

class MTPinputGeoChat;
class MTPDinputGeoChat;

class MTPgeoChatMessage;
class MTPDgeoChatMessageEmpty;
class MTPDgeoChatMessage;
class MTPDgeoChatMessageService;

class MTPgeochats_statedMessage;
class MTPDgeochats_statedMessage;

class MTPgeochats_located;
class MTPDgeochats_located;

class MTPgeochats_messages;
class MTPDgeochats_messages;
class MTPDgeochats_messagesSlice;

class MTPencryptedChat;
class MTPDencryptedChatEmpty;
class MTPDencryptedChatWaiting;
class MTPDencryptedChatRequested;
class MTPDencryptedChat;
class MTPDencryptedChatDiscarded;

class MTPinputEncryptedChat;
class MTPDinputEncryptedChat;

class MTPencryptedFile;
class MTPDencryptedFile;

class MTPinputEncryptedFile;
class MTPDinputEncryptedFileUploaded;
class MTPDinputEncryptedFile;
class MTPDinputEncryptedFileBigUploaded;

class MTPencryptedMessage;
class MTPDencryptedMessage;
class MTPDencryptedMessageService;

class MTPdecryptedMessageLayer;
class MTPDdecryptedMessageLayer;

class MTPdecryptedMessage;
class MTPDdecryptedMessage;
class MTPDdecryptedMessageService;

class MTPdecryptedMessageMedia;
class MTPDdecryptedMessageMediaPhoto;
class MTPDdecryptedMessageMediaVideo;
class MTPDdecryptedMessageMediaGeoPoint;
class MTPDdecryptedMessageMediaContact;
class MTPDdecryptedMessageMediaDocument;
class MTPDdecryptedMessageMediaAudio;

class MTPdecryptedMessageAction;
class MTPDdecryptedMessageActionSetMessageTTL;
class MTPDdecryptedMessageActionReadMessages;
class MTPDdecryptedMessageActionDeleteMessages;
class MTPDdecryptedMessageActionScreenshotMessages;
class MTPDdecryptedMessageActionNotifyLayer;

class MTPmessages_dhConfig;
class MTPDmessages_dhConfigNotModified;
class MTPDmessages_dhConfig;

class MTPmessages_sentEncryptedMessage;
class MTPDmessages_sentEncryptedMessage;
class MTPDmessages_sentEncryptedFile;

class MTPinputAudio;
class MTPDinputAudio;

class MTPinputDocument;
class MTPDinputDocument;

class MTPaudio;
class MTPDaudioEmpty;
class MTPDaudio;

class MTPdocument;
class MTPDdocumentEmpty;
class MTPDdocument;

class MTPhelp_support;
class MTPDhelp_support;

class MTPnotifyPeer;
class MTPDnotifyPeer;


// Boxed types definitions
typedef MTPBoxed<MTPresPQ> MTPResPQ;
typedef MTPBoxed<MTPp_Q_inner_data> MTPP_Q_inner_data;
typedef MTPBoxed<MTPserver_DH_Params> MTPServer_DH_Params;
typedef MTPBoxed<MTPserver_DH_inner_data> MTPServer_DH_inner_data;
typedef MTPBoxed<MTPclient_DH_Inner_Data> MTPClient_DH_Inner_Data;
typedef MTPBoxed<MTPset_client_DH_params_answer> MTPSet_client_DH_params_answer;
typedef MTPBoxed<MTPmsgsAck> MTPMsgsAck;
typedef MTPBoxed<MTPbadMsgNotification> MTPBadMsgNotification;
typedef MTPBoxed<MTPmsgsStateReq> MTPMsgsStateReq;
typedef MTPBoxed<MTPmsgsStateInfo> MTPMsgsStateInfo;
typedef MTPBoxed<MTPmsgsAllInfo> MTPMsgsAllInfo;
typedef MTPBoxed<MTPmsgDetailedInfo> MTPMsgDetailedInfo;
typedef MTPBoxed<MTPmsgResendReq> MTPMsgResendReq;
typedef MTPBoxed<MTPrpcError> MTPRpcError;
typedef MTPBoxed<MTPrpcDropAnswer> MTPRpcDropAnswer;
typedef MTPBoxed<MTPfutureSalt> MTPFutureSalt;
typedef MTPBoxed<MTPfutureSalts> MTPFutureSalts;
typedef MTPBoxed<MTPpong> MTPPong;
typedef MTPBoxed<MTPdestroySessionRes> MTPDestroySessionRes;
typedef MTPBoxed<MTPnewSession> MTPNewSession;
typedef MTPBoxed<MTPhttpWait> MTPHttpWait;
typedef MTPBoxed<MTPinputPeer> MTPInputPeer;
typedef MTPBoxed<MTPinputUser> MTPInputUser;
typedef MTPBoxed<MTPinputContact> MTPInputContact;
typedef MTPBoxed<MTPinputFile> MTPInputFile;
typedef MTPBoxed<MTPinputMedia> MTPInputMedia;
typedef MTPBoxed<MTPinputChatPhoto> MTPInputChatPhoto;
typedef MTPBoxed<MTPinputGeoPoint> MTPInputGeoPoint;
typedef MTPBoxed<MTPinputPhoto> MTPInputPhoto;
typedef MTPBoxed<MTPinputVideo> MTPInputVideo;
typedef MTPBoxed<MTPinputFileLocation> MTPInputFileLocation;
typedef MTPBoxed<MTPinputPhotoCrop> MTPInputPhotoCrop;
typedef MTPBoxed<MTPinputAppEvent> MTPInputAppEvent;
typedef MTPBoxed<MTPpeer> MTPPeer;
typedef MTPBoxed<MTPstorage_fileType> MTPstorage_FileType;
typedef MTPBoxed<MTPfileLocation> MTPFileLocation;
typedef MTPBoxed<MTPuser> MTPUser;
typedef MTPBoxed<MTPuserProfilePhoto> MTPUserProfilePhoto;
typedef MTPBoxed<MTPuserStatus> MTPUserStatus;
typedef MTPBoxed<MTPchat> MTPChat;
typedef MTPBoxed<MTPchatFull> MTPChatFull;
typedef MTPBoxed<MTPchatParticipant> MTPChatParticipant;
typedef MTPBoxed<MTPchatParticipants> MTPChatParticipants;
typedef MTPBoxed<MTPchatPhoto> MTPChatPhoto;
typedef MTPBoxed<MTPmessage> MTPMessage;
typedef MTPBoxed<MTPmessageMedia> MTPMessageMedia;
typedef MTPBoxed<MTPmessageAction> MTPMessageAction;
typedef MTPBoxed<MTPdialog> MTPDialog;
typedef MTPBoxed<MTPphoto> MTPPhoto;
typedef MTPBoxed<MTPphotoSize> MTPPhotoSize;
typedef MTPBoxed<MTPvideo> MTPVideo;
typedef MTPBoxed<MTPgeoPoint> MTPGeoPoint;
typedef MTPBoxed<MTPauth_checkedPhone> MTPauth_CheckedPhone;
typedef MTPBoxed<MTPauth_sentCode> MTPauth_SentCode;
typedef MTPBoxed<MTPauth_authorization> MTPauth_Authorization;
typedef MTPBoxed<MTPauth_exportedAuthorization> MTPauth_ExportedAuthorization;
typedef MTPBoxed<MTPinputNotifyPeer> MTPInputNotifyPeer;
typedef MTPBoxed<MTPinputPeerNotifyEvents> MTPInputPeerNotifyEvents;
typedef MTPBoxed<MTPinputPeerNotifySettings> MTPInputPeerNotifySettings;
typedef MTPBoxed<MTPpeerNotifyEvents> MTPPeerNotifyEvents;
typedef MTPBoxed<MTPpeerNotifySettings> MTPPeerNotifySettings;
typedef MTPBoxed<MTPwallPaper> MTPWallPaper;
typedef MTPBoxed<MTPuserFull> MTPUserFull;
typedef MTPBoxed<MTPcontact> MTPContact;
typedef MTPBoxed<MTPimportedContact> MTPImportedContact;
typedef MTPBoxed<MTPcontactBlocked> MTPContactBlocked;
typedef MTPBoxed<MTPcontactFound> MTPContactFound;
typedef MTPBoxed<MTPcontactSuggested> MTPContactSuggested;
typedef MTPBoxed<MTPcontactStatus> MTPContactStatus;
typedef MTPBoxed<MTPchatLocated> MTPChatLocated;
typedef MTPBoxed<MTPcontacts_foreignLink> MTPcontacts_ForeignLink;
typedef MTPBoxed<MTPcontacts_myLink> MTPcontacts_MyLink;
typedef MTPBoxed<MTPcontacts_link> MTPcontacts_Link;
typedef MTPBoxed<MTPcontacts_contacts> MTPcontacts_Contacts;
typedef MTPBoxed<MTPcontacts_importedContacts> MTPcontacts_ImportedContacts;
typedef MTPBoxed<MTPcontacts_blocked> MTPcontacts_Blocked;
typedef MTPBoxed<MTPcontacts_found> MTPcontacts_Found;
typedef MTPBoxed<MTPcontacts_suggested> MTPcontacts_Suggested;
typedef MTPBoxed<MTPmessages_dialogs> MTPmessages_Dialogs;
typedef MTPBoxed<MTPmessages_messages> MTPmessages_Messages;
typedef MTPBoxed<MTPmessages_message> MTPmessages_Message;
typedef MTPBoxed<MTPmessages_statedMessages> MTPmessages_StatedMessages;
typedef MTPBoxed<MTPmessages_statedMessage> MTPmessages_StatedMessage;
typedef MTPBoxed<MTPmessages_sentMessage> MTPmessages_SentMessage;
typedef MTPBoxed<MTPmessages_chat> MTPmessages_Chat;
typedef MTPBoxed<MTPmessages_chats> MTPmessages_Chats;
typedef MTPBoxed<MTPmessages_chatFull> MTPmessages_ChatFull;
typedef MTPBoxed<MTPmessages_affectedHistory> MTPmessages_AffectedHistory;
typedef MTPBoxed<MTPmessagesFilter> MTPMessagesFilter;
typedef MTPBoxed<MTPupdate> MTPUpdate;
typedef MTPBoxed<MTPupdates_state> MTPupdates_State;
typedef MTPBoxed<MTPupdates_difference> MTPupdates_Difference;
typedef MTPBoxed<MTPupdates> MTPUpdates;
typedef MTPBoxed<MTPphotos_photos> MTPphotos_Photos;
typedef MTPBoxed<MTPphotos_photo> MTPphotos_Photo;
typedef MTPBoxed<MTPupload_file> MTPupload_File;
typedef MTPBoxed<MTPdcOption> MTPDcOption;
typedef MTPBoxed<MTPconfig> MTPConfig;
typedef MTPBoxed<MTPnearestDc> MTPNearestDc;
typedef MTPBoxed<MTPhelp_appUpdate> MTPhelp_AppUpdate;
typedef MTPBoxed<MTPhelp_inviteText> MTPhelp_InviteText;
typedef MTPBoxed<MTPinputGeoChat> MTPInputGeoChat;
typedef MTPBoxed<MTPgeoChatMessage> MTPGeoChatMessage;
typedef MTPBoxed<MTPgeochats_statedMessage> MTPgeochats_StatedMessage;
typedef MTPBoxed<MTPgeochats_located> MTPgeochats_Located;
typedef MTPBoxed<MTPgeochats_messages> MTPgeochats_Messages;
typedef MTPBoxed<MTPencryptedChat> MTPEncryptedChat;
typedef MTPBoxed<MTPinputEncryptedChat> MTPInputEncryptedChat;
typedef MTPBoxed<MTPencryptedFile> MTPEncryptedFile;
typedef MTPBoxed<MTPinputEncryptedFile> MTPInputEncryptedFile;
typedef MTPBoxed<MTPencryptedMessage> MTPEncryptedMessage;
typedef MTPBoxed<MTPdecryptedMessageLayer> MTPDecryptedMessageLayer;
typedef MTPBoxed<MTPdecryptedMessage> MTPDecryptedMessage;
typedef MTPBoxed<MTPdecryptedMessageMedia> MTPDecryptedMessageMedia;
typedef MTPBoxed<MTPdecryptedMessageAction> MTPDecryptedMessageAction;
typedef MTPBoxed<MTPmessages_dhConfig> MTPmessages_DhConfig;
typedef MTPBoxed<MTPmessages_sentEncryptedMessage> MTPmessages_SentEncryptedMessage;
typedef MTPBoxed<MTPinputAudio> MTPInputAudio;
typedef MTPBoxed<MTPinputDocument> MTPInputDocument;
typedef MTPBoxed<MTPaudio> MTPAudio;
typedef MTPBoxed<MTPdocument> MTPDocument;
typedef MTPBoxed<MTPhelp_support> MTPhelp_Support;
typedef MTPBoxed<MTPnotifyPeer> MTPNotifyPeer;

// Type classes definitions

class MTPresPQ : private mtpDataOwner {
public:
	MTPresPQ();
	MTPresPQ(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_resPQ) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDresPQ &_resPQ() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDresPQ*)data;
	}
	const MTPDresPQ &c_resPQ() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDresPQ*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_resPQ);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPresPQ(MTPDresPQ *_data);

	friend MTPresPQ MTP_resPQ(const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPstring &_pq, const MTPVector<MTPlong> &_server_public_key_fingerprints);
};
typedef MTPBoxed<MTPresPQ> MTPResPQ;

class MTPp_Q_inner_data : private mtpDataOwner {
public:
	MTPp_Q_inner_data();
	MTPp_Q_inner_data(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_p_q_inner_data) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDp_q_inner_data &_p_q_inner_data() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDp_q_inner_data*)data;
	}
	const MTPDp_q_inner_data &c_p_q_inner_data() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDp_q_inner_data*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_p_q_inner_data);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPp_Q_inner_data(MTPDp_q_inner_data *_data);

	friend MTPp_Q_inner_data MTP_p_q_inner_data(const MTPstring &_pq, const MTPstring &_p, const MTPstring &_q, const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPint256 &_new_nonce);
};
typedef MTPBoxed<MTPp_Q_inner_data> MTPP_Q_inner_data;

class MTPserver_DH_Params : private mtpDataOwner {
public:
	MTPserver_DH_Params() : mtpDataOwner(0), _type(0) {
	}
	MTPserver_DH_Params(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDserver_DH_params_fail &_server_DH_params_fail() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_server_DH_params_fail) throw mtpErrorWrongTypeId(_type, mtpc_server_DH_params_fail);
		split();
		return *(MTPDserver_DH_params_fail*)data;
	}
	const MTPDserver_DH_params_fail &c_server_DH_params_fail() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_server_DH_params_fail) throw mtpErrorWrongTypeId(_type, mtpc_server_DH_params_fail);
		return *(const MTPDserver_DH_params_fail*)data;
	}

	MTPDserver_DH_params_ok &_server_DH_params_ok() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_server_DH_params_ok) throw mtpErrorWrongTypeId(_type, mtpc_server_DH_params_ok);
		split();
		return *(MTPDserver_DH_params_ok*)data;
	}
	const MTPDserver_DH_params_ok &c_server_DH_params_ok() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_server_DH_params_ok) throw mtpErrorWrongTypeId(_type, mtpc_server_DH_params_ok);
		return *(const MTPDserver_DH_params_ok*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPserver_DH_Params(mtpTypeId type);
	explicit MTPserver_DH_Params(MTPDserver_DH_params_fail *_data);
	explicit MTPserver_DH_Params(MTPDserver_DH_params_ok *_data);

	friend MTPserver_DH_Params MTP_server_DH_params_fail(const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPint128 &_new_nonce_hash);
	friend MTPserver_DH_Params MTP_server_DH_params_ok(const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPstring &_encrypted_answer);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPserver_DH_Params> MTPServer_DH_Params;

class MTPserver_DH_inner_data : private mtpDataOwner {
public:
	MTPserver_DH_inner_data();
	MTPserver_DH_inner_data(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_server_DH_inner_data) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDserver_DH_inner_data &_server_DH_inner_data() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDserver_DH_inner_data*)data;
	}
	const MTPDserver_DH_inner_data &c_server_DH_inner_data() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDserver_DH_inner_data*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_server_DH_inner_data);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPserver_DH_inner_data(MTPDserver_DH_inner_data *_data);

	friend MTPserver_DH_inner_data MTP_server_DH_inner_data(const MTPint128 &_nonce, const MTPint128 &_server_nonce, MTPint _g, const MTPstring &_dh_prime, const MTPstring &_g_a, MTPint _server_time);
};
typedef MTPBoxed<MTPserver_DH_inner_data> MTPServer_DH_inner_data;

class MTPclient_DH_Inner_Data : private mtpDataOwner {
public:
	MTPclient_DH_Inner_Data();
	MTPclient_DH_Inner_Data(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_client_DH_inner_data) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDclient_DH_inner_data &_client_DH_inner_data() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDclient_DH_inner_data*)data;
	}
	const MTPDclient_DH_inner_data &c_client_DH_inner_data() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDclient_DH_inner_data*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_client_DH_inner_data);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPclient_DH_Inner_Data(MTPDclient_DH_inner_data *_data);

	friend MTPclient_DH_Inner_Data MTP_client_DH_inner_data(const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPlong &_retry_id, const MTPstring &_g_b);
};
typedef MTPBoxed<MTPclient_DH_Inner_Data> MTPClient_DH_Inner_Data;

class MTPset_client_DH_params_answer : private mtpDataOwner {
public:
	MTPset_client_DH_params_answer() : mtpDataOwner(0), _type(0) {
	}
	MTPset_client_DH_params_answer(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDdh_gen_ok &_dh_gen_ok() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_dh_gen_ok) throw mtpErrorWrongTypeId(_type, mtpc_dh_gen_ok);
		split();
		return *(MTPDdh_gen_ok*)data;
	}
	const MTPDdh_gen_ok &c_dh_gen_ok() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_dh_gen_ok) throw mtpErrorWrongTypeId(_type, mtpc_dh_gen_ok);
		return *(const MTPDdh_gen_ok*)data;
	}

	MTPDdh_gen_retry &_dh_gen_retry() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_dh_gen_retry) throw mtpErrorWrongTypeId(_type, mtpc_dh_gen_retry);
		split();
		return *(MTPDdh_gen_retry*)data;
	}
	const MTPDdh_gen_retry &c_dh_gen_retry() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_dh_gen_retry) throw mtpErrorWrongTypeId(_type, mtpc_dh_gen_retry);
		return *(const MTPDdh_gen_retry*)data;
	}

	MTPDdh_gen_fail &_dh_gen_fail() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_dh_gen_fail) throw mtpErrorWrongTypeId(_type, mtpc_dh_gen_fail);
		split();
		return *(MTPDdh_gen_fail*)data;
	}
	const MTPDdh_gen_fail &c_dh_gen_fail() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_dh_gen_fail) throw mtpErrorWrongTypeId(_type, mtpc_dh_gen_fail);
		return *(const MTPDdh_gen_fail*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPset_client_DH_params_answer(mtpTypeId type);
	explicit MTPset_client_DH_params_answer(MTPDdh_gen_ok *_data);
	explicit MTPset_client_DH_params_answer(MTPDdh_gen_retry *_data);
	explicit MTPset_client_DH_params_answer(MTPDdh_gen_fail *_data);

	friend MTPset_client_DH_params_answer MTP_dh_gen_ok(const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPint128 &_new_nonce_hash1);
	friend MTPset_client_DH_params_answer MTP_dh_gen_retry(const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPint128 &_new_nonce_hash2);
	friend MTPset_client_DH_params_answer MTP_dh_gen_fail(const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPint128 &_new_nonce_hash3);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPset_client_DH_params_answer> MTPSet_client_DH_params_answer;

class MTPmsgsAck : private mtpDataOwner {
public:
	MTPmsgsAck();
	MTPmsgsAck(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_msgs_ack) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDmsgs_ack &_msgs_ack() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDmsgs_ack*)data;
	}
	const MTPDmsgs_ack &c_msgs_ack() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDmsgs_ack*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_msgs_ack);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPmsgsAck(MTPDmsgs_ack *_data);

	friend MTPmsgsAck MTP_msgs_ack(const MTPVector<MTPlong> &_msg_ids);
};
typedef MTPBoxed<MTPmsgsAck> MTPMsgsAck;

class MTPbadMsgNotification : private mtpDataOwner {
public:
	MTPbadMsgNotification() : mtpDataOwner(0), _type(0) {
	}
	MTPbadMsgNotification(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDbad_msg_notification &_bad_msg_notification() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_bad_msg_notification) throw mtpErrorWrongTypeId(_type, mtpc_bad_msg_notification);
		split();
		return *(MTPDbad_msg_notification*)data;
	}
	const MTPDbad_msg_notification &c_bad_msg_notification() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_bad_msg_notification) throw mtpErrorWrongTypeId(_type, mtpc_bad_msg_notification);
		return *(const MTPDbad_msg_notification*)data;
	}

	MTPDbad_server_salt &_bad_server_salt() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_bad_server_salt) throw mtpErrorWrongTypeId(_type, mtpc_bad_server_salt);
		split();
		return *(MTPDbad_server_salt*)data;
	}
	const MTPDbad_server_salt &c_bad_server_salt() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_bad_server_salt) throw mtpErrorWrongTypeId(_type, mtpc_bad_server_salt);
		return *(const MTPDbad_server_salt*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPbadMsgNotification(mtpTypeId type);
	explicit MTPbadMsgNotification(MTPDbad_msg_notification *_data);
	explicit MTPbadMsgNotification(MTPDbad_server_salt *_data);

	friend MTPbadMsgNotification MTP_bad_msg_notification(const MTPlong &_bad_msg_id, MTPint _bad_msg_seqno, MTPint _error_code);
	friend MTPbadMsgNotification MTP_bad_server_salt(const MTPlong &_bad_msg_id, MTPint _bad_msg_seqno, MTPint _error_code, const MTPlong &_new_server_salt);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPbadMsgNotification> MTPBadMsgNotification;

class MTPmsgsStateReq : private mtpDataOwner {
public:
	MTPmsgsStateReq();
	MTPmsgsStateReq(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_msgs_state_req) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDmsgs_state_req &_msgs_state_req() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDmsgs_state_req*)data;
	}
	const MTPDmsgs_state_req &c_msgs_state_req() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDmsgs_state_req*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_msgs_state_req);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPmsgsStateReq(MTPDmsgs_state_req *_data);

	friend MTPmsgsStateReq MTP_msgs_state_req(const MTPVector<MTPlong> &_msg_ids);
};
typedef MTPBoxed<MTPmsgsStateReq> MTPMsgsStateReq;

class MTPmsgsStateInfo : private mtpDataOwner {
public:
	MTPmsgsStateInfo();
	MTPmsgsStateInfo(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_msgs_state_info) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDmsgs_state_info &_msgs_state_info() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDmsgs_state_info*)data;
	}
	const MTPDmsgs_state_info &c_msgs_state_info() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDmsgs_state_info*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_msgs_state_info);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPmsgsStateInfo(MTPDmsgs_state_info *_data);

	friend MTPmsgsStateInfo MTP_msgs_state_info(const MTPlong &_req_msg_id, const MTPstring &_info);
};
typedef MTPBoxed<MTPmsgsStateInfo> MTPMsgsStateInfo;

class MTPmsgsAllInfo : private mtpDataOwner {
public:
	MTPmsgsAllInfo();
	MTPmsgsAllInfo(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_msgs_all_info) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDmsgs_all_info &_msgs_all_info() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDmsgs_all_info*)data;
	}
	const MTPDmsgs_all_info &c_msgs_all_info() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDmsgs_all_info*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_msgs_all_info);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPmsgsAllInfo(MTPDmsgs_all_info *_data);

	friend MTPmsgsAllInfo MTP_msgs_all_info(const MTPVector<MTPlong> &_msg_ids, const MTPstring &_info);
};
typedef MTPBoxed<MTPmsgsAllInfo> MTPMsgsAllInfo;

class MTPmsgDetailedInfo : private mtpDataOwner {
public:
	MTPmsgDetailedInfo() : mtpDataOwner(0), _type(0) {
	}
	MTPmsgDetailedInfo(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDmsg_detailed_info &_msg_detailed_info() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_msg_detailed_info) throw mtpErrorWrongTypeId(_type, mtpc_msg_detailed_info);
		split();
		return *(MTPDmsg_detailed_info*)data;
	}
	const MTPDmsg_detailed_info &c_msg_detailed_info() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_msg_detailed_info) throw mtpErrorWrongTypeId(_type, mtpc_msg_detailed_info);
		return *(const MTPDmsg_detailed_info*)data;
	}

	MTPDmsg_new_detailed_info &_msg_new_detailed_info() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_msg_new_detailed_info) throw mtpErrorWrongTypeId(_type, mtpc_msg_new_detailed_info);
		split();
		return *(MTPDmsg_new_detailed_info*)data;
	}
	const MTPDmsg_new_detailed_info &c_msg_new_detailed_info() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_msg_new_detailed_info) throw mtpErrorWrongTypeId(_type, mtpc_msg_new_detailed_info);
		return *(const MTPDmsg_new_detailed_info*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPmsgDetailedInfo(mtpTypeId type);
	explicit MTPmsgDetailedInfo(MTPDmsg_detailed_info *_data);
	explicit MTPmsgDetailedInfo(MTPDmsg_new_detailed_info *_data);

	friend MTPmsgDetailedInfo MTP_msg_detailed_info(const MTPlong &_msg_id, const MTPlong &_answer_msg_id, MTPint _bytes, MTPint _status);
	friend MTPmsgDetailedInfo MTP_msg_new_detailed_info(const MTPlong &_answer_msg_id, MTPint _bytes, MTPint _status);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPmsgDetailedInfo> MTPMsgDetailedInfo;

class MTPmsgResendReq : private mtpDataOwner {
public:
	MTPmsgResendReq();
	MTPmsgResendReq(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_msg_resend_req) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDmsg_resend_req &_msg_resend_req() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDmsg_resend_req*)data;
	}
	const MTPDmsg_resend_req &c_msg_resend_req() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDmsg_resend_req*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_msg_resend_req);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPmsgResendReq(MTPDmsg_resend_req *_data);

	friend MTPmsgResendReq MTP_msg_resend_req(const MTPVector<MTPlong> &_msg_ids);
};
typedef MTPBoxed<MTPmsgResendReq> MTPMsgResendReq;

class MTPrpcError : private mtpDataOwner {
public:
	MTPrpcError();
	MTPrpcError(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_rpc_error) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDrpc_error &_rpc_error() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDrpc_error*)data;
	}
	const MTPDrpc_error &c_rpc_error() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDrpc_error*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_rpc_error);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPrpcError(MTPDrpc_error *_data);

	friend MTPrpcError MTP_rpc_error(MTPint _error_code, const MTPstring &_error_message);
};
typedef MTPBoxed<MTPrpcError> MTPRpcError;

class MTPrpcDropAnswer : private mtpDataOwner {
public:
	MTPrpcDropAnswer() : mtpDataOwner(0), _type(0) {
	}
	MTPrpcDropAnswer(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDrpc_answer_dropped &_rpc_answer_dropped() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_rpc_answer_dropped) throw mtpErrorWrongTypeId(_type, mtpc_rpc_answer_dropped);
		split();
		return *(MTPDrpc_answer_dropped*)data;
	}
	const MTPDrpc_answer_dropped &c_rpc_answer_dropped() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_rpc_answer_dropped) throw mtpErrorWrongTypeId(_type, mtpc_rpc_answer_dropped);
		return *(const MTPDrpc_answer_dropped*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPrpcDropAnswer(mtpTypeId type);
	explicit MTPrpcDropAnswer(MTPDrpc_answer_dropped *_data);

	friend MTPrpcDropAnswer MTP_rpc_answer_unknown();
	friend MTPrpcDropAnswer MTP_rpc_answer_dropped_running();
	friend MTPrpcDropAnswer MTP_rpc_answer_dropped(const MTPlong &_msg_id, MTPint _seq_no, MTPint _bytes);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPrpcDropAnswer> MTPRpcDropAnswer;

class MTPfutureSalt : private mtpDataOwner {
public:
	MTPfutureSalt();
	MTPfutureSalt(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_future_salt) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDfuture_salt &_future_salt() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDfuture_salt*)data;
	}
	const MTPDfuture_salt &c_future_salt() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDfuture_salt*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_future_salt);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPfutureSalt(MTPDfuture_salt *_data);

	friend MTPfutureSalt MTP_future_salt(MTPint _valid_since, MTPint _valid_until, const MTPlong &_salt);
};
typedef MTPBoxed<MTPfutureSalt> MTPFutureSalt;

class MTPfutureSalts : private mtpDataOwner {
public:
	MTPfutureSalts();
	MTPfutureSalts(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_future_salts) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDfuture_salts &_future_salts() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDfuture_salts*)data;
	}
	const MTPDfuture_salts &c_future_salts() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDfuture_salts*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_future_salts);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPfutureSalts(MTPDfuture_salts *_data);

	friend MTPfutureSalts MTP_future_salts(const MTPlong &_req_msg_id, MTPint _now, const MTPvector<MTPfutureSalt> &_salts);
};
typedef MTPBoxed<MTPfutureSalts> MTPFutureSalts;

class MTPpong : private mtpDataOwner {
public:
	MTPpong();
	MTPpong(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_pong) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDpong &_pong() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDpong*)data;
	}
	const MTPDpong &c_pong() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDpong*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_pong);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPpong(MTPDpong *_data);

	friend MTPpong MTP_pong(const MTPlong &_msg_id, const MTPlong &_ping_id);
};
typedef MTPBoxed<MTPpong> MTPPong;

class MTPdestroySessionRes : private mtpDataOwner {
public:
	MTPdestroySessionRes() : mtpDataOwner(0), _type(0) {
	}
	MTPdestroySessionRes(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDdestroy_session_ok &_destroy_session_ok() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_destroy_session_ok) throw mtpErrorWrongTypeId(_type, mtpc_destroy_session_ok);
		split();
		return *(MTPDdestroy_session_ok*)data;
	}
	const MTPDdestroy_session_ok &c_destroy_session_ok() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_destroy_session_ok) throw mtpErrorWrongTypeId(_type, mtpc_destroy_session_ok);
		return *(const MTPDdestroy_session_ok*)data;
	}

	MTPDdestroy_session_none &_destroy_session_none() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_destroy_session_none) throw mtpErrorWrongTypeId(_type, mtpc_destroy_session_none);
		split();
		return *(MTPDdestroy_session_none*)data;
	}
	const MTPDdestroy_session_none &c_destroy_session_none() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_destroy_session_none) throw mtpErrorWrongTypeId(_type, mtpc_destroy_session_none);
		return *(const MTPDdestroy_session_none*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPdestroySessionRes(mtpTypeId type);
	explicit MTPdestroySessionRes(MTPDdestroy_session_ok *_data);
	explicit MTPdestroySessionRes(MTPDdestroy_session_none *_data);

	friend MTPdestroySessionRes MTP_destroy_session_ok(const MTPlong &_session_id);
	friend MTPdestroySessionRes MTP_destroy_session_none(const MTPlong &_session_id);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPdestroySessionRes> MTPDestroySessionRes;

class MTPnewSession : private mtpDataOwner {
public:
	MTPnewSession();
	MTPnewSession(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_new_session_created) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDnew_session_created &_new_session_created() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDnew_session_created*)data;
	}
	const MTPDnew_session_created &c_new_session_created() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDnew_session_created*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_new_session_created);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPnewSession(MTPDnew_session_created *_data);

	friend MTPnewSession MTP_new_session_created(const MTPlong &_first_msg_id, const MTPlong &_unique_id, const MTPlong &_server_salt);
};
typedef MTPBoxed<MTPnewSession> MTPNewSession;

class MTPhttpWait : private mtpDataOwner {
public:
	MTPhttpWait();
	MTPhttpWait(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_http_wait) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDhttp_wait &_http_wait() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDhttp_wait*)data;
	}
	const MTPDhttp_wait &c_http_wait() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDhttp_wait*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_http_wait);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPhttpWait(MTPDhttp_wait *_data);

	friend MTPhttpWait MTP_http_wait(MTPint _max_delay, MTPint _wait_after, MTPint _max_wait);
};
typedef MTPBoxed<MTPhttpWait> MTPHttpWait;

class MTPinputPeer : private mtpDataOwner {
public:
	MTPinputPeer() : mtpDataOwner(0), _type(0) {
	}
	MTPinputPeer(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDinputPeerContact &_inputPeerContact() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputPeerContact) throw mtpErrorWrongTypeId(_type, mtpc_inputPeerContact);
		split();
		return *(MTPDinputPeerContact*)data;
	}
	const MTPDinputPeerContact &c_inputPeerContact() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputPeerContact) throw mtpErrorWrongTypeId(_type, mtpc_inputPeerContact);
		return *(const MTPDinputPeerContact*)data;
	}

	MTPDinputPeerForeign &_inputPeerForeign() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputPeerForeign) throw mtpErrorWrongTypeId(_type, mtpc_inputPeerForeign);
		split();
		return *(MTPDinputPeerForeign*)data;
	}
	const MTPDinputPeerForeign &c_inputPeerForeign() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputPeerForeign) throw mtpErrorWrongTypeId(_type, mtpc_inputPeerForeign);
		return *(const MTPDinputPeerForeign*)data;
	}

	MTPDinputPeerChat &_inputPeerChat() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputPeerChat) throw mtpErrorWrongTypeId(_type, mtpc_inputPeerChat);
		split();
		return *(MTPDinputPeerChat*)data;
	}
	const MTPDinputPeerChat &c_inputPeerChat() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputPeerChat) throw mtpErrorWrongTypeId(_type, mtpc_inputPeerChat);
		return *(const MTPDinputPeerChat*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPinputPeer(mtpTypeId type);
	explicit MTPinputPeer(MTPDinputPeerContact *_data);
	explicit MTPinputPeer(MTPDinputPeerForeign *_data);
	explicit MTPinputPeer(MTPDinputPeerChat *_data);

	friend MTPinputPeer MTP_inputPeerEmpty();
	friend MTPinputPeer MTP_inputPeerSelf();
	friend MTPinputPeer MTP_inputPeerContact(MTPint _user_id);
	friend MTPinputPeer MTP_inputPeerForeign(MTPint _user_id, const MTPlong &_access_hash);
	friend MTPinputPeer MTP_inputPeerChat(MTPint _chat_id);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPinputPeer> MTPInputPeer;

class MTPinputUser : private mtpDataOwner {
public:
	MTPinputUser() : mtpDataOwner(0), _type(0) {
	}
	MTPinputUser(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDinputUserContact &_inputUserContact() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputUserContact) throw mtpErrorWrongTypeId(_type, mtpc_inputUserContact);
		split();
		return *(MTPDinputUserContact*)data;
	}
	const MTPDinputUserContact &c_inputUserContact() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputUserContact) throw mtpErrorWrongTypeId(_type, mtpc_inputUserContact);
		return *(const MTPDinputUserContact*)data;
	}

	MTPDinputUserForeign &_inputUserForeign() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputUserForeign) throw mtpErrorWrongTypeId(_type, mtpc_inputUserForeign);
		split();
		return *(MTPDinputUserForeign*)data;
	}
	const MTPDinputUserForeign &c_inputUserForeign() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputUserForeign) throw mtpErrorWrongTypeId(_type, mtpc_inputUserForeign);
		return *(const MTPDinputUserForeign*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPinputUser(mtpTypeId type);
	explicit MTPinputUser(MTPDinputUserContact *_data);
	explicit MTPinputUser(MTPDinputUserForeign *_data);

	friend MTPinputUser MTP_inputUserEmpty();
	friend MTPinputUser MTP_inputUserSelf();
	friend MTPinputUser MTP_inputUserContact(MTPint _user_id);
	friend MTPinputUser MTP_inputUserForeign(MTPint _user_id, const MTPlong &_access_hash);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPinputUser> MTPInputUser;

class MTPinputContact : private mtpDataOwner {
public:
	MTPinputContact();
	MTPinputContact(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_inputPhoneContact) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDinputPhoneContact &_inputPhoneContact() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDinputPhoneContact*)data;
	}
	const MTPDinputPhoneContact &c_inputPhoneContact() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDinputPhoneContact*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_inputPhoneContact);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPinputContact(MTPDinputPhoneContact *_data);

	friend MTPinputContact MTP_inputPhoneContact(const MTPlong &_client_id, const MTPstring &_phone, const MTPstring &_first_name, const MTPstring &_last_name);
};
typedef MTPBoxed<MTPinputContact> MTPInputContact;

class MTPinputFile : private mtpDataOwner {
public:
	MTPinputFile() : mtpDataOwner(0), _type(0) {
	}
	MTPinputFile(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDinputFile &_inputFile() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputFile) throw mtpErrorWrongTypeId(_type, mtpc_inputFile);
		split();
		return *(MTPDinputFile*)data;
	}
	const MTPDinputFile &c_inputFile() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputFile) throw mtpErrorWrongTypeId(_type, mtpc_inputFile);
		return *(const MTPDinputFile*)data;
	}

	MTPDinputFileBig &_inputFileBig() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputFileBig) throw mtpErrorWrongTypeId(_type, mtpc_inputFileBig);
		split();
		return *(MTPDinputFileBig*)data;
	}
	const MTPDinputFileBig &c_inputFileBig() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputFileBig) throw mtpErrorWrongTypeId(_type, mtpc_inputFileBig);
		return *(const MTPDinputFileBig*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPinputFile(mtpTypeId type);
	explicit MTPinputFile(MTPDinputFile *_data);
	explicit MTPinputFile(MTPDinputFileBig *_data);

	friend MTPinputFile MTP_inputFile(const MTPlong &_id, MTPint _parts, const MTPstring &_name, const MTPstring &_md5_checksum);
	friend MTPinputFile MTP_inputFileBig(const MTPlong &_id, MTPint _parts, const MTPstring &_name);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPinputFile> MTPInputFile;

class MTPinputMedia : private mtpDataOwner {
public:
	MTPinputMedia() : mtpDataOwner(0), _type(0) {
	}
	MTPinputMedia(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDinputMediaUploadedPhoto &_inputMediaUploadedPhoto() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputMediaUploadedPhoto) throw mtpErrorWrongTypeId(_type, mtpc_inputMediaUploadedPhoto);
		split();
		return *(MTPDinputMediaUploadedPhoto*)data;
	}
	const MTPDinputMediaUploadedPhoto &c_inputMediaUploadedPhoto() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputMediaUploadedPhoto) throw mtpErrorWrongTypeId(_type, mtpc_inputMediaUploadedPhoto);
		return *(const MTPDinputMediaUploadedPhoto*)data;
	}

	MTPDinputMediaPhoto &_inputMediaPhoto() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputMediaPhoto) throw mtpErrorWrongTypeId(_type, mtpc_inputMediaPhoto);
		split();
		return *(MTPDinputMediaPhoto*)data;
	}
	const MTPDinputMediaPhoto &c_inputMediaPhoto() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputMediaPhoto) throw mtpErrorWrongTypeId(_type, mtpc_inputMediaPhoto);
		return *(const MTPDinputMediaPhoto*)data;
	}

	MTPDinputMediaGeoPoint &_inputMediaGeoPoint() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputMediaGeoPoint) throw mtpErrorWrongTypeId(_type, mtpc_inputMediaGeoPoint);
		split();
		return *(MTPDinputMediaGeoPoint*)data;
	}
	const MTPDinputMediaGeoPoint &c_inputMediaGeoPoint() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputMediaGeoPoint) throw mtpErrorWrongTypeId(_type, mtpc_inputMediaGeoPoint);
		return *(const MTPDinputMediaGeoPoint*)data;
	}

	MTPDinputMediaContact &_inputMediaContact() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputMediaContact) throw mtpErrorWrongTypeId(_type, mtpc_inputMediaContact);
		split();
		return *(MTPDinputMediaContact*)data;
	}
	const MTPDinputMediaContact &c_inputMediaContact() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputMediaContact) throw mtpErrorWrongTypeId(_type, mtpc_inputMediaContact);
		return *(const MTPDinputMediaContact*)data;
	}

	MTPDinputMediaUploadedVideo &_inputMediaUploadedVideo() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputMediaUploadedVideo) throw mtpErrorWrongTypeId(_type, mtpc_inputMediaUploadedVideo);
		split();
		return *(MTPDinputMediaUploadedVideo*)data;
	}
	const MTPDinputMediaUploadedVideo &c_inputMediaUploadedVideo() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputMediaUploadedVideo) throw mtpErrorWrongTypeId(_type, mtpc_inputMediaUploadedVideo);
		return *(const MTPDinputMediaUploadedVideo*)data;
	}

	MTPDinputMediaUploadedThumbVideo &_inputMediaUploadedThumbVideo() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputMediaUploadedThumbVideo) throw mtpErrorWrongTypeId(_type, mtpc_inputMediaUploadedThumbVideo);
		split();
		return *(MTPDinputMediaUploadedThumbVideo*)data;
	}
	const MTPDinputMediaUploadedThumbVideo &c_inputMediaUploadedThumbVideo() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputMediaUploadedThumbVideo) throw mtpErrorWrongTypeId(_type, mtpc_inputMediaUploadedThumbVideo);
		return *(const MTPDinputMediaUploadedThumbVideo*)data;
	}

	MTPDinputMediaVideo &_inputMediaVideo() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputMediaVideo) throw mtpErrorWrongTypeId(_type, mtpc_inputMediaVideo);
		split();
		return *(MTPDinputMediaVideo*)data;
	}
	const MTPDinputMediaVideo &c_inputMediaVideo() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputMediaVideo) throw mtpErrorWrongTypeId(_type, mtpc_inputMediaVideo);
		return *(const MTPDinputMediaVideo*)data;
	}

	MTPDinputMediaUploadedAudio &_inputMediaUploadedAudio() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputMediaUploadedAudio) throw mtpErrorWrongTypeId(_type, mtpc_inputMediaUploadedAudio);
		split();
		return *(MTPDinputMediaUploadedAudio*)data;
	}
	const MTPDinputMediaUploadedAudio &c_inputMediaUploadedAudio() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputMediaUploadedAudio) throw mtpErrorWrongTypeId(_type, mtpc_inputMediaUploadedAudio);
		return *(const MTPDinputMediaUploadedAudio*)data;
	}

	MTPDinputMediaAudio &_inputMediaAudio() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputMediaAudio) throw mtpErrorWrongTypeId(_type, mtpc_inputMediaAudio);
		split();
		return *(MTPDinputMediaAudio*)data;
	}
	const MTPDinputMediaAudio &c_inputMediaAudio() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputMediaAudio) throw mtpErrorWrongTypeId(_type, mtpc_inputMediaAudio);
		return *(const MTPDinputMediaAudio*)data;
	}

	MTPDinputMediaUploadedDocument &_inputMediaUploadedDocument() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputMediaUploadedDocument) throw mtpErrorWrongTypeId(_type, mtpc_inputMediaUploadedDocument);
		split();
		return *(MTPDinputMediaUploadedDocument*)data;
	}
	const MTPDinputMediaUploadedDocument &c_inputMediaUploadedDocument() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputMediaUploadedDocument) throw mtpErrorWrongTypeId(_type, mtpc_inputMediaUploadedDocument);
		return *(const MTPDinputMediaUploadedDocument*)data;
	}

	MTPDinputMediaUploadedThumbDocument &_inputMediaUploadedThumbDocument() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputMediaUploadedThumbDocument) throw mtpErrorWrongTypeId(_type, mtpc_inputMediaUploadedThumbDocument);
		split();
		return *(MTPDinputMediaUploadedThumbDocument*)data;
	}
	const MTPDinputMediaUploadedThumbDocument &c_inputMediaUploadedThumbDocument() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputMediaUploadedThumbDocument) throw mtpErrorWrongTypeId(_type, mtpc_inputMediaUploadedThumbDocument);
		return *(const MTPDinputMediaUploadedThumbDocument*)data;
	}

	MTPDinputMediaDocument &_inputMediaDocument() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputMediaDocument) throw mtpErrorWrongTypeId(_type, mtpc_inputMediaDocument);
		split();
		return *(MTPDinputMediaDocument*)data;
	}
	const MTPDinputMediaDocument &c_inputMediaDocument() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputMediaDocument) throw mtpErrorWrongTypeId(_type, mtpc_inputMediaDocument);
		return *(const MTPDinputMediaDocument*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPinputMedia(mtpTypeId type);
	explicit MTPinputMedia(MTPDinputMediaUploadedPhoto *_data);
	explicit MTPinputMedia(MTPDinputMediaPhoto *_data);
	explicit MTPinputMedia(MTPDinputMediaGeoPoint *_data);
	explicit MTPinputMedia(MTPDinputMediaContact *_data);
	explicit MTPinputMedia(MTPDinputMediaUploadedVideo *_data);
	explicit MTPinputMedia(MTPDinputMediaUploadedThumbVideo *_data);
	explicit MTPinputMedia(MTPDinputMediaVideo *_data);
	explicit MTPinputMedia(MTPDinputMediaUploadedAudio *_data);
	explicit MTPinputMedia(MTPDinputMediaAudio *_data);
	explicit MTPinputMedia(MTPDinputMediaUploadedDocument *_data);
	explicit MTPinputMedia(MTPDinputMediaUploadedThumbDocument *_data);
	explicit MTPinputMedia(MTPDinputMediaDocument *_data);

	friend MTPinputMedia MTP_inputMediaEmpty();
	friend MTPinputMedia MTP_inputMediaUploadedPhoto(const MTPInputFile &_file);
	friend MTPinputMedia MTP_inputMediaPhoto(const MTPInputPhoto &_id);
	friend MTPinputMedia MTP_inputMediaGeoPoint(const MTPInputGeoPoint &_geo_point);
	friend MTPinputMedia MTP_inputMediaContact(const MTPstring &_phone_number, const MTPstring &_first_name, const MTPstring &_last_name);
	friend MTPinputMedia MTP_inputMediaUploadedVideo(const MTPInputFile &_file, MTPint _duration, MTPint _w, MTPint _h, const MTPstring &_mime_type);
	friend MTPinputMedia MTP_inputMediaUploadedThumbVideo(const MTPInputFile &_file, const MTPInputFile &_thumb, MTPint _duration, MTPint _w, MTPint _h, const MTPstring &_mime_type);
	friend MTPinputMedia MTP_inputMediaVideo(const MTPInputVideo &_id);
	friend MTPinputMedia MTP_inputMediaUploadedAudio(const MTPInputFile &_file, MTPint _duration, const MTPstring &_mime_type);
	friend MTPinputMedia MTP_inputMediaAudio(const MTPInputAudio &_id);
	friend MTPinputMedia MTP_inputMediaUploadedDocument(const MTPInputFile &_file, const MTPstring &_file_name, const MTPstring &_mime_type);
	friend MTPinputMedia MTP_inputMediaUploadedThumbDocument(const MTPInputFile &_file, const MTPInputFile &_thumb, const MTPstring &_file_name, const MTPstring &_mime_type);
	friend MTPinputMedia MTP_inputMediaDocument(const MTPInputDocument &_id);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPinputMedia> MTPInputMedia;

class MTPinputChatPhoto : private mtpDataOwner {
public:
	MTPinputChatPhoto() : mtpDataOwner(0), _type(0) {
	}
	MTPinputChatPhoto(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDinputChatUploadedPhoto &_inputChatUploadedPhoto() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputChatUploadedPhoto) throw mtpErrorWrongTypeId(_type, mtpc_inputChatUploadedPhoto);
		split();
		return *(MTPDinputChatUploadedPhoto*)data;
	}
	const MTPDinputChatUploadedPhoto &c_inputChatUploadedPhoto() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputChatUploadedPhoto) throw mtpErrorWrongTypeId(_type, mtpc_inputChatUploadedPhoto);
		return *(const MTPDinputChatUploadedPhoto*)data;
	}

	MTPDinputChatPhoto &_inputChatPhoto() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputChatPhoto) throw mtpErrorWrongTypeId(_type, mtpc_inputChatPhoto);
		split();
		return *(MTPDinputChatPhoto*)data;
	}
	const MTPDinputChatPhoto &c_inputChatPhoto() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputChatPhoto) throw mtpErrorWrongTypeId(_type, mtpc_inputChatPhoto);
		return *(const MTPDinputChatPhoto*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPinputChatPhoto(mtpTypeId type);
	explicit MTPinputChatPhoto(MTPDinputChatUploadedPhoto *_data);
	explicit MTPinputChatPhoto(MTPDinputChatPhoto *_data);

	friend MTPinputChatPhoto MTP_inputChatPhotoEmpty();
	friend MTPinputChatPhoto MTP_inputChatUploadedPhoto(const MTPInputFile &_file, const MTPInputPhotoCrop &_crop);
	friend MTPinputChatPhoto MTP_inputChatPhoto(const MTPInputPhoto &_id, const MTPInputPhotoCrop &_crop);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPinputChatPhoto> MTPInputChatPhoto;

class MTPinputGeoPoint : private mtpDataOwner {
public:
	MTPinputGeoPoint() : mtpDataOwner(0), _type(0) {
	}
	MTPinputGeoPoint(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDinputGeoPoint &_inputGeoPoint() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputGeoPoint) throw mtpErrorWrongTypeId(_type, mtpc_inputGeoPoint);
		split();
		return *(MTPDinputGeoPoint*)data;
	}
	const MTPDinputGeoPoint &c_inputGeoPoint() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputGeoPoint) throw mtpErrorWrongTypeId(_type, mtpc_inputGeoPoint);
		return *(const MTPDinputGeoPoint*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPinputGeoPoint(mtpTypeId type);
	explicit MTPinputGeoPoint(MTPDinputGeoPoint *_data);

	friend MTPinputGeoPoint MTP_inputGeoPointEmpty();
	friend MTPinputGeoPoint MTP_inputGeoPoint(const MTPdouble &_lat, const MTPdouble &_long);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPinputGeoPoint> MTPInputGeoPoint;

class MTPinputPhoto : private mtpDataOwner {
public:
	MTPinputPhoto() : mtpDataOwner(0), _type(0) {
	}
	MTPinputPhoto(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDinputPhoto &_inputPhoto() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputPhoto) throw mtpErrorWrongTypeId(_type, mtpc_inputPhoto);
		split();
		return *(MTPDinputPhoto*)data;
	}
	const MTPDinputPhoto &c_inputPhoto() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputPhoto) throw mtpErrorWrongTypeId(_type, mtpc_inputPhoto);
		return *(const MTPDinputPhoto*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPinputPhoto(mtpTypeId type);
	explicit MTPinputPhoto(MTPDinputPhoto *_data);

	friend MTPinputPhoto MTP_inputPhotoEmpty();
	friend MTPinputPhoto MTP_inputPhoto(const MTPlong &_id, const MTPlong &_access_hash);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPinputPhoto> MTPInputPhoto;

class MTPinputVideo : private mtpDataOwner {
public:
	MTPinputVideo() : mtpDataOwner(0), _type(0) {
	}
	MTPinputVideo(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDinputVideo &_inputVideo() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputVideo) throw mtpErrorWrongTypeId(_type, mtpc_inputVideo);
		split();
		return *(MTPDinputVideo*)data;
	}
	const MTPDinputVideo &c_inputVideo() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputVideo) throw mtpErrorWrongTypeId(_type, mtpc_inputVideo);
		return *(const MTPDinputVideo*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPinputVideo(mtpTypeId type);
	explicit MTPinputVideo(MTPDinputVideo *_data);

	friend MTPinputVideo MTP_inputVideoEmpty();
	friend MTPinputVideo MTP_inputVideo(const MTPlong &_id, const MTPlong &_access_hash);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPinputVideo> MTPInputVideo;

class MTPinputFileLocation : private mtpDataOwner {
public:
	MTPinputFileLocation() : mtpDataOwner(0), _type(0) {
	}
	MTPinputFileLocation(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDinputFileLocation &_inputFileLocation() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputFileLocation) throw mtpErrorWrongTypeId(_type, mtpc_inputFileLocation);
		split();
		return *(MTPDinputFileLocation*)data;
	}
	const MTPDinputFileLocation &c_inputFileLocation() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputFileLocation) throw mtpErrorWrongTypeId(_type, mtpc_inputFileLocation);
		return *(const MTPDinputFileLocation*)data;
	}

	MTPDinputVideoFileLocation &_inputVideoFileLocation() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputVideoFileLocation) throw mtpErrorWrongTypeId(_type, mtpc_inputVideoFileLocation);
		split();
		return *(MTPDinputVideoFileLocation*)data;
	}
	const MTPDinputVideoFileLocation &c_inputVideoFileLocation() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputVideoFileLocation) throw mtpErrorWrongTypeId(_type, mtpc_inputVideoFileLocation);
		return *(const MTPDinputVideoFileLocation*)data;
	}

	MTPDinputEncryptedFileLocation &_inputEncryptedFileLocation() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputEncryptedFileLocation) throw mtpErrorWrongTypeId(_type, mtpc_inputEncryptedFileLocation);
		split();
		return *(MTPDinputEncryptedFileLocation*)data;
	}
	const MTPDinputEncryptedFileLocation &c_inputEncryptedFileLocation() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputEncryptedFileLocation) throw mtpErrorWrongTypeId(_type, mtpc_inputEncryptedFileLocation);
		return *(const MTPDinputEncryptedFileLocation*)data;
	}

	MTPDinputAudioFileLocation &_inputAudioFileLocation() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputAudioFileLocation) throw mtpErrorWrongTypeId(_type, mtpc_inputAudioFileLocation);
		split();
		return *(MTPDinputAudioFileLocation*)data;
	}
	const MTPDinputAudioFileLocation &c_inputAudioFileLocation() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputAudioFileLocation) throw mtpErrorWrongTypeId(_type, mtpc_inputAudioFileLocation);
		return *(const MTPDinputAudioFileLocation*)data;
	}

	MTPDinputDocumentFileLocation &_inputDocumentFileLocation() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputDocumentFileLocation) throw mtpErrorWrongTypeId(_type, mtpc_inputDocumentFileLocation);
		split();
		return *(MTPDinputDocumentFileLocation*)data;
	}
	const MTPDinputDocumentFileLocation &c_inputDocumentFileLocation() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputDocumentFileLocation) throw mtpErrorWrongTypeId(_type, mtpc_inputDocumentFileLocation);
		return *(const MTPDinputDocumentFileLocation*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPinputFileLocation(mtpTypeId type);
	explicit MTPinputFileLocation(MTPDinputFileLocation *_data);
	explicit MTPinputFileLocation(MTPDinputVideoFileLocation *_data);
	explicit MTPinputFileLocation(MTPDinputEncryptedFileLocation *_data);
	explicit MTPinputFileLocation(MTPDinputAudioFileLocation *_data);
	explicit MTPinputFileLocation(MTPDinputDocumentFileLocation *_data);

	friend MTPinputFileLocation MTP_inputFileLocation(const MTPlong &_volume_id, MTPint _local_id, const MTPlong &_secret);
	friend MTPinputFileLocation MTP_inputVideoFileLocation(const MTPlong &_id, const MTPlong &_access_hash);
	friend MTPinputFileLocation MTP_inputEncryptedFileLocation(const MTPlong &_id, const MTPlong &_access_hash);
	friend MTPinputFileLocation MTP_inputAudioFileLocation(const MTPlong &_id, const MTPlong &_access_hash);
	friend MTPinputFileLocation MTP_inputDocumentFileLocation(const MTPlong &_id, const MTPlong &_access_hash);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPinputFileLocation> MTPInputFileLocation;

class MTPinputPhotoCrop : private mtpDataOwner {
public:
	MTPinputPhotoCrop() : mtpDataOwner(0), _type(0) {
	}
	MTPinputPhotoCrop(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDinputPhotoCrop &_inputPhotoCrop() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputPhotoCrop) throw mtpErrorWrongTypeId(_type, mtpc_inputPhotoCrop);
		split();
		return *(MTPDinputPhotoCrop*)data;
	}
	const MTPDinputPhotoCrop &c_inputPhotoCrop() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputPhotoCrop) throw mtpErrorWrongTypeId(_type, mtpc_inputPhotoCrop);
		return *(const MTPDinputPhotoCrop*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPinputPhotoCrop(mtpTypeId type);
	explicit MTPinputPhotoCrop(MTPDinputPhotoCrop *_data);

	friend MTPinputPhotoCrop MTP_inputPhotoCropAuto();
	friend MTPinputPhotoCrop MTP_inputPhotoCrop(const MTPdouble &_crop_left, const MTPdouble &_crop_top, const MTPdouble &_crop_width);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPinputPhotoCrop> MTPInputPhotoCrop;

class MTPinputAppEvent : private mtpDataOwner {
public:
	MTPinputAppEvent();
	MTPinputAppEvent(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_inputAppEvent) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDinputAppEvent &_inputAppEvent() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDinputAppEvent*)data;
	}
	const MTPDinputAppEvent &c_inputAppEvent() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDinputAppEvent*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_inputAppEvent);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPinputAppEvent(MTPDinputAppEvent *_data);

	friend MTPinputAppEvent MTP_inputAppEvent(const MTPdouble &_time, const MTPstring &_type, const MTPlong &_peer, const MTPstring &_data);
};
typedef MTPBoxed<MTPinputAppEvent> MTPInputAppEvent;

class MTPpeer : private mtpDataOwner {
public:
	MTPpeer() : mtpDataOwner(0), _type(0) {
	}
	MTPpeer(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDpeerUser &_peerUser() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_peerUser) throw mtpErrorWrongTypeId(_type, mtpc_peerUser);
		split();
		return *(MTPDpeerUser*)data;
	}
	const MTPDpeerUser &c_peerUser() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_peerUser) throw mtpErrorWrongTypeId(_type, mtpc_peerUser);
		return *(const MTPDpeerUser*)data;
	}

	MTPDpeerChat &_peerChat() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_peerChat) throw mtpErrorWrongTypeId(_type, mtpc_peerChat);
		split();
		return *(MTPDpeerChat*)data;
	}
	const MTPDpeerChat &c_peerChat() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_peerChat) throw mtpErrorWrongTypeId(_type, mtpc_peerChat);
		return *(const MTPDpeerChat*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPpeer(mtpTypeId type);
	explicit MTPpeer(MTPDpeerUser *_data);
	explicit MTPpeer(MTPDpeerChat *_data);

	friend MTPpeer MTP_peerUser(MTPint _user_id);
	friend MTPpeer MTP_peerChat(MTPint _chat_id);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPpeer> MTPPeer;

class MTPstorage_fileType {
public:
	MTPstorage_fileType() : _type(0) {
	}
	MTPstorage_fileType(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : _type(0) {
		read(from, end, cons);
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPstorage_fileType(mtpTypeId type);

	friend MTPstorage_fileType MTP_storage_fileUnknown();
	friend MTPstorage_fileType MTP_storage_fileJpeg();
	friend MTPstorage_fileType MTP_storage_fileGif();
	friend MTPstorage_fileType MTP_storage_filePng();
	friend MTPstorage_fileType MTP_storage_filePdf();
	friend MTPstorage_fileType MTP_storage_fileMp3();
	friend MTPstorage_fileType MTP_storage_fileMov();
	friend MTPstorage_fileType MTP_storage_filePartial();
	friend MTPstorage_fileType MTP_storage_fileMp4();
	friend MTPstorage_fileType MTP_storage_fileWebp();

	mtpTypeId _type;
};
typedef MTPBoxed<MTPstorage_fileType> MTPstorage_FileType;

class MTPfileLocation : private mtpDataOwner {
public:
	MTPfileLocation() : mtpDataOwner(0), _type(0) {
	}
	MTPfileLocation(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDfileLocationUnavailable &_fileLocationUnavailable() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_fileLocationUnavailable) throw mtpErrorWrongTypeId(_type, mtpc_fileLocationUnavailable);
		split();
		return *(MTPDfileLocationUnavailable*)data;
	}
	const MTPDfileLocationUnavailable &c_fileLocationUnavailable() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_fileLocationUnavailable) throw mtpErrorWrongTypeId(_type, mtpc_fileLocationUnavailable);
		return *(const MTPDfileLocationUnavailable*)data;
	}

	MTPDfileLocation &_fileLocation() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_fileLocation) throw mtpErrorWrongTypeId(_type, mtpc_fileLocation);
		split();
		return *(MTPDfileLocation*)data;
	}
	const MTPDfileLocation &c_fileLocation() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_fileLocation) throw mtpErrorWrongTypeId(_type, mtpc_fileLocation);
		return *(const MTPDfileLocation*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPfileLocation(mtpTypeId type);
	explicit MTPfileLocation(MTPDfileLocationUnavailable *_data);
	explicit MTPfileLocation(MTPDfileLocation *_data);

	friend MTPfileLocation MTP_fileLocationUnavailable(const MTPlong &_volume_id, MTPint _local_id, const MTPlong &_secret);
	friend MTPfileLocation MTP_fileLocation(MTPint _dc_id, const MTPlong &_volume_id, MTPint _local_id, const MTPlong &_secret);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPfileLocation> MTPFileLocation;

class MTPuser : private mtpDataOwner {
public:
	MTPuser() : mtpDataOwner(0), _type(0) {
	}
	MTPuser(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDuserEmpty &_userEmpty() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_userEmpty) throw mtpErrorWrongTypeId(_type, mtpc_userEmpty);
		split();
		return *(MTPDuserEmpty*)data;
	}
	const MTPDuserEmpty &c_userEmpty() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_userEmpty) throw mtpErrorWrongTypeId(_type, mtpc_userEmpty);
		return *(const MTPDuserEmpty*)data;
	}

	MTPDuserSelf &_userSelf() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_userSelf) throw mtpErrorWrongTypeId(_type, mtpc_userSelf);
		split();
		return *(MTPDuserSelf*)data;
	}
	const MTPDuserSelf &c_userSelf() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_userSelf) throw mtpErrorWrongTypeId(_type, mtpc_userSelf);
		return *(const MTPDuserSelf*)data;
	}

	MTPDuserContact &_userContact() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_userContact) throw mtpErrorWrongTypeId(_type, mtpc_userContact);
		split();
		return *(MTPDuserContact*)data;
	}
	const MTPDuserContact &c_userContact() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_userContact) throw mtpErrorWrongTypeId(_type, mtpc_userContact);
		return *(const MTPDuserContact*)data;
	}

	MTPDuserRequest &_userRequest() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_userRequest) throw mtpErrorWrongTypeId(_type, mtpc_userRequest);
		split();
		return *(MTPDuserRequest*)data;
	}
	const MTPDuserRequest &c_userRequest() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_userRequest) throw mtpErrorWrongTypeId(_type, mtpc_userRequest);
		return *(const MTPDuserRequest*)data;
	}

	MTPDuserForeign &_userForeign() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_userForeign) throw mtpErrorWrongTypeId(_type, mtpc_userForeign);
		split();
		return *(MTPDuserForeign*)data;
	}
	const MTPDuserForeign &c_userForeign() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_userForeign) throw mtpErrorWrongTypeId(_type, mtpc_userForeign);
		return *(const MTPDuserForeign*)data;
	}

	MTPDuserDeleted &_userDeleted() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_userDeleted) throw mtpErrorWrongTypeId(_type, mtpc_userDeleted);
		split();
		return *(MTPDuserDeleted*)data;
	}
	const MTPDuserDeleted &c_userDeleted() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_userDeleted) throw mtpErrorWrongTypeId(_type, mtpc_userDeleted);
		return *(const MTPDuserDeleted*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPuser(mtpTypeId type);
	explicit MTPuser(MTPDuserEmpty *_data);
	explicit MTPuser(MTPDuserSelf *_data);
	explicit MTPuser(MTPDuserContact *_data);
	explicit MTPuser(MTPDuserRequest *_data);
	explicit MTPuser(MTPDuserForeign *_data);
	explicit MTPuser(MTPDuserDeleted *_data);

	friend MTPuser MTP_userEmpty(MTPint _id);
	friend MTPuser MTP_userSelf(MTPint _id, const MTPstring &_first_name, const MTPstring &_last_name, const MTPstring &_phone, const MTPUserProfilePhoto &_photo, const MTPUserStatus &_status, MTPBool _inactive);
	friend MTPuser MTP_userContact(MTPint _id, const MTPstring &_first_name, const MTPstring &_last_name, const MTPlong &_access_hash, const MTPstring &_phone, const MTPUserProfilePhoto &_photo, const MTPUserStatus &_status);
	friend MTPuser MTP_userRequest(MTPint _id, const MTPstring &_first_name, const MTPstring &_last_name, const MTPlong &_access_hash, const MTPstring &_phone, const MTPUserProfilePhoto &_photo, const MTPUserStatus &_status);
	friend MTPuser MTP_userForeign(MTPint _id, const MTPstring &_first_name, const MTPstring &_last_name, const MTPlong &_access_hash, const MTPUserProfilePhoto &_photo, const MTPUserStatus &_status);
	friend MTPuser MTP_userDeleted(MTPint _id, const MTPstring &_first_name, const MTPstring &_last_name);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPuser> MTPUser;

class MTPuserProfilePhoto : private mtpDataOwner {
public:
	MTPuserProfilePhoto() : mtpDataOwner(0), _type(0) {
	}
	MTPuserProfilePhoto(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDuserProfilePhoto &_userProfilePhoto() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_userProfilePhoto) throw mtpErrorWrongTypeId(_type, mtpc_userProfilePhoto);
		split();
		return *(MTPDuserProfilePhoto*)data;
	}
	const MTPDuserProfilePhoto &c_userProfilePhoto() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_userProfilePhoto) throw mtpErrorWrongTypeId(_type, mtpc_userProfilePhoto);
		return *(const MTPDuserProfilePhoto*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPuserProfilePhoto(mtpTypeId type);
	explicit MTPuserProfilePhoto(MTPDuserProfilePhoto *_data);

	friend MTPuserProfilePhoto MTP_userProfilePhotoEmpty();
	friend MTPuserProfilePhoto MTP_userProfilePhoto(const MTPlong &_photo_id, const MTPFileLocation &_photo_small, const MTPFileLocation &_photo_big);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPuserProfilePhoto> MTPUserProfilePhoto;

class MTPuserStatus : private mtpDataOwner {
public:
	MTPuserStatus() : mtpDataOwner(0), _type(0) {
	}
	MTPuserStatus(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDuserStatusOnline &_userStatusOnline() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_userStatusOnline) throw mtpErrorWrongTypeId(_type, mtpc_userStatusOnline);
		split();
		return *(MTPDuserStatusOnline*)data;
	}
	const MTPDuserStatusOnline &c_userStatusOnline() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_userStatusOnline) throw mtpErrorWrongTypeId(_type, mtpc_userStatusOnline);
		return *(const MTPDuserStatusOnline*)data;
	}

	MTPDuserStatusOffline &_userStatusOffline() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_userStatusOffline) throw mtpErrorWrongTypeId(_type, mtpc_userStatusOffline);
		split();
		return *(MTPDuserStatusOffline*)data;
	}
	const MTPDuserStatusOffline &c_userStatusOffline() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_userStatusOffline) throw mtpErrorWrongTypeId(_type, mtpc_userStatusOffline);
		return *(const MTPDuserStatusOffline*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPuserStatus(mtpTypeId type);
	explicit MTPuserStatus(MTPDuserStatusOnline *_data);
	explicit MTPuserStatus(MTPDuserStatusOffline *_data);

	friend MTPuserStatus MTP_userStatusEmpty();
	friend MTPuserStatus MTP_userStatusOnline(MTPint _expires);
	friend MTPuserStatus MTP_userStatusOffline(MTPint _was_online);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPuserStatus> MTPUserStatus;

class MTPchat : private mtpDataOwner {
public:
	MTPchat() : mtpDataOwner(0), _type(0) {
	}
	MTPchat(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDchatEmpty &_chatEmpty() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_chatEmpty) throw mtpErrorWrongTypeId(_type, mtpc_chatEmpty);
		split();
		return *(MTPDchatEmpty*)data;
	}
	const MTPDchatEmpty &c_chatEmpty() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_chatEmpty) throw mtpErrorWrongTypeId(_type, mtpc_chatEmpty);
		return *(const MTPDchatEmpty*)data;
	}

	MTPDchat &_chat() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_chat) throw mtpErrorWrongTypeId(_type, mtpc_chat);
		split();
		return *(MTPDchat*)data;
	}
	const MTPDchat &c_chat() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_chat) throw mtpErrorWrongTypeId(_type, mtpc_chat);
		return *(const MTPDchat*)data;
	}

	MTPDchatForbidden &_chatForbidden() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_chatForbidden) throw mtpErrorWrongTypeId(_type, mtpc_chatForbidden);
		split();
		return *(MTPDchatForbidden*)data;
	}
	const MTPDchatForbidden &c_chatForbidden() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_chatForbidden) throw mtpErrorWrongTypeId(_type, mtpc_chatForbidden);
		return *(const MTPDchatForbidden*)data;
	}

	MTPDgeoChat &_geoChat() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_geoChat) throw mtpErrorWrongTypeId(_type, mtpc_geoChat);
		split();
		return *(MTPDgeoChat*)data;
	}
	const MTPDgeoChat &c_geoChat() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_geoChat) throw mtpErrorWrongTypeId(_type, mtpc_geoChat);
		return *(const MTPDgeoChat*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPchat(mtpTypeId type);
	explicit MTPchat(MTPDchatEmpty *_data);
	explicit MTPchat(MTPDchat *_data);
	explicit MTPchat(MTPDchatForbidden *_data);
	explicit MTPchat(MTPDgeoChat *_data);

	friend MTPchat MTP_chatEmpty(MTPint _id);
	friend MTPchat MTP_chat(MTPint _id, const MTPstring &_title, const MTPChatPhoto &_photo, MTPint _participants_count, MTPint _date, MTPBool _left, MTPint _version);
	friend MTPchat MTP_chatForbidden(MTPint _id, const MTPstring &_title, MTPint _date);
	friend MTPchat MTP_geoChat(MTPint _id, const MTPlong &_access_hash, const MTPstring &_title, const MTPstring &_address, const MTPstring &_venue, const MTPGeoPoint &_geo, const MTPChatPhoto &_photo, MTPint _participants_count, MTPint _date, MTPBool _checked_in, MTPint _version);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPchat> MTPChat;

class MTPchatFull : private mtpDataOwner {
public:
	MTPchatFull();
	MTPchatFull(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_chatFull) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDchatFull &_chatFull() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDchatFull*)data;
	}
	const MTPDchatFull &c_chatFull() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDchatFull*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_chatFull);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPchatFull(MTPDchatFull *_data);

	friend MTPchatFull MTP_chatFull(MTPint _id, const MTPChatParticipants &_participants, const MTPPhoto &_chat_photo, const MTPPeerNotifySettings &_notify_settings);
};
typedef MTPBoxed<MTPchatFull> MTPChatFull;

class MTPchatParticipant : private mtpDataOwner {
public:
	MTPchatParticipant();
	MTPchatParticipant(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_chatParticipant) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDchatParticipant &_chatParticipant() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDchatParticipant*)data;
	}
	const MTPDchatParticipant &c_chatParticipant() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDchatParticipant*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_chatParticipant);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPchatParticipant(MTPDchatParticipant *_data);

	friend MTPchatParticipant MTP_chatParticipant(MTPint _user_id, MTPint _inviter_id, MTPint _date);
};
typedef MTPBoxed<MTPchatParticipant> MTPChatParticipant;

class MTPchatParticipants : private mtpDataOwner {
public:
	MTPchatParticipants() : mtpDataOwner(0), _type(0) {
	}
	MTPchatParticipants(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDchatParticipantsForbidden &_chatParticipantsForbidden() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_chatParticipantsForbidden) throw mtpErrorWrongTypeId(_type, mtpc_chatParticipantsForbidden);
		split();
		return *(MTPDchatParticipantsForbidden*)data;
	}
	const MTPDchatParticipantsForbidden &c_chatParticipantsForbidden() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_chatParticipantsForbidden) throw mtpErrorWrongTypeId(_type, mtpc_chatParticipantsForbidden);
		return *(const MTPDchatParticipantsForbidden*)data;
	}

	MTPDchatParticipants &_chatParticipants() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_chatParticipants) throw mtpErrorWrongTypeId(_type, mtpc_chatParticipants);
		split();
		return *(MTPDchatParticipants*)data;
	}
	const MTPDchatParticipants &c_chatParticipants() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_chatParticipants) throw mtpErrorWrongTypeId(_type, mtpc_chatParticipants);
		return *(const MTPDchatParticipants*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPchatParticipants(mtpTypeId type);
	explicit MTPchatParticipants(MTPDchatParticipantsForbidden *_data);
	explicit MTPchatParticipants(MTPDchatParticipants *_data);

	friend MTPchatParticipants MTP_chatParticipantsForbidden(MTPint _chat_id);
	friend MTPchatParticipants MTP_chatParticipants(MTPint _chat_id, MTPint _admin_id, const MTPVector<MTPChatParticipant> &_participants, MTPint _version);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPchatParticipants> MTPChatParticipants;

class MTPchatPhoto : private mtpDataOwner {
public:
	MTPchatPhoto() : mtpDataOwner(0), _type(0) {
	}
	MTPchatPhoto(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDchatPhoto &_chatPhoto() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_chatPhoto) throw mtpErrorWrongTypeId(_type, mtpc_chatPhoto);
		split();
		return *(MTPDchatPhoto*)data;
	}
	const MTPDchatPhoto &c_chatPhoto() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_chatPhoto) throw mtpErrorWrongTypeId(_type, mtpc_chatPhoto);
		return *(const MTPDchatPhoto*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPchatPhoto(mtpTypeId type);
	explicit MTPchatPhoto(MTPDchatPhoto *_data);

	friend MTPchatPhoto MTP_chatPhotoEmpty();
	friend MTPchatPhoto MTP_chatPhoto(const MTPFileLocation &_photo_small, const MTPFileLocation &_photo_big);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPchatPhoto> MTPChatPhoto;

class MTPmessage : private mtpDataOwner {
public:
	MTPmessage() : mtpDataOwner(0), _type(0) {
	}
	MTPmessage(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDmessageEmpty &_messageEmpty() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageEmpty) throw mtpErrorWrongTypeId(_type, mtpc_messageEmpty);
		split();
		return *(MTPDmessageEmpty*)data;
	}
	const MTPDmessageEmpty &c_messageEmpty() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageEmpty) throw mtpErrorWrongTypeId(_type, mtpc_messageEmpty);
		return *(const MTPDmessageEmpty*)data;
	}

	MTPDmessage &_message() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_message) throw mtpErrorWrongTypeId(_type, mtpc_message);
		split();
		return *(MTPDmessage*)data;
	}
	const MTPDmessage &c_message() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_message) throw mtpErrorWrongTypeId(_type, mtpc_message);
		return *(const MTPDmessage*)data;
	}

	MTPDmessageForwarded &_messageForwarded() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageForwarded) throw mtpErrorWrongTypeId(_type, mtpc_messageForwarded);
		split();
		return *(MTPDmessageForwarded*)data;
	}
	const MTPDmessageForwarded &c_messageForwarded() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageForwarded) throw mtpErrorWrongTypeId(_type, mtpc_messageForwarded);
		return *(const MTPDmessageForwarded*)data;
	}

	MTPDmessageService &_messageService() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageService) throw mtpErrorWrongTypeId(_type, mtpc_messageService);
		split();
		return *(MTPDmessageService*)data;
	}
	const MTPDmessageService &c_messageService() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageService) throw mtpErrorWrongTypeId(_type, mtpc_messageService);
		return *(const MTPDmessageService*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPmessage(mtpTypeId type);
	explicit MTPmessage(MTPDmessageEmpty *_data);
	explicit MTPmessage(MTPDmessage *_data);
	explicit MTPmessage(MTPDmessageForwarded *_data);
	explicit MTPmessage(MTPDmessageService *_data);

	friend MTPmessage MTP_messageEmpty(MTPint _id);
	friend MTPmessage MTP_message(MTPint _id, MTPint _from_id, const MTPPeer &_to_id, MTPBool _out, MTPBool _unread, MTPint _date, const MTPstring &_message, const MTPMessageMedia &_media);
	friend MTPmessage MTP_messageForwarded(MTPint _id, MTPint _fwd_from_id, MTPint _fwd_date, MTPint _from_id, const MTPPeer &_to_id, MTPBool _out, MTPBool _unread, MTPint _date, const MTPstring &_message, const MTPMessageMedia &_media);
	friend MTPmessage MTP_messageService(MTPint _id, MTPint _from_id, const MTPPeer &_to_id, MTPBool _out, MTPBool _unread, MTPint _date, const MTPMessageAction &_action);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPmessage> MTPMessage;

class MTPmessageMedia : private mtpDataOwner {
public:
	MTPmessageMedia() : mtpDataOwner(0), _type(0) {
	}
	MTPmessageMedia(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDmessageMediaPhoto &_messageMediaPhoto() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageMediaPhoto) throw mtpErrorWrongTypeId(_type, mtpc_messageMediaPhoto);
		split();
		return *(MTPDmessageMediaPhoto*)data;
	}
	const MTPDmessageMediaPhoto &c_messageMediaPhoto() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageMediaPhoto) throw mtpErrorWrongTypeId(_type, mtpc_messageMediaPhoto);
		return *(const MTPDmessageMediaPhoto*)data;
	}

	MTPDmessageMediaVideo &_messageMediaVideo() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageMediaVideo) throw mtpErrorWrongTypeId(_type, mtpc_messageMediaVideo);
		split();
		return *(MTPDmessageMediaVideo*)data;
	}
	const MTPDmessageMediaVideo &c_messageMediaVideo() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageMediaVideo) throw mtpErrorWrongTypeId(_type, mtpc_messageMediaVideo);
		return *(const MTPDmessageMediaVideo*)data;
	}

	MTPDmessageMediaGeo &_messageMediaGeo() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageMediaGeo) throw mtpErrorWrongTypeId(_type, mtpc_messageMediaGeo);
		split();
		return *(MTPDmessageMediaGeo*)data;
	}
	const MTPDmessageMediaGeo &c_messageMediaGeo() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageMediaGeo) throw mtpErrorWrongTypeId(_type, mtpc_messageMediaGeo);
		return *(const MTPDmessageMediaGeo*)data;
	}

	MTPDmessageMediaContact &_messageMediaContact() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageMediaContact) throw mtpErrorWrongTypeId(_type, mtpc_messageMediaContact);
		split();
		return *(MTPDmessageMediaContact*)data;
	}
	const MTPDmessageMediaContact &c_messageMediaContact() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageMediaContact) throw mtpErrorWrongTypeId(_type, mtpc_messageMediaContact);
		return *(const MTPDmessageMediaContact*)data;
	}

	MTPDmessageMediaUnsupported &_messageMediaUnsupported() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageMediaUnsupported) throw mtpErrorWrongTypeId(_type, mtpc_messageMediaUnsupported);
		split();
		return *(MTPDmessageMediaUnsupported*)data;
	}
	const MTPDmessageMediaUnsupported &c_messageMediaUnsupported() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageMediaUnsupported) throw mtpErrorWrongTypeId(_type, mtpc_messageMediaUnsupported);
		return *(const MTPDmessageMediaUnsupported*)data;
	}

	MTPDmessageMediaDocument &_messageMediaDocument() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageMediaDocument) throw mtpErrorWrongTypeId(_type, mtpc_messageMediaDocument);
		split();
		return *(MTPDmessageMediaDocument*)data;
	}
	const MTPDmessageMediaDocument &c_messageMediaDocument() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageMediaDocument) throw mtpErrorWrongTypeId(_type, mtpc_messageMediaDocument);
		return *(const MTPDmessageMediaDocument*)data;
	}

	MTPDmessageMediaAudio &_messageMediaAudio() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageMediaAudio) throw mtpErrorWrongTypeId(_type, mtpc_messageMediaAudio);
		split();
		return *(MTPDmessageMediaAudio*)data;
	}
	const MTPDmessageMediaAudio &c_messageMediaAudio() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageMediaAudio) throw mtpErrorWrongTypeId(_type, mtpc_messageMediaAudio);
		return *(const MTPDmessageMediaAudio*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPmessageMedia(mtpTypeId type);
	explicit MTPmessageMedia(MTPDmessageMediaPhoto *_data);
	explicit MTPmessageMedia(MTPDmessageMediaVideo *_data);
	explicit MTPmessageMedia(MTPDmessageMediaGeo *_data);
	explicit MTPmessageMedia(MTPDmessageMediaContact *_data);
	explicit MTPmessageMedia(MTPDmessageMediaUnsupported *_data);
	explicit MTPmessageMedia(MTPDmessageMediaDocument *_data);
	explicit MTPmessageMedia(MTPDmessageMediaAudio *_data);

	friend MTPmessageMedia MTP_messageMediaEmpty();
	friend MTPmessageMedia MTP_messageMediaPhoto(const MTPPhoto &_photo);
	friend MTPmessageMedia MTP_messageMediaVideo(const MTPVideo &_video);
	friend MTPmessageMedia MTP_messageMediaGeo(const MTPGeoPoint &_geo);
	friend MTPmessageMedia MTP_messageMediaContact(const MTPstring &_phone_number, const MTPstring &_first_name, const MTPstring &_last_name, MTPint _user_id);
	friend MTPmessageMedia MTP_messageMediaUnsupported(const MTPbytes &_bytes);
	friend MTPmessageMedia MTP_messageMediaDocument(const MTPDocument &_document);
	friend MTPmessageMedia MTP_messageMediaAudio(const MTPAudio &_audio);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPmessageMedia> MTPMessageMedia;

class MTPmessageAction : private mtpDataOwner {
public:
	MTPmessageAction() : mtpDataOwner(0), _type(0) {
	}
	MTPmessageAction(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDmessageActionChatCreate &_messageActionChatCreate() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageActionChatCreate) throw mtpErrorWrongTypeId(_type, mtpc_messageActionChatCreate);
		split();
		return *(MTPDmessageActionChatCreate*)data;
	}
	const MTPDmessageActionChatCreate &c_messageActionChatCreate() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageActionChatCreate) throw mtpErrorWrongTypeId(_type, mtpc_messageActionChatCreate);
		return *(const MTPDmessageActionChatCreate*)data;
	}

	MTPDmessageActionChatEditTitle &_messageActionChatEditTitle() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageActionChatEditTitle) throw mtpErrorWrongTypeId(_type, mtpc_messageActionChatEditTitle);
		split();
		return *(MTPDmessageActionChatEditTitle*)data;
	}
	const MTPDmessageActionChatEditTitle &c_messageActionChatEditTitle() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageActionChatEditTitle) throw mtpErrorWrongTypeId(_type, mtpc_messageActionChatEditTitle);
		return *(const MTPDmessageActionChatEditTitle*)data;
	}

	MTPDmessageActionChatEditPhoto &_messageActionChatEditPhoto() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageActionChatEditPhoto) throw mtpErrorWrongTypeId(_type, mtpc_messageActionChatEditPhoto);
		split();
		return *(MTPDmessageActionChatEditPhoto*)data;
	}
	const MTPDmessageActionChatEditPhoto &c_messageActionChatEditPhoto() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageActionChatEditPhoto) throw mtpErrorWrongTypeId(_type, mtpc_messageActionChatEditPhoto);
		return *(const MTPDmessageActionChatEditPhoto*)data;
	}

	MTPDmessageActionChatAddUser &_messageActionChatAddUser() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageActionChatAddUser) throw mtpErrorWrongTypeId(_type, mtpc_messageActionChatAddUser);
		split();
		return *(MTPDmessageActionChatAddUser*)data;
	}
	const MTPDmessageActionChatAddUser &c_messageActionChatAddUser() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageActionChatAddUser) throw mtpErrorWrongTypeId(_type, mtpc_messageActionChatAddUser);
		return *(const MTPDmessageActionChatAddUser*)data;
	}

	MTPDmessageActionChatDeleteUser &_messageActionChatDeleteUser() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageActionChatDeleteUser) throw mtpErrorWrongTypeId(_type, mtpc_messageActionChatDeleteUser);
		split();
		return *(MTPDmessageActionChatDeleteUser*)data;
	}
	const MTPDmessageActionChatDeleteUser &c_messageActionChatDeleteUser() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageActionChatDeleteUser) throw mtpErrorWrongTypeId(_type, mtpc_messageActionChatDeleteUser);
		return *(const MTPDmessageActionChatDeleteUser*)data;
	}

	MTPDmessageActionGeoChatCreate &_messageActionGeoChatCreate() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageActionGeoChatCreate) throw mtpErrorWrongTypeId(_type, mtpc_messageActionGeoChatCreate);
		split();
		return *(MTPDmessageActionGeoChatCreate*)data;
	}
	const MTPDmessageActionGeoChatCreate &c_messageActionGeoChatCreate() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messageActionGeoChatCreate) throw mtpErrorWrongTypeId(_type, mtpc_messageActionGeoChatCreate);
		return *(const MTPDmessageActionGeoChatCreate*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPmessageAction(mtpTypeId type);
	explicit MTPmessageAction(MTPDmessageActionChatCreate *_data);
	explicit MTPmessageAction(MTPDmessageActionChatEditTitle *_data);
	explicit MTPmessageAction(MTPDmessageActionChatEditPhoto *_data);
	explicit MTPmessageAction(MTPDmessageActionChatAddUser *_data);
	explicit MTPmessageAction(MTPDmessageActionChatDeleteUser *_data);
	explicit MTPmessageAction(MTPDmessageActionGeoChatCreate *_data);

	friend MTPmessageAction MTP_messageActionEmpty();
	friend MTPmessageAction MTP_messageActionChatCreate(const MTPstring &_title, const MTPVector<MTPint> &_users);
	friend MTPmessageAction MTP_messageActionChatEditTitle(const MTPstring &_title);
	friend MTPmessageAction MTP_messageActionChatEditPhoto(const MTPPhoto &_photo);
	friend MTPmessageAction MTP_messageActionChatDeletePhoto();
	friend MTPmessageAction MTP_messageActionChatAddUser(MTPint _user_id);
	friend MTPmessageAction MTP_messageActionChatDeleteUser(MTPint _user_id);
	friend MTPmessageAction MTP_messageActionGeoChatCreate(const MTPstring &_title, const MTPstring &_address);
	friend MTPmessageAction MTP_messageActionGeoChatCheckin();

	mtpTypeId _type;
};
typedef MTPBoxed<MTPmessageAction> MTPMessageAction;

class MTPdialog : private mtpDataOwner {
public:
	MTPdialog();
	MTPdialog(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_dialog) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDdialog &_dialog() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDdialog*)data;
	}
	const MTPDdialog &c_dialog() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDdialog*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_dialog);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPdialog(MTPDdialog *_data);

	friend MTPdialog MTP_dialog(const MTPPeer &_peer, MTPint _top_message, MTPint _unread_count, const MTPPeerNotifySettings &_notify_settings);
};
typedef MTPBoxed<MTPdialog> MTPDialog;

class MTPphoto : private mtpDataOwner {
public:
	MTPphoto() : mtpDataOwner(0), _type(0) {
	}
	MTPphoto(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDphotoEmpty &_photoEmpty() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_photoEmpty) throw mtpErrorWrongTypeId(_type, mtpc_photoEmpty);
		split();
		return *(MTPDphotoEmpty*)data;
	}
	const MTPDphotoEmpty &c_photoEmpty() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_photoEmpty) throw mtpErrorWrongTypeId(_type, mtpc_photoEmpty);
		return *(const MTPDphotoEmpty*)data;
	}

	MTPDphoto &_photo() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_photo) throw mtpErrorWrongTypeId(_type, mtpc_photo);
		split();
		return *(MTPDphoto*)data;
	}
	const MTPDphoto &c_photo() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_photo) throw mtpErrorWrongTypeId(_type, mtpc_photo);
		return *(const MTPDphoto*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPphoto(mtpTypeId type);
	explicit MTPphoto(MTPDphotoEmpty *_data);
	explicit MTPphoto(MTPDphoto *_data);

	friend MTPphoto MTP_photoEmpty(const MTPlong &_id);
	friend MTPphoto MTP_photo(const MTPlong &_id, const MTPlong &_access_hash, MTPint _user_id, MTPint _date, const MTPstring &_caption, const MTPGeoPoint &_geo, const MTPVector<MTPPhotoSize> &_sizes);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPphoto> MTPPhoto;

class MTPphotoSize : private mtpDataOwner {
public:
	MTPphotoSize() : mtpDataOwner(0), _type(0) {
	}
	MTPphotoSize(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDphotoSizeEmpty &_photoSizeEmpty() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_photoSizeEmpty) throw mtpErrorWrongTypeId(_type, mtpc_photoSizeEmpty);
		split();
		return *(MTPDphotoSizeEmpty*)data;
	}
	const MTPDphotoSizeEmpty &c_photoSizeEmpty() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_photoSizeEmpty) throw mtpErrorWrongTypeId(_type, mtpc_photoSizeEmpty);
		return *(const MTPDphotoSizeEmpty*)data;
	}

	MTPDphotoSize &_photoSize() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_photoSize) throw mtpErrorWrongTypeId(_type, mtpc_photoSize);
		split();
		return *(MTPDphotoSize*)data;
	}
	const MTPDphotoSize &c_photoSize() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_photoSize) throw mtpErrorWrongTypeId(_type, mtpc_photoSize);
		return *(const MTPDphotoSize*)data;
	}

	MTPDphotoCachedSize &_photoCachedSize() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_photoCachedSize) throw mtpErrorWrongTypeId(_type, mtpc_photoCachedSize);
		split();
		return *(MTPDphotoCachedSize*)data;
	}
	const MTPDphotoCachedSize &c_photoCachedSize() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_photoCachedSize) throw mtpErrorWrongTypeId(_type, mtpc_photoCachedSize);
		return *(const MTPDphotoCachedSize*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPphotoSize(mtpTypeId type);
	explicit MTPphotoSize(MTPDphotoSizeEmpty *_data);
	explicit MTPphotoSize(MTPDphotoSize *_data);
	explicit MTPphotoSize(MTPDphotoCachedSize *_data);

	friend MTPphotoSize MTP_photoSizeEmpty(const MTPstring &_type);
	friend MTPphotoSize MTP_photoSize(const MTPstring &_type, const MTPFileLocation &_location, MTPint _w, MTPint _h, MTPint _size);
	friend MTPphotoSize MTP_photoCachedSize(const MTPstring &_type, const MTPFileLocation &_location, MTPint _w, MTPint _h, const MTPbytes &_bytes);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPphotoSize> MTPPhotoSize;

class MTPvideo : private mtpDataOwner {
public:
	MTPvideo() : mtpDataOwner(0), _type(0) {
	}
	MTPvideo(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDvideoEmpty &_videoEmpty() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_videoEmpty) throw mtpErrorWrongTypeId(_type, mtpc_videoEmpty);
		split();
		return *(MTPDvideoEmpty*)data;
	}
	const MTPDvideoEmpty &c_videoEmpty() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_videoEmpty) throw mtpErrorWrongTypeId(_type, mtpc_videoEmpty);
		return *(const MTPDvideoEmpty*)data;
	}

	MTPDvideo &_video() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_video) throw mtpErrorWrongTypeId(_type, mtpc_video);
		split();
		return *(MTPDvideo*)data;
	}
	const MTPDvideo &c_video() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_video) throw mtpErrorWrongTypeId(_type, mtpc_video);
		return *(const MTPDvideo*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPvideo(mtpTypeId type);
	explicit MTPvideo(MTPDvideoEmpty *_data);
	explicit MTPvideo(MTPDvideo *_data);

	friend MTPvideo MTP_videoEmpty(const MTPlong &_id);
	friend MTPvideo MTP_video(const MTPlong &_id, const MTPlong &_access_hash, MTPint _user_id, MTPint _date, const MTPstring &_caption, MTPint _duration, const MTPstring &_mime_type, MTPint _size, const MTPPhotoSize &_thumb, MTPint _dc_id, MTPint _w, MTPint _h);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPvideo> MTPVideo;

class MTPgeoPoint : private mtpDataOwner {
public:
	MTPgeoPoint() : mtpDataOwner(0), _type(0) {
	}
	MTPgeoPoint(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDgeoPoint &_geoPoint() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_geoPoint) throw mtpErrorWrongTypeId(_type, mtpc_geoPoint);
		split();
		return *(MTPDgeoPoint*)data;
	}
	const MTPDgeoPoint &c_geoPoint() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_geoPoint) throw mtpErrorWrongTypeId(_type, mtpc_geoPoint);
		return *(const MTPDgeoPoint*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPgeoPoint(mtpTypeId type);
	explicit MTPgeoPoint(MTPDgeoPoint *_data);

	friend MTPgeoPoint MTP_geoPointEmpty();
	friend MTPgeoPoint MTP_geoPoint(const MTPdouble &_long, const MTPdouble &_lat);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPgeoPoint> MTPGeoPoint;

class MTPauth_checkedPhone : private mtpDataOwner {
public:
	MTPauth_checkedPhone();
	MTPauth_checkedPhone(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_auth_checkedPhone) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDauth_checkedPhone &_auth_checkedPhone() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDauth_checkedPhone*)data;
	}
	const MTPDauth_checkedPhone &c_auth_checkedPhone() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDauth_checkedPhone*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_auth_checkedPhone);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPauth_checkedPhone(MTPDauth_checkedPhone *_data);

	friend MTPauth_checkedPhone MTP_auth_checkedPhone(MTPBool _phone_registered, MTPBool _phone_invited);
};
typedef MTPBoxed<MTPauth_checkedPhone> MTPauth_CheckedPhone;

class MTPauth_sentCode : private mtpDataOwner {
public:
	MTPauth_sentCode();
	MTPauth_sentCode(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_auth_sentCode) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDauth_sentCode &_auth_sentCode() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDauth_sentCode*)data;
	}
	const MTPDauth_sentCode &c_auth_sentCode() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDauth_sentCode*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_auth_sentCode);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPauth_sentCode(MTPDauth_sentCode *_data);

	friend MTPauth_sentCode MTP_auth_sentCode(MTPBool _phone_registered, const MTPstring &_phone_code_hash, MTPint _send_call_timeout, MTPBool _is_password);
};
typedef MTPBoxed<MTPauth_sentCode> MTPauth_SentCode;

class MTPauth_authorization : private mtpDataOwner {
public:
	MTPauth_authorization();
	MTPauth_authorization(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_auth_authorization) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDauth_authorization &_auth_authorization() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDauth_authorization*)data;
	}
	const MTPDauth_authorization &c_auth_authorization() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDauth_authorization*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_auth_authorization);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPauth_authorization(MTPDauth_authorization *_data);

	friend MTPauth_authorization MTP_auth_authorization(MTPint _expires, const MTPUser &_user);
};
typedef MTPBoxed<MTPauth_authorization> MTPauth_Authorization;

class MTPauth_exportedAuthorization : private mtpDataOwner {
public:
	MTPauth_exportedAuthorization();
	MTPauth_exportedAuthorization(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_auth_exportedAuthorization) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDauth_exportedAuthorization &_auth_exportedAuthorization() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDauth_exportedAuthorization*)data;
	}
	const MTPDauth_exportedAuthorization &c_auth_exportedAuthorization() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDauth_exportedAuthorization*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_auth_exportedAuthorization);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPauth_exportedAuthorization(MTPDauth_exportedAuthorization *_data);

	friend MTPauth_exportedAuthorization MTP_auth_exportedAuthorization(MTPint _id, const MTPbytes &_bytes);
};
typedef MTPBoxed<MTPauth_exportedAuthorization> MTPauth_ExportedAuthorization;

class MTPinputNotifyPeer : private mtpDataOwner {
public:
	MTPinputNotifyPeer() : mtpDataOwner(0), _type(0) {
	}
	MTPinputNotifyPeer(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDinputNotifyPeer &_inputNotifyPeer() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputNotifyPeer) throw mtpErrorWrongTypeId(_type, mtpc_inputNotifyPeer);
		split();
		return *(MTPDinputNotifyPeer*)data;
	}
	const MTPDinputNotifyPeer &c_inputNotifyPeer() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputNotifyPeer) throw mtpErrorWrongTypeId(_type, mtpc_inputNotifyPeer);
		return *(const MTPDinputNotifyPeer*)data;
	}

	MTPDinputNotifyGeoChatPeer &_inputNotifyGeoChatPeer() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputNotifyGeoChatPeer) throw mtpErrorWrongTypeId(_type, mtpc_inputNotifyGeoChatPeer);
		split();
		return *(MTPDinputNotifyGeoChatPeer*)data;
	}
	const MTPDinputNotifyGeoChatPeer &c_inputNotifyGeoChatPeer() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputNotifyGeoChatPeer) throw mtpErrorWrongTypeId(_type, mtpc_inputNotifyGeoChatPeer);
		return *(const MTPDinputNotifyGeoChatPeer*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPinputNotifyPeer(mtpTypeId type);
	explicit MTPinputNotifyPeer(MTPDinputNotifyPeer *_data);
	explicit MTPinputNotifyPeer(MTPDinputNotifyGeoChatPeer *_data);

	friend MTPinputNotifyPeer MTP_inputNotifyPeer(const MTPInputPeer &_peer);
	friend MTPinputNotifyPeer MTP_inputNotifyUsers();
	friend MTPinputNotifyPeer MTP_inputNotifyChats();
	friend MTPinputNotifyPeer MTP_inputNotifyAll();
	friend MTPinputNotifyPeer MTP_inputNotifyGeoChatPeer(const MTPInputGeoChat &_peer);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPinputNotifyPeer> MTPInputNotifyPeer;

class MTPinputPeerNotifyEvents {
public:
	MTPinputPeerNotifyEvents() : _type(0) {
	}
	MTPinputPeerNotifyEvents(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : _type(0) {
		read(from, end, cons);
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPinputPeerNotifyEvents(mtpTypeId type);

	friend MTPinputPeerNotifyEvents MTP_inputPeerNotifyEventsEmpty();
	friend MTPinputPeerNotifyEvents MTP_inputPeerNotifyEventsAll();

	mtpTypeId _type;
};
typedef MTPBoxed<MTPinputPeerNotifyEvents> MTPInputPeerNotifyEvents;

class MTPinputPeerNotifySettings : private mtpDataOwner {
public:
	MTPinputPeerNotifySettings();
	MTPinputPeerNotifySettings(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_inputPeerNotifySettings) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDinputPeerNotifySettings &_inputPeerNotifySettings() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDinputPeerNotifySettings*)data;
	}
	const MTPDinputPeerNotifySettings &c_inputPeerNotifySettings() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDinputPeerNotifySettings*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_inputPeerNotifySettings);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPinputPeerNotifySettings(MTPDinputPeerNotifySettings *_data);

	friend MTPinputPeerNotifySettings MTP_inputPeerNotifySettings(MTPint _mute_until, const MTPstring &_sound, MTPBool _show_previews, MTPint _events_mask);
};
typedef MTPBoxed<MTPinputPeerNotifySettings> MTPInputPeerNotifySettings;

class MTPpeerNotifyEvents {
public:
	MTPpeerNotifyEvents() : _type(0) {
	}
	MTPpeerNotifyEvents(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : _type(0) {
		read(from, end, cons);
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPpeerNotifyEvents(mtpTypeId type);

	friend MTPpeerNotifyEvents MTP_peerNotifyEventsEmpty();
	friend MTPpeerNotifyEvents MTP_peerNotifyEventsAll();

	mtpTypeId _type;
};
typedef MTPBoxed<MTPpeerNotifyEvents> MTPPeerNotifyEvents;

class MTPpeerNotifySettings : private mtpDataOwner {
public:
	MTPpeerNotifySettings() : mtpDataOwner(0), _type(0) {
	}
	MTPpeerNotifySettings(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDpeerNotifySettings &_peerNotifySettings() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_peerNotifySettings) throw mtpErrorWrongTypeId(_type, mtpc_peerNotifySettings);
		split();
		return *(MTPDpeerNotifySettings*)data;
	}
	const MTPDpeerNotifySettings &c_peerNotifySettings() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_peerNotifySettings) throw mtpErrorWrongTypeId(_type, mtpc_peerNotifySettings);
		return *(const MTPDpeerNotifySettings*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPpeerNotifySettings(mtpTypeId type);
	explicit MTPpeerNotifySettings(MTPDpeerNotifySettings *_data);

	friend MTPpeerNotifySettings MTP_peerNotifySettingsEmpty();
	friend MTPpeerNotifySettings MTP_peerNotifySettings(MTPint _mute_until, const MTPstring &_sound, MTPBool _show_previews, MTPint _events_mask);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPpeerNotifySettings> MTPPeerNotifySettings;

class MTPwallPaper : private mtpDataOwner {
public:
	MTPwallPaper() : mtpDataOwner(0), _type(0) {
	}
	MTPwallPaper(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDwallPaper &_wallPaper() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_wallPaper) throw mtpErrorWrongTypeId(_type, mtpc_wallPaper);
		split();
		return *(MTPDwallPaper*)data;
	}
	const MTPDwallPaper &c_wallPaper() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_wallPaper) throw mtpErrorWrongTypeId(_type, mtpc_wallPaper);
		return *(const MTPDwallPaper*)data;
	}

	MTPDwallPaperSolid &_wallPaperSolid() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_wallPaperSolid) throw mtpErrorWrongTypeId(_type, mtpc_wallPaperSolid);
		split();
		return *(MTPDwallPaperSolid*)data;
	}
	const MTPDwallPaperSolid &c_wallPaperSolid() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_wallPaperSolid) throw mtpErrorWrongTypeId(_type, mtpc_wallPaperSolid);
		return *(const MTPDwallPaperSolid*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPwallPaper(mtpTypeId type);
	explicit MTPwallPaper(MTPDwallPaper *_data);
	explicit MTPwallPaper(MTPDwallPaperSolid *_data);

	friend MTPwallPaper MTP_wallPaper(MTPint _id, const MTPstring &_title, const MTPVector<MTPPhotoSize> &_sizes, MTPint _color);
	friend MTPwallPaper MTP_wallPaperSolid(MTPint _id, const MTPstring &_title, MTPint _bg_color, MTPint _color);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPwallPaper> MTPWallPaper;

class MTPuserFull : private mtpDataOwner {
public:
	MTPuserFull();
	MTPuserFull(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_userFull) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDuserFull &_userFull() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDuserFull*)data;
	}
	const MTPDuserFull &c_userFull() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDuserFull*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_userFull);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPuserFull(MTPDuserFull *_data);

	friend MTPuserFull MTP_userFull(const MTPUser &_user, const MTPcontacts_Link &_link, const MTPPhoto &_profile_photo, const MTPPeerNotifySettings &_notify_settings, MTPBool _blocked, const MTPstring &_real_first_name, const MTPstring &_real_last_name);
};
typedef MTPBoxed<MTPuserFull> MTPUserFull;

class MTPcontact : private mtpDataOwner {
public:
	MTPcontact();
	MTPcontact(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contact) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDcontact &_contact() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDcontact*)data;
	}
	const MTPDcontact &c_contact() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDcontact*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contact);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPcontact(MTPDcontact *_data);

	friend MTPcontact MTP_contact(MTPint _user_id, MTPBool _mutual);
};
typedef MTPBoxed<MTPcontact> MTPContact;

class MTPimportedContact : private mtpDataOwner {
public:
	MTPimportedContact();
	MTPimportedContact(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_importedContact) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDimportedContact &_importedContact() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDimportedContact*)data;
	}
	const MTPDimportedContact &c_importedContact() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDimportedContact*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_importedContact);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPimportedContact(MTPDimportedContact *_data);

	friend MTPimportedContact MTP_importedContact(MTPint _user_id, const MTPlong &_client_id);
};
typedef MTPBoxed<MTPimportedContact> MTPImportedContact;

class MTPcontactBlocked : private mtpDataOwner {
public:
	MTPcontactBlocked();
	MTPcontactBlocked(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contactBlocked) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDcontactBlocked &_contactBlocked() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDcontactBlocked*)data;
	}
	const MTPDcontactBlocked &c_contactBlocked() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDcontactBlocked*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contactBlocked);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPcontactBlocked(MTPDcontactBlocked *_data);

	friend MTPcontactBlocked MTP_contactBlocked(MTPint _user_id, MTPint _date);
};
typedef MTPBoxed<MTPcontactBlocked> MTPContactBlocked;

class MTPcontactFound : private mtpDataOwner {
public:
	MTPcontactFound();
	MTPcontactFound(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contactFound) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDcontactFound &_contactFound() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDcontactFound*)data;
	}
	const MTPDcontactFound &c_contactFound() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDcontactFound*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contactFound);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPcontactFound(MTPDcontactFound *_data);

	friend MTPcontactFound MTP_contactFound(MTPint _user_id);
};
typedef MTPBoxed<MTPcontactFound> MTPContactFound;

class MTPcontactSuggested : private mtpDataOwner {
public:
	MTPcontactSuggested();
	MTPcontactSuggested(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contactSuggested) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDcontactSuggested &_contactSuggested() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDcontactSuggested*)data;
	}
	const MTPDcontactSuggested &c_contactSuggested() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDcontactSuggested*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contactSuggested);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPcontactSuggested(MTPDcontactSuggested *_data);

	friend MTPcontactSuggested MTP_contactSuggested(MTPint _user_id, MTPint _mutual_contacts);
};
typedef MTPBoxed<MTPcontactSuggested> MTPContactSuggested;

class MTPcontactStatus : private mtpDataOwner {
public:
	MTPcontactStatus();
	MTPcontactStatus(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contactStatus) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDcontactStatus &_contactStatus() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDcontactStatus*)data;
	}
	const MTPDcontactStatus &c_contactStatus() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDcontactStatus*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contactStatus);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPcontactStatus(MTPDcontactStatus *_data);

	friend MTPcontactStatus MTP_contactStatus(MTPint _user_id, MTPint _expires);
};
typedef MTPBoxed<MTPcontactStatus> MTPContactStatus;

class MTPchatLocated : private mtpDataOwner {
public:
	MTPchatLocated();
	MTPchatLocated(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_chatLocated) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDchatLocated &_chatLocated() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDchatLocated*)data;
	}
	const MTPDchatLocated &c_chatLocated() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDchatLocated*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_chatLocated);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPchatLocated(MTPDchatLocated *_data);

	friend MTPchatLocated MTP_chatLocated(MTPint _chat_id, MTPint _distance);
};
typedef MTPBoxed<MTPchatLocated> MTPChatLocated;

class MTPcontacts_foreignLink : private mtpDataOwner {
public:
	MTPcontacts_foreignLink() : mtpDataOwner(0), _type(0) {
	}
	MTPcontacts_foreignLink(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDcontacts_foreignLinkRequested &_contacts_foreignLinkRequested() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_contacts_foreignLinkRequested) throw mtpErrorWrongTypeId(_type, mtpc_contacts_foreignLinkRequested);
		split();
		return *(MTPDcontacts_foreignLinkRequested*)data;
	}
	const MTPDcontacts_foreignLinkRequested &c_contacts_foreignLinkRequested() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_contacts_foreignLinkRequested) throw mtpErrorWrongTypeId(_type, mtpc_contacts_foreignLinkRequested);
		return *(const MTPDcontacts_foreignLinkRequested*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPcontacts_foreignLink(mtpTypeId type);
	explicit MTPcontacts_foreignLink(MTPDcontacts_foreignLinkRequested *_data);

	friend MTPcontacts_foreignLink MTP_contacts_foreignLinkUnknown();
	friend MTPcontacts_foreignLink MTP_contacts_foreignLinkRequested(MTPBool _has_phone);
	friend MTPcontacts_foreignLink MTP_contacts_foreignLinkMutual();

	mtpTypeId _type;
};
typedef MTPBoxed<MTPcontacts_foreignLink> MTPcontacts_ForeignLink;

class MTPcontacts_myLink : private mtpDataOwner {
public:
	MTPcontacts_myLink() : mtpDataOwner(0), _type(0) {
	}
	MTPcontacts_myLink(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDcontacts_myLinkRequested &_contacts_myLinkRequested() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_contacts_myLinkRequested) throw mtpErrorWrongTypeId(_type, mtpc_contacts_myLinkRequested);
		split();
		return *(MTPDcontacts_myLinkRequested*)data;
	}
	const MTPDcontacts_myLinkRequested &c_contacts_myLinkRequested() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_contacts_myLinkRequested) throw mtpErrorWrongTypeId(_type, mtpc_contacts_myLinkRequested);
		return *(const MTPDcontacts_myLinkRequested*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPcontacts_myLink(mtpTypeId type);
	explicit MTPcontacts_myLink(MTPDcontacts_myLinkRequested *_data);

	friend MTPcontacts_myLink MTP_contacts_myLinkEmpty();
	friend MTPcontacts_myLink MTP_contacts_myLinkRequested(MTPBool _contact);
	friend MTPcontacts_myLink MTP_contacts_myLinkContact();

	mtpTypeId _type;
};
typedef MTPBoxed<MTPcontacts_myLink> MTPcontacts_MyLink;

class MTPcontacts_link : private mtpDataOwner {
public:
	MTPcontacts_link();
	MTPcontacts_link(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contacts_link) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDcontacts_link &_contacts_link() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDcontacts_link*)data;
	}
	const MTPDcontacts_link &c_contacts_link() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDcontacts_link*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contacts_link);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPcontacts_link(MTPDcontacts_link *_data);

	friend MTPcontacts_link MTP_contacts_link(const MTPcontacts_MyLink &_my_link, const MTPcontacts_ForeignLink &_foreign_link, const MTPUser &_user);
};
typedef MTPBoxed<MTPcontacts_link> MTPcontacts_Link;

class MTPcontacts_contacts : private mtpDataOwner {
public:
	MTPcontacts_contacts() : mtpDataOwner(0), _type(0) {
	}
	MTPcontacts_contacts(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDcontacts_contacts &_contacts_contacts() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_contacts_contacts) throw mtpErrorWrongTypeId(_type, mtpc_contacts_contacts);
		split();
		return *(MTPDcontacts_contacts*)data;
	}
	const MTPDcontacts_contacts &c_contacts_contacts() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_contacts_contacts) throw mtpErrorWrongTypeId(_type, mtpc_contacts_contacts);
		return *(const MTPDcontacts_contacts*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPcontacts_contacts(mtpTypeId type);
	explicit MTPcontacts_contacts(MTPDcontacts_contacts *_data);

	friend MTPcontacts_contacts MTP_contacts_contacts(const MTPVector<MTPContact> &_contacts, const MTPVector<MTPUser> &_users);
	friend MTPcontacts_contacts MTP_contacts_contactsNotModified();

	mtpTypeId _type;
};
typedef MTPBoxed<MTPcontacts_contacts> MTPcontacts_Contacts;

class MTPcontacts_importedContacts : private mtpDataOwner {
public:
	MTPcontacts_importedContacts();
	MTPcontacts_importedContacts(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contacts_importedContacts) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDcontacts_importedContacts &_contacts_importedContacts() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDcontacts_importedContacts*)data;
	}
	const MTPDcontacts_importedContacts &c_contacts_importedContacts() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDcontacts_importedContacts*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contacts_importedContacts);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPcontacts_importedContacts(MTPDcontacts_importedContacts *_data);

	friend MTPcontacts_importedContacts MTP_contacts_importedContacts(const MTPVector<MTPImportedContact> &_imported, const MTPVector<MTPlong> &_retry_contacts, const MTPVector<MTPUser> &_users);
};
typedef MTPBoxed<MTPcontacts_importedContacts> MTPcontacts_ImportedContacts;

class MTPcontacts_blocked : private mtpDataOwner {
public:
	MTPcontacts_blocked() : mtpDataOwner(0), _type(0) {
	}
	MTPcontacts_blocked(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDcontacts_blocked &_contacts_blocked() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_contacts_blocked) throw mtpErrorWrongTypeId(_type, mtpc_contacts_blocked);
		split();
		return *(MTPDcontacts_blocked*)data;
	}
	const MTPDcontacts_blocked &c_contacts_blocked() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_contacts_blocked) throw mtpErrorWrongTypeId(_type, mtpc_contacts_blocked);
		return *(const MTPDcontacts_blocked*)data;
	}

	MTPDcontacts_blockedSlice &_contacts_blockedSlice() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_contacts_blockedSlice) throw mtpErrorWrongTypeId(_type, mtpc_contacts_blockedSlice);
		split();
		return *(MTPDcontacts_blockedSlice*)data;
	}
	const MTPDcontacts_blockedSlice &c_contacts_blockedSlice() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_contacts_blockedSlice) throw mtpErrorWrongTypeId(_type, mtpc_contacts_blockedSlice);
		return *(const MTPDcontacts_blockedSlice*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPcontacts_blocked(mtpTypeId type);
	explicit MTPcontacts_blocked(MTPDcontacts_blocked *_data);
	explicit MTPcontacts_blocked(MTPDcontacts_blockedSlice *_data);

	friend MTPcontacts_blocked MTP_contacts_blocked(const MTPVector<MTPContactBlocked> &_blocked, const MTPVector<MTPUser> &_users);
	friend MTPcontacts_blocked MTP_contacts_blockedSlice(MTPint _count, const MTPVector<MTPContactBlocked> &_blocked, const MTPVector<MTPUser> &_users);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPcontacts_blocked> MTPcontacts_Blocked;

class MTPcontacts_found : private mtpDataOwner {
public:
	MTPcontacts_found();
	MTPcontacts_found(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contacts_found) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDcontacts_found &_contacts_found() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDcontacts_found*)data;
	}
	const MTPDcontacts_found &c_contacts_found() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDcontacts_found*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contacts_found);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPcontacts_found(MTPDcontacts_found *_data);

	friend MTPcontacts_found MTP_contacts_found(const MTPVector<MTPContactFound> &_results, const MTPVector<MTPUser> &_users);
};
typedef MTPBoxed<MTPcontacts_found> MTPcontacts_Found;

class MTPcontacts_suggested : private mtpDataOwner {
public:
	MTPcontacts_suggested();
	MTPcontacts_suggested(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contacts_suggested) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDcontacts_suggested &_contacts_suggested() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDcontacts_suggested*)data;
	}
	const MTPDcontacts_suggested &c_contacts_suggested() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDcontacts_suggested*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contacts_suggested);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPcontacts_suggested(MTPDcontacts_suggested *_data);

	friend MTPcontacts_suggested MTP_contacts_suggested(const MTPVector<MTPContactSuggested> &_results, const MTPVector<MTPUser> &_users);
};
typedef MTPBoxed<MTPcontacts_suggested> MTPcontacts_Suggested;

class MTPmessages_dialogs : private mtpDataOwner {
public:
	MTPmessages_dialogs() : mtpDataOwner(0), _type(0) {
	}
	MTPmessages_dialogs(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDmessages_dialogs &_messages_dialogs() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_dialogs) throw mtpErrorWrongTypeId(_type, mtpc_messages_dialogs);
		split();
		return *(MTPDmessages_dialogs*)data;
	}
	const MTPDmessages_dialogs &c_messages_dialogs() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_dialogs) throw mtpErrorWrongTypeId(_type, mtpc_messages_dialogs);
		return *(const MTPDmessages_dialogs*)data;
	}

	MTPDmessages_dialogsSlice &_messages_dialogsSlice() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_dialogsSlice) throw mtpErrorWrongTypeId(_type, mtpc_messages_dialogsSlice);
		split();
		return *(MTPDmessages_dialogsSlice*)data;
	}
	const MTPDmessages_dialogsSlice &c_messages_dialogsSlice() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_dialogsSlice) throw mtpErrorWrongTypeId(_type, mtpc_messages_dialogsSlice);
		return *(const MTPDmessages_dialogsSlice*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPmessages_dialogs(mtpTypeId type);
	explicit MTPmessages_dialogs(MTPDmessages_dialogs *_data);
	explicit MTPmessages_dialogs(MTPDmessages_dialogsSlice *_data);

	friend MTPmessages_dialogs MTP_messages_dialogs(const MTPVector<MTPDialog> &_dialogs, const MTPVector<MTPMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users);
	friend MTPmessages_dialogs MTP_messages_dialogsSlice(MTPint _count, const MTPVector<MTPDialog> &_dialogs, const MTPVector<MTPMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPmessages_dialogs> MTPmessages_Dialogs;

class MTPmessages_messages : private mtpDataOwner {
public:
	MTPmessages_messages() : mtpDataOwner(0), _type(0) {
	}
	MTPmessages_messages(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDmessages_messages &_messages_messages() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_messages) throw mtpErrorWrongTypeId(_type, mtpc_messages_messages);
		split();
		return *(MTPDmessages_messages*)data;
	}
	const MTPDmessages_messages &c_messages_messages() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_messages) throw mtpErrorWrongTypeId(_type, mtpc_messages_messages);
		return *(const MTPDmessages_messages*)data;
	}

	MTPDmessages_messagesSlice &_messages_messagesSlice() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_messagesSlice) throw mtpErrorWrongTypeId(_type, mtpc_messages_messagesSlice);
		split();
		return *(MTPDmessages_messagesSlice*)data;
	}
	const MTPDmessages_messagesSlice &c_messages_messagesSlice() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_messagesSlice) throw mtpErrorWrongTypeId(_type, mtpc_messages_messagesSlice);
		return *(const MTPDmessages_messagesSlice*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPmessages_messages(mtpTypeId type);
	explicit MTPmessages_messages(MTPDmessages_messages *_data);
	explicit MTPmessages_messages(MTPDmessages_messagesSlice *_data);

	friend MTPmessages_messages MTP_messages_messages(const MTPVector<MTPMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users);
	friend MTPmessages_messages MTP_messages_messagesSlice(MTPint _count, const MTPVector<MTPMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPmessages_messages> MTPmessages_Messages;

class MTPmessages_message : private mtpDataOwner {
public:
	MTPmessages_message() : mtpDataOwner(0), _type(0) {
	}
	MTPmessages_message(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDmessages_message &_messages_message() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_message) throw mtpErrorWrongTypeId(_type, mtpc_messages_message);
		split();
		return *(MTPDmessages_message*)data;
	}
	const MTPDmessages_message &c_messages_message() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_message) throw mtpErrorWrongTypeId(_type, mtpc_messages_message);
		return *(const MTPDmessages_message*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPmessages_message(mtpTypeId type);
	explicit MTPmessages_message(MTPDmessages_message *_data);

	friend MTPmessages_message MTP_messages_messageEmpty();
	friend MTPmessages_message MTP_messages_message(const MTPMessage &_message, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPmessages_message> MTPmessages_Message;

class MTPmessages_statedMessages : private mtpDataOwner {
public:
	MTPmessages_statedMessages() : mtpDataOwner(0), _type(0) {
	}
	MTPmessages_statedMessages(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDmessages_statedMessages &_messages_statedMessages() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_statedMessages) throw mtpErrorWrongTypeId(_type, mtpc_messages_statedMessages);
		split();
		return *(MTPDmessages_statedMessages*)data;
	}
	const MTPDmessages_statedMessages &c_messages_statedMessages() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_statedMessages) throw mtpErrorWrongTypeId(_type, mtpc_messages_statedMessages);
		return *(const MTPDmessages_statedMessages*)data;
	}

	MTPDmessages_statedMessagesLinks &_messages_statedMessagesLinks() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_statedMessagesLinks) throw mtpErrorWrongTypeId(_type, mtpc_messages_statedMessagesLinks);
		split();
		return *(MTPDmessages_statedMessagesLinks*)data;
	}
	const MTPDmessages_statedMessagesLinks &c_messages_statedMessagesLinks() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_statedMessagesLinks) throw mtpErrorWrongTypeId(_type, mtpc_messages_statedMessagesLinks);
		return *(const MTPDmessages_statedMessagesLinks*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPmessages_statedMessages(mtpTypeId type);
	explicit MTPmessages_statedMessages(MTPDmessages_statedMessages *_data);
	explicit MTPmessages_statedMessages(MTPDmessages_statedMessagesLinks *_data);

	friend MTPmessages_statedMessages MTP_messages_statedMessages(const MTPVector<MTPMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users, MTPint _pts, MTPint _seq);
	friend MTPmessages_statedMessages MTP_messages_statedMessagesLinks(const MTPVector<MTPMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users, const MTPVector<MTPcontacts_Link> &_links, MTPint _pts, MTPint _seq);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPmessages_statedMessages> MTPmessages_StatedMessages;

class MTPmessages_statedMessage : private mtpDataOwner {
public:
	MTPmessages_statedMessage() : mtpDataOwner(0), _type(0) {
	}
	MTPmessages_statedMessage(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDmessages_statedMessage &_messages_statedMessage() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_statedMessage) throw mtpErrorWrongTypeId(_type, mtpc_messages_statedMessage);
		split();
		return *(MTPDmessages_statedMessage*)data;
	}
	const MTPDmessages_statedMessage &c_messages_statedMessage() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_statedMessage) throw mtpErrorWrongTypeId(_type, mtpc_messages_statedMessage);
		return *(const MTPDmessages_statedMessage*)data;
	}

	MTPDmessages_statedMessageLink &_messages_statedMessageLink() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_statedMessageLink) throw mtpErrorWrongTypeId(_type, mtpc_messages_statedMessageLink);
		split();
		return *(MTPDmessages_statedMessageLink*)data;
	}
	const MTPDmessages_statedMessageLink &c_messages_statedMessageLink() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_statedMessageLink) throw mtpErrorWrongTypeId(_type, mtpc_messages_statedMessageLink);
		return *(const MTPDmessages_statedMessageLink*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPmessages_statedMessage(mtpTypeId type);
	explicit MTPmessages_statedMessage(MTPDmessages_statedMessage *_data);
	explicit MTPmessages_statedMessage(MTPDmessages_statedMessageLink *_data);

	friend MTPmessages_statedMessage MTP_messages_statedMessage(const MTPMessage &_message, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users, MTPint _pts, MTPint _seq);
	friend MTPmessages_statedMessage MTP_messages_statedMessageLink(const MTPMessage &_message, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users, const MTPVector<MTPcontacts_Link> &_links, MTPint _pts, MTPint _seq);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPmessages_statedMessage> MTPmessages_StatedMessage;

class MTPmessages_sentMessage : private mtpDataOwner {
public:
	MTPmessages_sentMessage() : mtpDataOwner(0), _type(0) {
	}
	MTPmessages_sentMessage(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDmessages_sentMessage &_messages_sentMessage() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_sentMessage) throw mtpErrorWrongTypeId(_type, mtpc_messages_sentMessage);
		split();
		return *(MTPDmessages_sentMessage*)data;
	}
	const MTPDmessages_sentMessage &c_messages_sentMessage() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_sentMessage) throw mtpErrorWrongTypeId(_type, mtpc_messages_sentMessage);
		return *(const MTPDmessages_sentMessage*)data;
	}

	MTPDmessages_sentMessageLink &_messages_sentMessageLink() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_sentMessageLink) throw mtpErrorWrongTypeId(_type, mtpc_messages_sentMessageLink);
		split();
		return *(MTPDmessages_sentMessageLink*)data;
	}
	const MTPDmessages_sentMessageLink &c_messages_sentMessageLink() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_sentMessageLink) throw mtpErrorWrongTypeId(_type, mtpc_messages_sentMessageLink);
		return *(const MTPDmessages_sentMessageLink*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPmessages_sentMessage(mtpTypeId type);
	explicit MTPmessages_sentMessage(MTPDmessages_sentMessage *_data);
	explicit MTPmessages_sentMessage(MTPDmessages_sentMessageLink *_data);

	friend MTPmessages_sentMessage MTP_messages_sentMessage(MTPint _id, MTPint _date, MTPint _pts, MTPint _seq);
	friend MTPmessages_sentMessage MTP_messages_sentMessageLink(MTPint _id, MTPint _date, MTPint _pts, MTPint _seq, const MTPVector<MTPcontacts_Link> &_links);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPmessages_sentMessage> MTPmessages_SentMessage;

class MTPmessages_chat : private mtpDataOwner {
public:
	MTPmessages_chat();
	MTPmessages_chat(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_chat) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDmessages_chat &_messages_chat() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDmessages_chat*)data;
	}
	const MTPDmessages_chat &c_messages_chat() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDmessages_chat*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_chat);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPmessages_chat(MTPDmessages_chat *_data);

	friend MTPmessages_chat MTP_messages_chat(const MTPChat &_chat, const MTPVector<MTPUser> &_users);
};
typedef MTPBoxed<MTPmessages_chat> MTPmessages_Chat;

class MTPmessages_chats : private mtpDataOwner {
public:
	MTPmessages_chats();
	MTPmessages_chats(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_chats) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDmessages_chats &_messages_chats() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDmessages_chats*)data;
	}
	const MTPDmessages_chats &c_messages_chats() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDmessages_chats*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_chats);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPmessages_chats(MTPDmessages_chats *_data);

	friend MTPmessages_chats MTP_messages_chats(const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users);
};
typedef MTPBoxed<MTPmessages_chats> MTPmessages_Chats;

class MTPmessages_chatFull : private mtpDataOwner {
public:
	MTPmessages_chatFull();
	MTPmessages_chatFull(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_chatFull) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDmessages_chatFull &_messages_chatFull() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDmessages_chatFull*)data;
	}
	const MTPDmessages_chatFull &c_messages_chatFull() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDmessages_chatFull*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_chatFull);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPmessages_chatFull(MTPDmessages_chatFull *_data);

	friend MTPmessages_chatFull MTP_messages_chatFull(const MTPChatFull &_full_chat, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users);
};
typedef MTPBoxed<MTPmessages_chatFull> MTPmessages_ChatFull;

class MTPmessages_affectedHistory : private mtpDataOwner {
public:
	MTPmessages_affectedHistory();
	MTPmessages_affectedHistory(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_affectedHistory) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDmessages_affectedHistory &_messages_affectedHistory() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDmessages_affectedHistory*)data;
	}
	const MTPDmessages_affectedHistory &c_messages_affectedHistory() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDmessages_affectedHistory*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_affectedHistory);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPmessages_affectedHistory(MTPDmessages_affectedHistory *_data);

	friend MTPmessages_affectedHistory MTP_messages_affectedHistory(MTPint _pts, MTPint _seq, MTPint _offset);
};
typedef MTPBoxed<MTPmessages_affectedHistory> MTPmessages_AffectedHistory;

class MTPmessagesFilter {
public:
	MTPmessagesFilter() : _type(0) {
	}
	MTPmessagesFilter(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : _type(0) {
		read(from, end, cons);
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPmessagesFilter(mtpTypeId type);

	friend MTPmessagesFilter MTP_inputMessagesFilterEmpty();
	friend MTPmessagesFilter MTP_inputMessagesFilterPhotos();
	friend MTPmessagesFilter MTP_inputMessagesFilterVideo();
	friend MTPmessagesFilter MTP_inputMessagesFilterPhotoVideo();
	friend MTPmessagesFilter MTP_inputMessagesFilterDocument();

	mtpTypeId _type;
};
typedef MTPBoxed<MTPmessagesFilter> MTPMessagesFilter;

class MTPupdate : private mtpDataOwner {
public:
	MTPupdate() : mtpDataOwner(0), _type(0) {
	}
	MTPupdate(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDupdateNewMessage &_updateNewMessage() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateNewMessage) throw mtpErrorWrongTypeId(_type, mtpc_updateNewMessage);
		split();
		return *(MTPDupdateNewMessage*)data;
	}
	const MTPDupdateNewMessage &c_updateNewMessage() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateNewMessage) throw mtpErrorWrongTypeId(_type, mtpc_updateNewMessage);
		return *(const MTPDupdateNewMessage*)data;
	}

	MTPDupdateMessageID &_updateMessageID() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateMessageID) throw mtpErrorWrongTypeId(_type, mtpc_updateMessageID);
		split();
		return *(MTPDupdateMessageID*)data;
	}
	const MTPDupdateMessageID &c_updateMessageID() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateMessageID) throw mtpErrorWrongTypeId(_type, mtpc_updateMessageID);
		return *(const MTPDupdateMessageID*)data;
	}

	MTPDupdateReadMessages &_updateReadMessages() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateReadMessages) throw mtpErrorWrongTypeId(_type, mtpc_updateReadMessages);
		split();
		return *(MTPDupdateReadMessages*)data;
	}
	const MTPDupdateReadMessages &c_updateReadMessages() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateReadMessages) throw mtpErrorWrongTypeId(_type, mtpc_updateReadMessages);
		return *(const MTPDupdateReadMessages*)data;
	}

	MTPDupdateDeleteMessages &_updateDeleteMessages() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateDeleteMessages) throw mtpErrorWrongTypeId(_type, mtpc_updateDeleteMessages);
		split();
		return *(MTPDupdateDeleteMessages*)data;
	}
	const MTPDupdateDeleteMessages &c_updateDeleteMessages() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateDeleteMessages) throw mtpErrorWrongTypeId(_type, mtpc_updateDeleteMessages);
		return *(const MTPDupdateDeleteMessages*)data;
	}

	MTPDupdateRestoreMessages &_updateRestoreMessages() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateRestoreMessages) throw mtpErrorWrongTypeId(_type, mtpc_updateRestoreMessages);
		split();
		return *(MTPDupdateRestoreMessages*)data;
	}
	const MTPDupdateRestoreMessages &c_updateRestoreMessages() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateRestoreMessages) throw mtpErrorWrongTypeId(_type, mtpc_updateRestoreMessages);
		return *(const MTPDupdateRestoreMessages*)data;
	}

	MTPDupdateUserTyping &_updateUserTyping() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateUserTyping) throw mtpErrorWrongTypeId(_type, mtpc_updateUserTyping);
		split();
		return *(MTPDupdateUserTyping*)data;
	}
	const MTPDupdateUserTyping &c_updateUserTyping() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateUserTyping) throw mtpErrorWrongTypeId(_type, mtpc_updateUserTyping);
		return *(const MTPDupdateUserTyping*)data;
	}

	MTPDupdateChatUserTyping &_updateChatUserTyping() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateChatUserTyping) throw mtpErrorWrongTypeId(_type, mtpc_updateChatUserTyping);
		split();
		return *(MTPDupdateChatUserTyping*)data;
	}
	const MTPDupdateChatUserTyping &c_updateChatUserTyping() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateChatUserTyping) throw mtpErrorWrongTypeId(_type, mtpc_updateChatUserTyping);
		return *(const MTPDupdateChatUserTyping*)data;
	}

	MTPDupdateChatParticipants &_updateChatParticipants() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateChatParticipants) throw mtpErrorWrongTypeId(_type, mtpc_updateChatParticipants);
		split();
		return *(MTPDupdateChatParticipants*)data;
	}
	const MTPDupdateChatParticipants &c_updateChatParticipants() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateChatParticipants) throw mtpErrorWrongTypeId(_type, mtpc_updateChatParticipants);
		return *(const MTPDupdateChatParticipants*)data;
	}

	MTPDupdateUserStatus &_updateUserStatus() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateUserStatus) throw mtpErrorWrongTypeId(_type, mtpc_updateUserStatus);
		split();
		return *(MTPDupdateUserStatus*)data;
	}
	const MTPDupdateUserStatus &c_updateUserStatus() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateUserStatus) throw mtpErrorWrongTypeId(_type, mtpc_updateUserStatus);
		return *(const MTPDupdateUserStatus*)data;
	}

	MTPDupdateUserName &_updateUserName() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateUserName) throw mtpErrorWrongTypeId(_type, mtpc_updateUserName);
		split();
		return *(MTPDupdateUserName*)data;
	}
	const MTPDupdateUserName &c_updateUserName() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateUserName) throw mtpErrorWrongTypeId(_type, mtpc_updateUserName);
		return *(const MTPDupdateUserName*)data;
	}

	MTPDupdateUserPhoto &_updateUserPhoto() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateUserPhoto) throw mtpErrorWrongTypeId(_type, mtpc_updateUserPhoto);
		split();
		return *(MTPDupdateUserPhoto*)data;
	}
	const MTPDupdateUserPhoto &c_updateUserPhoto() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateUserPhoto) throw mtpErrorWrongTypeId(_type, mtpc_updateUserPhoto);
		return *(const MTPDupdateUserPhoto*)data;
	}

	MTPDupdateContactRegistered &_updateContactRegistered() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateContactRegistered) throw mtpErrorWrongTypeId(_type, mtpc_updateContactRegistered);
		split();
		return *(MTPDupdateContactRegistered*)data;
	}
	const MTPDupdateContactRegistered &c_updateContactRegistered() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateContactRegistered) throw mtpErrorWrongTypeId(_type, mtpc_updateContactRegistered);
		return *(const MTPDupdateContactRegistered*)data;
	}

	MTPDupdateContactLink &_updateContactLink() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateContactLink) throw mtpErrorWrongTypeId(_type, mtpc_updateContactLink);
		split();
		return *(MTPDupdateContactLink*)data;
	}
	const MTPDupdateContactLink &c_updateContactLink() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateContactLink) throw mtpErrorWrongTypeId(_type, mtpc_updateContactLink);
		return *(const MTPDupdateContactLink*)data;
	}

	MTPDupdateActivation &_updateActivation() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateActivation) throw mtpErrorWrongTypeId(_type, mtpc_updateActivation);
		split();
		return *(MTPDupdateActivation*)data;
	}
	const MTPDupdateActivation &c_updateActivation() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateActivation) throw mtpErrorWrongTypeId(_type, mtpc_updateActivation);
		return *(const MTPDupdateActivation*)data;
	}

	MTPDupdateNewAuthorization &_updateNewAuthorization() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateNewAuthorization) throw mtpErrorWrongTypeId(_type, mtpc_updateNewAuthorization);
		split();
		return *(MTPDupdateNewAuthorization*)data;
	}
	const MTPDupdateNewAuthorization &c_updateNewAuthorization() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateNewAuthorization) throw mtpErrorWrongTypeId(_type, mtpc_updateNewAuthorization);
		return *(const MTPDupdateNewAuthorization*)data;
	}

	MTPDupdateNewGeoChatMessage &_updateNewGeoChatMessage() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateNewGeoChatMessage) throw mtpErrorWrongTypeId(_type, mtpc_updateNewGeoChatMessage);
		split();
		return *(MTPDupdateNewGeoChatMessage*)data;
	}
	const MTPDupdateNewGeoChatMessage &c_updateNewGeoChatMessage() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateNewGeoChatMessage) throw mtpErrorWrongTypeId(_type, mtpc_updateNewGeoChatMessage);
		return *(const MTPDupdateNewGeoChatMessage*)data;
	}

	MTPDupdateNewEncryptedMessage &_updateNewEncryptedMessage() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateNewEncryptedMessage) throw mtpErrorWrongTypeId(_type, mtpc_updateNewEncryptedMessage);
		split();
		return *(MTPDupdateNewEncryptedMessage*)data;
	}
	const MTPDupdateNewEncryptedMessage &c_updateNewEncryptedMessage() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateNewEncryptedMessage) throw mtpErrorWrongTypeId(_type, mtpc_updateNewEncryptedMessage);
		return *(const MTPDupdateNewEncryptedMessage*)data;
	}

	MTPDupdateEncryptedChatTyping &_updateEncryptedChatTyping() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateEncryptedChatTyping) throw mtpErrorWrongTypeId(_type, mtpc_updateEncryptedChatTyping);
		split();
		return *(MTPDupdateEncryptedChatTyping*)data;
	}
	const MTPDupdateEncryptedChatTyping &c_updateEncryptedChatTyping() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateEncryptedChatTyping) throw mtpErrorWrongTypeId(_type, mtpc_updateEncryptedChatTyping);
		return *(const MTPDupdateEncryptedChatTyping*)data;
	}

	MTPDupdateEncryption &_updateEncryption() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateEncryption) throw mtpErrorWrongTypeId(_type, mtpc_updateEncryption);
		split();
		return *(MTPDupdateEncryption*)data;
	}
	const MTPDupdateEncryption &c_updateEncryption() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateEncryption) throw mtpErrorWrongTypeId(_type, mtpc_updateEncryption);
		return *(const MTPDupdateEncryption*)data;
	}

	MTPDupdateEncryptedMessagesRead &_updateEncryptedMessagesRead() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateEncryptedMessagesRead) throw mtpErrorWrongTypeId(_type, mtpc_updateEncryptedMessagesRead);
		split();
		return *(MTPDupdateEncryptedMessagesRead*)data;
	}
	const MTPDupdateEncryptedMessagesRead &c_updateEncryptedMessagesRead() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateEncryptedMessagesRead) throw mtpErrorWrongTypeId(_type, mtpc_updateEncryptedMessagesRead);
		return *(const MTPDupdateEncryptedMessagesRead*)data;
	}

	MTPDupdateChatParticipantAdd &_updateChatParticipantAdd() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateChatParticipantAdd) throw mtpErrorWrongTypeId(_type, mtpc_updateChatParticipantAdd);
		split();
		return *(MTPDupdateChatParticipantAdd*)data;
	}
	const MTPDupdateChatParticipantAdd &c_updateChatParticipantAdd() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateChatParticipantAdd) throw mtpErrorWrongTypeId(_type, mtpc_updateChatParticipantAdd);
		return *(const MTPDupdateChatParticipantAdd*)data;
	}

	MTPDupdateChatParticipantDelete &_updateChatParticipantDelete() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateChatParticipantDelete) throw mtpErrorWrongTypeId(_type, mtpc_updateChatParticipantDelete);
		split();
		return *(MTPDupdateChatParticipantDelete*)data;
	}
	const MTPDupdateChatParticipantDelete &c_updateChatParticipantDelete() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateChatParticipantDelete) throw mtpErrorWrongTypeId(_type, mtpc_updateChatParticipantDelete);
		return *(const MTPDupdateChatParticipantDelete*)data;
	}

	MTPDupdateDcOptions &_updateDcOptions() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateDcOptions) throw mtpErrorWrongTypeId(_type, mtpc_updateDcOptions);
		split();
		return *(MTPDupdateDcOptions*)data;
	}
	const MTPDupdateDcOptions &c_updateDcOptions() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateDcOptions) throw mtpErrorWrongTypeId(_type, mtpc_updateDcOptions);
		return *(const MTPDupdateDcOptions*)data;
	}

	MTPDupdateUserBlocked &_updateUserBlocked() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateUserBlocked) throw mtpErrorWrongTypeId(_type, mtpc_updateUserBlocked);
		split();
		return *(MTPDupdateUserBlocked*)data;
	}
	const MTPDupdateUserBlocked &c_updateUserBlocked() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateUserBlocked) throw mtpErrorWrongTypeId(_type, mtpc_updateUserBlocked);
		return *(const MTPDupdateUserBlocked*)data;
	}

	MTPDupdateNotifySettings &_updateNotifySettings() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateNotifySettings) throw mtpErrorWrongTypeId(_type, mtpc_updateNotifySettings);
		split();
		return *(MTPDupdateNotifySettings*)data;
	}
	const MTPDupdateNotifySettings &c_updateNotifySettings() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateNotifySettings) throw mtpErrorWrongTypeId(_type, mtpc_updateNotifySettings);
		return *(const MTPDupdateNotifySettings*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPupdate(mtpTypeId type);
	explicit MTPupdate(MTPDupdateNewMessage *_data);
	explicit MTPupdate(MTPDupdateMessageID *_data);
	explicit MTPupdate(MTPDupdateReadMessages *_data);
	explicit MTPupdate(MTPDupdateDeleteMessages *_data);
	explicit MTPupdate(MTPDupdateRestoreMessages *_data);
	explicit MTPupdate(MTPDupdateUserTyping *_data);
	explicit MTPupdate(MTPDupdateChatUserTyping *_data);
	explicit MTPupdate(MTPDupdateChatParticipants *_data);
	explicit MTPupdate(MTPDupdateUserStatus *_data);
	explicit MTPupdate(MTPDupdateUserName *_data);
	explicit MTPupdate(MTPDupdateUserPhoto *_data);
	explicit MTPupdate(MTPDupdateContactRegistered *_data);
	explicit MTPupdate(MTPDupdateContactLink *_data);
	explicit MTPupdate(MTPDupdateActivation *_data);
	explicit MTPupdate(MTPDupdateNewAuthorization *_data);
	explicit MTPupdate(MTPDupdateNewGeoChatMessage *_data);
	explicit MTPupdate(MTPDupdateNewEncryptedMessage *_data);
	explicit MTPupdate(MTPDupdateEncryptedChatTyping *_data);
	explicit MTPupdate(MTPDupdateEncryption *_data);
	explicit MTPupdate(MTPDupdateEncryptedMessagesRead *_data);
	explicit MTPupdate(MTPDupdateChatParticipantAdd *_data);
	explicit MTPupdate(MTPDupdateChatParticipantDelete *_data);
	explicit MTPupdate(MTPDupdateDcOptions *_data);
	explicit MTPupdate(MTPDupdateUserBlocked *_data);
	explicit MTPupdate(MTPDupdateNotifySettings *_data);

	friend MTPupdate MTP_updateNewMessage(const MTPMessage &_message, MTPint _pts);
	friend MTPupdate MTP_updateMessageID(MTPint _id, const MTPlong &_random_id);
	friend MTPupdate MTP_updateReadMessages(const MTPVector<MTPint> &_messages, MTPint _pts);
	friend MTPupdate MTP_updateDeleteMessages(const MTPVector<MTPint> &_messages, MTPint _pts);
	friend MTPupdate MTP_updateRestoreMessages(const MTPVector<MTPint> &_messages, MTPint _pts);
	friend MTPupdate MTP_updateUserTyping(MTPint _user_id);
	friend MTPupdate MTP_updateChatUserTyping(MTPint _chat_id, MTPint _user_id);
	friend MTPupdate MTP_updateChatParticipants(const MTPChatParticipants &_participants);
	friend MTPupdate MTP_updateUserStatus(MTPint _user_id, const MTPUserStatus &_status);
	friend MTPupdate MTP_updateUserName(MTPint _user_id, const MTPstring &_first_name, const MTPstring &_last_name);
	friend MTPupdate MTP_updateUserPhoto(MTPint _user_id, MTPint _date, const MTPUserProfilePhoto &_photo, MTPBool _previous);
	friend MTPupdate MTP_updateContactRegistered(MTPint _user_id, MTPint _date);
	friend MTPupdate MTP_updateContactLink(MTPint _user_id, const MTPcontacts_MyLink &_my_link, const MTPcontacts_ForeignLink &_foreign_link);
	friend MTPupdate MTP_updateActivation(MTPint _user_id);
	friend MTPupdate MTP_updateNewAuthorization(const MTPlong &_auth_key_id, MTPint _date, const MTPstring &_device, const MTPstring &_location);
	friend MTPupdate MTP_updateNewGeoChatMessage(const MTPGeoChatMessage &_message);
	friend MTPupdate MTP_updateNewEncryptedMessage(const MTPEncryptedMessage &_message, MTPint _qts);
	friend MTPupdate MTP_updateEncryptedChatTyping(MTPint _chat_id);
	friend MTPupdate MTP_updateEncryption(const MTPEncryptedChat &_chat, MTPint _date);
	friend MTPupdate MTP_updateEncryptedMessagesRead(MTPint _chat_id, MTPint _max_date, MTPint _date);
	friend MTPupdate MTP_updateChatParticipantAdd(MTPint _chat_id, MTPint _user_id, MTPint _inviter_id, MTPint _version);
	friend MTPupdate MTP_updateChatParticipantDelete(MTPint _chat_id, MTPint _user_id, MTPint _version);
	friend MTPupdate MTP_updateDcOptions(const MTPVector<MTPDcOption> &_dc_options);
	friend MTPupdate MTP_updateUserBlocked(MTPint _user_id, MTPBool _blocked);
	friend MTPupdate MTP_updateNotifySettings(const MTPNotifyPeer &_peer, const MTPPeerNotifySettings &_notify_settings);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPupdate> MTPUpdate;

class MTPupdates_state : private mtpDataOwner {
public:
	MTPupdates_state();
	MTPupdates_state(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_updates_state) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDupdates_state &_updates_state() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDupdates_state*)data;
	}
	const MTPDupdates_state &c_updates_state() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDupdates_state*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_updates_state);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPupdates_state(MTPDupdates_state *_data);

	friend MTPupdates_state MTP_updates_state(MTPint _pts, MTPint _qts, MTPint _date, MTPint _seq, MTPint _unread_count);
};
typedef MTPBoxed<MTPupdates_state> MTPupdates_State;

class MTPupdates_difference : private mtpDataOwner {
public:
	MTPupdates_difference() : mtpDataOwner(0), _type(0) {
	}
	MTPupdates_difference(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDupdates_differenceEmpty &_updates_differenceEmpty() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updates_differenceEmpty) throw mtpErrorWrongTypeId(_type, mtpc_updates_differenceEmpty);
		split();
		return *(MTPDupdates_differenceEmpty*)data;
	}
	const MTPDupdates_differenceEmpty &c_updates_differenceEmpty() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updates_differenceEmpty) throw mtpErrorWrongTypeId(_type, mtpc_updates_differenceEmpty);
		return *(const MTPDupdates_differenceEmpty*)data;
	}

	MTPDupdates_difference &_updates_difference() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updates_difference) throw mtpErrorWrongTypeId(_type, mtpc_updates_difference);
		split();
		return *(MTPDupdates_difference*)data;
	}
	const MTPDupdates_difference &c_updates_difference() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updates_difference) throw mtpErrorWrongTypeId(_type, mtpc_updates_difference);
		return *(const MTPDupdates_difference*)data;
	}

	MTPDupdates_differenceSlice &_updates_differenceSlice() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updates_differenceSlice) throw mtpErrorWrongTypeId(_type, mtpc_updates_differenceSlice);
		split();
		return *(MTPDupdates_differenceSlice*)data;
	}
	const MTPDupdates_differenceSlice &c_updates_differenceSlice() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updates_differenceSlice) throw mtpErrorWrongTypeId(_type, mtpc_updates_differenceSlice);
		return *(const MTPDupdates_differenceSlice*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPupdates_difference(mtpTypeId type);
	explicit MTPupdates_difference(MTPDupdates_differenceEmpty *_data);
	explicit MTPupdates_difference(MTPDupdates_difference *_data);
	explicit MTPupdates_difference(MTPDupdates_differenceSlice *_data);

	friend MTPupdates_difference MTP_updates_differenceEmpty(MTPint _date, MTPint _seq);
	friend MTPupdates_difference MTP_updates_difference(const MTPVector<MTPMessage> &_new_messages, const MTPVector<MTPEncryptedMessage> &_new_encrypted_messages, const MTPVector<MTPUpdate> &_other_updates, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users, const MTPupdates_State &_state);
	friend MTPupdates_difference MTP_updates_differenceSlice(const MTPVector<MTPMessage> &_new_messages, const MTPVector<MTPEncryptedMessage> &_new_encrypted_messages, const MTPVector<MTPUpdate> &_other_updates, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users, const MTPupdates_State &_intermediate_state);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPupdates_difference> MTPupdates_Difference;

class MTPupdates : private mtpDataOwner {
public:
	MTPupdates() : mtpDataOwner(0), _type(0) {
	}
	MTPupdates(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDupdateShortMessage &_updateShortMessage() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateShortMessage) throw mtpErrorWrongTypeId(_type, mtpc_updateShortMessage);
		split();
		return *(MTPDupdateShortMessage*)data;
	}
	const MTPDupdateShortMessage &c_updateShortMessage() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateShortMessage) throw mtpErrorWrongTypeId(_type, mtpc_updateShortMessage);
		return *(const MTPDupdateShortMessage*)data;
	}

	MTPDupdateShortChatMessage &_updateShortChatMessage() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateShortChatMessage) throw mtpErrorWrongTypeId(_type, mtpc_updateShortChatMessage);
		split();
		return *(MTPDupdateShortChatMessage*)data;
	}
	const MTPDupdateShortChatMessage &c_updateShortChatMessage() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateShortChatMessage) throw mtpErrorWrongTypeId(_type, mtpc_updateShortChatMessage);
		return *(const MTPDupdateShortChatMessage*)data;
	}

	MTPDupdateShort &_updateShort() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateShort) throw mtpErrorWrongTypeId(_type, mtpc_updateShort);
		split();
		return *(MTPDupdateShort*)data;
	}
	const MTPDupdateShort &c_updateShort() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updateShort) throw mtpErrorWrongTypeId(_type, mtpc_updateShort);
		return *(const MTPDupdateShort*)data;
	}

	MTPDupdatesCombined &_updatesCombined() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updatesCombined) throw mtpErrorWrongTypeId(_type, mtpc_updatesCombined);
		split();
		return *(MTPDupdatesCombined*)data;
	}
	const MTPDupdatesCombined &c_updatesCombined() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updatesCombined) throw mtpErrorWrongTypeId(_type, mtpc_updatesCombined);
		return *(const MTPDupdatesCombined*)data;
	}

	MTPDupdates &_updates() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updates) throw mtpErrorWrongTypeId(_type, mtpc_updates);
		split();
		return *(MTPDupdates*)data;
	}
	const MTPDupdates &c_updates() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_updates) throw mtpErrorWrongTypeId(_type, mtpc_updates);
		return *(const MTPDupdates*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPupdates(mtpTypeId type);
	explicit MTPupdates(MTPDupdateShortMessage *_data);
	explicit MTPupdates(MTPDupdateShortChatMessage *_data);
	explicit MTPupdates(MTPDupdateShort *_data);
	explicit MTPupdates(MTPDupdatesCombined *_data);
	explicit MTPupdates(MTPDupdates *_data);

	friend MTPupdates MTP_updatesTooLong();
	friend MTPupdates MTP_updateShortMessage(MTPint _id, MTPint _from_id, const MTPstring &_message, MTPint _pts, MTPint _date, MTPint _seq);
	friend MTPupdates MTP_updateShortChatMessage(MTPint _id, MTPint _from_id, MTPint _chat_id, const MTPstring &_message, MTPint _pts, MTPint _date, MTPint _seq);
	friend MTPupdates MTP_updateShort(const MTPUpdate &_update, MTPint _date);
	friend MTPupdates MTP_updatesCombined(const MTPVector<MTPUpdate> &_updates, const MTPVector<MTPUser> &_users, const MTPVector<MTPChat> &_chats, MTPint _date, MTPint _seq_start, MTPint _seq);
	friend MTPupdates MTP_updates(const MTPVector<MTPUpdate> &_updates, const MTPVector<MTPUser> &_users, const MTPVector<MTPChat> &_chats, MTPint _date, MTPint _seq);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPupdates> MTPUpdates;

class MTPphotos_photos : private mtpDataOwner {
public:
	MTPphotos_photos() : mtpDataOwner(0), _type(0) {
	}
	MTPphotos_photos(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDphotos_photos &_photos_photos() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_photos_photos) throw mtpErrorWrongTypeId(_type, mtpc_photos_photos);
		split();
		return *(MTPDphotos_photos*)data;
	}
	const MTPDphotos_photos &c_photos_photos() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_photos_photos) throw mtpErrorWrongTypeId(_type, mtpc_photos_photos);
		return *(const MTPDphotos_photos*)data;
	}

	MTPDphotos_photosSlice &_photos_photosSlice() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_photos_photosSlice) throw mtpErrorWrongTypeId(_type, mtpc_photos_photosSlice);
		split();
		return *(MTPDphotos_photosSlice*)data;
	}
	const MTPDphotos_photosSlice &c_photos_photosSlice() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_photos_photosSlice) throw mtpErrorWrongTypeId(_type, mtpc_photos_photosSlice);
		return *(const MTPDphotos_photosSlice*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPphotos_photos(mtpTypeId type);
	explicit MTPphotos_photos(MTPDphotos_photos *_data);
	explicit MTPphotos_photos(MTPDphotos_photosSlice *_data);

	friend MTPphotos_photos MTP_photos_photos(const MTPVector<MTPPhoto> &_photos, const MTPVector<MTPUser> &_users);
	friend MTPphotos_photos MTP_photos_photosSlice(MTPint _count, const MTPVector<MTPPhoto> &_photos, const MTPVector<MTPUser> &_users);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPphotos_photos> MTPphotos_Photos;

class MTPphotos_photo : private mtpDataOwner {
public:
	MTPphotos_photo();
	MTPphotos_photo(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_photos_photo) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDphotos_photo &_photos_photo() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDphotos_photo*)data;
	}
	const MTPDphotos_photo &c_photos_photo() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDphotos_photo*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_photos_photo);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPphotos_photo(MTPDphotos_photo *_data);

	friend MTPphotos_photo MTP_photos_photo(const MTPPhoto &_photo, const MTPVector<MTPUser> &_users);
};
typedef MTPBoxed<MTPphotos_photo> MTPphotos_Photo;

class MTPupload_file : private mtpDataOwner {
public:
	MTPupload_file();
	MTPupload_file(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_upload_file) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDupload_file &_upload_file() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDupload_file*)data;
	}
	const MTPDupload_file &c_upload_file() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDupload_file*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_upload_file);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPupload_file(MTPDupload_file *_data);

	friend MTPupload_file MTP_upload_file(const MTPstorage_FileType &_type, MTPint _mtime, const MTPbytes &_bytes);
};
typedef MTPBoxed<MTPupload_file> MTPupload_File;

class MTPdcOption : private mtpDataOwner {
public:
	MTPdcOption();
	MTPdcOption(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_dcOption) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDdcOption &_dcOption() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDdcOption*)data;
	}
	const MTPDdcOption &c_dcOption() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDdcOption*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_dcOption);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPdcOption(MTPDdcOption *_data);

	friend MTPdcOption MTP_dcOption(MTPint _id, const MTPstring &_hostname, const MTPstring &_ip_address, MTPint _port);
};
typedef MTPBoxed<MTPdcOption> MTPDcOption;

class MTPconfig : private mtpDataOwner {
public:
	MTPconfig();
	MTPconfig(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_config) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDconfig &_config() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDconfig*)data;
	}
	const MTPDconfig &c_config() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDconfig*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_config);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPconfig(MTPDconfig *_data);

	friend MTPconfig MTP_config(MTPint _date, MTPBool _test_mode, MTPint _this_dc, const MTPVector<MTPDcOption> &_dc_options, MTPint _chat_size_max, MTPint _broadcast_size_max);
};
typedef MTPBoxed<MTPconfig> MTPConfig;

class MTPnearestDc : private mtpDataOwner {
public:
	MTPnearestDc();
	MTPnearestDc(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_nearestDc) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDnearestDc &_nearestDc() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDnearestDc*)data;
	}
	const MTPDnearestDc &c_nearestDc() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDnearestDc*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_nearestDc);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPnearestDc(MTPDnearestDc *_data);

	friend MTPnearestDc MTP_nearestDc(const MTPstring &_country, MTPint _this_dc, MTPint _nearest_dc);
};
typedef MTPBoxed<MTPnearestDc> MTPNearestDc;

class MTPhelp_appUpdate : private mtpDataOwner {
public:
	MTPhelp_appUpdate() : mtpDataOwner(0), _type(0) {
	}
	MTPhelp_appUpdate(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDhelp_appUpdate &_help_appUpdate() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_help_appUpdate) throw mtpErrorWrongTypeId(_type, mtpc_help_appUpdate);
		split();
		return *(MTPDhelp_appUpdate*)data;
	}
	const MTPDhelp_appUpdate &c_help_appUpdate() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_help_appUpdate) throw mtpErrorWrongTypeId(_type, mtpc_help_appUpdate);
		return *(const MTPDhelp_appUpdate*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPhelp_appUpdate(mtpTypeId type);
	explicit MTPhelp_appUpdate(MTPDhelp_appUpdate *_data);

	friend MTPhelp_appUpdate MTP_help_appUpdate(MTPint _id, MTPBool _critical, const MTPstring &_url, const MTPstring &_text);
	friend MTPhelp_appUpdate MTP_help_noAppUpdate();

	mtpTypeId _type;
};
typedef MTPBoxed<MTPhelp_appUpdate> MTPhelp_AppUpdate;

class MTPhelp_inviteText : private mtpDataOwner {
public:
	MTPhelp_inviteText();
	MTPhelp_inviteText(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_help_inviteText) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDhelp_inviteText &_help_inviteText() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDhelp_inviteText*)data;
	}
	const MTPDhelp_inviteText &c_help_inviteText() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDhelp_inviteText*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_help_inviteText);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPhelp_inviteText(MTPDhelp_inviteText *_data);

	friend MTPhelp_inviteText MTP_help_inviteText(const MTPstring &_message);
};
typedef MTPBoxed<MTPhelp_inviteText> MTPhelp_InviteText;

class MTPinputGeoChat : private mtpDataOwner {
public:
	MTPinputGeoChat();
	MTPinputGeoChat(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_inputGeoChat) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDinputGeoChat &_inputGeoChat() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDinputGeoChat*)data;
	}
	const MTPDinputGeoChat &c_inputGeoChat() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDinputGeoChat*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_inputGeoChat);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPinputGeoChat(MTPDinputGeoChat *_data);

	friend MTPinputGeoChat MTP_inputGeoChat(MTPint _chat_id, const MTPlong &_access_hash);
};
typedef MTPBoxed<MTPinputGeoChat> MTPInputGeoChat;

class MTPgeoChatMessage : private mtpDataOwner {
public:
	MTPgeoChatMessage() : mtpDataOwner(0), _type(0) {
	}
	MTPgeoChatMessage(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDgeoChatMessageEmpty &_geoChatMessageEmpty() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_geoChatMessageEmpty) throw mtpErrorWrongTypeId(_type, mtpc_geoChatMessageEmpty);
		split();
		return *(MTPDgeoChatMessageEmpty*)data;
	}
	const MTPDgeoChatMessageEmpty &c_geoChatMessageEmpty() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_geoChatMessageEmpty) throw mtpErrorWrongTypeId(_type, mtpc_geoChatMessageEmpty);
		return *(const MTPDgeoChatMessageEmpty*)data;
	}

	MTPDgeoChatMessage &_geoChatMessage() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_geoChatMessage) throw mtpErrorWrongTypeId(_type, mtpc_geoChatMessage);
		split();
		return *(MTPDgeoChatMessage*)data;
	}
	const MTPDgeoChatMessage &c_geoChatMessage() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_geoChatMessage) throw mtpErrorWrongTypeId(_type, mtpc_geoChatMessage);
		return *(const MTPDgeoChatMessage*)data;
	}

	MTPDgeoChatMessageService &_geoChatMessageService() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_geoChatMessageService) throw mtpErrorWrongTypeId(_type, mtpc_geoChatMessageService);
		split();
		return *(MTPDgeoChatMessageService*)data;
	}
	const MTPDgeoChatMessageService &c_geoChatMessageService() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_geoChatMessageService) throw mtpErrorWrongTypeId(_type, mtpc_geoChatMessageService);
		return *(const MTPDgeoChatMessageService*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPgeoChatMessage(mtpTypeId type);
	explicit MTPgeoChatMessage(MTPDgeoChatMessageEmpty *_data);
	explicit MTPgeoChatMessage(MTPDgeoChatMessage *_data);
	explicit MTPgeoChatMessage(MTPDgeoChatMessageService *_data);

	friend MTPgeoChatMessage MTP_geoChatMessageEmpty(MTPint _chat_id, MTPint _id);
	friend MTPgeoChatMessage MTP_geoChatMessage(MTPint _chat_id, MTPint _id, MTPint _from_id, MTPint _date, const MTPstring &_message, const MTPMessageMedia &_media);
	friend MTPgeoChatMessage MTP_geoChatMessageService(MTPint _chat_id, MTPint _id, MTPint _from_id, MTPint _date, const MTPMessageAction &_action);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPgeoChatMessage> MTPGeoChatMessage;

class MTPgeochats_statedMessage : private mtpDataOwner {
public:
	MTPgeochats_statedMessage();
	MTPgeochats_statedMessage(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_geochats_statedMessage) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDgeochats_statedMessage &_geochats_statedMessage() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDgeochats_statedMessage*)data;
	}
	const MTPDgeochats_statedMessage &c_geochats_statedMessage() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDgeochats_statedMessage*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_geochats_statedMessage);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPgeochats_statedMessage(MTPDgeochats_statedMessage *_data);

	friend MTPgeochats_statedMessage MTP_geochats_statedMessage(const MTPGeoChatMessage &_message, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users, MTPint _seq);
};
typedef MTPBoxed<MTPgeochats_statedMessage> MTPgeochats_StatedMessage;

class MTPgeochats_located : private mtpDataOwner {
public:
	MTPgeochats_located();
	MTPgeochats_located(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_geochats_located) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDgeochats_located &_geochats_located() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDgeochats_located*)data;
	}
	const MTPDgeochats_located &c_geochats_located() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDgeochats_located*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_geochats_located);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPgeochats_located(MTPDgeochats_located *_data);

	friend MTPgeochats_located MTP_geochats_located(const MTPVector<MTPChatLocated> &_results, const MTPVector<MTPGeoChatMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users);
};
typedef MTPBoxed<MTPgeochats_located> MTPgeochats_Located;

class MTPgeochats_messages : private mtpDataOwner {
public:
	MTPgeochats_messages() : mtpDataOwner(0), _type(0) {
	}
	MTPgeochats_messages(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDgeochats_messages &_geochats_messages() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_geochats_messages) throw mtpErrorWrongTypeId(_type, mtpc_geochats_messages);
		split();
		return *(MTPDgeochats_messages*)data;
	}
	const MTPDgeochats_messages &c_geochats_messages() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_geochats_messages) throw mtpErrorWrongTypeId(_type, mtpc_geochats_messages);
		return *(const MTPDgeochats_messages*)data;
	}

	MTPDgeochats_messagesSlice &_geochats_messagesSlice() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_geochats_messagesSlice) throw mtpErrorWrongTypeId(_type, mtpc_geochats_messagesSlice);
		split();
		return *(MTPDgeochats_messagesSlice*)data;
	}
	const MTPDgeochats_messagesSlice &c_geochats_messagesSlice() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_geochats_messagesSlice) throw mtpErrorWrongTypeId(_type, mtpc_geochats_messagesSlice);
		return *(const MTPDgeochats_messagesSlice*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPgeochats_messages(mtpTypeId type);
	explicit MTPgeochats_messages(MTPDgeochats_messages *_data);
	explicit MTPgeochats_messages(MTPDgeochats_messagesSlice *_data);

	friend MTPgeochats_messages MTP_geochats_messages(const MTPVector<MTPGeoChatMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users);
	friend MTPgeochats_messages MTP_geochats_messagesSlice(MTPint _count, const MTPVector<MTPGeoChatMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPgeochats_messages> MTPgeochats_Messages;

class MTPencryptedChat : private mtpDataOwner {
public:
	MTPencryptedChat() : mtpDataOwner(0), _type(0) {
	}
	MTPencryptedChat(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDencryptedChatEmpty &_encryptedChatEmpty() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_encryptedChatEmpty) throw mtpErrorWrongTypeId(_type, mtpc_encryptedChatEmpty);
		split();
		return *(MTPDencryptedChatEmpty*)data;
	}
	const MTPDencryptedChatEmpty &c_encryptedChatEmpty() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_encryptedChatEmpty) throw mtpErrorWrongTypeId(_type, mtpc_encryptedChatEmpty);
		return *(const MTPDencryptedChatEmpty*)data;
	}

	MTPDencryptedChatWaiting &_encryptedChatWaiting() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_encryptedChatWaiting) throw mtpErrorWrongTypeId(_type, mtpc_encryptedChatWaiting);
		split();
		return *(MTPDencryptedChatWaiting*)data;
	}
	const MTPDencryptedChatWaiting &c_encryptedChatWaiting() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_encryptedChatWaiting) throw mtpErrorWrongTypeId(_type, mtpc_encryptedChatWaiting);
		return *(const MTPDencryptedChatWaiting*)data;
	}

	MTPDencryptedChatRequested &_encryptedChatRequested() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_encryptedChatRequested) throw mtpErrorWrongTypeId(_type, mtpc_encryptedChatRequested);
		split();
		return *(MTPDencryptedChatRequested*)data;
	}
	const MTPDencryptedChatRequested &c_encryptedChatRequested() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_encryptedChatRequested) throw mtpErrorWrongTypeId(_type, mtpc_encryptedChatRequested);
		return *(const MTPDencryptedChatRequested*)data;
	}

	MTPDencryptedChat &_encryptedChat() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_encryptedChat) throw mtpErrorWrongTypeId(_type, mtpc_encryptedChat);
		split();
		return *(MTPDencryptedChat*)data;
	}
	const MTPDencryptedChat &c_encryptedChat() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_encryptedChat) throw mtpErrorWrongTypeId(_type, mtpc_encryptedChat);
		return *(const MTPDencryptedChat*)data;
	}

	MTPDencryptedChatDiscarded &_encryptedChatDiscarded() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_encryptedChatDiscarded) throw mtpErrorWrongTypeId(_type, mtpc_encryptedChatDiscarded);
		split();
		return *(MTPDencryptedChatDiscarded*)data;
	}
	const MTPDencryptedChatDiscarded &c_encryptedChatDiscarded() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_encryptedChatDiscarded) throw mtpErrorWrongTypeId(_type, mtpc_encryptedChatDiscarded);
		return *(const MTPDencryptedChatDiscarded*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPencryptedChat(mtpTypeId type);
	explicit MTPencryptedChat(MTPDencryptedChatEmpty *_data);
	explicit MTPencryptedChat(MTPDencryptedChatWaiting *_data);
	explicit MTPencryptedChat(MTPDencryptedChatRequested *_data);
	explicit MTPencryptedChat(MTPDencryptedChat *_data);
	explicit MTPencryptedChat(MTPDencryptedChatDiscarded *_data);

	friend MTPencryptedChat MTP_encryptedChatEmpty(MTPint _id);
	friend MTPencryptedChat MTP_encryptedChatWaiting(MTPint _id, const MTPlong &_access_hash, MTPint _date, MTPint _admin_id, MTPint _participant_id);
	friend MTPencryptedChat MTP_encryptedChatRequested(MTPint _id, const MTPlong &_access_hash, MTPint _date, MTPint _admin_id, MTPint _participant_id, const MTPbytes &_g_a);
	friend MTPencryptedChat MTP_encryptedChat(MTPint _id, const MTPlong &_access_hash, MTPint _date, MTPint _admin_id, MTPint _participant_id, const MTPbytes &_g_a_or_b, const MTPlong &_key_fingerprint);
	friend MTPencryptedChat MTP_encryptedChatDiscarded(MTPint _id);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPencryptedChat> MTPEncryptedChat;

class MTPinputEncryptedChat : private mtpDataOwner {
public:
	MTPinputEncryptedChat();
	MTPinputEncryptedChat(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_inputEncryptedChat) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDinputEncryptedChat &_inputEncryptedChat() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDinputEncryptedChat*)data;
	}
	const MTPDinputEncryptedChat &c_inputEncryptedChat() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDinputEncryptedChat*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_inputEncryptedChat);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPinputEncryptedChat(MTPDinputEncryptedChat *_data);

	friend MTPinputEncryptedChat MTP_inputEncryptedChat(MTPint _chat_id, const MTPlong &_access_hash);
};
typedef MTPBoxed<MTPinputEncryptedChat> MTPInputEncryptedChat;

class MTPencryptedFile : private mtpDataOwner {
public:
	MTPencryptedFile() : mtpDataOwner(0), _type(0) {
	}
	MTPencryptedFile(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDencryptedFile &_encryptedFile() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_encryptedFile) throw mtpErrorWrongTypeId(_type, mtpc_encryptedFile);
		split();
		return *(MTPDencryptedFile*)data;
	}
	const MTPDencryptedFile &c_encryptedFile() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_encryptedFile) throw mtpErrorWrongTypeId(_type, mtpc_encryptedFile);
		return *(const MTPDencryptedFile*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPencryptedFile(mtpTypeId type);
	explicit MTPencryptedFile(MTPDencryptedFile *_data);

	friend MTPencryptedFile MTP_encryptedFileEmpty();
	friend MTPencryptedFile MTP_encryptedFile(const MTPlong &_id, const MTPlong &_access_hash, MTPint _size, MTPint _dc_id, MTPint _key_fingerprint);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPencryptedFile> MTPEncryptedFile;

class MTPinputEncryptedFile : private mtpDataOwner {
public:
	MTPinputEncryptedFile() : mtpDataOwner(0), _type(0) {
	}
	MTPinputEncryptedFile(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDinputEncryptedFileUploaded &_inputEncryptedFileUploaded() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputEncryptedFileUploaded) throw mtpErrorWrongTypeId(_type, mtpc_inputEncryptedFileUploaded);
		split();
		return *(MTPDinputEncryptedFileUploaded*)data;
	}
	const MTPDinputEncryptedFileUploaded &c_inputEncryptedFileUploaded() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputEncryptedFileUploaded) throw mtpErrorWrongTypeId(_type, mtpc_inputEncryptedFileUploaded);
		return *(const MTPDinputEncryptedFileUploaded*)data;
	}

	MTPDinputEncryptedFile &_inputEncryptedFile() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputEncryptedFile) throw mtpErrorWrongTypeId(_type, mtpc_inputEncryptedFile);
		split();
		return *(MTPDinputEncryptedFile*)data;
	}
	const MTPDinputEncryptedFile &c_inputEncryptedFile() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputEncryptedFile) throw mtpErrorWrongTypeId(_type, mtpc_inputEncryptedFile);
		return *(const MTPDinputEncryptedFile*)data;
	}

	MTPDinputEncryptedFileBigUploaded &_inputEncryptedFileBigUploaded() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputEncryptedFileBigUploaded) throw mtpErrorWrongTypeId(_type, mtpc_inputEncryptedFileBigUploaded);
		split();
		return *(MTPDinputEncryptedFileBigUploaded*)data;
	}
	const MTPDinputEncryptedFileBigUploaded &c_inputEncryptedFileBigUploaded() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputEncryptedFileBigUploaded) throw mtpErrorWrongTypeId(_type, mtpc_inputEncryptedFileBigUploaded);
		return *(const MTPDinputEncryptedFileBigUploaded*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPinputEncryptedFile(mtpTypeId type);
	explicit MTPinputEncryptedFile(MTPDinputEncryptedFileUploaded *_data);
	explicit MTPinputEncryptedFile(MTPDinputEncryptedFile *_data);
	explicit MTPinputEncryptedFile(MTPDinputEncryptedFileBigUploaded *_data);

	friend MTPinputEncryptedFile MTP_inputEncryptedFileEmpty();
	friend MTPinputEncryptedFile MTP_inputEncryptedFileUploaded(const MTPlong &_id, MTPint _parts, const MTPstring &_md5_checksum, MTPint _key_fingerprint);
	friend MTPinputEncryptedFile MTP_inputEncryptedFile(const MTPlong &_id, const MTPlong &_access_hash);
	friend MTPinputEncryptedFile MTP_inputEncryptedFileBigUploaded(const MTPlong &_id, MTPint _parts, MTPint _key_fingerprint);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPinputEncryptedFile> MTPInputEncryptedFile;

class MTPencryptedMessage : private mtpDataOwner {
public:
	MTPencryptedMessage() : mtpDataOwner(0), _type(0) {
	}
	MTPencryptedMessage(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDencryptedMessage &_encryptedMessage() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_encryptedMessage) throw mtpErrorWrongTypeId(_type, mtpc_encryptedMessage);
		split();
		return *(MTPDencryptedMessage*)data;
	}
	const MTPDencryptedMessage &c_encryptedMessage() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_encryptedMessage) throw mtpErrorWrongTypeId(_type, mtpc_encryptedMessage);
		return *(const MTPDencryptedMessage*)data;
	}

	MTPDencryptedMessageService &_encryptedMessageService() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_encryptedMessageService) throw mtpErrorWrongTypeId(_type, mtpc_encryptedMessageService);
		split();
		return *(MTPDencryptedMessageService*)data;
	}
	const MTPDencryptedMessageService &c_encryptedMessageService() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_encryptedMessageService) throw mtpErrorWrongTypeId(_type, mtpc_encryptedMessageService);
		return *(const MTPDencryptedMessageService*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPencryptedMessage(mtpTypeId type);
	explicit MTPencryptedMessage(MTPDencryptedMessage *_data);
	explicit MTPencryptedMessage(MTPDencryptedMessageService *_data);

	friend MTPencryptedMessage MTP_encryptedMessage(const MTPlong &_random_id, MTPint _chat_id, MTPint _date, const MTPbytes &_bytes, const MTPEncryptedFile &_file);
	friend MTPencryptedMessage MTP_encryptedMessageService(const MTPlong &_random_id, MTPint _chat_id, MTPint _date, const MTPbytes &_bytes);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPencryptedMessage> MTPEncryptedMessage;

class MTPdecryptedMessageLayer : private mtpDataOwner {
public:
	MTPdecryptedMessageLayer();
	MTPdecryptedMessageLayer(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_decryptedMessageLayer) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDdecryptedMessageLayer &_decryptedMessageLayer() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDdecryptedMessageLayer*)data;
	}
	const MTPDdecryptedMessageLayer &c_decryptedMessageLayer() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDdecryptedMessageLayer*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_decryptedMessageLayer);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPdecryptedMessageLayer(MTPDdecryptedMessageLayer *_data);

	friend MTPdecryptedMessageLayer MTP_decryptedMessageLayer(MTPint _layer, const MTPDecryptedMessage &_message);
};
typedef MTPBoxed<MTPdecryptedMessageLayer> MTPDecryptedMessageLayer;

class MTPdecryptedMessage : private mtpDataOwner {
public:
	MTPdecryptedMessage() : mtpDataOwner(0), _type(0) {
	}
	MTPdecryptedMessage(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDdecryptedMessage &_decryptedMessage() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_decryptedMessage) throw mtpErrorWrongTypeId(_type, mtpc_decryptedMessage);
		split();
		return *(MTPDdecryptedMessage*)data;
	}
	const MTPDdecryptedMessage &c_decryptedMessage() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_decryptedMessage) throw mtpErrorWrongTypeId(_type, mtpc_decryptedMessage);
		return *(const MTPDdecryptedMessage*)data;
	}

	MTPDdecryptedMessageService &_decryptedMessageService() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_decryptedMessageService) throw mtpErrorWrongTypeId(_type, mtpc_decryptedMessageService);
		split();
		return *(MTPDdecryptedMessageService*)data;
	}
	const MTPDdecryptedMessageService &c_decryptedMessageService() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_decryptedMessageService) throw mtpErrorWrongTypeId(_type, mtpc_decryptedMessageService);
		return *(const MTPDdecryptedMessageService*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPdecryptedMessage(mtpTypeId type);
	explicit MTPdecryptedMessage(MTPDdecryptedMessage *_data);
	explicit MTPdecryptedMessage(MTPDdecryptedMessageService *_data);

	friend MTPdecryptedMessage MTP_decryptedMessage(const MTPlong &_random_id, const MTPbytes &_random_bytes, const MTPstring &_message, const MTPDecryptedMessageMedia &_media);
	friend MTPdecryptedMessage MTP_decryptedMessageService(const MTPlong &_random_id, const MTPbytes &_random_bytes, const MTPDecryptedMessageAction &_action);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPdecryptedMessage> MTPDecryptedMessage;

class MTPdecryptedMessageMedia : private mtpDataOwner {
public:
	MTPdecryptedMessageMedia() : mtpDataOwner(0), _type(0) {
	}
	MTPdecryptedMessageMedia(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDdecryptedMessageMediaPhoto &_decryptedMessageMediaPhoto() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_decryptedMessageMediaPhoto) throw mtpErrorWrongTypeId(_type, mtpc_decryptedMessageMediaPhoto);
		split();
		return *(MTPDdecryptedMessageMediaPhoto*)data;
	}
	const MTPDdecryptedMessageMediaPhoto &c_decryptedMessageMediaPhoto() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_decryptedMessageMediaPhoto) throw mtpErrorWrongTypeId(_type, mtpc_decryptedMessageMediaPhoto);
		return *(const MTPDdecryptedMessageMediaPhoto*)data;
	}

	MTPDdecryptedMessageMediaVideo &_decryptedMessageMediaVideo() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_decryptedMessageMediaVideo) throw mtpErrorWrongTypeId(_type, mtpc_decryptedMessageMediaVideo);
		split();
		return *(MTPDdecryptedMessageMediaVideo*)data;
	}
	const MTPDdecryptedMessageMediaVideo &c_decryptedMessageMediaVideo() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_decryptedMessageMediaVideo) throw mtpErrorWrongTypeId(_type, mtpc_decryptedMessageMediaVideo);
		return *(const MTPDdecryptedMessageMediaVideo*)data;
	}

	MTPDdecryptedMessageMediaGeoPoint &_decryptedMessageMediaGeoPoint() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_decryptedMessageMediaGeoPoint) throw mtpErrorWrongTypeId(_type, mtpc_decryptedMessageMediaGeoPoint);
		split();
		return *(MTPDdecryptedMessageMediaGeoPoint*)data;
	}
	const MTPDdecryptedMessageMediaGeoPoint &c_decryptedMessageMediaGeoPoint() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_decryptedMessageMediaGeoPoint) throw mtpErrorWrongTypeId(_type, mtpc_decryptedMessageMediaGeoPoint);
		return *(const MTPDdecryptedMessageMediaGeoPoint*)data;
	}

	MTPDdecryptedMessageMediaContact &_decryptedMessageMediaContact() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_decryptedMessageMediaContact) throw mtpErrorWrongTypeId(_type, mtpc_decryptedMessageMediaContact);
		split();
		return *(MTPDdecryptedMessageMediaContact*)data;
	}
	const MTPDdecryptedMessageMediaContact &c_decryptedMessageMediaContact() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_decryptedMessageMediaContact) throw mtpErrorWrongTypeId(_type, mtpc_decryptedMessageMediaContact);
		return *(const MTPDdecryptedMessageMediaContact*)data;
	}

	MTPDdecryptedMessageMediaDocument &_decryptedMessageMediaDocument() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_decryptedMessageMediaDocument) throw mtpErrorWrongTypeId(_type, mtpc_decryptedMessageMediaDocument);
		split();
		return *(MTPDdecryptedMessageMediaDocument*)data;
	}
	const MTPDdecryptedMessageMediaDocument &c_decryptedMessageMediaDocument() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_decryptedMessageMediaDocument) throw mtpErrorWrongTypeId(_type, mtpc_decryptedMessageMediaDocument);
		return *(const MTPDdecryptedMessageMediaDocument*)data;
	}

	MTPDdecryptedMessageMediaAudio &_decryptedMessageMediaAudio() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_decryptedMessageMediaAudio) throw mtpErrorWrongTypeId(_type, mtpc_decryptedMessageMediaAudio);
		split();
		return *(MTPDdecryptedMessageMediaAudio*)data;
	}
	const MTPDdecryptedMessageMediaAudio &c_decryptedMessageMediaAudio() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_decryptedMessageMediaAudio) throw mtpErrorWrongTypeId(_type, mtpc_decryptedMessageMediaAudio);
		return *(const MTPDdecryptedMessageMediaAudio*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPdecryptedMessageMedia(mtpTypeId type);
	explicit MTPdecryptedMessageMedia(MTPDdecryptedMessageMediaPhoto *_data);
	explicit MTPdecryptedMessageMedia(MTPDdecryptedMessageMediaVideo *_data);
	explicit MTPdecryptedMessageMedia(MTPDdecryptedMessageMediaGeoPoint *_data);
	explicit MTPdecryptedMessageMedia(MTPDdecryptedMessageMediaContact *_data);
	explicit MTPdecryptedMessageMedia(MTPDdecryptedMessageMediaDocument *_data);
	explicit MTPdecryptedMessageMedia(MTPDdecryptedMessageMediaAudio *_data);

	friend MTPdecryptedMessageMedia MTP_decryptedMessageMediaEmpty();
	friend MTPdecryptedMessageMedia MTP_decryptedMessageMediaPhoto(const MTPbytes &_thumb, MTPint _thumb_w, MTPint _thumb_h, MTPint _w, MTPint _h, MTPint _size, const MTPbytes &_key, const MTPbytes &_iv);
	friend MTPdecryptedMessageMedia MTP_decryptedMessageMediaVideo(const MTPbytes &_thumb, MTPint _thumb_w, MTPint _thumb_h, MTPint _duration, const MTPstring &_mime_type, MTPint _w, MTPint _h, MTPint _size, const MTPbytes &_key, const MTPbytes &_iv);
	friend MTPdecryptedMessageMedia MTP_decryptedMessageMediaGeoPoint(const MTPdouble &_lat, const MTPdouble &_long);
	friend MTPdecryptedMessageMedia MTP_decryptedMessageMediaContact(const MTPstring &_phone_number, const MTPstring &_first_name, const MTPstring &_last_name, MTPint _user_id);
	friend MTPdecryptedMessageMedia MTP_decryptedMessageMediaDocument(const MTPbytes &_thumb, MTPint _thumb_w, MTPint _thumb_h, const MTPstring &_file_name, const MTPstring &_mime_type, MTPint _size, const MTPbytes &_key, const MTPbytes &_iv);
	friend MTPdecryptedMessageMedia MTP_decryptedMessageMediaAudio(MTPint _duration, const MTPstring &_mime_type, MTPint _size, const MTPbytes &_key, const MTPbytes &_iv);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPdecryptedMessageMedia> MTPDecryptedMessageMedia;

class MTPdecryptedMessageAction : private mtpDataOwner {
public:
	MTPdecryptedMessageAction() : mtpDataOwner(0), _type(0) {
	}
	MTPdecryptedMessageAction(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDdecryptedMessageActionSetMessageTTL &_decryptedMessageActionSetMessageTTL() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_decryptedMessageActionSetMessageTTL) throw mtpErrorWrongTypeId(_type, mtpc_decryptedMessageActionSetMessageTTL);
		split();
		return *(MTPDdecryptedMessageActionSetMessageTTL*)data;
	}
	const MTPDdecryptedMessageActionSetMessageTTL &c_decryptedMessageActionSetMessageTTL() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_decryptedMessageActionSetMessageTTL) throw mtpErrorWrongTypeId(_type, mtpc_decryptedMessageActionSetMessageTTL);
		return *(const MTPDdecryptedMessageActionSetMessageTTL*)data;
	}

	MTPDdecryptedMessageActionReadMessages &_decryptedMessageActionReadMessages() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_decryptedMessageActionReadMessages) throw mtpErrorWrongTypeId(_type, mtpc_decryptedMessageActionReadMessages);
		split();
		return *(MTPDdecryptedMessageActionReadMessages*)data;
	}
	const MTPDdecryptedMessageActionReadMessages &c_decryptedMessageActionReadMessages() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_decryptedMessageActionReadMessages) throw mtpErrorWrongTypeId(_type, mtpc_decryptedMessageActionReadMessages);
		return *(const MTPDdecryptedMessageActionReadMessages*)data;
	}

	MTPDdecryptedMessageActionDeleteMessages &_decryptedMessageActionDeleteMessages() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_decryptedMessageActionDeleteMessages) throw mtpErrorWrongTypeId(_type, mtpc_decryptedMessageActionDeleteMessages);
		split();
		return *(MTPDdecryptedMessageActionDeleteMessages*)data;
	}
	const MTPDdecryptedMessageActionDeleteMessages &c_decryptedMessageActionDeleteMessages() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_decryptedMessageActionDeleteMessages) throw mtpErrorWrongTypeId(_type, mtpc_decryptedMessageActionDeleteMessages);
		return *(const MTPDdecryptedMessageActionDeleteMessages*)data;
	}

	MTPDdecryptedMessageActionScreenshotMessages &_decryptedMessageActionScreenshotMessages() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_decryptedMessageActionScreenshotMessages) throw mtpErrorWrongTypeId(_type, mtpc_decryptedMessageActionScreenshotMessages);
		split();
		return *(MTPDdecryptedMessageActionScreenshotMessages*)data;
	}
	const MTPDdecryptedMessageActionScreenshotMessages &c_decryptedMessageActionScreenshotMessages() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_decryptedMessageActionScreenshotMessages) throw mtpErrorWrongTypeId(_type, mtpc_decryptedMessageActionScreenshotMessages);
		return *(const MTPDdecryptedMessageActionScreenshotMessages*)data;
	}

	MTPDdecryptedMessageActionNotifyLayer &_decryptedMessageActionNotifyLayer() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_decryptedMessageActionNotifyLayer) throw mtpErrorWrongTypeId(_type, mtpc_decryptedMessageActionNotifyLayer);
		split();
		return *(MTPDdecryptedMessageActionNotifyLayer*)data;
	}
	const MTPDdecryptedMessageActionNotifyLayer &c_decryptedMessageActionNotifyLayer() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_decryptedMessageActionNotifyLayer) throw mtpErrorWrongTypeId(_type, mtpc_decryptedMessageActionNotifyLayer);
		return *(const MTPDdecryptedMessageActionNotifyLayer*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPdecryptedMessageAction(mtpTypeId type);
	explicit MTPdecryptedMessageAction(MTPDdecryptedMessageActionSetMessageTTL *_data);
	explicit MTPdecryptedMessageAction(MTPDdecryptedMessageActionReadMessages *_data);
	explicit MTPdecryptedMessageAction(MTPDdecryptedMessageActionDeleteMessages *_data);
	explicit MTPdecryptedMessageAction(MTPDdecryptedMessageActionScreenshotMessages *_data);
	explicit MTPdecryptedMessageAction(MTPDdecryptedMessageActionNotifyLayer *_data);

	friend MTPdecryptedMessageAction MTP_decryptedMessageActionSetMessageTTL(MTPint _ttl_seconds);
	friend MTPdecryptedMessageAction MTP_decryptedMessageActionReadMessages(const MTPVector<MTPlong> &_random_ids);
	friend MTPdecryptedMessageAction MTP_decryptedMessageActionDeleteMessages(const MTPVector<MTPlong> &_random_ids);
	friend MTPdecryptedMessageAction MTP_decryptedMessageActionScreenshotMessages(const MTPVector<MTPlong> &_random_ids);
	friend MTPdecryptedMessageAction MTP_decryptedMessageActionFlushHistory();
	friend MTPdecryptedMessageAction MTP_decryptedMessageActionNotifyLayer(MTPint _layer);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPdecryptedMessageAction> MTPDecryptedMessageAction;

class MTPmessages_dhConfig : private mtpDataOwner {
public:
	MTPmessages_dhConfig() : mtpDataOwner(0), _type(0) {
	}
	MTPmessages_dhConfig(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDmessages_dhConfigNotModified &_messages_dhConfigNotModified() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_dhConfigNotModified) throw mtpErrorWrongTypeId(_type, mtpc_messages_dhConfigNotModified);
		split();
		return *(MTPDmessages_dhConfigNotModified*)data;
	}
	const MTPDmessages_dhConfigNotModified &c_messages_dhConfigNotModified() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_dhConfigNotModified) throw mtpErrorWrongTypeId(_type, mtpc_messages_dhConfigNotModified);
		return *(const MTPDmessages_dhConfigNotModified*)data;
	}

	MTPDmessages_dhConfig &_messages_dhConfig() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_dhConfig) throw mtpErrorWrongTypeId(_type, mtpc_messages_dhConfig);
		split();
		return *(MTPDmessages_dhConfig*)data;
	}
	const MTPDmessages_dhConfig &c_messages_dhConfig() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_dhConfig) throw mtpErrorWrongTypeId(_type, mtpc_messages_dhConfig);
		return *(const MTPDmessages_dhConfig*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPmessages_dhConfig(mtpTypeId type);
	explicit MTPmessages_dhConfig(MTPDmessages_dhConfigNotModified *_data);
	explicit MTPmessages_dhConfig(MTPDmessages_dhConfig *_data);

	friend MTPmessages_dhConfig MTP_messages_dhConfigNotModified(const MTPbytes &_random);
	friend MTPmessages_dhConfig MTP_messages_dhConfig(MTPint _g, const MTPbytes &_p, MTPint _version, const MTPbytes &_random);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPmessages_dhConfig> MTPmessages_DhConfig;

class MTPmessages_sentEncryptedMessage : private mtpDataOwner {
public:
	MTPmessages_sentEncryptedMessage() : mtpDataOwner(0), _type(0) {
	}
	MTPmessages_sentEncryptedMessage(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDmessages_sentEncryptedMessage &_messages_sentEncryptedMessage() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_sentEncryptedMessage) throw mtpErrorWrongTypeId(_type, mtpc_messages_sentEncryptedMessage);
		split();
		return *(MTPDmessages_sentEncryptedMessage*)data;
	}
	const MTPDmessages_sentEncryptedMessage &c_messages_sentEncryptedMessage() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_sentEncryptedMessage) throw mtpErrorWrongTypeId(_type, mtpc_messages_sentEncryptedMessage);
		return *(const MTPDmessages_sentEncryptedMessage*)data;
	}

	MTPDmessages_sentEncryptedFile &_messages_sentEncryptedFile() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_sentEncryptedFile) throw mtpErrorWrongTypeId(_type, mtpc_messages_sentEncryptedFile);
		split();
		return *(MTPDmessages_sentEncryptedFile*)data;
	}
	const MTPDmessages_sentEncryptedFile &c_messages_sentEncryptedFile() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_messages_sentEncryptedFile) throw mtpErrorWrongTypeId(_type, mtpc_messages_sentEncryptedFile);
		return *(const MTPDmessages_sentEncryptedFile*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPmessages_sentEncryptedMessage(mtpTypeId type);
	explicit MTPmessages_sentEncryptedMessage(MTPDmessages_sentEncryptedMessage *_data);
	explicit MTPmessages_sentEncryptedMessage(MTPDmessages_sentEncryptedFile *_data);

	friend MTPmessages_sentEncryptedMessage MTP_messages_sentEncryptedMessage(MTPint _date);
	friend MTPmessages_sentEncryptedMessage MTP_messages_sentEncryptedFile(MTPint _date, const MTPEncryptedFile &_file);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPmessages_sentEncryptedMessage> MTPmessages_SentEncryptedMessage;

class MTPinputAudio : private mtpDataOwner {
public:
	MTPinputAudio() : mtpDataOwner(0), _type(0) {
	}
	MTPinputAudio(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDinputAudio &_inputAudio() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputAudio) throw mtpErrorWrongTypeId(_type, mtpc_inputAudio);
		split();
		return *(MTPDinputAudio*)data;
	}
	const MTPDinputAudio &c_inputAudio() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputAudio) throw mtpErrorWrongTypeId(_type, mtpc_inputAudio);
		return *(const MTPDinputAudio*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPinputAudio(mtpTypeId type);
	explicit MTPinputAudio(MTPDinputAudio *_data);

	friend MTPinputAudio MTP_inputAudioEmpty();
	friend MTPinputAudio MTP_inputAudio(const MTPlong &_id, const MTPlong &_access_hash);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPinputAudio> MTPInputAudio;

class MTPinputDocument : private mtpDataOwner {
public:
	MTPinputDocument() : mtpDataOwner(0), _type(0) {
	}
	MTPinputDocument(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDinputDocument &_inputDocument() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputDocument) throw mtpErrorWrongTypeId(_type, mtpc_inputDocument);
		split();
		return *(MTPDinputDocument*)data;
	}
	const MTPDinputDocument &c_inputDocument() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_inputDocument) throw mtpErrorWrongTypeId(_type, mtpc_inputDocument);
		return *(const MTPDinputDocument*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPinputDocument(mtpTypeId type);
	explicit MTPinputDocument(MTPDinputDocument *_data);

	friend MTPinputDocument MTP_inputDocumentEmpty();
	friend MTPinputDocument MTP_inputDocument(const MTPlong &_id, const MTPlong &_access_hash);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPinputDocument> MTPInputDocument;

class MTPaudio : private mtpDataOwner {
public:
	MTPaudio() : mtpDataOwner(0), _type(0) {
	}
	MTPaudio(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDaudioEmpty &_audioEmpty() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_audioEmpty) throw mtpErrorWrongTypeId(_type, mtpc_audioEmpty);
		split();
		return *(MTPDaudioEmpty*)data;
	}
	const MTPDaudioEmpty &c_audioEmpty() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_audioEmpty) throw mtpErrorWrongTypeId(_type, mtpc_audioEmpty);
		return *(const MTPDaudioEmpty*)data;
	}

	MTPDaudio &_audio() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_audio) throw mtpErrorWrongTypeId(_type, mtpc_audio);
		split();
		return *(MTPDaudio*)data;
	}
	const MTPDaudio &c_audio() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_audio) throw mtpErrorWrongTypeId(_type, mtpc_audio);
		return *(const MTPDaudio*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPaudio(mtpTypeId type);
	explicit MTPaudio(MTPDaudioEmpty *_data);
	explicit MTPaudio(MTPDaudio *_data);

	friend MTPaudio MTP_audioEmpty(const MTPlong &_id);
	friend MTPaudio MTP_audio(const MTPlong &_id, const MTPlong &_access_hash, MTPint _user_id, MTPint _date, MTPint _duration, const MTPstring &_mime_type, MTPint _size, MTPint _dc_id);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPaudio> MTPAudio;

class MTPdocument : private mtpDataOwner {
public:
	MTPdocument() : mtpDataOwner(0), _type(0) {
	}
	MTPdocument(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDdocumentEmpty &_documentEmpty() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_documentEmpty) throw mtpErrorWrongTypeId(_type, mtpc_documentEmpty);
		split();
		return *(MTPDdocumentEmpty*)data;
	}
	const MTPDdocumentEmpty &c_documentEmpty() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_documentEmpty) throw mtpErrorWrongTypeId(_type, mtpc_documentEmpty);
		return *(const MTPDdocumentEmpty*)data;
	}

	MTPDdocument &_document() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_document) throw mtpErrorWrongTypeId(_type, mtpc_document);
		split();
		return *(MTPDdocument*)data;
	}
	const MTPDdocument &c_document() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_document) throw mtpErrorWrongTypeId(_type, mtpc_document);
		return *(const MTPDdocument*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPdocument(mtpTypeId type);
	explicit MTPdocument(MTPDdocumentEmpty *_data);
	explicit MTPdocument(MTPDdocument *_data);

	friend MTPdocument MTP_documentEmpty(const MTPlong &_id);
	friend MTPdocument MTP_document(const MTPlong &_id, const MTPlong &_access_hash, MTPint _user_id, MTPint _date, const MTPstring &_file_name, const MTPstring &_mime_type, MTPint _size, const MTPPhotoSize &_thumb, MTPint _dc_id);

	mtpTypeId _type;
};
typedef MTPBoxed<MTPdocument> MTPDocument;

class MTPhelp_support : private mtpDataOwner {
public:
	MTPhelp_support();
	MTPhelp_support(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_help_support) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDhelp_support &_help_support() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDhelp_support*)data;
	}
	const MTPDhelp_support &c_help_support() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDhelp_support*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_help_support);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPhelp_support(MTPDhelp_support *_data);

	friend MTPhelp_support MTP_help_support(const MTPstring &_phone_number, const MTPUser &_user);
};
typedef MTPBoxed<MTPhelp_support> MTPhelp_Support;

class MTPnotifyPeer : private mtpDataOwner {
public:
	MTPnotifyPeer() : mtpDataOwner(0), _type(0) {
	}
	MTPnotifyPeer(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) : mtpDataOwner(0), _type(0) {
		read(from, end, cons);
	}

	MTPDnotifyPeer &_notifyPeer() {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_notifyPeer) throw mtpErrorWrongTypeId(_type, mtpc_notifyPeer);
		split();
		return *(MTPDnotifyPeer*)data;
	}
	const MTPDnotifyPeer &c_notifyPeer() const {
		if (!data) throw mtpErrorUninitialized();
		if (_type != mtpc_notifyPeer) throw mtpErrorWrongTypeId(_type, mtpc_notifyPeer);
		return *(const MTPDnotifyPeer*)data;
	}

	uint32 size() const;
	mtpTypeId type() const;
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons);
	void write(mtpBuffer &to) const;

	typedef void ResponseType;

private:
	explicit MTPnotifyPeer(mtpTypeId type);
	explicit MTPnotifyPeer(MTPDnotifyPeer *_data);

	friend MTPnotifyPeer MTP_notifyPeer(const MTPPeer &_peer);
	friend MTPnotifyPeer MTP_notifyUsers();
	friend MTPnotifyPeer MTP_notifyChats();
	friend MTPnotifyPeer MTP_notifyAll();

	mtpTypeId _type;
};
typedef MTPBoxed<MTPnotifyPeer> MTPNotifyPeer;

// Type constructors with data

class MTPDresPQ : public mtpDataImpl<MTPDresPQ> {
public:
	MTPDresPQ() {
	}
	MTPDresPQ(const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPstring &_pq, const MTPVector<MTPlong> &_server_public_key_fingerprints) : vnonce(_nonce), vserver_nonce(_server_nonce), vpq(_pq), vserver_public_key_fingerprints(_server_public_key_fingerprints) {
	}

	MTPint128 vnonce;
	MTPint128 vserver_nonce;
	MTPstring vpq;
	MTPVector<MTPlong> vserver_public_key_fingerprints;
};

class MTPDp_q_inner_data : public mtpDataImpl<MTPDp_q_inner_data> {
public:
	MTPDp_q_inner_data() {
	}
	MTPDp_q_inner_data(const MTPstring &_pq, const MTPstring &_p, const MTPstring &_q, const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPint256 &_new_nonce) : vpq(_pq), vp(_p), vq(_q), vnonce(_nonce), vserver_nonce(_server_nonce), vnew_nonce(_new_nonce) {
	}

	MTPstring vpq;
	MTPstring vp;
	MTPstring vq;
	MTPint128 vnonce;
	MTPint128 vserver_nonce;
	MTPint256 vnew_nonce;
};

class MTPDserver_DH_params_fail : public mtpDataImpl<MTPDserver_DH_params_fail> {
public:
	MTPDserver_DH_params_fail() {
	}
	MTPDserver_DH_params_fail(const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPint128 &_new_nonce_hash) : vnonce(_nonce), vserver_nonce(_server_nonce), vnew_nonce_hash(_new_nonce_hash) {
	}

	MTPint128 vnonce;
	MTPint128 vserver_nonce;
	MTPint128 vnew_nonce_hash;
};

class MTPDserver_DH_params_ok : public mtpDataImpl<MTPDserver_DH_params_ok> {
public:
	MTPDserver_DH_params_ok() {
	}
	MTPDserver_DH_params_ok(const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPstring &_encrypted_answer) : vnonce(_nonce), vserver_nonce(_server_nonce), vencrypted_answer(_encrypted_answer) {
	}

	MTPint128 vnonce;
	MTPint128 vserver_nonce;
	MTPstring vencrypted_answer;
};

class MTPDserver_DH_inner_data : public mtpDataImpl<MTPDserver_DH_inner_data> {
public:
	MTPDserver_DH_inner_data() {
	}
	MTPDserver_DH_inner_data(const MTPint128 &_nonce, const MTPint128 &_server_nonce, MTPint _g, const MTPstring &_dh_prime, const MTPstring &_g_a, MTPint _server_time) : vnonce(_nonce), vserver_nonce(_server_nonce), vg(_g), vdh_prime(_dh_prime), vg_a(_g_a), vserver_time(_server_time) {
	}

	MTPint128 vnonce;
	MTPint128 vserver_nonce;
	MTPint vg;
	MTPstring vdh_prime;
	MTPstring vg_a;
	MTPint vserver_time;
};

class MTPDclient_DH_inner_data : public mtpDataImpl<MTPDclient_DH_inner_data> {
public:
	MTPDclient_DH_inner_data() {
	}
	MTPDclient_DH_inner_data(const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPlong &_retry_id, const MTPstring &_g_b) : vnonce(_nonce), vserver_nonce(_server_nonce), vretry_id(_retry_id), vg_b(_g_b) {
	}

	MTPint128 vnonce;
	MTPint128 vserver_nonce;
	MTPlong vretry_id;
	MTPstring vg_b;
};

class MTPDdh_gen_ok : public mtpDataImpl<MTPDdh_gen_ok> {
public:
	MTPDdh_gen_ok() {
	}
	MTPDdh_gen_ok(const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPint128 &_new_nonce_hash1) : vnonce(_nonce), vserver_nonce(_server_nonce), vnew_nonce_hash1(_new_nonce_hash1) {
	}

	MTPint128 vnonce;
	MTPint128 vserver_nonce;
	MTPint128 vnew_nonce_hash1;
};

class MTPDdh_gen_retry : public mtpDataImpl<MTPDdh_gen_retry> {
public:
	MTPDdh_gen_retry() {
	}
	MTPDdh_gen_retry(const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPint128 &_new_nonce_hash2) : vnonce(_nonce), vserver_nonce(_server_nonce), vnew_nonce_hash2(_new_nonce_hash2) {
	}

	MTPint128 vnonce;
	MTPint128 vserver_nonce;
	MTPint128 vnew_nonce_hash2;
};

class MTPDdh_gen_fail : public mtpDataImpl<MTPDdh_gen_fail> {
public:
	MTPDdh_gen_fail() {
	}
	MTPDdh_gen_fail(const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPint128 &_new_nonce_hash3) : vnonce(_nonce), vserver_nonce(_server_nonce), vnew_nonce_hash3(_new_nonce_hash3) {
	}

	MTPint128 vnonce;
	MTPint128 vserver_nonce;
	MTPint128 vnew_nonce_hash3;
};

class MTPDmsgs_ack : public mtpDataImpl<MTPDmsgs_ack> {
public:
	MTPDmsgs_ack() {
	}
	MTPDmsgs_ack(const MTPVector<MTPlong> &_msg_ids) : vmsg_ids(_msg_ids) {
	}

	MTPVector<MTPlong> vmsg_ids;
};

class MTPDbad_msg_notification : public mtpDataImpl<MTPDbad_msg_notification> {
public:
	MTPDbad_msg_notification() {
	}
	MTPDbad_msg_notification(const MTPlong &_bad_msg_id, MTPint _bad_msg_seqno, MTPint _error_code) : vbad_msg_id(_bad_msg_id), vbad_msg_seqno(_bad_msg_seqno), verror_code(_error_code) {
	}

	MTPlong vbad_msg_id;
	MTPint vbad_msg_seqno;
	MTPint verror_code;
};

class MTPDbad_server_salt : public mtpDataImpl<MTPDbad_server_salt> {
public:
	MTPDbad_server_salt() {
	}
	MTPDbad_server_salt(const MTPlong &_bad_msg_id, MTPint _bad_msg_seqno, MTPint _error_code, const MTPlong &_new_server_salt) : vbad_msg_id(_bad_msg_id), vbad_msg_seqno(_bad_msg_seqno), verror_code(_error_code), vnew_server_salt(_new_server_salt) {
	}

	MTPlong vbad_msg_id;
	MTPint vbad_msg_seqno;
	MTPint verror_code;
	MTPlong vnew_server_salt;
};

class MTPDmsgs_state_req : public mtpDataImpl<MTPDmsgs_state_req> {
public:
	MTPDmsgs_state_req() {
	}
	MTPDmsgs_state_req(const MTPVector<MTPlong> &_msg_ids) : vmsg_ids(_msg_ids) {
	}

	MTPVector<MTPlong> vmsg_ids;
};

class MTPDmsgs_state_info : public mtpDataImpl<MTPDmsgs_state_info> {
public:
	MTPDmsgs_state_info() {
	}
	MTPDmsgs_state_info(const MTPlong &_req_msg_id, const MTPstring &_info) : vreq_msg_id(_req_msg_id), vinfo(_info) {
	}

	MTPlong vreq_msg_id;
	MTPstring vinfo;
};

class MTPDmsgs_all_info : public mtpDataImpl<MTPDmsgs_all_info> {
public:
	MTPDmsgs_all_info() {
	}
	MTPDmsgs_all_info(const MTPVector<MTPlong> &_msg_ids, const MTPstring &_info) : vmsg_ids(_msg_ids), vinfo(_info) {
	}

	MTPVector<MTPlong> vmsg_ids;
	MTPstring vinfo;
};

class MTPDmsg_detailed_info : public mtpDataImpl<MTPDmsg_detailed_info> {
public:
	MTPDmsg_detailed_info() {
	}
	MTPDmsg_detailed_info(const MTPlong &_msg_id, const MTPlong &_answer_msg_id, MTPint _bytes, MTPint _status) : vmsg_id(_msg_id), vanswer_msg_id(_answer_msg_id), vbytes(_bytes), vstatus(_status) {
	}

	MTPlong vmsg_id;
	MTPlong vanswer_msg_id;
	MTPint vbytes;
	MTPint vstatus;
};

class MTPDmsg_new_detailed_info : public mtpDataImpl<MTPDmsg_new_detailed_info> {
public:
	MTPDmsg_new_detailed_info() {
	}
	MTPDmsg_new_detailed_info(const MTPlong &_answer_msg_id, MTPint _bytes, MTPint _status) : vanswer_msg_id(_answer_msg_id), vbytes(_bytes), vstatus(_status) {
	}

	MTPlong vanswer_msg_id;
	MTPint vbytes;
	MTPint vstatus;
};

class MTPDmsg_resend_req : public mtpDataImpl<MTPDmsg_resend_req> {
public:
	MTPDmsg_resend_req() {
	}
	MTPDmsg_resend_req(const MTPVector<MTPlong> &_msg_ids) : vmsg_ids(_msg_ids) {
	}

	MTPVector<MTPlong> vmsg_ids;
};

class MTPDrpc_error : public mtpDataImpl<MTPDrpc_error> {
public:
	MTPDrpc_error() {
	}
	MTPDrpc_error(MTPint _error_code, const MTPstring &_error_message) : verror_code(_error_code), verror_message(_error_message) {
	}

	MTPint verror_code;
	MTPstring verror_message;
};

class MTPDrpc_answer_dropped : public mtpDataImpl<MTPDrpc_answer_dropped> {
public:
	MTPDrpc_answer_dropped() {
	}
	MTPDrpc_answer_dropped(const MTPlong &_msg_id, MTPint _seq_no, MTPint _bytes) : vmsg_id(_msg_id), vseq_no(_seq_no), vbytes(_bytes) {
	}

	MTPlong vmsg_id;
	MTPint vseq_no;
	MTPint vbytes;
};

class MTPDfuture_salt : public mtpDataImpl<MTPDfuture_salt> {
public:
	MTPDfuture_salt() {
	}
	MTPDfuture_salt(MTPint _valid_since, MTPint _valid_until, const MTPlong &_salt) : vvalid_since(_valid_since), vvalid_until(_valid_until), vsalt(_salt) {
	}

	MTPint vvalid_since;
	MTPint vvalid_until;
	MTPlong vsalt;
};

class MTPDfuture_salts : public mtpDataImpl<MTPDfuture_salts> {
public:
	MTPDfuture_salts() {
	}
	MTPDfuture_salts(const MTPlong &_req_msg_id, MTPint _now, const MTPvector<MTPfutureSalt> &_salts) : vreq_msg_id(_req_msg_id), vnow(_now), vsalts(_salts) {
	}

	MTPlong vreq_msg_id;
	MTPint vnow;
	MTPvector<MTPfutureSalt> vsalts;
};

class MTPDpong : public mtpDataImpl<MTPDpong> {
public:
	MTPDpong() {
	}
	MTPDpong(const MTPlong &_msg_id, const MTPlong &_ping_id) : vmsg_id(_msg_id), vping_id(_ping_id) {
	}

	MTPlong vmsg_id;
	MTPlong vping_id;
};

class MTPDdestroy_session_ok : public mtpDataImpl<MTPDdestroy_session_ok> {
public:
	MTPDdestroy_session_ok() {
	}
	MTPDdestroy_session_ok(const MTPlong &_session_id) : vsession_id(_session_id) {
	}

	MTPlong vsession_id;
};

class MTPDdestroy_session_none : public mtpDataImpl<MTPDdestroy_session_none> {
public:
	MTPDdestroy_session_none() {
	}
	MTPDdestroy_session_none(const MTPlong &_session_id) : vsession_id(_session_id) {
	}

	MTPlong vsession_id;
};

class MTPDnew_session_created : public mtpDataImpl<MTPDnew_session_created> {
public:
	MTPDnew_session_created() {
	}
	MTPDnew_session_created(const MTPlong &_first_msg_id, const MTPlong &_unique_id, const MTPlong &_server_salt) : vfirst_msg_id(_first_msg_id), vunique_id(_unique_id), vserver_salt(_server_salt) {
	}

	MTPlong vfirst_msg_id;
	MTPlong vunique_id;
	MTPlong vserver_salt;
};

class MTPDhttp_wait : public mtpDataImpl<MTPDhttp_wait> {
public:
	MTPDhttp_wait() {
	}
	MTPDhttp_wait(MTPint _max_delay, MTPint _wait_after, MTPint _max_wait) : vmax_delay(_max_delay), vwait_after(_wait_after), vmax_wait(_max_wait) {
	}

	MTPint vmax_delay;
	MTPint vwait_after;
	MTPint vmax_wait;
};

class MTPDinputPeerContact : public mtpDataImpl<MTPDinputPeerContact> {
public:
	MTPDinputPeerContact() {
	}
	MTPDinputPeerContact(MTPint _user_id) : vuser_id(_user_id) {
	}

	MTPint vuser_id;
};

class MTPDinputPeerForeign : public mtpDataImpl<MTPDinputPeerForeign> {
public:
	MTPDinputPeerForeign() {
	}
	MTPDinputPeerForeign(MTPint _user_id, const MTPlong &_access_hash) : vuser_id(_user_id), vaccess_hash(_access_hash) {
	}

	MTPint vuser_id;
	MTPlong vaccess_hash;
};

class MTPDinputPeerChat : public mtpDataImpl<MTPDinputPeerChat> {
public:
	MTPDinputPeerChat() {
	}
	MTPDinputPeerChat(MTPint _chat_id) : vchat_id(_chat_id) {
	}

	MTPint vchat_id;
};

class MTPDinputUserContact : public mtpDataImpl<MTPDinputUserContact> {
public:
	MTPDinputUserContact() {
	}
	MTPDinputUserContact(MTPint _user_id) : vuser_id(_user_id) {
	}

	MTPint vuser_id;
};

class MTPDinputUserForeign : public mtpDataImpl<MTPDinputUserForeign> {
public:
	MTPDinputUserForeign() {
	}
	MTPDinputUserForeign(MTPint _user_id, const MTPlong &_access_hash) : vuser_id(_user_id), vaccess_hash(_access_hash) {
	}

	MTPint vuser_id;
	MTPlong vaccess_hash;
};

class MTPDinputPhoneContact : public mtpDataImpl<MTPDinputPhoneContact> {
public:
	MTPDinputPhoneContact() {
	}
	MTPDinputPhoneContact(const MTPlong &_client_id, const MTPstring &_phone, const MTPstring &_first_name, const MTPstring &_last_name) : vclient_id(_client_id), vphone(_phone), vfirst_name(_first_name), vlast_name(_last_name) {
	}

	MTPlong vclient_id;
	MTPstring vphone;
	MTPstring vfirst_name;
	MTPstring vlast_name;
};

class MTPDinputFile : public mtpDataImpl<MTPDinputFile> {
public:
	MTPDinputFile() {
	}
	MTPDinputFile(const MTPlong &_id, MTPint _parts, const MTPstring &_name, const MTPstring &_md5_checksum) : vid(_id), vparts(_parts), vname(_name), vmd5_checksum(_md5_checksum) {
	}

	MTPlong vid;
	MTPint vparts;
	MTPstring vname;
	MTPstring vmd5_checksum;
};

class MTPDinputFileBig : public mtpDataImpl<MTPDinputFileBig> {
public:
	MTPDinputFileBig() {
	}
	MTPDinputFileBig(const MTPlong &_id, MTPint _parts, const MTPstring &_name) : vid(_id), vparts(_parts), vname(_name) {
	}

	MTPlong vid;
	MTPint vparts;
	MTPstring vname;
};

class MTPDinputMediaUploadedPhoto : public mtpDataImpl<MTPDinputMediaUploadedPhoto> {
public:
	MTPDinputMediaUploadedPhoto() {
	}
	MTPDinputMediaUploadedPhoto(const MTPInputFile &_file) : vfile(_file) {
	}

	MTPInputFile vfile;
};

class MTPDinputMediaPhoto : public mtpDataImpl<MTPDinputMediaPhoto> {
public:
	MTPDinputMediaPhoto() {
	}
	MTPDinputMediaPhoto(const MTPInputPhoto &_id) : vid(_id) {
	}

	MTPInputPhoto vid;
};

class MTPDinputMediaGeoPoint : public mtpDataImpl<MTPDinputMediaGeoPoint> {
public:
	MTPDinputMediaGeoPoint() {
	}
	MTPDinputMediaGeoPoint(const MTPInputGeoPoint &_geo_point) : vgeo_point(_geo_point) {
	}

	MTPInputGeoPoint vgeo_point;
};

class MTPDinputMediaContact : public mtpDataImpl<MTPDinputMediaContact> {
public:
	MTPDinputMediaContact() {
	}
	MTPDinputMediaContact(const MTPstring &_phone_number, const MTPstring &_first_name, const MTPstring &_last_name) : vphone_number(_phone_number), vfirst_name(_first_name), vlast_name(_last_name) {
	}

	MTPstring vphone_number;
	MTPstring vfirst_name;
	MTPstring vlast_name;
};

class MTPDinputMediaUploadedVideo : public mtpDataImpl<MTPDinputMediaUploadedVideo> {
public:
	MTPDinputMediaUploadedVideo() {
	}
	MTPDinputMediaUploadedVideo(const MTPInputFile &_file, MTPint _duration, MTPint _w, MTPint _h, const MTPstring &_mime_type) : vfile(_file), vduration(_duration), vw(_w), vh(_h), vmime_type(_mime_type) {
	}

	MTPInputFile vfile;
	MTPint vduration;
	MTPint vw;
	MTPint vh;
	MTPstring vmime_type;
};

class MTPDinputMediaUploadedThumbVideo : public mtpDataImpl<MTPDinputMediaUploadedThumbVideo> {
public:
	MTPDinputMediaUploadedThumbVideo() {
	}
	MTPDinputMediaUploadedThumbVideo(const MTPInputFile &_file, const MTPInputFile &_thumb, MTPint _duration, MTPint _w, MTPint _h, const MTPstring &_mime_type) : vfile(_file), vthumb(_thumb), vduration(_duration), vw(_w), vh(_h), vmime_type(_mime_type) {
	}

	MTPInputFile vfile;
	MTPInputFile vthumb;
	MTPint vduration;
	MTPint vw;
	MTPint vh;
	MTPstring vmime_type;
};

class MTPDinputMediaVideo : public mtpDataImpl<MTPDinputMediaVideo> {
public:
	MTPDinputMediaVideo() {
	}
	MTPDinputMediaVideo(const MTPInputVideo &_id) : vid(_id) {
	}

	MTPInputVideo vid;
};

class MTPDinputMediaUploadedAudio : public mtpDataImpl<MTPDinputMediaUploadedAudio> {
public:
	MTPDinputMediaUploadedAudio() {
	}
	MTPDinputMediaUploadedAudio(const MTPInputFile &_file, MTPint _duration, const MTPstring &_mime_type) : vfile(_file), vduration(_duration), vmime_type(_mime_type) {
	}

	MTPInputFile vfile;
	MTPint vduration;
	MTPstring vmime_type;
};

class MTPDinputMediaAudio : public mtpDataImpl<MTPDinputMediaAudio> {
public:
	MTPDinputMediaAudio() {
	}
	MTPDinputMediaAudio(const MTPInputAudio &_id) : vid(_id) {
	}

	MTPInputAudio vid;
};

class MTPDinputMediaUploadedDocument : public mtpDataImpl<MTPDinputMediaUploadedDocument> {
public:
	MTPDinputMediaUploadedDocument() {
	}
	MTPDinputMediaUploadedDocument(const MTPInputFile &_file, const MTPstring &_file_name, const MTPstring &_mime_type) : vfile(_file), vfile_name(_file_name), vmime_type(_mime_type) {
	}

	MTPInputFile vfile;
	MTPstring vfile_name;
	MTPstring vmime_type;
};

class MTPDinputMediaUploadedThumbDocument : public mtpDataImpl<MTPDinputMediaUploadedThumbDocument> {
public:
	MTPDinputMediaUploadedThumbDocument() {
	}
	MTPDinputMediaUploadedThumbDocument(const MTPInputFile &_file, const MTPInputFile &_thumb, const MTPstring &_file_name, const MTPstring &_mime_type) : vfile(_file), vthumb(_thumb), vfile_name(_file_name), vmime_type(_mime_type) {
	}

	MTPInputFile vfile;
	MTPInputFile vthumb;
	MTPstring vfile_name;
	MTPstring vmime_type;
};

class MTPDinputMediaDocument : public mtpDataImpl<MTPDinputMediaDocument> {
public:
	MTPDinputMediaDocument() {
	}
	MTPDinputMediaDocument(const MTPInputDocument &_id) : vid(_id) {
	}

	MTPInputDocument vid;
};

class MTPDinputChatUploadedPhoto : public mtpDataImpl<MTPDinputChatUploadedPhoto> {
public:
	MTPDinputChatUploadedPhoto() {
	}
	MTPDinputChatUploadedPhoto(const MTPInputFile &_file, const MTPInputPhotoCrop &_crop) : vfile(_file), vcrop(_crop) {
	}

	MTPInputFile vfile;
	MTPInputPhotoCrop vcrop;
};

class MTPDinputChatPhoto : public mtpDataImpl<MTPDinputChatPhoto> {
public:
	MTPDinputChatPhoto() {
	}
	MTPDinputChatPhoto(const MTPInputPhoto &_id, const MTPInputPhotoCrop &_crop) : vid(_id), vcrop(_crop) {
	}

	MTPInputPhoto vid;
	MTPInputPhotoCrop vcrop;
};

class MTPDinputGeoPoint : public mtpDataImpl<MTPDinputGeoPoint> {
public:
	MTPDinputGeoPoint() {
	}
	MTPDinputGeoPoint(const MTPdouble &_lat, const MTPdouble &_long) : vlat(_lat), vlong(_long) {
	}

	MTPdouble vlat;
	MTPdouble vlong;
};

class MTPDinputPhoto : public mtpDataImpl<MTPDinputPhoto> {
public:
	MTPDinputPhoto() {
	}
	MTPDinputPhoto(const MTPlong &_id, const MTPlong &_access_hash) : vid(_id), vaccess_hash(_access_hash) {
	}

	MTPlong vid;
	MTPlong vaccess_hash;
};

class MTPDinputVideo : public mtpDataImpl<MTPDinputVideo> {
public:
	MTPDinputVideo() {
	}
	MTPDinputVideo(const MTPlong &_id, const MTPlong &_access_hash) : vid(_id), vaccess_hash(_access_hash) {
	}

	MTPlong vid;
	MTPlong vaccess_hash;
};

class MTPDinputFileLocation : public mtpDataImpl<MTPDinputFileLocation> {
public:
	MTPDinputFileLocation() {
	}
	MTPDinputFileLocation(const MTPlong &_volume_id, MTPint _local_id, const MTPlong &_secret) : vvolume_id(_volume_id), vlocal_id(_local_id), vsecret(_secret) {
	}

	MTPlong vvolume_id;
	MTPint vlocal_id;
	MTPlong vsecret;
};

class MTPDinputVideoFileLocation : public mtpDataImpl<MTPDinputVideoFileLocation> {
public:
	MTPDinputVideoFileLocation() {
	}
	MTPDinputVideoFileLocation(const MTPlong &_id, const MTPlong &_access_hash) : vid(_id), vaccess_hash(_access_hash) {
	}

	MTPlong vid;
	MTPlong vaccess_hash;
};

class MTPDinputEncryptedFileLocation : public mtpDataImpl<MTPDinputEncryptedFileLocation> {
public:
	MTPDinputEncryptedFileLocation() {
	}
	MTPDinputEncryptedFileLocation(const MTPlong &_id, const MTPlong &_access_hash) : vid(_id), vaccess_hash(_access_hash) {
	}

	MTPlong vid;
	MTPlong vaccess_hash;
};

class MTPDinputAudioFileLocation : public mtpDataImpl<MTPDinputAudioFileLocation> {
public:
	MTPDinputAudioFileLocation() {
	}
	MTPDinputAudioFileLocation(const MTPlong &_id, const MTPlong &_access_hash) : vid(_id), vaccess_hash(_access_hash) {
	}

	MTPlong vid;
	MTPlong vaccess_hash;
};

class MTPDinputDocumentFileLocation : public mtpDataImpl<MTPDinputDocumentFileLocation> {
public:
	MTPDinputDocumentFileLocation() {
	}
	MTPDinputDocumentFileLocation(const MTPlong &_id, const MTPlong &_access_hash) : vid(_id), vaccess_hash(_access_hash) {
	}

	MTPlong vid;
	MTPlong vaccess_hash;
};

class MTPDinputPhotoCrop : public mtpDataImpl<MTPDinputPhotoCrop> {
public:
	MTPDinputPhotoCrop() {
	}
	MTPDinputPhotoCrop(const MTPdouble &_crop_left, const MTPdouble &_crop_top, const MTPdouble &_crop_width) : vcrop_left(_crop_left), vcrop_top(_crop_top), vcrop_width(_crop_width) {
	}

	MTPdouble vcrop_left;
	MTPdouble vcrop_top;
	MTPdouble vcrop_width;
};

class MTPDinputAppEvent : public mtpDataImpl<MTPDinputAppEvent> {
public:
	MTPDinputAppEvent() {
	}
	MTPDinputAppEvent(const MTPdouble &_time, const MTPstring &_type, const MTPlong &_peer, const MTPstring &_data) : vtime(_time), vtype(_type), vpeer(_peer), vdata(_data) {
	}

	MTPdouble vtime;
	MTPstring vtype;
	MTPlong vpeer;
	MTPstring vdata;
};

class MTPDpeerUser : public mtpDataImpl<MTPDpeerUser> {
public:
	MTPDpeerUser() {
	}
	MTPDpeerUser(MTPint _user_id) : vuser_id(_user_id) {
	}

	MTPint vuser_id;
};

class MTPDpeerChat : public mtpDataImpl<MTPDpeerChat> {
public:
	MTPDpeerChat() {
	}
	MTPDpeerChat(MTPint _chat_id) : vchat_id(_chat_id) {
	}

	MTPint vchat_id;
};

class MTPDfileLocationUnavailable : public mtpDataImpl<MTPDfileLocationUnavailable> {
public:
	MTPDfileLocationUnavailable() {
	}
	MTPDfileLocationUnavailable(const MTPlong &_volume_id, MTPint _local_id, const MTPlong &_secret) : vvolume_id(_volume_id), vlocal_id(_local_id), vsecret(_secret) {
	}

	MTPlong vvolume_id;
	MTPint vlocal_id;
	MTPlong vsecret;
};

class MTPDfileLocation : public mtpDataImpl<MTPDfileLocation> {
public:
	MTPDfileLocation() {
	}
	MTPDfileLocation(MTPint _dc_id, const MTPlong &_volume_id, MTPint _local_id, const MTPlong &_secret) : vdc_id(_dc_id), vvolume_id(_volume_id), vlocal_id(_local_id), vsecret(_secret) {
	}

	MTPint vdc_id;
	MTPlong vvolume_id;
	MTPint vlocal_id;
	MTPlong vsecret;
};

class MTPDuserEmpty : public mtpDataImpl<MTPDuserEmpty> {
public:
	MTPDuserEmpty() {
	}
	MTPDuserEmpty(MTPint _id) : vid(_id) {
	}

	MTPint vid;
};

class MTPDuserSelf : public mtpDataImpl<MTPDuserSelf> {
public:
	MTPDuserSelf() {
	}
	MTPDuserSelf(MTPint _id, const MTPstring &_first_name, const MTPstring &_last_name, const MTPstring &_phone, const MTPUserProfilePhoto &_photo, const MTPUserStatus &_status, MTPBool _inactive) : vid(_id), vfirst_name(_first_name), vlast_name(_last_name), vphone(_phone), vphoto(_photo), vstatus(_status), vinactive(_inactive) {
	}

	MTPint vid;
	MTPstring vfirst_name;
	MTPstring vlast_name;
	MTPstring vphone;
	MTPUserProfilePhoto vphoto;
	MTPUserStatus vstatus;
	MTPBool vinactive;
};

class MTPDuserContact : public mtpDataImpl<MTPDuserContact> {
public:
	MTPDuserContact() {
	}
	MTPDuserContact(MTPint _id, const MTPstring &_first_name, const MTPstring &_last_name, const MTPlong &_access_hash, const MTPstring &_phone, const MTPUserProfilePhoto &_photo, const MTPUserStatus &_status) : vid(_id), vfirst_name(_first_name), vlast_name(_last_name), vaccess_hash(_access_hash), vphone(_phone), vphoto(_photo), vstatus(_status) {
	}

	MTPint vid;
	MTPstring vfirst_name;
	MTPstring vlast_name;
	MTPlong vaccess_hash;
	MTPstring vphone;
	MTPUserProfilePhoto vphoto;
	MTPUserStatus vstatus;
};

class MTPDuserRequest : public mtpDataImpl<MTPDuserRequest> {
public:
	MTPDuserRequest() {
	}
	MTPDuserRequest(MTPint _id, const MTPstring &_first_name, const MTPstring &_last_name, const MTPlong &_access_hash, const MTPstring &_phone, const MTPUserProfilePhoto &_photo, const MTPUserStatus &_status) : vid(_id), vfirst_name(_first_name), vlast_name(_last_name), vaccess_hash(_access_hash), vphone(_phone), vphoto(_photo), vstatus(_status) {
	}

	MTPint vid;
	MTPstring vfirst_name;
	MTPstring vlast_name;
	MTPlong vaccess_hash;
	MTPstring vphone;
	MTPUserProfilePhoto vphoto;
	MTPUserStatus vstatus;
};

class MTPDuserForeign : public mtpDataImpl<MTPDuserForeign> {
public:
	MTPDuserForeign() {
	}
	MTPDuserForeign(MTPint _id, const MTPstring &_first_name, const MTPstring &_last_name, const MTPlong &_access_hash, const MTPUserProfilePhoto &_photo, const MTPUserStatus &_status) : vid(_id), vfirst_name(_first_name), vlast_name(_last_name), vaccess_hash(_access_hash), vphoto(_photo), vstatus(_status) {
	}

	MTPint vid;
	MTPstring vfirst_name;
	MTPstring vlast_name;
	MTPlong vaccess_hash;
	MTPUserProfilePhoto vphoto;
	MTPUserStatus vstatus;
};

class MTPDuserDeleted : public mtpDataImpl<MTPDuserDeleted> {
public:
	MTPDuserDeleted() {
	}
	MTPDuserDeleted(MTPint _id, const MTPstring &_first_name, const MTPstring &_last_name) : vid(_id), vfirst_name(_first_name), vlast_name(_last_name) {
	}

	MTPint vid;
	MTPstring vfirst_name;
	MTPstring vlast_name;
};

class MTPDuserProfilePhoto : public mtpDataImpl<MTPDuserProfilePhoto> {
public:
	MTPDuserProfilePhoto() {
	}
	MTPDuserProfilePhoto(const MTPlong &_photo_id, const MTPFileLocation &_photo_small, const MTPFileLocation &_photo_big) : vphoto_id(_photo_id), vphoto_small(_photo_small), vphoto_big(_photo_big) {
	}

	MTPlong vphoto_id;
	MTPFileLocation vphoto_small;
	MTPFileLocation vphoto_big;
};

class MTPDuserStatusOnline : public mtpDataImpl<MTPDuserStatusOnline> {
public:
	MTPDuserStatusOnline() {
	}
	MTPDuserStatusOnline(MTPint _expires) : vexpires(_expires) {
	}

	MTPint vexpires;
};

class MTPDuserStatusOffline : public mtpDataImpl<MTPDuserStatusOffline> {
public:
	MTPDuserStatusOffline() {
	}
	MTPDuserStatusOffline(MTPint _was_online) : vwas_online(_was_online) {
	}

	MTPint vwas_online;
};

class MTPDchatEmpty : public mtpDataImpl<MTPDchatEmpty> {
public:
	MTPDchatEmpty() {
	}
	MTPDchatEmpty(MTPint _id) : vid(_id) {
	}

	MTPint vid;
};

class MTPDchat : public mtpDataImpl<MTPDchat> {
public:
	MTPDchat() {
	}
	MTPDchat(MTPint _id, const MTPstring &_title, const MTPChatPhoto &_photo, MTPint _participants_count, MTPint _date, MTPBool _left, MTPint _version) : vid(_id), vtitle(_title), vphoto(_photo), vparticipants_count(_participants_count), vdate(_date), vleft(_left), vversion(_version) {
	}

	MTPint vid;
	MTPstring vtitle;
	MTPChatPhoto vphoto;
	MTPint vparticipants_count;
	MTPint vdate;
	MTPBool vleft;
	MTPint vversion;
};

class MTPDchatForbidden : public mtpDataImpl<MTPDchatForbidden> {
public:
	MTPDchatForbidden() {
	}
	MTPDchatForbidden(MTPint _id, const MTPstring &_title, MTPint _date) : vid(_id), vtitle(_title), vdate(_date) {
	}

	MTPint vid;
	MTPstring vtitle;
	MTPint vdate;
};

class MTPDgeoChat : public mtpDataImpl<MTPDgeoChat> {
public:
	MTPDgeoChat() {
	}
	MTPDgeoChat(MTPint _id, const MTPlong &_access_hash, const MTPstring &_title, const MTPstring &_address, const MTPstring &_venue, const MTPGeoPoint &_geo, const MTPChatPhoto &_photo, MTPint _participants_count, MTPint _date, MTPBool _checked_in, MTPint _version) : vid(_id), vaccess_hash(_access_hash), vtitle(_title), vaddress(_address), vvenue(_venue), vgeo(_geo), vphoto(_photo), vparticipants_count(_participants_count), vdate(_date), vchecked_in(_checked_in), vversion(_version) {
	}

	MTPint vid;
	MTPlong vaccess_hash;
	MTPstring vtitle;
	MTPstring vaddress;
	MTPstring vvenue;
	MTPGeoPoint vgeo;
	MTPChatPhoto vphoto;
	MTPint vparticipants_count;
	MTPint vdate;
	MTPBool vchecked_in;
	MTPint vversion;
};

class MTPDchatFull : public mtpDataImpl<MTPDchatFull> {
public:
	MTPDchatFull() {
	}
	MTPDchatFull(MTPint _id, const MTPChatParticipants &_participants, const MTPPhoto &_chat_photo, const MTPPeerNotifySettings &_notify_settings) : vid(_id), vparticipants(_participants), vchat_photo(_chat_photo), vnotify_settings(_notify_settings) {
	}

	MTPint vid;
	MTPChatParticipants vparticipants;
	MTPPhoto vchat_photo;
	MTPPeerNotifySettings vnotify_settings;
};

class MTPDchatParticipant : public mtpDataImpl<MTPDchatParticipant> {
public:
	MTPDchatParticipant() {
	}
	MTPDchatParticipant(MTPint _user_id, MTPint _inviter_id, MTPint _date) : vuser_id(_user_id), vinviter_id(_inviter_id), vdate(_date) {
	}

	MTPint vuser_id;
	MTPint vinviter_id;
	MTPint vdate;
};

class MTPDchatParticipantsForbidden : public mtpDataImpl<MTPDchatParticipantsForbidden> {
public:
	MTPDchatParticipantsForbidden() {
	}
	MTPDchatParticipantsForbidden(MTPint _chat_id) : vchat_id(_chat_id) {
	}

	MTPint vchat_id;
};

class MTPDchatParticipants : public mtpDataImpl<MTPDchatParticipants> {
public:
	MTPDchatParticipants() {
	}
	MTPDchatParticipants(MTPint _chat_id, MTPint _admin_id, const MTPVector<MTPChatParticipant> &_participants, MTPint _version) : vchat_id(_chat_id), vadmin_id(_admin_id), vparticipants(_participants), vversion(_version) {
	}

	MTPint vchat_id;
	MTPint vadmin_id;
	MTPVector<MTPChatParticipant> vparticipants;
	MTPint vversion;
};

class MTPDchatPhoto : public mtpDataImpl<MTPDchatPhoto> {
public:
	MTPDchatPhoto() {
	}
	MTPDchatPhoto(const MTPFileLocation &_photo_small, const MTPFileLocation &_photo_big) : vphoto_small(_photo_small), vphoto_big(_photo_big) {
	}

	MTPFileLocation vphoto_small;
	MTPFileLocation vphoto_big;
};

class MTPDmessageEmpty : public mtpDataImpl<MTPDmessageEmpty> {
public:
	MTPDmessageEmpty() {
	}
	MTPDmessageEmpty(MTPint _id) : vid(_id) {
	}

	MTPint vid;
};

class MTPDmessage : public mtpDataImpl<MTPDmessage> {
public:
	MTPDmessage() {
	}
	MTPDmessage(MTPint _id, MTPint _from_id, const MTPPeer &_to_id, MTPBool _out, MTPBool _unread, MTPint _date, const MTPstring &_message, const MTPMessageMedia &_media) : vid(_id), vfrom_id(_from_id), vto_id(_to_id), vout(_out), vunread(_unread), vdate(_date), vmessage(_message), vmedia(_media) {
	}

	MTPint vid;
	MTPint vfrom_id;
	MTPPeer vto_id;
	MTPBool vout;
	MTPBool vunread;
	MTPint vdate;
	MTPstring vmessage;
	MTPMessageMedia vmedia;
};

class MTPDmessageForwarded : public mtpDataImpl<MTPDmessageForwarded> {
public:
	MTPDmessageForwarded() {
	}
	MTPDmessageForwarded(MTPint _id, MTPint _fwd_from_id, MTPint _fwd_date, MTPint _from_id, const MTPPeer &_to_id, MTPBool _out, MTPBool _unread, MTPint _date, const MTPstring &_message, const MTPMessageMedia &_media) : vid(_id), vfwd_from_id(_fwd_from_id), vfwd_date(_fwd_date), vfrom_id(_from_id), vto_id(_to_id), vout(_out), vunread(_unread), vdate(_date), vmessage(_message), vmedia(_media) {
	}

	MTPint vid;
	MTPint vfwd_from_id;
	MTPint vfwd_date;
	MTPint vfrom_id;
	MTPPeer vto_id;
	MTPBool vout;
	MTPBool vunread;
	MTPint vdate;
	MTPstring vmessage;
	MTPMessageMedia vmedia;
};

class MTPDmessageService : public mtpDataImpl<MTPDmessageService> {
public:
	MTPDmessageService() {
	}
	MTPDmessageService(MTPint _id, MTPint _from_id, const MTPPeer &_to_id, MTPBool _out, MTPBool _unread, MTPint _date, const MTPMessageAction &_action) : vid(_id), vfrom_id(_from_id), vto_id(_to_id), vout(_out), vunread(_unread), vdate(_date), vaction(_action) {
	}

	MTPint vid;
	MTPint vfrom_id;
	MTPPeer vto_id;
	MTPBool vout;
	MTPBool vunread;
	MTPint vdate;
	MTPMessageAction vaction;
};

class MTPDmessageMediaPhoto : public mtpDataImpl<MTPDmessageMediaPhoto> {
public:
	MTPDmessageMediaPhoto() {
	}
	MTPDmessageMediaPhoto(const MTPPhoto &_photo) : vphoto(_photo) {
	}

	MTPPhoto vphoto;
};

class MTPDmessageMediaVideo : public mtpDataImpl<MTPDmessageMediaVideo> {
public:
	MTPDmessageMediaVideo() {
	}
	MTPDmessageMediaVideo(const MTPVideo &_video) : vvideo(_video) {
	}

	MTPVideo vvideo;
};

class MTPDmessageMediaGeo : public mtpDataImpl<MTPDmessageMediaGeo> {
public:
	MTPDmessageMediaGeo() {
	}
	MTPDmessageMediaGeo(const MTPGeoPoint &_geo) : vgeo(_geo) {
	}

	MTPGeoPoint vgeo;
};

class MTPDmessageMediaContact : public mtpDataImpl<MTPDmessageMediaContact> {
public:
	MTPDmessageMediaContact() {
	}
	MTPDmessageMediaContact(const MTPstring &_phone_number, const MTPstring &_first_name, const MTPstring &_last_name, MTPint _user_id) : vphone_number(_phone_number), vfirst_name(_first_name), vlast_name(_last_name), vuser_id(_user_id) {
	}

	MTPstring vphone_number;
	MTPstring vfirst_name;
	MTPstring vlast_name;
	MTPint vuser_id;
};

class MTPDmessageMediaUnsupported : public mtpDataImpl<MTPDmessageMediaUnsupported> {
public:
	MTPDmessageMediaUnsupported() {
	}
	MTPDmessageMediaUnsupported(const MTPbytes &_bytes) : vbytes(_bytes) {
	}

	MTPbytes vbytes;
};

class MTPDmessageMediaDocument : public mtpDataImpl<MTPDmessageMediaDocument> {
public:
	MTPDmessageMediaDocument() {
	}
	MTPDmessageMediaDocument(const MTPDocument &_document) : vdocument(_document) {
	}

	MTPDocument vdocument;
};

class MTPDmessageMediaAudio : public mtpDataImpl<MTPDmessageMediaAudio> {
public:
	MTPDmessageMediaAudio() {
	}
	MTPDmessageMediaAudio(const MTPAudio &_audio) : vaudio(_audio) {
	}

	MTPAudio vaudio;
};

class MTPDmessageActionChatCreate : public mtpDataImpl<MTPDmessageActionChatCreate> {
public:
	MTPDmessageActionChatCreate() {
	}
	MTPDmessageActionChatCreate(const MTPstring &_title, const MTPVector<MTPint> &_users) : vtitle(_title), vusers(_users) {
	}

	MTPstring vtitle;
	MTPVector<MTPint> vusers;
};

class MTPDmessageActionChatEditTitle : public mtpDataImpl<MTPDmessageActionChatEditTitle> {
public:
	MTPDmessageActionChatEditTitle() {
	}
	MTPDmessageActionChatEditTitle(const MTPstring &_title) : vtitle(_title) {
	}

	MTPstring vtitle;
};

class MTPDmessageActionChatEditPhoto : public mtpDataImpl<MTPDmessageActionChatEditPhoto> {
public:
	MTPDmessageActionChatEditPhoto() {
	}
	MTPDmessageActionChatEditPhoto(const MTPPhoto &_photo) : vphoto(_photo) {
	}

	MTPPhoto vphoto;
};

class MTPDmessageActionChatAddUser : public mtpDataImpl<MTPDmessageActionChatAddUser> {
public:
	MTPDmessageActionChatAddUser() {
	}
	MTPDmessageActionChatAddUser(MTPint _user_id) : vuser_id(_user_id) {
	}

	MTPint vuser_id;
};

class MTPDmessageActionChatDeleteUser : public mtpDataImpl<MTPDmessageActionChatDeleteUser> {
public:
	MTPDmessageActionChatDeleteUser() {
	}
	MTPDmessageActionChatDeleteUser(MTPint _user_id) : vuser_id(_user_id) {
	}

	MTPint vuser_id;
};

class MTPDmessageActionGeoChatCreate : public mtpDataImpl<MTPDmessageActionGeoChatCreate> {
public:
	MTPDmessageActionGeoChatCreate() {
	}
	MTPDmessageActionGeoChatCreate(const MTPstring &_title, const MTPstring &_address) : vtitle(_title), vaddress(_address) {
	}

	MTPstring vtitle;
	MTPstring vaddress;
};

class MTPDdialog : public mtpDataImpl<MTPDdialog> {
public:
	MTPDdialog() {
	}
	MTPDdialog(const MTPPeer &_peer, MTPint _top_message, MTPint _unread_count, const MTPPeerNotifySettings &_notify_settings) : vpeer(_peer), vtop_message(_top_message), vunread_count(_unread_count), vnotify_settings(_notify_settings) {
	}

	MTPPeer vpeer;
	MTPint vtop_message;
	MTPint vunread_count;
	MTPPeerNotifySettings vnotify_settings;
};

class MTPDphotoEmpty : public mtpDataImpl<MTPDphotoEmpty> {
public:
	MTPDphotoEmpty() {
	}
	MTPDphotoEmpty(const MTPlong &_id) : vid(_id) {
	}

	MTPlong vid;
};

class MTPDphoto : public mtpDataImpl<MTPDphoto> {
public:
	MTPDphoto() {
	}
	MTPDphoto(const MTPlong &_id, const MTPlong &_access_hash, MTPint _user_id, MTPint _date, const MTPstring &_caption, const MTPGeoPoint &_geo, const MTPVector<MTPPhotoSize> &_sizes) : vid(_id), vaccess_hash(_access_hash), vuser_id(_user_id), vdate(_date), vcaption(_caption), vgeo(_geo), vsizes(_sizes) {
	}

	MTPlong vid;
	MTPlong vaccess_hash;
	MTPint vuser_id;
	MTPint vdate;
	MTPstring vcaption;
	MTPGeoPoint vgeo;
	MTPVector<MTPPhotoSize> vsizes;
};

class MTPDphotoSizeEmpty : public mtpDataImpl<MTPDphotoSizeEmpty> {
public:
	MTPDphotoSizeEmpty() {
	}
	MTPDphotoSizeEmpty(const MTPstring &_type) : vtype(_type) {
	}

	MTPstring vtype;
};

class MTPDphotoSize : public mtpDataImpl<MTPDphotoSize> {
public:
	MTPDphotoSize() {
	}
	MTPDphotoSize(const MTPstring &_type, const MTPFileLocation &_location, MTPint _w, MTPint _h, MTPint _size) : vtype(_type), vlocation(_location), vw(_w), vh(_h), vsize(_size) {
	}

	MTPstring vtype;
	MTPFileLocation vlocation;
	MTPint vw;
	MTPint vh;
	MTPint vsize;
};

class MTPDphotoCachedSize : public mtpDataImpl<MTPDphotoCachedSize> {
public:
	MTPDphotoCachedSize() {
	}
	MTPDphotoCachedSize(const MTPstring &_type, const MTPFileLocation &_location, MTPint _w, MTPint _h, const MTPbytes &_bytes) : vtype(_type), vlocation(_location), vw(_w), vh(_h), vbytes(_bytes) {
	}

	MTPstring vtype;
	MTPFileLocation vlocation;
	MTPint vw;
	MTPint vh;
	MTPbytes vbytes;
};

class MTPDvideoEmpty : public mtpDataImpl<MTPDvideoEmpty> {
public:
	MTPDvideoEmpty() {
	}
	MTPDvideoEmpty(const MTPlong &_id) : vid(_id) {
	}

	MTPlong vid;
};

class MTPDvideo : public mtpDataImpl<MTPDvideo> {
public:
	MTPDvideo() {
	}
	MTPDvideo(const MTPlong &_id, const MTPlong &_access_hash, MTPint _user_id, MTPint _date, const MTPstring &_caption, MTPint _duration, const MTPstring &_mime_type, MTPint _size, const MTPPhotoSize &_thumb, MTPint _dc_id, MTPint _w, MTPint _h) : vid(_id), vaccess_hash(_access_hash), vuser_id(_user_id), vdate(_date), vcaption(_caption), vduration(_duration), vmime_type(_mime_type), vsize(_size), vthumb(_thumb), vdc_id(_dc_id), vw(_w), vh(_h) {
	}

	MTPlong vid;
	MTPlong vaccess_hash;
	MTPint vuser_id;
	MTPint vdate;
	MTPstring vcaption;
	MTPint vduration;
	MTPstring vmime_type;
	MTPint vsize;
	MTPPhotoSize vthumb;
	MTPint vdc_id;
	MTPint vw;
	MTPint vh;
};

class MTPDgeoPoint : public mtpDataImpl<MTPDgeoPoint> {
public:
	MTPDgeoPoint() {
	}
	MTPDgeoPoint(const MTPdouble &_long, const MTPdouble &_lat) : vlong(_long), vlat(_lat) {
	}

	MTPdouble vlong;
	MTPdouble vlat;
};

class MTPDauth_checkedPhone : public mtpDataImpl<MTPDauth_checkedPhone> {
public:
	MTPDauth_checkedPhone() {
	}
	MTPDauth_checkedPhone(MTPBool _phone_registered, MTPBool _phone_invited) : vphone_registered(_phone_registered), vphone_invited(_phone_invited) {
	}

	MTPBool vphone_registered;
	MTPBool vphone_invited;
};

class MTPDauth_sentCode : public mtpDataImpl<MTPDauth_sentCode> {
public:
	MTPDauth_sentCode() {
	}
	MTPDauth_sentCode(MTPBool _phone_registered, const MTPstring &_phone_code_hash, MTPint _send_call_timeout, MTPBool _is_password) : vphone_registered(_phone_registered), vphone_code_hash(_phone_code_hash), vsend_call_timeout(_send_call_timeout), vis_password(_is_password) {
	}

	MTPBool vphone_registered;
	MTPstring vphone_code_hash;
	MTPint vsend_call_timeout;
	MTPBool vis_password;
};

class MTPDauth_authorization : public mtpDataImpl<MTPDauth_authorization> {
public:
	MTPDauth_authorization() {
	}
	MTPDauth_authorization(MTPint _expires, const MTPUser &_user) : vexpires(_expires), vuser(_user) {
	}

	MTPint vexpires;
	MTPUser vuser;
};

class MTPDauth_exportedAuthorization : public mtpDataImpl<MTPDauth_exportedAuthorization> {
public:
	MTPDauth_exportedAuthorization() {
	}
	MTPDauth_exportedAuthorization(MTPint _id, const MTPbytes &_bytes) : vid(_id), vbytes(_bytes) {
	}

	MTPint vid;
	MTPbytes vbytes;
};

class MTPDinputNotifyPeer : public mtpDataImpl<MTPDinputNotifyPeer> {
public:
	MTPDinputNotifyPeer() {
	}
	MTPDinputNotifyPeer(const MTPInputPeer &_peer) : vpeer(_peer) {
	}

	MTPInputPeer vpeer;
};

class MTPDinputNotifyGeoChatPeer : public mtpDataImpl<MTPDinputNotifyGeoChatPeer> {
public:
	MTPDinputNotifyGeoChatPeer() {
	}
	MTPDinputNotifyGeoChatPeer(const MTPInputGeoChat &_peer) : vpeer(_peer) {
	}

	MTPInputGeoChat vpeer;
};

class MTPDinputPeerNotifySettings : public mtpDataImpl<MTPDinputPeerNotifySettings> {
public:
	MTPDinputPeerNotifySettings() {
	}
	MTPDinputPeerNotifySettings(MTPint _mute_until, const MTPstring &_sound, MTPBool _show_previews, MTPint _events_mask) : vmute_until(_mute_until), vsound(_sound), vshow_previews(_show_previews), vevents_mask(_events_mask) {
	}

	MTPint vmute_until;
	MTPstring vsound;
	MTPBool vshow_previews;
	MTPint vevents_mask;
};

class MTPDpeerNotifySettings : public mtpDataImpl<MTPDpeerNotifySettings> {
public:
	MTPDpeerNotifySettings() {
	}
	MTPDpeerNotifySettings(MTPint _mute_until, const MTPstring &_sound, MTPBool _show_previews, MTPint _events_mask) : vmute_until(_mute_until), vsound(_sound), vshow_previews(_show_previews), vevents_mask(_events_mask) {
	}

	MTPint vmute_until;
	MTPstring vsound;
	MTPBool vshow_previews;
	MTPint vevents_mask;
};

class MTPDwallPaper : public mtpDataImpl<MTPDwallPaper> {
public:
	MTPDwallPaper() {
	}
	MTPDwallPaper(MTPint _id, const MTPstring &_title, const MTPVector<MTPPhotoSize> &_sizes, MTPint _color) : vid(_id), vtitle(_title), vsizes(_sizes), vcolor(_color) {
	}

	MTPint vid;
	MTPstring vtitle;
	MTPVector<MTPPhotoSize> vsizes;
	MTPint vcolor;
};

class MTPDwallPaperSolid : public mtpDataImpl<MTPDwallPaperSolid> {
public:
	MTPDwallPaperSolid() {
	}
	MTPDwallPaperSolid(MTPint _id, const MTPstring &_title, MTPint _bg_color, MTPint _color) : vid(_id), vtitle(_title), vbg_color(_bg_color), vcolor(_color) {
	}

	MTPint vid;
	MTPstring vtitle;
	MTPint vbg_color;
	MTPint vcolor;
};

class MTPDuserFull : public mtpDataImpl<MTPDuserFull> {
public:
	MTPDuserFull() {
	}
	MTPDuserFull(const MTPUser &_user, const MTPcontacts_Link &_link, const MTPPhoto &_profile_photo, const MTPPeerNotifySettings &_notify_settings, MTPBool _blocked, const MTPstring &_real_first_name, const MTPstring &_real_last_name) : vuser(_user), vlink(_link), vprofile_photo(_profile_photo), vnotify_settings(_notify_settings), vblocked(_blocked), vreal_first_name(_real_first_name), vreal_last_name(_real_last_name) {
	}

	MTPUser vuser;
	MTPcontacts_Link vlink;
	MTPPhoto vprofile_photo;
	MTPPeerNotifySettings vnotify_settings;
	MTPBool vblocked;
	MTPstring vreal_first_name;
	MTPstring vreal_last_name;
};

class MTPDcontact : public mtpDataImpl<MTPDcontact> {
public:
	MTPDcontact() {
	}
	MTPDcontact(MTPint _user_id, MTPBool _mutual) : vuser_id(_user_id), vmutual(_mutual) {
	}

	MTPint vuser_id;
	MTPBool vmutual;
};

class MTPDimportedContact : public mtpDataImpl<MTPDimportedContact> {
public:
	MTPDimportedContact() {
	}
	MTPDimportedContact(MTPint _user_id, const MTPlong &_client_id) : vuser_id(_user_id), vclient_id(_client_id) {
	}

	MTPint vuser_id;
	MTPlong vclient_id;
};

class MTPDcontactBlocked : public mtpDataImpl<MTPDcontactBlocked> {
public:
	MTPDcontactBlocked() {
	}
	MTPDcontactBlocked(MTPint _user_id, MTPint _date) : vuser_id(_user_id), vdate(_date) {
	}

	MTPint vuser_id;
	MTPint vdate;
};

class MTPDcontactFound : public mtpDataImpl<MTPDcontactFound> {
public:
	MTPDcontactFound() {
	}
	MTPDcontactFound(MTPint _user_id) : vuser_id(_user_id) {
	}

	MTPint vuser_id;
};

class MTPDcontactSuggested : public mtpDataImpl<MTPDcontactSuggested> {
public:
	MTPDcontactSuggested() {
	}
	MTPDcontactSuggested(MTPint _user_id, MTPint _mutual_contacts) : vuser_id(_user_id), vmutual_contacts(_mutual_contacts) {
	}

	MTPint vuser_id;
	MTPint vmutual_contacts;
};

class MTPDcontactStatus : public mtpDataImpl<MTPDcontactStatus> {
public:
	MTPDcontactStatus() {
	}
	MTPDcontactStatus(MTPint _user_id, MTPint _expires) : vuser_id(_user_id), vexpires(_expires) {
	}

	MTPint vuser_id;
	MTPint vexpires;
};

class MTPDchatLocated : public mtpDataImpl<MTPDchatLocated> {
public:
	MTPDchatLocated() {
	}
	MTPDchatLocated(MTPint _chat_id, MTPint _distance) : vchat_id(_chat_id), vdistance(_distance) {
	}

	MTPint vchat_id;
	MTPint vdistance;
};

class MTPDcontacts_foreignLinkRequested : public mtpDataImpl<MTPDcontacts_foreignLinkRequested> {
public:
	MTPDcontacts_foreignLinkRequested() {
	}
	MTPDcontacts_foreignLinkRequested(MTPBool _has_phone) : vhas_phone(_has_phone) {
	}

	MTPBool vhas_phone;
};

class MTPDcontacts_myLinkRequested : public mtpDataImpl<MTPDcontacts_myLinkRequested> {
public:
	MTPDcontacts_myLinkRequested() {
	}
	MTPDcontacts_myLinkRequested(MTPBool _contact) : vcontact(_contact) {
	}

	MTPBool vcontact;
};

class MTPDcontacts_link : public mtpDataImpl<MTPDcontacts_link> {
public:
	MTPDcontacts_link() {
	}
	MTPDcontacts_link(const MTPcontacts_MyLink &_my_link, const MTPcontacts_ForeignLink &_foreign_link, const MTPUser &_user) : vmy_link(_my_link), vforeign_link(_foreign_link), vuser(_user) {
	}

	MTPcontacts_MyLink vmy_link;
	MTPcontacts_ForeignLink vforeign_link;
	MTPUser vuser;
};

class MTPDcontacts_contacts : public mtpDataImpl<MTPDcontacts_contacts> {
public:
	MTPDcontacts_contacts() {
	}
	MTPDcontacts_contacts(const MTPVector<MTPContact> &_contacts, const MTPVector<MTPUser> &_users) : vcontacts(_contacts), vusers(_users) {
	}

	MTPVector<MTPContact> vcontacts;
	MTPVector<MTPUser> vusers;
};

class MTPDcontacts_importedContacts : public mtpDataImpl<MTPDcontacts_importedContacts> {
public:
	MTPDcontacts_importedContacts() {
	}
	MTPDcontacts_importedContacts(const MTPVector<MTPImportedContact> &_imported, const MTPVector<MTPlong> &_retry_contacts, const MTPVector<MTPUser> &_users) : vimported(_imported), vretry_contacts(_retry_contacts), vusers(_users) {
	}

	MTPVector<MTPImportedContact> vimported;
	MTPVector<MTPlong> vretry_contacts;
	MTPVector<MTPUser> vusers;
};

class MTPDcontacts_blocked : public mtpDataImpl<MTPDcontacts_blocked> {
public:
	MTPDcontacts_blocked() {
	}
	MTPDcontacts_blocked(const MTPVector<MTPContactBlocked> &_blocked, const MTPVector<MTPUser> &_users) : vblocked(_blocked), vusers(_users) {
	}

	MTPVector<MTPContactBlocked> vblocked;
	MTPVector<MTPUser> vusers;
};

class MTPDcontacts_blockedSlice : public mtpDataImpl<MTPDcontacts_blockedSlice> {
public:
	MTPDcontacts_blockedSlice() {
	}
	MTPDcontacts_blockedSlice(MTPint _count, const MTPVector<MTPContactBlocked> &_blocked, const MTPVector<MTPUser> &_users) : vcount(_count), vblocked(_blocked), vusers(_users) {
	}

	MTPint vcount;
	MTPVector<MTPContactBlocked> vblocked;
	MTPVector<MTPUser> vusers;
};

class MTPDcontacts_found : public mtpDataImpl<MTPDcontacts_found> {
public:
	MTPDcontacts_found() {
	}
	MTPDcontacts_found(const MTPVector<MTPContactFound> &_results, const MTPVector<MTPUser> &_users) : vresults(_results), vusers(_users) {
	}

	MTPVector<MTPContactFound> vresults;
	MTPVector<MTPUser> vusers;
};

class MTPDcontacts_suggested : public mtpDataImpl<MTPDcontacts_suggested> {
public:
	MTPDcontacts_suggested() {
	}
	MTPDcontacts_suggested(const MTPVector<MTPContactSuggested> &_results, const MTPVector<MTPUser> &_users) : vresults(_results), vusers(_users) {
	}

	MTPVector<MTPContactSuggested> vresults;
	MTPVector<MTPUser> vusers;
};

class MTPDmessages_dialogs : public mtpDataImpl<MTPDmessages_dialogs> {
public:
	MTPDmessages_dialogs() {
	}
	MTPDmessages_dialogs(const MTPVector<MTPDialog> &_dialogs, const MTPVector<MTPMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users) : vdialogs(_dialogs), vmessages(_messages), vchats(_chats), vusers(_users) {
	}

	MTPVector<MTPDialog> vdialogs;
	MTPVector<MTPMessage> vmessages;
	MTPVector<MTPChat> vchats;
	MTPVector<MTPUser> vusers;
};

class MTPDmessages_dialogsSlice : public mtpDataImpl<MTPDmessages_dialogsSlice> {
public:
	MTPDmessages_dialogsSlice() {
	}
	MTPDmessages_dialogsSlice(MTPint _count, const MTPVector<MTPDialog> &_dialogs, const MTPVector<MTPMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users) : vcount(_count), vdialogs(_dialogs), vmessages(_messages), vchats(_chats), vusers(_users) {
	}

	MTPint vcount;
	MTPVector<MTPDialog> vdialogs;
	MTPVector<MTPMessage> vmessages;
	MTPVector<MTPChat> vchats;
	MTPVector<MTPUser> vusers;
};

class MTPDmessages_messages : public mtpDataImpl<MTPDmessages_messages> {
public:
	MTPDmessages_messages() {
	}
	MTPDmessages_messages(const MTPVector<MTPMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users) : vmessages(_messages), vchats(_chats), vusers(_users) {
	}

	MTPVector<MTPMessage> vmessages;
	MTPVector<MTPChat> vchats;
	MTPVector<MTPUser> vusers;
};

class MTPDmessages_messagesSlice : public mtpDataImpl<MTPDmessages_messagesSlice> {
public:
	MTPDmessages_messagesSlice() {
	}
	MTPDmessages_messagesSlice(MTPint _count, const MTPVector<MTPMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users) : vcount(_count), vmessages(_messages), vchats(_chats), vusers(_users) {
	}

	MTPint vcount;
	MTPVector<MTPMessage> vmessages;
	MTPVector<MTPChat> vchats;
	MTPVector<MTPUser> vusers;
};

class MTPDmessages_message : public mtpDataImpl<MTPDmessages_message> {
public:
	MTPDmessages_message() {
	}
	MTPDmessages_message(const MTPMessage &_message, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users) : vmessage(_message), vchats(_chats), vusers(_users) {
	}

	MTPMessage vmessage;
	MTPVector<MTPChat> vchats;
	MTPVector<MTPUser> vusers;
};

class MTPDmessages_statedMessages : public mtpDataImpl<MTPDmessages_statedMessages> {
public:
	MTPDmessages_statedMessages() {
	}
	MTPDmessages_statedMessages(const MTPVector<MTPMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users, MTPint _pts, MTPint _seq) : vmessages(_messages), vchats(_chats), vusers(_users), vpts(_pts), vseq(_seq) {
	}

	MTPVector<MTPMessage> vmessages;
	MTPVector<MTPChat> vchats;
	MTPVector<MTPUser> vusers;
	MTPint vpts;
	MTPint vseq;
};

class MTPDmessages_statedMessagesLinks : public mtpDataImpl<MTPDmessages_statedMessagesLinks> {
public:
	MTPDmessages_statedMessagesLinks() {
	}
	MTPDmessages_statedMessagesLinks(const MTPVector<MTPMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users, const MTPVector<MTPcontacts_Link> &_links, MTPint _pts, MTPint _seq) : vmessages(_messages), vchats(_chats), vusers(_users), vlinks(_links), vpts(_pts), vseq(_seq) {
	}

	MTPVector<MTPMessage> vmessages;
	MTPVector<MTPChat> vchats;
	MTPVector<MTPUser> vusers;
	MTPVector<MTPcontacts_Link> vlinks;
	MTPint vpts;
	MTPint vseq;
};

class MTPDmessages_statedMessage : public mtpDataImpl<MTPDmessages_statedMessage> {
public:
	MTPDmessages_statedMessage() {
	}
	MTPDmessages_statedMessage(const MTPMessage &_message, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users, MTPint _pts, MTPint _seq) : vmessage(_message), vchats(_chats), vusers(_users), vpts(_pts), vseq(_seq) {
	}

	MTPMessage vmessage;
	MTPVector<MTPChat> vchats;
	MTPVector<MTPUser> vusers;
	MTPint vpts;
	MTPint vseq;
};

class MTPDmessages_statedMessageLink : public mtpDataImpl<MTPDmessages_statedMessageLink> {
public:
	MTPDmessages_statedMessageLink() {
	}
	MTPDmessages_statedMessageLink(const MTPMessage &_message, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users, const MTPVector<MTPcontacts_Link> &_links, MTPint _pts, MTPint _seq) : vmessage(_message), vchats(_chats), vusers(_users), vlinks(_links), vpts(_pts), vseq(_seq) {
	}

	MTPMessage vmessage;
	MTPVector<MTPChat> vchats;
	MTPVector<MTPUser> vusers;
	MTPVector<MTPcontacts_Link> vlinks;
	MTPint vpts;
	MTPint vseq;
};

class MTPDmessages_sentMessage : public mtpDataImpl<MTPDmessages_sentMessage> {
public:
	MTPDmessages_sentMessage() {
	}
	MTPDmessages_sentMessage(MTPint _id, MTPint _date, MTPint _pts, MTPint _seq) : vid(_id), vdate(_date), vpts(_pts), vseq(_seq) {
	}

	MTPint vid;
	MTPint vdate;
	MTPint vpts;
	MTPint vseq;
};

class MTPDmessages_sentMessageLink : public mtpDataImpl<MTPDmessages_sentMessageLink> {
public:
	MTPDmessages_sentMessageLink() {
	}
	MTPDmessages_sentMessageLink(MTPint _id, MTPint _date, MTPint _pts, MTPint _seq, const MTPVector<MTPcontacts_Link> &_links) : vid(_id), vdate(_date), vpts(_pts), vseq(_seq), vlinks(_links) {
	}

	MTPint vid;
	MTPint vdate;
	MTPint vpts;
	MTPint vseq;
	MTPVector<MTPcontacts_Link> vlinks;
};

class MTPDmessages_chat : public mtpDataImpl<MTPDmessages_chat> {
public:
	MTPDmessages_chat() {
	}
	MTPDmessages_chat(const MTPChat &_chat, const MTPVector<MTPUser> &_users) : vchat(_chat), vusers(_users) {
	}

	MTPChat vchat;
	MTPVector<MTPUser> vusers;
};

class MTPDmessages_chats : public mtpDataImpl<MTPDmessages_chats> {
public:
	MTPDmessages_chats() {
	}
	MTPDmessages_chats(const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users) : vchats(_chats), vusers(_users) {
	}

	MTPVector<MTPChat> vchats;
	MTPVector<MTPUser> vusers;
};

class MTPDmessages_chatFull : public mtpDataImpl<MTPDmessages_chatFull> {
public:
	MTPDmessages_chatFull() {
	}
	MTPDmessages_chatFull(const MTPChatFull &_full_chat, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users) : vfull_chat(_full_chat), vchats(_chats), vusers(_users) {
	}

	MTPChatFull vfull_chat;
	MTPVector<MTPChat> vchats;
	MTPVector<MTPUser> vusers;
};

class MTPDmessages_affectedHistory : public mtpDataImpl<MTPDmessages_affectedHistory> {
public:
	MTPDmessages_affectedHistory() {
	}
	MTPDmessages_affectedHistory(MTPint _pts, MTPint _seq, MTPint _offset) : vpts(_pts), vseq(_seq), voffset(_offset) {
	}

	MTPint vpts;
	MTPint vseq;
	MTPint voffset;
};

class MTPDupdateNewMessage : public mtpDataImpl<MTPDupdateNewMessage> {
public:
	MTPDupdateNewMessage() {
	}
	MTPDupdateNewMessage(const MTPMessage &_message, MTPint _pts) : vmessage(_message), vpts(_pts) {
	}

	MTPMessage vmessage;
	MTPint vpts;
};

class MTPDupdateMessageID : public mtpDataImpl<MTPDupdateMessageID> {
public:
	MTPDupdateMessageID() {
	}
	MTPDupdateMessageID(MTPint _id, const MTPlong &_random_id) : vid(_id), vrandom_id(_random_id) {
	}

	MTPint vid;
	MTPlong vrandom_id;
};

class MTPDupdateReadMessages : public mtpDataImpl<MTPDupdateReadMessages> {
public:
	MTPDupdateReadMessages() {
	}
	MTPDupdateReadMessages(const MTPVector<MTPint> &_messages, MTPint _pts) : vmessages(_messages), vpts(_pts) {
	}

	MTPVector<MTPint> vmessages;
	MTPint vpts;
};

class MTPDupdateDeleteMessages : public mtpDataImpl<MTPDupdateDeleteMessages> {
public:
	MTPDupdateDeleteMessages() {
	}
	MTPDupdateDeleteMessages(const MTPVector<MTPint> &_messages, MTPint _pts) : vmessages(_messages), vpts(_pts) {
	}

	MTPVector<MTPint> vmessages;
	MTPint vpts;
};

class MTPDupdateRestoreMessages : public mtpDataImpl<MTPDupdateRestoreMessages> {
public:
	MTPDupdateRestoreMessages() {
	}
	MTPDupdateRestoreMessages(const MTPVector<MTPint> &_messages, MTPint _pts) : vmessages(_messages), vpts(_pts) {
	}

	MTPVector<MTPint> vmessages;
	MTPint vpts;
};

class MTPDupdateUserTyping : public mtpDataImpl<MTPDupdateUserTyping> {
public:
	MTPDupdateUserTyping() {
	}
	MTPDupdateUserTyping(MTPint _user_id) : vuser_id(_user_id) {
	}

	MTPint vuser_id;
};

class MTPDupdateChatUserTyping : public mtpDataImpl<MTPDupdateChatUserTyping> {
public:
	MTPDupdateChatUserTyping() {
	}
	MTPDupdateChatUserTyping(MTPint _chat_id, MTPint _user_id) : vchat_id(_chat_id), vuser_id(_user_id) {
	}

	MTPint vchat_id;
	MTPint vuser_id;
};

class MTPDupdateChatParticipants : public mtpDataImpl<MTPDupdateChatParticipants> {
public:
	MTPDupdateChatParticipants() {
	}
	MTPDupdateChatParticipants(const MTPChatParticipants &_participants) : vparticipants(_participants) {
	}

	MTPChatParticipants vparticipants;
};

class MTPDupdateUserStatus : public mtpDataImpl<MTPDupdateUserStatus> {
public:
	MTPDupdateUserStatus() {
	}
	MTPDupdateUserStatus(MTPint _user_id, const MTPUserStatus &_status) : vuser_id(_user_id), vstatus(_status) {
	}

	MTPint vuser_id;
	MTPUserStatus vstatus;
};

class MTPDupdateUserName : public mtpDataImpl<MTPDupdateUserName> {
public:
	MTPDupdateUserName() {
	}
	MTPDupdateUserName(MTPint _user_id, const MTPstring &_first_name, const MTPstring &_last_name) : vuser_id(_user_id), vfirst_name(_first_name), vlast_name(_last_name) {
	}

	MTPint vuser_id;
	MTPstring vfirst_name;
	MTPstring vlast_name;
};

class MTPDupdateUserPhoto : public mtpDataImpl<MTPDupdateUserPhoto> {
public:
	MTPDupdateUserPhoto() {
	}
	MTPDupdateUserPhoto(MTPint _user_id, MTPint _date, const MTPUserProfilePhoto &_photo, MTPBool _previous) : vuser_id(_user_id), vdate(_date), vphoto(_photo), vprevious(_previous) {
	}

	MTPint vuser_id;
	MTPint vdate;
	MTPUserProfilePhoto vphoto;
	MTPBool vprevious;
};

class MTPDupdateContactRegistered : public mtpDataImpl<MTPDupdateContactRegistered> {
public:
	MTPDupdateContactRegistered() {
	}
	MTPDupdateContactRegistered(MTPint _user_id, MTPint _date) : vuser_id(_user_id), vdate(_date) {
	}

	MTPint vuser_id;
	MTPint vdate;
};

class MTPDupdateContactLink : public mtpDataImpl<MTPDupdateContactLink> {
public:
	MTPDupdateContactLink() {
	}
	MTPDupdateContactLink(MTPint _user_id, const MTPcontacts_MyLink &_my_link, const MTPcontacts_ForeignLink &_foreign_link) : vuser_id(_user_id), vmy_link(_my_link), vforeign_link(_foreign_link) {
	}

	MTPint vuser_id;
	MTPcontacts_MyLink vmy_link;
	MTPcontacts_ForeignLink vforeign_link;
};

class MTPDupdateActivation : public mtpDataImpl<MTPDupdateActivation> {
public:
	MTPDupdateActivation() {
	}
	MTPDupdateActivation(MTPint _user_id) : vuser_id(_user_id) {
	}

	MTPint vuser_id;
};

class MTPDupdateNewAuthorization : public mtpDataImpl<MTPDupdateNewAuthorization> {
public:
	MTPDupdateNewAuthorization() {
	}
	MTPDupdateNewAuthorization(const MTPlong &_auth_key_id, MTPint _date, const MTPstring &_device, const MTPstring &_location) : vauth_key_id(_auth_key_id), vdate(_date), vdevice(_device), vlocation(_location) {
	}

	MTPlong vauth_key_id;
	MTPint vdate;
	MTPstring vdevice;
	MTPstring vlocation;
};

class MTPDupdateNewGeoChatMessage : public mtpDataImpl<MTPDupdateNewGeoChatMessage> {
public:
	MTPDupdateNewGeoChatMessage() {
	}
	MTPDupdateNewGeoChatMessage(const MTPGeoChatMessage &_message) : vmessage(_message) {
	}

	MTPGeoChatMessage vmessage;
};

class MTPDupdateNewEncryptedMessage : public mtpDataImpl<MTPDupdateNewEncryptedMessage> {
public:
	MTPDupdateNewEncryptedMessage() {
	}
	MTPDupdateNewEncryptedMessage(const MTPEncryptedMessage &_message, MTPint _qts) : vmessage(_message), vqts(_qts) {
	}

	MTPEncryptedMessage vmessage;
	MTPint vqts;
};

class MTPDupdateEncryptedChatTyping : public mtpDataImpl<MTPDupdateEncryptedChatTyping> {
public:
	MTPDupdateEncryptedChatTyping() {
	}
	MTPDupdateEncryptedChatTyping(MTPint _chat_id) : vchat_id(_chat_id) {
	}

	MTPint vchat_id;
};

class MTPDupdateEncryption : public mtpDataImpl<MTPDupdateEncryption> {
public:
	MTPDupdateEncryption() {
	}
	MTPDupdateEncryption(const MTPEncryptedChat &_chat, MTPint _date) : vchat(_chat), vdate(_date) {
	}

	MTPEncryptedChat vchat;
	MTPint vdate;
};

class MTPDupdateEncryptedMessagesRead : public mtpDataImpl<MTPDupdateEncryptedMessagesRead> {
public:
	MTPDupdateEncryptedMessagesRead() {
	}
	MTPDupdateEncryptedMessagesRead(MTPint _chat_id, MTPint _max_date, MTPint _date) : vchat_id(_chat_id), vmax_date(_max_date), vdate(_date) {
	}

	MTPint vchat_id;
	MTPint vmax_date;
	MTPint vdate;
};

class MTPDupdateChatParticipantAdd : public mtpDataImpl<MTPDupdateChatParticipantAdd> {
public:
	MTPDupdateChatParticipantAdd() {
	}
	MTPDupdateChatParticipantAdd(MTPint _chat_id, MTPint _user_id, MTPint _inviter_id, MTPint _version) : vchat_id(_chat_id), vuser_id(_user_id), vinviter_id(_inviter_id), vversion(_version) {
	}

	MTPint vchat_id;
	MTPint vuser_id;
	MTPint vinviter_id;
	MTPint vversion;
};

class MTPDupdateChatParticipantDelete : public mtpDataImpl<MTPDupdateChatParticipantDelete> {
public:
	MTPDupdateChatParticipantDelete() {
	}
	MTPDupdateChatParticipantDelete(MTPint _chat_id, MTPint _user_id, MTPint _version) : vchat_id(_chat_id), vuser_id(_user_id), vversion(_version) {
	}

	MTPint vchat_id;
	MTPint vuser_id;
	MTPint vversion;
};

class MTPDupdateDcOptions : public mtpDataImpl<MTPDupdateDcOptions> {
public:
	MTPDupdateDcOptions() {
	}
	MTPDupdateDcOptions(const MTPVector<MTPDcOption> &_dc_options) : vdc_options(_dc_options) {
	}

	MTPVector<MTPDcOption> vdc_options;
};

class MTPDupdateUserBlocked : public mtpDataImpl<MTPDupdateUserBlocked> {
public:
	MTPDupdateUserBlocked() {
	}
	MTPDupdateUserBlocked(MTPint _user_id, MTPBool _blocked) : vuser_id(_user_id), vblocked(_blocked) {
	}

	MTPint vuser_id;
	MTPBool vblocked;
};

class MTPDupdateNotifySettings : public mtpDataImpl<MTPDupdateNotifySettings> {
public:
	MTPDupdateNotifySettings() {
	}
	MTPDupdateNotifySettings(const MTPNotifyPeer &_peer, const MTPPeerNotifySettings &_notify_settings) : vpeer(_peer), vnotify_settings(_notify_settings) {
	}

	MTPNotifyPeer vpeer;
	MTPPeerNotifySettings vnotify_settings;
};

class MTPDupdates_state : public mtpDataImpl<MTPDupdates_state> {
public:
	MTPDupdates_state() {
	}
	MTPDupdates_state(MTPint _pts, MTPint _qts, MTPint _date, MTPint _seq, MTPint _unread_count) : vpts(_pts), vqts(_qts), vdate(_date), vseq(_seq), vunread_count(_unread_count) {
	}

	MTPint vpts;
	MTPint vqts;
	MTPint vdate;
	MTPint vseq;
	MTPint vunread_count;
};

class MTPDupdates_differenceEmpty : public mtpDataImpl<MTPDupdates_differenceEmpty> {
public:
	MTPDupdates_differenceEmpty() {
	}
	MTPDupdates_differenceEmpty(MTPint _date, MTPint _seq) : vdate(_date), vseq(_seq) {
	}

	MTPint vdate;
	MTPint vseq;
};

class MTPDupdates_difference : public mtpDataImpl<MTPDupdates_difference> {
public:
	MTPDupdates_difference() {
	}
	MTPDupdates_difference(const MTPVector<MTPMessage> &_new_messages, const MTPVector<MTPEncryptedMessage> &_new_encrypted_messages, const MTPVector<MTPUpdate> &_other_updates, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users, const MTPupdates_State &_state) : vnew_messages(_new_messages), vnew_encrypted_messages(_new_encrypted_messages), vother_updates(_other_updates), vchats(_chats), vusers(_users), vstate(_state) {
	}

	MTPVector<MTPMessage> vnew_messages;
	MTPVector<MTPEncryptedMessage> vnew_encrypted_messages;
	MTPVector<MTPUpdate> vother_updates;
	MTPVector<MTPChat> vchats;
	MTPVector<MTPUser> vusers;
	MTPupdates_State vstate;
};

class MTPDupdates_differenceSlice : public mtpDataImpl<MTPDupdates_differenceSlice> {
public:
	MTPDupdates_differenceSlice() {
	}
	MTPDupdates_differenceSlice(const MTPVector<MTPMessage> &_new_messages, const MTPVector<MTPEncryptedMessage> &_new_encrypted_messages, const MTPVector<MTPUpdate> &_other_updates, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users, const MTPupdates_State &_intermediate_state) : vnew_messages(_new_messages), vnew_encrypted_messages(_new_encrypted_messages), vother_updates(_other_updates), vchats(_chats), vusers(_users), vintermediate_state(_intermediate_state) {
	}

	MTPVector<MTPMessage> vnew_messages;
	MTPVector<MTPEncryptedMessage> vnew_encrypted_messages;
	MTPVector<MTPUpdate> vother_updates;
	MTPVector<MTPChat> vchats;
	MTPVector<MTPUser> vusers;
	MTPupdates_State vintermediate_state;
};

class MTPDupdateShortMessage : public mtpDataImpl<MTPDupdateShortMessage> {
public:
	MTPDupdateShortMessage() {
	}
	MTPDupdateShortMessage(MTPint _id, MTPint _from_id, const MTPstring &_message, MTPint _pts, MTPint _date, MTPint _seq) : vid(_id), vfrom_id(_from_id), vmessage(_message), vpts(_pts), vdate(_date), vseq(_seq) {
	}

	MTPint vid;
	MTPint vfrom_id;
	MTPstring vmessage;
	MTPint vpts;
	MTPint vdate;
	MTPint vseq;
};

class MTPDupdateShortChatMessage : public mtpDataImpl<MTPDupdateShortChatMessage> {
public:
	MTPDupdateShortChatMessage() {
	}
	MTPDupdateShortChatMessage(MTPint _id, MTPint _from_id, MTPint _chat_id, const MTPstring &_message, MTPint _pts, MTPint _date, MTPint _seq) : vid(_id), vfrom_id(_from_id), vchat_id(_chat_id), vmessage(_message), vpts(_pts), vdate(_date), vseq(_seq) {
	}

	MTPint vid;
	MTPint vfrom_id;
	MTPint vchat_id;
	MTPstring vmessage;
	MTPint vpts;
	MTPint vdate;
	MTPint vseq;
};

class MTPDupdateShort : public mtpDataImpl<MTPDupdateShort> {
public:
	MTPDupdateShort() {
	}
	MTPDupdateShort(const MTPUpdate &_update, MTPint _date) : vupdate(_update), vdate(_date) {
	}

	MTPUpdate vupdate;
	MTPint vdate;
};

class MTPDupdatesCombined : public mtpDataImpl<MTPDupdatesCombined> {
public:
	MTPDupdatesCombined() {
	}
	MTPDupdatesCombined(const MTPVector<MTPUpdate> &_updates, const MTPVector<MTPUser> &_users, const MTPVector<MTPChat> &_chats, MTPint _date, MTPint _seq_start, MTPint _seq) : vupdates(_updates), vusers(_users), vchats(_chats), vdate(_date), vseq_start(_seq_start), vseq(_seq) {
	}

	MTPVector<MTPUpdate> vupdates;
	MTPVector<MTPUser> vusers;
	MTPVector<MTPChat> vchats;
	MTPint vdate;
	MTPint vseq_start;
	MTPint vseq;
};

class MTPDupdates : public mtpDataImpl<MTPDupdates> {
public:
	MTPDupdates() {
	}
	MTPDupdates(const MTPVector<MTPUpdate> &_updates, const MTPVector<MTPUser> &_users, const MTPVector<MTPChat> &_chats, MTPint _date, MTPint _seq) : vupdates(_updates), vusers(_users), vchats(_chats), vdate(_date), vseq(_seq) {
	}

	MTPVector<MTPUpdate> vupdates;
	MTPVector<MTPUser> vusers;
	MTPVector<MTPChat> vchats;
	MTPint vdate;
	MTPint vseq;
};

class MTPDphotos_photos : public mtpDataImpl<MTPDphotos_photos> {
public:
	MTPDphotos_photos() {
	}
	MTPDphotos_photos(const MTPVector<MTPPhoto> &_photos, const MTPVector<MTPUser> &_users) : vphotos(_photos), vusers(_users) {
	}

	MTPVector<MTPPhoto> vphotos;
	MTPVector<MTPUser> vusers;
};

class MTPDphotos_photosSlice : public mtpDataImpl<MTPDphotos_photosSlice> {
public:
	MTPDphotos_photosSlice() {
	}
	MTPDphotos_photosSlice(MTPint _count, const MTPVector<MTPPhoto> &_photos, const MTPVector<MTPUser> &_users) : vcount(_count), vphotos(_photos), vusers(_users) {
	}

	MTPint vcount;
	MTPVector<MTPPhoto> vphotos;
	MTPVector<MTPUser> vusers;
};

class MTPDphotos_photo : public mtpDataImpl<MTPDphotos_photo> {
public:
	MTPDphotos_photo() {
	}
	MTPDphotos_photo(const MTPPhoto &_photo, const MTPVector<MTPUser> &_users) : vphoto(_photo), vusers(_users) {
	}

	MTPPhoto vphoto;
	MTPVector<MTPUser> vusers;
};

class MTPDupload_file : public mtpDataImpl<MTPDupload_file> {
public:
	MTPDupload_file() {
	}
	MTPDupload_file(const MTPstorage_FileType &_type, MTPint _mtime, const MTPbytes &_bytes) : vtype(_type), vmtime(_mtime), vbytes(_bytes) {
	}

	MTPstorage_FileType vtype;
	MTPint vmtime;
	MTPbytes vbytes;
};

class MTPDdcOption : public mtpDataImpl<MTPDdcOption> {
public:
	MTPDdcOption() {
	}
	MTPDdcOption(MTPint _id, const MTPstring &_hostname, const MTPstring &_ip_address, MTPint _port) : vid(_id), vhostname(_hostname), vip_address(_ip_address), vport(_port) {
	}

	MTPint vid;
	MTPstring vhostname;
	MTPstring vip_address;
	MTPint vport;
};

class MTPDconfig : public mtpDataImpl<MTPDconfig> {
public:
	MTPDconfig() {
	}
	MTPDconfig(MTPint _date, MTPBool _test_mode, MTPint _this_dc, const MTPVector<MTPDcOption> &_dc_options, MTPint _chat_size_max, MTPint _broadcast_size_max) : vdate(_date), vtest_mode(_test_mode), vthis_dc(_this_dc), vdc_options(_dc_options), vchat_size_max(_chat_size_max), vbroadcast_size_max(_broadcast_size_max) {
	}

	MTPint vdate;
	MTPBool vtest_mode;
	MTPint vthis_dc;
	MTPVector<MTPDcOption> vdc_options;
	MTPint vchat_size_max;
	MTPint vbroadcast_size_max;
};

class MTPDnearestDc : public mtpDataImpl<MTPDnearestDc> {
public:
	MTPDnearestDc() {
	}
	MTPDnearestDc(const MTPstring &_country, MTPint _this_dc, MTPint _nearest_dc) : vcountry(_country), vthis_dc(_this_dc), vnearest_dc(_nearest_dc) {
	}

	MTPstring vcountry;
	MTPint vthis_dc;
	MTPint vnearest_dc;
};

class MTPDhelp_appUpdate : public mtpDataImpl<MTPDhelp_appUpdate> {
public:
	MTPDhelp_appUpdate() {
	}
	MTPDhelp_appUpdate(MTPint _id, MTPBool _critical, const MTPstring &_url, const MTPstring &_text) : vid(_id), vcritical(_critical), vurl(_url), vtext(_text) {
	}

	MTPint vid;
	MTPBool vcritical;
	MTPstring vurl;
	MTPstring vtext;
};

class MTPDhelp_inviteText : public mtpDataImpl<MTPDhelp_inviteText> {
public:
	MTPDhelp_inviteText() {
	}
	MTPDhelp_inviteText(const MTPstring &_message) : vmessage(_message) {
	}

	MTPstring vmessage;
};

class MTPDinputGeoChat : public mtpDataImpl<MTPDinputGeoChat> {
public:
	MTPDinputGeoChat() {
	}
	MTPDinputGeoChat(MTPint _chat_id, const MTPlong &_access_hash) : vchat_id(_chat_id), vaccess_hash(_access_hash) {
	}

	MTPint vchat_id;
	MTPlong vaccess_hash;
};

class MTPDgeoChatMessageEmpty : public mtpDataImpl<MTPDgeoChatMessageEmpty> {
public:
	MTPDgeoChatMessageEmpty() {
	}
	MTPDgeoChatMessageEmpty(MTPint _chat_id, MTPint _id) : vchat_id(_chat_id), vid(_id) {
	}

	MTPint vchat_id;
	MTPint vid;
};

class MTPDgeoChatMessage : public mtpDataImpl<MTPDgeoChatMessage> {
public:
	MTPDgeoChatMessage() {
	}
	MTPDgeoChatMessage(MTPint _chat_id, MTPint _id, MTPint _from_id, MTPint _date, const MTPstring &_message, const MTPMessageMedia &_media) : vchat_id(_chat_id), vid(_id), vfrom_id(_from_id), vdate(_date), vmessage(_message), vmedia(_media) {
	}

	MTPint vchat_id;
	MTPint vid;
	MTPint vfrom_id;
	MTPint vdate;
	MTPstring vmessage;
	MTPMessageMedia vmedia;
};

class MTPDgeoChatMessageService : public mtpDataImpl<MTPDgeoChatMessageService> {
public:
	MTPDgeoChatMessageService() {
	}
	MTPDgeoChatMessageService(MTPint _chat_id, MTPint _id, MTPint _from_id, MTPint _date, const MTPMessageAction &_action) : vchat_id(_chat_id), vid(_id), vfrom_id(_from_id), vdate(_date), vaction(_action) {
	}

	MTPint vchat_id;
	MTPint vid;
	MTPint vfrom_id;
	MTPint vdate;
	MTPMessageAction vaction;
};

class MTPDgeochats_statedMessage : public mtpDataImpl<MTPDgeochats_statedMessage> {
public:
	MTPDgeochats_statedMessage() {
	}
	MTPDgeochats_statedMessage(const MTPGeoChatMessage &_message, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users, MTPint _seq) : vmessage(_message), vchats(_chats), vusers(_users), vseq(_seq) {
	}

	MTPGeoChatMessage vmessage;
	MTPVector<MTPChat> vchats;
	MTPVector<MTPUser> vusers;
	MTPint vseq;
};

class MTPDgeochats_located : public mtpDataImpl<MTPDgeochats_located> {
public:
	MTPDgeochats_located() {
	}
	MTPDgeochats_located(const MTPVector<MTPChatLocated> &_results, const MTPVector<MTPGeoChatMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users) : vresults(_results), vmessages(_messages), vchats(_chats), vusers(_users) {
	}

	MTPVector<MTPChatLocated> vresults;
	MTPVector<MTPGeoChatMessage> vmessages;
	MTPVector<MTPChat> vchats;
	MTPVector<MTPUser> vusers;
};

class MTPDgeochats_messages : public mtpDataImpl<MTPDgeochats_messages> {
public:
	MTPDgeochats_messages() {
	}
	MTPDgeochats_messages(const MTPVector<MTPGeoChatMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users) : vmessages(_messages), vchats(_chats), vusers(_users) {
	}

	MTPVector<MTPGeoChatMessage> vmessages;
	MTPVector<MTPChat> vchats;
	MTPVector<MTPUser> vusers;
};

class MTPDgeochats_messagesSlice : public mtpDataImpl<MTPDgeochats_messagesSlice> {
public:
	MTPDgeochats_messagesSlice() {
	}
	MTPDgeochats_messagesSlice(MTPint _count, const MTPVector<MTPGeoChatMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users) : vcount(_count), vmessages(_messages), vchats(_chats), vusers(_users) {
	}

	MTPint vcount;
	MTPVector<MTPGeoChatMessage> vmessages;
	MTPVector<MTPChat> vchats;
	MTPVector<MTPUser> vusers;
};

class MTPDencryptedChatEmpty : public mtpDataImpl<MTPDencryptedChatEmpty> {
public:
	MTPDencryptedChatEmpty() {
	}
	MTPDencryptedChatEmpty(MTPint _id) : vid(_id) {
	}

	MTPint vid;
};

class MTPDencryptedChatWaiting : public mtpDataImpl<MTPDencryptedChatWaiting> {
public:
	MTPDencryptedChatWaiting() {
	}
	MTPDencryptedChatWaiting(MTPint _id, const MTPlong &_access_hash, MTPint _date, MTPint _admin_id, MTPint _participant_id) : vid(_id), vaccess_hash(_access_hash), vdate(_date), vadmin_id(_admin_id), vparticipant_id(_participant_id) {
	}

	MTPint vid;
	MTPlong vaccess_hash;
	MTPint vdate;
	MTPint vadmin_id;
	MTPint vparticipant_id;
};

class MTPDencryptedChatRequested : public mtpDataImpl<MTPDencryptedChatRequested> {
public:
	MTPDencryptedChatRequested() {
	}
	MTPDencryptedChatRequested(MTPint _id, const MTPlong &_access_hash, MTPint _date, MTPint _admin_id, MTPint _participant_id, const MTPbytes &_g_a) : vid(_id), vaccess_hash(_access_hash), vdate(_date), vadmin_id(_admin_id), vparticipant_id(_participant_id), vg_a(_g_a) {
	}

	MTPint vid;
	MTPlong vaccess_hash;
	MTPint vdate;
	MTPint vadmin_id;
	MTPint vparticipant_id;
	MTPbytes vg_a;
};

class MTPDencryptedChat : public mtpDataImpl<MTPDencryptedChat> {
public:
	MTPDencryptedChat() {
	}
	MTPDencryptedChat(MTPint _id, const MTPlong &_access_hash, MTPint _date, MTPint _admin_id, MTPint _participant_id, const MTPbytes &_g_a_or_b, const MTPlong &_key_fingerprint) : vid(_id), vaccess_hash(_access_hash), vdate(_date), vadmin_id(_admin_id), vparticipant_id(_participant_id), vg_a_or_b(_g_a_or_b), vkey_fingerprint(_key_fingerprint) {
	}

	MTPint vid;
	MTPlong vaccess_hash;
	MTPint vdate;
	MTPint vadmin_id;
	MTPint vparticipant_id;
	MTPbytes vg_a_or_b;
	MTPlong vkey_fingerprint;
};

class MTPDencryptedChatDiscarded : public mtpDataImpl<MTPDencryptedChatDiscarded> {
public:
	MTPDencryptedChatDiscarded() {
	}
	MTPDencryptedChatDiscarded(MTPint _id) : vid(_id) {
	}

	MTPint vid;
};

class MTPDinputEncryptedChat : public mtpDataImpl<MTPDinputEncryptedChat> {
public:
	MTPDinputEncryptedChat() {
	}
	MTPDinputEncryptedChat(MTPint _chat_id, const MTPlong &_access_hash) : vchat_id(_chat_id), vaccess_hash(_access_hash) {
	}

	MTPint vchat_id;
	MTPlong vaccess_hash;
};

class MTPDencryptedFile : public mtpDataImpl<MTPDencryptedFile> {
public:
	MTPDencryptedFile() {
	}
	MTPDencryptedFile(const MTPlong &_id, const MTPlong &_access_hash, MTPint _size, MTPint _dc_id, MTPint _key_fingerprint) : vid(_id), vaccess_hash(_access_hash), vsize(_size), vdc_id(_dc_id), vkey_fingerprint(_key_fingerprint) {
	}

	MTPlong vid;
	MTPlong vaccess_hash;
	MTPint vsize;
	MTPint vdc_id;
	MTPint vkey_fingerprint;
};

class MTPDinputEncryptedFileUploaded : public mtpDataImpl<MTPDinputEncryptedFileUploaded> {
public:
	MTPDinputEncryptedFileUploaded() {
	}
	MTPDinputEncryptedFileUploaded(const MTPlong &_id, MTPint _parts, const MTPstring &_md5_checksum, MTPint _key_fingerprint) : vid(_id), vparts(_parts), vmd5_checksum(_md5_checksum), vkey_fingerprint(_key_fingerprint) {
	}

	MTPlong vid;
	MTPint vparts;
	MTPstring vmd5_checksum;
	MTPint vkey_fingerprint;
};

class MTPDinputEncryptedFile : public mtpDataImpl<MTPDinputEncryptedFile> {
public:
	MTPDinputEncryptedFile() {
	}
	MTPDinputEncryptedFile(const MTPlong &_id, const MTPlong &_access_hash) : vid(_id), vaccess_hash(_access_hash) {
	}

	MTPlong vid;
	MTPlong vaccess_hash;
};

class MTPDinputEncryptedFileBigUploaded : public mtpDataImpl<MTPDinputEncryptedFileBigUploaded> {
public:
	MTPDinputEncryptedFileBigUploaded() {
	}
	MTPDinputEncryptedFileBigUploaded(const MTPlong &_id, MTPint _parts, MTPint _key_fingerprint) : vid(_id), vparts(_parts), vkey_fingerprint(_key_fingerprint) {
	}

	MTPlong vid;
	MTPint vparts;
	MTPint vkey_fingerprint;
};

class MTPDencryptedMessage : public mtpDataImpl<MTPDencryptedMessage> {
public:
	MTPDencryptedMessage() {
	}
	MTPDencryptedMessage(const MTPlong &_random_id, MTPint _chat_id, MTPint _date, const MTPbytes &_bytes, const MTPEncryptedFile &_file) : vrandom_id(_random_id), vchat_id(_chat_id), vdate(_date), vbytes(_bytes), vfile(_file) {
	}

	MTPlong vrandom_id;
	MTPint vchat_id;
	MTPint vdate;
	MTPbytes vbytes;
	MTPEncryptedFile vfile;
};

class MTPDencryptedMessageService : public mtpDataImpl<MTPDencryptedMessageService> {
public:
	MTPDencryptedMessageService() {
	}
	MTPDencryptedMessageService(const MTPlong &_random_id, MTPint _chat_id, MTPint _date, const MTPbytes &_bytes) : vrandom_id(_random_id), vchat_id(_chat_id), vdate(_date), vbytes(_bytes) {
	}

	MTPlong vrandom_id;
	MTPint vchat_id;
	MTPint vdate;
	MTPbytes vbytes;
};

class MTPDdecryptedMessageLayer : public mtpDataImpl<MTPDdecryptedMessageLayer> {
public:
	MTPDdecryptedMessageLayer() {
	}
	MTPDdecryptedMessageLayer(MTPint _layer, const MTPDecryptedMessage &_message) : vlayer(_layer), vmessage(_message) {
	}

	MTPint vlayer;
	MTPDecryptedMessage vmessage;
};

class MTPDdecryptedMessage : public mtpDataImpl<MTPDdecryptedMessage> {
public:
	MTPDdecryptedMessage() {
	}
	MTPDdecryptedMessage(const MTPlong &_random_id, const MTPbytes &_random_bytes, const MTPstring &_message, const MTPDecryptedMessageMedia &_media) : vrandom_id(_random_id), vrandom_bytes(_random_bytes), vmessage(_message), vmedia(_media) {
	}

	MTPlong vrandom_id;
	MTPbytes vrandom_bytes;
	MTPstring vmessage;
	MTPDecryptedMessageMedia vmedia;
};

class MTPDdecryptedMessageService : public mtpDataImpl<MTPDdecryptedMessageService> {
public:
	MTPDdecryptedMessageService() {
	}
	MTPDdecryptedMessageService(const MTPlong &_random_id, const MTPbytes &_random_bytes, const MTPDecryptedMessageAction &_action) : vrandom_id(_random_id), vrandom_bytes(_random_bytes), vaction(_action) {
	}

	MTPlong vrandom_id;
	MTPbytes vrandom_bytes;
	MTPDecryptedMessageAction vaction;
};

class MTPDdecryptedMessageMediaPhoto : public mtpDataImpl<MTPDdecryptedMessageMediaPhoto> {
public:
	MTPDdecryptedMessageMediaPhoto() {
	}
	MTPDdecryptedMessageMediaPhoto(const MTPbytes &_thumb, MTPint _thumb_w, MTPint _thumb_h, MTPint _w, MTPint _h, MTPint _size, const MTPbytes &_key, const MTPbytes &_iv) : vthumb(_thumb), vthumb_w(_thumb_w), vthumb_h(_thumb_h), vw(_w), vh(_h), vsize(_size), vkey(_key), viv(_iv) {
	}

	MTPbytes vthumb;
	MTPint vthumb_w;
	MTPint vthumb_h;
	MTPint vw;
	MTPint vh;
	MTPint vsize;
	MTPbytes vkey;
	MTPbytes viv;
};

class MTPDdecryptedMessageMediaVideo : public mtpDataImpl<MTPDdecryptedMessageMediaVideo> {
public:
	MTPDdecryptedMessageMediaVideo() {
	}
	MTPDdecryptedMessageMediaVideo(const MTPbytes &_thumb, MTPint _thumb_w, MTPint _thumb_h, MTPint _duration, const MTPstring &_mime_type, MTPint _w, MTPint _h, MTPint _size, const MTPbytes &_key, const MTPbytes &_iv) : vthumb(_thumb), vthumb_w(_thumb_w), vthumb_h(_thumb_h), vduration(_duration), vmime_type(_mime_type), vw(_w), vh(_h), vsize(_size), vkey(_key), viv(_iv) {
	}

	MTPbytes vthumb;
	MTPint vthumb_w;
	MTPint vthumb_h;
	MTPint vduration;
	MTPstring vmime_type;
	MTPint vw;
	MTPint vh;
	MTPint vsize;
	MTPbytes vkey;
	MTPbytes viv;
};

class MTPDdecryptedMessageMediaGeoPoint : public mtpDataImpl<MTPDdecryptedMessageMediaGeoPoint> {
public:
	MTPDdecryptedMessageMediaGeoPoint() {
	}
	MTPDdecryptedMessageMediaGeoPoint(const MTPdouble &_lat, const MTPdouble &_long) : vlat(_lat), vlong(_long) {
	}

	MTPdouble vlat;
	MTPdouble vlong;
};

class MTPDdecryptedMessageMediaContact : public mtpDataImpl<MTPDdecryptedMessageMediaContact> {
public:
	MTPDdecryptedMessageMediaContact() {
	}
	MTPDdecryptedMessageMediaContact(const MTPstring &_phone_number, const MTPstring &_first_name, const MTPstring &_last_name, MTPint _user_id) : vphone_number(_phone_number), vfirst_name(_first_name), vlast_name(_last_name), vuser_id(_user_id) {
	}

	MTPstring vphone_number;
	MTPstring vfirst_name;
	MTPstring vlast_name;
	MTPint vuser_id;
};

class MTPDdecryptedMessageMediaDocument : public mtpDataImpl<MTPDdecryptedMessageMediaDocument> {
public:
	MTPDdecryptedMessageMediaDocument() {
	}
	MTPDdecryptedMessageMediaDocument(const MTPbytes &_thumb, MTPint _thumb_w, MTPint _thumb_h, const MTPstring &_file_name, const MTPstring &_mime_type, MTPint _size, const MTPbytes &_key, const MTPbytes &_iv) : vthumb(_thumb), vthumb_w(_thumb_w), vthumb_h(_thumb_h), vfile_name(_file_name), vmime_type(_mime_type), vsize(_size), vkey(_key), viv(_iv) {
	}

	MTPbytes vthumb;
	MTPint vthumb_w;
	MTPint vthumb_h;
	MTPstring vfile_name;
	MTPstring vmime_type;
	MTPint vsize;
	MTPbytes vkey;
	MTPbytes viv;
};

class MTPDdecryptedMessageMediaAudio : public mtpDataImpl<MTPDdecryptedMessageMediaAudio> {
public:
	MTPDdecryptedMessageMediaAudio() {
	}
	MTPDdecryptedMessageMediaAudio(MTPint _duration, const MTPstring &_mime_type, MTPint _size, const MTPbytes &_key, const MTPbytes &_iv) : vduration(_duration), vmime_type(_mime_type), vsize(_size), vkey(_key), viv(_iv) {
	}

	MTPint vduration;
	MTPstring vmime_type;
	MTPint vsize;
	MTPbytes vkey;
	MTPbytes viv;
};

class MTPDdecryptedMessageActionSetMessageTTL : public mtpDataImpl<MTPDdecryptedMessageActionSetMessageTTL> {
public:
	MTPDdecryptedMessageActionSetMessageTTL() {
	}
	MTPDdecryptedMessageActionSetMessageTTL(MTPint _ttl_seconds) : vttl_seconds(_ttl_seconds) {
	}

	MTPint vttl_seconds;
};

class MTPDdecryptedMessageActionReadMessages : public mtpDataImpl<MTPDdecryptedMessageActionReadMessages> {
public:
	MTPDdecryptedMessageActionReadMessages() {
	}
	MTPDdecryptedMessageActionReadMessages(const MTPVector<MTPlong> &_random_ids) : vrandom_ids(_random_ids) {
	}

	MTPVector<MTPlong> vrandom_ids;
};

class MTPDdecryptedMessageActionDeleteMessages : public mtpDataImpl<MTPDdecryptedMessageActionDeleteMessages> {
public:
	MTPDdecryptedMessageActionDeleteMessages() {
	}
	MTPDdecryptedMessageActionDeleteMessages(const MTPVector<MTPlong> &_random_ids) : vrandom_ids(_random_ids) {
	}

	MTPVector<MTPlong> vrandom_ids;
};

class MTPDdecryptedMessageActionScreenshotMessages : public mtpDataImpl<MTPDdecryptedMessageActionScreenshotMessages> {
public:
	MTPDdecryptedMessageActionScreenshotMessages() {
	}
	MTPDdecryptedMessageActionScreenshotMessages(const MTPVector<MTPlong> &_random_ids) : vrandom_ids(_random_ids) {
	}

	MTPVector<MTPlong> vrandom_ids;
};

class MTPDdecryptedMessageActionNotifyLayer : public mtpDataImpl<MTPDdecryptedMessageActionNotifyLayer> {
public:
	MTPDdecryptedMessageActionNotifyLayer() {
	}
	MTPDdecryptedMessageActionNotifyLayer(MTPint _layer) : vlayer(_layer) {
	}

	MTPint vlayer;
};

class MTPDmessages_dhConfigNotModified : public mtpDataImpl<MTPDmessages_dhConfigNotModified> {
public:
	MTPDmessages_dhConfigNotModified() {
	}
	MTPDmessages_dhConfigNotModified(const MTPbytes &_random) : vrandom(_random) {
	}

	MTPbytes vrandom;
};

class MTPDmessages_dhConfig : public mtpDataImpl<MTPDmessages_dhConfig> {
public:
	MTPDmessages_dhConfig() {
	}
	MTPDmessages_dhConfig(MTPint _g, const MTPbytes &_p, MTPint _version, const MTPbytes &_random) : vg(_g), vp(_p), vversion(_version), vrandom(_random) {
	}

	MTPint vg;
	MTPbytes vp;
	MTPint vversion;
	MTPbytes vrandom;
};

class MTPDmessages_sentEncryptedMessage : public mtpDataImpl<MTPDmessages_sentEncryptedMessage> {
public:
	MTPDmessages_sentEncryptedMessage() {
	}
	MTPDmessages_sentEncryptedMessage(MTPint _date) : vdate(_date) {
	}

	MTPint vdate;
};

class MTPDmessages_sentEncryptedFile : public mtpDataImpl<MTPDmessages_sentEncryptedFile> {
public:
	MTPDmessages_sentEncryptedFile() {
	}
	MTPDmessages_sentEncryptedFile(MTPint _date, const MTPEncryptedFile &_file) : vdate(_date), vfile(_file) {
	}

	MTPint vdate;
	MTPEncryptedFile vfile;
};

class MTPDinputAudio : public mtpDataImpl<MTPDinputAudio> {
public:
	MTPDinputAudio() {
	}
	MTPDinputAudio(const MTPlong &_id, const MTPlong &_access_hash) : vid(_id), vaccess_hash(_access_hash) {
	}

	MTPlong vid;
	MTPlong vaccess_hash;
};

class MTPDinputDocument : public mtpDataImpl<MTPDinputDocument> {
public:
	MTPDinputDocument() {
	}
	MTPDinputDocument(const MTPlong &_id, const MTPlong &_access_hash) : vid(_id), vaccess_hash(_access_hash) {
	}

	MTPlong vid;
	MTPlong vaccess_hash;
};

class MTPDaudioEmpty : public mtpDataImpl<MTPDaudioEmpty> {
public:
	MTPDaudioEmpty() {
	}
	MTPDaudioEmpty(const MTPlong &_id) : vid(_id) {
	}

	MTPlong vid;
};

class MTPDaudio : public mtpDataImpl<MTPDaudio> {
public:
	MTPDaudio() {
	}
	MTPDaudio(const MTPlong &_id, const MTPlong &_access_hash, MTPint _user_id, MTPint _date, MTPint _duration, const MTPstring &_mime_type, MTPint _size, MTPint _dc_id) : vid(_id), vaccess_hash(_access_hash), vuser_id(_user_id), vdate(_date), vduration(_duration), vmime_type(_mime_type), vsize(_size), vdc_id(_dc_id) {
	}

	MTPlong vid;
	MTPlong vaccess_hash;
	MTPint vuser_id;
	MTPint vdate;
	MTPint vduration;
	MTPstring vmime_type;
	MTPint vsize;
	MTPint vdc_id;
};

class MTPDdocumentEmpty : public mtpDataImpl<MTPDdocumentEmpty> {
public:
	MTPDdocumentEmpty() {
	}
	MTPDdocumentEmpty(const MTPlong &_id) : vid(_id) {
	}

	MTPlong vid;
};

class MTPDdocument : public mtpDataImpl<MTPDdocument> {
public:
	MTPDdocument() {
	}
	MTPDdocument(const MTPlong &_id, const MTPlong &_access_hash, MTPint _user_id, MTPint _date, const MTPstring &_file_name, const MTPstring &_mime_type, MTPint _size, const MTPPhotoSize &_thumb, MTPint _dc_id) : vid(_id), vaccess_hash(_access_hash), vuser_id(_user_id), vdate(_date), vfile_name(_file_name), vmime_type(_mime_type), vsize(_size), vthumb(_thumb), vdc_id(_dc_id) {
	}

	MTPlong vid;
	MTPlong vaccess_hash;
	MTPint vuser_id;
	MTPint vdate;
	MTPstring vfile_name;
	MTPstring vmime_type;
	MTPint vsize;
	MTPPhotoSize vthumb;
	MTPint vdc_id;
};

class MTPDhelp_support : public mtpDataImpl<MTPDhelp_support> {
public:
	MTPDhelp_support() {
	}
	MTPDhelp_support(const MTPstring &_phone_number, const MTPUser &_user) : vphone_number(_phone_number), vuser(_user) {
	}

	MTPstring vphone_number;
	MTPUser vuser;
};

class MTPDnotifyPeer : public mtpDataImpl<MTPDnotifyPeer> {
public:
	MTPDnotifyPeer() {
	}
	MTPDnotifyPeer(const MTPPeer &_peer) : vpeer(_peer) {
	}

	MTPPeer vpeer;
};

// RPC methods

class MTPreq_pq { // RPC method 'req_pq'
public:
	MTPint128 vnonce;

	MTPreq_pq() {
	}
	MTPreq_pq(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_req_pq) {
		read(from, end, cons);
	}
	MTPreq_pq(const MTPint128 &_nonce) : vnonce(_nonce) {
	}

	uint32 size() const {
		return vnonce.size();
	}
	mtpTypeId type() const {
		return mtpc_req_pq;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_req_pq) {
		vnonce.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vnonce.write(to);
	}

	typedef MTPResPQ ResponseType;
};
class MTPReq_pq : public MTPBoxed<MTPreq_pq> {
public:
	MTPReq_pq() {
	}
	MTPReq_pq(const MTPreq_pq &v) : MTPBoxed<MTPreq_pq>(v) {
	}
	MTPReq_pq(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPreq_pq>(from, end, cons) {
	}
	MTPReq_pq(const MTPint128 &_nonce) : MTPBoxed<MTPreq_pq>(MTPreq_pq(_nonce)) {
	}
};

class MTPreq_DH_params { // RPC method 'req_DH_params'
public:
	MTPint128 vnonce;
	MTPint128 vserver_nonce;
	MTPstring vp;
	MTPstring vq;
	MTPlong vpublic_key_fingerprint;
	MTPstring vencrypted_data;

	MTPreq_DH_params() {
	}
	MTPreq_DH_params(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_req_DH_params) {
		read(from, end, cons);
	}
	MTPreq_DH_params(const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPstring &_p, const MTPstring &_q, const MTPlong &_public_key_fingerprint, const MTPstring &_encrypted_data) : vnonce(_nonce), vserver_nonce(_server_nonce), vp(_p), vq(_q), vpublic_key_fingerprint(_public_key_fingerprint), vencrypted_data(_encrypted_data) {
	}

	uint32 size() const {
		return vnonce.size() + vserver_nonce.size() + vp.size() + vq.size() + vpublic_key_fingerprint.size() + vencrypted_data.size();
	}
	mtpTypeId type() const {
		return mtpc_req_DH_params;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_req_DH_params) {
		vnonce.read(from, end);
		vserver_nonce.read(from, end);
		vp.read(from, end);
		vq.read(from, end);
		vpublic_key_fingerprint.read(from, end);
		vencrypted_data.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vnonce.write(to);
		vserver_nonce.write(to);
		vp.write(to);
		vq.write(to);
		vpublic_key_fingerprint.write(to);
		vencrypted_data.write(to);
	}

	typedef MTPServer_DH_Params ResponseType;
};
class MTPReq_DH_params : public MTPBoxed<MTPreq_DH_params> {
public:
	MTPReq_DH_params() {
	}
	MTPReq_DH_params(const MTPreq_DH_params &v) : MTPBoxed<MTPreq_DH_params>(v) {
	}
	MTPReq_DH_params(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPreq_DH_params>(from, end, cons) {
	}
	MTPReq_DH_params(const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPstring &_p, const MTPstring &_q, const MTPlong &_public_key_fingerprint, const MTPstring &_encrypted_data) : MTPBoxed<MTPreq_DH_params>(MTPreq_DH_params(_nonce, _server_nonce, _p, _q, _public_key_fingerprint, _encrypted_data)) {
	}
};

class MTPset_client_DH_params { // RPC method 'set_client_DH_params'
public:
	MTPint128 vnonce;
	MTPint128 vserver_nonce;
	MTPstring vencrypted_data;

	MTPset_client_DH_params() {
	}
	MTPset_client_DH_params(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_set_client_DH_params) {
		read(from, end, cons);
	}
	MTPset_client_DH_params(const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPstring &_encrypted_data) : vnonce(_nonce), vserver_nonce(_server_nonce), vencrypted_data(_encrypted_data) {
	}

	uint32 size() const {
		return vnonce.size() + vserver_nonce.size() + vencrypted_data.size();
	}
	mtpTypeId type() const {
		return mtpc_set_client_DH_params;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_set_client_DH_params) {
		vnonce.read(from, end);
		vserver_nonce.read(from, end);
		vencrypted_data.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vnonce.write(to);
		vserver_nonce.write(to);
		vencrypted_data.write(to);
	}

	typedef MTPSet_client_DH_params_answer ResponseType;
};
class MTPSet_client_DH_params : public MTPBoxed<MTPset_client_DH_params> {
public:
	MTPSet_client_DH_params() {
	}
	MTPSet_client_DH_params(const MTPset_client_DH_params &v) : MTPBoxed<MTPset_client_DH_params>(v) {
	}
	MTPSet_client_DH_params(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPset_client_DH_params>(from, end, cons) {
	}
	MTPSet_client_DH_params(const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPstring &_encrypted_data) : MTPBoxed<MTPset_client_DH_params>(MTPset_client_DH_params(_nonce, _server_nonce, _encrypted_data)) {
	}
};

class MTPrpc_drop_answer { // RPC method 'rpc_drop_answer'
public:
	MTPlong vreq_msg_id;

	MTPrpc_drop_answer() {
	}
	MTPrpc_drop_answer(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_rpc_drop_answer) {
		read(from, end, cons);
	}
	MTPrpc_drop_answer(const MTPlong &_req_msg_id) : vreq_msg_id(_req_msg_id) {
	}

	uint32 size() const {
		return vreq_msg_id.size();
	}
	mtpTypeId type() const {
		return mtpc_rpc_drop_answer;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_rpc_drop_answer) {
		vreq_msg_id.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vreq_msg_id.write(to);
	}

	typedef MTPRpcDropAnswer ResponseType;
};
class MTPRpc_drop_answer : public MTPBoxed<MTPrpc_drop_answer> {
public:
	MTPRpc_drop_answer() {
	}
	MTPRpc_drop_answer(const MTPrpc_drop_answer &v) : MTPBoxed<MTPrpc_drop_answer>(v) {
	}
	MTPRpc_drop_answer(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPrpc_drop_answer>(from, end, cons) {
	}
	MTPRpc_drop_answer(const MTPlong &_req_msg_id) : MTPBoxed<MTPrpc_drop_answer>(MTPrpc_drop_answer(_req_msg_id)) {
	}
};

class MTPget_future_salts { // RPC method 'get_future_salts'
public:
	MTPint vnum;

	MTPget_future_salts() {
	}
	MTPget_future_salts(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_get_future_salts) {
		read(from, end, cons);
	}
	MTPget_future_salts(MTPint _num) : vnum(_num) {
	}

	uint32 size() const {
		return vnum.size();
	}
	mtpTypeId type() const {
		return mtpc_get_future_salts;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_get_future_salts) {
		vnum.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vnum.write(to);
	}

	typedef MTPFutureSalts ResponseType;
};
class MTPGet_future_salts : public MTPBoxed<MTPget_future_salts> {
public:
	MTPGet_future_salts() {
	}
	MTPGet_future_salts(const MTPget_future_salts &v) : MTPBoxed<MTPget_future_salts>(v) {
	}
	MTPGet_future_salts(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPget_future_salts>(from, end, cons) {
	}
	MTPGet_future_salts(MTPint _num) : MTPBoxed<MTPget_future_salts>(MTPget_future_salts(_num)) {
	}
};

class MTPping { // RPC method 'ping'
public:
	MTPlong vping_id;

	MTPping() {
	}
	MTPping(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_ping) {
		read(from, end, cons);
	}
	MTPping(const MTPlong &_ping_id) : vping_id(_ping_id) {
	}

	uint32 size() const {
		return vping_id.size();
	}
	mtpTypeId type() const {
		return mtpc_ping;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_ping) {
		vping_id.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vping_id.write(to);
	}

	typedef MTPPong ResponseType;
};
class MTPPing : public MTPBoxed<MTPping> {
public:
	MTPPing() {
	}
	MTPPing(const MTPping &v) : MTPBoxed<MTPping>(v) {
	}
	MTPPing(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPping>(from, end, cons) {
	}
	MTPPing(const MTPlong &_ping_id) : MTPBoxed<MTPping>(MTPping(_ping_id)) {
	}
};

class MTPping_delay_disconnect { // RPC method 'ping_delay_disconnect'
public:
	MTPlong vping_id;
	MTPint vdisconnect_delay;

	MTPping_delay_disconnect() {
	}
	MTPping_delay_disconnect(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_ping_delay_disconnect) {
		read(from, end, cons);
	}
	MTPping_delay_disconnect(const MTPlong &_ping_id, MTPint _disconnect_delay) : vping_id(_ping_id), vdisconnect_delay(_disconnect_delay) {
	}

	uint32 size() const {
		return vping_id.size() + vdisconnect_delay.size();
	}
	mtpTypeId type() const {
		return mtpc_ping_delay_disconnect;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_ping_delay_disconnect) {
		vping_id.read(from, end);
		vdisconnect_delay.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vping_id.write(to);
		vdisconnect_delay.write(to);
	}

	typedef MTPPong ResponseType;
};
class MTPPing_delay_disconnect : public MTPBoxed<MTPping_delay_disconnect> {
public:
	MTPPing_delay_disconnect() {
	}
	MTPPing_delay_disconnect(const MTPping_delay_disconnect &v) : MTPBoxed<MTPping_delay_disconnect>(v) {
	}
	MTPPing_delay_disconnect(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPping_delay_disconnect>(from, end, cons) {
	}
	MTPPing_delay_disconnect(const MTPlong &_ping_id, MTPint _disconnect_delay) : MTPBoxed<MTPping_delay_disconnect>(MTPping_delay_disconnect(_ping_id, _disconnect_delay)) {
	}
};

class MTPdestroy_session { // RPC method 'destroy_session'
public:
	MTPlong vsession_id;

	MTPdestroy_session() {
	}
	MTPdestroy_session(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_destroy_session) {
		read(from, end, cons);
	}
	MTPdestroy_session(const MTPlong &_session_id) : vsession_id(_session_id) {
	}

	uint32 size() const {
		return vsession_id.size();
	}
	mtpTypeId type() const {
		return mtpc_destroy_session;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_destroy_session) {
		vsession_id.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vsession_id.write(to);
	}

	typedef MTPDestroySessionRes ResponseType;
};
class MTPDestroy_session : public MTPBoxed<MTPdestroy_session> {
public:
	MTPDestroy_session() {
	}
	MTPDestroy_session(const MTPdestroy_session &v) : MTPBoxed<MTPdestroy_session>(v) {
	}
	MTPDestroy_session(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPdestroy_session>(from, end, cons) {
	}
	MTPDestroy_session(const MTPlong &_session_id) : MTPBoxed<MTPdestroy_session>(MTPdestroy_session(_session_id)) {
	}
};

class MTPregister_saveDeveloperInfo { // RPC method 'register.saveDeveloperInfo'
public:
	MTPstring vname;
	MTPstring vemail;
	MTPstring vphone_number;
	MTPint vage;
	MTPstring vcity;

	MTPregister_saveDeveloperInfo() {
	}
	MTPregister_saveDeveloperInfo(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_register_saveDeveloperInfo) {
		read(from, end, cons);
	}
	MTPregister_saveDeveloperInfo(const MTPstring &_name, const MTPstring &_email, const MTPstring &_phone_number, MTPint _age, const MTPstring &_city) : vname(_name), vemail(_email), vphone_number(_phone_number), vage(_age), vcity(_city) {
	}

	uint32 size() const {
		return vname.size() + vemail.size() + vphone_number.size() + vage.size() + vcity.size();
	}
	mtpTypeId type() const {
		return mtpc_register_saveDeveloperInfo;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_register_saveDeveloperInfo) {
		vname.read(from, end);
		vemail.read(from, end);
		vphone_number.read(from, end);
		vage.read(from, end);
		vcity.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vname.write(to);
		vemail.write(to);
		vphone_number.write(to);
		vage.write(to);
		vcity.write(to);
	}

	typedef MTPBool ResponseType;
};
class MTPregister_SaveDeveloperInfo : public MTPBoxed<MTPregister_saveDeveloperInfo> {
public:
	MTPregister_SaveDeveloperInfo() {
	}
	MTPregister_SaveDeveloperInfo(const MTPregister_saveDeveloperInfo &v) : MTPBoxed<MTPregister_saveDeveloperInfo>(v) {
	}
	MTPregister_SaveDeveloperInfo(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPregister_saveDeveloperInfo>(from, end, cons) {
	}
	MTPregister_SaveDeveloperInfo(const MTPstring &_name, const MTPstring &_email, const MTPstring &_phone_number, MTPint _age, const MTPstring &_city) : MTPBoxed<MTPregister_saveDeveloperInfo>(MTPregister_saveDeveloperInfo(_name, _email, _phone_number, _age, _city)) {
	}
};

template <class TQueryType>
class MTPinvokeAfterMsg { // RPC method 'invokeAfterMsg'
public:
	MTPlong vmsg_id;
	TQueryType vquery;

	MTPinvokeAfterMsg() {
	}
	MTPinvokeAfterMsg(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_invokeAfterMsg) {
		read(from, end, cons);
	}
	MTPinvokeAfterMsg(const MTPlong &_msg_id, const TQueryType &_query) : vmsg_id(_msg_id), vquery(_query) {
	}

	uint32 size() const {
		return vmsg_id.size() + vquery.size();
	}
	mtpTypeId type() const {
		return mtpc_invokeAfterMsg;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_invokeAfterMsg) {
		vmsg_id.read(from, end);
		vquery.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vmsg_id.write(to);
		vquery.write(to);
	}

	typedef typename TQueryType::ResponseType ResponseType;
};
template <typename TQueryType>
class MTPInvokeAfterMsg : public MTPBoxed<MTPinvokeAfterMsg<TQueryType> > {
public:
	MTPInvokeAfterMsg() {
	}
	MTPInvokeAfterMsg(const MTPinvokeAfterMsg<TQueryType> &v) : MTPBoxed<MTPinvokeAfterMsg<TQueryType> >(v) {
	}
	MTPInvokeAfterMsg(const MTPlong &_msg_id, const TQueryType &_query) : MTPBoxed<MTPinvokeAfterMsg<TQueryType> >(MTPinvokeAfterMsg<TQueryType>(_msg_id, _query)) {
	}
};

template <class TQueryType>
class MTPinvokeAfterMsgs { // RPC method 'invokeAfterMsgs'
public:
	MTPVector<MTPlong> vmsg_ids;
	TQueryType vquery;

	MTPinvokeAfterMsgs() {
	}
	MTPinvokeAfterMsgs(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_invokeAfterMsgs) {
		read(from, end, cons);
	}
	MTPinvokeAfterMsgs(const MTPVector<MTPlong> &_msg_ids, const TQueryType &_query) : vmsg_ids(_msg_ids), vquery(_query) {
	}

	uint32 size() const {
		return vmsg_ids.size() + vquery.size();
	}
	mtpTypeId type() const {
		return mtpc_invokeAfterMsgs;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_invokeAfterMsgs) {
		vmsg_ids.read(from, end);
		vquery.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vmsg_ids.write(to);
		vquery.write(to);
	}

	typedef typename TQueryType::ResponseType ResponseType;
};
template <typename TQueryType>
class MTPInvokeAfterMsgs : public MTPBoxed<MTPinvokeAfterMsgs<TQueryType> > {
public:
	MTPInvokeAfterMsgs() {
	}
	MTPInvokeAfterMsgs(const MTPinvokeAfterMsgs<TQueryType> &v) : MTPBoxed<MTPinvokeAfterMsgs<TQueryType> >(v) {
	}
	MTPInvokeAfterMsgs(const MTPVector<MTPlong> &_msg_ids, const TQueryType &_query) : MTPBoxed<MTPinvokeAfterMsgs<TQueryType> >(MTPinvokeAfterMsgs<TQueryType>(_msg_ids, _query)) {
	}
};

class MTPauth_checkPhone { // RPC method 'auth.checkPhone'
public:
	MTPstring vphone_number;

	MTPauth_checkPhone() {
	}
	MTPauth_checkPhone(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_auth_checkPhone) {
		read(from, end, cons);
	}
	MTPauth_checkPhone(const MTPstring &_phone_number) : vphone_number(_phone_number) {
	}

	uint32 size() const {
		return vphone_number.size();
	}
	mtpTypeId type() const {
		return mtpc_auth_checkPhone;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_auth_checkPhone) {
		vphone_number.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vphone_number.write(to);
	}

	typedef MTPauth_CheckedPhone ResponseType;
};
class MTPauth_CheckPhone : public MTPBoxed<MTPauth_checkPhone> {
public:
	MTPauth_CheckPhone() {
	}
	MTPauth_CheckPhone(const MTPauth_checkPhone &v) : MTPBoxed<MTPauth_checkPhone>(v) {
	}
	MTPauth_CheckPhone(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPauth_checkPhone>(from, end, cons) {
	}
	MTPauth_CheckPhone(const MTPstring &_phone_number) : MTPBoxed<MTPauth_checkPhone>(MTPauth_checkPhone(_phone_number)) {
	}
};

class MTPauth_sendCode { // RPC method 'auth.sendCode'
public:
	MTPstring vphone_number;
	MTPint vsms_type;
	MTPint vapi_id;
	MTPstring vapi_hash;
	MTPstring vlang_code;

	MTPauth_sendCode() {
	}
	MTPauth_sendCode(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_auth_sendCode) {
		read(from, end, cons);
	}
	MTPauth_sendCode(const MTPstring &_phone_number, MTPint _sms_type, MTPint _api_id, const MTPstring &_api_hash, const MTPstring &_lang_code) : vphone_number(_phone_number), vsms_type(_sms_type), vapi_id(_api_id), vapi_hash(_api_hash), vlang_code(_lang_code) {
	}

	uint32 size() const {
		return vphone_number.size() + vsms_type.size() + vapi_id.size() + vapi_hash.size() + vlang_code.size();
	}
	mtpTypeId type() const {
		return mtpc_auth_sendCode;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_auth_sendCode) {
		vphone_number.read(from, end);
		vsms_type.read(from, end);
		vapi_id.read(from, end);
		vapi_hash.read(from, end);
		vlang_code.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vphone_number.write(to);
		vsms_type.write(to);
		vapi_id.write(to);
		vapi_hash.write(to);
		vlang_code.write(to);
	}

	typedef MTPauth_SentCode ResponseType;
};
class MTPauth_SendCode : public MTPBoxed<MTPauth_sendCode> {
public:
	MTPauth_SendCode() {
	}
	MTPauth_SendCode(const MTPauth_sendCode &v) : MTPBoxed<MTPauth_sendCode>(v) {
	}
	MTPauth_SendCode(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPauth_sendCode>(from, end, cons) {
	}
	MTPauth_SendCode(const MTPstring &_phone_number, MTPint _sms_type, MTPint _api_id, const MTPstring &_api_hash, const MTPstring &_lang_code) : MTPBoxed<MTPauth_sendCode>(MTPauth_sendCode(_phone_number, _sms_type, _api_id, _api_hash, _lang_code)) {
	}
};

class MTPauth_sendCall { // RPC method 'auth.sendCall'
public:
	MTPstring vphone_number;
	MTPstring vphone_code_hash;

	MTPauth_sendCall() {
	}
	MTPauth_sendCall(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_auth_sendCall) {
		read(from, end, cons);
	}
	MTPauth_sendCall(const MTPstring &_phone_number, const MTPstring &_phone_code_hash) : vphone_number(_phone_number), vphone_code_hash(_phone_code_hash) {
	}

	uint32 size() const {
		return vphone_number.size() + vphone_code_hash.size();
	}
	mtpTypeId type() const {
		return mtpc_auth_sendCall;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_auth_sendCall) {
		vphone_number.read(from, end);
		vphone_code_hash.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vphone_number.write(to);
		vphone_code_hash.write(to);
	}

	typedef MTPBool ResponseType;
};
class MTPauth_SendCall : public MTPBoxed<MTPauth_sendCall> {
public:
	MTPauth_SendCall() {
	}
	MTPauth_SendCall(const MTPauth_sendCall &v) : MTPBoxed<MTPauth_sendCall>(v) {
	}
	MTPauth_SendCall(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPauth_sendCall>(from, end, cons) {
	}
	MTPauth_SendCall(const MTPstring &_phone_number, const MTPstring &_phone_code_hash) : MTPBoxed<MTPauth_sendCall>(MTPauth_sendCall(_phone_number, _phone_code_hash)) {
	}
};

class MTPauth_signUp { // RPC method 'auth.signUp'
public:
	MTPstring vphone_number;
	MTPstring vphone_code_hash;
	MTPstring vphone_code;
	MTPstring vfirst_name;
	MTPstring vlast_name;

	MTPauth_signUp() {
	}
	MTPauth_signUp(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_auth_signUp) {
		read(from, end, cons);
	}
	MTPauth_signUp(const MTPstring &_phone_number, const MTPstring &_phone_code_hash, const MTPstring &_phone_code, const MTPstring &_first_name, const MTPstring &_last_name) : vphone_number(_phone_number), vphone_code_hash(_phone_code_hash), vphone_code(_phone_code), vfirst_name(_first_name), vlast_name(_last_name) {
	}

	uint32 size() const {
		return vphone_number.size() + vphone_code_hash.size() + vphone_code.size() + vfirst_name.size() + vlast_name.size();
	}
	mtpTypeId type() const {
		return mtpc_auth_signUp;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_auth_signUp) {
		vphone_number.read(from, end);
		vphone_code_hash.read(from, end);
		vphone_code.read(from, end);
		vfirst_name.read(from, end);
		vlast_name.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vphone_number.write(to);
		vphone_code_hash.write(to);
		vphone_code.write(to);
		vfirst_name.write(to);
		vlast_name.write(to);
	}

	typedef MTPauth_Authorization ResponseType;
};
class MTPauth_SignUp : public MTPBoxed<MTPauth_signUp> {
public:
	MTPauth_SignUp() {
	}
	MTPauth_SignUp(const MTPauth_signUp &v) : MTPBoxed<MTPauth_signUp>(v) {
	}
	MTPauth_SignUp(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPauth_signUp>(from, end, cons) {
	}
	MTPauth_SignUp(const MTPstring &_phone_number, const MTPstring &_phone_code_hash, const MTPstring &_phone_code, const MTPstring &_first_name, const MTPstring &_last_name) : MTPBoxed<MTPauth_signUp>(MTPauth_signUp(_phone_number, _phone_code_hash, _phone_code, _first_name, _last_name)) {
	}
};

class MTPauth_signIn { // RPC method 'auth.signIn'
public:
	MTPstring vphone_number;
	MTPstring vphone_code_hash;
	MTPstring vphone_code;

	MTPauth_signIn() {
	}
	MTPauth_signIn(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_auth_signIn) {
		read(from, end, cons);
	}
	MTPauth_signIn(const MTPstring &_phone_number, const MTPstring &_phone_code_hash, const MTPstring &_phone_code) : vphone_number(_phone_number), vphone_code_hash(_phone_code_hash), vphone_code(_phone_code) {
	}

	uint32 size() const {
		return vphone_number.size() + vphone_code_hash.size() + vphone_code.size();
	}
	mtpTypeId type() const {
		return mtpc_auth_signIn;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_auth_signIn) {
		vphone_number.read(from, end);
		vphone_code_hash.read(from, end);
		vphone_code.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vphone_number.write(to);
		vphone_code_hash.write(to);
		vphone_code.write(to);
	}

	typedef MTPauth_Authorization ResponseType;
};
class MTPauth_SignIn : public MTPBoxed<MTPauth_signIn> {
public:
	MTPauth_SignIn() {
	}
	MTPauth_SignIn(const MTPauth_signIn &v) : MTPBoxed<MTPauth_signIn>(v) {
	}
	MTPauth_SignIn(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPauth_signIn>(from, end, cons) {
	}
	MTPauth_SignIn(const MTPstring &_phone_number, const MTPstring &_phone_code_hash, const MTPstring &_phone_code) : MTPBoxed<MTPauth_signIn>(MTPauth_signIn(_phone_number, _phone_code_hash, _phone_code)) {
	}
};

class MTPauth_logOut { // RPC method 'auth.logOut'
public:
	MTPauth_logOut() {
	}
	MTPauth_logOut(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_auth_logOut) {
		read(from, end, cons);
	}

	uint32 size() const {
		return 0;
	}
	mtpTypeId type() const {
		return mtpc_auth_logOut;
	}
	void read(const mtpPrime *&/*from*/, const mtpPrime */*end*/, mtpTypeId /*cons*/ = mtpc_auth_logOut) {
	}
	void write(mtpBuffer &/*to*/) const {
	}

	typedef MTPBool ResponseType;
};
class MTPauth_LogOut : public MTPBoxed<MTPauth_logOut> {
public:
	MTPauth_LogOut() {
	}
	MTPauth_LogOut(const MTPauth_logOut &v) : MTPBoxed<MTPauth_logOut>(v) {
	}
	MTPauth_LogOut(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPauth_logOut>(from, end, cons) {
	}
};

class MTPauth_resetAuthorizations { // RPC method 'auth.resetAuthorizations'
public:
	MTPauth_resetAuthorizations() {
	}
	MTPauth_resetAuthorizations(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_auth_resetAuthorizations) {
		read(from, end, cons);
	}

	uint32 size() const {
		return 0;
	}
	mtpTypeId type() const {
		return mtpc_auth_resetAuthorizations;
	}
	void read(const mtpPrime *&/*from*/, const mtpPrime */*end*/, mtpTypeId /*cons*/ = mtpc_auth_resetAuthorizations) {
	}
	void write(mtpBuffer &/*to*/) const {
	}

	typedef MTPBool ResponseType;
};
class MTPauth_ResetAuthorizations : public MTPBoxed<MTPauth_resetAuthorizations> {
public:
	MTPauth_ResetAuthorizations() {
	}
	MTPauth_ResetAuthorizations(const MTPauth_resetAuthorizations &v) : MTPBoxed<MTPauth_resetAuthorizations>(v) {
	}
	MTPauth_ResetAuthorizations(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPauth_resetAuthorizations>(from, end, cons) {
	}
};

class MTPauth_sendInvites { // RPC method 'auth.sendInvites'
public:
	MTPVector<MTPstring> vphone_numbers;
	MTPstring vmessage;

	MTPauth_sendInvites() {
	}
	MTPauth_sendInvites(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_auth_sendInvites) {
		read(from, end, cons);
	}
	MTPauth_sendInvites(const MTPVector<MTPstring> &_phone_numbers, const MTPstring &_message) : vphone_numbers(_phone_numbers), vmessage(_message) {
	}

	uint32 size() const {
		return vphone_numbers.size() + vmessage.size();
	}
	mtpTypeId type() const {
		return mtpc_auth_sendInvites;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_auth_sendInvites) {
		vphone_numbers.read(from, end);
		vmessage.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vphone_numbers.write(to);
		vmessage.write(to);
	}

	typedef MTPBool ResponseType;
};
class MTPauth_SendInvites : public MTPBoxed<MTPauth_sendInvites> {
public:
	MTPauth_SendInvites() {
	}
	MTPauth_SendInvites(const MTPauth_sendInvites &v) : MTPBoxed<MTPauth_sendInvites>(v) {
	}
	MTPauth_SendInvites(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPauth_sendInvites>(from, end, cons) {
	}
	MTPauth_SendInvites(const MTPVector<MTPstring> &_phone_numbers, const MTPstring &_message) : MTPBoxed<MTPauth_sendInvites>(MTPauth_sendInvites(_phone_numbers, _message)) {
	}
};

class MTPauth_exportAuthorization { // RPC method 'auth.exportAuthorization'
public:
	MTPint vdc_id;

	MTPauth_exportAuthorization() {
	}
	MTPauth_exportAuthorization(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_auth_exportAuthorization) {
		read(from, end, cons);
	}
	MTPauth_exportAuthorization(MTPint _dc_id) : vdc_id(_dc_id) {
	}

	uint32 size() const {
		return vdc_id.size();
	}
	mtpTypeId type() const {
		return mtpc_auth_exportAuthorization;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_auth_exportAuthorization) {
		vdc_id.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vdc_id.write(to);
	}

	typedef MTPauth_ExportedAuthorization ResponseType;
};
class MTPauth_ExportAuthorization : public MTPBoxed<MTPauth_exportAuthorization> {
public:
	MTPauth_ExportAuthorization() {
	}
	MTPauth_ExportAuthorization(const MTPauth_exportAuthorization &v) : MTPBoxed<MTPauth_exportAuthorization>(v) {
	}
	MTPauth_ExportAuthorization(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPauth_exportAuthorization>(from, end, cons) {
	}
	MTPauth_ExportAuthorization(MTPint _dc_id) : MTPBoxed<MTPauth_exportAuthorization>(MTPauth_exportAuthorization(_dc_id)) {
	}
};

class MTPauth_importAuthorization { // RPC method 'auth.importAuthorization'
public:
	MTPint vid;
	MTPbytes vbytes;

	MTPauth_importAuthorization() {
	}
	MTPauth_importAuthorization(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_auth_importAuthorization) {
		read(from, end, cons);
	}
	MTPauth_importAuthorization(MTPint _id, const MTPbytes &_bytes) : vid(_id), vbytes(_bytes) {
	}

	uint32 size() const {
		return vid.size() + vbytes.size();
	}
	mtpTypeId type() const {
		return mtpc_auth_importAuthorization;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_auth_importAuthorization) {
		vid.read(from, end);
		vbytes.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vid.write(to);
		vbytes.write(to);
	}

	typedef MTPauth_Authorization ResponseType;
};
class MTPauth_ImportAuthorization : public MTPBoxed<MTPauth_importAuthorization> {
public:
	MTPauth_ImportAuthorization() {
	}
	MTPauth_ImportAuthorization(const MTPauth_importAuthorization &v) : MTPBoxed<MTPauth_importAuthorization>(v) {
	}
	MTPauth_ImportAuthorization(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPauth_importAuthorization>(from, end, cons) {
	}
	MTPauth_ImportAuthorization(MTPint _id, const MTPbytes &_bytes) : MTPBoxed<MTPauth_importAuthorization>(MTPauth_importAuthorization(_id, _bytes)) {
	}
};

class MTPaccount_registerDevice { // RPC method 'account.registerDevice'
public:
	MTPint vtoken_type;
	MTPstring vtoken;
	MTPstring vdevice_model;
	MTPstring vsystem_version;
	MTPstring vapp_version;
	MTPBool vapp_sandbox;
	MTPstring vlang_code;

	MTPaccount_registerDevice() {
	}
	MTPaccount_registerDevice(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_account_registerDevice) {
		read(from, end, cons);
	}
	MTPaccount_registerDevice(MTPint _token_type, const MTPstring &_token, const MTPstring &_device_model, const MTPstring &_system_version, const MTPstring &_app_version, MTPBool _app_sandbox, const MTPstring &_lang_code) : vtoken_type(_token_type), vtoken(_token), vdevice_model(_device_model), vsystem_version(_system_version), vapp_version(_app_version), vapp_sandbox(_app_sandbox), vlang_code(_lang_code) {
	}

	uint32 size() const {
		return vtoken_type.size() + vtoken.size() + vdevice_model.size() + vsystem_version.size() + vapp_version.size() + vapp_sandbox.size() + vlang_code.size();
	}
	mtpTypeId type() const {
		return mtpc_account_registerDevice;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_account_registerDevice) {
		vtoken_type.read(from, end);
		vtoken.read(from, end);
		vdevice_model.read(from, end);
		vsystem_version.read(from, end);
		vapp_version.read(from, end);
		vapp_sandbox.read(from, end);
		vlang_code.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vtoken_type.write(to);
		vtoken.write(to);
		vdevice_model.write(to);
		vsystem_version.write(to);
		vapp_version.write(to);
		vapp_sandbox.write(to);
		vlang_code.write(to);
	}

	typedef MTPBool ResponseType;
};
class MTPaccount_RegisterDevice : public MTPBoxed<MTPaccount_registerDevice> {
public:
	MTPaccount_RegisterDevice() {
	}
	MTPaccount_RegisterDevice(const MTPaccount_registerDevice &v) : MTPBoxed<MTPaccount_registerDevice>(v) {
	}
	MTPaccount_RegisterDevice(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPaccount_registerDevice>(from, end, cons) {
	}
	MTPaccount_RegisterDevice(MTPint _token_type, const MTPstring &_token, const MTPstring &_device_model, const MTPstring &_system_version, const MTPstring &_app_version, MTPBool _app_sandbox, const MTPstring &_lang_code) : MTPBoxed<MTPaccount_registerDevice>(MTPaccount_registerDevice(_token_type, _token, _device_model, _system_version, _app_version, _app_sandbox, _lang_code)) {
	}
};

class MTPaccount_unregisterDevice { // RPC method 'account.unregisterDevice'
public:
	MTPint vtoken_type;
	MTPstring vtoken;

	MTPaccount_unregisterDevice() {
	}
	MTPaccount_unregisterDevice(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_account_unregisterDevice) {
		read(from, end, cons);
	}
	MTPaccount_unregisterDevice(MTPint _token_type, const MTPstring &_token) : vtoken_type(_token_type), vtoken(_token) {
	}

	uint32 size() const {
		return vtoken_type.size() + vtoken.size();
	}
	mtpTypeId type() const {
		return mtpc_account_unregisterDevice;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_account_unregisterDevice) {
		vtoken_type.read(from, end);
		vtoken.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vtoken_type.write(to);
		vtoken.write(to);
	}

	typedef MTPBool ResponseType;
};
class MTPaccount_UnregisterDevice : public MTPBoxed<MTPaccount_unregisterDevice> {
public:
	MTPaccount_UnregisterDevice() {
	}
	MTPaccount_UnregisterDevice(const MTPaccount_unregisterDevice &v) : MTPBoxed<MTPaccount_unregisterDevice>(v) {
	}
	MTPaccount_UnregisterDevice(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPaccount_unregisterDevice>(from, end, cons) {
	}
	MTPaccount_UnregisterDevice(MTPint _token_type, const MTPstring &_token) : MTPBoxed<MTPaccount_unregisterDevice>(MTPaccount_unregisterDevice(_token_type, _token)) {
	}
};

class MTPaccount_updateNotifySettings { // RPC method 'account.updateNotifySettings'
public:
	MTPInputNotifyPeer vpeer;
	MTPInputPeerNotifySettings vsettings;

	MTPaccount_updateNotifySettings() {
	}
	MTPaccount_updateNotifySettings(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_account_updateNotifySettings) {
		read(from, end, cons);
	}
	MTPaccount_updateNotifySettings(const MTPInputNotifyPeer &_peer, const MTPInputPeerNotifySettings &_settings) : vpeer(_peer), vsettings(_settings) {
	}

	uint32 size() const {
		return vpeer.size() + vsettings.size();
	}
	mtpTypeId type() const {
		return mtpc_account_updateNotifySettings;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_account_updateNotifySettings) {
		vpeer.read(from, end);
		vsettings.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpeer.write(to);
		vsettings.write(to);
	}

	typedef MTPBool ResponseType;
};
class MTPaccount_UpdateNotifySettings : public MTPBoxed<MTPaccount_updateNotifySettings> {
public:
	MTPaccount_UpdateNotifySettings() {
	}
	MTPaccount_UpdateNotifySettings(const MTPaccount_updateNotifySettings &v) : MTPBoxed<MTPaccount_updateNotifySettings>(v) {
	}
	MTPaccount_UpdateNotifySettings(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPaccount_updateNotifySettings>(from, end, cons) {
	}
	MTPaccount_UpdateNotifySettings(const MTPInputNotifyPeer &_peer, const MTPInputPeerNotifySettings &_settings) : MTPBoxed<MTPaccount_updateNotifySettings>(MTPaccount_updateNotifySettings(_peer, _settings)) {
	}
};

class MTPaccount_getNotifySettings { // RPC method 'account.getNotifySettings'
public:
	MTPInputNotifyPeer vpeer;

	MTPaccount_getNotifySettings() {
	}
	MTPaccount_getNotifySettings(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_account_getNotifySettings) {
		read(from, end, cons);
	}
	MTPaccount_getNotifySettings(const MTPInputNotifyPeer &_peer) : vpeer(_peer) {
	}

	uint32 size() const {
		return vpeer.size();
	}
	mtpTypeId type() const {
		return mtpc_account_getNotifySettings;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_account_getNotifySettings) {
		vpeer.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpeer.write(to);
	}

	typedef MTPPeerNotifySettings ResponseType;
};
class MTPaccount_GetNotifySettings : public MTPBoxed<MTPaccount_getNotifySettings> {
public:
	MTPaccount_GetNotifySettings() {
	}
	MTPaccount_GetNotifySettings(const MTPaccount_getNotifySettings &v) : MTPBoxed<MTPaccount_getNotifySettings>(v) {
	}
	MTPaccount_GetNotifySettings(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPaccount_getNotifySettings>(from, end, cons) {
	}
	MTPaccount_GetNotifySettings(const MTPInputNotifyPeer &_peer) : MTPBoxed<MTPaccount_getNotifySettings>(MTPaccount_getNotifySettings(_peer)) {
	}
};

class MTPaccount_resetNotifySettings { // RPC method 'account.resetNotifySettings'
public:
	MTPaccount_resetNotifySettings() {
	}
	MTPaccount_resetNotifySettings(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_account_resetNotifySettings) {
		read(from, end, cons);
	}

	uint32 size() const {
		return 0;
	}
	mtpTypeId type() const {
		return mtpc_account_resetNotifySettings;
	}
	void read(const mtpPrime *&/*from*/, const mtpPrime */*end*/, mtpTypeId /*cons*/ = mtpc_account_resetNotifySettings) {
	}
	void write(mtpBuffer &/*to*/) const {
	}

	typedef MTPBool ResponseType;
};
class MTPaccount_ResetNotifySettings : public MTPBoxed<MTPaccount_resetNotifySettings> {
public:
	MTPaccount_ResetNotifySettings() {
	}
	MTPaccount_ResetNotifySettings(const MTPaccount_resetNotifySettings &v) : MTPBoxed<MTPaccount_resetNotifySettings>(v) {
	}
	MTPaccount_ResetNotifySettings(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPaccount_resetNotifySettings>(from, end, cons) {
	}
};

class MTPaccount_updateProfile { // RPC method 'account.updateProfile'
public:
	MTPstring vfirst_name;
	MTPstring vlast_name;

	MTPaccount_updateProfile() {
	}
	MTPaccount_updateProfile(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_account_updateProfile) {
		read(from, end, cons);
	}
	MTPaccount_updateProfile(const MTPstring &_first_name, const MTPstring &_last_name) : vfirst_name(_first_name), vlast_name(_last_name) {
	}

	uint32 size() const {
		return vfirst_name.size() + vlast_name.size();
	}
	mtpTypeId type() const {
		return mtpc_account_updateProfile;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_account_updateProfile) {
		vfirst_name.read(from, end);
		vlast_name.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vfirst_name.write(to);
		vlast_name.write(to);
	}

	typedef MTPUser ResponseType;
};
class MTPaccount_UpdateProfile : public MTPBoxed<MTPaccount_updateProfile> {
public:
	MTPaccount_UpdateProfile() {
	}
	MTPaccount_UpdateProfile(const MTPaccount_updateProfile &v) : MTPBoxed<MTPaccount_updateProfile>(v) {
	}
	MTPaccount_UpdateProfile(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPaccount_updateProfile>(from, end, cons) {
	}
	MTPaccount_UpdateProfile(const MTPstring &_first_name, const MTPstring &_last_name) : MTPBoxed<MTPaccount_updateProfile>(MTPaccount_updateProfile(_first_name, _last_name)) {
	}
};

class MTPaccount_updateStatus { // RPC method 'account.updateStatus'
public:
	MTPBool voffline;

	MTPaccount_updateStatus() {
	}
	MTPaccount_updateStatus(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_account_updateStatus) {
		read(from, end, cons);
	}
	MTPaccount_updateStatus(MTPBool _offline) : voffline(_offline) {
	}

	uint32 size() const {
		return voffline.size();
	}
	mtpTypeId type() const {
		return mtpc_account_updateStatus;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_account_updateStatus) {
		voffline.read(from, end);
	}
	void write(mtpBuffer &to) const {
		voffline.write(to);
	}

	typedef MTPBool ResponseType;
};
class MTPaccount_UpdateStatus : public MTPBoxed<MTPaccount_updateStatus> {
public:
	MTPaccount_UpdateStatus() {
	}
	MTPaccount_UpdateStatus(const MTPaccount_updateStatus &v) : MTPBoxed<MTPaccount_updateStatus>(v) {
	}
	MTPaccount_UpdateStatus(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPaccount_updateStatus>(from, end, cons) {
	}
	MTPaccount_UpdateStatus(MTPBool _offline) : MTPBoxed<MTPaccount_updateStatus>(MTPaccount_updateStatus(_offline)) {
	}
};

class MTPaccount_getWallPapers { // RPC method 'account.getWallPapers'
public:
	MTPaccount_getWallPapers() {
	}
	MTPaccount_getWallPapers(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_account_getWallPapers) {
		read(from, end, cons);
	}

	uint32 size() const {
		return 0;
	}
	mtpTypeId type() const {
		return mtpc_account_getWallPapers;
	}
	void read(const mtpPrime *&/*from*/, const mtpPrime */*end*/, mtpTypeId /*cons*/ = mtpc_account_getWallPapers) {
	}
	void write(mtpBuffer &/*to*/) const {
	}

	typedef MTPVector<MTPWallPaper> ResponseType;
};
class MTPaccount_GetWallPapers : public MTPBoxed<MTPaccount_getWallPapers> {
public:
	MTPaccount_GetWallPapers() {
	}
	MTPaccount_GetWallPapers(const MTPaccount_getWallPapers &v) : MTPBoxed<MTPaccount_getWallPapers>(v) {
	}
	MTPaccount_GetWallPapers(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPaccount_getWallPapers>(from, end, cons) {
	}
};

class MTPusers_getUsers { // RPC method 'users.getUsers'
public:
	MTPVector<MTPInputUser> vid;

	MTPusers_getUsers() {
	}
	MTPusers_getUsers(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_users_getUsers) {
		read(from, end, cons);
	}
	MTPusers_getUsers(const MTPVector<MTPInputUser> &_id) : vid(_id) {
	}

	uint32 size() const {
		return vid.size();
	}
	mtpTypeId type() const {
		return mtpc_users_getUsers;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_users_getUsers) {
		vid.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vid.write(to);
	}

	typedef MTPVector<MTPUser> ResponseType;
};
class MTPusers_GetUsers : public MTPBoxed<MTPusers_getUsers> {
public:
	MTPusers_GetUsers() {
	}
	MTPusers_GetUsers(const MTPusers_getUsers &v) : MTPBoxed<MTPusers_getUsers>(v) {
	}
	MTPusers_GetUsers(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPusers_getUsers>(from, end, cons) {
	}
	MTPusers_GetUsers(const MTPVector<MTPInputUser> &_id) : MTPBoxed<MTPusers_getUsers>(MTPusers_getUsers(_id)) {
	}
};

class MTPusers_getFullUser { // RPC method 'users.getFullUser'
public:
	MTPInputUser vid;

	MTPusers_getFullUser() {
	}
	MTPusers_getFullUser(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_users_getFullUser) {
		read(from, end, cons);
	}
	MTPusers_getFullUser(const MTPInputUser &_id) : vid(_id) {
	}

	uint32 size() const {
		return vid.size();
	}
	mtpTypeId type() const {
		return mtpc_users_getFullUser;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_users_getFullUser) {
		vid.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vid.write(to);
	}

	typedef MTPUserFull ResponseType;
};
class MTPusers_GetFullUser : public MTPBoxed<MTPusers_getFullUser> {
public:
	MTPusers_GetFullUser() {
	}
	MTPusers_GetFullUser(const MTPusers_getFullUser &v) : MTPBoxed<MTPusers_getFullUser>(v) {
	}
	MTPusers_GetFullUser(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPusers_getFullUser>(from, end, cons) {
	}
	MTPusers_GetFullUser(const MTPInputUser &_id) : MTPBoxed<MTPusers_getFullUser>(MTPusers_getFullUser(_id)) {
	}
};

class MTPcontacts_getStatuses { // RPC method 'contacts.getStatuses'
public:
	MTPcontacts_getStatuses() {
	}
	MTPcontacts_getStatuses(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contacts_getStatuses) {
		read(from, end, cons);
	}

	uint32 size() const {
		return 0;
	}
	mtpTypeId type() const {
		return mtpc_contacts_getStatuses;
	}
	void read(const mtpPrime *&/*from*/, const mtpPrime */*end*/, mtpTypeId /*cons*/ = mtpc_contacts_getStatuses) {
	}
	void write(mtpBuffer &/*to*/) const {
	}

	typedef MTPVector<MTPContactStatus> ResponseType;
};
class MTPcontacts_GetStatuses : public MTPBoxed<MTPcontacts_getStatuses> {
public:
	MTPcontacts_GetStatuses() {
	}
	MTPcontacts_GetStatuses(const MTPcontacts_getStatuses &v) : MTPBoxed<MTPcontacts_getStatuses>(v) {
	}
	MTPcontacts_GetStatuses(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPcontacts_getStatuses>(from, end, cons) {
	}
};

class MTPcontacts_getContacts { // RPC method 'contacts.getContacts'
public:
	MTPstring vhash;

	MTPcontacts_getContacts() {
	}
	MTPcontacts_getContacts(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contacts_getContacts) {
		read(from, end, cons);
	}
	MTPcontacts_getContacts(const MTPstring &_hash) : vhash(_hash) {
	}

	uint32 size() const {
		return vhash.size();
	}
	mtpTypeId type() const {
		return mtpc_contacts_getContacts;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_contacts_getContacts) {
		vhash.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vhash.write(to);
	}

	typedef MTPcontacts_Contacts ResponseType;
};
class MTPcontacts_GetContacts : public MTPBoxed<MTPcontacts_getContacts> {
public:
	MTPcontacts_GetContacts() {
	}
	MTPcontacts_GetContacts(const MTPcontacts_getContacts &v) : MTPBoxed<MTPcontacts_getContacts>(v) {
	}
	MTPcontacts_GetContacts(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPcontacts_getContacts>(from, end, cons) {
	}
	MTPcontacts_GetContacts(const MTPstring &_hash) : MTPBoxed<MTPcontacts_getContacts>(MTPcontacts_getContacts(_hash)) {
	}
};

class MTPcontacts_importContacts { // RPC method 'contacts.importContacts'
public:
	MTPVector<MTPInputContact> vcontacts;
	MTPBool vreplace;

	MTPcontacts_importContacts() {
	}
	MTPcontacts_importContacts(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contacts_importContacts) {
		read(from, end, cons);
	}
	MTPcontacts_importContacts(const MTPVector<MTPInputContact> &_contacts, MTPBool _replace) : vcontacts(_contacts), vreplace(_replace) {
	}

	uint32 size() const {
		return vcontacts.size() + vreplace.size();
	}
	mtpTypeId type() const {
		return mtpc_contacts_importContacts;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_contacts_importContacts) {
		vcontacts.read(from, end);
		vreplace.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vcontacts.write(to);
		vreplace.write(to);
	}

	typedef MTPcontacts_ImportedContacts ResponseType;
};
class MTPcontacts_ImportContacts : public MTPBoxed<MTPcontacts_importContacts> {
public:
	MTPcontacts_ImportContacts() {
	}
	MTPcontacts_ImportContacts(const MTPcontacts_importContacts &v) : MTPBoxed<MTPcontacts_importContacts>(v) {
	}
	MTPcontacts_ImportContacts(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPcontacts_importContacts>(from, end, cons) {
	}
	MTPcontacts_ImportContacts(const MTPVector<MTPInputContact> &_contacts, MTPBool _replace) : MTPBoxed<MTPcontacts_importContacts>(MTPcontacts_importContacts(_contacts, _replace)) {
	}
};

class MTPcontacts_search { // RPC method 'contacts.search'
public:
	MTPstring vq;
	MTPint vlimit;

	MTPcontacts_search() {
	}
	MTPcontacts_search(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contacts_search) {
		read(from, end, cons);
	}
	MTPcontacts_search(const MTPstring &_q, MTPint _limit) : vq(_q), vlimit(_limit) {
	}

	uint32 size() const {
		return vq.size() + vlimit.size();
	}
	mtpTypeId type() const {
		return mtpc_contacts_search;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_contacts_search) {
		vq.read(from, end);
		vlimit.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vq.write(to);
		vlimit.write(to);
	}

	typedef MTPcontacts_Found ResponseType;
};
class MTPcontacts_Search : public MTPBoxed<MTPcontacts_search> {
public:
	MTPcontacts_Search() {
	}
	MTPcontacts_Search(const MTPcontacts_search &v) : MTPBoxed<MTPcontacts_search>(v) {
	}
	MTPcontacts_Search(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPcontacts_search>(from, end, cons) {
	}
	MTPcontacts_Search(const MTPstring &_q, MTPint _limit) : MTPBoxed<MTPcontacts_search>(MTPcontacts_search(_q, _limit)) {
	}
};

class MTPcontacts_getSuggested { // RPC method 'contacts.getSuggested'
public:
	MTPint vlimit;

	MTPcontacts_getSuggested() {
	}
	MTPcontacts_getSuggested(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contacts_getSuggested) {
		read(from, end, cons);
	}
	MTPcontacts_getSuggested(MTPint _limit) : vlimit(_limit) {
	}

	uint32 size() const {
		return vlimit.size();
	}
	mtpTypeId type() const {
		return mtpc_contacts_getSuggested;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_contacts_getSuggested) {
		vlimit.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vlimit.write(to);
	}

	typedef MTPcontacts_Suggested ResponseType;
};
class MTPcontacts_GetSuggested : public MTPBoxed<MTPcontacts_getSuggested> {
public:
	MTPcontacts_GetSuggested() {
	}
	MTPcontacts_GetSuggested(const MTPcontacts_getSuggested &v) : MTPBoxed<MTPcontacts_getSuggested>(v) {
	}
	MTPcontacts_GetSuggested(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPcontacts_getSuggested>(from, end, cons) {
	}
	MTPcontacts_GetSuggested(MTPint _limit) : MTPBoxed<MTPcontacts_getSuggested>(MTPcontacts_getSuggested(_limit)) {
	}
};

class MTPcontacts_deleteContact { // RPC method 'contacts.deleteContact'
public:
	MTPInputUser vid;

	MTPcontacts_deleteContact() {
	}
	MTPcontacts_deleteContact(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contacts_deleteContact) {
		read(from, end, cons);
	}
	MTPcontacts_deleteContact(const MTPInputUser &_id) : vid(_id) {
	}

	uint32 size() const {
		return vid.size();
	}
	mtpTypeId type() const {
		return mtpc_contacts_deleteContact;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_contacts_deleteContact) {
		vid.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vid.write(to);
	}

	typedef MTPcontacts_Link ResponseType;
};
class MTPcontacts_DeleteContact : public MTPBoxed<MTPcontacts_deleteContact> {
public:
	MTPcontacts_DeleteContact() {
	}
	MTPcontacts_DeleteContact(const MTPcontacts_deleteContact &v) : MTPBoxed<MTPcontacts_deleteContact>(v) {
	}
	MTPcontacts_DeleteContact(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPcontacts_deleteContact>(from, end, cons) {
	}
	MTPcontacts_DeleteContact(const MTPInputUser &_id) : MTPBoxed<MTPcontacts_deleteContact>(MTPcontacts_deleteContact(_id)) {
	}
};

class MTPcontacts_deleteContacts { // RPC method 'contacts.deleteContacts'
public:
	MTPVector<MTPInputUser> vid;

	MTPcontacts_deleteContacts() {
	}
	MTPcontacts_deleteContacts(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contacts_deleteContacts) {
		read(from, end, cons);
	}
	MTPcontacts_deleteContacts(const MTPVector<MTPInputUser> &_id) : vid(_id) {
	}

	uint32 size() const {
		return vid.size();
	}
	mtpTypeId type() const {
		return mtpc_contacts_deleteContacts;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_contacts_deleteContacts) {
		vid.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vid.write(to);
	}

	typedef MTPBool ResponseType;
};
class MTPcontacts_DeleteContacts : public MTPBoxed<MTPcontacts_deleteContacts> {
public:
	MTPcontacts_DeleteContacts() {
	}
	MTPcontacts_DeleteContacts(const MTPcontacts_deleteContacts &v) : MTPBoxed<MTPcontacts_deleteContacts>(v) {
	}
	MTPcontacts_DeleteContacts(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPcontacts_deleteContacts>(from, end, cons) {
	}
	MTPcontacts_DeleteContacts(const MTPVector<MTPInputUser> &_id) : MTPBoxed<MTPcontacts_deleteContacts>(MTPcontacts_deleteContacts(_id)) {
	}
};

class MTPcontacts_block { // RPC method 'contacts.block'
public:
	MTPInputUser vid;

	MTPcontacts_block() {
	}
	MTPcontacts_block(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contacts_block) {
		read(from, end, cons);
	}
	MTPcontacts_block(const MTPInputUser &_id) : vid(_id) {
	}

	uint32 size() const {
		return vid.size();
	}
	mtpTypeId type() const {
		return mtpc_contacts_block;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_contacts_block) {
		vid.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vid.write(to);
	}

	typedef MTPBool ResponseType;
};
class MTPcontacts_Block : public MTPBoxed<MTPcontacts_block> {
public:
	MTPcontacts_Block() {
	}
	MTPcontacts_Block(const MTPcontacts_block &v) : MTPBoxed<MTPcontacts_block>(v) {
	}
	MTPcontacts_Block(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPcontacts_block>(from, end, cons) {
	}
	MTPcontacts_Block(const MTPInputUser &_id) : MTPBoxed<MTPcontacts_block>(MTPcontacts_block(_id)) {
	}
};

class MTPcontacts_unblock { // RPC method 'contacts.unblock'
public:
	MTPInputUser vid;

	MTPcontacts_unblock() {
	}
	MTPcontacts_unblock(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contacts_unblock) {
		read(from, end, cons);
	}
	MTPcontacts_unblock(const MTPInputUser &_id) : vid(_id) {
	}

	uint32 size() const {
		return vid.size();
	}
	mtpTypeId type() const {
		return mtpc_contacts_unblock;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_contacts_unblock) {
		vid.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vid.write(to);
	}

	typedef MTPBool ResponseType;
};
class MTPcontacts_Unblock : public MTPBoxed<MTPcontacts_unblock> {
public:
	MTPcontacts_Unblock() {
	}
	MTPcontacts_Unblock(const MTPcontacts_unblock &v) : MTPBoxed<MTPcontacts_unblock>(v) {
	}
	MTPcontacts_Unblock(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPcontacts_unblock>(from, end, cons) {
	}
	MTPcontacts_Unblock(const MTPInputUser &_id) : MTPBoxed<MTPcontacts_unblock>(MTPcontacts_unblock(_id)) {
	}
};

class MTPcontacts_getBlocked { // RPC method 'contacts.getBlocked'
public:
	MTPint voffset;
	MTPint vlimit;

	MTPcontacts_getBlocked() {
	}
	MTPcontacts_getBlocked(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_contacts_getBlocked) {
		read(from, end, cons);
	}
	MTPcontacts_getBlocked(MTPint _offset, MTPint _limit) : voffset(_offset), vlimit(_limit) {
	}

	uint32 size() const {
		return voffset.size() + vlimit.size();
	}
	mtpTypeId type() const {
		return mtpc_contacts_getBlocked;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_contacts_getBlocked) {
		voffset.read(from, end);
		vlimit.read(from, end);
	}
	void write(mtpBuffer &to) const {
		voffset.write(to);
		vlimit.write(to);
	}

	typedef MTPcontacts_Blocked ResponseType;
};
class MTPcontacts_GetBlocked : public MTPBoxed<MTPcontacts_getBlocked> {
public:
	MTPcontacts_GetBlocked() {
	}
	MTPcontacts_GetBlocked(const MTPcontacts_getBlocked &v) : MTPBoxed<MTPcontacts_getBlocked>(v) {
	}
	MTPcontacts_GetBlocked(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPcontacts_getBlocked>(from, end, cons) {
	}
	MTPcontacts_GetBlocked(MTPint _offset, MTPint _limit) : MTPBoxed<MTPcontacts_getBlocked>(MTPcontacts_getBlocked(_offset, _limit)) {
	}
};

class MTPmessages_getMessages { // RPC method 'messages.getMessages'
public:
	MTPVector<MTPint> vid;

	MTPmessages_getMessages() {
	}
	MTPmessages_getMessages(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_getMessages) {
		read(from, end, cons);
	}
	MTPmessages_getMessages(const MTPVector<MTPint> &_id) : vid(_id) {
	}

	uint32 size() const {
		return vid.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_getMessages;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_getMessages) {
		vid.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vid.write(to);
	}

	typedef MTPmessages_Messages ResponseType;
};
class MTPmessages_GetMessages : public MTPBoxed<MTPmessages_getMessages> {
public:
	MTPmessages_GetMessages() {
	}
	MTPmessages_GetMessages(const MTPmessages_getMessages &v) : MTPBoxed<MTPmessages_getMessages>(v) {
	}
	MTPmessages_GetMessages(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_getMessages>(from, end, cons) {
	}
	MTPmessages_GetMessages(const MTPVector<MTPint> &_id) : MTPBoxed<MTPmessages_getMessages>(MTPmessages_getMessages(_id)) {
	}
};

class MTPmessages_getDialogs { // RPC method 'messages.getDialogs'
public:
	MTPint voffset;
	MTPint vmax_id;
	MTPint vlimit;

	MTPmessages_getDialogs() {
	}
	MTPmessages_getDialogs(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_getDialogs) {
		read(from, end, cons);
	}
	MTPmessages_getDialogs(MTPint _offset, MTPint _max_id, MTPint _limit) : voffset(_offset), vmax_id(_max_id), vlimit(_limit) {
	}

	uint32 size() const {
		return voffset.size() + vmax_id.size() + vlimit.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_getDialogs;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_getDialogs) {
		voffset.read(from, end);
		vmax_id.read(from, end);
		vlimit.read(from, end);
	}
	void write(mtpBuffer &to) const {
		voffset.write(to);
		vmax_id.write(to);
		vlimit.write(to);
	}

	typedef MTPmessages_Dialogs ResponseType;
};
class MTPmessages_GetDialogs : public MTPBoxed<MTPmessages_getDialogs> {
public:
	MTPmessages_GetDialogs() {
	}
	MTPmessages_GetDialogs(const MTPmessages_getDialogs &v) : MTPBoxed<MTPmessages_getDialogs>(v) {
	}
	MTPmessages_GetDialogs(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_getDialogs>(from, end, cons) {
	}
	MTPmessages_GetDialogs(MTPint _offset, MTPint _max_id, MTPint _limit) : MTPBoxed<MTPmessages_getDialogs>(MTPmessages_getDialogs(_offset, _max_id, _limit)) {
	}
};

class MTPmessages_getHistory { // RPC method 'messages.getHistory'
public:
	MTPInputPeer vpeer;
	MTPint voffset;
	MTPint vmax_id;
	MTPint vlimit;

	MTPmessages_getHistory() {
	}
	MTPmessages_getHistory(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_getHistory) {
		read(from, end, cons);
	}
	MTPmessages_getHistory(const MTPInputPeer &_peer, MTPint _offset, MTPint _max_id, MTPint _limit) : vpeer(_peer), voffset(_offset), vmax_id(_max_id), vlimit(_limit) {
	}

	uint32 size() const {
		return vpeer.size() + voffset.size() + vmax_id.size() + vlimit.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_getHistory;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_getHistory) {
		vpeer.read(from, end);
		voffset.read(from, end);
		vmax_id.read(from, end);
		vlimit.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpeer.write(to);
		voffset.write(to);
		vmax_id.write(to);
		vlimit.write(to);
	}

	typedef MTPmessages_Messages ResponseType;
};
class MTPmessages_GetHistory : public MTPBoxed<MTPmessages_getHistory> {
public:
	MTPmessages_GetHistory() {
	}
	MTPmessages_GetHistory(const MTPmessages_getHistory &v) : MTPBoxed<MTPmessages_getHistory>(v) {
	}
	MTPmessages_GetHistory(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_getHistory>(from, end, cons) {
	}
	MTPmessages_GetHistory(const MTPInputPeer &_peer, MTPint _offset, MTPint _max_id, MTPint _limit) : MTPBoxed<MTPmessages_getHistory>(MTPmessages_getHistory(_peer, _offset, _max_id, _limit)) {
	}
};

class MTPmessages_search { // RPC method 'messages.search'
public:
	MTPInputPeer vpeer;
	MTPstring vq;
	MTPMessagesFilter vfilter;
	MTPint vmin_date;
	MTPint vmax_date;
	MTPint voffset;
	MTPint vmax_id;
	MTPint vlimit;

	MTPmessages_search() {
	}
	MTPmessages_search(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_search) {
		read(from, end, cons);
	}
	MTPmessages_search(const MTPInputPeer &_peer, const MTPstring &_q, const MTPMessagesFilter &_filter, MTPint _min_date, MTPint _max_date, MTPint _offset, MTPint _max_id, MTPint _limit) : vpeer(_peer), vq(_q), vfilter(_filter), vmin_date(_min_date), vmax_date(_max_date), voffset(_offset), vmax_id(_max_id), vlimit(_limit) {
	}

	uint32 size() const {
		return vpeer.size() + vq.size() + vfilter.size() + vmin_date.size() + vmax_date.size() + voffset.size() + vmax_id.size() + vlimit.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_search;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_search) {
		vpeer.read(from, end);
		vq.read(from, end);
		vfilter.read(from, end);
		vmin_date.read(from, end);
		vmax_date.read(from, end);
		voffset.read(from, end);
		vmax_id.read(from, end);
		vlimit.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpeer.write(to);
		vq.write(to);
		vfilter.write(to);
		vmin_date.write(to);
		vmax_date.write(to);
		voffset.write(to);
		vmax_id.write(to);
		vlimit.write(to);
	}

	typedef MTPmessages_Messages ResponseType;
};
class MTPmessages_Search : public MTPBoxed<MTPmessages_search> {
public:
	MTPmessages_Search() {
	}
	MTPmessages_Search(const MTPmessages_search &v) : MTPBoxed<MTPmessages_search>(v) {
	}
	MTPmessages_Search(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_search>(from, end, cons) {
	}
	MTPmessages_Search(const MTPInputPeer &_peer, const MTPstring &_q, const MTPMessagesFilter &_filter, MTPint _min_date, MTPint _max_date, MTPint _offset, MTPint _max_id, MTPint _limit) : MTPBoxed<MTPmessages_search>(MTPmessages_search(_peer, _q, _filter, _min_date, _max_date, _offset, _max_id, _limit)) {
	}
};

class MTPmessages_readHistory { // RPC method 'messages.readHistory'
public:
	MTPInputPeer vpeer;
	MTPint vmax_id;
	MTPint voffset;

	MTPmessages_readHistory() {
	}
	MTPmessages_readHistory(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_readHistory) {
		read(from, end, cons);
	}
	MTPmessages_readHistory(const MTPInputPeer &_peer, MTPint _max_id, MTPint _offset) : vpeer(_peer), vmax_id(_max_id), voffset(_offset) {
	}

	uint32 size() const {
		return vpeer.size() + vmax_id.size() + voffset.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_readHistory;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_readHistory) {
		vpeer.read(from, end);
		vmax_id.read(from, end);
		voffset.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpeer.write(to);
		vmax_id.write(to);
		voffset.write(to);
	}

	typedef MTPmessages_AffectedHistory ResponseType;
};
class MTPmessages_ReadHistory : public MTPBoxed<MTPmessages_readHistory> {
public:
	MTPmessages_ReadHistory() {
	}
	MTPmessages_ReadHistory(const MTPmessages_readHistory &v) : MTPBoxed<MTPmessages_readHistory>(v) {
	}
	MTPmessages_ReadHistory(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_readHistory>(from, end, cons) {
	}
	MTPmessages_ReadHistory(const MTPInputPeer &_peer, MTPint _max_id, MTPint _offset) : MTPBoxed<MTPmessages_readHistory>(MTPmessages_readHistory(_peer, _max_id, _offset)) {
	}
};

class MTPmessages_deleteHistory { // RPC method 'messages.deleteHistory'
public:
	MTPInputPeer vpeer;
	MTPint voffset;

	MTPmessages_deleteHistory() {
	}
	MTPmessages_deleteHistory(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_deleteHistory) {
		read(from, end, cons);
	}
	MTPmessages_deleteHistory(const MTPInputPeer &_peer, MTPint _offset) : vpeer(_peer), voffset(_offset) {
	}

	uint32 size() const {
		return vpeer.size() + voffset.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_deleteHistory;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_deleteHistory) {
		vpeer.read(from, end);
		voffset.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpeer.write(to);
		voffset.write(to);
	}

	typedef MTPmessages_AffectedHistory ResponseType;
};
class MTPmessages_DeleteHistory : public MTPBoxed<MTPmessages_deleteHistory> {
public:
	MTPmessages_DeleteHistory() {
	}
	MTPmessages_DeleteHistory(const MTPmessages_deleteHistory &v) : MTPBoxed<MTPmessages_deleteHistory>(v) {
	}
	MTPmessages_DeleteHistory(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_deleteHistory>(from, end, cons) {
	}
	MTPmessages_DeleteHistory(const MTPInputPeer &_peer, MTPint _offset) : MTPBoxed<MTPmessages_deleteHistory>(MTPmessages_deleteHistory(_peer, _offset)) {
	}
};

class MTPmessages_deleteMessages { // RPC method 'messages.deleteMessages'
public:
	MTPVector<MTPint> vid;

	MTPmessages_deleteMessages() {
	}
	MTPmessages_deleteMessages(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_deleteMessages) {
		read(from, end, cons);
	}
	MTPmessages_deleteMessages(const MTPVector<MTPint> &_id) : vid(_id) {
	}

	uint32 size() const {
		return vid.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_deleteMessages;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_deleteMessages) {
		vid.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vid.write(to);
	}

	typedef MTPVector<MTPint> ResponseType;
};
class MTPmessages_DeleteMessages : public MTPBoxed<MTPmessages_deleteMessages> {
public:
	MTPmessages_DeleteMessages() {
	}
	MTPmessages_DeleteMessages(const MTPmessages_deleteMessages &v) : MTPBoxed<MTPmessages_deleteMessages>(v) {
	}
	MTPmessages_DeleteMessages(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_deleteMessages>(from, end, cons) {
	}
	MTPmessages_DeleteMessages(const MTPVector<MTPint> &_id) : MTPBoxed<MTPmessages_deleteMessages>(MTPmessages_deleteMessages(_id)) {
	}
};

class MTPmessages_restoreMessages { // RPC method 'messages.restoreMessages'
public:
	MTPVector<MTPint> vid;

	MTPmessages_restoreMessages() {
	}
	MTPmessages_restoreMessages(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_restoreMessages) {
		read(from, end, cons);
	}
	MTPmessages_restoreMessages(const MTPVector<MTPint> &_id) : vid(_id) {
	}

	uint32 size() const {
		return vid.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_restoreMessages;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_restoreMessages) {
		vid.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vid.write(to);
	}

	typedef MTPVector<MTPint> ResponseType;
};
class MTPmessages_RestoreMessages : public MTPBoxed<MTPmessages_restoreMessages> {
public:
	MTPmessages_RestoreMessages() {
	}
	MTPmessages_RestoreMessages(const MTPmessages_restoreMessages &v) : MTPBoxed<MTPmessages_restoreMessages>(v) {
	}
	MTPmessages_RestoreMessages(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_restoreMessages>(from, end, cons) {
	}
	MTPmessages_RestoreMessages(const MTPVector<MTPint> &_id) : MTPBoxed<MTPmessages_restoreMessages>(MTPmessages_restoreMessages(_id)) {
	}
};

class MTPmessages_receivedMessages { // RPC method 'messages.receivedMessages'
public:
	MTPint vmax_id;

	MTPmessages_receivedMessages() {
	}
	MTPmessages_receivedMessages(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_receivedMessages) {
		read(from, end, cons);
	}
	MTPmessages_receivedMessages(MTPint _max_id) : vmax_id(_max_id) {
	}

	uint32 size() const {
		return vmax_id.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_receivedMessages;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_receivedMessages) {
		vmax_id.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vmax_id.write(to);
	}

	typedef MTPVector<MTPint> ResponseType;
};
class MTPmessages_ReceivedMessages : public MTPBoxed<MTPmessages_receivedMessages> {
public:
	MTPmessages_ReceivedMessages() {
	}
	MTPmessages_ReceivedMessages(const MTPmessages_receivedMessages &v) : MTPBoxed<MTPmessages_receivedMessages>(v) {
	}
	MTPmessages_ReceivedMessages(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_receivedMessages>(from, end, cons) {
	}
	MTPmessages_ReceivedMessages(MTPint _max_id) : MTPBoxed<MTPmessages_receivedMessages>(MTPmessages_receivedMessages(_max_id)) {
	}
};

class MTPmessages_setTyping { // RPC method 'messages.setTyping'
public:
	MTPInputPeer vpeer;
	MTPBool vtyping;

	MTPmessages_setTyping() {
	}
	MTPmessages_setTyping(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_setTyping) {
		read(from, end, cons);
	}
	MTPmessages_setTyping(const MTPInputPeer &_peer, MTPBool _typing) : vpeer(_peer), vtyping(_typing) {
	}

	uint32 size() const {
		return vpeer.size() + vtyping.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_setTyping;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_setTyping) {
		vpeer.read(from, end);
		vtyping.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpeer.write(to);
		vtyping.write(to);
	}

	typedef MTPBool ResponseType;
};
class MTPmessages_SetTyping : public MTPBoxed<MTPmessages_setTyping> {
public:
	MTPmessages_SetTyping() {
	}
	MTPmessages_SetTyping(const MTPmessages_setTyping &v) : MTPBoxed<MTPmessages_setTyping>(v) {
	}
	MTPmessages_SetTyping(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_setTyping>(from, end, cons) {
	}
	MTPmessages_SetTyping(const MTPInputPeer &_peer, MTPBool _typing) : MTPBoxed<MTPmessages_setTyping>(MTPmessages_setTyping(_peer, _typing)) {
	}
};

class MTPmessages_sendMessage { // RPC method 'messages.sendMessage'
public:
	MTPInputPeer vpeer;
	MTPstring vmessage;
	MTPlong vrandom_id;

	MTPmessages_sendMessage() {
	}
	MTPmessages_sendMessage(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_sendMessage) {
		read(from, end, cons);
	}
	MTPmessages_sendMessage(const MTPInputPeer &_peer, const MTPstring &_message, const MTPlong &_random_id) : vpeer(_peer), vmessage(_message), vrandom_id(_random_id) {
	}

	uint32 size() const {
		return vpeer.size() + vmessage.size() + vrandom_id.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_sendMessage;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_sendMessage) {
		vpeer.read(from, end);
		vmessage.read(from, end);
		vrandom_id.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpeer.write(to);
		vmessage.write(to);
		vrandom_id.write(to);
	}

	typedef MTPmessages_SentMessage ResponseType;
};
class MTPmessages_SendMessage : public MTPBoxed<MTPmessages_sendMessage> {
public:
	MTPmessages_SendMessage() {
	}
	MTPmessages_SendMessage(const MTPmessages_sendMessage &v) : MTPBoxed<MTPmessages_sendMessage>(v) {
	}
	MTPmessages_SendMessage(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_sendMessage>(from, end, cons) {
	}
	MTPmessages_SendMessage(const MTPInputPeer &_peer, const MTPstring &_message, const MTPlong &_random_id) : MTPBoxed<MTPmessages_sendMessage>(MTPmessages_sendMessage(_peer, _message, _random_id)) {
	}
};

class MTPmessages_sendMedia { // RPC method 'messages.sendMedia'
public:
	MTPInputPeer vpeer;
	MTPInputMedia vmedia;
	MTPlong vrandom_id;

	MTPmessages_sendMedia() {
	}
	MTPmessages_sendMedia(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_sendMedia) {
		read(from, end, cons);
	}
	MTPmessages_sendMedia(const MTPInputPeer &_peer, const MTPInputMedia &_media, const MTPlong &_random_id) : vpeer(_peer), vmedia(_media), vrandom_id(_random_id) {
	}

	uint32 size() const {
		return vpeer.size() + vmedia.size() + vrandom_id.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_sendMedia;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_sendMedia) {
		vpeer.read(from, end);
		vmedia.read(from, end);
		vrandom_id.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpeer.write(to);
		vmedia.write(to);
		vrandom_id.write(to);
	}

	typedef MTPmessages_StatedMessage ResponseType;
};
class MTPmessages_SendMedia : public MTPBoxed<MTPmessages_sendMedia> {
public:
	MTPmessages_SendMedia() {
	}
	MTPmessages_SendMedia(const MTPmessages_sendMedia &v) : MTPBoxed<MTPmessages_sendMedia>(v) {
	}
	MTPmessages_SendMedia(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_sendMedia>(from, end, cons) {
	}
	MTPmessages_SendMedia(const MTPInputPeer &_peer, const MTPInputMedia &_media, const MTPlong &_random_id) : MTPBoxed<MTPmessages_sendMedia>(MTPmessages_sendMedia(_peer, _media, _random_id)) {
	}
};

class MTPmessages_forwardMessages { // RPC method 'messages.forwardMessages'
public:
	MTPInputPeer vpeer;
	MTPVector<MTPint> vid;

	MTPmessages_forwardMessages() {
	}
	MTPmessages_forwardMessages(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_forwardMessages) {
		read(from, end, cons);
	}
	MTPmessages_forwardMessages(const MTPInputPeer &_peer, const MTPVector<MTPint> &_id) : vpeer(_peer), vid(_id) {
	}

	uint32 size() const {
		return vpeer.size() + vid.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_forwardMessages;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_forwardMessages) {
		vpeer.read(from, end);
		vid.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpeer.write(to);
		vid.write(to);
	}

	typedef MTPmessages_StatedMessages ResponseType;
};
class MTPmessages_ForwardMessages : public MTPBoxed<MTPmessages_forwardMessages> {
public:
	MTPmessages_ForwardMessages() {
	}
	MTPmessages_ForwardMessages(const MTPmessages_forwardMessages &v) : MTPBoxed<MTPmessages_forwardMessages>(v) {
	}
	MTPmessages_ForwardMessages(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_forwardMessages>(from, end, cons) {
	}
	MTPmessages_ForwardMessages(const MTPInputPeer &_peer, const MTPVector<MTPint> &_id) : MTPBoxed<MTPmessages_forwardMessages>(MTPmessages_forwardMessages(_peer, _id)) {
	}
};

class MTPmessages_getChats { // RPC method 'messages.getChats'
public:
	MTPVector<MTPint> vid;

	MTPmessages_getChats() {
	}
	MTPmessages_getChats(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_getChats) {
		read(from, end, cons);
	}
	MTPmessages_getChats(const MTPVector<MTPint> &_id) : vid(_id) {
	}

	uint32 size() const {
		return vid.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_getChats;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_getChats) {
		vid.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vid.write(to);
	}

	typedef MTPmessages_Chats ResponseType;
};
class MTPmessages_GetChats : public MTPBoxed<MTPmessages_getChats> {
public:
	MTPmessages_GetChats() {
	}
	MTPmessages_GetChats(const MTPmessages_getChats &v) : MTPBoxed<MTPmessages_getChats>(v) {
	}
	MTPmessages_GetChats(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_getChats>(from, end, cons) {
	}
	MTPmessages_GetChats(const MTPVector<MTPint> &_id) : MTPBoxed<MTPmessages_getChats>(MTPmessages_getChats(_id)) {
	}
};

class MTPmessages_getFullChat { // RPC method 'messages.getFullChat'
public:
	MTPint vchat_id;

	MTPmessages_getFullChat() {
	}
	MTPmessages_getFullChat(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_getFullChat) {
		read(from, end, cons);
	}
	MTPmessages_getFullChat(MTPint _chat_id) : vchat_id(_chat_id) {
	}

	uint32 size() const {
		return vchat_id.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_getFullChat;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_getFullChat) {
		vchat_id.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vchat_id.write(to);
	}

	typedef MTPmessages_ChatFull ResponseType;
};
class MTPmessages_GetFullChat : public MTPBoxed<MTPmessages_getFullChat> {
public:
	MTPmessages_GetFullChat() {
	}
	MTPmessages_GetFullChat(const MTPmessages_getFullChat &v) : MTPBoxed<MTPmessages_getFullChat>(v) {
	}
	MTPmessages_GetFullChat(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_getFullChat>(from, end, cons) {
	}
	MTPmessages_GetFullChat(MTPint _chat_id) : MTPBoxed<MTPmessages_getFullChat>(MTPmessages_getFullChat(_chat_id)) {
	}
};

class MTPmessages_editChatTitle { // RPC method 'messages.editChatTitle'
public:
	MTPint vchat_id;
	MTPstring vtitle;

	MTPmessages_editChatTitle() {
	}
	MTPmessages_editChatTitle(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_editChatTitle) {
		read(from, end, cons);
	}
	MTPmessages_editChatTitle(MTPint _chat_id, const MTPstring &_title) : vchat_id(_chat_id), vtitle(_title) {
	}

	uint32 size() const {
		return vchat_id.size() + vtitle.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_editChatTitle;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_editChatTitle) {
		vchat_id.read(from, end);
		vtitle.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vchat_id.write(to);
		vtitle.write(to);
	}

	typedef MTPmessages_StatedMessage ResponseType;
};
class MTPmessages_EditChatTitle : public MTPBoxed<MTPmessages_editChatTitle> {
public:
	MTPmessages_EditChatTitle() {
	}
	MTPmessages_EditChatTitle(const MTPmessages_editChatTitle &v) : MTPBoxed<MTPmessages_editChatTitle>(v) {
	}
	MTPmessages_EditChatTitle(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_editChatTitle>(from, end, cons) {
	}
	MTPmessages_EditChatTitle(MTPint _chat_id, const MTPstring &_title) : MTPBoxed<MTPmessages_editChatTitle>(MTPmessages_editChatTitle(_chat_id, _title)) {
	}
};

class MTPmessages_editChatPhoto { // RPC method 'messages.editChatPhoto'
public:
	MTPint vchat_id;
	MTPInputChatPhoto vphoto;

	MTPmessages_editChatPhoto() {
	}
	MTPmessages_editChatPhoto(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_editChatPhoto) {
		read(from, end, cons);
	}
	MTPmessages_editChatPhoto(MTPint _chat_id, const MTPInputChatPhoto &_photo) : vchat_id(_chat_id), vphoto(_photo) {
	}

	uint32 size() const {
		return vchat_id.size() + vphoto.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_editChatPhoto;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_editChatPhoto) {
		vchat_id.read(from, end);
		vphoto.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vchat_id.write(to);
		vphoto.write(to);
	}

	typedef MTPmessages_StatedMessage ResponseType;
};
class MTPmessages_EditChatPhoto : public MTPBoxed<MTPmessages_editChatPhoto> {
public:
	MTPmessages_EditChatPhoto() {
	}
	MTPmessages_EditChatPhoto(const MTPmessages_editChatPhoto &v) : MTPBoxed<MTPmessages_editChatPhoto>(v) {
	}
	MTPmessages_EditChatPhoto(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_editChatPhoto>(from, end, cons) {
	}
	MTPmessages_EditChatPhoto(MTPint _chat_id, const MTPInputChatPhoto &_photo) : MTPBoxed<MTPmessages_editChatPhoto>(MTPmessages_editChatPhoto(_chat_id, _photo)) {
	}
};

class MTPmessages_addChatUser { // RPC method 'messages.addChatUser'
public:
	MTPint vchat_id;
	MTPInputUser vuser_id;
	MTPint vfwd_limit;

	MTPmessages_addChatUser() {
	}
	MTPmessages_addChatUser(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_addChatUser) {
		read(from, end, cons);
	}
	MTPmessages_addChatUser(MTPint _chat_id, const MTPInputUser &_user_id, MTPint _fwd_limit) : vchat_id(_chat_id), vuser_id(_user_id), vfwd_limit(_fwd_limit) {
	}

	uint32 size() const {
		return vchat_id.size() + vuser_id.size() + vfwd_limit.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_addChatUser;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_addChatUser) {
		vchat_id.read(from, end);
		vuser_id.read(from, end);
		vfwd_limit.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vchat_id.write(to);
		vuser_id.write(to);
		vfwd_limit.write(to);
	}

	typedef MTPmessages_StatedMessage ResponseType;
};
class MTPmessages_AddChatUser : public MTPBoxed<MTPmessages_addChatUser> {
public:
	MTPmessages_AddChatUser() {
	}
	MTPmessages_AddChatUser(const MTPmessages_addChatUser &v) : MTPBoxed<MTPmessages_addChatUser>(v) {
	}
	MTPmessages_AddChatUser(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_addChatUser>(from, end, cons) {
	}
	MTPmessages_AddChatUser(MTPint _chat_id, const MTPInputUser &_user_id, MTPint _fwd_limit) : MTPBoxed<MTPmessages_addChatUser>(MTPmessages_addChatUser(_chat_id, _user_id, _fwd_limit)) {
	}
};

class MTPmessages_deleteChatUser { // RPC method 'messages.deleteChatUser'
public:
	MTPint vchat_id;
	MTPInputUser vuser_id;

	MTPmessages_deleteChatUser() {
	}
	MTPmessages_deleteChatUser(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_deleteChatUser) {
		read(from, end, cons);
	}
	MTPmessages_deleteChatUser(MTPint _chat_id, const MTPInputUser &_user_id) : vchat_id(_chat_id), vuser_id(_user_id) {
	}

	uint32 size() const {
		return vchat_id.size() + vuser_id.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_deleteChatUser;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_deleteChatUser) {
		vchat_id.read(from, end);
		vuser_id.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vchat_id.write(to);
		vuser_id.write(to);
	}

	typedef MTPmessages_StatedMessage ResponseType;
};
class MTPmessages_DeleteChatUser : public MTPBoxed<MTPmessages_deleteChatUser> {
public:
	MTPmessages_DeleteChatUser() {
	}
	MTPmessages_DeleteChatUser(const MTPmessages_deleteChatUser &v) : MTPBoxed<MTPmessages_deleteChatUser>(v) {
	}
	MTPmessages_DeleteChatUser(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_deleteChatUser>(from, end, cons) {
	}
	MTPmessages_DeleteChatUser(MTPint _chat_id, const MTPInputUser &_user_id) : MTPBoxed<MTPmessages_deleteChatUser>(MTPmessages_deleteChatUser(_chat_id, _user_id)) {
	}
};

class MTPmessages_createChat { // RPC method 'messages.createChat'
public:
	MTPVector<MTPInputUser> vusers;
	MTPstring vtitle;

	MTPmessages_createChat() {
	}
	MTPmessages_createChat(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_createChat) {
		read(from, end, cons);
	}
	MTPmessages_createChat(const MTPVector<MTPInputUser> &_users, const MTPstring &_title) : vusers(_users), vtitle(_title) {
	}

	uint32 size() const {
		return vusers.size() + vtitle.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_createChat;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_createChat) {
		vusers.read(from, end);
		vtitle.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vusers.write(to);
		vtitle.write(to);
	}

	typedef MTPmessages_StatedMessage ResponseType;
};
class MTPmessages_CreateChat : public MTPBoxed<MTPmessages_createChat> {
public:
	MTPmessages_CreateChat() {
	}
	MTPmessages_CreateChat(const MTPmessages_createChat &v) : MTPBoxed<MTPmessages_createChat>(v) {
	}
	MTPmessages_CreateChat(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_createChat>(from, end, cons) {
	}
	MTPmessages_CreateChat(const MTPVector<MTPInputUser> &_users, const MTPstring &_title) : MTPBoxed<MTPmessages_createChat>(MTPmessages_createChat(_users, _title)) {
	}
};

class MTPupdates_getState { // RPC method 'updates.getState'
public:
	MTPupdates_getState() {
	}
	MTPupdates_getState(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_updates_getState) {
		read(from, end, cons);
	}

	uint32 size() const {
		return 0;
	}
	mtpTypeId type() const {
		return mtpc_updates_getState;
	}
	void read(const mtpPrime *&/*from*/, const mtpPrime */*end*/, mtpTypeId /*cons*/ = mtpc_updates_getState) {
	}
	void write(mtpBuffer &/*to*/) const {
	}

	typedef MTPupdates_State ResponseType;
};
class MTPupdates_GetState : public MTPBoxed<MTPupdates_getState> {
public:
	MTPupdates_GetState() {
	}
	MTPupdates_GetState(const MTPupdates_getState &v) : MTPBoxed<MTPupdates_getState>(v) {
	}
	MTPupdates_GetState(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPupdates_getState>(from, end, cons) {
	}
};

class MTPupdates_getDifference { // RPC method 'updates.getDifference'
public:
	MTPint vpts;
	MTPint vdate;
	MTPint vqts;

	MTPupdates_getDifference() {
	}
	MTPupdates_getDifference(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_updates_getDifference) {
		read(from, end, cons);
	}
	MTPupdates_getDifference(MTPint _pts, MTPint _date, MTPint _qts) : vpts(_pts), vdate(_date), vqts(_qts) {
	}

	uint32 size() const {
		return vpts.size() + vdate.size() + vqts.size();
	}
	mtpTypeId type() const {
		return mtpc_updates_getDifference;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_updates_getDifference) {
		vpts.read(from, end);
		vdate.read(from, end);
		vqts.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpts.write(to);
		vdate.write(to);
		vqts.write(to);
	}

	typedef MTPupdates_Difference ResponseType;
};
class MTPupdates_GetDifference : public MTPBoxed<MTPupdates_getDifference> {
public:
	MTPupdates_GetDifference() {
	}
	MTPupdates_GetDifference(const MTPupdates_getDifference &v) : MTPBoxed<MTPupdates_getDifference>(v) {
	}
	MTPupdates_GetDifference(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPupdates_getDifference>(from, end, cons) {
	}
	MTPupdates_GetDifference(MTPint _pts, MTPint _date, MTPint _qts) : MTPBoxed<MTPupdates_getDifference>(MTPupdates_getDifference(_pts, _date, _qts)) {
	}
};

class MTPphotos_updateProfilePhoto { // RPC method 'photos.updateProfilePhoto'
public:
	MTPInputPhoto vid;
	MTPInputPhotoCrop vcrop;

	MTPphotos_updateProfilePhoto() {
	}
	MTPphotos_updateProfilePhoto(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_photos_updateProfilePhoto) {
		read(from, end, cons);
	}
	MTPphotos_updateProfilePhoto(const MTPInputPhoto &_id, const MTPInputPhotoCrop &_crop) : vid(_id), vcrop(_crop) {
	}

	uint32 size() const {
		return vid.size() + vcrop.size();
	}
	mtpTypeId type() const {
		return mtpc_photos_updateProfilePhoto;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_photos_updateProfilePhoto) {
		vid.read(from, end);
		vcrop.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vid.write(to);
		vcrop.write(to);
	}

	typedef MTPUserProfilePhoto ResponseType;
};
class MTPphotos_UpdateProfilePhoto : public MTPBoxed<MTPphotos_updateProfilePhoto> {
public:
	MTPphotos_UpdateProfilePhoto() {
	}
	MTPphotos_UpdateProfilePhoto(const MTPphotos_updateProfilePhoto &v) : MTPBoxed<MTPphotos_updateProfilePhoto>(v) {
	}
	MTPphotos_UpdateProfilePhoto(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPphotos_updateProfilePhoto>(from, end, cons) {
	}
	MTPphotos_UpdateProfilePhoto(const MTPInputPhoto &_id, const MTPInputPhotoCrop &_crop) : MTPBoxed<MTPphotos_updateProfilePhoto>(MTPphotos_updateProfilePhoto(_id, _crop)) {
	}
};

class MTPphotos_uploadProfilePhoto { // RPC method 'photos.uploadProfilePhoto'
public:
	MTPInputFile vfile;
	MTPstring vcaption;
	MTPInputGeoPoint vgeo_point;
	MTPInputPhotoCrop vcrop;

	MTPphotos_uploadProfilePhoto() {
	}
	MTPphotos_uploadProfilePhoto(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_photos_uploadProfilePhoto) {
		read(from, end, cons);
	}
	MTPphotos_uploadProfilePhoto(const MTPInputFile &_file, const MTPstring &_caption, const MTPInputGeoPoint &_geo_point, const MTPInputPhotoCrop &_crop) : vfile(_file), vcaption(_caption), vgeo_point(_geo_point), vcrop(_crop) {
	}

	uint32 size() const {
		return vfile.size() + vcaption.size() + vgeo_point.size() + vcrop.size();
	}
	mtpTypeId type() const {
		return mtpc_photos_uploadProfilePhoto;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_photos_uploadProfilePhoto) {
		vfile.read(from, end);
		vcaption.read(from, end);
		vgeo_point.read(from, end);
		vcrop.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vfile.write(to);
		vcaption.write(to);
		vgeo_point.write(to);
		vcrop.write(to);
	}

	typedef MTPphotos_Photo ResponseType;
};
class MTPphotos_UploadProfilePhoto : public MTPBoxed<MTPphotos_uploadProfilePhoto> {
public:
	MTPphotos_UploadProfilePhoto() {
	}
	MTPphotos_UploadProfilePhoto(const MTPphotos_uploadProfilePhoto &v) : MTPBoxed<MTPphotos_uploadProfilePhoto>(v) {
	}
	MTPphotos_UploadProfilePhoto(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPphotos_uploadProfilePhoto>(from, end, cons) {
	}
	MTPphotos_UploadProfilePhoto(const MTPInputFile &_file, const MTPstring &_caption, const MTPInputGeoPoint &_geo_point, const MTPInputPhotoCrop &_crop) : MTPBoxed<MTPphotos_uploadProfilePhoto>(MTPphotos_uploadProfilePhoto(_file, _caption, _geo_point, _crop)) {
	}
};

class MTPupload_saveFilePart { // RPC method 'upload.saveFilePart'
public:
	MTPlong vfile_id;
	MTPint vfile_part;
	MTPbytes vbytes;

	MTPupload_saveFilePart() {
	}
	MTPupload_saveFilePart(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_upload_saveFilePart) {
		read(from, end, cons);
	}
	MTPupload_saveFilePart(const MTPlong &_file_id, MTPint _file_part, const MTPbytes &_bytes) : vfile_id(_file_id), vfile_part(_file_part), vbytes(_bytes) {
	}

	uint32 size() const {
		return vfile_id.size() + vfile_part.size() + vbytes.size();
	}
	mtpTypeId type() const {
		return mtpc_upload_saveFilePart;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_upload_saveFilePart) {
		vfile_id.read(from, end);
		vfile_part.read(from, end);
		vbytes.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vfile_id.write(to);
		vfile_part.write(to);
		vbytes.write(to);
	}

	typedef MTPBool ResponseType;
};
class MTPupload_SaveFilePart : public MTPBoxed<MTPupload_saveFilePart> {
public:
	MTPupload_SaveFilePart() {
	}
	MTPupload_SaveFilePart(const MTPupload_saveFilePart &v) : MTPBoxed<MTPupload_saveFilePart>(v) {
	}
	MTPupload_SaveFilePart(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPupload_saveFilePart>(from, end, cons) {
	}
	MTPupload_SaveFilePart(const MTPlong &_file_id, MTPint _file_part, const MTPbytes &_bytes) : MTPBoxed<MTPupload_saveFilePart>(MTPupload_saveFilePart(_file_id, _file_part, _bytes)) {
	}
};

class MTPupload_getFile { // RPC method 'upload.getFile'
public:
	MTPInputFileLocation vlocation;
	MTPint voffset;
	MTPint vlimit;

	MTPupload_getFile() {
	}
	MTPupload_getFile(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_upload_getFile) {
		read(from, end, cons);
	}
	MTPupload_getFile(const MTPInputFileLocation &_location, MTPint _offset, MTPint _limit) : vlocation(_location), voffset(_offset), vlimit(_limit) {
	}

	uint32 size() const {
		return vlocation.size() + voffset.size() + vlimit.size();
	}
	mtpTypeId type() const {
		return mtpc_upload_getFile;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_upload_getFile) {
		vlocation.read(from, end);
		voffset.read(from, end);
		vlimit.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vlocation.write(to);
		voffset.write(to);
		vlimit.write(to);
	}

	typedef MTPupload_File ResponseType;
};
class MTPupload_GetFile : public MTPBoxed<MTPupload_getFile> {
public:
	MTPupload_GetFile() {
	}
	MTPupload_GetFile(const MTPupload_getFile &v) : MTPBoxed<MTPupload_getFile>(v) {
	}
	MTPupload_GetFile(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPupload_getFile>(from, end, cons) {
	}
	MTPupload_GetFile(const MTPInputFileLocation &_location, MTPint _offset, MTPint _limit) : MTPBoxed<MTPupload_getFile>(MTPupload_getFile(_location, _offset, _limit)) {
	}
};

class MTPhelp_getConfig { // RPC method 'help.getConfig'
public:
	MTPhelp_getConfig() {
	}
	MTPhelp_getConfig(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_help_getConfig) {
		read(from, end, cons);
	}

	uint32 size() const {
		return 0;
	}
	mtpTypeId type() const {
		return mtpc_help_getConfig;
	}
	void read(const mtpPrime *&/*from*/, const mtpPrime */*end*/, mtpTypeId /*cons*/ = mtpc_help_getConfig) {
	}
	void write(mtpBuffer &/*to*/) const {
	}

	typedef MTPConfig ResponseType;
};
class MTPhelp_GetConfig : public MTPBoxed<MTPhelp_getConfig> {
public:
	MTPhelp_GetConfig() {
	}
	MTPhelp_GetConfig(const MTPhelp_getConfig &v) : MTPBoxed<MTPhelp_getConfig>(v) {
	}
	MTPhelp_GetConfig(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPhelp_getConfig>(from, end, cons) {
	}
};

class MTPhelp_getNearestDc { // RPC method 'help.getNearestDc'
public:
	MTPhelp_getNearestDc() {
	}
	MTPhelp_getNearestDc(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_help_getNearestDc) {
		read(from, end, cons);
	}

	uint32 size() const {
		return 0;
	}
	mtpTypeId type() const {
		return mtpc_help_getNearestDc;
	}
	void read(const mtpPrime *&/*from*/, const mtpPrime */*end*/, mtpTypeId /*cons*/ = mtpc_help_getNearestDc) {
	}
	void write(mtpBuffer &/*to*/) const {
	}

	typedef MTPNearestDc ResponseType;
};
class MTPhelp_GetNearestDc : public MTPBoxed<MTPhelp_getNearestDc> {
public:
	MTPhelp_GetNearestDc() {
	}
	MTPhelp_GetNearestDc(const MTPhelp_getNearestDc &v) : MTPBoxed<MTPhelp_getNearestDc>(v) {
	}
	MTPhelp_GetNearestDc(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPhelp_getNearestDc>(from, end, cons) {
	}
};

class MTPhelp_getAppUpdate { // RPC method 'help.getAppUpdate'
public:
	MTPstring vdevice_model;
	MTPstring vsystem_version;
	MTPstring vapp_version;
	MTPstring vlang_code;

	MTPhelp_getAppUpdate() {
	}
	MTPhelp_getAppUpdate(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_help_getAppUpdate) {
		read(from, end, cons);
	}
	MTPhelp_getAppUpdate(const MTPstring &_device_model, const MTPstring &_system_version, const MTPstring &_app_version, const MTPstring &_lang_code) : vdevice_model(_device_model), vsystem_version(_system_version), vapp_version(_app_version), vlang_code(_lang_code) {
	}

	uint32 size() const {
		return vdevice_model.size() + vsystem_version.size() + vapp_version.size() + vlang_code.size();
	}
	mtpTypeId type() const {
		return mtpc_help_getAppUpdate;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_help_getAppUpdate) {
		vdevice_model.read(from, end);
		vsystem_version.read(from, end);
		vapp_version.read(from, end);
		vlang_code.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vdevice_model.write(to);
		vsystem_version.write(to);
		vapp_version.write(to);
		vlang_code.write(to);
	}

	typedef MTPhelp_AppUpdate ResponseType;
};
class MTPhelp_GetAppUpdate : public MTPBoxed<MTPhelp_getAppUpdate> {
public:
	MTPhelp_GetAppUpdate() {
	}
	MTPhelp_GetAppUpdate(const MTPhelp_getAppUpdate &v) : MTPBoxed<MTPhelp_getAppUpdate>(v) {
	}
	MTPhelp_GetAppUpdate(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPhelp_getAppUpdate>(from, end, cons) {
	}
	MTPhelp_GetAppUpdate(const MTPstring &_device_model, const MTPstring &_system_version, const MTPstring &_app_version, const MTPstring &_lang_code) : MTPBoxed<MTPhelp_getAppUpdate>(MTPhelp_getAppUpdate(_device_model, _system_version, _app_version, _lang_code)) {
	}
};

class MTPhelp_saveAppLog { // RPC method 'help.saveAppLog'
public:
	MTPVector<MTPInputAppEvent> vevents;

	MTPhelp_saveAppLog() {
	}
	MTPhelp_saveAppLog(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_help_saveAppLog) {
		read(from, end, cons);
	}
	MTPhelp_saveAppLog(const MTPVector<MTPInputAppEvent> &_events) : vevents(_events) {
	}

	uint32 size() const {
		return vevents.size();
	}
	mtpTypeId type() const {
		return mtpc_help_saveAppLog;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_help_saveAppLog) {
		vevents.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vevents.write(to);
	}

	typedef MTPBool ResponseType;
};
class MTPhelp_SaveAppLog : public MTPBoxed<MTPhelp_saveAppLog> {
public:
	MTPhelp_SaveAppLog() {
	}
	MTPhelp_SaveAppLog(const MTPhelp_saveAppLog &v) : MTPBoxed<MTPhelp_saveAppLog>(v) {
	}
	MTPhelp_SaveAppLog(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPhelp_saveAppLog>(from, end, cons) {
	}
	MTPhelp_SaveAppLog(const MTPVector<MTPInputAppEvent> &_events) : MTPBoxed<MTPhelp_saveAppLog>(MTPhelp_saveAppLog(_events)) {
	}
};

class MTPhelp_getInviteText { // RPC method 'help.getInviteText'
public:
	MTPstring vlang_code;

	MTPhelp_getInviteText() {
	}
	MTPhelp_getInviteText(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_help_getInviteText) {
		read(from, end, cons);
	}
	MTPhelp_getInviteText(const MTPstring &_lang_code) : vlang_code(_lang_code) {
	}

	uint32 size() const {
		return vlang_code.size();
	}
	mtpTypeId type() const {
		return mtpc_help_getInviteText;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_help_getInviteText) {
		vlang_code.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vlang_code.write(to);
	}

	typedef MTPhelp_InviteText ResponseType;
};
class MTPhelp_GetInviteText : public MTPBoxed<MTPhelp_getInviteText> {
public:
	MTPhelp_GetInviteText() {
	}
	MTPhelp_GetInviteText(const MTPhelp_getInviteText &v) : MTPBoxed<MTPhelp_getInviteText>(v) {
	}
	MTPhelp_GetInviteText(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPhelp_getInviteText>(from, end, cons) {
	}
	MTPhelp_GetInviteText(const MTPstring &_lang_code) : MTPBoxed<MTPhelp_getInviteText>(MTPhelp_getInviteText(_lang_code)) {
	}
};

class MTPphotos_getUserPhotos { // RPC method 'photos.getUserPhotos'
public:
	MTPInputUser vuser_id;
	MTPint voffset;
	MTPint vmax_id;
	MTPint vlimit;

	MTPphotos_getUserPhotos() {
	}
	MTPphotos_getUserPhotos(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_photos_getUserPhotos) {
		read(from, end, cons);
	}
	MTPphotos_getUserPhotos(const MTPInputUser &_user_id, MTPint _offset, MTPint _max_id, MTPint _limit) : vuser_id(_user_id), voffset(_offset), vmax_id(_max_id), vlimit(_limit) {
	}

	uint32 size() const {
		return vuser_id.size() + voffset.size() + vmax_id.size() + vlimit.size();
	}
	mtpTypeId type() const {
		return mtpc_photos_getUserPhotos;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_photos_getUserPhotos) {
		vuser_id.read(from, end);
		voffset.read(from, end);
		vmax_id.read(from, end);
		vlimit.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vuser_id.write(to);
		voffset.write(to);
		vmax_id.write(to);
		vlimit.write(to);
	}

	typedef MTPphotos_Photos ResponseType;
};
class MTPphotos_GetUserPhotos : public MTPBoxed<MTPphotos_getUserPhotos> {
public:
	MTPphotos_GetUserPhotos() {
	}
	MTPphotos_GetUserPhotos(const MTPphotos_getUserPhotos &v) : MTPBoxed<MTPphotos_getUserPhotos>(v) {
	}
	MTPphotos_GetUserPhotos(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPphotos_getUserPhotos>(from, end, cons) {
	}
	MTPphotos_GetUserPhotos(const MTPInputUser &_user_id, MTPint _offset, MTPint _max_id, MTPint _limit) : MTPBoxed<MTPphotos_getUserPhotos>(MTPphotos_getUserPhotos(_user_id, _offset, _max_id, _limit)) {
	}
};

class MTPmessages_forwardMessage { // RPC method 'messages.forwardMessage'
public:
	MTPInputPeer vpeer;
	MTPint vid;
	MTPlong vrandom_id;

	MTPmessages_forwardMessage() {
	}
	MTPmessages_forwardMessage(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_forwardMessage) {
		read(from, end, cons);
	}
	MTPmessages_forwardMessage(const MTPInputPeer &_peer, MTPint _id, const MTPlong &_random_id) : vpeer(_peer), vid(_id), vrandom_id(_random_id) {
	}

	uint32 size() const {
		return vpeer.size() + vid.size() + vrandom_id.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_forwardMessage;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_forwardMessage) {
		vpeer.read(from, end);
		vid.read(from, end);
		vrandom_id.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpeer.write(to);
		vid.write(to);
		vrandom_id.write(to);
	}

	typedef MTPmessages_StatedMessage ResponseType;
};
class MTPmessages_ForwardMessage : public MTPBoxed<MTPmessages_forwardMessage> {
public:
	MTPmessages_ForwardMessage() {
	}
	MTPmessages_ForwardMessage(const MTPmessages_forwardMessage &v) : MTPBoxed<MTPmessages_forwardMessage>(v) {
	}
	MTPmessages_ForwardMessage(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_forwardMessage>(from, end, cons) {
	}
	MTPmessages_ForwardMessage(const MTPInputPeer &_peer, MTPint _id, const MTPlong &_random_id) : MTPBoxed<MTPmessages_forwardMessage>(MTPmessages_forwardMessage(_peer, _id, _random_id)) {
	}
};

class MTPmessages_sendBroadcast { // RPC method 'messages.sendBroadcast'
public:
	MTPVector<MTPInputUser> vcontacts;
	MTPstring vmessage;
	MTPInputMedia vmedia;

	MTPmessages_sendBroadcast() {
	}
	MTPmessages_sendBroadcast(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_sendBroadcast) {
		read(from, end, cons);
	}
	MTPmessages_sendBroadcast(const MTPVector<MTPInputUser> &_contacts, const MTPstring &_message, const MTPInputMedia &_media) : vcontacts(_contacts), vmessage(_message), vmedia(_media) {
	}

	uint32 size() const {
		return vcontacts.size() + vmessage.size() + vmedia.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_sendBroadcast;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_sendBroadcast) {
		vcontacts.read(from, end);
		vmessage.read(from, end);
		vmedia.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vcontacts.write(to);
		vmessage.write(to);
		vmedia.write(to);
	}

	typedef MTPmessages_StatedMessages ResponseType;
};
class MTPmessages_SendBroadcast : public MTPBoxed<MTPmessages_sendBroadcast> {
public:
	MTPmessages_SendBroadcast() {
	}
	MTPmessages_SendBroadcast(const MTPmessages_sendBroadcast &v) : MTPBoxed<MTPmessages_sendBroadcast>(v) {
	}
	MTPmessages_SendBroadcast(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_sendBroadcast>(from, end, cons) {
	}
	MTPmessages_SendBroadcast(const MTPVector<MTPInputUser> &_contacts, const MTPstring &_message, const MTPInputMedia &_media) : MTPBoxed<MTPmessages_sendBroadcast>(MTPmessages_sendBroadcast(_contacts, _message, _media)) {
	}
};

class MTPgeochats_getLocated { // RPC method 'geochats.getLocated'
public:
	MTPInputGeoPoint vgeo_point;
	MTPint vradius;
	MTPint vlimit;

	MTPgeochats_getLocated() {
	}
	MTPgeochats_getLocated(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_geochats_getLocated) {
		read(from, end, cons);
	}
	MTPgeochats_getLocated(const MTPInputGeoPoint &_geo_point, MTPint _radius, MTPint _limit) : vgeo_point(_geo_point), vradius(_radius), vlimit(_limit) {
	}

	uint32 size() const {
		return vgeo_point.size() + vradius.size() + vlimit.size();
	}
	mtpTypeId type() const {
		return mtpc_geochats_getLocated;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_geochats_getLocated) {
		vgeo_point.read(from, end);
		vradius.read(from, end);
		vlimit.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vgeo_point.write(to);
		vradius.write(to);
		vlimit.write(to);
	}

	typedef MTPgeochats_Located ResponseType;
};
class MTPgeochats_GetLocated : public MTPBoxed<MTPgeochats_getLocated> {
public:
	MTPgeochats_GetLocated() {
	}
	MTPgeochats_GetLocated(const MTPgeochats_getLocated &v) : MTPBoxed<MTPgeochats_getLocated>(v) {
	}
	MTPgeochats_GetLocated(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPgeochats_getLocated>(from, end, cons) {
	}
	MTPgeochats_GetLocated(const MTPInputGeoPoint &_geo_point, MTPint _radius, MTPint _limit) : MTPBoxed<MTPgeochats_getLocated>(MTPgeochats_getLocated(_geo_point, _radius, _limit)) {
	}
};

class MTPgeochats_getRecents { // RPC method 'geochats.getRecents'
public:
	MTPint voffset;
	MTPint vlimit;

	MTPgeochats_getRecents() {
	}
	MTPgeochats_getRecents(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_geochats_getRecents) {
		read(from, end, cons);
	}
	MTPgeochats_getRecents(MTPint _offset, MTPint _limit) : voffset(_offset), vlimit(_limit) {
	}

	uint32 size() const {
		return voffset.size() + vlimit.size();
	}
	mtpTypeId type() const {
		return mtpc_geochats_getRecents;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_geochats_getRecents) {
		voffset.read(from, end);
		vlimit.read(from, end);
	}
	void write(mtpBuffer &to) const {
		voffset.write(to);
		vlimit.write(to);
	}

	typedef MTPgeochats_Messages ResponseType;
};
class MTPgeochats_GetRecents : public MTPBoxed<MTPgeochats_getRecents> {
public:
	MTPgeochats_GetRecents() {
	}
	MTPgeochats_GetRecents(const MTPgeochats_getRecents &v) : MTPBoxed<MTPgeochats_getRecents>(v) {
	}
	MTPgeochats_GetRecents(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPgeochats_getRecents>(from, end, cons) {
	}
	MTPgeochats_GetRecents(MTPint _offset, MTPint _limit) : MTPBoxed<MTPgeochats_getRecents>(MTPgeochats_getRecents(_offset, _limit)) {
	}
};

class MTPgeochats_checkin { // RPC method 'geochats.checkin'
public:
	MTPInputGeoChat vpeer;

	MTPgeochats_checkin() {
	}
	MTPgeochats_checkin(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_geochats_checkin) {
		read(from, end, cons);
	}
	MTPgeochats_checkin(const MTPInputGeoChat &_peer) : vpeer(_peer) {
	}

	uint32 size() const {
		return vpeer.size();
	}
	mtpTypeId type() const {
		return mtpc_geochats_checkin;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_geochats_checkin) {
		vpeer.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpeer.write(to);
	}

	typedef MTPgeochats_StatedMessage ResponseType;
};
class MTPgeochats_Checkin : public MTPBoxed<MTPgeochats_checkin> {
public:
	MTPgeochats_Checkin() {
	}
	MTPgeochats_Checkin(const MTPgeochats_checkin &v) : MTPBoxed<MTPgeochats_checkin>(v) {
	}
	MTPgeochats_Checkin(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPgeochats_checkin>(from, end, cons) {
	}
	MTPgeochats_Checkin(const MTPInputGeoChat &_peer) : MTPBoxed<MTPgeochats_checkin>(MTPgeochats_checkin(_peer)) {
	}
};

class MTPgeochats_getFullChat { // RPC method 'geochats.getFullChat'
public:
	MTPInputGeoChat vpeer;

	MTPgeochats_getFullChat() {
	}
	MTPgeochats_getFullChat(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_geochats_getFullChat) {
		read(from, end, cons);
	}
	MTPgeochats_getFullChat(const MTPInputGeoChat &_peer) : vpeer(_peer) {
	}

	uint32 size() const {
		return vpeer.size();
	}
	mtpTypeId type() const {
		return mtpc_geochats_getFullChat;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_geochats_getFullChat) {
		vpeer.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpeer.write(to);
	}

	typedef MTPmessages_ChatFull ResponseType;
};
class MTPgeochats_GetFullChat : public MTPBoxed<MTPgeochats_getFullChat> {
public:
	MTPgeochats_GetFullChat() {
	}
	MTPgeochats_GetFullChat(const MTPgeochats_getFullChat &v) : MTPBoxed<MTPgeochats_getFullChat>(v) {
	}
	MTPgeochats_GetFullChat(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPgeochats_getFullChat>(from, end, cons) {
	}
	MTPgeochats_GetFullChat(const MTPInputGeoChat &_peer) : MTPBoxed<MTPgeochats_getFullChat>(MTPgeochats_getFullChat(_peer)) {
	}
};

class MTPgeochats_editChatTitle { // RPC method 'geochats.editChatTitle'
public:
	MTPInputGeoChat vpeer;
	MTPstring vtitle;
	MTPstring vaddress;

	MTPgeochats_editChatTitle() {
	}
	MTPgeochats_editChatTitle(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_geochats_editChatTitle) {
		read(from, end, cons);
	}
	MTPgeochats_editChatTitle(const MTPInputGeoChat &_peer, const MTPstring &_title, const MTPstring &_address) : vpeer(_peer), vtitle(_title), vaddress(_address) {
	}

	uint32 size() const {
		return vpeer.size() + vtitle.size() + vaddress.size();
	}
	mtpTypeId type() const {
		return mtpc_geochats_editChatTitle;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_geochats_editChatTitle) {
		vpeer.read(from, end);
		vtitle.read(from, end);
		vaddress.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpeer.write(to);
		vtitle.write(to);
		vaddress.write(to);
	}

	typedef MTPgeochats_StatedMessage ResponseType;
};
class MTPgeochats_EditChatTitle : public MTPBoxed<MTPgeochats_editChatTitle> {
public:
	MTPgeochats_EditChatTitle() {
	}
	MTPgeochats_EditChatTitle(const MTPgeochats_editChatTitle &v) : MTPBoxed<MTPgeochats_editChatTitle>(v) {
	}
	MTPgeochats_EditChatTitle(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPgeochats_editChatTitle>(from, end, cons) {
	}
	MTPgeochats_EditChatTitle(const MTPInputGeoChat &_peer, const MTPstring &_title, const MTPstring &_address) : MTPBoxed<MTPgeochats_editChatTitle>(MTPgeochats_editChatTitle(_peer, _title, _address)) {
	}
};

class MTPgeochats_editChatPhoto { // RPC method 'geochats.editChatPhoto'
public:
	MTPInputGeoChat vpeer;
	MTPInputChatPhoto vphoto;

	MTPgeochats_editChatPhoto() {
	}
	MTPgeochats_editChatPhoto(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_geochats_editChatPhoto) {
		read(from, end, cons);
	}
	MTPgeochats_editChatPhoto(const MTPInputGeoChat &_peer, const MTPInputChatPhoto &_photo) : vpeer(_peer), vphoto(_photo) {
	}

	uint32 size() const {
		return vpeer.size() + vphoto.size();
	}
	mtpTypeId type() const {
		return mtpc_geochats_editChatPhoto;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_geochats_editChatPhoto) {
		vpeer.read(from, end);
		vphoto.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpeer.write(to);
		vphoto.write(to);
	}

	typedef MTPgeochats_StatedMessage ResponseType;
};
class MTPgeochats_EditChatPhoto : public MTPBoxed<MTPgeochats_editChatPhoto> {
public:
	MTPgeochats_EditChatPhoto() {
	}
	MTPgeochats_EditChatPhoto(const MTPgeochats_editChatPhoto &v) : MTPBoxed<MTPgeochats_editChatPhoto>(v) {
	}
	MTPgeochats_EditChatPhoto(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPgeochats_editChatPhoto>(from, end, cons) {
	}
	MTPgeochats_EditChatPhoto(const MTPInputGeoChat &_peer, const MTPInputChatPhoto &_photo) : MTPBoxed<MTPgeochats_editChatPhoto>(MTPgeochats_editChatPhoto(_peer, _photo)) {
	}
};

class MTPgeochats_search { // RPC method 'geochats.search'
public:
	MTPInputGeoChat vpeer;
	MTPstring vq;
	MTPMessagesFilter vfilter;
	MTPint vmin_date;
	MTPint vmax_date;
	MTPint voffset;
	MTPint vmax_id;
	MTPint vlimit;

	MTPgeochats_search() {
	}
	MTPgeochats_search(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_geochats_search) {
		read(from, end, cons);
	}
	MTPgeochats_search(const MTPInputGeoChat &_peer, const MTPstring &_q, const MTPMessagesFilter &_filter, MTPint _min_date, MTPint _max_date, MTPint _offset, MTPint _max_id, MTPint _limit) : vpeer(_peer), vq(_q), vfilter(_filter), vmin_date(_min_date), vmax_date(_max_date), voffset(_offset), vmax_id(_max_id), vlimit(_limit) {
	}

	uint32 size() const {
		return vpeer.size() + vq.size() + vfilter.size() + vmin_date.size() + vmax_date.size() + voffset.size() + vmax_id.size() + vlimit.size();
	}
	mtpTypeId type() const {
		return mtpc_geochats_search;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_geochats_search) {
		vpeer.read(from, end);
		vq.read(from, end);
		vfilter.read(from, end);
		vmin_date.read(from, end);
		vmax_date.read(from, end);
		voffset.read(from, end);
		vmax_id.read(from, end);
		vlimit.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpeer.write(to);
		vq.write(to);
		vfilter.write(to);
		vmin_date.write(to);
		vmax_date.write(to);
		voffset.write(to);
		vmax_id.write(to);
		vlimit.write(to);
	}

	typedef MTPgeochats_Messages ResponseType;
};
class MTPgeochats_Search : public MTPBoxed<MTPgeochats_search> {
public:
	MTPgeochats_Search() {
	}
	MTPgeochats_Search(const MTPgeochats_search &v) : MTPBoxed<MTPgeochats_search>(v) {
	}
	MTPgeochats_Search(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPgeochats_search>(from, end, cons) {
	}
	MTPgeochats_Search(const MTPInputGeoChat &_peer, const MTPstring &_q, const MTPMessagesFilter &_filter, MTPint _min_date, MTPint _max_date, MTPint _offset, MTPint _max_id, MTPint _limit) : MTPBoxed<MTPgeochats_search>(MTPgeochats_search(_peer, _q, _filter, _min_date, _max_date, _offset, _max_id, _limit)) {
	}
};

class MTPgeochats_getHistory { // RPC method 'geochats.getHistory'
public:
	MTPInputGeoChat vpeer;
	MTPint voffset;
	MTPint vmax_id;
	MTPint vlimit;

	MTPgeochats_getHistory() {
	}
	MTPgeochats_getHistory(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_geochats_getHistory) {
		read(from, end, cons);
	}
	MTPgeochats_getHistory(const MTPInputGeoChat &_peer, MTPint _offset, MTPint _max_id, MTPint _limit) : vpeer(_peer), voffset(_offset), vmax_id(_max_id), vlimit(_limit) {
	}

	uint32 size() const {
		return vpeer.size() + voffset.size() + vmax_id.size() + vlimit.size();
	}
	mtpTypeId type() const {
		return mtpc_geochats_getHistory;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_geochats_getHistory) {
		vpeer.read(from, end);
		voffset.read(from, end);
		vmax_id.read(from, end);
		vlimit.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpeer.write(to);
		voffset.write(to);
		vmax_id.write(to);
		vlimit.write(to);
	}

	typedef MTPgeochats_Messages ResponseType;
};
class MTPgeochats_GetHistory : public MTPBoxed<MTPgeochats_getHistory> {
public:
	MTPgeochats_GetHistory() {
	}
	MTPgeochats_GetHistory(const MTPgeochats_getHistory &v) : MTPBoxed<MTPgeochats_getHistory>(v) {
	}
	MTPgeochats_GetHistory(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPgeochats_getHistory>(from, end, cons) {
	}
	MTPgeochats_GetHistory(const MTPInputGeoChat &_peer, MTPint _offset, MTPint _max_id, MTPint _limit) : MTPBoxed<MTPgeochats_getHistory>(MTPgeochats_getHistory(_peer, _offset, _max_id, _limit)) {
	}
};

class MTPgeochats_setTyping { // RPC method 'geochats.setTyping'
public:
	MTPInputGeoChat vpeer;
	MTPBool vtyping;

	MTPgeochats_setTyping() {
	}
	MTPgeochats_setTyping(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_geochats_setTyping) {
		read(from, end, cons);
	}
	MTPgeochats_setTyping(const MTPInputGeoChat &_peer, MTPBool _typing) : vpeer(_peer), vtyping(_typing) {
	}

	uint32 size() const {
		return vpeer.size() + vtyping.size();
	}
	mtpTypeId type() const {
		return mtpc_geochats_setTyping;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_geochats_setTyping) {
		vpeer.read(from, end);
		vtyping.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpeer.write(to);
		vtyping.write(to);
	}

	typedef MTPBool ResponseType;
};
class MTPgeochats_SetTyping : public MTPBoxed<MTPgeochats_setTyping> {
public:
	MTPgeochats_SetTyping() {
	}
	MTPgeochats_SetTyping(const MTPgeochats_setTyping &v) : MTPBoxed<MTPgeochats_setTyping>(v) {
	}
	MTPgeochats_SetTyping(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPgeochats_setTyping>(from, end, cons) {
	}
	MTPgeochats_SetTyping(const MTPInputGeoChat &_peer, MTPBool _typing) : MTPBoxed<MTPgeochats_setTyping>(MTPgeochats_setTyping(_peer, _typing)) {
	}
};

class MTPgeochats_sendMessage { // RPC method 'geochats.sendMessage'
public:
	MTPInputGeoChat vpeer;
	MTPstring vmessage;
	MTPlong vrandom_id;

	MTPgeochats_sendMessage() {
	}
	MTPgeochats_sendMessage(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_geochats_sendMessage) {
		read(from, end, cons);
	}
	MTPgeochats_sendMessage(const MTPInputGeoChat &_peer, const MTPstring &_message, const MTPlong &_random_id) : vpeer(_peer), vmessage(_message), vrandom_id(_random_id) {
	}

	uint32 size() const {
		return vpeer.size() + vmessage.size() + vrandom_id.size();
	}
	mtpTypeId type() const {
		return mtpc_geochats_sendMessage;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_geochats_sendMessage) {
		vpeer.read(from, end);
		vmessage.read(from, end);
		vrandom_id.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpeer.write(to);
		vmessage.write(to);
		vrandom_id.write(to);
	}

	typedef MTPgeochats_StatedMessage ResponseType;
};
class MTPgeochats_SendMessage : public MTPBoxed<MTPgeochats_sendMessage> {
public:
	MTPgeochats_SendMessage() {
	}
	MTPgeochats_SendMessage(const MTPgeochats_sendMessage &v) : MTPBoxed<MTPgeochats_sendMessage>(v) {
	}
	MTPgeochats_SendMessage(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPgeochats_sendMessage>(from, end, cons) {
	}
	MTPgeochats_SendMessage(const MTPInputGeoChat &_peer, const MTPstring &_message, const MTPlong &_random_id) : MTPBoxed<MTPgeochats_sendMessage>(MTPgeochats_sendMessage(_peer, _message, _random_id)) {
	}
};

class MTPgeochats_sendMedia { // RPC method 'geochats.sendMedia'
public:
	MTPInputGeoChat vpeer;
	MTPInputMedia vmedia;
	MTPlong vrandom_id;

	MTPgeochats_sendMedia() {
	}
	MTPgeochats_sendMedia(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_geochats_sendMedia) {
		read(from, end, cons);
	}
	MTPgeochats_sendMedia(const MTPInputGeoChat &_peer, const MTPInputMedia &_media, const MTPlong &_random_id) : vpeer(_peer), vmedia(_media), vrandom_id(_random_id) {
	}

	uint32 size() const {
		return vpeer.size() + vmedia.size() + vrandom_id.size();
	}
	mtpTypeId type() const {
		return mtpc_geochats_sendMedia;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_geochats_sendMedia) {
		vpeer.read(from, end);
		vmedia.read(from, end);
		vrandom_id.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpeer.write(to);
		vmedia.write(to);
		vrandom_id.write(to);
	}

	typedef MTPgeochats_StatedMessage ResponseType;
};
class MTPgeochats_SendMedia : public MTPBoxed<MTPgeochats_sendMedia> {
public:
	MTPgeochats_SendMedia() {
	}
	MTPgeochats_SendMedia(const MTPgeochats_sendMedia &v) : MTPBoxed<MTPgeochats_sendMedia>(v) {
	}
	MTPgeochats_SendMedia(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPgeochats_sendMedia>(from, end, cons) {
	}
	MTPgeochats_SendMedia(const MTPInputGeoChat &_peer, const MTPInputMedia &_media, const MTPlong &_random_id) : MTPBoxed<MTPgeochats_sendMedia>(MTPgeochats_sendMedia(_peer, _media, _random_id)) {
	}
};

class MTPgeochats_createGeoChat { // RPC method 'geochats.createGeoChat'
public:
	MTPstring vtitle;
	MTPInputGeoPoint vgeo_point;
	MTPstring vaddress;
	MTPstring vvenue;

	MTPgeochats_createGeoChat() {
	}
	MTPgeochats_createGeoChat(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_geochats_createGeoChat) {
		read(from, end, cons);
	}
	MTPgeochats_createGeoChat(const MTPstring &_title, const MTPInputGeoPoint &_geo_point, const MTPstring &_address, const MTPstring &_venue) : vtitle(_title), vgeo_point(_geo_point), vaddress(_address), vvenue(_venue) {
	}

	uint32 size() const {
		return vtitle.size() + vgeo_point.size() + vaddress.size() + vvenue.size();
	}
	mtpTypeId type() const {
		return mtpc_geochats_createGeoChat;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_geochats_createGeoChat) {
		vtitle.read(from, end);
		vgeo_point.read(from, end);
		vaddress.read(from, end);
		vvenue.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vtitle.write(to);
		vgeo_point.write(to);
		vaddress.write(to);
		vvenue.write(to);
	}

	typedef MTPgeochats_StatedMessage ResponseType;
};
class MTPgeochats_CreateGeoChat : public MTPBoxed<MTPgeochats_createGeoChat> {
public:
	MTPgeochats_CreateGeoChat() {
	}
	MTPgeochats_CreateGeoChat(const MTPgeochats_createGeoChat &v) : MTPBoxed<MTPgeochats_createGeoChat>(v) {
	}
	MTPgeochats_CreateGeoChat(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPgeochats_createGeoChat>(from, end, cons) {
	}
	MTPgeochats_CreateGeoChat(const MTPstring &_title, const MTPInputGeoPoint &_geo_point, const MTPstring &_address, const MTPstring &_venue) : MTPBoxed<MTPgeochats_createGeoChat>(MTPgeochats_createGeoChat(_title, _geo_point, _address, _venue)) {
	}
};

class MTPmessages_getDhConfig { // RPC method 'messages.getDhConfig'
public:
	MTPint vversion;
	MTPint vrandom_length;

	MTPmessages_getDhConfig() {
	}
	MTPmessages_getDhConfig(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_getDhConfig) {
		read(from, end, cons);
	}
	MTPmessages_getDhConfig(MTPint _version, MTPint _random_length) : vversion(_version), vrandom_length(_random_length) {
	}

	uint32 size() const {
		return vversion.size() + vrandom_length.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_getDhConfig;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_getDhConfig) {
		vversion.read(from, end);
		vrandom_length.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vversion.write(to);
		vrandom_length.write(to);
	}

	typedef MTPmessages_DhConfig ResponseType;
};
class MTPmessages_GetDhConfig : public MTPBoxed<MTPmessages_getDhConfig> {
public:
	MTPmessages_GetDhConfig() {
	}
	MTPmessages_GetDhConfig(const MTPmessages_getDhConfig &v) : MTPBoxed<MTPmessages_getDhConfig>(v) {
	}
	MTPmessages_GetDhConfig(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_getDhConfig>(from, end, cons) {
	}
	MTPmessages_GetDhConfig(MTPint _version, MTPint _random_length) : MTPBoxed<MTPmessages_getDhConfig>(MTPmessages_getDhConfig(_version, _random_length)) {
	}
};

class MTPmessages_requestEncryption { // RPC method 'messages.requestEncryption'
public:
	MTPInputUser vuser_id;
	MTPint vrandom_id;
	MTPbytes vg_a;

	MTPmessages_requestEncryption() {
	}
	MTPmessages_requestEncryption(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_requestEncryption) {
		read(from, end, cons);
	}
	MTPmessages_requestEncryption(const MTPInputUser &_user_id, MTPint _random_id, const MTPbytes &_g_a) : vuser_id(_user_id), vrandom_id(_random_id), vg_a(_g_a) {
	}

	uint32 size() const {
		return vuser_id.size() + vrandom_id.size() + vg_a.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_requestEncryption;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_requestEncryption) {
		vuser_id.read(from, end);
		vrandom_id.read(from, end);
		vg_a.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vuser_id.write(to);
		vrandom_id.write(to);
		vg_a.write(to);
	}

	typedef MTPEncryptedChat ResponseType;
};
class MTPmessages_RequestEncryption : public MTPBoxed<MTPmessages_requestEncryption> {
public:
	MTPmessages_RequestEncryption() {
	}
	MTPmessages_RequestEncryption(const MTPmessages_requestEncryption &v) : MTPBoxed<MTPmessages_requestEncryption>(v) {
	}
	MTPmessages_RequestEncryption(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_requestEncryption>(from, end, cons) {
	}
	MTPmessages_RequestEncryption(const MTPInputUser &_user_id, MTPint _random_id, const MTPbytes &_g_a) : MTPBoxed<MTPmessages_requestEncryption>(MTPmessages_requestEncryption(_user_id, _random_id, _g_a)) {
	}
};

class MTPmessages_acceptEncryption { // RPC method 'messages.acceptEncryption'
public:
	MTPInputEncryptedChat vpeer;
	MTPbytes vg_b;
	MTPlong vkey_fingerprint;

	MTPmessages_acceptEncryption() {
	}
	MTPmessages_acceptEncryption(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_acceptEncryption) {
		read(from, end, cons);
	}
	MTPmessages_acceptEncryption(const MTPInputEncryptedChat &_peer, const MTPbytes &_g_b, const MTPlong &_key_fingerprint) : vpeer(_peer), vg_b(_g_b), vkey_fingerprint(_key_fingerprint) {
	}

	uint32 size() const {
		return vpeer.size() + vg_b.size() + vkey_fingerprint.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_acceptEncryption;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_acceptEncryption) {
		vpeer.read(from, end);
		vg_b.read(from, end);
		vkey_fingerprint.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpeer.write(to);
		vg_b.write(to);
		vkey_fingerprint.write(to);
	}

	typedef MTPEncryptedChat ResponseType;
};
class MTPmessages_AcceptEncryption : public MTPBoxed<MTPmessages_acceptEncryption> {
public:
	MTPmessages_AcceptEncryption() {
	}
	MTPmessages_AcceptEncryption(const MTPmessages_acceptEncryption &v) : MTPBoxed<MTPmessages_acceptEncryption>(v) {
	}
	MTPmessages_AcceptEncryption(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_acceptEncryption>(from, end, cons) {
	}
	MTPmessages_AcceptEncryption(const MTPInputEncryptedChat &_peer, const MTPbytes &_g_b, const MTPlong &_key_fingerprint) : MTPBoxed<MTPmessages_acceptEncryption>(MTPmessages_acceptEncryption(_peer, _g_b, _key_fingerprint)) {
	}
};

class MTPmessages_discardEncryption { // RPC method 'messages.discardEncryption'
public:
	MTPint vchat_id;

	MTPmessages_discardEncryption() {
	}
	MTPmessages_discardEncryption(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_discardEncryption) {
		read(from, end, cons);
	}
	MTPmessages_discardEncryption(MTPint _chat_id) : vchat_id(_chat_id) {
	}

	uint32 size() const {
		return vchat_id.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_discardEncryption;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_discardEncryption) {
		vchat_id.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vchat_id.write(to);
	}

	typedef MTPBool ResponseType;
};
class MTPmessages_DiscardEncryption : public MTPBoxed<MTPmessages_discardEncryption> {
public:
	MTPmessages_DiscardEncryption() {
	}
	MTPmessages_DiscardEncryption(const MTPmessages_discardEncryption &v) : MTPBoxed<MTPmessages_discardEncryption>(v) {
	}
	MTPmessages_DiscardEncryption(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_discardEncryption>(from, end, cons) {
	}
	MTPmessages_DiscardEncryption(MTPint _chat_id) : MTPBoxed<MTPmessages_discardEncryption>(MTPmessages_discardEncryption(_chat_id)) {
	}
};

class MTPmessages_setEncryptedTyping { // RPC method 'messages.setEncryptedTyping'
public:
	MTPInputEncryptedChat vpeer;
	MTPBool vtyping;

	MTPmessages_setEncryptedTyping() {
	}
	MTPmessages_setEncryptedTyping(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_setEncryptedTyping) {
		read(from, end, cons);
	}
	MTPmessages_setEncryptedTyping(const MTPInputEncryptedChat &_peer, MTPBool _typing) : vpeer(_peer), vtyping(_typing) {
	}

	uint32 size() const {
		return vpeer.size() + vtyping.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_setEncryptedTyping;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_setEncryptedTyping) {
		vpeer.read(from, end);
		vtyping.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpeer.write(to);
		vtyping.write(to);
	}

	typedef MTPBool ResponseType;
};
class MTPmessages_SetEncryptedTyping : public MTPBoxed<MTPmessages_setEncryptedTyping> {
public:
	MTPmessages_SetEncryptedTyping() {
	}
	MTPmessages_SetEncryptedTyping(const MTPmessages_setEncryptedTyping &v) : MTPBoxed<MTPmessages_setEncryptedTyping>(v) {
	}
	MTPmessages_SetEncryptedTyping(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_setEncryptedTyping>(from, end, cons) {
	}
	MTPmessages_SetEncryptedTyping(const MTPInputEncryptedChat &_peer, MTPBool _typing) : MTPBoxed<MTPmessages_setEncryptedTyping>(MTPmessages_setEncryptedTyping(_peer, _typing)) {
	}
};

class MTPmessages_readEncryptedHistory { // RPC method 'messages.readEncryptedHistory'
public:
	MTPInputEncryptedChat vpeer;
	MTPint vmax_date;

	MTPmessages_readEncryptedHistory() {
	}
	MTPmessages_readEncryptedHistory(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_readEncryptedHistory) {
		read(from, end, cons);
	}
	MTPmessages_readEncryptedHistory(const MTPInputEncryptedChat &_peer, MTPint _max_date) : vpeer(_peer), vmax_date(_max_date) {
	}

	uint32 size() const {
		return vpeer.size() + vmax_date.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_readEncryptedHistory;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_readEncryptedHistory) {
		vpeer.read(from, end);
		vmax_date.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpeer.write(to);
		vmax_date.write(to);
	}

	typedef MTPBool ResponseType;
};
class MTPmessages_ReadEncryptedHistory : public MTPBoxed<MTPmessages_readEncryptedHistory> {
public:
	MTPmessages_ReadEncryptedHistory() {
	}
	MTPmessages_ReadEncryptedHistory(const MTPmessages_readEncryptedHistory &v) : MTPBoxed<MTPmessages_readEncryptedHistory>(v) {
	}
	MTPmessages_ReadEncryptedHistory(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_readEncryptedHistory>(from, end, cons) {
	}
	MTPmessages_ReadEncryptedHistory(const MTPInputEncryptedChat &_peer, MTPint _max_date) : MTPBoxed<MTPmessages_readEncryptedHistory>(MTPmessages_readEncryptedHistory(_peer, _max_date)) {
	}
};

class MTPmessages_sendEncrypted { // RPC method 'messages.sendEncrypted'
public:
	MTPInputEncryptedChat vpeer;
	MTPlong vrandom_id;
	MTPbytes vdata;

	MTPmessages_sendEncrypted() {
	}
	MTPmessages_sendEncrypted(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_sendEncrypted) {
		read(from, end, cons);
	}
	MTPmessages_sendEncrypted(const MTPInputEncryptedChat &_peer, const MTPlong &_random_id, const MTPbytes &_data) : vpeer(_peer), vrandom_id(_random_id), vdata(_data) {
	}

	uint32 size() const {
		return vpeer.size() + vrandom_id.size() + vdata.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_sendEncrypted;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_sendEncrypted) {
		vpeer.read(from, end);
		vrandom_id.read(from, end);
		vdata.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpeer.write(to);
		vrandom_id.write(to);
		vdata.write(to);
	}

	typedef MTPmessages_SentEncryptedMessage ResponseType;
};
class MTPmessages_SendEncrypted : public MTPBoxed<MTPmessages_sendEncrypted> {
public:
	MTPmessages_SendEncrypted() {
	}
	MTPmessages_SendEncrypted(const MTPmessages_sendEncrypted &v) : MTPBoxed<MTPmessages_sendEncrypted>(v) {
	}
	MTPmessages_SendEncrypted(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_sendEncrypted>(from, end, cons) {
	}
	MTPmessages_SendEncrypted(const MTPInputEncryptedChat &_peer, const MTPlong &_random_id, const MTPbytes &_data) : MTPBoxed<MTPmessages_sendEncrypted>(MTPmessages_sendEncrypted(_peer, _random_id, _data)) {
	}
};

class MTPmessages_sendEncryptedFile { // RPC method 'messages.sendEncryptedFile'
public:
	MTPInputEncryptedChat vpeer;
	MTPlong vrandom_id;
	MTPbytes vdata;
	MTPInputEncryptedFile vfile;

	MTPmessages_sendEncryptedFile() {
	}
	MTPmessages_sendEncryptedFile(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_sendEncryptedFile) {
		read(from, end, cons);
	}
	MTPmessages_sendEncryptedFile(const MTPInputEncryptedChat &_peer, const MTPlong &_random_id, const MTPbytes &_data, const MTPInputEncryptedFile &_file) : vpeer(_peer), vrandom_id(_random_id), vdata(_data), vfile(_file) {
	}

	uint32 size() const {
		return vpeer.size() + vrandom_id.size() + vdata.size() + vfile.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_sendEncryptedFile;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_sendEncryptedFile) {
		vpeer.read(from, end);
		vrandom_id.read(from, end);
		vdata.read(from, end);
		vfile.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpeer.write(to);
		vrandom_id.write(to);
		vdata.write(to);
		vfile.write(to);
	}

	typedef MTPmessages_SentEncryptedMessage ResponseType;
};
class MTPmessages_SendEncryptedFile : public MTPBoxed<MTPmessages_sendEncryptedFile> {
public:
	MTPmessages_SendEncryptedFile() {
	}
	MTPmessages_SendEncryptedFile(const MTPmessages_sendEncryptedFile &v) : MTPBoxed<MTPmessages_sendEncryptedFile>(v) {
	}
	MTPmessages_SendEncryptedFile(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_sendEncryptedFile>(from, end, cons) {
	}
	MTPmessages_SendEncryptedFile(const MTPInputEncryptedChat &_peer, const MTPlong &_random_id, const MTPbytes &_data, const MTPInputEncryptedFile &_file) : MTPBoxed<MTPmessages_sendEncryptedFile>(MTPmessages_sendEncryptedFile(_peer, _random_id, _data, _file)) {
	}
};

class MTPmessages_sendEncryptedService { // RPC method 'messages.sendEncryptedService'
public:
	MTPInputEncryptedChat vpeer;
	MTPlong vrandom_id;
	MTPbytes vdata;

	MTPmessages_sendEncryptedService() {
	}
	MTPmessages_sendEncryptedService(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_sendEncryptedService) {
		read(from, end, cons);
	}
	MTPmessages_sendEncryptedService(const MTPInputEncryptedChat &_peer, const MTPlong &_random_id, const MTPbytes &_data) : vpeer(_peer), vrandom_id(_random_id), vdata(_data) {
	}

	uint32 size() const {
		return vpeer.size() + vrandom_id.size() + vdata.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_sendEncryptedService;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_sendEncryptedService) {
		vpeer.read(from, end);
		vrandom_id.read(from, end);
		vdata.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vpeer.write(to);
		vrandom_id.write(to);
		vdata.write(to);
	}

	typedef MTPmessages_SentEncryptedMessage ResponseType;
};
class MTPmessages_SendEncryptedService : public MTPBoxed<MTPmessages_sendEncryptedService> {
public:
	MTPmessages_SendEncryptedService() {
	}
	MTPmessages_SendEncryptedService(const MTPmessages_sendEncryptedService &v) : MTPBoxed<MTPmessages_sendEncryptedService>(v) {
	}
	MTPmessages_SendEncryptedService(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_sendEncryptedService>(from, end, cons) {
	}
	MTPmessages_SendEncryptedService(const MTPInputEncryptedChat &_peer, const MTPlong &_random_id, const MTPbytes &_data) : MTPBoxed<MTPmessages_sendEncryptedService>(MTPmessages_sendEncryptedService(_peer, _random_id, _data)) {
	}
};

class MTPmessages_receivedQueue { // RPC method 'messages.receivedQueue'
public:
	MTPint vmax_qts;

	MTPmessages_receivedQueue() {
	}
	MTPmessages_receivedQueue(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_messages_receivedQueue) {
		read(from, end, cons);
	}
	MTPmessages_receivedQueue(MTPint _max_qts) : vmax_qts(_max_qts) {
	}

	uint32 size() const {
		return vmax_qts.size();
	}
	mtpTypeId type() const {
		return mtpc_messages_receivedQueue;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_messages_receivedQueue) {
		vmax_qts.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vmax_qts.write(to);
	}

	typedef MTPVector<MTPlong> ResponseType;
};
class MTPmessages_ReceivedQueue : public MTPBoxed<MTPmessages_receivedQueue> {
public:
	MTPmessages_ReceivedQueue() {
	}
	MTPmessages_ReceivedQueue(const MTPmessages_receivedQueue &v) : MTPBoxed<MTPmessages_receivedQueue>(v) {
	}
	MTPmessages_ReceivedQueue(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPmessages_receivedQueue>(from, end, cons) {
	}
	MTPmessages_ReceivedQueue(MTPint _max_qts) : MTPBoxed<MTPmessages_receivedQueue>(MTPmessages_receivedQueue(_max_qts)) {
	}
};

class MTPupload_saveBigFilePart { // RPC method 'upload.saveBigFilePart'
public:
	MTPlong vfile_id;
	MTPint vfile_part;
	MTPint vfile_total_parts;
	MTPbytes vbytes;

	MTPupload_saveBigFilePart() {
	}
	MTPupload_saveBigFilePart(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_upload_saveBigFilePart) {
		read(from, end, cons);
	}
	MTPupload_saveBigFilePart(const MTPlong &_file_id, MTPint _file_part, MTPint _file_total_parts, const MTPbytes &_bytes) : vfile_id(_file_id), vfile_part(_file_part), vfile_total_parts(_file_total_parts), vbytes(_bytes) {
	}

	uint32 size() const {
		return vfile_id.size() + vfile_part.size() + vfile_total_parts.size() + vbytes.size();
	}
	mtpTypeId type() const {
		return mtpc_upload_saveBigFilePart;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_upload_saveBigFilePart) {
		vfile_id.read(from, end);
		vfile_part.read(from, end);
		vfile_total_parts.read(from, end);
		vbytes.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vfile_id.write(to);
		vfile_part.write(to);
		vfile_total_parts.write(to);
		vbytes.write(to);
	}

	typedef MTPBool ResponseType;
};
class MTPupload_SaveBigFilePart : public MTPBoxed<MTPupload_saveBigFilePart> {
public:
	MTPupload_SaveBigFilePart() {
	}
	MTPupload_SaveBigFilePart(const MTPupload_saveBigFilePart &v) : MTPBoxed<MTPupload_saveBigFilePart>(v) {
	}
	MTPupload_SaveBigFilePart(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPupload_saveBigFilePart>(from, end, cons) {
	}
	MTPupload_SaveBigFilePart(const MTPlong &_file_id, MTPint _file_part, MTPint _file_total_parts, const MTPbytes &_bytes) : MTPBoxed<MTPupload_saveBigFilePart>(MTPupload_saveBigFilePart(_file_id, _file_part, _file_total_parts, _bytes)) {
	}
};

template <class TQueryType>
class MTPinitConnection { // RPC method 'initConnection'
public:
	MTPint vapi_id;
	MTPstring vdevice_model;
	MTPstring vsystem_version;
	MTPstring vapp_version;
	MTPstring vlang_code;
	TQueryType vquery;

	MTPinitConnection() {
	}
	MTPinitConnection(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_initConnection) {
		read(from, end, cons);
	}
	MTPinitConnection(MTPint _api_id, const MTPstring &_device_model, const MTPstring &_system_version, const MTPstring &_app_version, const MTPstring &_lang_code, const TQueryType &_query) : vapi_id(_api_id), vdevice_model(_device_model), vsystem_version(_system_version), vapp_version(_app_version), vlang_code(_lang_code), vquery(_query) {
	}

	uint32 size() const {
		return vapi_id.size() + vdevice_model.size() + vsystem_version.size() + vapp_version.size() + vlang_code.size() + vquery.size();
	}
	mtpTypeId type() const {
		return mtpc_initConnection;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId /*cons*/ = mtpc_initConnection) {
		vapi_id.read(from, end);
		vdevice_model.read(from, end);
		vsystem_version.read(from, end);
		vapp_version.read(from, end);
		vlang_code.read(from, end);
		vquery.read(from, end);
	}
	void write(mtpBuffer &to) const {
		vapi_id.write(to);
		vdevice_model.write(to);
		vsystem_version.write(to);
		vapp_version.write(to);
		vlang_code.write(to);
		vquery.write(to);
	}

	typedef typename TQueryType::ResponseType ResponseType;
};
template <typename TQueryType>
class MTPInitConnection : public MTPBoxed<MTPinitConnection<TQueryType> > {
public:
	MTPInitConnection() {
	}
	MTPInitConnection(const MTPinitConnection<TQueryType> &v) : MTPBoxed<MTPinitConnection<TQueryType> >(v) {
	}
	MTPInitConnection(MTPint _api_id, const MTPstring &_device_model, const MTPstring &_system_version, const MTPstring &_app_version, const MTPstring &_lang_code, const TQueryType &_query) : MTPBoxed<MTPinitConnection<TQueryType> >(MTPinitConnection<TQueryType>(_api_id, _device_model, _system_version, _app_version, _lang_code, _query)) {
	}
};

class MTPhelp_getSupport { // RPC method 'help.getSupport'
public:
	MTPhelp_getSupport() {
	}
	MTPhelp_getSupport(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_help_getSupport) {
		read(from, end, cons);
	}

	uint32 size() const {
		return 0;
	}
	mtpTypeId type() const {
		return mtpc_help_getSupport;
	}
	void read(const mtpPrime *&/*from*/, const mtpPrime */*end*/, mtpTypeId /*cons*/ = mtpc_help_getSupport) {
	}
	void write(mtpBuffer &/*to*/) const {
	}

	typedef MTPhelp_Support ResponseType;
};
class MTPhelp_GetSupport : public MTPBoxed<MTPhelp_getSupport> {
public:
	MTPhelp_GetSupport() {
	}
	MTPhelp_GetSupport(const MTPhelp_getSupport &v) : MTPBoxed<MTPhelp_getSupport>(v) {
	}
	MTPhelp_GetSupport(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPhelp_getSupport>(from, end, cons) {
	}
};

// Inline methods definition

inline MTPresPQ::MTPresPQ() : mtpDataOwner(new MTPDresPQ()) {
}

inline uint32 MTPresPQ::size() const {
	const MTPDresPQ &v(c_resPQ());
	return v.vnonce.size() + v.vserver_nonce.size() + v.vpq.size() + v.vserver_public_key_fingerprints.size();
}
inline mtpTypeId MTPresPQ::type() const {
	return mtpc_resPQ;
}
inline void MTPresPQ::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_resPQ) throw mtpErrorUnexpected(cons, "MTPresPQ");

	if (!data) setData(new MTPDresPQ());
	MTPDresPQ &v(_resPQ());
	v.vnonce.read(from, end);
	v.vserver_nonce.read(from, end);
	v.vpq.read(from, end);
	v.vserver_public_key_fingerprints.read(from, end);
}
inline void MTPresPQ::write(mtpBuffer &to) const {
	const MTPDresPQ &v(c_resPQ());
	v.vnonce.write(to);
	v.vserver_nonce.write(to);
	v.vpq.write(to);
	v.vserver_public_key_fingerprints.write(to);
}
inline MTPresPQ::MTPresPQ(MTPDresPQ *_data) : mtpDataOwner(_data) {
}
inline MTPresPQ MTP_resPQ(const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPstring &_pq, const MTPVector<MTPlong> &_server_public_key_fingerprints) {
	return MTPresPQ(new MTPDresPQ(_nonce, _server_nonce, _pq, _server_public_key_fingerprints));
}

inline MTPp_Q_inner_data::MTPp_Q_inner_data() : mtpDataOwner(new MTPDp_q_inner_data()) {
}

inline uint32 MTPp_Q_inner_data::size() const {
	const MTPDp_q_inner_data &v(c_p_q_inner_data());
	return v.vpq.size() + v.vp.size() + v.vq.size() + v.vnonce.size() + v.vserver_nonce.size() + v.vnew_nonce.size();
}
inline mtpTypeId MTPp_Q_inner_data::type() const {
	return mtpc_p_q_inner_data;
}
inline void MTPp_Q_inner_data::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_p_q_inner_data) throw mtpErrorUnexpected(cons, "MTPp_Q_inner_data");

	if (!data) setData(new MTPDp_q_inner_data());
	MTPDp_q_inner_data &v(_p_q_inner_data());
	v.vpq.read(from, end);
	v.vp.read(from, end);
	v.vq.read(from, end);
	v.vnonce.read(from, end);
	v.vserver_nonce.read(from, end);
	v.vnew_nonce.read(from, end);
}
inline void MTPp_Q_inner_data::write(mtpBuffer &to) const {
	const MTPDp_q_inner_data &v(c_p_q_inner_data());
	v.vpq.write(to);
	v.vp.write(to);
	v.vq.write(to);
	v.vnonce.write(to);
	v.vserver_nonce.write(to);
	v.vnew_nonce.write(to);
}
inline MTPp_Q_inner_data::MTPp_Q_inner_data(MTPDp_q_inner_data *_data) : mtpDataOwner(_data) {
}
inline MTPp_Q_inner_data MTP_p_q_inner_data(const MTPstring &_pq, const MTPstring &_p, const MTPstring &_q, const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPint256 &_new_nonce) {
	return MTPp_Q_inner_data(new MTPDp_q_inner_data(_pq, _p, _q, _nonce, _server_nonce, _new_nonce));
}

inline uint32 MTPserver_DH_Params::size() const {
	switch (_type) {
		case mtpc_server_DH_params_fail: {
			const MTPDserver_DH_params_fail &v(c_server_DH_params_fail());
			return v.vnonce.size() + v.vserver_nonce.size() + v.vnew_nonce_hash.size();
		}
		case mtpc_server_DH_params_ok: {
			const MTPDserver_DH_params_ok &v(c_server_DH_params_ok());
			return v.vnonce.size() + v.vserver_nonce.size() + v.vencrypted_answer.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPserver_DH_Params::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPserver_DH_Params::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_server_DH_params_fail: _type = cons; {
			if (!data) setData(new MTPDserver_DH_params_fail());
			MTPDserver_DH_params_fail &v(_server_DH_params_fail());
			v.vnonce.read(from, end);
			v.vserver_nonce.read(from, end);
			v.vnew_nonce_hash.read(from, end);
		} break;
		case mtpc_server_DH_params_ok: _type = cons; {
			if (!data) setData(new MTPDserver_DH_params_ok());
			MTPDserver_DH_params_ok &v(_server_DH_params_ok());
			v.vnonce.read(from, end);
			v.vserver_nonce.read(from, end);
			v.vencrypted_answer.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPserver_DH_Params");
	}
}
inline void MTPserver_DH_Params::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_server_DH_params_fail: {
			const MTPDserver_DH_params_fail &v(c_server_DH_params_fail());
			v.vnonce.write(to);
			v.vserver_nonce.write(to);
			v.vnew_nonce_hash.write(to);
		} break;
		case mtpc_server_DH_params_ok: {
			const MTPDserver_DH_params_ok &v(c_server_DH_params_ok());
			v.vnonce.write(to);
			v.vserver_nonce.write(to);
			v.vencrypted_answer.write(to);
		} break;
	}
}
inline MTPserver_DH_Params::MTPserver_DH_Params(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_server_DH_params_fail: setData(new MTPDserver_DH_params_fail()); break;
		case mtpc_server_DH_params_ok: setData(new MTPDserver_DH_params_ok()); break;
		default: throw mtpErrorBadTypeId(type, "MTPserver_DH_Params");
	}
}
inline MTPserver_DH_Params::MTPserver_DH_Params(MTPDserver_DH_params_fail *_data) : mtpDataOwner(_data), _type(mtpc_server_DH_params_fail) {
}
inline MTPserver_DH_Params::MTPserver_DH_Params(MTPDserver_DH_params_ok *_data) : mtpDataOwner(_data), _type(mtpc_server_DH_params_ok) {
}
inline MTPserver_DH_Params MTP_server_DH_params_fail(const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPint128 &_new_nonce_hash) {
	return MTPserver_DH_Params(new MTPDserver_DH_params_fail(_nonce, _server_nonce, _new_nonce_hash));
}
inline MTPserver_DH_Params MTP_server_DH_params_ok(const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPstring &_encrypted_answer) {
	return MTPserver_DH_Params(new MTPDserver_DH_params_ok(_nonce, _server_nonce, _encrypted_answer));
}

inline MTPserver_DH_inner_data::MTPserver_DH_inner_data() : mtpDataOwner(new MTPDserver_DH_inner_data()) {
}

inline uint32 MTPserver_DH_inner_data::size() const {
	const MTPDserver_DH_inner_data &v(c_server_DH_inner_data());
	return v.vnonce.size() + v.vserver_nonce.size() + v.vg.size() + v.vdh_prime.size() + v.vg_a.size() + v.vserver_time.size();
}
inline mtpTypeId MTPserver_DH_inner_data::type() const {
	return mtpc_server_DH_inner_data;
}
inline void MTPserver_DH_inner_data::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_server_DH_inner_data) throw mtpErrorUnexpected(cons, "MTPserver_DH_inner_data");

	if (!data) setData(new MTPDserver_DH_inner_data());
	MTPDserver_DH_inner_data &v(_server_DH_inner_data());
	v.vnonce.read(from, end);
	v.vserver_nonce.read(from, end);
	v.vg.read(from, end);
	v.vdh_prime.read(from, end);
	v.vg_a.read(from, end);
	v.vserver_time.read(from, end);
}
inline void MTPserver_DH_inner_data::write(mtpBuffer &to) const {
	const MTPDserver_DH_inner_data &v(c_server_DH_inner_data());
	v.vnonce.write(to);
	v.vserver_nonce.write(to);
	v.vg.write(to);
	v.vdh_prime.write(to);
	v.vg_a.write(to);
	v.vserver_time.write(to);
}
inline MTPserver_DH_inner_data::MTPserver_DH_inner_data(MTPDserver_DH_inner_data *_data) : mtpDataOwner(_data) {
}
inline MTPserver_DH_inner_data MTP_server_DH_inner_data(const MTPint128 &_nonce, const MTPint128 &_server_nonce, MTPint _g, const MTPstring &_dh_prime, const MTPstring &_g_a, MTPint _server_time) {
	return MTPserver_DH_inner_data(new MTPDserver_DH_inner_data(_nonce, _server_nonce, _g, _dh_prime, _g_a, _server_time));
}

inline MTPclient_DH_Inner_Data::MTPclient_DH_Inner_Data() : mtpDataOwner(new MTPDclient_DH_inner_data()) {
}

inline uint32 MTPclient_DH_Inner_Data::size() const {
	const MTPDclient_DH_inner_data &v(c_client_DH_inner_data());
	return v.vnonce.size() + v.vserver_nonce.size() + v.vretry_id.size() + v.vg_b.size();
}
inline mtpTypeId MTPclient_DH_Inner_Data::type() const {
	return mtpc_client_DH_inner_data;
}
inline void MTPclient_DH_Inner_Data::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_client_DH_inner_data) throw mtpErrorUnexpected(cons, "MTPclient_DH_Inner_Data");

	if (!data) setData(new MTPDclient_DH_inner_data());
	MTPDclient_DH_inner_data &v(_client_DH_inner_data());
	v.vnonce.read(from, end);
	v.vserver_nonce.read(from, end);
	v.vretry_id.read(from, end);
	v.vg_b.read(from, end);
}
inline void MTPclient_DH_Inner_Data::write(mtpBuffer &to) const {
	const MTPDclient_DH_inner_data &v(c_client_DH_inner_data());
	v.vnonce.write(to);
	v.vserver_nonce.write(to);
	v.vretry_id.write(to);
	v.vg_b.write(to);
}
inline MTPclient_DH_Inner_Data::MTPclient_DH_Inner_Data(MTPDclient_DH_inner_data *_data) : mtpDataOwner(_data) {
}
inline MTPclient_DH_Inner_Data MTP_client_DH_inner_data(const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPlong &_retry_id, const MTPstring &_g_b) {
	return MTPclient_DH_Inner_Data(new MTPDclient_DH_inner_data(_nonce, _server_nonce, _retry_id, _g_b));
}

inline uint32 MTPset_client_DH_params_answer::size() const {
	switch (_type) {
		case mtpc_dh_gen_ok: {
			const MTPDdh_gen_ok &v(c_dh_gen_ok());
			return v.vnonce.size() + v.vserver_nonce.size() + v.vnew_nonce_hash1.size();
		}
		case mtpc_dh_gen_retry: {
			const MTPDdh_gen_retry &v(c_dh_gen_retry());
			return v.vnonce.size() + v.vserver_nonce.size() + v.vnew_nonce_hash2.size();
		}
		case mtpc_dh_gen_fail: {
			const MTPDdh_gen_fail &v(c_dh_gen_fail());
			return v.vnonce.size() + v.vserver_nonce.size() + v.vnew_nonce_hash3.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPset_client_DH_params_answer::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPset_client_DH_params_answer::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_dh_gen_ok: _type = cons; {
			if (!data) setData(new MTPDdh_gen_ok());
			MTPDdh_gen_ok &v(_dh_gen_ok());
			v.vnonce.read(from, end);
			v.vserver_nonce.read(from, end);
			v.vnew_nonce_hash1.read(from, end);
		} break;
		case mtpc_dh_gen_retry: _type = cons; {
			if (!data) setData(new MTPDdh_gen_retry());
			MTPDdh_gen_retry &v(_dh_gen_retry());
			v.vnonce.read(from, end);
			v.vserver_nonce.read(from, end);
			v.vnew_nonce_hash2.read(from, end);
		} break;
		case mtpc_dh_gen_fail: _type = cons; {
			if (!data) setData(new MTPDdh_gen_fail());
			MTPDdh_gen_fail &v(_dh_gen_fail());
			v.vnonce.read(from, end);
			v.vserver_nonce.read(from, end);
			v.vnew_nonce_hash3.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPset_client_DH_params_answer");
	}
}
inline void MTPset_client_DH_params_answer::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_dh_gen_ok: {
			const MTPDdh_gen_ok &v(c_dh_gen_ok());
			v.vnonce.write(to);
			v.vserver_nonce.write(to);
			v.vnew_nonce_hash1.write(to);
		} break;
		case mtpc_dh_gen_retry: {
			const MTPDdh_gen_retry &v(c_dh_gen_retry());
			v.vnonce.write(to);
			v.vserver_nonce.write(to);
			v.vnew_nonce_hash2.write(to);
		} break;
		case mtpc_dh_gen_fail: {
			const MTPDdh_gen_fail &v(c_dh_gen_fail());
			v.vnonce.write(to);
			v.vserver_nonce.write(to);
			v.vnew_nonce_hash3.write(to);
		} break;
	}
}
inline MTPset_client_DH_params_answer::MTPset_client_DH_params_answer(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_dh_gen_ok: setData(new MTPDdh_gen_ok()); break;
		case mtpc_dh_gen_retry: setData(new MTPDdh_gen_retry()); break;
		case mtpc_dh_gen_fail: setData(new MTPDdh_gen_fail()); break;
		default: throw mtpErrorBadTypeId(type, "MTPset_client_DH_params_answer");
	}
}
inline MTPset_client_DH_params_answer::MTPset_client_DH_params_answer(MTPDdh_gen_ok *_data) : mtpDataOwner(_data), _type(mtpc_dh_gen_ok) {
}
inline MTPset_client_DH_params_answer::MTPset_client_DH_params_answer(MTPDdh_gen_retry *_data) : mtpDataOwner(_data), _type(mtpc_dh_gen_retry) {
}
inline MTPset_client_DH_params_answer::MTPset_client_DH_params_answer(MTPDdh_gen_fail *_data) : mtpDataOwner(_data), _type(mtpc_dh_gen_fail) {
}
inline MTPset_client_DH_params_answer MTP_dh_gen_ok(const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPint128 &_new_nonce_hash1) {
	return MTPset_client_DH_params_answer(new MTPDdh_gen_ok(_nonce, _server_nonce, _new_nonce_hash1));
}
inline MTPset_client_DH_params_answer MTP_dh_gen_retry(const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPint128 &_new_nonce_hash2) {
	return MTPset_client_DH_params_answer(new MTPDdh_gen_retry(_nonce, _server_nonce, _new_nonce_hash2));
}
inline MTPset_client_DH_params_answer MTP_dh_gen_fail(const MTPint128 &_nonce, const MTPint128 &_server_nonce, const MTPint128 &_new_nonce_hash3) {
	return MTPset_client_DH_params_answer(new MTPDdh_gen_fail(_nonce, _server_nonce, _new_nonce_hash3));
}

inline MTPmsgsAck::MTPmsgsAck() : mtpDataOwner(new MTPDmsgs_ack()) {
}

inline uint32 MTPmsgsAck::size() const {
	const MTPDmsgs_ack &v(c_msgs_ack());
	return v.vmsg_ids.size();
}
inline mtpTypeId MTPmsgsAck::type() const {
	return mtpc_msgs_ack;
}
inline void MTPmsgsAck::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_msgs_ack) throw mtpErrorUnexpected(cons, "MTPmsgsAck");

	if (!data) setData(new MTPDmsgs_ack());
	MTPDmsgs_ack &v(_msgs_ack());
	v.vmsg_ids.read(from, end);
}
inline void MTPmsgsAck::write(mtpBuffer &to) const {
	const MTPDmsgs_ack &v(c_msgs_ack());
	v.vmsg_ids.write(to);
}
inline MTPmsgsAck::MTPmsgsAck(MTPDmsgs_ack *_data) : mtpDataOwner(_data) {
}
inline MTPmsgsAck MTP_msgs_ack(const MTPVector<MTPlong> &_msg_ids) {
	return MTPmsgsAck(new MTPDmsgs_ack(_msg_ids));
}

inline uint32 MTPbadMsgNotification::size() const {
	switch (_type) {
		case mtpc_bad_msg_notification: {
			const MTPDbad_msg_notification &v(c_bad_msg_notification());
			return v.vbad_msg_id.size() + v.vbad_msg_seqno.size() + v.verror_code.size();
		}
		case mtpc_bad_server_salt: {
			const MTPDbad_server_salt &v(c_bad_server_salt());
			return v.vbad_msg_id.size() + v.vbad_msg_seqno.size() + v.verror_code.size() + v.vnew_server_salt.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPbadMsgNotification::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPbadMsgNotification::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_bad_msg_notification: _type = cons; {
			if (!data) setData(new MTPDbad_msg_notification());
			MTPDbad_msg_notification &v(_bad_msg_notification());
			v.vbad_msg_id.read(from, end);
			v.vbad_msg_seqno.read(from, end);
			v.verror_code.read(from, end);
		} break;
		case mtpc_bad_server_salt: _type = cons; {
			if (!data) setData(new MTPDbad_server_salt());
			MTPDbad_server_salt &v(_bad_server_salt());
			v.vbad_msg_id.read(from, end);
			v.vbad_msg_seqno.read(from, end);
			v.verror_code.read(from, end);
			v.vnew_server_salt.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPbadMsgNotification");
	}
}
inline void MTPbadMsgNotification::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_bad_msg_notification: {
			const MTPDbad_msg_notification &v(c_bad_msg_notification());
			v.vbad_msg_id.write(to);
			v.vbad_msg_seqno.write(to);
			v.verror_code.write(to);
		} break;
		case mtpc_bad_server_salt: {
			const MTPDbad_server_salt &v(c_bad_server_salt());
			v.vbad_msg_id.write(to);
			v.vbad_msg_seqno.write(to);
			v.verror_code.write(to);
			v.vnew_server_salt.write(to);
		} break;
	}
}
inline MTPbadMsgNotification::MTPbadMsgNotification(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_bad_msg_notification: setData(new MTPDbad_msg_notification()); break;
		case mtpc_bad_server_salt: setData(new MTPDbad_server_salt()); break;
		default: throw mtpErrorBadTypeId(type, "MTPbadMsgNotification");
	}
}
inline MTPbadMsgNotification::MTPbadMsgNotification(MTPDbad_msg_notification *_data) : mtpDataOwner(_data), _type(mtpc_bad_msg_notification) {
}
inline MTPbadMsgNotification::MTPbadMsgNotification(MTPDbad_server_salt *_data) : mtpDataOwner(_data), _type(mtpc_bad_server_salt) {
}
inline MTPbadMsgNotification MTP_bad_msg_notification(const MTPlong &_bad_msg_id, MTPint _bad_msg_seqno, MTPint _error_code) {
	return MTPbadMsgNotification(new MTPDbad_msg_notification(_bad_msg_id, _bad_msg_seqno, _error_code));
}
inline MTPbadMsgNotification MTP_bad_server_salt(const MTPlong &_bad_msg_id, MTPint _bad_msg_seqno, MTPint _error_code, const MTPlong &_new_server_salt) {
	return MTPbadMsgNotification(new MTPDbad_server_salt(_bad_msg_id, _bad_msg_seqno, _error_code, _new_server_salt));
}

inline MTPmsgsStateReq::MTPmsgsStateReq() : mtpDataOwner(new MTPDmsgs_state_req()) {
}

inline uint32 MTPmsgsStateReq::size() const {
	const MTPDmsgs_state_req &v(c_msgs_state_req());
	return v.vmsg_ids.size();
}
inline mtpTypeId MTPmsgsStateReq::type() const {
	return mtpc_msgs_state_req;
}
inline void MTPmsgsStateReq::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_msgs_state_req) throw mtpErrorUnexpected(cons, "MTPmsgsStateReq");

	if (!data) setData(new MTPDmsgs_state_req());
	MTPDmsgs_state_req &v(_msgs_state_req());
	v.vmsg_ids.read(from, end);
}
inline void MTPmsgsStateReq::write(mtpBuffer &to) const {
	const MTPDmsgs_state_req &v(c_msgs_state_req());
	v.vmsg_ids.write(to);
}
inline MTPmsgsStateReq::MTPmsgsStateReq(MTPDmsgs_state_req *_data) : mtpDataOwner(_data) {
}
inline MTPmsgsStateReq MTP_msgs_state_req(const MTPVector<MTPlong> &_msg_ids) {
	return MTPmsgsStateReq(new MTPDmsgs_state_req(_msg_ids));
}

inline MTPmsgsStateInfo::MTPmsgsStateInfo() : mtpDataOwner(new MTPDmsgs_state_info()) {
}

inline uint32 MTPmsgsStateInfo::size() const {
	const MTPDmsgs_state_info &v(c_msgs_state_info());
	return v.vreq_msg_id.size() + v.vinfo.size();
}
inline mtpTypeId MTPmsgsStateInfo::type() const {
	return mtpc_msgs_state_info;
}
inline void MTPmsgsStateInfo::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_msgs_state_info) throw mtpErrorUnexpected(cons, "MTPmsgsStateInfo");

	if (!data) setData(new MTPDmsgs_state_info());
	MTPDmsgs_state_info &v(_msgs_state_info());
	v.vreq_msg_id.read(from, end);
	v.vinfo.read(from, end);
}
inline void MTPmsgsStateInfo::write(mtpBuffer &to) const {
	const MTPDmsgs_state_info &v(c_msgs_state_info());
	v.vreq_msg_id.write(to);
	v.vinfo.write(to);
}
inline MTPmsgsStateInfo::MTPmsgsStateInfo(MTPDmsgs_state_info *_data) : mtpDataOwner(_data) {
}
inline MTPmsgsStateInfo MTP_msgs_state_info(const MTPlong &_req_msg_id, const MTPstring &_info) {
	return MTPmsgsStateInfo(new MTPDmsgs_state_info(_req_msg_id, _info));
}

inline MTPmsgsAllInfo::MTPmsgsAllInfo() : mtpDataOwner(new MTPDmsgs_all_info()) {
}

inline uint32 MTPmsgsAllInfo::size() const {
	const MTPDmsgs_all_info &v(c_msgs_all_info());
	return v.vmsg_ids.size() + v.vinfo.size();
}
inline mtpTypeId MTPmsgsAllInfo::type() const {
	return mtpc_msgs_all_info;
}
inline void MTPmsgsAllInfo::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_msgs_all_info) throw mtpErrorUnexpected(cons, "MTPmsgsAllInfo");

	if (!data) setData(new MTPDmsgs_all_info());
	MTPDmsgs_all_info &v(_msgs_all_info());
	v.vmsg_ids.read(from, end);
	v.vinfo.read(from, end);
}
inline void MTPmsgsAllInfo::write(mtpBuffer &to) const {
	const MTPDmsgs_all_info &v(c_msgs_all_info());
	v.vmsg_ids.write(to);
	v.vinfo.write(to);
}
inline MTPmsgsAllInfo::MTPmsgsAllInfo(MTPDmsgs_all_info *_data) : mtpDataOwner(_data) {
}
inline MTPmsgsAllInfo MTP_msgs_all_info(const MTPVector<MTPlong> &_msg_ids, const MTPstring &_info) {
	return MTPmsgsAllInfo(new MTPDmsgs_all_info(_msg_ids, _info));
}

inline uint32 MTPmsgDetailedInfo::size() const {
	switch (_type) {
		case mtpc_msg_detailed_info: {
			const MTPDmsg_detailed_info &v(c_msg_detailed_info());
			return v.vmsg_id.size() + v.vanswer_msg_id.size() + v.vbytes.size() + v.vstatus.size();
		}
		case mtpc_msg_new_detailed_info: {
			const MTPDmsg_new_detailed_info &v(c_msg_new_detailed_info());
			return v.vanswer_msg_id.size() + v.vbytes.size() + v.vstatus.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPmsgDetailedInfo::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPmsgDetailedInfo::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_msg_detailed_info: _type = cons; {
			if (!data) setData(new MTPDmsg_detailed_info());
			MTPDmsg_detailed_info &v(_msg_detailed_info());
			v.vmsg_id.read(from, end);
			v.vanswer_msg_id.read(from, end);
			v.vbytes.read(from, end);
			v.vstatus.read(from, end);
		} break;
		case mtpc_msg_new_detailed_info: _type = cons; {
			if (!data) setData(new MTPDmsg_new_detailed_info());
			MTPDmsg_new_detailed_info &v(_msg_new_detailed_info());
			v.vanswer_msg_id.read(from, end);
			v.vbytes.read(from, end);
			v.vstatus.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPmsgDetailedInfo");
	}
}
inline void MTPmsgDetailedInfo::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_msg_detailed_info: {
			const MTPDmsg_detailed_info &v(c_msg_detailed_info());
			v.vmsg_id.write(to);
			v.vanswer_msg_id.write(to);
			v.vbytes.write(to);
			v.vstatus.write(to);
		} break;
		case mtpc_msg_new_detailed_info: {
			const MTPDmsg_new_detailed_info &v(c_msg_new_detailed_info());
			v.vanswer_msg_id.write(to);
			v.vbytes.write(to);
			v.vstatus.write(to);
		} break;
	}
}
inline MTPmsgDetailedInfo::MTPmsgDetailedInfo(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_msg_detailed_info: setData(new MTPDmsg_detailed_info()); break;
		case mtpc_msg_new_detailed_info: setData(new MTPDmsg_new_detailed_info()); break;
		default: throw mtpErrorBadTypeId(type, "MTPmsgDetailedInfo");
	}
}
inline MTPmsgDetailedInfo::MTPmsgDetailedInfo(MTPDmsg_detailed_info *_data) : mtpDataOwner(_data), _type(mtpc_msg_detailed_info) {
}
inline MTPmsgDetailedInfo::MTPmsgDetailedInfo(MTPDmsg_new_detailed_info *_data) : mtpDataOwner(_data), _type(mtpc_msg_new_detailed_info) {
}
inline MTPmsgDetailedInfo MTP_msg_detailed_info(const MTPlong &_msg_id, const MTPlong &_answer_msg_id, MTPint _bytes, MTPint _status) {
	return MTPmsgDetailedInfo(new MTPDmsg_detailed_info(_msg_id, _answer_msg_id, _bytes, _status));
}
inline MTPmsgDetailedInfo MTP_msg_new_detailed_info(const MTPlong &_answer_msg_id, MTPint _bytes, MTPint _status) {
	return MTPmsgDetailedInfo(new MTPDmsg_new_detailed_info(_answer_msg_id, _bytes, _status));
}

inline MTPmsgResendReq::MTPmsgResendReq() : mtpDataOwner(new MTPDmsg_resend_req()) {
}

inline uint32 MTPmsgResendReq::size() const {
	const MTPDmsg_resend_req &v(c_msg_resend_req());
	return v.vmsg_ids.size();
}
inline mtpTypeId MTPmsgResendReq::type() const {
	return mtpc_msg_resend_req;
}
inline void MTPmsgResendReq::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_msg_resend_req) throw mtpErrorUnexpected(cons, "MTPmsgResendReq");

	if (!data) setData(new MTPDmsg_resend_req());
	MTPDmsg_resend_req &v(_msg_resend_req());
	v.vmsg_ids.read(from, end);
}
inline void MTPmsgResendReq::write(mtpBuffer &to) const {
	const MTPDmsg_resend_req &v(c_msg_resend_req());
	v.vmsg_ids.write(to);
}
inline MTPmsgResendReq::MTPmsgResendReq(MTPDmsg_resend_req *_data) : mtpDataOwner(_data) {
}
inline MTPmsgResendReq MTP_msg_resend_req(const MTPVector<MTPlong> &_msg_ids) {
	return MTPmsgResendReq(new MTPDmsg_resend_req(_msg_ids));
}

inline MTPrpcError::MTPrpcError() : mtpDataOwner(new MTPDrpc_error()) {
}

inline uint32 MTPrpcError::size() const {
	const MTPDrpc_error &v(c_rpc_error());
	return v.verror_code.size() + v.verror_message.size();
}
inline mtpTypeId MTPrpcError::type() const {
	return mtpc_rpc_error;
}
inline void MTPrpcError::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_rpc_error) throw mtpErrorUnexpected(cons, "MTPrpcError");

	if (!data) setData(new MTPDrpc_error());
	MTPDrpc_error &v(_rpc_error());
	v.verror_code.read(from, end);
	v.verror_message.read(from, end);
}
inline void MTPrpcError::write(mtpBuffer &to) const {
	const MTPDrpc_error &v(c_rpc_error());
	v.verror_code.write(to);
	v.verror_message.write(to);
}
inline MTPrpcError::MTPrpcError(MTPDrpc_error *_data) : mtpDataOwner(_data) {
}
inline MTPrpcError MTP_rpc_error(MTPint _error_code, const MTPstring &_error_message) {
	return MTPrpcError(new MTPDrpc_error(_error_code, _error_message));
}

inline uint32 MTPrpcDropAnswer::size() const {
	switch (_type) {
		case mtpc_rpc_answer_dropped: {
			const MTPDrpc_answer_dropped &v(c_rpc_answer_dropped());
			return v.vmsg_id.size() + v.vseq_no.size() + v.vbytes.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPrpcDropAnswer::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPrpcDropAnswer::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_rpc_answer_unknown: _type = cons; break;
		case mtpc_rpc_answer_dropped_running: _type = cons; break;
		case mtpc_rpc_answer_dropped: _type = cons; {
			if (!data) setData(new MTPDrpc_answer_dropped());
			MTPDrpc_answer_dropped &v(_rpc_answer_dropped());
			v.vmsg_id.read(from, end);
			v.vseq_no.read(from, end);
			v.vbytes.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPrpcDropAnswer");
	}
}
inline void MTPrpcDropAnswer::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_rpc_answer_dropped: {
			const MTPDrpc_answer_dropped &v(c_rpc_answer_dropped());
			v.vmsg_id.write(to);
			v.vseq_no.write(to);
			v.vbytes.write(to);
		} break;
	}
}
inline MTPrpcDropAnswer::MTPrpcDropAnswer(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_rpc_answer_unknown: break;
		case mtpc_rpc_answer_dropped_running: break;
		case mtpc_rpc_answer_dropped: setData(new MTPDrpc_answer_dropped()); break;
		default: throw mtpErrorBadTypeId(type, "MTPrpcDropAnswer");
	}
}
inline MTPrpcDropAnswer::MTPrpcDropAnswer(MTPDrpc_answer_dropped *_data) : mtpDataOwner(_data), _type(mtpc_rpc_answer_dropped) {
}
inline MTPrpcDropAnswer MTP_rpc_answer_unknown() {
	return MTPrpcDropAnswer(mtpc_rpc_answer_unknown);
}
inline MTPrpcDropAnswer MTP_rpc_answer_dropped_running() {
	return MTPrpcDropAnswer(mtpc_rpc_answer_dropped_running);
}
inline MTPrpcDropAnswer MTP_rpc_answer_dropped(const MTPlong &_msg_id, MTPint _seq_no, MTPint _bytes) {
	return MTPrpcDropAnswer(new MTPDrpc_answer_dropped(_msg_id, _seq_no, _bytes));
}

inline MTPfutureSalt::MTPfutureSalt() : mtpDataOwner(new MTPDfuture_salt()) {
}

inline uint32 MTPfutureSalt::size() const {
	const MTPDfuture_salt &v(c_future_salt());
	return v.vvalid_since.size() + v.vvalid_until.size() + v.vsalt.size();
}
inline mtpTypeId MTPfutureSalt::type() const {
	return mtpc_future_salt;
}
inline void MTPfutureSalt::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_future_salt) throw mtpErrorUnexpected(cons, "MTPfutureSalt");

	if (!data) setData(new MTPDfuture_salt());
	MTPDfuture_salt &v(_future_salt());
	v.vvalid_since.read(from, end);
	v.vvalid_until.read(from, end);
	v.vsalt.read(from, end);
}
inline void MTPfutureSalt::write(mtpBuffer &to) const {
	const MTPDfuture_salt &v(c_future_salt());
	v.vvalid_since.write(to);
	v.vvalid_until.write(to);
	v.vsalt.write(to);
}
inline MTPfutureSalt::MTPfutureSalt(MTPDfuture_salt *_data) : mtpDataOwner(_data) {
}
inline MTPfutureSalt MTP_future_salt(MTPint _valid_since, MTPint _valid_until, const MTPlong &_salt) {
	return MTPfutureSalt(new MTPDfuture_salt(_valid_since, _valid_until, _salt));
}

inline MTPfutureSalts::MTPfutureSalts() : mtpDataOwner(new MTPDfuture_salts()) {
}

inline uint32 MTPfutureSalts::size() const {
	const MTPDfuture_salts &v(c_future_salts());
	return v.vreq_msg_id.size() + v.vnow.size() + v.vsalts.size();
}
inline mtpTypeId MTPfutureSalts::type() const {
	return mtpc_future_salts;
}
inline void MTPfutureSalts::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_future_salts) throw mtpErrorUnexpected(cons, "MTPfutureSalts");

	if (!data) setData(new MTPDfuture_salts());
	MTPDfuture_salts &v(_future_salts());
	v.vreq_msg_id.read(from, end);
	v.vnow.read(from, end);
	v.vsalts.read(from, end);
}
inline void MTPfutureSalts::write(mtpBuffer &to) const {
	const MTPDfuture_salts &v(c_future_salts());
	v.vreq_msg_id.write(to);
	v.vnow.write(to);
	v.vsalts.write(to);
}
inline MTPfutureSalts::MTPfutureSalts(MTPDfuture_salts *_data) : mtpDataOwner(_data) {
}
inline MTPfutureSalts MTP_future_salts(const MTPlong &_req_msg_id, MTPint _now, const MTPvector<MTPfutureSalt> &_salts) {
	return MTPfutureSalts(new MTPDfuture_salts(_req_msg_id, _now, _salts));
}

inline MTPpong::MTPpong() : mtpDataOwner(new MTPDpong()) {
}

inline uint32 MTPpong::size() const {
	const MTPDpong &v(c_pong());
	return v.vmsg_id.size() + v.vping_id.size();
}
inline mtpTypeId MTPpong::type() const {
	return mtpc_pong;
}
inline void MTPpong::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_pong) throw mtpErrorUnexpected(cons, "MTPpong");

	if (!data) setData(new MTPDpong());
	MTPDpong &v(_pong());
	v.vmsg_id.read(from, end);
	v.vping_id.read(from, end);
}
inline void MTPpong::write(mtpBuffer &to) const {
	const MTPDpong &v(c_pong());
	v.vmsg_id.write(to);
	v.vping_id.write(to);
}
inline MTPpong::MTPpong(MTPDpong *_data) : mtpDataOwner(_data) {
}
inline MTPpong MTP_pong(const MTPlong &_msg_id, const MTPlong &_ping_id) {
	return MTPpong(new MTPDpong(_msg_id, _ping_id));
}

inline uint32 MTPdestroySessionRes::size() const {
	switch (_type) {
		case mtpc_destroy_session_ok: {
			const MTPDdestroy_session_ok &v(c_destroy_session_ok());
			return v.vsession_id.size();
		}
		case mtpc_destroy_session_none: {
			const MTPDdestroy_session_none &v(c_destroy_session_none());
			return v.vsession_id.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPdestroySessionRes::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPdestroySessionRes::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_destroy_session_ok: _type = cons; {
			if (!data) setData(new MTPDdestroy_session_ok());
			MTPDdestroy_session_ok &v(_destroy_session_ok());
			v.vsession_id.read(from, end);
		} break;
		case mtpc_destroy_session_none: _type = cons; {
			if (!data) setData(new MTPDdestroy_session_none());
			MTPDdestroy_session_none &v(_destroy_session_none());
			v.vsession_id.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPdestroySessionRes");
	}
}
inline void MTPdestroySessionRes::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_destroy_session_ok: {
			const MTPDdestroy_session_ok &v(c_destroy_session_ok());
			v.vsession_id.write(to);
		} break;
		case mtpc_destroy_session_none: {
			const MTPDdestroy_session_none &v(c_destroy_session_none());
			v.vsession_id.write(to);
		} break;
	}
}
inline MTPdestroySessionRes::MTPdestroySessionRes(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_destroy_session_ok: setData(new MTPDdestroy_session_ok()); break;
		case mtpc_destroy_session_none: setData(new MTPDdestroy_session_none()); break;
		default: throw mtpErrorBadTypeId(type, "MTPdestroySessionRes");
	}
}
inline MTPdestroySessionRes::MTPdestroySessionRes(MTPDdestroy_session_ok *_data) : mtpDataOwner(_data), _type(mtpc_destroy_session_ok) {
}
inline MTPdestroySessionRes::MTPdestroySessionRes(MTPDdestroy_session_none *_data) : mtpDataOwner(_data), _type(mtpc_destroy_session_none) {
}
inline MTPdestroySessionRes MTP_destroy_session_ok(const MTPlong &_session_id) {
	return MTPdestroySessionRes(new MTPDdestroy_session_ok(_session_id));
}
inline MTPdestroySessionRes MTP_destroy_session_none(const MTPlong &_session_id) {
	return MTPdestroySessionRes(new MTPDdestroy_session_none(_session_id));
}

inline MTPnewSession::MTPnewSession() : mtpDataOwner(new MTPDnew_session_created()) {
}

inline uint32 MTPnewSession::size() const {
	const MTPDnew_session_created &v(c_new_session_created());
	return v.vfirst_msg_id.size() + v.vunique_id.size() + v.vserver_salt.size();
}
inline mtpTypeId MTPnewSession::type() const {
	return mtpc_new_session_created;
}
inline void MTPnewSession::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_new_session_created) throw mtpErrorUnexpected(cons, "MTPnewSession");

	if (!data) setData(new MTPDnew_session_created());
	MTPDnew_session_created &v(_new_session_created());
	v.vfirst_msg_id.read(from, end);
	v.vunique_id.read(from, end);
	v.vserver_salt.read(from, end);
}
inline void MTPnewSession::write(mtpBuffer &to) const {
	const MTPDnew_session_created &v(c_new_session_created());
	v.vfirst_msg_id.write(to);
	v.vunique_id.write(to);
	v.vserver_salt.write(to);
}
inline MTPnewSession::MTPnewSession(MTPDnew_session_created *_data) : mtpDataOwner(_data) {
}
inline MTPnewSession MTP_new_session_created(const MTPlong &_first_msg_id, const MTPlong &_unique_id, const MTPlong &_server_salt) {
	return MTPnewSession(new MTPDnew_session_created(_first_msg_id, _unique_id, _server_salt));
}

inline MTPhttpWait::MTPhttpWait() : mtpDataOwner(new MTPDhttp_wait()) {
}

inline uint32 MTPhttpWait::size() const {
	const MTPDhttp_wait &v(c_http_wait());
	return v.vmax_delay.size() + v.vwait_after.size() + v.vmax_wait.size();
}
inline mtpTypeId MTPhttpWait::type() const {
	return mtpc_http_wait;
}
inline void MTPhttpWait::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_http_wait) throw mtpErrorUnexpected(cons, "MTPhttpWait");

	if (!data) setData(new MTPDhttp_wait());
	MTPDhttp_wait &v(_http_wait());
	v.vmax_delay.read(from, end);
	v.vwait_after.read(from, end);
	v.vmax_wait.read(from, end);
}
inline void MTPhttpWait::write(mtpBuffer &to) const {
	const MTPDhttp_wait &v(c_http_wait());
	v.vmax_delay.write(to);
	v.vwait_after.write(to);
	v.vmax_wait.write(to);
}
inline MTPhttpWait::MTPhttpWait(MTPDhttp_wait *_data) : mtpDataOwner(_data) {
}
inline MTPhttpWait MTP_http_wait(MTPint _max_delay, MTPint _wait_after, MTPint _max_wait) {
	return MTPhttpWait(new MTPDhttp_wait(_max_delay, _wait_after, _max_wait));
}

inline uint32 MTPinputPeer::size() const {
	switch (_type) {
		case mtpc_inputPeerContact: {
			const MTPDinputPeerContact &v(c_inputPeerContact());
			return v.vuser_id.size();
		}
		case mtpc_inputPeerForeign: {
			const MTPDinputPeerForeign &v(c_inputPeerForeign());
			return v.vuser_id.size() + v.vaccess_hash.size();
		}
		case mtpc_inputPeerChat: {
			const MTPDinputPeerChat &v(c_inputPeerChat());
			return v.vchat_id.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPinputPeer::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPinputPeer::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_inputPeerEmpty: _type = cons; break;
		case mtpc_inputPeerSelf: _type = cons; break;
		case mtpc_inputPeerContact: _type = cons; {
			if (!data) setData(new MTPDinputPeerContact());
			MTPDinputPeerContact &v(_inputPeerContact());
			v.vuser_id.read(from, end);
		} break;
		case mtpc_inputPeerForeign: _type = cons; {
			if (!data) setData(new MTPDinputPeerForeign());
			MTPDinputPeerForeign &v(_inputPeerForeign());
			v.vuser_id.read(from, end);
			v.vaccess_hash.read(from, end);
		} break;
		case mtpc_inputPeerChat: _type = cons; {
			if (!data) setData(new MTPDinputPeerChat());
			MTPDinputPeerChat &v(_inputPeerChat());
			v.vchat_id.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPinputPeer");
	}
}
inline void MTPinputPeer::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_inputPeerContact: {
			const MTPDinputPeerContact &v(c_inputPeerContact());
			v.vuser_id.write(to);
		} break;
		case mtpc_inputPeerForeign: {
			const MTPDinputPeerForeign &v(c_inputPeerForeign());
			v.vuser_id.write(to);
			v.vaccess_hash.write(to);
		} break;
		case mtpc_inputPeerChat: {
			const MTPDinputPeerChat &v(c_inputPeerChat());
			v.vchat_id.write(to);
		} break;
	}
}
inline MTPinputPeer::MTPinputPeer(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_inputPeerEmpty: break;
		case mtpc_inputPeerSelf: break;
		case mtpc_inputPeerContact: setData(new MTPDinputPeerContact()); break;
		case mtpc_inputPeerForeign: setData(new MTPDinputPeerForeign()); break;
		case mtpc_inputPeerChat: setData(new MTPDinputPeerChat()); break;
		default: throw mtpErrorBadTypeId(type, "MTPinputPeer");
	}
}
inline MTPinputPeer::MTPinputPeer(MTPDinputPeerContact *_data) : mtpDataOwner(_data), _type(mtpc_inputPeerContact) {
}
inline MTPinputPeer::MTPinputPeer(MTPDinputPeerForeign *_data) : mtpDataOwner(_data), _type(mtpc_inputPeerForeign) {
}
inline MTPinputPeer::MTPinputPeer(MTPDinputPeerChat *_data) : mtpDataOwner(_data), _type(mtpc_inputPeerChat) {
}
inline MTPinputPeer MTP_inputPeerEmpty() {
	return MTPinputPeer(mtpc_inputPeerEmpty);
}
inline MTPinputPeer MTP_inputPeerSelf() {
	return MTPinputPeer(mtpc_inputPeerSelf);
}
inline MTPinputPeer MTP_inputPeerContact(MTPint _user_id) {
	return MTPinputPeer(new MTPDinputPeerContact(_user_id));
}
inline MTPinputPeer MTP_inputPeerForeign(MTPint _user_id, const MTPlong &_access_hash) {
	return MTPinputPeer(new MTPDinputPeerForeign(_user_id, _access_hash));
}
inline MTPinputPeer MTP_inputPeerChat(MTPint _chat_id) {
	return MTPinputPeer(new MTPDinputPeerChat(_chat_id));
}

inline uint32 MTPinputUser::size() const {
	switch (_type) {
		case mtpc_inputUserContact: {
			const MTPDinputUserContact &v(c_inputUserContact());
			return v.vuser_id.size();
		}
		case mtpc_inputUserForeign: {
			const MTPDinputUserForeign &v(c_inputUserForeign());
			return v.vuser_id.size() + v.vaccess_hash.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPinputUser::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPinputUser::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_inputUserEmpty: _type = cons; break;
		case mtpc_inputUserSelf: _type = cons; break;
		case mtpc_inputUserContact: _type = cons; {
			if (!data) setData(new MTPDinputUserContact());
			MTPDinputUserContact &v(_inputUserContact());
			v.vuser_id.read(from, end);
		} break;
		case mtpc_inputUserForeign: _type = cons; {
			if (!data) setData(new MTPDinputUserForeign());
			MTPDinputUserForeign &v(_inputUserForeign());
			v.vuser_id.read(from, end);
			v.vaccess_hash.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPinputUser");
	}
}
inline void MTPinputUser::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_inputUserContact: {
			const MTPDinputUserContact &v(c_inputUserContact());
			v.vuser_id.write(to);
		} break;
		case mtpc_inputUserForeign: {
			const MTPDinputUserForeign &v(c_inputUserForeign());
			v.vuser_id.write(to);
			v.vaccess_hash.write(to);
		} break;
	}
}
inline MTPinputUser::MTPinputUser(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_inputUserEmpty: break;
		case mtpc_inputUserSelf: break;
		case mtpc_inputUserContact: setData(new MTPDinputUserContact()); break;
		case mtpc_inputUserForeign: setData(new MTPDinputUserForeign()); break;
		default: throw mtpErrorBadTypeId(type, "MTPinputUser");
	}
}
inline MTPinputUser::MTPinputUser(MTPDinputUserContact *_data) : mtpDataOwner(_data), _type(mtpc_inputUserContact) {
}
inline MTPinputUser::MTPinputUser(MTPDinputUserForeign *_data) : mtpDataOwner(_data), _type(mtpc_inputUserForeign) {
}
inline MTPinputUser MTP_inputUserEmpty() {
	return MTPinputUser(mtpc_inputUserEmpty);
}
inline MTPinputUser MTP_inputUserSelf() {
	return MTPinputUser(mtpc_inputUserSelf);
}
inline MTPinputUser MTP_inputUserContact(MTPint _user_id) {
	return MTPinputUser(new MTPDinputUserContact(_user_id));
}
inline MTPinputUser MTP_inputUserForeign(MTPint _user_id, const MTPlong &_access_hash) {
	return MTPinputUser(new MTPDinputUserForeign(_user_id, _access_hash));
}

inline MTPinputContact::MTPinputContact() : mtpDataOwner(new MTPDinputPhoneContact()) {
}

inline uint32 MTPinputContact::size() const {
	const MTPDinputPhoneContact &v(c_inputPhoneContact());
	return v.vclient_id.size() + v.vphone.size() + v.vfirst_name.size() + v.vlast_name.size();
}
inline mtpTypeId MTPinputContact::type() const {
	return mtpc_inputPhoneContact;
}
inline void MTPinputContact::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_inputPhoneContact) throw mtpErrorUnexpected(cons, "MTPinputContact");

	if (!data) setData(new MTPDinputPhoneContact());
	MTPDinputPhoneContact &v(_inputPhoneContact());
	v.vclient_id.read(from, end);
	v.vphone.read(from, end);
	v.vfirst_name.read(from, end);
	v.vlast_name.read(from, end);
}
inline void MTPinputContact::write(mtpBuffer &to) const {
	const MTPDinputPhoneContact &v(c_inputPhoneContact());
	v.vclient_id.write(to);
	v.vphone.write(to);
	v.vfirst_name.write(to);
	v.vlast_name.write(to);
}
inline MTPinputContact::MTPinputContact(MTPDinputPhoneContact *_data) : mtpDataOwner(_data) {
}
inline MTPinputContact MTP_inputPhoneContact(const MTPlong &_client_id, const MTPstring &_phone, const MTPstring &_first_name, const MTPstring &_last_name) {
	return MTPinputContact(new MTPDinputPhoneContact(_client_id, _phone, _first_name, _last_name));
}

inline uint32 MTPinputFile::size() const {
	switch (_type) {
		case mtpc_inputFile: {
			const MTPDinputFile &v(c_inputFile());
			return v.vid.size() + v.vparts.size() + v.vname.size() + v.vmd5_checksum.size();
		}
		case mtpc_inputFileBig: {
			const MTPDinputFileBig &v(c_inputFileBig());
			return v.vid.size() + v.vparts.size() + v.vname.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPinputFile::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPinputFile::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_inputFile: _type = cons; {
			if (!data) setData(new MTPDinputFile());
			MTPDinputFile &v(_inputFile());
			v.vid.read(from, end);
			v.vparts.read(from, end);
			v.vname.read(from, end);
			v.vmd5_checksum.read(from, end);
		} break;
		case mtpc_inputFileBig: _type = cons; {
			if (!data) setData(new MTPDinputFileBig());
			MTPDinputFileBig &v(_inputFileBig());
			v.vid.read(from, end);
			v.vparts.read(from, end);
			v.vname.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPinputFile");
	}
}
inline void MTPinputFile::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_inputFile: {
			const MTPDinputFile &v(c_inputFile());
			v.vid.write(to);
			v.vparts.write(to);
			v.vname.write(to);
			v.vmd5_checksum.write(to);
		} break;
		case mtpc_inputFileBig: {
			const MTPDinputFileBig &v(c_inputFileBig());
			v.vid.write(to);
			v.vparts.write(to);
			v.vname.write(to);
		} break;
	}
}
inline MTPinputFile::MTPinputFile(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_inputFile: setData(new MTPDinputFile()); break;
		case mtpc_inputFileBig: setData(new MTPDinputFileBig()); break;
		default: throw mtpErrorBadTypeId(type, "MTPinputFile");
	}
}
inline MTPinputFile::MTPinputFile(MTPDinputFile *_data) : mtpDataOwner(_data), _type(mtpc_inputFile) {
}
inline MTPinputFile::MTPinputFile(MTPDinputFileBig *_data) : mtpDataOwner(_data), _type(mtpc_inputFileBig) {
}
inline MTPinputFile MTP_inputFile(const MTPlong &_id, MTPint _parts, const MTPstring &_name, const MTPstring &_md5_checksum) {
	return MTPinputFile(new MTPDinputFile(_id, _parts, _name, _md5_checksum));
}
inline MTPinputFile MTP_inputFileBig(const MTPlong &_id, MTPint _parts, const MTPstring &_name) {
	return MTPinputFile(new MTPDinputFileBig(_id, _parts, _name));
}

inline uint32 MTPinputMedia::size() const {
	switch (_type) {
		case mtpc_inputMediaUploadedPhoto: {
			const MTPDinputMediaUploadedPhoto &v(c_inputMediaUploadedPhoto());
			return v.vfile.size();
		}
		case mtpc_inputMediaPhoto: {
			const MTPDinputMediaPhoto &v(c_inputMediaPhoto());
			return v.vid.size();
		}
		case mtpc_inputMediaGeoPoint: {
			const MTPDinputMediaGeoPoint &v(c_inputMediaGeoPoint());
			return v.vgeo_point.size();
		}
		case mtpc_inputMediaContact: {
			const MTPDinputMediaContact &v(c_inputMediaContact());
			return v.vphone_number.size() + v.vfirst_name.size() + v.vlast_name.size();
		}
		case mtpc_inputMediaUploadedVideo: {
			const MTPDinputMediaUploadedVideo &v(c_inputMediaUploadedVideo());
			return v.vfile.size() + v.vduration.size() + v.vw.size() + v.vh.size() + v.vmime_type.size();
		}
		case mtpc_inputMediaUploadedThumbVideo: {
			const MTPDinputMediaUploadedThumbVideo &v(c_inputMediaUploadedThumbVideo());
			return v.vfile.size() + v.vthumb.size() + v.vduration.size() + v.vw.size() + v.vh.size() + v.vmime_type.size();
		}
		case mtpc_inputMediaVideo: {
			const MTPDinputMediaVideo &v(c_inputMediaVideo());
			return v.vid.size();
		}
		case mtpc_inputMediaUploadedAudio: {
			const MTPDinputMediaUploadedAudio &v(c_inputMediaUploadedAudio());
			return v.vfile.size() + v.vduration.size() + v.vmime_type.size();
		}
		case mtpc_inputMediaAudio: {
			const MTPDinputMediaAudio &v(c_inputMediaAudio());
			return v.vid.size();
		}
		case mtpc_inputMediaUploadedDocument: {
			const MTPDinputMediaUploadedDocument &v(c_inputMediaUploadedDocument());
			return v.vfile.size() + v.vfile_name.size() + v.vmime_type.size();
		}
		case mtpc_inputMediaUploadedThumbDocument: {
			const MTPDinputMediaUploadedThumbDocument &v(c_inputMediaUploadedThumbDocument());
			return v.vfile.size() + v.vthumb.size() + v.vfile_name.size() + v.vmime_type.size();
		}
		case mtpc_inputMediaDocument: {
			const MTPDinputMediaDocument &v(c_inputMediaDocument());
			return v.vid.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPinputMedia::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPinputMedia::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_inputMediaEmpty: _type = cons; break;
		case mtpc_inputMediaUploadedPhoto: _type = cons; {
			if (!data) setData(new MTPDinputMediaUploadedPhoto());
			MTPDinputMediaUploadedPhoto &v(_inputMediaUploadedPhoto());
			v.vfile.read(from, end);
		} break;
		case mtpc_inputMediaPhoto: _type = cons; {
			if (!data) setData(new MTPDinputMediaPhoto());
			MTPDinputMediaPhoto &v(_inputMediaPhoto());
			v.vid.read(from, end);
		} break;
		case mtpc_inputMediaGeoPoint: _type = cons; {
			if (!data) setData(new MTPDinputMediaGeoPoint());
			MTPDinputMediaGeoPoint &v(_inputMediaGeoPoint());
			v.vgeo_point.read(from, end);
		} break;
		case mtpc_inputMediaContact: _type = cons; {
			if (!data) setData(new MTPDinputMediaContact());
			MTPDinputMediaContact &v(_inputMediaContact());
			v.vphone_number.read(from, end);
			v.vfirst_name.read(from, end);
			v.vlast_name.read(from, end);
		} break;
		case mtpc_inputMediaUploadedVideo: _type = cons; {
			if (!data) setData(new MTPDinputMediaUploadedVideo());
			MTPDinputMediaUploadedVideo &v(_inputMediaUploadedVideo());
			v.vfile.read(from, end);
			v.vduration.read(from, end);
			v.vw.read(from, end);
			v.vh.read(from, end);
			v.vmime_type.read(from, end);
		} break;
		case mtpc_inputMediaUploadedThumbVideo: _type = cons; {
			if (!data) setData(new MTPDinputMediaUploadedThumbVideo());
			MTPDinputMediaUploadedThumbVideo &v(_inputMediaUploadedThumbVideo());
			v.vfile.read(from, end);
			v.vthumb.read(from, end);
			v.vduration.read(from, end);
			v.vw.read(from, end);
			v.vh.read(from, end);
			v.vmime_type.read(from, end);
		} break;
		case mtpc_inputMediaVideo: _type = cons; {
			if (!data) setData(new MTPDinputMediaVideo());
			MTPDinputMediaVideo &v(_inputMediaVideo());
			v.vid.read(from, end);
		} break;
		case mtpc_inputMediaUploadedAudio: _type = cons; {
			if (!data) setData(new MTPDinputMediaUploadedAudio());
			MTPDinputMediaUploadedAudio &v(_inputMediaUploadedAudio());
			v.vfile.read(from, end);
			v.vduration.read(from, end);
			v.vmime_type.read(from, end);
		} break;
		case mtpc_inputMediaAudio: _type = cons; {
			if (!data) setData(new MTPDinputMediaAudio());
			MTPDinputMediaAudio &v(_inputMediaAudio());
			v.vid.read(from, end);
		} break;
		case mtpc_inputMediaUploadedDocument: _type = cons; {
			if (!data) setData(new MTPDinputMediaUploadedDocument());
			MTPDinputMediaUploadedDocument &v(_inputMediaUploadedDocument());
			v.vfile.read(from, end);
			v.vfile_name.read(from, end);
			v.vmime_type.read(from, end);
		} break;
		case mtpc_inputMediaUploadedThumbDocument: _type = cons; {
			if (!data) setData(new MTPDinputMediaUploadedThumbDocument());
			MTPDinputMediaUploadedThumbDocument &v(_inputMediaUploadedThumbDocument());
			v.vfile.read(from, end);
			v.vthumb.read(from, end);
			v.vfile_name.read(from, end);
			v.vmime_type.read(from, end);
		} break;
		case mtpc_inputMediaDocument: _type = cons; {
			if (!data) setData(new MTPDinputMediaDocument());
			MTPDinputMediaDocument &v(_inputMediaDocument());
			v.vid.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPinputMedia");
	}
}
inline void MTPinputMedia::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_inputMediaUploadedPhoto: {
			const MTPDinputMediaUploadedPhoto &v(c_inputMediaUploadedPhoto());
			v.vfile.write(to);
		} break;
		case mtpc_inputMediaPhoto: {
			const MTPDinputMediaPhoto &v(c_inputMediaPhoto());
			v.vid.write(to);
		} break;
		case mtpc_inputMediaGeoPoint: {
			const MTPDinputMediaGeoPoint &v(c_inputMediaGeoPoint());
			v.vgeo_point.write(to);
		} break;
		case mtpc_inputMediaContact: {
			const MTPDinputMediaContact &v(c_inputMediaContact());
			v.vphone_number.write(to);
			v.vfirst_name.write(to);
			v.vlast_name.write(to);
		} break;
		case mtpc_inputMediaUploadedVideo: {
			const MTPDinputMediaUploadedVideo &v(c_inputMediaUploadedVideo());
			v.vfile.write(to);
			v.vduration.write(to);
			v.vw.write(to);
			v.vh.write(to);
			v.vmime_type.write(to);
		} break;
		case mtpc_inputMediaUploadedThumbVideo: {
			const MTPDinputMediaUploadedThumbVideo &v(c_inputMediaUploadedThumbVideo());
			v.vfile.write(to);
			v.vthumb.write(to);
			v.vduration.write(to);
			v.vw.write(to);
			v.vh.write(to);
			v.vmime_type.write(to);
		} break;
		case mtpc_inputMediaVideo: {
			const MTPDinputMediaVideo &v(c_inputMediaVideo());
			v.vid.write(to);
		} break;
		case mtpc_inputMediaUploadedAudio: {
			const MTPDinputMediaUploadedAudio &v(c_inputMediaUploadedAudio());
			v.vfile.write(to);
			v.vduration.write(to);
			v.vmime_type.write(to);
		} break;
		case mtpc_inputMediaAudio: {
			const MTPDinputMediaAudio &v(c_inputMediaAudio());
			v.vid.write(to);
		} break;
		case mtpc_inputMediaUploadedDocument: {
			const MTPDinputMediaUploadedDocument &v(c_inputMediaUploadedDocument());
			v.vfile.write(to);
			v.vfile_name.write(to);
			v.vmime_type.write(to);
		} break;
		case mtpc_inputMediaUploadedThumbDocument: {
			const MTPDinputMediaUploadedThumbDocument &v(c_inputMediaUploadedThumbDocument());
			v.vfile.write(to);
			v.vthumb.write(to);
			v.vfile_name.write(to);
			v.vmime_type.write(to);
		} break;
		case mtpc_inputMediaDocument: {
			const MTPDinputMediaDocument &v(c_inputMediaDocument());
			v.vid.write(to);
		} break;
	}
}
inline MTPinputMedia::MTPinputMedia(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_inputMediaEmpty: break;
		case mtpc_inputMediaUploadedPhoto: setData(new MTPDinputMediaUploadedPhoto()); break;
		case mtpc_inputMediaPhoto: setData(new MTPDinputMediaPhoto()); break;
		case mtpc_inputMediaGeoPoint: setData(new MTPDinputMediaGeoPoint()); break;
		case mtpc_inputMediaContact: setData(new MTPDinputMediaContact()); break;
		case mtpc_inputMediaUploadedVideo: setData(new MTPDinputMediaUploadedVideo()); break;
		case mtpc_inputMediaUploadedThumbVideo: setData(new MTPDinputMediaUploadedThumbVideo()); break;
		case mtpc_inputMediaVideo: setData(new MTPDinputMediaVideo()); break;
		case mtpc_inputMediaUploadedAudio: setData(new MTPDinputMediaUploadedAudio()); break;
		case mtpc_inputMediaAudio: setData(new MTPDinputMediaAudio()); break;
		case mtpc_inputMediaUploadedDocument: setData(new MTPDinputMediaUploadedDocument()); break;
		case mtpc_inputMediaUploadedThumbDocument: setData(new MTPDinputMediaUploadedThumbDocument()); break;
		case mtpc_inputMediaDocument: setData(new MTPDinputMediaDocument()); break;
		default: throw mtpErrorBadTypeId(type, "MTPinputMedia");
	}
}
inline MTPinputMedia::MTPinputMedia(MTPDinputMediaUploadedPhoto *_data) : mtpDataOwner(_data), _type(mtpc_inputMediaUploadedPhoto) {
}
inline MTPinputMedia::MTPinputMedia(MTPDinputMediaPhoto *_data) : mtpDataOwner(_data), _type(mtpc_inputMediaPhoto) {
}
inline MTPinputMedia::MTPinputMedia(MTPDinputMediaGeoPoint *_data) : mtpDataOwner(_data), _type(mtpc_inputMediaGeoPoint) {
}
inline MTPinputMedia::MTPinputMedia(MTPDinputMediaContact *_data) : mtpDataOwner(_data), _type(mtpc_inputMediaContact) {
}
inline MTPinputMedia::MTPinputMedia(MTPDinputMediaUploadedVideo *_data) : mtpDataOwner(_data), _type(mtpc_inputMediaUploadedVideo) {
}
inline MTPinputMedia::MTPinputMedia(MTPDinputMediaUploadedThumbVideo *_data) : mtpDataOwner(_data), _type(mtpc_inputMediaUploadedThumbVideo) {
}
inline MTPinputMedia::MTPinputMedia(MTPDinputMediaVideo *_data) : mtpDataOwner(_data), _type(mtpc_inputMediaVideo) {
}
inline MTPinputMedia::MTPinputMedia(MTPDinputMediaUploadedAudio *_data) : mtpDataOwner(_data), _type(mtpc_inputMediaUploadedAudio) {
}
inline MTPinputMedia::MTPinputMedia(MTPDinputMediaAudio *_data) : mtpDataOwner(_data), _type(mtpc_inputMediaAudio) {
}
inline MTPinputMedia::MTPinputMedia(MTPDinputMediaUploadedDocument *_data) : mtpDataOwner(_data), _type(mtpc_inputMediaUploadedDocument) {
}
inline MTPinputMedia::MTPinputMedia(MTPDinputMediaUploadedThumbDocument *_data) : mtpDataOwner(_data), _type(mtpc_inputMediaUploadedThumbDocument) {
}
inline MTPinputMedia::MTPinputMedia(MTPDinputMediaDocument *_data) : mtpDataOwner(_data), _type(mtpc_inputMediaDocument) {
}
inline MTPinputMedia MTP_inputMediaEmpty() {
	return MTPinputMedia(mtpc_inputMediaEmpty);
}
inline MTPinputMedia MTP_inputMediaUploadedPhoto(const MTPInputFile &_file) {
	return MTPinputMedia(new MTPDinputMediaUploadedPhoto(_file));
}
inline MTPinputMedia MTP_inputMediaPhoto(const MTPInputPhoto &_id) {
	return MTPinputMedia(new MTPDinputMediaPhoto(_id));
}
inline MTPinputMedia MTP_inputMediaGeoPoint(const MTPInputGeoPoint &_geo_point) {
	return MTPinputMedia(new MTPDinputMediaGeoPoint(_geo_point));
}
inline MTPinputMedia MTP_inputMediaContact(const MTPstring &_phone_number, const MTPstring &_first_name, const MTPstring &_last_name) {
	return MTPinputMedia(new MTPDinputMediaContact(_phone_number, _first_name, _last_name));
}
inline MTPinputMedia MTP_inputMediaUploadedVideo(const MTPInputFile &_file, MTPint _duration, MTPint _w, MTPint _h, const MTPstring &_mime_type) {
	return MTPinputMedia(new MTPDinputMediaUploadedVideo(_file, _duration, _w, _h, _mime_type));
}
inline MTPinputMedia MTP_inputMediaUploadedThumbVideo(const MTPInputFile &_file, const MTPInputFile &_thumb, MTPint _duration, MTPint _w, MTPint _h, const MTPstring &_mime_type) {
	return MTPinputMedia(new MTPDinputMediaUploadedThumbVideo(_file, _thumb, _duration, _w, _h, _mime_type));
}
inline MTPinputMedia MTP_inputMediaVideo(const MTPInputVideo &_id) {
	return MTPinputMedia(new MTPDinputMediaVideo(_id));
}
inline MTPinputMedia MTP_inputMediaUploadedAudio(const MTPInputFile &_file, MTPint _duration, const MTPstring &_mime_type) {
	return MTPinputMedia(new MTPDinputMediaUploadedAudio(_file, _duration, _mime_type));
}
inline MTPinputMedia MTP_inputMediaAudio(const MTPInputAudio &_id) {
	return MTPinputMedia(new MTPDinputMediaAudio(_id));
}
inline MTPinputMedia MTP_inputMediaUploadedDocument(const MTPInputFile &_file, const MTPstring &_file_name, const MTPstring &_mime_type) {
	return MTPinputMedia(new MTPDinputMediaUploadedDocument(_file, _file_name, _mime_type));
}
inline MTPinputMedia MTP_inputMediaUploadedThumbDocument(const MTPInputFile &_file, const MTPInputFile &_thumb, const MTPstring &_file_name, const MTPstring &_mime_type) {
	return MTPinputMedia(new MTPDinputMediaUploadedThumbDocument(_file, _thumb, _file_name, _mime_type));
}
inline MTPinputMedia MTP_inputMediaDocument(const MTPInputDocument &_id) {
	return MTPinputMedia(new MTPDinputMediaDocument(_id));
}

inline uint32 MTPinputChatPhoto::size() const {
	switch (_type) {
		case mtpc_inputChatUploadedPhoto: {
			const MTPDinputChatUploadedPhoto &v(c_inputChatUploadedPhoto());
			return v.vfile.size() + v.vcrop.size();
		}
		case mtpc_inputChatPhoto: {
			const MTPDinputChatPhoto &v(c_inputChatPhoto());
			return v.vid.size() + v.vcrop.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPinputChatPhoto::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPinputChatPhoto::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_inputChatPhotoEmpty: _type = cons; break;
		case mtpc_inputChatUploadedPhoto: _type = cons; {
			if (!data) setData(new MTPDinputChatUploadedPhoto());
			MTPDinputChatUploadedPhoto &v(_inputChatUploadedPhoto());
			v.vfile.read(from, end);
			v.vcrop.read(from, end);
		} break;
		case mtpc_inputChatPhoto: _type = cons; {
			if (!data) setData(new MTPDinputChatPhoto());
			MTPDinputChatPhoto &v(_inputChatPhoto());
			v.vid.read(from, end);
			v.vcrop.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPinputChatPhoto");
	}
}
inline void MTPinputChatPhoto::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_inputChatUploadedPhoto: {
			const MTPDinputChatUploadedPhoto &v(c_inputChatUploadedPhoto());
			v.vfile.write(to);
			v.vcrop.write(to);
		} break;
		case mtpc_inputChatPhoto: {
			const MTPDinputChatPhoto &v(c_inputChatPhoto());
			v.vid.write(to);
			v.vcrop.write(to);
		} break;
	}
}
inline MTPinputChatPhoto::MTPinputChatPhoto(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_inputChatPhotoEmpty: break;
		case mtpc_inputChatUploadedPhoto: setData(new MTPDinputChatUploadedPhoto()); break;
		case mtpc_inputChatPhoto: setData(new MTPDinputChatPhoto()); break;
		default: throw mtpErrorBadTypeId(type, "MTPinputChatPhoto");
	}
}
inline MTPinputChatPhoto::MTPinputChatPhoto(MTPDinputChatUploadedPhoto *_data) : mtpDataOwner(_data), _type(mtpc_inputChatUploadedPhoto) {
}
inline MTPinputChatPhoto::MTPinputChatPhoto(MTPDinputChatPhoto *_data) : mtpDataOwner(_data), _type(mtpc_inputChatPhoto) {
}
inline MTPinputChatPhoto MTP_inputChatPhotoEmpty() {
	return MTPinputChatPhoto(mtpc_inputChatPhotoEmpty);
}
inline MTPinputChatPhoto MTP_inputChatUploadedPhoto(const MTPInputFile &_file, const MTPInputPhotoCrop &_crop) {
	return MTPinputChatPhoto(new MTPDinputChatUploadedPhoto(_file, _crop));
}
inline MTPinputChatPhoto MTP_inputChatPhoto(const MTPInputPhoto &_id, const MTPInputPhotoCrop &_crop) {
	return MTPinputChatPhoto(new MTPDinputChatPhoto(_id, _crop));
}

inline uint32 MTPinputGeoPoint::size() const {
	switch (_type) {
		case mtpc_inputGeoPoint: {
			const MTPDinputGeoPoint &v(c_inputGeoPoint());
			return v.vlat.size() + v.vlong.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPinputGeoPoint::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPinputGeoPoint::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_inputGeoPointEmpty: _type = cons; break;
		case mtpc_inputGeoPoint: _type = cons; {
			if (!data) setData(new MTPDinputGeoPoint());
			MTPDinputGeoPoint &v(_inputGeoPoint());
			v.vlat.read(from, end);
			v.vlong.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPinputGeoPoint");
	}
}
inline void MTPinputGeoPoint::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_inputGeoPoint: {
			const MTPDinputGeoPoint &v(c_inputGeoPoint());
			v.vlat.write(to);
			v.vlong.write(to);
		} break;
	}
}
inline MTPinputGeoPoint::MTPinputGeoPoint(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_inputGeoPointEmpty: break;
		case mtpc_inputGeoPoint: setData(new MTPDinputGeoPoint()); break;
		default: throw mtpErrorBadTypeId(type, "MTPinputGeoPoint");
	}
}
inline MTPinputGeoPoint::MTPinputGeoPoint(MTPDinputGeoPoint *_data) : mtpDataOwner(_data), _type(mtpc_inputGeoPoint) {
}
inline MTPinputGeoPoint MTP_inputGeoPointEmpty() {
	return MTPinputGeoPoint(mtpc_inputGeoPointEmpty);
}
inline MTPinputGeoPoint MTP_inputGeoPoint(const MTPdouble &_lat, const MTPdouble &_long) {
	return MTPinputGeoPoint(new MTPDinputGeoPoint(_lat, _long));
}

inline uint32 MTPinputPhoto::size() const {
	switch (_type) {
		case mtpc_inputPhoto: {
			const MTPDinputPhoto &v(c_inputPhoto());
			return v.vid.size() + v.vaccess_hash.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPinputPhoto::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPinputPhoto::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_inputPhotoEmpty: _type = cons; break;
		case mtpc_inputPhoto: _type = cons; {
			if (!data) setData(new MTPDinputPhoto());
			MTPDinputPhoto &v(_inputPhoto());
			v.vid.read(from, end);
			v.vaccess_hash.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPinputPhoto");
	}
}
inline void MTPinputPhoto::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_inputPhoto: {
			const MTPDinputPhoto &v(c_inputPhoto());
			v.vid.write(to);
			v.vaccess_hash.write(to);
		} break;
	}
}
inline MTPinputPhoto::MTPinputPhoto(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_inputPhotoEmpty: break;
		case mtpc_inputPhoto: setData(new MTPDinputPhoto()); break;
		default: throw mtpErrorBadTypeId(type, "MTPinputPhoto");
	}
}
inline MTPinputPhoto::MTPinputPhoto(MTPDinputPhoto *_data) : mtpDataOwner(_data), _type(mtpc_inputPhoto) {
}
inline MTPinputPhoto MTP_inputPhotoEmpty() {
	return MTPinputPhoto(mtpc_inputPhotoEmpty);
}
inline MTPinputPhoto MTP_inputPhoto(const MTPlong &_id, const MTPlong &_access_hash) {
	return MTPinputPhoto(new MTPDinputPhoto(_id, _access_hash));
}

inline uint32 MTPinputVideo::size() const {
	switch (_type) {
		case mtpc_inputVideo: {
			const MTPDinputVideo &v(c_inputVideo());
			return v.vid.size() + v.vaccess_hash.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPinputVideo::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPinputVideo::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_inputVideoEmpty: _type = cons; break;
		case mtpc_inputVideo: _type = cons; {
			if (!data) setData(new MTPDinputVideo());
			MTPDinputVideo &v(_inputVideo());
			v.vid.read(from, end);
			v.vaccess_hash.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPinputVideo");
	}
}
inline void MTPinputVideo::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_inputVideo: {
			const MTPDinputVideo &v(c_inputVideo());
			v.vid.write(to);
			v.vaccess_hash.write(to);
		} break;
	}
}
inline MTPinputVideo::MTPinputVideo(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_inputVideoEmpty: break;
		case mtpc_inputVideo: setData(new MTPDinputVideo()); break;
		default: throw mtpErrorBadTypeId(type, "MTPinputVideo");
	}
}
inline MTPinputVideo::MTPinputVideo(MTPDinputVideo *_data) : mtpDataOwner(_data), _type(mtpc_inputVideo) {
}
inline MTPinputVideo MTP_inputVideoEmpty() {
	return MTPinputVideo(mtpc_inputVideoEmpty);
}
inline MTPinputVideo MTP_inputVideo(const MTPlong &_id, const MTPlong &_access_hash) {
	return MTPinputVideo(new MTPDinputVideo(_id, _access_hash));
}

inline uint32 MTPinputFileLocation::size() const {
	switch (_type) {
		case mtpc_inputFileLocation: {
			const MTPDinputFileLocation &v(c_inputFileLocation());
			return v.vvolume_id.size() + v.vlocal_id.size() + v.vsecret.size();
		}
		case mtpc_inputVideoFileLocation: {
			const MTPDinputVideoFileLocation &v(c_inputVideoFileLocation());
			return v.vid.size() + v.vaccess_hash.size();
		}
		case mtpc_inputEncryptedFileLocation: {
			const MTPDinputEncryptedFileLocation &v(c_inputEncryptedFileLocation());
			return v.vid.size() + v.vaccess_hash.size();
		}
		case mtpc_inputAudioFileLocation: {
			const MTPDinputAudioFileLocation &v(c_inputAudioFileLocation());
			return v.vid.size() + v.vaccess_hash.size();
		}
		case mtpc_inputDocumentFileLocation: {
			const MTPDinputDocumentFileLocation &v(c_inputDocumentFileLocation());
			return v.vid.size() + v.vaccess_hash.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPinputFileLocation::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPinputFileLocation::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_inputFileLocation: _type = cons; {
			if (!data) setData(new MTPDinputFileLocation());
			MTPDinputFileLocation &v(_inputFileLocation());
			v.vvolume_id.read(from, end);
			v.vlocal_id.read(from, end);
			v.vsecret.read(from, end);
		} break;
		case mtpc_inputVideoFileLocation: _type = cons; {
			if (!data) setData(new MTPDinputVideoFileLocation());
			MTPDinputVideoFileLocation &v(_inputVideoFileLocation());
			v.vid.read(from, end);
			v.vaccess_hash.read(from, end);
		} break;
		case mtpc_inputEncryptedFileLocation: _type = cons; {
			if (!data) setData(new MTPDinputEncryptedFileLocation());
			MTPDinputEncryptedFileLocation &v(_inputEncryptedFileLocation());
			v.vid.read(from, end);
			v.vaccess_hash.read(from, end);
		} break;
		case mtpc_inputAudioFileLocation: _type = cons; {
			if (!data) setData(new MTPDinputAudioFileLocation());
			MTPDinputAudioFileLocation &v(_inputAudioFileLocation());
			v.vid.read(from, end);
			v.vaccess_hash.read(from, end);
		} break;
		case mtpc_inputDocumentFileLocation: _type = cons; {
			if (!data) setData(new MTPDinputDocumentFileLocation());
			MTPDinputDocumentFileLocation &v(_inputDocumentFileLocation());
			v.vid.read(from, end);
			v.vaccess_hash.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPinputFileLocation");
	}
}
inline void MTPinputFileLocation::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_inputFileLocation: {
			const MTPDinputFileLocation &v(c_inputFileLocation());
			v.vvolume_id.write(to);
			v.vlocal_id.write(to);
			v.vsecret.write(to);
		} break;
		case mtpc_inputVideoFileLocation: {
			const MTPDinputVideoFileLocation &v(c_inputVideoFileLocation());
			v.vid.write(to);
			v.vaccess_hash.write(to);
		} break;
		case mtpc_inputEncryptedFileLocation: {
			const MTPDinputEncryptedFileLocation &v(c_inputEncryptedFileLocation());
			v.vid.write(to);
			v.vaccess_hash.write(to);
		} break;
		case mtpc_inputAudioFileLocation: {
			const MTPDinputAudioFileLocation &v(c_inputAudioFileLocation());
			v.vid.write(to);
			v.vaccess_hash.write(to);
		} break;
		case mtpc_inputDocumentFileLocation: {
			const MTPDinputDocumentFileLocation &v(c_inputDocumentFileLocation());
			v.vid.write(to);
			v.vaccess_hash.write(to);
		} break;
	}
}
inline MTPinputFileLocation::MTPinputFileLocation(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_inputFileLocation: setData(new MTPDinputFileLocation()); break;
		case mtpc_inputVideoFileLocation: setData(new MTPDinputVideoFileLocation()); break;
		case mtpc_inputEncryptedFileLocation: setData(new MTPDinputEncryptedFileLocation()); break;
		case mtpc_inputAudioFileLocation: setData(new MTPDinputAudioFileLocation()); break;
		case mtpc_inputDocumentFileLocation: setData(new MTPDinputDocumentFileLocation()); break;
		default: throw mtpErrorBadTypeId(type, "MTPinputFileLocation");
	}
}
inline MTPinputFileLocation::MTPinputFileLocation(MTPDinputFileLocation *_data) : mtpDataOwner(_data), _type(mtpc_inputFileLocation) {
}
inline MTPinputFileLocation::MTPinputFileLocation(MTPDinputVideoFileLocation *_data) : mtpDataOwner(_data), _type(mtpc_inputVideoFileLocation) {
}
inline MTPinputFileLocation::MTPinputFileLocation(MTPDinputEncryptedFileLocation *_data) : mtpDataOwner(_data), _type(mtpc_inputEncryptedFileLocation) {
}
inline MTPinputFileLocation::MTPinputFileLocation(MTPDinputAudioFileLocation *_data) : mtpDataOwner(_data), _type(mtpc_inputAudioFileLocation) {
}
inline MTPinputFileLocation::MTPinputFileLocation(MTPDinputDocumentFileLocation *_data) : mtpDataOwner(_data), _type(mtpc_inputDocumentFileLocation) {
}
inline MTPinputFileLocation MTP_inputFileLocation(const MTPlong &_volume_id, MTPint _local_id, const MTPlong &_secret) {
	return MTPinputFileLocation(new MTPDinputFileLocation(_volume_id, _local_id, _secret));
}
inline MTPinputFileLocation MTP_inputVideoFileLocation(const MTPlong &_id, const MTPlong &_access_hash) {
	return MTPinputFileLocation(new MTPDinputVideoFileLocation(_id, _access_hash));
}
inline MTPinputFileLocation MTP_inputEncryptedFileLocation(const MTPlong &_id, const MTPlong &_access_hash) {
	return MTPinputFileLocation(new MTPDinputEncryptedFileLocation(_id, _access_hash));
}
inline MTPinputFileLocation MTP_inputAudioFileLocation(const MTPlong &_id, const MTPlong &_access_hash) {
	return MTPinputFileLocation(new MTPDinputAudioFileLocation(_id, _access_hash));
}
inline MTPinputFileLocation MTP_inputDocumentFileLocation(const MTPlong &_id, const MTPlong &_access_hash) {
	return MTPinputFileLocation(new MTPDinputDocumentFileLocation(_id, _access_hash));
}

inline uint32 MTPinputPhotoCrop::size() const {
	switch (_type) {
		case mtpc_inputPhotoCrop: {
			const MTPDinputPhotoCrop &v(c_inputPhotoCrop());
			return v.vcrop_left.size() + v.vcrop_top.size() + v.vcrop_width.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPinputPhotoCrop::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPinputPhotoCrop::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_inputPhotoCropAuto: _type = cons; break;
		case mtpc_inputPhotoCrop: _type = cons; {
			if (!data) setData(new MTPDinputPhotoCrop());
			MTPDinputPhotoCrop &v(_inputPhotoCrop());
			v.vcrop_left.read(from, end);
			v.vcrop_top.read(from, end);
			v.vcrop_width.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPinputPhotoCrop");
	}
}
inline void MTPinputPhotoCrop::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_inputPhotoCrop: {
			const MTPDinputPhotoCrop &v(c_inputPhotoCrop());
			v.vcrop_left.write(to);
			v.vcrop_top.write(to);
			v.vcrop_width.write(to);
		} break;
	}
}
inline MTPinputPhotoCrop::MTPinputPhotoCrop(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_inputPhotoCropAuto: break;
		case mtpc_inputPhotoCrop: setData(new MTPDinputPhotoCrop()); break;
		default: throw mtpErrorBadTypeId(type, "MTPinputPhotoCrop");
	}
}
inline MTPinputPhotoCrop::MTPinputPhotoCrop(MTPDinputPhotoCrop *_data) : mtpDataOwner(_data), _type(mtpc_inputPhotoCrop) {
}
inline MTPinputPhotoCrop MTP_inputPhotoCropAuto() {
	return MTPinputPhotoCrop(mtpc_inputPhotoCropAuto);
}
inline MTPinputPhotoCrop MTP_inputPhotoCrop(const MTPdouble &_crop_left, const MTPdouble &_crop_top, const MTPdouble &_crop_width) {
	return MTPinputPhotoCrop(new MTPDinputPhotoCrop(_crop_left, _crop_top, _crop_width));
}

inline MTPinputAppEvent::MTPinputAppEvent() : mtpDataOwner(new MTPDinputAppEvent()) {
}

inline uint32 MTPinputAppEvent::size() const {
	const MTPDinputAppEvent &v(c_inputAppEvent());
	return v.vtime.size() + v.vtype.size() + v.vpeer.size() + v.vdata.size();
}
inline mtpTypeId MTPinputAppEvent::type() const {
	return mtpc_inputAppEvent;
}
inline void MTPinputAppEvent::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_inputAppEvent) throw mtpErrorUnexpected(cons, "MTPinputAppEvent");

	if (!data) setData(new MTPDinputAppEvent());
	MTPDinputAppEvent &v(_inputAppEvent());
	v.vtime.read(from, end);
	v.vtype.read(from, end);
	v.vpeer.read(from, end);
	v.vdata.read(from, end);
}
inline void MTPinputAppEvent::write(mtpBuffer &to) const {
	const MTPDinputAppEvent &v(c_inputAppEvent());
	v.vtime.write(to);
	v.vtype.write(to);
	v.vpeer.write(to);
	v.vdata.write(to);
}
inline MTPinputAppEvent::MTPinputAppEvent(MTPDinputAppEvent *_data) : mtpDataOwner(_data) {
}
inline MTPinputAppEvent MTP_inputAppEvent(const MTPdouble &_time, const MTPstring &_type, const MTPlong &_peer, const MTPstring &_data) {
	return MTPinputAppEvent(new MTPDinputAppEvent(_time, _type, _peer, _data));
}

inline uint32 MTPpeer::size() const {
	switch (_type) {
		case mtpc_peerUser: {
			const MTPDpeerUser &v(c_peerUser());
			return v.vuser_id.size();
		}
		case mtpc_peerChat: {
			const MTPDpeerChat &v(c_peerChat());
			return v.vchat_id.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPpeer::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPpeer::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_peerUser: _type = cons; {
			if (!data) setData(new MTPDpeerUser());
			MTPDpeerUser &v(_peerUser());
			v.vuser_id.read(from, end);
		} break;
		case mtpc_peerChat: _type = cons; {
			if (!data) setData(new MTPDpeerChat());
			MTPDpeerChat &v(_peerChat());
			v.vchat_id.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPpeer");
	}
}
inline void MTPpeer::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_peerUser: {
			const MTPDpeerUser &v(c_peerUser());
			v.vuser_id.write(to);
		} break;
		case mtpc_peerChat: {
			const MTPDpeerChat &v(c_peerChat());
			v.vchat_id.write(to);
		} break;
	}
}
inline MTPpeer::MTPpeer(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_peerUser: setData(new MTPDpeerUser()); break;
		case mtpc_peerChat: setData(new MTPDpeerChat()); break;
		default: throw mtpErrorBadTypeId(type, "MTPpeer");
	}
}
inline MTPpeer::MTPpeer(MTPDpeerUser *_data) : mtpDataOwner(_data), _type(mtpc_peerUser) {
}
inline MTPpeer::MTPpeer(MTPDpeerChat *_data) : mtpDataOwner(_data), _type(mtpc_peerChat) {
}
inline MTPpeer MTP_peerUser(MTPint _user_id) {
	return MTPpeer(new MTPDpeerUser(_user_id));
}
inline MTPpeer MTP_peerChat(MTPint _chat_id) {
	return MTPpeer(new MTPDpeerChat(_chat_id));
}

inline uint32 MTPstorage_fileType::size() const {
	return 0;
}
inline mtpTypeId MTPstorage_fileType::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPstorage_fileType::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	switch (cons) {
		case mtpc_storage_fileUnknown: _type = cons; break;
		case mtpc_storage_fileJpeg: _type = cons; break;
		case mtpc_storage_fileGif: _type = cons; break;
		case mtpc_storage_filePng: _type = cons; break;
		case mtpc_storage_filePdf: _type = cons; break;
		case mtpc_storage_fileMp3: _type = cons; break;
		case mtpc_storage_fileMov: _type = cons; break;
		case mtpc_storage_filePartial: _type = cons; break;
		case mtpc_storage_fileMp4: _type = cons; break;
		case mtpc_storage_fileWebp: _type = cons; break;
		default: throw mtpErrorUnexpected(cons, "MTPstorage_fileType");
	}
}
inline void MTPstorage_fileType::write(mtpBuffer &/*to*/) const {
	switch (_type) {
	}
}
inline MTPstorage_fileType::MTPstorage_fileType(mtpTypeId type) : _type(type) {
	switch (type) {
		case mtpc_storage_fileUnknown: break;
		case mtpc_storage_fileJpeg: break;
		case mtpc_storage_fileGif: break;
		case mtpc_storage_filePng: break;
		case mtpc_storage_filePdf: break;
		case mtpc_storage_fileMp3: break;
		case mtpc_storage_fileMov: break;
		case mtpc_storage_filePartial: break;
		case mtpc_storage_fileMp4: break;
		case mtpc_storage_fileWebp: break;
		default: throw mtpErrorBadTypeId(type, "MTPstorage_fileType");
	}
}
inline MTPstorage_fileType MTP_storage_fileUnknown() {
	return MTPstorage_fileType(mtpc_storage_fileUnknown);
}
inline MTPstorage_fileType MTP_storage_fileJpeg() {
	return MTPstorage_fileType(mtpc_storage_fileJpeg);
}
inline MTPstorage_fileType MTP_storage_fileGif() {
	return MTPstorage_fileType(mtpc_storage_fileGif);
}
inline MTPstorage_fileType MTP_storage_filePng() {
	return MTPstorage_fileType(mtpc_storage_filePng);
}
inline MTPstorage_fileType MTP_storage_filePdf() {
	return MTPstorage_fileType(mtpc_storage_filePdf);
}
inline MTPstorage_fileType MTP_storage_fileMp3() {
	return MTPstorage_fileType(mtpc_storage_fileMp3);
}
inline MTPstorage_fileType MTP_storage_fileMov() {
	return MTPstorage_fileType(mtpc_storage_fileMov);
}
inline MTPstorage_fileType MTP_storage_filePartial() {
	return MTPstorage_fileType(mtpc_storage_filePartial);
}
inline MTPstorage_fileType MTP_storage_fileMp4() {
	return MTPstorage_fileType(mtpc_storage_fileMp4);
}
inline MTPstorage_fileType MTP_storage_fileWebp() {
	return MTPstorage_fileType(mtpc_storage_fileWebp);
}

inline uint32 MTPfileLocation::size() const {
	switch (_type) {
		case mtpc_fileLocationUnavailable: {
			const MTPDfileLocationUnavailable &v(c_fileLocationUnavailable());
			return v.vvolume_id.size() + v.vlocal_id.size() + v.vsecret.size();
		}
		case mtpc_fileLocation: {
			const MTPDfileLocation &v(c_fileLocation());
			return v.vdc_id.size() + v.vvolume_id.size() + v.vlocal_id.size() + v.vsecret.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPfileLocation::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPfileLocation::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_fileLocationUnavailable: _type = cons; {
			if (!data) setData(new MTPDfileLocationUnavailable());
			MTPDfileLocationUnavailable &v(_fileLocationUnavailable());
			v.vvolume_id.read(from, end);
			v.vlocal_id.read(from, end);
			v.vsecret.read(from, end);
		} break;
		case mtpc_fileLocation: _type = cons; {
			if (!data) setData(new MTPDfileLocation());
			MTPDfileLocation &v(_fileLocation());
			v.vdc_id.read(from, end);
			v.vvolume_id.read(from, end);
			v.vlocal_id.read(from, end);
			v.vsecret.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPfileLocation");
	}
}
inline void MTPfileLocation::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_fileLocationUnavailable: {
			const MTPDfileLocationUnavailable &v(c_fileLocationUnavailable());
			v.vvolume_id.write(to);
			v.vlocal_id.write(to);
			v.vsecret.write(to);
		} break;
		case mtpc_fileLocation: {
			const MTPDfileLocation &v(c_fileLocation());
			v.vdc_id.write(to);
			v.vvolume_id.write(to);
			v.vlocal_id.write(to);
			v.vsecret.write(to);
		} break;
	}
}
inline MTPfileLocation::MTPfileLocation(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_fileLocationUnavailable: setData(new MTPDfileLocationUnavailable()); break;
		case mtpc_fileLocation: setData(new MTPDfileLocation()); break;
		default: throw mtpErrorBadTypeId(type, "MTPfileLocation");
	}
}
inline MTPfileLocation::MTPfileLocation(MTPDfileLocationUnavailable *_data) : mtpDataOwner(_data), _type(mtpc_fileLocationUnavailable) {
}
inline MTPfileLocation::MTPfileLocation(MTPDfileLocation *_data) : mtpDataOwner(_data), _type(mtpc_fileLocation) {
}
inline MTPfileLocation MTP_fileLocationUnavailable(const MTPlong &_volume_id, MTPint _local_id, const MTPlong &_secret) {
	return MTPfileLocation(new MTPDfileLocationUnavailable(_volume_id, _local_id, _secret));
}
inline MTPfileLocation MTP_fileLocation(MTPint _dc_id, const MTPlong &_volume_id, MTPint _local_id, const MTPlong &_secret) {
	return MTPfileLocation(new MTPDfileLocation(_dc_id, _volume_id, _local_id, _secret));
}

inline uint32 MTPuser::size() const {
	switch (_type) {
		case mtpc_userEmpty: {
			const MTPDuserEmpty &v(c_userEmpty());
			return v.vid.size();
		}
		case mtpc_userSelf: {
			const MTPDuserSelf &v(c_userSelf());
			return v.vid.size() + v.vfirst_name.size() + v.vlast_name.size() + v.vphone.size() + v.vphoto.size() + v.vstatus.size() + v.vinactive.size();
		}
		case mtpc_userContact: {
			const MTPDuserContact &v(c_userContact());
			return v.vid.size() + v.vfirst_name.size() + v.vlast_name.size() + v.vaccess_hash.size() + v.vphone.size() + v.vphoto.size() + v.vstatus.size();
		}
		case mtpc_userRequest: {
			const MTPDuserRequest &v(c_userRequest());
			return v.vid.size() + v.vfirst_name.size() + v.vlast_name.size() + v.vaccess_hash.size() + v.vphone.size() + v.vphoto.size() + v.vstatus.size();
		}
		case mtpc_userForeign: {
			const MTPDuserForeign &v(c_userForeign());
			return v.vid.size() + v.vfirst_name.size() + v.vlast_name.size() + v.vaccess_hash.size() + v.vphoto.size() + v.vstatus.size();
		}
		case mtpc_userDeleted: {
			const MTPDuserDeleted &v(c_userDeleted());
			return v.vid.size() + v.vfirst_name.size() + v.vlast_name.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPuser::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPuser::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_userEmpty: _type = cons; {
			if (!data) setData(new MTPDuserEmpty());
			MTPDuserEmpty &v(_userEmpty());
			v.vid.read(from, end);
		} break;
		case mtpc_userSelf: _type = cons; {
			if (!data) setData(new MTPDuserSelf());
			MTPDuserSelf &v(_userSelf());
			v.vid.read(from, end);
			v.vfirst_name.read(from, end);
			v.vlast_name.read(from, end);
			v.vphone.read(from, end);
			v.vphoto.read(from, end);
			v.vstatus.read(from, end);
			v.vinactive.read(from, end);
		} break;
		case mtpc_userContact: _type = cons; {
			if (!data) setData(new MTPDuserContact());
			MTPDuserContact &v(_userContact());
			v.vid.read(from, end);
			v.vfirst_name.read(from, end);
			v.vlast_name.read(from, end);
			v.vaccess_hash.read(from, end);
			v.vphone.read(from, end);
			v.vphoto.read(from, end);
			v.vstatus.read(from, end);
		} break;
		case mtpc_userRequest: _type = cons; {
			if (!data) setData(new MTPDuserRequest());
			MTPDuserRequest &v(_userRequest());
			v.vid.read(from, end);
			v.vfirst_name.read(from, end);
			v.vlast_name.read(from, end);
			v.vaccess_hash.read(from, end);
			v.vphone.read(from, end);
			v.vphoto.read(from, end);
			v.vstatus.read(from, end);
		} break;
		case mtpc_userForeign: _type = cons; {
			if (!data) setData(new MTPDuserForeign());
			MTPDuserForeign &v(_userForeign());
			v.vid.read(from, end);
			v.vfirst_name.read(from, end);
			v.vlast_name.read(from, end);
			v.vaccess_hash.read(from, end);
			v.vphoto.read(from, end);
			v.vstatus.read(from, end);
		} break;
		case mtpc_userDeleted: _type = cons; {
			if (!data) setData(new MTPDuserDeleted());
			MTPDuserDeleted &v(_userDeleted());
			v.vid.read(from, end);
			v.vfirst_name.read(from, end);
			v.vlast_name.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPuser");
	}
}
inline void MTPuser::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_userEmpty: {
			const MTPDuserEmpty &v(c_userEmpty());
			v.vid.write(to);
		} break;
		case mtpc_userSelf: {
			const MTPDuserSelf &v(c_userSelf());
			v.vid.write(to);
			v.vfirst_name.write(to);
			v.vlast_name.write(to);
			v.vphone.write(to);
			v.vphoto.write(to);
			v.vstatus.write(to);
			v.vinactive.write(to);
		} break;
		case mtpc_userContact: {
			const MTPDuserContact &v(c_userContact());
			v.vid.write(to);
			v.vfirst_name.write(to);
			v.vlast_name.write(to);
			v.vaccess_hash.write(to);
			v.vphone.write(to);
			v.vphoto.write(to);
			v.vstatus.write(to);
		} break;
		case mtpc_userRequest: {
			const MTPDuserRequest &v(c_userRequest());
			v.vid.write(to);
			v.vfirst_name.write(to);
			v.vlast_name.write(to);
			v.vaccess_hash.write(to);
			v.vphone.write(to);
			v.vphoto.write(to);
			v.vstatus.write(to);
		} break;
		case mtpc_userForeign: {
			const MTPDuserForeign &v(c_userForeign());
			v.vid.write(to);
			v.vfirst_name.write(to);
			v.vlast_name.write(to);
			v.vaccess_hash.write(to);
			v.vphoto.write(to);
			v.vstatus.write(to);
		} break;
		case mtpc_userDeleted: {
			const MTPDuserDeleted &v(c_userDeleted());
			v.vid.write(to);
			v.vfirst_name.write(to);
			v.vlast_name.write(to);
		} break;
	}
}
inline MTPuser::MTPuser(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_userEmpty: setData(new MTPDuserEmpty()); break;
		case mtpc_userSelf: setData(new MTPDuserSelf()); break;
		case mtpc_userContact: setData(new MTPDuserContact()); break;
		case mtpc_userRequest: setData(new MTPDuserRequest()); break;
		case mtpc_userForeign: setData(new MTPDuserForeign()); break;
		case mtpc_userDeleted: setData(new MTPDuserDeleted()); break;
		default: throw mtpErrorBadTypeId(type, "MTPuser");
	}
}
inline MTPuser::MTPuser(MTPDuserEmpty *_data) : mtpDataOwner(_data), _type(mtpc_userEmpty) {
}
inline MTPuser::MTPuser(MTPDuserSelf *_data) : mtpDataOwner(_data), _type(mtpc_userSelf) {
}
inline MTPuser::MTPuser(MTPDuserContact *_data) : mtpDataOwner(_data), _type(mtpc_userContact) {
}
inline MTPuser::MTPuser(MTPDuserRequest *_data) : mtpDataOwner(_data), _type(mtpc_userRequest) {
}
inline MTPuser::MTPuser(MTPDuserForeign *_data) : mtpDataOwner(_data), _type(mtpc_userForeign) {
}
inline MTPuser::MTPuser(MTPDuserDeleted *_data) : mtpDataOwner(_data), _type(mtpc_userDeleted) {
}
inline MTPuser MTP_userEmpty(MTPint _id) {
	return MTPuser(new MTPDuserEmpty(_id));
}
inline MTPuser MTP_userSelf(MTPint _id, const MTPstring &_first_name, const MTPstring &_last_name, const MTPstring &_phone, const MTPUserProfilePhoto &_photo, const MTPUserStatus &_status, MTPBool _inactive) {
	return MTPuser(new MTPDuserSelf(_id, _first_name, _last_name, _phone, _photo, _status, _inactive));
}
inline MTPuser MTP_userContact(MTPint _id, const MTPstring &_first_name, const MTPstring &_last_name, const MTPlong &_access_hash, const MTPstring &_phone, const MTPUserProfilePhoto &_photo, const MTPUserStatus &_status) {
	return MTPuser(new MTPDuserContact(_id, _first_name, _last_name, _access_hash, _phone, _photo, _status));
}
inline MTPuser MTP_userRequest(MTPint _id, const MTPstring &_first_name, const MTPstring &_last_name, const MTPlong &_access_hash, const MTPstring &_phone, const MTPUserProfilePhoto &_photo, const MTPUserStatus &_status) {
	return MTPuser(new MTPDuserRequest(_id, _first_name, _last_name, _access_hash, _phone, _photo, _status));
}
inline MTPuser MTP_userForeign(MTPint _id, const MTPstring &_first_name, const MTPstring &_last_name, const MTPlong &_access_hash, const MTPUserProfilePhoto &_photo, const MTPUserStatus &_status) {
	return MTPuser(new MTPDuserForeign(_id, _first_name, _last_name, _access_hash, _photo, _status));
}
inline MTPuser MTP_userDeleted(MTPint _id, const MTPstring &_first_name, const MTPstring &_last_name) {
	return MTPuser(new MTPDuserDeleted(_id, _first_name, _last_name));
}

inline uint32 MTPuserProfilePhoto::size() const {
	switch (_type) {
		case mtpc_userProfilePhoto: {
			const MTPDuserProfilePhoto &v(c_userProfilePhoto());
			return v.vphoto_id.size() + v.vphoto_small.size() + v.vphoto_big.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPuserProfilePhoto::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPuserProfilePhoto::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_userProfilePhotoEmpty: _type = cons; break;
		case mtpc_userProfilePhoto: _type = cons; {
			if (!data) setData(new MTPDuserProfilePhoto());
			MTPDuserProfilePhoto &v(_userProfilePhoto());
			v.vphoto_id.read(from, end);
			v.vphoto_small.read(from, end);
			v.vphoto_big.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPuserProfilePhoto");
	}
}
inline void MTPuserProfilePhoto::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_userProfilePhoto: {
			const MTPDuserProfilePhoto &v(c_userProfilePhoto());
			v.vphoto_id.write(to);
			v.vphoto_small.write(to);
			v.vphoto_big.write(to);
		} break;
	}
}
inline MTPuserProfilePhoto::MTPuserProfilePhoto(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_userProfilePhotoEmpty: break;
		case mtpc_userProfilePhoto: setData(new MTPDuserProfilePhoto()); break;
		default: throw mtpErrorBadTypeId(type, "MTPuserProfilePhoto");
	}
}
inline MTPuserProfilePhoto::MTPuserProfilePhoto(MTPDuserProfilePhoto *_data) : mtpDataOwner(_data), _type(mtpc_userProfilePhoto) {
}
inline MTPuserProfilePhoto MTP_userProfilePhotoEmpty() {
	return MTPuserProfilePhoto(mtpc_userProfilePhotoEmpty);
}
inline MTPuserProfilePhoto MTP_userProfilePhoto(const MTPlong &_photo_id, const MTPFileLocation &_photo_small, const MTPFileLocation &_photo_big) {
	return MTPuserProfilePhoto(new MTPDuserProfilePhoto(_photo_id, _photo_small, _photo_big));
}

inline uint32 MTPuserStatus::size() const {
	switch (_type) {
		case mtpc_userStatusOnline: {
			const MTPDuserStatusOnline &v(c_userStatusOnline());
			return v.vexpires.size();
		}
		case mtpc_userStatusOffline: {
			const MTPDuserStatusOffline &v(c_userStatusOffline());
			return v.vwas_online.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPuserStatus::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPuserStatus::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_userStatusEmpty: _type = cons; break;
		case mtpc_userStatusOnline: _type = cons; {
			if (!data) setData(new MTPDuserStatusOnline());
			MTPDuserStatusOnline &v(_userStatusOnline());
			v.vexpires.read(from, end);
		} break;
		case mtpc_userStatusOffline: _type = cons; {
			if (!data) setData(new MTPDuserStatusOffline());
			MTPDuserStatusOffline &v(_userStatusOffline());
			v.vwas_online.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPuserStatus");
	}
}
inline void MTPuserStatus::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_userStatusOnline: {
			const MTPDuserStatusOnline &v(c_userStatusOnline());
			v.vexpires.write(to);
		} break;
		case mtpc_userStatusOffline: {
			const MTPDuserStatusOffline &v(c_userStatusOffline());
			v.vwas_online.write(to);
		} break;
	}
}
inline MTPuserStatus::MTPuserStatus(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_userStatusEmpty: break;
		case mtpc_userStatusOnline: setData(new MTPDuserStatusOnline()); break;
		case mtpc_userStatusOffline: setData(new MTPDuserStatusOffline()); break;
		default: throw mtpErrorBadTypeId(type, "MTPuserStatus");
	}
}
inline MTPuserStatus::MTPuserStatus(MTPDuserStatusOnline *_data) : mtpDataOwner(_data), _type(mtpc_userStatusOnline) {
}
inline MTPuserStatus::MTPuserStatus(MTPDuserStatusOffline *_data) : mtpDataOwner(_data), _type(mtpc_userStatusOffline) {
}
inline MTPuserStatus MTP_userStatusEmpty() {
	return MTPuserStatus(mtpc_userStatusEmpty);
}
inline MTPuserStatus MTP_userStatusOnline(MTPint _expires) {
	return MTPuserStatus(new MTPDuserStatusOnline(_expires));
}
inline MTPuserStatus MTP_userStatusOffline(MTPint _was_online) {
	return MTPuserStatus(new MTPDuserStatusOffline(_was_online));
}

inline uint32 MTPchat::size() const {
	switch (_type) {
		case mtpc_chatEmpty: {
			const MTPDchatEmpty &v(c_chatEmpty());
			return v.vid.size();
		}
		case mtpc_chat: {
			const MTPDchat &v(c_chat());
			return v.vid.size() + v.vtitle.size() + v.vphoto.size() + v.vparticipants_count.size() + v.vdate.size() + v.vleft.size() + v.vversion.size();
		}
		case mtpc_chatForbidden: {
			const MTPDchatForbidden &v(c_chatForbidden());
			return v.vid.size() + v.vtitle.size() + v.vdate.size();
		}
		case mtpc_geoChat: {
			const MTPDgeoChat &v(c_geoChat());
			return v.vid.size() + v.vaccess_hash.size() + v.vtitle.size() + v.vaddress.size() + v.vvenue.size() + v.vgeo.size() + v.vphoto.size() + v.vparticipants_count.size() + v.vdate.size() + v.vchecked_in.size() + v.vversion.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPchat::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPchat::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_chatEmpty: _type = cons; {
			if (!data) setData(new MTPDchatEmpty());
			MTPDchatEmpty &v(_chatEmpty());
			v.vid.read(from, end);
		} break;
		case mtpc_chat: _type = cons; {
			if (!data) setData(new MTPDchat());
			MTPDchat &v(_chat());
			v.vid.read(from, end);
			v.vtitle.read(from, end);
			v.vphoto.read(from, end);
			v.vparticipants_count.read(from, end);
			v.vdate.read(from, end);
			v.vleft.read(from, end);
			v.vversion.read(from, end);
		} break;
		case mtpc_chatForbidden: _type = cons; {
			if (!data) setData(new MTPDchatForbidden());
			MTPDchatForbidden &v(_chatForbidden());
			v.vid.read(from, end);
			v.vtitle.read(from, end);
			v.vdate.read(from, end);
		} break;
		case mtpc_geoChat: _type = cons; {
			if (!data) setData(new MTPDgeoChat());
			MTPDgeoChat &v(_geoChat());
			v.vid.read(from, end);
			v.vaccess_hash.read(from, end);
			v.vtitle.read(from, end);
			v.vaddress.read(from, end);
			v.vvenue.read(from, end);
			v.vgeo.read(from, end);
			v.vphoto.read(from, end);
			v.vparticipants_count.read(from, end);
			v.vdate.read(from, end);
			v.vchecked_in.read(from, end);
			v.vversion.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPchat");
	}
}
inline void MTPchat::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_chatEmpty: {
			const MTPDchatEmpty &v(c_chatEmpty());
			v.vid.write(to);
		} break;
		case mtpc_chat: {
			const MTPDchat &v(c_chat());
			v.vid.write(to);
			v.vtitle.write(to);
			v.vphoto.write(to);
			v.vparticipants_count.write(to);
			v.vdate.write(to);
			v.vleft.write(to);
			v.vversion.write(to);
		} break;
		case mtpc_chatForbidden: {
			const MTPDchatForbidden &v(c_chatForbidden());
			v.vid.write(to);
			v.vtitle.write(to);
			v.vdate.write(to);
		} break;
		case mtpc_geoChat: {
			const MTPDgeoChat &v(c_geoChat());
			v.vid.write(to);
			v.vaccess_hash.write(to);
			v.vtitle.write(to);
			v.vaddress.write(to);
			v.vvenue.write(to);
			v.vgeo.write(to);
			v.vphoto.write(to);
			v.vparticipants_count.write(to);
			v.vdate.write(to);
			v.vchecked_in.write(to);
			v.vversion.write(to);
		} break;
	}
}
inline MTPchat::MTPchat(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_chatEmpty: setData(new MTPDchatEmpty()); break;
		case mtpc_chat: setData(new MTPDchat()); break;
		case mtpc_chatForbidden: setData(new MTPDchatForbidden()); break;
		case mtpc_geoChat: setData(new MTPDgeoChat()); break;
		default: throw mtpErrorBadTypeId(type, "MTPchat");
	}
}
inline MTPchat::MTPchat(MTPDchatEmpty *_data) : mtpDataOwner(_data), _type(mtpc_chatEmpty) {
}
inline MTPchat::MTPchat(MTPDchat *_data) : mtpDataOwner(_data), _type(mtpc_chat) {
}
inline MTPchat::MTPchat(MTPDchatForbidden *_data) : mtpDataOwner(_data), _type(mtpc_chatForbidden) {
}
inline MTPchat::MTPchat(MTPDgeoChat *_data) : mtpDataOwner(_data), _type(mtpc_geoChat) {
}
inline MTPchat MTP_chatEmpty(MTPint _id) {
	return MTPchat(new MTPDchatEmpty(_id));
}
inline MTPchat MTP_chat(MTPint _id, const MTPstring &_title, const MTPChatPhoto &_photo, MTPint _participants_count, MTPint _date, MTPBool _left, MTPint _version) {
	return MTPchat(new MTPDchat(_id, _title, _photo, _participants_count, _date, _left, _version));
}
inline MTPchat MTP_chatForbidden(MTPint _id, const MTPstring &_title, MTPint _date) {
	return MTPchat(new MTPDchatForbidden(_id, _title, _date));
}
inline MTPchat MTP_geoChat(MTPint _id, const MTPlong &_access_hash, const MTPstring &_title, const MTPstring &_address, const MTPstring &_venue, const MTPGeoPoint &_geo, const MTPChatPhoto &_photo, MTPint _participants_count, MTPint _date, MTPBool _checked_in, MTPint _version) {
	return MTPchat(new MTPDgeoChat(_id, _access_hash, _title, _address, _venue, _geo, _photo, _participants_count, _date, _checked_in, _version));
}

inline MTPchatFull::MTPchatFull() : mtpDataOwner(new MTPDchatFull()) {
}

inline uint32 MTPchatFull::size() const {
	const MTPDchatFull &v(c_chatFull());
	return v.vid.size() + v.vparticipants.size() + v.vchat_photo.size() + v.vnotify_settings.size();
}
inline mtpTypeId MTPchatFull::type() const {
	return mtpc_chatFull;
}
inline void MTPchatFull::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_chatFull) throw mtpErrorUnexpected(cons, "MTPchatFull");

	if (!data) setData(new MTPDchatFull());
	MTPDchatFull &v(_chatFull());
	v.vid.read(from, end);
	v.vparticipants.read(from, end);
	v.vchat_photo.read(from, end);
	v.vnotify_settings.read(from, end);
}
inline void MTPchatFull::write(mtpBuffer &to) const {
	const MTPDchatFull &v(c_chatFull());
	v.vid.write(to);
	v.vparticipants.write(to);
	v.vchat_photo.write(to);
	v.vnotify_settings.write(to);
}
inline MTPchatFull::MTPchatFull(MTPDchatFull *_data) : mtpDataOwner(_data) {
}
inline MTPchatFull MTP_chatFull(MTPint _id, const MTPChatParticipants &_participants, const MTPPhoto &_chat_photo, const MTPPeerNotifySettings &_notify_settings) {
	return MTPchatFull(new MTPDchatFull(_id, _participants, _chat_photo, _notify_settings));
}

inline MTPchatParticipant::MTPchatParticipant() : mtpDataOwner(new MTPDchatParticipant()) {
}

inline uint32 MTPchatParticipant::size() const {
	const MTPDchatParticipant &v(c_chatParticipant());
	return v.vuser_id.size() + v.vinviter_id.size() + v.vdate.size();
}
inline mtpTypeId MTPchatParticipant::type() const {
	return mtpc_chatParticipant;
}
inline void MTPchatParticipant::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_chatParticipant) throw mtpErrorUnexpected(cons, "MTPchatParticipant");

	if (!data) setData(new MTPDchatParticipant());
	MTPDchatParticipant &v(_chatParticipant());
	v.vuser_id.read(from, end);
	v.vinviter_id.read(from, end);
	v.vdate.read(from, end);
}
inline void MTPchatParticipant::write(mtpBuffer &to) const {
	const MTPDchatParticipant &v(c_chatParticipant());
	v.vuser_id.write(to);
	v.vinviter_id.write(to);
	v.vdate.write(to);
}
inline MTPchatParticipant::MTPchatParticipant(MTPDchatParticipant *_data) : mtpDataOwner(_data) {
}
inline MTPchatParticipant MTP_chatParticipant(MTPint _user_id, MTPint _inviter_id, MTPint _date) {
	return MTPchatParticipant(new MTPDchatParticipant(_user_id, _inviter_id, _date));
}

inline uint32 MTPchatParticipants::size() const {
	switch (_type) {
		case mtpc_chatParticipantsForbidden: {
			const MTPDchatParticipantsForbidden &v(c_chatParticipantsForbidden());
			return v.vchat_id.size();
		}
		case mtpc_chatParticipants: {
			const MTPDchatParticipants &v(c_chatParticipants());
			return v.vchat_id.size() + v.vadmin_id.size() + v.vparticipants.size() + v.vversion.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPchatParticipants::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPchatParticipants::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_chatParticipantsForbidden: _type = cons; {
			if (!data) setData(new MTPDchatParticipantsForbidden());
			MTPDchatParticipantsForbidden &v(_chatParticipantsForbidden());
			v.vchat_id.read(from, end);
		} break;
		case mtpc_chatParticipants: _type = cons; {
			if (!data) setData(new MTPDchatParticipants());
			MTPDchatParticipants &v(_chatParticipants());
			v.vchat_id.read(from, end);
			v.vadmin_id.read(from, end);
			v.vparticipants.read(from, end);
			v.vversion.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPchatParticipants");
	}
}
inline void MTPchatParticipants::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_chatParticipantsForbidden: {
			const MTPDchatParticipantsForbidden &v(c_chatParticipantsForbidden());
			v.vchat_id.write(to);
		} break;
		case mtpc_chatParticipants: {
			const MTPDchatParticipants &v(c_chatParticipants());
			v.vchat_id.write(to);
			v.vadmin_id.write(to);
			v.vparticipants.write(to);
			v.vversion.write(to);
		} break;
	}
}
inline MTPchatParticipants::MTPchatParticipants(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_chatParticipantsForbidden: setData(new MTPDchatParticipantsForbidden()); break;
		case mtpc_chatParticipants: setData(new MTPDchatParticipants()); break;
		default: throw mtpErrorBadTypeId(type, "MTPchatParticipants");
	}
}
inline MTPchatParticipants::MTPchatParticipants(MTPDchatParticipantsForbidden *_data) : mtpDataOwner(_data), _type(mtpc_chatParticipantsForbidden) {
}
inline MTPchatParticipants::MTPchatParticipants(MTPDchatParticipants *_data) : mtpDataOwner(_data), _type(mtpc_chatParticipants) {
}
inline MTPchatParticipants MTP_chatParticipantsForbidden(MTPint _chat_id) {
	return MTPchatParticipants(new MTPDchatParticipantsForbidden(_chat_id));
}
inline MTPchatParticipants MTP_chatParticipants(MTPint _chat_id, MTPint _admin_id, const MTPVector<MTPChatParticipant> &_participants, MTPint _version) {
	return MTPchatParticipants(new MTPDchatParticipants(_chat_id, _admin_id, _participants, _version));
}

inline uint32 MTPchatPhoto::size() const {
	switch (_type) {
		case mtpc_chatPhoto: {
			const MTPDchatPhoto &v(c_chatPhoto());
			return v.vphoto_small.size() + v.vphoto_big.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPchatPhoto::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPchatPhoto::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_chatPhotoEmpty: _type = cons; break;
		case mtpc_chatPhoto: _type = cons; {
			if (!data) setData(new MTPDchatPhoto());
			MTPDchatPhoto &v(_chatPhoto());
			v.vphoto_small.read(from, end);
			v.vphoto_big.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPchatPhoto");
	}
}
inline void MTPchatPhoto::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_chatPhoto: {
			const MTPDchatPhoto &v(c_chatPhoto());
			v.vphoto_small.write(to);
			v.vphoto_big.write(to);
		} break;
	}
}
inline MTPchatPhoto::MTPchatPhoto(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_chatPhotoEmpty: break;
		case mtpc_chatPhoto: setData(new MTPDchatPhoto()); break;
		default: throw mtpErrorBadTypeId(type, "MTPchatPhoto");
	}
}
inline MTPchatPhoto::MTPchatPhoto(MTPDchatPhoto *_data) : mtpDataOwner(_data), _type(mtpc_chatPhoto) {
}
inline MTPchatPhoto MTP_chatPhotoEmpty() {
	return MTPchatPhoto(mtpc_chatPhotoEmpty);
}
inline MTPchatPhoto MTP_chatPhoto(const MTPFileLocation &_photo_small, const MTPFileLocation &_photo_big) {
	return MTPchatPhoto(new MTPDchatPhoto(_photo_small, _photo_big));
}

inline uint32 MTPmessage::size() const {
	switch (_type) {
		case mtpc_messageEmpty: {
			const MTPDmessageEmpty &v(c_messageEmpty());
			return v.vid.size();
		}
		case mtpc_message: {
			const MTPDmessage &v(c_message());
			return v.vid.size() + v.vfrom_id.size() + v.vto_id.size() + v.vout.size() + v.vunread.size() + v.vdate.size() + v.vmessage.size() + v.vmedia.size();
		}
		case mtpc_messageForwarded: {
			const MTPDmessageForwarded &v(c_messageForwarded());
			return v.vid.size() + v.vfwd_from_id.size() + v.vfwd_date.size() + v.vfrom_id.size() + v.vto_id.size() + v.vout.size() + v.vunread.size() + v.vdate.size() + v.vmessage.size() + v.vmedia.size();
		}
		case mtpc_messageService: {
			const MTPDmessageService &v(c_messageService());
			return v.vid.size() + v.vfrom_id.size() + v.vto_id.size() + v.vout.size() + v.vunread.size() + v.vdate.size() + v.vaction.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPmessage::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPmessage::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_messageEmpty: _type = cons; {
			if (!data) setData(new MTPDmessageEmpty());
			MTPDmessageEmpty &v(_messageEmpty());
			v.vid.read(from, end);
		} break;
		case mtpc_message: _type = cons; {
			if (!data) setData(new MTPDmessage());
			MTPDmessage &v(_message());
			v.vid.read(from, end);
			v.vfrom_id.read(from, end);
			v.vto_id.read(from, end);
			v.vout.read(from, end);
			v.vunread.read(from, end);
			v.vdate.read(from, end);
			v.vmessage.read(from, end);
			v.vmedia.read(from, end);
		} break;
		case mtpc_messageForwarded: _type = cons; {
			if (!data) setData(new MTPDmessageForwarded());
			MTPDmessageForwarded &v(_messageForwarded());
			v.vid.read(from, end);
			v.vfwd_from_id.read(from, end);
			v.vfwd_date.read(from, end);
			v.vfrom_id.read(from, end);
			v.vto_id.read(from, end);
			v.vout.read(from, end);
			v.vunread.read(from, end);
			v.vdate.read(from, end);
			v.vmessage.read(from, end);
			v.vmedia.read(from, end);
		} break;
		case mtpc_messageService: _type = cons; {
			if (!data) setData(new MTPDmessageService());
			MTPDmessageService &v(_messageService());
			v.vid.read(from, end);
			v.vfrom_id.read(from, end);
			v.vto_id.read(from, end);
			v.vout.read(from, end);
			v.vunread.read(from, end);
			v.vdate.read(from, end);
			v.vaction.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPmessage");
	}
}
inline void MTPmessage::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_messageEmpty: {
			const MTPDmessageEmpty &v(c_messageEmpty());
			v.vid.write(to);
		} break;
		case mtpc_message: {
			const MTPDmessage &v(c_message());
			v.vid.write(to);
			v.vfrom_id.write(to);
			v.vto_id.write(to);
			v.vout.write(to);
			v.vunread.write(to);
			v.vdate.write(to);
			v.vmessage.write(to);
			v.vmedia.write(to);
		} break;
		case mtpc_messageForwarded: {
			const MTPDmessageForwarded &v(c_messageForwarded());
			v.vid.write(to);
			v.vfwd_from_id.write(to);
			v.vfwd_date.write(to);
			v.vfrom_id.write(to);
			v.vto_id.write(to);
			v.vout.write(to);
			v.vunread.write(to);
			v.vdate.write(to);
			v.vmessage.write(to);
			v.vmedia.write(to);
		} break;
		case mtpc_messageService: {
			const MTPDmessageService &v(c_messageService());
			v.vid.write(to);
			v.vfrom_id.write(to);
			v.vto_id.write(to);
			v.vout.write(to);
			v.vunread.write(to);
			v.vdate.write(to);
			v.vaction.write(to);
		} break;
	}
}
inline MTPmessage::MTPmessage(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_messageEmpty: setData(new MTPDmessageEmpty()); break;
		case mtpc_message: setData(new MTPDmessage()); break;
		case mtpc_messageForwarded: setData(new MTPDmessageForwarded()); break;
		case mtpc_messageService: setData(new MTPDmessageService()); break;
		default: throw mtpErrorBadTypeId(type, "MTPmessage");
	}
}
inline MTPmessage::MTPmessage(MTPDmessageEmpty *_data) : mtpDataOwner(_data), _type(mtpc_messageEmpty) {
}
inline MTPmessage::MTPmessage(MTPDmessage *_data) : mtpDataOwner(_data), _type(mtpc_message) {
}
inline MTPmessage::MTPmessage(MTPDmessageForwarded *_data) : mtpDataOwner(_data), _type(mtpc_messageForwarded) {
}
inline MTPmessage::MTPmessage(MTPDmessageService *_data) : mtpDataOwner(_data), _type(mtpc_messageService) {
}
inline MTPmessage MTP_messageEmpty(MTPint _id) {
	return MTPmessage(new MTPDmessageEmpty(_id));
}
inline MTPmessage MTP_message(MTPint _id, MTPint _from_id, const MTPPeer &_to_id, MTPBool _out, MTPBool _unread, MTPint _date, const MTPstring &_message, const MTPMessageMedia &_media) {
	return MTPmessage(new MTPDmessage(_id, _from_id, _to_id, _out, _unread, _date, _message, _media));
}
inline MTPmessage MTP_messageForwarded(MTPint _id, MTPint _fwd_from_id, MTPint _fwd_date, MTPint _from_id, const MTPPeer &_to_id, MTPBool _out, MTPBool _unread, MTPint _date, const MTPstring &_message, const MTPMessageMedia &_media) {
	return MTPmessage(new MTPDmessageForwarded(_id, _fwd_from_id, _fwd_date, _from_id, _to_id, _out, _unread, _date, _message, _media));
}
inline MTPmessage MTP_messageService(MTPint _id, MTPint _from_id, const MTPPeer &_to_id, MTPBool _out, MTPBool _unread, MTPint _date, const MTPMessageAction &_action) {
	return MTPmessage(new MTPDmessageService(_id, _from_id, _to_id, _out, _unread, _date, _action));
}

inline uint32 MTPmessageMedia::size() const {
	switch (_type) {
		case mtpc_messageMediaPhoto: {
			const MTPDmessageMediaPhoto &v(c_messageMediaPhoto());
			return v.vphoto.size();
		}
		case mtpc_messageMediaVideo: {
			const MTPDmessageMediaVideo &v(c_messageMediaVideo());
			return v.vvideo.size();
		}
		case mtpc_messageMediaGeo: {
			const MTPDmessageMediaGeo &v(c_messageMediaGeo());
			return v.vgeo.size();
		}
		case mtpc_messageMediaContact: {
			const MTPDmessageMediaContact &v(c_messageMediaContact());
			return v.vphone_number.size() + v.vfirst_name.size() + v.vlast_name.size() + v.vuser_id.size();
		}
		case mtpc_messageMediaUnsupported: {
			const MTPDmessageMediaUnsupported &v(c_messageMediaUnsupported());
			return v.vbytes.size();
		}
		case mtpc_messageMediaDocument: {
			const MTPDmessageMediaDocument &v(c_messageMediaDocument());
			return v.vdocument.size();
		}
		case mtpc_messageMediaAudio: {
			const MTPDmessageMediaAudio &v(c_messageMediaAudio());
			return v.vaudio.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPmessageMedia::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPmessageMedia::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_messageMediaEmpty: _type = cons; break;
		case mtpc_messageMediaPhoto: _type = cons; {
			if (!data) setData(new MTPDmessageMediaPhoto());
			MTPDmessageMediaPhoto &v(_messageMediaPhoto());
			v.vphoto.read(from, end);
		} break;
		case mtpc_messageMediaVideo: _type = cons; {
			if (!data) setData(new MTPDmessageMediaVideo());
			MTPDmessageMediaVideo &v(_messageMediaVideo());
			v.vvideo.read(from, end);
		} break;
		case mtpc_messageMediaGeo: _type = cons; {
			if (!data) setData(new MTPDmessageMediaGeo());
			MTPDmessageMediaGeo &v(_messageMediaGeo());
			v.vgeo.read(from, end);
		} break;
		case mtpc_messageMediaContact: _type = cons; {
			if (!data) setData(new MTPDmessageMediaContact());
			MTPDmessageMediaContact &v(_messageMediaContact());
			v.vphone_number.read(from, end);
			v.vfirst_name.read(from, end);
			v.vlast_name.read(from, end);
			v.vuser_id.read(from, end);
		} break;
		case mtpc_messageMediaUnsupported: _type = cons; {
			if (!data) setData(new MTPDmessageMediaUnsupported());
			MTPDmessageMediaUnsupported &v(_messageMediaUnsupported());
			v.vbytes.read(from, end);
		} break;
		case mtpc_messageMediaDocument: _type = cons; {
			if (!data) setData(new MTPDmessageMediaDocument());
			MTPDmessageMediaDocument &v(_messageMediaDocument());
			v.vdocument.read(from, end);
		} break;
		case mtpc_messageMediaAudio: _type = cons; {
			if (!data) setData(new MTPDmessageMediaAudio());
			MTPDmessageMediaAudio &v(_messageMediaAudio());
			v.vaudio.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPmessageMedia");
	}
}
inline void MTPmessageMedia::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_messageMediaPhoto: {
			const MTPDmessageMediaPhoto &v(c_messageMediaPhoto());
			v.vphoto.write(to);
		} break;
		case mtpc_messageMediaVideo: {
			const MTPDmessageMediaVideo &v(c_messageMediaVideo());
			v.vvideo.write(to);
		} break;
		case mtpc_messageMediaGeo: {
			const MTPDmessageMediaGeo &v(c_messageMediaGeo());
			v.vgeo.write(to);
		} break;
		case mtpc_messageMediaContact: {
			const MTPDmessageMediaContact &v(c_messageMediaContact());
			v.vphone_number.write(to);
			v.vfirst_name.write(to);
			v.vlast_name.write(to);
			v.vuser_id.write(to);
		} break;
		case mtpc_messageMediaUnsupported: {
			const MTPDmessageMediaUnsupported &v(c_messageMediaUnsupported());
			v.vbytes.write(to);
		} break;
		case mtpc_messageMediaDocument: {
			const MTPDmessageMediaDocument &v(c_messageMediaDocument());
			v.vdocument.write(to);
		} break;
		case mtpc_messageMediaAudio: {
			const MTPDmessageMediaAudio &v(c_messageMediaAudio());
			v.vaudio.write(to);
		} break;
	}
}
inline MTPmessageMedia::MTPmessageMedia(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_messageMediaEmpty: break;
		case mtpc_messageMediaPhoto: setData(new MTPDmessageMediaPhoto()); break;
		case mtpc_messageMediaVideo: setData(new MTPDmessageMediaVideo()); break;
		case mtpc_messageMediaGeo: setData(new MTPDmessageMediaGeo()); break;
		case mtpc_messageMediaContact: setData(new MTPDmessageMediaContact()); break;
		case mtpc_messageMediaUnsupported: setData(new MTPDmessageMediaUnsupported()); break;
		case mtpc_messageMediaDocument: setData(new MTPDmessageMediaDocument()); break;
		case mtpc_messageMediaAudio: setData(new MTPDmessageMediaAudio()); break;
		default: throw mtpErrorBadTypeId(type, "MTPmessageMedia");
	}
}
inline MTPmessageMedia::MTPmessageMedia(MTPDmessageMediaPhoto *_data) : mtpDataOwner(_data), _type(mtpc_messageMediaPhoto) {
}
inline MTPmessageMedia::MTPmessageMedia(MTPDmessageMediaVideo *_data) : mtpDataOwner(_data), _type(mtpc_messageMediaVideo) {
}
inline MTPmessageMedia::MTPmessageMedia(MTPDmessageMediaGeo *_data) : mtpDataOwner(_data), _type(mtpc_messageMediaGeo) {
}
inline MTPmessageMedia::MTPmessageMedia(MTPDmessageMediaContact *_data) : mtpDataOwner(_data), _type(mtpc_messageMediaContact) {
}
inline MTPmessageMedia::MTPmessageMedia(MTPDmessageMediaUnsupported *_data) : mtpDataOwner(_data), _type(mtpc_messageMediaUnsupported) {
}
inline MTPmessageMedia::MTPmessageMedia(MTPDmessageMediaDocument *_data) : mtpDataOwner(_data), _type(mtpc_messageMediaDocument) {
}
inline MTPmessageMedia::MTPmessageMedia(MTPDmessageMediaAudio *_data) : mtpDataOwner(_data), _type(mtpc_messageMediaAudio) {
}
inline MTPmessageMedia MTP_messageMediaEmpty() {
	return MTPmessageMedia(mtpc_messageMediaEmpty);
}
inline MTPmessageMedia MTP_messageMediaPhoto(const MTPPhoto &_photo) {
	return MTPmessageMedia(new MTPDmessageMediaPhoto(_photo));
}
inline MTPmessageMedia MTP_messageMediaVideo(const MTPVideo &_video) {
	return MTPmessageMedia(new MTPDmessageMediaVideo(_video));
}
inline MTPmessageMedia MTP_messageMediaGeo(const MTPGeoPoint &_geo) {
	return MTPmessageMedia(new MTPDmessageMediaGeo(_geo));
}
inline MTPmessageMedia MTP_messageMediaContact(const MTPstring &_phone_number, const MTPstring &_first_name, const MTPstring &_last_name, MTPint _user_id) {
	return MTPmessageMedia(new MTPDmessageMediaContact(_phone_number, _first_name, _last_name, _user_id));
}
inline MTPmessageMedia MTP_messageMediaUnsupported(const MTPbytes &_bytes) {
	return MTPmessageMedia(new MTPDmessageMediaUnsupported(_bytes));
}
inline MTPmessageMedia MTP_messageMediaDocument(const MTPDocument &_document) {
	return MTPmessageMedia(new MTPDmessageMediaDocument(_document));
}
inline MTPmessageMedia MTP_messageMediaAudio(const MTPAudio &_audio) {
	return MTPmessageMedia(new MTPDmessageMediaAudio(_audio));
}

inline uint32 MTPmessageAction::size() const {
	switch (_type) {
		case mtpc_messageActionChatCreate: {
			const MTPDmessageActionChatCreate &v(c_messageActionChatCreate());
			return v.vtitle.size() + v.vusers.size();
		}
		case mtpc_messageActionChatEditTitle: {
			const MTPDmessageActionChatEditTitle &v(c_messageActionChatEditTitle());
			return v.vtitle.size();
		}
		case mtpc_messageActionChatEditPhoto: {
			const MTPDmessageActionChatEditPhoto &v(c_messageActionChatEditPhoto());
			return v.vphoto.size();
		}
		case mtpc_messageActionChatAddUser: {
			const MTPDmessageActionChatAddUser &v(c_messageActionChatAddUser());
			return v.vuser_id.size();
		}
		case mtpc_messageActionChatDeleteUser: {
			const MTPDmessageActionChatDeleteUser &v(c_messageActionChatDeleteUser());
			return v.vuser_id.size();
		}
		case mtpc_messageActionGeoChatCreate: {
			const MTPDmessageActionGeoChatCreate &v(c_messageActionGeoChatCreate());
			return v.vtitle.size() + v.vaddress.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPmessageAction::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPmessageAction::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_messageActionEmpty: _type = cons; break;
		case mtpc_messageActionChatCreate: _type = cons; {
			if (!data) setData(new MTPDmessageActionChatCreate());
			MTPDmessageActionChatCreate &v(_messageActionChatCreate());
			v.vtitle.read(from, end);
			v.vusers.read(from, end);
		} break;
		case mtpc_messageActionChatEditTitle: _type = cons; {
			if (!data) setData(new MTPDmessageActionChatEditTitle());
			MTPDmessageActionChatEditTitle &v(_messageActionChatEditTitle());
			v.vtitle.read(from, end);
		} break;
		case mtpc_messageActionChatEditPhoto: _type = cons; {
			if (!data) setData(new MTPDmessageActionChatEditPhoto());
			MTPDmessageActionChatEditPhoto &v(_messageActionChatEditPhoto());
			v.vphoto.read(from, end);
		} break;
		case mtpc_messageActionChatDeletePhoto: _type = cons; break;
		case mtpc_messageActionChatAddUser: _type = cons; {
			if (!data) setData(new MTPDmessageActionChatAddUser());
			MTPDmessageActionChatAddUser &v(_messageActionChatAddUser());
			v.vuser_id.read(from, end);
		} break;
		case mtpc_messageActionChatDeleteUser: _type = cons; {
			if (!data) setData(new MTPDmessageActionChatDeleteUser());
			MTPDmessageActionChatDeleteUser &v(_messageActionChatDeleteUser());
			v.vuser_id.read(from, end);
		} break;
		case mtpc_messageActionGeoChatCreate: _type = cons; {
			if (!data) setData(new MTPDmessageActionGeoChatCreate());
			MTPDmessageActionGeoChatCreate &v(_messageActionGeoChatCreate());
			v.vtitle.read(from, end);
			v.vaddress.read(from, end);
		} break;
		case mtpc_messageActionGeoChatCheckin: _type = cons; break;
		default: throw mtpErrorUnexpected(cons, "MTPmessageAction");
	}
}
inline void MTPmessageAction::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_messageActionChatCreate: {
			const MTPDmessageActionChatCreate &v(c_messageActionChatCreate());
			v.vtitle.write(to);
			v.vusers.write(to);
		} break;
		case mtpc_messageActionChatEditTitle: {
			const MTPDmessageActionChatEditTitle &v(c_messageActionChatEditTitle());
			v.vtitle.write(to);
		} break;
		case mtpc_messageActionChatEditPhoto: {
			const MTPDmessageActionChatEditPhoto &v(c_messageActionChatEditPhoto());
			v.vphoto.write(to);
		} break;
		case mtpc_messageActionChatAddUser: {
			const MTPDmessageActionChatAddUser &v(c_messageActionChatAddUser());
			v.vuser_id.write(to);
		} break;
		case mtpc_messageActionChatDeleteUser: {
			const MTPDmessageActionChatDeleteUser &v(c_messageActionChatDeleteUser());
			v.vuser_id.write(to);
		} break;
		case mtpc_messageActionGeoChatCreate: {
			const MTPDmessageActionGeoChatCreate &v(c_messageActionGeoChatCreate());
			v.vtitle.write(to);
			v.vaddress.write(to);
		} break;
	}
}
inline MTPmessageAction::MTPmessageAction(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_messageActionEmpty: break;
		case mtpc_messageActionChatCreate: setData(new MTPDmessageActionChatCreate()); break;
		case mtpc_messageActionChatEditTitle: setData(new MTPDmessageActionChatEditTitle()); break;
		case mtpc_messageActionChatEditPhoto: setData(new MTPDmessageActionChatEditPhoto()); break;
		case mtpc_messageActionChatDeletePhoto: break;
		case mtpc_messageActionChatAddUser: setData(new MTPDmessageActionChatAddUser()); break;
		case mtpc_messageActionChatDeleteUser: setData(new MTPDmessageActionChatDeleteUser()); break;
		case mtpc_messageActionGeoChatCreate: setData(new MTPDmessageActionGeoChatCreate()); break;
		case mtpc_messageActionGeoChatCheckin: break;
		default: throw mtpErrorBadTypeId(type, "MTPmessageAction");
	}
}
inline MTPmessageAction::MTPmessageAction(MTPDmessageActionChatCreate *_data) : mtpDataOwner(_data), _type(mtpc_messageActionChatCreate) {
}
inline MTPmessageAction::MTPmessageAction(MTPDmessageActionChatEditTitle *_data) : mtpDataOwner(_data), _type(mtpc_messageActionChatEditTitle) {
}
inline MTPmessageAction::MTPmessageAction(MTPDmessageActionChatEditPhoto *_data) : mtpDataOwner(_data), _type(mtpc_messageActionChatEditPhoto) {
}
inline MTPmessageAction::MTPmessageAction(MTPDmessageActionChatAddUser *_data) : mtpDataOwner(_data), _type(mtpc_messageActionChatAddUser) {
}
inline MTPmessageAction::MTPmessageAction(MTPDmessageActionChatDeleteUser *_data) : mtpDataOwner(_data), _type(mtpc_messageActionChatDeleteUser) {
}
inline MTPmessageAction::MTPmessageAction(MTPDmessageActionGeoChatCreate *_data) : mtpDataOwner(_data), _type(mtpc_messageActionGeoChatCreate) {
}
inline MTPmessageAction MTP_messageActionEmpty() {
	return MTPmessageAction(mtpc_messageActionEmpty);
}
inline MTPmessageAction MTP_messageActionChatCreate(const MTPstring &_title, const MTPVector<MTPint> &_users) {
	return MTPmessageAction(new MTPDmessageActionChatCreate(_title, _users));
}
inline MTPmessageAction MTP_messageActionChatEditTitle(const MTPstring &_title) {
	return MTPmessageAction(new MTPDmessageActionChatEditTitle(_title));
}
inline MTPmessageAction MTP_messageActionChatEditPhoto(const MTPPhoto &_photo) {
	return MTPmessageAction(new MTPDmessageActionChatEditPhoto(_photo));
}
inline MTPmessageAction MTP_messageActionChatDeletePhoto() {
	return MTPmessageAction(mtpc_messageActionChatDeletePhoto);
}
inline MTPmessageAction MTP_messageActionChatAddUser(MTPint _user_id) {
	return MTPmessageAction(new MTPDmessageActionChatAddUser(_user_id));
}
inline MTPmessageAction MTP_messageActionChatDeleteUser(MTPint _user_id) {
	return MTPmessageAction(new MTPDmessageActionChatDeleteUser(_user_id));
}
inline MTPmessageAction MTP_messageActionGeoChatCreate(const MTPstring &_title, const MTPstring &_address) {
	return MTPmessageAction(new MTPDmessageActionGeoChatCreate(_title, _address));
}
inline MTPmessageAction MTP_messageActionGeoChatCheckin() {
	return MTPmessageAction(mtpc_messageActionGeoChatCheckin);
}

inline MTPdialog::MTPdialog() : mtpDataOwner(new MTPDdialog()) {
}

inline uint32 MTPdialog::size() const {
	const MTPDdialog &v(c_dialog());
	return v.vpeer.size() + v.vtop_message.size() + v.vunread_count.size() + v.vnotify_settings.size();
}
inline mtpTypeId MTPdialog::type() const {
	return mtpc_dialog;
}
inline void MTPdialog::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_dialog) throw mtpErrorUnexpected(cons, "MTPdialog");

	if (!data) setData(new MTPDdialog());
	MTPDdialog &v(_dialog());
	v.vpeer.read(from, end);
	v.vtop_message.read(from, end);
	v.vunread_count.read(from, end);
	v.vnotify_settings.read(from, end);
}
inline void MTPdialog::write(mtpBuffer &to) const {
	const MTPDdialog &v(c_dialog());
	v.vpeer.write(to);
	v.vtop_message.write(to);
	v.vunread_count.write(to);
	v.vnotify_settings.write(to);
}
inline MTPdialog::MTPdialog(MTPDdialog *_data) : mtpDataOwner(_data) {
}
inline MTPdialog MTP_dialog(const MTPPeer &_peer, MTPint _top_message, MTPint _unread_count, const MTPPeerNotifySettings &_notify_settings) {
	return MTPdialog(new MTPDdialog(_peer, _top_message, _unread_count, _notify_settings));
}

inline uint32 MTPphoto::size() const {
	switch (_type) {
		case mtpc_photoEmpty: {
			const MTPDphotoEmpty &v(c_photoEmpty());
			return v.vid.size();
		}
		case mtpc_photo: {
			const MTPDphoto &v(c_photo());
			return v.vid.size() + v.vaccess_hash.size() + v.vuser_id.size() + v.vdate.size() + v.vcaption.size() + v.vgeo.size() + v.vsizes.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPphoto::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPphoto::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_photoEmpty: _type = cons; {
			if (!data) setData(new MTPDphotoEmpty());
			MTPDphotoEmpty &v(_photoEmpty());
			v.vid.read(from, end);
		} break;
		case mtpc_photo: _type = cons; {
			if (!data) setData(new MTPDphoto());
			MTPDphoto &v(_photo());
			v.vid.read(from, end);
			v.vaccess_hash.read(from, end);
			v.vuser_id.read(from, end);
			v.vdate.read(from, end);
			v.vcaption.read(from, end);
			v.vgeo.read(from, end);
			v.vsizes.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPphoto");
	}
}
inline void MTPphoto::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_photoEmpty: {
			const MTPDphotoEmpty &v(c_photoEmpty());
			v.vid.write(to);
		} break;
		case mtpc_photo: {
			const MTPDphoto &v(c_photo());
			v.vid.write(to);
			v.vaccess_hash.write(to);
			v.vuser_id.write(to);
			v.vdate.write(to);
			v.vcaption.write(to);
			v.vgeo.write(to);
			v.vsizes.write(to);
		} break;
	}
}
inline MTPphoto::MTPphoto(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_photoEmpty: setData(new MTPDphotoEmpty()); break;
		case mtpc_photo: setData(new MTPDphoto()); break;
		default: throw mtpErrorBadTypeId(type, "MTPphoto");
	}
}
inline MTPphoto::MTPphoto(MTPDphotoEmpty *_data) : mtpDataOwner(_data), _type(mtpc_photoEmpty) {
}
inline MTPphoto::MTPphoto(MTPDphoto *_data) : mtpDataOwner(_data), _type(mtpc_photo) {
}
inline MTPphoto MTP_photoEmpty(const MTPlong &_id) {
	return MTPphoto(new MTPDphotoEmpty(_id));
}
inline MTPphoto MTP_photo(const MTPlong &_id, const MTPlong &_access_hash, MTPint _user_id, MTPint _date, const MTPstring &_caption, const MTPGeoPoint &_geo, const MTPVector<MTPPhotoSize> &_sizes) {
	return MTPphoto(new MTPDphoto(_id, _access_hash, _user_id, _date, _caption, _geo, _sizes));
}

inline uint32 MTPphotoSize::size() const {
	switch (_type) {
		case mtpc_photoSizeEmpty: {
			const MTPDphotoSizeEmpty &v(c_photoSizeEmpty());
			return v.vtype.size();
		}
		case mtpc_photoSize: {
			const MTPDphotoSize &v(c_photoSize());
			return v.vtype.size() + v.vlocation.size() + v.vw.size() + v.vh.size() + v.vsize.size();
		}
		case mtpc_photoCachedSize: {
			const MTPDphotoCachedSize &v(c_photoCachedSize());
			return v.vtype.size() + v.vlocation.size() + v.vw.size() + v.vh.size() + v.vbytes.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPphotoSize::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPphotoSize::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_photoSizeEmpty: _type = cons; {
			if (!data) setData(new MTPDphotoSizeEmpty());
			MTPDphotoSizeEmpty &v(_photoSizeEmpty());
			v.vtype.read(from, end);
		} break;
		case mtpc_photoSize: _type = cons; {
			if (!data) setData(new MTPDphotoSize());
			MTPDphotoSize &v(_photoSize());
			v.vtype.read(from, end);
			v.vlocation.read(from, end);
			v.vw.read(from, end);
			v.vh.read(from, end);
			v.vsize.read(from, end);
		} break;
		case mtpc_photoCachedSize: _type = cons; {
			if (!data) setData(new MTPDphotoCachedSize());
			MTPDphotoCachedSize &v(_photoCachedSize());
			v.vtype.read(from, end);
			v.vlocation.read(from, end);
			v.vw.read(from, end);
			v.vh.read(from, end);
			v.vbytes.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPphotoSize");
	}
}
inline void MTPphotoSize::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_photoSizeEmpty: {
			const MTPDphotoSizeEmpty &v(c_photoSizeEmpty());
			v.vtype.write(to);
		} break;
		case mtpc_photoSize: {
			const MTPDphotoSize &v(c_photoSize());
			v.vtype.write(to);
			v.vlocation.write(to);
			v.vw.write(to);
			v.vh.write(to);
			v.vsize.write(to);
		} break;
		case mtpc_photoCachedSize: {
			const MTPDphotoCachedSize &v(c_photoCachedSize());
			v.vtype.write(to);
			v.vlocation.write(to);
			v.vw.write(to);
			v.vh.write(to);
			v.vbytes.write(to);
		} break;
	}
}
inline MTPphotoSize::MTPphotoSize(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_photoSizeEmpty: setData(new MTPDphotoSizeEmpty()); break;
		case mtpc_photoSize: setData(new MTPDphotoSize()); break;
		case mtpc_photoCachedSize: setData(new MTPDphotoCachedSize()); break;
		default: throw mtpErrorBadTypeId(type, "MTPphotoSize");
	}
}
inline MTPphotoSize::MTPphotoSize(MTPDphotoSizeEmpty *_data) : mtpDataOwner(_data), _type(mtpc_photoSizeEmpty) {
}
inline MTPphotoSize::MTPphotoSize(MTPDphotoSize *_data) : mtpDataOwner(_data), _type(mtpc_photoSize) {
}
inline MTPphotoSize::MTPphotoSize(MTPDphotoCachedSize *_data) : mtpDataOwner(_data), _type(mtpc_photoCachedSize) {
}
inline MTPphotoSize MTP_photoSizeEmpty(const MTPstring &_type) {
	return MTPphotoSize(new MTPDphotoSizeEmpty(_type));
}
inline MTPphotoSize MTP_photoSize(const MTPstring &_type, const MTPFileLocation &_location, MTPint _w, MTPint _h, MTPint _size) {
	return MTPphotoSize(new MTPDphotoSize(_type, _location, _w, _h, _size));
}
inline MTPphotoSize MTP_photoCachedSize(const MTPstring &_type, const MTPFileLocation &_location, MTPint _w, MTPint _h, const MTPbytes &_bytes) {
	return MTPphotoSize(new MTPDphotoCachedSize(_type, _location, _w, _h, _bytes));
}

inline uint32 MTPvideo::size() const {
	switch (_type) {
		case mtpc_videoEmpty: {
			const MTPDvideoEmpty &v(c_videoEmpty());
			return v.vid.size();
		}
		case mtpc_video: {
			const MTPDvideo &v(c_video());
			return v.vid.size() + v.vaccess_hash.size() + v.vuser_id.size() + v.vdate.size() + v.vcaption.size() + v.vduration.size() + v.vmime_type.size() + v.vsize.size() + v.vthumb.size() + v.vdc_id.size() + v.vw.size() + v.vh.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPvideo::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPvideo::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_videoEmpty: _type = cons; {
			if (!data) setData(new MTPDvideoEmpty());
			MTPDvideoEmpty &v(_videoEmpty());
			v.vid.read(from, end);
		} break;
		case mtpc_video: _type = cons; {
			if (!data) setData(new MTPDvideo());
			MTPDvideo &v(_video());
			v.vid.read(from, end);
			v.vaccess_hash.read(from, end);
			v.vuser_id.read(from, end);
			v.vdate.read(from, end);
			v.vcaption.read(from, end);
			v.vduration.read(from, end);
			v.vmime_type.read(from, end);
			v.vsize.read(from, end);
			v.vthumb.read(from, end);
			v.vdc_id.read(from, end);
			v.vw.read(from, end);
			v.vh.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPvideo");
	}
}
inline void MTPvideo::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_videoEmpty: {
			const MTPDvideoEmpty &v(c_videoEmpty());
			v.vid.write(to);
		} break;
		case mtpc_video: {
			const MTPDvideo &v(c_video());
			v.vid.write(to);
			v.vaccess_hash.write(to);
			v.vuser_id.write(to);
			v.vdate.write(to);
			v.vcaption.write(to);
			v.vduration.write(to);
			v.vmime_type.write(to);
			v.vsize.write(to);
			v.vthumb.write(to);
			v.vdc_id.write(to);
			v.vw.write(to);
			v.vh.write(to);
		} break;
	}
}
inline MTPvideo::MTPvideo(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_videoEmpty: setData(new MTPDvideoEmpty()); break;
		case mtpc_video: setData(new MTPDvideo()); break;
		default: throw mtpErrorBadTypeId(type, "MTPvideo");
	}
}
inline MTPvideo::MTPvideo(MTPDvideoEmpty *_data) : mtpDataOwner(_data), _type(mtpc_videoEmpty) {
}
inline MTPvideo::MTPvideo(MTPDvideo *_data) : mtpDataOwner(_data), _type(mtpc_video) {
}
inline MTPvideo MTP_videoEmpty(const MTPlong &_id) {
	return MTPvideo(new MTPDvideoEmpty(_id));
}
inline MTPvideo MTP_video(const MTPlong &_id, const MTPlong &_access_hash, MTPint _user_id, MTPint _date, const MTPstring &_caption, MTPint _duration, const MTPstring &_mime_type, MTPint _size, const MTPPhotoSize &_thumb, MTPint _dc_id, MTPint _w, MTPint _h) {
	return MTPvideo(new MTPDvideo(_id, _access_hash, _user_id, _date, _caption, _duration, _mime_type, _size, _thumb, _dc_id, _w, _h));
}

inline uint32 MTPgeoPoint::size() const {
	switch (_type) {
		case mtpc_geoPoint: {
			const MTPDgeoPoint &v(c_geoPoint());
			return v.vlong.size() + v.vlat.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPgeoPoint::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPgeoPoint::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_geoPointEmpty: _type = cons; break;
		case mtpc_geoPoint: _type = cons; {
			if (!data) setData(new MTPDgeoPoint());
			MTPDgeoPoint &v(_geoPoint());
			v.vlong.read(from, end);
			v.vlat.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPgeoPoint");
	}
}
inline void MTPgeoPoint::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_geoPoint: {
			const MTPDgeoPoint &v(c_geoPoint());
			v.vlong.write(to);
			v.vlat.write(to);
		} break;
	}
}
inline MTPgeoPoint::MTPgeoPoint(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_geoPointEmpty: break;
		case mtpc_geoPoint: setData(new MTPDgeoPoint()); break;
		default: throw mtpErrorBadTypeId(type, "MTPgeoPoint");
	}
}
inline MTPgeoPoint::MTPgeoPoint(MTPDgeoPoint *_data) : mtpDataOwner(_data), _type(mtpc_geoPoint) {
}
inline MTPgeoPoint MTP_geoPointEmpty() {
	return MTPgeoPoint(mtpc_geoPointEmpty);
}
inline MTPgeoPoint MTP_geoPoint(const MTPdouble &_long, const MTPdouble &_lat) {
	return MTPgeoPoint(new MTPDgeoPoint(_long, _lat));
}

inline MTPauth_checkedPhone::MTPauth_checkedPhone() : mtpDataOwner(new MTPDauth_checkedPhone()) {
}

inline uint32 MTPauth_checkedPhone::size() const {
	const MTPDauth_checkedPhone &v(c_auth_checkedPhone());
	return v.vphone_registered.size() + v.vphone_invited.size();
}
inline mtpTypeId MTPauth_checkedPhone::type() const {
	return mtpc_auth_checkedPhone;
}
inline void MTPauth_checkedPhone::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_auth_checkedPhone) throw mtpErrorUnexpected(cons, "MTPauth_checkedPhone");

	if (!data) setData(new MTPDauth_checkedPhone());
	MTPDauth_checkedPhone &v(_auth_checkedPhone());
	v.vphone_registered.read(from, end);
	v.vphone_invited.read(from, end);
}
inline void MTPauth_checkedPhone::write(mtpBuffer &to) const {
	const MTPDauth_checkedPhone &v(c_auth_checkedPhone());
	v.vphone_registered.write(to);
	v.vphone_invited.write(to);
}
inline MTPauth_checkedPhone::MTPauth_checkedPhone(MTPDauth_checkedPhone *_data) : mtpDataOwner(_data) {
}
inline MTPauth_checkedPhone MTP_auth_checkedPhone(MTPBool _phone_registered, MTPBool _phone_invited) {
	return MTPauth_checkedPhone(new MTPDauth_checkedPhone(_phone_registered, _phone_invited));
}

inline MTPauth_sentCode::MTPauth_sentCode() : mtpDataOwner(new MTPDauth_sentCode()) {
}

inline uint32 MTPauth_sentCode::size() const {
	const MTPDauth_sentCode &v(c_auth_sentCode());
	return v.vphone_registered.size() + v.vphone_code_hash.size() + v.vsend_call_timeout.size() + v.vis_password.size();
}
inline mtpTypeId MTPauth_sentCode::type() const {
	return mtpc_auth_sentCode;
}
inline void MTPauth_sentCode::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_auth_sentCode) throw mtpErrorUnexpected(cons, "MTPauth_sentCode");

	if (!data) setData(new MTPDauth_sentCode());
	MTPDauth_sentCode &v(_auth_sentCode());
	v.vphone_registered.read(from, end);
	v.vphone_code_hash.read(from, end);
	v.vsend_call_timeout.read(from, end);
	v.vis_password.read(from, end);
}
inline void MTPauth_sentCode::write(mtpBuffer &to) const {
	const MTPDauth_sentCode &v(c_auth_sentCode());
	v.vphone_registered.write(to);
	v.vphone_code_hash.write(to);
	v.vsend_call_timeout.write(to);
	v.vis_password.write(to);
}
inline MTPauth_sentCode::MTPauth_sentCode(MTPDauth_sentCode *_data) : mtpDataOwner(_data) {
}
inline MTPauth_sentCode MTP_auth_sentCode(MTPBool _phone_registered, const MTPstring &_phone_code_hash, MTPint _send_call_timeout, MTPBool _is_password) {
	return MTPauth_sentCode(new MTPDauth_sentCode(_phone_registered, _phone_code_hash, _send_call_timeout, _is_password));
}

inline MTPauth_authorization::MTPauth_authorization() : mtpDataOwner(new MTPDauth_authorization()) {
}

inline uint32 MTPauth_authorization::size() const {
	const MTPDauth_authorization &v(c_auth_authorization());
	return v.vexpires.size() + v.vuser.size();
}
inline mtpTypeId MTPauth_authorization::type() const {
	return mtpc_auth_authorization;
}
inline void MTPauth_authorization::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_auth_authorization) throw mtpErrorUnexpected(cons, "MTPauth_authorization");

	if (!data) setData(new MTPDauth_authorization());
	MTPDauth_authorization &v(_auth_authorization());
	v.vexpires.read(from, end);
	v.vuser.read(from, end);
}
inline void MTPauth_authorization::write(mtpBuffer &to) const {
	const MTPDauth_authorization &v(c_auth_authorization());
	v.vexpires.write(to);
	v.vuser.write(to);
}
inline MTPauth_authorization::MTPauth_authorization(MTPDauth_authorization *_data) : mtpDataOwner(_data) {
}
inline MTPauth_authorization MTP_auth_authorization(MTPint _expires, const MTPUser &_user) {
	return MTPauth_authorization(new MTPDauth_authorization(_expires, _user));
}

inline MTPauth_exportedAuthorization::MTPauth_exportedAuthorization() : mtpDataOwner(new MTPDauth_exportedAuthorization()) {
}

inline uint32 MTPauth_exportedAuthorization::size() const {
	const MTPDauth_exportedAuthorization &v(c_auth_exportedAuthorization());
	return v.vid.size() + v.vbytes.size();
}
inline mtpTypeId MTPauth_exportedAuthorization::type() const {
	return mtpc_auth_exportedAuthorization;
}
inline void MTPauth_exportedAuthorization::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_auth_exportedAuthorization) throw mtpErrorUnexpected(cons, "MTPauth_exportedAuthorization");

	if (!data) setData(new MTPDauth_exportedAuthorization());
	MTPDauth_exportedAuthorization &v(_auth_exportedAuthorization());
	v.vid.read(from, end);
	v.vbytes.read(from, end);
}
inline void MTPauth_exportedAuthorization::write(mtpBuffer &to) const {
	const MTPDauth_exportedAuthorization &v(c_auth_exportedAuthorization());
	v.vid.write(to);
	v.vbytes.write(to);
}
inline MTPauth_exportedAuthorization::MTPauth_exportedAuthorization(MTPDauth_exportedAuthorization *_data) : mtpDataOwner(_data) {
}
inline MTPauth_exportedAuthorization MTP_auth_exportedAuthorization(MTPint _id, const MTPbytes &_bytes) {
	return MTPauth_exportedAuthorization(new MTPDauth_exportedAuthorization(_id, _bytes));
}

inline uint32 MTPinputNotifyPeer::size() const {
	switch (_type) {
		case mtpc_inputNotifyPeer: {
			const MTPDinputNotifyPeer &v(c_inputNotifyPeer());
			return v.vpeer.size();
		}
		case mtpc_inputNotifyGeoChatPeer: {
			const MTPDinputNotifyGeoChatPeer &v(c_inputNotifyGeoChatPeer());
			return v.vpeer.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPinputNotifyPeer::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPinputNotifyPeer::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_inputNotifyPeer: _type = cons; {
			if (!data) setData(new MTPDinputNotifyPeer());
			MTPDinputNotifyPeer &v(_inputNotifyPeer());
			v.vpeer.read(from, end);
		} break;
		case mtpc_inputNotifyUsers: _type = cons; break;
		case mtpc_inputNotifyChats: _type = cons; break;
		case mtpc_inputNotifyAll: _type = cons; break;
		case mtpc_inputNotifyGeoChatPeer: _type = cons; {
			if (!data) setData(new MTPDinputNotifyGeoChatPeer());
			MTPDinputNotifyGeoChatPeer &v(_inputNotifyGeoChatPeer());
			v.vpeer.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPinputNotifyPeer");
	}
}
inline void MTPinputNotifyPeer::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_inputNotifyPeer: {
			const MTPDinputNotifyPeer &v(c_inputNotifyPeer());
			v.vpeer.write(to);
		} break;
		case mtpc_inputNotifyGeoChatPeer: {
			const MTPDinputNotifyGeoChatPeer &v(c_inputNotifyGeoChatPeer());
			v.vpeer.write(to);
		} break;
	}
}
inline MTPinputNotifyPeer::MTPinputNotifyPeer(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_inputNotifyPeer: setData(new MTPDinputNotifyPeer()); break;
		case mtpc_inputNotifyUsers: break;
		case mtpc_inputNotifyChats: break;
		case mtpc_inputNotifyAll: break;
		case mtpc_inputNotifyGeoChatPeer: setData(new MTPDinputNotifyGeoChatPeer()); break;
		default: throw mtpErrorBadTypeId(type, "MTPinputNotifyPeer");
	}
}
inline MTPinputNotifyPeer::MTPinputNotifyPeer(MTPDinputNotifyPeer *_data) : mtpDataOwner(_data), _type(mtpc_inputNotifyPeer) {
}
inline MTPinputNotifyPeer::MTPinputNotifyPeer(MTPDinputNotifyGeoChatPeer *_data) : mtpDataOwner(_data), _type(mtpc_inputNotifyGeoChatPeer) {
}
inline MTPinputNotifyPeer MTP_inputNotifyPeer(const MTPInputPeer &_peer) {
	return MTPinputNotifyPeer(new MTPDinputNotifyPeer(_peer));
}
inline MTPinputNotifyPeer MTP_inputNotifyUsers() {
	return MTPinputNotifyPeer(mtpc_inputNotifyUsers);
}
inline MTPinputNotifyPeer MTP_inputNotifyChats() {
	return MTPinputNotifyPeer(mtpc_inputNotifyChats);
}
inline MTPinputNotifyPeer MTP_inputNotifyAll() {
	return MTPinputNotifyPeer(mtpc_inputNotifyAll);
}
inline MTPinputNotifyPeer MTP_inputNotifyGeoChatPeer(const MTPInputGeoChat &_peer) {
	return MTPinputNotifyPeer(new MTPDinputNotifyGeoChatPeer(_peer));
}

inline uint32 MTPinputPeerNotifyEvents::size() const {
	return 0;
}
inline mtpTypeId MTPinputPeerNotifyEvents::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPinputPeerNotifyEvents::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	switch (cons) {
		case mtpc_inputPeerNotifyEventsEmpty: _type = cons; break;
		case mtpc_inputPeerNotifyEventsAll: _type = cons; break;
		default: throw mtpErrorUnexpected(cons, "MTPinputPeerNotifyEvents");
	}
}
inline void MTPinputPeerNotifyEvents::write(mtpBuffer &/*to*/) const {
	switch (_type) {
	}
}
inline MTPinputPeerNotifyEvents::MTPinputPeerNotifyEvents(mtpTypeId type) : _type(type) {
	switch (type) {
		case mtpc_inputPeerNotifyEventsEmpty: break;
		case mtpc_inputPeerNotifyEventsAll: break;
		default: throw mtpErrorBadTypeId(type, "MTPinputPeerNotifyEvents");
	}
}
inline MTPinputPeerNotifyEvents MTP_inputPeerNotifyEventsEmpty() {
	return MTPinputPeerNotifyEvents(mtpc_inputPeerNotifyEventsEmpty);
}
inline MTPinputPeerNotifyEvents MTP_inputPeerNotifyEventsAll() {
	return MTPinputPeerNotifyEvents(mtpc_inputPeerNotifyEventsAll);
}

inline MTPinputPeerNotifySettings::MTPinputPeerNotifySettings() : mtpDataOwner(new MTPDinputPeerNotifySettings()) {
}

inline uint32 MTPinputPeerNotifySettings::size() const {
	const MTPDinputPeerNotifySettings &v(c_inputPeerNotifySettings());
	return v.vmute_until.size() + v.vsound.size() + v.vshow_previews.size() + v.vevents_mask.size();
}
inline mtpTypeId MTPinputPeerNotifySettings::type() const {
	return mtpc_inputPeerNotifySettings;
}
inline void MTPinputPeerNotifySettings::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_inputPeerNotifySettings) throw mtpErrorUnexpected(cons, "MTPinputPeerNotifySettings");

	if (!data) setData(new MTPDinputPeerNotifySettings());
	MTPDinputPeerNotifySettings &v(_inputPeerNotifySettings());
	v.vmute_until.read(from, end);
	v.vsound.read(from, end);
	v.vshow_previews.read(from, end);
	v.vevents_mask.read(from, end);
}
inline void MTPinputPeerNotifySettings::write(mtpBuffer &to) const {
	const MTPDinputPeerNotifySettings &v(c_inputPeerNotifySettings());
	v.vmute_until.write(to);
	v.vsound.write(to);
	v.vshow_previews.write(to);
	v.vevents_mask.write(to);
}
inline MTPinputPeerNotifySettings::MTPinputPeerNotifySettings(MTPDinputPeerNotifySettings *_data) : mtpDataOwner(_data) {
}
inline MTPinputPeerNotifySettings MTP_inputPeerNotifySettings(MTPint _mute_until, const MTPstring &_sound, MTPBool _show_previews, MTPint _events_mask) {
	return MTPinputPeerNotifySettings(new MTPDinputPeerNotifySettings(_mute_until, _sound, _show_previews, _events_mask));
}

inline uint32 MTPpeerNotifyEvents::size() const {
	return 0;
}
inline mtpTypeId MTPpeerNotifyEvents::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPpeerNotifyEvents::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	switch (cons) {
		case mtpc_peerNotifyEventsEmpty: _type = cons; break;
		case mtpc_peerNotifyEventsAll: _type = cons; break;
		default: throw mtpErrorUnexpected(cons, "MTPpeerNotifyEvents");
	}
}
inline void MTPpeerNotifyEvents::write(mtpBuffer &/*to*/) const {
	switch (_type) {
	}
}
inline MTPpeerNotifyEvents::MTPpeerNotifyEvents(mtpTypeId type) : _type(type) {
	switch (type) {
		case mtpc_peerNotifyEventsEmpty: break;
		case mtpc_peerNotifyEventsAll: break;
		default: throw mtpErrorBadTypeId(type, "MTPpeerNotifyEvents");
	}
}
inline MTPpeerNotifyEvents MTP_peerNotifyEventsEmpty() {
	return MTPpeerNotifyEvents(mtpc_peerNotifyEventsEmpty);
}
inline MTPpeerNotifyEvents MTP_peerNotifyEventsAll() {
	return MTPpeerNotifyEvents(mtpc_peerNotifyEventsAll);
}

inline uint32 MTPpeerNotifySettings::size() const {
	switch (_type) {
		case mtpc_peerNotifySettings: {
			const MTPDpeerNotifySettings &v(c_peerNotifySettings());
			return v.vmute_until.size() + v.vsound.size() + v.vshow_previews.size() + v.vevents_mask.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPpeerNotifySettings::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPpeerNotifySettings::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_peerNotifySettingsEmpty: _type = cons; break;
		case mtpc_peerNotifySettings: _type = cons; {
			if (!data) setData(new MTPDpeerNotifySettings());
			MTPDpeerNotifySettings &v(_peerNotifySettings());
			v.vmute_until.read(from, end);
			v.vsound.read(from, end);
			v.vshow_previews.read(from, end);
			v.vevents_mask.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPpeerNotifySettings");
	}
}
inline void MTPpeerNotifySettings::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_peerNotifySettings: {
			const MTPDpeerNotifySettings &v(c_peerNotifySettings());
			v.vmute_until.write(to);
			v.vsound.write(to);
			v.vshow_previews.write(to);
			v.vevents_mask.write(to);
		} break;
	}
}
inline MTPpeerNotifySettings::MTPpeerNotifySettings(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_peerNotifySettingsEmpty: break;
		case mtpc_peerNotifySettings: setData(new MTPDpeerNotifySettings()); break;
		default: throw mtpErrorBadTypeId(type, "MTPpeerNotifySettings");
	}
}
inline MTPpeerNotifySettings::MTPpeerNotifySettings(MTPDpeerNotifySettings *_data) : mtpDataOwner(_data), _type(mtpc_peerNotifySettings) {
}
inline MTPpeerNotifySettings MTP_peerNotifySettingsEmpty() {
	return MTPpeerNotifySettings(mtpc_peerNotifySettingsEmpty);
}
inline MTPpeerNotifySettings MTP_peerNotifySettings(MTPint _mute_until, const MTPstring &_sound, MTPBool _show_previews, MTPint _events_mask) {
	return MTPpeerNotifySettings(new MTPDpeerNotifySettings(_mute_until, _sound, _show_previews, _events_mask));
}

inline uint32 MTPwallPaper::size() const {
	switch (_type) {
		case mtpc_wallPaper: {
			const MTPDwallPaper &v(c_wallPaper());
			return v.vid.size() + v.vtitle.size() + v.vsizes.size() + v.vcolor.size();
		}
		case mtpc_wallPaperSolid: {
			const MTPDwallPaperSolid &v(c_wallPaperSolid());
			return v.vid.size() + v.vtitle.size() + v.vbg_color.size() + v.vcolor.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPwallPaper::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPwallPaper::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_wallPaper: _type = cons; {
			if (!data) setData(new MTPDwallPaper());
			MTPDwallPaper &v(_wallPaper());
			v.vid.read(from, end);
			v.vtitle.read(from, end);
			v.vsizes.read(from, end);
			v.vcolor.read(from, end);
		} break;
		case mtpc_wallPaperSolid: _type = cons; {
			if (!data) setData(new MTPDwallPaperSolid());
			MTPDwallPaperSolid &v(_wallPaperSolid());
			v.vid.read(from, end);
			v.vtitle.read(from, end);
			v.vbg_color.read(from, end);
			v.vcolor.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPwallPaper");
	}
}
inline void MTPwallPaper::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_wallPaper: {
			const MTPDwallPaper &v(c_wallPaper());
			v.vid.write(to);
			v.vtitle.write(to);
			v.vsizes.write(to);
			v.vcolor.write(to);
		} break;
		case mtpc_wallPaperSolid: {
			const MTPDwallPaperSolid &v(c_wallPaperSolid());
			v.vid.write(to);
			v.vtitle.write(to);
			v.vbg_color.write(to);
			v.vcolor.write(to);
		} break;
	}
}
inline MTPwallPaper::MTPwallPaper(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_wallPaper: setData(new MTPDwallPaper()); break;
		case mtpc_wallPaperSolid: setData(new MTPDwallPaperSolid()); break;
		default: throw mtpErrorBadTypeId(type, "MTPwallPaper");
	}
}
inline MTPwallPaper::MTPwallPaper(MTPDwallPaper *_data) : mtpDataOwner(_data), _type(mtpc_wallPaper) {
}
inline MTPwallPaper::MTPwallPaper(MTPDwallPaperSolid *_data) : mtpDataOwner(_data), _type(mtpc_wallPaperSolid) {
}
inline MTPwallPaper MTP_wallPaper(MTPint _id, const MTPstring &_title, const MTPVector<MTPPhotoSize> &_sizes, MTPint _color) {
	return MTPwallPaper(new MTPDwallPaper(_id, _title, _sizes, _color));
}
inline MTPwallPaper MTP_wallPaperSolid(MTPint _id, const MTPstring &_title, MTPint _bg_color, MTPint _color) {
	return MTPwallPaper(new MTPDwallPaperSolid(_id, _title, _bg_color, _color));
}

inline MTPuserFull::MTPuserFull() : mtpDataOwner(new MTPDuserFull()) {
}

inline uint32 MTPuserFull::size() const {
	const MTPDuserFull &v(c_userFull());
	return v.vuser.size() + v.vlink.size() + v.vprofile_photo.size() + v.vnotify_settings.size() + v.vblocked.size() + v.vreal_first_name.size() + v.vreal_last_name.size();
}
inline mtpTypeId MTPuserFull::type() const {
	return mtpc_userFull;
}
inline void MTPuserFull::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_userFull) throw mtpErrorUnexpected(cons, "MTPuserFull");

	if (!data) setData(new MTPDuserFull());
	MTPDuserFull &v(_userFull());
	v.vuser.read(from, end);
	v.vlink.read(from, end);
	v.vprofile_photo.read(from, end);
	v.vnotify_settings.read(from, end);
	v.vblocked.read(from, end);
	v.vreal_first_name.read(from, end);
	v.vreal_last_name.read(from, end);
}
inline void MTPuserFull::write(mtpBuffer &to) const {
	const MTPDuserFull &v(c_userFull());
	v.vuser.write(to);
	v.vlink.write(to);
	v.vprofile_photo.write(to);
	v.vnotify_settings.write(to);
	v.vblocked.write(to);
	v.vreal_first_name.write(to);
	v.vreal_last_name.write(to);
}
inline MTPuserFull::MTPuserFull(MTPDuserFull *_data) : mtpDataOwner(_data) {
}
inline MTPuserFull MTP_userFull(const MTPUser &_user, const MTPcontacts_Link &_link, const MTPPhoto &_profile_photo, const MTPPeerNotifySettings &_notify_settings, MTPBool _blocked, const MTPstring &_real_first_name, const MTPstring &_real_last_name) {
	return MTPuserFull(new MTPDuserFull(_user, _link, _profile_photo, _notify_settings, _blocked, _real_first_name, _real_last_name));
}

inline MTPcontact::MTPcontact() : mtpDataOwner(new MTPDcontact()) {
}

inline uint32 MTPcontact::size() const {
	const MTPDcontact &v(c_contact());
	return v.vuser_id.size() + v.vmutual.size();
}
inline mtpTypeId MTPcontact::type() const {
	return mtpc_contact;
}
inline void MTPcontact::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_contact) throw mtpErrorUnexpected(cons, "MTPcontact");

	if (!data) setData(new MTPDcontact());
	MTPDcontact &v(_contact());
	v.vuser_id.read(from, end);
	v.vmutual.read(from, end);
}
inline void MTPcontact::write(mtpBuffer &to) const {
	const MTPDcontact &v(c_contact());
	v.vuser_id.write(to);
	v.vmutual.write(to);
}
inline MTPcontact::MTPcontact(MTPDcontact *_data) : mtpDataOwner(_data) {
}
inline MTPcontact MTP_contact(MTPint _user_id, MTPBool _mutual) {
	return MTPcontact(new MTPDcontact(_user_id, _mutual));
}

inline MTPimportedContact::MTPimportedContact() : mtpDataOwner(new MTPDimportedContact()) {
}

inline uint32 MTPimportedContact::size() const {
	const MTPDimportedContact &v(c_importedContact());
	return v.vuser_id.size() + v.vclient_id.size();
}
inline mtpTypeId MTPimportedContact::type() const {
	return mtpc_importedContact;
}
inline void MTPimportedContact::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_importedContact) throw mtpErrorUnexpected(cons, "MTPimportedContact");

	if (!data) setData(new MTPDimportedContact());
	MTPDimportedContact &v(_importedContact());
	v.vuser_id.read(from, end);
	v.vclient_id.read(from, end);
}
inline void MTPimportedContact::write(mtpBuffer &to) const {
	const MTPDimportedContact &v(c_importedContact());
	v.vuser_id.write(to);
	v.vclient_id.write(to);
}
inline MTPimportedContact::MTPimportedContact(MTPDimportedContact *_data) : mtpDataOwner(_data) {
}
inline MTPimportedContact MTP_importedContact(MTPint _user_id, const MTPlong &_client_id) {
	return MTPimportedContact(new MTPDimportedContact(_user_id, _client_id));
}

inline MTPcontactBlocked::MTPcontactBlocked() : mtpDataOwner(new MTPDcontactBlocked()) {
}

inline uint32 MTPcontactBlocked::size() const {
	const MTPDcontactBlocked &v(c_contactBlocked());
	return v.vuser_id.size() + v.vdate.size();
}
inline mtpTypeId MTPcontactBlocked::type() const {
	return mtpc_contactBlocked;
}
inline void MTPcontactBlocked::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_contactBlocked) throw mtpErrorUnexpected(cons, "MTPcontactBlocked");

	if (!data) setData(new MTPDcontactBlocked());
	MTPDcontactBlocked &v(_contactBlocked());
	v.vuser_id.read(from, end);
	v.vdate.read(from, end);
}
inline void MTPcontactBlocked::write(mtpBuffer &to) const {
	const MTPDcontactBlocked &v(c_contactBlocked());
	v.vuser_id.write(to);
	v.vdate.write(to);
}
inline MTPcontactBlocked::MTPcontactBlocked(MTPDcontactBlocked *_data) : mtpDataOwner(_data) {
}
inline MTPcontactBlocked MTP_contactBlocked(MTPint _user_id, MTPint _date) {
	return MTPcontactBlocked(new MTPDcontactBlocked(_user_id, _date));
}

inline MTPcontactFound::MTPcontactFound() : mtpDataOwner(new MTPDcontactFound()) {
}

inline uint32 MTPcontactFound::size() const {
	const MTPDcontactFound &v(c_contactFound());
	return v.vuser_id.size();
}
inline mtpTypeId MTPcontactFound::type() const {
	return mtpc_contactFound;
}
inline void MTPcontactFound::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_contactFound) throw mtpErrorUnexpected(cons, "MTPcontactFound");

	if (!data) setData(new MTPDcontactFound());
	MTPDcontactFound &v(_contactFound());
	v.vuser_id.read(from, end);
}
inline void MTPcontactFound::write(mtpBuffer &to) const {
	const MTPDcontactFound &v(c_contactFound());
	v.vuser_id.write(to);
}
inline MTPcontactFound::MTPcontactFound(MTPDcontactFound *_data) : mtpDataOwner(_data) {
}
inline MTPcontactFound MTP_contactFound(MTPint _user_id) {
	return MTPcontactFound(new MTPDcontactFound(_user_id));
}

inline MTPcontactSuggested::MTPcontactSuggested() : mtpDataOwner(new MTPDcontactSuggested()) {
}

inline uint32 MTPcontactSuggested::size() const {
	const MTPDcontactSuggested &v(c_contactSuggested());
	return v.vuser_id.size() + v.vmutual_contacts.size();
}
inline mtpTypeId MTPcontactSuggested::type() const {
	return mtpc_contactSuggested;
}
inline void MTPcontactSuggested::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_contactSuggested) throw mtpErrorUnexpected(cons, "MTPcontactSuggested");

	if (!data) setData(new MTPDcontactSuggested());
	MTPDcontactSuggested &v(_contactSuggested());
	v.vuser_id.read(from, end);
	v.vmutual_contacts.read(from, end);
}
inline void MTPcontactSuggested::write(mtpBuffer &to) const {
	const MTPDcontactSuggested &v(c_contactSuggested());
	v.vuser_id.write(to);
	v.vmutual_contacts.write(to);
}
inline MTPcontactSuggested::MTPcontactSuggested(MTPDcontactSuggested *_data) : mtpDataOwner(_data) {
}
inline MTPcontactSuggested MTP_contactSuggested(MTPint _user_id, MTPint _mutual_contacts) {
	return MTPcontactSuggested(new MTPDcontactSuggested(_user_id, _mutual_contacts));
}

inline MTPcontactStatus::MTPcontactStatus() : mtpDataOwner(new MTPDcontactStatus()) {
}

inline uint32 MTPcontactStatus::size() const {
	const MTPDcontactStatus &v(c_contactStatus());
	return v.vuser_id.size() + v.vexpires.size();
}
inline mtpTypeId MTPcontactStatus::type() const {
	return mtpc_contactStatus;
}
inline void MTPcontactStatus::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_contactStatus) throw mtpErrorUnexpected(cons, "MTPcontactStatus");

	if (!data) setData(new MTPDcontactStatus());
	MTPDcontactStatus &v(_contactStatus());
	v.vuser_id.read(from, end);
	v.vexpires.read(from, end);
}
inline void MTPcontactStatus::write(mtpBuffer &to) const {
	const MTPDcontactStatus &v(c_contactStatus());
	v.vuser_id.write(to);
	v.vexpires.write(to);
}
inline MTPcontactStatus::MTPcontactStatus(MTPDcontactStatus *_data) : mtpDataOwner(_data) {
}
inline MTPcontactStatus MTP_contactStatus(MTPint _user_id, MTPint _expires) {
	return MTPcontactStatus(new MTPDcontactStatus(_user_id, _expires));
}

inline MTPchatLocated::MTPchatLocated() : mtpDataOwner(new MTPDchatLocated()) {
}

inline uint32 MTPchatLocated::size() const {
	const MTPDchatLocated &v(c_chatLocated());
	return v.vchat_id.size() + v.vdistance.size();
}
inline mtpTypeId MTPchatLocated::type() const {
	return mtpc_chatLocated;
}
inline void MTPchatLocated::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_chatLocated) throw mtpErrorUnexpected(cons, "MTPchatLocated");

	if (!data) setData(new MTPDchatLocated());
	MTPDchatLocated &v(_chatLocated());
	v.vchat_id.read(from, end);
	v.vdistance.read(from, end);
}
inline void MTPchatLocated::write(mtpBuffer &to) const {
	const MTPDchatLocated &v(c_chatLocated());
	v.vchat_id.write(to);
	v.vdistance.write(to);
}
inline MTPchatLocated::MTPchatLocated(MTPDchatLocated *_data) : mtpDataOwner(_data) {
}
inline MTPchatLocated MTP_chatLocated(MTPint _chat_id, MTPint _distance) {
	return MTPchatLocated(new MTPDchatLocated(_chat_id, _distance));
}

inline uint32 MTPcontacts_foreignLink::size() const {
	switch (_type) {
		case mtpc_contacts_foreignLinkRequested: {
			const MTPDcontacts_foreignLinkRequested &v(c_contacts_foreignLinkRequested());
			return v.vhas_phone.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPcontacts_foreignLink::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPcontacts_foreignLink::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_contacts_foreignLinkUnknown: _type = cons; break;
		case mtpc_contacts_foreignLinkRequested: _type = cons; {
			if (!data) setData(new MTPDcontacts_foreignLinkRequested());
			MTPDcontacts_foreignLinkRequested &v(_contacts_foreignLinkRequested());
			v.vhas_phone.read(from, end);
		} break;
		case mtpc_contacts_foreignLinkMutual: _type = cons; break;
		default: throw mtpErrorUnexpected(cons, "MTPcontacts_foreignLink");
	}
}
inline void MTPcontacts_foreignLink::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_contacts_foreignLinkRequested: {
			const MTPDcontacts_foreignLinkRequested &v(c_contacts_foreignLinkRequested());
			v.vhas_phone.write(to);
		} break;
	}
}
inline MTPcontacts_foreignLink::MTPcontacts_foreignLink(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_contacts_foreignLinkUnknown: break;
		case mtpc_contacts_foreignLinkRequested: setData(new MTPDcontacts_foreignLinkRequested()); break;
		case mtpc_contacts_foreignLinkMutual: break;
		default: throw mtpErrorBadTypeId(type, "MTPcontacts_foreignLink");
	}
}
inline MTPcontacts_foreignLink::MTPcontacts_foreignLink(MTPDcontacts_foreignLinkRequested *_data) : mtpDataOwner(_data), _type(mtpc_contacts_foreignLinkRequested) {
}
inline MTPcontacts_foreignLink MTP_contacts_foreignLinkUnknown() {
	return MTPcontacts_foreignLink(mtpc_contacts_foreignLinkUnknown);
}
inline MTPcontacts_foreignLink MTP_contacts_foreignLinkRequested(MTPBool _has_phone) {
	return MTPcontacts_foreignLink(new MTPDcontacts_foreignLinkRequested(_has_phone));
}
inline MTPcontacts_foreignLink MTP_contacts_foreignLinkMutual() {
	return MTPcontacts_foreignLink(mtpc_contacts_foreignLinkMutual);
}

inline uint32 MTPcontacts_myLink::size() const {
	switch (_type) {
		case mtpc_contacts_myLinkRequested: {
			const MTPDcontacts_myLinkRequested &v(c_contacts_myLinkRequested());
			return v.vcontact.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPcontacts_myLink::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPcontacts_myLink::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_contacts_myLinkEmpty: _type = cons; break;
		case mtpc_contacts_myLinkRequested: _type = cons; {
			if (!data) setData(new MTPDcontacts_myLinkRequested());
			MTPDcontacts_myLinkRequested &v(_contacts_myLinkRequested());
			v.vcontact.read(from, end);
		} break;
		case mtpc_contacts_myLinkContact: _type = cons; break;
		default: throw mtpErrorUnexpected(cons, "MTPcontacts_myLink");
	}
}
inline void MTPcontacts_myLink::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_contacts_myLinkRequested: {
			const MTPDcontacts_myLinkRequested &v(c_contacts_myLinkRequested());
			v.vcontact.write(to);
		} break;
	}
}
inline MTPcontacts_myLink::MTPcontacts_myLink(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_contacts_myLinkEmpty: break;
		case mtpc_contacts_myLinkRequested: setData(new MTPDcontacts_myLinkRequested()); break;
		case mtpc_contacts_myLinkContact: break;
		default: throw mtpErrorBadTypeId(type, "MTPcontacts_myLink");
	}
}
inline MTPcontacts_myLink::MTPcontacts_myLink(MTPDcontacts_myLinkRequested *_data) : mtpDataOwner(_data), _type(mtpc_contacts_myLinkRequested) {
}
inline MTPcontacts_myLink MTP_contacts_myLinkEmpty() {
	return MTPcontacts_myLink(mtpc_contacts_myLinkEmpty);
}
inline MTPcontacts_myLink MTP_contacts_myLinkRequested(MTPBool _contact) {
	return MTPcontacts_myLink(new MTPDcontacts_myLinkRequested(_contact));
}
inline MTPcontacts_myLink MTP_contacts_myLinkContact() {
	return MTPcontacts_myLink(mtpc_contacts_myLinkContact);
}

inline MTPcontacts_link::MTPcontacts_link() : mtpDataOwner(new MTPDcontacts_link()) {
}

inline uint32 MTPcontacts_link::size() const {
	const MTPDcontacts_link &v(c_contacts_link());
	return v.vmy_link.size() + v.vforeign_link.size() + v.vuser.size();
}
inline mtpTypeId MTPcontacts_link::type() const {
	return mtpc_contacts_link;
}
inline void MTPcontacts_link::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_contacts_link) throw mtpErrorUnexpected(cons, "MTPcontacts_link");

	if (!data) setData(new MTPDcontacts_link());
	MTPDcontacts_link &v(_contacts_link());
	v.vmy_link.read(from, end);
	v.vforeign_link.read(from, end);
	v.vuser.read(from, end);
}
inline void MTPcontacts_link::write(mtpBuffer &to) const {
	const MTPDcontacts_link &v(c_contacts_link());
	v.vmy_link.write(to);
	v.vforeign_link.write(to);
	v.vuser.write(to);
}
inline MTPcontacts_link::MTPcontacts_link(MTPDcontacts_link *_data) : mtpDataOwner(_data) {
}
inline MTPcontacts_link MTP_contacts_link(const MTPcontacts_MyLink &_my_link, const MTPcontacts_ForeignLink &_foreign_link, const MTPUser &_user) {
	return MTPcontacts_link(new MTPDcontacts_link(_my_link, _foreign_link, _user));
}

inline uint32 MTPcontacts_contacts::size() const {
	switch (_type) {
		case mtpc_contacts_contacts: {
			const MTPDcontacts_contacts &v(c_contacts_contacts());
			return v.vcontacts.size() + v.vusers.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPcontacts_contacts::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPcontacts_contacts::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_contacts_contacts: _type = cons; {
			if (!data) setData(new MTPDcontacts_contacts());
			MTPDcontacts_contacts &v(_contacts_contacts());
			v.vcontacts.read(from, end);
			v.vusers.read(from, end);
		} break;
		case mtpc_contacts_contactsNotModified: _type = cons; break;
		default: throw mtpErrorUnexpected(cons, "MTPcontacts_contacts");
	}
}
inline void MTPcontacts_contacts::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_contacts_contacts: {
			const MTPDcontacts_contacts &v(c_contacts_contacts());
			v.vcontacts.write(to);
			v.vusers.write(to);
		} break;
	}
}
inline MTPcontacts_contacts::MTPcontacts_contacts(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_contacts_contacts: setData(new MTPDcontacts_contacts()); break;
		case mtpc_contacts_contactsNotModified: break;
		default: throw mtpErrorBadTypeId(type, "MTPcontacts_contacts");
	}
}
inline MTPcontacts_contacts::MTPcontacts_contacts(MTPDcontacts_contacts *_data) : mtpDataOwner(_data), _type(mtpc_contacts_contacts) {
}
inline MTPcontacts_contacts MTP_contacts_contacts(const MTPVector<MTPContact> &_contacts, const MTPVector<MTPUser> &_users) {
	return MTPcontacts_contacts(new MTPDcontacts_contacts(_contacts, _users));
}
inline MTPcontacts_contacts MTP_contacts_contactsNotModified() {
	return MTPcontacts_contacts(mtpc_contacts_contactsNotModified);
}

inline MTPcontacts_importedContacts::MTPcontacts_importedContacts() : mtpDataOwner(new MTPDcontacts_importedContacts()) {
}

inline uint32 MTPcontacts_importedContacts::size() const {
	const MTPDcontacts_importedContacts &v(c_contacts_importedContacts());
	return v.vimported.size() + v.vretry_contacts.size() + v.vusers.size();
}
inline mtpTypeId MTPcontacts_importedContacts::type() const {
	return mtpc_contacts_importedContacts;
}
inline void MTPcontacts_importedContacts::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_contacts_importedContacts) throw mtpErrorUnexpected(cons, "MTPcontacts_importedContacts");

	if (!data) setData(new MTPDcontacts_importedContacts());
	MTPDcontacts_importedContacts &v(_contacts_importedContacts());
	v.vimported.read(from, end);
	v.vretry_contacts.read(from, end);
	v.vusers.read(from, end);
}
inline void MTPcontacts_importedContacts::write(mtpBuffer &to) const {
	const MTPDcontacts_importedContacts &v(c_contacts_importedContacts());
	v.vimported.write(to);
	v.vretry_contacts.write(to);
	v.vusers.write(to);
}
inline MTPcontacts_importedContacts::MTPcontacts_importedContacts(MTPDcontacts_importedContacts *_data) : mtpDataOwner(_data) {
}
inline MTPcontacts_importedContacts MTP_contacts_importedContacts(const MTPVector<MTPImportedContact> &_imported, const MTPVector<MTPlong> &_retry_contacts, const MTPVector<MTPUser> &_users) {
	return MTPcontacts_importedContacts(new MTPDcontacts_importedContacts(_imported, _retry_contacts, _users));
}

inline uint32 MTPcontacts_blocked::size() const {
	switch (_type) {
		case mtpc_contacts_blocked: {
			const MTPDcontacts_blocked &v(c_contacts_blocked());
			return v.vblocked.size() + v.vusers.size();
		}
		case mtpc_contacts_blockedSlice: {
			const MTPDcontacts_blockedSlice &v(c_contacts_blockedSlice());
			return v.vcount.size() + v.vblocked.size() + v.vusers.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPcontacts_blocked::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPcontacts_blocked::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_contacts_blocked: _type = cons; {
			if (!data) setData(new MTPDcontacts_blocked());
			MTPDcontacts_blocked &v(_contacts_blocked());
			v.vblocked.read(from, end);
			v.vusers.read(from, end);
		} break;
		case mtpc_contacts_blockedSlice: _type = cons; {
			if (!data) setData(new MTPDcontacts_blockedSlice());
			MTPDcontacts_blockedSlice &v(_contacts_blockedSlice());
			v.vcount.read(from, end);
			v.vblocked.read(from, end);
			v.vusers.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPcontacts_blocked");
	}
}
inline void MTPcontacts_blocked::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_contacts_blocked: {
			const MTPDcontacts_blocked &v(c_contacts_blocked());
			v.vblocked.write(to);
			v.vusers.write(to);
		} break;
		case mtpc_contacts_blockedSlice: {
			const MTPDcontacts_blockedSlice &v(c_contacts_blockedSlice());
			v.vcount.write(to);
			v.vblocked.write(to);
			v.vusers.write(to);
		} break;
	}
}
inline MTPcontacts_blocked::MTPcontacts_blocked(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_contacts_blocked: setData(new MTPDcontacts_blocked()); break;
		case mtpc_contacts_blockedSlice: setData(new MTPDcontacts_blockedSlice()); break;
		default: throw mtpErrorBadTypeId(type, "MTPcontacts_blocked");
	}
}
inline MTPcontacts_blocked::MTPcontacts_blocked(MTPDcontacts_blocked *_data) : mtpDataOwner(_data), _type(mtpc_contacts_blocked) {
}
inline MTPcontacts_blocked::MTPcontacts_blocked(MTPDcontacts_blockedSlice *_data) : mtpDataOwner(_data), _type(mtpc_contacts_blockedSlice) {
}
inline MTPcontacts_blocked MTP_contacts_blocked(const MTPVector<MTPContactBlocked> &_blocked, const MTPVector<MTPUser> &_users) {
	return MTPcontacts_blocked(new MTPDcontacts_blocked(_blocked, _users));
}
inline MTPcontacts_blocked MTP_contacts_blockedSlice(MTPint _count, const MTPVector<MTPContactBlocked> &_blocked, const MTPVector<MTPUser> &_users) {
	return MTPcontacts_blocked(new MTPDcontacts_blockedSlice(_count, _blocked, _users));
}

inline MTPcontacts_found::MTPcontacts_found() : mtpDataOwner(new MTPDcontacts_found()) {
}

inline uint32 MTPcontacts_found::size() const {
	const MTPDcontacts_found &v(c_contacts_found());
	return v.vresults.size() + v.vusers.size();
}
inline mtpTypeId MTPcontacts_found::type() const {
	return mtpc_contacts_found;
}
inline void MTPcontacts_found::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_contacts_found) throw mtpErrorUnexpected(cons, "MTPcontacts_found");

	if (!data) setData(new MTPDcontacts_found());
	MTPDcontacts_found &v(_contacts_found());
	v.vresults.read(from, end);
	v.vusers.read(from, end);
}
inline void MTPcontacts_found::write(mtpBuffer &to) const {
	const MTPDcontacts_found &v(c_contacts_found());
	v.vresults.write(to);
	v.vusers.write(to);
}
inline MTPcontacts_found::MTPcontacts_found(MTPDcontacts_found *_data) : mtpDataOwner(_data) {
}
inline MTPcontacts_found MTP_contacts_found(const MTPVector<MTPContactFound> &_results, const MTPVector<MTPUser> &_users) {
	return MTPcontacts_found(new MTPDcontacts_found(_results, _users));
}

inline MTPcontacts_suggested::MTPcontacts_suggested() : mtpDataOwner(new MTPDcontacts_suggested()) {
}

inline uint32 MTPcontacts_suggested::size() const {
	const MTPDcontacts_suggested &v(c_contacts_suggested());
	return v.vresults.size() + v.vusers.size();
}
inline mtpTypeId MTPcontacts_suggested::type() const {
	return mtpc_contacts_suggested;
}
inline void MTPcontacts_suggested::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_contacts_suggested) throw mtpErrorUnexpected(cons, "MTPcontacts_suggested");

	if (!data) setData(new MTPDcontacts_suggested());
	MTPDcontacts_suggested &v(_contacts_suggested());
	v.vresults.read(from, end);
	v.vusers.read(from, end);
}
inline void MTPcontacts_suggested::write(mtpBuffer &to) const {
	const MTPDcontacts_suggested &v(c_contacts_suggested());
	v.vresults.write(to);
	v.vusers.write(to);
}
inline MTPcontacts_suggested::MTPcontacts_suggested(MTPDcontacts_suggested *_data) : mtpDataOwner(_data) {
}
inline MTPcontacts_suggested MTP_contacts_suggested(const MTPVector<MTPContactSuggested> &_results, const MTPVector<MTPUser> &_users) {
	return MTPcontacts_suggested(new MTPDcontacts_suggested(_results, _users));
}

inline uint32 MTPmessages_dialogs::size() const {
	switch (_type) {
		case mtpc_messages_dialogs: {
			const MTPDmessages_dialogs &v(c_messages_dialogs());
			return v.vdialogs.size() + v.vmessages.size() + v.vchats.size() + v.vusers.size();
		}
		case mtpc_messages_dialogsSlice: {
			const MTPDmessages_dialogsSlice &v(c_messages_dialogsSlice());
			return v.vcount.size() + v.vdialogs.size() + v.vmessages.size() + v.vchats.size() + v.vusers.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPmessages_dialogs::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPmessages_dialogs::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_messages_dialogs: _type = cons; {
			if (!data) setData(new MTPDmessages_dialogs());
			MTPDmessages_dialogs &v(_messages_dialogs());
			v.vdialogs.read(from, end);
			v.vmessages.read(from, end);
			v.vchats.read(from, end);
			v.vusers.read(from, end);
		} break;
		case mtpc_messages_dialogsSlice: _type = cons; {
			if (!data) setData(new MTPDmessages_dialogsSlice());
			MTPDmessages_dialogsSlice &v(_messages_dialogsSlice());
			v.vcount.read(from, end);
			v.vdialogs.read(from, end);
			v.vmessages.read(from, end);
			v.vchats.read(from, end);
			v.vusers.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPmessages_dialogs");
	}
}
inline void MTPmessages_dialogs::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_messages_dialogs: {
			const MTPDmessages_dialogs &v(c_messages_dialogs());
			v.vdialogs.write(to);
			v.vmessages.write(to);
			v.vchats.write(to);
			v.vusers.write(to);
		} break;
		case mtpc_messages_dialogsSlice: {
			const MTPDmessages_dialogsSlice &v(c_messages_dialogsSlice());
			v.vcount.write(to);
			v.vdialogs.write(to);
			v.vmessages.write(to);
			v.vchats.write(to);
			v.vusers.write(to);
		} break;
	}
}
inline MTPmessages_dialogs::MTPmessages_dialogs(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_messages_dialogs: setData(new MTPDmessages_dialogs()); break;
		case mtpc_messages_dialogsSlice: setData(new MTPDmessages_dialogsSlice()); break;
		default: throw mtpErrorBadTypeId(type, "MTPmessages_dialogs");
	}
}
inline MTPmessages_dialogs::MTPmessages_dialogs(MTPDmessages_dialogs *_data) : mtpDataOwner(_data), _type(mtpc_messages_dialogs) {
}
inline MTPmessages_dialogs::MTPmessages_dialogs(MTPDmessages_dialogsSlice *_data) : mtpDataOwner(_data), _type(mtpc_messages_dialogsSlice) {
}
inline MTPmessages_dialogs MTP_messages_dialogs(const MTPVector<MTPDialog> &_dialogs, const MTPVector<MTPMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users) {
	return MTPmessages_dialogs(new MTPDmessages_dialogs(_dialogs, _messages, _chats, _users));
}
inline MTPmessages_dialogs MTP_messages_dialogsSlice(MTPint _count, const MTPVector<MTPDialog> &_dialogs, const MTPVector<MTPMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users) {
	return MTPmessages_dialogs(new MTPDmessages_dialogsSlice(_count, _dialogs, _messages, _chats, _users));
}

inline uint32 MTPmessages_messages::size() const {
	switch (_type) {
		case mtpc_messages_messages: {
			const MTPDmessages_messages &v(c_messages_messages());
			return v.vmessages.size() + v.vchats.size() + v.vusers.size();
		}
		case mtpc_messages_messagesSlice: {
			const MTPDmessages_messagesSlice &v(c_messages_messagesSlice());
			return v.vcount.size() + v.vmessages.size() + v.vchats.size() + v.vusers.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPmessages_messages::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPmessages_messages::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_messages_messages: _type = cons; {
			if (!data) setData(new MTPDmessages_messages());
			MTPDmessages_messages &v(_messages_messages());
			v.vmessages.read(from, end);
			v.vchats.read(from, end);
			v.vusers.read(from, end);
		} break;
		case mtpc_messages_messagesSlice: _type = cons; {
			if (!data) setData(new MTPDmessages_messagesSlice());
			MTPDmessages_messagesSlice &v(_messages_messagesSlice());
			v.vcount.read(from, end);
			v.vmessages.read(from, end);
			v.vchats.read(from, end);
			v.vusers.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPmessages_messages");
	}
}
inline void MTPmessages_messages::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_messages_messages: {
			const MTPDmessages_messages &v(c_messages_messages());
			v.vmessages.write(to);
			v.vchats.write(to);
			v.vusers.write(to);
		} break;
		case mtpc_messages_messagesSlice: {
			const MTPDmessages_messagesSlice &v(c_messages_messagesSlice());
			v.vcount.write(to);
			v.vmessages.write(to);
			v.vchats.write(to);
			v.vusers.write(to);
		} break;
	}
}
inline MTPmessages_messages::MTPmessages_messages(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_messages_messages: setData(new MTPDmessages_messages()); break;
		case mtpc_messages_messagesSlice: setData(new MTPDmessages_messagesSlice()); break;
		default: throw mtpErrorBadTypeId(type, "MTPmessages_messages");
	}
}
inline MTPmessages_messages::MTPmessages_messages(MTPDmessages_messages *_data) : mtpDataOwner(_data), _type(mtpc_messages_messages) {
}
inline MTPmessages_messages::MTPmessages_messages(MTPDmessages_messagesSlice *_data) : mtpDataOwner(_data), _type(mtpc_messages_messagesSlice) {
}
inline MTPmessages_messages MTP_messages_messages(const MTPVector<MTPMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users) {
	return MTPmessages_messages(new MTPDmessages_messages(_messages, _chats, _users));
}
inline MTPmessages_messages MTP_messages_messagesSlice(MTPint _count, const MTPVector<MTPMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users) {
	return MTPmessages_messages(new MTPDmessages_messagesSlice(_count, _messages, _chats, _users));
}

inline uint32 MTPmessages_message::size() const {
	switch (_type) {
		case mtpc_messages_message: {
			const MTPDmessages_message &v(c_messages_message());
			return v.vmessage.size() + v.vchats.size() + v.vusers.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPmessages_message::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPmessages_message::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_messages_messageEmpty: _type = cons; break;
		case mtpc_messages_message: _type = cons; {
			if (!data) setData(new MTPDmessages_message());
			MTPDmessages_message &v(_messages_message());
			v.vmessage.read(from, end);
			v.vchats.read(from, end);
			v.vusers.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPmessages_message");
	}
}
inline void MTPmessages_message::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_messages_message: {
			const MTPDmessages_message &v(c_messages_message());
			v.vmessage.write(to);
			v.vchats.write(to);
			v.vusers.write(to);
		} break;
	}
}
inline MTPmessages_message::MTPmessages_message(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_messages_messageEmpty: break;
		case mtpc_messages_message: setData(new MTPDmessages_message()); break;
		default: throw mtpErrorBadTypeId(type, "MTPmessages_message");
	}
}
inline MTPmessages_message::MTPmessages_message(MTPDmessages_message *_data) : mtpDataOwner(_data), _type(mtpc_messages_message) {
}
inline MTPmessages_message MTP_messages_messageEmpty() {
	return MTPmessages_message(mtpc_messages_messageEmpty);
}
inline MTPmessages_message MTP_messages_message(const MTPMessage &_message, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users) {
	return MTPmessages_message(new MTPDmessages_message(_message, _chats, _users));
}

inline uint32 MTPmessages_statedMessages::size() const {
	switch (_type) {
		case mtpc_messages_statedMessages: {
			const MTPDmessages_statedMessages &v(c_messages_statedMessages());
			return v.vmessages.size() + v.vchats.size() + v.vusers.size() + v.vpts.size() + v.vseq.size();
		}
		case mtpc_messages_statedMessagesLinks: {
			const MTPDmessages_statedMessagesLinks &v(c_messages_statedMessagesLinks());
			return v.vmessages.size() + v.vchats.size() + v.vusers.size() + v.vlinks.size() + v.vpts.size() + v.vseq.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPmessages_statedMessages::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPmessages_statedMessages::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_messages_statedMessages: _type = cons; {
			if (!data) setData(new MTPDmessages_statedMessages());
			MTPDmessages_statedMessages &v(_messages_statedMessages());
			v.vmessages.read(from, end);
			v.vchats.read(from, end);
			v.vusers.read(from, end);
			v.vpts.read(from, end);
			v.vseq.read(from, end);
		} break;
		case mtpc_messages_statedMessagesLinks: _type = cons; {
			if (!data) setData(new MTPDmessages_statedMessagesLinks());
			MTPDmessages_statedMessagesLinks &v(_messages_statedMessagesLinks());
			v.vmessages.read(from, end);
			v.vchats.read(from, end);
			v.vusers.read(from, end);
			v.vlinks.read(from, end);
			v.vpts.read(from, end);
			v.vseq.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPmessages_statedMessages");
	}
}
inline void MTPmessages_statedMessages::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_messages_statedMessages: {
			const MTPDmessages_statedMessages &v(c_messages_statedMessages());
			v.vmessages.write(to);
			v.vchats.write(to);
			v.vusers.write(to);
			v.vpts.write(to);
			v.vseq.write(to);
		} break;
		case mtpc_messages_statedMessagesLinks: {
			const MTPDmessages_statedMessagesLinks &v(c_messages_statedMessagesLinks());
			v.vmessages.write(to);
			v.vchats.write(to);
			v.vusers.write(to);
			v.vlinks.write(to);
			v.vpts.write(to);
			v.vseq.write(to);
		} break;
	}
}
inline MTPmessages_statedMessages::MTPmessages_statedMessages(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_messages_statedMessages: setData(new MTPDmessages_statedMessages()); break;
		case mtpc_messages_statedMessagesLinks: setData(new MTPDmessages_statedMessagesLinks()); break;
		default: throw mtpErrorBadTypeId(type, "MTPmessages_statedMessages");
	}
}
inline MTPmessages_statedMessages::MTPmessages_statedMessages(MTPDmessages_statedMessages *_data) : mtpDataOwner(_data), _type(mtpc_messages_statedMessages) {
}
inline MTPmessages_statedMessages::MTPmessages_statedMessages(MTPDmessages_statedMessagesLinks *_data) : mtpDataOwner(_data), _type(mtpc_messages_statedMessagesLinks) {
}
inline MTPmessages_statedMessages MTP_messages_statedMessages(const MTPVector<MTPMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users, MTPint _pts, MTPint _seq) {
	return MTPmessages_statedMessages(new MTPDmessages_statedMessages(_messages, _chats, _users, _pts, _seq));
}
inline MTPmessages_statedMessages MTP_messages_statedMessagesLinks(const MTPVector<MTPMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users, const MTPVector<MTPcontacts_Link> &_links, MTPint _pts, MTPint _seq) {
	return MTPmessages_statedMessages(new MTPDmessages_statedMessagesLinks(_messages, _chats, _users, _links, _pts, _seq));
}

inline uint32 MTPmessages_statedMessage::size() const {
	switch (_type) {
		case mtpc_messages_statedMessage: {
			const MTPDmessages_statedMessage &v(c_messages_statedMessage());
			return v.vmessage.size() + v.vchats.size() + v.vusers.size() + v.vpts.size() + v.vseq.size();
		}
		case mtpc_messages_statedMessageLink: {
			const MTPDmessages_statedMessageLink &v(c_messages_statedMessageLink());
			return v.vmessage.size() + v.vchats.size() + v.vusers.size() + v.vlinks.size() + v.vpts.size() + v.vseq.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPmessages_statedMessage::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPmessages_statedMessage::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_messages_statedMessage: _type = cons; {
			if (!data) setData(new MTPDmessages_statedMessage());
			MTPDmessages_statedMessage &v(_messages_statedMessage());
			v.vmessage.read(from, end);
			v.vchats.read(from, end);
			v.vusers.read(from, end);
			v.vpts.read(from, end);
			v.vseq.read(from, end);
		} break;
		case mtpc_messages_statedMessageLink: _type = cons; {
			if (!data) setData(new MTPDmessages_statedMessageLink());
			MTPDmessages_statedMessageLink &v(_messages_statedMessageLink());
			v.vmessage.read(from, end);
			v.vchats.read(from, end);
			v.vusers.read(from, end);
			v.vlinks.read(from, end);
			v.vpts.read(from, end);
			v.vseq.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPmessages_statedMessage");
	}
}
inline void MTPmessages_statedMessage::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_messages_statedMessage: {
			const MTPDmessages_statedMessage &v(c_messages_statedMessage());
			v.vmessage.write(to);
			v.vchats.write(to);
			v.vusers.write(to);
			v.vpts.write(to);
			v.vseq.write(to);
		} break;
		case mtpc_messages_statedMessageLink: {
			const MTPDmessages_statedMessageLink &v(c_messages_statedMessageLink());
			v.vmessage.write(to);
			v.vchats.write(to);
			v.vusers.write(to);
			v.vlinks.write(to);
			v.vpts.write(to);
			v.vseq.write(to);
		} break;
	}
}
inline MTPmessages_statedMessage::MTPmessages_statedMessage(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_messages_statedMessage: setData(new MTPDmessages_statedMessage()); break;
		case mtpc_messages_statedMessageLink: setData(new MTPDmessages_statedMessageLink()); break;
		default: throw mtpErrorBadTypeId(type, "MTPmessages_statedMessage");
	}
}
inline MTPmessages_statedMessage::MTPmessages_statedMessage(MTPDmessages_statedMessage *_data) : mtpDataOwner(_data), _type(mtpc_messages_statedMessage) {
}
inline MTPmessages_statedMessage::MTPmessages_statedMessage(MTPDmessages_statedMessageLink *_data) : mtpDataOwner(_data), _type(mtpc_messages_statedMessageLink) {
}
inline MTPmessages_statedMessage MTP_messages_statedMessage(const MTPMessage &_message, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users, MTPint _pts, MTPint _seq) {
	return MTPmessages_statedMessage(new MTPDmessages_statedMessage(_message, _chats, _users, _pts, _seq));
}
inline MTPmessages_statedMessage MTP_messages_statedMessageLink(const MTPMessage &_message, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users, const MTPVector<MTPcontacts_Link> &_links, MTPint _pts, MTPint _seq) {
	return MTPmessages_statedMessage(new MTPDmessages_statedMessageLink(_message, _chats, _users, _links, _pts, _seq));
}

inline uint32 MTPmessages_sentMessage::size() const {
	switch (_type) {
		case mtpc_messages_sentMessage: {
			const MTPDmessages_sentMessage &v(c_messages_sentMessage());
			return v.vid.size() + v.vdate.size() + v.vpts.size() + v.vseq.size();
		}
		case mtpc_messages_sentMessageLink: {
			const MTPDmessages_sentMessageLink &v(c_messages_sentMessageLink());
			return v.vid.size() + v.vdate.size() + v.vpts.size() + v.vseq.size() + v.vlinks.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPmessages_sentMessage::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPmessages_sentMessage::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_messages_sentMessage: _type = cons; {
			if (!data) setData(new MTPDmessages_sentMessage());
			MTPDmessages_sentMessage &v(_messages_sentMessage());
			v.vid.read(from, end);
			v.vdate.read(from, end);
			v.vpts.read(from, end);
			v.vseq.read(from, end);
		} break;
		case mtpc_messages_sentMessageLink: _type = cons; {
			if (!data) setData(new MTPDmessages_sentMessageLink());
			MTPDmessages_sentMessageLink &v(_messages_sentMessageLink());
			v.vid.read(from, end);
			v.vdate.read(from, end);
			v.vpts.read(from, end);
			v.vseq.read(from, end);
			v.vlinks.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPmessages_sentMessage");
	}
}
inline void MTPmessages_sentMessage::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_messages_sentMessage: {
			const MTPDmessages_sentMessage &v(c_messages_sentMessage());
			v.vid.write(to);
			v.vdate.write(to);
			v.vpts.write(to);
			v.vseq.write(to);
		} break;
		case mtpc_messages_sentMessageLink: {
			const MTPDmessages_sentMessageLink &v(c_messages_sentMessageLink());
			v.vid.write(to);
			v.vdate.write(to);
			v.vpts.write(to);
			v.vseq.write(to);
			v.vlinks.write(to);
		} break;
	}
}
inline MTPmessages_sentMessage::MTPmessages_sentMessage(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_messages_sentMessage: setData(new MTPDmessages_sentMessage()); break;
		case mtpc_messages_sentMessageLink: setData(new MTPDmessages_sentMessageLink()); break;
		default: throw mtpErrorBadTypeId(type, "MTPmessages_sentMessage");
	}
}
inline MTPmessages_sentMessage::MTPmessages_sentMessage(MTPDmessages_sentMessage *_data) : mtpDataOwner(_data), _type(mtpc_messages_sentMessage) {
}
inline MTPmessages_sentMessage::MTPmessages_sentMessage(MTPDmessages_sentMessageLink *_data) : mtpDataOwner(_data), _type(mtpc_messages_sentMessageLink) {
}
inline MTPmessages_sentMessage MTP_messages_sentMessage(MTPint _id, MTPint _date, MTPint _pts, MTPint _seq) {
	return MTPmessages_sentMessage(new MTPDmessages_sentMessage(_id, _date, _pts, _seq));
}
inline MTPmessages_sentMessage MTP_messages_sentMessageLink(MTPint _id, MTPint _date, MTPint _pts, MTPint _seq, const MTPVector<MTPcontacts_Link> &_links) {
	return MTPmessages_sentMessage(new MTPDmessages_sentMessageLink(_id, _date, _pts, _seq, _links));
}

inline MTPmessages_chat::MTPmessages_chat() : mtpDataOwner(new MTPDmessages_chat()) {
}

inline uint32 MTPmessages_chat::size() const {
	const MTPDmessages_chat &v(c_messages_chat());
	return v.vchat.size() + v.vusers.size();
}
inline mtpTypeId MTPmessages_chat::type() const {
	return mtpc_messages_chat;
}
inline void MTPmessages_chat::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_messages_chat) throw mtpErrorUnexpected(cons, "MTPmessages_chat");

	if (!data) setData(new MTPDmessages_chat());
	MTPDmessages_chat &v(_messages_chat());
	v.vchat.read(from, end);
	v.vusers.read(from, end);
}
inline void MTPmessages_chat::write(mtpBuffer &to) const {
	const MTPDmessages_chat &v(c_messages_chat());
	v.vchat.write(to);
	v.vusers.write(to);
}
inline MTPmessages_chat::MTPmessages_chat(MTPDmessages_chat *_data) : mtpDataOwner(_data) {
}
inline MTPmessages_chat MTP_messages_chat(const MTPChat &_chat, const MTPVector<MTPUser> &_users) {
	return MTPmessages_chat(new MTPDmessages_chat(_chat, _users));
}

inline MTPmessages_chats::MTPmessages_chats() : mtpDataOwner(new MTPDmessages_chats()) {
}

inline uint32 MTPmessages_chats::size() const {
	const MTPDmessages_chats &v(c_messages_chats());
	return v.vchats.size() + v.vusers.size();
}
inline mtpTypeId MTPmessages_chats::type() const {
	return mtpc_messages_chats;
}
inline void MTPmessages_chats::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_messages_chats) throw mtpErrorUnexpected(cons, "MTPmessages_chats");

	if (!data) setData(new MTPDmessages_chats());
	MTPDmessages_chats &v(_messages_chats());
	v.vchats.read(from, end);
	v.vusers.read(from, end);
}
inline void MTPmessages_chats::write(mtpBuffer &to) const {
	const MTPDmessages_chats &v(c_messages_chats());
	v.vchats.write(to);
	v.vusers.write(to);
}
inline MTPmessages_chats::MTPmessages_chats(MTPDmessages_chats *_data) : mtpDataOwner(_data) {
}
inline MTPmessages_chats MTP_messages_chats(const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users) {
	return MTPmessages_chats(new MTPDmessages_chats(_chats, _users));
}

inline MTPmessages_chatFull::MTPmessages_chatFull() : mtpDataOwner(new MTPDmessages_chatFull()) {
}

inline uint32 MTPmessages_chatFull::size() const {
	const MTPDmessages_chatFull &v(c_messages_chatFull());
	return v.vfull_chat.size() + v.vchats.size() + v.vusers.size();
}
inline mtpTypeId MTPmessages_chatFull::type() const {
	return mtpc_messages_chatFull;
}
inline void MTPmessages_chatFull::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_messages_chatFull) throw mtpErrorUnexpected(cons, "MTPmessages_chatFull");

	if (!data) setData(new MTPDmessages_chatFull());
	MTPDmessages_chatFull &v(_messages_chatFull());
	v.vfull_chat.read(from, end);
	v.vchats.read(from, end);
	v.vusers.read(from, end);
}
inline void MTPmessages_chatFull::write(mtpBuffer &to) const {
	const MTPDmessages_chatFull &v(c_messages_chatFull());
	v.vfull_chat.write(to);
	v.vchats.write(to);
	v.vusers.write(to);
}
inline MTPmessages_chatFull::MTPmessages_chatFull(MTPDmessages_chatFull *_data) : mtpDataOwner(_data) {
}
inline MTPmessages_chatFull MTP_messages_chatFull(const MTPChatFull &_full_chat, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users) {
	return MTPmessages_chatFull(new MTPDmessages_chatFull(_full_chat, _chats, _users));
}

inline MTPmessages_affectedHistory::MTPmessages_affectedHistory() : mtpDataOwner(new MTPDmessages_affectedHistory()) {
}

inline uint32 MTPmessages_affectedHistory::size() const {
	const MTPDmessages_affectedHistory &v(c_messages_affectedHistory());
	return v.vpts.size() + v.vseq.size() + v.voffset.size();
}
inline mtpTypeId MTPmessages_affectedHistory::type() const {
	return mtpc_messages_affectedHistory;
}
inline void MTPmessages_affectedHistory::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_messages_affectedHistory) throw mtpErrorUnexpected(cons, "MTPmessages_affectedHistory");

	if (!data) setData(new MTPDmessages_affectedHistory());
	MTPDmessages_affectedHistory &v(_messages_affectedHistory());
	v.vpts.read(from, end);
	v.vseq.read(from, end);
	v.voffset.read(from, end);
}
inline void MTPmessages_affectedHistory::write(mtpBuffer &to) const {
	const MTPDmessages_affectedHistory &v(c_messages_affectedHistory());
	v.vpts.write(to);
	v.vseq.write(to);
	v.voffset.write(to);
}
inline MTPmessages_affectedHistory::MTPmessages_affectedHistory(MTPDmessages_affectedHistory *_data) : mtpDataOwner(_data) {
}
inline MTPmessages_affectedHistory MTP_messages_affectedHistory(MTPint _pts, MTPint _seq, MTPint _offset) {
	return MTPmessages_affectedHistory(new MTPDmessages_affectedHistory(_pts, _seq, _offset));
}

inline uint32 MTPmessagesFilter::size() const {
	return 0;
}
inline mtpTypeId MTPmessagesFilter::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPmessagesFilter::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	switch (cons) {
		case mtpc_inputMessagesFilterEmpty: _type = cons; break;
		case mtpc_inputMessagesFilterPhotos: _type = cons; break;
		case mtpc_inputMessagesFilterVideo: _type = cons; break;
		case mtpc_inputMessagesFilterPhotoVideo: _type = cons; break;
		case mtpc_inputMessagesFilterDocument: _type = cons; break;
		default: throw mtpErrorUnexpected(cons, "MTPmessagesFilter");
	}
}
inline void MTPmessagesFilter::write(mtpBuffer &/*to*/) const {
	switch (_type) {
	}
}
inline MTPmessagesFilter::MTPmessagesFilter(mtpTypeId type) : _type(type) {
	switch (type) {
		case mtpc_inputMessagesFilterEmpty: break;
		case mtpc_inputMessagesFilterPhotos: break;
		case mtpc_inputMessagesFilterVideo: break;
		case mtpc_inputMessagesFilterPhotoVideo: break;
		case mtpc_inputMessagesFilterDocument: break;
		default: throw mtpErrorBadTypeId(type, "MTPmessagesFilter");
	}
}
inline MTPmessagesFilter MTP_inputMessagesFilterEmpty() {
	return MTPmessagesFilter(mtpc_inputMessagesFilterEmpty);
}
inline MTPmessagesFilter MTP_inputMessagesFilterPhotos() {
	return MTPmessagesFilter(mtpc_inputMessagesFilterPhotos);
}
inline MTPmessagesFilter MTP_inputMessagesFilterVideo() {
	return MTPmessagesFilter(mtpc_inputMessagesFilterVideo);
}
inline MTPmessagesFilter MTP_inputMessagesFilterPhotoVideo() {
	return MTPmessagesFilter(mtpc_inputMessagesFilterPhotoVideo);
}
inline MTPmessagesFilter MTP_inputMessagesFilterDocument() {
	return MTPmessagesFilter(mtpc_inputMessagesFilterDocument);
}

inline uint32 MTPupdate::size() const {
	switch (_type) {
		case mtpc_updateNewMessage: {
			const MTPDupdateNewMessage &v(c_updateNewMessage());
			return v.vmessage.size() + v.vpts.size();
		}
		case mtpc_updateMessageID: {
			const MTPDupdateMessageID &v(c_updateMessageID());
			return v.vid.size() + v.vrandom_id.size();
		}
		case mtpc_updateReadMessages: {
			const MTPDupdateReadMessages &v(c_updateReadMessages());
			return v.vmessages.size() + v.vpts.size();
		}
		case mtpc_updateDeleteMessages: {
			const MTPDupdateDeleteMessages &v(c_updateDeleteMessages());
			return v.vmessages.size() + v.vpts.size();
		}
		case mtpc_updateRestoreMessages: {
			const MTPDupdateRestoreMessages &v(c_updateRestoreMessages());
			return v.vmessages.size() + v.vpts.size();
		}
		case mtpc_updateUserTyping: {
			const MTPDupdateUserTyping &v(c_updateUserTyping());
			return v.vuser_id.size();
		}
		case mtpc_updateChatUserTyping: {
			const MTPDupdateChatUserTyping &v(c_updateChatUserTyping());
			return v.vchat_id.size() + v.vuser_id.size();
		}
		case mtpc_updateChatParticipants: {
			const MTPDupdateChatParticipants &v(c_updateChatParticipants());
			return v.vparticipants.size();
		}
		case mtpc_updateUserStatus: {
			const MTPDupdateUserStatus &v(c_updateUserStatus());
			return v.vuser_id.size() + v.vstatus.size();
		}
		case mtpc_updateUserName: {
			const MTPDupdateUserName &v(c_updateUserName());
			return v.vuser_id.size() + v.vfirst_name.size() + v.vlast_name.size();
		}
		case mtpc_updateUserPhoto: {
			const MTPDupdateUserPhoto &v(c_updateUserPhoto());
			return v.vuser_id.size() + v.vdate.size() + v.vphoto.size() + v.vprevious.size();
		}
		case mtpc_updateContactRegistered: {
			const MTPDupdateContactRegistered &v(c_updateContactRegistered());
			return v.vuser_id.size() + v.vdate.size();
		}
		case mtpc_updateContactLink: {
			const MTPDupdateContactLink &v(c_updateContactLink());
			return v.vuser_id.size() + v.vmy_link.size() + v.vforeign_link.size();
		}
		case mtpc_updateActivation: {
			const MTPDupdateActivation &v(c_updateActivation());
			return v.vuser_id.size();
		}
		case mtpc_updateNewAuthorization: {
			const MTPDupdateNewAuthorization &v(c_updateNewAuthorization());
			return v.vauth_key_id.size() + v.vdate.size() + v.vdevice.size() + v.vlocation.size();
		}
		case mtpc_updateNewGeoChatMessage: {
			const MTPDupdateNewGeoChatMessage &v(c_updateNewGeoChatMessage());
			return v.vmessage.size();
		}
		case mtpc_updateNewEncryptedMessage: {
			const MTPDupdateNewEncryptedMessage &v(c_updateNewEncryptedMessage());
			return v.vmessage.size() + v.vqts.size();
		}
		case mtpc_updateEncryptedChatTyping: {
			const MTPDupdateEncryptedChatTyping &v(c_updateEncryptedChatTyping());
			return v.vchat_id.size();
		}
		case mtpc_updateEncryption: {
			const MTPDupdateEncryption &v(c_updateEncryption());
			return v.vchat.size() + v.vdate.size();
		}
		case mtpc_updateEncryptedMessagesRead: {
			const MTPDupdateEncryptedMessagesRead &v(c_updateEncryptedMessagesRead());
			return v.vchat_id.size() + v.vmax_date.size() + v.vdate.size();
		}
		case mtpc_updateChatParticipantAdd: {
			const MTPDupdateChatParticipantAdd &v(c_updateChatParticipantAdd());
			return v.vchat_id.size() + v.vuser_id.size() + v.vinviter_id.size() + v.vversion.size();
		}
		case mtpc_updateChatParticipantDelete: {
			const MTPDupdateChatParticipantDelete &v(c_updateChatParticipantDelete());
			return v.vchat_id.size() + v.vuser_id.size() + v.vversion.size();
		}
		case mtpc_updateDcOptions: {
			const MTPDupdateDcOptions &v(c_updateDcOptions());
			return v.vdc_options.size();
		}
		case mtpc_updateUserBlocked: {
			const MTPDupdateUserBlocked &v(c_updateUserBlocked());
			return v.vuser_id.size() + v.vblocked.size();
		}
		case mtpc_updateNotifySettings: {
			const MTPDupdateNotifySettings &v(c_updateNotifySettings());
			return v.vpeer.size() + v.vnotify_settings.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPupdate::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPupdate::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_updateNewMessage: _type = cons; {
			if (!data) setData(new MTPDupdateNewMessage());
			MTPDupdateNewMessage &v(_updateNewMessage());
			v.vmessage.read(from, end);
			v.vpts.read(from, end);
		} break;
		case mtpc_updateMessageID: _type = cons; {
			if (!data) setData(new MTPDupdateMessageID());
			MTPDupdateMessageID &v(_updateMessageID());
			v.vid.read(from, end);
			v.vrandom_id.read(from, end);
		} break;
		case mtpc_updateReadMessages: _type = cons; {
			if (!data) setData(new MTPDupdateReadMessages());
			MTPDupdateReadMessages &v(_updateReadMessages());
			v.vmessages.read(from, end);
			v.vpts.read(from, end);
		} break;
		case mtpc_updateDeleteMessages: _type = cons; {
			if (!data) setData(new MTPDupdateDeleteMessages());
			MTPDupdateDeleteMessages &v(_updateDeleteMessages());
			v.vmessages.read(from, end);
			v.vpts.read(from, end);
		} break;
		case mtpc_updateRestoreMessages: _type = cons; {
			if (!data) setData(new MTPDupdateRestoreMessages());
			MTPDupdateRestoreMessages &v(_updateRestoreMessages());
			v.vmessages.read(from, end);
			v.vpts.read(from, end);
		} break;
		case mtpc_updateUserTyping: _type = cons; {
			if (!data) setData(new MTPDupdateUserTyping());
			MTPDupdateUserTyping &v(_updateUserTyping());
			v.vuser_id.read(from, end);
		} break;
		case mtpc_updateChatUserTyping: _type = cons; {
			if (!data) setData(new MTPDupdateChatUserTyping());
			MTPDupdateChatUserTyping &v(_updateChatUserTyping());
			v.vchat_id.read(from, end);
			v.vuser_id.read(from, end);
		} break;
		case mtpc_updateChatParticipants: _type = cons; {
			if (!data) setData(new MTPDupdateChatParticipants());
			MTPDupdateChatParticipants &v(_updateChatParticipants());
			v.vparticipants.read(from, end);
		} break;
		case mtpc_updateUserStatus: _type = cons; {
			if (!data) setData(new MTPDupdateUserStatus());
			MTPDupdateUserStatus &v(_updateUserStatus());
			v.vuser_id.read(from, end);
			v.vstatus.read(from, end);
		} break;
		case mtpc_updateUserName: _type = cons; {
			if (!data) setData(new MTPDupdateUserName());
			MTPDupdateUserName &v(_updateUserName());
			v.vuser_id.read(from, end);
			v.vfirst_name.read(from, end);
			v.vlast_name.read(from, end);
		} break;
		case mtpc_updateUserPhoto: _type = cons; {
			if (!data) setData(new MTPDupdateUserPhoto());
			MTPDupdateUserPhoto &v(_updateUserPhoto());
			v.vuser_id.read(from, end);
			v.vdate.read(from, end);
			v.vphoto.read(from, end);
			v.vprevious.read(from, end);
		} break;
		case mtpc_updateContactRegistered: _type = cons; {
			if (!data) setData(new MTPDupdateContactRegistered());
			MTPDupdateContactRegistered &v(_updateContactRegistered());
			v.vuser_id.read(from, end);
			v.vdate.read(from, end);
		} break;
		case mtpc_updateContactLink: _type = cons; {
			if (!data) setData(new MTPDupdateContactLink());
			MTPDupdateContactLink &v(_updateContactLink());
			v.vuser_id.read(from, end);
			v.vmy_link.read(from, end);
			v.vforeign_link.read(from, end);
		} break;
		case mtpc_updateActivation: _type = cons; {
			if (!data) setData(new MTPDupdateActivation());
			MTPDupdateActivation &v(_updateActivation());
			v.vuser_id.read(from, end);
		} break;
		case mtpc_updateNewAuthorization: _type = cons; {
			if (!data) setData(new MTPDupdateNewAuthorization());
			MTPDupdateNewAuthorization &v(_updateNewAuthorization());
			v.vauth_key_id.read(from, end);
			v.vdate.read(from, end);
			v.vdevice.read(from, end);
			v.vlocation.read(from, end);
		} break;
		case mtpc_updateNewGeoChatMessage: _type = cons; {
			if (!data) setData(new MTPDupdateNewGeoChatMessage());
			MTPDupdateNewGeoChatMessage &v(_updateNewGeoChatMessage());
			v.vmessage.read(from, end);
		} break;
		case mtpc_updateNewEncryptedMessage: _type = cons; {
			if (!data) setData(new MTPDupdateNewEncryptedMessage());
			MTPDupdateNewEncryptedMessage &v(_updateNewEncryptedMessage());
			v.vmessage.read(from, end);
			v.vqts.read(from, end);
		} break;
		case mtpc_updateEncryptedChatTyping: _type = cons; {
			if (!data) setData(new MTPDupdateEncryptedChatTyping());
			MTPDupdateEncryptedChatTyping &v(_updateEncryptedChatTyping());
			v.vchat_id.read(from, end);
		} break;
		case mtpc_updateEncryption: _type = cons; {
			if (!data) setData(new MTPDupdateEncryption());
			MTPDupdateEncryption &v(_updateEncryption());
			v.vchat.read(from, end);
			v.vdate.read(from, end);
		} break;
		case mtpc_updateEncryptedMessagesRead: _type = cons; {
			if (!data) setData(new MTPDupdateEncryptedMessagesRead());
			MTPDupdateEncryptedMessagesRead &v(_updateEncryptedMessagesRead());
			v.vchat_id.read(from, end);
			v.vmax_date.read(from, end);
			v.vdate.read(from, end);
		} break;
		case mtpc_updateChatParticipantAdd: _type = cons; {
			if (!data) setData(new MTPDupdateChatParticipantAdd());
			MTPDupdateChatParticipantAdd &v(_updateChatParticipantAdd());
			v.vchat_id.read(from, end);
			v.vuser_id.read(from, end);
			v.vinviter_id.read(from, end);
			v.vversion.read(from, end);
		} break;
		case mtpc_updateChatParticipantDelete: _type = cons; {
			if (!data) setData(new MTPDupdateChatParticipantDelete());
			MTPDupdateChatParticipantDelete &v(_updateChatParticipantDelete());
			v.vchat_id.read(from, end);
			v.vuser_id.read(from, end);
			v.vversion.read(from, end);
		} break;
		case mtpc_updateDcOptions: _type = cons; {
			if (!data) setData(new MTPDupdateDcOptions());
			MTPDupdateDcOptions &v(_updateDcOptions());
			v.vdc_options.read(from, end);
		} break;
		case mtpc_updateUserBlocked: _type = cons; {
			if (!data) setData(new MTPDupdateUserBlocked());
			MTPDupdateUserBlocked &v(_updateUserBlocked());
			v.vuser_id.read(from, end);
			v.vblocked.read(from, end);
		} break;
		case mtpc_updateNotifySettings: _type = cons; {
			if (!data) setData(new MTPDupdateNotifySettings());
			MTPDupdateNotifySettings &v(_updateNotifySettings());
			v.vpeer.read(from, end);
			v.vnotify_settings.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPupdate");
	}
}
inline void MTPupdate::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_updateNewMessage: {
			const MTPDupdateNewMessage &v(c_updateNewMessage());
			v.vmessage.write(to);
			v.vpts.write(to);
		} break;
		case mtpc_updateMessageID: {
			const MTPDupdateMessageID &v(c_updateMessageID());
			v.vid.write(to);
			v.vrandom_id.write(to);
		} break;
		case mtpc_updateReadMessages: {
			const MTPDupdateReadMessages &v(c_updateReadMessages());
			v.vmessages.write(to);
			v.vpts.write(to);
		} break;
		case mtpc_updateDeleteMessages: {
			const MTPDupdateDeleteMessages &v(c_updateDeleteMessages());
			v.vmessages.write(to);
			v.vpts.write(to);
		} break;
		case mtpc_updateRestoreMessages: {
			const MTPDupdateRestoreMessages &v(c_updateRestoreMessages());
			v.vmessages.write(to);
			v.vpts.write(to);
		} break;
		case mtpc_updateUserTyping: {
			const MTPDupdateUserTyping &v(c_updateUserTyping());
			v.vuser_id.write(to);
		} break;
		case mtpc_updateChatUserTyping: {
			const MTPDupdateChatUserTyping &v(c_updateChatUserTyping());
			v.vchat_id.write(to);
			v.vuser_id.write(to);
		} break;
		case mtpc_updateChatParticipants: {
			const MTPDupdateChatParticipants &v(c_updateChatParticipants());
			v.vparticipants.write(to);
		} break;
		case mtpc_updateUserStatus: {
			const MTPDupdateUserStatus &v(c_updateUserStatus());
			v.vuser_id.write(to);
			v.vstatus.write(to);
		} break;
		case mtpc_updateUserName: {
			const MTPDupdateUserName &v(c_updateUserName());
			v.vuser_id.write(to);
			v.vfirst_name.write(to);
			v.vlast_name.write(to);
		} break;
		case mtpc_updateUserPhoto: {
			const MTPDupdateUserPhoto &v(c_updateUserPhoto());
			v.vuser_id.write(to);
			v.vdate.write(to);
			v.vphoto.write(to);
			v.vprevious.write(to);
		} break;
		case mtpc_updateContactRegistered: {
			const MTPDupdateContactRegistered &v(c_updateContactRegistered());
			v.vuser_id.write(to);
			v.vdate.write(to);
		} break;
		case mtpc_updateContactLink: {
			const MTPDupdateContactLink &v(c_updateContactLink());
			v.vuser_id.write(to);
			v.vmy_link.write(to);
			v.vforeign_link.write(to);
		} break;
		case mtpc_updateActivation: {
			const MTPDupdateActivation &v(c_updateActivation());
			v.vuser_id.write(to);
		} break;
		case mtpc_updateNewAuthorization: {
			const MTPDupdateNewAuthorization &v(c_updateNewAuthorization());
			v.vauth_key_id.write(to);
			v.vdate.write(to);
			v.vdevice.write(to);
			v.vlocation.write(to);
		} break;
		case mtpc_updateNewGeoChatMessage: {
			const MTPDupdateNewGeoChatMessage &v(c_updateNewGeoChatMessage());
			v.vmessage.write(to);
		} break;
		case mtpc_updateNewEncryptedMessage: {
			const MTPDupdateNewEncryptedMessage &v(c_updateNewEncryptedMessage());
			v.vmessage.write(to);
			v.vqts.write(to);
		} break;
		case mtpc_updateEncryptedChatTyping: {
			const MTPDupdateEncryptedChatTyping &v(c_updateEncryptedChatTyping());
			v.vchat_id.write(to);
		} break;
		case mtpc_updateEncryption: {
			const MTPDupdateEncryption &v(c_updateEncryption());
			v.vchat.write(to);
			v.vdate.write(to);
		} break;
		case mtpc_updateEncryptedMessagesRead: {
			const MTPDupdateEncryptedMessagesRead &v(c_updateEncryptedMessagesRead());
			v.vchat_id.write(to);
			v.vmax_date.write(to);
			v.vdate.write(to);
		} break;
		case mtpc_updateChatParticipantAdd: {
			const MTPDupdateChatParticipantAdd &v(c_updateChatParticipantAdd());
			v.vchat_id.write(to);
			v.vuser_id.write(to);
			v.vinviter_id.write(to);
			v.vversion.write(to);
		} break;
		case mtpc_updateChatParticipantDelete: {
			const MTPDupdateChatParticipantDelete &v(c_updateChatParticipantDelete());
			v.vchat_id.write(to);
			v.vuser_id.write(to);
			v.vversion.write(to);
		} break;
		case mtpc_updateDcOptions: {
			const MTPDupdateDcOptions &v(c_updateDcOptions());
			v.vdc_options.write(to);
		} break;
		case mtpc_updateUserBlocked: {
			const MTPDupdateUserBlocked &v(c_updateUserBlocked());
			v.vuser_id.write(to);
			v.vblocked.write(to);
		} break;
		case mtpc_updateNotifySettings: {
			const MTPDupdateNotifySettings &v(c_updateNotifySettings());
			v.vpeer.write(to);
			v.vnotify_settings.write(to);
		} break;
	}
}
inline MTPupdate::MTPupdate(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_updateNewMessage: setData(new MTPDupdateNewMessage()); break;
		case mtpc_updateMessageID: setData(new MTPDupdateMessageID()); break;
		case mtpc_updateReadMessages: setData(new MTPDupdateReadMessages()); break;
		case mtpc_updateDeleteMessages: setData(new MTPDupdateDeleteMessages()); break;
		case mtpc_updateRestoreMessages: setData(new MTPDupdateRestoreMessages()); break;
		case mtpc_updateUserTyping: setData(new MTPDupdateUserTyping()); break;
		case mtpc_updateChatUserTyping: setData(new MTPDupdateChatUserTyping()); break;
		case mtpc_updateChatParticipants: setData(new MTPDupdateChatParticipants()); break;
		case mtpc_updateUserStatus: setData(new MTPDupdateUserStatus()); break;
		case mtpc_updateUserName: setData(new MTPDupdateUserName()); break;
		case mtpc_updateUserPhoto: setData(new MTPDupdateUserPhoto()); break;
		case mtpc_updateContactRegistered: setData(new MTPDupdateContactRegistered()); break;
		case mtpc_updateContactLink: setData(new MTPDupdateContactLink()); break;
		case mtpc_updateActivation: setData(new MTPDupdateActivation()); break;
		case mtpc_updateNewAuthorization: setData(new MTPDupdateNewAuthorization()); break;
		case mtpc_updateNewGeoChatMessage: setData(new MTPDupdateNewGeoChatMessage()); break;
		case mtpc_updateNewEncryptedMessage: setData(new MTPDupdateNewEncryptedMessage()); break;
		case mtpc_updateEncryptedChatTyping: setData(new MTPDupdateEncryptedChatTyping()); break;
		case mtpc_updateEncryption: setData(new MTPDupdateEncryption()); break;
		case mtpc_updateEncryptedMessagesRead: setData(new MTPDupdateEncryptedMessagesRead()); break;
		case mtpc_updateChatParticipantAdd: setData(new MTPDupdateChatParticipantAdd()); break;
		case mtpc_updateChatParticipantDelete: setData(new MTPDupdateChatParticipantDelete()); break;
		case mtpc_updateDcOptions: setData(new MTPDupdateDcOptions()); break;
		case mtpc_updateUserBlocked: setData(new MTPDupdateUserBlocked()); break;
		case mtpc_updateNotifySettings: setData(new MTPDupdateNotifySettings()); break;
		default: throw mtpErrorBadTypeId(type, "MTPupdate");
	}
}
inline MTPupdate::MTPupdate(MTPDupdateNewMessage *_data) : mtpDataOwner(_data), _type(mtpc_updateNewMessage) {
}
inline MTPupdate::MTPupdate(MTPDupdateMessageID *_data) : mtpDataOwner(_data), _type(mtpc_updateMessageID) {
}
inline MTPupdate::MTPupdate(MTPDupdateReadMessages *_data) : mtpDataOwner(_data), _type(mtpc_updateReadMessages) {
}
inline MTPupdate::MTPupdate(MTPDupdateDeleteMessages *_data) : mtpDataOwner(_data), _type(mtpc_updateDeleteMessages) {
}
inline MTPupdate::MTPupdate(MTPDupdateRestoreMessages *_data) : mtpDataOwner(_data), _type(mtpc_updateRestoreMessages) {
}
inline MTPupdate::MTPupdate(MTPDupdateUserTyping *_data) : mtpDataOwner(_data), _type(mtpc_updateUserTyping) {
}
inline MTPupdate::MTPupdate(MTPDupdateChatUserTyping *_data) : mtpDataOwner(_data), _type(mtpc_updateChatUserTyping) {
}
inline MTPupdate::MTPupdate(MTPDupdateChatParticipants *_data) : mtpDataOwner(_data), _type(mtpc_updateChatParticipants) {
}
inline MTPupdate::MTPupdate(MTPDupdateUserStatus *_data) : mtpDataOwner(_data), _type(mtpc_updateUserStatus) {
}
inline MTPupdate::MTPupdate(MTPDupdateUserName *_data) : mtpDataOwner(_data), _type(mtpc_updateUserName) {
}
inline MTPupdate::MTPupdate(MTPDupdateUserPhoto *_data) : mtpDataOwner(_data), _type(mtpc_updateUserPhoto) {
}
inline MTPupdate::MTPupdate(MTPDupdateContactRegistered *_data) : mtpDataOwner(_data), _type(mtpc_updateContactRegistered) {
}
inline MTPupdate::MTPupdate(MTPDupdateContactLink *_data) : mtpDataOwner(_data), _type(mtpc_updateContactLink) {
}
inline MTPupdate::MTPupdate(MTPDupdateActivation *_data) : mtpDataOwner(_data), _type(mtpc_updateActivation) {
}
inline MTPupdate::MTPupdate(MTPDupdateNewAuthorization *_data) : mtpDataOwner(_data), _type(mtpc_updateNewAuthorization) {
}
inline MTPupdate::MTPupdate(MTPDupdateNewGeoChatMessage *_data) : mtpDataOwner(_data), _type(mtpc_updateNewGeoChatMessage) {
}
inline MTPupdate::MTPupdate(MTPDupdateNewEncryptedMessage *_data) : mtpDataOwner(_data), _type(mtpc_updateNewEncryptedMessage) {
}
inline MTPupdate::MTPupdate(MTPDupdateEncryptedChatTyping *_data) : mtpDataOwner(_data), _type(mtpc_updateEncryptedChatTyping) {
}
inline MTPupdate::MTPupdate(MTPDupdateEncryption *_data) : mtpDataOwner(_data), _type(mtpc_updateEncryption) {
}
inline MTPupdate::MTPupdate(MTPDupdateEncryptedMessagesRead *_data) : mtpDataOwner(_data), _type(mtpc_updateEncryptedMessagesRead) {
}
inline MTPupdate::MTPupdate(MTPDupdateChatParticipantAdd *_data) : mtpDataOwner(_data), _type(mtpc_updateChatParticipantAdd) {
}
inline MTPupdate::MTPupdate(MTPDupdateChatParticipantDelete *_data) : mtpDataOwner(_data), _type(mtpc_updateChatParticipantDelete) {
}
inline MTPupdate::MTPupdate(MTPDupdateDcOptions *_data) : mtpDataOwner(_data), _type(mtpc_updateDcOptions) {
}
inline MTPupdate::MTPupdate(MTPDupdateUserBlocked *_data) : mtpDataOwner(_data), _type(mtpc_updateUserBlocked) {
}
inline MTPupdate::MTPupdate(MTPDupdateNotifySettings *_data) : mtpDataOwner(_data), _type(mtpc_updateNotifySettings) {
}
inline MTPupdate MTP_updateNewMessage(const MTPMessage &_message, MTPint _pts) {
	return MTPupdate(new MTPDupdateNewMessage(_message, _pts));
}
inline MTPupdate MTP_updateMessageID(MTPint _id, const MTPlong &_random_id) {
	return MTPupdate(new MTPDupdateMessageID(_id, _random_id));
}
inline MTPupdate MTP_updateReadMessages(const MTPVector<MTPint> &_messages, MTPint _pts) {
	return MTPupdate(new MTPDupdateReadMessages(_messages, _pts));
}
inline MTPupdate MTP_updateDeleteMessages(const MTPVector<MTPint> &_messages, MTPint _pts) {
	return MTPupdate(new MTPDupdateDeleteMessages(_messages, _pts));
}
inline MTPupdate MTP_updateRestoreMessages(const MTPVector<MTPint> &_messages, MTPint _pts) {
	return MTPupdate(new MTPDupdateRestoreMessages(_messages, _pts));
}
inline MTPupdate MTP_updateUserTyping(MTPint _user_id) {
	return MTPupdate(new MTPDupdateUserTyping(_user_id));
}
inline MTPupdate MTP_updateChatUserTyping(MTPint _chat_id, MTPint _user_id) {
	return MTPupdate(new MTPDupdateChatUserTyping(_chat_id, _user_id));
}
inline MTPupdate MTP_updateChatParticipants(const MTPChatParticipants &_participants) {
	return MTPupdate(new MTPDupdateChatParticipants(_participants));
}
inline MTPupdate MTP_updateUserStatus(MTPint _user_id, const MTPUserStatus &_status) {
	return MTPupdate(new MTPDupdateUserStatus(_user_id, _status));
}
inline MTPupdate MTP_updateUserName(MTPint _user_id, const MTPstring &_first_name, const MTPstring &_last_name) {
	return MTPupdate(new MTPDupdateUserName(_user_id, _first_name, _last_name));
}
inline MTPupdate MTP_updateUserPhoto(MTPint _user_id, MTPint _date, const MTPUserProfilePhoto &_photo, MTPBool _previous) {
	return MTPupdate(new MTPDupdateUserPhoto(_user_id, _date, _photo, _previous));
}
inline MTPupdate MTP_updateContactRegistered(MTPint _user_id, MTPint _date) {
	return MTPupdate(new MTPDupdateContactRegistered(_user_id, _date));
}
inline MTPupdate MTP_updateContactLink(MTPint _user_id, const MTPcontacts_MyLink &_my_link, const MTPcontacts_ForeignLink &_foreign_link) {
	return MTPupdate(new MTPDupdateContactLink(_user_id, _my_link, _foreign_link));
}
inline MTPupdate MTP_updateActivation(MTPint _user_id) {
	return MTPupdate(new MTPDupdateActivation(_user_id));
}
inline MTPupdate MTP_updateNewAuthorization(const MTPlong &_auth_key_id, MTPint _date, const MTPstring &_device, const MTPstring &_location) {
	return MTPupdate(new MTPDupdateNewAuthorization(_auth_key_id, _date, _device, _location));
}
inline MTPupdate MTP_updateNewGeoChatMessage(const MTPGeoChatMessage &_message) {
	return MTPupdate(new MTPDupdateNewGeoChatMessage(_message));
}
inline MTPupdate MTP_updateNewEncryptedMessage(const MTPEncryptedMessage &_message, MTPint _qts) {
	return MTPupdate(new MTPDupdateNewEncryptedMessage(_message, _qts));
}
inline MTPupdate MTP_updateEncryptedChatTyping(MTPint _chat_id) {
	return MTPupdate(new MTPDupdateEncryptedChatTyping(_chat_id));
}
inline MTPupdate MTP_updateEncryption(const MTPEncryptedChat &_chat, MTPint _date) {
	return MTPupdate(new MTPDupdateEncryption(_chat, _date));
}
inline MTPupdate MTP_updateEncryptedMessagesRead(MTPint _chat_id, MTPint _max_date, MTPint _date) {
	return MTPupdate(new MTPDupdateEncryptedMessagesRead(_chat_id, _max_date, _date));
}
inline MTPupdate MTP_updateChatParticipantAdd(MTPint _chat_id, MTPint _user_id, MTPint _inviter_id, MTPint _version) {
	return MTPupdate(new MTPDupdateChatParticipantAdd(_chat_id, _user_id, _inviter_id, _version));
}
inline MTPupdate MTP_updateChatParticipantDelete(MTPint _chat_id, MTPint _user_id, MTPint _version) {
	return MTPupdate(new MTPDupdateChatParticipantDelete(_chat_id, _user_id, _version));
}
inline MTPupdate MTP_updateDcOptions(const MTPVector<MTPDcOption> &_dc_options) {
	return MTPupdate(new MTPDupdateDcOptions(_dc_options));
}
inline MTPupdate MTP_updateUserBlocked(MTPint _user_id, MTPBool _blocked) {
	return MTPupdate(new MTPDupdateUserBlocked(_user_id, _blocked));
}
inline MTPupdate MTP_updateNotifySettings(const MTPNotifyPeer &_peer, const MTPPeerNotifySettings &_notify_settings) {
	return MTPupdate(new MTPDupdateNotifySettings(_peer, _notify_settings));
}

inline MTPupdates_state::MTPupdates_state() : mtpDataOwner(new MTPDupdates_state()) {
}

inline uint32 MTPupdates_state::size() const {
	const MTPDupdates_state &v(c_updates_state());
	return v.vpts.size() + v.vqts.size() + v.vdate.size() + v.vseq.size() + v.vunread_count.size();
}
inline mtpTypeId MTPupdates_state::type() const {
	return mtpc_updates_state;
}
inline void MTPupdates_state::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_updates_state) throw mtpErrorUnexpected(cons, "MTPupdates_state");

	if (!data) setData(new MTPDupdates_state());
	MTPDupdates_state &v(_updates_state());
	v.vpts.read(from, end);
	v.vqts.read(from, end);
	v.vdate.read(from, end);
	v.vseq.read(from, end);
	v.vunread_count.read(from, end);
}
inline void MTPupdates_state::write(mtpBuffer &to) const {
	const MTPDupdates_state &v(c_updates_state());
	v.vpts.write(to);
	v.vqts.write(to);
	v.vdate.write(to);
	v.vseq.write(to);
	v.vunread_count.write(to);
}
inline MTPupdates_state::MTPupdates_state(MTPDupdates_state *_data) : mtpDataOwner(_data) {
}
inline MTPupdates_state MTP_updates_state(MTPint _pts, MTPint _qts, MTPint _date, MTPint _seq, MTPint _unread_count) {
	return MTPupdates_state(new MTPDupdates_state(_pts, _qts, _date, _seq, _unread_count));
}

inline uint32 MTPupdates_difference::size() const {
	switch (_type) {
		case mtpc_updates_differenceEmpty: {
			const MTPDupdates_differenceEmpty &v(c_updates_differenceEmpty());
			return v.vdate.size() + v.vseq.size();
		}
		case mtpc_updates_difference: {
			const MTPDupdates_difference &v(c_updates_difference());
			return v.vnew_messages.size() + v.vnew_encrypted_messages.size() + v.vother_updates.size() + v.vchats.size() + v.vusers.size() + v.vstate.size();
		}
		case mtpc_updates_differenceSlice: {
			const MTPDupdates_differenceSlice &v(c_updates_differenceSlice());
			return v.vnew_messages.size() + v.vnew_encrypted_messages.size() + v.vother_updates.size() + v.vchats.size() + v.vusers.size() + v.vintermediate_state.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPupdates_difference::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPupdates_difference::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_updates_differenceEmpty: _type = cons; {
			if (!data) setData(new MTPDupdates_differenceEmpty());
			MTPDupdates_differenceEmpty &v(_updates_differenceEmpty());
			v.vdate.read(from, end);
			v.vseq.read(from, end);
		} break;
		case mtpc_updates_difference: _type = cons; {
			if (!data) setData(new MTPDupdates_difference());
			MTPDupdates_difference &v(_updates_difference());
			v.vnew_messages.read(from, end);
			v.vnew_encrypted_messages.read(from, end);
			v.vother_updates.read(from, end);
			v.vchats.read(from, end);
			v.vusers.read(from, end);
			v.vstate.read(from, end);
		} break;
		case mtpc_updates_differenceSlice: _type = cons; {
			if (!data) setData(new MTPDupdates_differenceSlice());
			MTPDupdates_differenceSlice &v(_updates_differenceSlice());
			v.vnew_messages.read(from, end);
			v.vnew_encrypted_messages.read(from, end);
			v.vother_updates.read(from, end);
			v.vchats.read(from, end);
			v.vusers.read(from, end);
			v.vintermediate_state.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPupdates_difference");
	}
}
inline void MTPupdates_difference::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_updates_differenceEmpty: {
			const MTPDupdates_differenceEmpty &v(c_updates_differenceEmpty());
			v.vdate.write(to);
			v.vseq.write(to);
		} break;
		case mtpc_updates_difference: {
			const MTPDupdates_difference &v(c_updates_difference());
			v.vnew_messages.write(to);
			v.vnew_encrypted_messages.write(to);
			v.vother_updates.write(to);
			v.vchats.write(to);
			v.vusers.write(to);
			v.vstate.write(to);
		} break;
		case mtpc_updates_differenceSlice: {
			const MTPDupdates_differenceSlice &v(c_updates_differenceSlice());
			v.vnew_messages.write(to);
			v.vnew_encrypted_messages.write(to);
			v.vother_updates.write(to);
			v.vchats.write(to);
			v.vusers.write(to);
			v.vintermediate_state.write(to);
		} break;
	}
}
inline MTPupdates_difference::MTPupdates_difference(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_updates_differenceEmpty: setData(new MTPDupdates_differenceEmpty()); break;
		case mtpc_updates_difference: setData(new MTPDupdates_difference()); break;
		case mtpc_updates_differenceSlice: setData(new MTPDupdates_differenceSlice()); break;
		default: throw mtpErrorBadTypeId(type, "MTPupdates_difference");
	}
}
inline MTPupdates_difference::MTPupdates_difference(MTPDupdates_differenceEmpty *_data) : mtpDataOwner(_data), _type(mtpc_updates_differenceEmpty) {
}
inline MTPupdates_difference::MTPupdates_difference(MTPDupdates_difference *_data) : mtpDataOwner(_data), _type(mtpc_updates_difference) {
}
inline MTPupdates_difference::MTPupdates_difference(MTPDupdates_differenceSlice *_data) : mtpDataOwner(_data), _type(mtpc_updates_differenceSlice) {
}
inline MTPupdates_difference MTP_updates_differenceEmpty(MTPint _date, MTPint _seq) {
	return MTPupdates_difference(new MTPDupdates_differenceEmpty(_date, _seq));
}
inline MTPupdates_difference MTP_updates_difference(const MTPVector<MTPMessage> &_new_messages, const MTPVector<MTPEncryptedMessage> &_new_encrypted_messages, const MTPVector<MTPUpdate> &_other_updates, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users, const MTPupdates_State &_state) {
	return MTPupdates_difference(new MTPDupdates_difference(_new_messages, _new_encrypted_messages, _other_updates, _chats, _users, _state));
}
inline MTPupdates_difference MTP_updates_differenceSlice(const MTPVector<MTPMessage> &_new_messages, const MTPVector<MTPEncryptedMessage> &_new_encrypted_messages, const MTPVector<MTPUpdate> &_other_updates, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users, const MTPupdates_State &_intermediate_state) {
	return MTPupdates_difference(new MTPDupdates_differenceSlice(_new_messages, _new_encrypted_messages, _other_updates, _chats, _users, _intermediate_state));
}

inline uint32 MTPupdates::size() const {
	switch (_type) {
		case mtpc_updateShortMessage: {
			const MTPDupdateShortMessage &v(c_updateShortMessage());
			return v.vid.size() + v.vfrom_id.size() + v.vmessage.size() + v.vpts.size() + v.vdate.size() + v.vseq.size();
		}
		case mtpc_updateShortChatMessage: {
			const MTPDupdateShortChatMessage &v(c_updateShortChatMessage());
			return v.vid.size() + v.vfrom_id.size() + v.vchat_id.size() + v.vmessage.size() + v.vpts.size() + v.vdate.size() + v.vseq.size();
		}
		case mtpc_updateShort: {
			const MTPDupdateShort &v(c_updateShort());
			return v.vupdate.size() + v.vdate.size();
		}
		case mtpc_updatesCombined: {
			const MTPDupdatesCombined &v(c_updatesCombined());
			return v.vupdates.size() + v.vusers.size() + v.vchats.size() + v.vdate.size() + v.vseq_start.size() + v.vseq.size();
		}
		case mtpc_updates: {
			const MTPDupdates &v(c_updates());
			return v.vupdates.size() + v.vusers.size() + v.vchats.size() + v.vdate.size() + v.vseq.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPupdates::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPupdates::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_updatesTooLong: _type = cons; break;
		case mtpc_updateShortMessage: _type = cons; {
			if (!data) setData(new MTPDupdateShortMessage());
			MTPDupdateShortMessage &v(_updateShortMessage());
			v.vid.read(from, end);
			v.vfrom_id.read(from, end);
			v.vmessage.read(from, end);
			v.vpts.read(from, end);
			v.vdate.read(from, end);
			v.vseq.read(from, end);
		} break;
		case mtpc_updateShortChatMessage: _type = cons; {
			if (!data) setData(new MTPDupdateShortChatMessage());
			MTPDupdateShortChatMessage &v(_updateShortChatMessage());
			v.vid.read(from, end);
			v.vfrom_id.read(from, end);
			v.vchat_id.read(from, end);
			v.vmessage.read(from, end);
			v.vpts.read(from, end);
			v.vdate.read(from, end);
			v.vseq.read(from, end);
		} break;
		case mtpc_updateShort: _type = cons; {
			if (!data) setData(new MTPDupdateShort());
			MTPDupdateShort &v(_updateShort());
			v.vupdate.read(from, end);
			v.vdate.read(from, end);
		} break;
		case mtpc_updatesCombined: _type = cons; {
			if (!data) setData(new MTPDupdatesCombined());
			MTPDupdatesCombined &v(_updatesCombined());
			v.vupdates.read(from, end);
			v.vusers.read(from, end);
			v.vchats.read(from, end);
			v.vdate.read(from, end);
			v.vseq_start.read(from, end);
			v.vseq.read(from, end);
		} break;
		case mtpc_updates: _type = cons; {
			if (!data) setData(new MTPDupdates());
			MTPDupdates &v(_updates());
			v.vupdates.read(from, end);
			v.vusers.read(from, end);
			v.vchats.read(from, end);
			v.vdate.read(from, end);
			v.vseq.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPupdates");
	}
}
inline void MTPupdates::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_updateShortMessage: {
			const MTPDupdateShortMessage &v(c_updateShortMessage());
			v.vid.write(to);
			v.vfrom_id.write(to);
			v.vmessage.write(to);
			v.vpts.write(to);
			v.vdate.write(to);
			v.vseq.write(to);
		} break;
		case mtpc_updateShortChatMessage: {
			const MTPDupdateShortChatMessage &v(c_updateShortChatMessage());
			v.vid.write(to);
			v.vfrom_id.write(to);
			v.vchat_id.write(to);
			v.vmessage.write(to);
			v.vpts.write(to);
			v.vdate.write(to);
			v.vseq.write(to);
		} break;
		case mtpc_updateShort: {
			const MTPDupdateShort &v(c_updateShort());
			v.vupdate.write(to);
			v.vdate.write(to);
		} break;
		case mtpc_updatesCombined: {
			const MTPDupdatesCombined &v(c_updatesCombined());
			v.vupdates.write(to);
			v.vusers.write(to);
			v.vchats.write(to);
			v.vdate.write(to);
			v.vseq_start.write(to);
			v.vseq.write(to);
		} break;
		case mtpc_updates: {
			const MTPDupdates &v(c_updates());
			v.vupdates.write(to);
			v.vusers.write(to);
			v.vchats.write(to);
			v.vdate.write(to);
			v.vseq.write(to);
		} break;
	}
}
inline MTPupdates::MTPupdates(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_updatesTooLong: break;
		case mtpc_updateShortMessage: setData(new MTPDupdateShortMessage()); break;
		case mtpc_updateShortChatMessage: setData(new MTPDupdateShortChatMessage()); break;
		case mtpc_updateShort: setData(new MTPDupdateShort()); break;
		case mtpc_updatesCombined: setData(new MTPDupdatesCombined()); break;
		case mtpc_updates: setData(new MTPDupdates()); break;
		default: throw mtpErrorBadTypeId(type, "MTPupdates");
	}
}
inline MTPupdates::MTPupdates(MTPDupdateShortMessage *_data) : mtpDataOwner(_data), _type(mtpc_updateShortMessage) {
}
inline MTPupdates::MTPupdates(MTPDupdateShortChatMessage *_data) : mtpDataOwner(_data), _type(mtpc_updateShortChatMessage) {
}
inline MTPupdates::MTPupdates(MTPDupdateShort *_data) : mtpDataOwner(_data), _type(mtpc_updateShort) {
}
inline MTPupdates::MTPupdates(MTPDupdatesCombined *_data) : mtpDataOwner(_data), _type(mtpc_updatesCombined) {
}
inline MTPupdates::MTPupdates(MTPDupdates *_data) : mtpDataOwner(_data), _type(mtpc_updates) {
}
inline MTPupdates MTP_updatesTooLong() {
	return MTPupdates(mtpc_updatesTooLong);
}
inline MTPupdates MTP_updateShortMessage(MTPint _id, MTPint _from_id, const MTPstring &_message, MTPint _pts, MTPint _date, MTPint _seq) {
	return MTPupdates(new MTPDupdateShortMessage(_id, _from_id, _message, _pts, _date, _seq));
}
inline MTPupdates MTP_updateShortChatMessage(MTPint _id, MTPint _from_id, MTPint _chat_id, const MTPstring &_message, MTPint _pts, MTPint _date, MTPint _seq) {
	return MTPupdates(new MTPDupdateShortChatMessage(_id, _from_id, _chat_id, _message, _pts, _date, _seq));
}
inline MTPupdates MTP_updateShort(const MTPUpdate &_update, MTPint _date) {
	return MTPupdates(new MTPDupdateShort(_update, _date));
}
inline MTPupdates MTP_updatesCombined(const MTPVector<MTPUpdate> &_updates, const MTPVector<MTPUser> &_users, const MTPVector<MTPChat> &_chats, MTPint _date, MTPint _seq_start, MTPint _seq) {
	return MTPupdates(new MTPDupdatesCombined(_updates, _users, _chats, _date, _seq_start, _seq));
}
inline MTPupdates MTP_updates(const MTPVector<MTPUpdate> &_updates, const MTPVector<MTPUser> &_users, const MTPVector<MTPChat> &_chats, MTPint _date, MTPint _seq) {
	return MTPupdates(new MTPDupdates(_updates, _users, _chats, _date, _seq));
}

inline uint32 MTPphotos_photos::size() const {
	switch (_type) {
		case mtpc_photos_photos: {
			const MTPDphotos_photos &v(c_photos_photos());
			return v.vphotos.size() + v.vusers.size();
		}
		case mtpc_photos_photosSlice: {
			const MTPDphotos_photosSlice &v(c_photos_photosSlice());
			return v.vcount.size() + v.vphotos.size() + v.vusers.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPphotos_photos::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPphotos_photos::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_photos_photos: _type = cons; {
			if (!data) setData(new MTPDphotos_photos());
			MTPDphotos_photos &v(_photos_photos());
			v.vphotos.read(from, end);
			v.vusers.read(from, end);
		} break;
		case mtpc_photos_photosSlice: _type = cons; {
			if (!data) setData(new MTPDphotos_photosSlice());
			MTPDphotos_photosSlice &v(_photos_photosSlice());
			v.vcount.read(from, end);
			v.vphotos.read(from, end);
			v.vusers.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPphotos_photos");
	}
}
inline void MTPphotos_photos::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_photos_photos: {
			const MTPDphotos_photos &v(c_photos_photos());
			v.vphotos.write(to);
			v.vusers.write(to);
		} break;
		case mtpc_photos_photosSlice: {
			const MTPDphotos_photosSlice &v(c_photos_photosSlice());
			v.vcount.write(to);
			v.vphotos.write(to);
			v.vusers.write(to);
		} break;
	}
}
inline MTPphotos_photos::MTPphotos_photos(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_photos_photos: setData(new MTPDphotos_photos()); break;
		case mtpc_photos_photosSlice: setData(new MTPDphotos_photosSlice()); break;
		default: throw mtpErrorBadTypeId(type, "MTPphotos_photos");
	}
}
inline MTPphotos_photos::MTPphotos_photos(MTPDphotos_photos *_data) : mtpDataOwner(_data), _type(mtpc_photos_photos) {
}
inline MTPphotos_photos::MTPphotos_photos(MTPDphotos_photosSlice *_data) : mtpDataOwner(_data), _type(mtpc_photos_photosSlice) {
}
inline MTPphotos_photos MTP_photos_photos(const MTPVector<MTPPhoto> &_photos, const MTPVector<MTPUser> &_users) {
	return MTPphotos_photos(new MTPDphotos_photos(_photos, _users));
}
inline MTPphotos_photos MTP_photos_photosSlice(MTPint _count, const MTPVector<MTPPhoto> &_photos, const MTPVector<MTPUser> &_users) {
	return MTPphotos_photos(new MTPDphotos_photosSlice(_count, _photos, _users));
}

inline MTPphotos_photo::MTPphotos_photo() : mtpDataOwner(new MTPDphotos_photo()) {
}

inline uint32 MTPphotos_photo::size() const {
	const MTPDphotos_photo &v(c_photos_photo());
	return v.vphoto.size() + v.vusers.size();
}
inline mtpTypeId MTPphotos_photo::type() const {
	return mtpc_photos_photo;
}
inline void MTPphotos_photo::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_photos_photo) throw mtpErrorUnexpected(cons, "MTPphotos_photo");

	if (!data) setData(new MTPDphotos_photo());
	MTPDphotos_photo &v(_photos_photo());
	v.vphoto.read(from, end);
	v.vusers.read(from, end);
}
inline void MTPphotos_photo::write(mtpBuffer &to) const {
	const MTPDphotos_photo &v(c_photos_photo());
	v.vphoto.write(to);
	v.vusers.write(to);
}
inline MTPphotos_photo::MTPphotos_photo(MTPDphotos_photo *_data) : mtpDataOwner(_data) {
}
inline MTPphotos_photo MTP_photos_photo(const MTPPhoto &_photo, const MTPVector<MTPUser> &_users) {
	return MTPphotos_photo(new MTPDphotos_photo(_photo, _users));
}

inline MTPupload_file::MTPupload_file() : mtpDataOwner(new MTPDupload_file()) {
}

inline uint32 MTPupload_file::size() const {
	const MTPDupload_file &v(c_upload_file());
	return v.vtype.size() + v.vmtime.size() + v.vbytes.size();
}
inline mtpTypeId MTPupload_file::type() const {
	return mtpc_upload_file;
}
inline void MTPupload_file::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_upload_file) throw mtpErrorUnexpected(cons, "MTPupload_file");

	if (!data) setData(new MTPDupload_file());
	MTPDupload_file &v(_upload_file());
	v.vtype.read(from, end);
	v.vmtime.read(from, end);
	v.vbytes.read(from, end);
}
inline void MTPupload_file::write(mtpBuffer &to) const {
	const MTPDupload_file &v(c_upload_file());
	v.vtype.write(to);
	v.vmtime.write(to);
	v.vbytes.write(to);
}
inline MTPupload_file::MTPupload_file(MTPDupload_file *_data) : mtpDataOwner(_data) {
}
inline MTPupload_file MTP_upload_file(const MTPstorage_FileType &_type, MTPint _mtime, const MTPbytes &_bytes) {
	return MTPupload_file(new MTPDupload_file(_type, _mtime, _bytes));
}

inline MTPdcOption::MTPdcOption() : mtpDataOwner(new MTPDdcOption()) {
}

inline uint32 MTPdcOption::size() const {
	const MTPDdcOption &v(c_dcOption());
	return v.vid.size() + v.vhostname.size() + v.vip_address.size() + v.vport.size();
}
inline mtpTypeId MTPdcOption::type() const {
	return mtpc_dcOption;
}
inline void MTPdcOption::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_dcOption) throw mtpErrorUnexpected(cons, "MTPdcOption");

	if (!data) setData(new MTPDdcOption());
	MTPDdcOption &v(_dcOption());
	v.vid.read(from, end);
	v.vhostname.read(from, end);
	v.vip_address.read(from, end);
	v.vport.read(from, end);
}
inline void MTPdcOption::write(mtpBuffer &to) const {
	const MTPDdcOption &v(c_dcOption());
	v.vid.write(to);
	v.vhostname.write(to);
	v.vip_address.write(to);
	v.vport.write(to);
}
inline MTPdcOption::MTPdcOption(MTPDdcOption *_data) : mtpDataOwner(_data) {
}
inline MTPdcOption MTP_dcOption(MTPint _id, const MTPstring &_hostname, const MTPstring &_ip_address, MTPint _port) {
	return MTPdcOption(new MTPDdcOption(_id, _hostname, _ip_address, _port));
}

inline MTPconfig::MTPconfig() : mtpDataOwner(new MTPDconfig()) {
}

inline uint32 MTPconfig::size() const {
	const MTPDconfig &v(c_config());
	return v.vdate.size() + v.vtest_mode.size() + v.vthis_dc.size() + v.vdc_options.size() + v.vchat_size_max.size() + v.vbroadcast_size_max.size();
}
inline mtpTypeId MTPconfig::type() const {
	return mtpc_config;
}
inline void MTPconfig::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_config) throw mtpErrorUnexpected(cons, "MTPconfig");

	if (!data) setData(new MTPDconfig());
	MTPDconfig &v(_config());
	v.vdate.read(from, end);
	v.vtest_mode.read(from, end);
	v.vthis_dc.read(from, end);
	v.vdc_options.read(from, end);
	v.vchat_size_max.read(from, end);
	v.vbroadcast_size_max.read(from, end);
}
inline void MTPconfig::write(mtpBuffer &to) const {
	const MTPDconfig &v(c_config());
	v.vdate.write(to);
	v.vtest_mode.write(to);
	v.vthis_dc.write(to);
	v.vdc_options.write(to);
	v.vchat_size_max.write(to);
	v.vbroadcast_size_max.write(to);
}
inline MTPconfig::MTPconfig(MTPDconfig *_data) : mtpDataOwner(_data) {
}
inline MTPconfig MTP_config(MTPint _date, MTPBool _test_mode, MTPint _this_dc, const MTPVector<MTPDcOption> &_dc_options, MTPint _chat_size_max, MTPint _broadcast_size_max) {
	return MTPconfig(new MTPDconfig(_date, _test_mode, _this_dc, _dc_options, _chat_size_max, _broadcast_size_max));
}

inline MTPnearestDc::MTPnearestDc() : mtpDataOwner(new MTPDnearestDc()) {
}

inline uint32 MTPnearestDc::size() const {
	const MTPDnearestDc &v(c_nearestDc());
	return v.vcountry.size() + v.vthis_dc.size() + v.vnearest_dc.size();
}
inline mtpTypeId MTPnearestDc::type() const {
	return mtpc_nearestDc;
}
inline void MTPnearestDc::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_nearestDc) throw mtpErrorUnexpected(cons, "MTPnearestDc");

	if (!data) setData(new MTPDnearestDc());
	MTPDnearestDc &v(_nearestDc());
	v.vcountry.read(from, end);
	v.vthis_dc.read(from, end);
	v.vnearest_dc.read(from, end);
}
inline void MTPnearestDc::write(mtpBuffer &to) const {
	const MTPDnearestDc &v(c_nearestDc());
	v.vcountry.write(to);
	v.vthis_dc.write(to);
	v.vnearest_dc.write(to);
}
inline MTPnearestDc::MTPnearestDc(MTPDnearestDc *_data) : mtpDataOwner(_data) {
}
inline MTPnearestDc MTP_nearestDc(const MTPstring &_country, MTPint _this_dc, MTPint _nearest_dc) {
	return MTPnearestDc(new MTPDnearestDc(_country, _this_dc, _nearest_dc));
}

inline uint32 MTPhelp_appUpdate::size() const {
	switch (_type) {
		case mtpc_help_appUpdate: {
			const MTPDhelp_appUpdate &v(c_help_appUpdate());
			return v.vid.size() + v.vcritical.size() + v.vurl.size() + v.vtext.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPhelp_appUpdate::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPhelp_appUpdate::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_help_appUpdate: _type = cons; {
			if (!data) setData(new MTPDhelp_appUpdate());
			MTPDhelp_appUpdate &v(_help_appUpdate());
			v.vid.read(from, end);
			v.vcritical.read(from, end);
			v.vurl.read(from, end);
			v.vtext.read(from, end);
		} break;
		case mtpc_help_noAppUpdate: _type = cons; break;
		default: throw mtpErrorUnexpected(cons, "MTPhelp_appUpdate");
	}
}
inline void MTPhelp_appUpdate::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_help_appUpdate: {
			const MTPDhelp_appUpdate &v(c_help_appUpdate());
			v.vid.write(to);
			v.vcritical.write(to);
			v.vurl.write(to);
			v.vtext.write(to);
		} break;
	}
}
inline MTPhelp_appUpdate::MTPhelp_appUpdate(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_help_appUpdate: setData(new MTPDhelp_appUpdate()); break;
		case mtpc_help_noAppUpdate: break;
		default: throw mtpErrorBadTypeId(type, "MTPhelp_appUpdate");
	}
}
inline MTPhelp_appUpdate::MTPhelp_appUpdate(MTPDhelp_appUpdate *_data) : mtpDataOwner(_data), _type(mtpc_help_appUpdate) {
}
inline MTPhelp_appUpdate MTP_help_appUpdate(MTPint _id, MTPBool _critical, const MTPstring &_url, const MTPstring &_text) {
	return MTPhelp_appUpdate(new MTPDhelp_appUpdate(_id, _critical, _url, _text));
}
inline MTPhelp_appUpdate MTP_help_noAppUpdate() {
	return MTPhelp_appUpdate(mtpc_help_noAppUpdate);
}

inline MTPhelp_inviteText::MTPhelp_inviteText() : mtpDataOwner(new MTPDhelp_inviteText()) {
}

inline uint32 MTPhelp_inviteText::size() const {
	const MTPDhelp_inviteText &v(c_help_inviteText());
	return v.vmessage.size();
}
inline mtpTypeId MTPhelp_inviteText::type() const {
	return mtpc_help_inviteText;
}
inline void MTPhelp_inviteText::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_help_inviteText) throw mtpErrorUnexpected(cons, "MTPhelp_inviteText");

	if (!data) setData(new MTPDhelp_inviteText());
	MTPDhelp_inviteText &v(_help_inviteText());
	v.vmessage.read(from, end);
}
inline void MTPhelp_inviteText::write(mtpBuffer &to) const {
	const MTPDhelp_inviteText &v(c_help_inviteText());
	v.vmessage.write(to);
}
inline MTPhelp_inviteText::MTPhelp_inviteText(MTPDhelp_inviteText *_data) : mtpDataOwner(_data) {
}
inline MTPhelp_inviteText MTP_help_inviteText(const MTPstring &_message) {
	return MTPhelp_inviteText(new MTPDhelp_inviteText(_message));
}

inline MTPinputGeoChat::MTPinputGeoChat() : mtpDataOwner(new MTPDinputGeoChat()) {
}

inline uint32 MTPinputGeoChat::size() const {
	const MTPDinputGeoChat &v(c_inputGeoChat());
	return v.vchat_id.size() + v.vaccess_hash.size();
}
inline mtpTypeId MTPinputGeoChat::type() const {
	return mtpc_inputGeoChat;
}
inline void MTPinputGeoChat::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_inputGeoChat) throw mtpErrorUnexpected(cons, "MTPinputGeoChat");

	if (!data) setData(new MTPDinputGeoChat());
	MTPDinputGeoChat &v(_inputGeoChat());
	v.vchat_id.read(from, end);
	v.vaccess_hash.read(from, end);
}
inline void MTPinputGeoChat::write(mtpBuffer &to) const {
	const MTPDinputGeoChat &v(c_inputGeoChat());
	v.vchat_id.write(to);
	v.vaccess_hash.write(to);
}
inline MTPinputGeoChat::MTPinputGeoChat(MTPDinputGeoChat *_data) : mtpDataOwner(_data) {
}
inline MTPinputGeoChat MTP_inputGeoChat(MTPint _chat_id, const MTPlong &_access_hash) {
	return MTPinputGeoChat(new MTPDinputGeoChat(_chat_id, _access_hash));
}

inline uint32 MTPgeoChatMessage::size() const {
	switch (_type) {
		case mtpc_geoChatMessageEmpty: {
			const MTPDgeoChatMessageEmpty &v(c_geoChatMessageEmpty());
			return v.vchat_id.size() + v.vid.size();
		}
		case mtpc_geoChatMessage: {
			const MTPDgeoChatMessage &v(c_geoChatMessage());
			return v.vchat_id.size() + v.vid.size() + v.vfrom_id.size() + v.vdate.size() + v.vmessage.size() + v.vmedia.size();
		}
		case mtpc_geoChatMessageService: {
			const MTPDgeoChatMessageService &v(c_geoChatMessageService());
			return v.vchat_id.size() + v.vid.size() + v.vfrom_id.size() + v.vdate.size() + v.vaction.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPgeoChatMessage::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPgeoChatMessage::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_geoChatMessageEmpty: _type = cons; {
			if (!data) setData(new MTPDgeoChatMessageEmpty());
			MTPDgeoChatMessageEmpty &v(_geoChatMessageEmpty());
			v.vchat_id.read(from, end);
			v.vid.read(from, end);
		} break;
		case mtpc_geoChatMessage: _type = cons; {
			if (!data) setData(new MTPDgeoChatMessage());
			MTPDgeoChatMessage &v(_geoChatMessage());
			v.vchat_id.read(from, end);
			v.vid.read(from, end);
			v.vfrom_id.read(from, end);
			v.vdate.read(from, end);
			v.vmessage.read(from, end);
			v.vmedia.read(from, end);
		} break;
		case mtpc_geoChatMessageService: _type = cons; {
			if (!data) setData(new MTPDgeoChatMessageService());
			MTPDgeoChatMessageService &v(_geoChatMessageService());
			v.vchat_id.read(from, end);
			v.vid.read(from, end);
			v.vfrom_id.read(from, end);
			v.vdate.read(from, end);
			v.vaction.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPgeoChatMessage");
	}
}
inline void MTPgeoChatMessage::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_geoChatMessageEmpty: {
			const MTPDgeoChatMessageEmpty &v(c_geoChatMessageEmpty());
			v.vchat_id.write(to);
			v.vid.write(to);
		} break;
		case mtpc_geoChatMessage: {
			const MTPDgeoChatMessage &v(c_geoChatMessage());
			v.vchat_id.write(to);
			v.vid.write(to);
			v.vfrom_id.write(to);
			v.vdate.write(to);
			v.vmessage.write(to);
			v.vmedia.write(to);
		} break;
		case mtpc_geoChatMessageService: {
			const MTPDgeoChatMessageService &v(c_geoChatMessageService());
			v.vchat_id.write(to);
			v.vid.write(to);
			v.vfrom_id.write(to);
			v.vdate.write(to);
			v.vaction.write(to);
		} break;
	}
}
inline MTPgeoChatMessage::MTPgeoChatMessage(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_geoChatMessageEmpty: setData(new MTPDgeoChatMessageEmpty()); break;
		case mtpc_geoChatMessage: setData(new MTPDgeoChatMessage()); break;
		case mtpc_geoChatMessageService: setData(new MTPDgeoChatMessageService()); break;
		default: throw mtpErrorBadTypeId(type, "MTPgeoChatMessage");
	}
}
inline MTPgeoChatMessage::MTPgeoChatMessage(MTPDgeoChatMessageEmpty *_data) : mtpDataOwner(_data), _type(mtpc_geoChatMessageEmpty) {
}
inline MTPgeoChatMessage::MTPgeoChatMessage(MTPDgeoChatMessage *_data) : mtpDataOwner(_data), _type(mtpc_geoChatMessage) {
}
inline MTPgeoChatMessage::MTPgeoChatMessage(MTPDgeoChatMessageService *_data) : mtpDataOwner(_data), _type(mtpc_geoChatMessageService) {
}
inline MTPgeoChatMessage MTP_geoChatMessageEmpty(MTPint _chat_id, MTPint _id) {
	return MTPgeoChatMessage(new MTPDgeoChatMessageEmpty(_chat_id, _id));
}
inline MTPgeoChatMessage MTP_geoChatMessage(MTPint _chat_id, MTPint _id, MTPint _from_id, MTPint _date, const MTPstring &_message, const MTPMessageMedia &_media) {
	return MTPgeoChatMessage(new MTPDgeoChatMessage(_chat_id, _id, _from_id, _date, _message, _media));
}
inline MTPgeoChatMessage MTP_geoChatMessageService(MTPint _chat_id, MTPint _id, MTPint _from_id, MTPint _date, const MTPMessageAction &_action) {
	return MTPgeoChatMessage(new MTPDgeoChatMessageService(_chat_id, _id, _from_id, _date, _action));
}

inline MTPgeochats_statedMessage::MTPgeochats_statedMessage() : mtpDataOwner(new MTPDgeochats_statedMessage()) {
}

inline uint32 MTPgeochats_statedMessage::size() const {
	const MTPDgeochats_statedMessage &v(c_geochats_statedMessage());
	return v.vmessage.size() + v.vchats.size() + v.vusers.size() + v.vseq.size();
}
inline mtpTypeId MTPgeochats_statedMessage::type() const {
	return mtpc_geochats_statedMessage;
}
inline void MTPgeochats_statedMessage::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_geochats_statedMessage) throw mtpErrorUnexpected(cons, "MTPgeochats_statedMessage");

	if (!data) setData(new MTPDgeochats_statedMessage());
	MTPDgeochats_statedMessage &v(_geochats_statedMessage());
	v.vmessage.read(from, end);
	v.vchats.read(from, end);
	v.vusers.read(from, end);
	v.vseq.read(from, end);
}
inline void MTPgeochats_statedMessage::write(mtpBuffer &to) const {
	const MTPDgeochats_statedMessage &v(c_geochats_statedMessage());
	v.vmessage.write(to);
	v.vchats.write(to);
	v.vusers.write(to);
	v.vseq.write(to);
}
inline MTPgeochats_statedMessage::MTPgeochats_statedMessage(MTPDgeochats_statedMessage *_data) : mtpDataOwner(_data) {
}
inline MTPgeochats_statedMessage MTP_geochats_statedMessage(const MTPGeoChatMessage &_message, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users, MTPint _seq) {
	return MTPgeochats_statedMessage(new MTPDgeochats_statedMessage(_message, _chats, _users, _seq));
}

inline MTPgeochats_located::MTPgeochats_located() : mtpDataOwner(new MTPDgeochats_located()) {
}

inline uint32 MTPgeochats_located::size() const {
	const MTPDgeochats_located &v(c_geochats_located());
	return v.vresults.size() + v.vmessages.size() + v.vchats.size() + v.vusers.size();
}
inline mtpTypeId MTPgeochats_located::type() const {
	return mtpc_geochats_located;
}
inline void MTPgeochats_located::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_geochats_located) throw mtpErrorUnexpected(cons, "MTPgeochats_located");

	if (!data) setData(new MTPDgeochats_located());
	MTPDgeochats_located &v(_geochats_located());
	v.vresults.read(from, end);
	v.vmessages.read(from, end);
	v.vchats.read(from, end);
	v.vusers.read(from, end);
}
inline void MTPgeochats_located::write(mtpBuffer &to) const {
	const MTPDgeochats_located &v(c_geochats_located());
	v.vresults.write(to);
	v.vmessages.write(to);
	v.vchats.write(to);
	v.vusers.write(to);
}
inline MTPgeochats_located::MTPgeochats_located(MTPDgeochats_located *_data) : mtpDataOwner(_data) {
}
inline MTPgeochats_located MTP_geochats_located(const MTPVector<MTPChatLocated> &_results, const MTPVector<MTPGeoChatMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users) {
	return MTPgeochats_located(new MTPDgeochats_located(_results, _messages, _chats, _users));
}

inline uint32 MTPgeochats_messages::size() const {
	switch (_type) {
		case mtpc_geochats_messages: {
			const MTPDgeochats_messages &v(c_geochats_messages());
			return v.vmessages.size() + v.vchats.size() + v.vusers.size();
		}
		case mtpc_geochats_messagesSlice: {
			const MTPDgeochats_messagesSlice &v(c_geochats_messagesSlice());
			return v.vcount.size() + v.vmessages.size() + v.vchats.size() + v.vusers.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPgeochats_messages::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPgeochats_messages::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_geochats_messages: _type = cons; {
			if (!data) setData(new MTPDgeochats_messages());
			MTPDgeochats_messages &v(_geochats_messages());
			v.vmessages.read(from, end);
			v.vchats.read(from, end);
			v.vusers.read(from, end);
		} break;
		case mtpc_geochats_messagesSlice: _type = cons; {
			if (!data) setData(new MTPDgeochats_messagesSlice());
			MTPDgeochats_messagesSlice &v(_geochats_messagesSlice());
			v.vcount.read(from, end);
			v.vmessages.read(from, end);
			v.vchats.read(from, end);
			v.vusers.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPgeochats_messages");
	}
}
inline void MTPgeochats_messages::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_geochats_messages: {
			const MTPDgeochats_messages &v(c_geochats_messages());
			v.vmessages.write(to);
			v.vchats.write(to);
			v.vusers.write(to);
		} break;
		case mtpc_geochats_messagesSlice: {
			const MTPDgeochats_messagesSlice &v(c_geochats_messagesSlice());
			v.vcount.write(to);
			v.vmessages.write(to);
			v.vchats.write(to);
			v.vusers.write(to);
		} break;
	}
}
inline MTPgeochats_messages::MTPgeochats_messages(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_geochats_messages: setData(new MTPDgeochats_messages()); break;
		case mtpc_geochats_messagesSlice: setData(new MTPDgeochats_messagesSlice()); break;
		default: throw mtpErrorBadTypeId(type, "MTPgeochats_messages");
	}
}
inline MTPgeochats_messages::MTPgeochats_messages(MTPDgeochats_messages *_data) : mtpDataOwner(_data), _type(mtpc_geochats_messages) {
}
inline MTPgeochats_messages::MTPgeochats_messages(MTPDgeochats_messagesSlice *_data) : mtpDataOwner(_data), _type(mtpc_geochats_messagesSlice) {
}
inline MTPgeochats_messages MTP_geochats_messages(const MTPVector<MTPGeoChatMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users) {
	return MTPgeochats_messages(new MTPDgeochats_messages(_messages, _chats, _users));
}
inline MTPgeochats_messages MTP_geochats_messagesSlice(MTPint _count, const MTPVector<MTPGeoChatMessage> &_messages, const MTPVector<MTPChat> &_chats, const MTPVector<MTPUser> &_users) {
	return MTPgeochats_messages(new MTPDgeochats_messagesSlice(_count, _messages, _chats, _users));
}

inline uint32 MTPencryptedChat::size() const {
	switch (_type) {
		case mtpc_encryptedChatEmpty: {
			const MTPDencryptedChatEmpty &v(c_encryptedChatEmpty());
			return v.vid.size();
		}
		case mtpc_encryptedChatWaiting: {
			const MTPDencryptedChatWaiting &v(c_encryptedChatWaiting());
			return v.vid.size() + v.vaccess_hash.size() + v.vdate.size() + v.vadmin_id.size() + v.vparticipant_id.size();
		}
		case mtpc_encryptedChatRequested: {
			const MTPDencryptedChatRequested &v(c_encryptedChatRequested());
			return v.vid.size() + v.vaccess_hash.size() + v.vdate.size() + v.vadmin_id.size() + v.vparticipant_id.size() + v.vg_a.size();
		}
		case mtpc_encryptedChat: {
			const MTPDencryptedChat &v(c_encryptedChat());
			return v.vid.size() + v.vaccess_hash.size() + v.vdate.size() + v.vadmin_id.size() + v.vparticipant_id.size() + v.vg_a_or_b.size() + v.vkey_fingerprint.size();
		}
		case mtpc_encryptedChatDiscarded: {
			const MTPDencryptedChatDiscarded &v(c_encryptedChatDiscarded());
			return v.vid.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPencryptedChat::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPencryptedChat::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_encryptedChatEmpty: _type = cons; {
			if (!data) setData(new MTPDencryptedChatEmpty());
			MTPDencryptedChatEmpty &v(_encryptedChatEmpty());
			v.vid.read(from, end);
		} break;
		case mtpc_encryptedChatWaiting: _type = cons; {
			if (!data) setData(new MTPDencryptedChatWaiting());
			MTPDencryptedChatWaiting &v(_encryptedChatWaiting());
			v.vid.read(from, end);
			v.vaccess_hash.read(from, end);
			v.vdate.read(from, end);
			v.vadmin_id.read(from, end);
			v.vparticipant_id.read(from, end);
		} break;
		case mtpc_encryptedChatRequested: _type = cons; {
			if (!data) setData(new MTPDencryptedChatRequested());
			MTPDencryptedChatRequested &v(_encryptedChatRequested());
			v.vid.read(from, end);
			v.vaccess_hash.read(from, end);
			v.vdate.read(from, end);
			v.vadmin_id.read(from, end);
			v.vparticipant_id.read(from, end);
			v.vg_a.read(from, end);
		} break;
		case mtpc_encryptedChat: _type = cons; {
			if (!data) setData(new MTPDencryptedChat());
			MTPDencryptedChat &v(_encryptedChat());
			v.vid.read(from, end);
			v.vaccess_hash.read(from, end);
			v.vdate.read(from, end);
			v.vadmin_id.read(from, end);
			v.vparticipant_id.read(from, end);
			v.vg_a_or_b.read(from, end);
			v.vkey_fingerprint.read(from, end);
		} break;
		case mtpc_encryptedChatDiscarded: _type = cons; {
			if (!data) setData(new MTPDencryptedChatDiscarded());
			MTPDencryptedChatDiscarded &v(_encryptedChatDiscarded());
			v.vid.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPencryptedChat");
	}
}
inline void MTPencryptedChat::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_encryptedChatEmpty: {
			const MTPDencryptedChatEmpty &v(c_encryptedChatEmpty());
			v.vid.write(to);
		} break;
		case mtpc_encryptedChatWaiting: {
			const MTPDencryptedChatWaiting &v(c_encryptedChatWaiting());
			v.vid.write(to);
			v.vaccess_hash.write(to);
			v.vdate.write(to);
			v.vadmin_id.write(to);
			v.vparticipant_id.write(to);
		} break;
		case mtpc_encryptedChatRequested: {
			const MTPDencryptedChatRequested &v(c_encryptedChatRequested());
			v.vid.write(to);
			v.vaccess_hash.write(to);
			v.vdate.write(to);
			v.vadmin_id.write(to);
			v.vparticipant_id.write(to);
			v.vg_a.write(to);
		} break;
		case mtpc_encryptedChat: {
			const MTPDencryptedChat &v(c_encryptedChat());
			v.vid.write(to);
			v.vaccess_hash.write(to);
			v.vdate.write(to);
			v.vadmin_id.write(to);
			v.vparticipant_id.write(to);
			v.vg_a_or_b.write(to);
			v.vkey_fingerprint.write(to);
		} break;
		case mtpc_encryptedChatDiscarded: {
			const MTPDencryptedChatDiscarded &v(c_encryptedChatDiscarded());
			v.vid.write(to);
		} break;
	}
}
inline MTPencryptedChat::MTPencryptedChat(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_encryptedChatEmpty: setData(new MTPDencryptedChatEmpty()); break;
		case mtpc_encryptedChatWaiting: setData(new MTPDencryptedChatWaiting()); break;
		case mtpc_encryptedChatRequested: setData(new MTPDencryptedChatRequested()); break;
		case mtpc_encryptedChat: setData(new MTPDencryptedChat()); break;
		case mtpc_encryptedChatDiscarded: setData(new MTPDencryptedChatDiscarded()); break;
		default: throw mtpErrorBadTypeId(type, "MTPencryptedChat");
	}
}
inline MTPencryptedChat::MTPencryptedChat(MTPDencryptedChatEmpty *_data) : mtpDataOwner(_data), _type(mtpc_encryptedChatEmpty) {
}
inline MTPencryptedChat::MTPencryptedChat(MTPDencryptedChatWaiting *_data) : mtpDataOwner(_data), _type(mtpc_encryptedChatWaiting) {
}
inline MTPencryptedChat::MTPencryptedChat(MTPDencryptedChatRequested *_data) : mtpDataOwner(_data), _type(mtpc_encryptedChatRequested) {
}
inline MTPencryptedChat::MTPencryptedChat(MTPDencryptedChat *_data) : mtpDataOwner(_data), _type(mtpc_encryptedChat) {
}
inline MTPencryptedChat::MTPencryptedChat(MTPDencryptedChatDiscarded *_data) : mtpDataOwner(_data), _type(mtpc_encryptedChatDiscarded) {
}
inline MTPencryptedChat MTP_encryptedChatEmpty(MTPint _id) {
	return MTPencryptedChat(new MTPDencryptedChatEmpty(_id));
}
inline MTPencryptedChat MTP_encryptedChatWaiting(MTPint _id, const MTPlong &_access_hash, MTPint _date, MTPint _admin_id, MTPint _participant_id) {
	return MTPencryptedChat(new MTPDencryptedChatWaiting(_id, _access_hash, _date, _admin_id, _participant_id));
}
inline MTPencryptedChat MTP_encryptedChatRequested(MTPint _id, const MTPlong &_access_hash, MTPint _date, MTPint _admin_id, MTPint _participant_id, const MTPbytes &_g_a) {
	return MTPencryptedChat(new MTPDencryptedChatRequested(_id, _access_hash, _date, _admin_id, _participant_id, _g_a));
}
inline MTPencryptedChat MTP_encryptedChat(MTPint _id, const MTPlong &_access_hash, MTPint _date, MTPint _admin_id, MTPint _participant_id, const MTPbytes &_g_a_or_b, const MTPlong &_key_fingerprint) {
	return MTPencryptedChat(new MTPDencryptedChat(_id, _access_hash, _date, _admin_id, _participant_id, _g_a_or_b, _key_fingerprint));
}
inline MTPencryptedChat MTP_encryptedChatDiscarded(MTPint _id) {
	return MTPencryptedChat(new MTPDencryptedChatDiscarded(_id));
}

inline MTPinputEncryptedChat::MTPinputEncryptedChat() : mtpDataOwner(new MTPDinputEncryptedChat()) {
}

inline uint32 MTPinputEncryptedChat::size() const {
	const MTPDinputEncryptedChat &v(c_inputEncryptedChat());
	return v.vchat_id.size() + v.vaccess_hash.size();
}
inline mtpTypeId MTPinputEncryptedChat::type() const {
	return mtpc_inputEncryptedChat;
}
inline void MTPinputEncryptedChat::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_inputEncryptedChat) throw mtpErrorUnexpected(cons, "MTPinputEncryptedChat");

	if (!data) setData(new MTPDinputEncryptedChat());
	MTPDinputEncryptedChat &v(_inputEncryptedChat());
	v.vchat_id.read(from, end);
	v.vaccess_hash.read(from, end);
}
inline void MTPinputEncryptedChat::write(mtpBuffer &to) const {
	const MTPDinputEncryptedChat &v(c_inputEncryptedChat());
	v.vchat_id.write(to);
	v.vaccess_hash.write(to);
}
inline MTPinputEncryptedChat::MTPinputEncryptedChat(MTPDinputEncryptedChat *_data) : mtpDataOwner(_data) {
}
inline MTPinputEncryptedChat MTP_inputEncryptedChat(MTPint _chat_id, const MTPlong &_access_hash) {
	return MTPinputEncryptedChat(new MTPDinputEncryptedChat(_chat_id, _access_hash));
}

inline uint32 MTPencryptedFile::size() const {
	switch (_type) {
		case mtpc_encryptedFile: {
			const MTPDencryptedFile &v(c_encryptedFile());
			return v.vid.size() + v.vaccess_hash.size() + v.vsize.size() + v.vdc_id.size() + v.vkey_fingerprint.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPencryptedFile::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPencryptedFile::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_encryptedFileEmpty: _type = cons; break;
		case mtpc_encryptedFile: _type = cons; {
			if (!data) setData(new MTPDencryptedFile());
			MTPDencryptedFile &v(_encryptedFile());
			v.vid.read(from, end);
			v.vaccess_hash.read(from, end);
			v.vsize.read(from, end);
			v.vdc_id.read(from, end);
			v.vkey_fingerprint.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPencryptedFile");
	}
}
inline void MTPencryptedFile::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_encryptedFile: {
			const MTPDencryptedFile &v(c_encryptedFile());
			v.vid.write(to);
			v.vaccess_hash.write(to);
			v.vsize.write(to);
			v.vdc_id.write(to);
			v.vkey_fingerprint.write(to);
		} break;
	}
}
inline MTPencryptedFile::MTPencryptedFile(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_encryptedFileEmpty: break;
		case mtpc_encryptedFile: setData(new MTPDencryptedFile()); break;
		default: throw mtpErrorBadTypeId(type, "MTPencryptedFile");
	}
}
inline MTPencryptedFile::MTPencryptedFile(MTPDencryptedFile *_data) : mtpDataOwner(_data), _type(mtpc_encryptedFile) {
}
inline MTPencryptedFile MTP_encryptedFileEmpty() {
	return MTPencryptedFile(mtpc_encryptedFileEmpty);
}
inline MTPencryptedFile MTP_encryptedFile(const MTPlong &_id, const MTPlong &_access_hash, MTPint _size, MTPint _dc_id, MTPint _key_fingerprint) {
	return MTPencryptedFile(new MTPDencryptedFile(_id, _access_hash, _size, _dc_id, _key_fingerprint));
}

inline uint32 MTPinputEncryptedFile::size() const {
	switch (_type) {
		case mtpc_inputEncryptedFileUploaded: {
			const MTPDinputEncryptedFileUploaded &v(c_inputEncryptedFileUploaded());
			return v.vid.size() + v.vparts.size() + v.vmd5_checksum.size() + v.vkey_fingerprint.size();
		}
		case mtpc_inputEncryptedFile: {
			const MTPDinputEncryptedFile &v(c_inputEncryptedFile());
			return v.vid.size() + v.vaccess_hash.size();
		}
		case mtpc_inputEncryptedFileBigUploaded: {
			const MTPDinputEncryptedFileBigUploaded &v(c_inputEncryptedFileBigUploaded());
			return v.vid.size() + v.vparts.size() + v.vkey_fingerprint.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPinputEncryptedFile::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPinputEncryptedFile::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_inputEncryptedFileEmpty: _type = cons; break;
		case mtpc_inputEncryptedFileUploaded: _type = cons; {
			if (!data) setData(new MTPDinputEncryptedFileUploaded());
			MTPDinputEncryptedFileUploaded &v(_inputEncryptedFileUploaded());
			v.vid.read(from, end);
			v.vparts.read(from, end);
			v.vmd5_checksum.read(from, end);
			v.vkey_fingerprint.read(from, end);
		} break;
		case mtpc_inputEncryptedFile: _type = cons; {
			if (!data) setData(new MTPDinputEncryptedFile());
			MTPDinputEncryptedFile &v(_inputEncryptedFile());
			v.vid.read(from, end);
			v.vaccess_hash.read(from, end);
		} break;
		case mtpc_inputEncryptedFileBigUploaded: _type = cons; {
			if (!data) setData(new MTPDinputEncryptedFileBigUploaded());
			MTPDinputEncryptedFileBigUploaded &v(_inputEncryptedFileBigUploaded());
			v.vid.read(from, end);
			v.vparts.read(from, end);
			v.vkey_fingerprint.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPinputEncryptedFile");
	}
}
inline void MTPinputEncryptedFile::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_inputEncryptedFileUploaded: {
			const MTPDinputEncryptedFileUploaded &v(c_inputEncryptedFileUploaded());
			v.vid.write(to);
			v.vparts.write(to);
			v.vmd5_checksum.write(to);
			v.vkey_fingerprint.write(to);
		} break;
		case mtpc_inputEncryptedFile: {
			const MTPDinputEncryptedFile &v(c_inputEncryptedFile());
			v.vid.write(to);
			v.vaccess_hash.write(to);
		} break;
		case mtpc_inputEncryptedFileBigUploaded: {
			const MTPDinputEncryptedFileBigUploaded &v(c_inputEncryptedFileBigUploaded());
			v.vid.write(to);
			v.vparts.write(to);
			v.vkey_fingerprint.write(to);
		} break;
	}
}
inline MTPinputEncryptedFile::MTPinputEncryptedFile(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_inputEncryptedFileEmpty: break;
		case mtpc_inputEncryptedFileUploaded: setData(new MTPDinputEncryptedFileUploaded()); break;
		case mtpc_inputEncryptedFile: setData(new MTPDinputEncryptedFile()); break;
		case mtpc_inputEncryptedFileBigUploaded: setData(new MTPDinputEncryptedFileBigUploaded()); break;
		default: throw mtpErrorBadTypeId(type, "MTPinputEncryptedFile");
	}
}
inline MTPinputEncryptedFile::MTPinputEncryptedFile(MTPDinputEncryptedFileUploaded *_data) : mtpDataOwner(_data), _type(mtpc_inputEncryptedFileUploaded) {
}
inline MTPinputEncryptedFile::MTPinputEncryptedFile(MTPDinputEncryptedFile *_data) : mtpDataOwner(_data), _type(mtpc_inputEncryptedFile) {
}
inline MTPinputEncryptedFile::MTPinputEncryptedFile(MTPDinputEncryptedFileBigUploaded *_data) : mtpDataOwner(_data), _type(mtpc_inputEncryptedFileBigUploaded) {
}
inline MTPinputEncryptedFile MTP_inputEncryptedFileEmpty() {
	return MTPinputEncryptedFile(mtpc_inputEncryptedFileEmpty);
}
inline MTPinputEncryptedFile MTP_inputEncryptedFileUploaded(const MTPlong &_id, MTPint _parts, const MTPstring &_md5_checksum, MTPint _key_fingerprint) {
	return MTPinputEncryptedFile(new MTPDinputEncryptedFileUploaded(_id, _parts, _md5_checksum, _key_fingerprint));
}
inline MTPinputEncryptedFile MTP_inputEncryptedFile(const MTPlong &_id, const MTPlong &_access_hash) {
	return MTPinputEncryptedFile(new MTPDinputEncryptedFile(_id, _access_hash));
}
inline MTPinputEncryptedFile MTP_inputEncryptedFileBigUploaded(const MTPlong &_id, MTPint _parts, MTPint _key_fingerprint) {
	return MTPinputEncryptedFile(new MTPDinputEncryptedFileBigUploaded(_id, _parts, _key_fingerprint));
}

inline uint32 MTPencryptedMessage::size() const {
	switch (_type) {
		case mtpc_encryptedMessage: {
			const MTPDencryptedMessage &v(c_encryptedMessage());
			return v.vrandom_id.size() + v.vchat_id.size() + v.vdate.size() + v.vbytes.size() + v.vfile.size();
		}
		case mtpc_encryptedMessageService: {
			const MTPDencryptedMessageService &v(c_encryptedMessageService());
			return v.vrandom_id.size() + v.vchat_id.size() + v.vdate.size() + v.vbytes.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPencryptedMessage::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPencryptedMessage::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_encryptedMessage: _type = cons; {
			if (!data) setData(new MTPDencryptedMessage());
			MTPDencryptedMessage &v(_encryptedMessage());
			v.vrandom_id.read(from, end);
			v.vchat_id.read(from, end);
			v.vdate.read(from, end);
			v.vbytes.read(from, end);
			v.vfile.read(from, end);
		} break;
		case mtpc_encryptedMessageService: _type = cons; {
			if (!data) setData(new MTPDencryptedMessageService());
			MTPDencryptedMessageService &v(_encryptedMessageService());
			v.vrandom_id.read(from, end);
			v.vchat_id.read(from, end);
			v.vdate.read(from, end);
			v.vbytes.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPencryptedMessage");
	}
}
inline void MTPencryptedMessage::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_encryptedMessage: {
			const MTPDencryptedMessage &v(c_encryptedMessage());
			v.vrandom_id.write(to);
			v.vchat_id.write(to);
			v.vdate.write(to);
			v.vbytes.write(to);
			v.vfile.write(to);
		} break;
		case mtpc_encryptedMessageService: {
			const MTPDencryptedMessageService &v(c_encryptedMessageService());
			v.vrandom_id.write(to);
			v.vchat_id.write(to);
			v.vdate.write(to);
			v.vbytes.write(to);
		} break;
	}
}
inline MTPencryptedMessage::MTPencryptedMessage(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_encryptedMessage: setData(new MTPDencryptedMessage()); break;
		case mtpc_encryptedMessageService: setData(new MTPDencryptedMessageService()); break;
		default: throw mtpErrorBadTypeId(type, "MTPencryptedMessage");
	}
}
inline MTPencryptedMessage::MTPencryptedMessage(MTPDencryptedMessage *_data) : mtpDataOwner(_data), _type(mtpc_encryptedMessage) {
}
inline MTPencryptedMessage::MTPencryptedMessage(MTPDencryptedMessageService *_data) : mtpDataOwner(_data), _type(mtpc_encryptedMessageService) {
}
inline MTPencryptedMessage MTP_encryptedMessage(const MTPlong &_random_id, MTPint _chat_id, MTPint _date, const MTPbytes &_bytes, const MTPEncryptedFile &_file) {
	return MTPencryptedMessage(new MTPDencryptedMessage(_random_id, _chat_id, _date, _bytes, _file));
}
inline MTPencryptedMessage MTP_encryptedMessageService(const MTPlong &_random_id, MTPint _chat_id, MTPint _date, const MTPbytes &_bytes) {
	return MTPencryptedMessage(new MTPDencryptedMessageService(_random_id, _chat_id, _date, _bytes));
}

inline MTPdecryptedMessageLayer::MTPdecryptedMessageLayer() : mtpDataOwner(new MTPDdecryptedMessageLayer()) {
}

inline uint32 MTPdecryptedMessageLayer::size() const {
	const MTPDdecryptedMessageLayer &v(c_decryptedMessageLayer());
	return v.vlayer.size() + v.vmessage.size();
}
inline mtpTypeId MTPdecryptedMessageLayer::type() const {
	return mtpc_decryptedMessageLayer;
}
inline void MTPdecryptedMessageLayer::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_decryptedMessageLayer) throw mtpErrorUnexpected(cons, "MTPdecryptedMessageLayer");

	if (!data) setData(new MTPDdecryptedMessageLayer());
	MTPDdecryptedMessageLayer &v(_decryptedMessageLayer());
	v.vlayer.read(from, end);
	v.vmessage.read(from, end);
}
inline void MTPdecryptedMessageLayer::write(mtpBuffer &to) const {
	const MTPDdecryptedMessageLayer &v(c_decryptedMessageLayer());
	v.vlayer.write(to);
	v.vmessage.write(to);
}
inline MTPdecryptedMessageLayer::MTPdecryptedMessageLayer(MTPDdecryptedMessageLayer *_data) : mtpDataOwner(_data) {
}
inline MTPdecryptedMessageLayer MTP_decryptedMessageLayer(MTPint _layer, const MTPDecryptedMessage &_message) {
	return MTPdecryptedMessageLayer(new MTPDdecryptedMessageLayer(_layer, _message));
}

inline uint32 MTPdecryptedMessage::size() const {
	switch (_type) {
		case mtpc_decryptedMessage: {
			const MTPDdecryptedMessage &v(c_decryptedMessage());
			return v.vrandom_id.size() + v.vrandom_bytes.size() + v.vmessage.size() + v.vmedia.size();
		}
		case mtpc_decryptedMessageService: {
			const MTPDdecryptedMessageService &v(c_decryptedMessageService());
			return v.vrandom_id.size() + v.vrandom_bytes.size() + v.vaction.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPdecryptedMessage::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPdecryptedMessage::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_decryptedMessage: _type = cons; {
			if (!data) setData(new MTPDdecryptedMessage());
			MTPDdecryptedMessage &v(_decryptedMessage());
			v.vrandom_id.read(from, end);
			v.vrandom_bytes.read(from, end);
			v.vmessage.read(from, end);
			v.vmedia.read(from, end);
		} break;
		case mtpc_decryptedMessageService: _type = cons; {
			if (!data) setData(new MTPDdecryptedMessageService());
			MTPDdecryptedMessageService &v(_decryptedMessageService());
			v.vrandom_id.read(from, end);
			v.vrandom_bytes.read(from, end);
			v.vaction.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPdecryptedMessage");
	}
}
inline void MTPdecryptedMessage::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_decryptedMessage: {
			const MTPDdecryptedMessage &v(c_decryptedMessage());
			v.vrandom_id.write(to);
			v.vrandom_bytes.write(to);
			v.vmessage.write(to);
			v.vmedia.write(to);
		} break;
		case mtpc_decryptedMessageService: {
			const MTPDdecryptedMessageService &v(c_decryptedMessageService());
			v.vrandom_id.write(to);
			v.vrandom_bytes.write(to);
			v.vaction.write(to);
		} break;
	}
}
inline MTPdecryptedMessage::MTPdecryptedMessage(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_decryptedMessage: setData(new MTPDdecryptedMessage()); break;
		case mtpc_decryptedMessageService: setData(new MTPDdecryptedMessageService()); break;
		default: throw mtpErrorBadTypeId(type, "MTPdecryptedMessage");
	}
}
inline MTPdecryptedMessage::MTPdecryptedMessage(MTPDdecryptedMessage *_data) : mtpDataOwner(_data), _type(mtpc_decryptedMessage) {
}
inline MTPdecryptedMessage::MTPdecryptedMessage(MTPDdecryptedMessageService *_data) : mtpDataOwner(_data), _type(mtpc_decryptedMessageService) {
}
inline MTPdecryptedMessage MTP_decryptedMessage(const MTPlong &_random_id, const MTPbytes &_random_bytes, const MTPstring &_message, const MTPDecryptedMessageMedia &_media) {
	return MTPdecryptedMessage(new MTPDdecryptedMessage(_random_id, _random_bytes, _message, _media));
}
inline MTPdecryptedMessage MTP_decryptedMessageService(const MTPlong &_random_id, const MTPbytes &_random_bytes, const MTPDecryptedMessageAction &_action) {
	return MTPdecryptedMessage(new MTPDdecryptedMessageService(_random_id, _random_bytes, _action));
}

inline uint32 MTPdecryptedMessageMedia::size() const {
	switch (_type) {
		case mtpc_decryptedMessageMediaPhoto: {
			const MTPDdecryptedMessageMediaPhoto &v(c_decryptedMessageMediaPhoto());
			return v.vthumb.size() + v.vthumb_w.size() + v.vthumb_h.size() + v.vw.size() + v.vh.size() + v.vsize.size() + v.vkey.size() + v.viv.size();
		}
		case mtpc_decryptedMessageMediaVideo: {
			const MTPDdecryptedMessageMediaVideo &v(c_decryptedMessageMediaVideo());
			return v.vthumb.size() + v.vthumb_w.size() + v.vthumb_h.size() + v.vduration.size() + v.vmime_type.size() + v.vw.size() + v.vh.size() + v.vsize.size() + v.vkey.size() + v.viv.size();
		}
		case mtpc_decryptedMessageMediaGeoPoint: {
			const MTPDdecryptedMessageMediaGeoPoint &v(c_decryptedMessageMediaGeoPoint());
			return v.vlat.size() + v.vlong.size();
		}
		case mtpc_decryptedMessageMediaContact: {
			const MTPDdecryptedMessageMediaContact &v(c_decryptedMessageMediaContact());
			return v.vphone_number.size() + v.vfirst_name.size() + v.vlast_name.size() + v.vuser_id.size();
		}
		case mtpc_decryptedMessageMediaDocument: {
			const MTPDdecryptedMessageMediaDocument &v(c_decryptedMessageMediaDocument());
			return v.vthumb.size() + v.vthumb_w.size() + v.vthumb_h.size() + v.vfile_name.size() + v.vmime_type.size() + v.vsize.size() + v.vkey.size() + v.viv.size();
		}
		case mtpc_decryptedMessageMediaAudio: {
			const MTPDdecryptedMessageMediaAudio &v(c_decryptedMessageMediaAudio());
			return v.vduration.size() + v.vmime_type.size() + v.vsize.size() + v.vkey.size() + v.viv.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPdecryptedMessageMedia::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPdecryptedMessageMedia::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_decryptedMessageMediaEmpty: _type = cons; break;
		case mtpc_decryptedMessageMediaPhoto: _type = cons; {
			if (!data) setData(new MTPDdecryptedMessageMediaPhoto());
			MTPDdecryptedMessageMediaPhoto &v(_decryptedMessageMediaPhoto());
			v.vthumb.read(from, end);
			v.vthumb_w.read(from, end);
			v.vthumb_h.read(from, end);
			v.vw.read(from, end);
			v.vh.read(from, end);
			v.vsize.read(from, end);
			v.vkey.read(from, end);
			v.viv.read(from, end);
		} break;
		case mtpc_decryptedMessageMediaVideo: _type = cons; {
			if (!data) setData(new MTPDdecryptedMessageMediaVideo());
			MTPDdecryptedMessageMediaVideo &v(_decryptedMessageMediaVideo());
			v.vthumb.read(from, end);
			v.vthumb_w.read(from, end);
			v.vthumb_h.read(from, end);
			v.vduration.read(from, end);
			v.vmime_type.read(from, end);
			v.vw.read(from, end);
			v.vh.read(from, end);
			v.vsize.read(from, end);
			v.vkey.read(from, end);
			v.viv.read(from, end);
		} break;
		case mtpc_decryptedMessageMediaGeoPoint: _type = cons; {
			if (!data) setData(new MTPDdecryptedMessageMediaGeoPoint());
			MTPDdecryptedMessageMediaGeoPoint &v(_decryptedMessageMediaGeoPoint());
			v.vlat.read(from, end);
			v.vlong.read(from, end);
		} break;
		case mtpc_decryptedMessageMediaContact: _type = cons; {
			if (!data) setData(new MTPDdecryptedMessageMediaContact());
			MTPDdecryptedMessageMediaContact &v(_decryptedMessageMediaContact());
			v.vphone_number.read(from, end);
			v.vfirst_name.read(from, end);
			v.vlast_name.read(from, end);
			v.vuser_id.read(from, end);
		} break;
		case mtpc_decryptedMessageMediaDocument: _type = cons; {
			if (!data) setData(new MTPDdecryptedMessageMediaDocument());
			MTPDdecryptedMessageMediaDocument &v(_decryptedMessageMediaDocument());
			v.vthumb.read(from, end);
			v.vthumb_w.read(from, end);
			v.vthumb_h.read(from, end);
			v.vfile_name.read(from, end);
			v.vmime_type.read(from, end);
			v.vsize.read(from, end);
			v.vkey.read(from, end);
			v.viv.read(from, end);
		} break;
		case mtpc_decryptedMessageMediaAudio: _type = cons; {
			if (!data) setData(new MTPDdecryptedMessageMediaAudio());
			MTPDdecryptedMessageMediaAudio &v(_decryptedMessageMediaAudio());
			v.vduration.read(from, end);
			v.vmime_type.read(from, end);
			v.vsize.read(from, end);
			v.vkey.read(from, end);
			v.viv.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPdecryptedMessageMedia");
	}
}
inline void MTPdecryptedMessageMedia::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_decryptedMessageMediaPhoto: {
			const MTPDdecryptedMessageMediaPhoto &v(c_decryptedMessageMediaPhoto());
			v.vthumb.write(to);
			v.vthumb_w.write(to);
			v.vthumb_h.write(to);
			v.vw.write(to);
			v.vh.write(to);
			v.vsize.write(to);
			v.vkey.write(to);
			v.viv.write(to);
		} break;
		case mtpc_decryptedMessageMediaVideo: {
			const MTPDdecryptedMessageMediaVideo &v(c_decryptedMessageMediaVideo());
			v.vthumb.write(to);
			v.vthumb_w.write(to);
			v.vthumb_h.write(to);
			v.vduration.write(to);
			v.vmime_type.write(to);
			v.vw.write(to);
			v.vh.write(to);
			v.vsize.write(to);
			v.vkey.write(to);
			v.viv.write(to);
		} break;
		case mtpc_decryptedMessageMediaGeoPoint: {
			const MTPDdecryptedMessageMediaGeoPoint &v(c_decryptedMessageMediaGeoPoint());
			v.vlat.write(to);
			v.vlong.write(to);
		} break;
		case mtpc_decryptedMessageMediaContact: {
			const MTPDdecryptedMessageMediaContact &v(c_decryptedMessageMediaContact());
			v.vphone_number.write(to);
			v.vfirst_name.write(to);
			v.vlast_name.write(to);
			v.vuser_id.write(to);
		} break;
		case mtpc_decryptedMessageMediaDocument: {
			const MTPDdecryptedMessageMediaDocument &v(c_decryptedMessageMediaDocument());
			v.vthumb.write(to);
			v.vthumb_w.write(to);
			v.vthumb_h.write(to);
			v.vfile_name.write(to);
			v.vmime_type.write(to);
			v.vsize.write(to);
			v.vkey.write(to);
			v.viv.write(to);
		} break;
		case mtpc_decryptedMessageMediaAudio: {
			const MTPDdecryptedMessageMediaAudio &v(c_decryptedMessageMediaAudio());
			v.vduration.write(to);
			v.vmime_type.write(to);
			v.vsize.write(to);
			v.vkey.write(to);
			v.viv.write(to);
		} break;
	}
}
inline MTPdecryptedMessageMedia::MTPdecryptedMessageMedia(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_decryptedMessageMediaEmpty: break;
		case mtpc_decryptedMessageMediaPhoto: setData(new MTPDdecryptedMessageMediaPhoto()); break;
		case mtpc_decryptedMessageMediaVideo: setData(new MTPDdecryptedMessageMediaVideo()); break;
		case mtpc_decryptedMessageMediaGeoPoint: setData(new MTPDdecryptedMessageMediaGeoPoint()); break;
		case mtpc_decryptedMessageMediaContact: setData(new MTPDdecryptedMessageMediaContact()); break;
		case mtpc_decryptedMessageMediaDocument: setData(new MTPDdecryptedMessageMediaDocument()); break;
		case mtpc_decryptedMessageMediaAudio: setData(new MTPDdecryptedMessageMediaAudio()); break;
		default: throw mtpErrorBadTypeId(type, "MTPdecryptedMessageMedia");
	}
}
inline MTPdecryptedMessageMedia::MTPdecryptedMessageMedia(MTPDdecryptedMessageMediaPhoto *_data) : mtpDataOwner(_data), _type(mtpc_decryptedMessageMediaPhoto) {
}
inline MTPdecryptedMessageMedia::MTPdecryptedMessageMedia(MTPDdecryptedMessageMediaVideo *_data) : mtpDataOwner(_data), _type(mtpc_decryptedMessageMediaVideo) {
}
inline MTPdecryptedMessageMedia::MTPdecryptedMessageMedia(MTPDdecryptedMessageMediaGeoPoint *_data) : mtpDataOwner(_data), _type(mtpc_decryptedMessageMediaGeoPoint) {
}
inline MTPdecryptedMessageMedia::MTPdecryptedMessageMedia(MTPDdecryptedMessageMediaContact *_data) : mtpDataOwner(_data), _type(mtpc_decryptedMessageMediaContact) {
}
inline MTPdecryptedMessageMedia::MTPdecryptedMessageMedia(MTPDdecryptedMessageMediaDocument *_data) : mtpDataOwner(_data), _type(mtpc_decryptedMessageMediaDocument) {
}
inline MTPdecryptedMessageMedia::MTPdecryptedMessageMedia(MTPDdecryptedMessageMediaAudio *_data) : mtpDataOwner(_data), _type(mtpc_decryptedMessageMediaAudio) {
}
inline MTPdecryptedMessageMedia MTP_decryptedMessageMediaEmpty() {
	return MTPdecryptedMessageMedia(mtpc_decryptedMessageMediaEmpty);
}
inline MTPdecryptedMessageMedia MTP_decryptedMessageMediaPhoto(const MTPbytes &_thumb, MTPint _thumb_w, MTPint _thumb_h, MTPint _w, MTPint _h, MTPint _size, const MTPbytes &_key, const MTPbytes &_iv) {
	return MTPdecryptedMessageMedia(new MTPDdecryptedMessageMediaPhoto(_thumb, _thumb_w, _thumb_h, _w, _h, _size, _key, _iv));
}
inline MTPdecryptedMessageMedia MTP_decryptedMessageMediaVideo(const MTPbytes &_thumb, MTPint _thumb_w, MTPint _thumb_h, MTPint _duration, const MTPstring &_mime_type, MTPint _w, MTPint _h, MTPint _size, const MTPbytes &_key, const MTPbytes &_iv) {
	return MTPdecryptedMessageMedia(new MTPDdecryptedMessageMediaVideo(_thumb, _thumb_w, _thumb_h, _duration, _mime_type, _w, _h, _size, _key, _iv));
}
inline MTPdecryptedMessageMedia MTP_decryptedMessageMediaGeoPoint(const MTPdouble &_lat, const MTPdouble &_long) {
	return MTPdecryptedMessageMedia(new MTPDdecryptedMessageMediaGeoPoint(_lat, _long));
}
inline MTPdecryptedMessageMedia MTP_decryptedMessageMediaContact(const MTPstring &_phone_number, const MTPstring &_first_name, const MTPstring &_last_name, MTPint _user_id) {
	return MTPdecryptedMessageMedia(new MTPDdecryptedMessageMediaContact(_phone_number, _first_name, _last_name, _user_id));
}
inline MTPdecryptedMessageMedia MTP_decryptedMessageMediaDocument(const MTPbytes &_thumb, MTPint _thumb_w, MTPint _thumb_h, const MTPstring &_file_name, const MTPstring &_mime_type, MTPint _size, const MTPbytes &_key, const MTPbytes &_iv) {
	return MTPdecryptedMessageMedia(new MTPDdecryptedMessageMediaDocument(_thumb, _thumb_w, _thumb_h, _file_name, _mime_type, _size, _key, _iv));
}
inline MTPdecryptedMessageMedia MTP_decryptedMessageMediaAudio(MTPint _duration, const MTPstring &_mime_type, MTPint _size, const MTPbytes &_key, const MTPbytes &_iv) {
	return MTPdecryptedMessageMedia(new MTPDdecryptedMessageMediaAudio(_duration, _mime_type, _size, _key, _iv));
}

inline uint32 MTPdecryptedMessageAction::size() const {
	switch (_type) {
		case mtpc_decryptedMessageActionSetMessageTTL: {
			const MTPDdecryptedMessageActionSetMessageTTL &v(c_decryptedMessageActionSetMessageTTL());
			return v.vttl_seconds.size();
		}
		case mtpc_decryptedMessageActionReadMessages: {
			const MTPDdecryptedMessageActionReadMessages &v(c_decryptedMessageActionReadMessages());
			return v.vrandom_ids.size();
		}
		case mtpc_decryptedMessageActionDeleteMessages: {
			const MTPDdecryptedMessageActionDeleteMessages &v(c_decryptedMessageActionDeleteMessages());
			return v.vrandom_ids.size();
		}
		case mtpc_decryptedMessageActionScreenshotMessages: {
			const MTPDdecryptedMessageActionScreenshotMessages &v(c_decryptedMessageActionScreenshotMessages());
			return v.vrandom_ids.size();
		}
		case mtpc_decryptedMessageActionNotifyLayer: {
			const MTPDdecryptedMessageActionNotifyLayer &v(c_decryptedMessageActionNotifyLayer());
			return v.vlayer.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPdecryptedMessageAction::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPdecryptedMessageAction::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_decryptedMessageActionSetMessageTTL: _type = cons; {
			if (!data) setData(new MTPDdecryptedMessageActionSetMessageTTL());
			MTPDdecryptedMessageActionSetMessageTTL &v(_decryptedMessageActionSetMessageTTL());
			v.vttl_seconds.read(from, end);
		} break;
		case mtpc_decryptedMessageActionReadMessages: _type = cons; {
			if (!data) setData(new MTPDdecryptedMessageActionReadMessages());
			MTPDdecryptedMessageActionReadMessages &v(_decryptedMessageActionReadMessages());
			v.vrandom_ids.read(from, end);
		} break;
		case mtpc_decryptedMessageActionDeleteMessages: _type = cons; {
			if (!data) setData(new MTPDdecryptedMessageActionDeleteMessages());
			MTPDdecryptedMessageActionDeleteMessages &v(_decryptedMessageActionDeleteMessages());
			v.vrandom_ids.read(from, end);
		} break;
		case mtpc_decryptedMessageActionScreenshotMessages: _type = cons; {
			if (!data) setData(new MTPDdecryptedMessageActionScreenshotMessages());
			MTPDdecryptedMessageActionScreenshotMessages &v(_decryptedMessageActionScreenshotMessages());
			v.vrandom_ids.read(from, end);
		} break;
		case mtpc_decryptedMessageActionFlushHistory: _type = cons; break;
		case mtpc_decryptedMessageActionNotifyLayer: _type = cons; {
			if (!data) setData(new MTPDdecryptedMessageActionNotifyLayer());
			MTPDdecryptedMessageActionNotifyLayer &v(_decryptedMessageActionNotifyLayer());
			v.vlayer.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPdecryptedMessageAction");
	}
}
inline void MTPdecryptedMessageAction::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_decryptedMessageActionSetMessageTTL: {
			const MTPDdecryptedMessageActionSetMessageTTL &v(c_decryptedMessageActionSetMessageTTL());
			v.vttl_seconds.write(to);
		} break;
		case mtpc_decryptedMessageActionReadMessages: {
			const MTPDdecryptedMessageActionReadMessages &v(c_decryptedMessageActionReadMessages());
			v.vrandom_ids.write(to);
		} break;
		case mtpc_decryptedMessageActionDeleteMessages: {
			const MTPDdecryptedMessageActionDeleteMessages &v(c_decryptedMessageActionDeleteMessages());
			v.vrandom_ids.write(to);
		} break;
		case mtpc_decryptedMessageActionScreenshotMessages: {
			const MTPDdecryptedMessageActionScreenshotMessages &v(c_decryptedMessageActionScreenshotMessages());
			v.vrandom_ids.write(to);
		} break;
		case mtpc_decryptedMessageActionNotifyLayer: {
			const MTPDdecryptedMessageActionNotifyLayer &v(c_decryptedMessageActionNotifyLayer());
			v.vlayer.write(to);
		} break;
	}
}
inline MTPdecryptedMessageAction::MTPdecryptedMessageAction(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_decryptedMessageActionSetMessageTTL: setData(new MTPDdecryptedMessageActionSetMessageTTL()); break;
		case mtpc_decryptedMessageActionReadMessages: setData(new MTPDdecryptedMessageActionReadMessages()); break;
		case mtpc_decryptedMessageActionDeleteMessages: setData(new MTPDdecryptedMessageActionDeleteMessages()); break;
		case mtpc_decryptedMessageActionScreenshotMessages: setData(new MTPDdecryptedMessageActionScreenshotMessages()); break;
		case mtpc_decryptedMessageActionFlushHistory: break;
		case mtpc_decryptedMessageActionNotifyLayer: setData(new MTPDdecryptedMessageActionNotifyLayer()); break;
		default: throw mtpErrorBadTypeId(type, "MTPdecryptedMessageAction");
	}
}
inline MTPdecryptedMessageAction::MTPdecryptedMessageAction(MTPDdecryptedMessageActionSetMessageTTL *_data) : mtpDataOwner(_data), _type(mtpc_decryptedMessageActionSetMessageTTL) {
}
inline MTPdecryptedMessageAction::MTPdecryptedMessageAction(MTPDdecryptedMessageActionReadMessages *_data) : mtpDataOwner(_data), _type(mtpc_decryptedMessageActionReadMessages) {
}
inline MTPdecryptedMessageAction::MTPdecryptedMessageAction(MTPDdecryptedMessageActionDeleteMessages *_data) : mtpDataOwner(_data), _type(mtpc_decryptedMessageActionDeleteMessages) {
}
inline MTPdecryptedMessageAction::MTPdecryptedMessageAction(MTPDdecryptedMessageActionScreenshotMessages *_data) : mtpDataOwner(_data), _type(mtpc_decryptedMessageActionScreenshotMessages) {
}
inline MTPdecryptedMessageAction::MTPdecryptedMessageAction(MTPDdecryptedMessageActionNotifyLayer *_data) : mtpDataOwner(_data), _type(mtpc_decryptedMessageActionNotifyLayer) {
}
inline MTPdecryptedMessageAction MTP_decryptedMessageActionSetMessageTTL(MTPint _ttl_seconds) {
	return MTPdecryptedMessageAction(new MTPDdecryptedMessageActionSetMessageTTL(_ttl_seconds));
}
inline MTPdecryptedMessageAction MTP_decryptedMessageActionReadMessages(const MTPVector<MTPlong> &_random_ids) {
	return MTPdecryptedMessageAction(new MTPDdecryptedMessageActionReadMessages(_random_ids));
}
inline MTPdecryptedMessageAction MTP_decryptedMessageActionDeleteMessages(const MTPVector<MTPlong> &_random_ids) {
	return MTPdecryptedMessageAction(new MTPDdecryptedMessageActionDeleteMessages(_random_ids));
}
inline MTPdecryptedMessageAction MTP_decryptedMessageActionScreenshotMessages(const MTPVector<MTPlong> &_random_ids) {
	return MTPdecryptedMessageAction(new MTPDdecryptedMessageActionScreenshotMessages(_random_ids));
}
inline MTPdecryptedMessageAction MTP_decryptedMessageActionFlushHistory() {
	return MTPdecryptedMessageAction(mtpc_decryptedMessageActionFlushHistory);
}
inline MTPdecryptedMessageAction MTP_decryptedMessageActionNotifyLayer(MTPint _layer) {
	return MTPdecryptedMessageAction(new MTPDdecryptedMessageActionNotifyLayer(_layer));
}

inline uint32 MTPmessages_dhConfig::size() const {
	switch (_type) {
		case mtpc_messages_dhConfigNotModified: {
			const MTPDmessages_dhConfigNotModified &v(c_messages_dhConfigNotModified());
			return v.vrandom.size();
		}
		case mtpc_messages_dhConfig: {
			const MTPDmessages_dhConfig &v(c_messages_dhConfig());
			return v.vg.size() + v.vp.size() + v.vversion.size() + v.vrandom.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPmessages_dhConfig::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPmessages_dhConfig::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_messages_dhConfigNotModified: _type = cons; {
			if (!data) setData(new MTPDmessages_dhConfigNotModified());
			MTPDmessages_dhConfigNotModified &v(_messages_dhConfigNotModified());
			v.vrandom.read(from, end);
		} break;
		case mtpc_messages_dhConfig: _type = cons; {
			if (!data) setData(new MTPDmessages_dhConfig());
			MTPDmessages_dhConfig &v(_messages_dhConfig());
			v.vg.read(from, end);
			v.vp.read(from, end);
			v.vversion.read(from, end);
			v.vrandom.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPmessages_dhConfig");
	}
}
inline void MTPmessages_dhConfig::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_messages_dhConfigNotModified: {
			const MTPDmessages_dhConfigNotModified &v(c_messages_dhConfigNotModified());
			v.vrandom.write(to);
		} break;
		case mtpc_messages_dhConfig: {
			const MTPDmessages_dhConfig &v(c_messages_dhConfig());
			v.vg.write(to);
			v.vp.write(to);
			v.vversion.write(to);
			v.vrandom.write(to);
		} break;
	}
}
inline MTPmessages_dhConfig::MTPmessages_dhConfig(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_messages_dhConfigNotModified: setData(new MTPDmessages_dhConfigNotModified()); break;
		case mtpc_messages_dhConfig: setData(new MTPDmessages_dhConfig()); break;
		default: throw mtpErrorBadTypeId(type, "MTPmessages_dhConfig");
	}
}
inline MTPmessages_dhConfig::MTPmessages_dhConfig(MTPDmessages_dhConfigNotModified *_data) : mtpDataOwner(_data), _type(mtpc_messages_dhConfigNotModified) {
}
inline MTPmessages_dhConfig::MTPmessages_dhConfig(MTPDmessages_dhConfig *_data) : mtpDataOwner(_data), _type(mtpc_messages_dhConfig) {
}
inline MTPmessages_dhConfig MTP_messages_dhConfigNotModified(const MTPbytes &_random) {
	return MTPmessages_dhConfig(new MTPDmessages_dhConfigNotModified(_random));
}
inline MTPmessages_dhConfig MTP_messages_dhConfig(MTPint _g, const MTPbytes &_p, MTPint _version, const MTPbytes &_random) {
	return MTPmessages_dhConfig(new MTPDmessages_dhConfig(_g, _p, _version, _random));
}

inline uint32 MTPmessages_sentEncryptedMessage::size() const {
	switch (_type) {
		case mtpc_messages_sentEncryptedMessage: {
			const MTPDmessages_sentEncryptedMessage &v(c_messages_sentEncryptedMessage());
			return v.vdate.size();
		}
		case mtpc_messages_sentEncryptedFile: {
			const MTPDmessages_sentEncryptedFile &v(c_messages_sentEncryptedFile());
			return v.vdate.size() + v.vfile.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPmessages_sentEncryptedMessage::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPmessages_sentEncryptedMessage::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_messages_sentEncryptedMessage: _type = cons; {
			if (!data) setData(new MTPDmessages_sentEncryptedMessage());
			MTPDmessages_sentEncryptedMessage &v(_messages_sentEncryptedMessage());
			v.vdate.read(from, end);
		} break;
		case mtpc_messages_sentEncryptedFile: _type = cons; {
			if (!data) setData(new MTPDmessages_sentEncryptedFile());
			MTPDmessages_sentEncryptedFile &v(_messages_sentEncryptedFile());
			v.vdate.read(from, end);
			v.vfile.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPmessages_sentEncryptedMessage");
	}
}
inline void MTPmessages_sentEncryptedMessage::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_messages_sentEncryptedMessage: {
			const MTPDmessages_sentEncryptedMessage &v(c_messages_sentEncryptedMessage());
			v.vdate.write(to);
		} break;
		case mtpc_messages_sentEncryptedFile: {
			const MTPDmessages_sentEncryptedFile &v(c_messages_sentEncryptedFile());
			v.vdate.write(to);
			v.vfile.write(to);
		} break;
	}
}
inline MTPmessages_sentEncryptedMessage::MTPmessages_sentEncryptedMessage(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_messages_sentEncryptedMessage: setData(new MTPDmessages_sentEncryptedMessage()); break;
		case mtpc_messages_sentEncryptedFile: setData(new MTPDmessages_sentEncryptedFile()); break;
		default: throw mtpErrorBadTypeId(type, "MTPmessages_sentEncryptedMessage");
	}
}
inline MTPmessages_sentEncryptedMessage::MTPmessages_sentEncryptedMessage(MTPDmessages_sentEncryptedMessage *_data) : mtpDataOwner(_data), _type(mtpc_messages_sentEncryptedMessage) {
}
inline MTPmessages_sentEncryptedMessage::MTPmessages_sentEncryptedMessage(MTPDmessages_sentEncryptedFile *_data) : mtpDataOwner(_data), _type(mtpc_messages_sentEncryptedFile) {
}
inline MTPmessages_sentEncryptedMessage MTP_messages_sentEncryptedMessage(MTPint _date) {
	return MTPmessages_sentEncryptedMessage(new MTPDmessages_sentEncryptedMessage(_date));
}
inline MTPmessages_sentEncryptedMessage MTP_messages_sentEncryptedFile(MTPint _date, const MTPEncryptedFile &_file) {
	return MTPmessages_sentEncryptedMessage(new MTPDmessages_sentEncryptedFile(_date, _file));
}

inline uint32 MTPinputAudio::size() const {
	switch (_type) {
		case mtpc_inputAudio: {
			const MTPDinputAudio &v(c_inputAudio());
			return v.vid.size() + v.vaccess_hash.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPinputAudio::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPinputAudio::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_inputAudioEmpty: _type = cons; break;
		case mtpc_inputAudio: _type = cons; {
			if (!data) setData(new MTPDinputAudio());
			MTPDinputAudio &v(_inputAudio());
			v.vid.read(from, end);
			v.vaccess_hash.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPinputAudio");
	}
}
inline void MTPinputAudio::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_inputAudio: {
			const MTPDinputAudio &v(c_inputAudio());
			v.vid.write(to);
			v.vaccess_hash.write(to);
		} break;
	}
}
inline MTPinputAudio::MTPinputAudio(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_inputAudioEmpty: break;
		case mtpc_inputAudio: setData(new MTPDinputAudio()); break;
		default: throw mtpErrorBadTypeId(type, "MTPinputAudio");
	}
}
inline MTPinputAudio::MTPinputAudio(MTPDinputAudio *_data) : mtpDataOwner(_data), _type(mtpc_inputAudio) {
}
inline MTPinputAudio MTP_inputAudioEmpty() {
	return MTPinputAudio(mtpc_inputAudioEmpty);
}
inline MTPinputAudio MTP_inputAudio(const MTPlong &_id, const MTPlong &_access_hash) {
	return MTPinputAudio(new MTPDinputAudio(_id, _access_hash));
}

inline uint32 MTPinputDocument::size() const {
	switch (_type) {
		case mtpc_inputDocument: {
			const MTPDinputDocument &v(c_inputDocument());
			return v.vid.size() + v.vaccess_hash.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPinputDocument::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPinputDocument::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_inputDocumentEmpty: _type = cons; break;
		case mtpc_inputDocument: _type = cons; {
			if (!data) setData(new MTPDinputDocument());
			MTPDinputDocument &v(_inputDocument());
			v.vid.read(from, end);
			v.vaccess_hash.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPinputDocument");
	}
}
inline void MTPinputDocument::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_inputDocument: {
			const MTPDinputDocument &v(c_inputDocument());
			v.vid.write(to);
			v.vaccess_hash.write(to);
		} break;
	}
}
inline MTPinputDocument::MTPinputDocument(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_inputDocumentEmpty: break;
		case mtpc_inputDocument: setData(new MTPDinputDocument()); break;
		default: throw mtpErrorBadTypeId(type, "MTPinputDocument");
	}
}
inline MTPinputDocument::MTPinputDocument(MTPDinputDocument *_data) : mtpDataOwner(_data), _type(mtpc_inputDocument) {
}
inline MTPinputDocument MTP_inputDocumentEmpty() {
	return MTPinputDocument(mtpc_inputDocumentEmpty);
}
inline MTPinputDocument MTP_inputDocument(const MTPlong &_id, const MTPlong &_access_hash) {
	return MTPinputDocument(new MTPDinputDocument(_id, _access_hash));
}

inline uint32 MTPaudio::size() const {
	switch (_type) {
		case mtpc_audioEmpty: {
			const MTPDaudioEmpty &v(c_audioEmpty());
			return v.vid.size();
		}
		case mtpc_audio: {
			const MTPDaudio &v(c_audio());
			return v.vid.size() + v.vaccess_hash.size() + v.vuser_id.size() + v.vdate.size() + v.vduration.size() + v.vmime_type.size() + v.vsize.size() + v.vdc_id.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPaudio::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPaudio::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_audioEmpty: _type = cons; {
			if (!data) setData(new MTPDaudioEmpty());
			MTPDaudioEmpty &v(_audioEmpty());
			v.vid.read(from, end);
		} break;
		case mtpc_audio: _type = cons; {
			if (!data) setData(new MTPDaudio());
			MTPDaudio &v(_audio());
			v.vid.read(from, end);
			v.vaccess_hash.read(from, end);
			v.vuser_id.read(from, end);
			v.vdate.read(from, end);
			v.vduration.read(from, end);
			v.vmime_type.read(from, end);
			v.vsize.read(from, end);
			v.vdc_id.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPaudio");
	}
}
inline void MTPaudio::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_audioEmpty: {
			const MTPDaudioEmpty &v(c_audioEmpty());
			v.vid.write(to);
		} break;
		case mtpc_audio: {
			const MTPDaudio &v(c_audio());
			v.vid.write(to);
			v.vaccess_hash.write(to);
			v.vuser_id.write(to);
			v.vdate.write(to);
			v.vduration.write(to);
			v.vmime_type.write(to);
			v.vsize.write(to);
			v.vdc_id.write(to);
		} break;
	}
}
inline MTPaudio::MTPaudio(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_audioEmpty: setData(new MTPDaudioEmpty()); break;
		case mtpc_audio: setData(new MTPDaudio()); break;
		default: throw mtpErrorBadTypeId(type, "MTPaudio");
	}
}
inline MTPaudio::MTPaudio(MTPDaudioEmpty *_data) : mtpDataOwner(_data), _type(mtpc_audioEmpty) {
}
inline MTPaudio::MTPaudio(MTPDaudio *_data) : mtpDataOwner(_data), _type(mtpc_audio) {
}
inline MTPaudio MTP_audioEmpty(const MTPlong &_id) {
	return MTPaudio(new MTPDaudioEmpty(_id));
}
inline MTPaudio MTP_audio(const MTPlong &_id, const MTPlong &_access_hash, MTPint _user_id, MTPint _date, MTPint _duration, const MTPstring &_mime_type, MTPint _size, MTPint _dc_id) {
	return MTPaudio(new MTPDaudio(_id, _access_hash, _user_id, _date, _duration, _mime_type, _size, _dc_id));
}

inline uint32 MTPdocument::size() const {
	switch (_type) {
		case mtpc_documentEmpty: {
			const MTPDdocumentEmpty &v(c_documentEmpty());
			return v.vid.size();
		}
		case mtpc_document: {
			const MTPDdocument &v(c_document());
			return v.vid.size() + v.vaccess_hash.size() + v.vuser_id.size() + v.vdate.size() + v.vfile_name.size() + v.vmime_type.size() + v.vsize.size() + v.vthumb.size() + v.vdc_id.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPdocument::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPdocument::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_documentEmpty: _type = cons; {
			if (!data) setData(new MTPDdocumentEmpty());
			MTPDdocumentEmpty &v(_documentEmpty());
			v.vid.read(from, end);
		} break;
		case mtpc_document: _type = cons; {
			if (!data) setData(new MTPDdocument());
			MTPDdocument &v(_document());
			v.vid.read(from, end);
			v.vaccess_hash.read(from, end);
			v.vuser_id.read(from, end);
			v.vdate.read(from, end);
			v.vfile_name.read(from, end);
			v.vmime_type.read(from, end);
			v.vsize.read(from, end);
			v.vthumb.read(from, end);
			v.vdc_id.read(from, end);
		} break;
		default: throw mtpErrorUnexpected(cons, "MTPdocument");
	}
}
inline void MTPdocument::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_documentEmpty: {
			const MTPDdocumentEmpty &v(c_documentEmpty());
			v.vid.write(to);
		} break;
		case mtpc_document: {
			const MTPDdocument &v(c_document());
			v.vid.write(to);
			v.vaccess_hash.write(to);
			v.vuser_id.write(to);
			v.vdate.write(to);
			v.vfile_name.write(to);
			v.vmime_type.write(to);
			v.vsize.write(to);
			v.vthumb.write(to);
			v.vdc_id.write(to);
		} break;
	}
}
inline MTPdocument::MTPdocument(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_documentEmpty: setData(new MTPDdocumentEmpty()); break;
		case mtpc_document: setData(new MTPDdocument()); break;
		default: throw mtpErrorBadTypeId(type, "MTPdocument");
	}
}
inline MTPdocument::MTPdocument(MTPDdocumentEmpty *_data) : mtpDataOwner(_data), _type(mtpc_documentEmpty) {
}
inline MTPdocument::MTPdocument(MTPDdocument *_data) : mtpDataOwner(_data), _type(mtpc_document) {
}
inline MTPdocument MTP_documentEmpty(const MTPlong &_id) {
	return MTPdocument(new MTPDdocumentEmpty(_id));
}
inline MTPdocument MTP_document(const MTPlong &_id, const MTPlong &_access_hash, MTPint _user_id, MTPint _date, const MTPstring &_file_name, const MTPstring &_mime_type, MTPint _size, const MTPPhotoSize &_thumb, MTPint _dc_id) {
	return MTPdocument(new MTPDdocument(_id, _access_hash, _user_id, _date, _file_name, _mime_type, _size, _thumb, _dc_id));
}

inline MTPhelp_support::MTPhelp_support() : mtpDataOwner(new MTPDhelp_support()) {
}

inline uint32 MTPhelp_support::size() const {
	const MTPDhelp_support &v(c_help_support());
	return v.vphone_number.size() + v.vuser.size();
}
inline mtpTypeId MTPhelp_support::type() const {
	return mtpc_help_support;
}
inline void MTPhelp_support::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != mtpc_help_support) throw mtpErrorUnexpected(cons, "MTPhelp_support");

	if (!data) setData(new MTPDhelp_support());
	MTPDhelp_support &v(_help_support());
	v.vphone_number.read(from, end);
	v.vuser.read(from, end);
}
inline void MTPhelp_support::write(mtpBuffer &to) const {
	const MTPDhelp_support &v(c_help_support());
	v.vphone_number.write(to);
	v.vuser.write(to);
}
inline MTPhelp_support::MTPhelp_support(MTPDhelp_support *_data) : mtpDataOwner(_data) {
}
inline MTPhelp_support MTP_help_support(const MTPstring &_phone_number, const MTPUser &_user) {
	return MTPhelp_support(new MTPDhelp_support(_phone_number, _user));
}

inline uint32 MTPnotifyPeer::size() const {
	switch (_type) {
		case mtpc_notifyPeer: {
			const MTPDnotifyPeer &v(c_notifyPeer());
			return v.vpeer.size();
		}
	}
	return 0;
}
inline mtpTypeId MTPnotifyPeer::type() const {
	if (!_type) throw mtpErrorUninitialized();
	return _type;
}
inline void MTPnotifyPeer::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (cons != _type) setData(0);
	switch (cons) {
		case mtpc_notifyPeer: _type = cons; {
			if (!data) setData(new MTPDnotifyPeer());
			MTPDnotifyPeer &v(_notifyPeer());
			v.vpeer.read(from, end);
		} break;
		case mtpc_notifyUsers: _type = cons; break;
		case mtpc_notifyChats: _type = cons; break;
		case mtpc_notifyAll: _type = cons; break;
		default: throw mtpErrorUnexpected(cons, "MTPnotifyPeer");
	}
}
inline void MTPnotifyPeer::write(mtpBuffer &to) const {
	switch (_type) {
		case mtpc_notifyPeer: {
			const MTPDnotifyPeer &v(c_notifyPeer());
			v.vpeer.write(to);
		} break;
	}
}
inline MTPnotifyPeer::MTPnotifyPeer(mtpTypeId type) : mtpDataOwner(0), _type(type) {
	switch (type) {
		case mtpc_notifyPeer: setData(new MTPDnotifyPeer()); break;
		case mtpc_notifyUsers: break;
		case mtpc_notifyChats: break;
		case mtpc_notifyAll: break;
		default: throw mtpErrorBadTypeId(type, "MTPnotifyPeer");
	}
}
inline MTPnotifyPeer::MTPnotifyPeer(MTPDnotifyPeer *_data) : mtpDataOwner(_data), _type(mtpc_notifyPeer) {
}
inline MTPnotifyPeer MTP_notifyPeer(const MTPPeer &_peer) {
	return MTPnotifyPeer(new MTPDnotifyPeer(_peer));
}
inline MTPnotifyPeer MTP_notifyUsers() {
	return MTPnotifyPeer(mtpc_notifyUsers);
}
inline MTPnotifyPeer MTP_notifyChats() {
	return MTPnotifyPeer(mtpc_notifyChats);
}
inline MTPnotifyPeer MTP_notifyAll() {
	return MTPnotifyPeer(mtpc_notifyAll);
}

// Human-readable text serialization
#if (defined _DEBUG || defined _WITH_DEBUG)

inline QString mtpTextSerialize(const mtpPrime *&from, const mtpPrime *end, mtpPrime cons, uint32 level, mtpPrime vcons) {
	QString add = QString(" ").repeated(level * 2);

	const mtpPrime *start = from;
	try {
		if (!cons) {
			if (from >= end) {
				throw Exception("from >= 2");
			}
			cons = *from;
			++from;
			++start;
		}

		QString result;
		switch (mtpTypeId(cons)) {
		case mtpc_userProfilePhotoEmpty:
			result = " ";
		return "{ userProfilePhotoEmpty" + result + "}";

		case mtpc_userProfilePhoto:
			result += "\n" + add;
			result += "  photo_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  photo_small: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  photo_big: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ userProfilePhoto" + result + "}";

		case mtpc_rpc_error:
			result += "\n" + add;
			result += "  error_code: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  error_message: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ rpc_error" + result + "}";

		case mtpc_dh_gen_ok:
			result += "\n" + add;
			result += "  nonce: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
			result += "  server_nonce: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
			result += "  new_nonce_hash1: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
		return "{ dh_gen_ok" + result + "}";

		case mtpc_dh_gen_retry:
			result += "\n" + add;
			result += "  nonce: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
			result += "  server_nonce: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
			result += "  new_nonce_hash2: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
		return "{ dh_gen_retry" + result + "}";

		case mtpc_dh_gen_fail:
			result += "\n" + add;
			result += "  nonce: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
			result += "  server_nonce: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
			result += "  new_nonce_hash3: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
		return "{ dh_gen_fail" + result + "}";

		case mtpc_inputPeerEmpty:
			result = " ";
		return "{ inputPeerEmpty" + result + "}";

		case mtpc_inputPeerSelf:
			result = " ";
		return "{ inputPeerSelf" + result + "}";

		case mtpc_inputPeerContact:
			result += "\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ inputPeerContact" + result + "}";

		case mtpc_inputPeerForeign:
			result += "\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  access_hash: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ inputPeerForeign" + result + "}";

		case mtpc_inputPeerChat:
			result += "\n" + add;
			result += "  chat_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ inputPeerChat" + result + "}";

		case mtpc_photoEmpty:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ photoEmpty" + result + "}";

		case mtpc_photo:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  access_hash: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  caption: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  geo: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  sizes: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ photo" + result + "}";

		case mtpc_p_q_inner_data:
			result += "\n" + add;
			result += "  pq: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  p: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  q: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  nonce: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
			result += "  server_nonce: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
			result += "  new_nonce: " + mtpTextSerialize(from, end, mtpc_int256, level + 1) + ",\n" + add;
		return "{ p_q_inner_data" + result + "}";

		case mtpc_client_DH_inner_data:
			result += "\n" + add;
			result += "  nonce: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
			result += "  server_nonce: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
			result += "  retry_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  g_b: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ client_DH_inner_data" + result + "}";

		case mtpc_contacts_link:
			result += "\n" + add;
			result += "  my_link: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  foreign_link: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  user: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ contacts_link" + result + "}";

		case mtpc_inputPhotoCropAuto:
			result = " ";
		return "{ inputPhotoCropAuto" + result + "}";

		case mtpc_inputPhotoCrop:
			result += "\n" + add;
			result += "  crop_left: " + mtpTextSerialize(from, end, mtpc_double, level + 1) + ",\n" + add;
			result += "  crop_top: " + mtpTextSerialize(from, end, mtpc_double, level + 1) + ",\n" + add;
			result += "  crop_width: " + mtpTextSerialize(from, end, mtpc_double, level + 1) + ",\n" + add;
		return "{ inputPhotoCrop" + result + "}";

		case mtpc_inputFile:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  parts: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  md5_checksum: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ inputFile" + result + "}";

		case mtpc_inputFileBig:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  parts: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ inputFileBig" + result + "}";

		case mtpc_messageActionEmpty:
			result = " ";
		return "{ messageActionEmpty" + result + "}";

		case mtpc_messageActionChatCreate:
			result += "\n" + add;
			result += "  title: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1, mtpc_int) + ",\n" + add;
		return "{ messageActionChatCreate" + result + "}";

		case mtpc_messageActionChatEditTitle:
			result += "\n" + add;
			result += "  title: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ messageActionChatEditTitle" + result + "}";

		case mtpc_messageActionChatEditPhoto:
			result += "\n" + add;
			result += "  photo: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ messageActionChatEditPhoto" + result + "}";

		case mtpc_messageActionChatDeletePhoto:
			result = " ";
		return "{ messageActionChatDeletePhoto" + result + "}";

		case mtpc_messageActionChatAddUser:
			result += "\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ messageActionChatAddUser" + result + "}";

		case mtpc_messageActionChatDeleteUser:
			result += "\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ messageActionChatDeleteUser" + result + "}";

		case mtpc_messageActionGeoChatCreate:
			result += "\n" + add;
			result += "  title: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  address: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ messageActionGeoChatCreate" + result + "}";

		case mtpc_messageActionGeoChatCheckin:
			result = " ";
		return "{ messageActionGeoChatCheckin" + result + "}";

		case mtpc_inputMessagesFilterEmpty:
			result = " ";
		return "{ inputMessagesFilterEmpty" + result + "}";

		case mtpc_inputMessagesFilterPhotos:
			result = " ";
		return "{ inputMessagesFilterPhotos" + result + "}";

		case mtpc_inputMessagesFilterVideo:
			result = " ";
		return "{ inputMessagesFilterVideo" + result + "}";

		case mtpc_inputMessagesFilterPhotoVideo:
			result = " ";
		return "{ inputMessagesFilterPhotoVideo" + result + "}";

		case mtpc_inputMessagesFilterDocument:
			result = " ";
		return "{ inputMessagesFilterDocument" + result + "}";

		case mtpc_help_support:
			result += "\n" + add;
			result += "  phone_number: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  user: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ help_support" + result + "}";

		case mtpc_contactFound:
			result += "\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ contactFound" + result + "}";

		case mtpc_future_salts:
			result += "\n" + add;
			result += "  req_msg_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  now: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  salts: " + mtpTextSerialize(from, end, mtpc_vector, level + 1, mtpc_future_salt) + ",\n" + add;
		return "{ future_salts" + result + "}";

		case mtpc_inputPhotoEmpty:
			result = " ";
		return "{ inputPhotoEmpty" + result + "}";

		case mtpc_inputPhoto:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  access_hash: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ inputPhoto" + result + "}";

		case mtpc_chatParticipant:
			result += "\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  inviter_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ chatParticipant" + result + "}";

		case mtpc_auth_exportedAuthorization:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  bytes: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
		return "{ auth_exportedAuthorization" + result + "}";

		case mtpc_contactStatus:
			result += "\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  expires: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ contactStatus" + result + "}";

		case mtpc_new_session_created:
			result += "\n" + add;
			result += "  first_msg_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  unique_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  server_salt: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ new_session_created" + result + "}";

		case mtpc_geochats_located:
			result += "\n" + add;
			result += "  results: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  messages: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  chats: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ geochats_located" + result + "}";

		case mtpc_updatesTooLong:
			result = " ";
		return "{ updatesTooLong" + result + "}";

		case mtpc_updateShortMessage:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  from_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  message: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  pts: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  seq: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ updateShortMessage" + result + "}";

		case mtpc_updateShortChatMessage:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  from_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  chat_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  message: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  pts: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  seq: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ updateShortChatMessage" + result + "}";

		case mtpc_updateShort:
			result += "\n" + add;
			result += "  update: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ updateShort" + result + "}";

		case mtpc_updatesCombined:
			result += "\n" + add;
			result += "  updates: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  chats: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  seq_start: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  seq: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ updatesCombined" + result + "}";

		case mtpc_updates:
			result += "\n" + add;
			result += "  updates: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  chats: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  seq: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ updates" + result + "}";

		case mtpc_future_salt:
			result += "\n" + add;
			result += "  valid_since: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  valid_until: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  salt: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ future_salt" + result + "}";

		case mtpc_server_DH_inner_data:
			result += "\n" + add;
			result += "  nonce: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
			result += "  server_nonce: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
			result += "  g: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  dh_prime: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  g_a: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  server_time: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ server_DH_inner_data" + result + "}";

		case mtpc_resPQ:
			result += "\n" + add;
			result += "  nonce: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
			result += "  server_nonce: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
			result += "  pq: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  server_public_key_fingerprints: " + mtpTextSerialize(from, end, 0, level + 1, mtpc_long) + ",\n" + add;
		return "{ resPQ" + result + "}";

		case mtpc_upload_file:
			result += "\n" + add;
			result += "  type: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  mtime: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  bytes: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
		return "{ upload_file" + result + "}";

		case mtpc_inputMediaEmpty:
			result = " ";
		return "{ inputMediaEmpty" + result + "}";

		case mtpc_inputMediaUploadedPhoto:
			result += "\n" + add;
			result += "  file: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ inputMediaUploadedPhoto" + result + "}";

		case mtpc_inputMediaPhoto:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ inputMediaPhoto" + result + "}";

		case mtpc_inputMediaGeoPoint:
			result += "\n" + add;
			result += "  geo_point: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ inputMediaGeoPoint" + result + "}";

		case mtpc_inputMediaContact:
			result += "\n" + add;
			result += "  phone_number: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  first_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  last_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ inputMediaContact" + result + "}";

		case mtpc_inputMediaUploadedVideo:
			result += "\n" + add;
			result += "  file: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  duration: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  w: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  h: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  mime_type: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ inputMediaUploadedVideo" + result + "}";

		case mtpc_inputMediaUploadedThumbVideo:
			result += "\n" + add;
			result += "  file: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  thumb: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  duration: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  w: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  h: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  mime_type: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ inputMediaUploadedThumbVideo" + result + "}";

		case mtpc_inputMediaVideo:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ inputMediaVideo" + result + "}";

		case mtpc_inputMediaUploadedAudio:
			result += "\n" + add;
			result += "  file: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  duration: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  mime_type: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ inputMediaUploadedAudio" + result + "}";

		case mtpc_inputMediaAudio:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ inputMediaAudio" + result + "}";

		case mtpc_inputMediaUploadedDocument:
			result += "\n" + add;
			result += "  file: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  file_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  mime_type: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ inputMediaUploadedDocument" + result + "}";

		case mtpc_inputMediaUploadedThumbDocument:
			result += "\n" + add;
			result += "  file: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  thumb: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  file_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  mime_type: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ inputMediaUploadedThumbDocument" + result + "}";

		case mtpc_inputMediaDocument:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ inputMediaDocument" + result + "}";

		case mtpc_documentEmpty:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ documentEmpty" + result + "}";

		case mtpc_document:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  access_hash: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  file_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  mime_type: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  size: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  thumb: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  dc_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ document" + result + "}";

		case mtpc_inputEncryptedFileEmpty:
			result = " ";
		return "{ inputEncryptedFileEmpty" + result + "}";

		case mtpc_inputEncryptedFileUploaded:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  parts: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  md5_checksum: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  key_fingerprint: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ inputEncryptedFileUploaded" + result + "}";

		case mtpc_inputEncryptedFile:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  access_hash: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ inputEncryptedFile" + result + "}";

		case mtpc_inputEncryptedFileBigUploaded:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  parts: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  key_fingerprint: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ inputEncryptedFileBigUploaded" + result + "}";

		case mtpc_contacts_found:
			result += "\n" + add;
			result += "  results: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ contacts_found" + result + "}";

		case mtpc_inputFileLocation:
			result += "\n" + add;
			result += "  volume_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  local_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  secret: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ inputFileLocation" + result + "}";

		case mtpc_inputVideoFileLocation:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  access_hash: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ inputVideoFileLocation" + result + "}";

		case mtpc_inputEncryptedFileLocation:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  access_hash: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ inputEncryptedFileLocation" + result + "}";

		case mtpc_inputAudioFileLocation:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  access_hash: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ inputAudioFileLocation" + result + "}";

		case mtpc_inputDocumentFileLocation:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  access_hash: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ inputDocumentFileLocation" + result + "}";

		case mtpc_chatFull:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  participants: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  chat_photo: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  notify_settings: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ chatFull" + result + "}";

		case mtpc_chatParticipantsForbidden:
			result += "\n" + add;
			result += "  chat_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ chatParticipantsForbidden" + result + "}";

		case mtpc_chatParticipants:
			result += "\n" + add;
			result += "  chat_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  admin_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  participants: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  version: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ chatParticipants" + result + "}";

		case mtpc_msgs_ack:
			result += "\n" + add;
			result += "  msg_ids: " + mtpTextSerialize(from, end, 0, level + 1, mtpc_long) + ",\n" + add;
		return "{ msgs_ack" + result + "}";

		case mtpc_userFull:
			result += "\n" + add;
			result += "  user: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  link: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  profile_photo: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  notify_settings: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  blocked: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  real_first_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  real_last_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ userFull" + result + "}";

		case mtpc_videoEmpty:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ videoEmpty" + result + "}";

		case mtpc_video:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  access_hash: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  caption: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  duration: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  mime_type: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  size: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  thumb: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  dc_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  w: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  h: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ video" + result + "}";

		case mtpc_messageEmpty:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ messageEmpty" + result + "}";

		case mtpc_message:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  from_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  to_id: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  out: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  unread: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  message: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  media: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ message" + result + "}";

		case mtpc_messageForwarded:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  fwd_from_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  fwd_date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  from_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  to_id: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  out: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  unread: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  message: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  media: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ messageForwarded" + result + "}";

		case mtpc_messageService:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  from_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  to_id: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  out: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  unread: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  action: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ messageService" + result + "}";

		case mtpc_notifyPeer:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ notifyPeer" + result + "}";

		case mtpc_notifyUsers:
			result = " ";
		return "{ notifyUsers" + result + "}";

		case mtpc_notifyChats:
			result = " ";
		return "{ notifyChats" + result + "}";

		case mtpc_notifyAll:
			result = " ";
		return "{ notifyAll" + result + "}";

		case mtpc_messages_messageEmpty:
			result = " ";
		return "{ messages_messageEmpty" + result + "}";

		case mtpc_messages_message:
			result += "\n" + add;
			result += "  message: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  chats: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ messages_message" + result + "}";

		case mtpc_inputPhoneContact:
			result += "\n" + add;
			result += "  client_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  phone: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  first_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  last_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ inputPhoneContact" + result + "}";

		case mtpc_rpc_answer_unknown:
			result = " ";
		return "{ rpc_answer_unknown" + result + "}";

		case mtpc_rpc_answer_dropped_running:
			result = " ";
		return "{ rpc_answer_dropped_running" + result + "}";

		case mtpc_rpc_answer_dropped:
			result += "\n" + add;
			result += "  msg_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  seq_no: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  bytes: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ rpc_answer_dropped" + result + "}";

		case mtpc_inputVideoEmpty:
			result = " ";
		return "{ inputVideoEmpty" + result + "}";

		case mtpc_inputVideo:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  access_hash: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ inputVideo" + result + "}";

		case mtpc_decryptedMessageMediaEmpty:
			result = " ";
		return "{ decryptedMessageMediaEmpty" + result + "}";

		case mtpc_decryptedMessageMediaPhoto:
			result += "\n" + add;
			result += "  thumb: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
			result += "  thumb_w: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  thumb_h: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  w: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  h: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  size: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  key: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
			result += "  iv: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
		return "{ decryptedMessageMediaPhoto" + result + "}";

		case mtpc_decryptedMessageMediaVideo:
			result += "\n" + add;
			result += "  thumb: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
			result += "  thumb_w: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  thumb_h: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  duration: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  mime_type: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  w: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  h: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  size: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  key: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
			result += "  iv: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
		return "{ decryptedMessageMediaVideo" + result + "}";

		case mtpc_decryptedMessageMediaGeoPoint:
			result += "\n" + add;
			result += "  lat: " + mtpTextSerialize(from, end, mtpc_double, level + 1) + ",\n" + add;
			result += "  long: " + mtpTextSerialize(from, end, mtpc_double, level + 1) + ",\n" + add;
		return "{ decryptedMessageMediaGeoPoint" + result + "}";

		case mtpc_decryptedMessageMediaContact:
			result += "\n" + add;
			result += "  phone_number: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  first_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  last_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ decryptedMessageMediaContact" + result + "}";

		case mtpc_decryptedMessageMediaDocument:
			result += "\n" + add;
			result += "  thumb: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
			result += "  thumb_w: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  thumb_h: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  file_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  mime_type: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  size: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  key: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
			result += "  iv: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
		return "{ decryptedMessageMediaDocument" + result + "}";

		case mtpc_decryptedMessageMediaAudio:
			result += "\n" + add;
			result += "  duration: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  mime_type: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  size: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  key: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
			result += "  iv: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
		return "{ decryptedMessageMediaAudio" + result + "}";

		case mtpc_geoChatMessageEmpty:
			result += "\n" + add;
			result += "  chat_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ geoChatMessageEmpty" + result + "}";

		case mtpc_geoChatMessage:
			result += "\n" + add;
			result += "  chat_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  from_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  message: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  media: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ geoChatMessage" + result + "}";

		case mtpc_geoChatMessageService:
			result += "\n" + add;
			result += "  chat_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  from_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  action: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ geoChatMessageService" + result + "}";

		case mtpc_geoPointEmpty:
			result = " ";
		return "{ geoPointEmpty" + result + "}";

		case mtpc_geoPoint:
			result += "\n" + add;
			result += "  long: " + mtpTextSerialize(from, end, mtpc_double, level + 1) + ",\n" + add;
			result += "  lat: " + mtpTextSerialize(from, end, mtpc_double, level + 1) + ",\n" + add;
		return "{ geoPoint" + result + "}";

		case mtpc_messages_dialogs:
			result += "\n" + add;
			result += "  dialogs: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  messages: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  chats: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ messages_dialogs" + result + "}";

		case mtpc_messages_dialogsSlice:
			result += "\n" + add;
			result += "  count: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  dialogs: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  messages: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  chats: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ messages_dialogsSlice" + result + "}";

		case mtpc_messages_dhConfigNotModified:
			result += "\n" + add;
			result += "  random: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
		return "{ messages_dhConfigNotModified" + result + "}";

		case mtpc_messages_dhConfig:
			result += "\n" + add;
			result += "  g: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  p: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
			result += "  version: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  random: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
		return "{ messages_dhConfig" + result + "}";

		case mtpc_peerUser:
			result += "\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ peerUser" + result + "}";

		case mtpc_peerChat:
			result += "\n" + add;
			result += "  chat_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ peerChat" + result + "}";

		case mtpc_server_DH_params_fail:
			result += "\n" + add;
			result += "  nonce: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
			result += "  server_nonce: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
			result += "  new_nonce_hash: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
		return "{ server_DH_params_fail" + result + "}";

		case mtpc_server_DH_params_ok:
			result += "\n" + add;
			result += "  nonce: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
			result += "  server_nonce: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
			result += "  encrypted_answer: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ server_DH_params_ok" + result + "}";

		case mtpc_inputAppEvent:
			result += "\n" + add;
			result += "  time: " + mtpTextSerialize(from, end, mtpc_double, level + 1) + ",\n" + add;
			result += "  type: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  data: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ inputAppEvent" + result + "}";

		case mtpc_photos_photo:
			result += "\n" + add;
			result += "  photo: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ photos_photo" + result + "}";

		case mtpc_peerNotifyEventsEmpty:
			result = " ";
		return "{ peerNotifyEventsEmpty" + result + "}";

		case mtpc_peerNotifyEventsAll:
			result = " ";
		return "{ peerNotifyEventsAll" + result + "}";

		case mtpc_nearestDc:
			result += "\n" + add;
			result += "  country: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  this_dc: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  nearest_dc: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ nearestDc" + result + "}";

		case mtpc_wallPaper:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  title: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  sizes: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  color: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ wallPaper" + result + "}";

		case mtpc_wallPaperSolid:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  title: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  bg_color: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  color: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ wallPaperSolid" + result + "}";

		case mtpc_geochats_messages:
			result += "\n" + add;
			result += "  messages: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  chats: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ geochats_messages" + result + "}";

		case mtpc_geochats_messagesSlice:
			result += "\n" + add;
			result += "  count: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  messages: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  chats: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ geochats_messagesSlice" + result + "}";

		case mtpc_contacts_blocked:
			result += "\n" + add;
			result += "  blocked: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ contacts_blocked" + result + "}";

		case mtpc_contacts_blockedSlice:
			result += "\n" + add;
			result += "  count: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  blocked: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ contacts_blockedSlice" + result + "}";

		case mtpc_messages_statedMessage:
			result += "\n" + add;
			result += "  message: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  chats: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  pts: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  seq: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ messages_statedMessage" + result + "}";

		case mtpc_messages_statedMessageLink:
			result += "\n" + add;
			result += "  message: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  chats: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  links: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  pts: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  seq: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ messages_statedMessageLink" + result + "}";

		case mtpc_messageMediaEmpty:
			result = " ";
		return "{ messageMediaEmpty" + result + "}";

		case mtpc_messageMediaPhoto:
			result += "\n" + add;
			result += "  photo: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ messageMediaPhoto" + result + "}";

		case mtpc_messageMediaVideo:
			result += "\n" + add;
			result += "  video: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ messageMediaVideo" + result + "}";

		case mtpc_messageMediaGeo:
			result += "\n" + add;
			result += "  geo: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ messageMediaGeo" + result + "}";

		case mtpc_messageMediaContact:
			result += "\n" + add;
			result += "  phone_number: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  first_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  last_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ messageMediaContact" + result + "}";

		case mtpc_messageMediaUnsupported:
			result += "\n" + add;
			result += "  bytes: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
		return "{ messageMediaUnsupported" + result + "}";

		case mtpc_messageMediaDocument:
			result += "\n" + add;
			result += "  document: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ messageMediaDocument" + result + "}";

		case mtpc_messageMediaAudio:
			result += "\n" + add;
			result += "  audio: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ messageMediaAudio" + result + "}";

		case mtpc_inputGeoChat:
			result += "\n" + add;
			result += "  chat_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  access_hash: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ inputGeoChat" + result + "}";

		case mtpc_help_appUpdate:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  critical: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  url: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  text: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ help_appUpdate" + result + "}";

		case mtpc_help_noAppUpdate:
			result = " ";
		return "{ help_noAppUpdate" + result + "}";

		case mtpc_updates_differenceEmpty:
			result += "\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  seq: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ updates_differenceEmpty" + result + "}";

		case mtpc_updates_difference:
			result += "\n" + add;
			result += "  new_messages: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  new_encrypted_messages: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  other_updates: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  chats: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  state: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ updates_difference" + result + "}";

		case mtpc_updates_differenceSlice:
			result += "\n" + add;
			result += "  new_messages: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  new_encrypted_messages: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  other_updates: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  chats: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  intermediate_state: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ updates_differenceSlice" + result + "}";

		case mtpc_msgs_state_info:
			result += "\n" + add;
			result += "  req_msg_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  info: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ msgs_state_info" + result + "}";

		case mtpc_msgs_state_req:
			result += "\n" + add;
			result += "  msg_ids: " + mtpTextSerialize(from, end, 0, level + 1, mtpc_long) + ",\n" + add;
		return "{ msgs_state_req" + result + "}";

		case mtpc_msg_resend_req:
			result += "\n" + add;
			result += "  msg_ids: " + mtpTextSerialize(from, end, 0, level + 1, mtpc_long) + ",\n" + add;
		return "{ msg_resend_req" + result + "}";

		case mtpc_inputDocumentEmpty:
			result = " ";
		return "{ inputDocumentEmpty" + result + "}";

		case mtpc_inputDocument:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  access_hash: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ inputDocument" + result + "}";

		case mtpc_userStatusEmpty:
			result = " ";
		return "{ userStatusEmpty" + result + "}";

		case mtpc_userStatusOnline:
			result += "\n" + add;
			result += "  expires: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ userStatusOnline" + result + "}";

		case mtpc_userStatusOffline:
			result += "\n" + add;
			result += "  was_online: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ userStatusOffline" + result + "}";

		case mtpc_photos_photos:
			result += "\n" + add;
			result += "  photos: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ photos_photos" + result + "}";

		case mtpc_photos_photosSlice:
			result += "\n" + add;
			result += "  count: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  photos: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ photos_photosSlice" + result + "}";

		case mtpc_decryptedMessage:
			result += "\n" + add;
			result += "  random_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  random_bytes: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
			result += "  message: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  media: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ decryptedMessage" + result + "}";

		case mtpc_decryptedMessageService:
			result += "\n" + add;
			result += "  random_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  random_bytes: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
			result += "  action: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ decryptedMessageService" + result + "}";

		case mtpc_contacts_importedContacts:
			result += "\n" + add;
			result += "  imported: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  retry_contacts: " + mtpTextSerialize(from, end, 0, level + 1, mtpc_long) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ contacts_importedContacts" + result + "}";

		case mtpc_fileLocationUnavailable:
			result += "\n" + add;
			result += "  volume_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  local_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  secret: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ fileLocationUnavailable" + result + "}";

		case mtpc_fileLocation:
			result += "\n" + add;
			result += "  dc_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  volume_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  local_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  secret: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ fileLocation" + result + "}";

		case mtpc_photoSizeEmpty:
			result += "\n" + add;
			result += "  type: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ photoSizeEmpty" + result + "}";

		case mtpc_photoSize:
			result += "\n" + add;
			result += "  type: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  location: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  w: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  h: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  size: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ photoSize" + result + "}";

		case mtpc_photoCachedSize:
			result += "\n" + add;
			result += "  type: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  location: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  w: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  h: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  bytes: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
		return "{ photoCachedSize" + result + "}";

		case mtpc_msg_detailed_info:
			result += "\n" + add;
			result += "  msg_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  answer_msg_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  bytes: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  status: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ msg_detailed_info" + result + "}";

		case mtpc_msg_new_detailed_info:
			result += "\n" + add;
			result += "  answer_msg_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  bytes: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  status: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ msg_new_detailed_info" + result + "}";

		case mtpc_inputChatPhotoEmpty:
			result = " ";
		return "{ inputChatPhotoEmpty" + result + "}";

		case mtpc_inputChatUploadedPhoto:
			result += "\n" + add;
			result += "  file: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  crop: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ inputChatUploadedPhoto" + result + "}";

		case mtpc_inputChatPhoto:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  crop: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ inputChatPhoto" + result + "}";

		case mtpc_messages_sentMessage:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  pts: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  seq: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ messages_sentMessage" + result + "}";

		case mtpc_messages_sentMessageLink:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  pts: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  seq: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  links: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ messages_sentMessageLink" + result + "}";

		case mtpc_messages_chatFull:
			result += "\n" + add;
			result += "  full_chat: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  chats: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ messages_chatFull" + result + "}";

		case mtpc_geochats_statedMessage:
			result += "\n" + add;
			result += "  message: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  chats: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  seq: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ geochats_statedMessage" + result + "}";

		case mtpc_chatPhotoEmpty:
			result = " ";
		return "{ chatPhotoEmpty" + result + "}";

		case mtpc_chatPhoto:
			result += "\n" + add;
			result += "  photo_small: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  photo_big: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ chatPhoto" + result + "}";

		case mtpc_encryptedMessage:
			result += "\n" + add;
			result += "  random_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  chat_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  bytes: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
			result += "  file: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ encryptedMessage" + result + "}";

		case mtpc_encryptedMessageService:
			result += "\n" + add;
			result += "  random_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  chat_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  bytes: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
		return "{ encryptedMessageService" + result + "}";

		case mtpc_destroy_session_ok:
			result += "\n" + add;
			result += "  session_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ destroy_session_ok" + result + "}";

		case mtpc_destroy_session_none:
			result += "\n" + add;
			result += "  session_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ destroy_session_none" + result + "}";

		case mtpc_http_wait:
			result += "\n" + add;
			result += "  max_delay: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  wait_after: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  max_wait: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ http_wait" + result + "}";

		case mtpc_messages_sentEncryptedMessage:
			result += "\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ messages_sentEncryptedMessage" + result + "}";

		case mtpc_messages_sentEncryptedFile:
			result += "\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  file: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ messages_sentEncryptedFile" + result + "}";

		case mtpc_contacts_myLinkEmpty:
			result = " ";
		return "{ contacts_myLinkEmpty" + result + "}";

		case mtpc_contacts_myLinkRequested:
			result += "\n" + add;
			result += "  contact: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ contacts_myLinkRequested" + result + "}";

		case mtpc_contacts_myLinkContact:
			result = " ";
		return "{ contacts_myLinkContact" + result + "}";

		case mtpc_inputEncryptedChat:
			result += "\n" + add;
			result += "  chat_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  access_hash: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ inputEncryptedChat" + result + "}";

		case mtpc_messages_chats:
			result += "\n" + add;
			result += "  chats: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ messages_chats" + result + "}";

		case mtpc_encryptedChatEmpty:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ encryptedChatEmpty" + result + "}";

		case mtpc_encryptedChatWaiting:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  access_hash: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  admin_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  participant_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ encryptedChatWaiting" + result + "}";

		case mtpc_encryptedChatRequested:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  access_hash: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  admin_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  participant_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  g_a: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
		return "{ encryptedChatRequested" + result + "}";

		case mtpc_encryptedChat:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  access_hash: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  admin_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  participant_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  g_a_or_b: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
			result += "  key_fingerprint: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ encryptedChat" + result + "}";

		case mtpc_encryptedChatDiscarded:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ encryptedChatDiscarded" + result + "}";

		case mtpc_messages_messages:
			result += "\n" + add;
			result += "  messages: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  chats: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ messages_messages" + result + "}";

		case mtpc_messages_messagesSlice:
			result += "\n" + add;
			result += "  count: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  messages: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  chats: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ messages_messagesSlice" + result + "}";

		case mtpc_auth_checkedPhone:
			result += "\n" + add;
			result += "  phone_registered: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  phone_invited: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ auth_checkedPhone" + result + "}";

		case mtpc_contactSuggested:
			result += "\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  mutual_contacts: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ contactSuggested" + result + "}";

		case mtpc_contacts_foreignLinkUnknown:
			result = " ";
		return "{ contacts_foreignLinkUnknown" + result + "}";

		case mtpc_contacts_foreignLinkRequested:
			result += "\n" + add;
			result += "  has_phone: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ contacts_foreignLinkRequested" + result + "}";

		case mtpc_contacts_foreignLinkMutual:
			result = " ";
		return "{ contacts_foreignLinkMutual" + result + "}";

		case mtpc_inputAudioEmpty:
			result = " ";
		return "{ inputAudioEmpty" + result + "}";

		case mtpc_inputAudio:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  access_hash: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ inputAudio" + result + "}";

		case mtpc_contacts_contacts:
			result += "\n" + add;
			result += "  contacts: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ contacts_contacts" + result + "}";

		case mtpc_contacts_contactsNotModified:
			result = " ";
		return "{ contacts_contactsNotModified" + result + "}";

		case mtpc_chatEmpty:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ chatEmpty" + result + "}";

		case mtpc_chat:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  title: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  photo: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  participants_count: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  left: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  version: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ chat" + result + "}";

		case mtpc_chatForbidden:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  title: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ chatForbidden" + result + "}";

		case mtpc_geoChat:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  access_hash: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  title: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  address: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  venue: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  geo: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  photo: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  participants_count: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  checked_in: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  version: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ geoChat" + result + "}";

		case mtpc_pong:
			result += "\n" + add;
			result += "  msg_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  ping_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ pong" + result + "}";

		case mtpc_inputPeerNotifyEventsEmpty:
			result = " ";
		return "{ inputPeerNotifyEventsEmpty" + result + "}";

		case mtpc_inputPeerNotifyEventsAll:
			result = " ";
		return "{ inputPeerNotifyEventsAll" + result + "}";

		case mtpc_inputPeerNotifySettings:
			result += "\n" + add;
			result += "  mute_until: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  sound: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  show_previews: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  events_mask: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ inputPeerNotifySettings" + result + "}";

		case mtpc_messages_affectedHistory:
			result += "\n" + add;
			result += "  pts: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  seq: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  offset: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ messages_affectedHistory" + result + "}";

		case mtpc_inputNotifyPeer:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ inputNotifyPeer" + result + "}";

		case mtpc_inputNotifyUsers:
			result = " ";
		return "{ inputNotifyUsers" + result + "}";

		case mtpc_inputNotifyChats:
			result = " ";
		return "{ inputNotifyChats" + result + "}";

		case mtpc_inputNotifyAll:
			result = " ";
		return "{ inputNotifyAll" + result + "}";

		case mtpc_inputNotifyGeoChatPeer:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ inputNotifyGeoChatPeer" + result + "}";

		case mtpc_bad_msg_notification:
			result += "\n" + add;
			result += "  bad_msg_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  bad_msg_seqno: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  error_code: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ bad_msg_notification" + result + "}";

		case mtpc_bad_server_salt:
			result += "\n" + add;
			result += "  bad_msg_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  bad_msg_seqno: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  error_code: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  new_server_salt: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ bad_server_salt" + result + "}";

		case mtpc_config:
			result += "\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  test_mode: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  this_dc: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  dc_options: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  chat_size_max: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  broadcast_size_max: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ config" + result + "}";

		case mtpc_inputGeoPointEmpty:
			result = " ";
		return "{ inputGeoPointEmpty" + result + "}";

		case mtpc_inputGeoPoint:
			result += "\n" + add;
			result += "  lat: " + mtpTextSerialize(from, end, mtpc_double, level + 1) + ",\n" + add;
			result += "  long: " + mtpTextSerialize(from, end, mtpc_double, level + 1) + ",\n" + add;
		return "{ inputGeoPoint" + result + "}";

		case mtpc_inputUserEmpty:
			result = " ";
		return "{ inputUserEmpty" + result + "}";

		case mtpc_inputUserSelf:
			result = " ";
		return "{ inputUserSelf" + result + "}";

		case mtpc_inputUserContact:
			result += "\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ inputUserContact" + result + "}";

		case mtpc_inputUserForeign:
			result += "\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  access_hash: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ inputUserForeign" + result + "}";

		case mtpc_dialog:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  top_message: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  unread_count: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  notify_settings: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ dialog" + result + "}";

		case mtpc_importedContact:
			result += "\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  client_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ importedContact" + result + "}";

		case mtpc_dcOption:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  hostname: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  ip_address: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  port: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ dcOption" + result + "}";

		case mtpc_updateNewMessage:
			result += "\n" + add;
			result += "  message: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  pts: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ updateNewMessage" + result + "}";

		case mtpc_updateMessageID:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  random_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ updateMessageID" + result + "}";

		case mtpc_updateReadMessages:
			result += "\n" + add;
			result += "  messages: " + mtpTextSerialize(from, end, 0, level + 1, mtpc_int) + ",\n" + add;
			result += "  pts: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ updateReadMessages" + result + "}";

		case mtpc_updateDeleteMessages:
			result += "\n" + add;
			result += "  messages: " + mtpTextSerialize(from, end, 0, level + 1, mtpc_int) + ",\n" + add;
			result += "  pts: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ updateDeleteMessages" + result + "}";

		case mtpc_updateRestoreMessages:
			result += "\n" + add;
			result += "  messages: " + mtpTextSerialize(from, end, 0, level + 1, mtpc_int) + ",\n" + add;
			result += "  pts: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ updateRestoreMessages" + result + "}";

		case mtpc_updateUserTyping:
			result += "\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ updateUserTyping" + result + "}";

		case mtpc_updateChatUserTyping:
			result += "\n" + add;
			result += "  chat_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ updateChatUserTyping" + result + "}";

		case mtpc_updateChatParticipants:
			result += "\n" + add;
			result += "  participants: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ updateChatParticipants" + result + "}";

		case mtpc_updateUserStatus:
			result += "\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  status: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ updateUserStatus" + result + "}";

		case mtpc_updateUserName:
			result += "\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  first_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  last_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ updateUserName" + result + "}";

		case mtpc_updateUserPhoto:
			result += "\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  photo: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  previous: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ updateUserPhoto" + result + "}";

		case mtpc_updateContactRegistered:
			result += "\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ updateContactRegistered" + result + "}";

		case mtpc_updateContactLink:
			result += "\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  my_link: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  foreign_link: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ updateContactLink" + result + "}";

		case mtpc_updateActivation:
			result += "\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ updateActivation" + result + "}";

		case mtpc_updateNewAuthorization:
			result += "\n" + add;
			result += "  auth_key_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  device: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  location: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ updateNewAuthorization" + result + "}";

		case mtpc_updateNewGeoChatMessage:
			result += "\n" + add;
			result += "  message: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ updateNewGeoChatMessage" + result + "}";

		case mtpc_updateNewEncryptedMessage:
			result += "\n" + add;
			result += "  message: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  qts: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ updateNewEncryptedMessage" + result + "}";

		case mtpc_updateEncryptedChatTyping:
			result += "\n" + add;
			result += "  chat_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ updateEncryptedChatTyping" + result + "}";

		case mtpc_updateEncryption:
			result += "\n" + add;
			result += "  chat: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ updateEncryption" + result + "}";

		case mtpc_updateEncryptedMessagesRead:
			result += "\n" + add;
			result += "  chat_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  max_date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ updateEncryptedMessagesRead" + result + "}";

		case mtpc_updateChatParticipantAdd:
			result += "\n" + add;
			result += "  chat_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  inviter_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  version: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ updateChatParticipantAdd" + result + "}";

		case mtpc_updateChatParticipantDelete:
			result += "\n" + add;
			result += "  chat_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  version: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ updateChatParticipantDelete" + result + "}";

		case mtpc_updateDcOptions:
			result += "\n" + add;
			result += "  dc_options: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ updateDcOptions" + result + "}";

		case mtpc_updateUserBlocked:
			result += "\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  blocked: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ updateUserBlocked" + result + "}";

		case mtpc_updateNotifySettings:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  notify_settings: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ updateNotifySettings" + result + "}";

		case mtpc_decryptedMessageActionSetMessageTTL:
			result += "\n" + add;
			result += "  ttl_seconds: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ decryptedMessageActionSetMessageTTL" + result + "}";

		case mtpc_decryptedMessageActionReadMessages:
			result += "\n" + add;
			result += "  random_ids: " + mtpTextSerialize(from, end, 0, level + 1, mtpc_long) + ",\n" + add;
		return "{ decryptedMessageActionReadMessages" + result + "}";

		case mtpc_decryptedMessageActionDeleteMessages:
			result += "\n" + add;
			result += "  random_ids: " + mtpTextSerialize(from, end, 0, level + 1, mtpc_long) + ",\n" + add;
		return "{ decryptedMessageActionDeleteMessages" + result + "}";

		case mtpc_decryptedMessageActionScreenshotMessages:
			result += "\n" + add;
			result += "  random_ids: " + mtpTextSerialize(from, end, 0, level + 1, mtpc_long) + ",\n" + add;
		return "{ decryptedMessageActionScreenshotMessages" + result + "}";

		case mtpc_decryptedMessageActionFlushHistory:
			result = " ";
		return "{ decryptedMessageActionFlushHistory" + result + "}";

		case mtpc_decryptedMessageActionNotifyLayer:
			result += "\n" + add;
			result += "  layer: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ decryptedMessageActionNotifyLayer" + result + "}";

		case mtpc_peerNotifySettingsEmpty:
			result = " ";
		return "{ peerNotifySettingsEmpty" + result + "}";

		case mtpc_peerNotifySettings:
			result += "\n" + add;
			result += "  mute_until: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  sound: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  show_previews: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  events_mask: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ peerNotifySettings" + result + "}";

		case mtpc_userEmpty:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ userEmpty" + result + "}";

		case mtpc_userSelf:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  first_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  last_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  phone: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  photo: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  status: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  inactive: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ userSelf" + result + "}";

		case mtpc_userContact:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  first_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  last_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  access_hash: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  phone: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  photo: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  status: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ userContact" + result + "}";

		case mtpc_userRequest:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  first_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  last_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  access_hash: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  phone: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  photo: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  status: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ userRequest" + result + "}";

		case mtpc_userForeign:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  first_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  last_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  access_hash: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  photo: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  status: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ userForeign" + result + "}";

		case mtpc_userDeleted:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  first_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  last_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ userDeleted" + result + "}";

		case mtpc_contacts_suggested:
			result += "\n" + add;
			result += "  results: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ contacts_suggested" + result + "}";

		case mtpc_auth_authorization:
			result += "\n" + add;
			result += "  expires: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  user: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ auth_authorization" + result + "}";

		case mtpc_messages_chat:
			result += "\n" + add;
			result += "  chat: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ messages_chat" + result + "}";

		case mtpc_auth_sentCode:
			result += "\n" + add;
			result += "  phone_registered: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  phone_code_hash: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  send_call_timeout: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  is_password: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ auth_sentCode" + result + "}";

		case mtpc_audioEmpty:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ audioEmpty" + result + "}";

		case mtpc_audio:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  access_hash: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  duration: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  mime_type: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  size: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  dc_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ audio" + result + "}";

		case mtpc_messages_statedMessages:
			result += "\n" + add;
			result += "  messages: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  chats: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  pts: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  seq: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ messages_statedMessages" + result + "}";

		case mtpc_messages_statedMessagesLinks:
			result += "\n" + add;
			result += "  messages: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  chats: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  links: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  pts: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  seq: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ messages_statedMessagesLinks" + result + "}";

		case mtpc_contactBlocked:
			result += "\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ contactBlocked" + result + "}";

		case mtpc_storage_fileUnknown:
			result = " ";
		return "{ storage_fileUnknown" + result + "}";

		case mtpc_storage_fileJpeg:
			result = " ";
		return "{ storage_fileJpeg" + result + "}";

		case mtpc_storage_fileGif:
			result = " ";
		return "{ storage_fileGif" + result + "}";

		case mtpc_storage_filePng:
			result = " ";
		return "{ storage_filePng" + result + "}";

		case mtpc_storage_filePdf:
			result = " ";
		return "{ storage_filePdf" + result + "}";

		case mtpc_storage_fileMp3:
			result = " ";
		return "{ storage_fileMp3" + result + "}";

		case mtpc_storage_fileMov:
			result = " ";
		return "{ storage_fileMov" + result + "}";

		case mtpc_storage_filePartial:
			result = " ";
		return "{ storage_filePartial" + result + "}";

		case mtpc_storage_fileMp4:
			result = " ";
		return "{ storage_fileMp4" + result + "}";

		case mtpc_storage_fileWebp:
			result = " ";
		return "{ storage_fileWebp" + result + "}";

		case mtpc_help_inviteText:
			result += "\n" + add;
			result += "  message: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ help_inviteText" + result + "}";

		case mtpc_chatLocated:
			result += "\n" + add;
			result += "  chat_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  distance: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ chatLocated" + result + "}";

		case mtpc_contact:
			result += "\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  mutual: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ contact" + result + "}";

		case mtpc_decryptedMessageLayer:
			result += "\n" + add;
			result += "  layer: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  message: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ decryptedMessageLayer" + result + "}";

		case mtpc_updates_state:
			result += "\n" + add;
			result += "  pts: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  qts: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  seq: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  unread_count: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ updates_state" + result + "}";

		case mtpc_encryptedFileEmpty:
			result = " ";
		return "{ encryptedFileEmpty" + result + "}";

		case mtpc_encryptedFile:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  access_hash: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  size: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  dc_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  key_fingerprint: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ encryptedFile" + result + "}";

		case mtpc_msgs_all_info:
			result += "\n" + add;
			result += "  msg_ids: " + mtpTextSerialize(from, end, 0, level + 1, mtpc_long) + ",\n" + add;
			result += "  info: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ msgs_all_info" + result + "}";

		case mtpc_photos_updateProfilePhoto:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  crop: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ photos_updateProfilePhoto" + result + "}";

		case mtpc_messages_getMessages:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, 0, level + 1, mtpc_int) + ",\n" + add;
		return "{ messages_getMessages" + result + "}";

		case mtpc_messages_getHistory:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  offset: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  max_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  limit: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ messages_getHistory" + result + "}";

		case mtpc_messages_search:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  q: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  filter: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  min_date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  max_date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  offset: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  max_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  limit: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ messages_search" + result + "}";

		case mtpc_set_client_DH_params:
			result += "\n" + add;
			result += "  nonce: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
			result += "  server_nonce: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
			result += "  encrypted_data: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ set_client_DH_params" + result + "}";

		case mtpc_contacts_getStatuses:
			result = " ";
		return "{ contacts_getStatuses" + result + "}";

		case mtpc_auth_checkPhone:
			result += "\n" + add;
			result += "  phone_number: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ auth_checkPhone" + result + "}";

		case mtpc_help_getAppUpdate:
			result += "\n" + add;
			result += "  device_model: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  system_version: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  app_version: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  lang_code: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ help_getAppUpdate" + result + "}";

		case mtpc_updates_getDifference:
			result += "\n" + add;
			result += "  pts: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  qts: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ updates_getDifference" + result + "}";

		case mtpc_help_getInviteText:
			result += "\n" + add;
			result += "  lang_code: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ help_getInviteText" + result + "}";

		case mtpc_users_getFullUser:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ users_getFullUser" + result + "}";

		case mtpc_updates_getState:
			result = " ";
		return "{ updates_getState" + result + "}";

		case mtpc_contacts_getContacts:
			result += "\n" + add;
			result += "  hash: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ contacts_getContacts" + result + "}";

		case mtpc_geochats_checkin:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ geochats_checkin" + result + "}";

		case mtpc_geochats_editChatTitle:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  title: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  address: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ geochats_editChatTitle" + result + "}";

		case mtpc_geochats_editChatPhoto:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  photo: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ geochats_editChatPhoto" + result + "}";

		case mtpc_geochats_sendMessage:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  message: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  random_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ geochats_sendMessage" + result + "}";

		case mtpc_geochats_sendMedia:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  media: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  random_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ geochats_sendMedia" + result + "}";

		case mtpc_geochats_createGeoChat:
			result += "\n" + add;
			result += "  title: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  geo_point: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  address: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  venue: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ geochats_createGeoChat" + result + "}";

		case mtpc_ping:
			result += "\n" + add;
			result += "  ping_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ ping" + result + "}";

		case mtpc_ping_delay_disconnect:
			result += "\n" + add;
			result += "  ping_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  disconnect_delay: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ ping_delay_disconnect" + result + "}";

		case mtpc_help_getSupport:
			result = " ";
		return "{ help_getSupport" + result + "}";

		case mtpc_messages_readHistory:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  max_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  offset: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ messages_readHistory" + result + "}";

		case mtpc_messages_deleteHistory:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  offset: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ messages_deleteHistory" + result + "}";

		case mtpc_messages_deleteMessages:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, 0, level + 1, mtpc_int) + ",\n" + add;
		return "{ messages_deleteMessages" + result + "}";

		case mtpc_messages_restoreMessages:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, 0, level + 1, mtpc_int) + ",\n" + add;
		return "{ messages_restoreMessages" + result + "}";

		case mtpc_messages_receivedMessages:
			result += "\n" + add;
			result += "  max_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ messages_receivedMessages" + result + "}";

		case mtpc_users_getUsers:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ users_getUsers" + result + "}";

		case mtpc_get_future_salts:
			result += "\n" + add;
			result += "  num: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ get_future_salts" + result + "}";

		case mtpc_photos_getUserPhotos:
			result += "\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  offset: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  max_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  limit: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ photos_getUserPhotos" + result + "}";

		case mtpc_register_saveDeveloperInfo:
			result += "\n" + add;
			result += "  name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  email: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  phone_number: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  age: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  city: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ register_saveDeveloperInfo" + result + "}";

		case mtpc_auth_sendCall:
			result += "\n" + add;
			result += "  phone_number: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  phone_code_hash: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ auth_sendCall" + result + "}";

		case mtpc_auth_logOut:
			result = " ";
		return "{ auth_logOut" + result + "}";

		case mtpc_auth_resetAuthorizations:
			result = " ";
		return "{ auth_resetAuthorizations" + result + "}";

		case mtpc_auth_sendInvites:
			result += "\n" + add;
			result += "  phone_numbers: " + mtpTextSerialize(from, end, 0, level + 1, mtpc_string) + ",\n" + add;
			result += "  message: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ auth_sendInvites" + result + "}";

		case mtpc_account_registerDevice:
			result += "\n" + add;
			result += "  token_type: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  token: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  device_model: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  system_version: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  app_version: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  app_sandbox: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  lang_code: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ account_registerDevice" + result + "}";

		case mtpc_account_unregisterDevice:
			result += "\n" + add;
			result += "  token_type: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  token: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ account_unregisterDevice" + result + "}";

		case mtpc_account_updateNotifySettings:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  settings: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ account_updateNotifySettings" + result + "}";

		case mtpc_account_resetNotifySettings:
			result = " ";
		return "{ account_resetNotifySettings" + result + "}";

		case mtpc_account_updateStatus:
			result += "\n" + add;
			result += "  offline: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ account_updateStatus" + result + "}";

		case mtpc_contacts_deleteContacts:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ contacts_deleteContacts" + result + "}";

		case mtpc_contacts_block:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ contacts_block" + result + "}";

		case mtpc_contacts_unblock:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ contacts_unblock" + result + "}";

		case mtpc_messages_setTyping:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  typing: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ messages_setTyping" + result + "}";

		case mtpc_upload_saveFilePart:
			result += "\n" + add;
			result += "  file_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  file_part: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  bytes: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
		return "{ upload_saveFilePart" + result + "}";

		case mtpc_help_saveAppLog:
			result += "\n" + add;
			result += "  events: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ help_saveAppLog" + result + "}";

		case mtpc_geochats_setTyping:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  typing: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ geochats_setTyping" + result + "}";

		case mtpc_messages_discardEncryption:
			result += "\n" + add;
			result += "  chat_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ messages_discardEncryption" + result + "}";

		case mtpc_messages_setEncryptedTyping:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  typing: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ messages_setEncryptedTyping" + result + "}";

		case mtpc_messages_readEncryptedHistory:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  max_date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ messages_readEncryptedHistory" + result + "}";

		case mtpc_upload_saveBigFilePart:
			result += "\n" + add;
			result += "  file_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  file_part: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  file_total_parts: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  bytes: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
		return "{ upload_saveBigFilePart" + result + "}";

		case mtpc_req_pq:
			result += "\n" + add;
			result += "  nonce: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
		return "{ req_pq" + result + "}";

		case mtpc_auth_exportAuthorization:
			result += "\n" + add;
			result += "  dc_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ auth_exportAuthorization" + result + "}";

		case mtpc_contacts_importContacts:
			result += "\n" + add;
			result += "  contacts: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  replace: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ contacts_importContacts" + result + "}";

		case mtpc_rpc_drop_answer:
			result += "\n" + add;
			result += "  req_msg_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ rpc_drop_answer" + result + "}";

		case mtpc_help_getConfig:
			result = " ";
		return "{ help_getConfig" + result + "}";

		case mtpc_help_getNearestDc:
			result = " ";
		return "{ help_getNearestDc" + result + "}";

		case mtpc_messages_getDialogs:
			result += "\n" + add;
			result += "  offset: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  max_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  limit: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ messages_getDialogs" + result + "}";

		case mtpc_account_getNotifySettings:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ account_getNotifySettings" + result + "}";

		case mtpc_geochats_getLocated:
			result += "\n" + add;
			result += "  geo_point: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  radius: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  limit: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ geochats_getLocated" + result + "}";

		case mtpc_messages_getDhConfig:
			result += "\n" + add;
			result += "  version: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  random_length: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ messages_getDhConfig" + result + "}";

		case mtpc_account_updateProfile:
			result += "\n" + add;
			result += "  first_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  last_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ account_updateProfile" + result + "}";

		case mtpc_messages_getFullChat:
			result += "\n" + add;
			result += "  chat_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ messages_getFullChat" + result + "}";

		case mtpc_geochats_getFullChat:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ geochats_getFullChat" + result + "}";

		case mtpc_req_DH_params:
			result += "\n" + add;
			result += "  nonce: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
			result += "  server_nonce: " + mtpTextSerialize(from, end, mtpc_int128, level + 1) + ",\n" + add;
			result += "  p: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  q: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  public_key_fingerprint: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  encrypted_data: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ req_DH_params" + result + "}";

		case mtpc_contacts_getSuggested:
			result += "\n" + add;
			result += "  limit: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ contacts_getSuggested" + result + "}";

		case mtpc_auth_signUp:
			result += "\n" + add;
			result += "  phone_number: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  phone_code_hash: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  phone_code: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  first_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  last_name: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ auth_signUp" + result + "}";

		case mtpc_auth_signIn:
			result += "\n" + add;
			result += "  phone_number: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  phone_code_hash: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  phone_code: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ auth_signIn" + result + "}";

		case mtpc_auth_importAuthorization:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  bytes: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
		return "{ auth_importAuthorization" + result + "}";

		case mtpc_upload_getFile:
			result += "\n" + add;
			result += "  location: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  offset: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  limit: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ upload_getFile" + result + "}";

		case mtpc_photos_uploadProfilePhoto:
			result += "\n" + add;
			result += "  file: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  caption: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  geo_point: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  crop: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ photos_uploadProfilePhoto" + result + "}";

		case mtpc_auth_sendCode:
			result += "\n" + add;
			result += "  phone_number: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  sms_type: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  api_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  api_hash: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  lang_code: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ auth_sendCode" + result + "}";

		case mtpc_messages_forwardMessages:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, 0, level + 1, mtpc_int) + ",\n" + add;
		return "{ messages_forwardMessages" + result + "}";

		case mtpc_messages_sendBroadcast:
			result += "\n" + add;
			result += "  contacts: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  message: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  media: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ messages_sendBroadcast" + result + "}";

		case mtpc_messages_receivedQueue:
			result += "\n" + add;
			result += "  max_qts: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ messages_receivedQueue" + result + "}";

		case mtpc_contacts_search:
			result += "\n" + add;
			result += "  q: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  limit: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ contacts_search" + result + "}";

		case mtpc_messages_sendMessage:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  message: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  random_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ messages_sendMessage" + result + "}";

		case mtpc_geochats_getRecents:
			result += "\n" + add;
			result += "  offset: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  limit: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ geochats_getRecents" + result + "}";

		case mtpc_geochats_search:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  q: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  filter: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  min_date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  max_date: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  offset: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  max_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  limit: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ geochats_search" + result + "}";

		case mtpc_geochats_getHistory:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  offset: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  max_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  limit: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ geochats_getHistory" + result + "}";

		case mtpc_destroy_session:
			result += "\n" + add;
			result += "  session_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ destroy_session" + result + "}";

		case mtpc_account_getWallPapers:
			result = " ";
		return "{ account_getWallPapers" + result + "}";

		case mtpc_messages_sendEncrypted:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  random_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  data: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
		return "{ messages_sendEncrypted" + result + "}";

		case mtpc_messages_sendEncryptedFile:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  random_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  data: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
			result += "  file: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ messages_sendEncryptedFile" + result + "}";

		case mtpc_messages_sendEncryptedService:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  random_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  data: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
		return "{ messages_sendEncryptedService" + result + "}";

		case mtpc_contacts_getBlocked:
			result += "\n" + add;
			result += "  offset: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  limit: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ contacts_getBlocked" + result + "}";

		case mtpc_contacts_deleteContact:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ contacts_deleteContact" + result + "}";

		case mtpc_invokeAfterMsg:
			result += "\n" + add;
			result += "  msg_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
			result += "  query: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ invokeAfterMsg" + result + "}";

		case mtpc_invokeAfterMsgs:
			result += "\n" + add;
			result += "  msg_ids: " + mtpTextSerialize(from, end, 0, level + 1, mtpc_long) + ",\n" + add;
			result += "  query: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ invokeAfterMsgs" + result + "}";

		case mtpc_initConnection:
			result += "\n" + add;
			result += "  api_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  device_model: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  system_version: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  app_version: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  lang_code: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
			result += "  query: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ initConnection" + result + "}";

		case mtpc_messages_getChats:
			result += "\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, 0, level + 1, mtpc_int) + ",\n" + add;
		return "{ messages_getChats" + result + "}";

		case mtpc_messages_sendMedia:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  media: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  random_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ messages_sendMedia" + result + "}";

		case mtpc_messages_editChatTitle:
			result += "\n" + add;
			result += "  chat_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  title: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ messages_editChatTitle" + result + "}";

		case mtpc_messages_editChatPhoto:
			result += "\n" + add;
			result += "  chat_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  photo: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ messages_editChatPhoto" + result + "}";

		case mtpc_messages_addChatUser:
			result += "\n" + add;
			result += "  chat_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  fwd_limit: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		return "{ messages_addChatUser" + result + "}";

		case mtpc_messages_deleteChatUser:
			result += "\n" + add;
			result += "  chat_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
		return "{ messages_deleteChatUser" + result + "}";

		case mtpc_messages_createChat:
			result += "\n" + add;
			result += "  users: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  title: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
		return "{ messages_createChat" + result + "}";

		case mtpc_messages_forwardMessage:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  random_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ messages_forwardMessage" + result + "}";

		case mtpc_messages_requestEncryption:
			result += "\n" + add;
			result += "  user_id: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  random_id: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
			result += "  g_a: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
		return "{ messages_requestEncryption" + result + "}";

		case mtpc_messages_acceptEncryption:
			result += "\n" + add;
			result += "  peer: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
			result += "  g_b: " + mtpTextSerialize(from, end, mtpc_bytes, level + 1) + ",\n" + add;
			result += "  key_fingerprint: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		return "{ messages_acceptEncryption" + result + "}";
		}

		return mtpTextSerializeCore(from, end, cons, level, vcons);
	} catch (Exception &e) {
		QString result = "(" + QString(e.what()) + QString("), cons: %1").arg(cons);
		if (vcons) result += QString(", vcons: %1").arg(vcons);
		result += ", " + mb(start, (end - start) * sizeof(mtpPrime)).str();
		return "[ERROR] " + result;
	}
}

#endif
