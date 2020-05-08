#ifndef __UTM_H__
#define __UTM_H__

#ifdef __cplusplus
extern "C" {
#endif

extern int utmgeo(double *phi, double *lam, double clam, double x, double y, char hem);
extern int geoutm (double phi, double lam, double clam, double *x, double *y);

#ifdef __cplusplus
}
#endif

#endif
