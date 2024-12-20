#ifndef __AUR_H__
#define __AUR_H__

#include <notstd/core.h>
#include <notstd/json.h>

#include <auror/www.h>

#include <notstd/fzs.h>

#include <auror/pkgdesc.h>

#define AUR_DB_NAME "aur"
#define AUR_URL     "https://aur.archlinux.org"

#ifdef AUR_IMPLEMENT
#include <notstd/field.h>
#endif

typedef struct aur{
	__prv8 restapi_s ra;
}aur_s;

aur_s* aur_ctor(aur_s* aur);
aur_s* aur_dtor(aur_s* aur);
ddatabase_s* aur_search(aur_s* aur, const char* name, fzs_s** matchs);

ddatabase_s* aur_search_test(jvalue_s* jret, const char* name, fzs_s** matchs);

//aur dir cache : ~/.cache/auror
//aur download 
//

#endif
