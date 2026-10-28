This is a fork of Anders Gavare's wonderful gxemul emulator which
served as a very good base for experimenting and learning.

[http://gavare.se/gxemul/](GXemul)

To Build
===

    cmake -G "Unix Makefiles" -DCMAKE_CXX_FLAGS="-O3" .
    make

ROM and NVRAM
===

A 512k firmware
image, such as P91G1671 from pccbbs and an nvram image, such as [860.nvram](https://github.com/prozacchiwawa/gxemul-rs6000-860-test/blob/main/860.nvram) 
will allow it to start up to the firmware user interface, run personal
power dos (type 'eatabug' at the easy setup menu) and start operating
system installers from floppy or cdrom.  The main branch runs windows
nt 4 aix 4.1.5 and netbsd with some quirks.  os/2 powerpc can run in
it's rs/6000 model 850 personality, but the branch isn't mergable yet.

Win32
===

On win32, the console and any serial ports are available via
tcp ports (default port 8000).  You can set the start port with
-P on windows.  Use putty or similar to connect to the console
port to use the debugger and subsequent ports for the serial
ports.  You can also connect to the main port and use the redirect
command (for example, "redirect tty0") to move your connection to
the named port.

The story so far
===

My focus was to be able to run the native firmware of the quirky
RS/6000 model 860 and the operating systems released for it.  It's
still got a ways to go.

Parts of this are absent from upstream gxemul and it's my hope
to upstream some of it.

It now supports the s3 aurora and wd90c24a well enough to run the
console and various operating systems reasonably.  
The lsi53c810 is ported from qemu and needs finishing but IMO
would make a good addition to gxemul.

The fdc driver is likewise.

The serial port and interrupt controller have been completely
rewritten to hopefully be both more accurate and to serve
real operating systems better.

A few features from the real RTC chip have been added such as
the battery good bit, attached nvram and a watchdog timer
interface used in the PReP 604 reference design.

ISA DMA is implemented well enough to read the floppy, and is
needed for the IDE interface, but that isn't hooked up yet.

I've made additions as well to add different pseudo disk types
for the original firmware (-d R:...) and a copy of the nvram
(-d n:...).  If one has these from somewhere, they can be used
to boot into "Personal Power Firmware".

I rewrote the tlb and physical page cache to fix some bugs,
with the result that some in the wild operating systems now
run.

Future work
===

I made substantial changes to a few internal interfaces.  While
CPU architectures other than powerpc still build, I have not
tested them and especially my tlb rewrite probably breaks some
things.  I intend to restore them to working at least as well
as in the original gxemul evenutally.

Screenshots
===

![RS/6000 model 860 config screen](https://github.com/prozacchiwawa/gxemul-rs6000-860/blob/main/doc/2023-02-05-rs6000-860-firmware.png?raw=true)
![RS/6000 model 860 config screen](https://github.com/prozacchiwawa/gxemul-rs6000-860/blob/main/doc/2026-06-22-aix-help-viewer.png?raw=true)
