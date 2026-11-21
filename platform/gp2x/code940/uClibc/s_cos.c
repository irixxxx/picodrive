/* cos() for the 940 image. emu2413 references it in an unused rate-converter
 * path; built like the neighboring s_sin.c. */

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
