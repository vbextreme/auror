#ifndef __CONFIG_H__
#define __CONFIG_H__

#define DEFAULT_RETRY 3
#define DEFAULT_RELAX 500

#define CONFIG_PATH "/etc/auror.json"
#define DB_PATH     "/var/lib/pacman/sync"
#define LOCK_FILE   "db.lck"
#define CACHE_PATH  "/var/cache/pacman/pkg"
#define LOCAL_PATH  "/var/lib/pacman/local"
#define GPG_PATH    "/etc/pacman.d/gnupg"

typedef struct configOptions{
	char* rootDir;
	char* dbPath;
	char* localDir;
	char* cacheDir;
	char* arch;
	char* lock;
	unsigned long parallel;
	unsigned long retry;
	unsigned long timeout;
	int           aur;
}configOptions_s;

typedef struct configRepository{
	char*        name;
	char*        path;
	char**       mirror;
	char**       server;
}configRepository_s;

typedef struct themeRange{
	double value;
	unsigned color;
}themeRange_s;

typedef struct themeKV{
	char*    name;
	unsigned value;
}themeKV_s;

typedef struct configTheme{
	unsigned*     vcolors;
	unsigned      hcolor;
	unsigned      hsize;
	themeRange_s* speed;
	themeKV_s*    repo;
}configTheme_s;

typedef struct Config{
	configOptions_s     options;
	configRepository_s* repository;
	configTheme_s       theme;
}config_s;

config_s* config_load(const char* path, const char* root);

#endif
