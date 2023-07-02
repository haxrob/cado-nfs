message(STATUS "Checking for BSD/Linux execinfo interface")
include(CheckCSourceCompiles)
CHECK_C_SOURCE_COMPILES("
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
static void signal_handling (int signum)
{
   int sz = 100, i;
   void *buffer [sz];
   char** text;

   sz = backtrace (buffer, sz);
   text = backtrace_symbols (buffer, sz);
   for (i = 0; i < sz; i++)
       puts(text[i]);
   signal (signum, SIG_DFL);
   raise (signum);
}
int main()
{
    signal (SIGABRT, signal_handling);
    signal (SIGSEGV, signal_handling);
}
" HAVE_EXECINFO)

if(HAVE_EXECINFO)
    message(STATUS "Checking for BSD/Linux execinfo interface -- Yes")
else()
    message(STATUS "Checking for BSD/Linux execinfo interface -- No")
endif()
