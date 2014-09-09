/**
* @file px4_simple_app.c
* Minimal application example for PX4 autopilot.
*/
#include <nuttx/config.h>
#include <stdio.h>
#include <errno.h>
_export int px4_simple_app_main(int argc, char *argv[]);
int px4_simple_app_main(int argc, char *argv[])
{
printf("Hello Sky!\n");
return OK;
}
