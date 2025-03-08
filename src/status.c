#include <auror/inutility.h>
#include <auror/status.h>
#include <auror/jobs.h>
#include <auror/config.h>

#include <signal.h>

__private __atomic int RESIZE;
__private status_s* STATUS;

__private void draw_completed(status_s* status){
	unsigned const x   = *mem_len(status->available) + 1;
	term_gotoxy(x, status->line);
	bold_set();
	putchar('[');
	colorfg_set(0);
	colorfg_set(status->conf->theme.hcolor);
	term_hhbar(status->conf->theme.hsize, (status->completed*100.0)/status->total);
	colorfg_set(0);
	bold_set();
	putchar(']');
	colorfg_set(0);
	term_gotoxy(x, status->line+1);
	printf("[%5d/%5d]", status->completed, status->total);
}

__private void draw_new(status_s* status){
	unsigned const total = *mem_len(status->available);
	unsigned const vcol  = *mem_len(status->conf->theme.vcolors);
	term_gotoxy(0, status->line+1);
	for( unsigned i = 0; i < total; ++i ){
		colorfg_set(status->conf->theme.vcolors[i % vcol]);
		fputs("⌂", stdout);
	}
	colorbg_set(0);
}

__private void draw_desc(status_s* status){
	unsigned const x = *mem_len(status->available) + status->conf->theme.hsize + 4;
	term_gotoxy(x, status->line);
	printf("%s", status->desc);
}

__private void status_term_size_change(status_s* status){
	if( !RESIZE ) return;
	RESIZE = 0;
	unsigned nl = term_scroll_begin(2);
//	dbg_info("new status: %u old status: %u", nl, status->line);
	term_cursor_store();
	if( nl < status->line ){
		term_gotoxy(0, nl+1);
		term_line_cls();
	}
	else {
		term_gotoxy(0, status->line);
		term_line_cls();
		term_gotoxy(0, status->line+1);
		term_line_cls();
	}
	status->line = nl;
	draw_completed(status);
	draw_new(status);
	draw_desc(status);
	term_cursor_load();
	fflush(stdout);
}

__private void sig_resize(__unused int sig, __unused siginfo_t* sinfo, __unused void* context){
	RESIZE = 1;
	if( !mutex_trylock(&STATUS->lock) ) return;
	status_term_size_change(STATUS);
	mutex_unlock(&STATUS->lock);
}

__private void sig_attach(void){
	struct sigaction sa;
	memset(&sa, 0, sizeof(struct sigaction));
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = sig_resize;
	sa.sa_flags   = SA_SIGINFO;
	sigaction(SIGWINCH, &sa, NULL);	
}

status_s* status_ctor(status_s* status, config_s* conf, unsigned maxjobs){
	mutex_ctor(&status->lock);
	status->conf      = conf;
	status->completed = 0;
	status->speed     = 0;
	status->total     = 0;
	status->lastsync  = time_ms();
	status->available = MANY(int, maxjobs);
	mem_zero(status->available);
	*mem_len(status->available) = maxjobs;
	RESIZE = 0;
	STATUS = status;
	status->line =  term_scroll_begin(2);
	sig_attach();
	mlock(&status->lock){
		term_cursor_store();
		draw_new(status);
		term_cursor_load();
		fflush(stdout);
	}
	return status;
}

status_s* status_dtor(status_s* status){
	mem_free(status->available);
	term_cursor_store();
	for( unsigned i = 0; i < 2; ++i ){
		term_gotoxy(0, status->line+i);
		term_line_cls();
	}
	term_cursor_load();
	term_scroll_end();
	colorfg_set(0);
	puts("");
	return status;
}

unsigned status_new_id(status_s* status){
	__mlock mutex_t* lock = &status->lock;
	mutex_lock(lock);
	mforeach(status->available, i){
		if( !status->available[i] ){
			status->available[i] = 1;
			return i;
		}
	}
	die("internal error, to many jobs");
}

void status_refresh(status_s* status, unsigned id, double value, status_e state){
	static char* STATEMAP[] = { "⌂", "↓", "¤", "●", "✓"};
	unsigned color = status->conf->theme.vcolors[id % *mem_len(status->conf->theme.vcolors)];
	mlock(&status->lock){
		term_cursor_store();
		term_gotoxy(id, status->line);
		colorfg_set(color);
		term_vbar(value);
		colorfg_set(0);
		term_gotoxy(id, status->line+1);
		colorfg_set(color);
		fputs(STATEMAP[state], stdout);
		colorfg_set(0);
		term_cursor_load();
		fflush(stdout);
		status_term_size_change(status);
	}
}

void status_speed(status_s* status, double mib){
	unsigned const x = *mem_len(status->available) + status->conf->theme.hsize + 9;
	mlock(&status->lock){
		delay_t now = time_ms();
		if( now - status->lastsync > 1000 ){
			term_cursor_store();
			term_gotoxy(x, status->line+1);
			printf("%5.1fMiB", status->speed);
			term_cursor_load();
			fflush(stdout);
			status->speed = mib;
			status->lastsync = now;
		}
		else{
			status->speed += mib;
		}
		status_term_size_change(status);
	}
}

void status_completed(status_s* status, unsigned id){
	status_refresh(status, id, 0, STATUS_TYPE_COMPLETE);
	mlock(&status->lock){
		++status->completed;
		status->available[id] = 0;
		term_cursor_store();
		draw_completed(status);
		term_cursor_load();
		fflush(stdout);
		status_term_size_change(status);
	}
}

void status_description(status_s* status, const char* desc){
	mlock(&status->lock){
		strcpy(status->desc, desc);
		term_cursor_store();
		draw_desc(status);
		term_cursor_load();
		fflush(stdout);
		status_term_size_change(status);
	}
}



