#ifndef __MAPSHEET_PRIVATE_PARSER_H__
#define __MAPSHEET_PRIVATE_PARSER_H__

#include "mapsheet.h"
#include "projection.h"

typedef struct _sheet_list {
	MapSheet			sheet;
	struct _sheet_list	*next;
} SheetList;

typedef enum {
	Sheet_Meters,
	Sheet_Degrees
} SheetUnits;

typedef struct {
	SheetUnits	units;
	f64			x, y;
} SheetVec;

typedef struct {
	SheetVec	spacing;
	SheetVec	sizes;
	SheetVec	center;
	char		*backing_store;
} SheetDef;

typedef struct {
	SheetUnits	units;
	f64			value;
} Number;

typedef struct {
	Number		vec[4];
} NumVec;

#endif
