//                                MFEM Example 5
//
// Compile with: make ex5
//
// Sample runs:  ex5 -m ../data/square-disc.mesh
//               ex5 -m ../data/star.mesh
//               ex5 -m ../data/star.mesh -pa
//               ex5 -m ../data/beam-tet.mesh
//               ex5 -m ../data/beam-hex.mesh
//               ex5 -m ../data/beam-hex.mesh -pa
//               ex5 -m ../data/escher.mesh
//               ex5 -m ../data/fichera.mesh
//
// Device sample runs:
//               ex5 -m ../data/star.mesh -pa -d cuda
//               ex5 -m ../data/star.mesh -pa -d raja-cuda
//               ex5 -m ../data/star.mesh -pa -d raja-omp
//               ex5 -m ../data/beam-hex.mesh -pa -d cuda
//
// Description:  This example code solves a simple 2D/3D mixed Darcy problem
//               corresponding to the saddle point system
//
//                                 k*q + grad t = f
//                                 - div q      = g
//
//               with natural boundary condition -t = <given temperature>.
//               Here, we use a given exact solution (q,t) and compute the
//               corresponding r.h.s. (f,g).  We discretize with Raviart-Thomas
//               finite elements (heat flux q) and piecewise discontinuous
//               polynomials (temperature t).
//
//               The example demonstrates the use of the BlockOperator class, as
//               well as the collective saving of several grid functions in
//               VisIt (visit.llnl.gov) and ParaView (paraview.org) formats.
//
//               We recommend viewing examples 1-4 before viewing this example.

#include "mfem.hpp"
#include "darcyform.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>

using namespace std;
using namespace mfem;

// Define the analytical solution and forcing terms / boundary conditions
typedef std::function<double(const Vector &, double t)> TDFunc;
typedef std::function<void(const Vector &, Vector &)> VecFunc;
typedef std::function<void(const Vector &, DenseMatrix &)> MatFunc;

TDFunc GetTFun(double t_0, double a, const MatFunc &kFun);
VecFunc GetQFun(double t_0, double a, const MatFunc &kFun);
MatFunc GetKFun(double k, double ks, double ka);

int main(int argc, char *argv[])
{
   StopWatch chrono;

   // 1. Parse command-line options.
   const char *mesh_file = "";
   int nx = 0;
   int ny = 0;
   int order = 1;
   double ks = 1.;
   double ka = 0.;
   bool hybridization = false;
   bool pa = false;
   const char *device_config = "cpu";
   bool visualization = 1;

   OptionsParser args(argc, argv);
   args.AddOption(&mesh_file, "-m", "--mesh",
                  "Mesh file to use.");
   args.AddOption(&nx, "-nx", "--ncells-x",
                  "Number of cells in x.");
   args.AddOption(&ny, "-ny", "--ncells-y",
                  "Number of cells in y.");
   args.AddOption(&order, "-o", "--order",
                  "Finite element order (polynomial degree).");
   args.AddOption(&ks, "-ks", "--kappa_sym",
                  "Symmetric anisotropy of the heat conductivity tensor");
   args.AddOption(&ka, "-ka", "--kappa_anti",
                  "Antisymmetric anisotropy of the heat conductivity tensor");
   args.AddOption(&hybridization, "-hb", "--hybridization", "-no-hb",
                  "--no-hybridization", "Enable hybridization.");
   args.AddOption(&pa, "-pa", "--partial-assembly", "-no-pa",
                  "--no-partial-assembly", "Enable Partial Assembly.");
   args.AddOption(&device_config, "-d", "--device",
                  "Device configuration string, see Device::Configure().");
   args.AddOption(&visualization, "-vis", "--visualization", "-no-vis",
                  "--no-visualization",
                  "Enable or disable GLVis visualization.");
   args.Parse();
   if (!args.Good())
   {
      args.PrintUsage(cout);
      return 1;
   }
   args.PrintOptions(cout);

   // 2. Enable hardware devices such as GPUs, and programming models such as
   //    CUDA, OCCA, RAJA and OpenMP based on command line options.
   Device device(device_config);
   device.Print();

   // 3. Read the mesh from the given mesh file. We can handle triangular,
   //    quadrilateral, tetrahedral, hexahedral, surface and volume meshes with
   //    the same code.
   if (ny <= 0)
   {
      ny = nx;
   }

   Mesh *mesh = NULL;
   if (strlen(mesh_file) > 0)
   {
      mesh = new Mesh(mesh_file, 1, 1);
   }
   else
   {
      mesh = new Mesh(Mesh::MakeCartesian2D(nx, ny, Element::QUADRILATERAL));
   }

   int dim = mesh->Dimension();

   // 4. Refine the mesh to increase the resolution. In this example we do
   //    'ref_levels' of uniform refinement. We choose 'ref_levels' to be the
   //    largest number that gives a final mesh with no more than 10,000
   //    elements.
   if (strlen(mesh_file) > 0)
   {
      int ref_levels =
         (int)floor(log(10000./mesh->GetNE())/log(2.)/dim);
      for (int l = 0; l < ref_levels; l++)
      {
         mesh->UniformRefinement();
      }
   }

   // 5. Define a finite element space on the mesh. Here we use the
   //    Raviart-Thomas finite elements of the specified order.
   FiniteElementCollection *hdiv_coll(new RT_FECollection(order, dim));
   FiniteElementCollection *l2_coll(new L2_FECollection(order, dim));

   FiniteElementSpace *V_space = new FiniteElementSpace(mesh, hdiv_coll);
   FiniteElementSpace *W_space = new FiniteElementSpace(mesh, l2_coll);

   DarcyForm *darcy = new DarcyForm(V_space, W_space);

   // 6. Define the BlockStructure of the problem, i.e. define the array of
   //    offsets for each variable. The last component of the Array is the sum
   //    of the dimensions of each block.
   const Array<int> &block_offsets = darcy->GetOffsets();

   std::cout << "***********************************************************\n";
   std::cout << "dim(R) = " << block_offsets[1] - block_offsets[0] << "\n";
   std::cout << "dim(W) = " << block_offsets[2] - block_offsets[1] << "\n";
   std::cout << "dim(R+W) = " << block_offsets.Last() << "\n";
   std::cout << "***********************************************************\n";

   // 7. Define the coefficients, analytical solution, and rhs of the PDE.
   const double t_0 = 1.; //base temperature
   const double a = 1.; //heat capacity
   const double k = 1.; //base heat conductivity

   auto kFun = GetKFun(k, ks, ka);
   MatrixFunctionCoefficient kcoeff(dim, kFun);
   InverseMatrixCoefficient ikcoeff(kcoeff);

   auto tFun = GetTFun(t_0, a, kFun);
   FunctionCoefficient tcoeff(tFun);
   SumCoefficient gcoeff(0, tcoeff, 1.,
                         -1.);//<-- due to symmetrization, the sign is opposite

   auto qFun = GetQFun(t_0, a, kFun);
   VectorFunctionCoefficient qcoeff(dim, qFun);

   // 8. Allocate memory (x, rhs) for the analytical solution and the right hand
   //    side.  Define the GridFunction q,t for the finite element solution and
   //    linear forms fform and gform for the right hand side.  The data
   //    allocated by x and rhs are passed as a reference to the grid functions
   //    (q,t) and the linear forms (fform, gform).
   MemoryType mt = device.GetMemoryType();
   BlockVector x(block_offsets, mt), rhs(block_offsets, mt);

   LinearForm *fform(new LinearForm);
   fform->Update(V_space, rhs.GetBlock(0), 0);
   //fform->AddDomainIntegrator(new VectorFEDomainLFIntegrator(fcoeff));
   //fform->AddBoundaryIntegrator(new VectorFEBoundaryFluxLFIntegrator(fnatcoeff));
   fform->Assemble();
   fform->SyncAliasMemory(rhs);

   LinearForm *gform(new LinearForm);
   gform->Update(W_space, rhs.GetBlock(1), 0);
   gform->AddDomainIntegrator(new DomainLFIntegrator(gcoeff));
   /*Vector v ({-1., 0.});
   VectorConstantCoefficient vc(v);
   ConstantCoefficient one;
   gform->AddBdrFaceIntegrator(new BoundaryFlowIntegrator(one, vc, 1.));*/
   gform->Assemble();
   gform->SyncAliasMemory(rhs);

   // 9. Assemble the finite element matrices for the Darcy operator
   //
   //                            D = [ M  B^T ]
   //                                [ B   0  ]
   //     where:
   //
   //     M = \int_\Omega k u_h \cdot v_h d\Omega   q_h, v_h \in V_h
   //     B   = -\int_\Omega \div u_h q_h d\Omega   q_h \in V_h, w_h \in W_h
   //BilinearForm *Mq(new BilinearForm(V_space));
   //MixedBilinearForm *B(new MixedBilinearForm(V_space, W_space));
   BilinearForm *Mq = darcy->GetFluxMassForm();
   MixedBilinearForm *B = darcy->GetFluxDivForm();

   //if (pa) { Mq->SetAssemblyLevel(AssemblyLevel::PARTIAL); }
   Mq->AddDomainIntegrator(new VectorFEMassIntegrator(ikcoeff));
   //Mq->Assemble();
   //if (!pa) { Mq->Finalize(); }

   //if (pa) { B->SetAssemblyLevel(AssemblyLevel::PARTIAL); }
   B->AddDomainIntegrator(new VectorFEDivergenceIntegrator);
   //B->Assemble();
   //if (!pa) { B->Finalize(); }

   //set hybridization / assembly level

   Array<int> ess_flux_tdofs_list;
   /*Array<int> bdr_is_ess(mesh->bdr_attributes.Max());
   bdr_is_ess = 0;
   bdr_is_ess[3] = -1;
   V_space->GetEssentialTrueDofs(bdr_is_ess, ess_flux_tdofs_list);*/

   FiniteElementCollection *trace_coll = NULL;
   FiniteElementSpace *trace_space = NULL;

   chrono.Clear();
   chrono.Start();

   if (hybridization)
   {
      trace_coll = new RT_Trace_FECollection(order, dim, 0);
      //trace_coll = new DG_Interface_FECollection(order, dim, 0);
      trace_space = new FiniteElementSpace(mesh, trace_coll);
      darcy->EnableHybridization(trace_space,
                                 new NormalTraceJumpIntegrator(),
                                 ess_flux_tdofs_list);
   }

   if (pa) { darcy->SetAssemblyLevel(AssemblyLevel::PARTIAL); }

   darcy->Assemble();
   //if (!pa) { darcy->Finalize(); }

   //BlockOperator darcyOp(block_offsets);

   //TransposeOperator *Bt = NULL;

   /*if (pa)
   {
      Bt = new TransposeOperator(B);

      darcyOp.SetBlock(0,0, Mq);
      darcyOp.SetBlock(0,1, Bt, -1.0);
      darcyOp.SetBlock(1,0, B, -1.0);
   }
   else
   {
      SparseMatrix &M(Mq->SpMat());
      SparseMatrix &B(B->SpMat());
      B *= -1.;
      Bt = new TransposeOperator(&B);

      darcyOp.SetBlock(0,0, &M);
      darcyOp.SetBlock(0,1, Bt);
      darcyOp.SetBlock(1,0, &B);
   }*/

   OperatorHandle pDarcyOp;
   Vector X, RHS;
   //x = 1./ny;
   //darcy->FormSystemMatrix(ess_flux_tdofs_list, pDarcyOp);
   darcy->FormLinearSystem(ess_flux_tdofs_list, x, rhs,
                           pDarcyOp, X, RHS);

   chrono.Stop();
   std::cout << "Assembly took " << chrono.RealTime() << "s.\n";


   int maxIter(1000);
   double rtol(1.e-6);
   double atol(1.e-10);

   if (hybridization)
   {
      // 10. Construct the preconditioner
      GSSmoother prec(*pDarcyOp.As<SparseMatrix>());

      //ofstream ofs("H.txt");
      //pDarcyOp.As<SparseMatrix>()->PrintMM(ofs);
      //ofs.close();

      // 11. Solve the linear system with GMRES.
      //     Check the norm of the unpreconditioned residual.
      chrono.Clear();
      chrono.Start();
      GMRESSolver solver;
      solver.SetAbsTol(atol);
      solver.SetRelTol(rtol);
      solver.SetMaxIter(maxIter);
      solver.SetOperator(*pDarcyOp);
      solver.SetPreconditioner(prec);
      solver.SetPrintLevel(1);

      solver.Mult(RHS, X);
      darcy->RecoverFEMSolution(X, rhs, x);

      chrono.Stop();

      if (solver.GetConverged())
      {
         std::cout << "GMRES converged in " << solver.GetNumIterations()
                   << " iterations with a residual norm of "
                   << solver.GetFinalNorm() << ".\n";
      }
      else
      {
         std::cout << "GMRES did not converge in " << solver.GetNumIterations()
                   << " iterations. Residual norm is " << solver.GetFinalNorm()
                   << ".\n";
      }
      std::cout << "GMRES solver took " << chrono.RealTime() << "s.\n";
   }
   else
   {
      // 10. Construct the operators for preconditioner
      //
      //                 P = [ diag(M)         0         ]
      //                     [  0       B diag(M)^-1 B^T ]
      //
      //     Here we use Symmetric Gauss-Seidel to approximate the inverse of the
      //     temperature Schur Complement
      SparseMatrix *MinvBt = NULL;
      Vector Md(Mq->Height());

      BlockDiagonalPreconditioner darcyPrec(block_offsets);
      Solver *invM, *invS;
      SparseMatrix *S = NULL;

      if (pa)
      {
         Mq->AssembleDiagonal(Md);
         auto Md_host = Md.HostRead();
         Vector invMd(Mq->Height());
         for (int i=0; i<Mq->Height(); ++i)
         {
            invMd(i) = 1.0 / Md_host[i];
         }

         Vector BMBt_diag(B->Height());
         B->AssembleDiagonal_ADAt(invMd, BMBt_diag);

         Array<int> ess_tdof_list;  // empty

         invM = new OperatorJacobiSmoother(Md, ess_tdof_list);
         invS = new OperatorJacobiSmoother(BMBt_diag, ess_tdof_list);
      }
      else
      {
         SparseMatrix &Mm(Mq->SpMat());
         Mm.GetDiag(Md);
         Md.HostReadWrite();

         SparseMatrix &Bm(B->SpMat());
         MinvBt = Transpose(Bm);

         for (int i = 0; i < Md.Size(); i++)
         {
            MinvBt->ScaleRow(i, 1./Md(i));
         }

         S = Mult(Bm, *MinvBt);

         invM = new DSmoother(Mm);

#ifndef MFEM_USE_SUITESPARSE
         invS = new GSSmoother(*S);
#else
         invS = new UMFPackSolver(*S);
#endif
      }

      invM->iterative_mode = false;
      invS->iterative_mode = false;

      darcyPrec.SetDiagonalBlock(0, invM);
      darcyPrec.SetDiagonalBlock(1, invS);

      // 11. Solve the linear system with MINRES.
      //     Check the norm of the unpreconditioned residual.

      chrono.Clear();
      chrono.Start();
      MINRESSolver solver;
      solver.SetAbsTol(atol);
      solver.SetRelTol(rtol);
      solver.SetMaxIter(maxIter);
      solver.SetOperator(*pDarcyOp);
      solver.SetPreconditioner(darcyPrec);
      solver.SetPrintLevel(1);

      solver.Mult(RHS, X);
      darcy->RecoverFEMSolution(X, rhs, x);

      if (device.IsEnabled()) { x.HostRead(); }
      chrono.Stop();

      if (solver.GetConverged())
      {
         std::cout << "MINRES converged in " << solver.GetNumIterations()
                   << " iterations with a residual norm of "
                   << solver.GetFinalNorm() << ".\n";
      }
      else
      {
         std::cout << "MINRES did not converge in " << solver.GetNumIterations()
                   << " iterations. Residual norm is " << solver.GetFinalNorm()
                   << ".\n";
      }
      std::cout << "MINRES solver took " << chrono.RealTime() << "s.\n";

      delete invM;
      delete invS;
      delete S;
      //delete Bt;
      delete MinvBt;
   }

   // 12. Create the grid functions q and t. Compute the L2 error norms.
   GridFunction q, t;
   q.MakeRef(V_space, x.GetBlock(0), 0);
   t.MakeRef(W_space, x.GetBlock(1), 0);

   int order_quad = max(2, 2*order+1);
   const IntegrationRule *irs[Geometry::NumGeom];
   for (int i=0; i < Geometry::NumGeom; ++i)
   {
      irs[i] = &(IntRules.Get(i, order_quad));
   }

   double err_q  = q.ComputeL2Error(qcoeff, irs);
   double norm_q = ComputeLpNorm(2., qcoeff, *mesh, irs);
   double err_t  = t.ComputeL2Error(tcoeff, irs);
   double norm_t = ComputeLpNorm(2., tcoeff, *mesh, irs);

   std::cout << "|| q_h - q_ex || / || q_ex || = " << err_q / norm_q << "\n";
   std::cout << "|| t_h - t_ex || / || t_ex || = " << err_t / norm_t << "\n";

   // 13. Save the mesh and the solution. This output can be viewed later using
   //     GLVis: "glvis -m ex5.mesh -g sol_q.gf" or "glvis -m ex5.mesh -g
   //     sol_t.gf".
   {
      ofstream mesh_ofs("ex5.mesh");
      mesh_ofs.precision(8);
      mesh->Print(mesh_ofs);

      ofstream q_ofs("sol_q.gf");
      q_ofs.precision(8);
      q.Save(q_ofs);

      ofstream t_ofs("sol_t.gf");
      t_ofs.precision(8);
      t.Save(t_ofs);
   }

   // 14. Save data in the VisIt format
   VisItDataCollection visit_dc("Example5", mesh);
   visit_dc.RegisterField("heat flux", &q);
   visit_dc.RegisterField("temperature", &t);
   visit_dc.Save();

   // 15. Save data in the ParaView format
   ParaViewDataCollection paraview_dc("Example5", mesh);
   paraview_dc.SetPrefixPath("ParaView");
   paraview_dc.SetLevelsOfDetail(order);
   paraview_dc.SetCycle(0);
   paraview_dc.SetDataFormat(VTKFormat::BINARY);
   paraview_dc.SetHighOrderOutput(true);
   paraview_dc.SetTime(0.0); // set the time
   paraview_dc.RegisterField("heat flux",&q);
   paraview_dc.RegisterField("temperature",&t);
   paraview_dc.Save();

   // 16. Send the solution by socket to a GLVis server.
   if (visualization)
   {
      char vishost[] = "localhost";
      int  visport   = 19916;
      socketstream q_sock(vishost, visport);
      q_sock.precision(8);
      q_sock << "solution\n" << *mesh << q << "window_title 'Heat flux'" << endl;
      q_sock << "keys Rljvvvvvmmc" << endl;
      socketstream t_sock(vishost, visport);
      t_sock.precision(8);
      t_sock << "solution\n" << *mesh << t << "window_title 'Temperature'" << endl;
      t_sock << "keys Rljmmc" << endl;
   }

   // 17. Free the used memory.
   delete fform;
   delete gform;
   //delete Mq;
   //delete B;
   delete darcy;
   delete W_space;
   delete V_space;
   delete trace_space;
   delete l2_coll;
   delete hdiv_coll;
   delete trace_coll;
   delete mesh;

   return 0;
}

TDFunc GetTFun(double t_0, double a, const MatFunc &kFun)
{
   return [=](const Vector &x, double t) -> double
   {
      const int ndim = x.Size();
      double t0 = t_0 * sin(M_PI*x(0)) * sin(M_PI*x(1));
      if (ndim > 2)
      {
         t0 *= sin(M_PI*x(2));
      }

      Vector ddT((ndim<=2)?(2):(4));
      ddT(0) = -t_0 * M_PI*M_PI * sin(M_PI*x(0)) * sin(M_PI*x(1));//xx,yy
      ddT(1) = +t_0 * M_PI*M_PI * cos(M_PI*x(0)) * cos(M_PI*x(1));//xy
      if (ndim > 2)
      {
         ddT(0) *= sin(M_PI*x(2));//xx,yy,zz
         ddT(1) *= sin(M_PI*x(2));//xy
         ddT(2) = +t_0 * M_PI*M_PI * cos(M_PI*x(0)) * sin(M_PI*x(1)) * cos(M_PI*x(
                                                                              2));//xz
         ddT(3) = +t_0 * M_PI*M_PI * sin(M_PI*x(0)) * cos(M_PI*x(1)) * cos(M_PI*x(
                                                                              2));//yz

      }

      DenseMatrix kappa;
      kFun(x, kappa);

      double div = -(kappa(0,0) + kappa(1,1)) * ddT(0) - (kappa(0,1) + kappa(1,0)) * ddT(1);
      if (ndim > 2)
      {
         div += -kappa(2,2) * ddT(0) - (kappa(0,2) + kappa(2,0)) * ddT(2) - (kappa(1,
                                                                                   2) + kappa(2,1)) * ddT(3);
      }
      return t0 - div / a * t;
   };
}

VecFunc GetQFun(double t_0, double a, const MatFunc &kFun)
{
   return [=](const Vector &x, Vector &v)
   {
      const int vdim = x.Size();
      v.SetSize(vdim);

      Vector gT(vdim);
      gT = 0.;
      gT(0) = t_0 * M_PI * cos(M_PI*x(0)) * sin(M_PI*x(1));
      gT(1) = t_0 * M_PI * sin(M_PI*x(0)) * cos(M_PI*x(1));
      if (vdim > 2)
      {
         gT(0) *= sin(M_PI*x(2));
         gT(1) *= sin(M_PI*x(2));
         gT(2) = t_0 * M_PI * sin(M_PI*x(0)) * sin(M_PI*x(1)) * cos(M_PI*x(2));
      }

      DenseMatrix kappa;
      kFun(x, kappa);

      if (vdim <= 2)
      {
         v(0) = -kappa(0,0) * gT(0) -kappa(0,1) * gT(1);
         v(1) = -kappa(1,0) * gT(0) -kappa(1,1) * gT(1);
      }
      else
      {
         kappa.Mult(gT, v);
         v.Neg();
      }
   };
}

MatFunc GetKFun(double k, double ks, double ka)
{
   return [=](const Vector &x, DenseMatrix &kappa)
   {
      const int ndim = x.Size();
      kappa.Diag(k, ndim);
      kappa(0,0) *= ks;
      kappa(0,1) = +ka * k;
      kappa(1,0) = -ka * k;
      if (ndim > 2)
      {
         kappa(0,2) = +ka * k;
         kappa(2,0) = -ka * k;
      }
   };
}


