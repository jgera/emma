/* ----------------------------- MNI Header -----------------------------------
@NAME       : micreateimage.c
@INPUT      : Name of new MINC file, and a ton of options to control
              the dimensions and variables that are created in it. 
	      (See args.c for details on the argument parsing.)
@OUTPUT     : A brand new MINC file, which may inherit various
              parameters from an optional parent file.
@RETURNS    : 
@DESCRIPTION: Create a new MINC file, complete with image dimensions,
              dimension and dimension-width variables, image max/min
	      variables, and (of course) the image variable.  If the 
	      new file is based on a "parent" MINC file, any other 
	      variables and global attributes will be copied from 
	      the parent to the new file.  As much information as CAN
	      be copied about the image and image dimensions will also
	      be copied, but the fact that the new file may or may
	      not have the same orientation, dimensions, and dimension
	      lengths as its parent complicates matters greatly.
	      See various functions in dimensions.c for details.
@METHOD     : 
@GLOBALS    : ErrMsg (string set by functions and displayed by main())
              a bunch of globals required for ParseArgv
@CALLS      : MINC, NetCDF libraries
              various functions in args.c and dimensions.c
@CREATED    : September - November 1993, Greg Ward.
@MODIFIED   : 
---------------------------------------------------------------------------- */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <netcdf.h>
#include "ParseArgv.h"
#include "minc.h"
#include "mincutil.h"           /* for NCErrMsg () */
#include "time_stamp.h"
#include "micreateimage.h"
#include "args.h"
#include "dimensions.h"
#define PROGNAME      "micreateimage"

#define ERROR_CHECK(success) { if (!(success)) { ErrAbort (ErrMsg, TRUE, 1); }}



/* Global variables */

char    *ErrMsg;

/* 
 * Various options from the command line.  These must be global for
 * ParseArgv to work correctly.  They are interpreted by the functions
 * in args.c, and thence passed to the various functions (in dimensions.c
 * and this file) that need them.  Note that gChildFile and gParentFile
 * are the filenames; gChildFile doesn't really need to be global
 * (since it's not parsed out by ParseArgv -- it's just left over in
 * argv[]), but I've made it global for consistency.  (Also, this way
 * functions that set ErrMsg and need to know the name of the child
 * file can do so without yet another argument being added to that 
 * function.  Also, the "g" in all these just stands for "global".
 */

int     gSizes [MAX_IMAGE_DIM] = {-1,-1,-1,-1};
char   *gTypeStr = "byte";
double  gValidRange [NUM_VALID];
char   *gOrientation = "transverse";
char   *gChildFile;
char   *gParentFile;

/* Type strings (borrowed from Peter Neelin's mincinfo.c) */

char *type_names[] = 
   { NULL, "byte", "char", "short", "long", "float", "double" };


/* Function prototypes */

void usage (void);
void ErrAbort (char *msg, Boolean PrintUsage, int ExitCode);

Boolean OpenFiles (char parent_file[], char child_file[],
                   int *parent_CDF,    int *child_CDF);
void FinishExclusionLists (int ParentCDF,
			   int NumChildDims, char *ChildDimNames[],
			   int *NumExclude, int Exclude[]);
Boolean CreateImageVars (int CDF, int NumDim, int DimIDs[], 
			 nc_type NCType, Boolean Signed, double ValidRange[]);



/* ----------------------------- MNI Header -----------------------------------
@NAME       : usage
@INPUT      : void
@OUTPUT     : void
@RETURNS    : void
@DESCRIPTION: Prints usage information for micreateimage
@METHOD     : none
@GLOBALS    : none
@CALLS      : none
@CREATED    : June 2, 1993 by MW
@MODIFIED   :
---------------------------------------------------------------------------- */
void usage (void)
{
   fprintf (stderr, "\nUsage:\n");
   fprintf (stderr, "%s <MINC file> [option] [option] ...\n\n", PROGNAME);
   fprintf (stderr, "options may come in any order; %s -help for descriptions\n\n", PROGNAME);
}


/* ----------------------------- MNI Header -----------------------------------
@NAME       : ErrAbort
@INPUT      : msg - a nice, descriptive error message to print before bombing
              PrintUsage - flag whether to print a syntax summary
              ExitCode - integer to return to caller [via exit()]
@OUTPUT     : (N/A)  [does NOT return!]
@RETURNS    : (void) [does NOT return!]
@DESCRIPTION: Print out a usage summary, error message, and die.
@METHOD     : 
@GLOBALS    : 
@CALLS      : 
@CREATED    : 
@MODIFIED   : 
---------------------------------------------------------------------------- */
void ErrAbort (char *msg, Boolean PrintUsage, int ExitCode)
{
   if (PrintUsage) usage ();
   fprintf (stderr, "%s\n\n", msg);
   exit (ExitCode);
}




#ifdef DEBUG
/* ----------------------------- MNI Header -----------------------------------
@NAME       : DumpInfo
@INPUT      : CDF - a NetCDF file handle
@OUTPUT     : Info to stdout.
@RETURNS    : (void)
@DESCRIPTION: Prints to stdout a bunch of handy information about a NetCDF 
              file.  Includes number of dimensions, variables, and global
	      attributes; dimension lengths; variable types and dimensions.
@METHOD     : 
@GLOBALS    : 
@CALLS      : NetCDF library
@CREATED    : November 1993, Greg Ward.
@MODIFIED   : 
---------------------------------------------------------------------------- */
void DumpInfo (int CDF)
{
   int     NumDims;
   int     NumVars;
   int     NumAtts;
   int     i, j;
   char    Name [MAX_NC_NAME];
   long    Len;
   nc_type Type;
   int	   DimList [MAX_NC_DIMS];

   if (CDF < 0) return;

   ncinquire (CDF, &NumDims, &NumVars, &NumAtts, NULL);
   printf ("%d dimensions, %d variables, %d global attributes\n",
           NumDims, NumVars, NumAtts);

   for (i = 0; i < NumDims; i++)
   {
      ncdiminq (CDF, i, Name, &Len);
      printf ("Dim %d: %s (length %ld)\n", i, Name, Len);
   }

   for (i = 0; i < NumVars; i++)
   {
      ncvarinq (CDF, i, Name, &Type, &NumDims, DimList, &NumAtts);
      printf ("Var %d: %s (%s) (%d dimensions:", 
	      i, Name, type_names[Type], NumDims);
      for (j = 0; j < NumDims; j++)
      {
	 ncdiminq (CDF, DimList[j], Name, NULL);
	 printf (" %s", Name);
      }
      printf (")\n");
   }	
}
#endif





/* ----------------------------- MNI Header -----------------------------------
@NAME       : OpenFiles
@INPUT      : parent_file -> The name of the minc file to create the child
                             from, or NULL if there is no parent file.
              child_file  -> The name of the child file to be created.
              tm_stamp    -> A string to be prepended to the history attribute.
@OUTPUT     : parent_CDF  -> The cdfid of the opened parent file, or -1 if
                             no parent file was given.
              child_CDF   -> The cdfid of the created child file.
@RETURNS    : TRUE if all went well
              FALSE if error opening parent file (but only if one was supplied)
              FALSE if error creating child file
@DESCRIPTION: Opens the (optional) parent MINC file, and creates the (required)
              new MINC file.  Also creates the root variable (using
              micreate_std_variable) in the child file.
@METHOD     :
@GLOBALS    : none
@CALLS      : NetCDF routines
              MINC routines
@CREATED    : May 31, 1993 by MW
@MODIFIED   : Aug 11, 1993, GPW - added provisions for no parent file.
              Oct 27, 1993, GPW - moved from micreate.c to micreateimage.c;
              removed copying of attributes and history update; renamed
              from CreateChild to OpenFiles.
---------------------------------------------------------------------------- */
Boolean OpenFiles (char parent_file[], char child_file[],
                   int *parent_CDF,    int *child_CDF)
{
   /*
    * If a filename for the parent MINC file was supplied, open the file;
    * else return -1 for *parent_CDF.
    */
   
   if (parent_file != NULL)
   {
      *parent_CDF = ncopen (parent_file, NC_NOWRITE);
      if (*parent_CDF == MI_ERROR)
      {
         sprintf (ErrMsg, "Error opening input file %s: %s\n", 
                  parent_file, NCErrMsg (ncerr));
         return (FALSE);
      }
   }
   else
   {
      *parent_CDF = -1;
   }

   /* Create the child file, bomb if any error */
   
   *child_CDF = nccreate (child_file, NC_CLOBBER);
   if (*child_CDF == MI_ERROR) 
   {
      sprintf (ErrMsg, "Error creating child file %s: %s\n", 
               child_file, NCErrMsg (ncerr));
      ncclose (*parent_CDF);
      return (FALSE);
   }
   
#ifdef DEBUG
   printf ("OpenFiles:\n");
   printf (" Parent file %s, CDF %d\n", parent_file, *parent_CDF);
   printf (" Child file  %s, CDF %d\n\n", child_file, *child_CDF);
#endif

   /* The parent file is now open for reading, and the child file is */
   /* created and opened for definition.                             */
   
   /* Create the root variable */
/*   
   child_root = micreate_group_variable (*child_CDF, MIrootvariable);
   if (child_root == MI_ERROR)
   {
      sprintf (ErrMsg
*/
   return (TRUE);
}      /* OpenFiles () */




void FinishExclusionLists (int ParentCDF,
			   int NumChildDims, char *ChildDimNames[],
			   int *NumExclude, int Exclude[])
{
   int	NumParentDims;
   int	CurParentDim;
   char ParentDimName [MAX_NC_NAME];
   char ParentVarName [MAX_NC_NAME];
   int	ParentVar;
   int	WidthVar;
   int	CurChildDim;
   int	DimMatch;

#ifdef DEBUG
   printf ("FinishExclusionLists\n");
   printf (" Initial list of variables to exclude from copying:\n");
   for (i = 0; i < *NumExclude; i++)
   {
      ParentVar = Exclude [i];
      ncvarinq (ParentCDF, ParentVar, ParentVarName, NULL, NULL, NULL, NULL);
      printf ("  %s (ID %d)\n", ParentVarName, ParentVar);
   }
   putchar ('\n');
#endif

   /* 
    * Find all dimensions in the parent file, and for any that do not
    * have a corresponding dimension in the child file (using 
    * ChildDimNames[] to match), add that parent dimension to both
    * exclusion lists.
    */

#ifdef DEBUG
   printf (" Looking for unmatched parent dimensions...\n");
#endif

   ncinquire (ParentCDF, &NumParentDims, NULL, NULL, NULL);
   for (CurParentDim = 0; CurParentDim < NumParentDims; CurParentDim++)
   {
      ncdiminq (ParentCDF, CurParentDim, ParentDimName, NULL);

#ifdef DEBUG
      printf ("  Checking parent dimension %d (%s)\n", 
	      CurParentDim, ParentDimName);
#endif

      /* 
       * Get the ID's of the variables with the same name as this
       * dimension, and with the dimension name + "-width" -- these
       * will be needed if we are to add anything to the exclusion lists
       */

      strcpy (ParentVarName, ParentDimName);
      ParentVar = ncvarid (ParentCDF, ParentVarName);

      strcat (ParentVarName, "-width");
      WidthVar = ncvarid (ParentCDF, ParentVarName);

      /* Skip to next parent dimension if NEITHER one was found */

      if ((ParentVar == -1) && (WidthVar == -1))
      {
	 continue;
      }

#ifdef DEBUG
      printf ("  Dimension variable ID: %d; dimension-width variable ID: %d\n",
	      ParentVar, WidthVar);
#endif

      /* 
       * Loop through the names of the dimensions in the child file,
       * stopping only when we find one that matches ParentDimName
       * (or have gone through all the child's dimensions)
       */

      CurChildDim = 0;
      do
      {
#ifdef DEBUG
	 printf ("   Comparing with child dimension %d (%s)\n",
		 CurChildDim, ChildDimNames[CurChildDim]);
#endif
	 DimMatch = strcmp (ParentDimName, ChildDimNames[CurChildDim]);
	 CurChildDim++;
      } while ((DimMatch != 0) && (CurChildDim < NumChildDims));

      /*
       * If we got here without finding a dimension in the child file  
       * with the same name is the current dimension in the parent file,
       * then add this dimension's dimension and dimension-width variables
       * to both exclusion lists (but only if these variables actually
       * exist!)
       */

      if (DimMatch != 0)
      {
	 if (ParentVar != -1)
	 {
	    Exclude [(*NumExclude)++] = ParentVar;
	 }

	 if (WidthVar != -1)
	 {
	    Exclude [(*NumExclude)++] = WidthVar;
	 }
      }
   }     /* for CurParentDim */

   /* Now add all the obvious ones: MIrootvariable, MIimage, MIimagemax, MIimagemin */

   ParentVar = ncvarid (ParentCDF, MIrootvariable);
   if (ParentVar != -1)
   {
      Exclude [(*NumExclude)++] = ParentVar;
   }
   
   ParentVar = ncvarid (ParentCDF, MIimage);
   if (ParentVar != -1)
   {
      Exclude [(*NumExclude)++] = ParentVar;
   }
   
   ParentVar = ncvarid (ParentCDF, MIimagemax);
   if (ParentVar != -1)
   {
      Exclude [(*NumExclude)++] = ParentVar;
   }
   
   ParentVar = ncvarid (ParentCDF, MIimagemin);
   if (ParentVar != -1)
   {
      Exclude [(*NumExclude)++] = ParentVar;
   }
   

#ifdef DEBUG
   printf (" Final list of variables to exclude from copying:\n");
   for (i = 0; i < *NumExclude; i++)
   {
      ParentVar = Exclude [i];
      ncvarinq (ParentCDF, ParentVar, ParentVarName, NULL, NULL, NULL, NULL);
      printf ("  %s (ID %d)\n", ParentVarName, ParentVar);
   }
   putchar ('\n');
#endif

}     /* FinishExclusionLists () */



/* ----------------------------- MNI Header -----------------------------------@NAME       : CreateImageVars
@INPUT      : CDF - ID of the MINC file in which to create MIimagemax
                    and MIimagemin variables
              NumDim - *total* number of image dimensions (2,3, or 4)
              DimIDs - ID's of the NumDim image dimensions
	      NCType - type of the image variable
	      Signed - TRUE or FALSE, for the image variable
	      ValidRange - for the image variable
@OUTPUT     : 
@RETURNS    : TRUE on success
              FALSE if any error creating either variable
              (sets ErrMsg on error)
@DESCRIPTION: Create the MIimagemax and MIimagemin variables in a newly
              created MINC file (must be in definition mode!).  The 
              variables will depend on the two lowest (slowest-varying) 
              image dimensions, ie. frames and slices in the full 4-D
              case.  If the file has no frames or no slices (or both),
              that will be handled properly.
@METHOD     : 
@GLOBALS    : ErrMsg
@CALLS      : MINC library
@CREATED    : 93-10-28, Greg Ward: code moved from main()
@MODIFIED   : 93-11-10, GPW: renamed and modified from CreateMinMax
---------------------------------------------------------------------------- */
Boolean CreateImageVars (int CDF, int NumDim, int DimIDs[], 
			 nc_type NCType, Boolean Signed, double ValidRange[])
{
   int  image_id;
   int  max_id, min_id;         /* ID's of the newly-created variables */

#ifdef DEBUG
   printf ("CreateImageVars:\n");
   printf (" Creating MIimage variable with %d dimensions\n", NumDim);
#endif

   image_id = micreate_std_variable (CDF, MIimage, NCType,
                                     NumDim, DimIDs);
   (void) miattputstr (CDF, image_id, MIsigntype, MI_SIGN_STR(Signed));
   (void) miattputstr (CDF, image_id, MIcomplete, MI_FALSE);
   
   (void) ncattput (CDF, image_id, MIvalid_range, NC_DOUBLE, 2, ValidRange);

   /*
    * Create the image-max and image-min variables.  They should be
    * dependent on the "non-image" dimensions (ie. time and slices,
    * if they exist), so pass NumDim-2 as the number of
    * dimensions, and DimIDs as the list of dimension ID's -- 
    * micreate_std_variable should then only look at the first one
    * or two dimension IDs in the list.
    */

#ifdef DEBUG
   printf (" creating MIimagemin and MIimagemax with %d dimensions\n",
           NumDim-2);
#endif
   
   max_id = micreate_std_variable (CDF, MIimagemax, NC_DOUBLE,
                                   NumDim-2, DimIDs);
   min_id = micreate_std_variable (CDF, MIimagemin, NC_DOUBLE,
                                   NumDim-2, DimIDs);
   
   if ((max_id == MI_ERROR) || (min_id == MI_ERROR))
   {  
      sprintf (ErrMsg, "Error creating image max/min variables: %s\n",
               NCErrMsg (ncerr));
      return (FALSE);
   }

   return (TRUE);

}     /* CreateImageVars () */




/* ----------------------------- MNI Header -----------------------------------
@NAME       : UpdateHistory
@INPUT      : ChildCDF - the MINC file which will have TimeStamp prepended
                         to its history attribute
              TimeStamp - string to be added to history attribute in ChildCDF
@OUTPUT     : (none)
@RETURNS    : (void)
@DESCRIPTION: Update the history of a MINC file by prepending a string
              to it.  The history attribute will be created if it does
              not exist in the file specified by CDF; otherwise, its
              current value will be read in, the string TimeStamp will
              be prepended to it, and it will be re-written.
@METHOD     : 
@GLOBALS    : 
@CALLS      : NetCDF, MINC libraries
@CREATED    : 93-10-27, Greg Ward (from MW's code formerly in micreate)
@MODIFIED   : 93-11-16, Greg Ward: removed references to parent file; the
              attribute should now be copied from the parent file before
	      UpdateHistory is ever called.
---------------------------------------------------------------------------- */
void UpdateHistory (int ChildCDF, char *TimeStamp)
{
   nc_type  HistType;
   int      HistLen;

#ifdef DEBUG
   printf ("UpdateHistory:\n");
#endif


   /* Update the history of the child file */
   
   if (ncattinq (ChildCDF,NC_GLOBAL,MIhistory,&HistType,&HistLen) == MI_ERROR)
   {
#ifdef DEBUG
      printf (" creating history attribute\n");
#endif
      ncattput (ChildCDF, NC_GLOBAL, MIhistory, NC_CHAR, 
                strlen(TimeStamp), TimeStamp);
   }
   else
   {
      char    *OldHist;
      char    *NewHist;

#ifdef DEBUG
      printf (" adding to history attribute\n");
#endif
      OldHist = (char *) malloc ((size_t) (HistLen*sizeof(char)));
      ncattget (ChildCDF, NC_GLOBAL, MIhistory, (char *)OldHist);
      NewHist = (char *) malloc 
         ((size_t) (HistLen*sizeof(char) + strlen(TimeStamp) + 1));
      strcpy (NewHist, TimeStamp);
      strcat (NewHist, OldHist);
      ncattput (ChildCDF, NC_GLOBAL, MIhistory, NC_CHAR, 
                strlen(NewHist), NewHist);
      free (NewHist);
      free (OldHist);
   }

}     /* UpdateHistory () */




Boolean CopyOthers (int ParentCDF, int ChildCDF, 
		    int NumExclude, int Exclude[],
		    char *TimeStamp)
{
#ifdef DEBUG
   printf ("CopyOthers:\n");
   printf (" copying variable definitions...\n");
#endif

   if (micopy_all_var_defs(ParentCDF, ChildCDF, NumExclude, Exclude) == MI_ERROR)
   {
      sprintf (ErrMsg, "Error %d copying variable definitions: %s", 
	       ncerr, NCErrMsg (ncerr));
      ncclose (ChildCDF);
      return (FALSE);
   }

#ifdef DEBUG
   printf (" updating history...\n");
#endif 

   UpdateHistory (ChildCDF, TimeStamp);

#ifdef DEBUG
   printf (" copying variable values...\n");
#endif
   ncendef (ChildCDF);

   if (micopy_all_var_values(ParentCDF, ChildCDF, NumExclude, Exclude) == MI_ERROR)
   {
      sprintf (ErrMsg, "Error %d copying variable values: %s", 
	       ncerr, NCErrMsg (ncerr));
      ncclose (ChildCDF);
      return (FALSE);
   }

   return (TRUE);

}     /* CopyOthers () */



/* ----------------------------- MNI Header -----------------------------------
@NAME       : main
@INPUT      : 
@OUTPUT     : none
@RETURNS    : none
@DESCRIPTION: Sets up a new MINC file so that it can contain image data.
              Creates the dimensions, and the image, time, time-width,
              image-max, and image-min variables.
@METHOD     : none
@GLOBALS    : ncopts
@CALLS      : GetArgs
              CreateDims
              MINC library
              NetCDF library
@CREATED    : June 3, 1993 by MW
@MODIFIED   : 
---------------------------------------------------------------------------- */
int main (int argc, char *argv[])
{
   char   *TimeStamp;           /* to be put in the history attribute */
   nc_type NCType;
   Boolean Signed;

   long    NumFrames;           /* lengths of the various image dimensions */
   long    NumSlices;
   long    Height;
   long    Width;

   int     ChildCDF;
   int     ParentCDF;

   /* NumDim will be the number of image dimensions actually created in
    * the MINC file; DimIDs and DimNames will hold the ID's and names
    * of these dimensions.  There will be 2 dimensions if both NumFrames
    * and NumSlices are zero; 3 dimensions if either one but not both is
    * zero; and 4 dimensions if neither are zero.  (Height and Width must
    * always be non-zero.)
    */

   int     NumDim;       
   int     DimIDs [MAX_IMAGE_DIM];
   char   *DimNames [MAX_IMAGE_DIM];

   int	   NumExclude;
   int	   Exclude[MAX_NC_DIMS];


   ErrMsg = (char *) calloc (256, sizeof (char));
   TimeStamp = time_stamp (argc, argv);
   GetArgs (&argc, argv,
            &NumFrames, &NumSlices, &Height, &Width, 
            &NCType, &Signed);

#ifdef DEBUG
   printf ("main: Parent file: %s; new file: %s\n\n", gParentFile, gChildFile);
#endif

   ncopts = 0;

   ERROR_CHECK 
      (OpenFiles (gParentFile, gChildFile, &ParentCDF, &ChildCDF));

   ERROR_CHECK 
      (CreateDims (ChildCDF, NumFrames, NumSlices, Height, Width, 
		   gOrientation, &NumDim, DimIDs, DimNames));
   ERROR_CHECK
      (CreateDimVars (ParentCDF, ChildCDF, NumDim, DimIDs, DimNames, 
		      &NumExclude, Exclude));

   ERROR_CHECK 
      (CreateImageVars (ChildCDF, NumDim, DimIDs, NCType, Signed, gValidRange));

#ifdef DEBUG
   printf ("--------------------------------------------------------------\n");
   printf ("State of %s immediately before entering CopyOthers:\n",gParentFile);
   DumpInfo (ParentCDF);

   printf ("--------------------------------------------------------------\n");
   printf ("State of %s immediately before entering CopyOthers:\n", gChildFile);
   DumpInfo (ChildCDF);
#endif


   /*
    * Now, copy everything else of possible interest from the parent file
    * (but only if it exists!) to the child file.
    */

   if (ParentCDF != -1)
   {

      FinishExclusionLists (ParentCDF, NumDim, DimNames, &NumExclude, Exclude);

      ERROR_CHECK
	 (CopyOthers (ParentCDF, ChildCDF, NumExclude, Exclude, TimeStamp));
   }

   ncclose (ChildCDF);
   if (ParentCDF != -1)
   {
      ncclose (ParentCDF);
   }
   return (0);

}

