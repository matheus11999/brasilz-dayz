// Centralised configuration for the Discord webhook integration.
// Keeping the webhook URL and event toggles in one constants class avoids
// scattering tokens across multiple files. Switch a LOG_* flag to false to
// silence an event channel without touching the hook code.
class BZ_DiscordConfig
{
	// BrasilZ Discord webhook (channel-specific, server-only — never expose to clients).
	static const string WEBHOOK_URL = "https://discord.com/api/webhooks/1507529661033222234/4r1hCSdyuZ6-n8R9WoRz9lGaAWjPdZJPyATDXQR9hz_8aTynqq6YZkNB6KQAOMS4mJRn";

	// Username shown in the Discord message header.
	static const string BOT_USERNAME = "BrasilZ Server";

	// Embed colors in decimal (Discord requires decimal, not hex).
	static const int COLOR_GREEN  = 5763719;   // success: purchase, mission win
	static const int COLOR_RED    = 15548997;  // death, errors
	static const int COLOR_BLUE   = 3447003;   // connect
	static const int COLOR_YELLOW = 16776960;  // sale
	static const int COLOR_GRAY   = 9807270;   // disconnect, suicide
	static const int COLOR_ORANGE = 15105570;  // mission start, warning
	static const int COLOR_PURPLE = 10181046;  // PvP kill

	// Event toggles — flip to false to silence the channel.
	static const bool LOG_PURCHASE      = true;
	static const bool LOG_PURCHASE_FAIL = true;
	static const bool LOG_SALE          = true;
	static const bool LOG_CONNECT       = true;
	static const bool LOG_DISCONNECT    = true;
	static const bool LOG_KILL          = true;
	static const bool LOG_MISSION       = true;
	static const bool LOG_SPAWN         = true;
}
