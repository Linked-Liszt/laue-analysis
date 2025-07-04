/*
 *  checkFileType.c
 *  pixels2qs
 *
 *  Created by Jon Tischler on 5/2/09.
 *  Copyright 2009 Oak Ridge National Laboratory. All rights reserved.
 *
 */

#include <stdio.h> 
#include <string.h> 
#include "checkFileType.h"
#include "xmlUtility.h"

int	checkForXMLtags(char *buf, char *typeListIn);


int checkFileType(	/* return 1 for old style '$', reurn 2 for xml, return 0 if not OK */
char *filename,
char *typeLIstIn)	/* comma separated list of acceptable file types */
{
	FILE	*f;
	size_t	i;
	char	line[2048];
	
	if (strlen(filename)<1) return 0;
	if ( !(f=fopen(filename,"r")) ) return 0;
	i = fread(line, sizeof(char), 2046, f);
	fclose(f);
	if (i<1) return 0;

	if (checkFileTypeLine(line,typeLIstIn))		return 1;	/* is old style, return 1 */
	else if (checkForXMLtags(line,typeLIstIn))	return 2;	/* assume that listIn is a list of xml tags */
	return 0;
}



int	checkForXMLtags(
char *buf,									/* string to check for existance of tags */
char *tagListIn)							/* comma separated list of acceptable tags, each tag limited to 254 charaters */
{
	char	*t1,*t2;						/* pointers into tags tagListIn */
	char	*tfinal;						/* points to NULL after last character in tagListIn */
	char	tag[256];						/* local version of tag from the list */
	int		len;							/* actual length of each tag */
	
	if (!tagListIn || !buf) return 0;
	if (strlen(tagListIn)<1 || strlen(buf)<2) return 0;

	tfinal = tagListIn + strlen(tagListIn);	/* points to terminator of tagListIn */
	t1 = tagListIn;
	while (t1<tfinal) {
		t2 = strchr(t1,',');				/* find next ',', t2 points to character AFTER tag t1 */
		t2 = t2 ? t2 : tfinal;				/* if cannot find comma, then point to terminator */
		len = t2-t1;						/* length of tag (not including comma or terminator) */
		len = len>254 ? 254 : len;
		strncpy(tag,t1,len);				/* local copy of each tag */
		if (XMLtagExists(tag,buf)) return 1;/* found the xml tag */
		t1 = t2+1;							/* character after termainator or comma */
	}
	return 0;								/* failed to find a tag */
}


int checkFileTypeLine(						/* return TRUE if file is of type typeListIn */
char *lineIn,								/* starts with first line of file, this can even be the whole file, only first line is examined */
char *typeListIn)							/* comma separated list of acceptable file types */
{
	char	line[1024];						/* local copy of lineIn, this gets messed up by strtok() and other things */
	char	*p;								/* pointer into line */
	char	types[128][128];				/* an array of char strings one for each type in typeListIn, types are limited to a length of 128 */
	int		Nt=0;							/* number of types in typeListIn stored in types[][] */
	char	*t1,*t2;						/* pointers into types typeListIn */
	char	*tfinal;						/* points to last character in typeListIn */
	int		len;
	int		i;

	if (strlen(typeListIn)<1 || strlen(lineIn)<2) return 0;
	if (lineIn[0] != '$') return 0;			/* always have to start with a '$' */

	strncpy(line,lineIn,1022);				/* local copy of first line */
	line[1023] = '\0';

	/* fill array of types from typeListIn, at most 128 types, each one at most 127 long */
	t1 = typeListIn;
	tfinal = t1 + strlen(typeListIn)-1;
	for (Nt=0; Nt<128 && t1<tfinal; Nt++) {
		t2 = strchr(t1,',');				/* find next ',' */
		t2 = t2 ? t2-1 : tfinal;			/* if cannot find ',', then goto end */
		len = t2-t1+1;
		len = len>127 ? 127 : len;
		strncpy(types[Nt],t1,len);			/* local copy of each type */
		types[Nt][len] = '\0';
		t1 = t2+2;							/* start of next type, skip the comma */
	}

	p = strpbrk(line,"\r\n");				/* trim to end of first line */
	if (p) *p = '\0';
	p = strstr(line,"//");					/* trim off any comment */
	if(p) *p = '\0';
	for (p=strchr(line,','); p; p=strchr(line,',')) *p = ' ';	/* change all commas to ' ' */
	for (p=strchr(line,';'); p; p=strchr(line,';')) *p = ' ';	/* change all semicolons to ' ' */

	/* search for type in line that looks like "$filetype type" */
	p = strtok(line," \t");					/* the tag part (with '$') */
	if (!p) return 0;						/* no tag, failed */

	/* old style matching, something like $type.  This method is Deprecated */
	p++;
	for (i=0;i<Nt;i++) {
		if (!strcmp(types[i],p)) return 1;	/* found a match, using old style */
	}

	/* start the new style file type checking, first check that p =='filetype' */
	if (strcmp(p,"filetype")) return 0;		/* tag does not start with $filetype */

	/* check all the values for one that matches one of the types from typeListIn */
	while(p) {								/* loop over each value from line */
		for (i=0;i<Nt;i++) {
			if (!strcmp(types[i],p)) return 1;	/* found a match */
		}
		p = strtok(NULL," \t");				/* get next value from line to check */
	}

	return 0;								/* give up, no match */
}



