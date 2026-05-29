// solver.h
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "cnf.h"
#include "bcp.h"
#include "trail.h"

#ifndef RVV_SAT_SOLVER_H
#define RVV_SAT_SOLVER_H


int32_t pick_unassigned(const Formula *f, const Assignment *a);

enum sat_result { SAT, UNSAT };
enum sat_result solve(const Formula *f, Assignment *a);

#endif //RVV_SAT_SOLVER_H
