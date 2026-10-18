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
 *  If the number of inputs is more than 1, it is necessary to open up instance
 *  xterm windows for each input. (Otherwise the behaviour is undefined; i.e.
 *  which of two emulated serial controllers would get keyboard input?)
 *
 *  (If the -x command line option is NOT used, then instances are not opened up.
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
#include <sys/types.h>
#include <time.h>
#include <assert.h>
#include <io.h>
#include <fcntl.h>

#define NOMINMAX 1
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>

#include "console.h"
#include "emul.h"
#include "machine.h"
#include "settings.h"
#include "debugger.h"

using ssize_t = SSIZE_T;

DWORD WINAPI console_socket_thread(PVOID handle);

#define BSIZE 1024
#define BMAX (256 * 1024)

struct attending {
  int socket;
  std::vector<uint8_t> buf;
};

extern char *progname;
extern int verbose;
extern uint64_t single_step;
extern struct settings *global_settings;
static int redirect_stderr;
int console_port_nr = 8000;

//static struct termios console_oldtermios;
//static struct termios console_curtermios;

DWORD console_oldmode_in, console_oldmode_out;
DWORD console_curmode_in, console_curmode_out;

static struct settings *console_settings = NULL;
static int console_stdout_pending;

struct console_handle_platform {
	HANDLE		w_descriptor;
	HANDLE		r_descriptor;
  HANDLE    thread;
  DWORD     thread_id;

  HANDLE    relay_thread;
  DWORD     relay_thread_id;

  int port;
  HANDLE inputd;
  HANDLE console_instance_outputd;
};


/*
 *  console_deinit_main():
 *
 *  Restore host's console settings.
 */
void console_deinit_main(void)
{
	if (!console_initialized)
		return;

	console_initialized = 0;
}


void start_xterm_platform(int handle)
{
  auto platform = (struct console_handle_platform*)console_handles[handle].platform;
  if (platform->thread) {
    return;
  }

  // Previously code for starting an xterm was delayed until the hardware was used.
  // Now ports are bound immediately.
  //
  // We also allocate ports here and start immediately since we're still using the subprocess
  // model.

	HANDLE pipehandles[2];
	HANDLE pipehandlesB[2];
	SIZE_T bufferSize = 0;

	if (!CreatePipe(&pipehandles[0], &pipehandles[1], NULL, 0)) {
		fprintf(stderr, " [Failed to create pipe handle set 0]\n");
		exit(1);
	}

	if (!CreatePipe(&pipehandlesB[0], &pipehandlesB[1], NULL, 0)) {
		fprintf(stderr, " [Failed to create pipe handle set 1]\n");
		exit(1);
	}

	console_handles[handle].using_xterm = USING_XTERM;

	/*
	 *  write to filedes[1], read from filedesB[0]
	 */

  platform->inputd = pipehandles[0];
  platform->console_instance_outputd = pipehandlesB[1];
  platform->w_descriptor = pipehandles[1];
  platform->r_descriptor = pipehandlesB[0];

  platform->thread = CreateThread(nullptr, 0, console_socket_thread, (PVOID)platform, 0, &platform->thread_id);
}

#define AVAIL_RES_CTRL_C 1000

/*
 *  d_avail():
 *
 *  Returns 1 if anything is available on a descriptor.
 */
static int d_avail(HANDLE d)
{
	if (!d) return 0;
	auto ftype = GetFileType(d);
	if (ftype == FILE_TYPE_CHAR) {
		auto res = WaitForSingleObject(d, 0);
		return res == WAIT_OBJECT_0;
	} else if (ftype == FILE_TYPE_PIPE) {
		unsigned char curbyte[1];
		DWORD bytesread;
		DWORD bytesavailable;
		DWORD msgavailable;
		BOOL res = PeekNamedPipe(d, (LPVOID)&curbyte, 1, &bytesread, &bytesavailable, &msgavailable);
    if (!res) {
      return -1;
    }

    if (curbyte[0] == 3) {
      return AVAIL_RES_CTRL_C;
    }

    return bytesavailable;
	} else if (d) {
		fprintf(stderr, "Unknown file type 0x%04X\n", ftype);
	}
	return 0;
}


/*
 *  console_stdin_avail():
 *
 *  Returns 1 if a char is available from a handle's read descriptor,
 *  0 otherwise.
 */
int console_stdin_avail_platform(int handle)
{
  auto platform = (struct console_handle_platform*)console_handles[handle].platform;
  if (!platform) {
    return 0;
  }

	if (!console_handles[handle].in_use_for_input)
		return 0;

	if (console_handles[handle].using_xterm ==
	    USING_XTERM_BUT_NOT_YET_OPEN)
		return 0;

	int avail_res = d_avail(platform->r_descriptor);
  if (avail_res == AVAIL_RES_CTRL_C && handle == MAIN_CONSOLE) {
    // Recognize ctrl+c from a console.
    single_step = 1;
  }

  return !!avail_res;
}


int console_putchar_platform(int handle, int ch) {
  DWORD written;
  unsigned char buf[1] = { (unsigned char)ch };
  auto platform = (struct console_handle_platform*)console_handles[handle].platform;
  if (!WriteFile(platform->w_descriptor, buf, 1, &written, nullptr)) {
    return -1;
  }
  return written;
}

void die(const std::string &msg) {
  MessageBox(nullptr, msg.c_str(), "GXEmul console instance", MB_ICONHAND);
  exit(1);
}

// Windows can't select on a named pipe
DWORD WINAPI console_thread(PVOID p) {
  auto platform = (struct console_handle_platform*)p;
  auto port = platform->port;
  auto inputd = platform->inputd;
  
  DWORD len;
  char buf[1024];

  struct sockaddr_in sa;
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  sa.sin_port = htons((intptr_t)port);

  // We relay data from gxemul through this socket.
  int relay_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (relay_socket < 0) {
    die(std::string("couldn't make relay socket"));
  }

  if (connect(relay_socket, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
    die(std::string("couldn't connect relay socket"));
  }

  bool finished = false;
  while (!finished) {
    DWORD numberOfCharsRead = 0;
    if (!ReadFile(inputd, buf, sizeof(buf), &numberOfCharsRead, NULL)) {
      finished = true;
      break;
    }
    int written = 0;
    while (written < numberOfCharsRead) {
      int len;
      if ((len = send(relay_socket, buf + written, numberOfCharsRead - written, 0)) < 1) {
        finished = true;
        break;
      }
      written += len;
    }
  }

  shutdown(relay_socket, 2);
  closesocket(relay_socket);

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
  exit(0);
}

DWORD WINAPI console_socket_thread(PVOID p) {
  auto platform = (struct console_handle_platform*)p;
  auto port = platform->port;
  auto inputd = platform->inputd;
  auto console_instance_outputd = platform->console_instance_outputd;

	DWORD len = 0;
  uint8_t buf[BSIZE];
  int relay_socket;
  WSADATA wsadata;
  memset(&wsadata, 0, sizeof(wsadata));

  WSAStartup(0x101, &wsadata);

	/*  arg = '3,6' or similar, input and output descriptors  */
	/*  printf("console_instance(): arg = '%s'\n", arg);  */

  struct sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  int listen_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_socket < 0) {
    die(std::string("failed to create listen socket"));
  }

  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  if (bind(listen_socket, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
    die(std::string("failed to bind listen socket"));
  }

  if (listen(listen_socket, 5) < 0) {
    die(std::string("failed to listen"));
  }

  platform->relay_thread = CreateThread(nullptr, 0, console_thread, p, 0, &platform->relay_thread_id);
  if (!platform->relay_thread_id || platform->relay_thread == INVALID_HANDLE_VALUE) {
    die(std::string("couldn't create relay thread"));
  }

  // Start a thread and connect.  This socket will relay info from the pipe.
  int relay_read = accept(listen_socket, nullptr, nullptr);
  if (relay_read < 0) {
    die(std::string("couldn't accept relay socket"));
  }

  std::vector<struct attending> sockets_attending;
  std::vector<uint8_t> stored;
  bool finished = false;

  auto end_socket = [&buf](int socket) {
    while (recv(socket, (char *)buf, sizeof(buf), 0) > 0) {
      ;
    }

    shutdown(socket, 2);
    closesocket(socket);
  };

  while (!finished) {
    std::vector<size_t> cleanup_attending;
    struct fd_set readable, writable, error;

    FD_ZERO(&readable);
    FD_ZERO(&writable);
    FD_ZERO(&error);

    for (auto i = sockets_attending.begin(); i != sockets_attending.end(); i++) {
      FD_SET(i->socket, &readable);
      FD_SET(i->socket, &error);
      if (i->buf.size()) {
        FD_SET(i->socket, &writable);
      }
    }

    FD_SET(listen_socket, &readable);
    FD_SET(relay_read, &readable);
    FD_SET(relay_read, &error);

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    int select_result = select(0, &readable, &writable, &error, &tv);
    int numberOfCharsRead = 0;

    if (select_result < 0) {
      break;
    }

    if (select_result == 0) {
      continue;
    }

    if (FD_ISSET(listen_socket, &readable)) {
      // Accept
      int accepted = accept(listen_socket, nullptr, nullptr);
      DWORD nonblocking = 1;
      ioctlsocket(accepted, FIONBIO, &nonblocking);
      struct attending a;
      a.socket = accepted;
      if (stored.size()) {
        std::swap(stored, a.buf);
      }
      sockets_attending.push_back(a);
    }
    if (FD_ISSET(relay_read, &readable)) {
      // Data from gxemul
      if ((numberOfCharsRead = recv(relay_read, (char*)buf, sizeof(buf), 0)) < 0) {
        finished = true;
        break;
      }

      if (sockets_attending.size() == 0 && stored.size() < BMAX) {
        std::vector<uint8_t> buf_vect(buf, buf + numberOfCharsRead);
        stored.insert(std::end(stored), std::begin(buf_vect), std::end(buf_vect));
      } else {
        for (auto i = sockets_attending.begin(); i != sockets_attending.end(); i++) {
          if (i->buf.size() + numberOfCharsRead > BMAX) {
            cleanup_attending.push_back(i - sockets_attending.begin());
            continue;
          }
          for (int j = 0; j < numberOfCharsRead; j++) {
            i->buf.push_back(buf[j]);
          }
        }
      }
    }

    for (auto i = sockets_attending.begin(); i != sockets_attending.end(); i++) {
      if (FD_ISSET(i->socket, &writable)) {
        int sent;
        if ((sent = send(i->socket, (const char *)&i->buf[0], i->buf.size(), 0)) < 0) {
          cleanup_attending.push_back(i - sockets_attending.begin());
          continue;
        }
        if (sent >= i->buf.size()) {
          i->buf.clear();
        } else {
          i->buf.erase(i->buf.begin(), i->buf.begin() + sent);
        }
      }
      if (FD_ISSET(i->socket, &readable)) {
        // Data from attending
        int recv_result;
        if ((recv_result = recv(i->socket, (char *)buf, sizeof(buf), 0)) < 0) {
          cleanup_attending.push_back(i - sockets_attending.begin());
          continue;
        }
        if (recv_result) {
          if (!WriteFile(console_instance_outputd, buf, recv_result, &len, nullptr) || !len) {
            finished = true;
            break;
          }
        }
      }
    }

    for (auto i = cleanup_attending.begin(); i != cleanup_attending.end(); i++) {
      auto attending = &sockets_attending[*i];
      end_socket(attending->socket);
      sockets_attending.erase(sockets_attending.begin() + *i);
    }
	}

  end_socket(relay_read);
  closesocket(listen_socket);

  struct fd_set readable;
  for (auto i = sockets_attending.begin(); i != sockets_attending.end(); i++) {
    end_socket(i->socket);
  }

  WSACleanup();

  return 0;
}


/*
 *  console_new_handle():
 *
 *  Allocates a new console_handle struct, and returns a pointer to it.
 *
 *  For internal use.
 */
int console_new_handle_platform(int handle)
{
  struct console_handle *chp = &console_handles[handle];
  if (chp->platform) {
    return 0;
  }

  auto p = (struct console_handle_platform *)calloc(sizeof(struct console_handle_platform), 1);
  chp->platform = p;
  p->port = console_port_nr++;

  return 0;
}


/*
 *  console_init_main():
 *
 *  Puts the host's console into single-character (non-canonical) mode.
 */
int console_init_main_platform(struct emul *emul)
{
  allow_slaves = 1;
  console_new_handle_platform(MAIN_CONSOLE);
  start_xterm_platform(MAIN_CONSOLE);

  auto platform = (struct console_handle_platform*)console_handles[MAIN_CONSOLE].platform;
  auto result = 0;
  freopen("NUL", "w", stdout);
  auto stdout_fdes = _fileno(stdout);
  auto new_stdout_fd = _open_osfhandle((intptr_t)platform->w_descriptor, _O_TEXT);
  result = _dup2(new_stdout_fd, stdout_fdes);
  if (result) {
    return result;
  }
  if (!redirect_stderr) {
    freopen("NUL", "w", stderr);
    auto stderr_fdes = _fileno(stderr);
    result = _dup2(new_stdout_fd, stderr_fdes);
  }
  return result;
}

int console_read_platform(int handle, uint8_t *buf, size_t len) {
  HANDLE d;
  DWORD outlen;
  auto platform = (struct console_handle_platform*)console_handles[handle].platform;

  if (platform->r_descriptor)
    d = platform->r_descriptor;
  else
    d = GetStdHandle(STD_INPUT_HANDLE);

  if (!ReadFile(d, (LPVOID)buf, len, &outlen, NULL)) {
    return -1;
  }

  return outlen;
}

int redirect_stderr_platform(const char *new_name) {
  redirect_stderr = 1;
  if (!freopen(new_name, "w", stderr)) {
    MessageBox(nullptr, new_name, "Failed to redirect stderr", MB_ICONHAND);
    return -1;
  }

  return 0;
}

void console_platform_deinit() {
  for (int i = 0; i < n_console_handles; i++) {
    if (console_handles[i].platform) {
      auto platform = (struct console_handle_platform*)console_handles[i].platform;
      CloseHandle(platform->w_descriptor);
      CloseHandle(platform->r_descriptor);
    }
  }
}
