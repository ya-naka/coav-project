/**
 * \file main.c
 * \brief TEST PLUGIN
*/
#include <stdio.h>

#pragma instrumente function mock1
#pragma instrumente function mock2
#pragma instrumente function mock3,mock5
#pragma instrumente function (mock4,mock6)
#pragma instrumente function CANARD
#pragma instrumente function mock1
#pragma instrumente function (coincoin

void mock1() {
	printf("mock1\n");
}

void mock2() {
	printf("mock2\n");
}

void mock3() {
	printf("mock3\n");
}

void mock4() {
	printf("mock4\n");
}