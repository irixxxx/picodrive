/* Test wrapper: rename 940.c's fixed shared pointers so the host test can
 * supply process memory; the rest compiles as-is. */

#define shared_data host_shared_data
#define shared_ctl  host_shared_ctl
#define mp3_data    host_mp3_data

#include "../code940/940.c"

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
