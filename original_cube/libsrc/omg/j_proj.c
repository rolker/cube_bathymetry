
/*----------------------------------------------------------------------

	j_proj.c	-     John Hughes Clarke   -   Nov 10, 1995

	an attemtped rationallisation of the projection
	routines for utm,merc,rotutm and rotmerc

----------------------------------------------------------------------*/

#include "j_proj.h"
#include "j_phi.h"
#include "array.h"
#include "support.h"
#include "utm.h"
#include "wgs_init.h"

/*-------------------------------------------------------------
	this is differnt from Project only in that the y distance 
		is from the bottom (mapsheet cm up the page)
		rather than the top  (array pixels down from the top)

  ------------------------------------------------------------*/

void Proj_up(double vlon, double vlat, double *dblex, double *dbley,
                JHC_header *head)
{
	Project(vlon, vlat, dblex, dbley, head);
                *dbley = (double)head->dy - *dbley;

	
}
/*-------------------------------------------------------------*/

void Project(double vlon, double vlat, double *dblex, double *dbley,
                JHC_header *head)
{
double x, y;
double sin_rot_angle, cos_rot_angle, rot_angle;

		Ellipsoid = head->ellipsoid;


                if(head->projection == 1) {
          	  merc_init((double)head->xstep, 
		(double)head->proj_merid, 
		(double)head->xmin, (double)head->ymin);
                  mercator(vlon,vlat, dblex,dbley);
                } else if(head->projection == 2) {
                  geoutm(vlat, vlon, (double)head->proj_merid, dblex, dbley);
                   *dblex -= head->xmin;
                   *dbley -= head->ymin;
                   *dblex /= head->xstep;
                   *dbley /= head->ystep;
                } else if(head->projection == 4) {
                  geoutm(vlat, vlon, (double)head->proj_merid, dblex, dbley);
                   *dblex -= head->xmin;
                   *dbley -= head->ymin;
                    x= *dblex; y= *dbley;
                   rot_angle = head->ymax* M_PI/180.0;
                   sin_rot_angle = sin(rot_angle);
                   cos_rot_angle = cos(rot_angle);
                    *dbley = -x*sin_rot_angle + y*cos_rot_angle;
                    *dblex = x*cos_rot_angle + y*sin_rot_angle;
                   *dblex /= head->xstep;
                   *dbley /= head->ystep;
                   *dblex += (double)head->dx/2.0;
                   *dbley += (double)head->dy/2.0;


                } else if(head->projection == 5) {
          	  merc_init(head->xstep, (double)head->proj_merid, head->xmin, head->ymin);
                  mercator(vlon,vlat, dblex,dbley);
                    x= *dblex; y= *dbley;
                   rot_angle = head->ymax* M_PI/180.0;
                   sin_rot_angle = sin(rot_angle);
                   cos_rot_angle = cos(rot_angle);
                    *dbley = -x*sin_rot_angle + y*cos_rot_angle;
                    *dblex = x*cos_rot_angle + y*sin_rot_angle;
                   *dblex += (double)head->dx/2.0;
                   *dbley += (double)head->dy/2.0;
                }
                *dbley = (double)head->dy - *dbley;
}



/*----------------------------------------------------------------------*/




/* ------------------------------------------------- */

void inv_proj_up(double x_loc, double y_loc,
                double *lon,  double *lat,
                double *x , double *y, JHC_header *head)
{
                y_loc = (double)head->dy - y_loc;
	inv_proj(x_loc, y_loc, lon, lat, x, y, head);
}


/*-------------------------------------------------------------*/
void inv_proj(double x_loc, double y_loc,
                double *lon,  double *lat,
                double *x , double *y, JHC_header *head)
{
double xx, yy;
double sin_rot_angle, cos_rot_angle, rot_angle;


        y_loc = (double)head->dy-y_loc;


/*
	printf(" here %d\n", head->ellipsoid);
	printf(" proj %d\n", head->projection);
*/
		Ellipsoid = head->ellipsoid;

                if(head->projection == 1) {
          	  merc_init(head->xstep, (double)head->proj_merid, head->xmin, head->ymin);
                  inv_merc(x_loc, y_loc, lon, lat);
                } else if(head->projection == 2) {
                   x_loc *= (double)head->xstep;
                   y_loc *= (double)head->ystep;
                   x_loc += head->xmin;
                   y_loc += head->ymin;
                   *x = x_loc;
                   *y = y_loc;
			if(head->hemisphere == 1)
                  utmgeo(lat, lon, (double)head->proj_merid, x_loc, y_loc, 's');
			else
                  utmgeo(lat, lon, (double)head->proj_merid, x_loc, y_loc, 'n');
                } else if(head->projection == 4) {
                   x_loc -= (double)head->dx/2.0;
                   y_loc -= (double)head->dy/2.0;
                   x_loc *= (double)head->xstep;
                   y_loc *= (double)head->ystep;
                    xx= x_loc; yy= y_loc;
                   rot_angle = head->ymax* M_PI/180.0;
                   sin_rot_angle = sin(rot_angle);
                   cos_rot_angle = cos(rot_angle);
                    y_loc = xx*sin_rot_angle + yy*cos_rot_angle;
                    x_loc = xx*cos_rot_angle - yy*sin_rot_angle;
                   x_loc += head->xmin;
                   y_loc += head->ymin;
                   *x = x_loc;
                   *y = y_loc;
			if(head->hemisphere == 1)
                  utmgeo(lat, lon, (double)head->proj_merid, x_loc, y_loc, 's');
			else
                  utmgeo(lat, lon, (double)head->proj_merid, x_loc, y_loc, 'n');
                } else if(head->projection == 5) {
                   x_loc -= (double)head->dx/2.0;
                   y_loc -= (double)head->dy/2.0;
                    xx= x_loc; yy= y_loc;
                   rot_angle = head->ymax* M_PI/180.0;
                   sin_rot_angle = sin(rot_angle);
                   cos_rot_angle = cos(rot_angle);
                    y_loc = xx*sin_rot_angle + yy*cos_rot_angle;
                    x_loc = xx*cos_rot_angle - yy*sin_rot_angle;
          	  merc_init(head->xstep, (double)head->proj_merid, head->xmin, head->ymin);
                  inv_merc(x_loc, y_loc, lon, lat);
                }
}


/*-------------------------------------------------------------*/


