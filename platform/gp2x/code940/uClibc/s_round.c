/* @(#)s_round.c 5.1 93/09/24 */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */

/*
 * round(x)
 * Return x rounded to integral value to nearest,
 * rounding halfway cases away from zero.
 */

#include "math.h"

#ifdef __STDC__
	double round(double x)
#else
	double round(x)
	double x;
#endif
{
	/* nearest integer, rounding halves away from zero */
	if (x >= 0.0)
		return floor(x + 0.5);
	return -floor(-x + 0.5);
}
