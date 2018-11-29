#include <sys/stat.h>
#include <fcntl.h>
#include <libgen.h>

// X11
#include <X11/Xlib.h>

// weather
#include <curl/curl.h>
#include <cJSON/cJSON.h>

// wifi
#include <net/if.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <linux/nl80211.h>
#include <libnetlink.h>
#include <linux/if_arp.h>
#include <ctype.h>
#include <stdbool.h>

// disk usage
#include <sys/statvfs.h>

// memory
#include <proc/sysinfo.h>

// volume
#include <alsa/asoundlib.h>
#include <math.h>

#define STRING_LENGTH			128
#define TOTAL_LENGTH			1024
#define UTC_TO_CST_SECONDS		18000
#define HOUR					0
#define DAY						1

#define COLOR_NORMAL				color1
#define COLOR_ACTIVE				color2
#define COLOR1						color3
#define COLOR2						color4
#define COLOR_WARNING				color5
#define COLOR_ERROR					color6
#define GREEN_TEXT					color7
#define RED_TEXT					color8
#define COLOR_HEADING				COLOR_ACTIVE

#define TODO_FILE					"/home/user/.TODO"
#define STATUSBAR_LOG_FILE			"/home/user/.logs/dwm-statusbar.log"
#define DWM_LOG_FILE				"/home/user/.logs/dwm.log"
#define BACKUP_STATUS_FILE			"/home/user/.backup/.sb"
#define LOCATION					"0000000"
#define KEY							"00000000000000000000000000000000"
#define WEATHER_URL					"http://api.openweathermap.org/data/2.5/weather?id=" \
									LOCATION "&appid=" KEY "&units=imperial"
#define FORECAST_URL				"http://api.openweathermap.org/data/2.5/forecast?id=" \
									LOCATION "&appid=" KEY "&units=imperial"
#define RH_LOGIN					"username={username}&password={password}
#define DWM_CONFIG_FILE				"/home/user/.dwm/config.h"
#define NET_RX_FILE					"/sys/class/net/" WIFI_INTERFACE "/statistics/rx_bytes"
#define NET_TX_FILE					"/sys/class/net/" WIFI_INTERFACE "/statistics/tx_bytes"
#define CPU_USAGE_FILE				"/proc/stat"
#define CPU_TEMP_DIR				"/sys/class/hwmon/hwmon0/"
#define FAN_SPEED_DIR				"/sys/class/hwmon/hwmon2/device/"
#define SCREEN_BRIGHTNESS_FILE		"/sys/class/backlight/nvidia_backlight/brightness"
#define KBD_BRIGHTNESS_FILE			"/sys/class/leds/smc::kbd_backlight/brightness"
#define BATT_STATUS_FILE			"/sys/class/power_supply/BAT0/status"
#define BATT_CAPACITY_FILE			"/sys/class/power_supply/BAT0/capacity"

#define WIFI_INTERFACE				"wlp4s0"	// TODO get from /proc/net/arp?
#ifdef KBD_BRIGHTNESS_FILE
	#define DISPLAY_KBD				true
#else
	#define DISPLAY_KBD				false
#endif

#define STATUSBAR					0
#define TOPBAR						1
#define BOTTOMBAR					2
#define LOG							3
#define TODO						4
#define WEATHER						5
#define BACKUP						6
#define PORTFOLIO					7
#define WIFI						8
#define TIME						9
#define NETWORK						10
#define DISK						11
#define RAM							12
#define LOAD						13
#define CPU_USAGE					14
#define CPU_TEMP					15
#define FAN							16
#define BRIGHTNESS					17
#define VOLUME						18
#define BATTERY						19
#define NUM_FLAGS					20

#define MAX(a, b) a > b ? a : b
#define MIN(a, b) a < b ? a : b

#define SET_FLAG(flag, id) \
	flag ## _flags |= 1UL << id

#define REMOVE_FLAG(flag, id) \
	flag ## _flags &= !(1UL << id)

#define GET_FLAG(flag, id) \
	flag ## _flags & 1UL << id

#define CHECK_LENGTH(link) \
	int new_len = get_length(link); \
	if (link->len != new_len) { \
		*link->bar_len += (new_len - link->len); \
		link->len = new_len; \
		update_all = true; \
	}

#define SIMPLE_ERR(id, val) \
	{ SET_FLAG(err, id); \
	SET_FLAG(updated, id); \
	fprintf(stderr, "%s\t%s\n", asctime(tm_struct), val); \
	perror("\tError"); }

#define ERR(id, val, ret) \
	{ SIMPLE_ERR(id, val) \
	struct string_link *err_link = get_string_link(id); \
	strncpy(err_link->info, error_string, get_length(err_link)); \
	CHECK_LENGTH(err_link) \
	return ret; }

struct data_struct {
	char *data;
	int size;
};

struct disk_usage_struct {
	struct statvfs fs_stat;
	float bytes_used;
	float bytes_total;
	char unit_used;
	char unit_total;
} root_fs;

struct file_link {
	char *filename;
	struct file_link *next;
} *therm_list = NULL, *fan_list = NULL;

struct string_link {
	int id;
	int bar_id;
	char *heading;
	char *info;
	int len;
	int *bar_len;
	struct string_link *next;
} *string_list = NULL;

const char color1 = '';
const char color2 = '';
const char color3 = '';
const char color4 = '';
const char color5 = '';
const char color6 = '';
const char color7 = '';
const char color8 = '';

char statusbar[TOTAL_LENGTH];
int top_length;
int bottom_length;
char error_string[10];

int err_flags = 0;
int updated_flags = 0;
int func_flags = 0;

// singletons
CURL *sb_curl;
struct nl_sock *sb_socket;
int sb_id;
struct nl_msg *sb_msg;
struct nl_cb *sb_cb;
struct rtnl_handle sb_rth;
snd_mixer_elem_t *snd_elem;
snd_mixer_t *handle = NULL;
	
// booleans
bool update_all = false;
bool trunc_TODO = false;
bool internet_connected = false;
bool need_to_get_weather = true;
bool backup_occurring = false;
bool need_equity_previous_close = true;
bool equity_found = false;
bool portfolio_consts_found = false;
bool wifi_connected = false;
bool init_done = false;

int separator;
char weather_url[STRING_LENGTH];
char forecast_url[STRING_LENGTH];
int day_safe;	// due to cJSON's not being thread-safe
int temp_today;
char portfolio_url[STRING_LENGTH];
char token_header[STRING_LENGTH];
char account_number[STRING_LENGTH];
int day_equity_previous_close;
float equity_previous_close = 0.0;
struct curl_slist *headers = NULL;
struct tm *tm_struct = NULL;

// constant init values
int const_devidx;
int const_block_size;
int const_bar_max_len;
int const_cpu_ratio;
int const_temp_max;
int const_fan_min;
int const_fan_max;
int const_screen_brightness_max;
int const_kbd_brightness_max;
float const_vol_range;
