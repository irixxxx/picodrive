/* cos(x) for the 940 image.
 *
 * This is only here because emu2413's OPLL_RateConv_new() references cos(),
 * even though the rate converter is never instantiated at the native sample
 * rate (clock / 72), which is how PicoDrive drives it. So this is built the
 * same way as the neighboring s_sin.c and is exact; it just costs a bit of
 * code for a path that is not taken.
 */

#include "math.h"
#include "math_private.h"

#ifdef __STDC__
	double cos(double x)
#else
	double cos(x)
	double x;
#endif
{
	double y[2],z=0.0;
	int32_t n, ix;

    /* High word of x. */
	GET_HIGH_WORD(ix,x);

    /* |x| ~< pi/4 */
	ix &= 0x7fffffff;
	if(ix <= 0x3fe921fb) return __kernel_cos(x,z);

    /* cos(Inf or NaN) is NaN */
	else if (ix>=0x7ff00000) return x-x;

    /* argument reduction needed */
	else {
	    n = __ieee754_rem_pio2(x,y);
	    switch(n&3) {
		case 0: return  __kernel_cos(y[0],y[1]);
		case 1: return -__kernel_sin(y[0],y[1],1);
		case 2: return -__kernel_cos(y[0],y[1]);
		default:
			return  __kernel_sin(y[0],y[1],1);
	    }
	}
}
