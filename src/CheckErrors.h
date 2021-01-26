#include <u.h>
#include <libc.h>
#include <bio.h>

#include "scsireq.h"
#include<config>


#define CheckErrors.cpp
#include gtk
typedef struct {

	error_type ;
	error_code;
	error_key;



};

void set_error_type (char t);
char get_error_type (void );

void set_error_code (char c);
char get_error_code(void);

void set_error_key ( int k);
int get_error_key ( void);



