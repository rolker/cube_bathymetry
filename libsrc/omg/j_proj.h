/*----------------------------------------------------------------------

        j_proj.h        -     John Hughes Clarke   -   Nov 10, 1995

        an attemtped rationallisation of the projection
        routines for utm,merc,rotutm and rotmerc


	defines globals and functions pertaining to j_proj

----------------------------------------------------------------------*/

 
#ifndef J_proj_
#define J_proj_

#include "mercator.h"
#include "array.h"

/* rationallised projection algorythms */
void inv_proj(double x_loc, double y_loc,
                double *lon,  double *lat,
                double *x , double *y, JHC_header *head);

void inv_proj_up(double x_loc, double y_loc,
                double *lon,  double *lat,
                double *x , double *y, JHC_header *head);

void Project(double vlon, double vlat, double *dblex, double *dbley,
                JHC_header *head);
void Proj_up(double vlon, double vlat, double *dblex, double *dbley,
                JHC_header *head);


#endif





