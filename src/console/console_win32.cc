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

using ssize_t = SSIZE_T;

extern char *progname;
extern int verbose;
extern struct settings *global_settings;
static int redirect_stderr;

//static struct termios console_oldtermios;
//static struct termios console_curtermios;

DWORD console_oldmode_in, console_oldmode_out;
DWORD console_curmode_in, console_curmode_out;

/*  For 'slave' mode:  */
static HANDLE console_slave_outputd;

static struct settings *console_settings = NULL;
static int console_stdout_pending;

struct console_handle_platform {
	HANDLE		w_descriptor;
	HANDLE		r_descriptor;

	PROCESS_INFORMATION proc_info;
	bool                is_proc_info_valid;
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
	HANDLE pipehandles[2];
	HANDLE pipehandlesB[2];
	STARTUPINFOEXW startInfo = {{}, NULL};
	LPPROC_THREAD_ATTRIBUTE_LIST thrdList = NULL;
	SIZE_T bufferSize = 0;
	HANDLE inheritableHandles[2];
	//std::wstring str = L"GXemul: " + std::string(console_handles[handle].machine_name) + std::string(" ") + std::string(console_handles[handle].name);

	wchar_t cmdline[4096] = { 0 };
	wchar_t modulefile[4096] = { 0 };
	wchar_t title[4096] = { 0 };

  auto platform = (struct console_handle_platform*)console_handles[handle].platform;

	swprintf(title, L"GXemul: %S %S", console_handles[handle].machine_name, console_handles[handle].name);
	startInfo.StartupInfo.cb = sizeof(startInfo);
	InitializeProcThreadAttributeList(NULL, 1, 0, &bufferSize);
	if (!bufferSize) {
		fprintf(stderr, "Failed to allocate attribute list\n");
		exit(1);
	}
	thrdList = (LPPROC_THREAD_ATTRIBUTE_LIST)calloc(bufferSize, 1);
	if (!InitializeProcThreadAttributeList(thrdList, 1, 0, &bufferSize)) {
		fprintf(stderr, "Failed to initialize attribute list\n");
		exit(1);
	}
	GetModuleFileNameW(GetModuleHandleW(NULL), modulefile, 4096);

	if (!CreatePipe(&pipehandles[0], &pipehandles[1], NULL, 0)) {
		fprintf(stderr, " [Failed to create pipe handle set 0]\n");
		exit(1);
	}

	if (!CreatePipe(&pipehandlesB[0], &pipehandlesB[1], NULL, 0)) {
		fprintf(stderr, " [Failed to create pipe handle set 1]\n");
		exit(1);
	}
	swprintf(cmdline, L"%s -WW@S%llu,%llu", modulefile, pipehandles[0], pipehandlesB[1]);
	SetHandleInformation(pipehandles[0], HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
	SetHandleInformation(pipehandlesB[1], HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

	//HANDLE inheritableHandles[2] = { pipehandles[0], pipehandlesB[1] };
	inheritableHandles[0] = pipehandles[0];
	inheritableHandles[1] = pipehandlesB[1];
	startInfo.StartupInfo.lpTitle = title;
	startInfo.lpAttributeList = thrdList;

	if (!UpdateProcThreadAttribute(thrdList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, (PVOID)inheritableHandles, sizeof(inheritableHandles), NULL, NULL)) {
		fprintf(stderr, "Failed to update thread attributes\n");
	}
	PROCESS_INFORMATION info;
	BOOL res = CreateProcessW(modulefile, cmdline, NULL, NULL, TRUE, EXTENDED_STARTUPINFO_PRESENT | CREATE_NEW_CONSOLE, NULL, NULL, &startInfo.StartupInfo, &platform->proc_info);
	if (!res)
	{
		fprintf(stderr, "Failed to create slave terminals\n");
		exit(1);
	}

	DeleteProcThreadAttributeList(thrdList);

	console_handles[handle].using_xterm = USING_XTERM;

	/*
	 *  write to filedes[1], read from filedesB[0]
	 */

  platform->w_descriptor = pipehandles[1];
  platform->r_descriptor = pipehandlesB[0];
	platform->is_proc_info_valid = true;
}

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
		char curbyte[1024];
		DWORD bytesread;
		DWORD bytesavailable;
		DWORD msgavailable;
		BOOL res = PeekNamedPipe(d, (LPVOID)&curbyte, 1, &bytesread, &bytesavailable, &msgavailable);
    if (!res) {
      return -1;
    } else if (!bytesavailable) {
      Sleep(10);
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

	return d_avail(platform->r_descriptor);
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


DWORD WINAPI console_thread(PVOID console_slave_outputd) {
  DWORD len;
  char buf[1024];

  auto conin = GetStdHandle(STD_INPUT_HANDLE);
  auto conout = GetStdHandle(STD_OUTPUT_HANDLE);

  for (;;) {
    DWORD numberOfCharsRead = 0;
    if (!ReadConsole(conin, buf, sizeof(buf), &numberOfCharsRead, NULL)) {
      break;
    }
    if (numberOfCharsRead) {
      if (!WriteFile(console_slave_outputd, buf, numberOfCharsRead, &len, NULL) || !numberOfCharsRead) {
        perror("error writing to console handle");
        break;
      }
    } else {
      break;
    }
  }

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
	HANDLE inputd = 0;
	DWORD len = 0;
	const char *p = 0;
	char buf[16384] = { 0 };

  FreeConsole();
  AllocConsole();

  auto conin = GetStdHandle(STD_INPUT_HANDLE);
  auto conout = GetStdHandle(STD_OUTPUT_HANDLE);

	SetConsoleMode(conin, ENABLE_VIRTUAL_TERMINAL_INPUT);
	SetConsoleMode(conout, ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT);

	/*  arg = '3,6' or similar, input and output descriptors  */
	/*  printf("console_slave(): arg = '%s'\n", arg);  */

	inputd = (HANDLE)strtoull(arg, nullptr, 0); //atoi(arg);
	p = strchr(arg, ',');
	if (p == NULL) {
		printf("console_slave(): bad arg '%s'\n", arg);
		Sleep(5);
		exit(1);
	}
	console_slave_outputd = (HANDLE)strtoull(p+1, nullptr, 0);

  DWORD thread_id;
  HANDLE thread = CreateThread
    (nullptr,
     0,
     console_thread,
     console_slave_outputd,
     0,
     &thread_id);
  if (!thread || thread == INVALID_HANDLE_VALUE) {
    sprintf(buf, "thread error %d\n", GetLastError());
    MessageBox(nullptr, buf, "Console Thread", MB_ICONERROR);
  }

  for (;;) {
    auto res = ReadFile(inputd, (LPVOID)buf, sizeof(buf) - 1, &len, NULL);
    if (!res || len == 0) {
      break;
    }
    buf[len] = '\0';
    WriteConsole(conout, buf, len, &len, nullptr);
	}

  return;
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
  auto p = (struct console_handle_platform *)calloc(sizeof(struct console_handle_platform), 1);
  chp->platform = p;
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
