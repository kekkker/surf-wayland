#ifndef TYPES_H
#define TYPES_H

/* Argument structure for function parameters */
typedef union {
	int i;
	float f;
	const void *v;
} Arg;

#endif