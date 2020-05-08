/*
 * $Id: test_errmod.c 20 2005-06-22 15:20:16Z brc $
 * $Log$
 * Revision 1.1  2005/06/22 15:19:54  brc
 * Added to complete the contents of the repository.
 *
 * Revision 1.5.2.2  2002/12/15 02:03:46  brc
 * Modifications to make the code compile under Win32 without bitching.
 *
 * Revision 1.5.2.1  2002/07/14 02:20:48  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.5  2002/05/10 22:26:38  brc
 * Added facility to specify many of the environmental parameters at the
 * command line, so that we don't have to recompile the code to check the
 * effects of various components.
 *
 * Revision 1.4  2002/03/14 04:42:18  brc
 * Upgraded to allow use of split-head error models.
 *
 * Revision 1.3  2001/12/07 20:12:31  brc
 * Added facility to load a configuration file so that the parameters of the
 * error model and the default vessel can be specified.
 *
 * Revision 1.2  2001/05/13 02:37:55  brc
 * Added facility to set the device name on command line.
 *
 * Revision 1.1  2001/02/10 17:43:02  brc
 * Test sequence for error modeling - runs the default case from the MBES
 * error model spreadsheet for comparisson.
 *
 *
 * File:	test_errmod.c
 * Purpose:	Test error computation models
 * Date:	2000-11-28
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <getopt.h>
#include "stdtypes.h"
#include "ccom_general.h"
#include "device.h"
#include "vessel.h"
#include "error.h"
#include "sounding.h"
#include "errmod.h"

static char *modname = "test_errmod";
static char *modrev = "$Revision: 20 $";

static char *get_rcs_rev(const char *rev)
{
	char *p = strchr(rev, ' ');
	
	if (p != NULL) {
		char *b = strdup(++p);
		if ((p = b) != NULL && (b = strchr(p, ' ')) != NULL) *b = '\0';
	} else p = strdup("1.0");
	return(p);
}

#define DEG2RAD(x) ((x)*M_PI/180.0)
#define RAD2DEG(x) ((x)*180.0/M_PI)

typedef enum {
	DEVICE_NAME = 1,
	ARGC_EXPECTED
} Cmd;

static void Syntax(void)
{
	printf("test_errmod V%s [%s] - Test error computation models.\n",
			get_rcs_rev(modrev), __DATE__);
	printf("Syntax: test_errmod [opt] <device>\n");
	printf(" Options:\n");
	printf("  -f <file>   Configure library using <file>.\n");
	printf("  -r <roll>   Set instantaneous roll for scenario (deg.)\n");
	printf("  -p <pitch>  Set instantaneous pitch for scenario (deg.)\n");
	printf("  -h <heave>  Set peak instantaneous heave (m)\n");
	printf("  -s <sspeed> Set surface sound speed (m/s)\n");
	printf("  -d <depth>  Set nominal depth (m)\n");
	printf("  -v <pspeed> Set vessel speed (m/s)\n");
	printf("  -x <xover>  Set amplitude/phase detect cross-over count (samples).\n");
	printf(" Notes:\n");
	printf("  1. This computes error budgets for the data, using the full\n");
	printf("     MBES error model.\n");
}

#define NBEAMS	256

int main(int argc, char **argv)
{
	int				c;
	ErrMod			full, port, stbd;
	Projection		proj;
	Device			device;
	Vessel			defves, port_vsl, stbd_vsl;
	Platform		orient;
	Sounding		data[NBEAMS];
	u32				beam, nbeams, base_samples, np;
	f64				angle, scale;
	ParList			*parlist = NULL;
	Bool			params_set = False, split_head = False;
	f32				samp_freq, pulse_len, along_bw, across_bw;
	f32				base_roll = 2.0f,
					base_pitch = 2.0f,
					base_heave = 0.05f,
					base_sspeed = 1536.0f,
					base_pspeed = 6.68f,
					base_depth = 25.0f;
	u32				amp_phase_xover = 10;

	ccom_log_command_line(modname, argc, argv);

	opterr = 0;
	while ((c = getopt(argc, argv, "f:r:p:h:s:d:v:x:")) != EOF) {
		switch(c) {
			case 'x':
				amp_phase_xover = atoi(optarg);
				break;
			case 'v':
				base_pspeed = (f32)atof(optarg);
				break;
			case 'r':
				base_roll = (f32)atof(optarg);
				break;
			case 'p':
				base_pitch = (f32)atof(optarg);
				break;
			case 'h':
				base_heave = (f32)atof(optarg);
				break;
			case 's':
				base_sspeed = (f32)atof(optarg);
				break;
			case 'd':
				base_depth = (f32)atof(optarg);
				break;
			case 'f':
				if (!params_new_from_file(optarg, &parlist)) {
					error_msgv(modname, "error: failed reading \"%s\" for"
						" parameters.\n", optarg);
					return(1);
				}
				if (!params_execute(parlist)) {
					error_msg(modname, "error: failed executing parameter"
						" list.\n");
					return(1);
				}
				params_set = True;
				break;
			case '?':
				fprintf(stderr, "%s: unknown flag '%c'.\n", modname, optopt);
				Syntax();
				return(1);
		}
	}
	argc -= optind-1; argv += optind-1;
	if (argc != ARGC_EXPECTED) {
		Syntax();
		return(1);
	}
	/* Make default device and vessel configuration */
	if ((device = device_new_by_name(argv[DEVICE_NAME])) == NULL) {
		error_msgv(modname, "error: can't construct a device from \"%s\".\n",
			argv[DEVICE_NAME]);
		return(1);
	}
	nbeams = device_get_nbeams(device);
	
	defves = vessel_default();
	
	if ((device_get_properties(device)&DEVICE_IS_SPLITHEAD) != 0) {
		split_head = True;
		if ((port_vsl = vessel_get_head(VESSEL_HEAD_PORT)) == NULL ||
			(stbd_vsl = vessel_get_head(VESSEL_HEAD_STBD)) == NULL) {
			error_msg(modname, "error: failed to generate two head specs"
				" for split-head transducer.\n");
			return(1);
		}
	}
	if (!params_set) {
		/* Configure the vessel as the CSS Revisor (P-class launch) */
		defves->imu_x = -0.242;
		defves->imu_y = 0.004;
		defves->imu_z = 0.464;
		defves->gps_x = 0.219;
		defves->gps_y = -0.117;
		defves->gps_z = 4.503;
		defves->gps_latency = 0.250;
		defves->draft = 0.70; /* m */
	}
	proj = projection_new_utm(0.0, 0.0);
	if (device == NULL || defves == NULL || proj == NULL) {
		error_msg(modname, "failed to generate projection/device/vessel "
			"configurations.\n");
		return(1);
	}
	if ((full = errmod_new(device, defves, ERRMOD_FULL)) == NULL) {
		error_msg(modname, "failed generating full error model setup.\n");
		return(1);
	}
	if (split_head &&
		((port = errmod_new(device, port_vsl, ERRMOD_FULL)) == NULL ||
		 (stbd = errmod_new(device, stbd_vsl, ERRMOD_FULL)) == NULL)) {
		error_msg(modname, "error: failed generating port/stbd error models.\n");
		return(1);
	}
	
	/* Test 1: Run with just a spread on the N beams */
	memset(&orient, 0, sizeof(Platform));
	orient.surf_sspeed = base_sspeed;
	orient.mean_speed = base_sspeed;
	orient.vessel_speed = base_pspeed; /* m/s ~ 13kts */
	orient.roll = base_roll;
	orient.pitch = base_pitch;
	orient.heave = base_heave;	/* Total swell 0.10m */
	memset(data, 0, sizeof(Sounding)*NBEAMS);

	device_get_samp_freq(device, &samp_freq);
	device_get_pulse_len(device, &pulse_len);
	base_samples = (u32)ceil(samp_freq * pulse_len * 1.0e-3);
	device_get_beamwidths(device, &across_bw, &along_bw);
	scale = 2.0*DEG2RAD(across_bw)*samp_freq/orient.mean_speed;

	for (beam = 0; beam < nbeams; ++beam) {
		data[beam].depth = base_depth;
		if (split_head) {
			if (beam < nbeams/2)
				angle = errmod_get_beam_angle(port, beam);
			else
				angle = errmod_get_beam_angle(stbd, beam);
		} else
			angle = errmod_get_beam_angle(full, beam);
		data[beam].range = (f32)(data[beam].depth/cos(angle));
		data[beam].east = data[beam].depth*tan(angle);
		data[beam].beam_number = beam;
		/* Compute number of phase samples, assuming flat sea-floor */
		np = base_samples +
			(u32)ceil(scale*data[beam].depth*fabs(tan(angle))/cos(angle));
		if (np >= amp_phase_xover) {
			if (np > 255) np = 255;
			data[beam].flags = SOUNDING_DETECT_MTHD_FLAG;
			data[beam].flags |= SOUNDING_PUTWINDOWSZ(np);
		}
	}
	if (split_head) {
		if (!errmod_estimate(port, &orient, data, nbeams/2) ||
			!errmod_estimate(stbd, &orient, data+nbeams/2, nbeams/2)) {
			error_msg(modname, "failed estimating errors.\n");
			return(1);
		}
	} else
		if (!errmod_estimate(full, &orient, data, nbeams)) {
			error_msg(modname, "failed estimating errors for test 1.\n");
			return(1);
		}
	printf("Full error model (95%%):\n");
	for (beam = 0; beam < nbeams; ++beam) {
		if (split_head) {
			if (beam < nbeams/2)
				angle = errmod_get_beam_angle(port, beam);
			else
				angle = errmod_get_beam_angle(stbd, beam);
		} else
			angle = errmod_get_beam_angle(full, beam);
		printf("%d %lf %lf %lf %lf %lf %lf\n", beam,
			RAD2DEG(angle),
			data[beam].depth, data[beam].range,
			data[beam].east, 1.96*sqrt(data[beam].dz), 1.96*sqrt(data[beam].dr));
	}
	
	errmod_release(full);
	vessel_release(defves);
	device_release(device);	
	return(0);
}
