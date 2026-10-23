/* Test wrapper for the real 940 job loop.
 *
 * 940.c normally points its shared-memory pointers at the fixed GP2X
 * addresses.  Renaming them here lets the host test provide ordinary
 * process memory instead.  No production code is changed and the rest of
 * 940.c is compiled as-is.
 */

#define shared_data host_shared_data
#define shared_ctl  host_shared_ctl
#define mp3_data    host_mp3_data

#include "../platform/gp2x/code940/940.c"

#undef shared_data
#undef shared_ctl
#undef mp3_data

void code940_host_set_memory(_940_data_t *data, _940_ctl_t *ctl,
                             unsigned char *mp3)
{
  host_shared_data = data;
  host_shared_ctl = ctl;
  host_mp3_data = mp3;
}
