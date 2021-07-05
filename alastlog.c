#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <lastlog.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "lllib.h"

char * check_string(char *, int);
int check_time(struct lastlog *, long);
struct passwd *extract_user(char *);
void fatal(char, char *);
int get_log(char *, struct passwd *, long);
void get_option(char, char **, struct passwd **, long *, char **);
long parse_time(char *);
void print_headers();
int show_info(struct lastlog *, struct passwd *, long, int);
void show_time(struct lastlog *, char *);

#define LLOG_FILE		"/var/log/lastlog"
#define TIME_FORMAT		"%a %b %e %H:%M:%S %z %Y"
#define TIMESIZE		32
#define SECONDS_IN_DAY	86400
#define NO 				0
#define YES 			1

/*
 * main()
 * Method: Process command-line arguments, if any, and then call get_log()
 *		   to open the lastlog file (LLOG_FILE by default), with corresponding
 * 		   args, NULL if not specified.
 * Return: 0 on success, -1 on close() error, exits 1 and prints message to
 *		   stderr on other failures (see corresponding functions).
 *   Note: The while loop will cycle through options, any/all of -u, -t, or -f.
 *		   If it is not a valid option, fatal() is called and program exits.
 *		   get_option is only called when there is at least one more arg left
 *		   in addition to the '-' option, the (i+1) < ac part in the if case.
 */
int main (int ac, char *av[])
{
	int i = 1;
	int rv = 0;

	//initialize variables to default values, changes with user options
	struct passwd *user = NULL;
	long days = -1;
	char *file = NULL;

	//see Note section above for more on option processing
	while (i < ac)
	{
		if(av[i][0] == '-' && (i + 1) < ac)
			get_option(av[i][1], &av[i + 1], &user, &days, &file);
		else
			fatal('\0', av[i]);

		i += 2;				//go past the -X option, and its value
	}

	//If no file specified with -f, use LLOG_FILE
	if (file == NULL)
		rv = get_log(LLOG_FILE, user, days);
	else
		rv = get_log(file, user, days);

	return rv;
}

/*
 *	check_string()
 *	Purpose: see if the string is null terminated
 *	  Input: a string and the size of the string
 *	 Return: an empty string, if NULL; a null-terminated string
 *			 otherwise (could already be null-terminated when passed in).
 */
char * check_string(char *str, int size)
{
	if (str == NULL)				//if NULL, don't try to access element
		return "";
	else if (str[size - 1] != '\0')
		str[size - 1] = '\0';

	return str;
}

/*
 *	check_time()
 *	Purpose: see if the lastlogin time has occurred within the last days
 *	  Input: entry, the lastlogin time
 *			 days, specified by the user with the -t option
 *	 Return: NO, if login happened later than "days" ago
 *			 YES, if login has happened within the given # of "days"
 */
int check_time(struct lastlog *lp, long days)
{
	if (days != -1)									//is a time given with -t
	{
		time_t now;
		time_t login = (lp) ? lp->ll_time : 0;		//get ll_time, or 0 if null

		double delta = difftime(time(&now), login);	//secs b/w now and login

		if ( delta > (SECONDS_IN_DAY * days) )		//delta is more than range
			return NO;								//login is earlier than -t
	}

	return YES;
}

/*
 *	extract_user()
 *	Purpose: obtain a passwd struct for a given username/UID
 *	  Input: name, the name/UID that was specified following -u
 *	 Return: a pointer to the passwd struct for the given name/UID.
 *	 Errors: If getpwnam() fails, the function tries to parse the
 *			 name into a UID. If it is determined to not be a number,
 *			 an invalid message is output to stderr. If successful,
 *			 but getpwuid() fails, then the "name" specified is
 *			 unknown, and we exit.
 */
struct passwd *extract_user(char *name)
{
	struct passwd *user = NULL;

	if ( name == NULL)								//no name given, NULL
		return user;
	else if ( (user = getpwnam(name)) != NULL)		//name was a username
		return user;
	else											//try name as a UID
	{
		char *temp = NULL;
		long uid = strtol(name, &temp, 10);

		//If strtol returns 0 and copied all chars to temp, it failed
		if (uid == 0 && strcmp(name, temp) == 0)
		{
			fprintf(stderr, "alastlog: invalid user input: %s\n", temp);
			exit (1);
		}

		//We were able to parse out a UID, try getting user with that
		if ( (user = getpwuid(uid)) == NULL)
		{
			fprintf(stderr, "alastlog: Unknown user: %s\n", name);
			exit(1);
		}
	}

	return user;
}

/*
 *	fatal()
 *	Purpose: helper function to output error messages for bad option input.
 */
void fatal(char opt, char *arg)
{
	if(opt == '\0')
		fprintf(stderr, "alastlog: unexpected argument: %s\n", arg);
	else
		fprintf(stderr, "alastlog: invalid option -- '%c'\n", opt);

	fprintf(stderr, "Usage: alastlog [options]\n\nOptions:\n");
	fprintf(stderr, "\t-u LOGIN\tprint lastlog record for user LOGIN\n");
	fprintf(stderr, "\t-t DAYS\t\tprint only records more recent than DAYS\n");
	fprintf(stderr, "\t-f FILE\t\tread data from specified FILE\n\n");

	exit(1);
}

/*
 *	get_log()
 *	Purpose: Print out lastlog records, filtered as appropriate by user options
 *	  Input: file, lastlog to read from (LLOG_FILE by default)
 *			 user, specific username/UID to display record for
 *			 days, restrict output to logins within given number of days
 *	 Output: formatted headers and entries, through calling show_info
 *	 Errors: If there was a problem opening the lastlog file (ll_open) or
 *			 a problem extracting a provided user (extract_user), the program
 *			 will print a message to stderr and exit.
 */
int get_log(char *file, struct passwd *user, long days)
{
	if (ll_open(file) == -1)					//open lastlog file
	{
		perror(file);
		exit(1);
	}

	struct passwd *entry = user;				//store passwd record
	struct lastlog *ll;							//store lastlog record
	int headers = NO;							//have headers been printed

	if(entry == NULL)							//if -u user was not specified
		entry = getpwent();						//open passwd db to iterate

	while (entry)								//still have a passwd entry
	{
		if ( ll_seek(entry->pw_uid) == -1 )		//get the correct pos in buffer
			ll = NULL;							//error
		else
			ll = ll_read();						//okay to read

		headers = show_info(ll, entry, days, headers);

		if( user != NULL)						//a user specified with -u
			break;								//found them, so break
		else
			entry = getpwent();					//go until end of passwd db
	}

	if(user == NULL)							//if user not specified
		endpwent();								//close link to passwd database

	return ll_close();							//close lastlog file, -1 if err
}

/*
 *	get_option()
 *	Purpose: process command line options
 *	  Input: opt, the char following the '-' flag
 *			 value, the argument following the [-utf] flag
 *			 user, pointer to store specified user
 *			 days, pointer to store specified time restriction
 *			 file, pointer to store specified file
 *	 Return: None. This function passes through pointers from main
 *			 to store the variables.
 *	 Errors: For the -u and -t options, parsing functions are called to
 *			 determine if the input is valid. extract_user() obtains the
 *			 passwd entry, or exits if not found/invalid. parse_time()
 *			 changes the text input into a number, or exits if not valid.
 *	  Notes: If there is an invalid option (not -utf), fatal is called
 *			 to output a message to stderr and exit with a non-zero status.
 *			 See also, errors above for invalid input.
 */
void
get_option(char opt, char **val, struct passwd **user, long *days, char **file)
{
	if(opt == 'u')
		*user = extract_user(*val);		//check if valid user/if they exist
	else if (opt == 't')
		*days = parse_time(*val);		//check if valid time, exit if not
	else if (opt == 'f')
		*file = *val;					//ll_open will determine later if valid
	else
		fatal(opt, "");					//unrecognized option, exit with error

	return;
}

/*
 *	parse_time()
 *	Purpose: translate a DAY value into a corresponding time value
 *	  Input: value, the -t option the user entered
 *	 Return: the day value converted into a long
 *	 Errors: if the value is not a number, strtol will fail and the
 *			 program should exit
 */
long parse_time(char *value)
{
	char *temp = NULL;
	long time = strtol(value, &temp, 10);

	if(time == 0 && strcmp(value, temp) == 0)
	{
		fprintf(stderr, "alastlog: invalid numeric argument '%s'\n", value);
		exit(1);
	}

	return time;
}

/*
 *	print_headers() - output formatted lastlog headers
 */
void print_headers()
{
	printf("%-16.16s ", "Username");
	printf("%-8.8s ", "Port");
	printf("%-16.16s ", "From");
	printf("%s", "Latest");
	printf("\n");

	return;
}

/*
 *	show_info()
 *	Purpose: display information in lastlog record, with potential time filter
 *	  Input: lp, pointer to the lastlog record
 *			 ep, pointer to the user's passwd entry
 *			 days, entered with -t option (-1 if none); used to filter results
 *			 headers, used to determine if we should print headers
 *	 Output: fixed-width formatted columns for username, line, host, and time
 *	 Return: YES, if an entry was printed to output
 *			 headers, the current state of headers (either YES or NO). If a -t
 *			 	option is specified, show_info may be called multiple times
 *			 	without displaying output. If no users match the -t
 *				restriction, no users are displayed and neither should
 *				headers.
 *	   Note: For cases where the *lp is NULL, blank strings are printed as
 *			 trying to access the members of the struct (i.e. lp->ll_line)
 *			 would cause a seg fault.
 */
int show_info(struct lastlog *lp, struct passwd *ep, long days, int headers)
{
	//filter based on user-provided time in days, don't print if outside range
	if (check_time(lp, days) == NO)
		return headers;

	//check if we have already printed headers
	if (headers == NO)
		print_headers();

	printf("%-16.16s ", ep->pw_name);		//print username

	if (lp)									//if exists, print lastlog info
	{
		printf("%-8.8s ", check_string(lp->ll_line, UT_LINESIZE));
		printf("%-16.16s ", check_string(lp->ll_host, UT_HOSTSIZE));
	}
	else									//would seg-fault, print blanks
	{
		printf("%-8.8s ", "");
		printf("%-16.16s ", "");
	}

	show_time(lp, TIME_FORMAT);				//display time, or never logged in
	printf("\n");							//end of record, add newline

	return YES;
}

/*
 *	show_time()
 *	Purpose: format a time and print it
 *	  Input: lp, pointer to the lastlog record
 *			 fmt, the format to print the time in
 *	 Output: If the lastlog record is null, or the time is 0, the user
 *			 does not exist or has never logged on, so display that. Otherwise,
 *			 get the time, lp->ll_time, and format it according to "fmt".
 *	  Notes: copied, with slight modifications, from the who2.c code from
 *			 lecture #2.
 */
void show_time(struct lastlog *lp, char *fmt)
{
	if (lp == NULL || lp->ll_time == 0)		//user doesn't exist/no login
	{
		printf("**Never logged in**");
	}
	else									//parse the time into TIME_FORMAT
	{
		char result[TIMESIZE];
		time_t time = lp->ll_time;
		strftime(result, TIMESIZE, fmt, localtime(&time));

		printf("%s", result);
	}

	return;
}
