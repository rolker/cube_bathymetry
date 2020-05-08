/* -------------------------------------------------


	OMG_HDCS_jversion.h		John E. Hughes Clarke	Nov 21 1991.

	include file for  new proposed HDCS merged 

	PositionDepthImage format.

	Has evolved over time.....

	Main points are original structure tried to conform to 
	Nickerson et al.  HDCS structure.

	This didnt include imagery, especially Simrad EM1000/12 type
	image formats, so added custom extensions.
		(while departing from HDCS comfority)
	This structure handles a wide variety of other data types
	(generally subsets of the full requirements that a Simrad
	tool generates).

	In reality, as 30 days of 24 hour surveying at 1/4 second
	ping rates are required to be handled at once, cant live with this
	structure, so have developed a packed down structure that
	loses some of the obscure parameters and much of the 
	redundant dynamic range (heading to 10nanoRadians for Christ sake!)

	This works for most multibeams, not really appropriate for 
	phase bathymetric sidescans such as SMII/Sys09/MR1.
	Totally over the top for simple time series sidescans
	Having difficulty with FanSweep 10 and BottomChart multiple ping
	swaths too. Need multi roll pitch and heave.

	March 1996, Trying to allow for byte swapping and variable
	byte alignment so we can run transparently on DEC ALphas etc..

	July 1996 . expanding to cope with Submetrix ISIS and Fansweep 20
	 fundamental question: do you really handle the 1K+ solutions as 
			       stand along beams?

	September 1996 ... adding asynchronous attitude (RPHH) time
	series support 

***********************
			Feb 1998 -- Version 3 introduced

	why ?........

	-- because of the EM300

	-- Yaw stabilisatiomn means that the assumption that

		alongtrack  < 1/6 *MAX(acrosstrack, depth)

		is false.. (embedded in Version 2 compression )...

	-- want to add beam vector to beam data structure....

	-- want to add runtime parameter fields to profile header ..

******************************

   ------------------------------------------------- */

#ifndef omg_hdcs_
#define omg_hdcs_



#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include "stdtime.h"
#include "DCM_rthand.h"
#include "array.h"

/* _________________ SUMMARY RECORD STRUCTURE ____________________ */



typedef struct                    /* OMG_HDCS summary_header       */
        {
        char    fileID[4];           /* should be "HDCS"                      */
        int    sensorNumber;     /*  1 = depth file */
        int    subFileID;        /*  1 = data (as opposed to index) */
        int    fileVersion;      /* should normally be 1           */
					/* if it is 2 it is the new 
						packed down format
					and requires a lot of custom 
					subroutines */
			/* Feb 1998 -- Version 3 introduced */
        int    toolType;         /*
				Tool Type implies Profile Record Size
					  and     Depth   Record Size
					  and     Image   Record Size
					(actually defined in extended profile)	
					*/
        int    numProfiles;      /* # of profiles in the file             */
        int    numDepths;        /* # of depths in the file             */
        int    timeScale; 	 /* time scale (# of uSec. units) */
        int    refTime;          /* Reference time (100 sec. units)       */
        int    minTime;          /* Minimum time (offset wrt. ref.)       */
        int    maxTime;          /* Maximum time (offset wrt. ref.)       */
        int    positionType;     /* Geographic(1)/ UTM(2) */
        int    positionScale;    /* Position scale (# of nRad. units)     */
        int    refLat;           /* Reference latitude (100 nRadians)     */
        int    minLat;           /* Minimum latitude (offset wrt. ref.)   */
        int    maxLat;           /* Maximum latitude (offset wrt. ref.)   */
        int    refLong;          /* Reference longitude (100 nRadians)    */
        int    minLong;          /* Minimum longitude (offset wrt. ref.)  */
        int    maxLong;          /* Maximum longitude (offset wrt. ref.)  */
        int    minObsDepth;      /* Minimum depth (mm)                    */
        int    maxObsDepth;      /* Maximum depth (mm)                    */
        int    minProcDepth;     /* Minimum depth (mm)                    */
        int    maxProcDepth;     /* Maximum depth (mm)                    */
        int    status;     	  /*  status flags :
					 nav merged?
					 rejected line?
					 amplitudes present ?
					 imagery present ?
					 processed depths present?
				  	 depths examined by filter?	
				  	 depths examined by hydrographer?	
					 sound velocity applied ?
					 draft corrections applied ?
					 tide corrections applied ?
					 roll corrections applied ?
					 heave corrections applied ?
					 pitch corrections applied ? */
		/* status not actually used at all .... */
        } OMG_HDCS_summary_header;


#define OMG_HDCS_SUMMARY_HEADER_SIZE  96

typedef struct {
	int startrec;
	int endrec;

	int imintime;  
	int imaxtime;  
	int iminlat;  
	int imaxlat;  
	int iminlon;  
	int imaxlon;  

	double mintime;  
	double maxtime;  
	double minlat;  
	double maxlat;  
	double minlon;  
	double maxlon;  

	} OMG_HDCS_bounds;              

#define OMG_HDCS_BOUNDS_SIZE  32



typedef struct {
	int noProfiles;
	double maxLat;	
	double minLat;
	double maxLon;
	double minLon;
	int toolType;
	} OMG_rational_summary;


/* _________________ PROFILE RECORD STRUCTURE ____________________ */


/* specific FOR ATLAS SAPI data */
typedef struct OMG_HDCS_profile_subparams{

				unsigned short txBeamIndex;
				unsigned short txLevel;
				 short txBeamAngle;
				unsigned short txPulseLength;

				unsigned int ss_offset;
				unsigned short no_skipped_ss;
				unsigned short no_acquired_ss;
				unsigned short ss_sample_interval;

				unsigned short bscatClass; 
				unsigned short nrActualGainSets; 
				 short rxGup; 
				 short rxGain; 
				 short ar; 
				unsigned short rxtime[20];
				 short rxgain[20];

                                } OMG_HDCS_profile_subparams;

typedef struct OMG_HDCS_profile_header{
        int    status;           /*  status flags :
                                         rejected profile?
                                         amplitudes present ?
                                         imagery present ?
				*******  towfish data ? **********
                                         processed depths present?
                                         depths examined by filter?
                                         depths examined by hydrographer?
                                         sound velocity applied ?
                                         draft corrections applied ?
                                         tide corrections applied ?
                                         roll corrections applied ?
                                         heave corrections applied ?
                                         pitch corrections applied ? */
		/* actually status is either OK (0) 
			or	no nav (1)
			or unwanted for gridding (2) */
    
        int    numDepths;        /* Number of depths in profile        */
        int    timeOffset;       /* Time offset  wrt. header           */
        int    vesselLatOffset;   /* Latitude offset wrt. header        */
        int    vesselLongOffset;   /* Longitude offset wrt. header       */
        int    vesselHeading;      /* Heading (100 nRadians)             */
        int    vesselHeave;            /* Heave (mm)                   */
        int    vesselPitch;      /* Vessel pitch (100 nRadians)        */
        int    vesselRoll;       /* Vessel roll (100 nRadians)         */
        int    tide;             /* Tide (mm)                          */
        int    vesselVelocity;   /* Vessel Velocity (mm/s)             */
		/* transducer pitch is generally tucked into the vel field !!*/

	/* as far as we need to go for EM100 without imagery  */
	/* not any more, new EM100 ampliitude telegram is 
		full of gains and modes and ping no etc.. */

	char    power;
	char    TVG;
	char    attenuation;
	char    edflag;
        int soundVelocity; 	/* mm/s */
	int lengthImageDataField;
        int pingNo;

	char mode;  
	char Q_factor;  
	char pulseLength;   /* centisecs*/
	unsigned char unassigned;  


/* all this is added for Version 3, to cope with EM300 */

/* extra from depth telegram */
                unsigned short td_sound_speed;
                unsigned short samp_rate;

                unsigned char z_res_cm;
                unsigned char xy_res_cm;

		/* Below only exists for v3 */
/* extra from runtime telegram */
                unsigned char ssp_source;
                unsigned char filter_ID;

                unsigned short absorp_coeff;
                unsigned short tx_pulse_len;

                unsigned short tx_beam_width;
                unsigned short max_swath_width;

                unsigned char tx_power_reduction;
                unsigned char rx_beam_width;
                unsigned char rx_bandwidth;
                unsigned char rx_gain_reduction;

                unsigned char tvg_crossover;
                unsigned char beam_spacing;
                unsigned char coverage_sector;
                unsigned char yaw_stab_mode;

/* extra from HydroSweep MD2 Surf data */
		OMG_HDCS_profile_subparams params[2];


		/* ---- Towfish or dynamically stabilised transducer data ----
			(can be omitted for hull mounted fixed instruments
			 such as the Creed EM100 or EM12's)    */



        int    transducerDepth;  /* transducer or  towfish depth*/
        int    transducerPitch;  /* Transducer pitch (100 nRadians)    */
        int    transducerRoll;   /* Transducer roll (100 nRadians)     */
	/* enough for dyn. stab transducer */
        int    transducerHeading;  /* Transducer pitch (100 nRadians)    */
        int    transducerLatOffset;   /* Latitude offset wrt. vessel        */
        int    transducerLongOffset;  /* Longitude offset wrt. vessel       */
        int    transducerSlantRange; /*slantRange(mm) wrt. vessel (cable out)*/
        int    transducerAcross; /* horizontal Range (mm) wrt. vessel */
        int    transducerAlong; /* horizontal Range (mm) wrt. vessel */
        int    transducerBearing;  /* Bearing (100nRads) wrt. vessel       */
		/* noble effort to include all instruments,
			actually never used */

                                } OMG_HDCS_profile_header;

#define OMG_HDCS_PROFILE_HEADER_SIZE  26*4+216

typedef struct {
	 double time;
	 int noDepths;
	 double pingLat;
	 double pingLon;

	/* all the below at Tx ! */
	 float heading;
	 float roll;
	 float pitch;
	 float heave;
	 float mech_pitch;

	 float tide;
	 float surfaceSoundSpeed;
	 float samp_rate;
	 float draft_heavefree;

	} OMG_rational_profile;

	/* more realistic structure that is only tested for EM100/1000 */
typedef struct OMG_HDCS_packed_profile{
        int    timeOffset;       /* Time offset  wrt. header           */
        int    vesselLatOffset;   /* Latitude offset wrt. header        */
        int    vesselLongOffset;   /* Longitude offset wrt. header       */
        short    vesselHeading;      /* Heading (1 milliRadians)             */
        short    vesselHeave;            /* Heave (mm)                   */
        short    vesselPitch;      /* Vessel pitch (100 microRadians)        */
        short    vesselRoll;       /* Vessel roll (100 microRadians)         */
        short    tide;             /* Tide (mm)                          */
	short numDepths;

	char    power;
	char    TVG;
	char    attenuation;
	char pulseLength;   
	char mode;  
	unsigned char status;
	char edflag;
	char unused;
                                } OMG_HDCS_packed_profile;

typedef struct OMG_HDCS_packed_profile_v3{
        int    timeOffset;       /* Time offset  wrt. header           */
        int    vesselLatOffset;   /* Latitude offset wrt. header        */
        int    vesselLongOffset;   /* Longitude offset wrt. header       */
        short    vesselHeading;      /* Heading (1 milliRadians)             */
        short    vesselHeave;            /* Heave (mm)                   */
        short    vesselPitch;      /* Vessel pitch (100 microRadians)        */
        short    vesselRoll;       /* Vessel roll (100 microRadians)         */
        short    tide;             /* Tide (mm)                          */
	short numDepths;

	char    power;
	char    TVG;
	char    attenuation;
	char pulseLength;   
	char mode;  
	unsigned char status;
	char edflag;
	char unused;

/* extra from depth telegram */
                unsigned short td_sound_speed;
                unsigned short samp_rate;

                unsigned char z_res_cm;
                unsigned char xy_res_cm;

/* extra from runtime telegram */
                unsigned char ssp_source;
                unsigned char filter_ID;


                unsigned short absorp_coeff;
                unsigned short tx_pulse_len;

                unsigned short tx_beam_width;
                unsigned short max_swath_width;

                unsigned char tx_power_reduction;
                unsigned char rx_beam_width;
                unsigned char rx_bandwidth;
                unsigned char rx_gain_reduction;

                unsigned char tvg_crossover;
                unsigned char beam_spacing;
                unsigned char coverage_sector;
                unsigned char yaw_stab_mode;

/* extra from HydroSweep MD2 Surf data */
		OMG_HDCS_profile_subparams params[2];


                                } OMG_HDCS_packed_profile_v3;





/* _________________ BEAM RECORD STRUCTURE ____________________ */

typedef struct {
	  int status;
	  float depth;
	  float across;
	  float along;
	  double lat;
	  double lon;
	  float TWTT;
	  float beamangle;
	  float beamazimuth;
	  float tx_steer;
	  float rc_steer;
	  float rc_roll;
	  float rc_pitch;
	  float rc_mech_pitch;
	  float rc_heading;
	  float rc_heave;
	  float rc_surf_ss;
	  int no_samples;
	  int centre_no;
	  unsigned char bsdata[1024];
	} OMG_rational_beam;


/* new for getxyz2 to cope with CHS attibute demands  */

typedef struct {
/* all these are common to the ping but... */
	  double time;
	  float tide;
	  float draft;

	  int status;
	  float waterlevel_depth;
	  float reduced_depth;
	  double lat;
	  double lon;
	  float TWTT;
	  float beamangle;
	  float beamazimuth;

	} OMG_attrib_beam;

extern float supplied_draft;


typedef struct OMG_HDCS_beam {
        int    status;           /*  status flags :
                                         rejected beam?
					 lat/lon field present and calculated?
                                         amplitudes present ?
                                         imagery present ?
                                	 towfish data ? 
                                         processed depth present?
                                         depth examined by filter?
                                         depth examined by hydrographer?
                                         sound velocity applied ?
                                         draft corrections applied ?
                                         tide corrections applied ?
                                         roll corrections applied ?
                                         heave corrections applied ?
                                         pitch corrections applied ? */
		/* actually its either O.K (0) or bad (other) */
        int    observedDepth;    /* Depth (mm)                            */
	/* hereafter can be ommited for Single Beam sounder (8 bytes needed) */
        int    acrossTrack;      /* Across track position of depth (mm)   */
	/* hereafter can be ommited for seabeam (12 bytes needed) */
        int    alongTrack;       /* Along track position of depth (mm)    */
        int    latOffset;        /* Latitude offset wrt. profile          */
        int    longOffset;       /* Longitude offset wrt. profile         */
        int    processedDepth;    /* Depth (mm)                            */
	int    timeOffset;
        int    depthAccuracy;    /* Depth accuracy (mm)                   */

	/* ----------- 36 bytes per beam so far --------------- */
	/* hereafter can be ommited for EM100  (depth only) */

		unsigned char reflectivity;  
		char Q_factor;  /* phase or amplitude detection */ 
		char beam_no;  
		char freq;   /* 12.7, 13.0, 13.3, 95.0, Smii, GLORIA */

		char calibratedBackscatter; /* allows for slope footprint */ 
			/* plus looses Simrad-applied Lambertian correction */
		/* actually just removes all effects of power/TVG and atten. */
		char mindB;		
		char maxdB;
		unsigned char pseudoAngleIndependentBackscatter;		
			/* corrected for mean angular dependence
				for geological visualisation */

	/* hereafter can be ommited for EM100  (with signal strength) */

	/* ----------- another 8  bytes per beam  --------------- */

	        unsigned int range;   /* other option on EM 12 */
		/* relevant for image data */
                int no_samples;
                int offset;
                int centre_no;
		char sample_unit; /* whether in time or distance */
		char sample_interval; /* seconds or metres */
                char dummy[1];



/* all this is added for Version 3, to cope with EM300 */


                unsigned char  samp_win_length;
		short beam_depress_angle;
                unsigned short beam_heading_angle;



		/* need all this for EM-12 and EM1000. */

	/* ----------- another 20  bytes per beam  --------------- */


	/* -- creating a total of 64 bytes per beam ---- */

				} OMG_HDCS_beam;

#define OMG_HDCS_BEAM_SIZE  64


typedef struct OMG_HDCS_packed_beam{
        short    observedDepth;    /* Depth (scaled according to encoded scale factor)  */
        short    acrossTrack;      /* Across track position of depth (ditto)   */
		unsigned char status;
		unsigned char reflectivity;  
		char calibratedBackscatter; 
		unsigned char pseudoAngleIndependentBackscatter;		
		/* assumes   along track is < 1/6 of max of across and depth */
        short    alongTrack;       /* Along track position scaled and with SCaling  Factor encoded   */
	        unsigned short range;   /* other option on EM 12 */
                int offset;
                short no_samples;
                short centre_no;
				} OMG_HDCS_packed_beam;

typedef struct OMG_HDCS_packed_beam_v3{
        short    observedDepth;    /* Depth (scaled according to encoded scale factor)  */
        short    acrossTrack;      /* Across track position of depth (ditto)   */
		unsigned char status;
		unsigned char reflectivity;  
		char calibratedBackscatter; 
		unsigned char pseudoAngleIndependentBackscatter;		
        short    alongTrack;       /* Along track position ff depth (ditto)    */
	        unsigned short range;   /* other option on EM 12 */
                int offset;
                short no_samples;
                short centre_no;

		short beam_depress_angle;
                unsigned short beam_heading_angle;
                unsigned char  samp_win_length;
                unsigned char  scaling_factor;
		unsigned char Q_factor;  /* left this out of version 2 ! */
                unsigned char  v3_spare[1];


				} OMG_HDCS_packed_beam_v3;





/* ---------------- ATTITUDE --------------------------- */
/* ---------------- ATTITUDE --------------------------- */
/* ---------------- ATTITUDE --------------------------- */
/* ---------------- ATTITUDE --------------------------- */
/* ---------------- ATTITUDE --------------------------- */

typedef struct OMG_HDCS_summary_attitude              /* OMG_HDCS attitude summary    */
        {
        char    fileID[4];           /* should be "HDCS"                      */
        int    sensorNumber;     /*  2 = attitude file */
        int    subFileID;        /*  1 = data (as opposed to index) */
        int    fileVersion;      /* should normally be 1     */
        int    toolType;         /* */
        int    numRecords;      /* # of att records    */
        int    timeScale; 	  /* time scale (# of uSec. units) */
        int    refTime;          /* Reference time (100 sec. units)       */
        int    minTime;          /* Minimum time (offset wrt. ref.)       */
        int    maxTime;          /* Maximum time (offset wrt. ref.)       */
        int    status;     	  /*  status flags */
				/* 
				   1 - has roll
				   2 - has pitch
				   4 - has heading
				   8 - has heave
				   16 - has mechanical pitch 
				   32 - has surface sound speed 
				*/
        } OMG_HDCS_summary_attitude;

#define OMG_HDCS_SUMMARY_ATTITUDE_SIZE  44

typedef struct OMG_HDCS_attitude{
        int    timeOffset;       /* Time offset  wrt. header           */

/* new concept, depending on what the summary header says, the fields below 
time may or may not exist */

	signed short roll; /* 0.01 deg res */
	signed short pitch; /* 0.01 deg res */
	unsigned short heading; /* 0.01 deg res */
	signed short heave;	 /* 0.01 m res */
	signed short mech_pitch; /* 0.01 deg res +ve if tdcr pointed forward */
	unsigned short surface_ss; /* dm/s */
	} OMG_HDCS_attitude;

#define OMG_HDCS_ATTITUDE_SIZE  16

/* ---------------- ATTITUDE --------------------------- */
/* ---------------- ATTITUDE --------------------------- */
/* ---------------- ATTITUDE --------------------------- */
/* ---------------- ATTITUDE --------------------------- */
/* ---------------- ATTITUDE --------------------------- */



/* ____________________ TOOL SPECIFIC DEFINITIONS ___________________ */

#ifndef hdcs_
#define hdcs_




static char *OMG_HDCS_fileID_tag = "HDCS";

extern int use_tide_corr_flag;
extern int use_proc_corr_flag;
extern int use_ampl_shft_flag;
extern int use_lever_arm_flag;
extern location lever_arm;

extern int useallZ;

#define NO_TOOLS      30   /* currently */

#define PROFILE_LENGTH 0
#define MAX_NO_BEAMS  1
#define BEAM_LENGTH   2
#define IMAGE_LENGTH  3

static int     tool_defs[NO_TOOLS][4] = {
                                               /*  PRS   no. DRS    IRS */

                    /*   0:SingleBeam,        */  { 44 ,    1,   8  , 0    },
                    /*   1:ELAC BottomChartMk1*/  { 64 ,   56,  44  , 0    },
                    /*   2:EM12(dual)         */  { 64 ,  162,  64  , 0    },
                    /*   3:EM100 (just depths)*/  { 44 ,   32,  36  , 0    },
                    /*   4:FanSweep10 (old)   */  { 44 ,   52,  36  , 0    },
                    /*   5:Seabeam "Classic"  */  { 24 ,   19,  24  , 0    },
                    /*   6:EM3000S            */  { 64 ,  128,  64  , 0    },
                    /*   7:ROSS sweep         */  { 44 ,   16,  12  , 0    },
                    /*   8:EM12(single)       */  { 64 ,   81,  64  , 0    },
                    /*   9:EM100+Amplitudes   */  { 64 ,   32,  44  , 0    },
                    /*  10:EM1000             */  { 64 ,   60,  64  , 0    },
                    /*  11:LADS secondary     */  { 44 ,   24,  36  , 0    },
                    /*  12:EM3000D            */  { 64 ,  256,  64  , 0    },
                    /*  13:Seabeam 2100       */  { 44 ,  151,  44  , 0    },
                    /*  14:ISIS Submetrix     */  { 44 ,  128,  44  , 0    },
                    /*  15:EM-1000 (justampl) */  { 44 ,   60,  44  , 0    },
                    /*  16:SB2K               */  { 64 ,  121,  64  , 0    },
                    /*  17:Seabat9001         */  { 44 ,   60,  44  , 0    },
                    /*  18:FanSweep 10 A      */  { 64 ,   52,  64  , 0    },
                    /*  19:FanSweep 20        */  { 64 ,   1440,  64  , 0    },
                    /*  20:ISIS SWA format    */  { 64 ,   1280,  64  , 0    },
                    /*  21:SeaBeam 1180 Mk II */  { 64 ,   126,  44  , 0    },
                    /*  22:SeaBat 8101        */  { 64 ,   101,  64  , 0    },
                    /*  23:EM300              */  { 88 ,  137,  76  , 0    },
                    /*  24:EM121A             */  { 64 ,  121,  64  , 0    },
                    /*  25:SM2000             */  { 64 ,  128,  64  , 0    },
                    /*  26:HydroSweep MD2     */  { 280 ,  320,  76  , 0    },
                    /*  23:EM1002             */  { 88 ,  111,  76  , 0    },
                    /*  28:HUMMIN'BIRD        */  { 64 ,  6,  44  , 0    },
                    /*  29:Knudsen 320        */  { 00 ,  2,  0 , 0    }

                                };
static int     packed_tool_defs[NO_TOOLS][4] = {
                                               /*  PRS   no. DRS    IRS */

                    /*   0:SingleBeam,        */  { 32 ,    1,   20  , 0    },
                    /*   1:ELAC Bottom_Chart  */  { 32 ,   56,  12  , 0    },
                    /*   2:EM12(dual)         */  { 32 ,  162,  00  , 0    },
                    /*   3:EM100 (just depths)*/  { 32 ,   32,  00  , 0    },
                    /*   4:FanSweep           */  { 32 ,   52,  00  , 0    },
                    /*   5:Seabeam            */  { 32 ,   19,  10  , 0    },
                    /*   6:EM3000S            */  { 32 ,  128,  20  , 0    },
                    /*   7:ROSS sweep         */  { 32 ,   16,  10  , 0    },
                    /*   8:EM12(single)       */  { 32 ,   81,  20  , 0    },
                    /*   9:EM100+Amplitudes   */  { 32 ,   32,  12  , 0    },
                    /*  10:EM1000             */  { 32 ,   60,  20  , 0    },
                    /*  11:LADS secondary     */  { 32 ,   24,  12  , 0    },
                    /*  12:EM3000D            */  { 32 ,  256,  20  , 0    },
                    /*  13:Seabeam 2100       */  { 32 ,  151,  12  , 0    },
                    /*  14:ISIS Submetrix     */  { 32 ,  128,  20  , 0    },
                    /*  15:EM-1000 (justampl) */  { 32 ,   60,  00  , 0    },
                    /*  16:SB2K               */  { 32 ,  121,  00  , 0    },
                    /*  17:Seabat9001         */  { 32 ,   60,  12  , 0    },
                    /*  18:FanSweep 10 A      */  { 32 ,   52,  20  , 0    },
                    /*  19:FanSweep 20        */  { 32 ,   1440,  12  , 0   },
                    /*  20:ISIS SWA format    */  { 32 ,   1280,  8  , 0    },
                    /*  21:SeaBeam 1180 Mk II */  { 32 ,   126,  12  , 0    },
                    /*  22:SeaBat 8101        */  { 32 ,   101,  12  , 0    },
                    /*  23:EM300              */  { 32 ,  137,  20  , 0    },
                    /*  24:EM121A             */  { 32 ,  121,  20  , 0    },
                    /*  25:SM2000             */  { 32 ,  128,  20  , 0    },
                    /*  26:HydroSweep MD2     */  { 32 ,  320,  20  , 0    },
                    /*  27:EM1002             */  { 32 ,  111,  20  , 0    },
                    /*  28:HUMMIN'BIRD        */  { 32 ,  6,  12  , 0    },
                    /*  29:Knudsen 320        */  { 00 ,  2,  0 , 0    }

                                };

static int     packed_tool_defs_v3[NO_TOOLS][4] = {
                                               /*  PRS   no. DRS    IRS */

                    /*   0:SingleBeam,        */  { 32 ,    1,   20  , 0    },
                    /*   1:ELAC Bottom_Chart  */  { 32 ,   56,  12  , 0    },
                    /*   2:EM12(dual)         */  { 32 ,  162,  00  , 0    },
                    /*   3:EM100 (just depths)*/  { 32 ,   32,  00  , 0    },
                    /*   4:FanSweep           */  { 32 ,   52,  00  , 0    },
                    /*   5:Seabeam            */  { 32 ,   19,  10  , 0    },
                    /*   6:EM3000S            */  { 32 ,  128,  20  , 0    },
                    /*   7:ROSS sweep         */  { 32 ,   16,  10  , 0    },
                    /*   8:EM12(single)       */  { 32 ,   81,  28  , 0    },
                    /*   9:EM100+Amplitudes   */  { 32 ,   32,  12  , 0    },
                    /*  10:EM1000             */  { 56 ,   60,  28  , 0    },
                    /*  11:LADS secondary     */  { 32 ,   24,  12  , 0    },
                    /*  12:EM3000D            */  { 32 ,  256,  20  , 0    },
                    /*  13:Seabeam 2100       */  { 56 ,  151,  28  , 0    },
                    /*  14:ISIS Submetrix     */  { 36 ,  128,  28  , 0    },
                    /*  15:EM-1000 (justampl) */  { 32 ,   60,  00  , 0    },
                    /*  16:SB2K               */  { 32 ,  121,  00  , 0    },
                    /*  17:Seabat9001         */  { 32 ,   60,  20  , 0    },
                    /*  18:FanSweep 10 A      */  { 32 ,   52,  20  , 0    },
                    /*  19:FanSweep 20        */  { 32 ,   1440,  12  , 0   },
                    /*  20:ISIS SWA format    */  { 32 ,   1280,  8  , 0    },
                    /*  21:SeaBeam 1180 Mk II */  { 32 ,   126,  12  , 0    },
                    /*  22:SeaBat 8101        */  { 32 ,   101,  12  , 0    },
							/* note using the correct 135 */
                    /*  23:EM300              */  { 56 ,  135,  28  , 0    },
                    /*  24:EM121A             */  { 32 ,  121,  20  , 0    },
                    /*  25:SM2000             */  { 56 ,  128,  28  , 0    },
                    /*  26:HydroSweep MD2     */  { 272 ,  320,  28  , 0    },
                    /*  27:EM1002             */  { 56 ,  111,  28  , 0    },
                    /*  28:HUMMIN'BIRD        */  { 32 ,  6,  28  , 0    },
                    /*  29:Knudsen 320        */  { 36 ,  2,  28, 0    }

                                };


#define SingleBeam        0
#define ELAC_BottomChart  1
#define EM12_dual         2
#define EM100_depth       3
#define FanSweep          4
#define SeaBeam           5
#define EM3000            6
#define ROSS_Profiler 	  7
#define EM12_single       8
#define EM100_depth_ss    9
#define EM1000           10
#define LADS_2ndary      11
#define EM3000D 	 12
#define SB2100           13
#define ISIS_Submetrix   14
#define EM1000_ampl      15
#define SB2K      	 16
#define Seabat9001     	 17
#define FanSweep_10A   	 18
#define FanSweep_20    	 19
#define ISIS_SWA    	 20
#define SeaBeam_1180_MkII 21
#define SeaBat_8101 	22
#define EM300 	23
#define EM121A 	24
#define SM2000 	25
#define HydroSweep_MD2	26
#define EM1002	27
#define Humminbird	28
#define Knudsen_320	29


static char   *tool_names[NO_TOOLS] = {
                                "Single Beam Echo-Sounder",
                                "HoneyWell Elac BottomChart Mk I",
                                "Simrad EM-12 (dual system)",
                                "Simrad EM-100 (depths only)",
                                "Krupp-Atlas FanSweep 10",
                                "General Instruments SeaBeam (Classic)",
                                "Simrad - EM3000S",
                                "ROSS Sweep - MV Profiler DPW",
                                "Simrad EM-12 (single system)",
                                "Simrad EM-100 (depths and amplitudes)",
                                "Simrad EM-1000",
                                "RAN -- LADS (secondary format) ",
                                "Simrad - EM3000 dual head",
                                "SeaBeam 2100 series",
                                "ISIS Submetrix 100/ 2000",
                                "EM1000 with reflectivities",
                                "Seabeam 2000 ",
                                "Reson Seabat 9001 ",
                                "STN-Atlas FanSweep 10A",
                                "STN-Atlas FanSweep 20",
                                "ISIS Submetrix 100SWA",
                                "SeaBeam 1180 MkII",
                                "SeaBat 8101",
                                "Simrad EM300",
                                "Simrad EM121A",
                                "Simrad SM2000",
                                "HydroSweep MD2",
                                "Simrad EM1002",
                                "Hummin'bird 3D",
                                "Knudsen 320"
                                };




#endif 


/* ------------------------------------------------------ */
/* ------------------------------------------------------ */
/* ------------------------------------------------------ */
 /* -------------- SOME SUBROUTINE DECLARATIONS --------------------------- */

void    OMG_HDCS_write_summary(FILE *file);

int OMG_HDCS_offset(int toolType, unsigned int record);
int OMG_HDCS_packed_offset(int toolType, unsigned int record);
int OMG_HDCS_packed_offset_v3(int toolType, unsigned int record);

int OMG_HDCS_display(FILE *file, unsigned int record);
int OMG_HDCS_display_profile(FILE *file, unsigned int record);

int OMG_HDCS_dump_summary(FILE *file, OMG_HDCS_summary_header *summary);
int OMG_HDCS_read_summary(FILE *file, OMG_HDCS_summary_header *summary);


int OMG_HDCS_write_profile(FILE *file, int profile_no, 
			OMG_HDCS_profile_header *profile);
int OMG_HDCS_read_profile(FILE *file, int profile_no, 
			OMG_HDCS_profile_header *profile);


int OMG_HDCS_time_search(FILE *file, double wanttime, int *recnum);

int OMG_HDCS_get_raw_beams(FILE *file, int recnum, int *no_beams,
        OMG_HDCS_beam *beams);

int OMG_HDCS_put_raw_beams(FILE *file, int recnum, int no_beams,
        OMG_HDCS_beam *beams);

int OMG_HDCS_get_imagedata(FILE *file, int recnum, char *data);
int OMG_HDCS_put_imagedata(FILE *file, int recnum, char *data);

int OMG_HDCS_get_xyz_beams(FILE *file,int recnum, int *no_beams, double *xyz);
int OMG_HDCS_get_xyz2_beams(FILE *file, int recnum, int *no_beams, OMG_attrib_beam *abeam);


/* new subroutines to pack old verbose structure into new compressed
		structure */

void packdown_profile ( OMG_HDCS_profile_header profile,
                         OMG_HDCS_packed_profile *pprof);

void packdown_beam ( OMG_HDCS_beam beam,
                         OMG_HDCS_packed_beam *pbeam);

void unpack_profile ( OMG_HDCS_packed_profile profile,
                         OMG_HDCS_profile_header *uprof);

void unpack_beam ( OMG_HDCS_packed_beam beam,
                         OMG_HDCS_beam *ubeam);



/* NEW FOR VERSION 3 */

void packdown_profile_v3 ( OMG_HDCS_profile_header profile,
                         OMG_HDCS_packed_profile_v3 *pprof);

void packdown_beam_v3 ( OMG_HDCS_beam beam,
                         OMG_HDCS_packed_beam_v3 *pbeam);

void unpack_profile_v3 ( OMG_HDCS_packed_profile_v3 profile,
                         OMG_HDCS_profile_header *uprof);

void unpack_beam_v3 ( OMG_HDCS_packed_beam_v3 beam,
                         OMG_HDCS_beam *ubeam);




void add_beam_location(OMG_HDCS_beam *beam, OMG_HDCS_profile_header *profile,
                        OMG_HDCS_summary_header *summary);

void locate_beam(OMG_HDCS_beam *beam, OMG_HDCS_profile_header *profile,
                        OMG_HDCS_summary_header *summary,
		double *lat, double *lon);
void locate_transducer(OMG_HDCS_profile_header *profile,
                        OMG_HDCS_summary_header *summary,
		double *lat, double *lon);

/* ------------------------------------------------------ */
/* ------------------------------------------------------ */
 /* ASYNCHRONOUS TELEGRAM FUNCTIONS */
int OMG_HDCS_dump_att_summary(FILE *file, OMG_HDCS_summary_attitude *att_summary);
int OMG_HDCS_read_att_summary(FILE *file, OMG_HDCS_summary_attitude *att_summary);

int OMG_HDCS_write_attitude(FILE *file, int profile_no, 
			OMG_HDCS_attitude *attitude);
int OMG_HDCS_read_attitude(FILE *file, int profile_no, 
			OMG_HDCS_attitude *attitude);

int OMG_HDCS_att_time_search(FILE *file, double wanttime, int *recnum);

int OMG_HDCS_get_interpolated_async
                (FILE *file, double the_time, double max_time_gap, double delay,
                        float *roll, float *pitch, float *heading,
                        float *heave, float *mech_pitch,
                        float *surf_ss, int *beyond_att_flag,
                        double *beforetime, double *aftertime, int *closest_recno);
/* ------------------------------------------------------ */
/* ------------------------------------------------------ */


void OMG_get_rational_summary(FILE *file, OMG_rational_summary *ratsum);
void OMG_get_rational_profile(FILE *file, int profnum, 
		OMG_rational_profile *ratprof);
void OMG_get_rational_beams(FILE *file, int profnum, 
	int *no_beams, OMG_rational_beam *ratbeams);
void OMG_get_rational_beams_plus_trace(FILE *file, int profnum, 
	int *no_beams, OMG_rational_beam *ratbeams, FILE *tracefile);


void OMG_put_rational_beams(FILE *file, int profnum, 
	int no_beams, OMG_rational_beam *ratbeams);

/* ------------------------------------------------------ */
/* ------------------------------------------------------ */

int OMG_HDCS_dump_bounds(FILE *file, int recno, OMG_HDCS_bounds bounds);
int OMG_HDCS_read_bounds(FILE *file, int recno, OMG_HDCS_bounds *bounds);
int OMG_HDCS_interp_bounds(OMG_HDCS_bounds *bounds, OMG_HDCS_summary_header summary);
int OMG_HDCS_bounds_intersect_area(OMG_HDCS_bounds bounds, JHC_header head);
int OMG_HDCS_bounds_intersect_subarea(OMG_HDCS_bounds bounds, JHC_header head,
        double xmin, double xmax, double ymin, double ymax);
/* ------------------------------------------------------ */
/* ------------------------------------------------------ */


#endif
/* EOF */


