add_library(mapsheet
  mapsheet_cube.c
  #mapsheet_lex.c
  mapsheet_mean.c
  mapsheet_median.c
  mapsheet_nodal.c
  #mapsheet_par.c
  mapsheet.c
)

# To not have to rely on havinb bison/flex installed, 
# the following commands were used to generate the
# sources files to include with the repo
#
# bison -y -o mapsheet_par.c -d  mapsheet_par.y
#
# flex -X -o mapsheet_lex.c --header-file=mapsheet_lex.h mapsheet_lex.l
#
