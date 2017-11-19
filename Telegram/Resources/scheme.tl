// Core types (no need to gen)

//vector#1cb5c415 {t:Type} # [ t ] = Vector t;

///////////////////////////////
/////////////////// Layer cons
///////////////////////////////

//invokeAfterMsg#cb9f372d msg_id:long query:!X = X;
//invokeAfterMsgs#3dc4b4f0 msg_ids:Vector<long> query:!X = X;
//invokeWithLayer1#53835315 query:!X = X;
//invokeWithLayer2#289dd1f6 query:!X = X;
//invokeWithLayer3#b7475268 query:!X = X;
//invokeWithLayer4#dea0d430 query:!X = X;
//invokeWithLayer5#417a57ae query:!X = X;
//invokeWithLayer6#3a64d54d query:!X = X;
//invokeWithLayer7#a5be56d3 query:!X = X;
//invokeWithLayer8#e9abd9fd query:!X = X;
//invokeWithLayer9#76715a63 query:!X = X;
//invokeWithLayer10#39620c41 query:!X = X;
//invokeWithLayer11#a6b88fdf query:!X = X;
//invokeWithLayer12#dda60d3c query:!X = X;
//invokeWithLayer13#427c8ea2 query:!X = X;
//invokeWithLayer14#2b9b08fa query:!X = X;
//invokeWithLayer15#b4418b64 query:!X = X;
//invokeWithLayer16#cf5f0987 query:!X = X;
//invokeWithLayer17#50858a19 query:!X = X;
//invokeWithLayer18#1c900537 query:!X = X;
//invokeWithLayer#da9b0d0d layer:int query:!X = X; // after 18 layer

///////////////////////////////
/// Authorization key creation
///////////////////////////////

resPQ#05162463 nonce:int128 server_nonce:int128 pq:string server_public_key_fingerprints:Vector<long> = ResPQ;

p_q_inner_data#83c95aec pq:string p:string q:string nonce:int128 server_nonce:int128 new_nonce:int256 = P_Q_inner_data;

server_DH_params_fail#79cb045d nonce:int128 server_nonce:int128 new_nonce_hash:int128 = Server_DH_Params;
server_DH_params_ok#d0e8075c nonce:int128 server_nonce:int128 encrypted_answer:string = Server_DH_Params;

server_DH_inner_data#b5890dba nonce:int128 server_nonce:int128 g:int dh_prime:string g_a:string server_time:int = Server_DH_inner_data;

client_DH_inner_data#6643b654 nonce:int128 server_nonce:int128 retry_id:long g_b:string = Client_DH_Inner_Data;

dh_gen_ok#3bcbf734 nonce:int128 server_nonce:int128 new_nonce_hash1:int128 = Set_client_DH_params_answer;
dh_gen_retry#46dc1fb9 nonce:int128 server_nonce:int128 new_nonce_hash2:int128 = Set_client_DH_params_answer;
dh_gen_fail#a69dae02 nonce:int128 server_nonce:int128 new_nonce_hash3:int128 = Set_client_DH_params_answer;

destroy_auth_key_ok#f660e1d4 = DestroyAuthKeyRes;
destroy_auth_key_none#0a9f2259 = DestroyAuthKeyRes;
destroy_auth_key_fail#ea109b13 = DestroyAuthKeyRes;

---functions---

req_pq#60469778 nonce:int128 = ResPQ;

req_DH_params#d712e4be nonce:int128 server_nonce:int128 p:string q:string public_key_fingerprint:long encrypted_data:string = Server_DH_Params;

set_client_DH_params#f5045f1f nonce:int128 server_nonce:int128 encrypted_data:string = Set_client_DH_params_answer;

destroy_auth_key#d1435160 = DestroyAuthKeyRes;

///////////////////////////////
////////////// System messages
///////////////////////////////

---types---

msgs_ack#62d6b459 msg_ids:Vector<long> = MsgsAck;

bad_msg_notification#a7eff811 bad_msg_id:long bad_msg_seqno:int error_code:int = BadMsgNotification;
bad_server_salt#edab447b bad_msg_id:long bad_msg_seqno:int error_code:int new_server_salt:long = BadMsgNotification;

msgs_state_req#da69fb52 msg_ids:Vector<long> = MsgsStateReq;
msgs_state_info#04deb57d req_msg_id:long info:string = MsgsStateInfo;
msgs_all_info#8cc0d131 msg_ids:Vector<long> info:string = MsgsAllInfo;

msg_detailed_info#276d3ec6 msg_id:long answer_msg_id:long bytes:int status:int = MsgDetailedInfo;
msg_new_detailed_info#809db6df answer_msg_id:long bytes:int status:int = MsgDetailedInfo;

msg_resend_req#7d861a08 msg_ids:Vector<long> = MsgResendReq;

//rpc_result#f35c6d01 req_msg_id:long result:Object = RpcResult; // parsed manually

rpc_error#2144ca19 error_code:int error_message:string = RpcError;

rpc_answer_unknown#5e2ad36e = RpcDropAnswer;
rpc_answer_dropped_running#cd78e586 = RpcDropAnswer;
rpc_answer_dropped#a43ad8b7 msg_id:long seq_no:int bytes:int = RpcDropAnswer;

future_salt#0949d9dc valid_since:int valid_until:int salt:long = FutureSalt;
future_salts#ae500895 req_msg_id:long now:int salts:vector<future_salt> = FutureSalts;

pong#347773c5 msg_id:long ping_id:long = Pong;

destroy_session_ok#e22045fc session_id:long = DestroySessionRes;
destroy_session_none#62d350c9 session_id:long = DestroySessionRes;

new_session_created#9ec20908 first_msg_id:long unique_id:long server_salt:long = NewSession;

//message msg_id:long seqno:int bytes:int body:Object = Message; // parsed manually
//msg_container#73f1f8dc messages:vector<message> = MessageContainer; // parsed manually
//msg_copy#e06046b2 orig_message:Message = MessageCopy; // parsed manually, not used - use msg_container
//gzip_packed#3072cfa1 packed_data:string = Object; // parsed manually

http_wait#9299359f max_delay:int wait_after:int max_wait:int = HttpWait;

ipPort ipv4:int port:int = IpPort;
help.configSimple#d997c3c5 date:int expires:int dc_id:int ip_port_list:Vector<ipPort> = help.ConfigSimple;

---functions---

rpc_drop_answer#58e4a740 req_msg_id:long = RpcDropAnswer;

get_future_salts#b921bd04 num:int = FutureSalts;

ping#7abe77ec ping_id:long = Pong;
ping_delay_disconnect#f3427b8c ping_id:long disconnect_delay:int = Pong;

destroy_session#e7512126 session_id:long = DestroySessionRes;

contest.saveDeveloperInfo#9a5f6e95 vk_id:int name:string phone_number:string age:int city:string = Bool;

///////////////////////////////
///////// Main application API
///////////////////////////////

---types---

boolFalse#bc799737 = Bool;
boolTrue#997275b5 = Bool;

true#3fedd339 = True;

vector#1cb5c415 {t:Type} # [ t ] = Vector t;

error#c4b9f9bb code:int text:string = Error;

null#56730bcc = Null;

inputPeerEmpty#7f3b18ea = InputPeer;
inputPeerSelf#7da07ec9 = InputPeer;
inputPeerChat#179be863 chat_id:int = InputPeer;
inputPeerUser#7b8e7de6 user_id:int access_hash:long = InputPeer;
inputPeerChannel#20adaef8 channel_id:int access_hash:long = InputPeer;

inputUserEmpty#b98886cf = InputUser;
inputUserSelf#f7c1b13f = InputUser;
inputUser#d8292816 user_id:int access_hash:long = InputUser;

inputPhoneContact#f392b7f4 client_id:long phone:string first_name:string last_name:string = InputContact;

inputFile#f52ff27f id:long parts:int name:string md5_checksum:string = InputFile;
inputFileBig#fa4f0bb5 id:long parts:int name:string = InputFile;

inputMediaEmpty#9664f57f = InputMedia;
inputMediaUploadedPhoto#2f37e231 flags:# file:InputFile caption:string stickers:flags.0?Vector<InputDocument> ttl_seconds:flags.1?int = InputMedia;
inputMediaPhoto#81fa373a flags:# id:InputPhoto caption:string ttl_seconds:flags.0?int = InputMedia;
inputMediaGeoPoint#f9c44144 geo_point:InputGeoPoint = InputMedia;
inputMediaContact#a6e45987 phone_number:string first_name:string last_name:string = InputMedia;
inputMediaUploadedDocument#e39621fd flags:# nosound_video:flags.3?true file:InputFile thumb:flags.2?InputFile mime_type:string attributes:Vector<DocumentAttribute> caption:string stickers:flags.0?Vector<InputDocument> ttl_seconds:flags.1?int = InputMedia;
inputMediaDocument#5acb668e flags:# id:InputDocument caption:string ttl_seconds:flags.0?int = InputMedia;
inputMediaVenue#c13d1c11 geo_point:InputGeoPoint title:string address:string provider:string venue_id:string venue_type:string = InputMedia;
inputMediaGifExternal#4843b0fd url:string q:string = InputMedia;
inputMediaPhotoExternal#922aec1 flags:# url:string caption:string ttl_seconds:flags.0?int = InputMedia;
inputMediaDocumentExternal#b6f74335 flags:# url:string caption:string ttl_seconds:flags.0?int = InputMedia;
inputMediaGame#d33f43f3 id:InputGame = InputMedia;
inputMediaInvoice#92153685 flags:# title:string description:string photo:flags.0?InputWebDocument invoice:Invoice payload:bytes provider:string start_param:string = InputMedia;
inputMediaGeoLive#7b1a118f geo_point:InputGeoPoint period:int = InputMedia;

inputChatPhotoEmpty#1ca48f57 = InputChatPhoto;
inputChatUploadedPhoto#927c55b4 file:InputFile = InputChatPhoto;
inputChatPhoto#8953ad37 id:InputPhoto = InputChatPhoto;

inputGeoPointEmpty#e4c123d6 = InputGeoPoint;
inputGeoPoint#f3b7acc9 lat:double long:double = InputGeoPoint;

inputPhotoEmpty#1cd7bf0d = InputPhoto;
inputPhoto#fb95c6c4 id:long access_hash:long = InputPhoto;

inputFileLocation#14637196 volume_id:long local_id:int secret:long = InputFileLocation;
inputEncryptedFileLocation#f5235d55 id:long access_hash:long = InputFileLocation;
inputDocumentFileLocation#430f0724 id:long access_hash:long version:int = InputFileLocation;

inputAppEvent#770656a8 time:double type:string peer:long data:string = InputAppEvent;

peerUser#9db1bc6d user_id:int = Peer;
peerChat#bad0e5bb chat_id:int = Peer;
peerChannel#bddde532 channel_id:int = Peer;

storage.fileUnknown#aa963b05 = storage.FileType;
storage.filePartial#40bc6f52 = storage.FileType;
storage.fileJpeg#7efe0e = storage.FileType;
storage.fileGif#cae1aadf = storage.FileType;
storage.filePng#a4f63c0 = storage.FileType;
storage.filePdf#ae1e508d = storage.FileType;
storage.fileMp3#528a0677 = storage.FileType;
storage.fileMov#4b09ebbc = storage.FileType;
storage.fileMp4#b3cea0e4 = storage.FileType;
storage.fileWebp#1081464c = storage.FileType;

fileLocationUnavailable#7c596b46 volume_id:long local_id:int secret:long = FileLocation;
fileLocation#53d69076 dc_id:int volume_id:long local_id:int secret:long = FileLocation;

userEmpty#200250ba id:int = User;
user#2e13f4c3 flags:# self:flags.10?true contact:flags.11?true mutual_contact:flags.12?true deleted:flags.13?true bot:flags.14?true bot_chat_history:flags.15?true bot_nochats:flags.16?true verified:flags.17?true restricted:flags.18?true min:flags.20?true bot_inline_geo:flags.21?true id:int access_hash:flags.0?long first_name:flags.1?string last_name:flags.2?string username:flags.3?string phone:flags.4?string photo:flags.5?UserProfilePhoto status:flags.6?UserStatus bot_info_version:flags.14?int restriction_reason:flags.18?string bot_inline_placeholder:flags.19?string lang_code:flags.22?string = User;

userProfilePhotoEmpty#4f11bae1 = UserProfilePhoto;
userProfilePhoto#d559d8c8 photo_id:long photo_small:FileLocation photo_big:FileLocation = UserProfilePhoto;

userStatusEmpty#9d05049 = UserStatus;
userStatusOnline#edb93949 expires:int = UserStatus;
userStatusOffline#8c703f was_online:int = UserStatus;
userStatusRecently#e26f42f1 = UserStatus;
userStatusLastWeek#7bf09fc = UserStatus;
userStatusLastMonth#77ebc742 = UserStatus;

chatEmpty#9ba2d800 id:int = Chat;
chat#d91cdd54 flags:# creator:flags.0?true kicked:flags.1?true left:flags.2?true admins_enabled:flags.3?true admin:flags.4?true deactivated:flags.5?true id:int title:string photo:ChatPhoto participants_count:int date:int version:int migrated_to:flags.6?InputChannel = Chat;
chatForbidden#7328bdb id:int title:string = Chat;
channel#cb44b1c flags:# creator:flags.0?true left:flags.2?true editor:flags.3?true broadcast:flags.5?true verified:flags.7?true megagroup:flags.8?true restricted:flags.9?true democracy:flags.10?true signatures:flags.11?true min:flags.12?true id:int access_hash:flags.13?long title:string username:flags.6?string photo:ChatPhoto date:int version:int restriction_reason:flags.9?string admin_rights:flags.14?ChannelAdminRights banned_rights:flags.15?ChannelBannedRights = Chat;
channelForbidden#289da732 flags:# broadcast:flags.5?true megagroup:flags.8?true id:int access_hash:long title:string until_date:flags.16?int = Chat;

chatFull#2e02a614 id:int participants:ChatParticipants chat_photo:Photo notify_settings:PeerNotifySettings exported_invite:ExportedChatInvite bot_info:Vector<BotInfo> = ChatFull;
channelFull#76af5481 flags:# can_view_participants:flags.3?true can_set_username:flags.6?true can_set_stickers:flags.7?true hidden_prehistory:flags.10?true id:int about:string participants_count:flags.0?int admins_count:flags.1?int kicked_count:flags.2?int banned_count:flags.2?int read_inbox_max_id:int read_outbox_max_id:int unread_count:int chat_photo:Photo notify_settings:PeerNotifySettings exported_invite:ExportedChatInvite bot_info:Vector<BotInfo> migrated_from_chat_id:flags.4?int migrated_from_max_id:flags.4?int pinned_msg_id:flags.5?int stickerset:flags.8?StickerSet available_min_id:flags.9?int = ChatFull;

chatParticipant#c8d7493e user_id:int inviter_id:int date:int = ChatParticipant;
chatParticipantCreator#da13538a user_id:int = ChatParticipant;
chatParticipantAdmin#e2d6e436 user_id:int inviter_id:int date:int = ChatParticipant;

chatParticipantsForbidden#fc900c2b flags:# chat_id:int self_participant:flags.0?ChatParticipant = ChatParticipants;
chatParticipants#3f460fed chat_id:int participants:Vector<ChatParticipant> version:int = ChatParticipants;

chatPhotoEmpty#37c1011c = ChatPhoto;
chatPhoto#6153276a photo_small:FileLocation photo_big:FileLocation = ChatPhoto;

messageEmpty#83e5de54 id:int = Message;
message#90dddc11 flags:# out:flags.1?true mentioned:flags.4?true media_unread:flags.5?true silent:flags.13?true post:flags.14?true id:int from_id:flags.8?int to_id:Peer fwd_from:flags.2?MessageFwdHeader via_bot_id:flags.11?int reply_to_msg_id:flags.3?int date:int message:string media:flags.9?MessageMedia reply_markup:flags.6?ReplyMarkup entities:flags.7?Vector<MessageEntity> views:flags.10?int edit_date:flags.15?int post_author:flags.16?string = Message;
messageService#9e19a1f6 flags:# out:flags.1?true mentioned:flags.4?true media_unread:flags.5?true silent:flags.13?true post:flags.14?true id:int from_id:flags.8?int to_id:Peer reply_to_msg_id:flags.3?int date:int action:MessageAction = Message;

messageMediaEmpty#3ded6320 = MessageMedia;
messageMediaPhoto#b5223b0f flags:# photo:flags.0?Photo caption:flags.1?string ttl_seconds:flags.2?int = MessageMedia;
messageMediaGeo#56e0d474 geo:GeoPoint = MessageMedia;
messageMediaContact#5e7d2f39 phone_number:string first_name:string last_name:string user_id:int = MessageMedia;
messageMediaUnsupported#9f84f49e = MessageMedia;
messageMediaDocument#7c4414d3 flags:# document:flags.0?Document caption:flags.1?string ttl_seconds:flags.2?int = MessageMedia;
messageMediaWebPage#a32dd600 webpage:WebPage = MessageMedia;
messageMediaVenue#2ec0533f geo:GeoPoint title:string address:string provider:string venue_id:string venue_type:string = MessageMedia;
messageMediaGame#fdb19008 game:Game = MessageMedia;
messageMediaInvoice#84551347 flags:# shipping_address_requested:flags.1?true test:flags.3?true title:string description:string photo:flags.0?WebDocument receipt_msg_id:flags.2?int currency:string total_amount:long start_param:string = MessageMedia;
messageMediaGeoLive#7c3c2609 geo:GeoPoint period:int = MessageMedia;

messageActionEmpty#b6aef7b0 = MessageAction;
messageActionChatCreate#a6638b9a title:string users:Vector<int> = MessageAction;
messageActionChatEditTitle#b5a1ce5a title:string = MessageAction;
messageActionChatEditPhoto#7fcb13a8 photo:Photo = MessageAction;
messageActionChatDeletePhoto#95e3fbef = MessageAction;
messageActionChatAddUser#488a7337 users:Vector<int> = MessageAction;
messageActionChatDeleteUser#b2ae9b0c user_id:int = MessageAction;
messageActionChatJoinedByLink#f89cf5e8 inviter_id:int = MessageAction;
messageActionChannelCreate#95d2ac92 title:string = MessageAction;
messageActionChatMigrateTo#51bdb021 channel_id:int = MessageAction;
messageActionChannelMigrateFrom#b055eaee title:string chat_id:int = MessageAction;
messageActionPinMessage#94bd38ed = MessageAction;
messageActionHistoryClear#9fbab604 = MessageAction;
messageActionGameScore#92a72876 game_id:long score:int = MessageAction;
messageActionPaymentSentMe#8f31b327 flags:# currency:string total_amount:long payload:bytes info:flags.0?PaymentRequestedInfo shipping_option_id:flags.1?string charge:PaymentCharge = MessageAction;
messageActionPaymentSent#40699cd0 currency:string total_amount:long = MessageAction;
messageActionPhoneCall#80e11a7f flags:# call_id:long reason:flags.0?PhoneCallDiscardReason duration:flags.1?int = MessageAction;
messageActionScreenshotTaken#4792929b = MessageAction;
messageActionCustomAction#fae69f56 message:string = MessageAction;

dialog#e4def5db flags:# pinned:flags.2?true peer:Peer top_message:int read_inbox_max_id:int read_outbox_max_id:int unread_count:int unread_mentions_count:int notify_settings:PeerNotifySettings pts:flags.0?int draft:flags.1?DraftMessage = Dialog;

photoEmpty#2331b22d id:long = Photo;
photo#9288dd29 flags:# has_stickers:flags.0?true id:long access_hash:long date:int sizes:Vector<PhotoSize> = Photo;

photoSizeEmpty#e17e23c type:string = PhotoSize;
photoSize#77bfb61b type:string location:FileLocation w:int h:int size:int = PhotoSize;
photoCachedSize#e9a734fa type:string location:FileLocation w:int h:int bytes:bytes = PhotoSize;

geoPointEmpty#1117dd5f = GeoPoint;
geoPoint#2049d70c long:double lat:double = GeoPoint;

auth.checkedPhone#811ea28e phone_registered:Bool = auth.CheckedPhone;

auth.sentCode#5e002502 flags:# phone_registered:flags.0?true type:auth.SentCodeType phone_code_hash:string next_type:flags.1?auth.CodeType timeout:flags.2?int = auth.SentCode;

auth.authorization#cd050916 flags:# tmp_sessions:flags.0?int user:User = auth.Authorization;

auth.exportedAuthorization#df969c2d id:int bytes:bytes = auth.ExportedAuthorization;

inputNotifyPeer#b8bc5b0c peer:InputPeer = InputNotifyPeer;
inputNotifyUsers#193b4417 = InputNotifyPeer;
inputNotifyChats#4a95e84e = InputNotifyPeer;
inputNotifyAll#a429b886 = InputNotifyPeer;

inputPeerNotifyEventsEmpty#f03064d8 = InputPeerNotifyEvents;
inputPeerNotifyEventsAll#e86a2c74 = InputPeerNotifyEvents;

inputPeerNotifySettings#38935eb2 flags:# show_previews:flags.0?true silent:flags.1?true mute_until:int sound:string = InputPeerNotifySettings;

peerNotifyEventsEmpty#add53cb3 = PeerNotifyEvents;
peerNotifyEventsAll#6d1ded88 = PeerNotifyEvents;

peerNotifySettingsEmpty#70a68512 = PeerNotifySettings;
peerNotifySettings#9acda4c0 flags:# show_previews:flags.0?true silent:flags.1?true mute_until:int sound:string = PeerNotifySettings;

peerSettings#818426cd flags:# report_spam:flags.0?true = PeerSettings;

wallPaper#ccb03657 id:int title:string sizes:Vector<PhotoSize> color:int = WallPaper;
wallPaperSolid#63117f24 id:int title:string bg_color:int color:int = WallPaper;

inputReportReasonSpam#58dbcab8 = ReportReason;
inputReportReasonViolence#1e22c78d = ReportReason;
inputReportReasonPornography#2e59d922 = ReportReason;
inputReportReasonOther#e1746d0a text:string = ReportReason;

userFull#f220f3f flags:# blocked:flags.0?true phone_calls_available:flags.4?true phone_calls_private:flags.5?true user:User about:flags.1?string link:contacts.Link profile_photo:flags.2?Photo notify_settings:PeerNotifySettings bot_info:flags.3?BotInfo common_chats_count:int = UserFull;

contact#f911c994 user_id:int mutual:Bool = Contact;

importedContact#d0028438 user_id:int client_id:long = ImportedContact;

contactBlocked#561bc879 user_id:int date:int = ContactBlocked;

contactStatus#d3680c61 user_id:int status:UserStatus = ContactStatus;

contacts.link#3ace484c my_link:ContactLink foreign_link:ContactLink user:User = contacts.Link;

contacts.contactsNotModified#b74ba9d2 = contacts.Contacts;
contacts.contacts#eae87e42 contacts:Vector<Contact> saved_count:int users:Vector<User> = contacts.Contacts;

contacts.importedContacts#77d01c3b imported:Vector<ImportedContact> popular_invites:Vector<PopularContact> retry_contacts:Vector<long> users:Vector<User> = contacts.ImportedContacts;

contacts.blocked#1c138d15 blocked:Vector<ContactBlocked> users:Vector<User> = contacts.Blocked;
contacts.blockedSlice#900802a1 count:int blocked:Vector<ContactBlocked> users:Vector<User> = contacts.Blocked;

messages.dialogs#15ba6c40 dialogs:Vector<Dialog> messages:Vector<Message> chats:Vector<Chat> users:Vector<User> = messages.Dialogs;
messages.dialogsSlice#71e094f3 count:int dialogs:Vector<Dialog> messages:Vector<Message> chats:Vector<Chat> users:Vector<User> = messages.Dialogs;

messages.messages#8c718e87 messages:Vector<Message> chats:Vector<Chat> users:Vector<User> = messages.Messages;
messages.messagesSlice#b446ae3 count:int messages:Vector<Message> chats:Vector<Chat> users:Vector<User> = messages.Messages;
messages.channelMessages#99262e37 flags:# pts:int count:int messages:Vector<Message> chats:Vector<Chat> users:Vector<User> = messages.Messages;
messages.messagesNotModified#74535f21 count:int = messages.Messages;

messages.chats#64ff9fd5 chats:Vector<Chat> = messages.Chats;
messages.chatsSlice#9cd81144 count:int chats:Vector<Chat> = messages.Chats;

messages.chatFull#e5d7d19c full_chat:ChatFull chats:Vector<Chat> users:Vector<User> = messages.ChatFull;

messages.affectedHistory#b45c69d1 pts:int pts_count:int offset:int = messages.AffectedHistory;

inputMessagesFilterEmpty#57e2f66c = MessagesFilter;
inputMessagesFilterPhotos#9609a51c = MessagesFilter;
inputMessagesFilterVideo#9fc00e65 = MessagesFilter;
inputMessagesFilterPhotoVideo#56e9f0e4 = MessagesFilter;
inputMessagesFilterDocument#9eddf188 = MessagesFilter;
inputMessagesFilterUrl#7ef0dd87 = MessagesFilter;
inputMessagesFilterGif#ffc86587 = MessagesFilter;
inputMessagesFilterVoice#50f5c392 = MessagesFilter;
inputMessagesFilterMusic#3751b49e = MessagesFilter;
inputMessagesFilterChatPhotos#3a20ecb8 = MessagesFilter;
inputMessagesFilterPhoneCalls#80c99768 flags:# missed:flags.0?true = MessagesFilter;
inputMessagesFilterRoundVoice#7a7c17a4 = MessagesFilter;
inputMessagesFilterRoundVideo#b549da53 = MessagesFilter;
inputMessagesFilterMyMentions#c1f8e69a = MessagesFilter;
inputMessagesFilterGeo#e7026d0d = MessagesFilter;
inputMessagesFilterContacts#e062db83 = MessagesFilter;

updateNewMessage#1f2b0afd message:Message pts:int pts_count:int = Update;
updateMessageID#4e90bfd6 id:int random_id:long = Update;
updateDeleteMessages#a20db0e5 messages:Vector<int> pts:int pts_count:int = Update;
updateUserTyping#5c486927 user_id:int action:SendMessageAction = Update;
updateChatUserTyping#9a65ea1f chat_id:int user_id:int action:SendMessageAction = Update;
updateChatParticipants#7761198 participants:ChatParticipants = Update;
updateUserStatus#1bfbd823 user_id:int status:UserStatus = Update;
updateUserName#a7332b73 user_id:int first_name:string last_name:string username:string = Update;
updateUserPhoto#95313b0c user_id:int date:int photo:UserProfilePhoto previous:Bool = Update;
updateContactRegistered#2575bbb9 user_id:int date:int = Update;
updateContactLink#9d2e67c5 user_id:int my_link:ContactLink foreign_link:ContactLink = Update;
updateNewEncryptedMessage#12bcbd9a message:EncryptedMessage qts:int = Update;
updateEncryptedChatTyping#1710f156 chat_id:int = Update;
updateEncryption#b4a2e88d chat:EncryptedChat date:int = Update;
updateEncryptedMessagesRead#38fe25b7 chat_id:int max_date:int date:int = Update;
updateChatParticipantAdd#ea4b0e5c chat_id:int user_id:int inviter_id:int date:int version:int = Update;
updateChatParticipantDelete#6e5f8c22 chat_id:int user_id:int version:int = Update;
updateDcOptions#8e5e9873 dc_options:Vector<DcOption> = Update;
updateUserBlocked#80ece81a user_id:int blocked:Bool = Update;
updateNotifySettings#bec268ef peer:NotifyPeer notify_settings:PeerNotifySettings = Update;
updateServiceNotification#ebe46819 flags:# popup:flags.0?true inbox_date:flags.1?int type:string message:string media:MessageMedia entities:Vector<MessageEntity> = Update;
updatePrivacy#ee3b272a key:PrivacyKey rules:Vector<PrivacyRule> = Update;
updateUserPhone#12b9417b user_id:int phone:string = Update;
updateReadHistoryInbox#9961fd5c peer:Peer max_id:int pts:int pts_count:int = Update;
updateReadHistoryOutbox#2f2f21bf peer:Peer max_id:int pts:int pts_count:int = Update;
updateWebPage#7f891213 webpage:WebPage pts:int pts_count:int = Update;
updateReadMessagesContents#68c13933 messages:Vector<int> pts:int pts_count:int = Update;
updateChannelTooLong#eb0467fb flags:# channel_id:int pts:flags.0?int = Update;
updateChannel#b6d45656 channel_id:int = Update;
updateNewChannelMessage#62ba04d9 message:Message pts:int pts_count:int = Update;
updateReadChannelInbox#4214f37f channel_id:int max_id:int = Update;
updateDeleteChannelMessages#c37521c9 channel_id:int messages:Vector<int> pts:int pts_count:int = Update;
updateChannelMessageViews#98a12b4b channel_id:int id:int views:int = Update;
updateChatAdmins#6e947941 chat_id:int enabled:Bool version:int = Update;
updateChatParticipantAdmin#b6901959 chat_id:int user_id:int is_admin:Bool version:int = Update;
updateNewStickerSet#688a30aa stickerset:messages.StickerSet = Update;
updateStickerSetsOrder#bb2d201 flags:# masks:flags.0?true order:Vector<long> = Update;
updateStickerSets#43ae3dec = Update;
updateSavedGifs#9375341e = Update;
updateBotInlineQuery#54826690 flags:# query_id:long user_id:int query:string geo:flags.0?GeoPoint offset:string = Update;
updateBotInlineSend#e48f964 flags:# user_id:int query:string geo:flags.0?GeoPoint id:string msg_id:flags.1?InputBotInlineMessageID = Update;
updateEditChannelMessage#1b3f4df7 message:Message pts:int pts_count:int = Update;
updateChannelPinnedMessage#98592475 channel_id:int id:int = Update;
updateBotCallbackQuery#e73547e1 flags:# query_id:long user_id:int peer:Peer msg_id:int chat_instance:long data:flags.0?bytes game_short_name:flags.1?string = Update;
updateEditMessage#e40370a3 message:Message pts:int pts_count:int = Update;
updateInlineBotCallbackQuery#f9d27a5a flags:# query_id:long user_id:int msg_id:InputBotInlineMessageID chat_instance:long data:flags.0?bytes game_short_name:flags.1?string = Update;
updateReadChannelOutbox#25d6c9c7 channel_id:int max_id:int = Update;
updateDraftMessage#ee2bb969 peer:Peer draft:DraftMessage = Update;
updateReadFeaturedStickers#571d2742 = Update;
updateRecentStickers#9a422c20 = Update;
updateConfig#a229dd06 = Update;
updatePtsChanged#3354678f = Update;
updateChannelWebPage#40771900 channel_id:int webpage:WebPage pts:int pts_count:int = Update;
updateDialogPinned#d711a2cc flags:# pinned:flags.0?true peer:Peer = Update;
updatePinnedDialogs#d8caf68d flags:# order:flags.0?Vector<Peer> = Update;
updateBotWebhookJSON#8317c0c3 data:DataJSON = Update;
updateBotWebhookJSONQuery#9b9240a6 query_id:long data:DataJSON timeout:int = Update;
updateBotShippingQuery#e0cdc940 query_id:long user_id:int payload:bytes shipping_address:PostAddress = Update;
updateBotPrecheckoutQuery#5d2f3aa9 flags:# query_id:long user_id:int payload:bytes info:flags.0?PaymentRequestedInfo shipping_option_id:flags.1?string currency:string total_amount:long = Update;
updatePhoneCall#ab0f6b1e phone_call:PhoneCall = Update;
updateLangPackTooLong#10c2404b = Update;
updateLangPack#56022f4d difference:LangPackDifference = Update;
updateFavedStickers#e511996d = Update;
updateChannelReadMessagesContents#89893b45 channel_id:int messages:Vector<int> = Update;
updateContactsReset#7084a7be = Update;
updateChannelAvailableMessages#70db6837 channel_id:int available_min_id:int = Update;

updates.state#a56c2a3e pts:int qts:int date:int seq:int unread_count:int = updates.State;

updates.differenceEmpty#5d75a138 date:int seq:int = updates.Difference;
updates.difference#f49ca0 new_messages:Vector<Message> new_encrypted_messages:Vector<EncryptedMessage> other_updates:Vector<Update> chats:Vector<Chat> users:Vector<User> state:updates.State = updates.Difference;
updates.differenceSlice#a8fb1981 new_messages:Vector<Message> new_encrypted_messages:Vector<EncryptedMessage> other_updates:Vector<Update> chats:Vector<Chat> users:Vector<User> intermediate_state:updates.State = updates.Difference;
updates.differenceTooLong#4afe8f6d pts:int = updates.Difference;

updatesTooLong#e317af7e = Updates;
updateShortMessage#914fbf11 flags:# out:flags.1?true mentioned:flags.4?true media_unread:flags.5?true silent:flags.13?true id:int user_id:int message:string pts:int pts_count:int date:int fwd_from:flags.2?MessageFwdHeader via_bot_id:flags.11?int reply_to_msg_id:flags.3?int entities:flags.7?Vector<MessageEntity> = Updates;
updateShortChatMessage#16812688 flags:# out:flags.1?true mentioned:flags.4?true media_unread:flags.5?true silent:flags.13?true id:int from_id:int chat_id:int message:string pts:int pts_count:int date:int fwd_from:flags.2?MessageFwdHeader via_bot_id:flags.11?int reply_to_msg_id:flags.3?int entities:flags.7?Vector<MessageEntity> = Updates;
updateShort#78d4dec1 update:Update date:int = Updates;
updatesCombined#725b04c3 updates:Vector<Update> users:Vector<User> chats:Vector<Chat> date:int seq_start:int seq:int = Updates;
updates#74ae4240 updates:Vector<Update> users:Vector<User> chats:Vector<Chat> date:int seq:int = Updates;
updateShortSentMessage#11f1331c flags:# out:flags.1?true id:int pts:int pts_count:int date:int media:flags.9?MessageMedia entities:flags.7?Vector<MessageEntity> = Updates;

photos.photos#8dca6aa5 photos:Vector<Photo> users:Vector<User> = photos.Photos;
photos.photosSlice#15051f54 count:int photos:Vector<Photo> users:Vector<User> = photos.Photos;

photos.photo#20212ca8 photo:Photo users:Vector<User> = photos.Photo;

upload.file#96a18d5 type:storage.FileType mtime:int bytes:bytes = upload.File;
upload.fileCdnRedirect#ea52fe5a dc_id:int file_token:bytes encryption_key:bytes encryption_iv:bytes cdn_file_hashes:Vector<CdnFileHash> = upload.File;

dcOption#5d8c6cc flags:# ipv6:flags.0?true media_only:flags.1?true tcpo_only:flags.2?true cdn:flags.3?true static:flags.4?true id:int ip_address:string port:int = DcOption;

config#9c840964 flags:# phonecalls_enabled:flags.1?true date:int expires:int test_mode:Bool this_dc:int dc_options:Vector<DcOption> chat_size_max:int megagroup_size_max:int forwarded_count_max:int online_update_period_ms:int offline_blur_timeout_ms:int offline_idle_timeout_ms:int online_cloud_timeout_ms:int notify_cloud_delay_ms:int notify_default_delay_ms:int chat_big_size:int push_chat_period_ms:int push_chat_limit:int saved_gifs_limit:int edit_time_limit:int rating_e_decay:int stickers_recent_limit:int stickers_faved_limit:int channels_read_media_period:int tmp_sessions:flags.0?int pinned_dialogs_count_max:int call_receive_timeout_ms:int call_ring_timeout_ms:int call_connect_timeout_ms:int call_packet_timeout_ms:int me_url_prefix:string suggested_lang_code:flags.2?string lang_pack_version:flags.2?int disabled_features:Vector<DisabledFeature> = Config;

nearestDc#8e1a1775 country:string this_dc:int nearest_dc:int = NearestDc;

help.appUpdate#8987f311 id:int critical:Bool url:string text:string = help.AppUpdate;
help.noAppUpdate#c45a6536 = help.AppUpdate;

help.inviteText#18cb9f78 message:string = help.InviteText;

encryptedChatEmpty#ab7ec0a0 id:int = EncryptedChat;
encryptedChatWaiting#3bf703dc id:int access_hash:long date:int admin_id:int participant_id:int = EncryptedChat;
encryptedChatRequested#c878527e id:int access_hash:long date:int admin_id:int participant_id:int g_a:bytes = EncryptedChat;
encryptedChat#fa56ce36 id:int access_hash:long date:int admin_id:int participant_id:int g_a_or_b:bytes key_fingerprint:long = EncryptedChat;
encryptedChatDiscarded#13d6dd27 id:int = EncryptedChat;

inputEncryptedChat#f141b5e1 chat_id:int access_hash:long = InputEncryptedChat;

encryptedFileEmpty#c21f497e = EncryptedFile;
encryptedFile#4a70994c id:long access_hash:long size:int dc_id:int key_fingerprint:int = EncryptedFile;

inputEncryptedFileEmpty#1837c364 = InputEncryptedFile;
inputEncryptedFileUploaded#64bd0306 id:long parts:int md5_checksum:string key_fingerprint:int = InputEncryptedFile;
inputEncryptedFile#5a17b5e5 id:long access_hash:long = InputEncryptedFile;
inputEncryptedFileBigUploaded#2dc173c8 id:long parts:int key_fingerprint:int = InputEncryptedFile;

encryptedMessage#ed18c118 random_id:long chat_id:int date:int bytes:bytes file:EncryptedFile = EncryptedMessage;
encryptedMessageService#23734b06 random_id:long chat_id:int date:int bytes:bytes = EncryptedMessage;

messages.dhConfigNotModified#c0e24635 random:bytes = messages.DhConfig;
messages.dhConfig#2c221edd g:int p:bytes version:int random:bytes = messages.DhConfig;

messages.sentEncryptedMessage#560f8935 date:int = messages.SentEncryptedMessage;
messages.sentEncryptedFile#9493ff32 date:int file:EncryptedFile = messages.SentEncryptedMessage;

inputDocumentEmpty#72f0eaae = InputDocument;
inputDocument#18798952 id:long access_hash:long = InputDocument;

documentEmpty#36f8c871 id:long = Document;
document#87232bc7 id:long access_hash:long date:int mime_type:string size:int thumb:PhotoSize dc_id:int version:int attributes:Vector<DocumentAttribute> = Document;

help.support#17c6b5f6 phone_number:string user:User = help.Support;

notifyPeer#9fd40bd8 peer:Peer = NotifyPeer;
notifyUsers#b4c83b4c = NotifyPeer;
notifyChats#c007cec3 = NotifyPeer;
notifyAll#74d07c60 = NotifyPeer;

sendMessageTypingAction#16bf744e = SendMessageAction;
sendMessageCancelAction#fd5ec8f5 = SendMessageAction;
sendMessageRecordVideoAction#a187d66f = SendMessageAction;
sendMessageUploadVideoAction#e9763aec progress:int = SendMessageAction;
sendMessageRecordAudioAction#d52f73f7 = SendMessageAction;
sendMessageUploadAudioAction#f351d7ab progress:int = SendMessageAction;
sendMessageUploadPhotoAction#d1d34a26 progress:int = SendMessageAction;
sendMessageUploadDocumentAction#aa0cd9e4 progress:int = SendMessageAction;
sendMessageGeoLocationAction#176f8ba1 = SendMessageAction;
sendMessageChooseContactAction#628cbc6f = SendMessageAction;
sendMessageGamePlayAction#dd6a8f48 = SendMessageAction;
sendMessageRecordRoundAction#88f27fbc = SendMessageAction;
sendMessageUploadRoundAction#243e1c66 progress:int = SendMessageAction;

contacts.found#1aa1f784 results:Vector<Peer> chats:Vector<Chat> users:Vector<User> = contacts.Found;

inputPrivacyKeyStatusTimestamp#4f96cb18 = InputPrivacyKey;
inputPrivacyKeyChatInvite#bdfb0426 = InputPrivacyKey;
inputPrivacyKeyPhoneCall#fabadc5f = InputPrivacyKey;

privacyKeyStatusTimestamp#bc2eab30 = PrivacyKey;
privacyKeyChatInvite#500e6dfa = PrivacyKey;
privacyKeyPhoneCall#3d662b7b = PrivacyKey;

inputPrivacyValueAllowContacts#d09e07b = InputPrivacyRule;
inputPrivacyValueAllowAll#184b35ce = InputPrivacyRule;
inputPrivacyValueAllowUsers#131cc67f users:Vector<InputUser> = InputPrivacyRule;
inputPrivacyValueDisallowContacts#ba52007 = InputPrivacyRule;
inputPrivacyValueDisallowAll#d66b66c9 = InputPrivacyRule;
inputPrivacyValueDisallowUsers#90110467 users:Vector<InputUser> = InputPrivacyRule;

privacyValueAllowContacts#fffe1bac = PrivacyRule;
privacyValueAllowAll#65427b82 = PrivacyRule;
privacyValueAllowUsers#4d5bbe0c users:Vector<int> = PrivacyRule;
privacyValueDisallowContacts#f888fa1a = PrivacyRule;
privacyValueDisallowAll#8b73e763 = PrivacyRule;
privacyValueDisallowUsers#c7f49b7 users:Vector<int> = PrivacyRule;

account.privacyRules#554abb6f rules:Vector<PrivacyRule> users:Vector<User> = account.PrivacyRules;

accountDaysTTL#b8d0afdf days:int = AccountDaysTTL;

documentAttributeImageSize#6c37c15c w:int h:int = DocumentAttribute;
documentAttributeAnimated#11b58939 = DocumentAttribute;
documentAttributeSticker#6319d612 flags:# mask:flags.1?true alt:string stickerset:InputStickerSet mask_coords:flags.0?MaskCoords = DocumentAttribute;
documentAttributeVideo#ef02ce6 flags:# round_message:flags.0?true duration:int w:int h:int = DocumentAttribute;
documentAttributeAudio#9852f9c6 flags:# voice:flags.10?true duration:int title:flags.0?string performer:flags.1?string waveform:flags.2?bytes = DocumentAttribute;
documentAttributeFilename#15590068 file_name:string = DocumentAttribute;
documentAttributeHasStickers#9801d2f7 = DocumentAttribute;

messages.stickersNotModified#f1749a22 = messages.Stickers;
messages.stickers#8a8ecd32 hash:string stickers:Vector<Document> = messages.Stickers;

stickerPack#12b299d4 emoticon:string documents:Vector<long> = StickerPack;

messages.allStickersNotModified#e86602c3 = messages.AllStickers;
messages.allStickers#edfd405f hash:int sets:Vector<StickerSet> = messages.AllStickers;

disabledFeature#ae636f24 feature:string description:string = DisabledFeature;

messages.affectedMessages#84d19185 pts:int pts_count:int = messages.AffectedMessages;

contactLinkUnknown#5f4f9247 = ContactLink;
contactLinkNone#feedd3ad = ContactLink;
contactLinkHasPhone#268f3f59 = ContactLink;
contactLinkContact#d502c2d0 = ContactLink;

webPageEmpty#eb1477e8 id:long = WebPage;
webPagePending#c586da1c id:long date:int = WebPage;
webPage#5f07b4bc flags:# id:long url:string display_url:string hash:int type:flags.0?string site_name:flags.1?string title:flags.2?string description:flags.3?string photo:flags.4?Photo embed_url:flags.5?string embed_type:flags.5?string embed_width:flags.6?int embed_height:flags.6?int duration:flags.7?int author:flags.8?string document:flags.9?Document cached_page:flags.10?Page = WebPage;
webPageNotModified#85849473 = WebPage;

authorization#7bf2e6f6 hash:long flags:int device_model:string platform:string system_version:string api_id:int app_name:string app_version:string date_created:int date_active:int ip:string country:string region:string = Authorization;

account.authorizations#1250abde authorizations:Vector<Authorization> = account.Authorizations;

account.noPassword#96dabc18 new_salt:bytes email_unconfirmed_pattern:string = account.Password;
account.password#7c18141c current_salt:bytes new_salt:bytes hint:string has_recovery:Bool email_unconfirmed_pattern:string = account.Password;

account.passwordSettings#b7b72ab3 email:string = account.PasswordSettings;

account.passwordInputSettings#86916deb flags:# new_salt:flags.0?bytes new_password_hash:flags.0?bytes hint:flags.0?string email:flags.1?string = account.PasswordInputSettings;

auth.passwordRecovery#137948a5 email_pattern:string = auth.PasswordRecovery;

receivedNotifyMessage#a384b779 id:int flags:int = ReceivedNotifyMessage;

chatInviteEmpty#69df3769 = ExportedChatInvite;
chatInviteExported#fc2e05bc link:string = ExportedChatInvite;

chatInviteAlready#5a686d7c chat:Chat = ChatInvite;
chatInvite#db74f558 flags:# channel:flags.0?true broadcast:flags.1?true public:flags.2?true megagroup:flags.3?true title:string photo:ChatPhoto participants_count:int participants:flags.4?Vector<User> = ChatInvite;

inputStickerSetEmpty#ffb62b95 = InputStickerSet;
inputStickerSetID#9de7a269 id:long access_hash:long = InputStickerSet;
inputStickerSetShortName#861cc8a0 short_name:string = InputStickerSet;

stickerSet#cd303b41 flags:# installed:flags.0?true archived:flags.1?true official:flags.2?true masks:flags.3?true id:long access_hash:long title:string short_name:string count:int hash:int = StickerSet;

messages.stickerSet#b60a24a6 set:StickerSet packs:Vector<StickerPack> documents:Vector<Document> = messages.StickerSet;

botCommand#c27ac8c7 command:string description:string = BotCommand;

botInfo#98e81d3a user_id:int description:string commands:Vector<BotCommand> = BotInfo;

keyboardButton#a2fa4880 text:string = KeyboardButton;
keyboardButtonUrl#258aff05 text:string url:string = KeyboardButton;
keyboardButtonCallback#683a5e46 text:string data:bytes = KeyboardButton;
keyboardButtonRequestPhone#b16a6c29 text:string = KeyboardButton;
keyboardButtonRequestGeoLocation#fc796b3f text:string = KeyboardButton;
keyboardButtonSwitchInline#568a748 flags:# same_peer:flags.0?true text:string query:string = KeyboardButton;
keyboardButtonGame#50f41ccf text:string = KeyboardButton;
keyboardButtonBuy#afd93fbb text:string = KeyboardButton;

keyboardButtonRow#77608b83 buttons:Vector<KeyboardButton> = KeyboardButtonRow;

replyKeyboardHide#a03e5b85 flags:# selective:flags.2?true = ReplyMarkup;
replyKeyboardForceReply#f4108aa0 flags:# single_use:flags.1?true selective:flags.2?true = ReplyMarkup;
replyKeyboardMarkup#3502758c flags:# resize:flags.0?true single_use:flags.1?true selective:flags.2?true rows:Vector<KeyboardButtonRow> = ReplyMarkup;
replyInlineMarkup#48a30254 rows:Vector<KeyboardButtonRow> = ReplyMarkup;

messageEntityUnknown#bb92ba95 offset:int length:int = MessageEntity;
messageEntityMention#fa04579d offset:int length:int = MessageEntity;
messageEntityHashtag#6f635b0d offset:int length:int = MessageEntity;
messageEntityBotCommand#6cef8ac7 offset:int length:int = MessageEntity;
messageEntityUrl#6ed02538 offset:int length:int = MessageEntity;
messageEntityEmail#64e475c2 offset:int length:int = MessageEntity;
messageEntityBold#bd610bc9 offset:int length:int = MessageEntity;
messageEntityItalic#826f8b60 offset:int length:int = MessageEntity;
messageEntityCode#28a20571 offset:int length:int = MessageEntity;
messageEntityPre#73924be0 offset:int length:int language:string = MessageEntity;
messageEntityTextUrl#76a6d327 offset:int length:int url:string = MessageEntity;
messageEntityMentionName#352dca58 offset:int length:int user_id:int = MessageEntity;
inputMessageEntityMentionName#208e68c9 offset:int length:int user_id:InputUser = MessageEntity;

inputChannelEmpty#ee8c1e86 = InputChannel;
inputChannel#afeb712e channel_id:int access_hash:long = InputChannel;

contacts.resolvedPeer#7f077ad9 peer:Peer chats:Vector<Chat> users:Vector<User> = contacts.ResolvedPeer;

messageRange#ae30253 min_id:int max_id:int = MessageRange;

updates.channelDifferenceEmpty#3e11affb flags:# final:flags.0?true pts:int timeout:flags.1?int = updates.ChannelDifference;
updates.channelDifferenceTooLong#6a9d7b35 flags:# final:flags.0?true pts:int timeout:flags.1?int top_message:int read_inbox_max_id:int read_outbox_max_id:int unread_count:int unread_mentions_count:int messages:Vector<Message> chats:Vector<Chat> users:Vector<User> = updates.ChannelDifference;
updates.channelDifference#2064674e flags:# final:flags.0?true pts:int timeout:flags.1?int new_messages:Vector<Message> other_updates:Vector<Update> chats:Vector<Chat> users:Vector<User> = updates.ChannelDifference;

channelMessagesFilterEmpty#94d42ee7 = ChannelMessagesFilter;
channelMessagesFilter#cd77d957 flags:# exclude_new_messages:flags.1?true ranges:Vector<MessageRange> = ChannelMessagesFilter;

channelParticipant#15ebac1d user_id:int date:int = ChannelParticipant;
channelParticipantSelf#a3289a6d user_id:int inviter_id:int date:int = ChannelParticipant;
channelParticipantCreator#e3e2e1f9 user_id:int = ChannelParticipant;
channelParticipantAdmin#a82fa898 flags:# can_edit:flags.0?true user_id:int inviter_id:int promoted_by:int date:int admin_rights:ChannelAdminRights = ChannelParticipant;
channelParticipantBanned#222c1886 flags:# left:flags.0?true user_id:int kicked_by:int date:int banned_rights:ChannelBannedRights = ChannelParticipant;

channelParticipantsRecent#de3f3c79 = ChannelParticipantsFilter;
channelParticipantsAdmins#b4608969 = ChannelParticipantsFilter;
channelParticipantsKicked#a3b54985 q:string = ChannelParticipantsFilter;
channelParticipantsBots#b0d1865b = ChannelParticipantsFilter;
channelParticipantsBanned#1427a5e1 q:string = ChannelParticipantsFilter;
channelParticipantsSearch#656ac4b q:string = ChannelParticipantsFilter;

channels.channelParticipants#f56ee2a8 count:int participants:Vector<ChannelParticipant> users:Vector<User> = channels.ChannelParticipants;
channels.channelParticipantsNotModified#f0173fe9 = channels.ChannelParticipants;

channels.channelParticipant#d0d9b163 participant:ChannelParticipant users:Vector<User> = channels.ChannelParticipant;

help.termsOfService#f1ee3e90 text:string = help.TermsOfService;

foundGif#162ecc1f url:string thumb_url:string content_url:string content_type:string w:int h:int = FoundGif;
foundGifCached#9c750409 url:string photo:Photo document:Document = FoundGif;

messages.foundGifs#450a1c0a next_offset:int results:Vector<FoundGif> = messages.FoundGifs;

messages.savedGifsNotModified#e8025ca2 = messages.SavedGifs;
messages.savedGifs#2e0709a5 hash:int gifs:Vector<Document> = messages.SavedGifs;

inputBotInlineMessageMediaAuto#292fed13 flags:# caption:string reply_markup:flags.2?ReplyMarkup = InputBotInlineMessage;
inputBotInlineMessageText#3dcd7a87 flags:# no_webpage:flags.0?true message:string entities:flags.1?Vector<MessageEntity> reply_markup:flags.2?ReplyMarkup = InputBotInlineMessage;
inputBotInlineMessageMediaGeo#c1b15d65 flags:# geo_point:InputGeoPoint period:int reply_markup:flags.2?ReplyMarkup = InputBotInlineMessage;
inputBotInlineMessageMediaVenue#aaafadc8 flags:# geo_point:InputGeoPoint title:string address:string provider:string venue_id:string reply_markup:flags.2?ReplyMarkup = InputBotInlineMessage;
inputBotInlineMessageMediaContact#2daf01a7 flags:# phone_number:string first_name:string last_name:string reply_markup:flags.2?ReplyMarkup = InputBotInlineMessage;
inputBotInlineMessageGame#4b425864 flags:# reply_markup:flags.2?ReplyMarkup = InputBotInlineMessage;

inputBotInlineResult#2cbbe15a flags:# id:string type:string title:flags.1?string description:flags.2?string url:flags.3?string thumb_url:flags.4?string content_url:flags.5?string content_type:flags.5?string w:flags.6?int h:flags.6?int duration:flags.7?int send_message:InputBotInlineMessage = InputBotInlineResult;
inputBotInlineResultPhoto#a8d864a7 id:string type:string photo:InputPhoto send_message:InputBotInlineMessage = InputBotInlineResult;
inputBotInlineResultDocument#fff8fdc4 flags:# id:string type:string title:flags.1?string description:flags.2?string document:InputDocument send_message:InputBotInlineMessage = InputBotInlineResult;
inputBotInlineResultGame#4fa417f2 id:string short_name:string send_message:InputBotInlineMessage = InputBotInlineResult;

botInlineMessageMediaAuto#a74b15b flags:# caption:string reply_markup:flags.2?ReplyMarkup = BotInlineMessage;
botInlineMessageText#8c7f65e2 flags:# no_webpage:flags.0?true message:string entities:flags.1?Vector<MessageEntity> reply_markup:flags.2?ReplyMarkup = BotInlineMessage;
botInlineMessageMediaGeo#b722de65 flags:# geo:GeoPoint period:int reply_markup:flags.2?ReplyMarkup = BotInlineMessage;
botInlineMessageMediaVenue#4366232e flags:# geo:GeoPoint title:string address:string provider:string venue_id:string reply_markup:flags.2?ReplyMarkup = BotInlineMessage;
botInlineMessageMediaContact#35edb4d4 flags:# phone_number:string first_name:string last_name:string reply_markup:flags.2?ReplyMarkup = BotInlineMessage;

botInlineResult#9bebaeb9 flags:# id:string type:string title:flags.1?string description:flags.2?string url:flags.3?string thumb_url:flags.4?string content_url:flags.5?string content_type:flags.5?string w:flags.6?int h:flags.6?int duration:flags.7?int send_message:BotInlineMessage = BotInlineResult;
botInlineMediaResult#17db940b flags:# id:string type:string photo:flags.0?Photo document:flags.1?Document title:flags.2?string description:flags.3?string send_message:BotInlineMessage = BotInlineResult;

messages.botResults#947ca848 flags:# gallery:flags.0?true query_id:long next_offset:flags.1?string switch_pm:flags.2?InlineBotSwitchPM results:Vector<BotInlineResult> cache_time:int users:Vector<User> = messages.BotResults;

exportedMessageLink#1f486803 link:string = ExportedMessageLink;

messageFwdHeader#fadff4ac flags:# from_id:flags.0?int date:int channel_id:flags.1?int channel_post:flags.2?int post_author:flags.3?string = MessageFwdHeader;

auth.codeTypeSms#72a3158c = auth.CodeType;
auth.codeTypeCall#741cd3e3 = auth.CodeType;
auth.codeTypeFlashCall#226ccefb = auth.CodeType;

auth.sentCodeTypeApp#3dbb5986 length:int = auth.SentCodeType;
auth.sentCodeTypeSms#c000bba2 length:int = auth.SentCodeType;
auth.sentCodeTypeCall#5353e5a7 length:int = auth.SentCodeType;
auth.sentCodeTypeFlashCall#ab03c6d9 pattern:string = auth.SentCodeType;

messages.botCallbackAnswer#36585ea4 flags:# alert:flags.1?true has_url:flags.3?true native_ui:flags.4?true message:flags.0?string url:flags.2?string cache_time:int = messages.BotCallbackAnswer;

messages.messageEditData#26b5dde6 flags:# caption:flags.0?true = messages.MessageEditData;

inputBotInlineMessageID#890c3d89 dc_id:int id:long access_hash:long = InputBotInlineMessageID;

inlineBotSwitchPM#3c20629f text:string start_param:string = InlineBotSwitchPM;

messages.peerDialogs#3371c354 dialogs:Vector<Dialog> messages:Vector<Message> chats:Vector<Chat> users:Vector<User> state:updates.State = messages.PeerDialogs;

topPeer#edcdc05b peer:Peer rating:double = TopPeer;

topPeerCategoryBotsPM#ab661b5b = TopPeerCategory;
topPeerCategoryBotsInline#148677e2 = TopPeerCategory;
topPeerCategoryCorrespondents#637b7ed = TopPeerCategory;
topPeerCategoryGroups#bd17a14a = TopPeerCategory;
topPeerCategoryChannels#161d9628 = TopPeerCategory;
topPeerCategoryPhoneCalls#1e76a78c = TopPeerCategory;

topPeerCategoryPeers#fb834291 category:TopPeerCategory count:int peers:Vector<TopPeer> = TopPeerCategoryPeers;

contacts.topPeersNotModified#de266ef5 = contacts.TopPeers;
contacts.topPeers#70b772a8 categories:Vector<TopPeerCategoryPeers> chats:Vector<Chat> users:Vector<User> = contacts.TopPeers;

draftMessageEmpty#ba4baec5 = DraftMessage;
draftMessage#fd8e711f flags:# no_webpage:flags.1?true reply_to_msg_id:flags.0?int message:string entities:flags.3?Vector<MessageEntity> date:int = DraftMessage;

messages.featuredStickersNotModified#4ede3cf = messages.FeaturedStickers;
messages.featuredStickers#f89d88e5 hash:int sets:Vector<StickerSetCovered> unread:Vector<long> = messages.FeaturedStickers;

messages.recentStickersNotModified#b17f890 = messages.RecentStickers;
messages.recentStickers#5ce20970 hash:int stickers:Vector<Document> = messages.RecentStickers;

messages.archivedStickers#4fcba9c8 count:int sets:Vector<StickerSetCovered> = messages.ArchivedStickers;

messages.stickerSetInstallResultSuccess#38641628 = messages.StickerSetInstallResult;
messages.stickerSetInstallResultArchive#35e410a8 sets:Vector<StickerSetCovered> = messages.StickerSetInstallResult;

stickerSetCovered#6410a5d2 set:StickerSet cover:Document = StickerSetCovered;
stickerSetMultiCovered#3407e51b set:StickerSet covers:Vector<Document> = StickerSetCovered;

maskCoords#aed6dbb2 n:int x:double y:double zoom:double = MaskCoords;

inputStickeredMediaPhoto#4a992157 id:InputPhoto = InputStickeredMedia;
inputStickeredMediaDocument#438865b id:InputDocument = InputStickeredMedia;

game#bdf9653b flags:# id:long access_hash:long short_name:string title:string description:string photo:Photo document:flags.0?Document = Game;

inputGameID#32c3e77 id:long access_hash:long = InputGame;
inputGameShortName#c331e80a bot_id:InputUser short_name:string = InputGame;

highScore#58fffcd0 pos:int user_id:int score:int = HighScore;

messages.highScores#9a3bfd99 scores:Vector<HighScore> users:Vector<User> = messages.HighScores;

textEmpty#dc3d824f = RichText;
textPlain#744694e0 text:string = RichText;
textBold#6724abc4 text:RichText = RichText;
textItalic#d912a59c text:RichText = RichText;
textUnderline#c12622c4 text:RichText = RichText;
textStrike#9bf8bb95 text:RichText = RichText;
textFixed#6c3f19b9 text:RichText = RichText;
textUrl#3c2884c1 text:RichText url:string webpage_id:long = RichText;
textEmail#de5a0dd6 text:RichText email:string = RichText;
textConcat#7e6260d7 texts:Vector<RichText> = RichText;

pageBlockUnsupported#13567e8a = PageBlock;
pageBlockTitle#70abc3fd text:RichText = PageBlock;
pageBlockSubtitle#8ffa9a1f text:RichText = PageBlock;
pageBlockAuthorDate#baafe5e0 author:RichText published_date:int = PageBlock;
pageBlockHeader#bfd064ec text:RichText = PageBlock;
pageBlockSubheader#f12bb6e1 text:RichText = PageBlock;
pageBlockParagraph#467a0766 text:RichText = PageBlock;
pageBlockPreformatted#c070d93e text:RichText language:string = PageBlock;
pageBlockFooter#48870999 text:RichText = PageBlock;
pageBlockDivider#db20b188 = PageBlock;
pageBlockAnchor#ce0d37b0 name:string = PageBlock;
pageBlockList#3a58c7f4 ordered:Bool items:Vector<RichText> = PageBlock;
pageBlockBlockquote#263d7c26 text:RichText caption:RichText = PageBlock;
pageBlockPullquote#4f4456d3 text:RichText caption:RichText = PageBlock;
pageBlockPhoto#e9c69982 photo_id:long caption:RichText = PageBlock;
pageBlockVideo#d9d71866 flags:# autoplay:flags.0?true loop:flags.1?true video_id:long caption:RichText = PageBlock;
pageBlockCover#39f23300 cover:PageBlock = PageBlock;
pageBlockEmbed#cde200d1 flags:# full_width:flags.0?true allow_scrolling:flags.3?true url:flags.1?string html:flags.2?string poster_photo_id:flags.4?long w:int h:int caption:RichText = PageBlock;
pageBlockEmbedPost#292c7be9 url:string webpage_id:long author_photo_id:long author:string date:int blocks:Vector<PageBlock> caption:RichText = PageBlock;
pageBlockCollage#8b31c4f items:Vector<PageBlock> caption:RichText = PageBlock;
pageBlockSlideshow#130c8963 items:Vector<PageBlock> caption:RichText = PageBlock;
pageBlockChannel#ef1751b5 channel:Chat = PageBlock;
pageBlockAudio#31b81a7f audio_id:long caption:RichText = PageBlock;

pagePart#8e3f9ebe blocks:Vector<PageBlock> photos:Vector<Photo> documents:Vector<Document> = Page;
pageFull#556ec7aa blocks:Vector<PageBlock> photos:Vector<Photo> documents:Vector<Document> = Page;

phoneCallDiscardReasonMissed#85e42301 = PhoneCallDiscardReason;
phoneCallDiscardReasonDisconnect#e095c1a0 = PhoneCallDiscardReason;
phoneCallDiscardReasonHangup#57adc690 = PhoneCallDiscardReason;
phoneCallDiscardReasonBusy#faf7e8c9 = PhoneCallDiscardReason;

dataJSON#7d748d04 data:string = DataJSON;

labeledPrice#cb296bf8 label:string amount:long = LabeledPrice;

invoice#c30aa358 flags:# test:flags.0?true name_requested:flags.1?true phone_requested:flags.2?true email_requested:flags.3?true shipping_address_requested:flags.4?true flexible:flags.5?true currency:string prices:Vector<LabeledPrice> = Invoice;

paymentCharge#ea02c27e id:string provider_charge_id:string = PaymentCharge;

postAddress#1e8caaeb street_line1:string street_line2:string city:string state:string country_iso2:string post_code:string = PostAddress;

paymentRequestedInfo#909c3f94 flags:# name:flags.0?string phone:flags.1?string email:flags.2?string shipping_address:flags.3?PostAddress = PaymentRequestedInfo;

paymentSavedCredentialsCard#cdc27a1f id:string title:string = PaymentSavedCredentials;

webDocument#c61acbd8 url:string access_hash:long size:int mime_type:string attributes:Vector<DocumentAttribute> dc_id:int = WebDocument;

inputWebDocument#9bed434d url:string size:int mime_type:string attributes:Vector<DocumentAttribute> = InputWebDocument;

inputWebFileLocation#c239d686 url:string access_hash:long = InputWebFileLocation;

upload.webFile#21e753bc size:int mime_type:string file_type:storage.FileType mtime:int bytes:bytes = upload.WebFile;

payments.paymentForm#3f56aea3 flags:# can_save_credentials:flags.2?true password_missing:flags.3?true bot_id:int invoice:Invoice provider_id:int url:string native_provider:flags.4?string native_params:flags.4?DataJSON saved_info:flags.0?PaymentRequestedInfo saved_credentials:flags.1?PaymentSavedCredentials users:Vector<User> = payments.PaymentForm;

payments.validatedRequestedInfo#d1451883 flags:# id:flags.0?string shipping_options:flags.1?Vector<ShippingOption> = payments.ValidatedRequestedInfo;

payments.paymentResult#4e5f810d updates:Updates = payments.PaymentResult;
payments.paymentVerficationNeeded#6b56b921 url:string = payments.PaymentResult;

payments.paymentReceipt#500911e1 flags:# date:int bot_id:int invoice:Invoice provider_id:int info:flags.0?PaymentRequestedInfo shipping:flags.1?ShippingOption currency:string total_amount:long credentials_title:string users:Vector<User> = payments.PaymentReceipt;

payments.savedInfo#fb8fe43c flags:# has_saved_credentials:flags.1?true saved_info:flags.0?PaymentRequestedInfo = payments.SavedInfo;

inputPaymentCredentialsSaved#c10eb2cf id:string tmp_password:bytes = InputPaymentCredentials;
inputPaymentCredentials#3417d728 flags:# save:flags.0?true data:DataJSON = InputPaymentCredentials;
inputPaymentCredentialsApplePay#aa1c39f payment_data:DataJSON = InputPaymentCredentials;
inputPaymentCredentialsAndroidPay#795667a6 payment_token:DataJSON = InputPaymentCredentials;

account.tmpPassword#db64fd34 tmp_password:bytes valid_until:int = account.TmpPassword;

shippingOption#b6213cdf id:string title:string prices:Vector<LabeledPrice> = ShippingOption;

inputStickerSetItem#ffa0a496 flags:# document:InputDocument emoji:string mask_coords:flags.0?MaskCoords = InputStickerSetItem;

inputPhoneCall#1e36fded id:long access_hash:long = InputPhoneCall;

phoneCallEmpty#5366c915 id:long = PhoneCall;
phoneCallWaiting#1b8f4ad1 flags:# id:long access_hash:long date:int admin_id:int participant_id:int protocol:PhoneCallProtocol receive_date:flags.0?int = PhoneCall;
phoneCallRequested#83761ce4 id:long access_hash:long date:int admin_id:int participant_id:int g_a_hash:bytes protocol:PhoneCallProtocol = PhoneCall;
phoneCallAccepted#6d003d3f id:long access_hash:long date:int admin_id:int participant_id:int g_b:bytes protocol:PhoneCallProtocol = PhoneCall;
phoneCall#ffe6ab67 id:long access_hash:long date:int admin_id:int participant_id:int g_a_or_b:bytes key_fingerprint:long protocol:PhoneCallProtocol connection:PhoneConnection alternative_connections:Vector<PhoneConnection> start_date:int = PhoneCall;
phoneCallDiscarded#50ca4de1 flags:# need_rating:flags.2?true need_debug:flags.3?true id:long reason:flags.0?PhoneCallDiscardReason duration:flags.1?int = PhoneCall;

phoneConnection#9d4c17c0 id:long ip:string ipv6:string port:int peer_tag:bytes = PhoneConnection;

phoneCallProtocol#a2bb35cb flags:# udp_p2p:flags.0?true udp_reflector:flags.1?true min_layer:int max_layer:int = PhoneCallProtocol;

phone.phoneCall#ec82e140 phone_call:PhoneCall users:Vector<User> = phone.PhoneCall;

upload.cdnFileReuploadNeeded#eea8e46e request_token:bytes = upload.CdnFile;
upload.cdnFile#a99fca4f bytes:bytes = upload.CdnFile;

cdnPublicKey#c982eaba dc_id:int public_key:string = CdnPublicKey;

cdnConfig#5725e40a public_keys:Vector<CdnPublicKey> = CdnConfig;

langPackString#cad181f6 key:string value:string = LangPackString;
langPackStringPluralized#6c47ac9f flags:# key:string zero_value:flags.0?string one_value:flags.1?string two_value:flags.2?string few_value:flags.3?string many_value:flags.4?string other_value:string = LangPackString;
langPackStringDeleted#2979eeb2 key:string = LangPackString;

langPackDifference#f385c1f6 lang_code:string from_version:int version:int strings:Vector<LangPackString> = LangPackDifference;

langPackLanguage#117698f1 name:string native_name:string lang_code:string = LangPackLanguage;

channelAdminRights#5d7ceba5 flags:# change_info:flags.0?true post_messages:flags.1?true edit_messages:flags.2?true delete_messages:flags.3?true ban_users:flags.4?true invite_users:flags.5?true invite_link:flags.6?true pin_messages:flags.7?true add_admins:flags.9?true = ChannelAdminRights;

channelBannedRights#58cf4249 flags:# view_messages:flags.0?true send_messages:flags.1?true send_media:flags.2?true send_stickers:flags.3?true send_gifs:flags.4?true send_games:flags.5?true send_inline:flags.6?true embed_links:flags.7?true until_date:int = ChannelBannedRights;

channelAdminLogEventActionChangeTitle#e6dfb825 prev_value:string new_value:string = ChannelAdminLogEventAction;
channelAdminLogEventActionChangeAbout#55188a2e prev_value:string new_value:string = ChannelAdminLogEventAction;
channelAdminLogEventActionChangeUsername#6a4afc38 prev_value:string new_value:string = ChannelAdminLogEventAction;
channelAdminLogEventActionChangePhoto#b82f55c3 prev_photo:ChatPhoto new_photo:ChatPhoto = ChannelAdminLogEventAction;
channelAdminLogEventActionToggleInvites#1b7907ae new_value:Bool = ChannelAdminLogEventAction;
channelAdminLogEventActionToggleSignatures#26ae0971 new_value:Bool = ChannelAdminLogEventAction;
channelAdminLogEventActionUpdatePinned#e9e82c18 message:Message = ChannelAdminLogEventAction;
channelAdminLogEventActionEditMessage#709b2405 prev_message:Message new_message:Message = ChannelAdminLogEventAction;
channelAdminLogEventActionDeleteMessage#42e047bb message:Message = ChannelAdminLogEventAction;
channelAdminLogEventActionParticipantJoin#183040d3 = ChannelAdminLogEventAction;
channelAdminLogEventActionParticipantLeave#f89777f2 = ChannelAdminLogEventAction;
channelAdminLogEventActionParticipantInvite#e31c34d8 participant:ChannelParticipant = ChannelAdminLogEventAction;
channelAdminLogEventActionParticipantToggleBan#e6d83d7e prev_participant:ChannelParticipant new_participant:ChannelParticipant = ChannelAdminLogEventAction;
channelAdminLogEventActionParticipantToggleAdmin#d5676710 prev_participant:ChannelParticipant new_participant:ChannelParticipant = ChannelAdminLogEventAction;
channelAdminLogEventActionChangeStickerSet#b1c3caa7 prev_stickerset:InputStickerSet new_stickerset:InputStickerSet = ChannelAdminLogEventAction;
channelAdminLogEventActionTogglePreHistoryHidden#5f5c95f1 new_value:Bool = ChannelAdminLogEventAction;

channelAdminLogEvent#3b5a3e40 id:long date:int user_id:int action:ChannelAdminLogEventAction = ChannelAdminLogEvent;

channels.adminLogResults#ed8af74d events:Vector<ChannelAdminLogEvent> chats:Vector<Chat> users:Vector<User> = channels.AdminLogResults;

channelAdminLogEventsFilter#ea107ae4 flags:# join:flags.0?true leave:flags.1?true invite:flags.2?true ban:flags.3?true unban:flags.4?true kick:flags.5?true unkick:flags.6?true promote:flags.7?true demote:flags.8?true info:flags.9?true settings:flags.10?true pinned:flags.11?true edit:flags.12?true delete:flags.13?true = ChannelAdminLogEventsFilter;

popularContact#5ce14175 client_id:long importers:int = PopularContact;

cdnFileHash#77eec38f offset:int limit:int hash:bytes = CdnFileHash;

messages.favedStickersNotModified#9e8fa6d3 = messages.FavedStickers;
messages.favedStickers#f37f2f16 hash:int packs:Vector<StickerPack> stickers:Vector<Document> = messages.FavedStickers;

recentMeUrlUnknown#46e1d13d url:string = RecentMeUrl;
recentMeUrlUser#8dbc3336 url:string user_id:int = RecentMeUrl;
recentMeUrlChat#a01b22f9 url:string chat_id:int = RecentMeUrl;
recentMeUrlChatInvite#eb49081d url:string chat_invite:ChatInvite = RecentMeUrl;
recentMeUrlStickerSet#bc0a57dc url:string set:StickerSetCovered = RecentMeUrl;

help.recentMeUrls#e0310d7 urls:Vector<RecentMeUrl> chats:Vector<Chat> users:Vector<User> = help.RecentMeUrls;

---functions---

invokeAfterMsg#cb9f372d {X:Type} msg_id:long query:!X = X;
invokeAfterMsgs#3dc4b4f0 {X:Type} msg_ids:Vector<long> query:!X = X;
initConnection#c7481da6 {X:Type} api_id:int device_model:string system_version:string app_version:string system_lang_code:string lang_pack:string lang_code:string query:!X = X;
invokeWithLayer#da9b0d0d {X:Type} layer:int query:!X = X;
invokeWithoutUpdates#bf9459b7 {X:Type} query:!X = X;

auth.checkPhone#6fe51dfb phone_number:string = auth.CheckedPhone;
auth.sendCode#86aef0ec flags:# allow_flashcall:flags.0?true phone_number:string current_number:flags.0?Bool api_id:int api_hash:string = auth.SentCode;
auth.signUp#1b067634 phone_number:string phone_code_hash:string phone_code:string first_name:string last_name:string = auth.Authorization;
auth.signIn#bcd51581 phone_number:string phone_code_hash:string phone_code:string = auth.Authorization;
auth.logOut#5717da40 = Bool;
auth.resetAuthorizations#9fab0d1a = Bool;
auth.sendInvites#771c1d97 phone_numbers:Vector<string> message:string = Bool;
auth.exportAuthorization#e5bfffcd dc_id:int = auth.ExportedAuthorization;
auth.importAuthorization#e3ef9613 id:int bytes:bytes = auth.Authorization;
auth.bindTempAuthKey#cdd42a05 perm_auth_key_id:long nonce:long expires_at:int encrypted_message:bytes = Bool;
auth.importBotAuthorization#67a3ff2c flags:int api_id:int api_hash:string bot_auth_token:string = auth.Authorization;
auth.checkPassword#a63011e password_hash:bytes = auth.Authorization;
auth.requestPasswordRecovery#d897bc66 = auth.PasswordRecovery;
auth.recoverPassword#4ea56e92 code:string = auth.Authorization;
auth.resendCode#3ef1a9bf phone_number:string phone_code_hash:string = auth.SentCode;
auth.cancelCode#1f040578 phone_number:string phone_code_hash:string = Bool;
auth.dropTempAuthKeys#8e48a188 except_auth_keys:Vector<long> = Bool;

account.registerDevice#637ea878 token_type:int token:string = Bool;
account.unregisterDevice#65c55b40 token_type:int token:string = Bool;
account.updateNotifySettings#84be5b93 peer:InputNotifyPeer settings:InputPeerNotifySettings = Bool;
account.getNotifySettings#12b3ad31 peer:InputNotifyPeer = PeerNotifySettings;
account.resetNotifySettings#db7e1747 = Bool;
account.updateProfile#78515775 flags:# first_name:flags.0?string last_name:flags.1?string about:flags.2?string = User;
account.updateStatus#6628562c offline:Bool = Bool;
account.getWallPapers#c04cfac2 = Vector<WallPaper>;
account.reportPeer#ae189d5f peer:InputPeer reason:ReportReason = Bool;
account.checkUsername#2714d86c username:string = Bool;
account.updateUsername#3e0bdd7c username:string = User;
account.getPrivacy#dadbc950 key:InputPrivacyKey = account.PrivacyRules;
account.setPrivacy#c9f81ce8 key:InputPrivacyKey rules:Vector<InputPrivacyRule> = account.PrivacyRules;
account.deleteAccount#418d4e0b reason:string = Bool;
account.getAccountTTL#8fc711d = AccountDaysTTL;
account.setAccountTTL#2442485e ttl:AccountDaysTTL = Bool;
account.sendChangePhoneCode#8e57deb flags:# allow_flashcall:flags.0?true phone_number:string current_number:flags.0?Bool = auth.SentCode;
account.changePhone#70c32edb phone_number:string phone_code_hash:string phone_code:string = User;
account.updateDeviceLocked#38df3532 period:int = Bool;
account.getAuthorizations#e320c158 = account.Authorizations;
account.resetAuthorization#df77f3bc hash:long = Bool;
account.getPassword#548a30f5 = account.Password;
account.getPasswordSettings#bc8d11bb current_password_hash:bytes = account.PasswordSettings;
account.updatePasswordSettings#fa7c4b86 current_password_hash:bytes new_settings:account.PasswordInputSettings = Bool;
account.sendConfirmPhoneCode#1516d7bd flags:# allow_flashcall:flags.0?true hash:string current_number:flags.0?Bool = auth.SentCode;
account.confirmPhone#5f2178c3 phone_code_hash:string phone_code:string = Bool;
account.getTmpPassword#4a82327e password_hash:bytes period:int = account.TmpPassword;

users.getUsers#d91a548 id:Vector<InputUser> = Vector<User>;
users.getFullUser#ca30a5b1 id:InputUser = UserFull;

contacts.getStatuses#c4a353ee = Vector<ContactStatus>;
contacts.getContacts#c023849f hash:int = contacts.Contacts;
contacts.importContacts#2c800be5 contacts:Vector<InputContact> = contacts.ImportedContacts;
contacts.deleteContact#8e953744 id:InputUser = contacts.Link;
contacts.deleteContacts#59ab389e id:Vector<InputUser> = Bool;
contacts.block#332b49fc id:InputUser = Bool;
contacts.unblock#e54100bd id:InputUser = Bool;
contacts.getBlocked#f57c350f offset:int limit:int = contacts.Blocked;
contacts.exportCard#84e53737 = Vector<int>;
contacts.importCard#4fe196fe export_card:Vector<int> = User;
contacts.search#11f812d8 q:string limit:int = contacts.Found;
contacts.resolveUsername#f93ccba3 username:string = contacts.ResolvedPeer;
contacts.getTopPeers#d4982db5 flags:# correspondents:flags.0?true bots_pm:flags.1?true bots_inline:flags.2?true phone_calls:flags.3?true groups:flags.10?true channels:flags.15?true offset:int limit:int hash:int = contacts.TopPeers;
contacts.resetTopPeerRating#1ae373ac category:TopPeerCategory peer:InputPeer = Bool;
contacts.resetSaved#879537f1 = Bool;

messages.getMessages#4222fa74 id:Vector<int> = messages.Messages;
messages.getDialogs#191ba9c5 flags:# exclude_pinned:flags.0?true offset_date:int offset_id:int offset_peer:InputPeer limit:int = messages.Dialogs;
messages.getHistory#dcbb8260 peer:InputPeer offset_id:int offset_date:int add_offset:int limit:int max_id:int min_id:int hash:int = messages.Messages;
messages.search#39e9ea0 flags:# peer:InputPeer q:string from_id:flags.0?InputUser filter:MessagesFilter min_date:int max_date:int offset_id:int add_offset:int limit:int max_id:int min_id:int = messages.Messages;
messages.readHistory#e306d3a peer:InputPeer max_id:int = messages.AffectedMessages;
messages.deleteHistory#1c015b09 flags:# just_clear:flags.0?true peer:InputPeer max_id:int = messages.AffectedHistory;
messages.deleteMessages#e58e95d2 flags:# revoke:flags.0?true id:Vector<int> = messages.AffectedMessages;
messages.receivedMessages#5a954c0 max_id:int = Vector<ReceivedNotifyMessage>;
messages.setTyping#a3825e50 peer:InputPeer action:SendMessageAction = Bool;
messages.sendMessage#fa88427a flags:# no_webpage:flags.1?true silent:flags.5?true background:flags.6?true clear_draft:flags.7?true peer:InputPeer reply_to_msg_id:flags.0?int message:string random_id:long reply_markup:flags.2?ReplyMarkup entities:flags.3?Vector<MessageEntity> = Updates;
messages.sendMedia#c8f16791 flags:# silent:flags.5?true background:flags.6?true clear_draft:flags.7?true peer:InputPeer reply_to_msg_id:flags.0?int media:InputMedia random_id:long reply_markup:flags.2?ReplyMarkup = Updates;
messages.forwardMessages#708e0195 flags:# silent:flags.5?true background:flags.6?true with_my_score:flags.8?true from_peer:InputPeer id:Vector<int> random_id:Vector<long> to_peer:InputPeer = Updates;
messages.reportSpam#cf1592db peer:InputPeer = Bool;
messages.hideReportSpam#a8f1709b peer:InputPeer = Bool;
messages.getPeerSettings#3672e09c peer:InputPeer = PeerSettings;
messages.getChats#3c6aa187 id:Vector<int> = messages.Chats;
messages.getFullChat#3b831c66 chat_id:int = messages.ChatFull;
messages.editChatTitle#dc452855 chat_id:int title:string = Updates;
messages.editChatPhoto#ca4c79d8 chat_id:int photo:InputChatPhoto = Updates;
messages.addChatUser#f9a0aa09 chat_id:int user_id:InputUser fwd_limit:int = Updates;
messages.deleteChatUser#e0611f16 chat_id:int user_id:InputUser = Updates;
messages.createChat#9cb126e users:Vector<InputUser> title:string = Updates;
messages.forwardMessage#33963bf9 peer:InputPeer id:int random_id:long = Updates;
messages.getDhConfig#26cf8950 version:int random_length:int = messages.DhConfig;
messages.requestEncryption#f64daf43 user_id:InputUser random_id:int g_a:bytes = EncryptedChat;
messages.acceptEncryption#3dbc0415 peer:InputEncryptedChat g_b:bytes key_fingerprint:long = EncryptedChat;
messages.discardEncryption#edd923c5 chat_id:int = Bool;
messages.setEncryptedTyping#791451ed peer:InputEncryptedChat typing:Bool = Bool;
messages.readEncryptedHistory#7f4b690a peer:InputEncryptedChat max_date:int = Bool;
messages.sendEncrypted#a9776773 peer:InputEncryptedChat random_id:long data:bytes = messages.SentEncryptedMessage;
messages.sendEncryptedFile#9a901b66 peer:InputEncryptedChat random_id:long data:bytes file:InputEncryptedFile = messages.SentEncryptedMessage;
messages.sendEncryptedService#32d439a4 peer:InputEncryptedChat random_id:long data:bytes = messages.SentEncryptedMessage;
messages.receivedQueue#55a5bb66 max_qts:int = Vector<long>;
messages.reportEncryptedSpam#4b0c8c0f peer:InputEncryptedChat = Bool;
messages.readMessageContents#36a73f77 id:Vector<int> = messages.AffectedMessages;
messages.getAllStickers#1c9618b1 hash:int = messages.AllStickers;
messages.getWebPagePreview#25223e24 message:string = MessageMedia;
messages.exportChatInvite#7d885289 chat_id:int = ExportedChatInvite;
messages.checkChatInvite#3eadb1bb hash:string = ChatInvite;
messages.importChatInvite#6c50051c hash:string = Updates;
messages.getStickerSet#2619a90e stickerset:InputStickerSet = messages.StickerSet;
messages.installStickerSet#c78fe460 stickerset:InputStickerSet archived:Bool = messages.StickerSetInstallResult;
messages.uninstallStickerSet#f96e55de stickerset:InputStickerSet = Bool;
messages.startBot#e6df7378 bot:InputUser peer:InputPeer random_id:long start_param:string = Updates;
messages.getMessagesViews#c4c8a55d peer:InputPeer id:Vector<int> increment:Bool = Vector<int>;
messages.toggleChatAdmins#ec8bd9e1 chat_id:int enabled:Bool = Updates;
messages.editChatAdmin#a9e69f2e chat_id:int user_id:InputUser is_admin:Bool = Bool;
messages.migrateChat#15a3b8e3 chat_id:int = Updates;
messages.searchGlobal#9e3cacb0 q:string offset_date:int offset_peer:InputPeer offset_id:int limit:int = messages.Messages;
messages.reorderStickerSets#78337739 flags:# masks:flags.0?true order:Vector<long> = Bool;
messages.getDocumentByHash#338e2464 sha256:bytes size:int mime_type:string = Document;
messages.searchGifs#bf9a776b q:string offset:int = messages.FoundGifs;
messages.getSavedGifs#83bf3d52 hash:int = messages.SavedGifs;
messages.saveGif#327a30cb id:InputDocument unsave:Bool = Bool;
messages.getInlineBotResults#514e999d flags:# bot:InputUser peer:InputPeer geo_point:flags.0?InputGeoPoint query:string offset:string = messages.BotResults;
messages.setInlineBotResults#eb5ea206 flags:# gallery:flags.0?true private:flags.1?true query_id:long results:Vector<InputBotInlineResult> cache_time:int next_offset:flags.2?string switch_pm:flags.3?InlineBotSwitchPM = Bool;
messages.sendInlineBotResult#b16e06fe flags:# silent:flags.5?true background:flags.6?true clear_draft:flags.7?true peer:InputPeer reply_to_msg_id:flags.0?int random_id:long query_id:long id:string = Updates;
messages.getMessageEditData#fda68d36 peer:InputPeer id:int = messages.MessageEditData;
messages.editMessage#5d1b8dd flags:# no_webpage:flags.1?true stop_geo_live:flags.12?true peer:InputPeer id:int message:flags.11?string reply_markup:flags.2?ReplyMarkup entities:flags.3?Vector<MessageEntity> geo_point:flags.13?InputGeoPoint = Updates;
messages.editInlineBotMessage#b0e08243 flags:# no_webpage:flags.1?true stop_geo_live:flags.12?true id:InputBotInlineMessageID message:flags.11?string reply_markup:flags.2?ReplyMarkup entities:flags.3?Vector<MessageEntity> geo_point:flags.13?InputGeoPoint = Bool;
messages.getBotCallbackAnswer#810a9fec flags:# game:flags.1?true peer:InputPeer msg_id:int data:flags.0?bytes = messages.BotCallbackAnswer;
messages.setBotCallbackAnswer#d58f130a flags:# alert:flags.1?true query_id:long message:flags.0?string url:flags.2?string cache_time:int = Bool;
messages.getPeerDialogs#2d9776b9 peers:Vector<InputPeer> = messages.PeerDialogs;
messages.saveDraft#bc39e14b flags:# no_webpage:flags.1?true reply_to_msg_id:flags.0?int peer:InputPeer message:string entities:flags.3?Vector<MessageEntity> = Bool;
messages.getAllDrafts#6a3f8d65 = Updates;
messages.getFeaturedStickers#2dacca4f hash:int = messages.FeaturedStickers;
messages.readFeaturedStickers#5b118126 id:Vector<long> = Bool;
messages.getRecentStickers#5ea192c9 flags:# attached:flags.0?true hash:int = messages.RecentStickers;
messages.saveRecentSticker#392718f8 flags:# attached:flags.0?true id:InputDocument unsave:Bool = Bool;
messages.clearRecentStickers#8999602d flags:# attached:flags.0?true = Bool;
messages.getArchivedStickers#57f17692 flags:# masks:flags.0?true offset_id:long limit:int = messages.ArchivedStickers;
messages.getMaskStickers#65b8c79f hash:int = messages.AllStickers;
messages.getAttachedStickers#cc5b67cc media:InputStickeredMedia = Vector<StickerSetCovered>;
messages.setGameScore#8ef8ecc0 flags:# edit_message:flags.0?true force:flags.1?true peer:InputPeer id:int user_id:InputUser score:int = Updates;
messages.setInlineGameScore#15ad9f64 flags:# edit_message:flags.0?true force:flags.1?true id:InputBotInlineMessageID user_id:InputUser score:int = Bool;
messages.getGameHighScores#e822649d peer:InputPeer id:int user_id:InputUser = messages.HighScores;
messages.getInlineGameHighScores#f635e1b id:InputBotInlineMessageID user_id:InputUser = messages.HighScores;
messages.getCommonChats#d0a48c4 user_id:InputUser max_id:int limit:int = messages.Chats;
messages.getAllChats#eba80ff0 except_ids:Vector<int> = messages.Chats;
messages.getWebPage#32ca8f91 url:string hash:int = WebPage;
messages.toggleDialogPin#3289be6a flags:# pinned:flags.0?true peer:InputPeer = Bool;
messages.reorderPinnedDialogs#959ff644 flags:# force:flags.0?true order:Vector<InputPeer> = Bool;
messages.getPinnedDialogs#e254d64e = messages.PeerDialogs;
messages.setBotShippingResults#e5f672fa flags:# query_id:long error:flags.0?string shipping_options:flags.1?Vector<ShippingOption> = Bool;
messages.setBotPrecheckoutResults#9c2dd95 flags:# success:flags.1?true query_id:long error:flags.0?string = Bool;
messages.uploadMedia#519bc2b1 peer:InputPeer media:InputMedia = MessageMedia;
messages.sendScreenshotNotification#c97df020 peer:InputPeer reply_to_msg_id:int random_id:long = Updates;
messages.getFavedStickers#21ce0b0e hash:int = messages.FavedStickers;
messages.faveSticker#b9ffc55b id:InputDocument unfave:Bool = Bool;
messages.getUnreadMentions#46578472 peer:InputPeer offset_id:int add_offset:int limit:int max_id:int min_id:int = messages.Messages;
messages.readMentions#f0189d3 peer:InputPeer = messages.AffectedHistory;
messages.getRecentLocations#249431e2 peer:InputPeer limit:int = messages.Messages;

updates.getState#edd4882a = updates.State;
updates.getDifference#25939651 flags:# pts:int pts_total_limit:flags.0?int date:int qts:int = updates.Difference;
updates.getChannelDifference#3173d78 flags:# force:flags.0?true channel:InputChannel filter:ChannelMessagesFilter pts:int limit:int = updates.ChannelDifference;

photos.updateProfilePhoto#f0bb5152 id:InputPhoto = UserProfilePhoto;
photos.uploadProfilePhoto#4f32c098 file:InputFile = photos.Photo;
photos.deletePhotos#87cf7f2f id:Vector<InputPhoto> = Vector<long>;
photos.getUserPhotos#91cd32a8 user_id:InputUser offset:int max_id:long limit:int = photos.Photos;

upload.saveFilePart#b304a621 file_id:long file_part:int bytes:bytes = Bool;
upload.getFile#e3a6cfb5 location:InputFileLocation offset:int limit:int = upload.File;
upload.saveBigFilePart#de7b673d file_id:long file_part:int file_total_parts:int bytes:bytes = Bool;
upload.getWebFile#24e6818d location:InputWebFileLocation offset:int limit:int = upload.WebFile;
upload.getCdnFile#2000bcc3 file_token:bytes offset:int limit:int = upload.CdnFile;
upload.reuploadCdnFile#1af91c09 file_token:bytes request_token:bytes = Vector<CdnFileHash>;
upload.getCdnFileHashes#f715c87b file_token:bytes offset:int = Vector<CdnFileHash>;

help.getConfig#c4f9186b = Config;
help.getNearestDc#1fb33026 = NearestDc;
help.getAppUpdate#ae2de196 = help.AppUpdate;
help.saveAppLog#6f02f748 events:Vector<InputAppEvent> = Bool;
help.getInviteText#4d392343 = help.InviteText;
help.getSupport#9cdf08cd = help.Support;
help.getAppChangelog#9010ef6f prev_app_version:string = Updates;
help.getTermsOfService#350170f3 = help.TermsOfService;
help.setBotUpdatesStatus#ec22cfcd pending_updates_count:int message:string = Bool;
help.getCdnConfig#52029342 = CdnConfig;
help.getRecentMeUrls#3dc0f114 referer:string = help.RecentMeUrls;

channels.readHistory#cc104937 channel:InputChannel max_id:int = Bool;
channels.deleteMessages#84c1fd4e channel:InputChannel id:Vector<int> = messages.AffectedMessages;
channels.deleteUserHistory#d10dd71b channel:InputChannel user_id:InputUser = messages.AffectedHistory;
channels.reportSpam#fe087810 channel:InputChannel user_id:InputUser id:Vector<int> = Bool;
channels.getMessages#93d7b347 channel:InputChannel id:Vector<int> = messages.Messages;
channels.getParticipants#123e05e9 channel:InputChannel filter:ChannelParticipantsFilter offset:int limit:int hash:int = channels.ChannelParticipants;
channels.getParticipant#546dd7a6 channel:InputChannel user_id:InputUser = channels.ChannelParticipant;
channels.getChannels#a7f6bbb id:Vector<InputChannel> = messages.Chats;
channels.getFullChannel#8736a09 channel:InputChannel = messages.ChatFull;
channels.createChannel#f4893d7f flags:# broadcast:flags.0?true megagroup:flags.1?true title:string about:string = Updates;
channels.editAbout#13e27f1e channel:InputChannel about:string = Bool;
channels.editAdmin#20b88214 channel:InputChannel user_id:InputUser admin_rights:ChannelAdminRights = Updates;
channels.editTitle#566decd0 channel:InputChannel title:string = Updates;
channels.editPhoto#f12e57c9 channel:InputChannel photo:InputChatPhoto = Updates;
channels.checkUsername#10e6bd2c channel:InputChannel username:string = Bool;
channels.updateUsername#3514b3de channel:InputChannel username:string = Bool;
channels.joinChannel#24b524c5 channel:InputChannel = Updates;
channels.leaveChannel#f836aa95 channel:InputChannel = Updates;
channels.inviteToChannel#199f3a6c channel:InputChannel users:Vector<InputUser> = Updates;
channels.exportInvite#c7560885 channel:InputChannel = ExportedChatInvite;
channels.deleteChannel#c0111fe3 channel:InputChannel = Updates;
channels.toggleInvites#49609307 channel:InputChannel enabled:Bool = Updates;
channels.exportMessageLink#c846d22d channel:InputChannel id:int = ExportedMessageLink;
channels.toggleSignatures#1f69b606 channel:InputChannel enabled:Bool = Updates;
channels.updatePinnedMessage#a72ded52 flags:# silent:flags.0?true channel:InputChannel id:int = Updates;
channels.getAdminedPublicChannels#8d8d82d7 = messages.Chats;
channels.editBanned#bfd915cd channel:InputChannel user_id:InputUser banned_rights:ChannelBannedRights = Updates;
channels.getAdminLog#33ddf480 flags:# channel:InputChannel q:string events_filter:flags.0?ChannelAdminLogEventsFilter admins:flags.1?Vector<InputUser> max_id:long min_id:long limit:int = channels.AdminLogResults;
channels.setStickers#ea8ca4f9 channel:InputChannel stickerset:InputStickerSet = Bool;
channels.readMessageContents#eab5dc38 channel:InputChannel id:Vector<int> = Bool;
channels.deleteHistory#af369d42 channel:InputChannel max_id:int = Bool;
channels.togglePreHistoryHidden#eabbb94c channel:InputChannel enabled:Bool = Updates;

bots.sendCustomRequest#aa2769ed custom_method:string params:DataJSON = DataJSON;
bots.answerWebhookJSONQuery#e6213f4d query_id:long data:DataJSON = Bool;

payments.getPaymentForm#99f09745 msg_id:int = payments.PaymentForm;
payments.getPaymentReceipt#a092a980 msg_id:int = payments.PaymentReceipt;
payments.validateRequestedInfo#770a8e74 flags:# save:flags.0?true msg_id:int info:PaymentRequestedInfo = payments.ValidatedRequestedInfo;
payments.sendPaymentForm#2b8879b3 flags:# msg_id:int requested_info_id:flags.0?string shipping_option_id:flags.1?string credentials:InputPaymentCredentials = payments.PaymentResult;
payments.getSavedInfo#227d824b = payments.SavedInfo;
payments.clearSavedInfo#d83d70c1 flags:# credentials:flags.0?true info:flags.1?true = Bool;

stickers.createStickerSet#9bd86e6a flags:# masks:flags.0?true user_id:InputUser title:string short_name:string stickers:Vector<InputStickerSetItem> = messages.StickerSet;
stickers.removeStickerFromSet#f7760f51 sticker:InputDocument = messages.StickerSet;
stickers.changeStickerPosition#ffb6d4ca sticker:InputDocument position:int = messages.StickerSet;
stickers.addStickerToSet#8653febe stickerset:InputStickerSet sticker:InputStickerSetItem = messages.StickerSet;

phone.getCallConfig#55451fa9 = DataJSON;
phone.requestCall#5b95b3d4 user_id:InputUser random_id:int g_a_hash:bytes protocol:PhoneCallProtocol = phone.PhoneCall;
phone.acceptCall#3bd2b4a0 peer:InputPhoneCall g_b:bytes protocol:PhoneCallProtocol = phone.PhoneCall;
phone.confirmCall#2efe1722 peer:InputPhoneCall g_a:bytes key_fingerprint:long protocol:PhoneCallProtocol = phone.PhoneCall;
phone.receivedCall#17d54f61 peer:InputPhoneCall = Bool;
phone.discardCall#78d413a6 peer:InputPhoneCall duration:int reason:PhoneCallDiscardReason connection_id:long = Updates;
phone.setCallRating#1c536a34 peer:InputPhoneCall rating:int comment:string = Updates;
phone.saveCallDebug#277add7e peer:InputPhoneCall debug:DataJSON = Bool;

langpack.getLangPack#9ab5c58e lang_code:string = LangPackDifference;
langpack.getStrings#2e1ee318 lang_code:string keys:Vector<string> = Vector<LangPackString>;
langpack.getDifference#b2e4d7d from_version:int = LangPackDifference;
langpack.getLanguages#800fd57d = Vector<LangPackLanguage>;

// LAYER 72
