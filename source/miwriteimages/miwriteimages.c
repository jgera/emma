#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <float.h>
#include "gpw.h"
#include "minc.h"
#include "mincutil.h"
#include "mierrors.h"

#define NUM_ARGS       4           /* STRICT requirement!! */

#define MINC_FILE      argv[1]
#define SLICE_VECTOR   argv[2]
#define FRAME_VECTOR   argv[3]
#define TEMP_FILE      argv[4]
#define PROGNAME       "miwriteimages"


/*
 * Global variables (with apologies)
 */

Boolean    debug;
char       ErrMsg [256];         /* set as close to the occurence of the
                                    error as possible; displayed by whatever
                                    code exits */


/* ----------------------------- MNI Header -----------------------------------
@NAME       : ErrAbort
@INPUT      : msg - string to print before printing usage and exiting
              PrintUsage - whether or not to print a brief syntax summary
              ExitCode - code to return to caller via exit()
@OUTPUT     : (DOES NOT RETURN)
@RETURNS    : (DOES NOT RETURN)
@DESCRIPTION: Prints diagnostic and usage information for current
              program and ABORTS!!!
@METHOD     : none
@GLOBALS    : none
@CALLS      : none
@CREATED    : 93-6-4, Greg Ward
@MODIFIED   :
---------------------------------------------------------------------------- */
void ErrAbort (char msg[], Boolean PrintUsage, int ExitCode) 
{
   (void) fprintf (stderr, "Error: %s\n\n", msg);

   if (PrintUsage)
   {
      (void) fprintf (stderr, "Usage: \n");
      (void) fprintf (stderr, "%s <file name> ",PROGNAME);
      (void) fprintf (stderr, "<slices> <frames> <temp file name>\n\n");
   }
   exit (ExitCode);
}



/* ----------------------------- MNI Header -----------------------------------
@NAME       : GetVector
@INPUT      : vector_string -> A character string containing vector elements
                               separated by commas (eg. 1,2,3)
                               *or* an empty string *or* a string starting
                               with a - (both of which => empty vector)
              max_elements  -> The maximum number of vector elements.
@OUTPUT     : vector        -> An array of longs containing the vector
                               elements
@RETURNS    : number of vector members found (<= max_elements), or
              a negative number on failure
@DESCRIPTION: Takes a string containing a vector, and produces a one
              dimensional array of longs containing the vector elements.
@METHOD     : none
@GLOBALS    : none
@CALLS      : none
@CREATED    : June 1, 1993 by MW
@MODIFIED   : June 3, 1993 by GPW -- changed return value to an int
              indicating how many vector members were found, and added
              provision for empty vectors 
---------------------------------------------------------------------------- */
int GetVector (char vector_string[], long vector[], int max_elements)
{
   int member;
   char *token;

   /*
    * vector is empty (0 members) if string is empty OR starts with a -
    */

   if ((strlen (vector_string) == 0) || vector_string[0] == '-')
   {
      return (0);
   }
   
   member = 0;
   
   token = strtok (vector_string, ",");
   if (token != NULL)
   {
      while (token != NULL)
      {
         if (isdigit(token[0]))
         {
            vector[member++] = atoi (token);
         }
         else
         {
            return (-1);         /* non-numeric input */
         }
         if (member == max_elements)
         {
            return(-2);          /* too many members */
         }
         token = strtok (NULL, ",");
      }
   }
   else 
   {
      return (-3);               /* no tokens found (I think) */
   }
   return (member);
}     /* GetVector */



/* ----------------------------- MNI Header -----------------------------------
@NAME       : VerifyVectors
@INPUT      : Slices[], Frames[] - lists of desired slices/frames
              NumSlices, NumFrames - number of elements used in each array
              Image - pointer to struct describing the image:
                # of frames/slices, etc.
@OUTPUT     : 
@RETURNS    : TRUE if no member of Slices[] or Frames[] is invalid (i.e.
              larger than, respectively, Images->Slices or Images->Frames)
              FALSE otherwise
@DESCRIPTION: 
@METHOD     : 
@GLOBALS    : ErrMsg
@CALLS      : 
@CREATED    : 
@MODIFIED   : 
---------------------------------------------------------------------------- */
Boolean VerifyVectors (long Slices[], long Frames[],
                       int NumSlices, int NumFrames,
                       ImageInfoRec *Image)
{
   int   i;

   if (debug)
   { 
      printf ("Checking slices (%d of 'em) and frames (%d of 'em) for validity...\n", NumSlices, NumFrames);
      printf ("No slice >= %ld or frame >= %ld allowed\n",
              Image->Slices, Image->Frames);
   }

   for (i = 0; i < NumSlices; i++)
   {
      if (debug)
      {
         printf ("User slice %d is study slice %d\n", i, Slices[i]);
      }
      if ((Slices [i] >= Image->Slices) || (Slices [i] < 0))
      {
         sprintf (ErrMsg, "Bad slice number: %ld (must be < %ld)", 
                  Slices[i], Image->Slices);
         return (FALSE);
      }
   }     /* for i - loop slices */

   for (i = 0; i < NumFrames; i++)
   {
      if (debug)
      {
         printf ("User frame %d is study frame %d\n", i, Frames[i]);
      }
      if ((Frames [i] >= Image->Frames) || (Frames [i] < 0))
      {
         sprintf (ErrMsg, "Bad frame number: %ld (must be < %ld)", 
                  Frames[i], Image->Frames);
         return (FALSE);
      }

   }     /* for i - loop frames */

   return (TRUE);
}     /* VerifyVectors */



/* ----------------------------- MNI Header -----------------------------------
@NAME       : OpenTempFile
@INPUT      : Filename - name of the file to open
@OUTPUT     : 
@RETURNS    : pointer to the file structure if successful, NULL otherwise
@DESCRIPTION: Open a file for (binary) input.
@METHOD     : 
@GLOBALS    : ErrMsg
@CALLS      : 
@CREATED    : 93-6-3, Greg Ward
@MODIFIED   : 
---------------------------------------------------------------------------- */
FILE *OpenTempFile (char Filename [])
{
   FILE  *Newfile;

   Newfile = fopen (Filename, "rb");
   if (Newfile == NULL)
   {
      sprintf (ErrMsg, "Error opening input file %s", Filename);
      return (NULL);
   }
   
   return (Newfile);
}     /* OpenTempFile */


/* ----------------------------- MNI Header -----------------------------------
@NAME       : ReadNextImage
@INPUT      : Buffer - pointer to array of doubles where data will be put
                (must be preallocated with ImageSize doubles)
              ImageSize - number of doubles to read fromthe file
              InFile - the file to read from
@OUTPUT     : fills in Buffer
@RETURNS    : TRUE if image was read successfully
              FALSE if not all elements were read
@DESCRIPTION: 
@METHOD     : 
@GLOBALS    : debug
@CALLS      : 
@CREATED    : 93-6-3, Greg Ward
@MODIFIED   : 
---------------------------------------------------------------------------- */
Boolean ReadNextImage (double *Buffer, long ImageSize, FILE *InFile)
{ 
   int   AmtRead;
   int   i;

   AmtRead = fread (Buffer, sizeof (double), (size_t) ImageSize, InFile);
   if (debug)
   {
      printf ("Read an image from temp file; pointer now at byte %d\n", 
              ftell(InFile));
      printf ("%d values read; wanted %ld\n", AmtRead, ImageSize);
   }
   if ((debug) && (feof (InFile)))
   {
      printf ("At end of file\n");
   }
   if ((long) AmtRead != ImageSize)
   {
      return (FALSE);
   }
   else
   {
      return (TRUE);
   }
}     /* ReadNextImage */



/* ----------------------------- MNI Header -----------------------------------
@NAME       : PutMaxMin
@INPUT      : ImInfo - pointer to struct describing the image variable
              ImVals - pointer to array of doubles containing the image data
              SliceNum, FrameNum - needed to correctly place the max and min
                values into the MIimagemax and MIimagemin variables
              DoFrames - whether or not there is a time dimension in this file
@OUTPUT     : (none)
@RETURNS    : (void)
@DESCRIPTION: Finds the max and min values of an image, and puts them
              into the MIimagemax and MIimagemin variables associated
              with the specified image variable.
@METHOD     : 
@GLOBALS    : debug
@CALLS      : 
@CREATED    : 93-6-3, Greg Ward
@MODIFIED   : 
---------------------------------------------------------------------------- */
void PutMaxMin (ImageInfoRec *ImInfo, double *ImVals, 
                long SliceNum, long FrameNum, 
                Boolean DoFrames)
{
   int      i;
   double   Max, Min;
   long     Coord [2];
   
   Max = - DBL_MAX;
   Min = DBL_MAX;

   /*
    * Find the actual max and min values in the buffer
    */

   for (i = 0; i < ImInfo->ImageSize; i++)
   {
      if (ImVals [i] > Max)
      {
         Max = ImVals [i];
      }

      if (ImVals [i] < Min)
      {
         Min = ImVals [i];
      }
   }     /* for i */

   /*
    * Now figure out the Coord vector (where to put the max and min
    * within the MIimagemax and MIimagemin variables), and put 'em there
    */

   if (DoFrames)        /* i.e. some frame was specified */
   { 
      Coord [ImInfo->FrameDim] = FrameNum;
   }
   Coord [ImInfo->SliceDim] = SliceNum;

   if (debug)
   {
      printf ("Slice %ld, frame %ld: max is %lg, min is %lg\n", 
              SliceNum, FrameNum, Max, Min);
      printf ("Coord vector is: %ld %ld\n", Coord [0], Coord [1]);
   }

   mivarput1 (ImInfo->CDF, ImInfo->MaxID, Coord, NC_DOUBLE, MI_SIGNED, &Max);
   mivarput1 (ImInfo->CDF, ImInfo->MinID, Coord, NC_DOUBLE, MI_SIGNED, &Min);

}     /* PutMaxMin */


/* ----------------------------- MNI Header -----------------------------------
@NAME       : WriteImages
@INPUT      : TempFile - where the images come from
              Image - where they're going
              Slices - vector containing list of slices to write
              Frames - vector containing list of frames to write
              NumSlices - the number of elements of Slices[] actually used
              NumFrames - the number of elements of Frames[] actually used
@OUTPUT     : 
@RETURNS    : an error code as defined in mierrors.h
              ERR_NONE = all went well
              ERR_IN_TEMP = ran out of data reading the temp file
              ERR_OUT_MINC = some problem writing to MINC file
                (this should not happen!!!)
              also sets ErrMsg in the event of an error
@DESCRIPTION: Reads images sequentially from TempFile, and writes them into
              the image variable specified by *Image at the slice/frame
              locations specified by Slices[] and Frames[].  Smart enough
              to handle files with no time dimension, but assumes there
              is always a z dimension.
@METHOD     : 
@GLOBALS    : 
@CALLS      : 
@CREATED    : 93-6-3, Greg Ward
@MODIFIED   : 
---------------------------------------------------------------------------- */
int WriteImages (FILE *TempFile,
                 ImageInfoRec *Image,
                 long Slices[], 
                 long Frames[],
                 long NumSlices,
                 long NumFrames)
{
   int      slice, frame;
   long     Start [MAX_NC_DIMS], Count [MAX_NC_DIMS];
   double   *Buffer;
   Boolean  DoFrames;
   Boolean  Success;
   int      RetVal;

   Buffer = (double *) calloc (Image->ImageSize, sizeof (double));

   /*
    * First ensure that we will always read an *entire* image, but only
    * one slice/frame at a time (no matter how many slices/frames we
    * may be reading)
    */

   Start [Image->HeightDim] = 0; Count [Image->HeightDim] = Image->Height;
   Start [Image->WidthDim] = 0;  Count [Image->WidthDim] = Image->Width;
   Count [Image->SliceDim] = 1;
   if (NumFrames > 0)
   {
      Count [Image->FrameDim] = 1;
      DoFrames = TRUE;
   }
   else
   {
      DoFrames = FALSE;
      NumFrames = 1;
   }
  
   for (slice = 0; slice < NumSlices; slice++)
   {
      Start [Image->SliceDim] = Slices [slice];

      /* 
       * Loop through all frames, reading/writing one image each time.
       * Note that NumFrames will be one even if DoFrames is false; 
       * so this loop WILL always execute, but it and the functions it calls
       * (particularly PutMaxMin) act slightly differently depending on
       * the value of DoFrames,
       */

      for (frame = 0; frame < NumFrames; frame++)
      {
         Success = ReadNextImage (Buffer, Image->ImageSize, TempFile);
         if (!Success)
         {
            sprintf (ErrMsg, 
                    "Error reading from temporary file at slice %d, frame %d", 
                    slice, frame);
            return (ERR_IN_TEMP);
         }

         PutMaxMin (Image, Buffer, 
                    Slices [slice], Frames [frame], 
                    DoFrames);

         if (DoFrames)
         {
            Start [Image->FrameDim] = Frames [frame];
         }

         RetVal = miicv_put (Image->ICV, Start, Count, Buffer);
         if (RetVal == MI_ERROR)
         {
            sprintf (ErrMsg, "INTERNAL BUG: Fail on miicv_put: Error code %d",
                     ncerr);
            return (ERR_OUT_MINC);
         }
      }     /* for frame */
   }     /* for slice */

   /*
    * Use the MIcomplete attribute to signal that we are done writing
    */
   miattputstr (Image->CDF, Image->ID, MIcomplete, MI_TRUE);
   free (Buffer);
   return (ERR_NONE);

}     /* WriteImages */



int main (int argc, char *argv [])
{
   ImageInfoRec   ImInfo;
   long        Slice[MAX_NC_DIMS];
   long        Frame[MAX_NC_DIMS];
   long        NumSlices;
   long        NumFrames;
   FILE        *InFile;
   int         Result;
   int         i;                   /* debug only */

   debug = FALSE;
   if (argc != NUM_ARGS + 1)        /* +1 because argv[0] counts! */
   {
      ErrAbort ("Incorrect number of arguments", TRUE, ERR_ARGS);
   }

   /*
    * Parse the two lists of numbers first off
    */
   NumSlices = GetVector (SLICE_VECTOR, Slice, MAX_NC_DIMS);
   NumFrames = GetVector (FRAME_VECTOR, Frame, MAX_NC_DIMS);

   if ((NumSlices < 0) || (NumFrames < 0))
   {
      ErrAbort ("Error specifying slices and/or frames vector", 
                TRUE, ERR_ARGS);
   }
   if ((NumSlices > 1) && (NumFrames > 1))
   {
      ErrAbort ("Cannot specify both multiple frames and multiple slices", 
                TRUE, ERR_ARGS);
   }

   if (debug)
   { 
      printf ("Slices specified: ");
      for (i = 0; i < NumSlices; i++)
      {
         printf ("%8d", Slice[i]);
      }
      printf ("\n");

      printf ("Frames specified: ");
      if (NumFrames == 0)
      {
         printf ("(None)\n");
      }
      else
      {
         for (i = 0; i < NumFrames; i++)
         {
            printf ("%8d", Frame[i]);
         }
         printf ("\n");
      }
   }

	Result = OpenImage (MINC_FILE, &ImInfo, NC_WRITE);
   if (Result != ERR_NONE)
   {
      ErrAbort (ErrMsg, TRUE, Result);
   }

   if (!VerifyVectors (Slice, Frame, NumSlices, NumFrames, &ImInfo))
   {
      ErrAbort (ErrMsg, TRUE, ERR_ARGS);
   }

   InFile = OpenTempFile (TEMP_FILE);
   if (InFile == NULL)
   {
      ErrAbort (ErrMsg, TRUE, ERR_IN_TEMP);
   }

   Result = WriteImages (InFile, &ImInfo, Slice, Frame, NumSlices, NumFrames);
   if (Result != ERR_NONE)
   {
      ErrAbort (ErrMsg, TRUE, Result);
   }

   miicv_free (ImInfo.ICV);
   ncclose (ImInfo.CDF);
   exit (ERR_NONE);

}     /* main */