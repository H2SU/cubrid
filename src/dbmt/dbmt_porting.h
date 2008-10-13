/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * porting.h - 
 */

#ifndef	_DBMT_PORTING_H_
#define	_DBMT_PORTING_H_

#ident "$Id"

/*
 * IMPORTED SYSTEM HEADER FILES
 */

#include <errno.h>

#ifndef WIN32
#include <sys/types.h>
#include <sys/socket.h>
#endif

/*
 * IMPORTED OTHER HEADER FILES
 */

/*
 * EXPORTED DEFINITIONS
 */
#ifdef WIN32
#define PATH_MAX	256
#define NAME_MAX	256
#endif

#define MOVE_FILE(SRC_FILE, DEST_FILE)	\
	(unlink(DEST_FILE) || 1 ? rename(SRC_FILE, DEST_FILE) : -1)

#ifdef WIN32
#define CLOSE_SOCKET(X)		if (X >= 0) closesocket(X)
#else
#define CLOSE_SOCKET(X)		if (X >= 0) close(X)
#endif

#ifdef WIN32
#define	mkdir(dir, mode)	_mkdir(dir)
#define getpid()		_getpid()
#define O_RDONLY		_O_RDONLY
#define strcasecmp(str1, str2)	_stricmp(str1, str2)
#define snprintf		_snprintf

#define R_OK			4
#define W_OK			2
#define F_OK			0
#define X_OK			F_OK
#endif

#ifdef WIN32
#define SLEEP_SEC(X)			Sleep((X) * 1000)
#define SLEEP_MILISEC(sec, msec)	Sleep((sec) * 1000 + (msec))
#else
#define SLEEP_SEC(X)			sleep(X)
#define	SLEEP_MILISEC(sec, msec)	\
			do {		\
			  struct timeval sleep_time_val;		\
			  sleep_time_val.tv_sec = sec;			\
			  sleep_time_val.tv_usec = (msec) * 1000;	\
			  select(0, 0, 0, 0, &sleep_time_val);		\
			} while(0)
#endif

#ifdef WIN32
#define TIMEVAL_MAKE(X)		_ftime(X)
#define TIMEVAL_GET_SEC(X)	((int) ((X)->time))
#define TIMEVAL_GET_MSEC(X)	((int) ((X)->millitm))
#else
#define TIMEVAL_MAKE(X)		gettimeofday(X, NULL)
#define TIMEVAL_GET_SEC(X)	((int) ((X)->tv_sec))
#define TIMEVAL_GET_MSEC(X)	((int) (((X)->tv_usec) / 1000))
#endif

#ifdef WIN32
#define MUTEX_INIT(MUTEX)				\
	do {						\
	  MUTEX = CreateMutex(NULL, FALSE, NULL);	\
	} while (0)
#elif HPUX10_2
#define	MUTEX_INIT(MUTEX)				\
	pthread_mutex_init(MUTEX, pthread_mutexattr_default);
#else
#define MUTEX_INIT(MUTEX) 	pthread_mutex_init(MUTEX, NULL)
#endif

#ifdef WIN32
#define THREAD_BEGIN(THR_ID, FUNC, ARG)				\
	do {							\
	  THR_ID = _beginthread(FUNC, 0, (void*) (ARG));	\
	} while(0)
#elif HPUX10_2
#define	THREAD_BEGIN(THR_ID, FUNC, ARG)			\
	do {						\
	  pthread_create(&(THR_ID), pthread_attr_default, FUNC, ARG);	\
	  pthread_detach(&(THR_ID));			\
	} while (0)
#elif UNIXWARE7
#define THREAD_BEGIN(THR_ID, FUNC, ARG)		\
	do {					\
	  pthread_attr_t	thread_attr;	\
	  pthread_attr_init(&thread_attr);	\
	  pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED); \
	  pthread_attr_setstacksize(&thread_attr, 100 * 1024);	\
	  pthread_create(&(THR_ID), &thread_attr, FUNC, ARG);	\
	} while (0)
#else
#define THREAD_BEGIN(THR_ID, FUNC, ARG)		\
	do {					\
	  pthread_attr_t	thread_attr;	\
	  pthread_attr_init(&thread_attr);	\
	  pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED); \
	  pthread_create(&(THR_ID), &thread_attr, FUNC, ARG);	\
	} while (0)
#endif

#define FREE_MEM(PTR)		\
	do {			\
	  if (PTR) {		\
	    free(PTR);		\
	    PTR = 0;	\
	  }			\
	} while (0)

#define REALLOC(PTR, SIZE)	\
	(PTR == NULL) ? malloc(SIZE) : realloc(PTR, SIZE)

#ifdef UNIXWARE7
#define PROC_FORK()	fork1()
#else
#define PROC_FORK()	fork()
#endif

/*
 * EXPORTED TYPE DEFINITIONS
 */

#ifdef WIN32
typedef struct _timeb T_TIMEVAL;
#else
typedef struct timeval T_TIMEVAL;
#endif

#if defined(WIN32) || defined(SOLARIS) || defined(HPUX)
typedef int T_SOCKLEN;
#elif defined(UNIXWARE7)
typedef size_t T_SOCKLEN;
#else
typedef socklen_t T_SOCKLEN;
#endif

#ifndef WIN32
typedef int SOCKET;
#endif

/*
 * EXPORTED DEFINITIONS
 */

#ifdef WIN32
#define THREAD_FUNC	void
#define T_THREAD	int
#else
#define THREAD_FUNC	void*
#define T_THREAD	pthread_t
#endif

/*
 * EXPORTED FUNCTION PROTOTYPES
 */

/*
 * EXPORTED VARIABLES
 */

#endif /* _DBMT_PORTING_H_ */
