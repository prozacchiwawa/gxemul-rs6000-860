#ifndef CONFIG_H
#define CONFIG_H

#cmakedefine VERSION "@VERSION@"
#cmakedefine UNSTABLE_DEVEL
#define COMPILE_DATE "compiled on " __DATE__ ", " __TIME__
#cmakedefine WITH_X11
#cmakedefine HOST_LITTLE_ENDIAN

#endif  /*  CONFIG_H  */