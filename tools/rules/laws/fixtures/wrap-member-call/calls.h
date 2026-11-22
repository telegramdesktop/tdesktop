struct Controller {
	void showNotificationWithIcon(int message);
};
extern Controller *controller;
inline constexpr auto messageToDisplayInTheNotification = 1;
