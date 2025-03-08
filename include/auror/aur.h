#ifndef __AUR_H__
#define __AUR_H__

#include <notstd/core.h>
#include <notstd/json.h>
#include <notstd/fzs.h>
#include <notstd/list.h>

#include <auror/www.h>
#include <auror/database.h>

//#include <auror/pacman.h>

#define AUR_DB_NAME "aur"
#define AUR_URL     "https://aur.archlinux.org"
#define AUR_RESTAPI AUR_URL "/rpc/v5"

#ifdef AUR_IMPLEMENT
#include <notstd/field.h>
#endif

//#define PKGINFO_FLAG_DEPENDENCY       0x00010000
//#define PKGINFO_FLAG_BUILD_DEPENDENCY 0x00020000

//#define SYNC_REINSTALL                0x10000000

typedef struct pkgInfo{
	inherit_ld(struct pkgInfo);
	struct pkgInfo* parent;
	struct pkgInfo* deps;
	char*           name;
	unsigned        flags;
}pkgInfo_s;

typedef struct aurSync{
	pkgInfo_s* pkg;
}aurSync_s;

typedef struct aur{
	__prv8 www_s w;
}aur_s;

aur_s* aur_ctor(aur_s* aur);
void aur_dtor(void* paur);
void aur_search(aur_s* aur, arch_s* arch, const char* name);

#endif
