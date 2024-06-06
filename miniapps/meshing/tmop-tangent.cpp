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
//
//    ------------------------------------------------------------------
//      Fitting of Selected Mesh Nodes to Specified Physical Positions
//    ------------------------------------------------------------------
//
// This example fits a selected set of the mesh nodes to given physical
// positions while maintaining a valid mesh with good quality.
//
// Sample runs:
//   mpirun -np 4 tmop-tangent -rs 1 -m square01.mesh -o 1
//   mpirun -np 4 tmop-tangent -rs 1 -m rectangle01.mesh -o 1

#include "../common/mfem-common.hpp"
#include "tmop-tangent.hpp"

using namespace mfem;
using namespace std;

char vishost[] = "localhost";
int  visport   = 19916;
int  wsize     = 350;

int main (int argc, char *argv[])
{
   // Initialize MPI.
   Mpi::Init();
   int myid = Mpi::WorldRank();
   const char *mesh_file = "square01.mesh";
   int rs_levels     = 1;
   int mesh_poly_deg = 2;
   int quad_order    = 5;
   bool glvis        = true;
   // Parse command-line options.
   OptionsParser args(argc, argv);
   args.AddOption(&mesh_file, "-m", "--mesh",
		 "Mesh file to use.");
   args.AddOption(&rs_levels, "-rs", "--refine-serial",
		 "Number of times to refine the mesh uniformly in serial.");
   args.AddOption(&mesh_poly_deg, "-o", "--order",
       "Polynomial degree of mesh finite element space.");
   args.AddOption(&quad_order, "-qo", "--quad_order",
		 "Order of the quadrature rule.");
   args.AddOption(&glvis, "-vis", "--visualization", "-no-vis",
		 "--no-visualization",
		 "Enable or disable GLVis visualization.");
   args.Parse();
   if (!args.Good())
   {
      if (myid == 0) { args.PrintUsage(cout); }
      return 1;
   }
   if (myid == 0) { args.PrintOptions(cout); }

   // Read and refine the mesh.
   Mesh *mesh = new Mesh(mesh_file, 1, 1, false);
   for (int lev = 0; lev < rs_levels; lev++) { mesh->UniformRefinement(); }
   ParMesh pmesh(MPI_COMM_WORLD, *mesh);
   delete mesh;
   const int dim = pmesh.Dimension();

   // Setup mesh curvature and GridFunction that stores the coordinates.
   H1_FECollection fec_mesh(mesh_poly_deg, dim);
   ParFiniteElementSpace pfes_mesh(&pmesh, &fec_mesh, dim);
   pmesh.SetNodalFESpace(&pfes_mesh);
   ParGridFunction coord(&pfes_mesh);
   pmesh.SetNodalGridFunction(&coord);
   ParGridFunction x0(coord);

   // Move the mesh nodes to have non-trivial problem.
   // Top boundary is (x = t, y = 1 + 0.5 t).
   const int N = coord.Size() / 2;
   for (int i = 0; i < N; i++)
   {
      double x = coord(i);
      double y = coord(i+N);

      // Displace all x and y, so that the spacing is non-uniform.
      coord(i)     = x + x*(1-x)*0.8;
      coord(i + N) = y + y*(1-y)*0.8;

      x = coord(i);
      y = coord(i+N);

      // a adds deformation inside.
      // d pulls the top-right corner out.
      double a = 0.2, d = 0.5;
      coord(i)     = x + a * sin(M_PI * x) * sin(M_PI * y);
      coord(i + N) = y + a * sin(M_PI * x) * sin(M_PI * y) + d * x * y;
   }

   // Compute the minimum det(J) of the starting mesh.
   double min_detJ = infinity();
   const int NE = pmesh.GetNE();
   for (int e = 0; e < NE; e++)
   {
      const IntegrationRule &ir =
          IntRules.Get(pfes_mesh.GetFE(0)->GetGeomType(), quad_order);
      ElementTransformation *transf = pmesh.GetElementTransformation(e);
      for (int j = 0; j < ir.GetNPoints(); j++)
      {
         transf->SetIntPoint(&ir.IntPoint(j));
         min_detJ = min(min_detJ, transf->Jacobian().Det());
      }
   }
   MPI_Allreduce(MPI_IN_PLACE, &min_detJ, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
   if (myid == 0)
   { cout << "Minimum det(J) of the original mesh is " << min_detJ << endl; }
   MFEM_VERIFY(min_detJ > 0.0, "Inverted initial meshes are not supported.");

   // Mark which nodes to move tangentially (attribute 1).
   Array<bool> fit_marker(pfes_mesh.GetNDofs());
   ParGridFunction fit_marker_vis_gf(&pfes_mesh);
   Array<int> vdofs;
   fit_marker = false;
   fit_marker_vis_gf = 0.0;
   for (int e = 0; e < pmesh.GetNBE(); e++)
   {
      const int attr = pmesh.GetBdrElement(e)->GetAttribute();
      if (attr != 1) { continue; }

      const int nd = pfes_mesh.GetBE(e)->GetDof();
      pfes_mesh.GetBdrElementVDofs(e, vdofs);
      for (int j = 0; j < nd; j++)
      {
         fit_marker[vdofs[j]] = true;
         fit_marker_vis_gf(vdofs[j]) = 1.0;
      }
   }

   // Visualize the selected nodes and their target positions.
   if (glvis)
   {
      socketstream vis1, vis2;
      common::VisualizeField(vis1, "localhost", 19916, fit_marker_vis_gf,
			     "Target positions (DOFS with value 1)",
			     0, 0, 400, 400, (dim == 2) ? "Rjm" : "");
      common::VisualizeMesh(vis2, "localhost", 19916, pmesh, "Initial mesh",
                            400, 0, 400, 400, "me");
   }

   Array<int> ess_vdofs;
   for (int i = 0; i < pmesh.GetNBE(); i++)
   {
      const int nd = pfes_mesh.GetBE(i)->GetDof();
      const int attr = pmesh.GetBdrElement(i)->GetAttribute();
      pfes_mesh.GetBdrElementVDofs(i, vdofs);
      if (attr == 1)
      {
         // Eliminate y components.
         for (int j = 0; j < nd; j++)
         {
            ess_vdofs.Append(vdofs[j+nd]);
         }
      }
      else if (attr == 3)
      {
         // Fix y components.
         for (int j = 0; j < nd; j++)
         { ess_vdofs.Append(vdofs[j+nd]); }
      }
      else if (attr == 2 || attr == 4)
      {
         // Fix x components.
         for (int j = 0; j < nd; j++)
         { ess_vdofs.Append(vdofs[j]); }
      }
   }

   Line_Top line(fit_marker, pfes_mesh, coord, pmesh);
   line.ConvertPhysCoordToParam(coord);

   // TMOP setup.
   TMOP_QualityMetric *metric;
   if (dim == 2) { metric = new TMOP_Metric_002; }
   else          { metric = new TMOP_Metric_302; }
   TargetConstructor target(TargetConstructor::IDEAL_SHAPE_UNIT_SIZE,
                            pfes_mesh.GetComm());
   auto integ = new ParametrizedTMOP_Integrator(metric, &target, nullptr);
   integ->EnableTangentialMovement(fit_marker, line);

   // Linear solver.
   MINRESSolver minres(pfes_mesh.GetComm());
   minres.SetMaxIter(100);
   minres.SetRelTol(1e-8);
   minres.SetAbsTol(0.0);

   // Nonlinear solver.
   ParNonlinearForm a(&pfes_mesh);
   a.SetEssentialVDofs(ess_vdofs);
   a.AddDomainIntegrator(integ);
   const IntegrationRule &ir =
    IntRules.Get(pfes_mesh.GetFE(0)->GetGeomType(), quad_order);
   TMOPNewtonSolver solver(pfes_mesh.GetComm(), ir, 0);
   solver.SetOperator(a);
   solver.SetPreconditioner(minres);
   solver.SetPrintLevel(1);
   solver.SetMaxIter(10000);
   solver.SetRelTol(1e-6);
   solver.SetAbsTol(0.0);

   // Solve.
   Vector b(0);
   coord.SetTrueVector();
   solver.Mult(b, coord.GetTrueVector());
   coord.SetFromTrueVector();
   line.ConvertParamCoordToPhys(coord);
   if(glvis)
   {
      socketstream vis2;
      common::VisualizeMesh(vis2, "localhost", 19916, pmesh, "Final mesh",
                            800, 0, 400, 400, "me");
   }

   delete metric;
   return 0;
}
