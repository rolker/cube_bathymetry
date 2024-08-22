/* -------------------------------------------------


	OMG_HDCS_jversion.c		John E. Hughes Clarke	Nov 27 1991.

	subroutine file for  new proposed HDCS merged 


   ------------------------------------------------- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "OMG_HDCS_jversion.h"
#include "support.h"
#include "swapio.h"
#include "ref_coeffs.h"
#include "DCM_rthand.h"
#include "j_proj.h"
#include "j_phi.h"
#include "backscatter.h"

int use_tide_corr_flag;
int use_proc_corr_flag;
int use_ampl_shft_flag;
int use_lever_arm_flag;
location lever_arm;

int useallZ;

float supplied_draft;

 /* ----------------------------------------------------
  ---------------------------------------------------- */
int  OMG_HDCS_dump_summary(FILE *file, OMG_HDCS_summary_header *summary)
{
char data[OMG_HDCS_SUMMARY_HEADER_SIZE];
int offset=0;


	memcpy(&data[offset], summary->fileID,4); offset +=4;
	put_swapped_int(&data[offset],&(summary->sensorNumber)); offset +=4;
	put_swapped_int(&data[offset],&(summary->subFileID)); offset +=4;
	put_swapped_int(&data[offset],&(summary->fileVersion)); offset +=4;
	put_swapped_int(&data[offset],&(summary->toolType)); offset +=4;
	put_swapped_int(&data[offset],&(summary->numProfiles)); offset +=4;
	put_swapped_int(&data[offset],&(summary->numDepths)); offset +=4;
	put_swapped_int(&data[offset],&(summary->timeScale)); offset +=4;
	put_swapped_int(&data[offset],&(summary->refTime)); offset +=4;
	put_swapped_int(&data[offset],&(summary->minTime)); offset +=4;
	put_swapped_int(&data[offset],&(summary->maxTime)); offset +=4;
	put_swapped_int(&data[offset],&(summary->positionType)); offset +=4;
	put_swapped_int(&data[offset],&(summary->positionScale)); offset +=4;
	put_swapped_int(&data[offset],&(summary->refLat)); offset +=4;
	put_swapped_int(&data[offset],&(summary->minLat)); offset +=4;
	put_swapped_int(&data[offset],&(summary->maxLat)); offset +=4;
	put_swapped_int(&data[offset],&(summary->refLong)); offset +=4;
	put_swapped_int(&data[offset],&(summary->minLong)); offset +=4;
	put_swapped_int(&data[offset],&(summary->maxLong)); offset +=4;
	put_swapped_int(&data[offset],&(summary->minObsDepth)); offset +=4;
	put_swapped_int(&data[offset],&(summary->maxObsDepth)); offset +=4;
	put_swapped_int(&data[offset],&(summary->minProcDepth)); offset +=4;
	put_swapped_int(&data[offset],&(summary->maxProcDepth)); offset +=4;
	put_swapped_int(&data[offset],&(summary->status));
	
	fseek(file,0,0);
        fwrite(data,OMG_HDCS_SUMMARY_HEADER_SIZE,1,file);
	return(0);
}

 /* ----------------------------------------------------
  ---------------------------------------------------- */
int  OMG_HDCS_read_summary(FILE *file, OMG_HDCS_summary_header *summary)
{
char data[OMG_HDCS_SUMMARY_HEADER_SIZE];
int offset=0;

	fseek(file,0,0);
        fread(data,OMG_HDCS_SUMMARY_HEADER_SIZE,1,file);

	memcpy(summary->fileID,&data[0],4); offset +=4;
	get_swapped_int(&data[offset],&(summary->sensorNumber)); offset +=4;
	get_swapped_int(&data[offset],&(summary->subFileID)); offset +=4;
	get_swapped_int(&data[offset],&(summary->fileVersion)); offset +=4;
	get_swapped_int(&data[offset],&(summary->toolType)); offset +=4;
	get_swapped_int(&data[offset],&(summary->numProfiles)); offset +=4;
	get_swapped_int(&data[offset],&(summary->numDepths)); offset +=4;
	get_swapped_int(&data[offset],&(summary->timeScale)); offset +=4;
	get_swapped_int(&data[offset],&(summary->refTime)); offset +=4;
	get_swapped_int(&data[offset],&(summary->minTime)); offset +=4;
	get_swapped_int(&data[offset],&(summary->maxTime)); offset +=4;
	get_swapped_int(&data[offset],&(summary->positionType)); offset +=4;
	get_swapped_int(&data[offset],&(summary->positionScale)); offset +=4;
	get_swapped_int(&data[offset],&(summary->refLat)); offset +=4;
	get_swapped_int(&data[offset],&(summary->minLat)); offset +=4;
	get_swapped_int(&data[offset],&(summary->maxLat)); offset +=4;
	get_swapped_int(&data[offset],&(summary->refLong)); offset +=4;
	get_swapped_int(&data[offset],&(summary->minLong)); offset +=4;
	get_swapped_int(&data[offset],&(summary->maxLong)); offset +=4;
	get_swapped_int(&data[offset],&(summary->minObsDepth)); offset +=4;
	get_swapped_int(&data[offset],&(summary->maxObsDepth)); offset +=4;
	get_swapped_int(&data[offset],&(summary->minProcDepth)); offset +=4;
	get_swapped_int(&data[offset],&(summary->maxProcDepth)); offset +=4;
	get_swapped_int(&data[offset],&(summary->status));
	return(0);
}



 /* ----------------------------------------------------
	this dosent actually write!!! just prints out.,
	confusing...  use dump_summary to write.
  ---------------------------------------------------- */


void  OMG_HDCS_write_summary(FILE *file)
{
  OMG_HDCS_summary_header  summary;
  int diff_1970_1980;
  char datestring[30];
  double  ref_lat, ref_lon;


	OMG_HDCS_read_summary(file, &summary);

        if (strncmp(summary.fileID, OMG_HDCS_fileID_tag, 4)) 
			error("not an HDCS format file (write_summary)");

	if(summary.sensorNumber != 1) error("not a HDCS depth file");
	if(summary.subFileID != 1) error("not a HDCS depth data file");
	if(summary.fileVersion != 1 ) warning("not a HDCS Version 1 file");

	printf(" File Version : %d\n", summary.fileVersion);
	printf(" Tool Type : %s\n", tool_names[summary.toolType]);

        printf("profileRecordSize      : %d\n", 
		tool_defs[summary.toolType][PROFILE_LENGTH]);
        printf("BeamRecordSize         : %d\n", 
		tool_defs[summary.toolType][BEAM_LENGTH]);
        printf("ImageRecordSize        : %d\n", 
		tool_defs[summary.toolType][IMAGE_LENGTH]);
        printf("Max no. Beams          : %d\n", 
		tool_defs[summary.toolType][MAX_NO_BEAMS]);
        printf("numProfiles            : %d\n", summary.numProfiles);
        printf("numDepths              : %d\n", summary.numDepths);
        printf("timeScale              : %d\n", summary.timeScale);
        printf("refTime                : %d\n", summary.refTime);
        diff_1970_1980 = 10*365*24*60*60;
/*        stdtime_nicetime(datestring,"%W %M %D %Y %T",
                (int)((summary.refTime*100)+diff_1970_1980)); 
        printf(" reference Time : %s\n", datestring); */
        printf("minTime                : %d\n", summary.minTime);
        printf("maxTime                : %d\n", summary.maxTime);
        printf("positionType          : %d\n", summary.positionType);
        printf("positionScale          : %d\n", summary.positionScale);
/*
        printf("refLat                 : %d\n", summary.refLat);
        printf("minLat                 : %d\n", summary.minLat);
        printf("maxLat                 : %d\n", summary.maxLat);
        printf("refLong                : %d\n", summary.refLong);
        printf("minLong                : %d\n", summary.minLong);
        printf("maxLong                : %d\n", summary.maxLong);
*/
        ref_lat =  ((double)summary.refLat /10000000.0) *180.0/M_PI;
        ref_lon =  ((double)summary.refLong /10000000.0) *180.0/M_PI;
        printf("ref lat %f lon %f\n",  ref_lat, ref_lon);
        printf("min lat %f lon %f\n", 
	  ref_lat +((double)summary.minLat /1000000000.0) *180.0/M_PI,
          ref_lon +((double)summary.minLong /1000000000.0) *180.0/M_PI);
        printf("max lat %f lon %f\n", 
	  ref_lat +((double)summary.maxLat /1000000000.0) *180.0/M_PI,
          ref_lon +((double)summary.maxLong /1000000000.0) *180.0/M_PI);
        printf("minProcDepth           : %d\n", summary.minProcDepth);
        printf("maxProcDepth           : %d\n", summary.maxProcDepth);
        printf("minObsDepth            : %d\n", summary.minObsDepth);
        printf("maxObsDepth            : %d\n", summary.maxObsDepth);
}


/* -------------------------------------------------------

  ------------------------------------------------------- */
int OMG_HDCS_offset(int toolType, unsigned int record)
{
  int offset;

	if(toolType > NO_TOOLS-1) error("invalid tool type");

        offset = OMG_HDCS_SUMMARY_HEADER_SIZE +
	(
	tool_defs[toolType][PROFILE_LENGTH]+
	tool_defs[toolType][MAX_NO_BEAMS]*tool_defs[toolType][BEAM_LENGTH]+
	tool_defs[toolType][IMAGE_LENGTH]
	) * (int)record; 


	return(offset);
}
/* -------------------------------------------------------

  ------------------------------------------------------- */
int OMG_HDCS_packed_offset(int toolType, unsigned int record)
{
  int offset;

	if(toolType > NO_TOOLS-1) error("invalid tool type");

        offset = OMG_HDCS_SUMMARY_HEADER_SIZE +
	(
	packed_tool_defs[toolType][PROFILE_LENGTH]+
	packed_tool_defs[toolType][MAX_NO_BEAMS]*packed_tool_defs[toolType][BEAM_LENGTH]+
	packed_tool_defs[toolType][IMAGE_LENGTH]
	) * (int)record; 


	return(offset);
}
/* -------------------------------------------------------
  ------------------------------------------------------- */
int OMG_HDCS_packed_offset_v3(int toolType, unsigned int record)
{
  int offset;

	if(toolType > NO_TOOLS-1) error("invalid tool type");

        offset = OMG_HDCS_SUMMARY_HEADER_SIZE +
	(
	packed_tool_defs_v3[toolType][PROFILE_LENGTH]+
	packed_tool_defs_v3[toolType][MAX_NO_BEAMS]*packed_tool_defs_v3[toolType][BEAM_LENGTH]+
	packed_tool_defs_v3[toolType][IMAGE_LENGTH]
	) * (int)record; 


	return(offset);
}
/* -------------------------------------------------------

  ------------------------------------------------------- */

int OMG_HDCS_display_profile(FILE *file, unsigned int record)
{
  OMG_HDCS_summary_header  summary;
  OMG_HDCS_profile_header  profile;
  OMG_HDCS_beam            beam;
  int offset;
  int j;
  double  lat, lon, depth, pdepth;

	OMG_HDCS_read_summary(file, &summary);

        if (strncmp(summary.fileID, OMG_HDCS_fileID_tag,4)) 
			error("not an HDCS format file (display)");

	if(record >= summary.numProfiles) {
	  printf("asked for profile off end of file\n");
          return(0);
	}

        OMG_HDCS_read_profile(file,record, &profile);

        printf("timeOffset             : %d\n", profile.timeOffset);
        printf("numDepths              : %d\n", profile.numDepths);
	locate_transducer(&profile, &summary, &lat, &lon);
        printf("vessel position        : %f %f\n", lat, lon);
/*        printf("latOffset              : %d\n", profile.vesselLatOffset);
        printf("longOffset             : %d\n", profile.vesselLongOffset); */
        printf("heading                : %d\n", profile.vesselHeading);
        printf("vesselVelocity         : %d\n", profile.vesselVelocity);
        printf("vesselHeave            : %d\n", profile.vesselHeave);
        printf("vesselPitch            : %d\n", profile.vesselPitch);
        printf("vesselRoll             : %d\n", profile.vesselRoll);
        printf("tide                   : %d\n", profile.tide);
	if(tool_defs[summary.toolType][PROFILE_LENGTH] > 44) {
        printf("EM100 Power            : %d\n", profile.power);
        printf("EM100 TVG              : %d\n", profile.TVG);
        printf("EM100 Attenuation      : %d\n", profile.attenuation);

        printf("soundVelocity          : %d\n", profile.soundVelocity);
        printf("lengthImageDataField   : %d\n", profile.lengthImageDataField);
        printf("pingNo                 : %d\n", profile.pingNo);
        printf("mode                   : %d\n", profile.mode);
        printf("Q_factor               : %d\n", profile.Q_factor);
        printf("pulseLength            : %d\n", profile.pulseLength);
	}

/*
	if(tool_defs[summary.toolType][PROFILE_LENGTH] > 64) {
        printf("transducerDepth        : %d\n", profile.transducerDepth);
        printf("transducerPitch        : %d\n", profile.transducerPitch);
        printf("transducerRoll         : %d\n", profile.transducerRoll);
        printf("transducerHeading      : %d\n", profile.transducerHeading);
        printf("transducerLatOffset    : %d\n", profile.transducerLatOffset);
        printf("transducerLongOffset   : %d\n", profile.transducerLongOffset);
        printf("transducerSlantRange   : %d\n", profile.transducerSlantRange);
        printf("transducerAcross       : %d\n", profile.transducerAcross);
        printf("transducerAlong        : %d\n", profile.transducerAlong);
        printf("transducerBearing      : %d\n", profile.transducerBearing);
	}
*/
        if(summary.toolType == HydroSweep_MD2) {


        printf(" MD2 spec: %d %d %d %d\n",
                profile.params[0].txBeamIndex,
                profile.params[0].txLevel,
                profile.params[0].txBeamAngle,
                profile.params[0].txPulseLength);
        printf(" MD2 spec: %d %d %d %d\n",
                profile.params[0].ss_offset,
                profile.params[0].no_acquired_ss,
                profile.params[0].no_skipped_ss,
                profile.params[0].ss_sample_interval);
        printf(" MD2 spec: %d %d %d %d %d\n\n",
                profile.params[0].bscatClass,
                profile.params[0].nrActualGainSets,
                profile.params[0].rxGup,
                profile.params[0].rxGain,
                profile.params[0].ar);

        printf(" MD2 spec: %d %d %d %d\n",
                profile.params[1].txBeamIndex,
                profile.params[1].txLevel,
                profile.params[1].txBeamAngle,
                profile.params[1].txPulseLength);
        printf(" MD2 spec: %d %d %d %d\n",
                profile.params[1].ss_offset,
                profile.params[1].no_acquired_ss,
                profile.params[1].no_skipped_ss,
                profile.params[1].ss_sample_interval);
        printf(" MD2 spec: %d %d %d %d %d\n\n",
                profile.params[1].bscatClass,
                profile.params[1].nrActualGainSets,
                profile.params[1].rxGup,
                profile.params[1].rxGain,
                profile.params[1].ar);
	}



	return(0);
}
int OMG_HDCS_display(FILE *file, unsigned int record)
{
  OMG_HDCS_summary_header  summary;
  OMG_HDCS_profile_header  profile;
  OMG_HDCS_beam            beams[1440];
  int offset;
  int actual_no_beams;
  int j;
  double  lat, lon, depth, pdepth;

	OMG_HDCS_read_summary(file, &summary);

        if (strncmp(summary.fileID, OMG_HDCS_fileID_tag,4)) 
			error("not an HDCS format file (display)");

	if(record >= summary.numProfiles) {
	  printf("asked for profile off end of file\n");
          return(0);
	}

        OMG_HDCS_read_profile(file,record, &profile);

        printf("timeOffset             : %d\n", profile.timeOffset);
        printf("numDepths              : %d\n", profile.numDepths);
	locate_transducer(&profile, &summary, &lat, &lon);
        printf("vessel position        : %f %f\n", lat, lon);
/*        printf("latOffset              : %d\n", profile.vesselLatOffset);
        printf("longOffset             : %d\n", profile.vesselLongOffset); */
        printf("heading                : %d\n", profile.vesselHeading);
        printf("vesselVelocity         : %d\n", profile.vesselVelocity);
        printf("vesselHeave            : %d\n", profile.vesselHeave);
        printf("vesselPitch            : %d\n", profile.vesselPitch);
        printf("vesselRoll             : %d\n", profile.vesselRoll);
        printf("tide                   : %d\n", profile.tide);
	if(tool_defs[summary.toolType][PROFILE_LENGTH] > 44) {
/*
        printf("centreDepth            : %d\n", profile.centreDepth);
*/
        printf("EM100 Power            : %d\n", profile.power);
        printf("EM100 TVG              : %d\n", profile.TVG);
        printf("EM100 Attenuation      : %d\n", profile.attenuation);

        printf("soundVelocity          : %d\n", profile.soundVelocity);
        printf("lengthImageDataField   : %d\n", profile.lengthImageDataField);
        printf("pingNo                 : %d\n", profile.pingNo);
        printf("mode                   : %d\n", profile.mode);
        printf("Q_factor               : %d\n", profile.Q_factor);
        printf("pulseLength            : %d\n", profile.pulseLength);
	}
	if(tool_defs[summary.toolType][PROFILE_LENGTH] > 64) {
        printf("transducerDepth        : %d\n", profile.transducerDepth);
        printf("transducerPitch        : %d\n", profile.transducerPitch);
        printf("transducerRoll         : %d\n", profile.transducerRoll);
        printf("transducerHeading      : %d\n", profile.transducerHeading);
        printf("transducerLatOffset    : %d\n", profile.transducerLatOffset);
        printf("transducerLongOffset   : %d\n", profile.transducerLongOffset);
        printf("transducerSlantRange   : %d\n", profile.transducerSlantRange);
        printf("transducerAcross       : %d\n", profile.transducerAcross);
        printf("transducerAlong        : %d\n", profile.transducerAlong);
        printf("transducerBearing      : %d\n", profile.transducerBearing);
	}

            OMG_HDCS_get_raw_beams(file,record,
                        &actual_no_beams, beams);

            for(j=0;j<summary.numDepths;j++) {
/*
            for(j=0;j<profile.numDepths;j++) {
*/
/* have to think that lat lon may not be present !!! */
	locate_beam(&beams[j], &profile, &summary, &lat, &lon);
	      depth = -(float)beams[j].observedDepth*
		(float)summary.positionScale/1000.;
	      pdepth = -(float)beams[j].processedDepth*
		(float)summary.positionScale/1000.;
        printf("beams[j]  %3d  position : %13.8f %13.8f  ", j, lat, lon);
        printf(" along/across Track  : %10.3f %10.3f  ", 
		(float)beams[j].alongTrack/1000.0, 
		(float)beams[j].acrossTrack/1000.0);
        printf("Depth  : %7.2f %7.2f ", depth, pdepth);
	if(tool_defs[summary.toolType][BEAM_LENGTH] > 36) {
        printf("refl %3d  ", beams[j].reflectivity);  
        printf("pAIB %3d  ", beams[j].pseudoAngleIndependentBackscatter);;  
        printf("Qfac %3d  ", beams[j].Q_factor);;  
	}
	if(tool_defs[summary.toolType][BEAM_LENGTH] > 44) {
        printf("range  %5d  ", beams[j].range);  
        printf("nosam  %3d  ", beams[j].no_samples);  
        printf("off  %4d  ", beams[j].offset);  
        printf("ceno  %3d  ", beams[j].centre_no);  
	}
	printf("\n");
/*
        printf("timeOffset             : %d\n", beams[j].timeOffset); 
        printf("status;       
        printf("depthAccuracy;   
        printf("processedDepth;   
	
        printf("reflectivity;  
        printf("Q_factor;  
        printf("beam_no;  
        printf("freq;  
        printf("calibratedBackscatter;  
        printf("mindB;  
        printf("maxdB;  
        printf("pseudoAngleIndependentBackscatter;  

        printf("range;  
        printf("no_samples;
        printf("offset;
        printf("centre_no;
        printf("sample_unit; 
        printf("sample_interval;
*/ 
            } 

	return(1);
}

/* -------------------------------------------------------

  ------------------------------------------------------- */
int OMG_HDCS_read_profile(FILE *file, int profile_no, 
	OMG_HDCS_profile_header *profile)
{
  char data[OMG_HDCS_PROFILE_HEADER_SIZE];
  OMG_HDCS_summary_header summary;
  OMG_HDCS_packed_profile packed_profile;
  OMG_HDCS_packed_profile_v3 packed_profile_v3;
  int offset, byte_off;
  int read=0;
  int i,j;


	memset(data,0,OMG_HDCS_PROFILE_HEADER_SIZE);

	OMG_HDCS_read_summary(file, &summary);


        if(profile_no < 0 || profile_no >= summary.numProfiles) {
          return(0);
        }
	if(summary.fileVersion == 1) {

	offset = OMG_HDCS_offset(summary.toolType, profile_no);
        fseek(file, offset, 0);

        read = fread(data,tool_defs[summary.toolType][PROFILE_LENGTH], 
		1,file);
	byte_off = 0;
	get_swapped_int(&data[byte_off],&(profile->status)); byte_off +=4;
	get_swapped_int(&data[byte_off],&(profile->numDepths)); byte_off +=4;
	get_swapped_int(&data[byte_off],&(profile->timeOffset)); byte_off +=4;
	get_swapped_int(&data[byte_off],&(profile->vesselLatOffset)); byte_off +=4;
	get_swapped_int(&data[byte_off],&(profile->vesselLongOffset)); byte_off +=4;
	get_swapped_int(&data[byte_off],&(profile->vesselHeading)); byte_off +=4;
	get_swapped_int(&data[byte_off],&(profile->vesselHeave)); byte_off +=4;
	get_swapped_int(&data[byte_off],&(profile->vesselPitch)); byte_off +=4;
	get_swapped_int(&data[byte_off],&(profile->vesselRoll)); byte_off +=4;
	get_swapped_int(&data[byte_off],&(profile->tide)); byte_off +=4;
	get_swapped_int(&data[byte_off],&(profile->vesselVelocity)); byte_off +=4;
	memcpy(&(profile->power), &data[byte_off],1); byte_off +=1;
	memcpy(&(profile->TVG), &data[byte_off],1); byte_off +=1;
	memcpy(&(profile->attenuation), &data[byte_off],1); byte_off +=1;
	memcpy(&(profile->edflag), &data[byte_off],1); byte_off +=1;
	get_swapped_int(&data[byte_off],&(profile->soundVelocity)); byte_off +=4;
	get_swapped_int(&data[byte_off],&(profile->lengthImageDataField)); byte_off +=4;
	get_swapped_int(&data[byte_off],&(profile->pingNo)); byte_off +=4;
	memcpy(&(profile->mode), &data[byte_off],1); byte_off +=1;
	memcpy(&(profile->Q_factor), &data[byte_off],1); byte_off +=1;
	memcpy(&(profile->pulseLength), &data[byte_off],1); byte_off +=1;
	memcpy(&(profile->unassigned), &data[byte_off],1); byte_off +=1;

/* AS WE DONT EVER USE THESE IM HARD_COMMENTED THEM OUT 
	get_swapped_int(&data[byte_off],&(profile->transducerDepth)); byte_off +=4;
	get_swapped_int(&data[byte_off],&(profile->transducerPitch)); byte_off +=4;
	get_swapped_int(&data[byte_off],&(profile->transducerRoll)); byte_off +=4;
	get_swapped_int(&data[byte_off],&(profile->transducerHeading)); byte_off +=4;
	get_swapped_int(&data[byte_off],&(profile->transducerLatOffset)); byte_off +=4;
	get_swapped_int(&data[byte_off],&(profile->transducerLongOffset)); byte_off +=4;
	get_swapped_int(&data[byte_off],&(profile->transducerSlantRange)); byte_off +=4;
	get_swapped_int(&data[byte_off],&(profile->transducerAcross)); byte_off +=4;
	get_swapped_int(&data[byte_off],&(profile->transducerAlong)); byte_off +=4;
	get_swapped_int(&data[byte_off],&(profile->transducerBearing)); byte_off +=4;
*/
	  return(read);

	} else if(summary.fileVersion == 2) {
	offset = OMG_HDCS_packed_offset(summary.toolType, profile_no);
        fseek(file, offset, 0);

        read= fread(data,
		packed_tool_defs[summary.toolType][PROFILE_LENGTH], 
		1,file);
	byte_off=0;
	get_swapped_int(&data[byte_off],&packed_profile.timeOffset); 
			byte_off +=4;
	get_swapped_int(&data[byte_off],&packed_profile.vesselLatOffset); 
			byte_off +=4;
	get_swapped_int(&data[byte_off],&packed_profile.vesselLongOffset); 
			byte_off +=4;
	get_swapped_short(&data[byte_off],&packed_profile.vesselHeading); 
			byte_off +=2;
	get_swapped_short(&data[byte_off],&packed_profile.vesselHeave); 
			byte_off +=2;
	get_swapped_short(&data[byte_off],&packed_profile.vesselPitch); 
			byte_off +=2;
	get_swapped_short(&data[byte_off],&packed_profile.vesselRoll); 
			byte_off +=2;
	get_swapped_short(&data[byte_off],&packed_profile.tide); 
			byte_off +=2;
	get_swapped_short(&data[byte_off],&packed_profile.numDepths); 
			byte_off +=2;
	memcpy(&packed_profile.power, &data[byte_off],1); byte_off +=1;
	memcpy(&packed_profile.TVG, &data[byte_off],1); byte_off +=1;
	memcpy(&packed_profile.attenuation, &data[byte_off],1); byte_off +=1;
	memcpy(&packed_profile.pulseLength, &data[byte_off],1); byte_off +=1;
	memcpy(&packed_profile.mode, &data[byte_off],1); byte_off +=1;
	memcpy(&packed_profile.status, &data[byte_off],1); byte_off +=1;
	memcpy(&packed_profile.edflag, &data[byte_off],1); byte_off +=1;
	memcpy(&packed_profile.unused, &data[byte_off],1); byte_off +=1;

	unpack_profile(packed_profile, profile);
	return(read);

	} else if(summary.fileVersion == 3) {

	offset = OMG_HDCS_packed_offset_v3(summary.toolType, profile_no);
        fseek(file, offset, 0);

        read= fread(data,
		packed_tool_defs_v3[summary.toolType][PROFILE_LENGTH], 
		1,file);
	byte_off=0;

        get_swapped_int(&data[byte_off],&packed_profile_v3.timeOffset);
                        byte_off +=4;
        get_swapped_int(&data[byte_off],&packed_profile_v3.vesselLatOffset);
                        byte_off +=4;
        get_swapped_int(&data[byte_off],&packed_profile_v3.vesselLongOffset);
                        byte_off +=4;
        get_swapped_short(&data[byte_off],&packed_profile_v3.vesselHeading);
                        byte_off +=2;
        get_swapped_short(&data[byte_off],&packed_profile_v3.vesselHeave);
                        byte_off +=2;
        get_swapped_short(&data[byte_off],&packed_profile_v3.vesselPitch);
                        byte_off +=2;
        get_swapped_short(&data[byte_off],&packed_profile_v3.vesselRoll);
                        byte_off +=2;
        get_swapped_short(&data[byte_off],&packed_profile_v3.tide);
                        byte_off +=2;
        get_swapped_short(&data[byte_off],&packed_profile_v3.numDepths);
                        byte_off +=2;
        memcpy(&packed_profile_v3.power, &data[byte_off],1); byte_off +=1;
        memcpy(&packed_profile_v3.TVG, &data[byte_off],1); byte_off +=1;
        memcpy(&packed_profile_v3.attenuation, &data[byte_off],1); byte_off +=1;
        memcpy(&packed_profile_v3.pulseLength, &data[byte_off],1); byte_off +=1;
        memcpy(&packed_profile_v3.mode, &data[byte_off],1); byte_off +=1;
        memcpy(&packed_profile_v3.status, &data[byte_off],1); byte_off +=1;
        memcpy(&packed_profile_v3.edflag, &data[byte_off],1); byte_off +=1;
        memcpy(&packed_profile_v3.unused, &data[byte_off],1); byte_off +=1;


        get_swapped_short(&data[byte_off],&packed_profile_v3.td_sound_speed);
                        byte_off +=2;
        get_swapped_short(&data[byte_off],&packed_profile_v3.samp_rate);
                        byte_off +=2;

        memcpy(&packed_profile_v3.z_res_cm, &data[byte_off],1); byte_off +=1;
        memcpy(&packed_profile_v3.xy_res_cm, &data[byte_off],1); byte_off +=1;
        memcpy(&packed_profile_v3.ssp_source, &data[byte_off],1); byte_off +=1;
        memcpy(&packed_profile_v3.filter_ID, &data[byte_off],1); byte_off +=1;


        get_swapped_short(&data[byte_off],&packed_profile_v3.absorp_coeff);
                        byte_off +=2;
        get_swapped_short(&data[byte_off],&packed_profile_v3.tx_pulse_len);
                        byte_off +=2;

        get_swapped_short(&data[byte_off],&packed_profile_v3.tx_beam_width);
                        byte_off +=2;
        get_swapped_short(&data[byte_off],&packed_profile_v3.max_swath_width);
                        byte_off +=2;

        memcpy(&packed_profile_v3.tx_power_reduction, &data[byte_off],1); byte_off +=1;
        memcpy(&packed_profile_v3.rx_beam_width, &data[byte_off],1); byte_off +=1;
        memcpy(&packed_profile_v3.rx_bandwidth, &data[byte_off],1); byte_off +=1;
        memcpy(&packed_profile_v3.rx_gain_reduction, &data[byte_off],1); byte_off +=1;

        memcpy(&packed_profile_v3.tvg_crossover, &data[byte_off],1); byte_off +=1;
        memcpy(&packed_profile_v3.beam_spacing, &data[byte_off],1); byte_off +=1;
        memcpy(&packed_profile_v3.coverage_sector, &data[byte_off],1); byte_off +=1;
        memcpy(&packed_profile_v3.yaw_stab_mode, &data[byte_off],1); byte_off +=1;


	for(j=0;j<2;j++) {
        get_swapped_short(&data[byte_off],&packed_profile_v3.params[j].txBeamIndex);
                        byte_off +=2;
        get_swapped_short(&data[byte_off],&packed_profile_v3.params[j].txLevel);
                        byte_off +=2;
        get_swapped_short(&data[byte_off],&packed_profile_v3.params[j].txBeamAngle);
                        byte_off +=2;
        get_swapped_short(&data[byte_off],&packed_profile_v3.params[j].txPulseLength);
                        byte_off +=2;

        get_swapped_int(&data[byte_off],&packed_profile_v3.params[j].ss_offset);
                        byte_off +=4;
        get_swapped_short(&data[byte_off],&packed_profile_v3.params[j].no_skipped_ss);
                        byte_off +=2;
        get_swapped_short(&data[byte_off],&packed_profile_v3.params[j].no_acquired_ss);
                        byte_off +=2;
        get_swapped_short(&data[byte_off],&packed_profile_v3.params[j].ss_sample_interval);
                        byte_off +=2;

        get_swapped_short(&data[byte_off],&packed_profile_v3.params[j].bscatClass);
                        byte_off +=2;
        get_swapped_short(&data[byte_off],&packed_profile_v3.params[j].nrActualGainSets);
                        byte_off +=2;
        get_swapped_short(&data[byte_off],&packed_profile_v3.params[j].rxGup);
                        byte_off +=2;
        get_swapped_short(&data[byte_off],&packed_profile_v3.params[j].rxGain);
                        byte_off +=2;
        get_swapped_short(&data[byte_off],&packed_profile_v3.params[j].ar);
                        byte_off +=2;

	for(i=0;i<20;i++)  {
        get_swapped_short(&data[byte_off],&packed_profile_v3.params[j].rxtime[i]);
                        byte_off +=2;
	}
	for(i=0;i<20;i++)  {
        get_swapped_short(&data[byte_off],&packed_profile_v3.params[j].rxgain[i]);
                        byte_off +=2;
	}

	}


        unpack_profile_v3(packed_profile_v3, profile);
        return(read);





	}
	return(0);
}

/* -------------------------------------------------------

  ------------------------------------------------------- */
int OMG_HDCS_write_profile(FILE *file, int profile_no, 
		OMG_HDCS_profile_header *profile)
{
  char data[OMG_HDCS_PROFILE_HEADER_SIZE];
  OMG_HDCS_summary_header summary;
  OMG_HDCS_packed_profile packed_profile;
  OMG_HDCS_packed_profile_v3 packed_profile_v3;
  int offset, byte_off;
  int i,j;

	OMG_HDCS_read_summary(file, &summary);

	if(profile_no < 0 ) {
	  return(0);
	}
        if( profile_no >= summary.numProfiles) {
/*
	 printf(" %d appending - ", summary.numProfiles);   
*/
	summary.numProfiles = profile_no+1;
	OMG_HDCS_dump_summary(file, &summary);
	}

	if(summary.fileVersion == 1) {

	byte_off = 0;
	put_swapped_int(&data[byte_off],&(profile->status)); byte_off +=4;
	put_swapped_int(&data[byte_off],&(profile->numDepths)); byte_off +=4;
	put_swapped_int(&data[byte_off],&(profile->timeOffset)); byte_off +=4;
	put_swapped_int(&data[byte_off],&(profile->vesselLatOffset)); byte_off +=4;
	put_swapped_int(&data[byte_off],&(profile->vesselLongOffset)); byte_off +=4;
	put_swapped_int(&data[byte_off],&(profile->vesselHeading)); byte_off +=4;
	put_swapped_int(&data[byte_off],&(profile->vesselHeave)); byte_off +=4;
	put_swapped_int(&data[byte_off],&(profile->vesselPitch)); byte_off +=4;
	put_swapped_int(&data[byte_off],&(profile->vesselRoll)); byte_off +=4;
	put_swapped_int(&data[byte_off],&(profile->tide)); byte_off +=4;
	put_swapped_int(&data[byte_off],&(profile->vesselVelocity)); byte_off +=4;
	memcpy(&data[byte_off], &(profile->power),1); byte_off +=1;
	memcpy(&data[byte_off], &(profile->TVG),1); byte_off +=1;
	memcpy(&data[byte_off], &(profile->attenuation),1); byte_off +=1;
	memcpy(&data[byte_off], &(profile->edflag),1); byte_off +=1;
	put_swapped_int(&data[byte_off],&(profile->soundVelocity)); byte_off +=4;
	put_swapped_int(&data[byte_off],&(profile->lengthImageDataField)); byte_off +=4;
	put_swapped_int(&data[byte_off],&(profile->pingNo)); byte_off +=4;
	memcpy(&data[byte_off], &(profile->mode),1); byte_off +=1;
	memcpy(&data[byte_off], &(profile->Q_factor),1); byte_off +=1;
	memcpy(&data[byte_off], &(profile->pulseLength),1); byte_off +=1;
	memcpy(&data[byte_off], &(profile->unassigned),1); byte_off +=1;

/* AS WE DONT EVER USE THESE IM HARD_COMMENTED THEM OUT 
	put_swapped_int(&data[byte_off],&(profile->transducerDepth)); byte_off +=4;
	put_swapped_int(&data[byte_off],&(profile->transducerPitch)); byte_off +=4;
	put_swapped_int(&data[byte_off],&(profile->transducerRoll)); byte_off +=4;
	put_swapped_int(&data[byte_off],&(profile->transducerHeading)); byte_off +=4;
	put_swapped_int(&data[byte_off],&(profile->transducerLatOffset)); byte_off +=4;
	put_swapped_int(&data[byte_off],&(profile->transducerLongOffset)); byte_off +=4;
	put_swapped_int(&data[byte_off],&(profile->transducerSlantRange)); byte_off +=4;
	put_swapped_int(&data[byte_off],&(profile->transducerAcross)); byte_off +=4;
	put_swapped_int(&data[byte_off],&(profile->transducerAlong)); byte_off +=4;
	put_swapped_int(&data[byte_off],&(profile->transducerBearing)); byte_off +=4;
*/

	offset = OMG_HDCS_offset(summary.toolType, profile_no);
        fseek(file, offset, 0);
        return(fwrite(data,tool_defs[summary.toolType][PROFILE_LENGTH], 
		1,file));

	} else if(summary.fileVersion == 2) {

	packdown_profile(*profile, &packed_profile);

	byte_off=0;
	put_swapped_int(&data[byte_off],&packed_profile.timeOffset); 
			byte_off +=4;
	put_swapped_int(&data[byte_off],&packed_profile.vesselLatOffset); 
			byte_off +=4;
	put_swapped_int(&data[byte_off],&packed_profile.vesselLongOffset); 
			byte_off +=4;
	put_swapped_short(&data[byte_off],&packed_profile.vesselHeading); 
			byte_off +=2;
	put_swapped_short(&data[byte_off],&packed_profile.vesselHeave); 
			byte_off +=2;
	put_swapped_short(&data[byte_off],&packed_profile.vesselPitch); 
			byte_off +=2;
	put_swapped_short(&data[byte_off],&packed_profile.vesselRoll); 
			byte_off +=2;
	put_swapped_short(&data[byte_off],&packed_profile.tide); 
			byte_off +=2;
	put_swapped_short(&data[byte_off],&packed_profile.numDepths); 
			byte_off +=2;
	memcpy(&data[byte_off], &packed_profile.power,1); byte_off +=1;
	memcpy(&data[byte_off], &packed_profile.TVG,1); byte_off +=1;
	memcpy(&data[byte_off], &packed_profile.attenuation,1); byte_off +=1;
	memcpy(&data[byte_off], &packed_profile.pulseLength,1); byte_off +=1;
	memcpy(&data[byte_off], &packed_profile.mode,1); byte_off +=1;
	memcpy(&data[byte_off], &packed_profile.status,1); byte_off +=1;
	memcpy(&data[byte_off], &packed_profile.edflag,1); byte_off +=1;
	memcpy(&data[byte_off], &packed_profile.unused,1); byte_off +=1;

	offset = OMG_HDCS_packed_offset(summary.toolType, profile_no);
        fseek(file, offset, 0);
        return(fwrite(data,
		packed_tool_defs[summary.toolType][PROFILE_LENGTH], 
		1,file));

        } else if(summary.fileVersion == 3) {

        packdown_profile_v3(*profile, &packed_profile_v3);

        byte_off=0;
        put_swapped_int(&data[byte_off],&packed_profile_v3.timeOffset);
                        byte_off +=4;
        put_swapped_int(&data[byte_off],&packed_profile_v3.vesselLatOffset);
                        byte_off +=4;
        put_swapped_int(&data[byte_off],&packed_profile_v3.vesselLongOffset);
                        byte_off +=4;
        put_swapped_short(&data[byte_off],&packed_profile_v3.vesselHeading);
                        byte_off +=2;
        put_swapped_short(&data[byte_off],&packed_profile_v3.vesselHeave);
                        byte_off +=2;
        put_swapped_short(&data[byte_off],&packed_profile_v3.vesselPitch);
                        byte_off +=2;
        put_swapped_short(&data[byte_off],&packed_profile_v3.vesselRoll);
                        byte_off +=2;
        put_swapped_short(&data[byte_off],&packed_profile_v3.tide);
                        byte_off +=2;
        put_swapped_short(&data[byte_off],&packed_profile_v3.numDepths);
                        byte_off +=2;
        memcpy(&data[byte_off], &packed_profile_v3.power,1); byte_off +=1;
        memcpy(&data[byte_off], &packed_profile_v3.TVG,1); byte_off +=1;
        memcpy(&data[byte_off], &packed_profile_v3.attenuation,1); byte_off +=1;
        memcpy(&data[byte_off], &packed_profile_v3.pulseLength,1); byte_off +=1;
        memcpy(&data[byte_off], &packed_profile_v3.mode,1); byte_off +=1;
        memcpy(&data[byte_off], &packed_profile_v3.status,1); byte_off +=1;
        memcpy(&data[byte_off], &packed_profile_v3.edflag,1); byte_off +=1;
        memcpy(&data[byte_off], &packed_profile_v3.unused,1); byte_off +=1;



        put_swapped_short(&data[byte_off],&packed_profile_v3.td_sound_speed);
                        byte_off +=2;
        put_swapped_short(&data[byte_off],&packed_profile_v3.samp_rate);
                        byte_off +=2;

        memcpy(&data[byte_off], &packed_profile_v3.z_res_cm,1); byte_off +=1;
        memcpy(&data[byte_off], &packed_profile_v3.xy_res_cm,1); byte_off +=1;
        memcpy(&data[byte_off], &packed_profile_v3.ssp_source,1); byte_off +=1;
        memcpy(&data[byte_off], &packed_profile_v3.filter_ID,1); byte_off +=1;



        put_swapped_short(&data[byte_off],&packed_profile_v3.absorp_coeff);
                        byte_off +=2;
        put_swapped_short(&data[byte_off],&packed_profile_v3.tx_pulse_len);
                        byte_off +=2;

        put_swapped_short(&data[byte_off],&packed_profile_v3.tx_beam_width);
                        byte_off +=2;
        put_swapped_short(&data[byte_off],&packed_profile_v3.max_swath_width);
                        byte_off +=2;

        memcpy(&data[byte_off], &packed_profile_v3.tx_power_reduction,1); byte_off +=1;
        memcpy(&data[byte_off], &packed_profile_v3.rx_beam_width,1); byte_off +=1;
        memcpy(&data[byte_off], &packed_profile_v3.rx_bandwidth,1); byte_off +=1;
        memcpy(&data[byte_off], &packed_profile_v3.rx_gain_reduction,1); byte_off +=1;

        memcpy(&data[byte_off], &packed_profile_v3.tvg_crossover,1); byte_off +=1;
        memcpy(&data[byte_off], &packed_profile_v3.beam_spacing,1); byte_off +=1;
        memcpy(&data[byte_off], &packed_profile_v3.coverage_sector,1); byte_off +=1;
        memcpy(&data[byte_off], &packed_profile_v3.yaw_stab_mode,1); byte_off +=1;

        for(j=0;j<2;j++) {
        put_swapped_short(&data[byte_off],&packed_profile_v3.params[j].txBeamIndex);
                        byte_off +=2;
        put_swapped_short(&data[byte_off],&packed_profile_v3.params[j].txLevel);
                        byte_off +=2;
        put_swapped_short(&data[byte_off],&packed_profile_v3.params[j].txBeamAngle);
                        byte_off +=2;
        put_swapped_short(&data[byte_off],&packed_profile_v3.params[j].txPulseLength);
                        byte_off +=2;

        put_swapped_int(&data[byte_off],&packed_profile_v3.params[j].ss_offset);
                        byte_off +=4;
        put_swapped_short(&data[byte_off],&packed_profile_v3.params[j].no_skipped_ss);
                        byte_off +=2;
        put_swapped_short(&data[byte_off],&packed_profile_v3.params[j].no_acquired_ss);
                        byte_off +=2;
        put_swapped_short(&data[byte_off],&packed_profile_v3.params[j].ss_sample_interval);
                        byte_off +=2;

        put_swapped_short(&data[byte_off],&packed_profile_v3.params[j].bscatClass);
                        byte_off +=2;
        put_swapped_short(&data[byte_off],&packed_profile_v3.params[j].nrActualGainSets);
                        byte_off +=2;
        put_swapped_short(&data[byte_off],&packed_profile_v3.params[j].rxGup);
                        byte_off +=2;
        put_swapped_short(&data[byte_off],&packed_profile_v3.params[j].rxGain);
                        byte_off +=2;
        put_swapped_short(&data[byte_off],&packed_profile_v3.params[j].ar);
                        byte_off +=2;

        for(i=0;i<20;i++) {
        put_swapped_short(&data[byte_off],&packed_profile_v3.params[j].rxtime[i]);
                        byte_off +=2;
	}
        for(i=0;i<20;i++) {
        put_swapped_short(&data[byte_off],&packed_profile_v3.params[j].rxgain[i]);
                        byte_off +=2;
	}

        }




        offset = OMG_HDCS_packed_offset_v3(summary.toolType, profile_no);
        fseek(file, offset, 0);
        return(fwrite(data,
                packed_tool_defs_v3[summary.toolType][PROFILE_LENGTH],
                1,file));



	}
	return(0);
}

/* -------------------------------------------------------

  ------------------------------------------------------- */
void packdown_profile ( OMG_HDCS_profile_header profile, 
			 OMG_HDCS_packed_profile *pprof)
{
		pprof->status = profile.status;	
		pprof->timeOffset = profile.timeOffset;	
		pprof->vesselLatOffset = profile.vesselLatOffset;	
		pprof->vesselLongOffset = profile.vesselLongOffset;	
		pprof->vesselHeading = profile.vesselHeading/10000;
		pprof->vesselHeave = profile.vesselHeave;	
		pprof->vesselPitch = profile.vesselPitch/1000;	
		pprof->vesselRoll = profile.vesselRoll/1000;	
		pprof->tide = profile.tide;	
		pprof->power = profile.power;	
		pprof->TVG = profile.TVG;	
		pprof->attenuation = profile.attenuation;	
		pprof->pulseLength = profile.pulseLength;	
		pprof->mode = profile.mode;	
		pprof->status = profile.status;	
		pprof->numDepths = profile.numDepths;	
		pprof->edflag = profile.edflag;	
		pprof->unused = profile.unassigned;	
}
/* -------------------------------------------------------

  ------------------------------------------------------- */
void packdown_profile_v3 ( OMG_HDCS_profile_header profile, 
			 OMG_HDCS_packed_profile_v3 *pprof)
{
		pprof->status = profile.status;	
		pprof->timeOffset = profile.timeOffset;	
		pprof->vesselLatOffset = profile.vesselLatOffset;	
		pprof->vesselLongOffset = profile.vesselLongOffset;	
		pprof->vesselHeading = profile.vesselHeading/10000;
		pprof->vesselHeave = profile.vesselHeave;	
		pprof->vesselPitch = profile.vesselPitch/1000;	
		pprof->vesselRoll = profile.vesselRoll/1000;	
		pprof->tide = profile.tide;	
		pprof->power = profile.power;	
		pprof->TVG = profile.TVG;	
		pprof->attenuation = profile.attenuation;	
		pprof->pulseLength = profile.pulseLength;	
		pprof->mode = profile.mode;	
		pprof->status = profile.status;	
		pprof->numDepths = profile.numDepths;	
		pprof->edflag = profile.edflag;	
		pprof->unused = profile.unassigned;	

		pprof->td_sound_speed = profile.td_sound_speed;	
		pprof->samp_rate = profile.samp_rate;	
		pprof->z_res_cm = profile.z_res_cm;	
		pprof->xy_res_cm = profile.xy_res_cm;	
		pprof->ssp_source = profile.ssp_source;	
		pprof->filter_ID = profile.filter_ID;	
		pprof->absorp_coeff = profile.absorp_coeff;	
		pprof->tx_pulse_len = profile.tx_pulse_len;	
		pprof->tx_beam_width = profile.tx_beam_width;	
		pprof->max_swath_width = profile.max_swath_width;	
		pprof->tx_power_reduction = profile.tx_power_reduction;	
		pprof->rx_beam_width = profile.rx_beam_width;	
		pprof->rx_bandwidth = profile.rx_bandwidth;	
		pprof->rx_gain_reduction = profile.rx_gain_reduction;	
		pprof->tvg_crossover = profile.tvg_crossover;	
		pprof->beam_spacing = profile.beam_spacing;	
		pprof->coverage_sector = profile.coverage_sector;	
		pprof->yaw_stab_mode = profile.yaw_stab_mode;	


		memcpy(&pprof->params[0], &profile.params[0], sizeof(OMG_HDCS_profile_subparams));
		memcpy(&pprof->params[1], &profile.params[1], sizeof(OMG_HDCS_profile_subparams));

}
/* -------------------------------------------------------

  ------------------------------------------------------- */
void packdown_beam ( OMG_HDCS_beam beam, 
			 OMG_HDCS_packed_beam *pbeam)
{
  int ScaleFactor;
  int MaxVal;
/* if maximum of abs(acrossTrack) and abs(observedDepth)
		   less than     30m, 1mm res,
	           less than    300m, 1cm res,
	           less than  3,000m, 1dm res,
	           less than 30,000m, 1m res */

	if(abs(beam.acrossTrack) > abs(beam.observedDepth))
		MaxVal = abs(beam.acrossTrack);
	else 
		MaxVal = abs(beam.observedDepth);

	if(MaxVal < 30000 )ScaleFactor = 1;
	else if(MaxVal < 300000 )ScaleFactor = 10;
	else if(MaxVal < 3000000 )ScaleFactor = 100;
	else ScaleFactor = 1000;

		pbeam->observedDepth = beam.observedDepth/ScaleFactor;	
		pbeam->acrossTrack = beam.acrossTrack/ScaleFactor;	
		pbeam->alongTrack = beam.alongTrack/ScaleFactor;	

/* encode the scale factor into the along track */
	        if(ScaleFactor == 1) pbeam->alongTrack += -20000;
	        else if(ScaleFactor == 10) pbeam->alongTrack += -10000;
	        else if(ScaleFactor == 100) pbeam->alongTrack += 0;
	        else if(ScaleFactor == 1000) pbeam->alongTrack += 10000;
/* note that we are assuming that the along track will never be more than
one sixth of the Maximum value in the data  
	and so as to be backward compatible the default is decimetres */
		pbeam->status = beam.status;	
		pbeam->reflectivity = beam.reflectivity/2;
		if((unsigned char)beam.Q_factor> 127) 
			pbeam->reflectivity += 128;	
		pbeam->calibratedBackscatter = beam.calibratedBackscatter;	
		pbeam->pseudoAngleIndependentBackscatter = 
			beam.pseudoAngleIndependentBackscatter;	
		pbeam->range = beam.range;	
		pbeam->offset = beam.offset;	
		pbeam->no_samples = beam.no_samples;	
		pbeam->centre_no = beam.centre_no;	
}

/* -------------------------------------------------------

  ------------------------------------------------------- */

void packdown_beam_v3 ( OMG_HDCS_beam beam,
                         OMG_HDCS_packed_beam_v3 *pbeam)
{
  int ScaleFactor;
  int MaxVal;
/* if maximum of abs(acrossTrack) and abs(observedDepth)
                   less than     32m, 1mm res,
                   less than     64m, 2mm res,
                   less than    128m, 4mm res,
                   less than  4,096m, 12.8cm res   
                   less than 40,960m, 1.28m res */

        if(abs(beam.acrossTrack) > abs(beam.observedDepth))
                MaxVal = abs(beam.acrossTrack);
        else
                MaxVal = abs(beam.observedDepth);



        if(MaxVal < 32000 ){
		ScaleFactor = pow(2.0,0.0);
		pbeam->scaling_factor = 0;
        } else if(MaxVal < 64000 ){
		ScaleFactor = pow(2.0,1.0);
		pbeam->scaling_factor = 1;
        } else if(MaxVal < 128000 ){
		ScaleFactor = pow(2.0,2.0);
		pbeam->scaling_factor = 2;
        } else if(MaxVal < 256000 ){
		ScaleFactor = pow(2.0,3.0);
		pbeam->scaling_factor = 3;
        } else if(MaxVal < 512000 ){
		ScaleFactor = pow(2.0,4.0);
		pbeam->scaling_factor = 4;
        } else if(MaxVal < 1024000 ){
		ScaleFactor = pow(2.0,5.0);
		pbeam->scaling_factor = 5;
        } else if(MaxVal < 2048000 ){
		ScaleFactor = pow(2.0,6.0);
		pbeam->scaling_factor = 6;
        } else if(MaxVal < 4096000 ){
		ScaleFactor = pow(2.0,7.0);
		pbeam->scaling_factor = 7;
        } else if(MaxVal < 8192000 ){
		ScaleFactor = pow(2.0,8.0);
		pbeam->scaling_factor = 8;
        } else if(MaxVal < 32768000 ){
		ScaleFactor = pow(2.0,10.0);
		pbeam->scaling_factor = 10;
	} else {
		error(" Never dreamed you would want a range of > 32 km! %d %d (dep ,acr)",
			beam.acrossTrack, beam.observedDepth);
	}

/*
	printf(" %.2f - sf %d %d\n", (float)MaxVal/1000.0, ScaleFactor, pbeam->scaling_factor);
*/
                pbeam->observedDepth = beam.observedDepth/ScaleFactor;
                pbeam->acrossTrack = beam.acrossTrack/ScaleFactor;
                pbeam->alongTrack = beam.alongTrack/ScaleFactor;

                pbeam->status = beam.status;
                pbeam->reflectivity = beam.reflectivity;
                pbeam->Q_factor = beam.Q_factor;
                pbeam->calibratedBackscatter = beam.calibratedBackscatter;
                pbeam->pseudoAngleIndependentBackscatter =
                        beam.pseudoAngleIndependentBackscatter;
                pbeam->range = beam.range;
                pbeam->offset = beam.offset;
                pbeam->no_samples = beam.no_samples;
                pbeam->centre_no = beam.centre_no;


		pbeam->beam_depress_angle = beam.beam_depress_angle;	
		pbeam->beam_heading_angle = beam.beam_heading_angle;	
		pbeam->samp_win_length = beam.samp_win_length;	
}
/* -------------------------------------------------------

  ------------------------------------------------------- */
void unpack_profile ( OMG_HDCS_packed_profile profile, 
			 OMG_HDCS_profile_header *uprof)
{
		uprof->status = profile.status;	
		uprof->timeOffset = profile.timeOffset;	
		uprof->vesselLatOffset = profile.vesselLatOffset;	
		uprof->vesselLongOffset = profile.vesselLongOffset;	
		uprof->vesselHeading = profile.vesselHeading*10000;
		uprof->vesselHeave = profile.vesselHeave;	
		uprof->vesselPitch = profile.vesselPitch*1000;	
		uprof->vesselRoll = profile.vesselRoll*1000;	
		uprof->tide = profile.tide;	
		uprof->power = profile.power;	
		uprof->TVG = profile.TVG;	
		uprof->attenuation = profile.attenuation;	
		uprof->pulseLength = profile.pulseLength;	
		uprof->mode = profile.mode;	
		uprof->status = profile.status;	
		uprof->numDepths = profile.numDepths;	
		uprof->edflag = profile.edflag;	
		uprof->unassigned = profile.unused;	
}
/* -------------------------------------------------------

  ------------------------------------------------------- */
void unpack_profile_v3 ( OMG_HDCS_packed_profile_v3 profile, 
			 OMG_HDCS_profile_header *uprof)
{
		uprof->status = profile.status;	
		uprof->timeOffset = profile.timeOffset;	
		uprof->vesselLatOffset = profile.vesselLatOffset;	
		uprof->vesselLongOffset = profile.vesselLongOffset;	
		uprof->vesselHeading = profile.vesselHeading*10000;
		uprof->vesselHeave = profile.vesselHeave;	
		uprof->vesselPitch = profile.vesselPitch*1000;	
		uprof->vesselRoll = profile.vesselRoll*1000;	
		uprof->tide = profile.tide;	
		uprof->power = profile.power;	
		uprof->TVG = profile.TVG;	
		uprof->attenuation = profile.attenuation;	
		uprof->pulseLength = profile.pulseLength;	
		uprof->mode = profile.mode;	
		uprof->status = profile.status;	
		uprof->numDepths = profile.numDepths;	
		uprof->edflag = profile.edflag;	
		uprof->unassigned = profile.unused;	

                uprof->td_sound_speed = profile.td_sound_speed;
                uprof->samp_rate = profile.samp_rate;
                uprof->z_res_cm = profile.z_res_cm;
                uprof->xy_res_cm = profile.xy_res_cm;
                uprof->ssp_source = profile.ssp_source;
                uprof->filter_ID = profile.filter_ID;
                uprof->absorp_coeff = profile.absorp_coeff;
                uprof->tx_pulse_len = profile.tx_pulse_len;
                uprof->tx_beam_width = profile.tx_beam_width;
                uprof->max_swath_width = profile.max_swath_width;
                uprof->tx_power_reduction = profile.tx_power_reduction;
                uprof->rx_beam_width = profile.rx_beam_width;
                uprof->rx_bandwidth = profile.rx_bandwidth;
                uprof->rx_gain_reduction = profile.rx_gain_reduction;
                uprof->tvg_crossover = profile.tvg_crossover;
                uprof->beam_spacing = profile.beam_spacing;
                uprof->coverage_sector = profile.coverage_sector;
                uprof->yaw_stab_mode = profile.yaw_stab_mode;

		memcpy(&uprof->params[0], &profile.params[0], sizeof(OMG_HDCS_profile_subparams));
		memcpy(&uprof->params[1], &profile.params[1], sizeof(OMG_HDCS_profile_subparams));
}


/* -------------------------------------------------------

  ------------------------------------------------------- */
void unpack_beam ( OMG_HDCS_packed_beam beam, 
			 OMG_HDCS_beam *ubeam )
{
  int ScaleFactor;

/* back the scaling factor out of the Along Track value */
	if(beam.alongTrack < -13000) {
		ScaleFactor = 1;
		beam.alongTrack +=20000;
	} else if(beam.alongTrack < -5000) {
		ScaleFactor = 10;
		beam.alongTrack +=10000;
	} else if(beam.alongTrack < 5000) {
		ScaleFactor = 100;
		beam.alongTrack +=0000;
	} else if(beam.alongTrack < 15000) {
		ScaleFactor = 1000;
		beam.alongTrack -=10000;
	}
		ubeam->observedDepth = beam.observedDepth*ScaleFactor;	
		ubeam->acrossTrack = beam.acrossTrack*ScaleFactor;	
		ubeam->alongTrack = beam.alongTrack*ScaleFactor;	
		ubeam->status = beam.status;	
        	if(beam.reflectivity > 127) 
        	ubeam->reflectivity = (beam.reflectivity-128)*2;
        	else 
        	ubeam->reflectivity = beam.reflectivity*2;
        	ubeam->Q_factor = beam.reflectivity;
		ubeam->calibratedBackscatter = beam.calibratedBackscatter;	
		ubeam->pseudoAngleIndependentBackscatter = 
		beam.pseudoAngleIndependentBackscatter;	
		ubeam->range = beam.range;	
		ubeam->offset = beam.offset;	
		ubeam->no_samples = beam.no_samples;	
		ubeam->centre_no = beam.centre_no;	




}
/* -------------------------------------------------------

  ------------------------------------------------------- */
void unpack_beam_v3 ( OMG_HDCS_packed_beam_v3 beam, 
			 OMG_HDCS_beam *ubeam )
{
  int ScaleFactor;


/* hopefully more robust way of storing the scaling factor,
	the scale factor is the power of 2 used as the ScaleFactor */
		ScaleFactor = pow(2.0,(double)beam.scaling_factor);

/*
	printf(" JJJJJJJJ back - sf %d %d\n",  ScaleFactor, beam.scaling_factor);
*/
/*
	printf(" back - sf %d %d\n",  ScaleFactor, beam.scaling_factor);
*/
		ubeam->observedDepth = beam.observedDepth*ScaleFactor;	
/*
	printf(" JJJJJJJJ depth %d %d\n",  beam.observedDepth, ubeam->observedDepth);
*/
		ubeam->acrossTrack = beam.acrossTrack*ScaleFactor;	
		ubeam->alongTrack = beam.alongTrack*ScaleFactor;	

		ubeam->status = beam.status;	
        	ubeam->reflectivity = beam.reflectivity;
        	ubeam->Q_factor = beam.Q_factor;
		ubeam->calibratedBackscatter = beam.calibratedBackscatter;	
		ubeam->pseudoAngleIndependentBackscatter = 
		beam.pseudoAngleIndependentBackscatter;	
		ubeam->range = beam.range;	
		ubeam->offset = beam.offset;	
		ubeam->no_samples = beam.no_samples;	
		ubeam->centre_no = beam.centre_no;	

		ubeam->beam_depress_angle = beam.beam_depress_angle;	
		ubeam->beam_heading_angle = beam.beam_heading_angle;	
		ubeam->samp_win_length = beam.samp_win_length;	





}

/* -------------------------------------------------------

  ------------------------------------------------------- */
void add_beam_location(OMG_HDCS_beam *beam, OMG_HDCS_profile_header *profile,
			OMG_HDCS_summary_header *summary)
{
double ref_lat, heading;;

        ref_lat =  ((double)summary->refLat /10000000.0) *180.0/M_PI;

/*
		heading =
		(double)profile->vesselHeading*1.0E-07+M_PI/2.0;

	printf("adding beam location : %d %f -- %f %f\n",
		profile->vesselHeading,
		heading*180.0/M_PI, cos(heading), sin(heading));
*/

		beam->latOffset = 
          (cos((double)profile->vesselHeading*1.0E-07+M_PI/2.0)
                         * ((double)beam->acrossTrack/1000.0)
                         * (1.0/(1853.0*60.0))
                         * (M_PI/180.0) * 1.0E09    )
                         +
          (cos((double)profile->vesselHeading*1.0E-07)
                         * ((double)beam->alongTrack/1000.0)
                         * (1.0/(1853.0*60.0))
                         * (M_PI/180.0) * 1.0E09);
        	beam->longOffset =
          (sin((double)profile->vesselHeading*1.0E-07+M_PI/2.0)
                         * ((double)beam->acrossTrack/1000.0)
                         * (1.0/cos(ref_lat*M_PI/180.0))
                         * (1.0/(1853.0*60.0))
                         * (M_PI/180.0) * 1.0E09   )
                         +
          (sin((double)profile->vesselHeading*1.0E-07)
                         * ((double)beam->alongTrack/1000.0)
                         * (1.0/cos(ref_lat*M_PI/180.0))
                         * (1.0/(1853.0*60.0))
                         * (M_PI/180.0) * 1.0E09);


}



/* -------------------------------------------------------

  ------------------------------------------------------- */
int OMG_HDCS_time_search (FILE *file, double wanttime, int *recnum)
{ 
/*-------------------------------------------------------------------------
Perform binary search for time (standard time (1970).)
	The alg for a binary search is pretty simple :
	lo = 0
	hi = last rec

	while (lo != hi)
		rn = (hi+lo)/2
		r= rec(rn)
		if (r.date < date)
			lo = rn
		else if (r.date > date)
			hi = rn
		else lo=hi=rn
----------------------------------------------------------------------*/

  OMG_HDCS_summary_header  summary;
  OMG_HDCS_profile_header  profile;
	int length, offset;
	int rn = 0;
	int hi,lo,rc;

	double  foundtime;

	/* first determine how many records in the file */
	OMG_HDCS_read_summary(file, &summary);

	length = summary.numProfiles;
	hi = rc = length-1;
	lo = 0;

	if (hi<0) return 0;




	do {
		rn = (hi+lo)/2;
        OMG_HDCS_read_profile(file,rn, &profile);

		foundtime = (double)profile.timeOffset*
			    (double)summary.timeScale*1.0e-6+
			    (double)summary.refTime*100.0;

		if (foundtime < wanttime)
			lo = rn+1;
		else if (foundtime > wanttime)
			hi = rn;
		else 
			lo = hi = rn;
	} while (hi > lo);
                     
/*!!!!!! careful have chosen always to get the one after !!!!! */
	if (foundtime < wanttime) rn++;

/* be careful not to let an invalid number escape */
	if (rn<0) rn=0;		/* should never happen */
	if (rn>rc) rn=rc;
	
	*recnum = rn;

	return 1;	
}  
/* -------------------------------------------------------

  ------------------------------------------------------- */
int OMG_HDCS_att_time_search (FILE *file, double wanttime, int *recnum)
{ 
/*-------------------------------------------------------------------------
Perform binary search for time (standard time (1970).)
	The alg for a binary search is pretty simple :
	lo = 0
	hi = last rec

	while (lo != hi)
		rn = (hi+lo)/2
		r= rec(rn)
		if (r.date < date)
			lo = rn
		else if (r.date > date)
			hi = rn
		else lo=hi=rn
----------------------------------------------------------------------*/

  OMG_HDCS_summary_attitude  att_summary;
  OMG_HDCS_attitude  attitude;
	int length, offset;
	int rn = 0;
	int hi,lo,rc;

	double  foundtime;

	/* first determine how many records in the file */
	OMG_HDCS_read_att_summary(file, &att_summary);

	length = att_summary.numRecords;
	hi = rc = length-1;
	lo = 0;

	if (hi<0) return 0;




	do {
		rn = (hi+lo)/2;
        OMG_HDCS_read_attitude(file,rn, &attitude);

		foundtime = (double)attitude.timeOffset*
			    (double)att_summary.timeScale*1.0e-6+
			    (double)att_summary.refTime*100.0;

		if (foundtime < wanttime)
			lo = rn+1;
		else if (foundtime > wanttime)
			hi = rn;
		else 
			lo = hi = rn;
	} while (hi > lo);
                     
/*!!!!!! careful have chosen always to get the one after !!!!! */
	if (foundtime < wanttime) rn++;

/* be careful not to let an invalid number escape */
	if (rn<0) rn=0;		/* should never happen */
	if (rn>rc) rn=rc;
	
	*recnum = rn;

	return 1;	
}  
/* -------------------------------------------------------

  ------------------------------------------------------- */
int OMG_HDCS_get_imagedata(FILE *file, int recnum, char *data)
{
  OMG_HDCS_summary_header  summary;
  OMG_HDCS_profile_header  profile;
  int offset;
  int j;

	OMG_HDCS_read_summary(file, &summary);


        if(recnum >= summary.numProfiles) {
          return(0);
        }
	offset = OMG_HDCS_offset(summary.toolType, recnum+1);
        fseek(file, offset-tool_defs[summary.toolType][IMAGE_LENGTH],0);
        fread(data,tool_defs[summary.toolType][IMAGE_LENGTH], 1,file);

           return(1);
}

/* -------------------------------------------------------

  ------------------------------------------------------- */
int OMG_HDCS_put_imagedata(FILE *file, int recnum, char *data)
{
  OMG_HDCS_summary_header  summary;
  OMG_HDCS_profile_header  profile;
  int offset;
  int j;

	OMG_HDCS_read_summary(file, &summary);


        if(recnum < 0 ) {
          return(0);
        }
        if( recnum >= summary.numProfiles) {

/*	printf(" %d appending - ", summary.numProfiles); */
        summary.numProfiles = recnum+1;
        fseek(file,0,0);
        fwrite(&summary,sizeof(summary),1,file);
        }

	offset = OMG_HDCS_offset(summary.toolType, recnum+1);
        fseek(file, offset-tool_defs[summary.toolType][IMAGE_LENGTH],0);
        fread(data,tool_defs[summary.toolType][IMAGE_LENGTH], 1,file);

           return(1);
}





/* -------------------------------------------------------

  ------------------------------------------------------- */
int OMG_HDCS_put_raw_beams(FILE *file, int recnum, int no_beams,
        OMG_HDCS_beam *beams)
{
  char data[OMG_HDCS_BEAM_SIZE];
  OMG_HDCS_summary_header  summary;
  OMG_HDCS_packed_beam            pbeam;
  OMG_HDCS_packed_beam_v3            pbeam_v3;
  int offset, byte_off;
  int j;
  int written;

	OMG_HDCS_read_summary(file, &summary);


        if(recnum < 0 ) {
          no_beams = -1;
          return(0);
        }
        if( recnum >= summary.numProfiles) {
/*
	printf(" %d appending - ", summary.numProfiles);  
*/
        summary.numProfiles = recnum+1;
        fseek(file,0,0);
	OMG_HDCS_dump_summary(file, &summary);
        }

	if(summary.fileVersion == 1) {
	offset = OMG_HDCS_offset(summary.toolType, recnum);
        fseek(file, offset, 0);
	fseek(file,tool_defs[summary.toolType][PROFILE_LENGTH],1);

            for(j=0;j<no_beams;j++) {
		byte_off=0;
		put_swapped_int(&data[byte_off], &((beams+j)->status)); byte_off+=4;
		put_swapped_int(&data[byte_off], &((beams+j)->observedDepth)); byte_off+=4;
		put_swapped_int(&data[byte_off], &((beams+j)->acrossTrack)); byte_off+=4;
		put_swapped_int(&data[byte_off], &((beams+j)->alongTrack)); byte_off+=4;
		put_swapped_int(&data[byte_off], &((beams+j)->latOffset)); byte_off+=4;
		put_swapped_int(&data[byte_off], &((beams+j)->longOffset)); byte_off+=4;
		put_swapped_int(&data[byte_off], &((beams+j)->processedDepth)); byte_off+=4;
		put_swapped_int(&data[byte_off], &((beams+j)->timeOffset)); byte_off+=4;
		put_swapped_int(&data[byte_off], &((beams+j)->depthAccuracy)); byte_off+=4;

		memcpy(&data[byte_off], &((beams+j)->reflectivity),1); byte_off+=1;
		memcpy(&data[byte_off], &((beams+j)->Q_factor),1); byte_off+=1;
		memcpy(&data[byte_off], &((beams+j)->beam_no),1); byte_off+=1;
		memcpy(&data[byte_off], &((beams+j)->freq),1); byte_off+=1;
		memcpy(&data[byte_off], &((beams+j)->calibratedBackscatter),1); byte_off+=1;
		memcpy(&data[byte_off], &((beams+j)->mindB),1); byte_off+=1;
		memcpy(&data[byte_off], &((beams+j)->maxdB),1); byte_off+=1;
		memcpy(&data[byte_off], &((beams+j)->pseudoAngleIndependentBackscatter),1); byte_off+=1;

		put_swapped_int(&data[byte_off], &((beams+j)->range)); byte_off+=4;
		put_swapped_int(&data[byte_off], &((beams+j)->no_samples)); byte_off+=4;
		put_swapped_int(&data[byte_off], &((beams+j)->offset)); byte_off+=4;
		put_swapped_int(&data[byte_off], &((beams+j)->centre_no)); byte_off+=4;

		memcpy(&data[byte_off], &((beams+j)->sample_unit),1); byte_off+=1;
		memcpy(&data[byte_off], &((beams+j)->sample_interval),1); byte_off+=1;
		memcpy(&data[byte_off], &((beams+j)->dummy[0]),1); byte_off+=1;
		memcpy(&data[byte_off], &((beams+j)->dummy[1]),1); byte_off+=1;

		written =
              fwrite(data,tool_defs[summary.toolType][BEAM_LENGTH], 1,file);
            }
	} else if(summary.fileVersion == 2) {
	offset = OMG_HDCS_packed_offset(summary.toolType, recnum);
        fseek(file, offset, 0);
	fseek(file,packed_tool_defs[summary.toolType][PROFILE_LENGTH],1);

            for(j=0;j<no_beams;j++) {
	    packdown_beam(*(beams+j), &pbeam);

		byte_off=0;
		put_swapped_short(&data[byte_off], &pbeam.observedDepth); byte_off+=2;
		put_swapped_short(&data[byte_off], &pbeam.acrossTrack); byte_off+=2;
		memcpy(&data[byte_off], &pbeam.status,1); byte_off+=1;
		memcpy(&data[byte_off], &pbeam.reflectivity,1); byte_off+=1;
		memcpy(&data[byte_off], &pbeam.calibratedBackscatter,1); byte_off+=1;
		memcpy(&data[byte_off], &pbeam.pseudoAngleIndependentBackscatter,1); byte_off+=1;

		put_swapped_short(&data[byte_off], &pbeam.alongTrack); byte_off+=2;
		put_swapped_short(&data[byte_off], &pbeam.range); byte_off+=2;
		put_swapped_int(&data[byte_off], &pbeam.offset); byte_off+=4;
		put_swapped_short(&data[byte_off], &pbeam.no_samples); byte_off+=2;
		put_swapped_short(&data[byte_off], &pbeam.centre_no); byte_off+=2;



		written =
              fwrite(data,
		packed_tool_defs[summary.toolType][BEAM_LENGTH], 1,file);
	    }

        } else if(summary.fileVersion == 3) {
        offset = OMG_HDCS_packed_offset_v3(summary.toolType, recnum);
        fseek(file, offset, 0);
        fseek(file,packed_tool_defs_v3[summary.toolType][PROFILE_LENGTH],1);
	
/*
	printf(" packing down %d\n", no_beams);
*/
            for(j=0;j<no_beams;j++) {
            packdown_beam_v3(*(beams+j), &pbeam_v3);
/*
	printf("%d %d- ", j, (beams+j)->observedDepth);
	printf(" %d- ",  pbeam_v3.observedDepth);
*/
                byte_off=0;
                put_swapped_short(&data[byte_off], &pbeam_v3.observedDepth); byte_off+=2;
                put_swapped_short(&data[byte_off], &pbeam_v3.acrossTrack); byte_off+=2;
                memcpy(&data[byte_off], &pbeam_v3.status,1); byte_off+=1;
                memcpy(&data[byte_off], &pbeam_v3.reflectivity,1); byte_off+=1;
                memcpy(&data[byte_off], &pbeam_v3.calibratedBackscatter,1); byte_off+=1;
                memcpy(&data[byte_off], &pbeam_v3.pseudoAngleIndependentBackscatter,1); byte_off+=1;

                put_swapped_short(&data[byte_off], &pbeam_v3.alongTrack); byte_off+=2;
                put_swapped_short(&data[byte_off], &pbeam_v3.range); byte_off+=2;
                put_swapped_int(&data[byte_off], &pbeam_v3.offset); byte_off+=4;
                put_swapped_short(&data[byte_off], &pbeam_v3.no_samples); byte_off+=2;
                put_swapped_short(&data[byte_off], &pbeam_v3.centre_no); byte_off+=2;



                put_swapped_short(&data[byte_off], &pbeam_v3.beam_depress_angle); byte_off+=2;
                put_swapped_short(&data[byte_off], &pbeam_v3.beam_heading_angle); byte_off+=2;
                memcpy(&data[byte_off], &pbeam_v3.samp_win_length,1); byte_off+=1;
                memcpy(&data[byte_off], &pbeam_v3.scaling_factor,1); byte_off+=1;
                memcpy(&data[byte_off], &pbeam_v3.Q_factor,1); byte_off+=1;
                memcpy(&data[byte_off], &pbeam_v3.v3_spare[0],1); byte_off+=1;







/*
		printf(" writing %d beam from %d\n", j, ftell(file));
*/
		written =
              fwrite(data,
                packed_tool_defs_v3[summary.toolType][BEAM_LENGTH], 1,file);






            }
	}
           return(written);
}



/* -------------------------------------------------------

  ------------------------------------------------------- */
int OMG_HDCS_get_raw_beams(FILE *file, int recnum, int *no_beams, 
	OMG_HDCS_beam *beams)
{
  char data[OMG_HDCS_BEAM_SIZE];
  OMG_HDCS_summary_header  summary;
  OMG_HDCS_profile_header  profile;
  OMG_HDCS_packed_profile  packed_profile;
  OMG_HDCS_packed_beam            pbeam;
  OMG_HDCS_packed_beam_v3            pbeam_v3;
  int offset, byte_off;
  int j;
  int calc_beam_pos =1;

	/* bit of a hack, but for speed, if the file is actually
		a packed version (without beam lat/lons), you can
		choose not to calcuate them in the unpacking process
		if you arent actually going to need them */

	if((signed int)(*no_beams) == -999) calc_beam_pos=0;

	OMG_HDCS_read_summary(file, &summary);


        if(recnum >= summary.numProfiles) {
          *no_beams = -1;
          return(0);
        }


	/* !!!!!!!!!!!!!!!!!!!!! */
	/* NOTE this implicitly aligns the pointer at the start of the beams !!!! */

        OMG_HDCS_read_profile(file,recnum, &profile);
        *no_beams = profile.numDepths;



	if(summary.fileVersion == 1) {
          for(j=0;j<*no_beams;j++)  {
              fread(data,tool_defs[summary.toolType][BEAM_LENGTH], 1,file);

		byte_off=0;
		get_swapped_int(&data[byte_off], &((beams+j)->status)); byte_off+=4;
		get_swapped_int(&data[byte_off], &((beams+j)->observedDepth)); byte_off+=4;
		get_swapped_int(&data[byte_off], &((beams+j)->acrossTrack)); byte_off+=4;
		get_swapped_int(&data[byte_off], &((beams+j)->alongTrack)); byte_off+=4;
		get_swapped_int(&data[byte_off], &((beams+j)->latOffset)); byte_off+=4;
		get_swapped_int(&data[byte_off], &((beams+j)->longOffset)); byte_off+=4;
		get_swapped_int(&data[byte_off], &((beams+j)->processedDepth)); byte_off+=4;
		get_swapped_int(&data[byte_off], &((beams+j)->timeOffset)); byte_off+=4;
		get_swapped_int(&data[byte_off], &((beams+j)->depthAccuracy)); byte_off+=4;

		memcpy(&((beams+j)->reflectivity), &data[byte_off],1); byte_off+=1;
		memcpy(&((beams+j)->Q_factor), &data[byte_off],1); byte_off+=1;
		memcpy(&((beams+j)->beam_no), &data[byte_off],1); byte_off+=1;
		memcpy(&((beams+j)->freq), &data[byte_off],1); byte_off+=1;
		memcpy(&((beams+j)->calibratedBackscatter), &data[byte_off],1); byte_off+=1;
		memcpy(&((beams+j)->mindB), &data[byte_off],1); byte_off+=1;
		memcpy(&((beams+j)->maxdB), &data[byte_off],1); byte_off+=1;
		memcpy(&((beams+j)->pseudoAngleIndependentBackscatter), &data[byte_off],1); byte_off+=1;

		get_swapped_int(&data[byte_off], &((beams+j)->range)); byte_off+=4;
		get_swapped_int(&data[byte_off], &((beams+j)->no_samples)); byte_off+=4;
		get_swapped_int(&data[byte_off], &((beams+j)->offset)); byte_off+=4;
		get_swapped_int(&data[byte_off], &((beams+j)->centre_no)); byte_off+=4;

		memcpy(&((beams+j)->sample_unit), &data[byte_off],1); byte_off+=1;
		memcpy(&((beams+j)->sample_interval), &data[byte_off],1); byte_off+=1;
		memcpy(&((beams+j)->dummy[0]), &data[byte_off],1); byte_off+=1;
		memcpy(&((beams+j)->dummy[1]), &data[byte_off],1); byte_off+=1;
	  }
	} else if(summary.fileVersion == 2) {
/*
		if(calc_beam_pos) unpack_profile(packed_profile, &profile);
*/
            for(j=0;j<*no_beams;j++) {
              fread(data,
		packed_tool_defs[summary.toolType][BEAM_LENGTH], 1,file);
		byte_off=0;
		get_swapped_short(&data[byte_off], &pbeam.observedDepth); byte_off+=2;
		get_swapped_short(&data[byte_off], &pbeam.acrossTrack); byte_off+=2;
		memcpy(&pbeam.status, &data[byte_off],1); byte_off+=1;
		memcpy(&pbeam.reflectivity, &data[byte_off],1); byte_off+=1;
		memcpy(&pbeam.calibratedBackscatter, &data[byte_off],1); byte_off+=1;
		memcpy(&pbeam.pseudoAngleIndependentBackscatter, &data[byte_off],1); byte_off+=1;

		get_swapped_short(&data[byte_off], &pbeam.alongTrack); byte_off+=2;
		get_swapped_short(&data[byte_off], &pbeam.range); byte_off+=2;
		get_swapped_int(&data[byte_off], &pbeam.offset); byte_off+=4;
		get_swapped_short(&data[byte_off], &pbeam.no_samples); byte_off+=2;
		get_swapped_short(&data[byte_off], &pbeam.centre_no); byte_off+=2;

		unpack_beam(pbeam, beams+j); 
		if(calc_beam_pos) 
		add_beam_location(beams+j, &profile, &summary);
	    }


        } else if(summary.fileVersion == 3) {
/*
                if(calc_beam_pos) unpack_profile(packed_profile, &profile);
*/
            for(j=0;j<*no_beams;j++) {
/*
		printf(" reading %d beam from %d\n", j, ftell(file));
*/
              fread(data,
                packed_tool_defs_v3[summary.toolType][BEAM_LENGTH], 1,file);
                byte_off=0;
                get_swapped_short(&data[byte_off], &pbeam_v3.observedDepth); byte_off+=2;
                get_swapped_short(&data[byte_off], &pbeam_v3.acrossTrack); byte_off+=2;
                memcpy(&pbeam_v3.status, &data[byte_off],1); byte_off+=1;
                memcpy(&pbeam_v3.reflectivity, &data[byte_off],1); byte_off+=1;
                memcpy(&pbeam_v3.calibratedBackscatter, &data[byte_off],1); byte_off+=1;
                memcpy(&pbeam_v3.pseudoAngleIndependentBackscatter, &data[byte_off],1); byte_off+=1;

                get_swapped_short(&data[byte_off], &pbeam_v3.alongTrack); byte_off+=2;
                get_swapped_short(&data[byte_off], &pbeam_v3.range); byte_off+=2;
                get_swapped_int(&data[byte_off], &pbeam_v3.offset); byte_off+=4;
                get_swapped_short(&data[byte_off], &pbeam_v3.no_samples); byte_off+=2;
                get_swapped_short(&data[byte_off], &pbeam_v3.centre_no); byte_off+=2;

                get_swapped_short(&data[byte_off], &pbeam_v3.beam_depress_angle); byte_off+=2;
                get_swapped_short(&data[byte_off], &pbeam_v3.beam_heading_angle); byte_off+=2;
                memcpy(&pbeam_v3.samp_win_length, &data[byte_off],1); byte_off+=1;
                memcpy(&pbeam_v3.scaling_factor, &data[byte_off],1); byte_off+=1;
                memcpy(&pbeam_v3.Q_factor, &data[byte_off],1); byte_off+=1;
                memcpy(&pbeam_v3.v3_spare[0], &data[byte_off],1); byte_off+=1;

                unpack_beam_v3(pbeam_v3, beams+j);
/*
	printf("%d %d- ", j, (beams+j)->observedDepth);
	printf(" %d (%d)\n ",  pbeam_v3.observedDepth, pbeam_v3.scaling_factor);
*/
                if(calc_beam_pos)
                add_beam_location(beams+j, &profile, &summary);



	    }






	}
           return(1);

}


/* asks for nobeams set of double XYZ triplets
	if global variables:
		tide_corr_flag and
		proc_corr_flag are set, it will
		tide correct and/or use proccessed depth fields.
*/

/* -------------------------------------------------------

  ------------------------------------------------------- */
int OMG_HDCS_get_xyz_beams(FILE *file, int recnum, int *no_beams, double *xyz)
{
  OMG_HDCS_summary_header  summary;
  OMG_HDCS_profile_header  profile;
  OMG_HDCS_beam            beams[1440], *beam;
  int offset;
  int j;
float depth_in, depth_out, across_in, across_out;
ref_coeff use_coeffs;
int coeff_flag=0;
int newloc;

attitude att;
location induced;
float induced_heave;
double ampl_shifter;


	if(*xyz == -999.9) coeff_flag=1;

	OMG_HDCS_read_summary(file, &summary);


	if(abs(recnum) >= summary.numProfiles) {
	  *no_beams = -1;
          return(0);
	}

        if(coeff_flag)  {
                get_coeffs(recnum, &use_coeffs);
                set_up_ref_coefficients(&use_coeffs);
        }


	if(OMG_HDCS_read_profile(file, abs(recnum),&profile) != 1) return(0);

	if(profile.status !=0) {
	  *no_beams = -1;
          return(2);
	}

	if(use_lever_arm_flag) {
                att.roll =  (float)profile.vesselRoll *1.0e-7*180.0/M_PI;
                att.pitch =  (float)profile.vesselPitch *1.0e-7*180.0/M_PI;
		att.yaw=0;
		DCM_rotate(lever_arm, att, &induced);
		induced_heave = induced.z - lever_arm.z;
/*
		printf("roll %f pitch %f induced heave %f\n",
		att.roll, att.pitch, induced_heave);
*/
	}
          
	*no_beams = profile.numDepths;

	if(OMG_HDCS_get_raw_beams(file, abs(recnum), no_beams, beams) != 1) 
			return(0);

	if(coeff_flag) {
            for(j=0;j<*no_beams;j++) { 
		beam = &beams[j];
	      if(beam->observedDepth && beam->acrossTrack && (beam->status == 0 || useallZ)) {
                depth_in = -(float)beam->observedDepth/1000.0;
                across_in = (float)beam->acrossTrack/1000.0;
                get_refracted_location(depth_in, across_in,
                                &depth_out, &across_out);
                beam->observedDepth = -(int)(depth_out*1000.0);
                beam->acrossTrack  = (int)(across_out*1000.0);
                add_beam_location(beam, &profile, &summary);

	      }
	    }
	}

            for(j=0;j<*no_beams;j++) { 
		beam = &beams[j];
	      if(beam->observedDepth > 0000.0 && 
		 beam->observedDepth < 10000000.0 
		&& 
		(beam->status == 0 || useallZ) ) {
	locate_beam(beam, &profile, &summary, xyz+1+j*4, xyz+0+j*4);

		if(use_tide_corr_flag) {
		  if(use_proc_corr_flag) {
	            *(xyz+2+j*4) = 
			-(double)(beam->processedDepth-profile.tide)
			*(double)summary.positionScale/1000.;
		  } else {
	            *(xyz+2+j*4) = 
			-(double)(beam->observedDepth-profile.tide)
			*(double)summary.positionScale/1000.;
		  }
		} else {
		  if(use_proc_corr_flag) {
	            *(xyz+2+j*4) = 
			-(double)beam->processedDepth*
			(double)summary.positionScale/1000.;
		  } else {
	            *(xyz+2+j*4) = 
			-(double)beam->observedDepth*
			(double)summary.positionScale/1000.;
		  }
		}

/* new fourth field which is the approximate observed slant range */
		*(xyz+3+j*4) =
		 	sqrt(
		pow(
		(double)beam->observedDepth*
                        (double)summary.positionScale/1000.0 
		,2.0)
			+
		pow(
		(double)beam->acrossTrack*
                        (double)summary.positionScale/1000.0 
		,2.0)
		);

/*
		printf("%d %d %d %d %fJJJJJJ\n", 
		j, beam->observedDepth, beam->acrossTrack, 
		summary.positionScale, *(xyz+3+j*4) );
*/
/* WILD HACK TO MINIMISE NADIR OFFSET */
/*
		printf(" %d %d ++ %d -- %d %d\n",
		summary.toolType, use_ampl_shft_flag, 
			j, (unsigned char)beam->Q_factor,beam->Q_factor);
*/
/*    REMOVED COS DOSEN"T WORK RIGHT IN DEEPER WATER 
	if(summary.toolType == EM1000 && 
		use_ampl_shft_flag && (unsigned char)beam->Q_factor < 127)
		*(xyz+2+j*4) -= 0.15;
*/

/* ------------------------------------------ */
/*
      ADDED THIS TO TAKE CARE OF OUTER BEAM PULLDOWN.  AFTER TALKING WITH
      ANDRE GODIN, WHO WAS CONVERSING WITH SIMRAD, IT WAS FINALLY (ADMITTED ?)
      THAT THE CALIBRATION CONSTANTS ON THE U24 CHIP FOR THE NEW TRANSDUCER
      WERE INCORRECT, CAUSING A SHIFT DOWN IN THE OUTER BEAMS.  I LOOKED AT
      MANY PROFILES AND FOUND THAT BEAMS 1-6 AND 52-60 REQUIRED A SHIFT
      (DEPTH DEPENDANT) THAT WAS CONSTANT FOR ALL THE BEAMS.  THE DEPTH
      VALUES RAMPED UP FROM BEAMS 7-10 AND 51-48 IN A LINEAR FASHION, SO
      I WEIGHTED THE AMPLITUDE SHIFTER ACCORDING TO BEAM POSITION.  THIS
      HELPS A GREAT DEAL, AND JOHN, IF YOU THOUGHT YOURS WAS A WILD HACK,

!!!!  LOOK AT THIS !!!!!   HATS OFF TO SIMRAD....

      Bill Danforth  12/11/96

*/
        if(summary.toolType == EM1000 &&
                use_ampl_shft_flag && (j < 11 || j > 47))
                {
                if(j < 11)
                    {
                    if(j == 10)
                         ampl_shifter = .0105 * .15;
                    else if(j == 9)
                         ampl_shifter = .0105 * .3;
                    else if(j == 8)
                         ampl_shifter = .0105 * .5;
                    else if(j == 7)
                         ampl_shifter = .0105 * .7;
                    else if(j == 6)
                         ampl_shifter = .0105 * .85;
                    else
                         ampl_shifter = .0105;
                    }
                else
                    {

                    if(j == 48)
                         ampl_shifter = .0105 * .15;
                    else if(j == 49)
                         ampl_shifter = .0105 * .3;
                    else if(j == 50)
                         ampl_shifter = .0105 * .5;
                    else if(j == 51)
                         ampl_shifter = .0105 * .7;
                    else if(j == 52)
                         ampl_shifter = .0105 * .85;
                    else
                         ampl_shifter = .0105;
                    }

                *(xyz+2+j*4) -= *(xyz+2+j*4) * ampl_shifter;
                }

/*
        if(summary.toolType == EM1000 &&
                use_ampl_shft_flag && (j < 10 || j > 48))
                {
                if(j == 9 || j == 49)
                     ampl_shifter = .0125 * .2;
                else if(j == 8 || j == 50)
                     ampl_shifter = .0125 * .4;
                else if(j == 7 || j == 51)
                     ampl_shifter = .0125 * .6;
                else if(j == 6 || j == 52)
                     ampl_shifter = .0125 * .8;
                else
                     ampl_shifter = .0125;

                *(xyz+2+j*4) -= *(xyz+2+j*4) * ampl_shifter;
                }
*/


/* ------------------------------------------ */






	if(use_lever_arm_flag)  *(xyz+2+j*4) -= induced_heave;
	
	      } else {
	      *(xyz+1+j*4) = 0.0;
	      *(xyz+2+j*4) = 0.0;
	      *(xyz+0+j*4) = 0.0;
	      *(xyz+3+j*4) = 0.0;
	      }

            }

	   return(1);
}


/* EOF */


/* -------------------------------------------------------

upgrade to all more beam attributes to be passed on to weigh_grid

  ------------------------------------------------------- */
int OMG_HDCS_get_xyz2_beams(FILE *file, int recnum, int *no_beams, OMG_attrib_beam *abeam)
{
  OMG_HDCS_summary_header  summary;
  OMG_HDCS_profile_header  profile;
  OMG_HDCS_beam            beams[1440], *beam;
  int offset;
  int j;
float depth_in, depth_out, across_in, across_out;
ref_coeff use_coeffs;
int coeff_flag=0;
int newloc;
float altitude, radial_dist;
double ship_rel_azi;
attitude att;
location induced;
float induced_heave;


	if(*no_beams == -999) coeff_flag=1;

	OMG_HDCS_read_summary(file, &summary);


	if(abs(recnum) >= summary.numProfiles) {
	  *no_beams = -1;
          return(0);
	}

        if(coeff_flag)  {
                get_coeffs(recnum, &use_coeffs);
                set_up_ref_coefficients(&use_coeffs);
        }


	if(OMG_HDCS_read_profile(file, abs(recnum),&profile) != 1) return(0);

	if(profile.status !=0) {
	  *no_beams = -1;
          return(2);
	}

	if(use_lever_arm_flag) {
                att.roll =  (float)profile.vesselRoll *1.0e-7*180.0/M_PI;
                att.pitch =  (float)profile.vesselPitch *1.0e-7*180.0/M_PI;
		att.yaw=0;
		DCM_rotate(lever_arm, att, &induced);
		induced_heave = induced.z - lever_arm.z;
	}
          
	*no_beams = profile.numDepths;

	if(OMG_HDCS_get_raw_beams(file, abs(recnum), no_beams, beams) != 1) 
			return(0);

	if(coeff_flag) {
            for(j=0;j<*no_beams;j++) { 
		beam = &beams[j];
	      if(beam->observedDepth && beam->acrossTrack && (beam->status == 0 || useallZ)) {
                depth_in = -(float)beam->observedDepth/1000.0;
                across_in = (float)beam->acrossTrack/1000.0;
                get_refracted_location(depth_in, across_in,
                                &depth_out, &across_out);
                beam->observedDepth = -(int)(depth_out*1000.0);
                beam->acrossTrack  = (int)(across_out*1000.0);
                add_beam_location(beam, &profile, &summary);

	      }
	    }
	}

            for(j=0;j<*no_beams;j++) { 
		beam = &beams[j];
	      if(beam->observedDepth > 0000.0 && 
		 beam->observedDepth < 10000000.0 
		&& 
		(beam->status == 0 || useallZ) ) {
/*
typedef struct {
          double time;
          int status;
          float waterlevel_depth;
          float reduced_depth;
          float draft;
          float tide;
          double lat;
          double lon;
          float TWTT;
          float beamangle;
          float beamazimuth;
        } OMG_full_beam;
*/
	locate_beam(beam, &profile, &summary, &(abeam+j)->lat, &(abeam+j)->lon);

		     (abeam+j)->reduced_depth = 
			-(double)(beam->observedDepth-profile.tide)
			*(double)summary.positionScale/1000.;
		     (abeam+j)->waterlevel_depth = 
			-(double)beam->observedDepth*
			(double)summary.positionScale/1000.;

/* while called at TWTT, as currently implemented, it is the approximate observed slant range */
		altitude = 
                ((double)beam->observedDepth*
                        (double)summary.positionScale/1000.0)
			- supplied_draft;
	        radial_dist =
		(double)beam->acrossTrack*
                        (double)summary.positionScale/1000.0 ;

		(abeam+j)->TWTT = 
		 	sqrt( pow( altitude ,2.0)
			+ pow( radial_dist ,2.0));

		(abeam+j)->status =  beam->status;
		(abeam+j)->tide = (float)profile.tide*(double)summary.positionScale/1000.0;
		(abeam+j)->draft = supplied_draft;

		/* for masking/weighting by angle */
		(abeam+j)->beamangle = (180.0/M_PI) * atan(fabs(radial_dist)/altitude);


		/* in case we want to implement ellipsoidal footprints */
		get360azi((double)beam->acrossTrack,
			  (double)beam->alongTrack, &ship_rel_azi);
		(abeam+j)->beamazimuth = 
			(double)profile.vesselHeading*1.0E-07+ ship_rel_azi;
		(abeam+j)->beamazimuth *= 180.0/M_PI;
		if((abeam+j)->beamazimuth > 360.0) (abeam+j)->beamazimuth -=360.0;
		if((abeam+j)->beamazimuth <   0.0) (abeam+j)->beamazimuth +=360.0;

		/* 1970 time to be stored for CHS */
		(abeam+j)->time = (double)profile.timeOffset*
                            (double)summary.timeScale*1.0e-6+
                            (double)summary.refTime*100.0;


	if(use_lever_arm_flag)  {
		     (abeam+j)->reduced_depth -= induced_heave;
		     (abeam+j)->waterlevel_depth -= induced_heave;
	}
	
	      } else {
		(abeam+j)->reduced_depth = 
		(abeam+j)->waterlevel_depth = 
		(abeam+j)->lat = 
		(abeam+j)->lon = 
		(abeam+j)->TWTT = 
		(abeam+j)->status =  
		(abeam+j)->tide = 
		(abeam+j)->draft = 
		(abeam+j)->beamangle = 
		(abeam+j)->beamazimuth = 
		(abeam+j)->time =  0.0;
	      }

            }

	if(summary.toolType == EM3000D) {
/* for beams  0 25 and 230 to 255 we are going to duplicate the soundings */
/*
            for(j= (*no_beams)-1;j>-1;j--) { 
		if(j < 25) newloc = j*2;
		else if(j < 230)  newloc = 24*2 +(j-24)*1;
		else  newloc = 24*2 +205*1 + (j-230)*2;
		printf(" moving beam %d to %d\n", j, newloc);

		if(j < 230) {
		} else if(j < 25) {
		} else {
	      *(xyz+0+j*4) = lon;
	      *(xyz+1+j*4) = lat;
	      *(xyz+2+j*4) = depth;
	      *(xyz+3+j*4) = slant range;
		}

	   } 
	printf("\n\n\n");
*/
	} /* onyl of a dual EM3K */
 
	   return(1);
}


/* EOF */


/* -------------------------------------------------------

  ------------------------------------------------------- */

void locate_transducer(OMG_HDCS_profile_header *profile,
                        OMG_HDCS_summary_header *summary,
                double *lat, double *lon)
{
	if(!profile->status) {
	      *lat = (180.0/M_PI) * ( (double)summary->refLat*1.0E-7 +
			  (double)profile->vesselLatOffset*1.0E-9*
			  (double)summary->positionScale );

	      *lon = (180.0/M_PI) * ( (double)summary->refLong*1.0E-7 +
			  (double)profile->vesselLongOffset*1.0E-9*
			  (double)summary->positionScale );
	} else {
		*lat = *lon = 999.0;
	}
}

/* -------------------------------------------------------

  ------------------------------------------------------- */

void locate_beam(OMG_HDCS_beam *beam, OMG_HDCS_profile_header *profile,
                        OMG_HDCS_summary_header *summary,
                double *lat, double *lon)
{
	if(!profile->status) {
	      *lat = (180.0/M_PI) * ( (double)summary->refLat*1.0E-7 +
			  (double)profile->vesselLatOffset*1.0E-9*
			  (double)summary->positionScale+ 
			  (double)beam->latOffset*1.0E-9*
			  (double)summary->positionScale );

	      *lon = (180.0/M_PI) * ( (double)summary->refLong*1.0E-7 +
			  (double)profile->vesselLongOffset*1.0E-9*
			  (double)summary->positionScale+ 
			  (double)beam->longOffset*1.0E-9*
			  (double)summary->positionScale );
	} else {
		*lat = *lon = 999.0;
	}
}

/* -------------------------------------------------------

  ------------------------------------------------------- */
int OMG_HDCS_dump_att_summary(FILE *file, OMG_HDCS_summary_attitude *att_summary)
{

char data[OMG_HDCS_SUMMARY_ATTITUDE_SIZE];
int offset=0;


        memcpy(&data[offset], att_summary->fileID,4); offset +=4;
        put_swapped_int(&data[offset],&(att_summary->sensorNumber)); offset +=4;
        put_swapped_int(&data[offset],&(att_summary->subFileID)); offset +=4;
        put_swapped_int(&data[offset],&(att_summary->fileVersion)); offset +=4;
        put_swapped_int(&data[offset],&(att_summary->toolType)); offset +=4;
        put_swapped_int(&data[offset],&(att_summary->numRecords)); offset +=4;
        put_swapped_int(&data[offset],&(att_summary->timeScale)); offset +=4;
        put_swapped_int(&data[offset],&(att_summary->refTime)); offset +=4;
        put_swapped_int(&data[offset],&(att_summary->minTime)); offset +=4;
        put_swapped_int(&data[offset],&(att_summary->maxTime)); offset +=4;
        put_swapped_int(&data[offset],&(att_summary->status));


        fseek(file,0,0);
        fwrite(data,OMG_HDCS_SUMMARY_ATTITUDE_SIZE,1,file);
	return(0);
}
/* ------------------------------------------------------------- */
int OMG_HDCS_read_att_summary(FILE *file, OMG_HDCS_summary_attitude *att_summary)
{

char data[OMG_HDCS_SUMMARY_ATTITUDE_SIZE];
int offset=0;

        fseek(file,0,0);
        fread(data,OMG_HDCS_SUMMARY_ATTITUDE_SIZE,1,file);

        memcpy(att_summary->fileID,&data[0],4); offset +=4;
        get_swapped_int(&data[offset],&(att_summary->sensorNumber)); offset +=4;
        get_swapped_int(&data[offset],&(att_summary->subFileID)); offset +=4;
        get_swapped_int(&data[offset],&(att_summary->fileVersion)); offset +=4;
        get_swapped_int(&data[offset],&(att_summary->toolType)); offset +=4;
        get_swapped_int(&data[offset],&(att_summary->numRecords)); offset +=4;
        get_swapped_int(&data[offset],&(att_summary->timeScale)); offset +=4;
        get_swapped_int(&data[offset],&(att_summary->refTime)); offset +=4;
        get_swapped_int(&data[offset],&(att_summary->minTime)); offset +=4;
        get_swapped_int(&data[offset],&(att_summary->maxTime)); offset +=4;
        get_swapped_int(&data[offset],&(att_summary->status));
	return(0);
}
/* ------------------------------------------------------------- */

int OMG_HDCS_write_attitude(FILE *file, int profile_no,
                        OMG_HDCS_attitude *attitude)
{
  char data[OMG_HDCS_ATTITUDE_SIZE];
  OMG_HDCS_summary_attitude att_summary;
  int offset, byte_off, recsize;
  signed short sigsh;

        OMG_HDCS_read_att_summary(file, &att_summary);

        if(profile_no < 0 ) {
          return(0);
        }
        if( profile_no >= att_summary.numRecords) {
/*
         printf(" %d appending - ", att_summary.numRecords);
*/
        att_summary.numRecords = profile_no+1;
        OMG_HDCS_dump_att_summary(file, &att_summary);
        }

	recsize = 4; /* time */
	if(att_summary.status & 1) recsize +=2; /* roll */
	if(att_summary.status & 2) recsize +=2; /* pitch */
	if(att_summary.status & 4) recsize +=2; /* heading */
	if(att_summary.status & 8) recsize +=2; /* heave */
	if(att_summary.status & 16) recsize +=2; /* mechanical pitch */
	if(att_summary.status & 32) recsize +=2; /* surface sound speed */


	offset = OMG_HDCS_SUMMARY_ATTITUDE_SIZE + profile_no*recsize;
/*
	printf("record %d recsize  %d offset %d\n", 
		profile_no, recsize, offset);
*/

	
        byte_off=0;
        put_swapped_int(&data[byte_off],&(attitude->timeOffset));
                        byte_off +=4;

/* only writing out orientation component if say so in status header */
	if(att_summary.status & 1) {
        put_swapped_short(&data[byte_off],&(attitude->roll));
                        byte_off +=2;
	}
	if(att_summary.status & 2) {
        put_swapped_short(&data[byte_off],&(attitude->pitch));
                        byte_off +=2;
	}
	if(att_summary.status & 4) {
	sigsh = attitude->heading;
        put_swapped_short(&data[byte_off],&sigsh);
                        byte_off +=2;
	}
	if(att_summary.status & 8) {
        put_swapped_short(&data[byte_off],&(attitude->heave));
                        byte_off +=2;
	}
	if(att_summary.status & 16) {
        put_swapped_short(&data[byte_off],&(attitude->mech_pitch));
                        byte_off +=2;
	}
	if(att_summary.status & 32) {
        put_swapped_short(&data[byte_off],&(attitude->surface_ss));
                        byte_off +=2;
	}

	fseek(file, offset,0);
	return(fwrite(data,recsize,1,file));

}
/* ------------------------------------------------------------- */
int OMG_HDCS_read_attitude(FILE *file, int profile_no,
                        OMG_HDCS_attitude *attitude)
{
  char data[OMG_HDCS_ATTITUDE_SIZE];
  OMG_HDCS_summary_attitude att_summary;
  int offset, byte_off, recsize;
  int read=0;
  signed short sigsh;



        memset(data,0,OMG_HDCS_ATTITUDE_SIZE);

        OMG_HDCS_read_att_summary(file, &att_summary);


        if(profile_no < 0 || profile_no >= att_summary.numRecords) {
          return(0);
        }

        recsize = 4; /* time */
        if(att_summary.status & 1) recsize +=2;
        if(att_summary.status & 2) recsize +=2;
        if(att_summary.status & 4) recsize +=2;
        if(att_summary.status & 8) recsize +=2;
        if(att_summary.status & 16) recsize +=2; /* mech pitch */
        if(att_summary.status & 32) recsize +=2; /* surface ss */


        offset = OMG_HDCS_SUMMARY_ATTITUDE_SIZE + profile_no*recsize;
/*
        printf("record %d recsize  %d offset %d\n",
                profile_no, recsize, offset);
*/


        fseek(file, offset, 0);
        read= fread(data, recsize, 1,file);

        byte_off=0;
        get_swapped_int(&data[byte_off],&(attitude->timeOffset));
                        byte_off +=4;
	
	if(att_summary.status & 1) {
        get_swapped_short(&data[byte_off],&(attitude->roll));
                        byte_off +=2;
	}
	if(att_summary.status & 2) {
        get_swapped_short(&data[byte_off],&(attitude->pitch));
                        byte_off +=2;
	}
	if(att_summary.status & 4) {
        get_swapped_short(&data[byte_off],&sigsh);
	attitude->heading = sigsh;
                        byte_off +=2;
	}
	if(att_summary.status & 8) {
        get_swapped_short(&data[byte_off],&(attitude->heave));
                        byte_off +=2;
	}
	if(att_summary.status & 16) {
        get_swapped_short(&data[byte_off],&(attitude->mech_pitch));
                        byte_off +=2;
	}
	if(att_summary.status & 32) {
        get_swapped_short(&data[byte_off],&(attitude->surface_ss));
                        byte_off +=2;
	}
	return(read);
}
/* ------------------------------------------------------------- */

void OMG_get_rational_summary(FILE *file, OMG_rational_summary *ratsum)
{
OMG_HDCS_summary_header  summary;



	 OMG_HDCS_read_summary(file, &summary);

	ratsum->noProfiles = summary.numProfiles;
	ratsum->maxLat 
               = (180.0/M_PI) * ( (double)summary.refLat*1.0E-7 +
                 (double)summary.maxLat*1.0E-9*(double)summary.positionScale);
	ratsum->maxLon 
               = (180.0/M_PI) * ( (double)summary.refLong*1.0E-7 +
                 (double)summary.maxLong*1.0E-9*(double)summary.positionScale);
	ratsum->minLat 
               = (180.0/M_PI) * ( (double)summary.refLat*1.0E-7 +
                 (double)summary.minLat*1.0E-9*(double)summary.positionScale);
	ratsum->minLon 
               = (180.0/M_PI) * ( (double)summary.refLong*1.0E-7 +
                 (double)summary.minLong*1.0E-9*(double)summary.positionScale);


	ratsum->toolType = summary.toolType;

}
/* ------------------------------------------------------------- */

void OMG_get_rational_profile(FILE *file, int profnum, OMG_rational_profile *ratprof)
{
OMG_HDCS_summary_header  summary;
OMG_HDCS_profile_header  profile;

        OMG_HDCS_read_summary(file, &summary);
	if(profnum < 0) error("asking for negative profile number");
	if(profnum > summary.numProfiles-1) error("asking for too large profile number");

	OMG_HDCS_read_profile(file, profnum, &profile);


	ratprof->time  
            	      = ((double)profile.timeOffset)
                        * ((double)summary.timeScale) *1.0e-6
                        + ((double)summary.refTime)*100.0;

	ratprof->noDepths =  profile.numDepths;
	locate_transducer(&profile, &summary, &ratprof->pingLat, &ratprof->pingLon); 

                ratprof->roll =  (float)profile.vesselRoll *1.0e-7*180.0/M_PI;
                ratprof->pitch =  (float)profile.vesselPitch *1.0e-7*180.0/M_PI;
                ratprof->heave =  (float)profile.vesselHeave /1000.0;
                ratprof->heading =  (float)profile.vesselHeading *1.0e-7*180.0/M_PI;
               ratprof->tide =  (float)profile.tide *0.001;
               ratprof->surfaceSoundSpeed =  (float)profile.soundVelocity *0.1;

		if(summary.fileVersion >=3) 
               ratprof->surfaceSoundSpeed =  (float)profile.td_sound_speed *0.1;

               ratprof->samp_rate =  (float)profile.samp_rate;

}
/* ------------------------------------------------------------- */

void OMG_put_rational_beams(FILE *file, int profnum, int no_beams, OMG_rational_beam *ratbeams)
{
  OMG_HDCS_summary_header  summary;
  OMG_HDCS_profile_header  profile;
  OMG_HDCS_beam            beams[1440];
  int i;
  float beamangle, beamazimuth;

        OMG_HDCS_read_summary(file, &summary);
        if(profnum < 0) error("asking for negative profile number");
        if(profnum > summary.numProfiles-1) error("asking for too large profile number");

        OMG_HDCS_read_profile(file, profnum, &profile);


	/* note ignores the no_beams you give it... */
	OMG_HDCS_get_raw_beams(file, profnum, &no_beams, beams);

	for(i=0;i< no_beams;i++) {

	  beams[i].observedDepth =
		(int)(
	  (ratbeams+i)->depth/ ((float)summary.positionScale/1000.0)
			);

	  beams[i].acrossTrack =
		(int)(
	  (ratbeams+i)->across/ ((float)summary.positionScale/1000.0)
			);

	  beams[i].alongTrack =
		(int)(
	  (ratbeams+i)->along/ ((float)summary.positionScale/1000.0)
			);



	}

	OMG_HDCS_put_raw_beams(file, profnum, no_beams, beams);
}

/* ------------------------------------------------------------- */

void OMG_get_rational_beams(FILE *file, int profnum, int *no_beams, OMG_rational_beam *ratbeams)
{
  OMG_HDCS_summary_header  summary;
  OMG_HDCS_profile_header  profile;
  OMG_HDCS_beam            beams[1440];
  int i;
  float beamangle, beamazimuth;

        OMG_HDCS_read_summary(file, &summary);
        if(profnum < 0) error("asking for negative profile number");
        if(profnum > summary.numProfiles-1) error("asking for too large profile number");

        OMG_HDCS_read_profile(file, profnum, &profile);


	OMG_HDCS_get_raw_beams(file, profnum, no_beams, beams);

	for(i=0;i< *no_beams;i++) {

	  (ratbeams+i)->status = beams[i].status;
	  (ratbeams+i)->depth = (float)beams[i].observedDepth*
                        (float)summary.positionScale/1000.;	
	  (ratbeams+i)->across =	(float)beams[i].acrossTrack*
                        (float)summary.positionScale/1000.;
	  (ratbeams+i)->along =	(float)beams[i].alongTrack*
                        (float)summary.positionScale/1000.;

	  (ratbeams+i)->TWTT = beams[i].range;

	  if(summary.toolType == EM1002  ||
	     summary.toolType == EM3000 || 
	     summary.toolType == EM3000D || 
	     summary.toolType == EM300 ) {


		 (ratbeams+i)->TWTT  /= (float) profile.samp_rate;
		 (ratbeams+i)->TWTT  /= 4.0;

	  }


	  if(summary.toolType == EM1000 && profile.mode == 3) {

	  if((ratbeams+i)->depth)
	  (ratbeams+i)->beamangle = -75.0 + (2.5/4.0) +(float)i*2.5;

	  } else if (summary.toolType == EM1000) {

		(ratbeams+i)->beamangle =  -99.0;

	  } else 
/*
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
*/
	  if(summary.toolType == EM1002  ||
	     summary.toolType == EM3000 || 
	     summary.toolType == EM3000D || 
	     summary.toolType == EM300 ) {
		(ratbeams+i)->beamangle =  (float)beams[i].beam_depress_angle* 0.01;
		(ratbeams+i)->beamazimuth =  (float)beams[i].beam_heading_angle* 0.01;
		(ratbeams+i)->rc_steer =  (float)beams[i].beam_depress_angle* 0.01;
		(ratbeams+i)->tx_steer =  (float)((signed short)beams[i].beam_heading_angle)* 0.01;
	  } else {
		(ratbeams+i)->beamangle =   0.0;
		(ratbeams+i)->beamazimuth =  0.0;
	  }
		(ratbeams+i)->rc_roll =  0.0;
		(ratbeams+i)->rc_pitch =  0.0;
		(ratbeams+i)->rc_mech_pitch =  0.0;
		(ratbeams+i)->rc_heading =  0.0;
		(ratbeams+i)->rc_heave =  0.0;

	locate_beam(&beams[i], &profile, &summary,
                &(ratbeams+i)->lat, 
                &(ratbeams+i)->lon);

	}

	/* here is the test to see which HOP the EM1000 is in */
	if(summary.toolType == EM1000 && profile.mode == 3) {
	if( (ratbeams+30)->depth) {

		beamangle = atan((ratbeams+30)->across/(ratbeams+30)->depth)*180.0/M_PI;
		if(beamangle > 1.2) {
			printf(" offset swath \n");	
		   for(i=0;i< *no_beams;i++) 
	  		if ((ratbeams+i)->depth )
	  		(ratbeams+i)->beamangle +=1.25;
		} else {
			printf(" non offset swath \n");	
		}
	}
	}

}
void OMG_get_rational_beams_plus_trace(FILE *file, int profnum,
        int *no_beams, OMG_rational_beam *ratbeams, FILE *tracefile)
{
  OMG_HDCS_summary_header  summary;
  OMG_HDCS_profile_header  profile;
  OMG_HDCS_beam            beams[1440];
  int i;

        OMG_HDCS_read_summary(file, &summary);
        if(profnum < 0) error("asking for negative profile number");
        if(profnum > summary.numProfiles-1) error("asking for too large profile number");

        OMG_HDCS_read_profile(file, profnum, &profile);


        OMG_HDCS_get_raw_beams(file, profnum, no_beams, beams);

        for(i=0;i< *no_beams;i++) {

          (ratbeams+i)->status = beams[i].status;
          (ratbeams+i)->depth = (float)beams[i].observedDepth*
                        (float)summary.positionScale/1000.;
          (ratbeams+i)->across =        (float)beams[i].acrossTrack*
                        (float)summary.positionScale/1000.;
          (ratbeams+i)->along = (float)beams[i].alongTrack*
                        (float)summary.positionScale/1000.;

        locate_beam(&beams[i], &profile, &summary,
                &(ratbeams+i)->lat,
                &(ratbeams+i)->lon);


	  memset((ratbeams+i)->bsdata, 0, 1024);
	  if(beams[i].offset > 1024) {
		 beams[i].offset = 1024;
		warning("apparently more than 1024 ss trace samples in a beam!");
	  } else if(beams[i].offset < 0) {
		 beams[i].offset = 0;
	  }
	  if(beams[i].offset != 0) {
	  fseek(tracefile, beams[i].offset,0l);
	  fread((ratbeams+i)->bsdata, beams[i].no_samples, 1, tracefile);
	  }
	  (ratbeams+i)->no_samples = beams[i].no_samples;
	  (ratbeams+i)->centre_no = beams[i].centre_no;

        }

	

}

/* ------------------------------------------------------------------
  ---------------------------------------------------------------- */
int OMG_HDCS_dump_bounds(FILE *file, int recno, OMG_HDCS_bounds Bounds)
{

	fseek(file, recno* OMG_HDCS_BOUNDS_SIZE , 0);
	    jswapint_fwrite(&Bounds.startrec,4,1, file);
            jswapint_fwrite(&Bounds.endrec,4,1, file);
            jswapint_fwrite(&Bounds.imintime,4,1, file);
            jswapint_fwrite(&Bounds.imaxtime,4,1, file);
            jswapint_fwrite(&Bounds.iminlat,4,1, file);
            jswapint_fwrite(&Bounds.imaxlat,4,1, file);
            jswapint_fwrite(&Bounds.iminlon,4,1, file);
            jswapint_fwrite(&Bounds.imaxlon,4,1, file);

	return(0);
}	

/* ------------------------------------------------------------------
  ---------------------------------------------------------------- */
int OMG_HDCS_read_bounds(FILE *file, int recno, OMG_HDCS_bounds *Bounds)
{
	fseek(file, recno* OMG_HDCS_BOUNDS_SIZE , 0);
	    jswapint_fread(&Bounds->startrec,4,1, file);
            jswapint_fread(&Bounds->endrec,4,1, file);
            jswapint_fread(&Bounds->imintime,4,1, file);
            jswapint_fread(&Bounds->imaxtime,4,1, file);
            jswapint_fread(&Bounds->iminlat,4,1, file);
            jswapint_fread(&Bounds->imaxlat,4,1, file);
            jswapint_fread(&Bounds->iminlon,4,1, file);
            jswapint_fread(&Bounds->imaxlon,4,1, file);

	  Bounds->mintime =
	  Bounds->maxtime =
	  Bounds->minlat =
	  Bounds->maxlat =
	  Bounds->minlon =
	  Bounds->maxlon = 0.0;
	return(0);
}

/* ------------------------------------------------------------------
  ---------------------------------------------------------------- */
int OMG_HDCS_interp_bounds(OMG_HDCS_bounds *Bounds, OMG_HDCS_summary_header summary)
{

        Bounds->mintime = ((double)Bounds->imintime)
                        * ((double)summary.timeScale) *1.0e-6
                        + ((double)summary.refTime)*100.0;
        Bounds->minlat = (180.0/M_PI) * ( (double)summary.refLat*1.0E-7 +
                 (double)Bounds->iminlat*1.0E-9*(double)summary.positionScale);
        Bounds->minlon = (180.0/M_PI) * ( (double)summary.refLong*1.0E-7 +
                 (double)Bounds->iminlon*1.0E-9*(double)summary.positionScale);
        Bounds->maxtime = ((double)Bounds->imaxtime)
                        * ((double)summary.timeScale) *1.0e-6
                        + ((double)summary.refTime)*100.0;
        Bounds->maxlat = (180.0/M_PI) * ( (double)summary.refLat*1.0E-7 +
                 (double)Bounds->imaxlat*1.0E-9*(double)summary.positionScale);
        Bounds->maxlon = (180.0/M_PI) * ( (double)summary.refLong*1.0E-7 +
                 (double)Bounds->imaxlon*1.0E-9*(double)summary.positionScale);

	return(0);
}

int OMG_HDCS_bounds_intersect_area(OMG_HDCS_bounds bounds, JHC_header head)
{
	return (OMG_HDCS_bounds_intersect_subarea(bounds, head,
		(double)0, (double)head.dx,
		(double)0, (double)head.dy) ); 



}


int OMG_HDCS_bounds_intersect_subarea(OMG_HDCS_bounds bounds, JHC_header head,
	double xmin, double xmax, double ymin, double ymax)
{
double x[13], y[13];
double utmn, utme;
int i, inarea;

	/*first see if any bound corner projects in the within the array coordinates of the subarea */

                  Project(bounds.minlon, bounds.minlat,
                                &x[0],&y[0],&head);
                  Project(bounds.minlon, bounds.maxlat,
                                &x[1],&y[1],&head);
                  Project(bounds.maxlon, bounds.maxlat,
                                &x[2],&y[2],&head);
                  Project(bounds.maxlon, bounds.minlat,
                                &x[3],&y[3],&head);

/* and for those near tricky mismatches make fake sub points  */
	/* the middle */
	x[4] = (x[0] +x[1] + x[2] +x[3] )/ 4.0;
	y[4] = (y[0] +y[1] + y[2] +y[3] )/ 4.0;
	/* the centres of the sides */
	x[5] = (x[0] +x[1])/ 2.0;
	y[5] = (y[0] +y[1])/ 2.0;
	x[6] = (x[1] +x[2])/ 2.0;
	y[6] = (y[1] +y[2])/ 2.0;
	x[7] = (x[2] +x[3])/ 2.0;
	y[7] = (y[2] +y[3])/ 2.0;
	x[8] = (x[3] +x[0])/ 2.0;
	y[8] = (y[3] +y[0])/ 2.0;
	/* the centres of the 4 quads */
	x[9] =  (x[4] +x[0])/ 2.0;
	y[9] =  (y[4] +y[0])/ 2.0;
	x[10] = (x[4] +x[1])/ 2.0;
	y[10] = (y[4] +y[1])/ 2.0;
	x[11] = (x[4] +x[2])/ 2.0;
	y[11] = (y[4] +y[2])/ 2.0;
	x[12] = (x[4] +x[3])/ 2.0;
	y[12] = (y[4] +y[3])/ 2.0;


/* XXXXXX IMPERFECT should really to the rigorous - edges interesct test
 but too lazy right now. Could fail for skinny windows */

	inarea=0;

	for(i=0;i<13;i++)
		if(x[i] >= xmin && x[i] < xmax &&
		   y[i] >= ymin && y[i] < ymax) inarea++;

	if(inarea) return(1);




	/* if that fails still a chance if */
	/* or inversely see if any of the area corners are within the geocoords of the bounds */

        inv_proj(xmin, ymin, &x[0], &y[0], &utme, &utmn,  &head);
        inv_proj(xmin, ymax, &x[1], &y[1], &utme, &utmn,  &head);
        inv_proj(xmax, ymin, &x[2], &y[2], &utme, &utmn,  &head);
        inv_proj(xmax, ymax, &x[3], &y[3], &utme, &utmn,  &head);

/* and for those near tricky mismatches make fake sub points  */
	/* the middle */
	x[4] = (x[0] +x[1] + x[2] +x[3] )/ 4.0;
	y[4] = (y[0] +y[1] + y[2] +y[3] )/ 4.0;
	/* the centres of the sides */
	x[5] = (x[0] +x[1])/ 2.0;
	y[5] = (y[0] +y[1])/ 2.0;
	x[6] = (x[1] +x[2])/ 2.0;
	y[6] = (y[1] +y[2])/ 2.0;
	x[7] = (x[2] +x[3])/ 2.0;
	y[7] = (y[2] +y[3])/ 2.0;
	x[8] = (x[3] +x[0])/ 2.0;
	y[8] = (y[3] +y[0])/ 2.0;
	/* the centres of the 4 quads */
	x[9] =  (x[4] +x[0])/ 2.0;
	y[9] =  (y[4] +y[0])/ 2.0;
	x[10] = (x[4] +x[1])/ 2.0;
	y[10] = (y[4] +y[1])/ 2.0;
	x[11] = (x[4] +x[2])/ 2.0;
	y[11] = (y[4] +y[2])/ 2.0;
	x[12] = (x[4] +x[3])/ 2.0;
	y[12] = (y[4] +y[3])/ 2.0;


	for(i=0;i<13;i++)
		if(x[i] >= bounds.minlon && x[i] <= bounds.maxlon &&
		   y[i] >= bounds.minlat && y[i] <= bounds.maxlat) inarea++;

	if(inarea) return(1);


	/* otherwise */
	return(0);


}


int OMG_HDCS_get_interpolated_async
                (FILE *file, double the_time, double max_time_gap, double delay,
                        float *roll, float *pitch, float *heading,
                        float *heave, float *mech_pitch,
                        float *surf_ss, int *beyond_att_flag,
			double *beforetime, double *aftertime, int *closest_recno)
{
int att_rec_no;
OMG_HDCS_attitude att_rec, after_att_rec;
OMG_HDCS_summary_attitude  att_summary;
int gotvalid;
char datestring[80];
double weight1, weight2;
double 	bef_, aft_;

	int i,j, k;
	int debug_flag=0;

  

/* first get the summary to find out what async values you have got in the file */
        OMG_HDCS_read_att_summary(file, &att_summary);


	the_time += delay;
	
	    OMG_HDCS_att_time_search(file, the_time, &att_rec_no);
		*closest_recno = att_rec_no;
		/* **** get the one before */
	    gotvalid = OMG_HDCS_read_attitude(file,att_rec_no-1,&att_rec);

	if(!gotvalid) {
		*roll =
		*pitch =
		*heading =
		*heave =
		*mech_pitch =
		*surf_ss = 0.0;
		*beyond_att_flag =1;
		return(-1);
	}

            *beforetime = ((double)att_rec.timeOffset)
            		* ((double)att_summary.timeScale) *1.0e-6
	    		+ ((double)att_summary.refTime)*100.0;

	    gotvalid = OMG_HDCS_read_attitude(file,att_rec_no,&after_att_rec);

	if(!gotvalid) {
		*roll =
		*pitch =
		*heading =
		*heave =
		*mech_pitch =
		*surf_ss = 0.0;
		*beyond_att_flag =1;
		return(-1);
	}

            *aftertime = ((double)after_att_rec.timeOffset)
            		* ((double)att_summary.timeScale) *1.0e-6
	    		+ ((double)att_summary.refTime)*100.0;


/*
	    *beforetime -= delay;
	    *aftertime -= delay;
*/

	/* checking that the attitudes are time sequential */
	if(*aftertime <= *beforetime ) {
		printf(" ASYNC no time step between attitude samples - %f %f\n" ,
		*aftertime, *beforetime);
		*roll =
		*pitch =
		*heading =
		*heave =
		*mech_pitch =
		*surf_ss = 0.0;
		*beyond_att_flag =1;
		return(-1);
	}

	/* checking there isnt too big a time jump */
	if(fabs(aftertime-beforetime) > max_time_gap) {
		printf(" ASYNC record not bound by fixes within time limit (%f %f)\n",
			 max_time_gap, *aftertime- *beforetime);
		*roll =
		*pitch =
		*heading =
		*heave =
		*mech_pitch =
		*surf_ss = 0.0;
		*beyond_att_flag =1;
		return(-1);
	}

	weight1 = (the_time -*beforetime)/(*aftertime- *beforetime);
	weight2 = (*aftertime -the_time)/(*aftertime- *beforetime);


	*beyond_att_flag =0;

	if(weight1 < 0.0 || weight1 > 1.0 || 
			weight2 > 1.0 || weight2 < 0.0 ) {
		printf(" ASYNC interp. -- something wrong with the weighting .... \n");
		*roll =
		*pitch =
		*heading =
		*heave =
		*mech_pitch =
		*surf_ss = 0.0;
		*beyond_att_flag =1;
		return(-1);

	} else {

/* note, for simplicity we are assuming roll and pitch are not wrapping 
	around 180 degrees! */

        if(att_summary.status & 1) {            /* roll */
	 bef_ = (float)att_rec.roll/100.0;
	 aft_ = (float)after_att_rec.roll/100.0;
	 *roll = bef_*weight2 + aft_*weight1;
	} else {
	 *roll =0.0;
	}

        if(att_summary.status & 2) {            /* pitch */
	 bef_ = (float)att_rec.pitch/100.0;
	 aft_ = (float)after_att_rec.pitch/100.0;
	 *pitch = bef_*weight2 + aft_*weight1;
	} else {
	 *pitch =0.0;
	}

        if(att_summary.status & 4) {            /* heading */
		/* !!!!!!!!!!! Not Proper cos. heading wraps through 360!!
		  fortunately we normally don't ask for other heading */
	 bef_ = (float)att_rec.heading/100.0;
	 aft_ = (float)after_att_rec.heading/100.0;
	 *heading = bef_*weight2 + aft_*weight1;
	} else {
	 *heading =0.0;
	}

        if(att_summary.status & 8) {            /* heave */
	 bef_ = (float)att_rec.heave/100.0;
	 aft_ = (float)after_att_rec.heave/100.0;
	 *heave = bef_*weight2 + aft_*weight1;
	} else {
	 *heave =0.0;
	}

        if(att_summary.status & 16) {            /* mechanical pitch */
	 bef_ = (float)att_rec.mech_pitch/100.0;
	 aft_ = (float)after_att_rec.mech_pitch/100.0;
	 *mech_pitch = bef_*weight2 + aft_*weight1;
	} else {
	 *mech_pitch =0.0;
	}

        if(att_summary.status & 32) {            /* surface sound speed */
	 bef_ = (float)att_rec.surface_ss/10.0;
	 aft_ = (float)after_att_rec.surface_ss/10.0;
	 *surf_ss = bef_*weight2 + aft_*weight1;
	} else {
	 *surf_ss =0.0;
	}

	}


	
	    	if(debug_flag) {

 stdtime_nicetime(datestring,"%W %M %D %Y %T", the_time);
  printf(" rec %d  image %s %0.5f\n", k, datestring, 
		the_time - (double)((int)the_time));

 stdtime_nicetime(datestring,"%W %M %D %Y %T", *beforetime);
  printf("  rec %d before %s  %f \n", att_rec_no-1, datestring,
		*beforetime - (double)((int)*beforetime));
	  printf(" %f %f\n", (float)att_rec.roll, (float)att_rec.pitch); 

 stdtime_nicetime(datestring,"%W %M %D %Y %T", *aftertime);
  printf("  rec %d after %s %f\n", att_rec_no, datestring, 
		*aftertime - (double)((int)*aftertime));
	  printf(" %f %f\n", (float)after_att_rec.roll, (float)after_att_rec.pitch); 

	  printf(" %f %f\n", *roll, *pitch); 
	  printf(" %f %f\n", weight1, weight2); 
 		}


	return(0);
}
