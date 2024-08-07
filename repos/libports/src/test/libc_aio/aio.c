/*
 * \brief  AIO test
 * \author Benjamin Lamowski
 * \date   2031-07-31
 */
#include <aio.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/event.h>

int
main(int argc, char **argv)
{
	/* Check argument for write test */
	if (argc != 2)
		 err(EXIT_FAILURE, "Usage: %s path\n", argv[0]);

	/* Create kqueue */
	int kq = kqueue();
	if (kq == -1)
		 err(EXIT_FAILURE, "kqueue() failed");

	/*
	 * Write test
	 */
	char *write_string = "Hello log\n";

	int write_fd = open(argv[1], O_WRONLY);

	if (write_fd == -1)
		 err(EXIT_FAILURE, "Failed to open '%s'",argv[1]);

	struct aiocb write_cb;
	write_cb.aio_fildes = write_fd;
	write_cb.aio_buf = write_string;
	write_cb.aio_nbytes = strlen(write_string) + 1;
	write_cb.aio_offset = 0;
	write_cb.aio_sigevent.sigev_notify = SIGEV_KEVENT;
	write_cb.aio_sigevent.sigev_notify_kqueue = kq;
	write_cb.aio_sigevent.sigev_notify_kevent_flags = 0;


	aio_write(&write_cb);

	while (aio_error(&write_cb) == EINPROGRESS) {
		 sleep(1);
	}

	int error = aio_error(&write_cb);
	if (error == 0)
		 printf("Aio write retval: %li\n", aio_return(&write_cb));
	else
		 err(EXIT_FAILURE, "Aio returned write error: %i\n", error);


	/*
	 * Read test
	 */
	int read_fd = open("/dev/rtc", O_RDONLY);
	if (read_fd == -1)
		 err(EXIT_FAILURE, "Failed to open '%s'", argv[1]);

	struct aiocb read_cb;
	char buf[10];

	read_cb.aio_fildes =read_fd;
	read_cb.aio_buf = buf;
	read_cb.aio_nbytes = sizeof(buf);
	read_cb.aio_offset = 0;
	read_cb.aio_sigevent.sigev_notify = SIGEV_KEVENT;
	read_cb.aio_sigevent.sigev_notify_kqueue = kq;
	read_cb.aio_sigevent.sigev_notify_kevent_flags = 0;

	aio_read(&read_cb);

	while (aio_error(&read_cb) == EINPROGRESS) {
		 sleep(1);
	}

	error = aio_error(&read_cb);
	if (error == 0)
		 printf("Aio write retval: %li\n", aio_return(&read_cb));
	else
		 err(EXIT_FAILURE, "Aio returned read error: %i\n", error);

	for (int i = 0; i < 2 ; i++) {
		/* Event triggered */
		struct kevent tevent;

		/* Sleep until something happens. */
		int ret = kevent(kq,NULL, 0, &tevent, 1, NULL);

		if (ret == -1) {
			err(EXIT_FAILURE, "kevent wait");
		} else if (ret > 0) {
		    if (tevent.flags & EV_ERROR)
			     errx(EXIT_FAILURE, "Event error.\n");
		    else
			     printf("Got AIO event for %lu'\n", tevent.ident);
		}
	}

	/* kqueues are destroyed upon close() */
	(void)close(kq);
	(void)close(write_fd);

	printf("--- test succeeded ---\n");

	return 0;
}
