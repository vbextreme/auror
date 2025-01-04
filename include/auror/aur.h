#ifndef __AUR_H__
#define __AUR_H__

#include <notstd/core.h>
#include <notstd/json.h>
#include <notstd/fzs.h>
#include <notstd/list.h>

#include <auror/www.h>
#include <auror/pkgdesc.h>
#include <auror/pacman.h>

#define AUR_DB_NAME "aur"
#define AUR_URL     "https://aur.archlinux.org"

#ifdef AUR_IMPLEMENT
#include <notstd/field.h>
#endif

#define PKGINFO_FLAG_DEPENDENCY       0x00010000
#define PKGINFO_FLAG_BUILD_DEPENDENCY 0x00020000

#define SYNC_REINSTALL                0x10000000

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
	__prv8 restapi_s ra;
}aur_s;

aur_s* aur_ctor(aur_s* aur);
aur_s* aur_dtor(aur_s* aur);
ddatabase_s* aur_search(aur_s* aur, const char* name, fzs_s** matchs);
ddatabase_s* aur_search_test(jvalue_s* jret, const char* name, fzs_s** matchs);

void aur_dependency_resolve(aur_s* aur, pacman_s* pacman, aurSync_s* sync, pkgInfo_s* parent, char** name, unsigned flags);
//aur dir cache : ~/.cache/auror
//aur download 
//

#endif
