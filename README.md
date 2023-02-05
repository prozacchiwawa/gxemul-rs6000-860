This is a fork of Anders Gavare's wonderful gxemul emulator which
served as a very good base for experimenting and learning.

[http://gavare.se/gxemul/](GXemul)

My focus was to be able to run the native firmware of the quirky
RS/6000 model 860 and the operating systems released for it.  It's
still got a ways to go: typing 'eatabug' at the easy config screen
does drop into the builtin shell and it can be interacted with,
but it uses features I haven't finished emulating in the S3 vga
yet.

Parts of this are absent from upstream gxemul and it's my hope
to upstream some of it.

The lsi53c810 is ported from qemu and needs finishing but IMO
would make a good addition to gxemul.

The fdc driver is likewise.

A few features from the real RTC chip have been added such as
the battery good bit, attached nvram and a watchdog timer
interface used in the PReP 604 reference design.

ISA DMA is implemented well enough to read the floppy, and is
needed for the IDE interface, but that isn't hooked up yet.

I've made additions as well to add different pseudo disk types
for the original firmware (-d R:...) and a copy of the nvram
(-d n:...).  If one has these from somewhere, they can be used
to boot into "Personal Power Firmware".

![RS/6000 model 860 config screen](https://github.com/prozacchiwawa/gxemul-rs6000-860/blob/main/doc/2023-02-05-rs6000-860-firmware.png?raw=true)
