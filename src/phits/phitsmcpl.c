
/////////////////////////////////////////////////////////////////////////////////////
//                                                                                 //
//  phitsmcpl : Code for converting between MCPL and binary PHITS dump files.      //
//                                                                                 //
//                                                                                 //
//  Compilation of phitsmcpl.c can proceed via any compliant C-compiler using      //
//  -std=c99 later. Furthermore, the following preprocessor flag can be used       //
//  when compiling phitsmcpl.c to fine tune the build process.                     //
//                                                                                 //
//  PHITSMCPL_HDR_INCPATH  : Specify alternative value if the phitsmcpl header     //
//                         itself is not to be included as "phitsmcpl.h".          //
//  PHITSREAD_HDR_INCPATH  : Specify alternative value if the phitsread header     //
//                         is not to be included as "phitsread.h".                 //
//  MCPL_HEADER_INCPATH  : Specify alternative value if the MCPL header is         //
//                         not to be included as "mcpl.h".                         //
//                                                                                 //
// This file can be freely used as per the terms in the LICENSE file.              //
//                                                                                 //
// However, note that usage of PHITS-related utilities might require additional    //
// permissions and licenses from third-parties, which is not within the scope of   //
// the MCPL project itself.                                                        //
//                                                                                 //
// Written 2019, thomas.kittelmann@esss.se (European Spallation Source).           //
//                                                                                 //
/////////////////////////////////////////////////////////////////////////////////////

#ifdef PHITSMCPL_HDR_INCPATH
#  include PHITSMCPL_HDR_INCPATH
#else
#  include "phitsmcpl.h"
#endif

#ifdef PHITSREAD_HDR_INCPATH
#  include PHITSREAD_HDR_INCPATH
#else
#  include "phitsread.h"
#endif

#ifdef MCPL_HEADER_INCPATH
#  include MCPL_HEADER_INCPATH
#else
#  include "mcpl.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <assert.h>

void phits_error(const char * msg);//fwd declare internal function from phitsread.c

int phitsmcpl_buf_is_text(size_t n, const unsigned char * buf) {
  //We correctly allow ASCII & UTF-8 but falsely classify UTF-16 and UTF-32 as
  //data. See http://stackoverflow.com/questions/277521#277568 for how we could
  //also detect UTF-16 & UTF-32.
  const unsigned char * bufE = buf + n;
  for (; buf!=bufE; ++buf)
    if ( ! ( ( *buf >=9 && *buf<=13 ) || ( *buf >=32 && *buf<=126 ) || *buf >=128 ) )
      return 0;
  return 1;
}

int phitsmcpl_file2buf(const char * filename, unsigned char** buf, size_t* lbuf, size_t maxsize, int require_text) {
  *buf = 0;
  *lbuf = 0;
  FILE * file = fopen(filename, "rb");
  if (!file) {
    printf("Error: could not open file %s.\n",filename);
    return 0;
  }

  size_t pos_begin = ftell(file);
  size_t bbuf_size = maxsize;//default to max size (in case SEEK_END does not work)
  int bbuf_size_guess = 1;
  if (!fseek( file, 0, SEEK_END )) {
    size_t pos_end = ftell(file);
    bbuf_size = pos_end-pos_begin;
    bbuf_size_guess = 0;
    if (bbuf_size<50) {
      printf("Error: file %s is suspiciously short.\n",filename);
      return 0;
    }
    if (bbuf_size>104857600) {
      printf("Error: file %s is larger than %g bytes.\n",filename,(double)maxsize);
      return 0;
    }
  }
  if (fseek( file, 0, SEEK_SET)) {
    printf("Error: Could not rewind file %s.\n",filename);
    return 0;
  }
  unsigned char * bbuf = malloc(bbuf_size);
  unsigned char * bbuf_iter = bbuf;
  size_t left = bbuf_size;
  while (left) {
    size_t nb = fread(bbuf_iter, 1, left, file);
    if (bbuf_size_guess&&nb==0) {
      bbuf_size -= left;
      break;
    }
    if (nb==0||nb>left) {
      printf("Error: file %s read-error.\n",filename);
      free(bbuf);
      return 0;
    }
    bbuf_iter += nb;
    left -= nb;
  }
  fclose(file);

  if ( require_text && !phitsmcpl_buf_is_text(bbuf_size, bbuf) ) {
    printf("Error: file %s does not appear to be a text file.\n",filename);
    free(bbuf);
    return 0;
  }
  *buf = bbuf;
  *lbuf = bbuf_size;
  return 1;
}

int phits2mcpl(const char * phitsfile, const char * mcplfile)
{
  return phits2mcpl2(phitsfile, mcplfile, 0, 1, 0, 0);
}

int phits2mcpl2( const char * phitsdumpfile, const char * mcplfile,
                 int opt_dp, int opt_gzip,
                 const char * inputdeckfile,
                 const char * dumpsummaryfile )
{
  phits_file_t f = phits_open_file(phitsdumpfile);
  mcpl_outfile_t mcplfh = mcpl_create_outfile(mcplfile);

  mcpl_hdr_set_srcname(mcplfh,"PHITS");

  mcpl_hdr_add_comment(mcplfh,"Converted from PHITS with phits2mcpl (from MCPL release v" MCPL_VERSION_STR ")");

  if (opt_dp)
    mcpl_enable_doubleprec(mcplfh);

  if (phits_has_polarisation(f))
    mcpl_enable_polarisation(mcplfh);

  if (inputdeckfile) {
    unsigned char* cfgfile_buf;
    size_t cfgfile_lbuf;
    if (!phitsmcpl_file2buf(inputdeckfile, &cfgfile_buf, &cfgfile_lbuf, 104857600, 1))
      return 0;
    //We won't do much for sanity checks since we want to avoid the risk of
    //false positives, but at least the word "dump" should occur in both input
    //deck and dump summary files:
    if (!strstr((const char*)cfgfile_buf, "dump")) {
      printf("Error: specified configuration file %s looks invalid as it does not contain the word \"dump\".\n",inputdeckfile);
      return 0;
    }
    mcpl_hdr_add_data(mcplfh, "phits_input_deck", (uint32_t)cfgfile_lbuf,(const char *)cfgfile_buf);
    free(cfgfile_buf);
  }
  if (dumpsummaryfile) {
    unsigned char* summaryfile_buf;
    size_t summaryfile_lbuf;
    if (!phitsmcpl_file2buf(dumpsummaryfile, &summaryfile_buf, &summaryfile_lbuf, 104857600, 1))
      return 0;
    //Same check as for the input deck above:
    if (!strstr((const char*)summaryfile_buf, "dump")) {
      printf("Error: specified dump summary file %s looks invalid as it does not contain the word \"dump\".\n",dumpsummaryfile);
      return 0;
    }
    mcpl_hdr_add_data(mcplfh, "phits_dump_summary_file", (uint32_t)summaryfile_lbuf,(const char *)summaryfile_buf);
    free(summaryfile_buf);
  }

  mcpl_particle_t* mcpl_particle = mcpl_get_empty_particle(mcplfh);

  const phits_particle_t * p;
  while ((p=phits_load_particle(f))) {
    if (!p->pdgcode) {
      printf("Warning: ignored particle with no PDG code set (raw phits kt code was %li).\n",p->rawtype);
      continue;
    }
    mcpl_particle->pdgcode = p->pdgcode;
    mcpl_particle->position[0] = p->x;//already in cm
    mcpl_particle->position[1] = p->y;//already in cm
    mcpl_particle->position[2] = p->z;//already in cm
    mcpl_particle->direction[0] = p->dirx;
    mcpl_particle->direction[1] = p->diry;
    mcpl_particle->direction[2] = p->dirz;
    mcpl_particle->polarisation[0] = p->polx;
    mcpl_particle->polarisation[1] = p->poly;
    mcpl_particle->polarisation[2] = p->polz;
    mcpl_particle->time = p->time * 1.0e6;//nanoseconds (PHITS) to milliseconds (MCPL)
    mcpl_particle->weight = p->weight;
    mcpl_particle->ekin = p->ekin;//already in MeV
    mcpl_add_particle(mcplfh,mcpl_particle);
  }

  const char * tmp = mcpl_outfile_filename(mcplfh);
  size_t laf = strlen(tmp);
  char * actual_filename = malloc(laf+1);
  actual_filename[0]='\0';
  strcat(actual_filename,tmp);

  int did_gzip = 0;
  if (opt_gzip)
    did_gzip = mcpl_closeandgzip_outfile(mcplfh);
  else
    mcpl_close_outfile(mcplfh);
  phits_close_file(f);

  printf("Created %s%s\n",actual_filename,(did_gzip?".gz":""));
  free(actual_filename);
  return 1;
}

void phits2mcpl_parse_args( int argc,char **argv, const char** infile,
                            const char **outfile,  const char **cfgfile,
                            const char **dumpsummaryfile,
                            int* double_prec, int* do_gzip ) {
  *cfgfile = 0;
  *dumpsummaryfile = 0;
  *infile = 0;
  *outfile = 0;
  *double_prec = 0;
  *do_gzip = 1;
  int i;
  for (i=1; i < argc; ++i) {
    if (argv[i][0]=='\0')
      continue;
    if (strcmp(argv[i],"-h")==0||strcmp(argv[i],"--help")==0) {
      const char * progname = strrchr(argv[0], '/');
      progname = progname ? progname + 1 : argv[0];
      printf("Usage:\n\n");
      printf("  %s [options] dumpfile [output.mcpl]\n\n",progname);
      printf("Converts the Monte Carlo particles in the input dump file (binary PHITS dump\n"
             "file format in suitable configuration) to MCPL format and stores in the\n"
             "designated output file (defaults to \"output.mcpl\").\n"
             "\n"
             "Options:\n"
             "\n"
             "  -h, --help   : Show this usage information.\n"
             "  -d, --double : Enable double-precision storage of floating point values.\n"
             "  -n, --nogzip : Do not attempt to gzip output file.\n"
             "  -c FILE      : Embed entire configuration FILE (the input deck)\n"
             "                 used to produce dumpfile in the MCPL header.\n"
             "  -s FILE      : Embed into the MCPL header the dump summary text file,\n"
             "                 which was produced along with the dumpfile itself.\n"
             );
      exit(0);
    }
    if (strcmp(argv[i],"-c")==0) {
      if (i+1==argc||argv[i+1][0]=='-') {
        printf("Error: Missing argument for -c\n");
        exit(1);
      }
      ++i;
      if (*cfgfile) {
        printf("Error: -c specified more than once\n");
        exit(1);
      }
      *cfgfile = argv[i];
      continue;
    }
    if (strcmp(argv[i],"-s")==0) {
      if (i+1==argc||argv[i+1][0]=='-') {
        printf("Error: Missing argument for -s\n");
        exit(1);
      }
      ++i;
      if (*dumpsummaryfile) {
        printf("Error: -s specified more than once\n");
        exit(1);
      }
      *dumpsummaryfile = argv[i];
      continue;
    }

    if (strcmp(argv[i],"-d")==0||strcmp(argv[i],"--double")==0) {
      *double_prec = 1;
      continue;
    }
    if (strcmp(argv[i],"-n")==0||strcmp(argv[i],"--nogzip")==0) {
      *do_gzip = 0;
      continue;
    }
    if (argv[i][0]=='-') {
      printf("Error: Unknown argument: %s\n",argv[i]);
      exit(1);
    }
    if (!*infile) {
      *infile = argv[i];
      continue;
    }
    if (!*outfile) {
      *outfile = argv[i];
      continue;
    }
    printf("Error: Too many arguments! (run with -h or --help for usage instructions)\n");
    exit(1);
  }
  if (!*infile) {
    printf("Error: Too few arguments! (run with -h or --help for usage instructions)\n");
    exit(1);
  }
  if (!*outfile)
    *outfile = "output.mcpl";
  if (strcmp(*infile,*outfile)==0) {
    //basic test, easy to cheat:
    printf("Error: input and output files are identical.\n");
    exit(1);
  }
}

int phits2mcpl_app(int argc,char** argv)
{
  const char * infile;
  const char * outfile;
  const char * cfgfile;
  const char * dumphdrfile;
  int double_prec, do_gzip;
  phits2mcpl_parse_args(argc,argv,&infile,&outfile,&cfgfile,&dumphdrfile,&double_prec,&do_gzip);
  int ok = phits2mcpl2(infile, outfile,double_prec, do_gzip,cfgfile,dumphdrfile);
  return ok ? 0 : 1;
}

void phits_writerecord(FILE* outfile, int reclen, size_t lbuf, char* buf)
{
  if (reclen==4) {
    uint32_t rl = lbuf;
    size_t nb = fwrite(&rl, 1, sizeof(rl), outfile);
    if (nb!=sizeof(rl))
      phits_error("write error");
    nb = fwrite(buf, 1, lbuf, outfile);
    if (nb!=lbuf)
      phits_error("write error");
    nb = fwrite(&rl, 1, sizeof(rl), outfile);
    if (nb!=sizeof(rl))
      phits_error("write error");
  } else {
    assert(reclen==8);
    uint64_t rl = lbuf;
    size_t nb = fwrite(&rl, 1, sizeof(rl), outfile);
    if (nb!=sizeof(rl))
      phits_error("write error");
    nb = fwrite(buf, 1, lbuf, outfile);
    if (nb!=lbuf)
      phits_error("write error");
    nb = fwrite(&rl, 1, sizeof(rl), outfile);
    if (nb!=sizeof(rl))
      phits_error("write error");
  }
}

int mcpl2phits( const char * inmcplfile, const char * outphitsdumpfile,
                int use_polarisation, long nparticles_limit, int reclen )
{
  if ( reclen != 4 && reclen != 8 )
    phits_error("Reclen parameter should be 4 (32bit Fortran record markers, recommended) or 8 (64bit Fortran record markers)");

  mcpl_file_t fmcpl = mcpl_open_file(inmcplfile);

  printf( "Opened MCPL file produced with \"%s\" (contains %llu particles)\n",
          mcpl_hdr_srcname(fmcpl),
          (unsigned long long)mcpl_hdr_nparticles(fmcpl) );

  printf("Creating (or overwriting) output PHITS file.\n");

  //Open new phits file:
  FILE * fout = fopen(outphitsdumpfile,"wb");

  if (!fout)
    phits_error("Problems opening new PHITS file");

  const mcpl_particle_t* mcpl_p;

  long long used = 0;
  long long skipped_nophitstype = 0;

  printf("Initiating particle conversion loop.\n");

  double dumpdata[13] = {0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.};//explicit since gcc 4.1-4.6 might warn on ={0}; syntax

  while ( ( mcpl_p = mcpl_read(fmcpl) ) ) {
    int32_t rawtype =  conv_code_pdg2phits( mcpl_p->pdgcode );
    if (!rawtype) {
      ++skipped_nophitstype;
      if (skipped_nophitstype<=100) {
        printf("WARNING: Found PDG code (%li) in the MCPL file which can not be converted to a PHITS particle code\n",
               (long)mcpl_p->pdgcode);
        if (skipped_nophitstype==100)
          printf("WARNING: Suppressing future warnings regarding non-convertible PDG codes.\n");
      }
      continue;
    }

    assert(rawtype!=0);

    dumpdata[0] = rawtype;
    dumpdata[1] = mcpl_p->position[0];//Already in cm
    dumpdata[2] = mcpl_p->position[1];//Already in cm
    dumpdata[3] = mcpl_p->position[2];//Already in cm
    dumpdata[4] = mcpl_p->direction[0];
    dumpdata[5] = mcpl_p->direction[1];
    dumpdata[6] = mcpl_p->direction[2];
    dumpdata[7] = mcpl_p->ekin;//Already in MeV
    dumpdata[8] = mcpl_p->weight;
    dumpdata[9] = mcpl_p->time * 1.0e-6;//ms->ns
    dumpdata[10] = mcpl_p->polarisation[0];
    dumpdata[11] = mcpl_p->polarisation[1];
    dumpdata[12] = mcpl_p->polarisation[2];

    if (used==INT32_MAX) {
      printf("WARNING: Writing more than 2147483647 (maximum value of 32 bit integers) particles in the PHITS dump "
             "file - it is not known whether PHITS will be able to deal with such files correctly.\n");
    }
    phits_writerecord(fout,reclen,sizeof(double)*(use_polarisation?13:10),(char*)&dumpdata[0]);

    if (++used==nparticles_limit) {
      long long remaining = mcpl_hdr_nparticles(fmcpl) - skipped_nophitstype - used;
      if (remaining)
        printf("Output limit of %li particles reached. Ignoring remaining %lli particles in the MCPL file.\n",
               nparticles_limit,remaining);
      break;
    }


  }

  printf("Ending particle conversion loop.\n");

  if (skipped_nophitstype) {
    printf("WARNING: Ignored %lli particles in the input MCPL file since their PDG codes"
           " could not be converted to PHITS codes.\n",(long long)skipped_nophitstype);
  }
  mcpl_close_file(fmcpl);
  fclose(fout);

  printf("Created %s with %lli particles.\n",outphitsdumpfile,(long long)used);

  return 1;


}


int mcpl2phits_app_usage( const char** argv, const char * errmsg ) {
  if (errmsg) {
    printf("ERROR: %s\n\n",errmsg);
    printf("Run with -h or --help for usage information\n");
    return 1;
  }
  const char * progname = strrchr(argv[0], '/');
  progname =  progname ? progname + 1 : argv[0];
  printf("Usage:\n\n");
  printf("  %s [options] <input.mcpl> [phits.dmp]\n\n",progname);
  printf("Converts the Monte Carlo particles in the input MCPL file to binary PHITS\n"
         "dump file format and stores the result in the designated output file\n"
         "(defaults to \"phitsdata_dmp\"). The file can be read in PHITS using\n"
         "a configuration of (assuming the filename is \"phits.dmp\"):\n"
         "     dump = 13\n"
         "     1 2 3 4 5 6 7 8 9 10 14 15 16\n"
         "     file = phits.dmp\n"
         "\n"
         "Options:\n"
         "\n"
         "  -h, --help   : Show this usage information.\n"
         "  -n, --nopol  : Do not write polarisation info (saving ~22%% in file size). The\n"
         "                 PHITS configuration reading the file must then be (assuming the\n"
         "                 filename is \"phits.dmp\"):\n"
         "                                            dump = 10\n"
         "                                            1 2 3 4 5 6 7 8 9 10\n"
         "                                            file = phits.dmp\n"
         "  -f           : Write Fortran records with 64 bit integer markers. Note that\n"
         "                 the default (32 bit) is almost always the correct choice.\n"
         "  -l<LIMIT>    : Limit the number of particles transferred to the PHITS file\n"
         "                 (defaults to 0, meaning no limit).\n"
         );
  return 0;
}

int mcpl2phits_parse_args( int argc,const char **argv, const char** inmcplfile,
                           const char **outphitsfile, long* nparticles_limit,
                           int* use64bitreclen, int* nopolarisation ) {
  //returns: 0 all ok, 1: error, -1: all ok but do nothing (-h/--help mode)
  *inmcplfile = 0;
  *outphitsfile = 0;
  *nparticles_limit = INT32_MAX;
  *use64bitreclen = 0;
  *nopolarisation = 0;

  int64_t opt_num_limit = -1;
  int i;
  for (i = 1; i<argc; ++i) {
    const char * a = argv[i];
    size_t n = strlen(a);
    if (!n)
      continue;
    if (n>=2&&a[0]=='-'&&a[1]!='-') {
      //short options:
      int64_t * consume_digit = 0;
      size_t j;
      for (j=1; j<n; ++j) {
        if (consume_digit) {
          if (a[j]<'0'||a[j]>'9')
            return mcpl2phits_app_usage(argv,"Bad option: expected number");
          *consume_digit *= 10;
          *consume_digit += a[j] - '0';
          continue;
        }
        switch(a[j]) {
        case 'h': mcpl2phits_app_usage(argv,0); return -1;
        case 'l': consume_digit = &opt_num_limit; break;
        case 'f': *use64bitreclen = 1; break;
        case 'n': *nopolarisation = 1; break;
        default:
          return mcpl2phits_app_usage(argv,"Unrecognised option");
        }
        if (consume_digit) {
          *consume_digit = 0;
          if (j+1==n)
            return mcpl2phits_app_usage(argv,"Bad option: missing number");
        }
      }

    } else if (n==6 && strcmp(a,"--help")==0) {
      mcpl2phits_app_usage(argv,0);
      return -1;
    } else if (n>=1&&a[0]!='-') {
      if (*outphitsfile)
        return mcpl2phits_app_usage(argv,"Too many arguments.");
      else if (*inmcplfile) *outphitsfile = a;
      else *inmcplfile = a;
    } else {
      return mcpl2phits_app_usage(argv,"Bad arguments");
    }
  }

  if (!*inmcplfile)
    return mcpl2phits_app_usage(argv,"Missing argument : input MCPL file");
  if (!*outphitsfile)
    *outphitsfile = "phits.dmp";

  if (opt_num_limit<=0)
    opt_num_limit = 0;

  //NB: For now we allow unlimited number of particles in the file - but let the
  //mcpl2phits method emit a WARNING if exceeding INT32_MAX particles.
  *nparticles_limit = opt_num_limit;

  return 0;
}

int mcpl2phits_app( int argc, char** argv ) {

  const char * inmcplfile;
  const char * outphitsfile;
  long nparticles_limit;
  int use64bitreclen, nopolarisation;
  int parse = mcpl2phits_parse_args( argc, (const char**)argv, &inmcplfile, &outphitsfile,
                                     &nparticles_limit, &use64bitreclen, &nopolarisation);
  if (parse==-1)// --help
    return 0;

  if (parse)// parse error
    return parse;

  int reclen = (use64bitreclen?8:4);

  if (mcpl2phits(inmcplfile, outphitsfile, (nopolarisation?0:1), nparticles_limit, reclen))
    return 0;

  return 1;
}
