/****************************************************************************

   THIS SOFTWARE IS NOT COPYRIGHTED

   HP offers the following for use in the public domain.  HP makes no
   warranty with regard to the software or it's performance and the
   user accepts the software "AS IS" with all faults.

   HP DISCLAIMS ANY WARRANTIES, EXPRESS OR IMPLIED, WITH REGARD
   TO THIS SOFTWARE INCLUDING BUT NOT LIMITED TO THE WARRANTIES
   OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.

****************************************************************************/

/****************************************************************************
 *  Header: remcom.c,v 1.34 91/03/09 12:29:49 glenne Exp $
 *
 *  Module name: remcom.c $
 *  Revision: 1.34 $
 *  Date: 91/03/09 12:29:49 $
 *  Contributor:     Lake Stevens Instrument Division$
 *
 *  Description:     low level support for gdb debugger. $
 *
 *  Considerations:  only works on target hardware $
 *
 *  Written by:      Glenn Engel $
 *  ModuleState:     Experimental $
 *
 *  NOTES:           See Below $
 *
 *  Modified for 386 by Jim Kingdon, Cygnus Support.
 *  Modified for ReactOS by Casper S. Hornstrup <chorns@users.sourceforge.net>
 *
 *  To enable debugger support, two things need to happen.  One, setting
 *  up a routine so that it is in the exception path, is necessary in order
 *  to allow any breakpoints or error conditions to be properly intercepted
 *  and reported to gdb.
 *  Two, a breakpoint needs to be generated to begin communication.
 ER*
 *  Because gdb will sometimes write to the stack area to execute function
 *  calls, this program cannot rely on using the supervisor stack so it
 *  uses it's own stack area.
 *
 *************
 *
 *    The following gdb commands are supported:
 *
 * command          function                               Return value
 *
 *    g             return the value of the CPU Registers  hex data or ENN
 *    G             set the value of the CPU Registers     OK or ENN
 *
 *    mAA..AA,LLLL  Read LLLL bytes at address AA..AA      hex data or ENN
 *    MAA..AA,LLLL: Write LLLL bytes at address AA.AA      OK or ENN
 *
 *    c             Resume at current address              SNN   ( signal NN)
 *    cAA..AA       Continue at address AA..AA             SNN
 *
 *    s             Step one instruction                   SNN
 *    sAA..AA       Step one instruction from AA..AA       SNN
 *
 *    k             kill
 *
 *    ?             What was the last sigval ?             SNN   (signal NN)
 *
 * All commands and responses are sent with a packet which includes a
 * Checksum.  A packet consists of
 *
 * $<packet info>#<Checksum>.
 *
 * where
 * <packet info> :: <characters representing the command or response>
 * <Checksum>    :: < two hex digits computed as modulo 256 sum of <packetinfo>>
 *
 * When a packet is received, it is first acknowledged with either '+' or '-'.
 * '+' indicates a successful transfer.  '-' indicates a failed transfer.
 *
 * Example:
 *
 * Host:                  Reply:
 * $m0,10#2a               +$00010203040506070809101112131415#42
 *
 ****************************************************************************/

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <arpa/inet.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include "cpu.h"
#include "memory.h"
#include "thirdparty/ppc_spr.h"
#include "debugger.h"

typedef struct _BREAKPOINT {
    int OldCode;
    int *Address;
} BREAKPOINT, *PBREAKPOINT;

extern volatile int single_step;
extern volatile int exit_debugger;
extern int debugger_n_steps_left_before_interaction;

BREAKPOINT BreakPoints[64];
char DataOutBuffer[65536];
volatile int DataOutAddr, DataOutCsum;
char DataInBuffer[128];
volatile int DataInAddr, ParseState = 0, ComputedCsum, ActualCsum;
volatile int PacketSent = 0, SendSignal = 0;
volatile int Continue = 0, Signal = 0;

int gdbstub_socket = -1, gdbstub_listen = -1;

typedef cpu ppc_trap_frame_t;

volatile ppc_trap_frame_t RegisterSaves, *RegisterSaveArea = &RegisterSaves;
char *hex = "0123456789abcdef";

#define RCV 0
#define THR 0
#define BAUDLOW 0
#define BAUDHIGH 1
#define IER 1
#define FCR 2
#define ISR 2
#define LCR 3
#define MCR 4
#define LSR 5
#define MSR 6
#define SPR 7

static void Wait(struct cpu *cpu);

int isxdigit(int ch)
{
    return
        (ch >= 'A' && ch <= 'F') ||
        (ch >= 'a' && ch <= 'f') ||
        (ch >= '0' && ch <= '9');
}

void gdbstub_send(char c) {
    fprintf(stderr, "%c", c);
    if (gdbstub_socket != -1) {
        if (send(gdbstub_socket, &c, 1, 0) < 1) {
            close(gdbstub_socket);
            gdbstub_socket = -1;
        }
    }
}

int rdy(struct cpu *cpu, int wait)
{
    int result = 0;
    int n_pfds = 1;
    if (gdbstub_listen == -1) {
        return 0;
    }

    struct pollfd pfd_read[2] = { };
    pfd_read[0].fd = gdbstub_listen;
    pfd_read[0].events = POLLIN;

    if (gdbstub_socket != -1) {
        pfd_read[1].fd = gdbstub_socket;
        pfd_read[1].events = POLLIN;
        n_pfds += 1;
    }

    int poll_result = poll(pfd_read, n_pfds, wait ? 1000 : 0);
    if (poll_result < 1) {
        return 0;
    }

    // Try to read from the socket.
    if (pfd_read[1].revents & POLLIN) {
        result = 1;
    }

    // Try to accept if needed.
    if (pfd_read[0].revents & POLLIN) {
        struct sockaddr_in sa = { };
        socklen_t len = sizeof(sa);
        gdbstub_socket = accept(gdbstub_listen, (struct sockaddr *)&sa, &len);
    }

    return result;
}

char gdbstub_recv() {
    char c;

    int res = recv(gdbstub_socket, &c, sizeof(c), 0);
    if (res < 1) {
        close(gdbstub_socket);
        gdbstub_socket = -1;
        c = 0xff;
        Continue = true;
    }

    return c;
}

static void close_ports() {
    if (gdbstub_socket != -1) {
        close(gdbstub_socket);
    }
    if (gdbstub_listen != -1) {
        close(gdbstub_listen);
    }
}

void GdblibSetup()
{
    gdbstub_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (gdbstub_listen == -1) {
        return;
    }

    struct sockaddr_in sa = { };
    sa.sin_family = 2;
    sa.sin_port = htons(3322);
    sa.sin_addr.s_addr = htonl(0x7f000001);

    if (bind(gdbstub_listen, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
        close(gdbstub_listen);
        gdbstub_listen = -1;
        return;
    }

    listen(gdbstub_listen, 5);

    atexit(close_ports);
}

int GdblibActive()
{
    return gdbstub_socket != -1;
}

int GdblibCheckWaiting(struct cpu *cpu) {
    return rdy(cpu, 0);
}

int GdblibCheckConnected() {
    return gdbstub_socket != -1;
}

static void Wait(struct cpu *cpu)
{
    Continue = false;
    while(!Continue) {
        if (rdy(cpu, 1)) {
            GdblibSerialInterrupt(cpu);
        }
    }
    fprintf(stderr, "gdblib wait done\n");
}

void SerialWrite(int ch)
{
    gdbstub_send(ch);
}

int SerialRead()
{
    return gdbstub_recv();
}

int hex2int(int ch)
{
    if (ch >= 'a' && ch <= 'f') return ch + 10 - 'a';
    else if (ch >= 'A' && ch <= 'F') return ch + 10 - 'A';
    else return ch - '0';
}

int PacketReadHexNumber(int dig)
{
    int i;
    int result = 0;
    for (i = 0; i < dig && isxdigit(DataInBuffer[DataInAddr]); i++)
    {
        result <<= 4;
        result |= hex2int(DataInBuffer[DataInAddr++]);
    }
    return result;
}

void PacketWriteChar(int ch)
{
    DataOutCsum += ch;
    DataOutBuffer[DataOutAddr++] = ch;
}

int PacketWriteHexNumber(int hnum, int dig, int little_endian)
{
    int i;
    if (little_endian) {
        for (i = 0; i < dig / 2; i++) {
            PacketWriteChar(hex[(hnum >> 4) & 15]);
            PacketWriteChar(hex[hnum & 15]);
            hnum >>= 8;
        }
    } else {
        hnum <<= (8 - dig) * 4;
        for (i = 0; i < dig; i++)
        {
            PacketWriteChar(hex[(hnum >> 28) & 15]);
            hnum <<= 4;
        }
    }
    return i;
}

void PacketStart()
{
    DataOutCsum = 0;
    DataOutAddr = 0;
}

void PacketEnd()
{
}

void PacketFinish(struct cpu *cpu)
{
    int i, ch;

    PacketSent = 0;

    do {
        SerialWrite('$');
        for (i = 0; i < DataOutAddr; i++)
        {
            SerialWrite(DataOutBuffer[i]);
        }
        SerialWrite('#');
        SerialWrite(hex[(DataOutCsum >> 4) & 15]);
        SerialWrite(hex[DataOutCsum & 15]);

        while(!rdy(cpu, 1));
        if (SerialRead() == '+') break;
    } while(PacketSent != 1);
}

void PacketWriteString(char *str)
{
    while(*str) PacketWriteChar(*str++);
}

void PacketOk(struct cpu *cpu)
{
    PacketStart();
    PacketWriteString("OK");
    PacketFinish(cpu);
}

void PacketEmpty(struct cpu *cpu)
{
    PacketStart();
    PacketFinish(cpu);
}

void PacketWriteSignal(struct cpu *cpu, int code)
{
    PacketStart();
    PacketWriteChar('S');
    PacketWriteHexNumber(code, 2, 0);
    PacketFinish(cpu);
}

void PacketWriteError(struct cpu *cpu, int code)
{
    PacketStart();
    PacketWriteChar('E');
    PacketWriteHexNumber(code, 2, 0);
    PacketFinish(cpu);
}

void GotPacket(struct cpu *cpu)
{
    int i, memaddr, memsize;
    uint8_t membuf[4];
    char namebuf[10];
    auto OldSaveArea = RegisterSaveArea;
    RegisterSaveArea = cpu;

    switch (DataInBuffer[DataInAddr++])
    {
    case 'g':
        PacketStart();
        // Copy out 108 registers
        for (i = 0; i < 32; i++) {
            PacketWriteHexNumber(RegisterSaveArea->cd.ppc.gpr[i], 8, cpu->cd.ppc.msr & PPC_MSR_LE);
        }
        for (i = 0; i < 32; i++) {
            PacketWriteHexNumber(RegisterSaveArea->cd.ppc.fpr[i], 8, cpu->cd.ppc.msr & PPC_MSR_LE);
        }
        for (i = 0; i < 32; i++) {
            PacketWriteHexNumber(0, 8, 0);
        }

        // [x] 0xfe019d70
        PacketWriteHexNumber(RegisterSaveArea->pc, 8, cpu->cd.ppc.msr & PPC_MSR_LE);
        // [x] 0x13031
        PacketWriteHexNumber(RegisterSaveArea->cd.ppc.msr, 8, cpu->cd.ppc.msr & PPC_MSR_LE);
        PacketWriteHexNumber(RegisterSaveArea->cd.ppc.cr, 8, cpu->cd.ppc.msr & PPC_MSR_LE);
        PacketWriteHexNumber(RegisterSaveArea->cd.ppc.spr[8], 8, cpu->cd.ppc.msr & PPC_MSR_LE);
        PacketWriteHexNumber(RegisterSaveArea->cd.ppc.spr[9], 8, cpu->cd.ppc.msr & PPC_MSR_LE);
        PacketWriteHexNumber(RegisterSaveArea->cd.ppc.fpscr, 8, cpu->cd.ppc.msr & PPC_MSR_LE);
        PacketWriteHexNumber(RegisterSaveArea->cd.ppc.spr[1], 8, cpu->cd.ppc.msr & PPC_MSR_LE);
        for (i = 0; i < 4; i++) {
            PacketWriteHexNumber(0, 8, cpu->cd.ppc.msr & PPC_MSR_LE);
        }
        PacketFinish(cpu);
        break;

    case 'G':
        // Don't support for now.
        PacketOk(cpu);
        break;

    case 'p':
        PacketStart();
        PacketWriteHexNumber(0, 8, cpu->cd.ppc.msr & PPC_MSR_LE);
        PacketFinish(cpu);
        break;

    case 'm':
        memaddr = PacketReadHexNumber(8);
        DataInAddr++;
        memsize = PacketReadHexNumber(8);
        PacketStart();

        while(memsize > 0)
        {
            int readsize = memsize > sizeof(membuf) ? sizeof(membuf) : memsize;

            if (cpu->memory_rw(cpu, cpu->mem, memaddr, membuf, readsize, MEM_READ, CACHE_NONE | NO_EXCEPTIONS) == MEMORY_ACCESS_FAILED) {
                break;
            }

            memsize -= readsize;
            memaddr += readsize;

            for (int i = 0; i < readsize; i++) {
                PacketWriteHexNumber(membuf[i], 2, cpu->cd.ppc.msr & PPC_MSR_LE);
            }
        }

        PacketFinish(cpu);
        break;

    case 'M':
        memaddr = PacketReadHexNumber(8);
        DataInAddr++;
        memsize = PacketReadHexNumber(8);
        DataInAddr++;
        while(memsize-- > 0)
        {
            *((char *)memaddr++) = PacketReadHexNumber(2);
        }
        PacketOk(cpu);
        break;

    case '?':
        PacketWriteSignal(cpu, Signal);
        break;

    case 'C':
    case 'c':
        Continue = 1;
        break;

    case 'H':
        if (DataInBuffer[1] == 'c') {
            Continue = 1;
            debugger_step(cpu->machine, 1);
        }
        PacketOk(cpu);
        break;

    case 's':
        RegisterSaveArea->cd.ppc.spr[SPR_SRR1] |= 16;
        PacketOk(cpu);
        Continue = 1;
        break;

    case 'v':
        PacketEmpty(cpu);
        break;

    case 'z': // Delete breakpoint
        PacketReadHexNumber(1);
        DataInAddr++;
        memaddr = PacketReadHexNumber(8);
        breakpoint_delete(cpu->machine, memaddr);
        PacketOk(cpu);
        break;

    case 'Z': // Add breakpoint
        PacketReadHexNumber(1);
        DataInAddr++;
        memaddr = PacketReadHexNumber(8);
        sprintf(namebuf, "%08x", memaddr);
        breakpoint_add(cpu->machine, memaddr, namebuf, strlen(namebuf));
        PacketOk(cpu);
        break;

    case 'q':
        fprintf(stderr, "qPacket: %c\n", DataInBuffer[1]);
        switch (DataInBuffer[1])
        {
        case 'A': /*ttached*/
            PacketStart();
            PacketWriteString("1");
            PacketFinish(cpu);
            break;

        case 'C': /* thread id */
            PacketStart();
            PacketWriteString("0100");
            PacketFinish(cpu);
            break;

        case 'S': /*upported => nothing*/
            PacketStart();
            PacketWriteString("xmlRegisters-;swbreak-;hwbreak-");
            PacketFinish(cpu);
            break;

        case 'O': /*ffsets*/
            PacketEmpty(cpu);
            break;

        case 'f': /* thread list */
            if (DataInBuffer[2] != 'T') { /*hreadinfo*/
                PacketEmpty(cpu);
                break;
            }

            PacketStart();
            PacketWriteString("m");
            PacketWriteHexNumber(0,1, cpu->cd.ppc.msr & PPC_MSR_LE);
            PacketFinish(cpu);
            break;

        case 's':
            PacketStart();
            PacketWriteString("l");
            PacketFinish(cpu);
            break;

        default:
            PacketEmpty(cpu);
            break;
        }
        break;

    default:
        PacketOk(cpu);
        break;
    }

    RegisterSaveArea = OldSaveArea;
}

void GdblibSerialInterrupt(struct cpu *cpu)
{
    int ch = SerialRead();
    fprintf(stderr, "%c", ch);

    if (ch == '+')
    {
        PacketSent = 1;
    }
    else if (ch == '-')
    {
        PacketSent = -1;
    }
    else if (ch == '$')
    {
        DataInAddr = 0;
        ParseState = 0;
        ComputedCsum = 0;
        ActualCsum = 0;
    }
    else if (ch == '#' && ParseState == 0)
    {
        ParseState = 2;
    }
    else if (ch == 3 && ParseState == 0)
    {
        ParseState = 0;
        DataInAddr = 0;
        Wait(cpu);
        return;
    }
    else if (ParseState == 0)
    {
        ComputedCsum += ch;
        DataInBuffer[DataInAddr++] = ch;
    }
    else if (ParseState == 2)
    {
        ActualCsum = ch;
        ParseState++;
    }
    else if (ParseState == 3)
    {
        ActualCsum = hex2int(ch) | (hex2int(ActualCsum) << 4);
        ComputedCsum &= 255;
        ParseState = -1;
        if (ComputedCsum == ActualCsum)
        {
            ComputedCsum = 0;
            DataInBuffer[DataInAddr] = 0;
            DataInAddr = 0;
            Continue = 0;
            SerialWrite('+');
            GotPacket(cpu);
        }
        else {
            SerialWrite('-');
        }
    }
}

void GdblibTakeException(struct cpu *cpu, int n)
{
    fprintf(stderr, "report exception %d to gdb\n", n);
    Signal = n;
    SendSignal = 1;
    auto OldSaveArea = RegisterSaveArea;
    RegisterSaveArea = cpu;
    if (SendSignal) {
        fprintf(stderr, "send signal packet\n");
        PacketWriteSignal(cpu, Signal);
    }
    SendSignal = 0;
    Continue = 0;
    Wait(cpu);
    RegisterSaveArea = OldSaveArea;
    fprintf(stderr, "return from gdb exception\n");
}

/* EOF */
