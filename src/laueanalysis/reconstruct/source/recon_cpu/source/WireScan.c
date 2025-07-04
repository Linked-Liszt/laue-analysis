#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include "mathUtil.h"
#include "microHDF5.h"
#include "WireScanDataTypesN.h"
#include "WireScan.h"
#include "readGeoN.h"
#include "misc.h"
#include "depth_correction.h"

#define TYPICAL_mA		102.		/* average current, used with normalization */
#define TYPICAL_cnt3	88100.		/* average value of cnt3, used with normalization */

const long MiB = (1<<20);					/* 2^20, one mega byte */
/* const int REVERSE_X_AXIS = 0;			// set to non-zero value to flip x axis - double-check */
/* const double PI = 3.14159265358979323;	// not used anymore */

#define CHECK_FREE(A)   { if(A) free(A); (A)=NULL; }

/* control functions */
int main (int argc, const char **argv);
int start(char* infile, char* outfile, char* geofile, double depth_start, double depth_end, double resolution, int first_image, int last_image, \
	int out_pixel_type, int wireEdge, char* normalization, char* depthCorrectStr);
void printHelpText(void);
void processAll( int file_num_start, int file_num_end, char* fn_base, char* fn_out_base, char* normalization, gsl_matrix_float * depthCorrectMap);
void readSingleImage(char* filename, int imageIndex, int ilow, int ihi, int jlow, int jhi, char* normalization);
int find_first_valid_i(int i1, int i2, int jlo, int jhi, point_xyz wire, BOOLEAN use_leading_wire_edge);
int find_last_valid_i(int i1, int i2, int jlo, int jhi, point_xyz wire, BOOLEAN use_leading_wire_edge);
point_xyz wirePosition2beamLine(point_xyz wire_pos);

/* File I/O */
void getImageInfo(char* fn_base, int file_num_start, int file_num_end);
void get_intensity_map(char* filename_base, int file_num_start);
void readImageSet(char* fn_base, int ilow, int ihi, int jlow, int jhi, int file_num_start, int file_num_end, char* normalization);
void writeAllHeaders(char* fn_in_first, char* fn_out_base, int file_num_start, int file_num_end);
void write1Header(char* finalTemplate, char* fn_base, int file_num);
void write_depth_data(size_t start_i, size_t end_i, char* fn_base);
void write_depth_datai(int file_num, size_t start_i, size_t end_i, char* fileName);

/* image memory and image manipulation */
void setup_depth_images(int numImages);
void clear_depth_images(ws_image_set *is);
void delete_images(void);
void get_difference_images(void);
void add_pixel_intensity_at_depth(point_ccd pixel, double intensity, double depth);
//inline void add_pixel_intensity_at_index(point_ccd pixel, double intensity, long index);
void add_pixel_intensity_at_index(size_t i, size_t j, double intensity, long index);

/* actual calculations */
double index_to_beam_depth(long index);
double get_trapezoid_height(double partial_start, double partial_end, double full_start, double full_end, double depth);
point_xyz pixel_to_point_xyz(point_ccd pixel);
double pixel_xyz_to_depth(point_xyz point_on_ccd_xyz, point_xyz wire_position, BOOLEAN use_leading_wire_edge);
void depth_resolve(int i_start, int i_stop);
//inline void depth_resolve_pixel(double pixel_intensity, point_ccd pixel, point_xyz point, point_xyz next_point, point_xyz wire_position_1, point_xyz wire_position_2, BOOLEAN use_leading_wire_edge);
//inline void depth_resolve_pixel(double pixel_intensity, size_t i, size_t j, point_xyz point, point_xyz next_point, point_xyz wire_position_1, point_xyz wire_position_2, BOOLEAN use_leading_wire_edge);
void depth_resolve_pixel(double pixel_intensity, size_t i, size_t j, point_xyz point, point_xyz next_point, point_xyz wire_position_1, point_xyz wire_position_2, BOOLEAN use_leading_wire_edge);
void print_imaging_parameters(ws_imaging_parameters ip);


#ifdef DEBUG_ALL					/* temp debug variable for JZT */
int slowWay=0;						/* true if found reading stripes the slow way */
int verbosePixel=0;
//	#define pixelTESTi 49
//	#define pixelTESTj 60
//#define pixelTESTi 1092
//#define pixelTESTj 881
#if defined(pixelTESTi) && defined(pixelTESTj)
#define DEBUG_1_PIXEL
#endif
//	•abc()  from Igor
//	Intensity of Si_wire_1[881, 1092] = 60100
//	pixel[881, 1092] --> {13.85, 510.97, -2.23}mm
//	depth = 55.62 µm
void testing_depth(void);
void printPieceOfArrayInt(int ilo, int ihi, int jhlo, int jhi, int Nx, int Ny, unsigned short int buf[Nx][Ny],int itype);
void printPieceOfArrayDouble(int ilo, int ihi, int jhlo, int jhi, int Nx, int Ny, double buf[Nx][Ny]);
void printPieceOf_gsl_matrix(int ilo, int ihi, int jlo, int jhi, gsl_matrix *mat);
#endif



int main (int argc, const char *argv[]) {
	int		c;
	char	infile[FILENAME_MAX];
	char	outfile[FILENAME_MAX];
	char	geofile[FILENAME_MAX];
	char	paramfile[FILENAME_MAX];
	char	normalization[FILENAME_MAX];	/* if empty, then do not normalize */

	infile[0] = outfile[0] = geofile[0] = paramfile[0] = normalization[0] = '\0';
	double	depth_start = 0.;
	double	depth_end = 0.;
	double	resolution = 1;
	int		first_image = 0;
	int		last_image = 0;					/* defaults to last image in multi-image file */
	int		out_pixel_type = -1;			/* -1 flags that output image should have same type as input image */
	int		wireEdge = 1;					/* 1=leading edge of wire, 0=trailing edge of wire, -1=both edges */
	unsigned long required=0, requiredFlags=((1<<5)-1);	/* (1<<5)-1 == (2^5 - 1) requiredFlags are the arguments that must be set */
	int		ivalue;
#ifdef DEBUG_ALL
	char	ApplicationsPath[FILENAME_MAX];		/* path to applications, used for h5repack */
#endif

	/* initialize some globals */
	geoIn.wire.axis[0]=1; geoIn.wire.axis[0]=geoIn.wire.axis[0]=0;	/* default wire.axis is {1,0,0} */
	geoIn.wire.R[0] = geoIn.wire.R[1] = geoIn.wire.R[2] = 0;		/* default PM500 rotation of wire is 0 */
	distortionPath[0] = '\0';				/* start with it empty */
	depthCorrectStr[0] = '\0';				/* start with it empty */
	verbose = 0;
	percent = 100;
	cutoff = 0;
	AVAILABLE_RAM_MiB = 128;
	detNum = 0;								/* detector number */
#ifdef DEBUG_ALL
	getParentPath(ApplicationsPath);
	printf("ApplicationsPath = '%s'\n",ApplicationsPath);
#endif

	while (1)
	{
		static struct option long_options[] =
		{
			{"infile",				required_argument,		0,	'i'},
			{"outfile",				required_argument,		0,	'o'},
			{"geofile",				required_argument,		0,	'g'},
			{"depth-start",			required_argument,		0,	's'},
			{"depth-end",			required_argument,		0,	'e'},
			{"resolution",			required_argument,		0,	'r'},
			{"verbose",				required_argument,		0,	'v'},
			{"first-image",			required_argument,		0,	'f'},
			{"last-image",			required_argument,		0,	'l'},
			{"normalization",		required_argument,		0,	'n'},
			{"percent-to-process",	required_argument,		0,	'p'},
			{"wire-edges",			required_argument,		0,	'w'},
			{"memory",				required_argument,		0,	'm'},
			{"type-output-pixel",	required_argument,		0,	't'},
			{"distortion_map",		required_argument,		0,	'd'},
			{"detector_number",		required_argument,		0,	'D'},
			{"wireDepths",			required_argument,		0,	'W'},
			{"Parameters File",		required_argument,		0,	'F'},
			{"ignore",				optional_argument,		0,	'@'},
			{"help",				no_argument,			0,	'h'},
			{0, 0, 0, 0}
		};
		/* getopt_long stores the option index here. */
		int option_index = 0;

		c = getopt_long (argc, (char * const *)argv, "i:o:g:s:e:r:v:f:l:n:p:w:m:t:d:D:W:F:@::h::", long_options, &option_index);

		/* Detect the end of the options.  */
		if (c == -1)
			break;

		switch (c)
		{
			case 0:
				break;

			case 'i':
				strncpy(infile,optarg,FILENAME_MAX-2);
				infile[FILENAME_MAX-1] = '\0';				/* strncpy may not terminate */
				required = required | (1<<0);
				break;

			case 'o':
				strncpy(outfile,optarg,FILENAME_MAX-2);
				outfile[FILENAME_MAX-1] = '\0';				/* strncpy may not terminate */
				required = required | (1<<1);
				break;

			case 'g':
				strncpy(geofile,optarg,FILENAME_MAX-2);
				geofile[FILENAME_MAX-1] = '\0';				/* strncpy may not terminate */
				required = required | (1<<2);
				break;

			case 's':
				depth_start = atof(optarg);
				break;

			case 'e':
				depth_end = atof(optarg);
				required = required | (1<<3);
				break;

			case 'r':
				resolution = atof(optarg);
				break;

			case 'v':
				verbose = atoi(optarg);
				if (verbose < 0) verbose = 0;
				break;

			case 'f':
				first_image = atoi(optarg);
				break;

			case 'l':
				last_image = atoi(optarg);
				break;

			case 'n':
				strncpy(normalization,optarg,FILENAME_MAX-2);
				normalization[FILENAME_MAX-1] = '\0';		/* strncpy may not terminate */
				break;

			case 'p':
				percent = (float)atof(optarg);
				percent = MAX(0,percent);
				percent = MIN(100,percent);
				break;

			case 'm':
				AVAILABLE_RAM_MiB = atoi(optarg);
				AVAILABLE_RAM_MiB = MAX(AVAILABLE_RAM_MiB,1);
				break;

			case 't':
				ivalue = atoi(optarg);
				if (ivalue<0 ||ivalue>7 || ivalue==4) {
					error("-t switch needs to be followed by 0, 1, 2, 3, 5, 6, or 7\n");
					fprintf(stderr,"0=float(4 byte),   1=long(4 byte),  2=int(2 byte),  3=uint (2 byte)\n");
					fprintf(stderr,"5=double(8 byte),  6=int8 (1 byte), 7=uint8(1 type)\n");
					exit(1);
					return 1;
				}
				out_pixel_type = ivalue;					/* type of output pixel uses old WinView values */
				break;

			case 'w':
				if ('l'==optarg[0]) wireEdge = 1;			/* use only leading edge of wire */
				else if ('t'==optarg[0]) wireEdge = 0;		/* use only trailing edge of wire */
				else if ('b'==optarg[0]) {
					wireEdge = -1;							/* use both leading and trailing edges of wire */
					/* use type long for the output image (need + and - values) it not previously specified */
					out_pixel_type = out_pixel_type<0 ? 1 : out_pixel_type;
				}
				else {
					error("-w switch needs to be followed by l, t, or b\n");
					exit(1);
					return 1;
				}
				break;

			case 'd':
				strncpy(distortionPath,optarg,1022);
				distortionPath[1023-1] = '\0';				/* strncpy may not terminate */
				break;

			case 'D':
				detNum = atoi(optarg);
				required = required | (1<<4);
				if (detNum < 0 || detNum>2) {
					error("-D detector number must be 0, 1, or 2\n");
					exit(1);
					return 1;
				}
				break;

			case 'W':
				strncpy(depthCorrectStr,optarg,1022);
				depthCorrectStr[1023-1] = '\0';				/* strncpy may not terminate */
				break;
				
			case '@':				/* a do nothing, just skip */
				break;

			case 'F':				/* read all the parameters from a key=value file */
				strncpy(paramfile,optarg,FILENAME_MAX-2);
				paramfile[FILENAME_MAX-1] = '\0';			/* strncpy may not terminate */
				geofile[0] = '\0';
				required = readAllParameters(paramfile,infile,outfile,normalization,&first_image,&last_image,\
					&depth_start,&depth_end,&resolution,&out_pixel_type,&wireEdge,&detNum,distortionPath) ? 0 : 0xFFFFFFFF;
				break;

			default:
				printf ("Unknown command line argument(s)\n");

			case 'h':
				required = 0;								/* forces printing of help */
				break;
		}
	}
	if (required==0) {										/* nothing input, show help */
		printHelpText();
		exit(1);
	}
	else if (requiredFlags ^ (required & requiredFlags)) {	/* means NOT all required arguments have been set */
		error("some required -D detector number must be 0, 1, or 2\n");
		exit(1);
	}

	if (verbose > 0) {
		time_t systime;
		systime = time(NULL);

		printf("\nStarting execution at %s\n",ctime(&systime));
		printf("infile = '%s'",infile);
		printf("\noutfile = '%s'",outfile);
		printf("\ngeofile = '%s'",geofile);
		printf("\ndistortion map = '%s'",distortionPath);
		if (depthCorrectStr[0]) printf("\ndepthCorrect = '%s'",depthCorrectStr);
		if (paramfile[0]) printf("\nparamFile = '%s'",paramfile);
		printf("\ndepth range = [%g, %g]micron with resolution of %g micron",depth_start,depth_end,resolution);
		printf("\nimage index range = [%d, %d]  using %g%% of pixels",first_image,last_image,percent);
		if (normalization[0]) printf("\nnormalizing by value in tag:  '%s'",normalization);
		else printf("\nnot normalizing");

		if (wireEdge<0) printf("\nusing both leading and trailing edges of wire");
		else if (wireEdge) printf("\nusing only leading edge of wire (the usual)");
		else printf("\nusing oly TRAILING edge of wire");
		if (out_pixel_type >= 0) printf("\nwriting output images as type long");
		printf("\nusing %dMiB of RAM, and verbose = %d",AVAILABLE_RAM_MiB,verbose);
		printf("\n\n");
	}
	fflush(stdout);

	start(infile, outfile, geofile, depth_start, depth_end, resolution, first_image, last_image, out_pixel_type, wireEdge, normalization, depthCorrectStr);

	if (verbose) {
		time_t systime;
		systime = time(NULL);
		printf("\nExecution ended at %s",ctime(&systime));
	}
	return 0;
}


void printHelpText(void)
{
	printf("\nUsage: WireScan -i <file> -o <file> -g <file> [-s <\x23>] -e <\x23> [-r <\x23>] [-v <\x23>] [-f <\x23>] -l <\x23> [-p <\x23>]  [-t <\x23>]  [-m <\x23>] [-?] \n\n");
	printf("\n-i <file>,\t --infile=<file>\t\tlocation and leading section of file names to process");
	printf("\n-o <file>,\t --outfile=<file>\t\tlocation and leading section of file names to create");
	printf("\n-g <file>,\t --geofile=<file>\t\tlocation of file containing parameters from the wirescan");
	printf("\n-d <file>,\t --distortion map=<file>\tlocation of file with the distortion map, dXYdistortion");
	printf("\n-s <\x23>,\t\t --depth-start=<\x23>\t\tdepth to begin recording values at - inclusive");
	printf("\n-e <\x23>,\t\t --depth-end=<\x23>\t\tdepth to stop recording values at - inclusive");
	printf("\n-r <\x23>,\t\t --resolution=<\x23>\t\tum depth covered by a single depth-resolved image");
	printf("\n-v <\x23>,\t\t --verbose=<\x23>\t\t\toutput detailed output of varying degrees (0, 1, 2, 3)");
	printf("\n-f <\x23>,\t\t --first-image=<\x23>\t\tnumber of first image to process - inclusive");
	printf("\n-l <\x23>,\t\t --last-image=<\x23>\t\tnumber of last image to process - inclusive");
	printf("\n-n <tag>,\t --normalization=<tag>\t\ttag of variable in header to use for normalizing incident intensity, optional");
	printf("\n-p <\x23>,\t\t --percent-to-process=<\x23>\tonly process the p%% brightest pixels in image");
	printf("\n-w <l,t,b>,\t --wire-edges\t\t\tuse leading, trailing, or both edges of wire, (for both, output images will then be longs)");
	printf("\n-t <\x23>,\t\t --type-output-pixel=<\x23>\ttype of output pixel (uses old WinView numbers), optional");
	printf("\n-m <\x23>,\t\t --memory=<\x23>\t\t\tdefine the amount of memory in MiB that the programme is allowed to use");
	printf("\n-W <file>,\t --wireDepths=<file>\t\tfile with depth corrections for each pixel");
	printf("\n-?,\t\t --help\t\t\t\tdisplay this help");
	printf("\n\n");
	printf("Example: WireScan -i /images/image_ -o /result/image_ -g /geo/file -s 0 -e 100 -r 1 -v 1 -f 1 -l 401 -p 1\n\n");
	return;
}


int start(
	char *infile,					/* base name of input image files */
	char *outfile,					/* base name of output image files */
	char *geofile,					/* full path to geometry file */
	double depth_start,				/* first depth in reconstruction range (micron) */
	double depth_end,				/* last depth in reconstruction range (micron) */
	double resolution,				/* depth resolution (micron) */
	int first_image,				/* index to first input image file */
	int last_image,					/* index to last input image file */
	int out_pixel_type,				/* type to use for the output pixel */
	int wireEdge,					/* 1=leading edge of wire, 0=trailing edge of wire, -1=both edges */
	char *normalization,			/* optional tag for normalization */
	char* depthCorrectStr)			/* optional name of file with depth corrections for each pixel */
{
	double	seconds;				/* seconds of CPU time used */
	time_t	executionTime;			/* number of seconds since program started */
	clock_t	tstart = clock();		/* clock() provides cpu usage, not total elapsed time */
	time_t	sec0 = time(NULL);		/* time (since EPOCH) when program starts */
	int		err=0;

	if (strlen(geofile)<1) { }								/* skip if no geo file specified, could have been entered via -F command line flag */
	else if (!(err=readGeoFromFile(geofile, & geoIn))) {	/* readGeoFromFile returns 1=error */
		geo2calibration(&geoIn, detNum);					/* take values from geoIn and put them into calibration */
	} else err = 1;
	if (err) {
		error("Could not load geometry from a file");
		exit(1);
	}
	int i;
	for (i=0;i<MAX_Ndetectors;i+=1) geoIn.d[i].used = 0;	/* mark all detectors in geo as unused */
	geoIn.d[detNum].used = 1;								/* mark as used the one being used */
	if (verbose > 0) {
		printCalibration(verbose);
		printf("\n");
		fflush(stdout);
	}


	gsl_matrix_float * depthCorrectMap=NULL;
	depthCorrectMap = load_depth_correction_map(depthCorrectStr);


	/* write first part of summary, then close it and write last part after computing */
	FILE *f=NULL;
	char summaryFile[FILENAME_MAX];
	sprintf(summaryFile,"%ssummary.txt",outfile);
	if (!(f=fopen(summaryFile, "w"))) { printf("\nERROR -- start(), failed to open file '%s'\n\n",summaryFile); exit(1); }
	writeSummaryHead(f, infile, outfile, geofile, depth_start, depth_end, resolution, first_image, last_image, out_pixel_type, wireEdge, normalization, depthCorrectStr);
	fclose(f);

	/* initialize image_set.*, contains partial input images & wire positions and partial output images & total intensity */
	image_set.wire_scanned.v = NULL;
	image_set.wire_scanned.alloc = image_set.wire_scanned.size = 0;
	image_set.depth_resolved.v = NULL;
	image_set.depth_resolved.alloc = image_set.depth_resolved.size = 0;
	image_set.wire_positions.v = NULL;
	image_set.wire_positions.alloc = image_set.wire_positions.size = 0;
	image_set.depth_intensity.v = NULL;
	image_set.depth_intensity.alloc = image_set.depth_intensity.size = 0;

	user_preferences.depth_resolution = resolution;				/* depth resolution and range of the reconstruction (micron) */
	depth_start = round(depth_start/resolution)*resolution;		/* depth range should have same resolution as step size */
	depth_end = round(depth_end/resolution)*resolution;
	user_preferences.depth_start = depth_start;
	user_preferences.depth_end = depth_end;
	user_preferences.NoutputDepths = round((depth_end - depth_start) / resolution + 1.0);
	user_preferences.out_pixel_type = out_pixel_type;
	user_preferences.wireEdge = wireEdge;
	if (user_preferences.NoutputDepths < 1) {
		error("no output images to process");
		exit(1);
	}

#ifdef USE_DISTORTION_CORRECTION
	load_peak_correction_maps(distortionPath);
	/*	load_peak_correction_maps("/Users/tischler/dev/reconstructXcode_Mar07/dXYdistortion"); */
	/*	load_peak_correction_maps("/home/nathaniel/Desktop/Reconstruction/WireScan/dXYdistortion"); */
#endif

	/* *********************** this does everything *********************** */
	processAll(first_image, last_image, infile, outfile, normalization,depthCorrectMap);

	delete_images();
	/* TODO: clear the depth-resolved images from memory*/
	seconds = ((double)(clock() - tstart)) /((double)CLOCKS_PER_SEC);
	executionTime = time(NULL) - sec0;	/* number of seconds since program started */

	/* write remainder of summary file with the total intensity vs depth, for the user to check and see if the depth range is correct */
	if (!(f=fopen(summaryFile, "a"))) printf("\nERROR -- start(), failed to re-open file '%s'\n\n",summaryFile);
	else {														/* re-open file, this section added Apr 1, 2008  JZT */
		/* writeSummaryTail(f, seconds); */
		writeSummaryTail(f, (double)executionTime);
		fclose(f);
	}
	/* if (verbose) printf("\ntotal execution time for this process took %.1f seconds",seconds); */
	if (verbose) printf("\ntotal execution time for this process took %ld sec, for a CPU time of %.1f seconds",executionTime,seconds);

	/* de-allocate and zero out image_set.depth_intensity */
	CHECK_FREE(image_set.depth_intensity.v)
	image_set.depth_intensity.alloc = image_set.depth_intensity.size = 0;
#ifdef DEBUG_ALL					/* temp debug variable for JZT */
	if (slowWay) printf("\n\n********************************\n	reading the slow way\n********************************\n\n");
#endif

	return 0;
}




void processAll(
	int		file_num_start,				/* index to first input image file */
	int		file_num_end,				/* index to last input image file */
	char	*fn_base,					/* base name of input image files */
	char	*fn_out_base,				/* base name of output image files */
	char	*normalization,				/* optional tag for normalization */
//#pragma GCC diagnostic push
//#pragma GCC diagnostic ignored "-Wunused-parameter"			/* do not warn that depthCorrectMap, is unused */
	gsl_matrix_float *depthCorrectMap)	/* optional map of depth corrections */
//#pragma GCC diagnostic pop
{
#warning Have code for getting depthCorrectMap, but no way to use it yet.
	/* TODO: Have code for getting depthCorrectMap, but no way to use it yet. */
	if (verbose > 0) printf("\nloading image information");
	fflush(stdout);

	getImageInfo(fn_base, file_num_start, file_num_end);		/* sets many of the values in the structure imaging_parameters which is a global */

#ifdef DEBUG_1_PIXEL
	testing_depth();
#endif
	get_intensity_map(fn_base, file_num_start);					/* finds cutoff, and saves the first image of the wire scan for later comparison */

	/* set values in the output header */
	int	output_pixel_type;										/* WinView number type of output pixels */
	int	pixel_size;												/* for output image, number of bytes/pixel */
	output_pixel_type = (user_preferences.out_pixel_type < 0) ? imaging_parameters.in_pixel_type : user_preferences.out_pixel_type;
	pixel_size = (user_preferences.out_pixel_type < 0) ? imaging_parameters.in_pixel_bytes : WinView_itype2len(user_preferences.out_pixel_type);
	copyHDF5structure(&output_header, &in_header);			/* duplicate in_header into output_header */
	output_header.isize = pixel_size;							/* change size of pixels for output files */
	output_header.itype = output_pixel_type;
	output_header.xWire = output_header.yWire = output_header.zWire = NAN;	/* no wire positions in output file */

	/* create all of the output files, and write the headers, with a dummy image filled with 0 */
	char fn_in_first[FILENAME_MAX];								/* name of first input file */
	sprintf(fn_in_first,"%s%d.h5",fn_base,file_num_start);

#ifdef DEBUG_ALL
	clock_t tstart = clock();
	if (verbose > 0) { fprintf(stderr,"\nallocating disk space for results..."); fflush(stdout); }
#endif
	writeAllHeaders(fn_in_first,fn_out_base, 0, user_preferences.NoutputDepths - 1);
#ifdef DEBUG_ALL
	if (verbose > 0) { fprintf(stderr,"     took %.2f sec",((double)(clock() - tstart)) /((double)CLOCKS_PER_SEC)); fflush(stdout); }
#endif

	/* [file_num_start, file_num_end] is the total range of files to read */
	int		start_i, end_i;											/* first and last rows of the image to process, may be less than whole image depending upon depth range and wire range */
	/*		actually for HDF5 files, you probably have to do the whole range */
	size_t	rows;													/* number of rows (i's) that can be processed at once, limited by memory.  (1<<20) = 2^20 = 1MiB */
	size_t	max_rows;												/* maximum number of rows that can be processed with this memory allocation */
	rows = AVAILABLE_RAM_MiB * MiB;									/* total number of bytes available */
	rows -= (imaging_parameters.nROI_i * imaging_parameters.nROI_j * sizeof(double) * 3);	/* subract space for intensity and distortion maps */
	rows /= (imaging_parameters.nROI_j * sizeof(double));									/* divide by number of bytes per line */
	rows /= (imaging_parameters.NinputImages + user_preferences.NoutputDepths);				/* divide by number of images to store */
	rows = MAX(rows,1);												/* always at least one row */
	max_rows = rows;												/* save maxium value for later */
	if (verbose > 0) printf("\nFrom the amount of RAM, can process %lu rows at once",rows);
	fflush(stdout);

	/* get starting row and stopping row positions in images (range of i) */
	if (verbose > 0) { printf("\nsetup depth-resolved images in memory"); fflush(stdout); }
	start_i = 0;													/* start with whole image, then trim down depending upon wire range and depth range */
	end_i = (int)(in_header.xdim - 1);
	if (verbose > 0) printf("\nprocess rows %d thru %d",start_i,end_i);

	if (0) {
		if (user_preferences.wireEdge>=0) {								/* using only one edge of the wire */
			start_i = find_first_valid_i(start_i,end_i,0,imaging_parameters.nROI_j-1,imaging_parameters.wire_first_xyz,(BOOLEAN)(user_preferences.wireEdge));
			end_i = find_last_valid_i(start_i,end_i,0,imaging_parameters.nROI_j-1,imaging_parameters.wire_last_xyz,(BOOLEAN)(user_preferences.wireEdge));
		}
		else {															/* using both edges of the wire */
			int i1,i2;
			i1 = find_first_valid_i(start_i,end_i,0,imaging_parameters.nROI_j-1,imaging_parameters.wire_first_xyz,0);
			i2 = find_first_valid_i(start_i,end_i,0,imaging_parameters.nROI_j-1,imaging_parameters.wire_first_xyz,1);
			start_i = MIN(i1,i2);
			i1 = find_last_valid_i(start_i,end_i,0,imaging_parameters.nROI_j-1,imaging_parameters.wire_last_xyz,0);
			i2 = find_last_valid_i(start_i,end_i,0,imaging_parameters.nROI_j-1,imaging_parameters.wire_last_xyz,1);
			end_i = MAX(i1,i2);
		}
	}
	if (start_i<0 || end_i<0) {
		char errStr[1024];
		sprintf(errStr,"Could not find valid starting or stopping rows, got [%d, %d]",start_i,end_i);
		error(errStr);
		exit(1);
	}
	rows = MIN(rows,(size_t)(end_i-start_i+1));						/* re-set in case [start_i,end_i] is smaller, only have a few left */
	imaging_parameters.rows_at_one_time = rows;						/* number of rows that can be processed at one time due to memory limitations */
	if (verbose > 0) printf("\nneed to process rows %d thru %d, can do %lu rows at a time",start_i,end_i,rows);

	/* current row indicies */
	int cur_start_i = start_i;										/* start and stop row for one band of image that fits into memory */
	int cur_stop_i = (int)(start_i + rows - 1);
	cur_stop_i = MIN(end_i,cur_stop_i);

	/* in input and output images need space for (imaging_parameters.rows_at_one_time = rows) rows */
	/* allocate space for wire_scanned images of length (rows = imaging_parameters.rows_at_one_time) */
	setup_depth_images(file_num_end-file_num_start+1);				/* allocate space and initialize the structure image_set, which contains the output */
	if (verbose > 0) print_imaging_parameters(imaging_parameters);

	/* loop through ram-managable stripes of the image and process them */
	while (cur_start_i <= end_i ) {
		imaging_parameters.current_selection_start = cur_start_i;
		imaging_parameters.current_selection_end = cur_stop_i;

		clear_depth_images(&image_set);				/* sets all images in image_set.depth_resolved and image_set.wire_scanned to zero, does not de-allocate the space they use, or change .size or .alloc */
		/* NOTE, do NOT clear image_set.depth_intensity or image_set.wire_positions */
		if (verbose > 1) printf("\n");
		if (verbose > 0) printf("\nprocessing rows %d thru %d  (%d of %d)...",cur_start_i,cur_stop_i,cur_stop_i-cur_start_i+1,end_i-start_i+1);
		fflush(stdout);

		/* read stripes from the input image files */
		readImageSet(fn_base, cur_start_i, cur_stop_i, 0, imaging_parameters.nROI_j - 1, file_num_start, file_num_end, normalization);

		if (verbose > 1) printf("\n\tdepth resolving");
		if (verbose == 2) printf("       ");
		fflush(stdout);

		/* depth resolve the set of stripes just read */
		depth_resolve(cur_start_i, cur_stop_i);

		if (verbose > 1) printf("\n\twriting out data");
		if (verbose == 2) printf("      ");
		fflush(stdout);

		/* write the depth resolved stripes to the output image files */
		write_depth_data((size_t)cur_start_i, (size_t)cur_stop_i, fn_out_base);

		cur_start_i = cur_stop_i + 1;					/* increase row limits for next stripe */
		cur_stop_i = MIN(cur_stop_i+(int)rows,end_i);	/* make sure loop doesn't go outside of the assigned area. */
	}
	imaging_parameters.rows_at_one_time = max_rows;		/* save this for output to summary file */

	if (verbose > 1) printf("\n\nfinishing\n");
	fflush(stdout);
}





/* depth sort out the intensity for for the pixels in one stripe */
void depth_resolve(
	int i_start,			/* starting row of this stripe */
	int i_stop)				/* final row of this stripe*/
{
	point_ccd pixel_edge;					/* pixel indicies for an edge of a pixel (e.g. [117,90.5]) */
	point_xyz front_edge;					/* xyz coords of the front edge of a pixel */
	point_xyz back_edge;					/* xyz coords of the back edge of a pixel */
	double	diff_value;						/* intensity difference between two wire steps for a pixel */
	dvector pixel_values;					/* vector to hold one pixel's values at all depths */
	size_t	step;							/* index over the input images */
	size_t	idep;							/* index into depths */
	size_t	i,j;							/* loop indicies */

	pixel_values.size = pixel_values.alloc = imaging_parameters.NinputImages - 1 - 1;
	pixel_values.v = calloc(pixel_values.alloc,sizeof(double));				/* allocate space for array of doubles in the vector */
	if (!(pixel_values.v)) { fprintf(stderr,"\ncannot allocate space for pixel_values, %ld points\n",pixel_values.alloc); exit(1); }

#ifdef DEBUG_1_PIXEL
	if (i_start<=pixelTESTi && pixelTESTi<=i_stop) { printf("\n\n  ****** start story of one pixel, [%g, %g]\n",(double)pixelTESTi,(double)pixelTESTj); verbosePixel = 1; }
#endif
	get_difference_images();												/* sequential subtraction on all of the input images. */
#ifdef DEBUG_1_PIXEL
	verbosePixel = 0;
#endif

#warning "This loop is constructed assuming that the wire scans in the j direction, true for Orange detector, what about Yellow and Purple?"
#warning "Also assumed is that the wire scans from low j to high j (high 2theta to low 2theta), so leading edge of pixel is -0.5"
	for (i = i_start; i <= (size_t)i_stop; i++) {							/* loop over selected part of i */
		pixel_edge.i = (double)i;
		for (j=0; j < (size_t)imaging_parameters.nROI_j; j++) {				/* loop over all of j, wire travels in the j direction for the orange detector */
			if (j==0) {														/* only need to recompute this for first j */
				pixel_edge.j = 0.5 + ((double)j - 1);						/* upstream edge of pixel for the first pixel, (j-1) is the 'previous' j */
				back_edge = pixel_to_point_xyz(pixel_edge);					/* this is the back edge of the first pixel */
			}
			else {
				back_edge = front_edge;										/* reuse last front edge as the current back edge */
			}
#ifdef DEBUG_1_PIXEL
			verbosePixel = (i==pixelTESTi) && (j==pixelTESTj);
#endif
			pixel_edge.j = 0.5 + (double)j;
			front_edge = pixel_to_point_xyz(pixel_edge);					/* the front edge of this pixel */
			if ( gsl_matrix_get(intensity_map, i, j)  < cutoff) continue;	/* not enough intensity, skip this pixel */
			for (idep=0;idep<pixel_values.size;idep++) pixel_values.v[idep]=0.;	/* clear the pixel vector along depth, set all to zero */
#ifdef DEBUG_1_PIXEL
			if (verbosePixel)
				printf("\nback_edge = {%g, %g, %g},  front_edge = {%g, %g, %g} for pixel[%lu, %lu]",back_edge.x,back_edge.y,back_edge.z,front_edge.x,front_edge.y,front_edge.z,i,j);
#endif

			/* load the pixel vector full of values for this pixel
			 *  - 1 - 1 because images have already been differenced within the matricies
			 * meaning that the last image hasn't been differenced against anything and so we ignore it.
			 */
			for (step=0; step < (size_t)(imaging_parameters.NinputImages - 1 - 1); step++) {
				/* pixel locations are real coordinates on detector, but image is stripe of image from middle of image - correct for this. */
				pixel_values.v[step] = gsl_matrix_get(image_set.wire_scanned.v[step], i - imaging_parameters.current_selection_start, j);
			}

#warning "TODO: put any curve-fitting stuff here before we go through the pixel in a line"

#warning "are the limits of this loop correct?, should it be one longer?"
			for (step=0; step < (pixel_values.size)-1; step++) {			/* loop over all of the differenced intensities of this pixel */
				diff_value = pixel_values.v[step];
#ifdef DEBUG_1_PIXEL
				if (verbosePixel) printf("\n∆ pixel[%lu] values = %g",step,diff_value);
#endif
				if (diff_value==0) continue;								/* only process for non-zero intensity */
				else if (user_preferences.wireEdge<0) {						/* using both leading and trailing edges of the wire */
					/* DDDDDDDDDDDDDDDDD */
					depth_resolve_pixel(diff_value, i,j, back_edge, front_edge, image_set.wire_positions.v[step], image_set.wire_positions.v[step+1], 1);
					depth_resolve_pixel(diff_value, i,j, back_edge, front_edge, image_set.wire_positions.v[step], image_set.wire_positions.v[step+1], 0);
				}
				else if (user_preferences.wireEdge && diff_value>0 || !(user_preferences.wireEdge) && diff_value<0) {
					depth_resolve_pixel(diff_value, i,j, back_edge, front_edge, image_set.wire_positions.v[step], image_set.wire_positions.v[step+1], user_preferences.wireEdge);
				}
#ifdef DEBUG_1_PIXEL
				if (verbosePixel) printf("\n∆ pixel[%lu] values = %g",step,diff_value);
#endif
			}
#ifdef DEBUG_1_PIXEL
			if (verbosePixel) { printf("\n  ****** done with story of one pixel[%lu, %lu]\n",i,j); verbosePixel = 0; }
#endif
		}
	}
#ifdef DEBUG_1_PIXEL
	verbosePixel = 0;
#endif
	return;
}


/* Given the difference intensity at one pixel for two wire positions, distribute the difference intensity into the depth histogram */
/* This routine only tests for zero pixel_intensity, it does not avoid negative intensities,  this routine can accumulate negative intensities. */
/* This routine assumes that the wire is moving "forward" */
void depth_resolve_pixel(
	double pixel_intensity,				/* difference of the intensity at the two wire positions */
	size_t	i,							/* indicies to the the pixel being processed, relative to the full stored image, range is (xdim,ydim) */
	size_t	j,
	point_xyz back_edge,				/* xyz postition of the trailing edge of the pixel in beam line coords relative to the Si */
	point_xyz front_edge,				/* xyz postition of the leading edge of the pixel in beam line coords relative to the Si */
	point_xyz wire_position_1,			/* first wire position (xyz) in beam line coords relative to the Si */
	point_xyz wire_position_2,			/* second wire position (xyz) in beam line coords relative to the Si */
	BOOLEAN use_leading_wire_edge)		/* true=(use leading endge of wire), false=(use trailing edge of wire) */
{
	double	partial_start;					/* trapezoid parameters, depth where partial intensity begins (micron) */
	double	full_start;						/* depth where full pixel intensity begins (micron) */
	double	full_end;						/* depth where full pixel intensity ends (micron) */
	double	partial_end;					/* depth where partial pixel intensity ends (micron) */
	double	area;							/* area of trapezoid */
	double	maxDepth;						/* depth of deepest reconstructed image (micron) */
	double	dDepth;							/* local version of user_preferences.depth_resolution */
	long	m;								/* index to depth */
/*	double	depthOffset=0.0;				// depth correction for this pixel */

	if (pixel_intensity==0) return;											/* do not process pixels without intensity */
	pixel_intensity = use_leading_wire_edge ? pixel_intensity : -pixel_intensity;	/* invert intensity for trailing edge */

	dDepth = user_preferences.depth_resolution;								/* just a local copy */
	maxDepth = dDepth*(image_set.depth_resolved.size- 1) + user_preferences.depth_start;	/* max reconstructed depth (mciron) */
	/* change maxDepth by depth offset DDDDDDDDDDDDDD */
	
	/* get the depths over which the intensity from this pixel could originate.  These points define the trapezoid. */
	partial_end = pixel_xyz_to_depth(back_edge, wire_position_2, use_leading_wire_edge);
	partial_start = pixel_xyz_to_depth(front_edge, wire_position_1, use_leading_wire_edge);
	/* change partial_end and partial_start by depth offset DDDDDDDDDDDDDD */
	if (partial_end < user_preferences.depth_start || partial_start > maxDepth) return;		/* trapezoid does not overlap depth-resolved region, do not process */

	full_start = pixel_xyz_to_depth(back_edge, wire_position_1, use_leading_wire_edge);
	full_end = pixel_xyz_to_depth(front_edge, wire_position_2, use_leading_wire_edge);
	/* change full_start and full_end by depth offset DDDDDDDDDDDDDD */
	if (full_end < full_start) {			/* in case mid points are backwards, ensure proper order by swapping */
		double swap;
		swap = full_end;
		full_end = full_start;
		full_start = swap;
	}
	area = (full_end + partial_end - full_end - partial_start) / 2;			/* area of trapezoid assuming a height of 1, used for normalizing */
	if (area < 0 || isnan(area)) return;									/* do not process if trapezoid has no area (or is NAN) */

	long imax = (long)image_set.depth_resolved.size- 1;						/* imax is maximum allowed value of index */
	long start_index, end_index;											/* range of output images for this trapezoid */
	start_index = (long)floor((partial_start - user_preferences.depth_start) / dDepth);
	start_index = MAX((long)0,start_index);									/* start_index lie in range [0,imax] */
	start_index = MIN(imax,start_index);
	end_index = (long)ceil((partial_end - user_preferences.depth_start) / dDepth);
	end_index = MAX(start_index,end_index);									/* end_index must lie in range [start_index, imax] */
	end_index = MIN(imax,end_index);

#ifdef DEBUG_1_PIXEL
	if (verbosePixel) printf("\n\ttrapezoid over range (% .3f, % .3f) micron == image index[%ld, %ld],  area=%g",partial_start,partial_end,start_index,end_index,area);
#endif

	double area_in_range = 0;
	double depth_1, depth_2, height_1, height_2;							/* one part of the trapezoid that overlaps the current bin */
	double depth_i, depth_i1;												/* depth range of depth bin i */
	for (m = start_index; m <= end_index; m++) {							/* loop over possible depth indicies (m is index to depth-resolved image) */
		area_in_range = 0;
		depth_i = index_to_beam_depth(m) - (dDepth*0.5);					/* ends of current depth bin */
		depth_i1 = depth_i + dDepth;

		if (full_start > depth_i && partial_start < depth_i1) {				/* this depth bin overlaps first part of trapezoid (sloping up from zero) */
			depth_1 = MAX(depth_i,partial_start);
			depth_2 = MIN(depth_i1,full_start);
			height_1 = get_trapezoid_height(partial_start, partial_end, full_start, full_end, depth_1);
			height_2 = get_trapezoid_height(partial_start, partial_end, full_start, full_end, depth_2);
			area_in_range += ((height_1 + height_2) / 2 * (depth_2 - depth_1));
		}

		if (full_end > depth_i && full_start < depth_i1) {					/* this depth bin overlaps second part of trapezoid (the flat top) */
			depth_1 = MAX(depth_i,full_start);
			depth_2 = MIN(depth_i1,full_end);
			area_in_range += (depth_2 - depth_1);							/* the height of both points is 1, so area is just the width */
		}

		if (partial_end > depth_i && full_end < depth_i1) {					/* this depth bin overlaps third part of trapezoid (sloping down to zero) */
			depth_1 = MAX(depth_i,full_end);
			depth_2 = MIN(depth_i1,partial_end);
			height_1 = get_trapezoid_height(partial_start, partial_end, full_start, full_end, depth_1);
			height_2 = get_trapezoid_height(partial_start, partial_end, full_start, full_end, depth_2);
			area_in_range += ((height_1 + height_2) / 2 * (depth_2 - depth_1));
		}

		if (area_in_range>0) add_pixel_intensity_at_index(i,j, pixel_intensity * (area_in_range / area), m);		/* do not accumulate zeros */
	}
}

void add_pixel_intensity_at_index(
	size_t	i,							/* indicies to pixel, relative to the full stored image, range is (xdim,ydim) */
	size_t	j,
	double intensity,					/* intensity to add */
	long index)							/* depth index */
{
	double *d;							/* pointer to value in gsl_matrix */

#ifdef DEBUG_1_PIXEL
	if (verbosePixel && i==pixelTESTi && j==pixelTESTj) printf("\n\t\t adding %g to pixel [%lu, %lu] at depth index %ld",intensity,i,j,index);
#endif

	if (index < 0 || (unsigned long)index >= image_set.depth_resolved.size) return;	/* ignore if index is outside of valid range */
	i -= imaging_parameters.current_selection_start;	/* get pixel indicies relative to this stripe */

	/* get a pointer to the existing value of that pixel at that depth */
	d = gsl_matrix_ptr(image_set.depth_resolved.v[index], i,j);
	*d += intensity;
	image_set.depth_intensity.v[index] += intensity;	/* accumulate for the summary file */
}


/* for a trapezoid of max height 1, find the actual height at x=depth, y=0 outside of [partial_start,partial_end] & y=1 in [full_start,full_end] */
/* the order of the conditionals was chosen by their likelihood, the most likely test should come first, the least likely last. */
double get_trapezoid_height(
	double	partial_start,				/* first depth where trapezoid becomes non-zero */
	double	partial_end,				/* last depth where trapezoid is non-zero */
	double	full_start,					/* first depth of the flat top */
	double	full_end,					/* last depth of the flat top */
	double	depth)						/* depth we want the value for */
{
	if ( depth <= partial_start || depth >= partial_end )	return 0;								/* depth is outside trapezoid */
	else if( depth < full_start )	return (depth - partial_start) / (full_start - partial_start);	/* depth in first sloping up part */
	else if( depth > full_end )		return (partial_end - depth) / (partial_end - full_end);		/* depth in sloping down part */
	return 1;																						/* depth in flat middle */
}



/*inline double pixel_to_depth(point_ccd pixel, point_xyz wire_position, BOOLEAN use_leading_wire_edge);
 *inline double pixel_to_depth(point_ccd pixel, point_xyz wire_position, BOOLEAN use_leading_wire_edge)
 *{
 *	double depth;
 *	point_xyz point_on_ccd_xyz;
 *	point_on_ccd_xyz = pixel_to_point_xyz(pixel);
 *	depth = pixel_xyz_to_depth(point_on_ccd_xyz, wire_position, use_leading_wire_edge);
 *	return depth;
 *}
 */




/* Returns depth (starting point of ray with one end point at point_on_ccd_xyz that is tangent */
/* to leading (or trailing) edge of the wire and intersects the incident beam.  The returned depth is relative to the Si position (origin) */
/* depth is measured along the incident beam from the origin, not just the z value. */
double pixel_xyz_to_depth(
	point_xyz point_on_ccd_xyz,			/* end point of ray, an xyz location on the detector */
	point_xyz wire_position,			/* wire center, used to find the tangent point, has been PM500 corrected, origin subtracted, rotated by rho */
	BOOLEAN use_leading_wire_edge)		/* which edge of wire are using here, TRUE for leading edge */
{
	point_xyz	pixelPos;								/* current pixel position */
	point_xyz	ki;										/* incident beam direction */
	point_xyz	S;										/* point where rays intersects incident beam */
	double		pixel_to_wireCenter_y;					/* vector from pixel to wire center, y,z coordinates */
	double		pixel_to_wireCenter_z;
	double		pixel_to_wireCenter_len;				/* length of vector pixel_to_wireCenter (only y & z components) */
	double		wire_radius;							/* wire radius */
	double		phi0;									/* angle from yhat to wireCenter (measured at the pixel) */
	double		dphi;									/* angle between line from detector to centre of wire and to tangent of wire */
	double		tanphi;									/* phi is angle from yhat to tangent point on wire */
	double		b_reflected;
	double		depth;									/* the result */

	/* change coordinate system so that wire axis lies along {1,0,0}, a rotated system */

	ki.x = calibration.wire.ki.x;						/* ki = rho x {0,0,1} */
	ki.y = calibration.wire.ki.y;
	ki.z = calibration.wire.ki.z;
	pixelPos = MatrixMultiply31(calibration.wire.rho,point_on_ccd_xyz);	/* pixelPos = rho x point_on_ccd_xyz, rotate pxiel center to new coordinate system */

	pixel_to_wireCenter_y = wire_position.y - pixelPos.y; /* vector from point on detector to wire centre. */
	pixel_to_wireCenter_z = wire_position.z - pixelPos.z;
	pixel_to_wireCenter_len = sqrt(pixel_to_wireCenter_y*pixel_to_wireCenter_y + pixel_to_wireCenter_z*pixel_to_wireCenter_z);/* length of vector pixel_to_wireCenter */

	wire_radius = calibration.wire.diameter / 2;		/* wire radius */
	phi0 = atan2(pixel_to_wireCenter_z , pixel_to_wireCenter_y);	/* angle from yhat to wireCenter (measured at the pixel) */
	dphi = asin(wire_radius / pixel_to_wireCenter_len);	/* angle between line from detector to centre of wire and line to tangent of wire */
	tanphi = tan(phi0+(use_leading_wire_edge ? -dphi : dphi));	/* phi is angle from yhat to V (measured at the pixel) */

	b_reflected = pixelPos.z - pixelPos.y * tanphi;		/* line from pixel to tangent point is:   z = y*tan(phio±dphi) + b */
	/* line of incident beam is:   y = kiy/kiz * z		Thiis line goes through origin, so intercept is 0 */
	/* find intersection of this line and line from pixel to tangent point */
	S.z = b_reflected / (1-tanphi * ki.y / ki.z);		/* intersection of two lines at this z value */
	S.y = ki.y / ki.z * S.z;							/* corresponding y of point on incident beam */
	S.x = ki.x / ki.z * S.z;							/* corresponding z of point on incident beam */
	depth = DOT3(ki,S);

	/*	if (verbosePixel) {
	 *		printf("\n    -- pixel on detector = {%.3f, %.3f, %.3f}",point_on_ccd_xyz.x,point_on_ccd_xyz.y,point_on_ccd_xyz.z);
	 *		printf("\n       wire center = {%.3f, %.3f, %.3f} relative to Si (micron)",wire_position.x,wire_position.y,wire_position.z);
	 *		printf("\n       pixel_to_wireCenter = {%.9lf, %.9lf}µm,  |v|=%.9f",pixel_to_wireCenter_y,pixel_to_wireCenter_z,pixel_to_wireCenter_len);
	 *		printf("\n       phi0 = %g (rad),   dphi = %g (rad),   tanphi = %g,   depth = %.2f (micron)\n",phi0,dphi,tanphi,DOT3(ki,S));
	 *	}
	 */
	return depth;										/* depth measured along incident beam (remember that ki is normalized) */
}





/* Take the indicies to a detector pixel and returns an 3vector point in beam-line coordinates of the pixel centre
 * Here is the only place where the corrections for a ROI (binning & sub-region of detector) has been used.  Hopefully it is the only place needed.
 * All pixel values (binned & un-binned) are zero based.
 * This routine uses the same conventions a used in Igor
 */
point_xyz pixel_to_point_xyz(
	point_ccd pixel)					/* input, binned ROI (zero-based) pixel value on detector, can be non-integer, and can lie outside range (e.g. -05 is acceptable) */
{
	point_xyz coordinates;								/* point with coordinates in R3 to return */
	point_ccd corrected_pixel;							/* pixel data to be filled by the peak_correction method */
	double	x,y,z;										/* 3d coordinates */

#warning "here is the only place where the pixel is swapped for the transpose in an HDF5 file"
	corrected_pixel.i = pixel.j;						/* the transpose swap needed with the HDF5 files */
	corrected_pixel.j = pixel.i;

	/* convert pixel from binned ROI value to full frame un-binned pixels, both binned and un-binned are zero based. */
	corrected_pixel.i = corrected_pixel.i * imaging_parameters.bini + imaging_parameters.starti;		/* convert from binned ROI to full chip un-binned pixel */
	corrected_pixel.j = corrected_pixel.j * imaging_parameters.binj + imaging_parameters.startj;
	corrected_pixel.i += (imaging_parameters.bini-1)/2.;	/* move from leading edge of pixel to the pixel center(e) */
	corrected_pixel.j += (imaging_parameters.binj-1)/2.;	/*	this is needed because the center of a pixel changes with binning */

#ifdef DEBUG_1_PIXEL
	if (verbosePixel) printf("\nin pixel_to_point_xyz(), pixel = [%g, %g] (binned ROI, on input),   size is (%g, %g) (micron)",pixel.i,pixel.j,calibration.pixel_size_i,calibration.pixel_size_j);
	if (verbosePixel) printf("\n   corrected_pixel = [%g, %g] (un-binned full chip pixels)",corrected_pixel.i,corrected_pixel.j);
#endif
	corrected_pixel = PEAKCORRECTION(corrected_pixel);		/* do the distortion correction */

#if defined(DEBUG_ALL) && defined(USE_DISTORTION_CORRECTION)
	if (verbosePixel) printf("\n   distortion corrected_pixel = [%g, %g] (un-binned full chip pixels)",corrected_pixel.i,corrected_pixel.j);
#endif

	/* get 3D coordinates in detector frame of the pixel */
	x = (corrected_pixel.i - 0.5*(calibration.ccd_pixels_i - 1)) * calibration.pixel_size_i;	/* (x', y', z') position of pixel (detector frame) */
	y = (corrected_pixel.j - 0.5*(calibration.ccd_pixels_j - 1)) * calibration.pixel_size_j;	/* note, in detector frame all points on detector have z'=0 */
	/*if (REVERSE_X_AXIS) x = -x; */

	x += calibration.P.x;									/* translate by P (P is from geoN.detector.P) */
	y += calibration.P.y;
	z  = calibration.P.z;

	/* finally, rotate (x,y,z) by rotation vector geo.detector.R using precomputed matrix calibration.detector_rotation[3][3] */
	coordinates.x = calibration.detector_rotation[0][0] * x + calibration.detector_rotation[0][1] * y + calibration.detector_rotation[0][2] * z;
	coordinates.y = calibration.detector_rotation[1][0] * x + calibration.detector_rotation[1][1] * y + calibration.detector_rotation[1][2] * z;
	coordinates.z = calibration.detector_rotation[2][0] * x + calibration.detector_rotation[2][1] * y + calibration.detector_rotation[2][2] * z;
#ifdef DEBUG_1_PIXEL
	if (verbosePixel) printf("\n   pixel xyz coordinates = (%g, %g, %g)\n",coordinates.x,coordinates.y,coordinates.z);
#endif
	return coordinates;									/* return point_xyz coordinates */
}








/* allocate space and initialize the structure image_set, which contains the output */
void setup_depth_images(
	int numImages)						/* number of input images, needed for .wire_scanned and .wire_positions */
{
	long	Ndepths;				/* number of depth points */
	long	i;

	Ndepths = user_preferences.NoutputDepths;
	if (Ndepths<1 || numImages<1) {											/* nothing to do */
		image_set.depth_intensity.v =NULL;
		image_set.depth_resolved.v = NULL;
		image_set.depth_intensity.alloc = image_set.depth_intensity.size = 0;
		image_set.depth_resolved.alloc = image_set.depth_resolved.size = 0;
		image_set.wire_scanned.alloc = image_set.wire_scanned.size = 0;
		image_set.wire_positions.alloc = image_set.wire_positions.size = 0;
		return;
	}
	if (image_set.depth_intensity.v || image_set.depth_resolved.v || image_set.wire_positions.v || image_set.wire_scanned.v) {
		error("ERROR -- setup_depth_images(), one of 'image_set.*.v' is not NULL\n");
		exit(1);
	}
	if (image_set.depth_intensity.alloc || image_set.depth_resolved.alloc || image_set.wire_positions.alloc || image_set.wire_scanned.alloc) {
		error("ERROR -- setup_depth_images(), one of 'image_set.*.alloc' is not NULL\n");
		exit(1);
	}

	image_set.depth_intensity.v = calloc((size_t)Ndepths,sizeof(double));/* allocate space for array of doubles in the vector */
	if (!(image_set.depth_intensity.v)) { fprintf(stderr,"\ncannot allocate space for image_set.depth_intensity, %ld points\n",Ndepths); exit(1); }
	image_set.depth_intensity.alloc = image_set.depth_intensity.size = Ndepths;
	for (i=0; i<Ndepths; i++) image_set.depth_intensity.v[i] = 0.;		/* init to all zeros */

	image_set.depth_resolved.v = calloc((size_t)Ndepths,sizeof(gsl_matrix *));	/* allocate space for array of pointers to gsl_matricies (these are image) in the vector */
	if (!(image_set.depth_resolved.v)) { fprintf(stderr,"\ncannot allocate space for image_set.depth_resolved, %ld points\n",Ndepths); exit(1); }
	image_set.depth_resolved.alloc = image_set.depth_resolved.size = Ndepths;
	for (i=0; i<Ndepths; i++) {
		image_set.depth_resolved.v[i] = gsl_matrix_calloc((size_t)(imaging_parameters.rows_at_one_time), (size_t)(imaging_parameters.nROI_j));	/* pointers to gsl_matrix containing space for the image, initialized to zero */
	}

	/* *************** */
	/* allocate for .wire_scanned and .wire_positions for numImages input images */
	point_xyz badPnt;
	badPnt.x = badPnt.y = badPnt.z = NAN;
	image_set.wire_positions.v = calloc((size_t)numImages,sizeof(point_xyz));/* allocate space for array of doubles in the vector */
	if (!(image_set.wire_positions.v)) { fprintf(stderr,"\ncannot allocate space for image_set.wire_positions, %d points\n",numImages); exit(1); }
	image_set.wire_positions.alloc = numImages;							/* room allocated */
	image_set.wire_positions.size = numImages;							/* and set length used also */
	for (i=0; i<numImages; i++) image_set.wire_positions.v[i] = badPnt;	/* set all values to NAN */

	image_set.wire_scanned.v = calloc((size_t)numImages,sizeof(gsl_matrix *));	/* allocate space for array of pointers to gsl_matricies these are pieces of the input images */
	if (!(image_set.wire_scanned.v)) { fprintf(stderr,"\ncannot allocate space for image_set.wire_scanned, %d points\n",numImages); exit(1); }
	image_set.wire_scanned.alloc = numImages;							/* room allocated */
	image_set.wire_scanned.size = 0;									/* but nothing set */
	for (i=0; i<numImages; i++) {
		image_set.wire_scanned.v[i] = gsl_matrix_calloc((size_t)(imaging_parameters.rows_at_one_time), (size_t)(imaging_parameters.nROI_j));	/* pointers to gsl_matrix containing space for the image */
	}
}
/*	for (i = (long)(user_preferences.depth_start / user_preferences.depth_resolution); i <= (long)(user_preferences.depth_end / user_preferences.depth_resolution); i ++ ) {
 *		image = gsl_matrix_calloc(imaging_parameters.nROI_i, imaging_parameters.rows_at_one_time);
 *		image_set.depth_resolved.push_back(image);
 *		image_set.depth_intensity.push_back(0);
 *	}
 */


/* this just sets the images in image_set to zero, it does NOT de-allocate the space they use */
/* for .depth_resolved & .wire_scanned, set all the elements to zero, assumes space already allocated */
void clear_depth_images(
	ws_image_set *is)
{
	size_t i;
	for (i=0; i < is->depth_resolved.alloc; i++) gsl_matrix_set_zero(is->depth_resolved.v[i]);
	for (i=0; i < is->wire_scanned.alloc; i++) gsl_matrix_set_zero(is->wire_scanned.v[i]);
}

void delete_images(void)				/* delete the images stored in image_set, and deallocate everything too, do: .wire_scanned, .depth_resolved, and .wire_positions, but NOT .depth_intensity */
{
	gsl_matrix * image;

	/* de-allocate and zero out .wire_scanned */
	while (image_set.wire_scanned.alloc) {
		image = image_set.wire_scanned.v[--(image_set.wire_scanned.alloc)];
		if (image != 0) gsl_matrix_free(image);
	}
	CHECK_FREE(image_set.wire_scanned.v)
	image_set.wire_scanned.alloc = image_set.wire_scanned.size = 0;

	/* de-allocate and zero out .depth_resolved */
	while (image_set.depth_resolved.alloc) {
		image = image_set.depth_resolved.v[--(image_set.depth_resolved.alloc)];
		if (image != 0) gsl_matrix_free(image);
	}
	CHECK_FREE(image_set.depth_resolved.v)
	image_set.depth_resolved.alloc = image_set.depth_resolved.size = 0;

	/* de-allocate and zero out .wire_positions */
	CHECK_FREE(image_set.wire_positions.v)
	image_set.wire_positions.alloc = image_set.wire_positions.size = 0;
}
/*
 *	void delete_images()
 *	{
 *		gsl_matrix * image;
 *
 *		while (image_set.wire_scanned.size())
 *		{
 *			image = image_set.wire_scanned.back();
 *			if (image != 0) gsl_matrix_free ( image );
 *			image_set.wire_scanned.pop_back();
 *		}
 *		//image_set.wire_scanned.clear();
 *		image_set.wire_positions.clear();
 *	}
 */

/* subtract from each image from its following image */
void get_difference_images(void)
{
	size_t m;
	for (m=0; m < (image_set.wire_scanned.size)-1; m++) {
#ifdef DEBUG_1_PIXEL
		if (verbosePixel) printf("pixel[%d,%d] raw image[% 3d] = %g\n",pixelTESTi,pixelTESTj,(int)m,gsl_matrix_get(image_set.wire_scanned.v[m], pixelTESTi -  imaging_parameters.current_selection_start, pixelTESTj));
#endif
		gsl_matrix_sub(image_set.wire_scanned.v[m], image_set.wire_scanned.v[m+1]);	/* gsl_matrix_sub(a,b) -->  a -= b */
	}
}



/*  FILE IO  */



/* sets many of the values in the gobal structure imaging_parameters */
/* get header information from first and last input images, this is called at start of program */
void getImageInfo(
	char	*fn_base,						/* base file input name */
	int		file_num_start,					/* index to first input file */
	int		file_num_end)					/* index to last input file */
{
	point_xyz wire_pos;
	char	filename[FILENAME_MAX];						/* full filename */

#ifndef PRINT_HDF5_MESSAGES
	H5Eset_auto2(H5E_DEFAULT,NULL,NULL);				/* turn off printing of HDF5 errors */
#endif

	sprintf(filename,"%s%d.h5",fn_base,file_num_start);
	if (readHDF5header(filename, &in_header)) goto error_path;
	imaging_parameters.nROI_i = (int)(in_header.xdim);		/* number of binned pixels along the x direction of one image */
	imaging_parameters.nROI_j = (int)(in_header.ydim);		/* number of binned pixels in one full stored image along detector y */
	imaging_parameters.in_pixel_type = in_header.itype;	/* type (e.g. float, int, ...) of a pixel value, uses the WinView pixel types */
	imaging_parameters.in_pixel_bytes = in_header.isize;	/* number of bytes used to specify one pixel strength (bytes) */
	imaging_parameters.starti = (int)(in_header.startx);	/* definition of the ROI for a full image all pixel coordinates are zero based */
	imaging_parameters.endi   = (int)(in_header.endx);
	imaging_parameters.startj = (int)(in_header.starty);
	imaging_parameters.endj   = (int)(in_header.endy);
	imaging_parameters.bini   = (int)(in_header.groupx);
	imaging_parameters.binj   = (int)(in_header.groupy);

	imaging_parameters.NinputImages = file_num_end - file_num_start + 1;/* number of input images to process */

	positionerType = positionerTypeFromFileTime(in_header.fileTime);		/* sets global value position type, needed for wirePosition2beamLine() */
	wire_pos.x = in_header.xWire;
	wire_pos.y = in_header.yWire;
	wire_pos.z = in_header.zWire;
	imaging_parameters.wire_first_xyz = wirePosition2beamLine(wire_pos);/* correct raw wire position: PM500 distortion, origin, PM500 rotation, wire axis rotation */

	/* get wire position of the last image in the wire scan */
	struct HDF5_Header header;
	sprintf(filename,"%s%d.h5",fn_base,file_num_end);
	if (readHDF5header(filename, &header)) goto error_path;
	wire_pos.x = header.xWire;
	wire_pos.y = header.yWire;
	wire_pos.z = header.zWire;
	imaging_parameters.wire_last_xyz = wirePosition2beamLine(wire_pos);	/* correct raw wire position: PM500 distortion, origin, PM500 rotation, wire axis rotation */
	return;

error_path:
	error("getImageInfo(), could not read first of last header information, imaging_parameters.* not set\n");
	exit(1);
}


/* loads first image into intensity_map, and finds cutoff, the intensity that decides which pixels to use (uses percent to find cutoff) */
/* set the global 'cutoff' */
void get_intensity_map(
	char	*filename_base,				/* base name of image file */
	int		file_num_start)				/* index of image file */
{
	size_t	dimi = imaging_parameters.nROI_i;	/* full size of image */
	size_t	dimj = imaging_parameters.nROI_j;
	char	filename[FILENAME_MAX];		/* full filename */
	struct HDF5_Header header;

	intensity_map = gsl_matrix_alloc(dimi, dimj);	/* get memory for one whole image */
	sprintf(filename,"%s%d.h5",filename_base,file_num_start);
	readHDF5header(filename, &header);

	/* read data (of any kind) into a double array */
	if (HDF5ReadROIdouble(filename,"entry1/data/data", &(intensity_map->data), 0, (dimi-1), 0,(dimj-1), &in_header)) { error("\nFailed to open intensity map file"); exit(1); }


#ifdef DEBUG_1_PIXEL
	printf(" ***in get_intensity_map()...\n");
	printf("pixelTESTi=%d, pixelTESTj=%d    intensity_map->data[%u*%lu+%u] = %lg\n",pixelTESTi,pixelTESTj,pixelTESTi,intensity_map->tda,pixelTESTj,intensity_map->data[pixelTESTi*intensity_map->tda+pixelTESTj]);
	printf("pixelTESTi=%d, pixelTESTj+1=%d    intensity_map->data[%u*%lu+%u] = %lg\n",pixelTESTi,pixelTESTj+1,pixelTESTi,intensity_map->tda,pixelTESTj+1,intensity_map->data[pixelTESTi*intensity_map->tda+pixelTESTj+1]);
	printPieceOf_gsl_matrix(pixelTESTi-2, pixelTESTi+2, pixelTESTj-2, pixelTESTj+2, intensity_map);
	printf(" ***done with get_intensity_map()\n");
#endif


	/* remove image noise below certain value */
	size_t	sort_len = dimj*dimi;
	double *intensity_sorted;		/* std::vector<double> intensity_sorted;		This line is in WireScan.h, move it to here */
	intensity_sorted = (double*)calloc(sort_len,sizeof(double));
	if (!intensity_sorted) { fprintf(stderr,"\nCould not allocate intensity_sorted %ld bytes in get_intensity_map()\n",sort_len); exit(1); }

	//	size_t	i, j, m;
	//	for (m=j=0; j < dimj; j++) { for (i=0; i < dimi; i++) intensity_sorted[m++] = gsl_matrix_get(intensity_map, i, j); }
	//	// #warning "is this memcpy correct?"
	//	memcpy(intensity_sorted,intensity_map->data,dimi*dimj*sizeof(double));
	//	printf("gsl_matrix_get(intensity_map,%d, %d) = %g\n",pixelTESTi,pixelTESTj,gsl_matrix_get(intensity_map,pixelTESTi,pixelTESTj));
	//	printf("intensity_map->tda = %lu\n",intensity_map->tda);
	//	printf("intensity_sorted[%d*%lu + %d] = %g\n",pixelTESTi,dimj,pixelTESTj,intensity_sorted[pixelTESTi*dimj+pixelTESTj]);

	memcpy(intensity_sorted,intensity_map->data,dimi*dimj*sizeof(double));
	qsort(intensity_sorted,(dimj*dimi), sizeof(double), (void *)compare_double);

	cutoff = intensity_sorted[ (size_t)floor((double)sort_len * MIN((100. - percent)/100.,1.)) ];
	cutoff = MAX(cutoff,1);
	CHECK_FREE(intensity_sorted);

	if (verbose > 0) printf("\nignoring pixels with a value less than %d",cutoff);
	fflush(stdout);
	return;
}


void readImageSet(
	char	*fn_base,					/* base name of input image files */
	int		ilow,						/* range of ROI to read from file */
	int		ihi,
	int		jlow,						/* for best speed, jhi-jlow+1 == ydim */
	int		jhi,
	int		file_num_start,				/* index of first input image */
	int		file_num_end,				/* infex of last input image */
	char	*normalization)				/* optional tag for normalization */
{
	int		f;
	char	filename[FILENAME_MAX];			/* full filename */
	if (verbose > 1) printf("\n\tabout to load %d new images...",(file_num_end - file_num_start) + 1);
	fflush(stdout);

	for (f = file_num_start; f <= file_num_end; f++) {
#ifdef DEBUG_1_PIXEL
		if (f==file_num_start) verbosePixel=1;
#endif

		sprintf(filename,"%s%d.h5",fn_base,f);
		readSingleImage(filename, f-file_num_start, ilow, ihi, jlow, jhi, normalization);	/* load a single image from disk */

#ifdef DEBUG_1_PIXEL
		verbosePixel=0;
#endif
	}
	if (verbose > 2) printf("\n\t\tloaded images");
	fflush(stdout);
}

void readSingleImage(
	char	*filename,							/* fully qualified file name */
	int		imageIndex,							/* index to images, image number that appears in the full file name - first image number */
	int		ilow,								/* range of ROI to read from file */
	int		ihi,								/* these are in terms of the image stored in the file, not raw un-binned pixels of the detector */
	int		jlow,
	int		jhi,
	char	*normalization)						/* full path to meta-data to be used for normalization, if not found (i.e. empty string) nothing is done */
{
	struct HDF5_Header header;
	int		i,j;

	point_xyz wire_pos;						/* position of wire retrieved from image */

	/* matrix to store image */
	gsl_matrix *image;
	int dimi = ihi - ilow + 1;
	int dimj = jhi - jlow + 1;				/* for best performance jlow-jhi+1 == ydim */
	double *buf = NULL;

	/* set image_set.wire_scanned.size to be big enough (probably just increment .size by 1) */
	if ( (unsigned int)imageIndex >= image_set.wire_scanned.alloc) {/* do not have enough gsl_matrix arrays allocated */
		fprintf(stderr,"\nERROR -- readSingleImage(), need room for image #%d, but only have .alloc = %ld",imageIndex,image_set.wire_scanned.alloc);
		exit(2);
	}
	image_set.wire_scanned.size = imageIndex+1;		/* number of input images read so far */
	image = image_set.wire_scanned.v[imageIndex];

	if (readHDF5header(filename, &header)){
		error("Error reading image header");
		exit(1);
	}

	/* read data (of any kind) into a double array */
#ifdef DEBUG_ALL
	slowWay = ((size_t)(jhi-jlow+1)<(header.ydim)) || slowWay;	/* check stripe orientation */
#endif
	if (HDF5ReadROIdouble(filename, "entry1/data/data", &buf, (size_t)ilow, (size_t)ihi, (size_t)jlow, (size_t)jhi, &header)) {
		error("Error reading image");
		exit(1);
	}

	/* transfer into a gsl_matrix, cannot do memcpy because last stripe is narrower & so there could be a mismatch */
	for (j = 0; j < dimj; j++) {
		for (i = 0; i < dimi; i++) {
			gsl_matrix_set(image, (size_t)i, (size_t)j, buf[i*dimj + j]);
		}
	}

#ifdef DEBUG_1_PIXEL
	if (verbosePixel && ilow<=pixelTESTi && pixelTESTi<=ihi) {
		printf("\n ++++++++++ in readSingleImage(), finished reading i=[%d, %d], j=[%d, %d]",ilow,ihi,jlow,jhi);
		printf("\n ++++++++++ pixel[%d,%d] = %g,     ROI: i=[%d,%d], j=[%d, %d],  Nj=%d",pixelTESTi,pixelTESTj, buf[dimj*(pixelTESTi-ilow) + pixelTESTj],ilow,ihi,jlow,jhi,dimj);
		printf("\n ++++++++++ gsl pixel[%d-%d,%d] = %g",pixelTESTi,ilow,pixelTESTj,gsl_matrix_get(image, pixelTESTi-ilow, pixelTESTj));
		printf("\n            gsl_matrix.size1 = %lu,  gsl_matrix.size2 = %lu,  gsl_matrix.tda = %lu",image->size1,image->size2,image->tda);
		fflush(stdout);
	}
#endif

	/* resolve any normalization shortcuts here */
	char normUse[FILENAME_MAX];							/* value after resolving shortcuts */
	if (strcmp(normalization,"mA")==0) strncpy(normUse,"/entry1/microDiffraction/source/current",FILENAME_MAX-2);	/* shortcut for beam current */
	/*	else if (strcmp(normalization,"Io")==0) normUse[0]='\0'; */
	/*	else if (strcmp(normalization,"cnt3")==0) normUse[0]='\0'; */
	else strncpy(normUse,normalization,FILENAME_MAX-2);	/* no shortcut found, use what was passed */
	normUse[FILENAME_MAX-1] = '\0';						/* strncpy may not terminate */
	if (normUse[0]) {									/* if I have a normalization tag, try to use it */
		double norm;
		norm = readHDF5oneValue(filename, normUse);
		if (norm == norm) {								/* not true if norm is NAN */
#ifdef TYPICAL_mA
			if (strcmp(normUse,"mA")==0) norm /= TYPICAL_mA; /* for beam current, divide by typical beam current */
#endif
#ifdef TYPICAL_cnt3
			if (strcmp(normUse,"cnt3")==0) norm /= TYPICAL_cnt3;
#endif
			/* printf("\nnorm = %g      %d\n",norm,norm==norm); */
			gsl_matrix_scale(image,norm);
		}
	}

	if ((size_t)imageIndex >= image_set.wire_positions.size) { error("readSingleImage(), image_set.wire_positions.alloc too small"); exit(3); }
	/* #warning "the wire position is corrected here when it is read in for: PM500, origin, rotation (by rho)" */
	wire_pos.x = header.xWire;
	wire_pos.y = header.yWire;
	wire_pos.z = header.zWire;
	image_set.wire_positions.v[imageIndex] = wirePosition2beamLine(wire_pos);	/* correct raw wire position: PM500 distortion, origin, PM500 rotation, wire axis rotation */

	CHECK_FREE(buf);
}



/* write correct headers and blank (all zero) images for every output HDF5 file */
void writeAllHeaders(
	char	*fn_in_first,				/* full path (including the .h5) to the first input file */
	char	*fn_out_base,				/* full path up to index of the reconstructed output files */
	int		file_num_start,				/* first output file number */
	int		file_num_end)				/* last output file number */
{
	size_t	pixel_size = (user_preferences.out_pixel_type < 0) ? imaging_parameters.in_pixel_bytes : WinView_itype2len(user_preferences.out_pixel_type);
	size_t	NpixelsImage = imaging_parameters.nROI_i * imaging_parameters.nROI_j;	/* number of pixels in one whole image */
	int		i;
	char	filenameTemp[L_tmpnam];			/* a temp file, delete at end of this routine */
	char	finalTemplate[L_tmpnam];		/* final template file */
	char	*buf=NULL;
	int		dims[2] = {(int)(output_header.xdim), (int)(output_header.ydim)};
	hid_t	file_id=0;

	/* buf is the same size of one of the output images and is initialized by calloc() to all zeros */
	buf = (char*)calloc(NpixelsImage , pixel_size);
	if (!buf) {fprintf(stderr,"\nCould not allocate buf %lu bytes in writeAllHeaders()\n", NpixelsImage*imaging_parameters.in_pixel_bytes); exit(1);}

	/* create the first output file using fn_in_first as a template */
//#pragma GCC diagnostic push
//#pragma GCC diagnostic ignored "-Wdeprecated"			/* do not warn that tmpnam is deprecated */
	strcpy(filenameTemp,tmpnam(NULL));					/* get unique filenames */
	strcpy(finalTemplate,tmpnam(NULL));
//#pragma GCC diagnostic pop
	copyFile(fn_in_first,filenameTemp,1);

	/* delete the main data in file, and delete the wire positions from the template otuput file */
	if ((file_id=H5Fopen(filenameTemp,H5F_ACC_RDWR,H5P_DEFAULT))<=0) { fprintf(stderr,"error after file open, file_id = %d\n",file_id); goto error_path; }
	if ((file_id=H5Fopen(filenameTemp,H5F_ACC_RDWR,H5P_DEFAULT))<=0) { fprintf(stderr,"error after file open, file_id = %d\n",file_id); goto error_path; }
	if (deleteDataFromFile(file_id,"entry1/data","data")) { fprintf(stderr,"error trying to delete \"/entry1/data/data\", file_id = %d\n",file_id); goto error_path; }	/* delete the data */
	if (deleteDataFromFile(file_id,"entry1","wireX")) { fprintf(stderr,"error trying to delete \"/entry1/wireX\", file_id = %d\n",file_id); goto error_path; }			/* delete the wire positions */
	if (deleteDataFromFile(file_id,"entry1","wireY")) { fprintf(stderr,"error trying to delete \"/entry1/wireY\", file_id = %d\n",file_id); goto error_path; }
	if (deleteDataFromFile(file_id,"entry1","wireZ")) { fprintf(stderr,"error trying to delete \"/entry1/wireZ\", file_id = %d\n",file_id); goto error_path; }
	if (H5Fclose(file_id)) { fprintf(stderr,"file close error\n"); goto error_path; } else file_id = 0;

	/* write the depth */
	writeDepthInFile(filenameTemp,0.0);					/* create & write the depth, it will overwrite if depth already present */

	/* make a re-packed version of file */
	if (repackFile(filenameTemp,finalTemplate)) { fprintf(stderr,"error after calling repackFile()\n"); goto error_path; }

	/* re-create the /entry1/data/data, same full size, but with appropriate data type */
	if(createNewData(finalTemplate,"entry1/data/data",2,dims,getHDFtype(output_header.itype))) fprintf(stderr,"error after calling createNewData()\n");

	/* write entire image back into file, but using different data type, using HDF5WriteROI() */
	/*	for (i=0;i<(1024*1024);i++) wholeImage[i] = wholeImage[i] & 0x7FFFFFFF;	// trim off high order bit */
	if ((HDF5WriteROI(finalTemplate,"entry1/data/data",buf,0,(output_header.xdim)-1,0,(output_header.ydim)-1,getHDFtype(output_header.itype),&output_header)))
	{ fprintf(stderr,"error from HDF5WriteROI()\n"); goto error_path; }

	/* create each of the output files with the correct depth in it */
	for (i = file_num_start; i <= file_num_end; i++) write1Header(finalTemplate,fn_out_base, i);

	/* delete both unused files */
	printf("\n ********************************* START FIX HERE *********************************\n");
	deleteFile(filenameTemp);
	deleteFile(finalTemplate);
	printf("\n *********************************   END FIX HERE *********************************\n");
	CHECK_FREE(buf);
	return;

error_path:
	CHECK_FREE(buf);
	exit(1);
}
/* write the correct header and a single image of all zeros for an output HDF5 file */
void write1Header(
	char	*finalTemplate,				/* template output file, a cleaned up version of the input file */
	char	*fn_base,					/* full path up to index of the reconstructed output files */
	int		file_num)					/* output file number to write */
{
	double	depth;						/* depth measured from Si (micron) */
	char	fname[FILENAME_MAX];		/* full name of file to write */

	depth = index_to_beam_depth(file_num);
	if (fabs(depth)>1e7) {
		fprintf(stderr,"\nwrite1Header(), depthSi=%g out of range +-10e6\n",depth);
		exit(1);
	}

	/* duplicate template file with correct name */
	sprintf(fname,"%s%d.h5",fn_base,file_num);
	copyFile(finalTemplate,fname,1);

	/* write the depth */
	writeDepthInFile(fname,depth);		/* create & write the depth, it will overwrite if depth already present */
}



/* write out one stripe of the reconstructed image, and the correct depth */
/* multiple image version */
void write_depth_data(
	size_t	start_i,					/* start i of this stripe */
	size_t	end_i,						/* end i of this stripe */
	char	*fn_base)					/* base name of file, just add index and .h5 */
{
	//	int file_num_end = (int)((user_preferences.depth_end - user_preferences.depth_start) / user_preferences.depth_resolution);
	int file_num_end = user_preferences.NoutputDepths - 1;
	int m;
	char fileName[FILENAME_MAX];

	/*	if (verbose == 2) printf("     "); */
	for (m=0; m <= file_num_end; m++) {									/* output file numbers are in the range [0, file_num_end] */
		sprintf(fileName,"%s%d.h5",fn_base,m);
		write_depth_datai(m, start_i, end_i, fileName);
	}
}
/* single image version, write both the depth and the data */
void write_depth_datai(
	int		file_num,					/* the file number to write also the index into the number of output images, zero based */
	size_t	start_i,					/* start and end i of this stripe */
	size_t	end_i,
	char	*fileName)					/* fully qualified name of file */
{
	int		output_pixel_type, pixel_size;
	size_t	m;
	gsl_matrix *gslMatrix = image_set.depth_resolved.v[file_num];		/* pointer to the gsl_matrix with data to write */

	if (gslMatrix->size2 != gslMatrix->tda) {							/* I will be assuming that this is true, so check here */
		error("write_depth_datai(), gslMatrix.size1 != gslMatrix.tda");	/* this is needed so I can just write the gslMatrix->data directly */
		exit(1);
	}

#ifdef DEBUG_1_PIXEL
	if (start_i<=pixelTESTi && pixelTESTi<=end_i)
		printf("\t%%%%\t about to write stripe[%lu, %lu] of output image % 3d, pixel[%d,%d] = %g\t\tmax pixel = %g\n", \
			start_i,end_i,file_num,pixelTESTi,pixelTESTj,gsl_matrix_get(gslMatrix, pixelTESTi -  start_i, pixelTESTj),gsl_matrix_max(gslMatrix));
#endif

	output_pixel_type = (user_preferences.out_pixel_type < 0) ? imaging_parameters.in_pixel_type : user_preferences.out_pixel_type;
	pixel_size = (user_preferences.out_pixel_type < 0) ? imaging_parameters.in_pixel_bytes : WinView_itype2len(user_preferences.out_pixel_type);
	if (user_preferences.wireEdge>=0) {									/* using only one edge of wire, so ensure that that all values are positive (not using both edges of wire) */
		double	*d;
		if (gsl_matrix_max(gslMatrix)<=0.0) return;						/* do not need to write blocks of zero */
		for (m=0, d=gslMatrix->data; m<(gslMatrix->size1 * gslMatrix->tda + gslMatrix->size2); m++, d++) {
			*d = MAX(0,*d);												/* set all values in gslMatrix to a minimum of zero, no negative numbers */
		}
	}
	else if (gsl_matrix_max(gslMatrix)==0 && gsl_matrix_min(gslMatrix)==0) return;	/* for both sides of wire check if this piece of an image is all zero */

	/*	WinViewWriteROI(readfile, (char*)cbuf, output_pixel_type, imaging_parameters.nROI_i, 0, imaging_parameters.nROI_i - 1, start_i, end_i); */
	struct HDF5_Header header;
	header.xdim = in_header.xdim;				/* copy only the needed values into header */
	header.ydim = in_header.ydim;
	header.isize = pixel_size;
	header.itype = output_pixel_type;

	HDF5WriteROI(fileName,"entry1/data/data",(void*)(gslMatrix->data), start_i, end_i, 0, (size_t)(imaging_parameters.nROI_j - 1), H5T_IEEE_F64LE, &header);
}









/* convert index of a depth resolved image to its depth along the beam (micron) */
/* this is the depth of the center of the bin */
double index_to_beam_depth(
	long	index)						/* index to depth resolved images */
{
	double absolute_depth = index * user_preferences.depth_resolution;
	absolute_depth += user_preferences.depth_start;
	return absolute_depth;
}


#warning "find_first_valid_i() and find_last_valid_i() my be unusable with this detector"
/* find the lowest possible row (i) in the image given the depth range and image size.  Only the first wire position of the scan is needed. */
int find_first_valid_i(
	int ilo,							/* check i in the range [ilo,ihi], thses are usually the ends of the image */
	int ihi,
	int j1,								/* check pixels (i,j1) and(i,j2) */
	int j2,
	point_xyz wire,						/* first wire position (xyz) of wire scan (in beam line coords relative to the Si) */
	BOOLEAN use_leading_wire_edge)		/* true=used leading endge of wire, false=use trailing edge of wire */
{
	int i;							/* index to a row in the image */
	double	d;						/* computed depth from either j1 or j2 */
	point_ccd pixel_edge;			/* pixel indicies for the edge of a pixel (e.g. [117,90.5]) */
	point_xyz back_edge;			/* xyz postition of the trailing edge of the pixel (in beam line coords relative to the Si) */
	double depth_end = user_preferences.depth_end;							/* max depth of a reconstructed image */

	for (i=ilo; i<=ihi; i++) {
		pixel_edge.i = 0.5 + (double)i;
		pixel_edge.j = (double)j1;
		back_edge = pixel_to_point_xyz(pixel_edge);							/* the back (low) edge of this pixel, using j1 */
		d = pixel_xyz_to_depth(back_edge, wire, use_leading_wire_edge);
		/* change d by depth offset DDDDDDDDDDDDDD ?????*/
		if (user_preferences.depth_start <= d && d <= depth_end) return i;	/* this d lies in our depth range, so i is OK */
		if (i==ilo && user_preferences.depth_start > d && d <= depth_end) return i;	/* limiting i is negative */

		pixel_edge.j = (double)j2;
		back_edge = pixel_to_point_xyz(pixel_edge);							/* the back (low) edge of this pixel, using j2 */
		d = pixel_xyz_to_depth(back_edge, wire, use_leading_wire_edge);
		/* change d by depth offset DDDDDDDDDDDDDD ?????*/
		if (user_preferences.depth_start <= d && d <= depth_end) return i;	/* this d lies in our depth range, so i is OK */
		if (i==ilo && user_preferences.depth_start > d && d <= depth_end) return i;	/* limiting i is negative */
	}
	return -1;																/* none of the i were acceptable */
}


/* find the highest possible row (i) in the image given the depth range and image size.  Only the last wire position of the scan is needed. */
int find_last_valid_i(
	int ilo,							/* check i in the range [ilo,ihi] */
	int ihi,
	int j1,								/* check pixels (i,j1) and(i,j2) ilo & ihi are usually edges of image */
	int j2,
	point_xyz wire,						/* last wire position (xyz) of wire scan (in beam line coords relative to the Si) */
	BOOLEAN use_leading_wire_edge)		/* true=used leading endge of wire, false=use trailing edge of wire */
{
	int i;							/* index to a row in the image */
	double	d;						/* computed depth from either j1 or j2 */
	point_ccd pixel_edge;			/* pixel indicies for the edge of a pixel (e.g. [117,90.5]) */
	point_xyz front_edge;			/* xyz postition of the leading edge of the pixel (in beam line coords relative to the Si) */
	double depth_end = user_preferences.depth_end;							/* max depth of a reconstructed image */

	for (i=ihi; i>=ilo; i--) {
		pixel_edge.i = -0.5 + (double)i;
		pixel_edge.j = (double)j1;
		front_edge = pixel_to_point_xyz(pixel_edge);						/* the front (low) edge of this pixel, using j1 */
		d = pixel_xyz_to_depth(front_edge, wire, use_leading_wire_edge);
		/* change d by depth offset DDDDDDDDDDDDDD ?????*/
		if (user_preferences.depth_start <= d && d <= depth_end) return i;	/* this d lies in our depth range, so i is OK */
		if (i==ihi && d > depth_end) return i;								/* limiting i is past end of detector */

		pixel_edge.j = (double)j2;
		front_edge = pixel_to_point_xyz(pixel_edge);						/* the front (low) edge of this pixel, using j2 */
		d = pixel_xyz_to_depth(front_edge, wire, use_leading_wire_edge);
		/* change d by depth offset DDDDDDDDDDDDDD ?????*/
		if (user_preferences.depth_start <= d && d <= depth_end) return i;	/* this d lies in our depth range, so i is OK */
		if (i==ihi && d > depth_end) return i;								/* limiting i is past end of detector */
	}
	return -1;																/* none of the i were acceptable */
}



/* convert PM500 {x,y,z} to beam line {x,y,z} */
point_xyz wirePosition2beamLine(
	point_xyz wire_pos)								/* PM500 {x,y,z} values */
{
	double x,y,z;

	x = X2corrected(wire_pos.x);				/* do PM500 distortion correction for wire */
	y = Y2corrected(wire_pos.y);
	z = Z2corrected(wire_pos.z);
	x -= calibration.wire.centre_at_si_xyz.x;	/* offset wire to origin (the Si position) */
	y -= calibration.wire.centre_at_si_xyz.y;
	z -= calibration.wire.centre_at_si_xyz.z;

	/* rotate by the orientation of the positioner, this does not make the wire axis parallel to beam-line x-axis */
	wire_pos.x = calibration.wire.rotation[0][0]*x + calibration.wire.rotation[0][1]*y + calibration.wire.rotation[0][2]*z;	/* {X2,Y2,Z2} = w.Rij x {x,y,z},   rotate by R (a small rotation) */
	wire_pos.y = calibration.wire.rotation[1][0]*x + calibration.wire.rotation[1][1]*y + calibration.wire.rotation[1][2]*z;
	wire_pos.z = calibration.wire.rotation[2][0]*x + calibration.wire.rotation[2][1]*y + calibration.wire.rotation[2][2]*z;

	/* #warning "what should I do about the rotation to put wire axis parallel to beam line axis" */
	wire_pos = MatrixMultiply31(calibration.wire.rho,wire_pos);	/* wire_centre = rho x wire_centre, rotate wire position so wire axis lies along {1,0,0} */

	return wire_pos;
}






#ifdef DEBUG_ALL
void printPieceOf_gsl_matrix(				/* print some of the gsl_matrix */
	int		ilo,								/* i range to print is [ihi, ilo] */
	int		ihi,
	int		jlo,								/* j range to print is [jhi, jlo] */
	int		jhi,
	gsl_matrix *mat)
{
	int		i,j;
	printf("\nfor gsl_matrix_get(mat,i,j)  (j is the fast index)\nj\t i=");
	for (i=ilo;i<=ihi;i++) printf("\t% 5d",i); printf("\n");
	for (j=jlo;j<=jhi;j++) {
		printf("%d\t",j);
		for (i=ilo;i<=ihi;i++) printf("\t %g",gsl_matrix_get(mat,(size_t)i,(size_t)j));
		printf("\n");
	}
}


void printPieceOfArrayDouble(				/* print some of the double array */
	int		ilo,								/* i range to print is [ihi, ilo] */
	int		ihi,
	int		jlo,								/* j range to print is [jhi, jlo] */
	int		jhi,
	int		Nx,									/* size of array,  buf[Nx][Ny] */
	int		Ny,
	double buf[Nx][Ny])
{
	int		i,j;
	printf("\nfor double array[j][i]\nj\t i=");
	for (i=ilo;i<=ihi;i++) printf("\t% 5d",i); printf("\n");
	for (j=jlo;j<=jhi;j++) {
		printf("%d\t",j);
		for (i=ilo;i<=ihi;i++) printf("\t %g",buf[j][i]);
		printf("\n");
	}
}

void printPieceOfArrayInt(					/* print some of the array */
	int		ilo,								/* i range to print is [ihi, ilo] */
	int		ihi,
	int		jlo,								/* j range to print is [jhi, jlo] */
	int		jhi,
	int		Nx,									/* size of array,  buf[Nx][Ny] */
	int		Ny,
	unsigned short int buf[Nx][Ny],
	int		itype)
{
	int		i,j;
	printf("\nfor int array[j][i]\nj\t i=");
	for (i=ilo;i<=ihi;i++) printf("\t% 5d",i); printf("\n");
	for (j=jlo;j<=jhi;j++) {
		printf("%d\t",j);

		if (itype==3)
		{
			for (i=ilo;i<=ihi;i++) printf("\t %u",buf[j][i]);
		}
		else if (itype==2) {
			for (i=ilo;i<=ihi;i++) printf("\t %hd",buf[j][i]);
		}
		printf("\n");
	}
}
#endif




#if (0)
/*	THIS ROUTINE IS NOT USED, BUT KEEP IT AROUND ANYHOW */
double slitWidth(point_xyz a, point_xyz b, point_xyz sa, point_xyz sb);
/* calculate width of wire step perpendicular to the ray from source to wire, the width of the virtual slit */
double slitWidth(					/* distance between wire points a and b perp to ray = { (a+b)/2 - (sa+sb)/2 } */
	point_xyz a,						/* first wire position */
	point_xyz b,						/* second wire position */
	point_xyz sa,						/* first source point */
	point_xyz sb)						/* second source point */
{
	double dx,dy,dz;					/* vector from b to a (between two wire positions) */
	double dw2;							/* square of distance between two wire positions, |b-a|^2 */
	double dirx,diry,dirz;				/* direction vector from average source to average wire (not normalized) */
	double dir2;						/* square of length of dir */
	double dot;							/* dir .dot. dw */
	double wid2;						/* square of the answer */

	dirx = ((b.x+a.x) - (sb.x+sa.x))/2.;/* (a+b)/2 - (sa+sb)/2, ray from pixel to wire */
	diry = ((b.y+a.y) - (sb.y+sa.y))/2.;
	dirz = ((b.z+a.z) - (sb.z+sa.z))/2.;
	dir2 = dirx*dirx + diry*diry + dirz*dirz;
	if (dir2 <=0) return 0;				/* slit width is zero */

	dx = b.x - a.x;						/* vector between two wire positions */
	dy = b.y - a.y;
	dz = b.z - a.z;
	dw2 = sqrt(dx*dx + dy*dy + dz*dz);

	dot = dirx*dx + diry*dy + dirz*dz;

	wid2 = dw2 - (dot*dot)/dir2;		/* this line is just pythagoras */
	wid2 = MAX(wid2,0);
	return sqrt(wid2);
}
#endif



void print_imaging_parameters(
	ws_imaging_parameters ip)
{
	printf("\n\n*************  value of ws_imaging_parameters structure  *************\n");
	printf("\t\t[nROI_i, nROI_j] = [%d, %d]\n", ip.nROI_i,ip.nROI_j);
	printf("\t\tstartx=%d,  endx=%d,  binx=%d,  no. of points = %d\n",ip.starti,ip.endi,ip.bini,ip.endi-ip.starti+1);
	printf("\t\tstarty=%d,  endy=%d,  biny=%d,  no. of points = %d\n",ip.startj,ip.endj,ip.binj,ip.endj-ip.startj+1);
	printf("\t\tthere are %d input images\n",ip.NinputImages);
	printf("\t\tinput pixel is of type %d,   (length = %d bytes) // 0=float, 1=long, 2=short, 3=ushort, 4=char, 5=double, 6=signed char, 7=uchar\n",ip.in_pixel_type, ip.in_pixel_bytes);
	printf("\t\tfirst wire position is  {%g, %g, %g}\n",ip.wire_first_xyz.x,ip.wire_first_xyz.y,ip.wire_first_xyz.z);
	printf("\t\t last wire position is  {%g, %g, %g}\n",ip.wire_last_xyz.x,ip.wire_last_xyz.y,ip.wire_last_xyz.z);
	printf("\t\tcan measure rows_at_one_time = %lu\n",ip.rows_at_one_time);
	printf("\t\tcurrent row range is [%d, %d]\n", ip.current_selection_start,ip.current_selection_end);
	printf("*************  end of ws_imaging_parameters structure  *************\n");
	printf("\n");
}


#ifdef DEBUG_1_PIXEL
/*
 head->xDimDet	= get1HDF5data_int(file_id,"/entry1/detector/Nx",&ivalue) ? XDIMDET : ivalue;
 head->yDimDet	= get1HDF5data_int(file_id,"/entry1/detector/Ny",&ivalue) ? YDIMDET : ivalue;
 head->startx	= get1HDF5data_int(file_id,"/entry1/detector/startx",&ivalue) ? STARTX : ivalue;
 head->endx		= get1HDF5data_int(file_id,"/entry1/detector/endx",&ivalue) ? ENDX : ivalue;
 head->groupx	= get1HDF5data_int(file_id,"/entry1/detector/groupx",&ivalue) ? GROUPX : ivalue;
 head->starty	= get1HDF5data_int(file_id,"/entry1/detector/starty",&ivalue) ? STARTY : ivalue;
 head->endy		= get1HDF5data_int(file_id,"/entry1/detector/endy",&ivalue) ? ENDY : ivalue;
 head->groupy	= get1HDF5data_int(file_id,"/entry1/detector/groupy",&ivalue) ? GROUPY : ivalue;
 
 
 ROI: un-binned
 Xstart=820, Xsize=120		along beam
 Ystart=1045, Ysize=100		perpendicular to beam
 
 depth = -167.396710542 µm, using leading ede
 depth = -167.396710541619
 depth = -219.436661679 µm, using trailing edge
 depth = -219.436661679071
 
 value of [50,60] = 9660 for file 932
 value of [60,50] = 11852 for h5 file
 */
void testing_depth(void)
{
	point_ccd	pixel;
	point_xyz	xyzPixel;
	point_xyz	xyzWire;
	//	double		depth;
	//	BOOLEAN		use_leading_wire_edge=1;		// 1==989.266,   0==989.265
	//	double		leadingIgor = -167.396710541619;
	//	double		trailingIgor = -219.436661679071;


	pixel.i = pixelTESTi;
	pixel.j = pixelTESTj;
	xyzWire.x = -0.05;
	xyzWire.y = -5815.28;
	xyzWire.z = -1572.63;

	double H,F, root2=1./sqrt(2.);
	H =  root2 * xyzWire.y + root2 * xyzWire.z;
	F = -root2 * xyzWire.y + root2 * xyzWire.z;

	printf("\n\n\n start of testing_depth()\n");
	/*
	 printf("\n\ndetector P = {%.6f, %.6f, %.6f}\n",calibration.P.x,calibration.P.y,calibration.P.z);
	 printf("calibration.detector_rotation = \n");
	 printf("%13.9f	%13.9f	%13.9f	\n",calibration.detector_rotation[0][0],calibration.detector_rotation[0][1],calibration.detector_rotation[0][2]);
	 printf("%13.9f	%13.9f	%13.9f	\n",calibration.detector_rotation[1][0],calibration.detector_rotation[1][1],calibration.detector_rotation[1][2]);
	 printf("%13.9f	%13.9f	%13.9f	\n",calibration.detector_rotation[2][0],calibration.detector_rotation[2][1],calibration.detector_rotation[2][2]);
	 printf("***pixel X = %.9lf\n",calibration.detector_rotation[0][0] * 209773.0 + calibration.detector_rotation[0][1] * 14587.0 + calibration.detector_rotation[0][2] * 510991.0);
	 */

	xyzPixel = pixel_to_point_xyz(pixel);						/* input, binned ROI (zero-based) pixel value on detector, can be non-integer, and can lie outside range (e.g. -05 is acceptable) */
	printf("pixel = [%g, %g] --> {%.12lf, %.12lf, %.12lf}mm\n",pixel.i,pixel.j,xyzPixel.x/1000.,xyzPixel.y/1000.,xyzPixel.z/1000.);
	printf("wire starts at {%.6f, %.6f, %.6f}µm   H=%g, F=%g\n",xyzWire.x,xyzWire.y,xyzWire.z,H,F);

	xyzWire = wirePosition2beamLine(xyzWire);
	printf("corrected wire at {%.6f, %.6f, %.6f}µm\n",xyzWire.x,xyzWire.y,xyzWire.z);


	//	depth = pixel_xyz_to_depth(xyzPixel,xyzWire,use_leading_wire_edge);
	//	printf("depth (leading) = %.9f µm,  Igor got %.9f,  ∆=%.9f\n",depth,leadingIgor,depth-leadingIgor);
	//
	//	depth = pixel_xyz_to_depth(xyzPixel,xyzWire,0);
	//	printf("depth (trailing) = %.9f µm,  Igor got %.9f,  ∆=%.9f\n",depth,trailingIgor,depth-trailingIgor);

	printf(" done with testing_depth()\n\n");
	return;
	exit(10);
}
#endif
