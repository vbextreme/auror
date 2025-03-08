#include <notstd/core.h>
#include <notstd/str.h>
#include <notstd/json.h>

#include <auror/inutility.h>
#include <auror/config.h>

__private char* mirror_replace_var(const char* src, const char* repo, const char* arch){
	char* dst = MANY(char, 128);
	const char* var;
   	while( (var=strchr(src, '$')) ){
		dst = mem_upsize(dst, var-src);
		memcpy(&dst[mem_header(dst)->len], src, var-src);
		mem_header(dst)->len += var-src;
		if( !strncmp(var, "$repo", 5) ){
			unsigned const len = strlen(repo);
			dst = mem_upsize(dst, len);
			memcpy(&dst[mem_header(dst)->len], repo, len);
			mem_header(dst)->len += len;
			src = var+5;
		}
		else if( !strncmp(var, "$arch", 5) ){
			unsigned const len = strlen(arch);
			dst = mem_upsize(dst, len);
			memcpy(&dst[mem_header(dst)->len], arch, len);
			mem_header(dst)->len += len;
			src = var+5;
		}
		else{
			die("\nunknow variable in mirrorlist");
		}
	}
	unsigned const len = strlen(src);
	if( len ){
		dst = mem_upsize(dst, len);
		memcpy(&dst[mem_header(dst)->len], src, len);
		mem_header(dst)->len += len;
	}
	return mem_nullterm(dst);
}

__private char* mirror_url(const char* src){
	const char* var = strchr(src, '$');
	if( !var || var - 1 <= src ) die("wrong mirror %s", src);
	return str_dup(src, var-src);
}

__private char** conf_mirror_load(const char* path, const char* repo, const char* arch){
	FILE* f = fopen(path, "r");
	if( !f ) die("unable to open mirror file %s:: %m", path);
	char buf[4096];
	char** ret = MANY(char*, 16);
	while( fgets(buf, 4096, f) ){
		char* st = (char*)str_skip_h(buf);
		if( *st == '#' ) continue;
		if( strncmp(st, "Server", 6) ) continue;
		st = (char*)str_skip_h(st+6);
		if( *st != '=' ) continue;
		st = (char*)str_skip_h(st+1);
		unsigned len = strlen(st);
		while( st[len-1] == ' ' || st[len-1] == '\t' || st[len-1] == '\n' ) --len;
		st[len] = 0;
		unsigned id = mem_ipush(&ret);
		if( !repo || !arch ){
			ret[id] = mirror_url(st);
		}
		else{
			ret[id] = mirror_replace_var(st, repo, arch);
		}
	}
	fclose(f);
	return ret;
}

__private void conf_set(jvalue_s* obj, const char* name, jvtype_e type, void* out, const char* root){
	jvalue_s* tmp = jvalue_property(obj, name);
	if( tmp->type == JV_ERR ) die("missing '%s' in config", name);
	if( tmp->type != type ) die("%s aspected property %s but got %s", name, jvalue_type_to_name(type), jvalue_type_to_name(tmp->type));
	switch( type ){
		default        : die("unaspected type for object %s", name); break;
		case JV_UNUM   : *(long*)out  = tmp->u; break;
		case JV_BOOLEAN: *(int*)out   = tmp->b; break;
		case JV_STRING : 
			if( root ){
				__free char* t = path_explode(tmp->s);
				*(void**)out = path_cat(str_dup(root, 0), t);
			}
			else{
				*(void**)out = mem_borrowed(tmp->s);
			}
		break;
	}
}

__private unsigned* conf_array_unsigned(jvalue_s* array){
	if( array->type != JV_ARRAY ) die("aspected array");
	unsigned const count = *mem_len(array->a);
	unsigned* ret = MANY(unsigned, count);
	*mem_len(ret) = count;
	for( unsigned i = 0; i < count; ++i ){
		switch( array->a[i].type ){
			case JV_UNUM : ret[i] = array->a[i].u; break;
			default      : die("aspected unsgned value but give %s", jvalue_type_to_name(array->a[i].type));
		}
	}
	return ret;
}

__private themeRange_s* conf_range(jvalue_s* range){
	if( range->type != JV_ARRAY ) die("aspected array of range");
	themeRange_s* ret = MANY(themeRange_s, 5);
	mforeach(range->a, i){
		if( range->a[i].type != JV_OBJECT ) die("aspected range object");
		jvalue_s* value = jvalue_property(&range->a[i], "value");
		if( value->type != JV_FLOAT && value->type != JV_UNUM ) die("required range.value as number");
		jvalue_s* color = jvalue_property(&range->a[i], "color");
		if( color->type != JV_UNUM ) die("required range.color as unsigned but give %s", jvalue_type_to_name(color->type));
		unsigned id = mem_ipush(&ret);
		ret[id].value = value->type == JV_UNUM ? value->u : value->f;
		ret[id].color = color->u;
	}
	return ret;
}

__private themeKV_s* conf_kv(jvalue_s* kv){
	if( kv->type != JV_ARRAY ) die("aspected array of kv");
	themeKV_s* ret = MANY(themeKV_s, 5);
	mforeach(kv->a, i){
		if( kv->a[i].type != JV_OBJECT ) die("aspected kv object");
		jvalue_s* key = jvalue_property(&kv->a[i], "key");
		if( key->type != JV_STRING ) die("required key(%s) as string", jvalue_type_to_name(key->type));
		jvalue_s* value = jvalue_property(&kv->a[i], "value");
		if( value->type != JV_UNUM ) die("required value as number but give %s", jvalue_type_to_name(value->type));
		unsigned id = mem_ipush(&ret);
		ret[id].name  = mem_borrowed(key->s);
		ret[id].value = value->u;
	}
	return ret;
}

__private void conf_cleanup(void* pconf){
	config_s* conf = pconf;
	mforeach(conf->repository, i){
		mem_free(conf->repository[i].name);
		mem_free(conf->repository[i].path);
	}
	mem_free(conf->repository);
	mem_free(conf->options.arch);
	mem_free(conf->options.cacheDir);
	mem_free(conf->options.dbPath);
//	mem_free(conf->options.gpgDir);
	mem_free(conf->options.lock);
	mem_free(conf->options.rootDir);
}

config_s* config_load(const char* path, const char* root){
	config_s* conf = NEW(config_s);
	mem_header(conf)->cleanup = conf_cleanup;
	__free char* buf = mem_nullterm(load_file(path, 1));
	__free jvalue_s* jconf = json_decode(buf, NULL, NULL);
	if( !jconf ) die("unable to load config, json error");
	
	jvalue_s* opt = jvalue_property(jconf, "options");
	if( opt->type != JV_OBJECT ) die("option objects not exists");
	conf_set(opt, "arch"    , JV_STRING , &conf->options.arch, NULL);
	conf_set(opt, "parallel", JV_UNUM    , &conf->options.parallel, NULL);
	conf_set(opt, "retry"   , JV_UNUM    , &conf->options.retry, NULL);
	conf_set(opt, "timeout" , JV_UNUM    , &conf->options.timeout, NULL);
	conf_set(opt, "aur"     , JV_BOOLEAN, &conf->options.aur, NULL);
	conf_set(opt, "db"      , JV_STRING , &conf->options.dbPath, root);
	conf_set(opt, "local"   , JV_STRING , &conf->options.localDir, root);
	conf_set(opt, "lock"    , JV_STRING , &conf->options.lock, root);
	conf_set(opt, "cache"   , JV_STRING , &conf->options.cacheDir, root);

	jvalue_s* repo = jvalue_property(jconf, "repository");
	if( repo->type != JV_ARRAY ) die("option repository not exists or is not array");
	conf->repository = MANY(configRepository_s, mem_header(repo->a)->len);
	mem_header(conf->repository)->len = mem_header(repo->a)->len;
	mforeach(repo->a, i){
		if( repo->a[i].type != JV_OBJECT ) die("aspected only object as repository element");
		conf_set(&repo->a[i], "name", JV_STRING, &conf->repository[i].name, NULL);
		conf_set(&repo->a[i], "path", JV_STRING, &conf->repository[i].path, NULL);
		conf->repository[i].mirror = conf_mirror_load(conf->repository[i].path, conf->repository[i].name, conf->options.arch);
		conf->repository[i].server = conf_mirror_load(conf->repository[i].path, NULL, NULL);
	}
	
	jvalue_s* thm = jvalue_property(jconf, "theme");
	if( opt->type != JV_OBJECT ) die("option theme not exists");
	jvalue_s* pro = jvalue_property(thm, "progress");
	if( opt->type != JV_OBJECT ) die("option theme.progress not exists");
	conf->theme.vcolors = conf_array_unsigned(jvalue_property(pro, "vcolors"));
	conf_set(pro, "hcolor", JV_UNUM, &conf->theme.hcolor, NULL);
	conf_set(pro, "hsize" , JV_UNUM, &conf->theme.hsize , NULL);
	jvalue_s* spe = jvalue_property(thm, "speed");
	if( opt->type != JV_OBJECT ) die("option theme.speed not exists");
	conf->theme.speed = conf_range(jvalue_property(spe, "range"));
	conf->theme.repo = conf_kv(jvalue_property(thm, "repository"));

	return conf;
}

