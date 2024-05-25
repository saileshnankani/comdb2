#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <strings.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <mem.h>
#include <cdb2api.h>
#include <list.h>

#include "stepper_client.h"

#define MAX_LINE 65536
#define MAX_NODES 128

static int debug = 0;

static int usage( char *prg);
static int parse_line( char *line, char **query);

/* two minute max */
#define MAXTIME (60*2)

void timed_out(int sig, siginfo_t *info, void *uap)
{
    char buf[32] = "Timed out\n";
    int lrc = write(STDERR_FILENO, buf, 10);
    if (lrc == -1) {
        printf("ERROR: %s write returns -1\n", __func__);
    }
    exit(1);
}

struct dummy_cdb2buf {
    int fd;
     /* Don't care about the rest. */
};

struct dummy_cdb2_hndl {
     char dbname[64];
     char cluster[64];
     char type[64];
     char hosts[MAX_NODES][64];
     int  ports[MAX_NODES];
     int  hosts_connected[MAX_NODES];
     struct dummy_cdb2buf   *sb;
     /* Don't care about the rest. */
};

void disconnect_cdb2h(cdb2_hndl_tp * cdb2h) {
    struct dummy_cdb2_hndl *dummy_hndl;
    dummy_hndl = (struct dummy_cdb2_hndl *)cdb2h;
   //  if (dummy_hndl->sb)
   //      shutdown(dummy_hndl->sb->fd, 2);
}

/* storing clients (i.e. comm pipes) */
struct client
{
   int   id;
   cdb2_hndl_tp *db;

   LINKC_T(struct client) lnk;
};

int main( int argc, char **argv)
{
   char  *infile = NULL;
   char  *outfile = NULL;
   char  *dbname = NULL;
   char  *stage;
   FILE  *file = NULL;
   FILE  *out = NULL;
   char  line[MAX_LINE];
   int   rc = 0;
   int   lineno = 0;

   comdb2ma_init(0, 0);

   if (argc!=3 && argc !=4)
      usage( argv[0]);

   dbname = argv[1];
   stage = "default";
   infile = argv[2];

   if (getenv("DEBUG")) {
       debug = 1;
   }
   outfile = (argc == 4)?argv[3]:NULL;

   file = fopen( infile, "r");
   if (!file)
   {
      fprintf( stderr, "Open %s error %d %s\n",
            infile, errno, strerror(errno));
      exit(1);
   }

   if (outfile)
   {
      out = fopen( outfile, "w");
      if (!file)
      {
         fprintf( stderr, "Open %s error %d %s\n",
               infile, errno, strerror(errno));
         fclose(file);
         exit(1);
      }
   }
   else
      out = stdout;

   rc = clnt_repo_init(debug);
   if (rc)
   {
      fprintf( stderr, "Error open repo rc=%d\n", rc);
      fclose (file);
      exit(1);
   }

   if( ! isatty(fileno(file) ) )
   {
      struct sigaction act = {{0}};
      act.sa_sigaction = timed_out;
      act.sa_flags = (SA_SIGINFO | SA_RESETHAND);
      sigfillset( &act.sa_mask );

      if( sigaction(SIGALRM, &act, NULL) != 0)
      {
         fprintf(stderr,"Failed to setup sigalrm handler: %s\n", 
                strerror(errno));
         exit(1);
      }

      alarm(MAXTIME);
   }

   while (fgets( line, sizeof(line), file))
   {
      client_t *clnt = NULL;
      char *query = NULL;
      int   id = 0;
    
      lineno++;

      if(line[0] == '\n' || line[0] == '#')
         continue;

      if (strcmp(line, "bounce_connection\n") == 0) {
         client_t *clnt;
         LISTC_FOR_EACH( &clients, clnt, lnk)
         {
            disconnect_cdb2h(clnt->db);
         }
         continue;
      }

      id = parse_line( line, &query);
      if (id<0)
      {
         fprintf( stderr, "Syntax error line %d, expect [NUM query] \"%s\"\n",
               lineno, line);
         break;
      }
      assert( query);
  
      clnt = clnt_get(id);
      if (!clnt)
      {
         clnt = clnt_open(dbname, stage, id);
      }
      if(!clnt)
      {
         fprintf( stderr, "Error open client id=%d\n", id);
         rc = -1;
         break;
      } 

      if (strcmp(query, "DoNe") == 0) {
         clnt_close(clnt);  
         continue;
      }


      if (debug)
         fprintf( out, "%d [%s]\n", id, query);
      rc = clnt_run_query(clnt, query, out);
      if (rc)
      {
         fprintf( stderr, "Error with query to clnt id=%d, %s",
               id, query);
         break;
      }
   }

   if (debug)
   {
      if (rc)
      {
         printf( "TEST FAILED\n");
      }
      else
      {
         printf( "TEST OK\n");
      }
   }


   fclose( file);
   fclose( out);

   /* this will also close opened clients */
   if (clnt_repo_destroy())
   {
      fprintf( stderr, "Error close repo\n");
      exit(1);
   }

   exit((rc)?1:0);
}

static int usage( char *prg)
{
   fprintf( stderr, "Usage %s dbname test_script [outfile]\n",
         prg);
   exit(1);
}

static int parse_line( char *line, char **query)
{
   // TODO: This expects a line to be of the form "<client id> <sql query>".
   // Extend this function so that it also understands "bounce_connection".
   int   id = 0;
   char  *sp = line;

   if (!sp)
      return -1;

   /* jump spaces */
   while( *sp && *sp == ' ') sp++;
   if (!*sp)
      return -1;

   char *s = strchr(line, '\n');
   if (s) *s = 0;

   /* get clnt id */
   id = atoi(sp);
   if (id<0)
      return -1;

   /* get sql query */
   sp = strchr(line, ' ');
   if (!sp)
      return -1;
   while (*sp && *sp == ' ') sp++;
   if (!*sp)
      return -1;

   *query = sp;
   return id;
}

// TODO: not sure why this is only undefined on MacOS
#ifdef _DARWIN_C_SOURCE
int gbl_ssl_allow_localhost = 0;
#endif
