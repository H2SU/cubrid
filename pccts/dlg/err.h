/*
 * err.h
 *
 * Standard error handling mechanism
 *
 * $Revision: 1.3 $
 * SOFTWARE RIGHTS
 *
 * We reserve no LEGAL rights to the Purdue Compiler Construction Tool
 * Set (PCCTS) -- PCCTS is in the public domain.  An individual or
 * company may do whatever they wish with source code distributed with
 * PCCTS or the code generated by PCCTS, including the incorporation of
 * PCCTS, or its output, into commerical software.
 * 
 * We encourage users to develop software with PCCTS.  However, we do ask
 * that credit is given to us for developing PCCTS.  By "credit",
 * we mean that if you incorporate our source code into one of your
 * programs (commercial product, research project, or otherwise) that you
 * acknowledge this fact somewhere in the documentation, research report,
 * etc...  If you like PCCTS and have developed a nice tool with the
 * output, please mention that you developed it using PCCTS.  In
 * addition, we ask that this header remain intact in our source code.
 * As long as these guidelines are kept, we expect to continue enhancing
 * this system and expect to make other tools available as they are
 * completed.
 *
 * Has grown to hold all kinds of stuff (err.h is increasingly misnamed)
 *
 * Terence Parr: 1989-1993
 * Purdue University Electrical Engineering
 * ANTLR Version 1.10
 */

#ifndef ZZERR_H
#define ZZERR_H

#include <string.h>
#ifdef __STDC__
#include <stdarg.h>
#else
#include <varargs.h>
#endif

/* Define usable bits per unsigned int word (used for set stuff) */
#ifdef PC
#define WORDSIZE 16
#define LogWordSize	4
#else
#define	WORDSIZE 32
#define LogWordSize 5
#endif

#define	MODWORD(x) ((x) & (WORDSIZE-1))		/* x % WORDSIZE */
#define	DIVWORD(x) ((x) >> LogWordSize)		/* x / WORDSIZE */

/* This is not put into the global pccts_parser structure because it is
 * hidden and does not need to be saved during a "save state" operation
 */
/* maximum of 32 bits/unsigned int and must be 8 bits/byte */
static SetWordType bitmask[] = {
	0x00000001, 0x00000002, 0x00000004, 0x00000008,
	0x00000010, 0x00000020, 0x00000040, 0x00000080
};

void
#ifdef __STDC__
zzresynch(SetWordType *wd,SetWordType mask)
#else
zzresynch(wd,mask)
SetWordType *wd, mask;
#endif
{
	static int consumed = 1;

	/* if you enter here without having consumed a token from last resynch
	 * force a token consumption.
	 */
	if ( !consumed ) {zzCONSUME; return;}

	/* if current token is in resynch set, we've got what we wanted */
	if ( wd[LA(1)]&mask || LA(1) == zzEOF_TOKEN ) {consumed=0; return;}
	
	/* scan until we find something in the resynch set */
	while ( !(wd[LA(1)]&mask) && LA(1) != zzEOF_TOKEN ) {zzCONSUME;}
	consumed=1;
}

/* input looks like:
 *		zzFAIL(k, e1, e2, ...,&zzMissSet,&zzMissText,&zzBadTok,&zzBadText)
 * where the zzMiss stuff is set here to the token that did not match
 * (and which set wasn't it a member of).
 */
void
#ifdef __STDC__
zzFAIL(int k, ...)
#else
zzFAIL(va_alist)
va_dcl
#endif
{
#ifdef LL_K
	static char text[LL_K*ZZLEXBUFSIZE+1];
	SetWordType *f[LL_K];
#else
	static char text[ZZLEXBUFSIZE+1];
	SetWordType *f[1];
#endif
	SetWordType **miss_set;
	char **miss_text;
	unsigned *bad_tok;
	char **bad_text;
	unsigned *err_k;
	int i;
	va_list ap;
#ifndef __STDC__
	int k;
#endif
#ifdef __STDC__
	va_start(ap, k);
#else
	va_start(ap);
	k = va_arg(ap, int);	/* how many lookahead sets? */
#endif
	text[0] = '\0';
	for (i=1; i<=k; i++)	/* collect all lookahead sets */
	{
		f[i-1] = va_arg(ap, SetWordType *);
	}
	for (i=1; i<=k; i++)	/* look for offending token */
	{
		if ( i>1 ) strcat(text, " ");
		strcat(text, LATEXT(i));
		if ( !zzset_el(LA(i), f[i-1]) ) break;
	}
	miss_set = va_arg(ap, SetWordType **);
	miss_text = va_arg(ap, char **);
	bad_tok = va_arg(ap, unsigned *);
	bad_text = va_arg(ap, char **);
	err_k = va_arg(ap, unsigned *);
	if ( i>k )
	{
		/* bad; lookahead is permutation that cannot be matched,
		 * but, the ith token of lookahead is valid at the ith position
		 * (The old LL sub 1 (k) versus LL(k) parsing technique)
		 */
		*miss_set = NULL;
		*miss_text = zzlextext;
		*bad_tok = LA(1);
		*bad_text = LATEXT(1);
		*err_k = k;
		return;
	}
/*	fprintf(stderr, "%s not in %dth set\n", zztokens[LA(i)], i);*/
	*miss_set = f[i-1];
	*miss_text = text;
	*bad_tok = LA(i);
	*bad_text = LATEXT(i);
	if ( i==1 ) *err_k = 1;
	else *err_k = k;
}

void
#ifdef __STDC__
zzsave_antlr_state(zzantlr_state *buf)
#else
zzsave_antlr_state(buf)
zzantlr_state *buf;
#endif
{
#ifdef LL_K
	int i;
#endif

#ifdef ZZCAN_GUESS
	buf->guess_start = zzguess_start;
	buf->guessing = zzguessing;
#endif
	buf->asp = zzasp;
#ifdef ZZINF_LOOK
	buf->inf_labase = zzinf_labase;
	buf->inf_last = zzinf_last;
#endif
#ifdef DEMAND_LOOK
	buf->dirty = zzdirty;
#endif
#ifdef LL_K
	for (i=0; i<LL_K; i++) buf->tokenLA[i] = zztokenLA[i];
	for (i=0; i<LL_K; i++) strcpy(buf->textLA[i], zztextLA[i]);
	buf->lap = zzlap;
	buf->labase = zzlabase;
#else
	buf->token = zztoken;
#endif
}

void
#ifdef __STDC__
zzrestore_antlr_state(zzantlr_state *buf)
#else
zzrestore_antlr_state(buf)
zzantlr_state *buf;
#endif
{
#ifdef LL_K
	int i;
#endif

#ifdef ZZCAN_GUESS
	zzguess_start = buf->guess_start;
	zzguessing = buf->guessing;
#endif
	zzasp = buf->asp;
#ifdef ZZINF_LOOK
	zzinf_labase = buf->inf_labase;
	zzinf_last = buf->inf_last;
#endif
#ifdef DEMAND_LOOK
	zzdirty = buf->dirty;
#endif
#ifdef LL_K
	for (i=0; i<LL_K; i++) zztokenLA[i] = buf->tokenLA[i];
	for (i=0; i<LL_K; i++) strcpy(zztextLA[i], buf->textLA[i]);
	zzlap = buf->lap;
	zzlabase = buf->labase;
#else
	zztoken = buf->token;
#endif
}

#ifndef USER_ZZSYN
/* standard error reporting function */
void
#ifdef __STDC__
zzsyn(char *text, unsigned tok, char *egroup, SetWordType *eset, unsigned etok, int k, char *bad_text)
#else
zzsyn(text, tok, egroup, eset, etok, k, bad_text)
char *text, *egroup, *bad_text;
unsigned tok;
unsigned etok;
int k;
SetWordType *eset;
#endif
{
	
	fprintf(stderr, "line %d: syntax error at \"%s\"", zzline, (tok==zzEOF_TOKEN)?"EOF":text);
	if ( !etok && !eset ) {fprintf(stderr, "\n"); return;}
	if ( k==1 ) fprintf(stderr, " missing");
	else
	{
		fprintf(stderr, "; \"%s\" not", bad_text);
		if ( zzset_deg(eset)>1 ) fprintf(stderr, " in");
	}
	if ( zzset_deg(eset)>0 ) zzedecode(eset);
	else fprintf(stderr, " %s", zztokens[etok]);
	if ( strlen(egroup) > 0 ) fprintf(stderr, " in %s", egroup);
	fprintf(stderr, "\n");
}
#endif

/* is b an element of set p? */
int
#ifdef __STDC__
zzset_el(unsigned b, SetWordType *p)
#else
zzset_el(b,p)
unsigned b;
SetWordType *p;
#endif
{
	return( p[DIVWORD(b)] & bitmask[MODWORD(b)] );
}

int
#ifdef __STDC__
zzset_deg(SetWordType *a)
#else
zzset_deg(a)
SetWordType *a;
#endif
{
	/* Fast compute degree of a set... the number
	   of elements present in the set.  Assumes
	   that all word bits are used in the set
	*/
	register SetWordType *p = a;
	register SetWordType *endp = &(a[zzSET_SIZE]);
	register int degree = 0;

	if ( a == NULL ) return 0;
	while ( p < endp )
	{
		register SetWordType t = *p;
		register SetWordType *b = &(bitmask[0]);
		do {
			if (t & *b) ++degree;
		} while (++b < &(bitmask[WORDSIZE]));
		p++;
	}

	return(degree);
}

void
#ifdef __STDC__
zzedecode(SetWordType *a)
#else
zzedecode(a)
SetWordType *a;
#endif
{
	register SetWordType *p = a;
	register SetWordType *endp = &(p[zzSET_SIZE]);
	register SetWordType e = 0;

	if ( zzset_deg(a)>1 ) fprintf(stderr, " {");
	do {
		register SetWordType t = *p;
		register SetWordType *b = &(bitmask[0]);
		do {
			if ( t & *b ) fprintf(stderr, " %s", zztokens[e]);
			e++;
		} while (++b < &(bitmask[sizeof(SetWordType)*8]));
	} while (++p < endp);
	if ( zzset_deg(a)>1 ) fprintf(stderr, " }");
}

#ifndef ZZUSE_MACROS

#ifdef DEMAND_LOOK

#ifdef LL_K
int
#ifdef __STDC__
_zzmatch(int _t, char **zzBadText, char **zzMissText,
		unsigned *zzMissTok, unsigned *zzBadTok,
		SetWordType **zzMissSet)
#else
_zzmatch(_t, zzBadText, zzMissText, zzMissTok, zzBadTok, zzMissSet)
int _t;
char **zzBadText;
char **zzMissText;
unsigned *zzMissTok, *zzBadTok;
SetWordType **zzMissSet;
#endif
{
	if ( zzdirty==LL_K ) {
		zzCONSUME;
	}
	if ( LA(1)!=_t ) {		
		*zzBadText = *zzMissText=LATEXT(1);	
		*zzMissTok= _t; *zzBadTok=LA(1); 
		*zzMissSet=NULL;				
		return 0;
	}
	zzMakeAttr						
	zzdirty++;						
	zzlabase++;						
	return 1;
}

#else

int
#ifdef __STDC__
_zzmatch(int _t, char **zzBadText, char **zzMissText,
		 unsigned *zzMissTok, unsigned *zzBadTok, SetWordType **zzMissSet)
#else
_zzmatch(_t, zzBadText, zzMissText, zzMissTok, zzBadTok, zzMissSet)
int _t;
char **zzBadText;
char **zzMissText;
unsigned *zzMissTok, *zzBadTok;
SetWordType **zzMissSet;
#endif
{								
	if ( zzdirty ) {zzCONSUME;}		
	if ( LA(1)!=_t ) {
		*zzBadText = *zzMissText=LATEXT(1);	
		*zzMissTok= _t; *zzBadTok=LA(1); 
		*zzMissSet=NULL;				
		return 0;
	}								
	zzdirty = 1;					
	zzMakeAttr						
	return 1;
}
#endif /*LL_K*/

#else

int
#ifdef __STDC__
_zzmatch(int _t, char **zzBadText, char **zzMissText,
		unsigned *zzMissTok, unsigned *zzBadTok,
		SetWordType **zzMissSet)
#else
_zzmatch(_t, zzBadText, zzMissText, zzMissTok, zzBadTok, zzMissSet)
int _t;
char **zzBadText;
char **zzMissText;
unsigned *zzMissTok, *zzBadTok;
SetWordType **zzMissSet;
#endif
{
	if ( LA(1)!=_t ) {				
		*zzBadText = *zzMissText=LATEXT(1);	
		*zzMissTok= _t; *zzBadTok=LA(1); 
		*zzMissSet=NULL;				
		return 0;
	}
	zzMakeAttr
	return 1;
}
#endif /*DEMAND_LOOK*/

#ifdef ZZINF_LOOK
void
#ifdef __STDC__
_inf_zzgettok(void)
#else
_inf_zzgettok()
#endif
{
	if ( zzinf_labase >= zzinf_last )					
		{NLA = DEFAULT_EOF_TOKEN; strcpy(NLATEXT, "");}	
	else {											
		NLA = zzinf_tokens[zzinf_labase];				
		strcpy(NLATEXT, zzinf_text[zzinf_labase]);		
		zzinf_labase++; 								
	}												
}
#endif

#endif /* !def ZZUSE_MACROS */

#ifdef ZZINF_LOOK
/* allocate default size text,token arrays;
 * then, read all of the input reallocing the arrays as needed.
 * Once the number of total tokens is known, the LATEXT(i) array (zzinf_text)
 * is allocated and it's pointers are set to the tokens in zzinf_text_buffer.
 */
void
#ifdef __STDC__
zzfill_inf_look(void)
#else
zzfill_inf_look()
#endif
{
	int tok;
	int zzinf_token_buffer_size = ZZINF_DEF_TOKEN_BUFFER_SIZE;
	int zzinf_text_buffer_size = ZZINF_DEF_TEXT_BUFFER_SIZE;
	int zzinf_text_buffer_index = 0;
	int zzinf_lap = 0;

	/* allocate text/token buffers */
	zzinf_text_buffer = (char *) malloc(zzinf_text_buffer_size);
	if ( zzinf_text_buffer == NULL )
	{
		fprintf(stderr, "cannot allocate lookahead text buffer (%d bytes)\n", 
		zzinf_text_buffer_size);
		exit(-1);									    
	}
	zzinf_tokens = (int *) calloc(zzinf_token_buffer_size,sizeof(int)); 
	if ( zzinf_tokens == NULL )
	{
		fprintf(stderr,	"cannot allocate token buffer (%d tokens)\n", 
				zzinf_token_buffer_size);
		exit(-1);									    
	}

	/* get tokens, copying text to text buffer */
	zzinf_text_buffer_index = 0;
	do {
		zzgettok();
		while ( zzinf_lap>=zzinf_token_buffer_size )
		{
			zzinf_token_buffer_size += ZZINF_BUFFER_TOKEN_CHUNK_SIZE; 
			zzinf_tokens = (int *) realloc(zzinf_tokens,
												 zzinf_token_buffer_size*sizeof(int));
			if ( zzinf_tokens == NULL )
			{
				fprintf(stderr, "cannot allocate lookahead token buffer (%d tokens)\n", 
						zzinf_token_buffer_size);
				exit(-1);
			}
		}
		while ( (zzinf_text_buffer_index+strlen(NLATEXT)+1) >= zzinf_text_buffer_size )
		{
			zzinf_text_buffer_size += ZZINF_BUFFER_TEXT_CHUNK_SIZE; 
			zzinf_text_buffer = (char *) realloc(zzinf_text_buffer,
												 zzinf_text_buffer_size);
			if ( zzinf_text_buffer == NULL )
			{
				fprintf(stderr,	"cannot allocate lookahead text buffer (%d bytes)\n", 
						zzinf_text_buffer_size);
				exit(-1);
			}
		}
		/* record token and text of input symbol */
		tok = zzinf_tokens[zzinf_lap] = NLA;
		strcpy(&zzinf_text_buffer[zzinf_text_buffer_index], NLATEXT);
		zzinf_text_buffer_index += strlen(NLATEXT)+1;		    
		zzinf_lap++;
	} while (tok!=DEFAULT_EOF_TOKEN);
	zzinf_labase = 0;
	zzinf_last = zzinf_lap-1;

	/* allocate ptrs to text of ith token */
	zzinf_text = (char **) calloc(zzinf_last+1,sizeof(char *));
	if ( zzinf_text == NULL )
	{
		fprintf(stderr,	"cannot allocate lookahead text buffer (%d)\n", 
				zzinf_text_buffer_size); 
		exit(-1);										    
	}													    
	zzinf_text_buffer_index = 0;
	zzinf_lap = 0;
	/* set ptrs so that zzinf_text[i] is the text of the ith token found on input */
	while (zzinf_lap<=zzinf_last)
	{
	    zzinf_text[zzinf_lap++] = &zzinf_text_buffer[zzinf_text_buffer_index]; 
		zzinf_text_buffer_index += strlen(&zzinf_text_buffer[zzinf_text_buffer_index])+1; 
	}
}
#endif

#endif /* ERR_H */
