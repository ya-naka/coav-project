/**
 * \file main.c
 * \brief TEST PLUGIN
*/
#include <stdio.h>

#pragma instrument function mock1

void mock1() {
	printf("mock1\n");
}