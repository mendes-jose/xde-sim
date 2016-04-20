#ifndef COMMON_H
#define COMMON_H


#if defined(_WIN32)
#include <Windows.h>

#elif defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))
#include <unistd.h>    /* POSIX flags */
#include <time.h>    /* clock_gettime(), time() */
#include <sys/time.h>    /* gethrtime(), gettimeofday() */
#include <X11/Xlib.h>

#if defined(__MACH__) && defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_time.h>
#endif

#else
#error "Unable to define Common::getRealTime( ) for an unknown OS."
#endif

namespace aiv
{
namespace Common
{

  /*!
   * \brief
   * Returns the Fast Inverse Square Root.
   */
  float finvsqrt( float number );

  /*!
   * \brief
   * Returns the real time, in seconds, or -1.0 if an error occurred.
   *
   * Time is measured since an arbitrary and OS-dependent start time.
   * The returned real time is only useful for computing an elapsed time
   * between two calls to this function.
   */
  double getRealTime( );

}
}
#endif // COMMON_H

// cmake:sourcegroup=Helpers
