// Core types (no need to gen)

//boolFalse#bc799737 = Bool;
//boolTrue#997275b5 = Bool;

//vector#1cb5c415 {t:Type} # [ t ] = Vector t;

//error#c4b9f9bb code:int text:string = Error;

//null#56730bcc = Null;

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



inputPeerEmpty#7f3b18ea = InputPeer;
inputPeerSelf#7da07ec9 = InputPeer;
inputPeerContact#1023dbe8 user_id:int = InputPeer;
inputPeerForeign#9b447325 user_id:int access_hash:long = InputPeer;
inputPeerChat#179be863 chat_id:int = InputPeer;

inputUserEmpty#b98886cf = InputUser;
inputUserSelf#f7c1b13f = InputUser;
inputUserContact#86e94f65 user_id:int = InputUser;
inputUserForeign#655e74ff user_id:int access_hash:long = InputUser;

inputPhoneContact#f392b7f4 client_id:long phone:string first_name:string last_name:string = InputContact;

inputFile#f52ff27f id:long parts:int name:string md5_checksum:string = InputFile;

inputMediaEmpty#9664f57f = InputMedia;
inputMediaUploadedPhoto#2dc53a7d file:InputFile = InputMedia;
inputMediaPhoto#8f2ab2ec id:InputPhoto = InputMedia;
inputMediaGeoPoint#f9c44144 geo_point:InputGeoPoint = InputMedia;
inputMediaContact#a6e45987 phone_number:string first_name:string last_name:string = InputMedia;
inputMediaUploadedVideo#133ad6f6 file:InputFile duration:int w:int h:int mime_type:string = InputMedia;
inputMediaUploadedThumbVideo#9912dabf file:InputFile thumb:InputFile duration:int w:int h:int mime_type:string = InputMedia;
inputMediaVideo#7f023ae6 id:InputVideo = InputMedia;

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

inputPhotoCropAuto#ade6b004 = InputPhotoCrop;
inputPhotoCrop#d9915325 crop_left:double crop_top:double crop_width:double = InputPhotoCrop;

inputAppEvent#770656a8 time:double type:string peer:long data:string = InputAppEvent;

peerUser#9db1bc6d user_id:int = Peer;
peerChat#bad0e5bb chat_id:int = Peer;

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
userSelf#720535ec id:int first_name:string last_name:string phone:string photo:UserProfilePhoto status:UserStatus inactive:Bool = User;
userContact#f2fb8319 id:int first_name:string last_name:string access_hash:long phone:string photo:UserProfilePhoto status:UserStatus = User;
userRequest#22e8ceb0 id:int first_name:string last_name:string access_hash:long phone:string photo:UserProfilePhoto status:UserStatus = User;
userForeign#5214c89d id:int first_name:string last_name:string access_hash:long photo:UserProfilePhoto status:UserStatus = User;
userDeleted#b29ad7cc id:int first_name:string last_name:string = User;

userProfilePhotoEmpty#4f11bae1 = UserProfilePhoto;
userProfilePhoto#d559d8c8 photo_id:long photo_small:FileLocation photo_big:FileLocation = UserProfilePhoto;

userStatusEmpty#9d05049 = UserStatus;
userStatusOnline#edb93949 expires:int = UserStatus;
userStatusOffline#8c703f was_online:int = UserStatus;

chatEmpty#9ba2d800 id:int = Chat;
chat#6e9c9bc7 id:int title:string photo:ChatPhoto participants_count:int date:int left:Bool version:int = Chat;
chatForbidden#fb0ccc41 id:int title:string date:int = Chat;

chatFull#630e61be id:int participants:ChatParticipants chat_photo:Photo notify_settings:PeerNotifySettings = ChatFull;

chatParticipant#c8d7493e user_id:int inviter_id:int date:int = ChatParticipant;

chatParticipantsForbidden#fd2bb8a chat_id:int = ChatParticipants;
chatParticipants#7841b415 chat_id:int admin_id:int participants:Vector<ChatParticipant> version:int = ChatParticipants;

chatPhotoEmpty#37c1011c = ChatPhoto;
chatPhoto#6153276a photo_small:FileLocation photo_big:FileLocation = ChatPhoto;

messageEmpty#83e5de54 id:int = Message;
message#22eb6aba id:int from_id:int to_id:Peer out:Bool unread:Bool date:int message:string media:MessageMedia = Message;
messageForwarded#5f46804 id:int fwd_from_id:int fwd_date:int from_id:int to_id:Peer out:Bool unread:Bool date:int message:string media:MessageMedia = Message;
messageService#9f8d60bb id:int from_id:int to_id:Peer out:Bool unread:Bool date:int action:MessageAction = Message;

messageMediaEmpty#3ded6320 = MessageMedia;
messageMediaPhoto#c8c45a2a photo:Photo = MessageMedia;
messageMediaVideo#a2d24290 video:Video = MessageMedia;
messageMediaGeo#56e0d474 geo:GeoPoint = MessageMedia;
messageMediaContact#5e7d2f39 phone_number:string first_name:string last_name:string user_id:int = MessageMedia;
messageMediaUnsupported#29632a36 bytes:bytes = MessageMedia;

messageActionEmpty#b6aef7b0 = MessageAction;
messageActionChatCreate#a6638b9a title:string users:Vector<int> = MessageAction;
messageActionChatEditTitle#b5a1ce5a title:string = MessageAction;
messageActionChatEditPhoto#7fcb13a8 photo:Photo = MessageAction;
messageActionChatDeletePhoto#95e3fbef = MessageAction;
messageActionChatAddUser#5e3cfc4b user_id:int = MessageAction;
messageActionChatDeleteUser#b2ae9b0c user_id:int = MessageAction;

dialog#ab3a99ac peer:Peer top_message:int unread_count:int notify_settings:PeerNotifySettings = Dialog;

photoEmpty#2331b22d id:long = Photo;
photo#22b56751 id:long access_hash:long user_id:int date:int caption:string geo:GeoPoint sizes:Vector<PhotoSize> = Photo;

photoSizeEmpty#e17e23c type:string = PhotoSize;
photoSize#77bfb61b type:string location:FileLocation w:int h:int size:int = PhotoSize;
photoCachedSize#e9a734fa type:string location:FileLocation w:int h:int bytes:bytes = PhotoSize;

videoEmpty#c10658a8 id:long = Video;
video#388fa391 id:long access_hash:long user_id:int date:int caption:string duration:int mime_type:string size:int thumb:PhotoSize dc_id:int w:int h:int = Video;

geoPointEmpty#1117dd5f = GeoPoint;
geoPoint#2049d70c long:double lat:double = GeoPoint;

auth.checkedPhone#e300cc3b phone_registered:Bool phone_invited:Bool = auth.CheckedPhone;

auth.sentCode#efed51d9 phone_registered:Bool phone_code_hash:string send_call_timeout:int is_password:Bool = auth.SentCode;

auth.authorization#f6b673a4 expires:int user:User = auth.Authorization;

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

userFull#771095da user:User link:contacts.Link profile_photo:Photo notify_settings:PeerNotifySettings blocked:Bool real_first_name:string real_last_name:string = UserFull;

contact#f911c994 user_id:int mutual:Bool = Contact;

importedContact#d0028438 user_id:int client_id:long = ImportedContact;

contactBlocked#561bc879 user_id:int date:int = ContactBlocked;

contactFound#ea879f95 user_id:int = ContactFound;

contactSuggested#3de191a1 user_id:int mutual_contacts:int = ContactSuggested;

contactStatus#aa77b873 user_id:int expires:int = ContactStatus;

chatLocated#3631cf4c chat_id:int distance:int = ChatLocated;

contacts.foreignLinkUnknown#133421f8 = contacts.ForeignLink;
contacts.foreignLinkRequested#a7801f47 has_phone:Bool = contacts.ForeignLink;
contacts.foreignLinkMutual#1bea8ce1 = contacts.ForeignLink;

contacts.myLinkEmpty#d22a1c60 = contacts.MyLink;
contacts.myLinkRequested#6c69efee contact:Bool = contacts.MyLink;
contacts.myLinkContact#c240ebd9 = contacts.MyLink;

contacts.link#eccea3f5 my_link:contacts.MyLink foreign_link:contacts.ForeignLink user:User = contacts.Link;

contacts.contacts#6f8b8cb2 contacts:Vector<Contact> users:Vector<User> = contacts.Contacts;
contacts.contactsNotModified#b74ba9d2 = contacts.Contacts;

contacts.importedContacts#ad524315 imported:Vector<ImportedContact> retry_contacts:Vector<long> users:Vector<User> = contacts.ImportedContacts;

contacts.blocked#1c138d15 blocked:Vector<ContactBlocked> users:Vector<User> = contacts.Blocked;
contacts.blockedSlice#900802a1 count:int blocked:Vector<ContactBlocked> users:Vector<User> = contacts.Blocked;

contacts.found#566000e results:Vector<ContactFound> users:Vector<User> = contacts.Found;

contacts.suggested#5649dcc5 results:Vector<ContactSuggested> users:Vector<User> = contacts.Suggested;

messages.dialogs#15ba6c40 dialogs:Vector<Dialog> messages:Vector<Message> chats:Vector<Chat> users:Vector<User> = messages.Dialogs;
messages.dialogsSlice#71e094f3 count:int dialogs:Vector<Dialog> messages:Vector<Message> chats:Vector<Chat> users:Vector<User> = messages.Dialogs;

messages.messages#8c718e87 messages:Vector<Message> chats:Vector<Chat> users:Vector<User> = messages.Messages;
messages.messagesSlice#b446ae3 count:int messages:Vector<Message> chats:Vector<Chat> users:Vector<User> = messages.Messages;

messages.messageEmpty#3f4e0648 = messages.Message;
messages.message#ff90c417 message:Message chats:Vector<Chat> users:Vector<User> = messages.Message;

messages.statedMessages#969478bb messages:Vector<Message> chats:Vector<Chat> users:Vector<User> pts:int seq:int = messages.StatedMessages;

messages.statedMessage#d07ae726 message:Message chats:Vector<Chat> users:Vector<User> pts:int seq:int = messages.StatedMessage;

messages.sentMessage#d1f4d35c id:int date:int pts:int seq:int = messages.SentMessage;

messages.chat#40e9002a chat:Chat users:Vector<User> = messages.Chat;

messages.chats#8150cbd8 chats:Vector<Chat> users:Vector<User> = messages.Chats;

messages.chatFull#e5d7d19c full_chat:ChatFull chats:Vector<Chat> users:Vector<User> = messages.ChatFull;

messages.affectedHistory#b7de36f2 pts:int seq:int offset:int = messages.AffectedHistory;

inputMessagesFilterEmpty#57e2f66c = MessagesFilter;
inputMessagesFilterPhotos#9609a51c = MessagesFilter;
inputMessagesFilterVideo#9fc00e65 = MessagesFilter;
inputMessagesFilterPhotoVideo#56e9f0e4 = MessagesFilter;
inputMessagesFilterDocument#9eddf188 = MessagesFilter;
inputMessagesFilterAudio#cfc87522 = MessagesFilter;

updateNewMessage#13abdb3 message:Message pts:int = Update;
updateMessageID#4e90bfd6 id:int random_id:long = Update;
updateReadMessages#c6649e31 messages:Vector<int> pts:int = Update;
updateDeleteMessages#a92bfe26 messages:Vector<int> pts:int = Update;
updateRestoreMessages#d15de04d messages:Vector<int> pts:int = Update;
updateUserTyping#6baa8508 user_id:int = Update;
updateChatUserTyping#3c46cfe6 chat_id:int user_id:int = Update;
updateChatParticipants#7761198 participants:ChatParticipants = Update;
updateUserStatus#1bfbd823 user_id:int status:UserStatus = Update;
updateUserName#da22d9ad user_id:int first_name:string last_name:string = Update;
updateUserPhoto#95313b0c user_id:int date:int photo:UserProfilePhoto previous:Bool = Update;
updateContactRegistered#2575bbb9 user_id:int date:int = Update;
updateContactLink#51a48a9a user_id:int my_link:contacts.MyLink foreign_link:contacts.ForeignLink = Update;
updateActivation#6f690963 user_id:int = Update;
updateNewAuthorization#8f06529a auth_key_id:long date:int device:string location:string = Update;

updates.state#a56c2a3e pts:int qts:int date:int seq:int unread_count:int = updates.State;

updates.differenceEmpty#5d75a138 date:int seq:int = updates.Difference;
updates.difference#f49ca0 new_messages:Vector<Message> new_encrypted_messages:Vector<EncryptedMessage> other_updates:Vector<Update> chats:Vector<Chat> users:Vector<User> state:updates.State = updates.Difference;
updates.differenceSlice#a8fb1981 new_messages:Vector<Message> new_encrypted_messages:Vector<EncryptedMessage> other_updates:Vector<Update> chats:Vector<Chat> users:Vector<User> intermediate_state:updates.State = updates.Difference;

updatesTooLong#e317af7e = Updates;
updateShortMessage#d3f45784 id:int from_id:int message:string pts:int date:int seq:int = Updates;
updateShortChatMessage#2b2fbd4e id:int from_id:int chat_id:int message:string pts:int date:int seq:int = Updates;
updateShort#78d4dec1 update:Update date:int = Updates;
updatesCombined#725b04c3 updates:Vector<Update> users:Vector<User> chats:Vector<Chat> date:int seq_start:int seq:int = Updates;
updates#74ae4240 updates:Vector<Update> users:Vector<User> chats:Vector<Chat> date:int seq:int = Updates;

photos.photos#8dca6aa5 photos:Vector<Photo> users:Vector<User> = photos.Photos;
photos.photosSlice#15051f54 count:int photos:Vector<Photo> users:Vector<User> = photos.Photos;

photos.photo#20212ca8 photo:Photo users:Vector<User> = photos.Photo;

upload.file#96a18d5 type:storage.FileType mtime:int bytes:bytes = upload.File;

dcOption#2ec2a43c id:int hostname:string ip_address:string port:int = DcOption;

config#2e54dd74 date:int test_mode:Bool this_dc:int dc_options:Vector<DcOption> chat_size_max:int broadcast_size_max:int = Config;

nearestDc#8e1a1775 country:string this_dc:int nearest_dc:int = NearestDc;

help.appUpdate#8987f311 id:int critical:Bool url:string text:string = help.AppUpdate;
help.noAppUpdate#c45a6536 = help.AppUpdate;

help.inviteText#18cb9f78 message:string = help.InviteText;

messages.statedMessagesLinks#3e74f5c6 messages:Vector<Message> chats:Vector<Chat> users:Vector<User> links:Vector<contacts.Link> pts:int seq:int = messages.StatedMessages;

messages.statedMessageLink#a9af2881 message:Message chats:Vector<Chat> users:Vector<User> links:Vector<contacts.Link> pts:int seq:int = messages.StatedMessage;

messages.sentMessageLink#e9db4a3f id:int date:int pts:int seq:int links:Vector<contacts.Link> = messages.SentMessage;

inputGeoChat#74d456fa chat_id:int access_hash:long = InputGeoChat;

inputNotifyGeoChatPeer#4d8ddec8 peer:InputGeoChat = InputNotifyPeer;

geoChat#75eaea5a id:int access_hash:long title:string address:string venue:string geo:GeoPoint photo:ChatPhoto participants_count:int date:int checked_in:Bool version:int = Chat;

geoChatMessageEmpty#60311a9b chat_id:int id:int = GeoChatMessage;
geoChatMessage#4505f8e1 chat_id:int id:int from_id:int date:int message:string media:MessageMedia = GeoChatMessage;
geoChatMessageService#d34fa24e chat_id:int id:int from_id:int date:int action:MessageAction = GeoChatMessage;

geochats.statedMessage#17b1578b message:GeoChatMessage chats:Vector<Chat> users:Vector<User> seq:int = geochats.StatedMessage;

geochats.located#48feb267 results:Vector<ChatLocated> messages:Vector<GeoChatMessage> chats:Vector<Chat> users:Vector<User> = geochats.Located;

geochats.messages#d1526db1 messages:Vector<GeoChatMessage> chats:Vector<Chat> users:Vector<User> = geochats.Messages;
geochats.messagesSlice#bc5863e8 count:int messages:Vector<GeoChatMessage> chats:Vector<Chat> users:Vector<User> = geochats.Messages;

messageActionGeoChatCreate#6f038ebc title:string address:string = MessageAction;
messageActionGeoChatCheckin#c7d53de = MessageAction;

updateNewGeoChatMessage#5a68e3f7 message:GeoChatMessage = Update;

wallPaperSolid#63117f24 id:int title:string bg_color:int color:int = WallPaper;

updateNewEncryptedMessage#12bcbd9a message:EncryptedMessage qts:int = Update;
updateEncryptedChatTyping#1710f156 chat_id:int = Update;
updateEncryption#b4a2e88d chat:EncryptedChat date:int = Update;
updateEncryptedMessagesRead#38fe25b7 chat_id:int max_date:int date:int = Update;

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

inputEncryptedFileLocation#f5235d55 id:long access_hash:long = InputFileLocation;

encryptedMessage#ed18c118 random_id:long chat_id:int date:int bytes:bytes file:EncryptedFile = EncryptedMessage;
encryptedMessageService#23734b06 random_id:long chat_id:int date:int bytes:bytes = EncryptedMessage;

decryptedMessageLayer#99a438cf layer:int message:DecryptedMessage = DecryptedMessageLayer;

decryptedMessage#1f814f1f random_id:long random_bytes:bytes message:string media:DecryptedMessageMedia = DecryptedMessage;
decryptedMessageService#aa48327d random_id:long random_bytes:bytes action:DecryptedMessageAction = DecryptedMessage;

decryptedMessageMediaEmpty#89f5c4a = DecryptedMessageMedia;
decryptedMessageMediaPhoto#32798a8c thumb:bytes thumb_w:int thumb_h:int w:int h:int size:int key:bytes iv:bytes = DecryptedMessageMedia;
decryptedMessageMediaVideo#524a415d thumb:bytes thumb_w:int thumb_h:int duration:int mime_type:string w:int h:int size:int key:bytes iv:bytes = DecryptedMessageMedia;
decryptedMessageMediaGeoPoint#35480a59 lat:double long:double = DecryptedMessageMedia;
decryptedMessageMediaContact#588a0a97 phone_number:string first_name:string last_name:string user_id:int = DecryptedMessageMedia;

decryptedMessageActionSetMessageTTL#a1733aec ttl_seconds:int = DecryptedMessageAction;

messages.dhConfigNotModified#c0e24635 random:bytes = messages.DhConfig;
messages.dhConfig#2c221edd g:int p:bytes version:int random:bytes = messages.DhConfig;

messages.sentEncryptedMessage#560f8935 date:int = messages.SentEncryptedMessage;
messages.sentEncryptedFile#9493ff32 date:int file:EncryptedFile = messages.SentEncryptedMessage;

inputFileBig#fa4f0bb5 id:long parts:int name:string = InputFile;

inputEncryptedFileBigUploaded#2dc173c8 id:long parts:int key_fingerprint:int = InputEncryptedFile;

updateChatParticipantAdd#3a0eeb22 chat_id:int user_id:int inviter_id:int version:int = Update;
updateChatParticipantDelete#6e5f8c22 chat_id:int user_id:int version:int = Update;
updateDcOptions#8e5e9873 dc_options:Vector<DcOption> = Update;

inputMediaUploadedAudio#4e498cab file:InputFile duration:int mime_type:string = InputMedia;
inputMediaAudio#89938781 id:InputAudio = InputMedia;
inputMediaUploadedDocument#34e794bd file:InputFile file_name:string mime_type:string = InputMedia;
inputMediaUploadedThumbDocument#3e46de5d file:InputFile thumb:InputFile file_name:string mime_type:string = InputMedia;
inputMediaDocument#d184e841 id:InputDocument = InputMedia;

messageMediaDocument#2fda2204 document:Document = MessageMedia;
messageMediaAudio#c6b68300 audio:Audio = MessageMedia;

inputAudioEmpty#d95adc84 = InputAudio;
inputAudio#77d440ff id:long access_hash:long = InputAudio;

inputDocumentEmpty#72f0eaae = InputDocument;
inputDocument#18798952 id:long access_hash:long = InputDocument;

inputAudioFileLocation#74dc404d id:long access_hash:long = InputFileLocation;
inputDocumentFileLocation#4e45abe9 id:long access_hash:long = InputFileLocation;

decryptedMessageMediaDocument#b095434b thumb:bytes thumb_w:int thumb_h:int file_name:string mime_type:string size:int key:bytes iv:bytes = DecryptedMessageMedia;
decryptedMessageMediaAudio#57e0a9cb duration:int mime_type:string size:int key:bytes iv:bytes = DecryptedMessageMedia;

audioEmpty#586988d8 id:long = Audio;
audio#c7ac6496 id:long access_hash:long user_id:int date:int duration:int mime_type:string size:int dc_id:int = Audio;

documentEmpty#36f8c871 id:long = Document;
document#9efc6326 id:long access_hash:long user_id:int date:int file_name:string mime_type:string size:int thumb:PhotoSize dc_id:int = Document;

help.support#17c6b5f6 phone_number:string user:User = help.Support;

decryptedMessageActionReadMessages#c4f40be random_ids:Vector<long> = DecryptedMessageAction;
decryptedMessageActionDeleteMessages#65614304 random_ids:Vector<long> = DecryptedMessageAction;
decryptedMessageActionScreenshotMessages#8ac1f475 random_ids:Vector<long> = DecryptedMessageAction;
decryptedMessageActionFlushHistory#6719e45c = DecryptedMessageAction;
decryptedMessageActionNotifyLayer#f3048883 layer:int = DecryptedMessageAction;

notifyPeer#9fd40bd8 peer:Peer = NotifyPeer;
notifyUsers#b4c83b4c = NotifyPeer;
notifyChats#c007cec3 = NotifyPeer;
notifyAll#74d07c60 = NotifyPeer;

updateUserBlocked#80ece81a user_id:int blocked:Bool = Update;
updateNotifySettings#bec268ef peer:NotifyPeer notify_settings:PeerNotifySettings = Update;

---functions---

invokeAfterMsg#cb9f372d msg_id:long query:!X = X;

invokeAfterMsgs#3dc4b4f0 msg_ids:Vector<long> query:!X = X;

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

account.registerDevice#446c712c token_type:int token:string device_model:string system_version:string app_version:string app_sandbox:Bool lang_code:string = Bool;
account.unregisterDevice#65c55b40 token_type:int token:string = Bool;
account.updateNotifySettings#84be5b93 peer:InputNotifyPeer settings:InputPeerNotifySettings = Bool;
account.getNotifySettings#12b3ad31 peer:InputNotifyPeer = PeerNotifySettings;
account.resetNotifySettings#db7e1747 = Bool;
account.updateProfile#f0888d68 first_name:string last_name:string = User;
account.updateStatus#6628562c offline:Bool = Bool;
account.getWallPapers#c04cfac2 = Vector<WallPaper>;

users.getUsers#d91a548 id:Vector<InputUser> = Vector<User>;
users.getFullUser#ca30a5b1 id:InputUser = UserFull;

contacts.getStatuses#c4a353ee = Vector<ContactStatus>;
contacts.getContacts#22c6aa08 hash:string = contacts.Contacts;
contacts.importContacts#da30b32d contacts:Vector<InputContact> replace:Bool = contacts.ImportedContacts;
contacts.search#11f812d8 q:string limit:int = contacts.Found;
contacts.getSuggested#cd773428 limit:int = contacts.Suggested;
contacts.deleteContact#8e953744 id:InputUser = contacts.Link;
contacts.deleteContacts#59ab389e id:Vector<InputUser> = Bool;
contacts.block#332b49fc id:InputUser = Bool;
contacts.unblock#e54100bd id:InputUser = Bool;
contacts.getBlocked#f57c350f offset:int limit:int = contacts.Blocked;

messages.getMessages#4222fa74 id:Vector<int> = messages.Messages;
messages.getDialogs#eccf1df6 offset:int max_id:int limit:int = messages.Dialogs;
messages.getHistory#92a1df2f peer:InputPeer offset:int max_id:int limit:int = messages.Messages;
messages.search#7e9f2ab peer:InputPeer q:string filter:MessagesFilter min_date:int max_date:int offset:int max_id:int limit:int = messages.Messages;
messages.readHistory#b04f2510 peer:InputPeer max_id:int offset:int = messages.AffectedHistory;
messages.deleteHistory#f4f8fb61 peer:InputPeer offset:int = messages.AffectedHistory;
messages.deleteMessages#14f2dd0a id:Vector<int> = Vector<int>;
messages.restoreMessages#395f9d7e id:Vector<int> = Vector<int>;
messages.receivedMessages#28abcb68 max_id:int = Vector<int>;
messages.setTyping#719839e9 peer:InputPeer typing:Bool = Bool;
messages.sendMessage#4cde0aab peer:InputPeer message:string random_id:long = messages.SentMessage;
messages.sendMedia#a3c85d76 peer:InputPeer media:InputMedia random_id:long = messages.StatedMessage;
messages.forwardMessages#514cd10f peer:InputPeer id:Vector<int> = messages.StatedMessages;
messages.getChats#3c6aa187 id:Vector<int> = messages.Chats;
messages.getFullChat#3b831c66 chat_id:int = messages.ChatFull;
messages.editChatTitle#b4bc68b5 chat_id:int title:string = messages.StatedMessage;
messages.editChatPhoto#d881821d chat_id:int photo:InputChatPhoto = messages.StatedMessage;
messages.addChatUser#2ee9ee9e chat_id:int user_id:InputUser fwd_limit:int = messages.StatedMessage;
messages.deleteChatUser#c3c5cd23 chat_id:int user_id:InputUser = messages.StatedMessage;
messages.createChat#419d9aee users:Vector<InputUser> title:string = messages.StatedMessage;

updates.getState#edd4882a = updates.State;
updates.getDifference#a041495 pts:int date:int qts:int = updates.Difference;

photos.updateProfilePhoto#eef579a0 id:InputPhoto crop:InputPhotoCrop = UserProfilePhoto;
photos.uploadProfilePhoto#d50f9c88 file:InputFile caption:string geo_point:InputGeoPoint crop:InputPhotoCrop = photos.Photo;

upload.saveFilePart#b304a621 file_id:long file_part:int bytes:bytes = Bool;
upload.getFile#e3a6cfb5 location:InputFileLocation offset:int limit:int = upload.File;

help.getConfig#c4f9186b = Config;
help.getNearestDc#1fb33026 = NearestDc;
help.getAppUpdate#c812ac7e device_model:string system_version:string app_version:string lang_code:string = help.AppUpdate;
help.saveAppLog#6f02f748 events:Vector<InputAppEvent> = Bool;
help.getInviteText#a4a95186 lang_code:string = help.InviteText;

photos.getUserPhotos#b7ee553c user_id:InputUser offset:int max_id:int limit:int = photos.Photos;

messages.forwardMessage#3f3f4f2 peer:InputPeer id:int random_id:long = messages.StatedMessage;
messages.sendBroadcast#41bb0972 contacts:Vector<InputUser> message:string media:InputMedia = messages.StatedMessages;

geochats.getLocated#7f192d8f geo_point:InputGeoPoint radius:int limit:int = geochats.Located;
geochats.getRecents#e1427e6f offset:int limit:int = geochats.Messages;
geochats.checkin#55b3e8fb peer:InputGeoChat = geochats.StatedMessage;
geochats.getFullChat#6722dd6f peer:InputGeoChat = messages.ChatFull;
geochats.editChatTitle#4c8e2273 peer:InputGeoChat title:string address:string = geochats.StatedMessage;
geochats.editChatPhoto#35d81a95 peer:InputGeoChat photo:InputChatPhoto = geochats.StatedMessage;
geochats.search#cfcdc44d peer:InputGeoChat q:string filter:MessagesFilter min_date:int max_date:int offset:int max_id:int limit:int = geochats.Messages;
geochats.getHistory#b53f7a68 peer:InputGeoChat offset:int max_id:int limit:int = geochats.Messages;
geochats.setTyping#8b8a729 peer:InputGeoChat typing:Bool = Bool;
geochats.sendMessage#61b0044 peer:InputGeoChat message:string random_id:long = geochats.StatedMessage;
geochats.sendMedia#b8f0deff peer:InputGeoChat media:InputMedia random_id:long = geochats.StatedMessage;
geochats.createGeoChat#e092e16 title:string geo_point:InputGeoPoint address:string venue:string = geochats.StatedMessage;

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

upload.saveBigFilePart#de7b673d file_id:long file_part:int file_total_parts:int bytes:bytes = Bool;

initConnection#69796de9 api_id:int device_model:string system_version:string app_version:string lang_code:string query:!X = X;

help.getSupport#9cdf08cd = help.Support;
