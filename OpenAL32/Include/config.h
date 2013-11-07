#ifndef CONFIG_H
#define CONFIG_H

/* Define to the library version */
#define ALSOFT_VERSION "1.14"

/* Define if we have the ALSA backend */
/* #undef HAVE_ALSA */

/* Define if we have the OSS backend */
/* #undef HAVE_OSS */

/* Define if we have the Solaris backend */
/* #undef HAVE_SOLARIS */

/* Define if we have the SndIO backend */
/* #undef HAVE_SNDIO */

/* Define if we have the BlackBerry asound backend */
#define HAVE_ASOUND

/* Define if we have the MMDevApi backend */
/* #undef HAVE_MMDEVAPI */

/* Define if we have the DSound backend */
/* #undef HAVE_DSOUND */

/* Define if we have the Windows Multimedia backend */
/* un#def HAVE_WINMM */

/* Define if we have the PortAudio backend */
/* #undef HAVE_PORTAUDIO */

/* Define if we have the PulseAudio backend */
/* #undef HAVE_PULSEAUDIO */

/* Define if we have the CoreAudio backend */
/* #undef HAVE_COREAUDIO */

/* Define if we have the OpenSL backend */
/* #undef HAVE_OPENSL */

/* Define if we have the Wave Writer backend */
#define HAVE_WAVE

/* Define if we have dlfcn.h */
#define HAVE_DLFCN_H

/* Define if we have the stat function */
#define HAVE_STAT

/* Define if we have the powf function */
#define HAVE_POWF

/* Define if we have the sqrtf function */
#define HAVE_SQRTF

/* Define if we have the acosf function */
#define HAVE_ACOSF

/* Define if we have the atanf function */
#define HAVE_ATANF

/* Define if we have the atan2f function */
#define HAVE_ATAN2F

/* Define if we have the fabsf function */
#define HAVE_FABSF

/* Define if we have the log10f function */
#define HAVE_LOG10F

/* Define if we have the floorf function */
#define HAVE_FLOORF

/* Define if we have the strtof function */
#define HAVE_STRTOF

/* Define if we have stdint.h */
#define HAVE_STDINT_H

/* Define if we have the __int64 type */
/* #undef HAVE___INT64 */
#define HAVE___INT64

/* Define to the size of a long int type */
#define SIZEOF_LONG 4

/* Define to the size of a long long int type */
#define SIZEOF_LONG_LONG 8

/* Define if we have GCC's destructor attribute */
#define HAVE_GCC_DESTRUCTOR

/* Define if we have GCC's format attribute */
#define HAVE_GCC_FORMAT

/* Define if we have pthread_np.h */
/* #undef HAVE_PTHREAD_NP_H */

/* Define if we have arm_neon.h */
#define HAVE_ARM_NEON_H

/* Define if we have guiddef.h */
/* #undef HAVE_GUIDDEF_H */

/* Define if we have guiddef.h */
/* #undef HAVE_INITGUID_H */

/* Define if we have ieeefp.h */
#define HAVE_IEEEFP_H

/* Define if we have float.h */
#define HAVE_FLOAT_H

/* Define if we have fpu_control.h */
/* #undef HAVE_FPU_CONTROL_H */

/* Define if we have fenv.h */
#define HAVE_FENV_H

/* Define if we have fesetround() */
#define HAVE_FESETROUND

/* Define if we have _controlfp() */
/* #undef HAVE__CONTROLFP */

/* Define if we have pthread_setschedparam() */
#define HAVE_PTHREAD_SETSCHEDPARAM

/* Define if we have the restrict keyword */
/* #undef HAVE_RESTRICT */

/* Define if we have the __restrict keyword */
/* #undef HAVE___RESTRICT */

#endif
