/*
 * Unobtrusively log data coming in on a serial device.
 *
 * Diomidis Spinellis, December 2001
 *
 * $Id: montty.c,v 1.2 2001/12/09 12:19:24 dds Exp $
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <poll.h>
#include <syslog.h>
#include <libutil.h>

/*
 * Expand src to dst
 */
static void
expand(char *src, char *dst, int len)
{
	int n = 0;
	char *p;

	for (p = src, n = 0; *p && n < len - 1; p++, n++)
		if (*p == '\\')
			switch (*++p) {
			case '\\': *dst++ = '\\'; break;
			case 'a': *dst++ = '\a'; break;
			case 'b': *dst++ = '\b'; break;
			case 'f': *dst++ = '\f'; break;
			case 't': *dst++ = '\t'; break;
			case 'r': *dst++ = '\r'; break;
			case 'n': *dst++ = '\n'; break;
			case 'v': *dst++ = '\v'; break;
			case '0': *dst++ = '\0'; break;
			default:
				syslog(LOG_ERR, "invalid escape in %s", src);
				exit(1);
			}
		else
			*dst++ = *p;
	*dst++ = 0;
}


main(int argc, char *argv[])
{
	FILE *f;
	char logname[1024];
	char buff[1024];
	char devname[1024];
	struct pollfd pfd[1];
	int i, n;
	/* True when initialisation strings must be sent */
	int need_init = 1;	
	int lockresult;

	if (argc < 2) {
		fprintf(stderr, "usage: %s line [initialisation string] ...\n"
				"e.g. %s cuaa0 'ATS82=76\\r\\n'\n", argv[0], argv[0]);
		exit(1);
	}
	daemon(0, 0);
	snprintf(logname, sizeof(logname), "montty.%s", argv[1]);
	openlog(logname, 0, LOG_LOCAL0);
	syslog(LOG_INFO, "starting up: pid %d", getpid());
	snprintf(buff, sizeof(buff), "/var/run/montty.%s.pid", argv[1]);
	if ((f = fopen(buff, "w")) == NULL) {
		syslog(LOG_ERR, "unable to open pid file %s: %m", buff);
		exit(1);
	} else {
		fprintf(f, "%d\n", getpid());
		fclose(f);
	}
	snprintf(devname, sizeof(devname), "/dev/%s", argv[1]);
	if ((pfd[0].fd = open(devname, O_RDWR | O_NONBLOCK)) < 0) {
		syslog(LOG_ERR, "unable to open monitor file %s: %m", devname);
		exit(1);
	}
	syslog(LOG_INFO, "monitoring %s", devname);
	pfd[0].events = POLLIN | POLLRDNORM | POLLERR;
	for (;;) {
		if (!need_init)
			/* No initialisation needed, just wait for input */
			if (poll(pfd, 1, INFTIM) < 0) {
				syslog(LOG_ERR, "poll(INFTIM) failed: %m");
				exit(1);
			}
		/* 
		 * We have input, or we need to initialise the device;
		 * acquire a lock.
		 */
		switch (lockresult = uu_lock(argv[1])) {
		case UU_LOCK_OK:
			/*
			 * Now that we have the lock,
			 * check if we need to write the initialisation data.
			 */
			if (need_init) {
				for (i = 1; i < argc; i++)
					expand(argv[i], buff, sizeof(buff));
					if (write(pfd[0].fd, buff, strlen(buff) < 0)) {
						syslog(LOG_ERR, "write failed: %m");
						exit(1);
					}
				need_init = 0;
			}
			/* Check if there is still something to read. */
			if (poll(pfd, 1, 0) < 0) {
				syslog(LOG_ERR, "poll(0) failed: %m");
				exit(1);
			}
			if (pfd[0].revents & (POLLIN | POLLRDNORM)) {
				if ((n = read(pfd[0].fd, buff, sizeof(buff))) == -1) {
					syslog(LOG_ERR, "read failed: %m");
					exit(1);
				}
				buff[n] = 0;
				syslog(LOG_INFO, "%s", buff);
			}
			if (uu_unlock(argv[1]) == -1) {
				syslog(LOG_ERR, "uu_unlock error %m");
				exit(1);
			}
			break;
		case UU_LOCK_INUSE:
			sleep(1);
			/* Someone is using the device, we must re-init it */
			need_init = 1;
			break;
		default:
			syslog(LOG_ERR, "uu_lock error %s", uu_lockerr(lockresult));
		}
	}
}
