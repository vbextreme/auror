#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <limits.h>
#include <sys/mman.h>
#include <sys/file.h>

#include <notstd/core.h>
#include <notstd/str.h>
#include <notstd/threads.h>

#include <auror/inutility.h>
#include <auror/transaction.h>

__private stack_t SSTK;
__private __aligned(4096) char  SSTACK[SIGSTKSZ];
__private __atomic        int   LOCKACQUIRED = 0;
__private __aligned(4096) char  LOCKFILE[PATH_MAX];

__private void onexit(void){
	dbg_info("normal exit");
	transaction_end();
}

__private void endaction(__unused int signum, siginfo_t* info, __unused void* context){
	dprintf(2, "the ghost got me: %s, game over\n", strsignal(info->si_signo));
	transaction_end();
	_exit(1);
}

__private int pid_exists(unsigned pid){
	char path[128];
	sprintf(path, "/proc/%u/stat", pid);
	int fd = open(path, O_RDONLY);
	if( fd == -1 ) return 0;
	close(fd);
	return 1;
}

void transaction_begin(config_s* conf){
	dbg_info("begin_transaction %s", conf->options.lock);
	unsigned usesignal[] = {
		SIGILL,
		SIGINT,
		SIGQUIT,
		SIGSEGV,
		SIGBUS,
		SIGFPE,
		SIGTERM
	};
	
	iassert(strlen(conf->options.lock) < PATH_MAX);
	strcpy(LOCKFILE, conf->options.lock);
	mprotect(LOCKFILE, sizeof LOCKFILE, PROT_READ);
	
	int fd = open(LOCKFILE, O_CREAT | O_EXCL | O_RDWR, 0600);
	if( fd == -1 ){
		fd = open(LOCKFILE, O_RDWR);
		if( fd == -1 ) die("unable to create db.lck: %m");
		if( flock(fd, LOCK_EX) ) die("unable to lock db.lck, other process still running");
		char buf[512];
		int len = read(fd, buf, 512);
		if( len < 0 ){
			flock(fd, LOCK_UN);
			die("unable to read db.lck: %m");
		}
		if( len > 64 ){
			flock(fd, LOCK_UN);
			die("db.lck probably is corrupted");
		}
		buf[len] = 0;
		if( pid_exists(strtoul(buf, NULL, 10)) ){
			flock(fd, LOCK_UN);
			die("other process still running");
		}
		ftruncate(fd, 0);
	}
	else{
		if( flock(fd, LOCK_EX) ) die("unable to lock db.lck, other process still running");
	}
	if( dprintf(fd, "%u", getpid()) < 0 ) {
		flock(fd, LOCK_UN);
		close(fd);
		unlink(LOCKFILE);
		die("unable to write to db.lck: %m");
	}
	flock(fd, LOCK_UN);
	close(fd);
	LOCKACQUIRED = 1;
	
	atexit(onexit);
	
	SSTK.ss_size  = SIGSTKSZ;
	SSTK.ss_sp    = SSTACK;//mmap(NULL, sstck.ss_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
	SSTK.ss_flags = 0;
	//if( sstck.ss_sp == MAP_FAILED ) die("unable to create signal stack: %m");
	if( sigaltstack(&SSTK, NULL) == -1 ){
		die("sigaltstack error: %m");
	}
	
	struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = endaction;
    sa.sa_flags   = SA_SIGINFO | SA_ONSTACK;
	for( unsigned i = 0; i < sizeof_vector(usesignal); ++i ){
	    sigaction(usesignal[i], &sa, NULL);
	}
}

void transaction_end(void){
	dbg_info("end transaction %s", LOCKFILE);
	if( LOCKACQUIRED ){
		unlink(LOCKFILE);
		LOCKACQUIRED = 0;
	}
}

/*
char* pp_transaction_file(ppConfig_s* conf, const char* name, const char* transaction){
	return str_printf("%s/%s.%s.transaction", conf->options.cacheDir, name, transaction);
}

void pp_transaction_apply(const char* transactionFile, const char* destFile){
	if( rename(transactionFile, destFile) ){
		die("\nunable to apply transaction %s -> %s :: %m",transactionFile, destFile);
	}
}
*/
