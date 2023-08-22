// Copyright (c) 2010-2023, Lawrence Livermore National Security, LLC. Produced
// at the Lawrence Livermore National Laboratory. All Rights reserved. See files
// LICENSE and NOTICE for details. LLNL-CODE-806117.
//
// This file is part of the MFEM library. For more information and source code
// availability visit https://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the BSD-3 license. We welcome feedback and contributions, see file
// CONTRIBUTING.md for details.

// Rosenbrock benchmark example

/**
 * xval: design variables
 * fval: objective value
 * dfdx: gradient of objective
 * gx: constraint values
 * dgdx: gradient of constraints
 * xmin: lower bound on design variables
 * xmax: upper bound on design variables
 * norm2: norm of KKT residual
 * normInf: infinity norm of KKT residual
 * xo1: old design variables
 * xo2: old design variables
 * U: upper asymptote
 * L: lower asymptote
 * init: initial asymptote
 * decrease: decrease factor
 * increase: increase factor
 * n: number of design variables
 * m: number of constraints
 * a: Rosenbrock parameter
 * b: Rosenbrock parameter
*/

#include "mtop_MMA.hpp"
#include <fstream>

//using namespace mfem;
using namespace mma;

void Rosenbrock(double* xval, double a, double b, double* fval, double* dfdx,
                double* gx, double* dgdx);

int main(int argc, char *argv[])
{
   //Rosenbrock parameters
   double aR = 1.0;
   double bR = 100.0;
   // Objective parameters
   int nVar = 2;
   int nCon = 1;
   double epsimin = 0.0000001;
   int sx = 1;
   double* xval = new double[nVar];

   double* fval = new double[1];
   double* dfdx = new double[nVar];
   double* gx = new double[nCon];
   double* dgdx = new double[nVar * nCon];
   double* xmin = new double[nVar];
   double* xmax = new double[nVar];
   // Simulation parameters
   int iter = 0;
   int maxiter = 4;
   int restart = maxiter + 1;
   double norm2 = 0.0;
   double normInf = 0.0;
   double kkttol = 0.0;
   
   //Initialize
   MMA MMAmain(nVar, nCon, iter);
   std::ofstream mma;
   mma.open("mma.dat");
   //remove("sub.dat");

   for (int i = 0; i < nVar; i++)
   {
      xval[i] = 0.0;
      xmin[i] = -2.0;
      xmax[i] = 2.0;
   }
   
   mma << xval[0] << " " << xval[1] << std::endl;
   Rosenbrock(xval, aR, bR, fval, dfdx, gx, dgdx);

   //kktnorm = kkttol + 10;
   while (iter < maxiter)
   {
      
      iter++;
      // Run MMA
      //MMAmain.mmasub(nVar, nCon, iter, xval, xmin, xmax, xo1, xo2, fval, dfdx, gx, dgdx);
      MMAmain.Update(nVar, nCon, iter, xval, xmin, xmax, fval, dfdx, gx, dgdx);
      // Compute objective and constraints
      Rosenbrock(xval, aR, bR, fval, dfdx, gx, dgdx);
      mma << xval[0] << " " << xval[1] << std::endl;
      // Compute KKT residual
      //MMAmain.kktcheck(nCon, nVar, xmma, ymma, xmin, xmax, dfdx, gx, dgdx, &kktnorm);

      if (iter % restart == 0)
      {
         MMAmain.Restart(xval, nVar, iter);
      }
   }
   //printf("Iteration %d: kktnorm = %f\n", iter, kktnorm);
   //mma.close();

   return 0;
}

// The optimizaiton problem
void Rosenbrock(double* xval, double a, double b, double* fval, double* dfdx,
                double* gx, double* dgdx)
{
   // f = b*(y-x^2)^2 + (a-x)^2
   *fval = b * (xval[1] - xval[0]*xval[0])*(xval[1] - xval[0]*xval[0]) +
           (a - xval[0])*(a - xval[0]);

   dfdx[0] = -4.0*b*xval[0]*(xval[1] - xval[0]*xval[0]) - 2.0*(a - xval[0]);
   dfdx[1] = 2.0*b*(xval[1] - xval[0]*xval[0]);

   gx[0] = (xval[0] - 1.0)*(xval[0] - 1.0) + (xval[1] - 1.0)*(xval[1] - 1.0) - 4.0;
   //gx[1] = xval[0] - 2;
   //gx[2] = xval[1] - 1;

   dgdx[0] = 2.0*(xval[0] - 1.0);
   dgdx[1] = 2.0*(xval[1] - 1.0);
   //dgdx[2] = 1.0;
   //dgdx[3] = 0.0;
   //dgdx[4] = 0.0;
   //dgdx[5] = 1.0;
}
