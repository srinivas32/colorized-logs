/* Autoconf patching by David Hedbor, neotron@lysator.liu.se */
/*********************************************************************/
/* file: main.c - main module - signal setup/shutdown etc            */
/*                             TINTIN++                              */
/*          (T)he K(I)cki(N) (T)ickin D(I)kumud Clie(N)t             */
/*                     coded by peter unold 1992                     */
/*********************************************************************/
#include "config.h"
#include <stdlib.h>
#ifdef STDC_HEADERS
# include <string.h>
# include <stdlib.h>
#else
# ifndef HAVE_MEMCPY
#  define memcpy(d, s, n) bcopy ((s), (d), (n))
#  define memmove(d, s, n) bcopy ((s), (d), (n))
# endif
#endif
#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#include <stdarg.h>

#include <signal.h>
#include "tintin.h"
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/resource.h>

#ifndef BADSIG
#define BADSIG (void (*)())-1
#endif

typedef void (*sighandler_t)(int);

/*************** globals ******************/
int term_echoing = TRUE;
int keypad= DEFAULT_KEYPAD;
int puts_echoing = TRUE;
int alnum = 0;
int acnum = 0;
int subnum = 0;
int varnum = 0;
int hinum = 0;
int routnum = 0;
int pdnum = 0;
int antisubnum = 0;
int bindnum = 0;
char homepath[1025];
char E = 27;
int gotpassword=0;
int got_more_kludge=0;
extern int hist_num;
extern int need_resize;
extern int LINES,COLS;
char *tintin_exec;
struct session *lastdraft;
int aborting=0;
extern int recursion;
char *_;

struct session *sessionlist, *activesession, *nullsession;
char **pvars;	/* the %0, %1, %2,....%9 variables */
char tintin_char = DEFAULT_TINTIN_CHAR;
char verbatim_char = DEFAULT_VERBATIM_CHAR;
char prev_command[BUFFER_SIZE];
void tintin(void);
void read_mud(struct session *ses);
void do_one_line(char *line,int nl,struct session *ses);
void snoop(char *buffer, struct session *ses);
void tintin_puts(char *cptr, struct session *ses);
void tintin_puts1(char *cptr, struct session *ses);
void tintin_printf(struct session *ses, const char *format, ...);
void tintin_eprintf(struct session *ses, const char *format, ...);
char status[BUFFER_SIZE];

/************ externs *************/
extern char done_input[BUFFER_SIZE];
extern void user_init(void);
extern int process_kbd(struct session *ses);
extern void textout(char *txt);
extern void user_pause(void);
extern void user_resume(void);

extern int ticker_interrupted, time0;
extern int tick_size, sec_to_tick;

static void myquitsig(void);
extern struct session *newactive_session(void);
extern struct session *parse_input(char *input,int override_verbatim,struct session *ses);
extern struct session *read_command(char *filename, struct session *ses);
extern struct completenode *complete_head;
extern struct listnode *init_list(void);
extern void read_complete(void);
extern void syserr(char *msg);
extern void sigwinch(void);
extern void user_resize(void);
extern char* mystrdup(const char*);
extern int do_one_antisub(char *line, struct session *ses);
extern void prompt(struct session *ses);
extern int check_event(int time, struct session *ses);
extern void check_all_actions(char *line, struct session *ses);
extern void check_all_promptactions(char *line, struct session *ses);
extern void do_all_high(char *line,struct session *ses);
extern void do_all_sub(char *line, struct session *ses);
extern void do_in_MUD_colors(char *txt,int quotetype);
extern void init_bind(void);
extern int isnotblank(char *line,int flag);
extern int match(char *regex, char *string);
extern void textout_draft(char *txt, int flag);
extern void user_done(void);
extern void user_passwd(int x);
extern int iscompleteprompt(char *line);
void echo_input(char *txt);
extern struct hashtable* init_hash();
extern void init_parse();

#ifdef HAVE_TIME_H
#include <time.h>
#endif
extern void do_history(char *buffer, struct session *ses);
extern int read_buffer_mud(char *buffer, struct session *ses);
extern void cleanup_session(struct session *ses);

void tstphandler(int sig)
{
    user_pause();
    kill(getpid(), SIGSTOP);
}

void sigcont(void)
{
    user_resume();
}

void sigchild(void)
{
    while (waitpid(-1,0,WNOHANG)>0);
}

void sigsegv(void)
{
    user_done();
    write(2,"Segmentation fault.\n",20);
    exit(11);
}

void sigfpe(void)
{
    user_done();
    write(2,"Floating point exception.\n",26);
    exit(8);
}

int new_news(void)
{
    struct stat KBtin, news;
    
    if (stat(tintin_exec, &KBtin))
        return 0;       /* can't stat the executable??? */
    if (stat(NEWS_FILE, &news))
#ifdef DATA_PATH
        if (stat(DATA_PATH "/" NEWS_FILE, &news))
#endif
            return 0;       /* either no NEWS file, or can't stat it */
    return (news.st_ctime>=news.st_atime)||(KBtin.st_ctime+10>news.st_atime);
}

/************************/
/* the #suspend command */
/************************/
void suspend_command(char *arg, struct session *ses)
{
    tstphandler(SIGTSTP);
}

void setup_signals(void)
{
    struct sigaction act;
    sigemptyset(&act.sa_mask);
    act.sa_flags=SA_RESTART;

    if (signal(SIGTERM, (sighandler_t)myquitsig) == BADSIG)
        syserr("signal SIGTERM");
    if (signal(SIGQUIT, (sighandler_t)myquitsig) == BADSIG)
        syserr("signal SIGQUIT");
    if (signal(SIGINT, (sighandler_t)myquitsig) == BADSIG)
        syserr("signal SIGINT");
    act.sa_handler=(sighandler_t)tstphandler;
    if (sigaction(SIGTSTP,&act,0))
        syserr("sigaction SIGTSTP");
    act.sa_handler=(sighandler_t)sigcont;
    if (sigaction(SIGCONT,&act,0))
        syserr("sigaction SIGCONT");
    act.sa_handler=(sighandler_t)sigwinch;
    if (sigaction(SIGWINCH,&act,0))
        syserr("sigaction SIGWINCH");
    act.sa_handler=(sighandler_t)sigchild;
    if (sigaction(SIGCHLD,&act,0))
        syserr("sigaction SIGCHLD");
    if (signal(SIGSEGV,(sighandler_t)sigsegv) == BADSIG)
        syserr("signal SIGSEGV");
    if (signal(SIGFPE,(sighandler_t)sigfpe) == BADSIG)
        syserr("signal SIGFPE");
    if (signal(SIGPIPE, SIG_IGN) == BADSIG)
        syserr("signal SIGPIPE");
}

/****************************************/
/* attempt to assure enough stack space */
/****************************************/
void setup_ulimit(void)
{
    struct rlimit rlim;

    if (getrlimit(RLIMIT_STACK,&rlim))
        return;
    if ((unsigned int)rlim.rlim_cur>=STACK_LIMIT)
        return;
    if (rlim.rlim_cur==rlim.rlim_max)
        return;
    if ((unsigned int)rlim.rlim_max<STACK_LIMIT)
        rlim.rlim_cur=rlim.rlim_max;
    else
        rlim.rlim_cur=STACK_LIMIT;
    setrlimit(RLIMIT_STACK,&rlim);
}

/**************************************************************************/
/* main() - show title - setup signals - init lists - readcoms - tintin() */
/**************************************************************************/
int main(int argc, char **argv, char **environ)
{
    struct session *ses;
    char *strptr, temp[BUFFER_SIZE];
    int arg_num;
    int fd;

    tintin_exec=argv[0];
    init_bind();
    init_parse();
    strcpy(status,EMPTY_LINE);
    user_init();
    /*  read_complete();		no tab-completion */
    hist_num=-1;
    ses = NULL;
    srand(getpid()^time(0));
    lastdraft=0;

    tintin_printf(0,"~2~##################################################");
    tintin_printf(0, "#~7~                ~12~K B ~3~t i n~7~     v %-15s ~2~#", VERSION_NUM);
    tintin_printf(0,"#~7~ current developer: ~9~Adam Borowski               ~2~#");
    tintin_printf(0,"#~7~                        (~9~kilobyte@mimuw.edu.pl~7~) ~2~#");
    tintin_printf(0,"#~7~ based on ~12~tintin++~7~ v 2.1.9 by Peter Unold,      ~2~#");
    tintin_printf(0,"#~7~  Bill Reiss, David A. Wagner, Joann Ellsworth, ~2~#");
    tintin_printf(0,"#~7~     Jeremy C. Jack, Ulan@GrimneMUD and         ~2~#");
    tintin_printf(0,"#~7~  Jacek Narebski (jnareb@jester.ds2.uw.edu.pl)  ~2~#");
    tintin_printf(0,"##################################################~7~");
    tintin_printf(0,"~15~#session <name> <host> <port> ~7~to connect to a remote server");
    tintin_printf(0,"                              ~8~#ses t2t towers.angband.com 9999");
    tintin_printf(0,"~15~#run <name> <command>         ~7~to run a local command");
    tintin_printf(0,"                              ~8~#run advent adventure");
    tintin_printf(0,"                              ~8~#run sql mysqlclient");
    tintin_printf(0,"~15~#help                         ~7~to get the help index");
    if (new_news())
        tintin_printf(ses,"Check #news now!");

    setup_signals();
    setup_ulimit();
    time0 = time(NULL);

    nullsession=(struct session *)malloc(sizeof(struct session));
    nullsession->name=mystrdup("main");
    nullsession->address=0;
    nullsession->tickstatus = FALSE;
    nullsession->tick_size = DEFAULT_TICK_SIZE;
    nullsession->time0 = 0;
    nullsession->snoopstatus = TRUE;
    nullsession->logfile = NULL;
    nullsession->blank = DEFAULT_DISPLAY_BLANK;
    nullsession->echo = DEFAULT_ECHO;
    nullsession->speedwalk = DEFAULT_SPEEDWALK;
    nullsession->togglesubs = DEFAULT_TOGGLESUBS;
    nullsession->presub = DEFAULT_PRESUB;
    nullsession->verbatim = 0;
    nullsession->ignore = DEFAULT_IGNORE;
    nullsession->aliases = init_hash();
    nullsession->actions = init_list();
    nullsession->prompts = init_list();
    nullsession->subs = init_list();
    nullsession->myvars = init_hash();
    nullsession->highs = init_list();
    nullsession->pathdirs = init_hash();
    nullsession->socket = 0;
    nullsession->issocket = 0;
    nullsession->naws = 0;
    nullsession->server_echo = 0;
    nullsession->antisubs = init_list();
    nullsession->binds = init_hash();
    nullsession->socketbit = 0;
    nullsession->next = 0;
    nullsession->sessionstart=nullsession->idle_since=time(0);
    nullsession->debuglogfile=0;
    {
        int i;
        for (i=0;i<HISTORY_SIZE;i++)
            nullsession->history[i]=0;
        for (i=0;i<MAX_LOCATIONS;i++)
        {
            nullsession->routes[i]=0;
            nullsession->locations[i]=0;
        }
    };
    nullsession->path = init_list();
    nullsession->no_return = 0;
    nullsession->path_length = 0;
    nullsession->last_line[0] = 0;
    nullsession->events = NULL;
    nullsession->verbose=0;
    sessionlist = nullsession;
    activesession = nullsession;
    pvars=0;

    nullsession->mesvar[0] = DEFAULT_ALIAS_MESS;
    nullsession->mesvar[1] = DEFAULT_ACTION_MESS;
    nullsession->mesvar[2] = DEFAULT_SUB_MESS;
    nullsession->mesvar[3] = DEFAULT_EVENT_MESS;
    nullsession->mesvar[4] = DEFAULT_HIGHLIGHT_MESS;
    nullsession->mesvar[5] = DEFAULT_VARIABLE_MESS;
    nullsession->mesvar[6] = DEFAULT_ROUTE_MESS;
    nullsession->mesvar[7] = DEFAULT_GOTO_MESS;
    nullsession->mesvar[8] = DEFAULT_BIND_MESS;
    nullsession->mesvar[9] = DEFAULT_SYSTEM_MESS;
    nullsession->mesvar[10]= DEFAULT_PATH_MESS;
    nullsession->mesvar[11]= DEFAULT_ERROR_MESS;

    *homepath = '\0';
    if (!strcmp(DEFAULT_FILE_DIR, "HOME"))
        if ((strptr = (char *)getenv("HOME")))
            strcpy(homepath, strptr);
        else
            *homepath = '\0';
    else
        strcpy(homepath, DEFAULT_FILE_DIR);
    arg_num = 1;
    if (argc > 1 && argv[1])
    {
        if (*argv[1] == '-' && *(argv[1] + 1) == 'v')
        {
            arg_num = 2;
            nullsession->verbose = TRUE;
        }
    }
    activesession=nullsession;
    if (argc > arg_num)
        while (argc>arg_num)
        {
            tintin_printf(0, "#READING {%s}", argv[arg_num]);
            activesession = read_command(argv[arg_num++], activesession);
        }
    else
    {
        strcpy(temp, homepath);
        strcat(temp, "/.tintinrc");
        if ((fd = open(temp, O_RDONLY)) > 0)
        {                   	/* Check if it exists */
            close(fd);
            activesession = read_command(temp, activesession);
        }
        else
        {
            if ((strptr = (char *)getenv("HOME")))
            {
                strcpy(homepath, strptr);
                strcpy(temp, homepath);
                strcat(temp, "/.tintinrc");
                if ((fd = open(temp, O_RDONLY)) > 0)
                {	                /* Check if it exists */
                    close(fd);
                    activesession = read_command(temp, nullsession);
                }
            }
        }
    }
    tintin();
    return 0;
}

/******************************************************/
/* return seconds to next tick (global, all sessions) */
/* also display tick messages                         */
/******************************************************/
int check_events(void)
{
    struct session *sp;
    int tick_time = 0, curr_time, tt;

    curr_time = time(NULL);
    for (sp = sessionlist; sp; sp = sp->next)
    {
        tt = check_event(curr_time, sp);
        /* printf("#%s %d(%d)\n", sp->name, tt, curr_time); */
        if (tt > curr_time && (tick_time == 0 || tt < tick_time))
            tick_time = tt;
    }

    if (tick_time > curr_time)
        return (tick_time - curr_time);
    return (0);
}

/***************************/
/* the main loop of tintin */
/***************************/
void tintin(void)
{
    int done, readfdmask, result;
    struct session *sesptr, *t;
    struct timeval tv;

    while (TRUE)
    {
        readfdmask = 1;
        for (sesptr = sessionlist; sesptr; sesptr = sesptr->next)
            readfdmask |= sesptr->socketbit;

        tv.tv_sec = check_events();
        tv.tv_usec = 0;

        result = select(32, (fd_set *)&readfdmask, 0, 0, &tv);

        if (need_resize)
        {
            char buf[BUFFER_SIZE];
            
            user_resize();
            sprintf(buf, "#NEW SCREEN SIZE: %dx%d.", COLS, LINES);
            tintin_puts1(buf, activesession);
        }

        if (result == 0)
            continue;
        else if (result < 0 && errno == EINTR)
            continue;   /* Interrupted system call */
        else if (result < 0)
            syserr("select");

        if (readfdmask & 1)
        {
            done = process_kbd(activesession);
            if (done)
            {
                hist_num=-1;
                if (term_echoing || (got_more_kludge && done_input[0]))
                    /* got_more_kludge: echo any non-empty line */
                {
                    if (activesession && *done_input)
                        if (strcmp(done_input, prev_command))
                            do_history(done_input, activesession);
                    if (activesession->echo)
                        echo_input(done_input);
                }
                if (*done_input)
                    strcpy(prev_command, done_input);
                aborting=0;
                activesession = parse_input(done_input,0,activesession);
                recursion=0;
            }
        }
        for (sesptr = sessionlist; sesptr; sesptr = t)
        {
            t = sesptr->next;
            if (sesptr->socketbit & readfdmask)
            {
                aborting=0;
                read_mud(sesptr);
            }
        }
        if (activesession->server_echo
            && (2-activesession->server_echo != gotpassword))
        {
            gotpassword= 2-activesession->server_echo;
            if (!gotpassword)
                got_more_kludge=0;
            user_passwd(gotpassword && !got_more_kludge);
            term_echoing=!gotpassword;
        }
    }
}


/*************************************************************/
/* read text from mud and test for actions/snoop/substitutes */
/*************************************************************/
void read_mud(struct session *ses)
{
    char buffer[BUFFER_SIZE], linebuffer[BUFFER_SIZE], *cpsource, *cpdest;
    char temp[BUFFER_SIZE];
    int didget, n, count;

    if ((didget = read_buffer_mud(buffer, ses))==-1)
    {
        cleanup_session(ses);
        if (ses == activesession)
            activesession = newactive_session();
        return;
    }
    else
    {
        if (!didget)
            return; /* possible if only telnet protocol data was received */
    }
    if (ses->logfile)
    {
        if (!OLD_LOG)
        {
            count = 0;
            for (n = 0; n < didget; n++)
                if (buffer[n] != '\r')
                    temp[count++] = buffer[n];
            if (fwrite(temp, count, 1, ses->logfile)<1)
            {
abort_log:
                ses->logfile=0;
                tintin_eprintf(ses, "#WRITE ERROR -- LOGGING DISABLED.  Disk full?");
            }
        }
        else
            if (fwrite(buffer, didget, 1, ses->logfile)<didget)
                goto abort_log;
    }

    cpsource = buffer;
    strcpy(linebuffer, ses->last_line);
    cpdest = strchr(linebuffer,'\0');

    while (*cpsource)
    {		/*cut out each of the lines and process */
        if (*cpsource == '\n')
        {
            *cpdest = '\0';
            do_one_line(linebuffer,1,ses);

            cpsource++;
            *(cpdest = linebuffer)=0;
        }
        else
            if (*cpsource=='\r')
                cpsource++;
            else
                *cpdest++ = *cpsource++;
    }
    if (cpdest-linebuffer>INPUT_CHUNK) /* let's split too long lines */
    {
        *cpdest=0;
        do_one_line(linebuffer,1,ses);
        cpdest=linebuffer;
    }
    *cpdest = '\0';
    strcpy(ses->last_line,linebuffer);
    if (!ses->more_coming)
        if (cpdest!=linebuffer)
            do_one_line(linebuffer,0,ses);
}
/**********************************************************/
/* do all of the functions to one line of buffer          */
/**********************************************************/
void do_one_line(char *line,int nl,struct session *ses)
{
    int isnb;

    switch (ses->server_echo)
    {
    case 0:
        if (match(PROMPT_FOR_PW_TEXT,line) && !gotpassword)
        {
            gotpassword=1;
            user_passwd(1);
            term_echoing=FALSE;
        };
        break;
    case 1:
        if (match(PROMPT_FOR_MORE_TEXT,line))
        {
            user_passwd(0);
            got_more_kludge=1;
        };
    };
    _=line;
    do_in_MUD_colors(line,0);
    isnb=isnotblank(line,0);
    if (!ses->ignore && (nl||isnb))
        check_all_promptactions(line, ses);
    if (nl && !ses->presub && !ses->ignore)
        check_all_actions(line, ses);
    if (!ses->togglesubs && (nl||isnb) && !do_one_antisub(line, ses))
        do_all_sub(line, ses);
    if (nl && ses->presub && !ses->ignore)
        check_all_actions(line, ses);
    if (isnb&&!ses->togglesubs)
        do_all_high(line, ses);
    if (isnotblank(line,ses->blank))
    {
        if (ses==activesession)
        {
            if (nl)
            {
                if (!activesession->server_echo)
                    gotpassword=0;
                sprintf(strchr(line,0),"\n");
                textout_draft(0, 0);
                textout(line);
                lastdraft=0;
            }
            else
            {
                isnb = ses->gas ? ses->ga : iscompleteprompt(line);
#ifdef PARTIAL_LINE_MARKER
                sprintf(strchr(line,0),PARTIAL_LINE_MARKER);
#endif
                textout_draft(line, isnb);
                lastdraft=ses;
            }
        }
        else
            if (ses->snoopstatus)
                snoop(line,ses);
    }
    _=0;
}

/**********************************************************/
/* snoop session ses - chop up lines and put'em in buffer */
/**********************************************************/
void snoop(char *buffer, struct session *ses)
{
    tintin_printf(0,"%s%% %s\n",ses->name,buffer);
}

/*****************************************************/
/* output to screen should go throught this function */
/* text gets checked for actions                     */
/*****************************************************/
void tintin_puts(char *cptr, struct session *ses)
{
    char line[BUFFER_SIZE];
    strcpy(line,cptr);
    if (ses)
    {
        _=line;
        check_all_actions(line, ses);
        _=0;
    }
    tintin_printf(ses,line);
}

/*****************************************************/
/* output to screen should go throught this function */
/* text gets checked for substitutes and actions     */
/*****************************************************/
void tintin_puts1(char *cptr, struct session *ses)
{
    char line[BUFFER_SIZE];

    strcpy(line,cptr);

    _=line;
    if (!ses->presub && !ses->ignore)
        check_all_actions(line, ses);
    if (!ses->togglesubs)
        if (!do_one_antisub(line, ses))
            do_all_sub(line, ses);
    if (ses->presub && !ses->ignore)
        check_all_actions(line, ses);
    if (!ses->togglesubs)
        do_all_high(line, ses);
    if (isnotblank(line,ses->blank))
        if (ses==activesession)
        {
            cptr=strchr(line,0);
            if (cptr-line>=BUFFER_SIZE-2)
                cptr=line+BUFFER_SIZE-2;
            cptr[0]='\n';
            cptr[1]=0;
            textout(line);
        }
    _=0;
}

void tintin_printf(struct session *ses, const char *format, ...)
{
    va_list ap;
#ifdef HAVE_VSNPRINTF
    char buf[BUFFER_SIZE];
#else
    char buf[BUFFER_SIZE*4]; /* let's hope this will never overflow... */
#endif

    if ((ses == activesession || ses == nullsession || !ses) && puts_echoing)
    {
        va_start(ap, format);
#ifdef HAVE_VSNPRINTF
        if (vsnprintf(buf, BUFFER_SIZE-1, format, ap)>BUFFER_SIZE-2)
            buf[BUFFER_SIZE-3]='>';
#else
        vsprintf(buf, format, ap);
#endif
        va_end(ap);
        strcat(buf, "\n");
        textout(buf);
    }
}

void tintin_eprintf(struct session *ses, const char *format, ...)
{
    va_list ap;
#ifdef HAVE_VSNPRINTF
    char buf[BUFFER_SIZE];
#else
    char buf[BUFFER_SIZE*4]; /* let's hope this will never overflow... */
#endif

    /* note: the behavior on !ses is wrong */
    if ((ses == activesession || ses == nullsession || !ses) && (puts_echoing||!ses||ses->mesvar[11]))
    {
        va_start(ap, format);
#ifdef HAVE_VSNPRINTF
        if (vsnprintf(buf, BUFFER_SIZE-1, format, ap)>BUFFER_SIZE-2)
            buf[BUFFER_SIZE-3]='>';
#else
        vsprintf(buf, format, ap);
#endif
        va_end(ap);
        strcat(buf, "\n");
        textout(buf);
    }
}

void echo_input(char *txt)
{
    char out[BUFFER_SIZE],*cptr,*optr;
    
    optr=out;
    cptr=txt;
    while (*cptr)
    {
        if ((*optr++=*cptr++)=='~')
            optr+=sprintf(optr, "~:~");
        if (optr-out > BUFFER_SIZE-10)
            break;
    }
    *optr++='\n';
    *optr=0;
    textout(out);
}

/**********************************************************/
/* Here's where we go when we wanna quit TINTIN FAAAAAAST */
/**********************************************************/
static void myquitsig(void)
{
    struct session *sesptr, *t;

    for (sesptr = sessionlist; sesptr; sesptr = t)
    {
        t = sesptr->next;
        if (sesptr!=nullsession)
            cleanup_session(sesptr);
    }
    sesptr = NULL;

    textout("~7~\n");
    textout("Your fireball hits TINTIN with full force, causing an immediate death.\n");
    textout("TINTIN is dead! R.I.P.\n");
    textout("Your blood freezes as you hear TINTINs death cry.\n");
    user_done();
    exit(0);
}
