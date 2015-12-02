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

---functions---

req_pq#60469778 nonce:int128 = ResPQ;

req_DH_params#d712e4be nonce:int128 server_nonce:int128 p:string q:string public_key_fingerprint:long encrypted_data:string = Server_DH_Params;

set_client_DH_params#f5045f1f nonce:int128 server_nonce:int128 encrypted_data:string = Set_client_DH_params_answer;

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
future_salts#ae500895 req_msg_id:long now:int salts:vector<futureSalt> = FutureSalts;

pong#347773c5 msg_id:long ping_id:long = Pong;

destroy_session_ok#e22045fc session_id:long = DestroySessionRes;
destroy_session_none#62d350c9 session_id:long = DestroySessionRes;

new_session_created#9ec20908 first_msg_id:long unique_id:long server_salt:long = NewSession;

//message msg_id:long seqno:int bytes:int body:Object = Message; // parsed manually
//msg_container#73f1f8dc messages:vector<message> = MessageContainer; // parsed manually
//msg_copy#e06046b2 orig_message:Message = MessageCopy; // parsed manually, not used - use msg_container
//gzip_packed#3072cfa1 packed_data:string = Object; // parsed manually

http_wait#9299359f max_delay:int wait_after:int max_wait:int = HttpWait;

---functions---

rpc_drop_answer#58e4a740 req_msg_id:long = RpcDropAnswer;

get_future_salts#b921bd04 num:int = FutureSalts;

ping#7abe77ec ping_id:long = Pong;
ping_delay_disconnect#f3427b8c ping_id:long disconnect_delay:int = Pong;

destroy_session#e7512126 session_id:long = DestroySessionRes;

register.saveDeveloperInfo#9a5f6e95 name:string email:string phone_number:string age:int city:string = Bool;

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
inputMediaUploadedPhoto#f7aff1c0 file:InputFile caption:string = InputMedia;
inputMediaPhoto#e9bfb4f3 id:InputPhoto caption:string = InputMedia;
inputMediaGeoPoint#f9c44144 geo_point:InputGeoPoint = InputMedia;
inputMediaContact#a6e45987 phone_number:string first_name:string last_name:string = InputMedia;
inputMediaUploadedVideo#82713fdf file:InputFile duration:int w:int h:int mime_type:string caption:string = InputMedia;
inputMediaUploadedThumbVideo#7780ddf9 file:InputFile thumb:InputFile duration:int w:int h:int mime_type:string caption:string = InputMedia;
inputMediaVideo#936a4ebd id:InputVideo caption:string = InputMedia;
inputMediaUploadedAudio#4e498cab file:InputFile duration:int mime_type:string = InputMedia;
inputMediaAudio#89938781 id:InputAudio = InputMedia;
inputMediaUploadedDocument#ffe76b78 file:InputFile mime_type:string attributes:Vector<DocumentAttribute> = InputMedia;
inputMediaUploadedThumbDocument#41481486 file:InputFile thumb:InputFile mime_type:string attributes:Vector<DocumentAttribute> = InputMedia;
inputMediaDocument#d184e841 id:InputDocument = InputMedia;
inputMediaVenue#2827a81a geo_point:InputGeoPoint title:string address:string provider:string venue_id:string = InputMedia;

inputChatPhotoEmpty#1ca48f57 = InputChatPhoto;
inputChatUploadedPhoto#94254732 file:InputFile crop:InputPhotoCrop = InputChatPhoto;
inputChatPhoto#b2e1bf08 id:InputPhoto crop:InputPhotoCrop = InputChatPhoto;

inputGeoPointEmpty#e4c123d6 = InputGeoPoint;
inputGeoPoint#f3b7acc9 lat:double long:double = InputGeoPoint;

inputPhotoEmpty#1cd7bf0d = InputPhoto;
inputPhoto#fb95c6c4 id:long access_hash:long = InputPhoto;

inputVideoEmpty#5508ec75 = InputVideo;
inputVideo#ee579652 id:long access_hash:long = InputVideo;

inputFileLocation#14637196 volume_id:long local_id:int secret:long = InputFileLocation;
inputVideoFileLocation#3d0364ec id:long access_hash:long = InputFileLocation;
inputEncryptedFileLocation#f5235d55 id:long access_hash:long = InputFileLocation;
inputAudioFileLocation#74dc404d id:long access_hash:long = InputFileLocation;
inputDocumentFileLocation#4e45abe9 id:long access_hash:long = InputFileLocation;

inputPhotoCropAuto#ade6b004 = InputPhotoCrop;
inputPhotoCrop#d9915325 crop_left:double crop_top:double crop_width:double = InputPhotoCrop;

inputAppEvent#770656a8 time:double type:string peer:long data:string = InputAppEvent;

peerUser#9db1bc6d user_id:int = Peer;
peerChat#bad0e5bb chat_id:int = Peer;
peerChannel#bddde532 channel_id:int = Peer;

storage.fileUnknown#aa963b05 = storage.FileType;
storage.fileJpeg#7efe0e = storage.FileType;
storage.fileGif#cae1aadf = storage.FileType;
storage.filePng#a4f63c0 = storage.FileType;
storage.filePdf#ae1e508d = storage.FileType;
storage.fileMp3#528a0677 = storage.FileType;
storage.fileMov#4b09ebbc = storage.FileType;
storage.filePartial#40bc6f52 = storage.FileType;
storage.fileMp4#b3cea0e4 = storage.FileType;
storage.fileWebp#1081464c = storage.FileType;

fileLocationUnavailable#7c596b46 volume_id:long local_id:int secret:long = FileLocation;
fileLocation#53d69076 dc_id:int volume_id:long local_id:int secret:long = FileLocation;

userEmpty#200250ba id:int = User;
user#22e49072 flags:# self:flags.10?true contact:flags.11?true mutual_contact:flags.12?true deleted:flags.13?true bot:flags.14?true bot_chat_history:flags.15?true bot_nochats:flags.16?true verified:flags.17?true id:int access_hash:flags.0?long first_name:flags.1?string last_name:flags.2?string username:flags.3?string phone:flags.4?string photo:flags.5?UserProfilePhoto status:flags.6?UserStatus bot_info_version:flags.14?int = User;

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
channel#678e9587 flags:# creator:flags.0?true kicked:flags.1?true left:flags.2?true editor:flags.3?true moderator:flags.4?true broadcast:flags.5?true verified:flags.7?true megagroup:flags.8?true id:int access_hash:long title:string username:flags.6?string photo:ChatPhoto date:int version:int = Chat;
channelForbidden#2d85832c id:int access_hash:long title:string = Chat;

chatFull#2e02a614 id:int participants:ChatParticipants chat_photo:Photo notify_settings:PeerNotifySettings exported_invite:ExportedChatInvite bot_info:Vector<BotInfo> = ChatFull;
channelFull#9e341ddf flags:# can_view_participants:flags.3?true id:int about:string participants_count:flags.0?int admins_count:flags.1?int kicked_count:flags.2?int read_inbox_max_id:int unread_count:int unread_important_count:int chat_photo:Photo notify_settings:PeerNotifySettings exported_invite:ExportedChatInvite bot_info:Vector<BotInfo> migrated_from_chat_id:flags.4?int migrated_from_max_id:flags.4?int = ChatFull;

chatParticipant#c8d7493e user_id:int inviter_id:int date:int = ChatParticipant;
chatParticipantCreator#da13538a user_id:int = ChatParticipant;
chatParticipantAdmin#e2d6e436 user_id:int inviter_id:int date:int = ChatParticipant;

chatParticipantsForbidden#fc900c2b flags:# chat_id:int self_participant:flags.0?ChatParticipant = ChatParticipants;
chatParticipants#3f460fed chat_id:int participants:Vector<ChatParticipant> version:int = ChatParticipants;

chatPhotoEmpty#37c1011c = ChatPhoto;
chatPhoto#6153276a photo_small:FileLocation photo_big:FileLocation = ChatPhoto;

messageEmpty#83e5de54 id:int = Message;
message#5ba66c13 flags:# unread:flags.0?true out:flags.1?true mentioned:flags.4?true media_unread:flags.5?true id:int from_id:flags.8?int to_id:Peer fwd_from_id:flags.2?Peer fwd_date:flags.2?int reply_to_msg_id:flags.3?int date:int message:string media:flags.9?MessageMedia reply_markup:flags.6?ReplyMarkup entities:flags.7?Vector<MessageEntity> views:flags.10?int = Message;
messageService#c06b9607 flags:# unread:flags.0?true out:flags.1?true mentioned:flags.4?true media_unread:flags.5?true id:int from_id:flags.8?int to_id:Peer date:int action:MessageAction = Message;

messageMediaEmpty#3ded6320 = MessageMedia;
messageMediaPhoto#3d8ce53d photo:Photo caption:string = MessageMedia;
messageMediaVideo#5bcf1675 video:Video caption:string = MessageMedia;
messageMediaGeo#56e0d474 geo:GeoPoint = MessageMedia;
messageMediaContact#5e7d2f39 phone_number:string first_name:string last_name:string user_id:int = MessageMedia;
messageMediaUnsupported#9f84f49e = MessageMedia;
messageMediaDocument#2fda2204 document:Document = MessageMedia;
messageMediaAudio#c6b68300 audio:Audio = MessageMedia;
messageMediaWebPage#a32dd600 webpage:WebPage = MessageMedia;
messageMediaVenue#7912b71f geo:GeoPoint title:string address:string provider:string venue_id:string = MessageMedia;

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

dialog#c1dd804a peer:Peer top_message:int read_inbox_max_id:int unread_count:int notify_settings:PeerNotifySettings = Dialog;
dialogChannel#5b8496b2 peer:Peer top_message:int top_important_message:int read_inbox_max_id:int unread_count:int unread_important_count:int notify_settings:PeerNotifySettings pts:int = Dialog;

photoEmpty#2331b22d id:long = Photo;
photo#cded42fe id:long access_hash:long date:int sizes:Vector<PhotoSize> = Photo;

photoSizeEmpty#e17e23c type:string = PhotoSize;
photoSize#77bfb61b type:string location:FileLocation w:int h:int size:int = PhotoSize;
photoCachedSize#e9a734fa type:string location:FileLocation w:int h:int bytes:bytes = PhotoSize;

videoEmpty#c10658a8 id:long = Video;
video#f72887d3 id:long access_hash:long date:int duration:int mime_type:string size:int thumb:PhotoSize dc_id:int w:int h:int = Video;

geoPointEmpty#1117dd5f = GeoPoint;
geoPoint#2049d70c long:double lat:double = GeoPoint;

auth.checkedPhone#811ea28e phone_registered:Bool = auth.CheckedPhone;

auth.sentCode#efed51d9 phone_registered:Bool phone_code_hash:string send_call_timeout:int is_password:Bool = auth.SentCode;
auth.sentAppCode#e325edcf phone_registered:Bool phone_code_hash:string send_call_timeout:int is_password:Bool = auth.SentCode;

auth.authorization#ff036af1 user:User = auth.Authorization;

auth.exportedAuthorization#df969c2d id:int bytes:bytes = auth.ExportedAuthorization;

inputNotifyPeer#b8bc5b0c peer:InputPeer = InputNotifyPeer;
inputNotifyUsers#193b4417 = InputNotifyPeer;
inputNotifyChats#4a95e84e = InputNotifyPeer;
inputNotifyAll#a429b886 = InputNotifyPeer;

inputPeerNotifyEventsEmpty#f03064d8 = InputPeerNotifyEvents;
inputPeerNotifyEventsAll#e86a2c74 = InputPeerNotifyEvents;

inputPeerNotifySettings#46a2ce98 mute_until:int sound:string show_previews:Bool events_mask:int = InputPeerNotifySettings;

peerNotifyEventsEmpty#add53cb3 = PeerNotifyEvents;
peerNotifyEventsAll#6d1ded88 = PeerNotifyEvents;

peerNotifySettingsEmpty#70a68512 = PeerNotifySettings;
peerNotifySettings#8d5e11ee mute_until:int sound:string show_previews:Bool events_mask:int = PeerNotifySettings;

wallPaper#ccb03657 id:int title:string sizes:Vector<PhotoSize> color:int = WallPaper;
wallPaperSolid#63117f24 id:int title:string bg_color:int color:int = WallPaper;

inputReportReasonSpam#58dbcab8 = ReportReason;
inputReportReasonViolence#1e22c78d = ReportReason;
inputReportReasonPornography#2e59d922 = ReportReason;
inputReportReasonOther#e1746d0a text:string = ReportReason;

userFull#5a89ac5b user:User link:contacts.Link profile_photo:Photo notify_settings:PeerNotifySettings blocked:Bool bot_info:BotInfo = UserFull;

contact#f911c994 user_id:int mutual:Bool = Contact;

importedContact#d0028438 user_id:int client_id:long = ImportedContact;

contactBlocked#561bc879 user_id:int date:int = ContactBlocked;

contactSuggested#3de191a1 user_id:int mutual_contacts:int = ContactSuggested;

contactStatus#d3680c61 user_id:int status:UserStatus = ContactStatus;

contacts.link#3ace484c my_link:ContactLink foreign_link:ContactLink user:User = contacts.Link;

contacts.contactsNotModified#b74ba9d2 = contacts.Contacts;
contacts.contacts#6f8b8cb2 contacts:Vector<Contact> users:Vector<User> = contacts.Contacts;

contacts.importedContacts#ad524315 imported:Vector<ImportedContact> retry_contacts:Vector<long> users:Vector<User> = contacts.ImportedContacts;

contacts.blocked#1c138d15 blocked:Vector<ContactBlocked> users:Vector<User> = contacts.Blocked;
contacts.blockedSlice#900802a1 count:int blocked:Vector<ContactBlocked> users:Vector<User> = contacts.Blocked;

contacts.suggested#5649dcc5 results:Vector<ContactSuggested> users:Vector<User> = contacts.Suggested;

messages.dialogs#15ba6c40 dialogs:Vector<Dialog> messages:Vector<Message> chats:Vector<Chat> users:Vector<User> = messages.Dialogs;
messages.dialogsSlice#71e094f3 count:int dialogs:Vector<Dialog> messages:Vector<Message> chats:Vector<Chat> users:Vector<User> = messages.Dialogs;

messages.messages#8c718e87 messages:Vector<Message> chats:Vector<Chat> users:Vector<User> = messages.Messages;
messages.messagesSlice#b446ae3 count:int messages:Vector<Message> chats:Vector<Chat> users:Vector<User> = messages.Messages;
messages.channelMessages#bc0f17bc flags:# pts:int count:int messages:Vector<Message> collapsed:flags.0?Vector<MessageGroup> chats:Vector<Chat> users:Vector<User> = messages.Messages;

messages.chats#64ff9fd5 chats:Vector<Chat> = messages.Chats;

messages.chatFull#e5d7d19c full_chat:ChatFull chats:Vector<Chat> users:Vector<User> = messages.ChatFull;

messages.affectedHistory#b45c69d1 pts:int pts_count:int offset:int = messages.AffectedHistory;

inputMessagesFilterEmpty#57e2f66c = MessagesFilter;
inputMessagesFilterPhotos#9609a51c = MessagesFilter;
inputMessagesFilterVideo#9fc00e65 = MessagesFilter;
inputMessagesFilterPhotoVideo#56e9f0e4 = MessagesFilter;
inputMessagesFilterPhotoVideoDocuments#d95e73bb = MessagesFilter;
inputMessagesFilterDocument#9eddf188 = MessagesFilter;
inputMessagesFilterAudio#cfc87522 = MessagesFilter;
inputMessagesFilterAudioDocuments#5afbf764 = MessagesFilter;
inputMessagesFilterUrl#7ef0dd87 = MessagesFilter;

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
updateNewAuthorization#8f06529a auth_key_id:long date:int device:string location:string = Update;
updateNewEncryptedMessage#12bcbd9a message:EncryptedMessage qts:int = Update;
updateEncryptedChatTyping#1710f156 chat_id:int = Update;
updateEncryption#b4a2e88d chat:EncryptedChat date:int = Update;
updateEncryptedMessagesRead#38fe25b7 chat_id:int max_date:int date:int = Update;
updateChatParticipantAdd#ea4b0e5c chat_id:int user_id:int inviter_id:int date:int version:int = Update;
updateChatParticipantDelete#6e5f8c22 chat_id:int user_id:int version:int = Update;
updateDcOptions#8e5e9873 dc_options:Vector<DcOption> = Update;
updateUserBlocked#80ece81a user_id:int blocked:Bool = Update;
updateNotifySettings#bec268ef peer:NotifyPeer notify_settings:PeerNotifySettings = Update;
updateServiceNotification#382dd3e4 type:string message:string media:MessageMedia popup:Bool = Update;
updatePrivacy#ee3b272a key:PrivacyKey rules:Vector<PrivacyRule> = Update;
updateUserPhone#12b9417b user_id:int phone:string = Update;
updateReadHistoryInbox#9961fd5c peer:Peer max_id:int pts:int pts_count:int = Update;
updateReadHistoryOutbox#2f2f21bf peer:Peer max_id:int pts:int pts_count:int = Update;
updateWebPage#7f891213 webpage:WebPage pts:int pts_count:int = Update;
updateReadMessagesContents#68c13933 messages:Vector<int> pts:int pts_count:int = Update;
updateChannelTooLong#60946422 channel_id:int = Update;
updateChannel#b6d45656 channel_id:int = Update;
updateChannelGroup#c36c1e3c channel_id:int group:MessageGroup = Update;
updateNewChannelMessage#62ba04d9 message:Message pts:int pts_count:int = Update;
updateReadChannelInbox#4214f37f channel_id:int max_id:int = Update;
updateDeleteChannelMessages#c37521c9 channel_id:int messages:Vector<int> pts:int pts_count:int = Update;
updateChannelMessageViews#98a12b4b channel_id:int id:int views:int = Update;
updateChatAdmins#6e947941 chat_id:int enabled:Bool version:int = Update;
updateChatParticipantAdmin#b6901959 chat_id:int user_id:int is_admin:Bool version:int = Update;
updateNewStickerSet#688a30aa stickerset:messages.StickerSet = Update;
updateStickerSetsOrder#f0dfb451 order:Vector<long> = Update;
updateStickerSets#43ae3dec = Update;

updates.state#a56c2a3e pts:int qts:int date:int seq:int unread_count:int = updates.State;

updates.differenceEmpty#5d75a138 date:int seq:int = updates.Difference;
updates.difference#f49ca0 new_messages:Vector<Message> new_encrypted_messages:Vector<EncryptedMessage> other_updates:Vector<Update> chats:Vector<Chat> users:Vector<User> state:updates.State = updates.Difference;
updates.differenceSlice#a8fb1981 new_messages:Vector<Message> new_encrypted_messages:Vector<EncryptedMessage> other_updates:Vector<Update> chats:Vector<Chat> users:Vector<User> intermediate_state:updates.State = updates.Difference;

updatesTooLong#e317af7e = Updates;
updateShortMessage#f7d91a46 flags:# unread:flags.0?true out:flags.1?true mentioned:flags.4?true media_unread:flags.5?true id:int user_id:int message:string pts:int pts_count:int date:int fwd_from_id:flags.2?Peer fwd_date:flags.2?int reply_to_msg_id:flags.3?int entities:flags.7?Vector<MessageEntity> = Updates;
updateShortChatMessage#cac7fdd2 flags:# unread:flags.0?true out:flags.1?true mentioned:flags.4?true media_unread:flags.5?true id:int from_id:int chat_id:int message:string pts:int pts_count:int date:int fwd_from_id:flags.2?Peer fwd_date:flags.2?int reply_to_msg_id:flags.3?int entities:flags.7?Vector<MessageEntity> = Updates;
updateShort#78d4dec1 update:Update date:int = Updates;
updatesCombined#725b04c3 updates:Vector<Update> users:Vector<User> chats:Vector<Chat> date:int seq_start:int seq:int = Updates;
updates#74ae4240 updates:Vector<Update> users:Vector<User> chats:Vector<Chat> date:int seq:int = Updates;
updateShortSentMessage#11f1331c flags:# unread:flags.0?true out:flags.1?true id:int pts:int pts_count:int date:int media:flags.9?MessageMedia entities:flags.7?Vector<MessageEntity> = Updates;

photos.photos#8dca6aa5 photos:Vector<Photo> users:Vector<User> = photos.Photos;
photos.photosSlice#15051f54 count:int photos:Vector<Photo> users:Vector<User> = photos.Photos;

photos.photo#20212ca8 photo:Photo users:Vector<User> = photos.Photo;

upload.file#96a18d5 type:storage.FileType mtime:int bytes:bytes = upload.File;

dcOption#5d8c6cc flags:# ipv6:flags.0?true media_only:flags.1?true id:int ip_address:string port:int = DcOption;

config#6cb6e65e date:int expires:int test_mode:Bool this_dc:int dc_options:Vector<DcOption> chat_size_max:int megagroup_size_max:int forwarded_count_max:int online_update_period_ms:int offline_blur_timeout_ms:int offline_idle_timeout_ms:int online_cloud_timeout_ms:int notify_cloud_delay_ms:int notify_default_delay_ms:int chat_big_size:int push_chat_period_ms:int push_chat_limit:int disabled_features:Vector<DisabledFeature> = Config;

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

inputAudioEmpty#d95adc84 = InputAudio;
inputAudio#77d440ff id:long access_hash:long = InputAudio;

inputDocumentEmpty#72f0eaae = InputDocument;
inputDocument#18798952 id:long access_hash:long = InputDocument;

audioEmpty#586988d8 id:long = Audio;
audio#f9e35055 id:long access_hash:long date:int duration:int mime_type:string size:int dc_id:int = Audio;

documentEmpty#36f8c871 id:long = Document;
document#f9a39f4f id:long access_hash:long date:int mime_type:string size:int thumb:PhotoSize dc_id:int attributes:Vector<DocumentAttribute> = Document;

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

contacts.found#1aa1f784 results:Vector<Peer> chats:Vector<Chat> users:Vector<User> = contacts.Found;

inputPrivacyKeyStatusTimestamp#4f96cb18 = InputPrivacyKey;

privacyKeyStatusTimestamp#bc2eab30 = PrivacyKey;

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

account.sentChangePhoneCode#a4f58c4c phone_code_hash:string send_call_timeout:int = account.SentChangePhoneCode;

documentAttributeImageSize#6c37c15c w:int h:int = DocumentAttribute;
documentAttributeAnimated#11b58939 = DocumentAttribute;
documentAttributeSticker#3a556302 alt:string stickerset:InputStickerSet = DocumentAttribute;
documentAttributeVideo#5910cccb duration:int w:int h:int = DocumentAttribute;
documentAttributeAudio#ded218e0 duration:int title:string performer:string = DocumentAttribute;
documentAttributeFilename#15590068 file_name:string = DocumentAttribute;

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
webPage#ca820ed7 flags:# id:long url:string display_url:string type:flags.0?string site_name:flags.1?string title:flags.2?string description:flags.3?string photo:flags.4?Photo embed_url:flags.5?string embed_type:flags.5?string embed_width:flags.6?int embed_height:flags.6?int duration:flags.7?int author:flags.8?string document:flags.9?Document = WebPage;

authorization#7bf2e6f6 hash:long flags:int device_model:string platform:string system_version:string api_id:int app_name:string app_version:string date_created:int date_active:int ip:string country:string region:string = Authorization;

account.authorizations#1250abde authorizations:Vector<Authorization> = account.Authorizations;

account.noPassword#96dabc18 new_salt:bytes email_unconfirmed_pattern:string = account.Password;
account.password#7c18141c current_salt:bytes new_salt:bytes hint:string has_recovery:Bool email_unconfirmed_pattern:string = account.Password;

account.passwordSettings#b7b72ab3 email:string = account.PasswordSettings;

account.passwordInputSettings#bcfc532c flags:# new_salt:flags.0?bytes new_password_hash:flags.0?bytes hint:flags.0?string email:flags.1?string = account.PasswordInputSettings;

auth.passwordRecovery#137948a5 email_pattern:string = auth.PasswordRecovery;

receivedNotifyMessage#a384b779 id:int flags:int = ReceivedNotifyMessage;

chatInviteEmpty#69df3769 = ExportedChatInvite;
chatInviteExported#fc2e05bc link:string = ExportedChatInvite;

chatInviteAlready#5a686d7c chat:Chat = ChatInvite;
chatInvite#93e99b60 flags:# channel:flags.0?true broadcast:flags.1?true public:flags.2?true megagroup:flags.3?true title:string = ChatInvite;

inputStickerSetEmpty#ffb62b95 = InputStickerSet;
inputStickerSetID#9de7a269 id:long access_hash:long = InputStickerSet;
inputStickerSetShortName#861cc8a0 short_name:string = InputStickerSet;

stickerSet#cd303b41 flags:# installed:flags.0?true disabled:flags.1?true official:flags.2?true id:long access_hash:long title:string short_name:string count:int hash:int = StickerSet;

messages.stickerSet#b60a24a6 set:StickerSet packs:Vector<StickerPack> documents:Vector<Document> = messages.StickerSet;

botCommand#c27ac8c7 command:string description:string = BotCommand;

botInfoEmpty#bb2e37ce = BotInfo;
botInfo#9cf585d user_id:int version:int share_text:string description:string commands:Vector<BotCommand> = BotInfo;

keyboardButton#a2fa4880 text:string = KeyboardButton;

keyboardButtonRow#77608b83 buttons:Vector<KeyboardButton> = KeyboardButtonRow;

replyKeyboardHide#a03e5b85 flags:# selective:flags.2?true = ReplyMarkup;
replyKeyboardForceReply#f4108aa0 flags:# single_use:flags.1?true selective:flags.2?true = ReplyMarkup;
replyKeyboardMarkup#3502758c flags:# resize:flags.0?true single_use:flags.1?true selective:flags.2?true rows:Vector<KeyboardButtonRow> = ReplyMarkup;

help.appChangelogEmpty#af7e0394 = help.AppChangelog;
help.appChangelog#4668e6bd text:string = help.AppChangelog;

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

inputChannelEmpty#ee8c1e86 = InputChannel;
inputChannel#afeb712e channel_id:int access_hash:long = InputChannel;

contacts.resolvedPeer#7f077ad9 peer:Peer chats:Vector<Chat> users:Vector<User> = contacts.ResolvedPeer;

messageRange#ae30253 min_id:int max_id:int = MessageRange;

messageGroup#e8346f53 min_id:int max_id:int count:int date:int = MessageGroup;

updates.channelDifferenceEmpty#3e11affb flags:# final:flags.0?true pts:int timeout:flags.1?int = updates.ChannelDifference;
updates.channelDifferenceTooLong#5e167646 flags:# final:flags.0?true pts:int timeout:flags.1?int top_message:int top_important_message:int read_inbox_max_id:int unread_count:int unread_important_count:int messages:Vector<Message> chats:Vector<Chat> users:Vector<User> = updates.ChannelDifference;
updates.channelDifference#2064674e flags:# final:flags.0?true pts:int timeout:flags.1?int new_messages:Vector<Message> other_updates:Vector<Update> chats:Vector<Chat> users:Vector<User> = updates.ChannelDifference;

channelMessagesFilterEmpty#94d42ee7 = ChannelMessagesFilter;
channelMessagesFilter#cd77d957 flags:# important_only:flags.0?true exclude_new_messages:flags.1?true ranges:Vector<MessageRange> = ChannelMessagesFilter;
channelMessagesFilterCollapsed#fa01232e = ChannelMessagesFilter;

channelParticipant#15ebac1d user_id:int date:int = ChannelParticipant;
channelParticipantSelf#a3289a6d user_id:int inviter_id:int date:int = ChannelParticipant;
channelParticipantModerator#91057fef user_id:int inviter_id:int date:int = ChannelParticipant;
channelParticipantEditor#98192d61 user_id:int inviter_id:int date:int = ChannelParticipant;
channelParticipantKicked#8cc5e69a user_id:int kicked_by:int date:int = ChannelParticipant;
channelParticipantCreator#e3e2e1f9 user_id:int = ChannelParticipant;

channelParticipantsRecent#de3f3c79 = ChannelParticipantsFilter;
channelParticipantsAdmins#b4608969 = ChannelParticipantsFilter;
channelParticipantsKicked#3c37bb7a = ChannelParticipantsFilter;
channelParticipantsBots#b0d1865b = ChannelParticipantsFilter;

channelRoleEmpty#b285a0c6 = ChannelParticipantRole;
channelRoleModerator#9618d975 = ChannelParticipantRole;
channelRoleEditor#820bfe8c = ChannelParticipantRole;

channels.channelParticipants#f56ee2a8 count:int participants:Vector<ChannelParticipant> users:Vector<User> = channels.ChannelParticipants;

channels.channelParticipant#d0d9b163 participant:ChannelParticipant users:Vector<User> = channels.ChannelParticipant;

help.termsOfService#f1ee3e90 text:string = help.TermsOfService;

---functions---

invokeAfterMsg#cb9f372d {X:Type} msg_id:long query:!X = X;
invokeAfterMsgs#3dc4b4f0 {X:Type} msg_ids:Vector<long> query:!X = X;
initConnection#69796de9 {X:Type} api_id:int device_model:string system_version:string app_version:string lang_code:string query:!X = X;
invokeWithLayer#da9b0d0d {X:Type} layer:int query:!X = X;
invokeWithoutUpdates#bf9459b7 {X:Type} query:!X = X;

auth.checkPhone#6fe51dfb phone_number:string = auth.CheckedPhone;
auth.sendCode#768d5f4d phone_number:string sms_type:int api_id:int api_hash:string lang_code:string = auth.SentCode;
auth.sendCall#3c51564 phone_number:string phone_code_hash:string = Bool;
auth.signUp#1b067634 phone_number:string phone_code_hash:string phone_code:string first_name:string last_name:string = auth.Authorization;
auth.signIn#bcd51581 phone_number:string phone_code_hash:string phone_code:string = auth.Authorization;
auth.logOut#5717da40 = Bool;
auth.resetAuthorizations#9fab0d1a = Bool;
auth.sendInvites#771c1d97 phone_numbers:Vector<string> message:string = Bool;
auth.exportAuthorization#e5bfffcd dc_id:int = auth.ExportedAuthorization;
auth.importAuthorization#e3ef9613 id:int bytes:bytes = auth.Authorization;
auth.bindTempAuthKey#cdd42a05 perm_auth_key_id:long nonce:long expires_at:int encrypted_message:bytes = Bool;
auth.sendSms#da9f3e8 phone_number:string phone_code_hash:string = Bool;
auth.importBotAuthorization#67a3ff2c flags:int api_id:int api_hash:string bot_auth_token:string = auth.Authorization;
auth.checkPassword#a63011e password_hash:bytes = auth.Authorization;
auth.requestPasswordRecovery#d897bc66 = auth.PasswordRecovery;
auth.recoverPassword#4ea56e92 code:string = auth.Authorization;

account.registerDevice#446c712c token_type:int token:string device_model:string system_version:string app_version:string app_sandbox:Bool lang_code:string = Bool;
account.unregisterDevice#65c55b40 token_type:int token:string = Bool;
account.updateNotifySettings#84be5b93 peer:InputNotifyPeer settings:InputPeerNotifySettings = Bool;
account.getNotifySettings#12b3ad31 peer:InputNotifyPeer = PeerNotifySettings;
account.resetNotifySettings#db7e1747 = Bool;
account.updateProfile#f0888d68 first_name:string last_name:string = User;
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
account.sendChangePhoneCode#a407a8f4 phone_number:string = account.SentChangePhoneCode;
account.changePhone#70c32edb phone_number:string phone_code_hash:string phone_code:string = User;
account.updateDeviceLocked#38df3532 period:int = Bool;
account.getAuthorizations#e320c158 = account.Authorizations;
account.resetAuthorization#df77f3bc hash:long = Bool;
account.getPassword#548a30f5 = account.Password;
account.getPasswordSettings#bc8d11bb current_password_hash:bytes = account.PasswordSettings;
account.updatePasswordSettings#fa7c4b86 current_password_hash:bytes new_settings:account.PasswordInputSettings = Bool;

users.getUsers#d91a548 id:Vector<InputUser> = Vector<User>;
users.getFullUser#ca30a5b1 id:InputUser = UserFull;

contacts.getStatuses#c4a353ee = Vector<ContactStatus>;
contacts.getContacts#22c6aa08 hash:string = contacts.Contacts;
contacts.importContacts#da30b32d contacts:Vector<InputContact> replace:Bool = contacts.ImportedContacts;
contacts.getSuggested#cd773428 limit:int = contacts.Suggested;
contacts.deleteContact#8e953744 id:InputUser = contacts.Link;
contacts.deleteContacts#59ab389e id:Vector<InputUser> = Bool;
contacts.block#332b49fc id:InputUser = Bool;
contacts.unblock#e54100bd id:InputUser = Bool;
contacts.getBlocked#f57c350f offset:int limit:int = contacts.Blocked;
contacts.exportCard#84e53737 = Vector<int>;
contacts.importCard#4fe196fe export_card:Vector<int> = User;
contacts.search#11f812d8 q:string limit:int = contacts.Found;
contacts.resolveUsername#f93ccba3 username:string = contacts.ResolvedPeer;

messages.getMessages#4222fa74 id:Vector<int> = messages.Messages;
messages.getDialogs#6b47f94d offset_date:int offset_id:int offset_peer:InputPeer limit:int = messages.Dialogs;
messages.getHistory#8a8ec2da peer:InputPeer offset_id:int add_offset:int limit:int max_id:int min_id:int = messages.Messages;
messages.search#d4569248 flags:# important_only:flags.0?true peer:InputPeer q:string filter:MessagesFilter min_date:int max_date:int offset:int max_id:int limit:int = messages.Messages;
messages.readHistory#e306d3a peer:InputPeer max_id:int = messages.AffectedMessages;
messages.deleteHistory#b7c13bd9 peer:InputPeer max_id:int = messages.AffectedHistory;
messages.deleteMessages#a5f18925 id:Vector<int> = messages.AffectedMessages;
messages.receivedMessages#5a954c0 max_id:int = Vector<ReceivedNotifyMessage>;
messages.setTyping#a3825e50 peer:InputPeer action:SendMessageAction = Bool;
messages.sendMessage#fa88427a flags:# no_webpage:flags.1?true broadcast:flags.4?true peer:InputPeer reply_to_msg_id:flags.0?int message:string random_id:long reply_markup:flags.2?ReplyMarkup entities:flags.3?Vector<MessageEntity> = Updates;
messages.sendMedia#c8f16791 flags:# broadcast:flags.4?true peer:InputPeer reply_to_msg_id:flags.0?int media:InputMedia random_id:long reply_markup:flags.2?ReplyMarkup = Updates;
messages.forwardMessages#708e0195 flags:# broadcast:flags.4?true from_peer:InputPeer id:Vector<int> random_id:Vector<long> to_peer:InputPeer = Updates;
messages.reportSpam#cf1592db peer:InputPeer = Bool;
messages.getChats#3c6aa187 id:Vector<int> = messages.Chats;
messages.getFullChat#3b831c66 chat_id:int = messages.ChatFull;
messages.editChatTitle#dc452855 chat_id:int title:string = Updates;
messages.editChatPhoto#ca4c79d8 chat_id:int photo:InputChatPhoto = Updates;
messages.addChatUser#f9a0aa09 chat_id:int user_id:InputUser fwd_limit:int = Updates;
messages.deleteChatUser#e0611f16 chat_id:int user_id:InputUser = Updates;
messages.createChat#9cb126e users:Vector<InputUser> title:string = Updates;
messages.forwardMessage#33963bf9 peer:InputPeer id:int random_id:long = Updates;
messages.sendBroadcast#bf73f4da contacts:Vector<InputUser> random_id:Vector<long> message:string media:InputMedia = Updates;
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
messages.readMessageContents#36a73f77 id:Vector<int> = messages.AffectedMessages;
messages.getStickers#ae22e045 emoticon:string hash:string = messages.Stickers;
messages.getAllStickers#1c9618b1 hash:int = messages.AllStickers;
messages.getWebPagePreview#25223e24 message:string = MessageMedia;
messages.exportChatInvite#7d885289 chat_id:int = ExportedChatInvite;
messages.checkChatInvite#3eadb1bb hash:string = ChatInvite;
messages.importChatInvite#6c50051c hash:string = Updates;
messages.getStickerSet#2619a90e stickerset:InputStickerSet = messages.StickerSet;
messages.installStickerSet#7b30c3a6 stickerset:InputStickerSet disabled:Bool = Bool;
messages.uninstallStickerSet#f96e55de stickerset:InputStickerSet = Bool;
messages.startBot#e6df7378 bot:InputUser peer:InputPeer random_id:long start_param:string = Updates;
messages.getMessagesViews#c4c8a55d peer:InputPeer id:Vector<int> increment:Bool = Vector<int>;
messages.toggleChatAdmins#ec8bd9e1 chat_id:int enabled:Bool = Updates;
messages.editChatAdmin#a9e69f2e chat_id:int user_id:InputUser is_admin:Bool = Bool;
messages.migrateChat#15a3b8e3 chat_id:int = Updates;
messages.searchGlobal#9e3cacb0 q:string offset_date:int offset_peer:InputPeer offset_id:int limit:int = messages.Messages;
messages.reorderStickerSets#9fcfbc30 order:Vector<long> = Bool;

updates.getState#edd4882a = updates.State;
updates.getDifference#a041495 pts:int date:int qts:int = updates.Difference;
updates.getChannelDifference#bb32d7c0 channel:InputChannel filter:ChannelMessagesFilter pts:int limit:int = updates.ChannelDifference;

photos.updateProfilePhoto#eef579a0 id:InputPhoto crop:InputPhotoCrop = UserProfilePhoto;
photos.uploadProfilePhoto#d50f9c88 file:InputFile caption:string geo_point:InputGeoPoint crop:InputPhotoCrop = photos.Photo;
photos.deletePhotos#87cf7f2f id:Vector<InputPhoto> = Vector<long>;
photos.getUserPhotos#91cd32a8 user_id:InputUser offset:int max_id:long limit:int = photos.Photos;

upload.saveFilePart#b304a621 file_id:long file_part:int bytes:bytes = Bool;
upload.getFile#e3a6cfb5 location:InputFileLocation offset:int limit:int = upload.File;
upload.saveBigFilePart#de7b673d file_id:long file_part:int file_total_parts:int bytes:bytes = Bool;

help.getConfig#c4f9186b = Config;
help.getNearestDc#1fb33026 = NearestDc;
help.getAppUpdate#c812ac7e device_model:string system_version:string app_version:string lang_code:string = help.AppUpdate;
help.saveAppLog#6f02f748 events:Vector<InputAppEvent> = Bool;
help.getInviteText#a4a95186 lang_code:string = help.InviteText;
help.getSupport#9cdf08cd = help.Support;
help.getAppChangelog#5bab7fb2 device_model:string system_version:string app_version:string lang_code:string = help.AppChangelog;
help.getTermsOfService#37d78f83 lang_code:string = help.TermsOfService;

channels.getDialogs#a9d3d249 offset:int limit:int = messages.Dialogs;
channels.getImportantHistory#ddb929cb channel:InputChannel offset_id:int add_offset:int limit:int max_id:int min_id:int = messages.Messages;
channels.readHistory#cc104937 channel:InputChannel max_id:int = Bool;
channels.deleteMessages#84c1fd4e channel:InputChannel id:Vector<int> = messages.AffectedMessages;
channels.deleteUserHistory#d10dd71b channel:InputChannel user_id:InputUser = messages.AffectedHistory;
channels.reportSpam#fe087810 channel:InputChannel user_id:InputUser id:Vector<int> = Bool;
channels.getMessages#93d7b347 channel:InputChannel id:Vector<int> = messages.Messages;
channels.getParticipants#24d98f92 channel:InputChannel filter:ChannelParticipantsFilter offset:int limit:int = channels.ChannelParticipants;
channels.getParticipant#546dd7a6 channel:InputChannel user_id:InputUser = channels.ChannelParticipant;
channels.getChannels#a7f6bbb id:Vector<InputChannel> = messages.Chats;
channels.getFullChannel#8736a09 channel:InputChannel = messages.ChatFull;
channels.createChannel#f4893d7f flags:# broadcast:flags.0?true megagroup:flags.1?true title:string about:string = Updates;
channels.editAbout#13e27f1e channel:InputChannel about:string = Bool;
channels.editAdmin#eb7611d0 channel:InputChannel user_id:InputUser role:ChannelParticipantRole = Updates;
channels.editTitle#566decd0 channel:InputChannel title:string = Updates;
channels.editPhoto#f12e57c9 channel:InputChannel photo:InputChatPhoto = Updates;
channels.toggleComments#aaa29e88 channel:InputChannel enabled:Bool = Updates;
channels.checkUsername#10e6bd2c channel:InputChannel username:string = Bool;
channels.updateUsername#3514b3de channel:InputChannel username:string = Bool;
channels.joinChannel#24b524c5 channel:InputChannel = Updates;
channels.leaveChannel#f836aa95 channel:InputChannel = Updates;
channels.inviteToChannel#199f3a6c channel:InputChannel users:Vector<InputUser> = Updates;
channels.kickFromChannel#a672de14 channel:InputChannel user_id:InputUser kicked:Bool = Updates;
channels.exportInvite#c7560885 channel:InputChannel = ExportedChatInvite;
channels.deleteChannel#c0111fe3 channel:InputChannel = Updates;
