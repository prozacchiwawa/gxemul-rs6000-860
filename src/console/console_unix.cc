/*
 *  Copyright (C) 2003-2014  Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE   
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *  SUCH DAMAGE.
 *
 *
 *  Generic console support functions.
 *
 *  This module is used by individual device drivers, for example serial
 *  controllers, keyboards, or other devices which need to attach to the
 *  emulator's real stdin/stdout.
 *
 *  The idea is that several input and output streams (console handles) are
 *  allowed. As long as the number of input streams is <= 1, then everything
 *  can be done in the emulator's default terminal window.
 *
 *  If the number of inputs is more than 1, it is necessary to open up slave
 *  xterm windows for each input. (Otherwise the behaviour is undefined; i.e.
 *  which of two emulated serial controllers would get keyboard input?)
 *
 *  (If the -x command line option is NOT used, then slaves are not opened up.
 *  Instead, a warning message is printed, and input is not allowed.)
 *
 *  Note that console handles that _allow_ input but are not yet used for
 *  output are not counted. This allows a machine to have, say, 2 serial ports
 *  which can be used for both input and output, and it will still be possible
 *  to run in the default terminal window as long as only one of those serial
 *  ports is actually used.
 *
 *  xterms are opened up "on demand", when output is sent to them.
 *
 *  The MAIN console handle (fixed as handle nr 0) is the one used by the
 *  default terminal window. A machine which registers a serial controller,
 *  which should be used as the main way of communicating with guest operating
 *  systems running on that machine, should set machine->main_console_handle
 *  to the handle of the correct port on that controller.
 *
 *
 *  NOTE: The code in this module is mostly non-reentrant.
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/types.h>
#include <time.h>
#include <assert.h>

#include "console.h"
#include "emul.h"
#include "machine.h"
#include "settings.h"

extern char *progname;
static int console_slave_outputd;
static struct termios console_oldtermios;
static struct termios console_curtermios;

/*
 *  console_deinit_main():
 *
 *  Restore host's console settings.
 */
void console_deinit_main(void)
{
	if (!console_initialized)
		return;

	tcsetattr(STDIN_FILENO, TCSANOW, &console_oldtermios);

	console_initialized = 0;
}

/*  For 'slave' mode:  */
static struct termios console_slave_tios;

struct console_handle_platform {
	int		w_descriptor;
	int		r_descriptor;
};

/*
 *  console_sigcont():
 *
 *  If the user presses CTRL-Z (to stop the emulator process) and then
 *  continues, the termios settings might have been invalidated. This
 *  function restores them.
 *
 *  (This function should be set as the SIGCONT signal handler in src/emul.c.)
 */
void console_sigcont(int x)
{
	if (!console_initialized)
		return;

	/*  Make sure that the correct (current) termios setting is active:  */
	tcsetattr(STDIN_FILENO, TCSANOW, &console_curtermios);

	/*  Reset the signal handler:  */
	signal(SIGCONT, console_sigcont);
}


/*
 *  start_xterm():
 *
 *  When using X11 (well, when allow_slaves is set), this routine tries to
 *  start up an xterm, with another copy of gxemul inside. The other gxemul
 *  copy is given arguments that will cause it to run console_slave().
 *
 *  TODO: This is ugly and hardcoded. Clean it up.
 */
void start_xterm_platform(int handle)
{
	int filedes[2];
	int filedesB[2];
	int res;
	size_t mlen;
	char **a;
	pid_t p;

	res = pipe(filedes);
	if (res) {
		printf("[ start_xterm(): pipe(): %i ]\n", errno);
		exit(1);
	}

	res = pipe(filedesB);
	if (res) {
		printf("[ start_xterm(): pipe(): %i ]\n", errno);
		exit(1);
	}

	/*  printf("filedes = %i,%i\n", filedes[0], filedes[1]);  */
	/*  printf("filedesB = %i,%i\n", filedesB[0], filedesB[1]);  */

	/*  NOTE/warning: Hardcoded max nr of args!  */
	CHECK_ALLOCATION(a = (char **) malloc(sizeof(char *) * 20));

	a[0] = getenv("XTERM");
	if (a[0] == NULL)
		a[0] = strdup("xterm");
	a[1] = strdup("-geometry");
	a[2] = strdup("80x25");
	a[3] = strdup("-title");
	mlen = strlen(console_handles[handle].name) +
	    strlen(console_handles[handle].machine_name) + 30;
	CHECK_ALLOCATION(a[4] = (char *) malloc(mlen));
	snprintf(a[4], mlen, "GXemul: %s %s",
	    console_handles[handle].machine_name,
	    console_handles[handle].name);
	a[5] = strdup("-e");
	a[6] = progname;
	CHECK_ALLOCATION(a[7] = (char *) malloc(80));
	snprintf(a[7], 80, "-WW@S%i,%i", filedes[0], filedesB[1]);
	a[8] = NULL;

	p = fork();
	if (p == -1) {
		printf("[ start_xterm(): ERROR while trying to "
		    "fork(): %i ]\n", errno);
		exit(1);
	} else if (p == 0) {
		close(filedes[1]);
		close(filedesB[0]);

		p = setsid();
		if (p < 0)
			printf("[ start_xterm(): ERROR while trying "
			    "to do a setsid(): %i ]\n", errno);

		res = execvp(a[0], a);
		printf("[ start_xterm(): ERROR while trying to "
		    "execvp(\"");
		while (a[0] != NULL) {
			printf("%s", a[0]);
			if (a[1] != NULL)
				printf(" ");
			a++;
		}
		printf("\"): %i ]\n", errno);
		if (errno == ENOENT)
			printf("[ Most probably you don't have xterm"
			    " in your PATH. Try again. ]\n");
		exit(1);
	}

	/*  TODO: free a and a[*]  */

	close(filedes[0]);
	close(filedesB[1]);

	console_handles[handle].using_xterm = USING_XTERM;

	/*
	 *  write to filedes[1], read from filedesB[0]
	 */

        auto platform = (struct console_handle_platform *)console_handles[handle].platform;
	platform->w_descriptor = filedes[1];
	platform->r_descriptor = filedesB[0];
}


/*
 *  d_avail():
 *
 *  Returns 1 if anything is available on a descriptor.
 */
static int d_avail(int d)
{
	fd_set rfds;
	struct timeval tv;

	FD_ZERO(&rfds);
	FD_SET(d, &rfds);
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	int selres = select(d+1, &rfds, NULL, NULL, &tv);
	return selres > 0;
}


/*
 *  console_stdin_avail():
 *
 *  Returns 1 if a char is available from a handle's read descriptor,
 *  0 otherwise.
 */
int console_stdin_avail_platform(int handle)
{
	if (!console_handles[handle].in_use_for_input)
		return 0;

	if (!allow_slaves)
		return d_avail(STDIN_FILENO);

	if (console_handles[handle].using_xterm ==
	    USING_XTERM_BUT_NOT_YET_OPEN)
		return 0;

  auto platform = (struct console_handle_platform *)console_handles[handle].platform;
	return d_avail(platform->r_descriptor);
}


/*
 *  console_slave_sigint():
 */
static void console_slave_sigint(int x)
{
	char buf[1];

	/*  Send a ctrl-c:  */
	buf[0] = 3;
	if (write(console_slave_outputd, buf, sizeof(buf)) != sizeof(buf))
		perror("error writing to console handle");

	/*  Reset the signal handler:  */
	signal(SIGINT, console_slave_sigint);
}


/*
 *  console_slave_sigcont():
 *
 *  See comment for console_sigcont. This is for used by console_slave().
 */
static void console_slave_sigcont(int x)
{
	/*  Make sure our 'current' termios setting is active:  */
	tcsetattr(STDIN_FILENO, TCSANOW, &console_slave_tios);

	/*  Reset the signal handler:  */
	signal(SIGCONT, console_slave_sigcont);
}

int console_init_main_platform(struct emul *emul) {
  int tra = 0;

  console_new_handle_platform(MAIN_CONSOLE);

	tcgetattr(STDIN_FILENO, &console_oldtermios);
	memcpy(&console_curtermios, &console_oldtermios,
         sizeof (struct termios));

	console_curtermios.c_lflag &= ~ICANON;
	console_curtermios.c_cc[VTIME] = 0;
	console_curtermios.c_cc[VMIN] = 1;

	console_curtermios.c_lflag &= ~ECHO;

	/*
	 *  Most guest OSes seem to work ok without ~ICRNL, but Linux on
	 *  DECstation requires it to be usable.  Unfortunately, clearing
	 *  out ICRNL makes tracing with '-t ... |more' akward, as you
	 *  might need to use CTRL-J instead of the enter key.  Hence,
	 *  this bit is only cleared if we're not tracing:
	 */
	tra = 0;
	for (int i=0; i<emul->n_machines; i++)
		if (emul->machines[i]->show_trace_tree ||
		    emul->machines[i]->instruction_trace ||
		    emul->machines[i]->register_dump)
			tra = 1;
	if (!tra)
		console_curtermios.c_iflag &= ~ICRNL;

	tcsetattr(STDIN_FILENO, TCSANOW, &console_curtermios);

  auto platform = (struct console_handle_platform *)console_handles[MAIN_CONSOLE].platform;
	platform->w_descriptor = 1;
	platform->r_descriptor = 0;

  return 0;
}

/*
 *  console_slave():
 *
 *  This function is used when running with X11, and gxemul opens up
 *  separate xterms for each emulated terminal or serial port.
 */
void console_slave(const char *arg)
{
	int inputd;
	int len;
	const char *p;
	char buf[16384];

	/*  arg = '3,6' or similar, input and output descriptors  */
	/*  printf("console_slave(): arg = '%s'\n", arg);  */

	inputd = atoi(arg);
	p = strchr(arg, ',');
	if (p == NULL) {
		printf("console_slave(): bad arg '%s'\n", arg);
		sleep(5);
		exit(1);
	}
	console_slave_outputd = atoi(p+1);

	/*  Set the terminal to raw mode:  */
	tcgetattr(STDIN_FILENO, &console_slave_tios);

	console_slave_tios.c_lflag &= ~ICANON;
	console_slave_tios.c_cc[VTIME] = 0;
	console_slave_tios.c_cc[VMIN] = 1;
	console_slave_tios.c_lflag &= ~ECHO;
	console_slave_tios.c_iflag &= ~ICRNL;
	tcsetattr(STDIN_FILENO, TCSANOW, &console_slave_tios);

	signal(SIGINT, console_slave_sigint);
	signal(SIGCONT, console_slave_sigcont);

	for (;;) {
		/*  TODO: select() on both inputd and stdin  */

		if (d_avail(inputd)) {
			len = read(inputd, buf, sizeof(buf) - 1);
			if (len < 1)
				exit(0);
			buf[len] = '\0';
			printf("%s", buf);
			fflush(stdout);
		}

		if (d_avail(STDIN_FILENO)) {
			len = read(STDIN_FILENO, buf, sizeof(buf));
			if (len < 1)
				exit(0);
			if (write(console_slave_outputd, buf, len) != len)
				perror("error writing to console handle");
		}

		usleep(10000);
	}
}

int console_putchar_platform(int handle, int ch) {
  auto platform = (struct console_handle_platform *)console_handles[handle].platform;
  return write(platform->w_descriptor, &ch, 1);
}

int console_new_handle_platform(int handle) {
  struct console_handle *chp = &console_handles[handle];
  auto p = (struct console_handle_platform *)calloc(sizeof(struct console_handle_platform), 1);
  chp->platform = p;
  return 0;
}

int console_read_platform(int handle, uint8_t *buf, size_t len) {
  int d;

  auto platform = (struct console_handle_platform *)console_handles[handle].platform;
  if (!allow_slaves)
    d = STDIN_FILENO;
  else
    d = platform->r_descriptor;

  return read(d, buf, len);
}

int redirect_stderr_platform(const char *new_name) {
  if (!freopen(new_name, "w", stderr)) {
    perror(new_name);
    return -1;
  }

  return 0;
}
