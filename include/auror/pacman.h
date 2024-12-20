#ifndef __PACMAN_H__
#define __PACMAN_H__

#include <notstd/core.h>
#include <notstd/fzs.h>

#include <auror/pkgdesc.h>
#include <auror/ini.h>

#define PACMAN_LOCAL_INSTALL "/var/lib/pacman/local"
#define PACMAN_LOCAL_DB      "/var/lib/pacman/sync"
#define PACMAN_CONFIG        "/etc/pacman.conf"

#define PACMAN_DB_CUSTOM_NAME  "custom"
#define PACMAN_DB_CUSTOM_URL   ""
#define PACMAN_DB_INSTALL_NAME "install"

#define DB_FLAG_UPSTREAM     0x01
#define PKG_FLAG_INSTALL     0x01


#define PACMAN_UPGRADE       "sudo pacman -Syu"

typedef struct pacman{
	ddatabase_s** db;
	ini_s         config;
	char*         dbLocalPath;
	unsigned      iddbaur;
	unsigned      iddbinstall;
}pacman_s;

void pacman_ctor(pacman_s* pacman);
void pacman_upgrade(void);
void pacman_search(pacman_s* pacman, const char* name, fzs_s** matchs);
void pacman_scan_installed(pacman_s* pacman, ddatabase_s* db);

#endif
